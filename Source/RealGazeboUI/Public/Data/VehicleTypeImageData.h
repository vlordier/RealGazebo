// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Sub-author: MinKyu Kim
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "VehicleTypeImageData.generated.h"

/**
 * Data table structure for vehicle type images
 * Allows users to configure vehicle type codes and their corresponding images
 */
USTRUCT(BlueprintType, meta = (DataTable = "true"))
struct REALGAZEBOUI_API FVehicleTypeImageRow : public FTableRowBase
{
    GENERATED_BODY()

public:
    FVehicleTypeImageRow()
    {
        VehicleTypeCode = 0;
        VehicleImage = nullptr;
    }

    /** Vehicle type code that matches the network protocol (0-255) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle Type", meta = (DisplayName = "Vehicle Type Code"))
    uint8 VehicleTypeCode;

    /** Image/icon to display for this vehicle type */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle Type", meta = (DisplayName = "Vehicle Image"))
    TSoftObjectPtr<UTexture2D> VehicleImage;
};