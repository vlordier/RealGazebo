// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Core/RealGazeboStreamingTypes.h"

// Live555 forward declarations
class FramedSource;
class UsageEnvironment;

/**
 * H264 Live Video Source for Live555
 *
 * Custom FramedSource that provides H.264 NAL units from Unreal Engine
 * hardware encoder to Live555 RTSP server for streaming.
 *
 * Thread-safe frame delivery from encoding thread to RTSP thread.
 */
class REALGAZEBOSTREAMING_API FRealGazeboH264Source
{
public:
	/**
	 * Create new H264 live source for specific stream
	 * @param env Live555 usage environment
	 * @param StreamKey Stream identifier
	 * @return FramedSource instance
	 */
	static FramedSource* CreateNew(UsageEnvironment& env, const FStreamKey& StreamKey);

	/**
	 * Push frame data to stream (thread-safe)
	 * @param StreamKey Target stream
	 * @param FrameData H.264 encoded data (NAL units)
	 * @param Timestamp Frame timestamp
	 * @param bIsKeyFrame True if I-frame
	 */
	static void PushFrameData(const FStreamKey& StreamKey, const TArray<uint8>& FrameData,
	                          double Timestamp, bool bIsKeyFrame);


	/**
	 * Set maximum queue size for stream
	 * @param StreamKey Target stream
	 * @param MaxQueueSize Maximum number of frames to buffer
	 */
	static void SetMaxQueueSize(const FStreamKey& StreamKey, int32 MaxQueueSize);

protected:
	// Internal Live555 FramedSource implementation
	class FH264LiveFramedSource;

	// Individual NAL unit data (for per-NAL delivery to Live555)
	struct FNALUnitData
	{
		TArray<uint8> Data;         // NAL unit payload (Annex-B format with start code)
		double Timestamp = 0.0;     // Presentation timestamp
		bool bIsKeyFrame = false;   // True if this NAL is part of a keyframe
		uint8 NALType = 0;          // NAL unit type (1=P-slice, 5=I-slice, etc.)
		uint64 FrameNumber = 0;     // Frame this NAL belongs to

		FNALUnitData() = default;
		FNALUnitData(const TArray<uint8>& InData, double InTimestamp, bool bInIsKeyFrame,
		             uint8 InNALType, uint64 InFrameNumber)
			: Data(InData), Timestamp(InTimestamp), bIsKeyFrame(bInIsKeyFrame)
			, NALType(InNALType), FrameNumber(InFrameNumber)
		{}
	};

	// Per-stream NAL unit queue (delivers one NAL at a time to Live555)
	struct FStreamNALQueue
	{
		TQueue<TSharedPtr<FNALUnitData>> NALUnits;  // Queue of individual NAL units
		std::atomic<int32> CurrentSize{0};          // Current queue size (atomic)
		uint64 FrameCounter = 0;                    // Monotonic frame counter
		int32 MaxQueueSize = 300;                   // Max NAL units (set by StreamManager: FPS x 10)
		int64 NALsDropped = 0;                      // Stats: NALs dropped due to queue full

		// TQueue is not copyable - prevent accidental copies
		FStreamNALQueue() = default;
		FStreamNALQueue(const FStreamNALQueue&) = delete;
		FStreamNALQueue& operator=(const FStreamNALQueue&) = delete;
	};

	// Stream NAL queues (thread-safe access)
	// Using TSharedPtr because TQueue is not copyable
	static TMap<FStreamKey, TSharedPtr<FStreamNALQueue>> StreamNALQueues;
	static FCriticalSection NALQueueMutex;
};
