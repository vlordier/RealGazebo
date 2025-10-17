// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "GazeboBridgeTypes.h"
#include "Data/VehicleTypeImageData.h"
#include "RealGazeboVehicleData.generated.h"

// Forward declarations
class AVehicleBasePawn;


USTRUCT(BlueprintType, meta = (DataTable = "true"))
struct REALGAZEBO_API FRealGazeboVehicleConfigRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Descriptive name for the vehicle type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle Config", meta = (DisplayName = "Vehicle Name"))
	FString VehicleName = TEXT("Unknown");

	/** Unique code to identify vehicle type (0-255) for network messages */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle Config", meta = (DisplayName = "Vehicle Type Code"))
	uint8 VehicleTypeCode = 0;

	/** Number of motors/propellers on this vehicle type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle Config", meta = (DisplayName = "Motor Count"))
	int32 MotorCount = 0;

	/** Number of servo actuators on this vehicle type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle Config", meta = (DisplayName = "Servo Count"))
	int32 ServoCount = 0;

	/** Blueprint class to spawn for this vehicle type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle Config", meta = (DisplayName = "Vehicle Pawn Class"))
	TSubclassOf<AVehicleBasePawn> VehiclePawnClass;

	/** Image/icon to display for this vehicle type in the UI */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle Config", meta = (DisplayName = "Vehicle Image"))
	TSoftObjectPtr<UTexture2D> VehicleImage;

	/** Calculate expected packet size for motor speed messages */
	int32 GetMotorSpeedPacketSize() const
	{
		return 3 + (MotorCount * 4); // 3 header bytes + 4 bytes per motor
	}

	/** Calculate expected packet size for servo data messages */
	int32 GetServoPacketSize() const
	{
		return 3 + (ServoCount * 28);
	}

	/** Convert to FBridgeVehicleConfigRow for Bridge subsystem */
	FBridgeVehicleConfigRow ToBridgeConfigRow() const
	{
		FBridgeVehicleConfigRow BridgeRow;
		BridgeRow.VehicleName = VehicleName;
		BridgeRow.VehicleTypeCode = VehicleTypeCode;
		BridgeRow.MotorCount = MotorCount;
		BridgeRow.ServoCount = ServoCount;
		BridgeRow.VehiclePawnClass = VehiclePawnClass;
		return BridgeRow;
	}

	/** Convert to FVehicleTypeImageRow for UI subsystem */
	FVehicleTypeImageRow ToVehicleTypeImageRow() const
	{
		FVehicleTypeImageRow ImageRow;
		ImageRow.VehicleTypeCode = VehicleTypeCode;
		ImageRow.VehicleImage = VehicleImage;
		return ImageRow;
	}

	/** Check if this row has valid UI image data */
	bool HasVehicleImage() const
	{
		return !VehicleImage.IsNull();
	}

	/** Get vehicle image texture (may return nullptr if not loaded/set) */
	UTexture2D* GetVehicleImageTexture() const
	{
		return VehicleImage.Get();
	}
};
