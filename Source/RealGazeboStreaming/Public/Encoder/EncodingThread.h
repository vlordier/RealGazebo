// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "StreamingTypes.h"
#include "HardwareEncoderWrapper.h"
#include "FrameCapture.h"

/**
 * FEncodingThread
 *
 * Per-stream background encoding thread.
 * Runs encoding loop without blocking game thread.
 *
 * CRITICAL: Each stream has its own isolated thread!
 * Never share encoding threads between streams.
 *
 * Thread Responsibilities:
 * 1. Wait for captured frames (from FrameCapture)
 * 2. Submit frames to hardware encoder
 * 3. Retrieve encoded NAL units
 * 4. Forward NAL units to RTSP server
 *
 * Lifecycle:
 * - Start() - Creates thread and starts encoding loop
 * - QueueFrame() - Add captured frame to encode queue
 * - Run() - Main encoding loop (executes on background thread)
 * - Stop() - Signals thread to stop
 * - Shutdown() - Waits for thread to exit and cleans up
 *
 * Thread Safety:
 * - Frame queue protected by mutex
 * - Encoder is NOT thread-safe internally (accessed only by this thread)
 * - NAL output queue is thread-safe (accessed by RTSP thread)
 */
class FEncodingThread : public FRunnable
{
public:
	//----------------------------------------------------------
	// Delegates
	//----------------------------------------------------------

	/** Called when NAL units are encoded (thread-safe) */
	DECLARE_DELEGATE_OneParam(FOnNALUnitsEncoded, const TArray<FEncodedNALUnit>&);

	//----------------------------------------------------------
	// Construction & Initialization
	//----------------------------------------------------------

	/**
	 * Constructor
	 *
	 * @param InStreamID - Unique stream identifier
	 * @param InEncoder - Hardware encoder instance (owned by pipeline, accessed only by this thread)
	 * @param InFramePool - Frame pool for releasing frames after encoding (CRITICAL: prevents pool exhaustion)
	 * @param InMaxQueueSize - Maximum frame queue size (FPS-aware, prevents memory buildup)
	 */
	FEncodingThread(const FStreamIdentifier& InStreamID, TSharedPtr<FHardwareEncoderWrapper> InEncoder, TSharedPtr<FFramePool> InFramePool, int32 InMaxQueueSize);

	/** Destructor */
	virtual ~FEncodingThread() override;

	/**
	 * Start encoding thread.
	 *
	 * @return True if thread started successfully
	 */
	bool Start();

	/** Shutdown thread and wait for exit */
	void Shutdown();

	//----------------------------------------------------------
	// Frame Queue
	//----------------------------------------------------------

	/**
	 * Queue a captured frame for encoding.
	 * Thread-safe - can be called from game thread.
	 *
	 * @param CapturedFrame - Frame to encode
	 * @return True if queued successfully
	 */
	bool QueueFrame(const FCaptureFrame& CapturedFrame);

	/** Get number of frames waiting to be encoded */
	int32 GetQueuedFrameCount() const;

	//----------------------------------------------------------
	// Callbacks
	//----------------------------------------------------------

	/** Register callback for when NAL units are encoded */
	void SetNALEncodedCallback(FOnNALUnitsEncoded InCallback)
	{
		OnNALUnitsEncoded = InCallback;
	}

	//----------------------------------------------------------
	// FRunnable Interface
	//----------------------------------------------------------

	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

	//----------------------------------------------------------
	// Status
	//----------------------------------------------------------

	/** Is thread running? */
	bool IsRunning() const { return bRunning; }

	/** Get statistics string */
	FString GetStatsString() const;

private:
	//----------------------------------------------------------
	// Internal Encoding Loop
	//----------------------------------------------------------

	/**
	 * Process one frame from the queue.
	 * Runs on encoding thread.
	 *
	 * @return True if frame was processed
	 */
	bool ProcessNextFrame();

	//----------------------------------------------------------
	// Configuration
	//----------------------------------------------------------

	/** Stream identifier */
	FStreamIdentifier StreamID;

	/** Hardware encoder (accessed only by this thread) */
	TSharedPtr<FHardwareEncoderWrapper> Encoder;

	/** Frame pool for releasing frames after encoding (CRITICAL: prevents pool exhaustion) */
	TSharedPtr<FFramePool> FramePool;

	//----------------------------------------------------------
	// Threading
	//----------------------------------------------------------

	/** Thread instance */
	TUniquePtr<FRunnableThread> Thread;

	/** Thread name */
	FString ThreadName;

	/** Is thread running? */
	std::atomic<bool> bRunning{false};

	/** Should thread exit? */
	std::atomic<bool> bStopRequested{false};

	//----------------------------------------------------------
	// Frame Queue (Thread-Safe)
	//----------------------------------------------------------

	/** Queue of frames waiting to be encoded */
	TQueue<FCaptureFrame> FrameQueue;

	/** Mutex for frame queue statistics (TQueue is already thread-safe for enqueue/dequeue) */
	mutable FCriticalSection QueueMutex;

	/** Current queue size (atomic tracking since TQueue has no size() method) */
	std::atomic<int32> QueueSize{0};

	/** Maximum queue size (FPS-aware, prevents memory buildup if encoding is too slow) */
	int32 MaxQueueSize;

	//----------------------------------------------------------
	// Callbacks
	//----------------------------------------------------------

	/** Callback when NAL units are encoded */
	FOnNALUnitsEncoded OnNALUnitsEncoded;

	//----------------------------------------------------------
	// Statistics
	//----------------------------------------------------------

	/** Total frames encoded */
	std::atomic<uint64> TotalFramesProcessed{0};

	/** Total frames dropped (queue full) */
	std::atomic<uint64> TotalFramesDropped{0};

	/** Last frame process timestamp */
	double LastProcessTime = 0.0;

	/** Thread iteration count */
	std::atomic<uint64> IterationCount{0};
};
