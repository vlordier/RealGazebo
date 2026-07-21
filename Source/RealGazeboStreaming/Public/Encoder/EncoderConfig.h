// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "StreamingTypes.h"

#if !PLATFORM_MAC
#include "VideoEncoder.h"
#endif

struct FEncoderConfig
{
	int32 Width = 1024;
	int32 Height = 768;
	int32 FrameRate = 30;
	uint64 Bitrate = 2000000;
	int32 GOPSize = 15;

	EEncoderPreset Preset = EEncoderPreset::UltraLowLatency;
	ERealGazeboH264Profile Profile = ERealGazeboH264Profile::Baseline;
	bool bConstantBitrate = true;
	bool bZeroCopy = true;
	bool bMultipass = false;
	int32 NumBFrames = 0;
	int32 NumRefFrames = 1;

	FEncoderConfig() = default;

	explicit FEncoderConfig(const FStreamConfig& StreamConfig)
	{
		StreamConfig.GetResolution(Width, Height);
		FrameRate = StreamConfig.GetFrameRateValue();
		Bitrate = static_cast<uint64>(StreamConfig.GetBitrate()) * 1000;
		GOPSize = StreamConfig.GetGOPSize();
		Preset = StreamConfig.Preset;
		Profile = StreamConfig.Profile;
		bZeroCopy = StreamConfig.bZeroCopy;
	}

#if !PLATFORM_MAC
	AVEncoder::FVideoEncoder::H264Profile GetAVEncoderProfile() const;
	AVEncoder::FVideoEncoder::RateControlMode GetRateControlMode() const;
	AVEncoder::FVideoEncoder::MultipassMode GetMultipassMode() const;
#endif

	bool Validate(FString& OutErrorMessage) const
	{
		if (Width < 320 || Width > 3840)
		{
			OutErrorMessage = FString::Printf(TEXT("Invalid width %d (must be 320-3840)"), Width);
			return false;
		}
		if (Height < 240 || Height > 2160)
		{
			OutErrorMessage = FString::Printf(TEXT("Invalid height %d (must be 240-2160)"), Height);
			return false;
		}
		if (FrameRate < 10 || FrameRate > 120)
		{
			OutErrorMessage = FString::Printf(TEXT("Invalid frame rate %d (must be 10-120)"), FrameRate);
			return false;
		}
		const uint64 MinBitrate = 500000;
		const uint64 MaxBitrate = 50000000;
		if (Bitrate < MinBitrate || Bitrate > MaxBitrate)
		{
			OutErrorMessage = FString::Printf(TEXT("Invalid bitrate %llu (must be 500kbps-50Mbps)"), Bitrate);
			return false;
		}
		if (GOPSize < 5 || GOPSize > 120)
		{
			OutErrorMessage = FString::Printf(TEXT("Invalid GOP size %d (must be 5-120)"), GOPSize);
			return false;
		}
		if (!bZeroCopy)
		{
			OutErrorMessage = TEXT("Zero-copy must be enabled for ultra-low latency");
			return false;
		}
		if (NumBFrames != 0)
		{
			OutErrorMessage = TEXT("B-frames must be disabled (0) for ultra-low latency");
			return false;
		}
		if (NumRefFrames > 1)
		{
			OutErrorMessage = TEXT("Reference frames must be 1 for ultra-low latency");
			return false;
		}
		OutErrorMessage = TEXT("Validation successful");
		return true;
	}

	FString ToString() const
	{
		return FString::Printf(
			TEXT("EncoderConfig: %dx%d @ %dfps, %.2f Mbps, GOP=%d, Preset=%s, Profile=%s, ZeroCopy=%s"),
			Width, Height, FrameRate,
			Bitrate / 1000000.0,
			GOPSize,
			*EncoderPresetToString(Preset),
			TEXT("Baseline"),
			bZeroCopy ? TEXT("Yes") : TEXT("No")
		);
	}
};
