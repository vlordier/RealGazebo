// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Core/RealGazeboStreamManager.h"
#include "Core/RealGazeboStreamingSubsystem.h"
#include "Core/RealGazeboStreamingTypes.h"
#include "Core/GazeboBridgeSubsystem.h"
#include "Utils/RealGazeboStreamingUtils.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "RHI.h"
#include "GenericPlatform/GenericPlatformMisc.h"

ARealGazeboStreamManager::ARealGazeboStreamManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f; // Update 10 times per second
}

void ARealGazeboStreamManager::BeginPlay()
{
	Super::BeginPlay();

	// Apply verbose logging setting at startup
	if (bVerboseLogging)
	{
		LogRealGazeboStreaming.SetVerbosity(ELogVerbosity::Verbose);
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamManager: BeginPlay - Verbose logging: %s"),
		bVerboseLogging ? TEXT("ENABLED") : TEXT("DISABLED"));

	// Detect system GPU and recommended encoder
	DetectSystemGPU();

	// Validate configuration before initialization
	if (!ValidateConfiguration())
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("StreamManager: Configuration validation failed"));
		StreamingStatus = TEXT("Configuration Error");
		return;
	}

	// Get subsystem references
	StreamingSubsystem = URealGazeboStreamingSubsystem::GetStreamingSubsystem(this);
	BridgeSubsystem = UGazeboBridgeSubsystem::GetBridgeSubsystem(this);

	if (!StreamingSubsystem.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("StreamManager: Failed to get StreamingSubsystem"));
		StreamingStatus = TEXT("Subsystem Not Available");
		return;
	}

	// Initialize streaming
	InitializeStreaming();

	// Auto-start RTSP server if enabled
	if (bAutoStartRTSP)
	{
		StartRTSPServer();
	}

	// Start periodic status updates
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(StatusUpdateTimer, this, &ARealGazeboStreamManager::UpdateStatusDisplay, 1.0f, true);
		World->GetTimerManager().SetTimer(StatsUpdateTimer, this, &ARealGazeboStreamManager::UpdateStatistics, StatsUpdateInterval, true);

		if (bAutoStartStreams)
		{
			UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamManager: Auto-start enabled"));
		}
	}

	StreamingStatus = TEXT("Active");
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamManager: Initialized successfully"));
}

void ARealGazeboStreamManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamManager: EndPlay"));

	// Clear timers
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StatusUpdateTimer);
		World->GetTimerManager().ClearTimer(StatsUpdateTimer);
	}

	// Stop all streams
	StopAllStreams();

	// Stop RTSP server
	StopRTSPServer();

	StreamingStatus = TEXT("Stopped");

	Super::EndPlay(EndPlayReason);
}

void ARealGazeboStreamManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Periodic lightweight updates can go here if needed
}

