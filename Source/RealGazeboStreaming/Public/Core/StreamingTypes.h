// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "GazeboBridgeTypes.h" // For FVehicleID
#include "StreamingTypes.generated.h"

//----------------------------------------------------------
// Stream Resolution Presets (Optimized for Robotics/Industrial Cameras - 4:3 Aspect Ratio)
//----------------------------------------------------------

UENUM(BlueprintType)
enum class EStreamResolution : uint8
{
	// Standard robotics camera resolutions (4:3 aspect ratio)
	VGA_640x480      UMETA(DisplayName = "VGA 640x480"),
	SVGA_800x600     UMETA(DisplayName = "SVGA 800x600"),
	XGA_1024x768     UMETA(DisplayName = "XGA 1024x768"),
	SXGA_1280x960    UMETA(DisplayName = "SXGA 1280x960"),
	UXGA_1600x1200   UMETA(DisplayName = "UXGA 1600x1200"),
};

//----------------------------------------------------------
// Stream Frame Rate Presets (Ultra-Low-Latency Compatible)
//----------------------------------------------------------

UENUM(BlueprintType)
enum class EStreamFrameRate : uint8
{
	Invalid = 0  UMETA(Hidden),
	FPS_15 = 15  UMETA(DisplayName = "15 FPS"),
	FPS_30 = 30  UMETA(DisplayName = "30 FPS"),
	FPS_60 = 60  UMETA(DisplayName = "60 FPS")
};

//----------------------------------------------------------
// Encoder Preset (Latency vs Quality Trade-off)
// All presets supported - choose based on stream count and latency requirements
//----------------------------------------------------------

UENUM(BlueprintType)
enum class EEncoderPreset : uint8
{
	UltraLowLatency  UMETA(DisplayName = "Ultra Low Latency"),
};

//----------------------------------------------------------
// H264 Profile (Compatibility vs Quality)
// All profiles supported - choose based on client device capabilities
//----------------------------------------------------------

UENUM(BlueprintType)
enum class EH264Profile : uint8
{
	Baseline         UMETA(DisplayName = "Baseline"),
};

//----------------------------------------------------------
// Encoder Type (Hardware Acceleration)
//----------------------------------------------------------

UENUM(BlueprintType)
enum class EEncoderType : uint8
{
	Unknown          UMETA(DisplayName = "Unknown"),
	NVENC            UMETA(DisplayName = "NVENC (NVIDIA GPU)"),
	AMF              UMETA(DisplayName = "AMF (AMD GPU)"),
};

//----------------------------------------------------------
// Stream State
//----------------------------------------------------------

UENUM(BlueprintType)
enum class EStreamState : uint8
{
	Idle             UMETA(DisplayName = "Idle"),
	Initializing     UMETA(DisplayName = "Initializing"),
	Active           UMETA(DisplayName = "Active"),
	Error            UMETA(DisplayName = "Error"),
	Stopping         UMETA(DisplayName = "Stopping")
};

//----------------------------------------------------------
// Stream Identifier (Vehicle + Camera)
//----------------------------------------------------------

USTRUCT(BlueprintType)
struct REALGAZEBOSTREAMING_API FStreamIdentifier
{
	GENERATED_BODY()

	/** Vehicle identification (auto-populated from vehicle pawn) */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Identifier")
	FVehicleID VehicleID;

	/** Camera identifier (e.g., "front", "right", "left", "bottom", "fpv") */
	UPROPERTY(BlueprintReadWrite, Category = "Stream Identifier")
	FString CameraID = TEXT("front");

	/** Vehicle type name (e.g., "x500", "iris") - used in RTSP URL instead of numeric code */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Identifier")
	FString VehicleTypeName;

	FStreamIdentifier() = default;
	FStreamIdentifier(const FVehicleID& InVehicleID, const FString& InCameraID)
		: VehicleID(InVehicleID), CameraID(InCameraID) {}
	FStreamIdentifier(const FVehicleID& InVehicleID, const FString& InCameraID, const FString& InVehicleTypeName)
		: VehicleID(InVehicleID), CameraID(InCameraID), VehicleTypeName(InVehicleTypeName) {}

	/** Convert to string format: vehicle_type_name_num/camera_id (e.g., x500_1/front) */
	FString ToString() const
	{
		if (!VehicleTypeName.IsEmpty())
		{
			return FString::Printf(TEXT("%s_%d/%s"), *VehicleTypeName.ToLower(), VehicleID.VehicleNum, *CameraID);
		}
		// Fallback to numeric format if name not set
		return FString::Printf(TEXT("%s/%s"), *VehicleID.ToString(), *CameraID);
	}

	/** Get RTSP path segment (used in URL construction) */
	FString ToRTSPPath() const
	{
		return ToString();
	}

	bool operator==(const FStreamIdentifier& Other) const
	{
		return VehicleID == Other.VehicleID && CameraID.Equals(Other.CameraID, ESearchCase::IgnoreCase);
	}

	bool operator!=(const FStreamIdentifier& Other) const
	{
		return !(*this == Other);
	}

