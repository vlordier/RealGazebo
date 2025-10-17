// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Data/RealGazeboVehicleListItem.h"
#include "Engine/Engine.h"
#include "RealGazeboUI.h"

URealGazeboVehicleListItem::URealGazeboVehicleListItem()
{
    InitializeDefaults();
}

void URealGazeboVehicleListItem::InitializeDefaults()
{
    VehicleID = FVehicleID(0, 0);
    VehicleName = TEXT("Unknown Vehicle");
    VehicleTypeName = TEXT("Unknown");
    Position = FVector::ZeroVector;
    Rotation = FRotator::ZeroRotator;
    
    // Placeholder values for future telemetry
    BatteryPercentage = 100.0f;
    Status = TEXT("No Data");
    
    LastUpdateTime = FPlatformTime::Seconds();
}

void URealGazeboVehicleListItem::UpdateFromRuntimeData(const FVehicleRuntimeData& RuntimeData)
{
    // Update core transform data
    Position = RuntimeData.Position;
    Rotation = RuntimeData.Rotation.Rotator();
    
    // Update timing
    LastUpdateTime = FPlatformTime::Seconds();
    
    // Keep status as placeholder
    Status = TEXT("No Data");
    
    // Vehicle data updated
}

void URealGazeboVehicleListItem::UpdateTransform(const FVector& NewPosition, const FRotator& NewRotation)
{
    Position = NewPosition;
    Rotation = NewRotation;
    LastUpdateTime = FPlatformTime::Seconds();
    
    // Keep status as placeholder
    Status = TEXT("No Data");
}


FString URealGazeboVehicleListItem::GetFormattedPosition() const
{
    // Position is in Unreal Engine coordinates (centimeters)
    return FString::Printf(TEXT("%.1f, %.1f, %.1f"), Position.X, Position.Y, Position.Z);
}

FString URealGazeboVehicleListItem::GetFormattedStatus() const
{
    if (BatteryPercentage < 100.0f)
    {
        return FString::Printf(TEXT("%s | Battery: %.0f%%"), *Status, BatteryPercentage);
    }
    return Status;
}