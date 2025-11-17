// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.
#pragma once

#include "CoreMinimal.h"
#include "RealGazeboStreamingStats.generated.h"

/**
 * Streaming statistics for monitoring performance
 * Tracks queues, frames, bitrate, and memory usage
 */
USTRUCT(BlueprintType)
struct REALGAZEBOSTREAMING_API FStreamingStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Queues")
	int32 EncodingQueueDepth = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Queues")
	int32 RTSPQueueDepth = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Queues")
	bool bIsBackpressured = false;

	UPROPERTY(BlueprintReadOnly, Category = "Frames")
	int64 TotalFramesEncoded = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Encoding")
	float CurrentBitrateMbps = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Encoding")
	float AverageBitrateMbps = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Encoding")
	int32 KeyFrameCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Memory")
	int32 PooledFrameCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Memory")
	int32 ActiveFrameCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Memory")
	float EstimatedMemoryMB = 0.0f;

	FStreamingStats()
	{
	}

	/** Reset all statistics */
	void Reset()
	{
		FMemory::Memzero(this, sizeof(FStreamingStats));
	}
};
