// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Threading/RealGazeboRTSPThread.h"
#include "RTSP/RealGazeboRTSPServer.h"
#include "Core/RealGazeboStreamingTypes.h"

FRealGazeboRTSPThread::FRealGazeboRTSPThread(TSharedPtr<FRealGazeboRTSPServer> InRTSPServer)
	: RTSPServer(InRTSPServer)
	, ThreadHandle(nullptr)
{
}

FRealGazeboRTSPThread::~FRealGazeboRTSPThread()
{
	Stop();

	if (ThreadHandle)
	{
		ThreadHandle->WaitForCompletion();
		delete ThreadHandle;
		ThreadHandle = nullptr;
	}
}

bool FRealGazeboRTSPThread::Init()
{
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSPThread: Initialized"));
	return true;
}

uint32 FRealGazeboRTSPThread::Run()
{
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSPThread: Started"));
	bIsRunning.store(true, std::memory_order_release);

	while (!bStopRequested.load(std::memory_order_acquire))
	{
		// Process frame queues and deliver to RTSP server
		// Note: The RTSP server runs its own thread with Live555's doEventLoop()
		// This thread only needs to dequeue frames and push them to the server
		const int32 FramesProcessed = ProcessFrameQueues();

		// Sleep only when idle (no frames processed)
		if (FramesProcessed == 0)
		{
			FPlatformProcess::Sleep(0.001f);  // 1ms sleep when idle
		}
	}

	bIsRunning.store(false, std::memory_order_release);
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSPThread: Stopped"));
	return 0;
}

void FRealGazeboRTSPThread::Stop()
{
	bStopRequested.store(true, std::memory_order_release);
}

void FRealGazeboRTSPThread::Exit()
{
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RTSPThread: Exiting"));
}

bool FRealGazeboRTSPThread::Start()
{
	if (ThreadHandle)
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("RTSPThread: Already started"));
		return false;
	}

	bStopRequested.store(false);
	ThreadHandle = FRunnableThread::Create(this, TEXT("RealGazeboRTSPThread"), 0,
	                                        TPri_Normal);  // Normal priority for network I/O

	return ThreadHandle != nullptr;
}

bool FRealGazeboRTSPThread::EnqueueFrame(const FStreamKey& StreamKey, TSharedPtr<FEncodedFrameData> EncodedFrame)
{
	if (!EncodedFrame.IsValid())
	{
		return false;
	}

	TSharedPtr<FStreamQueue> StreamQueue = GetOrCreateStreamQueue(StreamKey);
	if (!StreamQueue.IsValid())
	{
		return false;
	}

	const bool bEnqueued = StreamQueue->Queue.Enqueue(EncodedFrame);

	if (!bEnqueued)
	{
		StreamQueue->FramesDropped.fetch_add(1);
	}

	return bEnqueued;
}

int32 FRealGazeboRTSPThread::ProcessFrameQueues()
{
	FScopeLock Lock(&QueueMapMutex);

	int32 TotalProcessed = 0;

	for (auto& Pair : StreamQueues)
	{
		const FStreamKey& StreamKey = Pair.Key;
		TSharedPtr<FStreamQueue>& StreamQueue = Pair.Value;

		if (StreamQueue.IsValid())
		{
			if (ProcessStreamQueue(StreamKey, *StreamQueue))
			{
				TotalProcessed++;
			}
		}
	}

	return TotalProcessed;
}

bool FRealGazeboRTSPThread::ProcessStreamQueue(const FStreamKey& StreamKey, FStreamQueue& StreamQueue)
{
	TSharedPtr<FEncodedFrameData> EncodedFrame;
	if (!StreamQueue.Queue.Dequeue(EncodedFrame))
	{
		return false;  // Queue empty
	}

	if (!EncodedFrame.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Error,
			TEXT("RTSPThread: ProcessStreamQueue - Invalid frame dequeued for %s"),
			*StreamKey.ToDebugString());
		return false;
	}

	// CRITICAL DEBUG (2025-11-17): Log frame dequeue to trace pipeline flow
	UE_LOG(LogRealGazeboStreaming, Warning,
		TEXT("RTSPThread: Dequeued frame %llu for %s | Size: %d bytes | Keyframe: %s | Pushing to RTSPServer..."),
		EncodedFrame->FrameNumber, *StreamKey.ToDebugString(), EncodedFrame->EncodedData.Num(),
		EncodedFrame->bIsKeyFrame ? TEXT("YES") : TEXT("NO"));

	// Push frame to RTSP server
	if (RTSPServer.IsValid())
	{
		const bool bSuccess = RTSPServer->PushFrame(StreamKey, EncodedFrame);
		if (bSuccess)
		{
			StreamQueue.FramesSent.fetch_add(1);
			UE_LOG(LogRealGazeboStreaming, Warning,
				TEXT("RTSPThread: Successfully pushed frame %llu to RTSPServer for %s"),
				EncodedFrame->FrameNumber, *StreamKey.ToDebugString());
			return true;
		}
		else
		{
			// CRITICAL: PushFrame failed - stream likely not registered!
			UE_LOG(LogRealGazeboStreaming, Error,
				TEXT("RTSPThread: FAILED to push frame %llu to RTSPServer for %s - Stream not registered in RTSPServer?"),
				EncodedFrame->FrameNumber, *StreamKey.ToDebugString());
		}
	}
	else
	{
		UE_LOG(LogRealGazeboStreaming, Error,
			TEXT("RTSPThread: RTSPServer is INVALID for %s"),
			*StreamKey.ToDebugString());
	}

	return false;
}

TSharedPtr<FRealGazeboRTSPThread::FStreamQueue> FRealGazeboRTSPThread::GetOrCreateStreamQueue(const FStreamKey& StreamKey)
{
	FScopeLock Lock(&QueueMapMutex);

	TSharedPtr<FStreamQueue>* Found = StreamQueues.Find(StreamKey);
	if (Found)
	{
		return *Found;
	}

	// Create new queue for this stream
	TSharedPtr<FStreamQueue> NewQueue = MakeShared<FStreamQueue>();
	StreamQueues.Add(StreamKey, NewQueue);

	UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("RTSPThread: Created queue for stream %s"),
		*StreamKey.ToString());

	return NewQueue;
}
