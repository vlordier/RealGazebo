// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Utils/RealGazeboPixelFormatUtils.h"

bool FRealGazeboPixelFormatUtils::IsSupportedFormat(EPixelFormat Format)
{
	// We primarily support 8-bit RGBA formats for color conversion
	switch (Format)
	{
	case PF_B8G8R8A8:
	case PF_R8G8B8A8:
		return true;
	default:
		return false;
	}
}

int32 FRealGazeboPixelFormatUtils::GetBytesPerPixel(EPixelFormat Format)
{
	switch (Format)
	{
	case PF_B8G8R8A8:
	case PF_R8G8B8A8:
		return 4;
	case PF_FloatRGBA:
		return 16;
	case PF_A16B16G16R16:
		return 8;
	default:
		return 0;
	}
}

FString FRealGazeboPixelFormatUtils::GetFormatName(EPixelFormat Format)
{
	switch (Format)
	{
	case PF_B8G8R8A8:
		return TEXT("BGRA8");
	case PF_R8G8B8A8:
		return TEXT("RGBA8");
	case PF_FloatRGBA:
		return TEXT("FloatRGBA");
	case PF_A16B16G16R16:
		return TEXT("RGBA16");
	default:
		return TEXT("Unknown");
	}
}

int32 FRealGazeboPixelFormatUtils::GetBufferSize(int32 Width, int32 Height, EPixelFormat Format)
{
	const int32 BytesPerPixel = GetBytesPerPixel(Format);
	return Width * Height * BytesPerPixel;
}

bool FRealGazeboPixelFormatUtils::RequiresAlignment(EPixelFormat Format)
{
	// Most formats benefit from cache-line alignment (64 bytes)
	return true;
}

int32 FRealGazeboPixelFormatUtils::GetRequiredAlignment(EPixelFormat Format)
{
	// Cache-line alignment (64 bytes) for optimal SIMD performance
	return 64;
}

void FRealGazeboPixelFormatUtils::GetYUV420PlaneSizes(int32 Width, int32 Height, int32& OutYSize, int32& OutUSize, int32& OutVSize)
{
	// Y plane: full resolution (Width x Height)
	OutYSize = Width * Height;

	// U and V planes: quarter resolution (Width/2 x Height/2 each)
	const int32 ChromaWidth = Width / 2;
	const int32 ChromaHeight = Height / 2;
	OutUSize = ChromaWidth * ChromaHeight;
	OutVSize = ChromaWidth * ChromaHeight;
}

int32 FRealGazeboPixelFormatUtils::GetYUV420BufferSize(int32 Width, int32 Height)
{
	int32 YSize, USize, VSize;
	GetYUV420PlaneSizes(Width, Height, YSize, USize, VSize);
	return YSize + USize + VSize;
}

bool FRealGazeboPixelFormatUtils::ValidateDimensionsForEncoding(int32 Width, int32 Height)
{
	// YUV420P requires even dimensions (due to 2x2 chroma subsampling)
	return (Width % 2 == 0) && (Height % 2 == 0) && (Width > 0) && (Height > 0);
}

FIntPoint FRealGazeboPixelFormatUtils::RoundToValidEncodingSize(const FIntPoint& Dimensions)
{
	// Round down to nearest even number
	return FIntPoint(
		(Dimensions.X / 2) * 2,
		(Dimensions.Y / 2) * 2
	);
}