	friend uint32 GetTypeHash(const FStreamIdentifier& StreamID)
	{
		return HashCombine(GetTypeHash(StreamID.VehicleID), GetTypeHash(StreamID.CameraID));
	}
};

//----------------------------------------------------------
// Stream Configuration (Runtime Configurable)
//----------------------------------------------------------

USTRUCT(BlueprintType)
struct REALGAZEBOSTREAMING_API FStreamConfig
{
	GENERATED_BODY()

	/** Stream resolution preset (Default: XGA 1024x768 - optimal for robotics cameras) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stream Config")
	EStreamResolution Resolution = EStreamResolution::XGA_1024x768;

	/** Target frame rate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stream Config")
	EStreamFrameRate FrameRate = EStreamFrameRate::FPS_30;

	// Internal settings (hardcoded for ultra-low latency - not exposed to user)
	// Bitrate: Auto-calculated based on resolution + FPS (see GetBitrate())
	// GOP Size: Auto-calculated as FPS/2 for 0.5s keyframe interval (see GetGOPSize())
	EEncoderPreset Preset = EEncoderPreset::UltraLowLatency;
	EH264Profile Profile = EH264Profile::Baseline;
	bool bZeroCopy = true;

	/** Get actual resolution */
	void GetResolution(int32& OutWidth, int32& OutHeight) const
	{
		switch (Resolution)
		{
			case EStreamResolution::VGA_640x480:
				OutWidth = 640;
				OutHeight = 480;
				break;
			case EStreamResolution::SVGA_800x600:
				OutWidth = 800;
				OutHeight = 600;
				break;
			case EStreamResolution::XGA_1024x768:
				OutWidth = 1024;
				OutHeight = 768;
				break;
			case EStreamResolution::SXGA_1280x960:
				OutWidth = 1280;
				OutHeight = 960;
				break;
			case EStreamResolution::UXGA_1600x1200:
				OutWidth = 1600;
				OutHeight = 1200;
				break;
			default:
				// Default to XGA (optimal for robotics)
				OutWidth = 1024;
				OutHeight = 768;
				break;
		}
	}

	/** Get frame rate as integer */
	int32 GetFrameRateValue() const
	{
		// Handle invalid/default case
		if (FrameRate == EStreamFrameRate::Invalid)
		{
			return 30; // Default to 30 FPS
		}
		return static_cast<int32>(FrameRate);
	}

	/** Calculate optimal bitrate based on resolution and frame rate (in kbps) */
	int32 CalculateOptimalBitrate() const
	{
		// Base bitrates at 30fps (in kbps)
		int32 BaseBitrate = 2000;

		switch (Resolution)
		{
			case EStreamResolution::VGA_640x480:
				BaseBitrate = 1000;
				break;
			case EStreamResolution::SVGA_800x600:
				BaseBitrate = 1500;
				break;
			case EStreamResolution::XGA_1024x768:
				BaseBitrate = 2000;
				break;
			case EStreamResolution::SXGA_1280x960:
				BaseBitrate = 4000;
				break;
			case EStreamResolution::UXGA_1600x1200:
				BaseBitrate = 6000;
				break;
		}

		// Scale by frame rate
		float FPSMultiplier = 1.0f;
		switch (FrameRate)
		{
			case EStreamFrameRate::Invalid:
				FPSMultiplier = 1.0f; // Default to 30 FPS multiplier
				break;
			case EStreamFrameRate::FPS_15:
				FPSMultiplier = 0.67f;  // 15 FPS: Less temporal data
				break;
			case EStreamFrameRate::FPS_30:
				FPSMultiplier = 1.0f;   // 30 FPS: Baseline
				break;
			case EStreamFrameRate::FPS_60:
				FPSMultiplier = 1.7f;   // 60 FPS: More temporal data (improved from 1.5x)
				break;
		}

		return FMath::RoundToInt(BaseBitrate * FPSMultiplier);
	}

	/** Get bitrate in kilobits per second (auto-calculated) */
	int32 GetBitrate() const
	{
		return CalculateOptimalBitrate();
	}

	/** Calculate optimal GOP size based on FPS (for 0.5 second keyframe interval) */
	int32 CalculateOptimalGOPSize() const
	{
		// Target: Keyframe every 0.5 seconds for ultra-low latency
		// GOP = FPS * 0.5
		int32 TargetGOP = GetFrameRateValue() / 2;

		// Clamp to reasonable range
		return FMath::Clamp(TargetGOP, 5, 30);
	}

	/** Get GOP size (always auto-calculated as FPS/2 for 0.5s keyframe interval) */
	int32 GetGOPSize() const
	{
		return CalculateOptimalGOPSize();
	}

	/** Get optimal frame pool size based on FPS (maintains ~130ms buffer) */
	int32 GetFramePoolSize() const
	{
		const int32 FPS = GetFrameRateValue();

		if (FPS <= 20) return 2;   // 15 FPS: 2 frames = 133ms
		if (FPS <= 40) return 4;   // 30 FPS: 4 frames = 133ms
		if (FPS <= 60) return 8;   // 60 FPS: 8 frames = 133ms

		return 8;  // Future-proof for higher FPS
	}

