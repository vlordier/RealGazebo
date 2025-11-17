// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Threading/RealGazeboEncodingThread.h"
#include "Threading/RealGazeboRTSPThread.h"
#include "Encoding/RealGazeboHardwareEncoder.h"
#include "Pipeline/RealGazeboFramePool.h"
#include "RTSP/RealGazeboRTSPServer.h"
#include "Core/RealGazeboStreamingTypes.h"

FRealGazeboEncodingThread::FRealGazeboEncodingThread(TSharedPtr<FRealGazeboFramePool> InFramePool,
                                                       TSharedPtr<FRealGazeboRTSPThread> InRTSPThread,
                                                       TSharedPtr<FRealGazeboRTSPServer> InRTSPServer)
	: FramePool(InFramePool)
	, RTSPThread(InRTSPThread)
	, RTSPServer(InRTSPServer)
	, ThreadHandle(nullptr)
{
}

FRealGazeboEncodingThread::~FRealGazeboEncodingThread()
{
	// CRITICAL FIX (Bug #3): Proper thread shutdown to prevent use-after-free
	// Reference: OBS NVENC shutdown pattern

	// Step 1: Signal thread to stop (atomic release)
	Stop();

	// Step 2: Wait for Run() to exit BEFORE touching encoders
	if (ThreadHandle)
	{
		ThreadHandle->WaitForCompletion();
		delete ThreadHandle;
		ThreadHandle = nullptr;
	}

	// Step 3: NOW safe to cleanup encoders (thread is fully stopped)
	{
		FScopeLock Lock(&EncoderMapMutex);

		// Shutdown and release all encoders
		for (auto& Pair : StreamEncoders)
		{
			TSharedPtr<FStreamEncoder> StreamEncoder = Pair.Value;
			if (StreamEncoder.IsValid() && StreamEncoder->Encoder.IsValid())
			{
				UE_LOG(LogRealGazeboStreaming, Verbose,
					TEXT("EncodingThread destructor: Shutting down encoder for stream %s"),
					*Pair.Key.ToString());

				StreamEncoder->Encoder->Shutdown();
				StreamEncoder->Encoder.Reset();
			}
		}

		StreamEncoders.Empty();
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("EncodingThread: Destructor complete"));
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
		const int32 FramesProcessed = ProcessEncodingQueues();

		// CRITICAL FIX: Only sleep when NO work was done
		// Sleeping unconditionally adds 1ms latency to every frame
		if (FramesProcessed == 0)
		{
			FPlatformProcess::Sleep(IDLE_SLEEP_TIME);  // 1ms sleep when idle
		}
		// else: Immediately loop back to process more frames (minimal latency)
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

bool FRealGazeboEncodingThread::RegisterEncoder(const FStreamKey& StreamKey, TSharedPtr<IRealGazeboHardwareEncoder> Encoder, int32 BitrateKbps)
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

	// Cache bitrate for RTSP SDP (will be synced to MediaSubsession when SPS/PPS is set)
	StreamEncoder->BitrateKbps.store(BitrateKbps, std::memory_order_release);

	StreamEncoders.Add(StreamKey, StreamEncoder);

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("EncodingThread: Registered encoder for stream %s (%s) - Texture Encoding: %s | Bitrate: %d kbps"),
		*StreamKey.ToString(), *Encoder->GetEncoderName(),
		StreamEncoder->bSupportsTextureEncoding.load() ? TEXT("YES") : TEXT("NO"), BitrateKbps);

	// DEBUG: Log detailed StreamKey info for isolation verification
	UE_LOG(LogRealGazeboStreaming, Verbose,
		TEXT("EncodingThread: RegisterEncoder - %s | Encoder: %s | Total encoders: %d"),
		*StreamKey.ToDebugString(), *Encoder->GetEncoderName(), StreamEncoders.Num());

	return true;
}