#if WITH_EDITOR
void ARealGazeboStreamManager::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	// Handle verbose logging toggle
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ARealGazeboStreamManager, bVerboseLogging))
	{
		LogRealGazeboStreaming.SetVerbosity(bVerboseLogging ? ELogVerbosity::Verbose : ELogVerbosity::Log);
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamManager: Verbose logging %s"),
			bVerboseLogging ? TEXT("ENABLED") : TEXT("DISABLED"));
	}
	// Handle aspect ratio change
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ARealGazeboStreamManager, DefaultAspectRatio))
	{
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamManager: Aspect ratio changed to %s"),
		       *UEnum::GetValueAsString(DefaultAspectRatio));
	}
	// RTSP port change requires PIE restart
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ARealGazeboStreamManager, RTSPPort))
	{
		UE_LOG(LogRealGazeboStreaming, Warning,
			TEXT("StreamManager: RTSP port changed to %d - restart PIE to apply"), RTSPPort);
	}
	// Frame rate change updates GOP, queue size, and bitrate
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ARealGazeboStreamManager, DefaultFrameRate))
	{
		int32 OldGOPSize = GOPSize;
		int32 OldMaxQueueSize = MaxQueueSize;
		int32 OldBitrate = BitrateKbps;
		int32 FPSValue = (DefaultFrameRate == EStreamFrameRate::FPS_60) ? 60 : 30;

		GOPSize = FPSValue;  // 1.0s keyframe interval
		MaxQueueSize = FPSValue * 10;  // 5-second buffer

		// Recalculate bitrate (frame rate aware)
		FRealGazeboStreamConfig TempConfig;
		TempConfig.Resolution = GetActiveResolution();
		TempConfig.FrameRate = DefaultFrameRate;
		TempConfig.UpdateComputedValues();
		BitrateKbps = TempConfig.BitrateKbps;

		UE_LOG(LogRealGazeboStreaming, Log,
			TEXT("StreamManager: FPS changed - GOP: %d->%d, Queue: %d->%d, Bitrate: %d->%d kbps"),
			OldGOPSize, GOPSize, OldMaxQueueSize, MaxQueueSize, OldBitrate, BitrateKbps);

		UpdateAllCameraSettings();
	}
	// Resolution change updates bitrate and adaptive quality
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ARealGazeboStreamManager, DefaultResolution_16_9) ||
	         PropertyName == GET_MEMBER_NAME_CHECKED(ARealGazeboStreamManager, DefaultResolution_4_3))
	{
		int32 OldBitrate = BitrateKbps;

		FRealGazeboStreamConfig TempConfig;
		TempConfig.Resolution = GetActiveResolution();
		TempConfig.FrameRate = DefaultFrameRate;
		TempConfig.UpdateComputedValues();
		BitrateKbps = TempConfig.BitrateKbps;

		// Auto-enable adaptive quality for >=1080p
		bEnableAdaptiveQuality = (TempConfig.Dimensions.X * TempConfig.Dimensions.Y >= 1920 * 1080);

		UE_LOG(LogRealGazeboStreaming, Log,
			TEXT("StreamManager: Resolution %dx%d - Bitrate: %d->%d kbps, Adaptive: %s"),
			TempConfig.Dimensions.X, TempConfig.Dimensions.Y, OldBitrate, BitrateKbps,
			bEnableAdaptiveQuality ? TEXT("ON") : TEXT("OFF"));

		UpdateAllCameraSettings();
	}
}
#endif

void ARealGazeboStreamManager::InitializeStreaming()
{
	if (!StreamingSubsystem.IsValid())
	{
		return;
	}

	// Set RTSP port (server starts lazily on first camera registration)
	StreamingSubsystem->SetRTSPPort(RTSPPort);

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamManager: Initialized with RTSP port %d"), RTSPPort);
}

bool ARealGazeboStreamManager::ValidateConfiguration() const
{
	if (RTSPPort < 1024 || RTSPPort > 65535)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("StreamManager: Invalid RTSP port %d (1024-65535)"), RTSPPort);
		return false;
	}

	if (MaxQueueSize < 5 || MaxQueueSize > 1000)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("StreamManager: Invalid queue size %d (5-1000)"), MaxQueueSize);
		return false;
	}

	if (GOPSize < 1 || GOPSize > 300)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("StreamManager: Invalid GOP size %d (1-300)"), GOPSize);
		return false;
	}

	return true;
}

