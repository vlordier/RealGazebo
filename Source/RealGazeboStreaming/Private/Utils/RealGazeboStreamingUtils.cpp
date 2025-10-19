// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Utils/RealGazeboStreamingUtils.h"

FIntPoint FRealGazeboStreamingUtils::GetResolutionDimensions(EStreamResolution Resolution)
{
	switch (Resolution)
	{
		// 16:9 Resolutions (12 total)
		case EStreamResolution::R16_9_240p:  return FIntPoint(426, 240);   // 240p SD
		case EStreamResolution::R16_9_360p:  return FIntPoint(640, 360);   // nHD
		case EStreamResolution::R16_9_480p:  return FIntPoint(854, 480);   // FWVGA
		case EStreamResolution::R16_9_540p:  return FIntPoint(960, 540);   // qHD
		case EStreamResolution::R16_9_576p:  return FIntPoint(1024, 576);  // WSVGA
		case EStreamResolution::R16_9_720p:  return FIntPoint(1280, 720);  // HD (720p)
		case EStreamResolution::R16_9_768p:  return FIntPoint(1366, 768);  // FWXGA
		case EStreamResolution::R16_9_900p:  return FIntPoint(1600, 900);  // HD+
		case EStreamResolution::R16_9_1080p: return FIntPoint(1920, 1080); // Full HD (1080p)
		case EStreamResolution::R16_9_1440p: return FIntPoint(2560, 1440); // QHD (1440p / 2K)
		case EStreamResolution::R16_9_1800p: return FIntPoint(3200, 1800); // QHD+
		case EStreamResolution::R16_9_2160p: return FIntPoint(3840, 2160); // 4K UHD

		// 4:3 Resolutions (11 total)
		case EStreamResolution::R4_3_240p:   return FIntPoint(320, 240);   // QVGA 
		case EStreamResolution::R4_3_480p:   return FIntPoint(640, 480);   // VGA
		case EStreamResolution::R4_3_600p:   return FIntPoint(800, 600);   // SVGA
		case EStreamResolution::R4_3_768p:   return FIntPoint(1024, 768);  // XGA
		case EStreamResolution::R4_3_960p:   return FIntPoint(1280, 960);  // SXGA-
		case EStreamResolution::R4_3_1050p:  return FIntPoint(1400, 1050); // SXGA+
		case EStreamResolution::R4_3_1200p:  return FIntPoint(1600, 1200); // UXGA
		case EStreamResolution::R4_3_1440p:  return FIntPoint(1920, 1440); // QXGA
		case EStreamResolution::R4_3_1536p:  return FIntPoint(2048, 1536); // QXGA 
		case EStreamResolution::R4_3_1920p:  return FIntPoint(2560, 1920); // QSXGA
		case EStreamResolution::R4_3_2400p:  return FIntPoint(3200, 2400); // QUXGA

		default: return FIntPoint(1280, 720);
	}
}

int32 FRealGazeboStreamingUtils::CalculateBitrate(EStreamResolution Resolution, EStreamQuality Quality)
{
	// Get resolution dimensions
	FIntPoint Dimensions = GetResolutionDimensions(Resolution);

	// Calculate total pixels
	int64 TotalPixels = static_cast<int64>(Dimensions.X) * static_cast<int64>(Dimensions.Y);

	// Base bitrate calculation: ~0.1 bits per pixel for High quality at 30 FPS
	// This is a industry-standard formula for H.264 bitrate estimation
	// Formula: Bitrate (kbps) = (Width x Height x BitsPerPixel x FPS) / 1000
	// For High quality: ~0.1 bpp, Medium: ~0.075 bpp, Low: ~0.05 bpp, Ultra: ~0.15 bpp

	float BitsPerPixel = 0.1f;  // Baseline for High quality
	const int32 AssumedFPS = 30;  // Standard FPS for bitrate calculation

	// Adjust bits per pixel based on quality
	switch (Quality)
	{
		case EStreamQuality::Low:    BitsPerPixel = 0.05f;  break;  // 50% of High
		case EStreamQuality::Medium: BitsPerPixel = 0.075f; break;  // 75% of High
		case EStreamQuality::High:   BitsPerPixel = 0.1f;   break;  // Baseline
		case EStreamQuality::Ultra:  BitsPerPixel = 0.15f;  break;  // 150% of High
	}

	// Calculate bitrate: (Pixels x BPP x FPS) / 1000 = kbps
	float BitrateKbps = (TotalPixels * BitsPerPixel * AssumedFPS) / 1000.0f;

	// Apply minimum/maximum constraints
	int32 FinalBitrate = FMath::RoundToInt(BitrateKbps);
	FinalBitrate = FMath::Clamp(FinalBitrate, 500, 50000);  // Min 500kbps, Max 50Mbps

	return FinalBitrate;
}