void FRealGazeboEncodingThread::UnregisterEncoder(const FStreamKey& StreamKey)
{
	FScopeLock Lock(&EncoderMapMutex);

	TSharedPtr<FStreamEncoder>* FoundEncoder = StreamEncoders.Find(StreamKey);
	if (!FoundEncoder || !FoundEncoder->IsValid())
	{
		return;
	}

	TSharedPtr<FStreamEncoder> StreamEncoder = *FoundEncoder;

	// CRITICAL: Drain pending frames from TextureQueue BEFORE encoder shutdown
	// This prevents ActiveFrames leak when encoder is destroyed with pending frames
	int32 DrainedFrames = 0;
	TSharedPtr<FTextureFrameData> TextureFrame;
	while (StreamEncoder->TextureQueue.Dequeue(TextureFrame))
	{
		DrainedFrames++;
		// Frame automatically released when TSharedPtr goes out of scope
	}

	if (DrainedFrames > 0)
	{
		UE_LOG(LogRealGazeboStreaming, Log,
			TEXT("EncodingThread: Drained %d pending frames from TextureQueue for stream %s"),
			DrainedFrames, *StreamKey.ToString());
	}

	// Shutdown encoder to release any ActiveFrames from ObtainInputFrame calls
	if (StreamEncoder->Encoder.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Verbose,
			TEXT("EncodingThread: Shutting down encoder for stream %s"),
			*StreamKey.ToString());

		StreamEncoder->Encoder->Shutdown();
		StreamEncoder->Encoder.Reset();
	}

	// Now safe to remove from map
	if (StreamEncoders.Remove(StreamKey) > 0)
	{
		UE_LOG(LogRealGazeboStreaming, Log,
			TEXT("EncodingThread: Unregistered encoder for stream %s"),
			*StreamKey.ToString());
	}
}

