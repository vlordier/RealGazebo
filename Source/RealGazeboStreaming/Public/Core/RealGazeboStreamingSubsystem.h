// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Core/RealGazeboStreamingTypes.h"
#include "Core/RealGazeboStreamConfig.h"
#include "Utils/RealGazeboStreamingStats.h"
#include "RealGazeboStreamingSubsystem.generated.h"

// Forward declarations
class FRealGazeboStreamPipeline;
class FRealGazeboFramePool;
class FRealGazeboConversionThread;
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
	 * Pause stream for specific camera
	 * @param StreamKey Stream key to pause
	 */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo|Streaming")
	void PauseStream(const FStreamKey& StreamKey);

	/**
	 * Resume stream for specific camera
	 * @param StreamKey Stream key to resume
	 */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo|Streaming")
	void ResumeStream(const FStreamKey& StreamKey);

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

	/**
	 * Get stream statistics for all streams
	 */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo|Streaming")
	TMap<FStreamKey, FStreamingStats> GetAllStreamStats() const;

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

	/** Get pipeline for stream key */
	TSharedPtr<FRealGazeboStreamPipeline> GetPipeline(const FStreamKey& StreamKey) const;

	/**
	 * Check if stream supports GPU texture encoding (zero-copy path)
	 * @param StreamKey Stream to check
	 * @return True if encoder supports direct GPU texture input
	 */
	bool SupportsTextureEncoding(const FStreamKey& StreamKey) const;

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
	                        double Timestamp, uint64 FrameNumber);

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

	// ========================================
	// Member Variables
	// ========================================

	/** Active stream pipelines (key = FStreamKey) */
	TMap<FStreamKey, TSharedPtr<FRealGazeboStreamPipeline>> ActivePipelines;

	/** Shared frame buffer pool */
	TSharedPtr<FRealGazeboFramePool> FramePool;

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
