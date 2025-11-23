// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "StreamingTypes.h"
#include "RealGazeboStreamingSubsystem.generated.h"

// Forward declarations
class UVehicleCameraComponent;
class FStreamingPipeline;
class FRTSPServerWrapper;

/**
 * URealGazeboStreamingSubsystem
 *
 * Game instance subsystem that manages all vehicle camera streams.
 * Coordinates RTSP server and streaming pipelines.
 *
 * Responsibilities:
 * - Start/stop RTSP server
 * - Create/destroy streaming pipelines
 * - Manage stream lifecycle
 * - Enforce stream isolation guarantees
 *
 * Architecture:
 * - One RTSP server (shared, runs on background thread)
 * - Multiple streaming pipelines (one per stream, fully isolated)
 * - Each pipeline: Capture → Pool → Encoder → Thread → NAL → RTSP
 */
UCLASS()
class REALGAZEBOSTREAMING_API URealGazeboStreamingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//----------------------------------------------------------
	// USubsystem Interface
	//----------------------------------------------------------

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	//----------------------------------------------------------
	// RTSP Server Management
	//----------------------------------------------------------

	/**
	 * Start RTSP server on specified port.
	 *
	 * @param Port - RTSP port (default 8554)
	 * @return True if started successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo Streaming")
	bool StartRTSPServer(int32 Port = 8554);

	/** Stop RTSP server */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo Streaming")
	void StopRTSPServer();

	/** Is RTSP server running? */
	UFUNCTION(BlueprintPure, Category = "RealGazebo Streaming")
	bool IsRTSPServerRunning() const;

	//----------------------------------------------------------
	// Stream Management
	//----------------------------------------------------------

	/**
	 * Create stream for camera component.
	 * Uses default configuration set by ARealGazeboStreamingManager (Resolution + FPS).
	 * All cameras get the SAME configuration for consistency.
	 *
	 * @param Camera - Vehicle camera component
	 * @return True if stream created successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo Streaming")
	bool CreateStream(UVehicleCameraComponent* Camera);

	/**
	 * Create stream with custom configuration.
	 *
	 * @param Camera - Vehicle camera component
	 * @param Config - Stream configuration
	 * @return True if stream created successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo Streaming")
	bool CreateStreamWithConfig(UVehicleCameraComponent* Camera, const FStreamConfig& Config);

	/**
	 * Destroy stream.
	 *
	 * @param StreamID - Stream identifier
	 */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo Streaming")
	void DestroyStream(const FStreamIdentifier& StreamID);

	/** Destroy all active streams */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo Streaming")
	void DestroyAllStreams();

	//----------------------------------------------------------
	// Camera Registration
	//----------------------------------------------------------

	/** Register camera component (called by component's BeginPlay) */
	void RegisterCamera(UVehicleCameraComponent* Camera);

	/** Unregister camera component (called by component's EndPlay) */
	void UnregisterCamera(UVehicleCameraComponent* Camera);

	//----------------------------------------------------------
	// Query
	//----------------------------------------------------------

	/** Get stream info */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo Streaming")
	FStreamInfo GetStreamInfo(const FStreamIdentifier& StreamID) const;

	/** Get all active stream identifiers */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo Streaming")
	TArray<FStreamIdentifier> GetActiveStreams() const;

	/** Get number of active streams */
	UFUNCTION(BlueprintPure, Category = "RealGazebo Streaming")
	int32 GetActiveStreamCount() const;

	/** Get all registered cameras (not yet streaming) - C++ only */
	TArray<TWeakObjectPtr<UVehicleCameraComponent>> GetRegisteredCameras() const { return RegisteredCameras; }

	/** Get number of registered cameras (waiting to stream) */
	UFUNCTION(BlueprintPure, Category = "RealGazebo Streaming")
	int32 GetRegisteredCameraCount() const { return RegisteredCameras.Num(); }

	//----------------------------------------------------------
	// Configuration
	//----------------------------------------------------------

	/** Set default stream configuration (applied to new streams) */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo Streaming")
	void SetDefaultStreamConfig(const FStreamConfig& Config);

	/** Get default stream configuration */
	UFUNCTION(BlueprintPure, Category = "RealGazebo Streaming")
	FStreamConfig GetDefaultStreamConfig() const { return DefaultStreamConfig; }

	//----------------------------------------------------------
	// Events
	//----------------------------------------------------------

	/** Broadcast when streaming system starts */
	UPROPERTY(BlueprintAssignable, Category = "RealGazebo Streaming")
	FOnStreamingStarted OnStreamingStarted;

	/** Broadcast when streaming system stops */
	UPROPERTY(BlueprintAssignable, Category = "RealGazebo Streaming")
	FOnStreamingStopped OnStreamingStopped;

	/** Broadcast when a stream is created */
	UPROPERTY(BlueprintAssignable, Category = "RealGazebo Streaming")
	FOnStreamCreated OnStreamCreated;

	/** Broadcast when a stream is destroyed */
	UPROPERTY(BlueprintAssignable, Category = "RealGazebo Streaming")
	FOnStreamDestroyed OnStreamDestroyed;

	/** Broadcast when encoding error occurs */
	UPROPERTY(BlueprintAssignable, Category = "RealGazebo Streaming")
	FOnEncodingError OnEncodingError;

private:
	//----------------------------------------------------------
	// Internal
	//----------------------------------------------------------

	/** Tick all active streams (frame capture) - returns true to continue ticking */
	bool TickStreams(float DeltaTime);

	/** Bind to world tick */
	void BindToWorldTick();

	/** Unbind from world tick */
	void UnbindFromWorldTick();

	//----------------------------------------------------------
	// RTSP Server
	//----------------------------------------------------------

	/** RTSP server (shared across all streams) */
	TSharedPtr<FRTSPServerWrapper> RTSPServer;

	//----------------------------------------------------------
	// Active Streams (Per-Stream Isolation!)
	//----------------------------------------------------------

	/** Active streaming pipelines (keyed by StreamID) */
	TMap<FStreamIdentifier, TUniquePtr<FStreamingPipeline>> ActiveStreams;

	/** Registered cameras (auto-started by ARealGazeboStreamingManager when RTSP server starts) */
	TArray<TWeakObjectPtr<UVehicleCameraComponent>> RegisteredCameras;

	//----------------------------------------------------------
	// Configuration
	//----------------------------------------------------------

	/** Default stream configuration */
	FStreamConfig DefaultStreamConfig;

	//----------------------------------------------------------
	// Tick Handle
	//----------------------------------------------------------

	/** Ticker delegate handle (FTSTicker returns FDelegateHandle in UE 5.1) */
	FTSTicker::FDelegateHandle TickDelegateHandle;
};
