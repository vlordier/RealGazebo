// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Threading/RealGazeboEncodingThread.h"
#include "Threading/RealGazeboRTSPThread.h"
#include "Encoding/RealGazeboHardwareEncoder.h"
#include "Pipeline/RealGazeboFramePool.h"
#include "Core/RealGazeboStreamingLogger.h"

FRealGazeboEncodingThread::FRealGazeboEncodingThread(TSharedPtr<FRealGazeboFramePool> InFramePool,
                                                       TSharedPtr<FRealGazeboRTSPThread> InRTSPThread)
	: FramePool(InFramePool)
	, RTSPThread(InRTSPThread)
	, ThreadHandle(nullptr)
{
}

FRealGazeboEncodingThread::~FRealGazeboEncodingThread()
{
	Stop();

	if (ThreadHandle)
	{
		ThreadHandle->WaitForCompletion();
		delete ThreadHandle;
		ThreadHandle = nullptr;
	}
}

bool FRealGazeboEncodingThread::Init()
{
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("EncodingThread: Initialized"));
	return true;
}

uint32 FRealGazeboEncodingThread::Run()
{
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("EncodingThread: Started"));
	bIsRunning.store(true, std::memory_order_release);

	while (!bStopRequested.load(std::memory_order_acquire))
	{
		ProcessEncodingQueues();

		// Sleep briefly if no work was done
		FPlatformProcess::Sleep(IDLE_SLEEP_TIME);
	}

	bIsRunning.store(false, std::memory_order_release);
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("EncodingThread: Stopped"));
	return 0;
}

void FRealGazeboEncodingThread::Stop()
{
	bStopRequested.store(true, std::memory_order_release);
}

void FRealGazeboEncodingThread::Exit()
{
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("EncodingThread: Exiting"));
}

bool FRealGazeboEncodingThread::Start()
{
	if (ThreadHandle)
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("EncodingThread: Already started"));
		return false;
	}

	bStopRequested.store(false);
	ThreadHandle = FRunnableThread::Create(this, TEXT("RealGazeboEncodingThread"), 0,
	                                        TPri_Highest);  // Highest priority for encoding

	return ThreadHandle != nullptr;
}

bool FRealGazeboEncodingThread::RegisterEncoder(const FStreamKey& StreamKey, TSharedPtr<IRealGazeboHardwareEncoder> Encoder)
{
	if (!Encoder.IsValid())
	{
		return false;
	}

	FScopeLock Lock(&EncoderMapMutex);

	if (StreamEncoders.Contains(StreamKey))
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("EncodingThread: Encoder already registered for stream %s"),
			*StreamKey.ToString());
		return false;
	}

	TSharedPtr<FStreamEncoder> StreamEncoder = MakeShared<FStreamEncoder>();
	StreamEncoder->Encoder = Encoder;

	// Detect encoder capabilities
	StreamEncoder->bSupportsTextureEncoding.store(Encoder->SupportsTextureEncoding());

	StreamEncoders.Add(StreamKey, StreamEncoder);

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("EncodingThread: Registered encoder for stream %s (%s) - Texture Encoding: %s"),
		*StreamKey.ToString(), *Encoder->GetEncoderName(),
		StreamEncoder->bSupportsTextureEncoding.load() ? TEXT("YES") : TEXT("NO"));

	return true;
}

void FRealGazeboEncodingThread::UnregisterEncoder(const FStreamKey& StreamKey)
{
	FScopeLock Lock(&EncoderMapMutex);

	if (StreamEncoders.Remove(StreamKey) > 0)
	{
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("EncodingThread: Unregistered encoder for stream %s"),
			*StreamKey.ToString());
	}
}

bool FRealGazeboEncodingThread::EnqueueTextureFrame(const FStreamKey& StreamKey, FTexture2DRHIRef Texture,
                                                     double Timestamp, uint64 FrameNumber)
{
	TSharedPtr<FStreamEncoder> StreamEncoder = GetStreamEncoder(StreamKey);
	if (!StreamEncoder.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("EncodingThread: No encoder registered for stream %s"),
			*StreamKey.ToString());
		return false;
	}

	// Hardware encoding only - verify support
	if (!StreamEncoder->bSupportsTextureEncoding.load())
	{
		UE_LOG(LogRealGazeboStreaming, Error,
			TEXT("EncodingThread: Hardware encoder not available for stream %s. Only NVENC/AMF supported."),
			*StreamKey.ToString());
		return false;
	}

	// Create texture frame wrapper
	TSharedPtr<FTextureFrameData> TextureFrame = MakeShared<FTextureFrameData>();
	TextureFrame->Texture = Texture;
	TextureFrame->CaptureTimestamp = Timestamp;
	TextureFrame->FrameNumber = FrameNumber;
	TextureFrame->Dimensions = FIntPoint(Texture->GetSizeX(), Texture->GetSizeY());

	return StreamEncoder->TextureQueue.Enqueue(TextureFrame);
}

void FRealGazeboEncodingThread::RequestKeyFrame(const FStreamKey& StreamKey)
{
	TSharedPtr<FStreamEncoder> StreamEncoder = GetStreamEncoder(StreamKey);
	if (StreamEncoder.IsValid())
	{
		StreamEncoder->bRequestKeyFrame.store(true);
	}
}

void FRealGazeboEncodingThread::UpdateBitrate(const FStreamKey& StreamKey, int32 NewBitrateKbps)
{
	TSharedPtr<FStreamEncoder> StreamEncoder = GetStreamEncoder(StreamKey);
	if (StreamEncoder.IsValid() && StreamEncoder->Encoder.IsValid())
	{
		StreamEncoder->Encoder->UpdateBitrate(NewBitrateKbps);
	}
}

