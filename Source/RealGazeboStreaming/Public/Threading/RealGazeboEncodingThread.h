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
#include "Pipeline/RealGazeboFramePool.h"
#include "Core/RealGazeboStreamingTypes.h"
#include "RHI.h"
#include "RHIResources.h"
#include <atomic>

// Forward declarations
class IRealGazeboHardwareEncoder;
class FRealGazeboRTSPThread;
class FRealGazeboRTSPServer;

// FTextureFrameData is defined in Pipeline/RealGazeboFrameData.h

/**
 * Hardware Encoding Thread (HARDWARE ONLY)
 *
 * Dedicated thread that performs H.264 hardware encoding via NVENC/AMF
 * Zero-copy GPU texture encoding - no CPU readback or conversion
 *
 * Flow:
 * 1. Capture thread pushes GPU textures directly to encoding queue
 * 2. Encoding thread dequeues textures (up to MAX_BATCH_SIZE per tick)
 * 3. Textures are encoded using hardware encoder (NVENC/AMF) with zero-copy
 * 4. Encoded H.264 frames are pushed to RTSP thread queue
 * 5. Textures are released (RHI handles cleanup)
 *
 * Performance:
 * - Thread Priority: Highest (time-critical for low latency)
 * - Batch Processing: Up to 4 frames per tick for GPU efficiency
 * - Target: <3ms encode time per frame (hardware accelerated)
 * - Minimal blocking with 1ms sleep when idle
 */
class REALGAZEBOSTREAMING_API FRealGazeboEncodingThread : public FRunnable
{
public:
	/**
	 * Constructor
	 * @param InFramePool Shared frame pool for memory management
	 * @param InRTSPThread RTSP thread for encoded frame delivery
	 * @param InRTSPServer RTSP server for SPS/PPS configuration
	 */
	FRealGazeboEncodingThread(TSharedPtr<FRealGazeboFramePool> InFramePool,
	                          TSharedPtr<FRealGazeboRTSPThread> InRTSPThread,
	                          TSharedPtr<FRealGazeboRTSPServer> InRTSPServer);
	virtual ~FRealGazeboEncodingThread();

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
	 * Register encoder for stream
	 * @param StreamKey Stream identifier
	 * @param Encoder Hardware encoder instance
	 * @param BitrateKbps Encoder bitrate in kbps (for RTSP SDP)
	 * @return True if registered successfully
	 */
	bool RegisterEncoder(const FStreamKey& StreamKey, TSharedPtr<IRealGazeboHardwareEncoder> Encoder, int32 BitrateKbps);

	/**
	 * Unregister encoder for stream
	 * @param StreamKey Stream to unregister
	 */
	void UnregisterEncoder(const FStreamKey& StreamKey);

	/**
	 * Enqueue GPU texture for encoding (hardware-only)
	 * @param StreamKey Target stream
	 * @param Texture GPU texture from RenderTarget
	 * @param Timestamp Frame capture timestamp
	 * @param FrameNumber Frame sequence number
	 * @return True if enqueued successfully
	 */
	bool EnqueueTextureFrame(const FStreamKey& StreamKey, FTexture2DRHIRef Texture,
	                         int64 TimestampUs, uint64 FrameNumber);

	/**
	 * Request keyframe for next encode
	 * @param StreamKey Target stream
	 */
	void RequestKeyFrame(const FStreamKey& StreamKey);

	/**
	 * Update bitrate for stream
	 * @param StreamKey Target stream
	 * @param NewBitrateKbps New bitrate
	 */
	void UpdateBitrate(const FStreamKey& StreamKey, int32 NewBitrateKbps);

	/**
	 * Check if stream encoder supports GPU texture input
	 * @param StreamKey Stream to check
	 * @return True if texture encoding supported
	 */
	bool SupportsTextureEncoding(const FStreamKey& StreamKey) const;

	/**
	 * Get statistics for stream
	 * @param StreamKey Stream to query
	 * @param OutQueueDepth Current queue depth
	 * @param OutFramesEncoded Total frames encoded
	 * @param OutFramesDropped Total frames dropped
	 * @param OutAvgEncodeTimeMs Average encode time
	 */
	void GetStreamStatistics(const FStreamKey& StreamKey, int32& OutQueueDepth,
	                         int64& OutFramesEncoded, int64& OutFramesDropped,
	                         float& OutAvgEncodeTimeMs) const;

private:
	/** Batch processing configuration */
	static constexpr int32 MAX_BATCH_SIZE = 4;
	static constexpr float IDLE_SLEEP_TIME = 0.001f; // 1ms

	/** Per-stream encoder (hardware only) */
	struct FStreamEncoder
	{
		TSharedPtr<IRealGazeboHardwareEncoder> Encoder;

		// GPU texture encoding queue (zero-copy)
		TRealGazeboStreamQueue<TSharedPtr<FTextureFrameData>> TextureQueue;

		std::atomic<bool> bRequestKeyFrame{false};
		std::atomic<bool> bSupportsTextureEncoding{false};
		std::atomic<bool> bSPSPPSSet{false};  // Track if SPS/PPS has been set for this stream
		std::atomic<int64> FramesEncoded{0};
		std::atomic<int64> FramesDropped{0};
		std::atomic<int32> BitrateKbps{3250};  // Cached bitrate for RTSP SDP (default: 720p@30fps)

		// Thread-safe statistics with mutex protection
		mutable FCriticalSection StatsMutex;
		double TotalEncodeTime = 0.0;  // Protected by StatsMutex
		int32 EncodeCount = 0;         // Protected by StatsMutex
	};

	/** Frame pool for memory management */
	TSharedPtr<FRealGazeboFramePool> FramePool;

	/** RTSP thread for output */
	TSharedPtr<FRealGazeboRTSPThread> RTSPThread;

	/** RTSP server for SPS/PPS configuration */
	TSharedPtr<FRealGazeboRTSPServer> RTSPServer;

	/** Per-stream encoders */
	TMap<FStreamKey, TSharedPtr<FStreamEncoder>> StreamEncoders;
	mutable FCriticalSection EncoderMapMutex;

	/** Thread control */
	FRunnableThread* ThreadHandle = nullptr;
	std::atomic<bool> bIsRunning{false};
	std::atomic<bool> bStopRequested{false};

	/** Internal methods */
	int32 ProcessEncodingQueues();  // Returns number of frames processed
	int32 ProcessStreamEncoder(const FStreamKey& StreamKey, FStreamEncoder& StreamEncoder);

	// Hardware texture encoding
	bool EncodeTextureFrame(const FStreamKey& StreamKey, FStreamEncoder& StreamEncoder,
	                        TSharedPtr<FTextureFrameData> TextureFrame,
	                        TSharedPtr<FEncodedFrameData>& OutEncodedFrame);

	TSharedPtr<FStreamEncoder> GetStreamEncoder(const FStreamKey& StreamKey) const;
};
