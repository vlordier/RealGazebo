// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneCaptureComponent2D.h"
#include "StreamingTypes.h"
#include "VehicleCameraComponent.generated.h"

/**
 * UVehicleCameraComponent
 *
 * Scene capture component for vehicle camera streaming.
 * Uses POLLING to detect vehicle activation - NO Blueprint wiring needed!
 *
 * Usage:
 * 1. Add this component to vehicle Blueprint
 * 2. Set CameraID (e.g., "front", "bottom", "fpv")
 * 3. VehicleID auto-detected from owning VehicleBasePawn via polling
 * 4. Stream auto-starts when VehicleID becomes valid (not 0_0)
 *
 * Stream URL: rtsp://localhost:8554/{vehicle_type}_{vehicle_num}/{camera_id}
 * Example: rtsp://localhost:8554/x500_0/front
 */
UCLASS(ClassGroup=(RealGazebo), meta=(BlueprintSpawnableComponent))
class REALGAZEBOSTREAMING_API UVehicleCameraComponent : public USceneCaptureComponent2D
{
	GENERATED_BODY()

public:
	UVehicleCameraComponent();

	//----------------------------------------------------------
	// Configuration (Edit in Blueprint)
	//----------------------------------------------------------

	/** Camera ID for RTSP URL (e.g., "front", "right", "bottom", "fpv") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealGazebo Streaming")
	FString CameraID = TEXT("front");

	/** Enable streaming for this camera */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealGazebo Streaming")
	bool bEnableStreaming = true;

	//----------------------------------------------------------
	// Runtime Status (Read-Only)
	//----------------------------------------------------------

	/** Vehicle ID (auto-detected from VehicleBasePawn) */
	UPROPERTY(BlueprintReadOnly, Category = "RealGazebo Streaming")
	FVehicleID VehicleID;

	/** Vehicle type name (auto-detected from VehicleBasePawn, e.g., "x500", "iris") */
	UPROPERTY(BlueprintReadOnly, Category = "RealGazebo Streaming")
	FString VehicleTypeName;

	/** RTSP URL for this stream (set after stream starts) */
	UPROPERTY(BlueprintReadOnly, Category = "RealGazebo Streaming")
	FString RTSPURL;

	/** Is stream currently active? */
	UPROPERTY(BlueprintReadOnly, Category = "RealGazebo Streaming")
	bool bIsStreaming = false;

	/** Is camera initialized? (VehicleID detected) */
	UPROPERTY(BlueprintReadOnly, Category = "RealGazebo Streaming")
	bool bIsInitialized = false;

	//----------------------------------------------------------
	// Lifecycle
	//----------------------------------------------------------

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	//----------------------------------------------------------
	// Public API
	//----------------------------------------------------------

	/** Get unique stream identifier (VehicleID + CameraID) */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo Streaming")
	FStreamIdentifier GetStreamIdentifier() const;

	/** Start streaming this camera */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo Streaming")
	bool StartStreaming();

	/** Stop streaming this camera */
	UFUNCTION(BlueprintCallable, Category = "RealGazebo Streaming")
	void StopStreaming();

private:
	/** Poll VehicleID from owning VehicleBasePawn */
	void TryPopulateVehicleID();

	/** Register with streaming subsystem */
	void RegisterWithSubsystem();

	/** Unregister from streaming subsystem */
	void UnregisterFromSubsystem();
};