bool FRealGazeboEncodingThread::EnqueueTextureFrame(const FStreamKey& StreamKey, FTexture2DRHIRef Texture,
                                                     int64 TimestampUs, uint64 FrameNumber)
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

	// Create texture frame wrapper with microsecond-precision timing
	TSharedPtr<FTextureFrameData> TextureFrame = MakeShared<FTextureFrameData>();
	TextureFrame->Texture = Texture;
	TextureFrame->CaptureTimestampUs = TimestampUs;
	TextureFrame->FrameNumber = FrameNumber;
	TextureFrame->Dimensions = FIntPoint(Texture->GetSizeX(), Texture->GetSizeY());

	const bool bEnqueued = StreamEncoder->TextureQueue.Enqueue(TextureFrame);

	// DEBUG: Log texture frame enqueue for isolation verification
	if (bEnqueued)
	{
		UE_LOG(LogRealGazeboStreaming, VeryVerbose,
			TEXT("EncodingThread: EnqueueTextureFrame - %s | Frame %llu | Queue depth: %d"),
			*StreamKey.ToDebugString(), FrameNumber, StreamEncoder->TextureQueue.GetDepth());
	}

	return bEnqueued;
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
	if (StreamEncoder.IsValid())
	{
		// Update encoder hardware bitrate
		if (StreamEncoder->Encoder.IsValid())
		{
			StreamEncoder->Encoder->UpdateBitrate(NewBitrateKbps);
		}

		// Update cached bitrate for RTSP SDP
		StreamEncoder->BitrateKbps.store(NewBitrateKbps, std::memory_order_release);

		// Sync to MediaSubsession cache
		FRealGazeboMediaSubsession::UpdateBitrate(StreamKey, NewBitrateKbps);
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

int32 FRealGazeboEncodingThread::ProcessEncodingQueues()
{
	// CRITICAL FIX: Snapshot encoder list to avoid holding mutex during encoding
	// Previous code held EncoderMapMutex while encoding, blocking render thread's
	// SubmitTextureFrame() and adding 1-5ms latency per frame

	TArray<TPair<FStreamKey, TSharedPtr<FStreamEncoder>>> EncoderSnapshot;
	{
		FScopeLock Lock(&EncoderMapMutex);
		EncoderSnapshot.Reserve(StreamEncoders.Num());
		for (const auto& Pair : StreamEncoders)
		{
			EncoderSnapshot.Add(Pair);
		}
	}
	// Mutex released - render thread can now submit frames without blocking

	int32 TotalProcessed = 0;

	for (auto& Pair : EncoderSnapshot)  // Non-const for mutable access to shared pointer
	{
		const FStreamKey& StreamKey = Pair.Key;
		TSharedPtr<FStreamEncoder> StreamEncoder = Pair.Value;  // Copy shared pointer

		if (StreamEncoder.IsValid())
		{
			const int32 Processed = ProcessStreamEncoder(StreamKey, *StreamEncoder);
			TotalProcessed += Processed;
		}
	}

	return TotalProcessed;
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

	// Acquire encoded frame from pool with custom deleter for automatic release
	if (FramePool.IsValid())
	{
		OutEncodedFrame = FramePool->AcquireEncodedFrame(TextureFrame->FrameNumber, false);

		// CRITICAL FIX (Bug #5): Use move semantics to avoid circular reference
		// Previous code captured TSharedPtr by value, creating circular reference
		// New code moves ownership into lambda, allowing proper cleanup when ref count reaches 0
		TWeakPtr<FRealGazeboFramePool> WeakPool = FramePool;
		FEncodedFrameData* RawPtr = OutEncodedFrame.Get();

		OutEncodedFrame = TSharedPtr<FEncodedFrameData>(RawPtr,
			[WeakPool, Frame = MoveTemp(OutEncodedFrame)](FEncodedFrameData*) mutable
			{
				// Custom deleter: Return frame to pool instead of deleting
				if (TSharedPtr<FRealGazeboFramePool> Pool = WeakPool.Pin())
				{
					Pool->ReleaseEncodedFrame(Frame);
				}
				// Frame's TSharedPtr will properly delete when lambda is destroyed
			});
	}
	else
	{
		OutEncodedFrame = MakeShared<FEncodedFrameData>();
		OutEncodedFrame->FrameNumber = TextureFrame->FrameNumber;
	}

	// Copy dimensions from texture frame
	OutEncodedFrame->Dimensions = TextureFrame->Dimensions;

	// Encode GPU texture directly (zero-copy) with microsecond-precision timing
	const int64 StartTimeUs = RealGazeboStreamingTime::GetTimeMicroseconds();
	const bool bSuccess = StreamEncoder.Encoder->EncodeTextureFrame(TextureFrame->Texture, OutEncodedFrame,
	                                                                 TextureFrame->CaptureTimestampUs);
	const int64 EndTimeUs = RealGazeboStreamingTime::GetTimeMicroseconds();

	if (bSuccess)
	{
		// DEBUG: Log encoded frame output for isolation verification
		UE_LOG(LogRealGazeboStreaming, VeryVerbose,
			TEXT("EncodingThread: EncodeTextureFrame - %s | Frame %llu | Encoded: %d bytes | Time: %.2f ms"),
			*StreamKey.ToDebugString(), OutEncodedFrame->FrameNumber, OutEncodedFrame->EncodedData.Num(),
			RealGazeboStreamingTime::MicrosecondsToMilliseconds(EndTimeUs - StartTimeUs));

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
			StreamEncoder.TotalEncodeTime += RealGazeboStreamingTime::MicrosecondsToMilliseconds(EndTimeUs - StartTimeUs) / 1000.0;
			StreamEncoder.EncodeCount++;
		}

		// CRITICAL: Set SPS/PPS on first successful encode
		// SPS/PPS are extracted from the first keyframe by the encoder
		// This must be done before RTSP clients connect to ensure proper H.264 initialization
		if (!StreamEncoder.bSPSPPSSet.load(std::memory_order_acquire) && RTSPServer.IsValid())
		{
			TArray<uint8> SPS, PPS;
			if (StreamEncoder.Encoder.IsValid() &&
			    StreamEncoder.Encoder->GetSPS(SPS) &&
			    StreamEncoder.Encoder->GetPPS(PPS))
			{
				// CRITICAL FIX (Bug #7): Validate SPS/PPS before sending to RTSP server
				// Invalid parameter sets cause decoder initialization failures and stream corruption
				const bool bValidSPS = SPS.Num() >= 4 && (SPS[0] & 0x1F) == 7;  // NAL type 7 = SPS
				const bool bValidPPS = PPS.Num() >= 4 && (PPS[0] & 0x1F) == 8;  // NAL type 8 = PPS

				if (bValidSPS && bValidPPS)
				{
					RTSPServer->SetSPSPPS(StreamKey, SPS, PPS);
					StreamEncoder.bSPSPPSSet.store(true, std::memory_order_release);

					// CRITICAL: Update cached bitrate in MediaSubsession immediately after SPS/PPS
					// This ensures RTSP SDP reports correct bitrate when clients connect
					// Use cached bitrate from RegisterEncoder (matches encoder config)
					const int32 BitrateKbps = StreamEncoder.BitrateKbps.load(std::memory_order_acquire);
					FRealGazeboMediaSubsession::UpdateBitrate(StreamKey, BitrateKbps);

					UE_LOG(LogRealGazeboStreaming, Log,
						TEXT("EncodingThread: Set SPS/PPS for stream %s (SPS: %d bytes, PPS: %d bytes, Bitrate: %d kbps)"),
						*StreamKey.ToString(), SPS.Num(), PPS.Num(), BitrateKbps);
				}
				else
				{
					UE_LOG(LogRealGazeboStreaming, Error,
						TEXT("EncodingThread: Invalid SPS/PPS for stream %s (SPS: %d bytes type %d, PPS: %d bytes type %d)"),
						*StreamKey.ToString(),
						SPS.Num(), SPS.Num() > 0 ? (SPS[0] & 0x1F) : 0,
						PPS.Num(), PPS.Num() > 0 ? (PPS[0] & 0x1F) : 0);
				}
			}
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
