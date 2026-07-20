// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "GazeboBridgeTypes.h" // For FVehicleID
#include "StreamingTypes.generated.h"

//----------------------------------------------------------
// Stream Resolution Presets
// Optimized for robotics and industrial cameras with 4:3 aspect ratio.
// These resolutions are widely supported by machine vision cameras.
//----------------------------------------------------------

UENUM(BlueprintType)
enum class EStreamResolution : uint8
{
	VGA_640x480      UMETA(DisplayName = "VGA 640x480"),
	SVGA_800x600     UMETA(DisplayName = "SVGA 800x600"),
	XGA_1024x768     UMETA(DisplayName = "XGA 1024x768"),
	SXGA_1280x960    UMETA(DisplayName = "SXGA 1280x960"),
	UXGA_1600x1200   UMETA(DisplayName = "UXGA 1600x1200"),
};

//----------------------------------------------------------
// Stream Frame Rate Presets
// All frame rates are optimized for ultra-low-latency encoding.
// Higher frame rates provide smoother video but require more bandwidth and GPU resources.
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
// Encoder Preset
// Determines the latency-quality tradeoff for hardware encoding.
// UltraLowLatency: Minimizes encoding delay (<16ms per frame), ideal for real-time applications.
//----------------------------------------------------------

UENUM(BlueprintType)
enum class EEncoderPreset : uint8
{
	UltraLowLatency  UMETA(DisplayName = "Ultra Low Latency"),
};

//----------------------------------------------------------
// H264 Profile
// Determines codec compatibility and compression features.
// Baseline: Maximum compatibility with all devices, including mobile clients.
//----------------------------------------------------------

UENUM(BlueprintType)
enum class ERealGazeboH264Profile : uint8
{
	Baseline         UMETA(DisplayName = "Baseline"),
};

//----------------------------------------------------------
// Encoder Type
// Hardware encoder automatically detected based on available GPU.
// NVENC: NVIDIA GPU hardware encoding (via CUDA on Linux, Direct3D on Windows).
// AMF: AMD GPU hardware encoding (via Vulkan on Linux, Direct3D on Windows).
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
// Represents the current lifecycle state of a streaming pipeline.
//----------------------------------------------------------

UENUM(BlueprintType)
enum class EStreamState : uint8
{
	Idle             UMETA(DisplayName = "Idle"),             // Stream created but not started
	Initializing     UMETA(DisplayName = "Initializing"),     // Encoder and RTSP session initializing
	Active           UMETA(DisplayName = "Active"),           // Stream encoding and available for clients
	Error            UMETA(DisplayName = "Error"),            // Stream encountered an error
	Stopping         UMETA(DisplayName = "Stopping")          // Stream shutting down
};

//----------------------------------------------------------
// Stream Identifier
// Uniquely identifies each camera stream by combining vehicle and camera IDs.
// This identifier is used for stream registration, RTSP URL generation, and pipeline management.
//----------------------------------------------------------

USTRUCT(BlueprintType)
struct REALGAZEBOSTREAMING_API FStreamIdentifier
{
	GENERATED_BODY()

	/** Vehicle identification - automatically populated from the owning vehicle pawn */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Identifier")
	FVehicleID VehicleID;

	/** Camera identifier - user-defined name such as "front", "right", "left", "bottom", "fpv" */
	UPROPERTY(BlueprintReadWrite, Category = "Stream Identifier")
	FString CameraID = TEXT("front");

	/** Vehicle type name (e.g., "x500", "iris") - used in RTSP URLs for better readability */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Identifier")
	FString VehicleTypeName;

	FStreamIdentifier() = default;
	FStreamIdentifier(const FVehicleID& InVehicleID, const FString& InCameraID)
		: VehicleID(InVehicleID), CameraID(InCameraID) {}
	FStreamIdentifier(const FVehicleID& InVehicleID, const FString& InCameraID, const FString& InVehicleTypeName)
		: VehicleID(InVehicleID), CameraID(InCameraID), VehicleTypeName(InVehicleTypeName) {}

