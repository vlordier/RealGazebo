// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHIResources.h"
#include "Misc/Timespan.h"

/**
 * GPU Texture Frame Data (HARDWARE ENCODING ONLY)
 * Zero-copy GPU texture passthrough for NVENC/AMF
 * No CPU readback or color conversion - direct GPU encoding
 *
 * Timing: Uses microsecond precision (int64) following UE5.1 AVEncoder standards
 */
struct REALGAZEBOSTREAMING_API FTextureFrameData
{
	/** GPU texture reference (RHI texture) */
	FTexture2DRHIRef Texture;

	/** Frame dimensions */
	FIntPoint Dimensions;

	/** Capture timestamp in microseconds (int64 for precision) */
	int64 CaptureTimestampUs;

	/** Frame sequence number */
	uint64 FrameNumber;

	FTextureFrameData()
		: Dimensions(FIntPoint::ZeroValue)
		, CaptureTimestampUs(0)
		, FrameNumber(0)
	{
	}

	/** Validate texture frame */
	bool IsValid() const
	{
		return Texture.IsValid() && Dimensions.X > 0 && Dimensions.Y > 0;
	}

	/** Get capture time as FTimespan (type-safe) */
	FTimespan GetCaptureTime() const
	{
		return FTimespan::FromMicroseconds(CaptureTimestampUs);
	}

	/** Get capture time in milliseconds (convenience) */
	double GetCaptureTimeMs() const
	{
		return CaptureTimestampUs / 1000.0;
	}
};

/**
 * Encoded H.264 frame data (NAL units)
 * Ready for RTSP streaming
 *
 * Timing: Uses microsecond precision (int64) following UE5.1 AVEncoder standards
 * Compatible with NVENC/AMF APIs which expect microsecond timestamps
 */
struct REALGAZEBOSTREAMING_API FEncodedFrameData
{
	/** Frame dimensions */
	FIntPoint Dimensions;

	/** Original capture timestamp (microseconds) */
	int64 CaptureTimestampUs;

	/** Encoding completion timestamp (microseconds) */
	int64 EncodingTimestampUs;

	/** Frame sequence number */
	uint64 FrameNumber;

	/** Is this a keyframe (I-frame)? */
	bool bIsKeyFrame;

	/** H.264 NAL units (complete access unit) */
	TArray<uint8> EncodedData;

	/** Presentation timestamp (microseconds) - for RTSP RTP timestamps */
	int64 PresentationTimeUs;

	FEncodedFrameData()
		: Dimensions(FIntPoint::ZeroValue)
		, CaptureTimestampUs(0)
		, EncodingTimestampUs(0)
		, FrameNumber(0)
		, bIsKeyFrame(false)
		, PresentationTimeUs(0)
	{
	}

	FEncodedFrameData(uint64 InFrameNumber, bool bInIsKeyFrame)
		: Dimensions(FIntPoint::ZeroValue)
		, CaptureTimestampUs(0)
		, EncodingTimestampUs(0)
		, FrameNumber(InFrameNumber)
		, bIsKeyFrame(bInIsKeyFrame)
		, PresentationTimeUs(0)
	{
	}

	/** Reset for reuse (pooling) */
	void Reset()
	{
		Dimensions = FIntPoint::ZeroValue;
		CaptureTimestampUs = 0;
		EncodingTimestampUs = 0;
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

	/** Get encoding time in milliseconds (optimized - no FPlatformTime calls) */
	double GetEncodingTimeMs() const
	{
		return (EncodingTimestampUs - CaptureTimestampUs) / 1000.0;
	}

	/** Get encoding duration as FTimespan (type-safe) */
	FTimespan GetEncodingDuration() const
	{
		return FTimespan::FromMicroseconds(EncodingTimestampUs - CaptureTimestampUs);
	}

	/** Get bitrate in Mbps for this frame */
	float GetBitrateMbps(float FrameRateFPS) const
	{
		if (FrameRateFPS <= 0.0f) return 0.0f;
		const float BitsPerSecond = EncodedData.Num() * 8.0f * FrameRateFPS;
		return BitsPerSecond / 1000000.0f;
	}

	/** Get capture time as FTimespan (type-safe) */
	FTimespan GetCaptureTime() const
	{
		return FTimespan::FromMicroseconds(CaptureTimestampUs);
	}

	/** Get encoding completion time as FTimespan (type-safe) */
	FTimespan GetEncodingTime() const
	{
		return FTimespan::FromMicroseconds(EncodingTimestampUs);
	}
};
