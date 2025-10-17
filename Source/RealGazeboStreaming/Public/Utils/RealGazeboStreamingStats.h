// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.
#pragma once

#include "CoreMinimal.h"
#include "RealGazeboStreamingStats.generated.h"

/**
 * Comprehensive streaming statistics
 * Tracks timing, queues, frames, bitrate, and memory
 */
USTRUCT(BlueprintType)
struct REALGAZEBOSTREAMING_API FStreamingStats
{
	GENERATED_BODY()

	// Timing (milliseconds)
	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	float CaptureTimeMs = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	float EncodingTimeMs = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	float RTSPTimeMs = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Performance")
	float TotalLatencyMs = 0.0f;

	// Queue depths (hardware-only: only encoding and RTSP queues)
	UPROPERTY(BlueprintReadOnly, Category = "Queues")
	int32 EncodingQueueDepth = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Queues")
	int32 RTSPQueueDepth = 0;

	// Frame counters (using int64 for Blueprint compatibility)
	UPROPERTY(BlueprintReadOnly, Category = "Frames")
	int64 TotalFramesCaptured = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Frames")
	int64 TotalFramesEncoded = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Frames")
	int64 TotalFramesDropped = 0;

	// Bitrate
	UPROPERTY(BlueprintReadOnly, Category = "Encoding")
	float CurrentBitrateMbps = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Encoding")
	float AverageBitrateMbps = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Encoding")
	int32 KeyFrameCount = 0;

	// Pool statistics
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

	/** Aggregate stats from another instance */
	void Aggregate(const FStreamingStats& Other)
	{
		// Average timing
		CaptureTimeMs = (CaptureTimeMs + Other.CaptureTimeMs) * 0.5f;
		EncodingTimeMs = (EncodingTimeMs + Other.EncodingTimeMs) * 0.5f;
		RTSPTimeMs = (RTSPTimeMs + Other.RTSPTimeMs) * 0.5f;
		TotalLatencyMs = (TotalLatencyMs + Other.TotalLatencyMs) * 0.5f;

		// Sum queue depths (hardware-only: no conversion queue)
		EncodingQueueDepth += Other.EncodingQueueDepth;
		RTSPQueueDepth += Other.RTSPQueueDepth;

		// Sum counters
		TotalFramesCaptured += Other.TotalFramesCaptured;
		TotalFramesEncoded += Other.TotalFramesEncoded;
		TotalFramesDropped += Other.TotalFramesDropped;

		// Average bitrate
		CurrentBitrateMbps = (CurrentBitrateMbps + Other.CurrentBitrateMbps) * 0.5f;
		AverageBitrateMbps = (AverageBitrateMbps + Other.AverageBitrateMbps) * 0.5f;
		KeyFrameCount += Other.KeyFrameCount;

		// Sum memory stats
		PooledFrameCount += Other.PooledFrameCount;
		ActiveFrameCount += Other.ActiveFrameCount;
		EstimatedMemoryMB += Other.EstimatedMemoryMB;
	}
};
