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
	// Configuration Properties

	/** Camera ID for multi-camera vehicles (e.g., "fpv", "gimbal", "rear"). Leave empty for single camera setups. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Configuration", meta = (
		DisplayName = "Camera ID",
		ToolTip = "Optional camera identifier for multi-camera vehicles. Leave empty for single camera setups."))
	FString CameraID;

	/** Override auto-detected vehicle ID. Leave at 0,0 to auto-detect from owning VehicleBasePawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Streaming|Configuration", meta = (
		DisplayName = "Override Vehicle ID (Auto-detected)",
		ToolTip = "Manually set Vehicle ID. Leave at 0,0 to auto-detect from owning VehicleBasePawn."))
	FVehicleID VehicleIDOverride;

	/** Field of view in degrees */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Configuration", meta = (
		DisplayName = "Field of View",
		ClampMin = "5.0", ClampMax = "170.0"))
	float FieldOfView = 90.0f;

	/** Auto-start streaming when component begins play */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Configuration", meta = (
		DisplayName = "Auto Start"))
	bool bAutoStart = false;

	/** Frame capture rate in frames per second. Can differ from stream output frame rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Advanced", meta = (
		DisplayName = "Capture Frame Rate",
		ClampMin = "15", ClampMax = "60"))
	int32 CaptureFrameRate = 30;

	// Streaming Control Methods

	/** Start streaming this camera */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo|Streaming")
	bool StartStreaming();

	/** Stop streaming this camera */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo|Streaming")
	void StopStreaming();

	/** Pause streaming without destroying the pipeline */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo|Streaming")
	void PauseStreaming();

	/** Resume a paused stream */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo|Streaming")
	void ResumeStreaming();

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

	// Debug Options

	/** Enable on-screen debug information display */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Debug", meta = (
		DisplayName = "Show Debug Info"))
	bool bShowDebugInfo = false;

	/** Color for debug text overlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Debug", meta = (
		DisplayName = "Debug Color"))
	FColor DebugColor = FColor::Cyan;

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

	/** Cached streaming subsystem */
	UPROPERTY(Transient)
	URealGazeboStreamingSubsystem* StreamingSubsystem = nullptr;

	/** Detected or overridden vehicle ID (set during BeginPlay) */
	FVehicleID DetectedVehicleID;

	/** Detected vehicle type name (e.g., "X500", "Iris") for RTSP URLs */
	FString DetectedVehicleTypeName;

	/** Scene capture component (created dynamically) */
	UPROPERTY(Transient)
	class USceneCaptureComponent2D* SceneCaptureComponent = nullptr;

	/** Render target for capturing frames */
	UPROPERTY(Transient)
	class UTextureRenderTarget2D* RenderTarget = nullptr;

	/** Is streaming active */
	bool bIsStreaming = false;

	/** Frame capture timer */
	float FrameCaptureTimer = 0.0f;

	/** Frame capture interval (1.0 / CaptureFrameRate) */
	float FrameCaptureInterval = 0.033f;

	/** Frame sequence counter */
	uint64 FrameSequence = 0;
};
