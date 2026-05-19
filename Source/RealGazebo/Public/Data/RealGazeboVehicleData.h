// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
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

	/**
	 * Blueprint class to spawn for this vehicle type.
	 *
	 * Legacy hard reference - kept for backward compatibility with existing DataTable rows.
	 * For new rows (especially mod rows shipped in Pak DLC), prefer VehiclePawnClassSoft below,
	 * which avoids forcing the pawn asset to load whenever the row is read.
	 *
	 * Resolution order at spawn time (see UVehicleRegistrySubsystem::GetVehicleClassLoaded):
	 *   1. VehiclePawnClassSoft (if set) - LoadSynchronous'd on demand
	 *   2. VehiclePawnClass (this field) - fallback for legacy rows
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle Config", meta = (DisplayName = "Vehicle Pawn Class (Legacy)"))
	TSubclassOf<AVehicleBasePawn> VehiclePawnClass;

	/**
	 * Soft reference to the vehicle pawn class. Preferred field for new content,
	 * particularly mod DataTables shipped via Pak DLC where the pawn class itself
	 * is also part of the mod content.
	 *
	 * When this field is set, it takes precedence over VehiclePawnClass.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle Config", meta = (DisplayName = "Vehicle Pawn Class"))
	TSoftClassPtr<AVehicleBasePawn> VehiclePawnClassSoft;

	/** Image/icon to display for this vehicle type in the UI */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle Config", meta = (DisplayName = "Vehicle Image"))
	TSoftObjectPtr<UTexture2D> VehicleImage;

	/** Controls whether this object appears in the UI vehicle list */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle Config",
	          meta = (DisplayName = "Show In UI",
	                  ToolTip = "If false, this object will not appear in the UI vehicle list (useful for persons, props, etc.)"))
	bool bShowInUI = true; // Default true for backward compatibility with existing vehicles

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
		BridgeRow.bShowInUI = bShowInUI;
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
