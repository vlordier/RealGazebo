// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "PixelFormat.h"

/**
 * Pixel Format Utilities
 * Helper functions for working with pixel formats in streaming context
 */
class REALGAZEBOSTREAMING_API FRealGazeboPixelFormatUtils
{
public:
	/**
	 * Check if pixel format is supported for streaming
	 */
	static bool IsSupportedFormat(EPixelFormat Format);

	/**
	 * Get bytes per pixel for a given format
	 */
	static int32 GetBytesPerPixel(EPixelFormat Format);

	/**
	 * Get readable format name
	 */
	static FString GetFormatName(EPixelFormat Format);

	/**
	 * Calculate buffer size for given dimensions and format
	 */
	static int32 GetBufferSize(int32 Width, int32 Height, EPixelFormat Format);

	/**
	 * Check if format requires special alignment
	 */
	static bool RequiresAlignment(EPixelFormat Format);

	/**
	 * Get required alignment for format (in bytes)
	 */
	static int32 GetRequiredAlignment(EPixelFormat Format);

	/**
	 * Calculate YUV420P plane sizes
	 */
	static void GetYUV420PlaneSizes(int32 Width, int32 Height, int32& OutYSize, int32& OutUSize, int32& OutVSize);

	/**
	 * Calculate total YUV420P buffer size
	 */
	static int32 GetYUV420BufferSize(int32 Width, int32 Height);

	/**
	 * Validate dimensions for encoding (must be even for YUV420P)
	 */
	static bool ValidateDimensionsForEncoding(int32 Width, int32 Height);

	/**
	 * Round dimensions to valid encoding size
	 */
	static FIntPoint RoundToValidEncodingSize(const FIntPoint& Dimensions);
};