	/** Get optimal frame queue size (2x pool size, maintains ~260ms buffer) */
	int32 GetFrameQueueSize() const
	{
		const int32 PoolSize = GetFramePoolSize();

		// Queue = 2x pool, absorbs encoder jitter
		int32 QueueSize = PoolSize * 2;

		// Clamp to safe bounds
		return FMath::Clamp(QueueSize, 4, 16);
	}

	/** Get optimal NAL queue size (1 second buffer for network jitter) */
	int32 GetNALQueueSize() const
	{
		const int32 FPS = GetFrameRateValue();

		// Target: 1 second of encoded frames
		const float TargetSeconds = 1.0f;
		int32 NALs = FMath::RoundToInt(FPS * TargetSeconds);

		// Clamp to reasonable range
		return FMath::Clamp(NALs, 16, 96);
	}

	/** Get maximum NAL unit size based on resolution (prevents I-frame rejection) */
	int32 GetMaxNALSizeBytes() const
	{
		// INCREASED: I-frames can be larger than expected at high quality
		// Error logs showed XGA I-frames reaching 137KB - need generous margin
		switch (Resolution)
		{
			case EStreamResolution::UXGA_1600x1200:
				return 262144;  // 256 KB (very large I-frames at high res)
			case EStreamResolution::SXGA_1280x960:
				return 196608;  // 192 KB (large I-frames)
			case EStreamResolution::XGA_1024x768:
				return 163840;  // 160 KB (XGA I-frames measured at 137KB)
			case EStreamResolution::SVGA_800x600:
				return 131072;  // 128 KB (medium I-frames)
			default:
				return 98304;   // 96 KB (small I-frames for VGA)
		}
	}
};

//----------------------------------------------------------
// Stream Information (Status & Metadata)
//----------------------------------------------------------

USTRUCT(BlueprintType)
struct REALGAZEBOSTREAMING_API FStreamInfo
{
	GENERATED_BODY()

	/** Stream identifier */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Info")
	FStreamIdentifier StreamID;

	/** RTSP URL for this stream */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Info")
	FString RTSPURL;

	/** Current stream state */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Info")
	EStreamState State = EStreamState::Idle;

	/** Current stream configuration */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Info")
	FStreamConfig Config;

	/** Encoder type being used */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Info")
	EEncoderType EncoderType = EEncoderType::Unknown;

	/** Number of currently connected RTSP clients */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Info")
	int32 ConnectedClients = 0;

	/** Total frames encoded since stream start */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Info")
	int64 TotalFramesEncoded = 0;

	/** Current encoding frame rate (actual, may differ from target) */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Info")
	float ActualFPS = 0.0f;

	/** Average encoding time per frame (milliseconds) */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Info")
	float AvgEncodingTimeMs = 0.0f;

	/** Timestamp when stream was created */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Info")
	FDateTime CreatedTime;
};

//----------------------------------------------------------
// Event Delegates
//----------------------------------------------------------

// Called when streaming system starts
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStreamingStarted);

// Called when streaming system stops
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStreamingStopped);

// Called when a new stream is created (VehicleID, CameraID, URL)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnStreamCreated,
	const FVehicleID&, VehicleID,
	const FString&, CameraID,
	const FString&, RTSPURL);

// Called when a stream is destroyed (VehicleID, CameraID)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnStreamDestroyed,
	const FVehicleID&, VehicleID,
	const FString&, CameraID);

// Called when an encoding error occurs (VehicleID, CameraID, ErrorMessage)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnEncodingError,
	const FVehicleID&, VehicleID,
	const FString&, CameraID,
	const FString&, ErrorMessage);

//----------------------------------------------------------
// Helper Functions
//----------------------------------------------------------

inline FString StreamResolutionToString(EStreamResolution Resolution)
{
	switch (Resolution)
	{
		case EStreamResolution::VGA_640x480:      return TEXT("640x480");
		case EStreamResolution::SVGA_800x600:     return TEXT("800x600");
		case EStreamResolution::XGA_1024x768:     return TEXT("1024x768");
		case EStreamResolution::SXGA_1280x960:    return TEXT("1280x960");
		case EStreamResolution::UXGA_1600x1200:   return TEXT("1600x1200");
		default:                                  return TEXT("Unknown");
	}
}

inline FString EncoderPresetToString(EEncoderPreset Preset)
{
	switch (Preset)
	{
		case EEncoderPreset::UltraLowLatency: return TEXT("UltraLowLatency");
		default:                              return TEXT("Unknown");
	}
}

inline FString EncoderTypeToString(EEncoderType Type)
{
	switch (Type)
	{
		case EEncoderType::NVENC:    return TEXT("NVENC");
		case EEncoderType::AMF:      return TEXT("AMF");
		default:                     return TEXT("Unknown");
	}
}

inline FString StreamStateToString(EStreamState State)
{
	switch (State)
	{
		case EStreamState::Idle:         return TEXT("Idle");
		case EStreamState::Initializing: return TEXT("Initializing");
		case EStreamState::Active:       return TEXT("Active");
		case EStreamState::Error:        return TEXT("Error");
		case EStreamState::Stopping:     return TEXT("Stopping");
		default:                         return TEXT("Unknown");
	}
}
