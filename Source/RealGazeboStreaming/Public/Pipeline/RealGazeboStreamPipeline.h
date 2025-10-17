// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Core/RealGazeboStreamingTypes.h"
#include "Core/RealGazeboStreamConfig.h"
#include "Utils/RealGazeboStreamingStats.h"
#include "Pipeline/RealGazeboFrameData.h"
#include "Pipeline/RealGazeboStreamQueue.h"

// Forward declarations
class FRealGazeboFramePool;
class URealGazeboStreamingCamera;

/**
 * Per-stream pipeline management
 * Owns queues, tracks state, coordinates multi-threaded processing
 * Thread-safe for access from game thread and worker threads
 */
class REALGAZEBOSTREAMING_API FRealGazeboStreamPipeline : public TSharedFromThis<FRealGazeboStreamPipeline>
{
public:
	/**
	 * Constructor
	 * @param InStreamKey Unique stream identifier
	 * @param InConfig Stream configuration
	 * @param InFramePool Shared frame buffer pool
	 */
	FRealGazeboStreamPipeline(const FStreamKey& InStreamKey,
	                          const FRealGazeboStreamConfig& InConfig,
	                          TSharedPtr<FRealGazeboFramePool> InFramePool);

	~FRealGazeboStreamPipeline();

	// ========================================
	// Lifecycle
	// ========================================

	/** Start streaming pipeline */
	bool Start();

	/** Stop streaming pipeline */
	void Stop();

	/** Pause streaming (retains queues) */
	void Pause();

	/** Resume streaming */
	void Resume();

	/** Check if pipeline is active */
	bool IsActive() const;

	/** Get current state */
	EStreamState GetState() const;

	// ========================================
	// Frame Processing (called by threads)
	// ========================================

	/**
	 * Submit encoded frame after encoding (called by encoding thread)
	 * Hardware-only: GPU textures are encoded directly by encoding thread
	 * @param Frame Encoded H.264 frame data
	 * @return True if enqueued successfully
	 */
	bool SubmitEncodedFrame(TSharedPtr<FEncodedFrameData> Frame);

	/**
	 * Get next encoded frame for RTSP (called by RTSP thread)
	 * @param OutFrame Receives frame data
	 * @return True if frame available
	 */
	bool GetNextEncodedFrame(TSharedPtr<FEncodedFrameData>& OutFrame);

	// ========================================
	// Configuration
	// ========================================

	/** Get stream key */
	const FStreamKey& GetStreamKey() const { return StreamKey; }

	/** Get stream configuration */
	const FRealGazeboStreamConfig& GetConfig() const { return Config; }

	/** Update configuration (must be stopped) */
	bool UpdateConfig(const FRealGazeboStreamConfig& NewConfig);

	// ========================================
	// Statistics
	// ========================================

	/** Get current statistics */
	FStreamingStats GetStats() const;

	/** Reset statistics */
	void ResetStats();

	/** Update statistics (called periodically) */
	void UpdateStats();

	// ========================================
	// Adaptive Quality
	// ========================================

	/** Check if backpressure is detected */
	bool IsBackpressured() const;

	/** Get recommended bitrate adjustment factor (0.5-1.5) */
	float GetAdaptiveQualityFactor() const;

	/** Check if frame should be dropped (adaptive quality) */
	bool ShouldDropFrame(bool bIsKeyFrame) const;

	// ========================================
	// Queue Access (for monitoring)
	// ========================================

	/** Get RTSP queue depth (hardware-only: encoding queue managed by encoding thread) */
	int32 GetRTSPQueueDepth() const { return RTSPQueue.GetDepth(); }

	/** Get frame pool reference */
	TSharedPtr<FRealGazeboFramePool> GetFramePool() const { return FramePool; }

private:
	/** Stream identifier */
	FStreamKey StreamKey;

	/** Stream configuration */
	FRealGazeboStreamConfig Config;

	/** Shared frame pool */
	TSharedPtr<FRealGazeboFramePool> FramePool;

	/** Current state */
	std::atomic<EStreamState> CurrentState;

	/** Frame sequence counter */
	std::atomic<uint64> FrameSequence;

	// ========================================
	// Processing Queues
	// ========================================

	/** Encoded frame queue (Encoding thread → RTSP thread) */
	/** Note: Hardware encoding uses direct GPU texture → Encoder path (zero-copy) */
	/** Texture queue is managed by encoding thread per-stream encoder */
	TRealGazeboStreamQueue<TSharedPtr<FEncodedFrameData>> RTSPQueue;

	// ========================================
	// Statistics
	// ========================================

	/** Current statistics */
	FStreamingStats Stats;

	/** Statistics mutex */
	mutable FCriticalSection StatsMutex;

	/** Last stats update time */
	double LastStatsUpdateTime;

	/** Backpressure start time (for adaptive quality) */
	double BackpressureStartTime;

	/** Adaptive quality reduction factor */
	std::atomic<float> AdaptiveQualityFactor;
};
