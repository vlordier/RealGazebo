// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Core/RealGazeboStreamingTypes.h"
#include "Utils/RealGazeboStreamingStats.h"
#include "Core/GazeboBridgeTypes.h"
#include "RealGazeboStreamingCamera.generated.h"

class URealGazeboStreamingSubsystem;
class FRealGazeboSceneCapture;

/**
 * Real Gazebo Streaming Camera Component
 *
 * This component captures and streams video from vehicle cameras via RTSP protocol.
 * It automatically detects the vehicle ID from the owning VehicleBasePawn and
 * registers with the streaming subsystem for hardware-accelerated encoding.
 *
 * Add this component to vehicle blueprints to enable streaming. The vehicle ID
 * is detected automatically, and the camera will appear as an RTSP stream at
 * rtsp://localhost:8554/<vehicle_type>_<vehicle_num> or with a camera ID suffix
 * for multi-camera setups.
 */
UCLASS(ClassGroup = (RealGazebo), meta = (BlueprintSpawnableComponent))
class REALGAZEBOSTREAMING_API URealGazeboStreamingCamera : public USceneComponent
{
	GENERATED_BODY()

public:
	URealGazeboStreamingCamera();

protected:
	//~ Begin UActorComponent Interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

public:
	//========================================
	// USER CONFIGURATION (Edit These)
	//========================================

	/** Camera ID - REQUIRED for RTSP URL (e.g., "front", "right", "gimbal"). Must be unique per vehicle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Camera Settings", meta = (
		DisplayName = "Camera ID (REQUIRED)",
		ToolTip = "REQUIRED: Camera identifier for RTSP URL. Examples: 'front', 'right', 'gimbal', 'rear'. Must be unique per vehicle.\nRTSP URL format: rtsp://localhost:8554/{vehicle_id}/{camera_id}"))
	FString CameraID;

	/** Field of view in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Camera Settings", meta = (
		DisplayName = "Field of View",
		ClampMin = "5.0", ClampMax = "170.0"))
	float FieldOfView = 90.0f;

	//========================================
	// AUTO-DETECTED INFO (Read-Only)
	//========================================

	/** Detected vehicle ID (auto-detected from VehicleBasePawn at runtime) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Streaming|Auto-Detected Info", meta = (
		DisplayName = "Vehicle ID (Auto-Detected)"))
	FVehicleID DetectedVehicleID;

	/** Detected vehicle type name (e.g., "iris_1", "x500_2") */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Streaming|Auto-Detected Info", meta = (
		DisplayName = "Vehicle Name (Auto-Detected)"))
	FString DetectedVehicleTypeName;

	/** Frame capture rate in frames per second (auto-set from StreamManager) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Streaming|Auto-Detected Info", meta = (
		DisplayName = "Capture Frame Rate"))
	int32 CaptureFrameRate = 30;

	// Streaming Control Methods

	/** Start streaming this camera */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo|Streaming")
	bool StartStreaming();

	/** Stop streaming this camera */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo|Streaming")
	void StopStreaming();

	/** Check if camera is currently streaming */
	UFUNCTION(BlueprintPure, Category = "RealGazebo|Streaming")
	bool IsStreaming() const;

	/** Get current stream state */
	UFUNCTION(BlueprintPure, Category = "RealGazebo|Streaming")
	EStreamState GetStreamState() const;

	// Stream Information

	/** Get RTSP URL for this camera stream */
	UFUNCTION(BlueprintPure, Category = "RealGazebo|Streaming")
	FString GetRTSPURL() const;

	/** Get stream key used for identification */
	UFUNCTION(BlueprintPure, Category = "RealGazebo|Streaming")
	FStreamKey GetStreamKey() const;

	/** Get current streaming statistics */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo|Streaming")
	bool GetStreamingStats(FStreamingStats& OutStats) const;

	/** Get render target used for frame capture. Useful for visualization or debugging. */
	UFUNCTION(BlueprintPure, Category = "RealGazebo|Streaming")
	class UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget; }

	/** Update streaming settings from StreamManager at runtime (allows hot-reload of quality settings) */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo|Streaming")
	void UpdateStreamingSettings();

	// Debug Options

	/** Enable on-screen debug information display */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Debug", meta = (
		DisplayName = "Show Debug Info"))
	bool bShowDebugInfo = false;

private:
	/** Initialize streaming system */
	void InitializeStreaming();

	/** Shutdown streaming */
	void ShutdownStreaming();

	/** Detect vehicle ID from owning vehicle pawn */
	bool DetectVehicleID(FVehicleID& OutVehicleID);

	/** Capture frame and submit to pipeline */
	void CaptureFrame();

	/** Draw debug info overlay */
	void DrawDebugInfo();

	/** Validate CameraID is not empty (now always required) */
	bool ValidateCameraID() const;

	/** Cached streaming subsystem */
	UPROPERTY(Transient)
	URealGazeboStreamingSubsystem* StreamingSubsystem = nullptr;

	/** Scene capture component (created dynamically) */
	UPROPERTY(Transient)
	class USceneCaptureComponent2D* SceneCaptureComponent = nullptr;

	/** Render target for capturing frames */
	UPROPERTY(Transient)
	class UTextureRenderTarget2D* RenderTarget = nullptr;

	/** Is streaming initialized (deferred until vehicle activation) */
	bool bIsInitialized = false;

	/** Is streaming active */
	bool bIsStreaming = false;

	/** Last absolute capture time (seconds) - decoupled from game tick rate */
	double LastCaptureTime = 0.0;

	/** Frame capture interval (1.0 / CaptureFrameRate) */
	float FrameCaptureInterval = 0.033f;

	/** Frame sequence counter */
	uint64 FrameSequence = 0;

	/** GOP (Group of Pictures) size for keyframe scheduling (overridden by StreamManager config) */
	int32 GOPSize = 30;  // Default: 1.0s @ 30fps (industry standard)

	/** Scene readiness flag - delays streaming until scene has loaded (prevents black frames) */
	bool bSceneReady = false;

	/** Prevents repetitive errors for empty CameraID */
	mutable bool bHasLoggedCameraIDError = false;
};