void ARealGazeboStreamManager::UpdateStatusDisplay()
{
	if (!StreamingSubsystem.IsValid())
	{
		return;
	}

	ActiveStreamsCount = StreamingSubsystem->GetActiveStreamCount();

	// Dynamic adaptive quality (>=1080p OR >5 active streams)
	bool bOldAdaptive = bEnableAdaptiveQuality;
	FRealGazeboStreamConfig TempConfig;
	TempConfig.Resolution = GetActiveResolution();
	TempConfig.UpdateComputedValues();
	const int32 TotalPixels = TempConfig.Dimensions.X * TempConfig.Dimensions.Y;

	bEnableAdaptiveQuality = (TotalPixels >= 1920 * 1080 || ActiveStreamsCount > 5);

	if (bOldAdaptive != bEnableAdaptiveQuality)
	{
		UE_LOG(LogRealGazeboStreaming, Log,
			TEXT("StreamManager: Adaptive Quality %s (streams: %d, resolution: %dx%d)"),
			bEnableAdaptiveQuality ? TEXT("ON") : TEXT("OFF"),
			ActiveStreamsCount, TempConfig.Dimensions.X, TempConfig.Dimensions.Y);
	}

	// Update RTSP server status
	RTSPServerStatus = StreamingSubsystem->IsRTSPServerRunning() ? TEXT("Running") : TEXT("Stopped");

	// Update streaming status based on current state
	if (StreamingSubsystem->IsRTSPServerRunning())
	{
		StreamingStatus = FString::Printf(TEXT("Active (%d streams)"), ActiveStreamsCount);
	}
	else
	{
		StreamingStatus = TEXT("RTSP Server Stopped");
	}
}

void ARealGazeboStreamManager::UpdateStatistics()
{
	if (!StreamingSubsystem.IsValid() || !bShowPerformanceStats)
	{
		return;
	}

	// Get aggregated statistics
	FStreamingStats AggregatedStats;
	StreamingSubsystem->GetAggregatedStats(AggregatedStats);

	// Log statistics
	if (bVerboseLogging)
	{
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamManager Stats:"));
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("  - Active Streams: %d"), ActiveStreamsCount);
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("  - Total Frames Encoded: %llu"), AggregatedStats.TotalFramesEncoded);
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("  - Average Bitrate: %.2f Mbps"), AggregatedStats.AverageBitrateMbps);
	}

	// Dynamic bitrate adjustment based on backpressure (if adaptive quality enabled)
	if (bEnableAdaptiveQuality)
	{
		UpdateAdaptiveBitrates();
	}
}

void ARealGazeboStreamManager::UpdateAdaptiveBitrates()
{
	if (!StreamingSubsystem.IsValid())
	{
		return;
	}

	// Get all active streams
	TArray<FStreamKey> StreamKeys = StreamingSubsystem->GetAllStreamKeys();

	for (const FStreamKey& StreamKey : StreamKeys)
	{
		// Get pipeline to check backpressure
		TSharedPtr<FRealGazeboStreamPipeline> Pipeline = StreamingSubsystem->GetPipeline(StreamKey);
		if (!Pipeline.IsValid())
		{
			continue;
		}

		// Get adaptive quality factor (0.5-1.5 based on backpressure duration)
		const float QualityFactor = Pipeline->GetAdaptiveQualityFactor();

		// Only adjust if quality factor has changed significantly from baseline
		if (FMath::Abs(QualityFactor - 1.0f) > 0.05f)
		{
			// Calculate new bitrate based on quality factor
			const int32 BaseBitrateKbps = Pipeline->GetConfig().BitrateKbps;
			const int32 AdjustedBitrateKbps = static_cast<int32>(BaseBitrateKbps * QualityFactor);

			// Clamp bitrate to reasonable bounds (50% to 150% of base)
			const int32 MinBitrate = BaseBitrateKbps / 2;
			const int32 MaxBitrate = (BaseBitrateKbps * 3) / 2;
			const int32 NewBitrateKbps = FMath::Clamp(AdjustedBitrateKbps, MinBitrate, MaxBitrate);

			// Apply bitrate adjustment via encoding thread
			if (bVerboseLogging)
			{
				UE_LOG(LogRealGazeboStreaming, Verbose,
					TEXT("StreamManager: Adjusting bitrate for %s: %d -> %d kbps (factor: %.2f)"),
					*StreamKey.ToString(), BaseBitrateKbps, NewBitrateKbps, QualityFactor);
			}

			// Apply bitrate update to encoder
			StreamingSubsystem->UpdateStreamBitrate(StreamKey, NewBitrateKbps);
		}
	}
}

