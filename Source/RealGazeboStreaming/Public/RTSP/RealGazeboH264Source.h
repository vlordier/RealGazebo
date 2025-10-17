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
	 * Check if frame data is available for stream
	 * @param StreamKey Stream to query
	 * @return True if frame available
	 */
	static bool HasFrameData(const FStreamKey& StreamKey);

	/**
	 * Clear all frame buffers (on shutdown)
	 */
	static void ClearAllFrameBuffers();

protected:
	// Internal Live555 FramedSource implementation
	class FH264LiveFramedSource;

	// Per-stream frame buffer data
	struct FFrameBuffer
	{
		TArray<uint8> Data;
		double Timestamp = 0.0;
		bool bIsKeyFrame = false;
		bool bHasNewFrame = false;
	};

	// Stream frame buffers (thread-safe access)
	static TMap<FStreamKey, FFrameBuffer> StreamFrameBuffers;
	static FCriticalSection FrameBufferMutex;
};