	/**
	 * Convert stream identifier to human-readable string format.
	 * Format: vehicle_type_name_num/camera_id (e.g., "x500_1/front")
	 * Falls back to numeric format if vehicle type name is not set.
	 */
	FString ToString() const
	{
		if (!VehicleTypeName.IsEmpty())
		{
			return FString::Printf(TEXT("%s_%d/%s"), *VehicleTypeName.ToLower(), VehicleID.VehicleNum, *CameraID);
		}
		// Fallback: Use numeric vehicle type code when name is unavailable
		return FString::Printf(TEXT("%s/%s"), *VehicleID.ToString(), *CameraID);
	}

	/**
	 * Get RTSP path segment for URL construction.
	 * Returns the same format as ToString() for consistency.
	 */
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
// Stream Configuration
// User-configurable settings for video stream quality and performance.
// Additional parameters like bitrate and GOP size are automatically calculated.
//----------------------------------------------------------

USTRUCT(BlueprintType)
struct REALGAZEBOSTREAMING_API FStreamConfig
{
	GENERATED_BODY()

	/** Stream resolution preset - default: XGA 1024x768 (optimal for robotics cameras) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stream Config")
	EStreamResolution Resolution = EStreamResolution::XGA_1024x768;

	/** Target frame rate - higher values provide smoother video but require more resources */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stream Config")
	EStreamFrameRate FrameRate = EStreamFrameRate::FPS_30;

	//----------------------------------------------------------
	// Internal settings (not exposed to users)
	// These are hardcoded for ultra-low latency streaming:
	// - Bitrate: Auto-calculated based on resolution + FPS (see GetBitrate())
	// - GOP Size: Auto-calculated as FPS/2 for 0.5s keyframe interval (see GetGOPSize())
	//----------------------------------------------------------
	EEncoderPreset Preset = EEncoderPreset::UltraLowLatency;
	ERealGazeboH264Profile Profile = ERealGazeboH264Profile::Baseline;
	bool bZeroCopy = true;

