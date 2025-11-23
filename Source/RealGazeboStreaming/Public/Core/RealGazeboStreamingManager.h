// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StreamingTypes.h"
#include "RealGazeboStreamingManager.generated.h"

/**
 * ARealGazeboStreamingManager
 *
 * Level actor for configuring RealGazebo streaming system.
 * Drag into level and configure streaming parameters via Details panel.
 *
 * USER-FRIENDLY INTERFACE
 * Only exposes essential settings:
 * - Resolution (dropdown)
 * - Frame Rate (dropdown)
 * - RTSP Port (number)
 * - Auto Start Streaming (checkbox)
 *
 * All other settings are hardcoded for ultra-low latency:
 * - Preset: UltraLowLatency
 * - Profile: H.264 Baseline
 * - Bitrate: Auto-calculated based on Resolution + FPS
 * - Zero-Copy: Enabled
 * - GOP Size: Auto-calculated as FPS/2 for 0.5s keyframe interval
 *
 * UNIFIED CONFIGURATION
 * All cameras receive the SAME settings from this manager.
 * This ensures consistent quality across all streams.
 *
 * Usage:
 * 1. Drag this actor into your level
 * 2. Configure Resolution (default: XGA 1024x768)
 * 3. Configure FPS (default: 30)
 * 4. Configure RTSP Port (default: 8554)
 * 5. Set AutoStartStreaming = true
 * 6. Add UVehicleCameraComponent to vehicle Blueprints with bEnableStreaming=true
 * 7. Play → All registered cameras auto-start with the same configuration
 *
 * RTSP URLs generated as:
 * rtsp://localhost:PORT/vehicle_type_num/camera_id
 */
UCLASS(ClassGroup=(RealGazebo), meta=(BlueprintSpawnableComponent))
class REALGAZEBOSTREAMING_API ARealGazeboStreamingManager : public AActor
{
	GENERATED_BODY()

public:
	//----------------------------------------------------------
	// Construction
	//----------------------------------------------------------

	ARealGazeboStreamingManager();

	//----------------------------------------------------------
	// User Configuration (ONLY THESE PROPERTIES!)
	//----------------------------------------------------------

	/**
	 * Default stream resolution for all cameras.
	 * Robotics-optimized 4:3 aspect ratios.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming Settings",
	          meta = (DisplayName = "Resolution"))
	EStreamResolution DefaultResolution = EStreamResolution::XGA_1024x768;

	/**
	 * Default frame rate for all cameras.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming Settings",
	          meta = (DisplayName = "Frame Rate"))
	EStreamFrameRate DefaultFrameRate = EStreamFrameRate::FPS_30;

	/**
	 * RTSP server port.
	 * Default: 8554 (standard RTSP port)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming Settings",
	          meta = (DisplayName = "RTSP Port", ClampMin = "1024", ClampMax = "65535"))
	int32 RTSPPort = 8554;

	/**
	 * Auto-start streaming when level begins?
	 * If true, all vehicle cameras with bEnableStreaming=true will start automatically.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming Settings",
	          meta = (DisplayName = "Auto Start Streaming"))
	bool bAutoStartStreaming = true;

	//----------------------------------------------------------
	// Runtime Info (Read-Only)
	//----------------------------------------------------------

	/**
	 * Is RTSP server running?
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Streaming Status")
	bool bIsServerRunning = false;

	/**
	 * Number of active streams.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Streaming Status")
	int32 ActiveStreamCount = 0;

	/**
	 * Auto-calculated bitrate (based on Resolution + FPS).
	 * Displayed for informational purposes.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Streaming Status",
	          meta = (DisplayName = "Auto-Calculated Bitrate (kbps)"))
	int32 CalculatedBitrate = 2000;

	//----------------------------------------------------------
	// Lifecycle
	//----------------------------------------------------------

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	//----------------------------------------------------------
	// API
	//----------------------------------------------------------

	/** Manually start RTSP server and all camera streams */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo Streaming")
	bool StartStreaming();

	/** Manually stop all streams and RTSP server */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo Streaming")
	void StopStreaming();

	/** Get default stream configuration (derived from Resolution + FPS) */
	UFUNCTION(BlueprintPure, Category = "RealGazebo Streaming")
	FStreamConfig GetDefaultStreamConfig() const;

	/** Get all active stream URLs */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo Streaming")
	TArray<FString> GetActiveStreamURLs() const;

private:
	/** Get streaming subsystem */
	class URealGazeboStreamingSubsystem* GetStreamingSubsystem() const;

	/** Update calculated bitrate display */
	void UpdateCalculatedBitrate();

	/** Start all registered camera streams */
	void StartAllCameraStreams();
};
