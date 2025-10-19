// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Core/GazeboBridgeTypes.h"  // For FVehicleID
#include "RealGazeboStreamingTypes.generated.h"

/**
 * Stream aspect ratio options
 */
UENUM(BlueprintType)
enum class EStreamAspectRatio : uint8
{
	/** 16:9 aspect ratio (widescreen) */
	Ratio_16_9 UMETA(DisplayName = "16:9 (Widescreen)"),

	/** 4:3 aspect ratio (standard) */
	Ratio_4_3 UMETA(DisplayName = "4:3 (Standard)")
};

/**
 * Predefined stream resolutions (complete list - 23 resolutions)
 * Supports both 16:9 widescreen (12) and 4:3 standard (11) aspect ratios
 */
UENUM(BlueprintType)
enum class EStreamResolution : uint8
{
	//========================================================================
	// 16:9 Resolutions (12 total)
	//========================================================================

	/** 426x240 - 240p SD (16:9) */
	R16_9_240p UMETA(DisplayName = "240p (426x240) SD"),

	/** 640x360 - nHD (16:9) */
	R16_9_360p UMETA(DisplayName = "360p (640x360) nHD"),

	/** 854x480 - FWVGA (16:9) */
	R16_9_480p UMETA(DisplayName = "480p (854x480) FWVGA"),

	/** 960x540 - qHD (16:9) */
	R16_9_540p UMETA(DisplayName = "540p (960x540) qHD"),

	/** 1024x576 - WSVGA (16:9) */
	R16_9_576p UMETA(DisplayName = "576p (1024x576) WSVGA"),

	/** 1280x720 - HD (16:9) */
	R16_9_720p UMETA(DisplayName = "720p (1280x720) HD"),

	/** 1366x768 - FWXGA (16:9) */
	R16_9_768p UMETA(DisplayName = "768p (1366x768) FWXGA"),

	/** 1600x900 - HD+ (16:9) */
	R16_9_900p UMETA(DisplayName = "900p (1600x900) HD+"),

	/** 1920x1080 - Full HD (16:9) */
	R16_9_1080p UMETA(DisplayName = "1080p (1920x1080) Full HD"),

	/** 2560x1440 - QHD/2K (16:9) */
	R16_9_1440p UMETA(DisplayName = "1440p (2560x1440) QHD/2K"),

	/** 3200x1800 - QHD+ (16:9) */
	R16_9_1800p UMETA(DisplayName = "1800p (3200x1800) QHD+"),

	/** 3840x2160 - 4K UHD (16:9) */
	R16_9_2160p UMETA(DisplayName = "2160p (3840x2160) 4K UHD"),

	//========================================================================
	// 4:3 Resolutions (11 total)
	//========================================================================

	/** 320x240 - QVGA (4:3) */
	R4_3_240p UMETA(DisplayName = "240p (320x240) QVGA"),

	/** 640x480 - VGA (4:3) */
	R4_3_480p UMETA(DisplayName = "480p (640x480) VGA"),

	/** 800x600 - SVGA (4:3) */
	R4_3_600p UMETA(DisplayName = "600p (800x600) SVGA"),

	/** 1024x768 - XGA (4:3) */
	R4_3_768p UMETA(DisplayName = "768p (1024x768) XGA"),

	/** 1280x960 - SXGA- (4:3) */
	R4_3_960p UMETA(DisplayName = "960p (1280x960) SXGA-"),

	/** 1400x1050 - SXGA+ (4:3) */
	R4_3_1050p UMETA(DisplayName = "1050p (1400x1050) SXGA+"),

	/** 1600x1200 - UXGA (4:3) */
	R4_3_1200p UMETA(DisplayName = "1200p (1600x1200) UXGA"),

	/** 1920x1440 - QXGA (4:3) */
	R4_3_1440p UMETA(DisplayName = "1440p (1920x1440) QXGA"),

	/** 2048x1536 - QXGA (4:3) */
	R4_3_1536p UMETA(DisplayName = "1536p (2048x1536) QXGA"),

	/** 2560x1920 - QSXGA (4:3) */
	R4_3_1920p UMETA(DisplayName = "1920p (2560x1920) QSXGA"),

	/** 3200x2400 - QUXGA (4:3) */
	R4_3_2400p UMETA(DisplayName = "2400p (3200x2400) QUXGA")
};