int32 FRealGazeboStreamingUtils::GetFrameRateValue(EStreamFrameRate FrameRate)
{
	switch (FrameRate)
	{
		case EStreamFrameRate::FPS_15: return 15;
		case EStreamFrameRate::FPS_30: return 30;
		case EStreamFrameRate::FPS_60: return 60;
		default: return 30;
	}
}

TArray<EStreamResolution> FRealGazeboStreamingUtils::GetResolutionsForAspectRatio(EStreamAspectRatio AspectRatio)
{
	TArray<EStreamResolution> Resolutions;

	if (AspectRatio == EStreamAspectRatio::Ratio_16_9)
	{
		// All 12 16:9 resolutions
		Resolutions.Add(EStreamResolution::R16_9_240p);
		Resolutions.Add(EStreamResolution::R16_9_360p);
		Resolutions.Add(EStreamResolution::R16_9_480p);
		Resolutions.Add(EStreamResolution::R16_9_540p);
		Resolutions.Add(EStreamResolution::R16_9_576p);
		Resolutions.Add(EStreamResolution::R16_9_720p);
		Resolutions.Add(EStreamResolution::R16_9_768p);
		Resolutions.Add(EStreamResolution::R16_9_900p);
		Resolutions.Add(EStreamResolution::R16_9_1080p);
		Resolutions.Add(EStreamResolution::R16_9_1440p);
		Resolutions.Add(EStreamResolution::R16_9_1800p);
		Resolutions.Add(EStreamResolution::R16_9_2160p);
	}
	else // 4:3
	{
		// All 11 4:3 resolutions
		Resolutions.Add(EStreamResolution::R4_3_240p);
		Resolutions.Add(EStreamResolution::R4_3_480p);
		Resolutions.Add(EStreamResolution::R4_3_600p);
		Resolutions.Add(EStreamResolution::R4_3_768p);
		Resolutions.Add(EStreamResolution::R4_3_960p);
		Resolutions.Add(EStreamResolution::R4_3_1050p);
		Resolutions.Add(EStreamResolution::R4_3_1200p);
		Resolutions.Add(EStreamResolution::R4_3_1440p);
		Resolutions.Add(EStreamResolution::R4_3_1536p);
		Resolutions.Add(EStreamResolution::R4_3_1920p);
		Resolutions.Add(EStreamResolution::R4_3_2400p);
	}

	return Resolutions;
}

bool FRealGazeboStreamingUtils::IsResolutionCompatibleWithAspectRatio(EStreamResolution Resolution, EStreamAspectRatio AspectRatio)
{
	TArray<EStreamResolution> CompatibleResolutions = GetResolutionsForAspectRatio(AspectRatio);
	return CompatibleResolutions.Contains(Resolution);
}

FString FRealGazeboStreamingUtils::H264ProfileToString(EH264Profile Profile)
{
	switch (Profile)
	{
		case EH264Profile::Baseline: return TEXT("Baseline");
		case EH264Profile::Main:     return TEXT("Main");
		case EH264Profile::High:     return TEXT("High");
		default: return TEXT("Main");
	}
}

int32 FRealGazeboStreamingUtils::GetH264ProfileLevel(EH264Profile Profile)
{
	switch (Profile)
	{
		case EH264Profile::Baseline: return 30;  // Level 3.0
		case EH264Profile::Main:     return 40;  // Level 4.0
		case EH264Profile::High:     return 41;  // Level 4.1
		default: return 40;
	}
}
