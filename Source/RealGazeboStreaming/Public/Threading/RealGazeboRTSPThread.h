// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Pipeline/RealGazeboStreamQueue.h"
#include "Pipeline/RealGazeboFrameData.h"
#include "Core/RealGazeboStreamingTypes.h"
#include <atomic>

// Forward declarations
class FRealGazeboRTSPServer;

/**
 * RTSP Thread for Live555 Event Loop
 *
 * Dedicated thread that runs the Live555 task scheduler event loop
 * and handles RTP/RTCP networking for all active streams.
 *
 * This thread MUST run continuously without blocking to maintain
 * smooth RTSP streaming for all connected clients.
 *
 * Flow:
 * 1. Encoding thread pushes encoded frames to RTSP queues
 * 2. RTSP thread dequeues frames
 * 3. Frames are delivered to Live555 H264 sources
 * 4. Live555 handles RTP/RTCP packetization and transmission
 *
 * Performance:
 * - Non-blocking event loop (Live555 doEventLoop with timeout)
 * - Handles multiple concurrent client streams
 * - Minimal latency (<5ms) from queue to network
 */
class REALGAZEBOSTREAMING_API FRealGazeboRTSPThread : public FRunnable
{
public:
	/**
	 * Constructor
	 * @param InRTSPServer RTSP server instance to manage
	 */
	explicit FRealGazeboRTSPThread(TSharedPtr<FRealGazeboRTSPServer> InRTSPServer);
	virtual ~FRealGazeboRTSPThread();

	//~ Begin FRunnable Interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;
	//~ End FRunnable Interface

	/**
	 * Start the thread
	 * @return True if thread started successfully
	 */
	bool Start();

	/**
	 * Check if thread is running
	 */
	bool IsRunning() const { return bIsRunning.load(std::memory_order_acquire); }

	/**
	 * Enqueue encoded frame for RTSP streaming
	 * @param StreamKey Target stream
	 * @param EncodedFrame Frame to stream
	 * @return True if enqueued successfully
	 */
	bool EnqueueFrame(const FStreamKey& StreamKey, TSharedPtr<FEncodedFrameData> EncodedFrame);

private:
	/** Per-stream queue and statistics */
	struct FStreamQueue
	{
		TRealGazeboStreamQueue<TSharedPtr<FEncodedFrameData>> Queue;
		std::atomic<int64> FramesSent{0};
		std::atomic<int64> FramesDropped{0};
	};

	/** RTSP server reference */
	TSharedPtr<FRealGazeboRTSPServer> RTSPServer;

	/** Per-stream queues */
	TMap<FStreamKey, TSharedPtr<FStreamQueue>> StreamQueues;
	mutable FCriticalSection QueueMapMutex;

	/** Thread control */
	FRunnableThread* ThreadHandle = nullptr;
	std::atomic<bool> bIsRunning{false};
	std::atomic<bool> bStopRequested{false};

	/** Internal methods */
	int32 ProcessFrameQueues();  // Returns number of frames processed
	bool ProcessStreamQueue(const FStreamKey& StreamKey, FStreamQueue& StreamQueue);
	TSharedPtr<FStreamQueue> GetOrCreateStreamQueue(const FStreamKey& StreamKey);
};