	/**
	 * Get actual resolution dimensions in pixels.
	 * @param OutWidth - Output width in pixels
	 * @param OutHeight - Output height in pixels
	 */
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
				// Fallback: Default to XGA (optimal balance for robotics)
				OutWidth = 1024;
				OutHeight = 768;
				break;
		}
	}

	/**
	 * Get frame rate as integer value.
	 * @return Frame rate in frames per second (defaults to 30 if invalid)
	 */
	int32 GetFrameRateValue() const
	{
		if (FrameRate == EStreamFrameRate::Invalid)
		{
			return 30; // Default: 30 FPS for reliable streaming
		}
		return static_cast<int32>(FrameRate);
	}

	/**
	 * Calculate optimal bitrate based on resolution and frame rate.
	 * Bitrate is auto-calculated to balance quality and network bandwidth.
	 * @return Bitrate in kilobits per second (kbps)
	 */
	int32 CalculateOptimalBitrate() const
	{
		// Base bitrate values calibrated for 30 FPS (in kbps)
		int32 BaseBitrate = 2000;

		switch (Resolution)
		{
			case EStreamResolution::VGA_640x480:
				BaseBitrate = 1000;  // 640x480: Low bandwidth for remote/mobile viewing
				break;
			case EStreamResolution::SVGA_800x600:
				BaseBitrate = 1500;  // 800x600: Moderate quality
				break;
			case EStreamResolution::XGA_1024x768:
				BaseBitrate = 2000;  // 1024x768: Default balanced quality
				break;
			case EStreamResolution::SXGA_1280x960:
				BaseBitrate = 4000;  // 1280x960: High quality
				break;
			case EStreamResolution::UXGA_1600x1200:
				BaseBitrate = 6000;  // 1600x1200: Maximum quality
				break;
		}

		// Apply frame rate multiplier to account for temporal complexity
		float FPSMultiplier = 1.0f;
		switch (FrameRate)
		{
			case EStreamFrameRate::Invalid:
				FPSMultiplier = 1.0f;   // Default: 30 FPS baseline
				break;
			case EStreamFrameRate::FPS_15:
				FPSMultiplier = 0.67f;  // 15 FPS: Reduced temporal data
				break;
			case EStreamFrameRate::FPS_30:
				FPSMultiplier = 1.0f;   // 30 FPS: Baseline reference
				break;
			case EStreamFrameRate::FPS_60:
				FPSMultiplier = 1.7f;   // 60 FPS: Increased temporal data
				break;
		}

		return FMath::RoundToInt(BaseBitrate * FPSMultiplier);
	}

	/**
	 * Get encoder bitrate in kilobits per second.
	 * This value is automatically calculated - users do not configure it directly.
	 * @return Bitrate in kbps
	 */
	int32 GetBitrate() const
	{
		return CalculateOptimalBitrate();
	}

	/**
	 * Calculate optimal GOP (Group of Pictures) size for ultra-low latency.
	 * GOP determines how often keyframes (I-frames) are inserted.
	 * Smaller GOP = Lower latency + Faster client connection, but higher bandwidth.
	 * Target: Keyframe every 0.5 seconds (GOP = FPS / 2).
	 * @return GOP size in frames
	 */
	int32 CalculateOptimalGOPSize() const
	{
		// Calculate GOP for 0.5 second keyframe interval
		int32 TargetGOP = GetFrameRateValue() / 2;

		// Clamp to safe range (minimum 5 for encoding stability, maximum 30 for latency)
		return FMath::Clamp(TargetGOP, 5, 30);
	}

	/**
	 * Get GOP size for encoder configuration.
	 * Always auto-calculated as FPS/2 for 0.5 second keyframe intervals.
	 * @return GOP size in frames
	 */
	int32 GetGOPSize() const
	{
		return CalculateOptimalGOPSize();
	}

	/**
	 * Get optimal frame pool size for GPU texture reuse.
	 * Pool size is calibrated to maintain approximately 130ms of buffering.
	 * Larger pools reduce GPU memory allocation overhead.
	 * @return Number of frames to keep in the reusable pool
	 */
	int32 GetFramePoolSize() const
	{
		const int32 FPS = GetFrameRateValue();

		if (FPS <= 20) return 2;   // 15 FPS: 2 frames ~133ms buffer
		if (FPS <= 40) return 4;   // 30 FPS: 4 frames ~133ms buffer
		if (FPS <= 60) return 8;   // 60 FPS: 8 frames ~133ms buffer

		return 8;  // Future-proof: Maximum pool size for higher frame rates
	}

	/**
	 * Get optimal frame queue size for encoder input buffering.
	 * Queue is 2x the pool size to absorb encoder timing jitter (~260ms buffer).
	 * Prevents frame drops when encoder experiences temporary delays.
	 * @return Maximum number of frames queued for encoding
	 */
	int32 GetFrameQueueSize() const
	{
		const int32 PoolSize = GetFramePoolSize();

		// Queue size: Double the pool size to absorb encoder jitter
		int32 QueueSize = PoolSize * 2;

		// Clamp to safe memory bounds (4-16 frames)
		return FMath::Clamp(QueueSize, 4, 16);
	}

	/**
	 * Get optimal NAL queue size for network buffering.
	 * Maintains 1 second of encoded frames to handle network jitter and client lag.
	 * Protects against temporary network congestion without dropping frames.
	 * @return Maximum number of NAL units (encoded frames) in network queue
	 */
	int32 GetNALQueueSize() const
	{
		const int32 FPS = GetFrameRateValue();

		// Target: 1 second of encoded video
		const float TargetSeconds = 1.0f;
		int32 NALs = FMath::RoundToInt(FPS * TargetSeconds);

		// Clamp to reasonable memory bounds (16-96 NAL units)
		return FMath::Clamp(NALs, 16, 96);
	}

	/**
	 * Get maximum allowed NAL unit size based on resolution.
	 * Prevents rejection of large I-frames (keyframes) which can be substantial.
	 * Values are calibrated from real-world measurements to include safety margin.
	 * @return Maximum NAL unit size in bytes
	 */
	int32 GetMaxNALSizeBytes() const
	{
		// Note: I-frame sizes measured in production showed XGA reaching 137KB
		// Values include generous safety margin to prevent frame rejection
		switch (Resolution)
		{
			case EStreamResolution::UXGA_1600x1200:
				return 262144;  // 256 KB - Very large I-frames at maximum resolution
			case EStreamResolution::SXGA_1280x960:
				return 196608;  // 192 KB - Large I-frames at high resolution
			case EStreamResolution::XGA_1024x768:
				return 163840;  // 160 KB - Measured at 137KB, margin for quality spikes
			case EStreamResolution::SVGA_800x600:
				return 131072;  // 128 KB - Medium I-frames
			default:
				return 98304;   // 96 KB - Small I-frames for VGA resolution
		}
	}
};

