// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "RealGazeboViewerTypes.generated.h"

/**
 * Camera modes for RealGazebo Viewer Controller
 * Based on AirSim architecture but adapted for RealGazebo
 */
UENUM(BlueprintType)
enum class ERealGazeboViewerMode : uint8
{
    /** Manual free-flying camera with WASD controls */
    Manual UMETA(DisplayName = "Manual Camera"),

    /** First person view attached to vehicle center (cockpit view) */
    FirstPerson UMETA(DisplayName = "First Person Camera"),

    /** Third person chase camera following vehicle with spring arm */
    ThirdPerson UMETA(DisplayName = "Third Person Camera")
};

/**
 * Camera preset for Manual camera mode quick teleportation
 * Stores a named location/rotation with optional thumbnail image
 */
USTRUCT(BlueprintType)
struct REALGAZEBOUI_API FCameraPreset
{
    GENERATED_BODY()

    /** Display name for this preset (e.g., "VILS", "Urban", "C-Track") */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Preset")
    FString PresetName;

    /** Camera location in world space */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Preset")
    FVector Location;

    /** Camera rotation */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Preset")
    FRotator Rotation;

    /** Default constructor */
    FCameraPreset()
        : PresetName(TEXT("Unnamed"))
        , Location(FVector::ZeroVector)
        , Rotation(FRotator::ZeroRotator)
    {}

    /** Constructor with parameters */
    FCameraPreset(const FString& InName, const FVector& InLocation, const FRotator& InRotation)
        : PresetName(InName)
        , Location(InLocation)
        , Rotation(InRotation)
    {}
};