bool FRealGazeboEncodingThread::SupportsTextureEncoding(const FStreamKey& StreamKey) const
{
	TSharedPtr<FStreamEncoder> StreamEncoder = GetStreamEncoder(StreamKey);
	return StreamEncoder.IsValid() ? StreamEncoder->bSupportsTextureEncoding.load() : false;
}

void FRealGazeboEncodingThread::GetStreamStatistics(const FStreamKey& StreamKey, int32& OutQueueDepth,
                                                     int64& OutFramesEncoded, int64& OutFramesDropped,
                                                     float& OutAvgEncodeTimeMs) const
{
	TSharedPtr<FStreamEncoder> StreamEncoder = GetStreamEncoder(StreamKey);
	if (StreamEncoder.IsValid())
	{
		// Texture queue depth (hardware encoding only)
		OutQueueDepth = StreamEncoder->TextureQueue.GetDepth();
		OutFramesEncoded = StreamEncoder->FramesEncoded.load();
		OutFramesDropped = StreamEncoder->FramesDropped.load();

		// Read timing statistics with mutex protection
		{
			FScopeLock Lock(&StreamEncoder->StatsMutex);
			OutAvgEncodeTimeMs = StreamEncoder->EncodeCount > 0 ?
				static_cast<float>(StreamEncoder->TotalEncodeTime * 1000.0 / StreamEncoder->EncodeCount) : 0.0f;
		}
	}
	else
	{
		OutQueueDepth = 0;
		OutFramesEncoded = 0;
		OutFramesDropped = 0;
		OutAvgEncodeTimeMs = 0.0f;
	}
}

void FRealGazeboEncodingThread::ProcessEncodingQueues()
{
	FScopeLock Lock(&EncoderMapMutex);

	int32 TotalProcessed = 0;

	for (auto& Pair : StreamEncoders)
	{
		const FStreamKey& StreamKey = Pair.Key;
		TSharedPtr<FStreamEncoder>& StreamEncoder = Pair.Value;

		const int32 Processed = ProcessStreamEncoder(StreamKey, *StreamEncoder);
		TotalProcessed += Processed;
	}

	// If nothing was processed, thread will sleep
}

int32 FRealGazeboEncodingThread::ProcessStreamEncoder(const FStreamKey& StreamKey, FStreamEncoder& StreamEncoder)
{
	int32 ProcessedCount = 0;

	// Handle keyframe request
	if (StreamEncoder.bRequestKeyFrame.load())
	{
		if (StreamEncoder.Encoder.IsValid())
		{
			StreamEncoder.Encoder->RequestKeyFrame();
		}
		StreamEncoder.bRequestKeyFrame.store(false);
	}

	// Process up to MAX_BATCH_SIZE GPU texture frames per tick
	for (int32 I = 0; I < MAX_BATCH_SIZE; ++I)
	{
		TSharedPtr<FTextureFrameData> TextureFrame;
		if (!StreamEncoder.TextureQueue.Dequeue(TextureFrame))
		{
			break;  // Queue empty, exit batch processing
		}

		// Encode GPU texture directly (zero-copy hardware encoding)
		TSharedPtr<FEncodedFrameData> EncodedFrame;
		const bool bEncodeSuccess = EncodeTextureFrame(StreamKey, StreamEncoder, TextureFrame, EncodedFrame);

		// Texture is automatically released when TSharedPtr goes out of scope

		if (bEncodeSuccess)
		{
			ProcessedCount++;
		}
	}

	return ProcessedCount;
}

bool FRealGazeboEncodingThread::EncodeTextureFrame(const FStreamKey& StreamKey, FStreamEncoder& StreamEncoder,
                                                    TSharedPtr<FTextureFrameData> TextureFrame,
                                                    TSharedPtr<FEncodedFrameData>& OutEncodedFrame)
{
	if (!StreamEncoder.Encoder.IsValid())
	{
		return false;
	}

	// Acquire encoded frame from pool
	if (FramePool.IsValid())
	{
		OutEncodedFrame = FramePool->AcquireEncodedFrame(TextureFrame->FrameNumber, false);
	}
	else
	{
		OutEncodedFrame = MakeShared<FEncodedFrameData>();
		OutEncodedFrame->FrameNumber = TextureFrame->FrameNumber;
	}

	// Copy dimensions from texture frame
	OutEncodedFrame->Dimensions = TextureFrame->Dimensions;

	// Encode GPU texture directly (zero-copy)
	const double StartTime = FPlatformTime::Seconds();
	const bool bSuccess = StreamEncoder.Encoder->EncodeTextureFrame(TextureFrame->Texture, OutEncodedFrame,
	                                                                 TextureFrame->CaptureTimestamp);
	const double EndTime = FPlatformTime::Seconds();

	if (bSuccess)
	{
		// Send to RTSP thread
		if (RTSPThread.IsValid())
		{
			RTSPThread->EnqueueFrame(StreamKey, OutEncodedFrame);
		}

		// Update statistics (thread-safe)
		StreamEncoder.FramesEncoded.fetch_add(1);

		// Update timing statistics with mutex protection
		{
			FScopeLock Lock(&StreamEncoder.StatsMutex);
			StreamEncoder.TotalEncodeTime += (EndTime - StartTime);
			StreamEncoder.EncodeCount++;
		}
	}
	else
	{
		StreamEncoder.FramesDropped.fetch_add(1);
	}

	return bSuccess;
}

TSharedPtr<FRealGazeboEncodingThread::FStreamEncoder> FRealGazeboEncodingThread::GetStreamEncoder(const FStreamKey& StreamKey) const
{
	FScopeLock Lock(&EncoderMapMutex);

	const TSharedPtr<FStreamEncoder>* Found = StreamEncoders.Find(StreamKey);
	return Found ? *Found : nullptr;
}
