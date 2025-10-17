// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Core/RealGazeboStreamingTypes.h"
#include "RealGazeboStreamConfig.generated.h"

/**
 * Stream configuration struct
 * Contains all settings for configuring a stream
 */
USTRUCT(BlueprintType)
struct REALGAZEBOSTREAMING_API FRealGazeboStreamConfig
{
	GENERATED_BODY()

	// Basic settings
	UPROPERTY(BlueprintReadWrite, Category = "Configuration")
	EStreamAspectRatio AspectRatio = EStreamAspectRatio::Ratio_16_9;

	UPROPERTY(BlueprintReadWrite, Category = "Configuration")
	EStreamResolution Resolution = EStreamResolution::R16_9_720p;

	UPROPERTY(BlueprintReadWrite, Category = "Configuration")
	EStreamFrameRate FrameRate = EStreamFrameRate::FPS_30;

	UPROPERTY(BlueprintReadWrite, Category = "Configuration")
	EStreamQuality Quality = EStreamQuality::High;

	// Advanced settings
	UPROPERTY(BlueprintReadWrite, Category = "Configuration|Advanced")
	EH264Profile EncodingProfile = EH264Profile::Main;

	UPROPERTY(BlueprintReadWrite, Category = "Configuration|Advanced")
	int32 GOPSize = 60;

	UPROPERTY(BlueprintReadWrite, Category = "Configuration|Advanced")
	bool bEnableAdaptiveQuality = true;

	UPROPERTY(BlueprintReadWrite, Category = "Configuration")
	int32 RTSPPort = 8554;

	// Computed values (read-only)
	UPROPERTY(BlueprintReadOnly, Category = "Configuration|Computed")
	FIntPoint Dimensions = FIntPoint(1280, 720);

	UPROPERTY(BlueprintReadOnly, Category = "Configuration|Computed")
	int32 BitrateKbps = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Configuration|Computed")
	int32 FPSValue = 30;

	FRealGazeboStreamConfig()
	{
		UpdateComputedValues();
	}

	/** Update computed values based on current settings */
	void UpdateComputedValues();

	/** Validate configuration */
	bool IsValid(FString& OutErrorMessage) const;
};
