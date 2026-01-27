// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
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
 * Complete isolated streaming pipeline for a single camera stream.
 * Orchestrates the full video pipeline: Capture -> Pool -> Encode -> NAL -> RTSP
 *
 * CRITICAL DESIGN: Complete Per-Stream Isolation
 * Each stream has its OWN dedicated instances of ALL components:
 * - Frame Pool: Independent GPU texture pool (no sharing between streams)
 * - Hardware Encoder: Dedicated NVENC/AMF instance (prevents state pollution)
 * - Encoding Thread: Isolated background thread
 * - NAL Queue: Separate H.264 packet queue (prevents frame crosstalk)
 * - GPU Fence: Independent synchronization primitive
 *
 * This isolation architecture is the FIX for stream crosstalk issues.
 * Streams never share state or resources except the RTSP server itself.
 *
 * Pipeline Components (All Per-Instance):
 * 1. FFramePool - Reusable GPU texture pool (~4 textures, ~130ms buffer)
 * 2. FFrameCapture - Scene rendering to GPU texture with fence synchronization
 * 3. FHardwareEncoderWrapper - NVENC (NVIDIA) or AMF (AMD) hardware encoder
 * 4. FEncodingThread - Background worker thread for encoding operations
 * 5. FH264StreamSource - Live555 RTSP source with NAL unit queue
 *
 * Lifecycle:
 * 1. Initialize() - Create and configure all components
 * 2. Start() - Begin encoding loop and register with RTSP server
 * 3. CaptureFrame() - Called every frame from game thread to capture scenes
 * 4. Stop() - Stop encoding loop and unregister from RTSP server
 * 5. Shutdown() - Clean up and release all resources
 */
class FStreamingPipeline
{
public:
	//----------------------------------------------------------
	// Construction & Initialization
	//----------------------------------------------------------

	/**
	 * Constructor - Creates an isolated pipeline for one camera stream.
	 *
	 * @param InStreamID - Unique stream identifier (Vehicle + Camera)
	 * @param InSceneCapture - Unreal Engine scene capture component for rendering
	 * @param InRTSPServer - Shared RTSP server instance (pipelines are isolated, server is shared)
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
	// Pipeline Components
	// CRITICAL: ALL components are PER-INSTANCE - NEVER SHARED between streams!
	//----------------------------------------------------------

	/**
	 * Frame Pool (Per-Stream)
	 * GPU texture pool for this stream only. Maintains ~4 reusable textures to reduce allocation overhead.
	 * Uses TSharedPtr (not TUniquePtr) because FFrameCapture and FEncodingThread need shared access
	 * for coordinated frame release after encoding completes.
	 */
	TSharedPtr<FFramePool> FramePool;

	/**
	 * Frame Capture (Per-Stream)
	 * Renders scene to GPU texture with dedicated GPU fence for synchronization.
	 * Each stream has independent capture instance to prevent interference.
	 */
	TUniquePtr<FFrameCapture> FrameCapture;

	/**
	 * Hardware Encoder (Per-Stream)
	 * Dedicated NVENC (NVIDIA) or AMF (AMD) encoder instance.
	 * Never shared between streams to prevent encoder state pollution and crosstalk.
	 * Uses TSharedPtr for safe access from encoding thread.
	 */
	TSharedPtr<FHardwareEncoderWrapper> HardwareEncoder;

	/**
	 * Encoding Thread (Per-Stream)
	 * Isolated background worker thread for encoding operations.
	 * Each stream has its own thread to enable parallel encoding across multiple streams.
	 */
	TUniquePtr<FEncodingThread> EncodingThread;

	/**
	 * H.264 Stream Source (Per-Stream)
	 * Live555 RTSP source with dedicated NAL unit queue.
	 * CRITICAL: Each stream has its own queue (NOT a static shared queue) to prevent frame crosstalk.
	 */
	TUniquePtr<FH264StreamSource> H264Source;

	//----------------------------------------------------------
	// State
	//----------------------------------------------------------

	/** Is pipeline initialized? */
	std::atomic<bool> bInitialized{false};

	/** Is pipeline streaming? */
	std::atomic<bool> bStreaming{false};

	/** Is pipeline shutting down? Used to prevent callbacks during cleanup */
	std::atomic<bool> bShuttingDown{false};

	/** Mutex to protect H264Source access during callbacks */
	mutable FCriticalSection H264SourceMutex;

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
