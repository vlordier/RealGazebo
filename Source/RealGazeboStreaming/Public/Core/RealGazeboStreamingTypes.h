// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Logging/LogMacros.h"
#include "Core/GazeboBridgeTypes.h"  // For FVehicleID
#include "RealGazeboStreamingTypes.generated.h"

//----------------------------------------------------------
// Logging
//----------------------------------------------------------

DECLARE_LOG_CATEGORY_EXTERN(LogRealGazeboStreaming, Log, All);

//----------------------------------------------------------
// ULTRA-LOW LATENCY STREAMING
// RealGazeboStreaming is optimized for robotics RTSP/RTP streaming
//----------------------------------------------------------

/**
 * Stream frame rate options (USER CONFIGURABLE)
 * Only 30 and 60 FPS supported for optimal encoder performance
 */
UENUM(BlueprintType)
enum class EStreamFrameRate : uint8
{
	FPS_30 UMETA(DisplayName = "30 FPS"),
	FPS_60 UMETA(DisplayName = "60 FPS")
};

/**
 * Stream aspect ratio options
 */
UENUM(BlueprintType)
enum class EStreamAspectRatio : uint8
{
	Ratio_16_9 UMETA(DisplayName = "16:9 (Widescreen)"),
	Ratio_4_3  UMETA(DisplayName = "4:3 (Standard)")
};

/**
 * 16:9 Resolution options (separate enum for aspect ratio selection)
 * Ultra-low latency safe resolutions (16-pixel aligned WIDTH)
 * Optimized for H.264 Baseline profile with <100ms latency
 */
UENUM(BlueprintType)
enum class EStreamResolution_16_9 : uint8
{
	R16_9_360p  UMETA(DisplayName = "360p (640x360)"),
	R16_9_540p  UMETA(DisplayName = "540p (960x540)"),
	R16_9_576p  UMETA(DisplayName = "576p (1024x576)"),
	R16_9_720p  UMETA(DisplayName = "720p (1280x720) HD"),
	R16_9_900p  UMETA(DisplayName = "900p (1600x900)"),
	R16_9_1080p UMETA(DisplayName = "1080p (1920x1080) Full HD")
};

/**
 * 4:3 Resolution options (separate enum for aspect ratio selection)
 * Ultra-low latency safe resolutions (16-pixel aligned WIDTH)
 * Optimized for H.264 Baseline profile with <100ms latency
 */
UENUM(BlueprintType)
enum class EStreamResolution_4_3 : uint8
{
	R4_3_240p  UMETA(DisplayName = "240p (320x240) QVGA"),
	R4_3_480p  UMETA(DisplayName = "480p (640x480) VGA"),
	R4_3_600p  UMETA(DisplayName = "600p (800x600) SVGA"),
	R4_3_768p  UMETA(DisplayName = "768p (1024x768) XGA"),
	R4_3_960p  UMETA(DisplayName = "960p (1280x960)"),
	R4_3_1200p UMETA(DisplayName = "1200p (1600x1200)")
};

/**
 * Safe ultra-low latency resolutions (USER CONFIGURABLE)
 * Optimized for H.264 Baseline profile with <100ms latency target
 * All resolutions have 16-pixel aligned WIDTH for NVENC/AMF hardware encoder
 *
 * REMOVED resolutions:
 * - 426x240 (width not 16-aligned, caused "Invalid level prefix" H.264 decoder errors)
 * - 854x480 (width not 16-aligned)
 * - 1366x768 (width not 16-aligned)
 * - 1400x1050 (width not 16-aligned)
 * - 1920x1440 (too high for ultra-low latency Baseline profile)
 * - 2560x1440 QHD (too high for ultra-low latency Baseline profile)
 * - 3840x2160 4K (too high for ultra-low latency Baseline profile)
 */
UENUM(BlueprintType)
enum class EStreamResolution : uint8
{
	// 16:9 Safe Resolutions (width 16-aligned, <=1080p for ultra-low latency)
	R16_9_360p UMETA(DisplayName = "360p (640x360)"),
	R16_9_540p UMETA(DisplayName = "540p (960x540)"),
	R16_9_576p UMETA(DisplayName = "576p (1024x576)"),
	R16_9_720p UMETA(DisplayName = "720p (1280x720) HD"),
	R16_9_900p UMETA(DisplayName = "900p (1600x900)"),
	R16_9_1080p UMETA(DisplayName = "1080p (1920x1080) Full HD"),

