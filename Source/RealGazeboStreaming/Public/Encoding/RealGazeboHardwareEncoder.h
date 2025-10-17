// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Core/RealGazeboStreamConfig.h"
#include "Pipeline/RealGazeboFrameData.h"
#include "RHI.h"
#include "RHIResources.h"

/**
 * Hardware Encoder Base Interface (HARDWARE ONLY)
 * Abstract base for NVENC and AMF encoders
 *
 * Zero-Copy GPU Texture Encoding:
 * - Direct GPU texture input via FTexture2DRHIRef
 * - No CPU readback or color conversion
 * - NVENC uses CUDA interop (Vulkan/D3D → CUDA)
 * - AMF uses native RHI (Vulkan/D3D direct)
 *
 * Software encoders are NOT supported in RealGazeboStreaming.
 * Required hardware: NVIDIA GTX 600+ or AMD RX 400+
 */
class REALGAZEBOSTREAMING_API IRealGazeboHardwareEncoder
{
public:
	virtual ~IRealGazeboHardwareEncoder() = default;

	/**
	 * Initialize encoder with configuration
	 * @param Config Stream configuration
	 * @return True if initialized successfully
	 */
	virtual bool Initialize(const FRealGazeboStreamConfig& Config) = 0;

	/**
	 * Shutdown encoder and release resources
	 */
	virtual void Shutdown() = 0;

	/**
	 * Encode GPU texture frame to H.264 (HARDWARE ONLY)
	 * Zero-copy encoding - no CPU readback
	 * @param SourceTexture RenderTarget texture from SceneCapture2D
	 * @param OutEncodedFrame Destination encoded frame
	 * @param Timestamp Frame timestamp in seconds
	 * @return True if encoded successfully
	 */
	virtual bool EncodeTextureFrame(FTexture2DRHIRef SourceTexture, TSharedPtr<FEncodedFrameData> OutEncodedFrame, double Timestamp) = 0;

	/**
	 * Request keyframe for next encode
	 */
	virtual void RequestKeyFrame() = 0;

	/**
	 * Update bitrate dynamically
	 * @param NewBitrateKbps New bitrate in kbps
	 */
	virtual void UpdateBitrate(int32 NewBitrateKbps) = 0;

	/**
	 * Get encoder name
	 */
	virtual FString GetEncoderName() const = 0;

	/**
	 * Check if encoder is hardware-accelerated (always true)
	 */
	virtual bool IsHardwareAccelerated() const = 0;

	/**
	 * Check if encoder supports GPU texture input (always true)
	 * @return True (all encoders support texture encoding)
	 */
	virtual bool SupportsTextureEncoding() const { return true; }

	/**
	 * Get SPS (Sequence Parameter Set) for RTSP clients
	 * @param OutSPS Output SPS data
	 * @return True if SPS is available
	 */
	virtual bool GetSPS(TArray<uint8>& OutSPS) { return false; }

	/**
	 * Get PPS (Picture Parameter Set) for RTSP clients
	 * @param OutPPS Output PPS data
	 * @return True if PPS is available
	 */
	virtual bool GetPPS(TArray<uint8>& OutPPS) { return false; }
};