//----------------------------------------------------------
// Stream Information
// Runtime status and performance metrics for an active stream.
// Read-only data exposed to Blueprint for monitoring and UI display.
//----------------------------------------------------------

USTRUCT(BlueprintType)
struct REALGAZEBOSTREAMING_API FStreamInfo
{
	GENERATED_BODY()

	/** Unique stream identifier (vehicle + camera) */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Info")
	FStreamIdentifier StreamID;

	/** RTSP URL for client connections (e.g., rtsp://localhost:8554/x500_1/front) */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Info")
	FString RTSPURL;

	/** Current lifecycle state of the stream */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Info")
	EStreamState State = EStreamState::Idle;

	/** Active stream configuration (resolution, FPS, encoding settings) */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Info")
	FStreamConfig Config;

	/** Hardware encoder being used (NVENC for NVIDIA, AMF for AMD) */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Info")
	EEncoderType EncoderType = EEncoderType::Unknown;

	/** Number of RTSP clients currently watching this stream */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Info")
	int32 ConnectedClients = 0;

	/** Total number of frames encoded since stream started */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Info")
	int64 TotalFramesEncoded = 0;

	/** Actual encoding frame rate (may differ from target due to performance) */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Info")
	float ActualFPS = 0.0f;

	/** Average time to encode each frame in milliseconds */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Info")
	float AvgEncodingTimeMs = 0.0f;

	/** Timestamp when this stream was created */
	UPROPERTY(BlueprintReadOnly, Category = "Stream Info")
	FDateTime CreatedTime;
};

//----------------------------------------------------------
// Event Delegates
// Blueprint-compatible delegates for streaming system events.
// Use these to respond to stream lifecycle changes and errors.
//----------------------------------------------------------

/** Fired when the streaming subsystem starts and RTSP server is ready */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStreamingStarted);

/** Fired when the streaming subsystem stops and RTSP server shuts down */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStreamingStopped);

/**
 * Fired when a new stream is created and available for clients.
 * @param VehicleID - Vehicle identification
 * @param CameraID - Camera identifier (e.g., "front")
 * @param RTSPURL - Full RTSP URL for client connections
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnStreamCreated,
	const FVehicleID&, VehicleID,
	const FString&, CameraID,
	const FString&, RTSPURL);

/**
 * Fired when a stream is destroyed and no longer available.
 * @param VehicleID - Vehicle identification
 * @param CameraID - Camera identifier
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnStreamDestroyed,
	const FVehicleID&, VehicleID,
	const FString&, CameraID);

/**
 * Fired when an encoding error occurs during stream operation.
 * @param VehicleID - Vehicle identification
 * @param CameraID - Camera identifier
 * @param ErrorMessage - Description of the error
 */
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
