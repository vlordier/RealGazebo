// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHIResources.h"

/**
 * GPU Texture Frame Data (HARDWARE ENCODING ONLY)
 * Zero-copy GPU texture passthrough for NVENC/AMF
 * No CPU readback or color conversion - direct GPU encoding
 */
struct REALGAZEBOSTREAMING_API FTextureFrameData
{
	/** GPU texture reference (RHI texture) */
	FTexture2DRHIRef Texture;

	/** Frame dimensions */
	FIntPoint Dimensions;

	/** Timestamp when captured (game thread time) */
	double CaptureTimestamp;

	/** Frame sequence number */
	uint64 FrameNumber;

	FTextureFrameData()
		: Dimensions(FIntPoint::ZeroValue)
		, CaptureTimestamp(0.0)
		, FrameNumber(0)
	{
	}

	/** Validate texture frame */
	bool IsValid() const
	{
		return Texture.IsValid() && Dimensions.X > 0 && Dimensions.Y > 0;
	}
};

/**
 * Encoded H.264 frame data (NAL units)
 * Ready for RTSP streaming
 */
struct REALGAZEBOSTREAMING_API FEncodedFrameData
{
	/** Frame dimensions */
	FIntPoint Dimensions;

	/** Original capture timestamp */
	double CaptureTimestamp;

	/** Encoding completion timestamp */
	double EncodingTimestamp;

	/** Frame sequence number */
	uint64 FrameNumber;

	/** Is this a keyframe (I-frame)? */
	bool bIsKeyFrame;

	/** H.264 NAL units (complete access unit) */
	TArray<uint8> EncodedData;

	/** Presentation timestamp (microseconds) */
	int64 PresentationTimeUs;

	FEncodedFrameData()
		: Dimensions(FIntPoint::ZeroValue)
		, CaptureTimestamp(0.0)
		, EncodingTimestamp(0.0)
		, FrameNumber(0)
		, bIsKeyFrame(false)
		, PresentationTimeUs(0)
	{
	}

	FEncodedFrameData(uint64 InFrameNumber, bool bInIsKeyFrame)
		: Dimensions(FIntPoint::ZeroValue)
		, CaptureTimestamp(0.0)
		, EncodingTimestamp(0.0)
		, FrameNumber(InFrameNumber)
		, bIsKeyFrame(bInIsKeyFrame)
		, PresentationTimeUs(0)
	{
	}

	/** Clone frame data (for pooling) */
	TSharedPtr<FEncodedFrameData> Clone() const
	{
		TSharedPtr<FEncodedFrameData> ClonedFrame = MakeShared<FEncodedFrameData>();
		ClonedFrame->Dimensions = Dimensions;
		ClonedFrame->CaptureTimestamp = CaptureTimestamp;
		ClonedFrame->EncodingTimestamp = EncodingTimestamp;
		ClonedFrame->FrameNumber = FrameNumber;
		ClonedFrame->bIsKeyFrame = bIsKeyFrame;
		ClonedFrame->EncodedData = EncodedData;
		ClonedFrame->PresentationTimeUs = PresentationTimeUs;
		return ClonedFrame;
	}

	/** Reset for reuse (pooling) */
	void Reset()
	{
		Dimensions = FIntPoint::ZeroValue;
		CaptureTimestamp = 0.0;
		EncodingTimestamp = 0.0;
		FrameNumber = 0;
		bIsKeyFrame = false;
		PresentationTimeUs = 0;
		EncodedData.Reset();
	}

	/** Get encoded data size in bytes */
	int32 GetDataSize() const
	{
		return EncodedData.Num();
	}

	/** Validate encoded data */
	bool IsValid() const
	{
		return EncodedData.Num() > 0 && Dimensions.X > 0 && Dimensions.Y > 0;
	}

	/** Get encoding time in milliseconds */
	float GetEncodingTimeMs() const
	{
		return (EncodingTimestamp - CaptureTimestamp) * 1000.0f;
	}

	/** Get bitrate in Mbps for this frame */
	float GetBitrateMbps(float FrameRateFPS) const
	{
		if (FrameRateFPS <= 0.0f) return 0.0f;
		const float BitsPerSecond = EncodedData.Num() * 8.0f * FrameRateFPS;
		return BitsPerSecond / 1000000.0f;
	}
};