//----------------------------------------------------------
// Runtime Control API
//----------------------------------------------------------

void ARealGazeboStreamManager::StartRTSPServer()
{
	if (!StreamingSubsystem.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("StreamManager: Cannot start RTSP server - subsystem not available"));
		return;
	}

	if (StreamingSubsystem->IsRTSPServerRunning())
	{
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamManager: RTSP server is already running on port %d"), RTSPPort);
		UpdateStatusDisplay();
		return;
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamManager: RTSP server uses lazy initialization - will start on first camera registration"));
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamManager: RTSP port configured as %d"), RTSPPort);

	// RTSP server uses lazy initialization - starts automatically when first camera registers.
	// This allows port configuration before server creation.

	UpdateStatusDisplay();
}

void ARealGazeboStreamManager::StopRTSPServer()
{
	if (!StreamingSubsystem.IsValid())
	{
		return;
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamManager: Stopping RTSP server"));

	// Stop all streams first
	StopAllStreams();

	// RTSP server will be stopped automatically during subsystem shutdown
	UpdateStatusDisplay();
}

void ARealGazeboStreamManager::StartAllStreams()
{
	if (!StreamingSubsystem.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("StreamManager: Cannot start streams - subsystem not available"));
		return;
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamManager: Starting all streams"));

	StreamingSubsystem->StartAllStreams();

	UpdateStatusDisplay();
}

void ARealGazeboStreamManager::StopAllStreams()
{
	if (!StreamingSubsystem.IsValid())
	{
		return;
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamManager: Stopping all streams"));

	StreamingSubsystem->StopAllStreams();

	UpdateStatusDisplay();
}

bool ARealGazeboStreamManager::IsRTSPServerRunning() const
{
	if (!StreamingSubsystem.IsValid())
	{
		return false;
	}

	return StreamingSubsystem->IsRTSPServerRunning();
}

void ARealGazeboStreamManager::UpdateAllCameraSettings()
{
	if (!StreamingSubsystem.IsValid())
	{
		return;
	}

	FRealGazeboStreamConfig NewConfig = GetDefaultStreamConfig();
	TArray<FStreamKey> StreamKeys = StreamingSubsystem->GetAllStreamKeys();

	if (StreamKeys.Num() == 0)
	{
		return;
	}

	int32 UpdateCount = 0;
	int32 RestartCount = 0;

	for (const FStreamKey& StreamKey : StreamKeys)
	{
		// Check if stream is active
		EStreamState State = StreamingSubsystem->GetStreamState(StreamKey);
		bool bWasStreaming = (State == EStreamState::Streaming);

		// Stop stream if active (required for config update)
		if (bWasStreaming)
		{
			StreamingSubsystem->StopStream(StreamKey);
			RestartCount++;
		}

		// Update configuration
		if (StreamingSubsystem->UpdateStreamConfig(StreamKey, NewConfig))
		{
			UpdateCount++;
		}

		// Restart stream if it was active
		if (bWasStreaming)
		{
			StreamingSubsystem->StartStream(StreamKey);
		}
	}

	UE_LOG(LogRealGazeboStreaming, Log,
		TEXT("StreamManager: Updated %d/%d cameras (restarted %d active streams) - FPS: %d, GOP: %d, Bitrate: %d kbps"),
		UpdateCount, StreamKeys.Num(), RestartCount, NewConfig.FPSValue, NewConfig.GOPSize, NewConfig.BitrateKbps);
}

//----------------------------------------------------------
// Status Information API
//----------------------------------------------------------

int32 ARealGazeboStreamManager::GetActiveStreamCount() const
{
	if (!StreamingSubsystem.IsValid())
	{
		return 0;
	}

	return StreamingSubsystem->GetActiveStreamCount();
}

TArray<FStreamKey> ARealGazeboStreamManager::GetAllStreamKeys() const
{
	if (!StreamingSubsystem.IsValid())
	{
		return TArray<FStreamKey>();
	}

	return StreamingSubsystem->GetAllStreamKeys();
}

void ARealGazeboStreamManager::GetAggregatedStats(FStreamingStats& OutStats) const
{
	if (!StreamingSubsystem.IsValid())
	{
		OutStats = FStreamingStats();
		return;
	}

	StreamingSubsystem->GetAggregatedStats(OutStats);
}

//----------------------------------------------------------
// Configuration Helpers
//----------------------------------------------------------

FRealGazeboStreamConfig ARealGazeboStreamManager::GetDefaultStreamConfig() const
{
	FRealGazeboStreamConfig Config;

	Config.AspectRatio = DefaultAspectRatio;
	Config.Resolution = GetActiveResolution();
	Config.FrameRate = DefaultFrameRate;
	Config.EncodingProfile = EncodingProfile;
	// DON'T set GOPSize here - let UpdateComputedValues() calculate it from FrameRate
	Config.bEnableAdaptiveQuality = bEnableAdaptiveQuality;

	// Performance settings
	Config.MaxQueueSize = MaxQueueSize;
	Config.bAllowFrameDropping = bAllowFrameDropping;

	// CRITICAL DEBUG (2025-11-17): Log FPS BEFORE UpdateComputedValues()
	UE_LOG(LogRealGazeboStreaming, Warning,
		TEXT("GetDefaultStreamConfig BEFORE UpdateComputedValues - FrameRate enum: %d | FPSValue: %d | GOPSize: %d"),
		static_cast<int32>(Config.FrameRate), Config.FPSValue, Config.GOPSize);

	// Compute derived values (including GOPSize from FrameRate)
	Config.UpdateComputedValues();

	// CRITICAL DEBUG (2025-11-17): Log FPS AFTER UpdateComputedValues()
	UE_LOG(LogRealGazeboStreaming, Warning,
		TEXT("GetDefaultStreamConfig AFTER UpdateComputedValues - FrameRate enum: %d | FPSValue: %d | GOPSize: %d | BitrateKbps: %d"),
		static_cast<int32>(Config.FrameRate), Config.FPSValue, Config.GOPSize, Config.BitrateKbps);

	return Config;
}

//----------------------------------------------------------
// System Detection and Helpers
//----------------------------------------------------------

void ARealGazeboStreamManager::DetectSystemGPU()
{
	// Get GPU adapter name from RHI
	FString AdapterName = GRHIAdapterName;
	FString VendorName = TEXT("Unknown");

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamManager: Detecting GPU..."));
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("  - Adapter: %s"), *AdapterName);

	// Simple vendor detection - NVIDIA, AMD, or Intel
	if (AdapterName.Contains(TEXT("NVIDIA")) || AdapterName.Contains(TEXT("GeForce")) ||
	    AdapterName.Contains(TEXT("Quadro")) || AdapterName.Contains(TEXT("Tesla")))
	{
		VendorName = TEXT("NVIDIA");
		RecommendedEncoder = TEXT("NVENC");
	}
	else if (AdapterName.Contains(TEXT("AMD")) || AdapterName.Contains(TEXT("Radeon")) ||
	         AdapterName.Contains(TEXT("ATI")))
	{
		VendorName = TEXT("AMD");
		RecommendedEncoder = TEXT("AMF");
	}
	else if (AdapterName.Contains(TEXT("Intel")))
	{
		VendorName = TEXT("Intel");
		RecommendedEncoder = TEXT("Not Supported (Requires NVIDIA or AMD GPU)");
	}
	else
	{
		VendorName = TEXT("Unknown");
		RecommendedEncoder = TEXT("Unknown (Requires NVIDIA or AMD GPU)");
	}

	DetectedGPUVendor = VendorName;
	DetectedGPUName = AdapterName;

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("  - Vendor: %s"), *VendorName);
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("  - Encoder: %s"), *RecommendedEncoder);

	// Auto-configure encoding parameters for supported GPUs
	if (VendorName == TEXT("NVIDIA") || VendorName == TEXT("AMD"))
	{
		// GOP size: 1.0 second intervals (industry standard for RTSP streaming)
		int32 FPSValue = 30; // Default
		switch (DefaultFrameRate)
		{
		case EStreamFrameRate::FPS_30: FPSValue = 30; GOPSize = 30; break;  // 1.0s @ 30 FPS
		case EStreamFrameRate::FPS_60: FPSValue = 60; GOPSize = 60; break;  // 1.0s @ 60 FPS
		}

		// Auto-select quality and H.264 profile based on resolution
		EStreamResolution ActiveRes = GetActiveResolution();

		// Use pixel count for both 16:9 and 4:3 aspect ratios
		FIntPoint Dimensions;
		FRealGazeboStreamConfig TempConfig;
		TempConfig.Resolution = ActiveRes;
		TempConfig.FrameRate = DefaultFrameRate;  // Include frame rate for accurate bitrate
		TempConfig.UpdateComputedValues();
		Dimensions = TempConfig.Dimensions;
		BitrateKbps = TempConfig.BitrateKbps;  // Store computed bitrate for display
		const int32 TotalPixels = Dimensions.X * Dimensions.Y;

		// Use Baseline profile for RTSP streaming compatibility (2025-11-13).
		// Prevents decoder errors in ffplay/VLC. Trade-off: ~5-10% higher bitrate but clean decoding.
		EncodingProfile = EH264Profile::Baseline;

		// Initial adaptive quality setting (dynamically updated based on active camera count).
		// Enable for high-resolution scenarios (>=1080p) at startup.
		bEnableAdaptiveQuality = (TotalPixels >= 1920 * 1080);

		// NAL queue size scales with frame rate to maintain 5-second buffer (FPS x 10, ~2 NAL units per frame).
		// Prevents queue overflow when client connects slowly or network has jitter.
		MaxQueueSize = FPSValue * 10;

		UE_LOG(LogRealGazeboStreaming, Log, TEXT("  - GOP Size: %d (1.0s @ %d FPS - industry standard)"), GOPSize, FPSValue);
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("  - Bitrate: %d kbps (frame rate aware: 600-8000 range)"), BitrateKbps);
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("  - H.264 Profile: %s"), *UEnum::GetValueAsString(EncodingProfile));
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("  - Adaptive Quality: %s (bitrate ±50%% based on backpressure)"), bEnableAdaptiveQuality ? TEXT("Enabled") : TEXT("Disabled"));
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("  - Queue Size: %d NAL units (5s buffer @ %d FPS)"), MaxQueueSize, FPSValue);
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("  - Concurrent Streams: Dynamic (limited by GPU encoder sessions)"));
	}
}

EStreamResolution ARealGazeboStreamManager::GetActiveResolution() const
{
	if (DefaultAspectRatio == EStreamAspectRatio::Ratio_16_9)
	{
		// Convert EStreamResolution_16_9 to legacy EStreamResolution
		// The enum values are defined in the same order, so we can cast directly
		return static_cast<EStreamResolution>(DefaultResolution_16_9);
	}
	else
	{
		// 4:3 resolutions follow 16:9 in the unified enum, so we add an offset of 6.
		// All resolutions use 16-pixel aligned width for NVENC/AMF compatibility (prevents decoder errors).
		constexpr uint8 Count_16_9_Resolutions = static_cast<uint8>(EStreamResolution_16_9::R16_9_1080p) + 1;
		return static_cast<EStreamResolution>(static_cast<uint8>(DefaultResolution_4_3) + Count_16_9_Resolutions);
	}
}
