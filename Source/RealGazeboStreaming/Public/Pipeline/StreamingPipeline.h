// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "StreamingTypes.h"
#include "EncoderConfig.h"
#include "FramePool.h"
#include "FrameCapture.h"
#include "HardwareEncoderWrapper.h"
#include "EncodingThread.h"
#include "H264StreamSource.h"

// Forward declarations
class USceneCaptureComponent2D;
class FRTSPServerWrapper;

/**
 * FStreamingPipeline
 *
 * Complete isolated streaming pipeline for ONE stream.
 * Orchestrates: Capture → Pool → Encode → NAL → RTSP
 *
 * CRITICAL: Each stream has its OWN pipeline instance!
 *
 * This is the FIX for stream crosstalk:
 * - Each pipeline has its OWN frame pool (not shared)
 * - Each pipeline has its OWN encoder instance (not shared)
 * - Each pipeline has its OWN encoding thread (not shared)
 * - Each pipeline has its OWN NAL queue (not shared)
 * - Each pipeline has its OWN GPU fence (not shared)
 *
 * Components (All Per-Instance):
 * 1. FFramePool - GPU texture pool (3 frames)
 * 2. FFrameCapture - Captures scene to GPU texture
 * 3. FHardwareEncoderWrapper - NVENC/AMF encoder
 * 4. FEncodingThread - Background encoding thread
 * 5. FH264StreamSource - Live555 NAL source
 *
 * Lifecycle:
 * 1. Initialize() - Creates all components
 * 2. Start() - Begins encoding loop
 * 3. CaptureFrame() - Called every game tick
 * 4. Stop() - Stops encoding
 * 5. Shutdown() - Cleans up resources
 */
class FStreamingPipeline
{
public:
	//----------------------------------------------------------
	// Construction & Initialization
	//----------------------------------------------------------

	/**
	 * Constructor
	 *
	 * @param InStreamID - Unique stream identifier
	 * @param InSceneCapture - UE scene capture component
	 * @param InRTSPServer - Shared RTSP server (streams are isolated, server is shared)
	 */
	FStreamingPipeline(const FStreamIdentifier& InStreamID,
	                   USceneCaptureComponent2D* InSceneCapture,
	                   TSharedPtr<FRTSPServerWrapper> InRTSPServer);

	/** Destructor */
	~FStreamingPipeline();

	/**
	 * Initialize pipeline with configuration.
	 *
	 * @param Config - Stream configuration
	 * @param OutErrorMessage - Error message if failed
	 * @return True if initialized successfully
	 */
	bool Initialize(const FStreamConfig& Config, FString& OutErrorMessage);

	/** Shutdown pipeline and release all resources */
	void Shutdown();

	//----------------------------------------------------------
	// Stream Control
	//----------------------------------------------------------

	/**
	 * Start streaming.
	 * Begins encoding and adds stream to RTSP server.
	 *
	 * @param OutRTSPURL - Generated RTSP URL
	 * @return True if started successfully
	 */
	bool Start(FString& OutRTSPURL);

	/** Stop streaming */
	void Stop();

	//----------------------------------------------------------
	// Frame Capture
	//----------------------------------------------------------

	/**
	 * Capture current frame from scene capture.
	 * Call this every frame from game thread.
	 *
	 * @return True if frame captured successfully
	 */
	bool CaptureFrame();

	//----------------------------------------------------------
	// Configuration
	//----------------------------------------------------------

	/**
	 * Update stream configuration (resolution, FPS, etc.)
	 * Can be called while streaming (will restart encoder).
	 *
	 * @param NewConfig - New configuration
	 * @param OutErrorMessage - Error message if failed
	 * @return True if updated successfully
	 */
	bool UpdateConfiguration(const FStreamConfig& NewConfig, FString& OutErrorMessage);

	//----------------------------------------------------------
	// Status
	//----------------------------------------------------------

	/** Is pipeline initialized? */
	bool IsInitialized() const { return bInitialized; }

	/** Is pipeline actively streaming? */
	bool IsStreaming() const { return bStreaming; }

	/** Get stream identifier */
	const FStreamIdentifier& GetStreamID() const { return StreamID; }

	/** Get current configuration */
	const FStreamConfig& GetConfig() const { return Config; }

	/** Get current encoder type */
	EEncoderType GetEncoderType() const;

	/** Get RTSP URL */
	FString GetRTSPURL() const { return RTSPURL; }

	/** Get stream info (for UI/debugging) */
	FStreamInfo GetStreamInfo() const;

	/** Get statistics string */
	FString GetStatsString() const;

private:
	//----------------------------------------------------------
	// Internal Initialization
	//----------------------------------------------------------

	/** Initialize all pipeline components */
	bool InitializeComponents(FString& OutErrorMessage);

	/** Cleanup all components */
	void CleanupComponents();

	//----------------------------------------------------------
	// Callbacks
	//----------------------------------------------------------

	/** Called when encoding thread produces NAL units */
	void OnNALUnitsEncoded(const TArray<FEncodedNALUnit>& NALUnits);

	//----------------------------------------------------------
	// Configuration
	//----------------------------------------------------------

	/** Stream identifier (VehicleID + CameraID) */
	FStreamIdentifier StreamID;

	/** Stream configuration */
	FStreamConfig Config;

	/** Encoder configuration (derived from Config) */
	FEncoderConfig EncoderConfig;

	/** Scene capture component (not owned) */
	TWeakObjectPtr<USceneCaptureComponent2D> SceneCapture;

	/** RTSP server (shared across streams, but streams are isolated) */
	TSharedPtr<FRTSPServerWrapper> RTSPServer;

	/** Generated RTSP URL */
	FString RTSPURL;

	//----------------------------------------------------------
	// Pipeline Components (ALL PER-INSTANCE - NOT SHARED!)
	//----------------------------------------------------------

	/**
	 * PER-STREAM frame pool
	 * Each stream has its own isolated GPU texture pool.
	 * NOTE: Must be TSharedPtr (not TUniquePtr) because it's shared with
	 * FFrameCapture and FEncodingThread for frame release after encoding.
	 */
	TSharedPtr<FFramePool> FramePool;

	/**
	 * PER-STREAM frame capture
	 * Each stream has its own capture instance with isolated GPU fence.
	 */
	TUniquePtr<FFrameCapture> FrameCapture;

	/**
	 * PER-STREAM hardware encoder
	 * Each stream has its own NVENC/AMF encoder instance.
	 */
	TSharedPtr<FHardwareEncoderWrapper> HardwareEncoder;

	/**
	 * PER-STREAM encoding thread
	 * Each stream has its own background thread.
	 */
	TUniquePtr<FEncodingThread> EncodingThread;

	/**
	 * PER-STREAM H.264 source
	 * Each stream has its own NAL queue (not static shared!).
	 */
	TUniquePtr<FH264StreamSource> H264Source;

	//----------------------------------------------------------
	// State
	//----------------------------------------------------------

	/** Is pipeline initialized? */
	std::atomic<bool> bInitialized{false};

	/** Is pipeline streaming? */
	std::atomic<bool> bStreaming{false};

	/** Frame counter */
	std::atomic<uint64> FrameCounter{0};

	/** Timestamp when stream started */
	FDateTime StartTime;

	//----------------------------------------------------------
	// Statistics
	//----------------------------------------------------------

	/** Total frames captured */
	std::atomic<uint64> TotalFramesCaptured{0};

	/** Total capture failures */
	std::atomic<uint64> CaptureFailureCount{0};

	/** Last capture timestamp */
	double LastCaptureTime = 0.0;
};
