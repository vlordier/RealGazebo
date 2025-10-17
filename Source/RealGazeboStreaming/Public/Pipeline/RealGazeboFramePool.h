// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Pipeline/RealGazeboFrameData.h"

/**
 * Frame buffer pooling system (HARDWARE ENCODING ONLY)
 * Recycles H.264 encoded frame buffers to minimize allocations
 * Thread-safe for multi-threaded streaming pipeline
 *
 * Note: Raw/YUV frame pooling removed - zero-copy GPU texture encoding only
 */
class REALGAZEBOSTREAMING_API FRealGazeboFramePool
{
public:
	/**
	 * Constructor
	 * @param InMaxPoolSize Maximum number of frames to keep in pool
	 */
	explicit FRealGazeboFramePool(int32 InMaxPoolSize = 20);

	~FRealGazeboFramePool();

	/**
	 * Acquire encoded frame from pool (or create new)
	 * @param FrameNumber Frame sequence number
	 * @param bIsKeyFrame Is this a keyframe
	 * @return Shared pointer to frame data
	 */
	TSharedPtr<FEncodedFrameData> AcquireEncodedFrame(uint64 FrameNumber, bool bIsKeyFrame);

	/**
	 * Release encoded frame back to pool
	 * @param Frame Frame to release (will be reset)
	 */
	void ReleaseEncodedFrame(TSharedPtr<FEncodedFrameData> Frame);

	/**
	 * Clear all pooled frames (releases memory)
	 */
	void ClearPool();

	/**
	 * Shrink pool to max size (remove excess frames)
	 */
	void ShrinkPool();

	/**
	 * Get pool statistics
	 */
	void GetPoolStats(int32& OutEncodedPooled, int32& OutEncodedActive, float& OutEstimatedMemoryMB) const;

	/**
	 * Get total active frames
	 */
	int32 GetTotalActiveFrames() const;

	/**
	 * Get total pooled frames
	 */
	int32 GetTotalPooledFrames() const;

	/**
	 * Get estimated memory usage in MB
	 */
	float GetEstimatedMemoryMB() const;

	/**
	 * Get debug string with pool statistics
	 */
	FString GetDebugString() const;

private:
	/** Maximum frames to keep in pool */
	int32 MaxPoolSize;

	/** Pooled encoded frames (resolution-independent) */
	TArray<TSharedPtr<FEncodedFrameData>> EncodedFramePool;

	/** Active frame counter */
	std::atomic<int32> ActiveEncodedFrames;

	/** Statistics */
	std::atomic<uint64> TotalEncodedFramesCreated;
	std::atomic<uint64> TotalEncodedFramesReused;

	/** Thread safety */
	mutable FCriticalSection EncodedPoolMutex;
};
