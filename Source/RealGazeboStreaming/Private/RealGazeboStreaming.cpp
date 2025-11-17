// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "RealGazeboStreaming.h"
#include "Core/RealGazeboStreamingTypes.h"
#include "Utils/RealGazeboStreamingUtils.h"
#include "Engine/Engine.h"

#define LOCTEXT_NAMESPACE "FRealGazeboStreamingModule"

//----------------------------------------------------------
// Logger Definition
//----------------------------------------------------------

DEFINE_LOG_CATEGORY(LogRealGazeboStreaming);

//----------------------------------------------------------
// Ultra-Low Latency Stream Configuration Implementation
//----------------------------------------------------------

void FRealGazeboStreamConfig::UpdateComputedValues()
{
	// Compute dimensions from resolution
	Dimensions = FRealGazeboStreamingUtils::GetResolutionDimensions(Resolution);

	// Compute FPS value first (needed for bitrate calculation)
	FPSValue = FRealGazeboStreamingUtils::GetFrameRateValue(FrameRate);

	// Compute optimal bitrate based on resolution AND frame rate
	// 60 FPS requires ~30-50% higher bitrate due to temporal complexity
	BitrateKbps = FRealGazeboStreamingUtils::CalculateBitrateUltraLowLatency(Resolution, FrameRate);

	// Compute GOP size (1.0s keyframe interval - industry standard for RTSP)
	// 30 FPS -> GOP 30, 60 FPS -> GOP 60
	// NOTE: StreamManager should NOT set GOPSize before calling this - let it auto-compute
	GOPSize = FRealGazeboStreamingUtils::CalculateGOPSize(FrameRate);

	// Profile is always Baseline (locked for maximum compatibility)
	EncodingProfile = EH264Profile::Baseline;

	UE_LOG(LogRealGazeboStreaming, Verbose,
		TEXT("StreamConfig: Auto-computed settings - Resolution: %dx%d, Bitrate: %d kbps @ %d FPS, GOP: %d (1.0s), Profile: Baseline"),
		Dimensions.X, Dimensions.Y, BitrateKbps, FPSValue, GOPSize);
}

bool FRealGazeboStreamConfig::IsValid(FString& OutErrorMessage) const
{
	// Validate dimensions
	if (Dimensions.X <= 0 || Dimensions.Y <= 0)
	{
		OutErrorMessage = TEXT("Invalid dimensions - must be positive");
		return false;
	}

	// Validate bitrate (ultra-low latency range: 600-8000 kbps)
	if (BitrateKbps < RealGazeboStreamingConstants::MIN_BITRATE_KBPS ||
		BitrateKbps > RealGazeboStreamingConstants::MAX_BITRATE_KBPS)
	{
		OutErrorMessage = FString::Printf(
			TEXT("Bitrate %d kbps outside ultra-low latency range (600-8000 kbps)"),
			BitrateKbps);
		return false;
	}

	// Validate FPS
	if (FPSValue <= 0 || FPSValue > 60)
	{
		OutErrorMessage = FString::Printf(TEXT("Invalid frame rate: %d (must be 15/30/60)"), FPSValue);
		return false;
	}

	// Validate GOP (ultra-low latency range: 30-60, which is 1.0s @ 30-60 FPS)
	if (GOPSize < RealGazeboStreamingConstants::MIN_GOP_SIZE ||
		GOPSize > RealGazeboStreamingConstants::MAX_GOP_SIZE)
	{
		OutErrorMessage = FString::Printf(
			TEXT("GOP size %d outside ultra-low latency range (30-60, which is 1.0s @ 30-60 FPS)"),
			GOPSize);
		return false;
	}

	// Validate RTSP port
	if (RTSPPort < 1024 || RTSPPort > 65535)
	{
		OutErrorMessage = FString::Printf(TEXT("Invalid RTSP port: %d (must be 1024-65535)"), RTSPPort);
		return false;
	}

	// Validate profile (must be Baseline)
	if (EncodingProfile != EH264Profile::Baseline)
	{
		OutErrorMessage = TEXT("Profile must be Baseline for ultra-low latency");
		return false;
	}

	OutErrorMessage = TEXT("");
	return true;
}

//----------------------------------------------------------
// Module Implementation
//----------------------------------------------------------

FRealGazeboStreamingModule* FRealGazeboStreamingModule::ModuleInstance = nullptr;

void FRealGazeboStreamingModule::StartupModule()
{
	UE_LOG(LogRealGazeboStreaming, Log,
		TEXT("RealGazeboStreaming Module: Starting - Ultra-Low Latency RTSP/RTP Streaming"));
	UE_LOG(LogRealGazeboStreaming, Log,
		TEXT("  - Mode: Hardware-accelerated H.264 encoding (NVENC/AMF)"));
	UE_LOG(LogRealGazeboStreaming, Log,
		TEXT("  - Profile: Baseline (locked for maximum compatibility)"));
	UE_LOG(LogRealGazeboStreaming, Log,
		TEXT("  - Bitrate: 600-8000 kbps CBR (frame rate aware, optimized for low latency)"));
	UE_LOG(LogRealGazeboStreaming, Log,
		TEXT("  - GOP: 1.0s keyframe interval (industry standard for RTSP)"));
	UE_LOG(LogRealGazeboStreaming, Log,
		TEXT("  - B-frames: 0 (ultra-low latency, zero additional delay)"));
	UE_LOG(LogRealGazeboStreaming, Log,
		TEXT("  - Supported GPUs: NVIDIA (GTX 600+), AMD (HD 7000+)"));

	ModuleInstance = this;

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RealGazeboStreaming Module: Initialized successfully"));
}

void FRealGazeboStreamingModule::ShutdownModule()
{
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RealGazeboStreaming Module: Shutting down"));

	ModuleInstance = nullptr;

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RealGazeboStreaming Module: Shutdown complete"));
}

FRealGazeboStreamingModule& FRealGazeboStreamingModule::Get()
{
	return FModuleManager::LoadModuleChecked<FRealGazeboStreamingModule>("RealGazeboStreaming");
}

bool FRealGazeboStreamingModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("RealGazeboStreaming");
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRealGazeboStreamingModule, RealGazeboStreaming)
