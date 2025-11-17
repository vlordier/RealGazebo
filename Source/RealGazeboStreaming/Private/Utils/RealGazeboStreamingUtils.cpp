// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Utils/RealGazeboStreamingUtils.h"

namespace
{
	struct FResolutionData
	{
		int32 Width;
		int32 Height;
		int32 BitrateKbps_30fps;   // Bitrate for 30 FPS
		int32 BitrateKbps_60fps;   // Bitrate for 60 FPS (higher due to temporal complexity)
	};

	// Resolution lookup table for ultra-low latency RTSP streaming (H.264 Baseline profile)
	// All resolutions have 16-pixel aligned WIDTH to prevent H.264 bitstream corruption
	//
	// BITRATE RECOMMENDATIONS (2025-11-17):
	// Optimized for ultra-low latency H.264 streaming with CBR (Constant Bitrate)
	// Encoding settings: tune=zerolatency, 0 B-frames, 1-second keyframe interval
	//
	// Key principles:
	// - CBR for predictable network bandwidth (critical for robotics/drones)
	// - Conservative bitrates to prevent network congestion
	// - Safe margin below 8 Mbps ceiling for adaptive quality headroom
	// - 60 FPS gets ~30-50% higher bitrate due to temporal complexity
	//
	// REMOVED resolutions:
	// - 426x240, 854x480, 1366x768, 1400x1050 (width not 16-aligned, caused decoder errors)
	// - 1920x1440, 2560x1440, 3840x2160 (too high for ultra-low latency Baseline profile)
	constexpr FResolutionData SafeResolutions[] = {
		//    Width   Height  30fps    60fps
		// 16:9 aspect ratio (width 16-aligned, <=1080p)
		{640,   360,   1200,   2000},   // 360p: 800-1500 @ 30fps, 1200-2500 @ 60fps (optimized range)
		{960,   540,   2250,   3500},   // 540p: 1500-3000 @ 30fps, 2500-4500 @ 60fps (optimized range)
		{1024,  576,   2650,   4000},   // 576p: 1800-3500 @ 30fps, 2800-5000 @ 60fps (optimized range)
		{1280,  720,   3250,   5000},   // 720p: 2500-4000 @ 30fps, 3500-6000 @ 60fps (optimized range)
		{1600,  900,   4750,   6500},   // 900p: 3500-6000 @ 30fps, 5000-8000 @ 60fps (optimized range)
		{1920,  1080,  5000,   7500},   // 1080p: 4000-6000 @ 30fps, 6000-9000 @ 60fps (clamped to 7500 for headroom)

		// 4:3 aspect ratio (width 16-aligned, <=1200p)
		{320,   240,   600,    1200},   // 240p: 400-800 @ 30fps (interpolated for 60fps)
		{640,   480,   1500,   2400},   // 480p: 1000-2000 @ 30fps, 1800-3000 @ 60fps (optimized range)
		{800,   600,   2000,   3250},   // 600p: 1500-2500 @ 30fps, 2500-4000 @ 60fps (optimized range)
		{1024,  768,   2750,   4000},   // 768p: 2000-3500 @ 30fps, 3000-5000 @ 60fps (optimized range)
		{1280,  960,   4000,   6000},   // 960p: 3000-5000 @ 30fps, 4500-7000 @ 60fps (optimized range)
		{1600,  1200,  6000,   8000}    // 1200p: 4500-7000 @ 30fps, 7000-12000 @ 60fps (clamped to 8000 for ceiling)
	};

	constexpr int32 NumSafeResolutions = sizeof(SafeResolutions) / sizeof(FResolutionData);
}

FIntPoint FRealGazeboStreamingUtils::GetResolutionDimensions(EStreamResolution Resolution)
{
	const uint8 Index = static_cast<uint8>(Resolution);

	if (Index < NumSafeResolutions)
	{
		const FResolutionData& Data = SafeResolutions[Index];
		return FIntPoint(Data.Width, Data.Height);
	}

	// Fallback to 720p
	return FIntPoint(1280, 720);
}

int32 FRealGazeboStreamingUtils::CalculateBitrateUltraLowLatency(EStreamResolution Resolution, EStreamFrameRate FrameRate)
{
	const uint8 Index = static_cast<uint8>(Resolution);

	if (Index < NumSafeResolutions)
	{
		const FResolutionData& Data = SafeResolutions[Index];

		// Select bitrate based on frame rate
		// 60 FPS requires higher bitrate due to increased temporal complexity
		return (FrameRate == EStreamFrameRate::FPS_60)
			? Data.BitrateKbps_60fps
			: Data.BitrateKbps_30fps;
	}

	// Fallback to 720p @ 30fps bitrate
	return 3250;
}

int32 FRealGazeboStreamingUtils::GetFrameRateValue(EStreamFrameRate FrameRate)
{
	switch (FrameRate)
	{
		case EStreamFrameRate::FPS_30: return 30;
		case EStreamFrameRate::FPS_60: return 60;
		default: return 30;  // Fallback to 30 FPS
	}
}

int32 FRealGazeboStreamingUtils::CalculateGOPSize(EStreamFrameRate FrameRate)
{
	const int32 FPS = GetFrameRateValue(FrameRate);

	// INDUSTRY STANDARD: 1-second keyframe interval for RTSP/RTP streaming
	// This matches:
	// - YouTube Live: 2-4s GOP
	// - Twitch: 2s GOP
	// - RTSP surveillance: 1-2s GOP
	// - Live555 recommendation: 1s GOP (balance of seekability vs bandwidth)
	//
	// Benefits over 0.5s GOP:
	// - 10-15% bandwidth savings (fewer keyframes)
	// - Smoother bitrate profile (less spiky)
	// - Industry standard for RTSP streaming
	// - Better for recording and archiving
	//
	// Trade-off:
	// - Channel switch time: 1s vs 0.5s (acceptable for most use cases)
	// - Error recovery: 1s vs 0.5s (still fast enough for RTSP)
	//
	// FPS mapping (30 and 60 FPS only):
	//   30 FPS -> GOP 30 (1.0s)
	//   60 FPS -> GOP 60 (1.0s)
	const int32 GOP = FPS;  // 1-second keyframe interval

	return FMath::Max(GOP, 1);  // Only prevent zero/negative
}