	// 4:3 Safe Resolutions (width 16-aligned, <=1200p for ultra-low latency)
	R4_3_240p UMETA(DisplayName = "240p (320x240) QVGA"),
	R4_3_480p UMETA(DisplayName = "480p (640x480) VGA"),
	R4_3_600p UMETA(DisplayName = "600p (800x600) SVGA"),
	R4_3_768p UMETA(DisplayName = "768p (1024x768) XGA"),
	R4_3_960p UMETA(DisplayName = "960p (1280x960)"),
	R4_3_1200p UMETA(DisplayName = "1200p (1600x1200)")
};

/**
 * H.264 encoding profiles (LOCKED TO BASELINE)
 */
UENUM(BlueprintType)
enum class EH264Profile : uint8
{
	Baseline UMETA(DisplayName = "Baseline (Maximum Compatibility)")
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
 */
USTRUCT(BlueprintType)
struct REALGAZEBOSTREAMING_API FStreamKey
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Stream")
	FVehicleID VehicleID;

	UPROPERTY(BlueprintReadWrite, Category = "Stream")
	FString VehicleTypeName;

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

	FString ToString() const
	{
		FString VehicleIdentifier = VehicleTypeName.IsEmpty()
			? FString::Printf(TEXT("%d_%d"), VehicleID.VehicleType, VehicleID.VehicleNum)
			: VehicleTypeName;

		if (CameraID.IsEmpty())
		{
			return VehicleIdentifier;
		}
		return FString::Printf(TEXT("%s/%s"), *VehicleIdentifier, *CameraID);
	}

	bool operator==(const FStreamKey& Other) const
	{
		return VehicleID == Other.VehicleID
			&& VehicleTypeName.Equals(Other.VehicleTypeName)
			&& CameraID.Equals(Other.CameraID);
	}

	friend uint32 GetTypeHash(const FStreamKey& Key)
	{
		uint32 Hash = GetTypeHash(Key.VehicleID);
		Hash = HashCombine(Hash, GetTypeHash(Key.VehicleTypeName));
		Hash = HashCombine(Hash, GetTypeHash(Key.CameraID));
		return Hash;
	}

	/** Debug: Get detailed stream key information for isolation verification */
	FString ToDebugString() const
	{
		const uint32 Hash = GetTypeHash(*this);
		return FString::Printf(
			TEXT("[StreamKey] Path='%s' | VID=(%d,%d) | Type='%s' | Cam='%s' | Hash=0x%08X"),
			*ToString(),
			VehicleID.VehicleType, VehicleID.VehicleNum,
			*VehicleTypeName,
			CameraID.IsEmpty() ? TEXT("<none>") : *CameraID,
			Hash
		);
	}
};

/**
 * ULTRA-LOW LATENCY STREAM CONFIGURATION
 *
 * USER CONTROLS (only 2 settings):
 * - Resolution (safe presets: 240p-1080p for 16:9, 240p-1200p for 4:3)
 * - Frame Rate (30/60 FPS only)
 *
 * AUTOMATIC SETTINGS (system-computed, optimized for ultra-low latency):
 * - Profile: Baseline (locked - maximum compatibility)
 * - GOP Size: FPS (1.0s keyframe interval - industry standard for RTSP)
 * - Bitrate: 600-8000 kbps CBR (frame rate aware, optimized)
 * - B-frames: 0 (zero latency penalty)
 *
 * Design Philosophy:
 * - CBR (Constant Bitrate) for predictable network bandwidth
 * - Frame rate aware bitrate (60 FPS gets 30-50% more due to temporal complexity)
 * - Conservative bitrates to prevent network congestion
 * - Safe margin below 8 Mbps ceiling for adaptive quality headroom
 * - 1.0s GOP (industry standard for RTSP/RTP streaming)
 * - <100ms latency target for robotics/drone control
 */
USTRUCT(BlueprintType)
struct REALGAZEBOSTREAMING_API FRealGazeboStreamConfig
{
	GENERATED_BODY()

	//========================================
	// USER SETTINGS (Only These Are Editable)
	//========================================

	/** Aspect Ratio - 16:9 or 4:3 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stream Settings")
	EStreamAspectRatio AspectRatio = EStreamAspectRatio::Ratio_16_9;

	/** Resolution - Safe ultra-low latency presets only */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stream Settings")
	EStreamResolution Resolution = EStreamResolution::R16_9_720p;

