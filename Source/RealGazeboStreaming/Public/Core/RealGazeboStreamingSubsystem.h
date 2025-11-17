// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Core/RealGazeboStreamingTypes.h"
#include "Utils/RealGazeboStreamingStats.h"
#include "RealGazeboStreamingSubsystem.generated.h"

// Forward declarations
class FRealGazeboStreamPipeline;
class FRealGazeboFramePool;
class FRealGazeboEncodingThread;
class FRealGazeboRTSPThread;
class FRealGazeboRTSPServer;
class URealGazeboStreamingCamera;
struct FVehicleID;

/**
 * RealGazebo Streaming Subsystem
 * GameInstanceSubsystem that manages all streaming operations
 * Persists across level changes
 */
UCLASS()
class REALGAZEBOSTREAMING_API URealGazeboStreamingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem Interface

	/**
	 * Get subsystem instance from world context
	 * @param WorldContext Any UObject with world context
	 * @return Subsystem instance or nullptr
	 */
	UFUNCTION(BlueprintPure, Category = "RealGazebo|Streaming", meta = (WorldContext = "WorldContextObject"))
	static URealGazeboStreamingSubsystem* GetStreamingSubsystem(const UObject* WorldContextObject);

	// ========================================
	// Camera Registration
	// ========================================

	/**
	 * Register camera for streaming
	 * @param Camera Camera component to register
	 * @param Config Stream configuration
	 * @return True if registered successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo|Streaming")
	bool RegisterCamera(URealGazeboStreamingCamera* Camera, const FRealGazeboStreamConfig& Config);

	/**
	 * Unregister camera from streaming
	 * @param Camera Camera component to unregister
	 */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo|Streaming")
	void UnregisterCamera(URealGazeboStreamingCamera* Camera);

	/**
	 * Check if camera is registered
	 * @param StreamKey Stream key to check
	 * @return True if camera is registered
	 */
	UFUNCTION(BlueprintPure, Category = "RealGazebo|Streaming")
	bool IsCameraRegistered(const FStreamKey& StreamKey) const;

	// ========================================
	// Stream Control
	// ========================================

	/**
	 * Start stream for specific camera
	 * @param StreamKey Stream key to start
	 * @return True if started successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo|Streaming")
	bool StartStream(const FStreamKey& StreamKey);

	/**
	 * Stop stream for specific camera
	 * @param StreamKey Stream key to stop
	 */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo|Streaming")
	void StopStream(const FStreamKey& StreamKey);

	/**
	 * Start all registered streams
	 */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo|Streaming")
	void StartAllStreams();

	/**
	 * Stop all active streams
	 */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo|Streaming")
	void StopAllStreams();

	/**
	 * Get stream state
	 * @param StreamKey Stream key to query
	 * @return Current stream state
	 */
	UFUNCTION(BlueprintPure, Category = "RealGazebo|Streaming")
	EStreamState GetStreamState(const FStreamKey& StreamKey) const;

	/**
	 * Get number of active streams
	 */
	UFUNCTION(BlueprintPure, Category = "RealGazebo|Streaming")
	int32 GetActiveStreamCount() const;

	// ========================================
	// Statistics
	// ========================================

	/**
	 * Get statistics for specific stream
	 * @param StreamKey Stream key to query
	 * @param OutStats Receives stream statistics
	 * @return True if stream exists and stats were retrieved
	 */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo|Streaming")
	bool GetStreamStats(const FStreamKey& StreamKey, FStreamingStats& OutStats) const;

	/**
	 * Get aggregated statistics across all streams
	 * @param OutStats Receives aggregated statistics
	 */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo|Streaming")
	void GetAggregatedStats(FStreamingStats& OutStats) const;

	/**
	 * Get all stream keys
	 */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo|Streaming")
	TArray<FStreamKey> GetAllStreamKeys() const;

	// ========================================
	// RTSP Server
	// ========================================

	/**
	 * Get RTSP URL for stream
	 * @param StreamKey Stream key to query
	 * @return RTSP URL (e.g., "rtsp://localhost:8554/iris_0/fpv")
	 */
	UFUNCTION(BlueprintPure, Category = "RealGazebo|Streaming")
	FString GetRTSPURL(const FStreamKey& StreamKey) const;

	/**
	 * Get RTSP server port
	 */
	UFUNCTION(BlueprintPure, Category = "RealGazebo|Streaming")
	int32 GetRTSPPort() const;

	/**
	 * Set RTSP server port (must be called before RTSP server starts)
	 * @param Port New RTSP port (1024-65535)
	 * @return True if port was set successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo|Streaming")
	bool SetRTSPPort(int32 Port);

	/**
	 * Check if RTSP server is running
	 */
	UFUNCTION(BlueprintPure, Category = "RealGazebo|Streaming")
	bool IsRTSPServerRunning() const;

	// ========================================
	// Configuration
	// ========================================

	/**
	 * Update stream configuration
	 * @param StreamKey Stream to update
	 * @param NewConfig New configuration
	 * @return True if updated successfully (stream must be stopped)
	 */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo|Streaming")
	bool UpdateStreamConfig(const FStreamKey& StreamKey, const FRealGazeboStreamConfig& NewConfig);

	/**
	 * Get stream configuration
	 * @param StreamKey Stream to query
	 * @param OutConfig Receives configuration
	 * @return True if stream exists
	 */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo|Streaming")
	bool GetStreamConfig(const FStreamKey& StreamKey, FRealGazeboStreamConfig& OutConfig) const;

	// ========================================
	// Internal API (C++ only)
	// ========================================

	/** Get shared frame pool */
	TSharedPtr<FRealGazeboFramePool> GetFramePool() const { return FramePool; }

	/** Get scene capture component pool for reusable capture components */
	TSharedPtr<class FRealGazeboSceneCapturePool> GetSceneCapturePool() const { return SceneCapturePool; }

	/** Get pipeline for stream key */
	TSharedPtr<FRealGazeboStreamPipeline> GetPipeline(const FStreamKey& StreamKey) const;

	/**
	 * Check if stream supports GPU texture encoding (zero-copy path)
	 * @param StreamKey Stream to check
	 * @return True if encoder supports direct GPU texture input
	 */
	bool SupportsTextureEncoding(const FStreamKey& StreamKey) const;

	/**
	 * Check if stream is experiencing backpressure (considers both encoding and RTSP queues)
	 * @param StreamKey Stream to check
	 * @return True if either encoding or RTSP queue is >75% full
	 */
	bool IsStreamBackpressured(const FStreamKey& StreamKey) const;

	/**
	 * Submit GPU texture frame directly to encoding thread (zero-copy path)
	 * Only use if SupportsTextureEncoding() returns true
	 * @param StreamKey Target stream
	 * @param Texture GPU texture from RenderTarget
	 * @param Timestamp Frame capture timestamp
	 * @param FrameNumber Frame sequence number
	 * @return True if enqueued successfully
	 */
	bool SubmitTextureFrame(const FStreamKey& StreamKey, FTexture2DRHIRef Texture,
	                        int64 TimestampUs, uint64 FrameNumber);

	/**
	 * Update bitrate for stream dynamically (adaptive quality)
	 * @param StreamKey Target stream
	 * @param NewBitrateKbps New bitrate in kbps
	 */
	void UpdateStreamBitrate(const FStreamKey& StreamKey, int32 NewBitrateKbps);

	/**
	 * Request keyframe (I-frame) for stream
	 * @param StreamKey Target stream
	 */
	void RequestKeyFrame(const FStreamKey& StreamKey);

