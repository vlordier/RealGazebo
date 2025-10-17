// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Core/RealGazeboStreamConfig.h"
#include "Utils/RealGazeboStreamingUtils.h"

void FRealGazeboStreamConfig::UpdateComputedValues()
{
	Dimensions = FRealGazeboStreamingUtils::GetResolutionDimensions(Resolution);
	BitrateKbps = FRealGazeboStreamingUtils::CalculateBitrate(Resolution, Quality);
	FPSValue = FRealGazeboStreamingUtils::GetFrameRateValue(FrameRate);
}

bool FRealGazeboStreamConfig::IsValid(FString& OutErrorMessage) const
{
	if (Dimensions.X <= 0 || Dimensions.Y <= 0)
	{
		OutErrorMessage = TEXT("Invalid dimensions");
		return false;
	}

	if (BitrateKbps <= 0)
	{
		OutErrorMessage = TEXT("Invalid bitrate");
		return false;
	}

	if (FPSValue <= 0)
	{
		OutErrorMessage = TEXT("Invalid frame rate");
		return false;
	}

	if (RTSPPort < 1024 || RTSPPort > 65535)
	{
		OutErrorMessage = FString::Printf(TEXT("Invalid RTSP port: %d (must be 1024-65535)"), RTSPPort);
		return false;
	}

	if (GOPSize < 15 || GOPSize > 300)
	{
		OutErrorMessage = FString::Printf(TEXT("Invalid GOP size: %d (must be 15-300)"), GOPSize);
		return false;
	}

	// Check aspect ratio matches resolution
	if (!FRealGazeboStreamingUtils::IsResolutionCompatibleWithAspectRatio(Resolution, AspectRatio))
	{
		OutErrorMessage = TEXT("Resolution does not match selected aspect ratio");
		return false;
	}

	OutErrorMessage = TEXT("");
	return true;
}
