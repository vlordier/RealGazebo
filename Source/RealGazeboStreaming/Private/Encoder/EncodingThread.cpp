// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Encoder/EncodingThread.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

//----------------------------------------------------------
// Construction & Initialization
//----------------------------------------------------------

FEncodingThread::FEncodingThread(const FStreamIdentifier& InStreamID, TSharedPtr<FHardwareEncoderWrapper> InEncoder, TSharedPtr<FFramePool> InFramePool, int32 InMaxQueueSize)
	: StreamID(InStreamID)
	, Encoder(InEncoder)
	, FramePool(InFramePool)
	, MaxQueueSize(InMaxQueueSize)
{
	ThreadName = FString::Printf(TEXT("EncodingThread_%s"), *StreamID.ToString());
	UE_LOG(LogTemp, Log, TEXT("EncodingThread: Created for stream %s (MaxQueueSize=%d)"), *StreamID.ToString(), MaxQueueSize);
}

FEncodingThread::~FEncodingThread()
{
	Shutdown();
}

bool FEncodingThread::Start()
{
	if (bRunning)
	{
		UE_LOG(LogTemp, Warning, TEXT("EncodingThread: Already running"));
		return true;
	}

	if (!Encoder || !Encoder->IsReady())
	{
		UE_LOG(LogTemp, Error, TEXT("EncodingThread: Encoder not ready"));
		return false;
	}

	// Create and start thread
	bStopRequested = false;
	Thread = TUniquePtr<FRunnableThread>(FRunnableThread::Create(
		this,
		*ThreadName,
		0, // Auto stack size
		TPri_Normal,
		FPlatformAffinity::GetPoolThreadMask()
	));

	if (!Thread)
	{
		UE_LOG(LogTemp, Error, TEXT("EncodingThread: Failed to create thread"));
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("EncodingThread: Started thread for stream %s"), *StreamID.ToString());
	return true;
}

void FEncodingThread::Stop()
{
	if (!bRunning)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("EncodingThread: Stopping thread for stream %s"), *StreamID.ToString());
	bStopRequested = true;
}

void FEncodingThread::Shutdown()
{
	if (!Thread)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("EncodingThread: Shutting down for stream %s"), *StreamID.ToString());

	// Stop thread
	Stop();

	// Wait for thread to exit (with timeout)
	Thread->WaitForCompletion();
	Thread.Reset();

	UE_LOG(LogTemp, Log, TEXT("EncodingThread: Shutdown complete for stream %s (Processed: %llu, Dropped: %llu)"),
		*StreamID.ToString(),
		TotalFramesProcessed.load(),
		TotalFramesDropped.load());
}

//----------------------------------------------------------
// Frame Queue
//----------------------------------------------------------

bool FEncodingThread::QueueFrame(const FCaptureFrame& CapturedFrame)
{
	if (!bRunning || bStopRequested)
	{
		return false;
	}

	// Check queue size to prevent unbounded growth
	if (QueueSize >= MaxQueueSize)
	{
		TotalFramesDropped++;
		UE_LOG(LogTemp, Warning, TEXT("EncodingThread: Queue full for stream %s - dropping frame (QueueSize=%d/%d)"),
			*StreamID.ToString(), QueueSize.load(), MaxQueueSize);
		return false;
	}

	// Enqueue frame and increment size atomically
	FrameQueue.Enqueue(CapturedFrame);
	QueueSize++;
	return true;
}

int32 FEncodingThread::GetQueuedFrameCount() const
{
	return QueueSize.load();
}

//----------------------------------------------------------
// FRunnable Interface
//----------------------------------------------------------

bool FEncodingThread::Init()
{
	UE_LOG(LogTemp, Log, TEXT("EncodingThread: Init() for stream %s"), *StreamID.ToString());
	bRunning = true;
	return true;
}