	/** Frame Rate - 15/30/60 FPS */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stream Settings")
	EStreamFrameRate FrameRate = EStreamFrameRate::FPS_30;

	//========================================
	// AUTO-COMPUTED (Read-Only, System Managed)
	//========================================

	/** Dimensions in pixels (computed from Resolution) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Auto-Computed")
	FIntPoint Dimensions = FIntPoint(1280, 720);

	/** Bitrate in kbps (auto-computed: 2-8 Mbps based on resolution) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Auto-Computed")
	int32 BitrateKbps = 0;

	/** FPS value (computed from FrameRate enum) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Auto-Computed")
	int32 FPSValue = 30;

	/** GOP size (auto-computed: FPS for 1.0s keyframe interval, industry standard for RTSP) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Auto-Computed")
	int32 GOPSize = 30;

	/** H.264 Profile (LOCKED to Baseline) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Auto-Computed")
	EH264Profile EncodingProfile = EH264Profile::Baseline;

	//========================================
	// INTERNAL (Not Exposed to Blueprints)
	//========================================

	int32 RTSPPort = 8554;
	int32 MaxQueueSize = 10;
	bool bAllowFrameDropping = true;
	bool bEnableAdaptiveQuality = false;

	FRealGazeboStreamConfig()
	{
		UpdateComputedValues();
	}

	/** Compute all automatic settings based on Resolution and FrameRate */
	void UpdateComputedValues();

	/** Validate ultra-low latency requirements */
	bool IsValid(FString& OutErrorMessage) const;
};

/**
 * High-precision timing utilities for video streaming
 */
namespace RealGazeboStreamingTime
{
	inline int64 GetTimeMicroseconds()
	{
		return static_cast<int64>(FPlatformTime::Seconds() * 1000000.0);
	}

	inline double GetTimeMilliseconds()
	{
		return FPlatformTime::Seconds() * 1000.0;
	}

	inline int64 SecondsToMicroseconds(double Seconds)
	{
		return static_cast<int64>(Seconds * 1000000.0);
	}

	inline double MicrosecondsToMilliseconds(int64 Microseconds)
	{
		return Microseconds / 1000.0;
	}

	inline int64 MillisecondsToMicroseconds(double Milliseconds)
	{
		return static_cast<int64>(Milliseconds * 1000.0);
	}

	inline int64 GetElapsedMicroseconds(int64 StartTimeUs)
	{
		return GetTimeMicroseconds() - StartTimeUs;
	}

	inline double GetElapsedMilliseconds(int64 StartTimeUs)
	{
		return MicrosecondsToMilliseconds(GetElapsedMicroseconds(StartTimeUs));
	}
}

/**
 * Ultra-low latency streaming constants
 */
namespace RealGazeboStreamingConstants
{
	// Performance settings
	constexpr int32 FRAME_POOL_SIZE = 20;
	constexpr int32 MAX_QUEUE_SIZE = 10;
	constexpr bool ALLOW_FRAME_DROPPING = true;

	// Network settings
	constexpr bool ENABLE_STREAMING = true;
	constexpr int32 RTSP_PORT = 8554;

	// Default configuration
	constexpr EStreamResolution DEFAULT_RESOLUTION = EStreamResolution::R16_9_720p;
	constexpr EStreamFrameRate DEFAULT_FRAME_RATE = EStreamFrameRate::FPS_30;
	constexpr EH264Profile DEFAULT_H264_PROFILE = EH264Profile::Baseline;

	// Ultra-low latency constraints (enforced by encoders)
	// Optimized for CBR streaming with frame rate awareness
	constexpr int32 MIN_BITRATE_KBPS = 600;     // Minimum: 240p @ 30fps
	constexpr int32 MAX_BITRATE_KBPS = 8000;    // Maximum: 1080p/1200p @ 60fps (ceiling for headroom)
	constexpr int32 MIN_GOP_SIZE = 30;          // Minimum GOP (1.0s @ 30fps - industry standard)
	constexpr int32 MAX_GOP_SIZE = 60;          // Maximum GOP (1.0s @ 60fps - industry standard)

	// Hardware encoding (no software fallback)
	constexpr bool PREFER_HARDWARE_ENCODING = true;
}
