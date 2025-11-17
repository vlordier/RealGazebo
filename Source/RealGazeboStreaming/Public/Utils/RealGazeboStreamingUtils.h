// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Core/RealGazeboStreamingTypes.h"

/**
 * Streaming utilities for resolution and encoding calculations
 */
class REALGAZEBOSTREAMING_API FRealGazeboStreamingUtils
{
public:
	/** Get pixel dimensions for resolution enum */
	static FIntPoint GetResolutionDimensions(EStreamResolution Resolution);

	/** Calculate optimal bitrate for resolution and frame rate (frame rate aware CBR) */
	static int32 CalculateBitrateUltraLowLatency(EStreamResolution Resolution, EStreamFrameRate FrameRate);

	/** Convert frame rate enum to FPS value */
	static int32 GetFrameRateValue(EStreamFrameRate FrameRate);

	/** Calculate GOP size for frame rate (1.0s keyframe interval - industry standard) */
	static int32 CalculateGOPSize(EStreamFrameRate FrameRate);
};
