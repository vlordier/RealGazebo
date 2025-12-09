// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
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

    // Initialize telemetry values with "no data" defaults
    BatteryPercentage = -1.0f; // -1 indicates no data received yet
    Status = TEXT("--");
    NavState = ENavigationState::MANUAL;

    LastUpdateTime = FPlatformTime::Seconds();
}

void URealGazeboVehicleListItem::UpdateFromRuntimeData(const FVehicleRuntimeData& RuntimeData)
{
    // Update core transform data
    Position = RuntimeData.Position;
    Rotation = RuntimeData.Rotation.Rotator();

    // Update battery and navigation state only if valid data has been received
    // RuntimeData starts with BatteryRemaining = -1.0, which indicates no AdditionalData packet yet
    if (RuntimeData.BatteryRemaining >= 0.0f)
    {
        // Valid battery data received, update both battery and nav state
        BatteryPercentage = RuntimeData.BatteryRemaining * 100.0f; // Convert 0.0-1.0 to 0-100
        NavState = RuntimeData.NavState;
        Status = NavigationStateToString(RuntimeData.NavState);
    }
    // else: keep existing values ("--" for battery, "--" for status) until data arrives

    // Update timing
    LastUpdateTime = FPlatformTime::Seconds();
}

void URealGazeboVehicleListItem::UpdateTransform(const FVector& NewPosition, const FRotator& NewRotation)
{
    Position = NewPosition;
    Rotation = NewRotation;
    LastUpdateTime = FPlatformTime::Seconds();
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

