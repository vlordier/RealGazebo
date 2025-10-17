// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Core/RealGazeboStreamingTypes.h"

/**
 * Utility functions for streaming configuration
 */
class REALGAZEBOSTREAMING_API FRealGazeboStreamingUtils
{
public:
	/** Get resolution dimensions from enum */
	static FIntPoint GetResolutionDimensions(EStreamResolution Resolution);

	/** Calculate bitrate in kbps based on resolution and quality */
	static int32 CalculateBitrate(EStreamResolution Resolution, EStreamQuality Quality);

	/** Get FPS value from enum */
	static int32 GetFrameRateValue(EStreamFrameRate FrameRate);

	/** Get resolutions for a specific aspect ratio */
	static TArray<EStreamResolution> GetResolutionsForAspectRatio(EStreamAspectRatio AspectRatio);

	/** Check if resolution matches aspect ratio */
	static bool IsResolutionCompatibleWithAspectRatio(EStreamResolution Resolution, EStreamAspectRatio AspectRatio);

	/** Convert H.264 profile enum to string */
	static FString H264ProfileToString(EH264Profile Profile);

	/** Get H.264 profile level */
	static int32 GetH264ProfileLevel(EH264Profile Profile);
};