private:
	/** Initialize worker threads */
	void InitializeThreads();

	/** Shutdown worker threads */
	void ShutdownThreads();

	/** Initialize RTSP server */
	bool InitializeRTSPServer();

	/** Shutdown RTSP server */
	void ShutdownRTSPServer();

	/** Validate stream configuration */
	bool ValidateStreamConfig(const FRealGazeboStreamConfig& Config, FString& OutErrorMessage) const;

	/** Generate RTSP URL for stream key */
	FString GenerateRTSPURL(const FStreamKey& StreamKey) const;

	/** Update pool capacities based on active camera/stream count */
	void UpdatePoolCapacities();

	// ========================================
	// Member Variables
	// ========================================

	/** Active stream pipelines (key = FStreamKey) */
	TMap<FStreamKey, TSharedPtr<FRealGazeboStreamPipeline>> ActivePipelines;

	/** Shared frame buffer pool */
	TSharedPtr<FRealGazeboFramePool> FramePool;

	/** SceneCapture2D component pool for reducing allocation overhead */
	TSharedPtr<class FRealGazeboSceneCapturePool> SceneCapturePool;

	/** Encoding thread (hardware encoding only) */
	TSharedPtr<FRealGazeboEncodingThread> EncodingThread;
	FRunnableThread* EncodingThreadHandle = nullptr;

	/** RTSP thread (runs Live555 event loop) */
	TSharedPtr<FRealGazeboRTSPThread> RTSPThread;
	FRunnableThread* RTSPThreadHandle = nullptr;

	/** RTSP server instance */
	TSharedPtr<FRealGazeboRTSPServer> RTSPServer;

	/** RTSP port */
	int32 RTSPPort = 8554;

	/** Subsystem initialized flag */
	bool bIsInitialized = false;

	/** Thread safety */
	mutable FCriticalSection PipelineMapMutex;
};