/**
 * Stream frame rate options
 */
UENUM(BlueprintType)
enum class EStreamFrameRate : uint8
{
	FPS_15 UMETA(DisplayName = "15 FPS"),
	FPS_30 UMETA(DisplayName = "30 FPS"),
	FPS_60 UMETA(DisplayName = "60 FPS")
};

/**
 * Stream quality presets
 */
UENUM(BlueprintType)
enum class EStreamQuality : uint8
{
	/** Low quality (reduces bitrate by 50%) */
	Low UMETA(DisplayName = "Low"),

	/** Medium quality (reduces bitrate by 25%) */
	Medium UMETA(DisplayName = "Medium"),

	/** High quality (baseline bitrate) */
	High UMETA(DisplayName = "High"),

	/** Ultra quality (increases bitrate by 50%) */
	Ultra UMETA(DisplayName = "Ultra")
};

/**
 * H.264 encoding profiles
 */
UENUM(BlueprintType)
enum class EH264Profile : uint8
{
	/** Baseline profile (maximum compatibility, mobile devices) */
	Baseline UMETA(DisplayName = "Baseline (Maximum Compatibility)"),

	/** Main profile (balanced compression, widely supported) */
	Main UMETA(DisplayName = "Main (Recommended)"),

	/** High profile (best compression, modern devices) */
	High UMETA(DisplayName = "High (Best Quality)")
};

/**
 * Stream state
 */
UENUM(BlueprintType)
enum class EStreamState : uint8
{
	Stopped,
	Starting,
	Streaming,
	Paused,
	Stopping,
	Error
};

/**
 * Unique identifier for a stream
 * Combination of VehicleID + VehicleTypeName + CameraID (optional)
 */
USTRUCT(BlueprintType)
struct REALGAZEBOSTREAMING_API FStreamKey
{
	GENERATED_BODY()

	/** Vehicle identifier from RealGazeboBridge */
	UPROPERTY(BlueprintReadWrite, Category = "Stream")
	FVehicleID VehicleID;

	/** Vehicle type name (e.g., "X500", "Iris", "Rover") for readable RTSP URLs */
	UPROPERTY(BlueprintReadWrite, Category = "Stream")
	FString VehicleTypeName;

	/** Optional camera identifier for multi-camera setups (empty = default camera) */
	UPROPERTY(BlueprintReadWrite, Category = "Stream")
	FString CameraID;

	FStreamKey()
		: VehicleID()
		, VehicleTypeName(TEXT(""))
		, CameraID(TEXT(""))
	{
	}

	FStreamKey(const FVehicleID& InVehicleID, const FString& InVehicleTypeName = TEXT(""), const FString& InCameraID = TEXT(""))
		: VehicleID(InVehicleID)
		, VehicleTypeName(InVehicleTypeName)
		, CameraID(InCameraID)
	{
	}

	/** Generate RTSP URL path component using vehicle type name */
	FString ToString() const
	{
		// Use vehicle type name if available (e.g., "X500_0"), otherwise fall back to type code (e.g., "0_0")
		FString VehicleIdentifier = VehicleTypeName.IsEmpty()
			? FString::Printf(TEXT("%d_%d"), VehicleID.VehicleType, VehicleID.VehicleNum)
			: FString::Printf(TEXT("%s_%d"), *VehicleTypeName, VehicleID.VehicleNum);

		if (CameraID.IsEmpty())
		{
			// Format: vehiclename_vehiclenum (e.g., "X500_0" for first X500)
			return VehicleIdentifier;
		}
		// Format: vehiclename_vehiclenum/cameraid (e.g., "X500_0/fpv")
		return FString::Printf(TEXT("%s/%s"), *VehicleIdentifier, *CameraID);
	}

	/** Equality operator */
	bool operator==(const FStreamKey& Other) const
	{
		return VehicleID == Other.VehicleID && CameraID.Equals(Other.CameraID);
	}

	/** Generate hash for use in TMap */
	friend uint32 GetTypeHash(const FStreamKey& Key)
	{
		return HashCombine(GetTypeHash(Key.VehicleID), GetTypeHash(Key.CameraID));
	}
};

/**
 * Stream state change event delegate
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnStreamStateChanged, FStreamKey, StreamKey, EStreamState, NewState);

/**
 * Stream statistics update event delegate
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStreamStatsUpdated, FStreamKey, StreamKey);
