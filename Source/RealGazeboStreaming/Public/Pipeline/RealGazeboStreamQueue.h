// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Containers/Queue.h"
#include <atomic>

/**
 * Thread-safe queue for streaming pipeline
 * Supports backpressure, atomic counters, and frame dropping
 *
 * Template parameter T should be TSharedPtr<FrameData>
 */
template<typename T>
class REALGAZEBOSTREAMING_API TRealGazeboStreamQueue
{
public:
	/**
	 * Constructor
	 * @param InMaxSize Maximum queue size (0 = unlimited)
	 * @param InName Debug name for logging
	 */
	TRealGazeboStreamQueue(int32 InMaxSize = 10, const FString& InName = TEXT("StreamQueue"))
		: MaxQueueSize(InMaxSize)
		, QueueName(InName)
		, TotalEnqueued(0)
		, TotalDequeued(0)
		, TotalDropped(0)
		, CurrentDepth(0)
		, PeakDepth(0)
	{
	}

	~TRealGazeboStreamQueue()
	{
		Clear();
	}

	/**
	 * Enqueue item with backpressure support
	 * @param Item Item to enqueue
	 * @param bDropIfFull If true, drops item when queue is full. If false, returns false.
	 * @param bDropOldest If true and queue is full, drops oldest item to make room
	 * @return True if enqueued successfully, false if dropped or queue full
	 */
	bool Enqueue(const T& Item, bool bDropIfFull = true, bool bDropOldest = false)
	{
		FScopeLock Lock(&QueueMutex);

		// Check if queue is full
		if (MaxQueueSize > 0 && CurrentDepth.load(std::memory_order_relaxed) >= MaxQueueSize)
		{
			if (bDropOldest)
			{
				// Drop oldest item to make room
				T DroppedItem;
				Queue.Dequeue(DroppedItem);
				TotalDropped.fetch_add(1, std::memory_order_relaxed);
				CurrentDepth.fetch_sub(1, std::memory_order_relaxed);
			}
			else if (bDropIfFull)
			{
				// Drop new item
				TotalDropped.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			else
			{
				// Queue full, don't drop
				return false;
			}
		}

		// Enqueue item
		Queue.Enqueue(Item);
		TotalEnqueued.fetch_add(1, std::memory_order_relaxed);

		// Update depth tracking
		int32 NewDepth = CurrentDepth.fetch_add(1, std::memory_order_relaxed) + 1;
		UpdatePeakDepth(NewDepth);

		return true;
	}

	/**
	 * Dequeue item
	 * @param OutItem Receives dequeued item
	 * @return True if item was dequeued, false if queue empty
	 */
	bool Dequeue(T& OutItem)
	{
		FScopeLock Lock(&QueueMutex);

		if (Queue.Dequeue(OutItem))
		{
			TotalDequeued.fetch_add(1, std::memory_order_relaxed);
			CurrentDepth.fetch_sub(1, std::memory_order_relaxed);
			return true;
		}

		return false;
	}

	/**
	 * Clear all items from queue
	 */
	void Clear()
	{
		FScopeLock Lock(&QueueMutex);
		Queue.Empty();
		CurrentDepth.store(0, std::memory_order_relaxed);
	}

	/**
	 * Check if queue is empty
	 */
	bool IsEmpty() const
	{
		return CurrentDepth.load(std::memory_order_relaxed) == 0;
	}

	/**
	 * Get current queue depth (thread-safe)
	 */
	int32 GetDepth() const
	{
		return CurrentDepth.load(std::memory_order_relaxed);
	}

	/**
	 * Get peak queue depth since creation
	 */
	int32 GetPeakDepth() const
	{
		return PeakDepth.load(std::memory_order_relaxed);
	}

	/**
	 * Get max queue size
	 */
	int32 GetMaxSize() const
	{
		return MaxQueueSize;
	}

	/**
	 * Check if queue is experiencing backpressure (>75% full)
	 */
	bool IsBackpressured() const
	{
		if (MaxQueueSize <= 0) return false;
		return GetDepth() > (MaxQueueSize * 3 / 4);
	}

	/**
	 * Get total items enqueued (lifetime)
	 */
	uint64 GetTotalEnqueued() const
	{
		return TotalEnqueued.load(std::memory_order_relaxed);
	}

	/**
	 * Get total items dequeued (lifetime)
	 */
	uint64 GetTotalDequeued() const
	{
		return TotalDequeued.load(std::memory_order_relaxed);
	}

	/**
	 * Get total items dropped (lifetime)
	 */
	uint64 GetTotalDropped() const
	{
		return TotalDropped.load(std::memory_order_relaxed);
	}

	/**
	 * Get queue name
	 */
	const FString& GetName() const
	{
		return QueueName;
	}

	/**
	 * Reset statistics (does not clear queue)
	 */
	void ResetStats()
	{
		TotalEnqueued.store(0, std::memory_order_relaxed);
		TotalDequeued.store(0, std::memory_order_relaxed);
		TotalDropped.store(0, std::memory_order_relaxed);
		PeakDepth.store(CurrentDepth.load(std::memory_order_relaxed), std::memory_order_relaxed);
	}

	/**
	 * Get debug string with queue statistics
	 */
	FString GetDebugString() const
	{
		const int32 Depth = GetDepth();
		const float Utilization = (MaxQueueSize > 0) ? (static_cast<float>(Depth) / MaxQueueSize * 100.0f) : 0.0f;
		return FString::Printf(
			TEXT("%s: Depth=%d/%d (%.1f%%), Peak=%d, Enqueued=%llu, Dequeued=%llu, Dropped=%llu"),
			*QueueName,
			Depth,
			MaxQueueSize,
			Utilization,
			GetPeakDepth(),
			GetTotalEnqueued(),
			GetTotalDequeued(),
			GetTotalDropped()
		);
	}

private:
	/** Update peak depth if current is higher */
	void UpdatePeakDepth(int32 Depth)
	{
		int32 CurrentPeak = PeakDepth.load(std::memory_order_relaxed);
		while (Depth > CurrentPeak && !PeakDepth.compare_exchange_weak(CurrentPeak, Depth)) {}
	}

	/** Underlying lock-free queue */
	TQueue<T> Queue;

	/** Mutex for thread safety */
	mutable FCriticalSection QueueMutex;

	/** Maximum queue size (0 = unlimited) */
	int32 MaxQueueSize;

	/** Queue name for debugging */
	FString QueueName;

	/** Atomic counters */
	std::atomic<uint64> TotalEnqueued;
	std::atomic<uint64> TotalDequeued;
	std::atomic<uint64> TotalDropped;
	std::atomic<int32> CurrentDepth;
	std::atomic<int32> PeakDepth;
};
