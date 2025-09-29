// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Sub-author: MinKyu Kim
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
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