uint32 FEncodingThread::Run()
{
	UE_LOG(LogTemp, Log, TEXT("EncodingThread: Run() started for stream %s"), *StreamID.ToString());

	// Main encoding loop
	while (!bStopRequested)
	{
		IterationCount++;

		// Process next frame from queue
		if (!ProcessNextFrame())
		{
			// No frames to process - sleep briefly to avoid busy-waiting
			FPlatformProcess::Sleep(0.001f); // 1ms sleep
		}

		// Yield to other threads periodically
		if (IterationCount % 100 == 0)
		{
			FPlatformProcess::Sleep(0.0f); // Yield CPU
		}
	}

	UE_LOG(LogTemp, Log, TEXT("EncodingThread: Run() exiting for stream %s"), *StreamID.ToString());
	return 0;
}

void FEncodingThread::Exit()
{
	UE_LOG(LogTemp, Log, TEXT("EncodingThread: Exit() for stream %s"), *StreamID.ToString());
	bRunning = false;
}

//----------------------------------------------------------
// Internal Encoding Loop
//----------------------------------------------------------

bool FEncodingThread::ProcessNextFrame()
{
	// Dequeue frame and decrement size atomically
	FCaptureFrame CapturedFrame;
	if (!FrameQueue.Dequeue(CapturedFrame))
	{
		return false; // Queue empty
	}
	QueueSize--;

	// Verify frame is valid
	if (!CapturedFrame.PooledFrame || !CapturedFrame.PooledFrame->IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("EncodingThread: Invalid pooled frame for stream %s"),
			*StreamID.ToString());
		// CRITICAL FIX: Release invalid frame back to pool to prevent exhaustion
		if (FramePool && CapturedFrame.PooledFrame)
		{
			FramePool->ReleaseFrame(CapturedFrame.PooledFrame);
		}
		return false;
	}

	// Wait for GPU fence (frame might still be rendering)
	if (CapturedFrame.GPUFence && !CapturedFrame.GPUFence->Poll())
	{
		// Frame not ready yet - wait
		CapturedFrame.WaitForGPU();
	}

	// Submit frame to encoder
	const bool bSuccess = Encoder->EncodeFrame(
		CapturedFrame.PooledFrame->Texture,
		CapturedFrame.FrameNumber,
		false // Don't force keyframe
	);

	if (!bSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("EncodingThread: Failed to encode frame %llu for stream %s"),
			CapturedFrame.FrameNumber, *StreamID.ToString());
		// CRITICAL FIX: Release frame back to pool even on encode failure
		if (FramePool)
		{
			FramePool->ReleaseFrame(CapturedFrame.PooledFrame);
		}
		return false;
	}

	// Retrieve encoded NAL units
	TArray<FEncodedNALUnit> NALUnits;
	if (Encoder->GetEncodedData(NALUnits) && NALUnits.Num() > 0)
	{
		// Broadcast NAL units to RTSP server
		if (OnNALUnitsEncoded.IsBound())
		{
			OnNALUnitsEncoded.Execute(NALUnits);
		}
	}

	// CRITICAL FIX: Release frame back to pool after successful encoding
	// This prevents pool exhaustion - without this, pool runs out of frames!
	if (FramePool)
	{
		FramePool->ReleaseFrame(CapturedFrame.PooledFrame);
	}

	LastProcessTime = FPlatformTime::Seconds();
	TotalFramesProcessed++;

	return true;
}

//----------------------------------------------------------
// Statistics
//----------------------------------------------------------

FString FEncodingThread::GetStatsString() const
{
	const double CurrentTime = FPlatformTime::Seconds();
	const double TimeSinceLastProcess = CurrentTime - LastProcessTime;

	return FString::Printf(
		TEXT("EncodingThread [%s]: Running=%s, Processed=%llu, Dropped=%llu, Queued=%d, Iterations=%llu, LastProcess=%.3fs ago"),
		*StreamID.ToString(),
		bRunning.load() ? TEXT("Yes") : TEXT("No"),
		TotalFramesProcessed.load(),
		TotalFramesDropped.load(),
		GetQueuedFrameCount(),
		IterationCount.load(),
		TimeSinceLastProcess
	);
}
