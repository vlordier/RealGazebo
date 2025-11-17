// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/RealGazeboStreamingTypes.h"
#include "Utils/RealGazeboStreamingStats.h"
#include "RealGazeboStreamManager.generated.h"

// Forward declarations
class URealGazeboStreamingSubsystem;
class UGazeboBridgeSubsystem;

/**
 * RealGazebo Stream Manager Actor - ULTRA-LOW LATENCY MODE
 *
 * Simple plug-and-play RTSP streaming manager for robotics applications.
 * Automatically configures optimal ultra-low latency settings.
 *
 * USER CONTROLS (Only 2 Settings):
 * - Resolution (safe presets: 240p-1080p for 16:9, 240p-1200p for 4:3)
 * - Frame Rate (30/60 FPS only)
 *
 * AUTOMATIC SETTINGS (System Managed, Optimized for Low Latency):
 * - Profile: Baseline (locked for maximum compatibility)
 * - Bitrate: 600-8000 kbps CBR (frame rate aware, 60 FPS gets 30-50% more)
 * - GOP: FPS (1.0s keyframes - industry standard for RTSP)
 * - B-frames: 0 (ultra-low latency, zero additional delay)
 *
 * Setup:
 * 1. Place ARealGazeboBridgeManager in level
 * 2. Place ARealGazeboStreamManager in level
 * 3. Add URealGazeboStreamingCamera components to vehicles
 * 4. Configure Resolution + FPS (bitrate auto-calculated)
 * 5. Play and access streams via RTSP (rtsp://localhost:8554/<vehicle>)
 *
 * Performance:
 * - <100ms glass-to-glass latencyGOPSize
 * - Hardware encoding only (NVENC/AMF)
 * - CBR for predictable network bandwidth
 * - Adaptive quality for backpressure handling
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "RealGazebo Stream Manager"))
class REALGAZEBOSTREAMING_API ARealGazeboStreamManager : public AActor
{
	GENERATED_BODY()

public:
	ARealGazeboStreamManager();

	//========================================
	// RUNTIME SETTINGS
	//========================================

	/** Auto-start RTSP server when level begins */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stream Manager|Runtime")
	bool bAutoStartRTSP = true;

	/** Auto-start streaming for registered cameras */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stream Manager|Runtime")
	bool bAutoStartStreams = true;

	//========================================
	// NETWORK SETTINGS
	//========================================

	/** RTSP Server Port */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stream Manager|Network",
	          meta = (ClampMin = "1024", ClampMax = "65535"))
	int32 RTSPPort = 8554;

	//========================================
	// USER STREAM SETTINGS (Only These!)
	//========================================

	/** Default Aspect Ratio - User configurable */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stream Manager|User Settings",
	          meta = (DisplayName = "Aspect Ratio"))
	EStreamAspectRatio DefaultAspectRatio = EStreamAspectRatio::Ratio_16_9;

	/** Default 16:9 Resolution - User configurable */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stream Manager|User Settings",
	          meta = (DisplayName = "Resolution (16:9)", EditCondition = "DefaultAspectRatio == EStreamAspectRatio::Ratio_16_9", EditConditionHides))
	EStreamResolution_16_9 DefaultResolution_16_9 = EStreamResolution_16_9::R16_9_720p;

	/** Default 4:3 Resolution - User configurable */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stream Manager|User Settings",
	          meta = (DisplayName = "Resolution (4:3)", EditCondition = "DefaultAspectRatio == EStreamAspectRatio::Ratio_4_3", EditConditionHides))
	EStreamResolution_4_3 DefaultResolution_4_3 = EStreamResolution_4_3::R4_3_480p;

	/** Default Frame Rate - User configurable */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stream Manager|User Settings",
	          meta = (DisplayName = "Frame Rate"))
	EStreamFrameRate DefaultFrameRate = EStreamFrameRate::FPS_30;

	//========================================
	// AUTO-COMPUTED DISPLAY (Read-Only)
	//========================================

	/** Bitrate - Auto-computed based on resolution and frame rate */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stream Manager|Auto-Computed",
	          meta = (DisplayName = "Bitrate (Auto, kbps)"))
	int32 BitrateKbps = 3250;

	/** GOP Size - Auto-computed based on FPS (1.0s keyframe interval) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stream Manager|Auto-Computed",
	          meta = (DisplayName = "GOP Size (Auto)"))
	int32 GOPSize = 30;

	/** H.264 Profile (locked to Baseline) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stream Manager|Auto-Computed",
	          meta = (DisplayName = "Profile (Locked)"))
	EH264Profile EncodingProfile = EH264Profile::Baseline;

	//========================================
	// PERFORMANCE SETTINGS
	//========================================

	/** Maximum encoding queue size (auto-computed: FPS × 10 for 5-second buffer) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stream Manager|Performance",
	          meta = (DisplayName = "Max Queue Size (Auto: FPS×10)"))
	int32 MaxQueueSize = 300;

	/** Frame dropping (REQUIRED for ultra-low latency - always enabled) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stream Manager|Performance",
	          meta = (DisplayName = "Frame Dropping (Required)"))
	bool bAllowFrameDropping = true;

	/** Enable adaptive quality adjustment based on backpressure */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stream Manager|Performance")
	bool bEnableAdaptiveQuality = false;

	//========================================
	// HARDWARE ENCODER INFO (Auto-Detected, Read-Only)
	//========================================

	/** Detected GPU vendor */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stream Manager|Hardware Info")
	FString DetectedGPUVendor = TEXT("Not Detected");

	/** Detected GPU name */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stream Manager|Hardware Info")
	FString DetectedGPUName = TEXT("Not Detected");

	/** Recommended encoder */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stream Manager|Hardware Info")
	FString RecommendedEncoder = TEXT("Not Detected");

	//========================================
	// DEBUG SETTINGS
	//========================================

	/** Show performance statistics overlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stream Manager|Debug")
	bool bShowPerformanceStats = false;

	/** Enable verbose logging - Shows detailed Verbose-level logs (GetStreamKey details, config changes, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stream Manager|Debug",
	          meta = (DisplayName = "Verbose Logging", ToolTip = "Dynamically enables Verbose-level logs. Toggle in editor to see detailed stream isolation, configuration, and debug information."))
	bool bVerboseLogging = false;

	/** Statistics update interval (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stream Manager|Debug",
	          meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float StatsUpdateInterval = 1.0f;

	//========================================
	// RUNTIME CONTROL API
	//========================================

	UFUNCTION(BlueprintCallable, Category = "Stream Manager|Control")
	void StartRTSPServer();

	UFUNCTION(BlueprintCallable, Category = "Stream Manager|Control")
	void StopRTSPServer();

	UFUNCTION(BlueprintCallable, Category = "Stream Manager|Control")
	void StartAllStreams();

	UFUNCTION(BlueprintCallable, Category = "Stream Manager|Control")
	void StopAllStreams();

	UFUNCTION(BlueprintCallable, Category = "Stream Manager|Control")
	bool IsRTSPServerRunning() const;

	UFUNCTION(BlueprintCallable, Category = "Stream Manager|Control")
	void UpdateAllCameraSettings();

	//========================================
	// STATUS INFORMATION API
	//========================================

	UFUNCTION(BlueprintCallable, Category = "Stream Manager|Status")
	int32 GetActiveStreamCount() const;

	UFUNCTION(BlueprintCallable, Category = "Stream Manager|Status")
	TArray<FStreamKey> GetAllStreamKeys() const;

	UFUNCTION(BlueprintCallable, Category = "Stream Manager|Status")
	void GetAggregatedStats(FStreamingStats& OutStats) const;

	//========================================
	// CONFIGURATION HELPERS
	//========================================

	/** Create default stream configuration with auto-computed settings */
	UFUNCTION(BlueprintCallable, Category = "Stream Manager|Config")
	FRealGazeboStreamConfig GetDefaultStreamConfig() const;

protected:
	//========================================
	// ACTOR LIFECYCLE
	//========================================

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	//========================================
	// INTERNAL LOGIC
	//========================================

	void InitializeStreaming();
	bool ValidateConfiguration() const;
	void UpdateStatusDisplay();
	void UpdateStatistics();
	void UpdateAdaptiveBitrates();
	void DetectSystemGPU();
	EStreamResolution GetActiveResolution() const;

	UPROPERTY()
	TWeakObjectPtr<URealGazeboStreamingSubsystem> StreamingSubsystem;

	UPROPERTY()
	TWeakObjectPtr<UGazeboBridgeSubsystem> BridgeSubsystem;

	//========================================
	// STATUS DISPLAY
	//========================================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stream Manager|Status")
	FString StreamingStatus = TEXT("Not Started");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stream Manager|Status")
	int32 ActiveStreamsCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stream Manager|Status")
	FString RTSPServerStatus = TEXT("Stopped");

private:
	FTimerHandle StatusUpdateTimer;
	FTimerHandle StatsUpdateTimer;
};
