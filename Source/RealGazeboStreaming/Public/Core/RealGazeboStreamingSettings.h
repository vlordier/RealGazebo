// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Core/RealGazeboStreamingTypes.h"
#include "RealGazeboStreamingSettings.generated.h"

/**
 * RealGazebo Streaming Settings
 * Accessible via Project Settings > Plugins > RealGazebo Streaming
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="RealGazebo Streaming"))
class REALGAZEBOSTREAMING_API URealGazeboStreamingSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	URealGazeboStreamingSettings();

	//~ Begin UDeveloperSettings Interface
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;
	//~ End UDeveloperSettings Interface

	/** Get settings singleton */
	static const URealGazeboStreamingSettings* Get();

	// ========================================
	// General Settings
	// ========================================

	/** Enable streaming system */
	UPROPERTY(Config, EditAnywhere, Category = "General", meta = (
		DisplayName = "Enable Streaming",
		ToolTip = "Enable the RealGazebo streaming system. Disabling will prevent all streams from starting."))
	bool bEnableStreaming = true;

	/** Default RTSP port */
	UPROPERTY(Config, EditAnywhere, Category = "General", meta = (
		DisplayName = "RTSP Port",
		ToolTip = "Default RTSP server port. Standard port is 8554.",
		ClampMin = "1024", ClampMax = "65535"))
	int32 RTSPPort = 8554;

	/** Auto-start streaming on BeginPlay */
	UPROPERTY(Config, EditAnywhere, Category = "General", meta = (
		DisplayName = "Auto Start Streaming",
		ToolTip = "Automatically start streaming when cameras are spawned."))
	bool bAutoStartStreaming = false;

	// ========================================
	// Default Configuration
	// ========================================

	/** Default aspect ratio */
	UPROPERTY(Config, EditAnywhere, Category = "Defaults", meta = (
		DisplayName = "Default Aspect Ratio",
		ToolTip = "Default aspect ratio for new streams."))
	EStreamAspectRatio DefaultAspectRatio = EStreamAspectRatio::Ratio_16_9;

	/** Default resolution */
	UPROPERTY(Config, EditAnywhere, Category = "Defaults", meta = (
		DisplayName = "Default Resolution",
		ToolTip = "Default resolution for new streams."))
	EStreamResolution DefaultResolution = EStreamResolution::R16_9_720p;

	/** Default frame rate */
	UPROPERTY(Config, EditAnywhere, Category = "Defaults", meta = (
		DisplayName = "Default Frame Rate",
		ToolTip = "Default frame rate for new streams."))
	EStreamFrameRate DefaultFrameRate = EStreamFrameRate::FPS_30;

	/** Default quality */
	UPROPERTY(Config, EditAnywhere, Category = "Defaults", meta = (
		DisplayName = "Default Quality",
		ToolTip = "Default quality preset for new streams."))
	EStreamQuality DefaultQuality = EStreamQuality::High;

	/** Default H.264 profile */
	UPROPERTY(Config, EditAnywhere, Category = "Defaults", meta = (
		DisplayName = "Default H.264 Profile",
		ToolTip = "Default H.264 encoding profile."))
	EH264Profile DefaultEncodingProfile = EH264Profile::Main;

	/** Default GOP size */
	UPROPERTY(Config, EditAnywhere, Category = "Defaults", meta = (
		DisplayName = "Default GOP Size",
		ToolTip = "Default Group of Pictures size (keyframe interval).",
		ClampMin = "15", ClampMax = "300"))
	int32 DefaultGOPSize = 60;

	/** Enable adaptive quality by default */
	UPROPERTY(Config, EditAnywhere, Category = "Defaults", meta = (
		DisplayName = "Enable Adaptive Quality",
		ToolTip = "Enable adaptive quality system that reduces bitrate when queues fill up."))
	bool bEnableAdaptiveQuality = true;

	// ========================================
	// Performance Settings
	// ========================================

	/** Maximum concurrent streams */
	UPROPERTY(Config, EditAnywhere, Category = "Performance", meta = (
		DisplayName = "Max Concurrent Streams",
		ToolTip = "Maximum number of concurrent streams. Exceeding this limit will fail stream creation.",
		ClampMin = "1", ClampMax = "50"))
	int32 MaxConcurrentStreams = 20;

	/** Frame pool size per resolution */
	UPROPERTY(Config, EditAnywhere, Category = "Performance", meta = (
		DisplayName = "Frame Pool Size",
		ToolTip = "Maximum number of frames to keep in pool per resolution. Higher = less allocations but more memory.",
		ClampMin = "5", ClampMax = "50"))
	int32 FramePoolSize = 20;

	/** Conversion thread priority (note: configured in code, not exposed) */
	bool bConversionThreadHighPriority = true;

	/** Encoding thread priority (note: configured in code, not exposed) */
	bool bEncodingThreadHighPriority = true;

	/** RTSP thread priority (note: configured in code, not exposed) */
	bool bRTSPThreadHighPriority = false;

	/** Maximum queue size */
	UPROPERTY(Config, EditAnywhere, Category = "Performance|Queues", meta = (
		DisplayName = "Max Queue Size",
		ToolTip = "Maximum size for conversion/encoding/RTSP queues. Lower = less latency but more drops.",
		ClampMin = "5", ClampMax = "30"))
	int32 MaxQueueSize = 10;

	/** Allow frame dropping */
	UPROPERTY(Config, EditAnywhere, Category = "Performance|Queues", meta = (
		DisplayName = "Allow Frame Dropping",
		ToolTip = "Allow dropping frames when queues are full. Disabling may cause blocking."))
	bool bAllowFrameDropping = true;

	// ========================================
	// Encoding Settings
	// ========================================

	/** Prefer hardware encoding */
	UPROPERTY(Config, EditAnywhere, Category = "Encoding", meta = (
		DisplayName = "Prefer Hardware Encoding",
		ToolTip = "Prefer hardware encoding (NVENC/AMF) over software encoding."))
	bool bPreferHardwareEncoding = true;

	/** Force NVENC (for debugging) */
	UPROPERTY(Config, EditAnywhere, Category = "Encoding|Debug", meta = (
		DisplayName = "Force NVENC",
		ToolTip = "Force NVENC encoder even if AMD GPU detected. For debugging only."))
	bool bForceNVENC = false;

	/** Force AMF (for debugging) */
	UPROPERTY(Config, EditAnywhere, Category = "Encoding|Debug", meta = (
		DisplayName = "Force AMF",
		ToolTip = "Force AMD AMF encoder even if NVIDIA GPU detected. For debugging only."))
	bool bForceAMF = false;

	// ========================================
	// Debug Settings
	// ========================================

	/** Enable verbose logging */
	UPROPERTY(Config, EditAnywhere, Category = "Debug", meta = (
		DisplayName = "Verbose Logging",
		ToolTip = "Enable verbose logging for debugging."))
	bool bVerboseLogging = false;

	/** Show performance stats overlay */
	UPROPERTY(Config, EditAnywhere, Category = "Debug", meta = (
		DisplayName = "Show Performance Stats",
		ToolTip = "Show performance statistics overlay on screen."))
	bool bShowPerformanceStats = false;

	/** Stats update interval (seconds) */
	UPROPERTY(Config, EditAnywhere, Category = "Debug", meta = (
		DisplayName = "Stats Update Interval",
		ToolTip = "How often to update performance statistics (seconds).",
		ClampMin = "0.1", ClampMax = "5.0"))
	float StatsUpdateInterval = 1.0f;
};
