// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
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
 * Per-stream background encoding thread that runs the encoding loop without blocking the game thread.
 * Enables parallel encoding of multiple streams by running each encoder on its own worker thread.
 *
 * CRITICAL ISOLATION REQUIREMENT:
 * Each stream has its own dedicated encoding thread. Never share threads between streams
 * as encoders maintain internal state that cannot be safely shared.
 *
 * Thread Responsibilities:
 * 1. Dequeue captured frames from the frame queue
 * 2. Submit frames to the hardware encoder (NVENC/AMF)
 * 3. Retrieve encoded NAL units from the encoder
 * 4. Forward NAL units to the RTSP streaming layer via callback
 *
 * Lifecycle:
 * - Start() - Create thread and begin encoding loop
 * - QueueFrame() - Add captured frame to processing queue (game thread)
 * - Run() - Main encoding loop running on background thread
 * - Stop() - Signal thread to gracefully stop
 * - Shutdown() - Wait for thread exit and clean up resources
 *
 * Thread Safety Considerations:
 * - Frame input queue: Thread-safe (TQueue + mutex for statistics)
 * - Hardware encoder: NOT thread-safe, accessed exclusively by this thread
 * - NAL output callback: Thread-safe, called from this thread
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
		FScopeLock Lock(&CallbackMutex);
		OnNALUnitsEncoded = InCallback;
	}

	/** Clear callback - MUST be called before destroying the callback target */
	void ClearNALEncodedCallback()
	{
		FScopeLock Lock(&CallbackMutex);
		OnNALUnitsEncoded.Unbind();
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

	/** Mutex for callback access */
	mutable FCriticalSection CallbackMutex;

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
