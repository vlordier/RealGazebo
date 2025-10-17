// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Encoding/RealGazeboHardwareEncoder.h"
#include "VideoEncoder.h"
#include "VideoEncoderInput.h"
#include "RHI.h"
#include "RHIResources.h"

#if PLATFORM_WINDOWS
#include "ID3D12DynamicRHI.h"
#endif

#if PLATFORM_DESKTOP && !PLATFORM_APPLE
#include "vulkan/vulkan_core.h"
#include "IVulkanDynamicRHI.h"
#endif

namespace AVEncoder
{
	class FVideoEncoder;
	class FVideoEncoderInput;
	class FVideoEncoderInputFrame;
	class FCodecPacket;
}

/**
 * NVIDIA NVENC Hardware Encoder
 *
 * H.264 hardware encoder using NVIDIA NVENC via UE5's AVEncoder API.
 * Supports direct GPU texture input via CUDA for zero-copy encoding.
 *
 * Platform Support:
 * - Linux + Vulkan: Uses CUDA context with Vulkan→CUDA texture interop
 * - Windows + D3D12: Uses CUDA context with D3D12→CUDA texture interop
 * - Windows + D3D11: Uses CUDA context with D3D11→CUDA texture interop
 *
 * Features:
 * - Hardware accelerated encoding on NVIDIA GPUs
 * - Direct GPU texture input (no CPU readback)
 * - Low latency (<5ms encode time)
 * - Dynamic bitrate adjustment
 * - Keyframe on demand
 * - Configurable H.264 profile (Baseline/Main/High)
 * - GOP size control
 */
class REALGAZEBOSTREAMING_API FRealGazeboNVENCEncoder : public IRealGazeboHardwareEncoder
{
public:
	FRealGazeboNVENCEncoder();
	virtual ~FRealGazeboNVENCEncoder();

	//~ Begin IRealGazeboHardwareEncoder Interface
	virtual bool Initialize(const FRealGazeboStreamConfig& Config) override;
	virtual void Shutdown() override;
	virtual bool EncodeTextureFrame(FTexture2DRHIRef SourceTexture, TSharedPtr<FEncodedFrameData> OutEncodedFrame, double Timestamp) override;
	virtual void RequestKeyFrame() override;
	virtual void UpdateBitrate(int32 NewBitrateKbps) override;
	virtual FString GetEncoderName() const override { return TEXT("NVENC"); }
	virtual bool IsHardwareAccelerated() const override { return true; }
	virtual bool SupportsTextureEncoding() const override { return true; }
	virtual bool GetSPS(TArray<uint8>& OutSPS) override;
	virtual bool GetPPS(TArray<uint8>& OutPPS) override;
	//~ End IRealGazeboHardwareEncoder Interface

	/**
	 * Check if NVENC is available on this system
	 * @return True if NVIDIA GPU with NVENC support is present
	 */
	static bool IsAvailable();

private:
	/** Input type for platform-specific texture handling */
	enum class EEncoderInputType : uint8
	{
		CUDA,      // CUDA array (Linux Vulkan, Windows D3D12)
		D3D11,     // D3D11 texture (Windows)
		Vulkan     // Vulkan image (Linux)
	};

	/** AVEncoder instance */
	TUniquePtr<AVEncoder::FVideoEncoder> VideoEncoder;

	/** AVEncoder input */
	TSharedPtr<AVEncoder::FVideoEncoderInput> EncoderInput;

	/** Input type used by encoder */
	EEncoderInputType EncoderInputType = EEncoderInputType::CUDA;

	/** Encoder configuration */
	FRealGazeboStreamConfig StreamConfig;

	/** Initialization state */
	bool bIsInitialized = false;

	/** Keyframe request flag */
	std::atomic<bool> bRequestKeyFrame{false};

	/** Latest encoded packet storage */
	FCriticalSection EncodedDataMutex;
	TArray<uint8> LatestEncodedData;
	bool bHasEncodedData = false;

	/** Cached SPS/PPS (extracted from first keyframe) */
	TArray<uint8> CachedSPS;
	TArray<uint8> CachedPPS;
	bool bHasSPSPPS = false;

	/** Frame counter for GOP management */
	int32 FrameCounter = 0;

	/**
	 * Check if current RHI device is NVIDIA
	 */
	bool IsRHIDeviceNVIDIA() const;

	/**
	 * Check if current RHI device is AMD
	 */
	bool IsRHIDeviceAMD() const;

	/**
	 * Callback for encoded packets from AVEncoder
	 */
	void OnEncodedPacket(uint32 LayerIndex, const TSharedPtr<AVEncoder::FVideoEncoderInputFrame> Frame,
	                     const AVEncoder::FCodecPacket& Packet);

	/**
	 * Parse Annex-B stream and extract SPS/PPS NAL units
	 */
	void ParseAndCacheSPSPPS(const uint8* Data, int32 Size);

	/**
	 * Get H.264 profile from config
	 */
	AVEncoder::FVideoEncoder::H264Profile GetH264Profile() const;

#if PLATFORM_DESKTOP && !PLATFORM_APPLE
	/**
	 * Convert Vulkan texture to CUDA array for encoding (Linux)
	 */
	void SetTextureCUDAVulkan(TSharedPtr<AVEncoder::FVideoEncoderInputFrame> InputFrame, FTexture2DRHIRef Texture);
#endif

#if PLATFORM_WINDOWS
	/**
	 * Convert D3D12 texture to CUDA array for encoding (Windows)
	 */
	void SetTextureCUDAD3D12(TSharedPtr<AVEncoder::FVideoEncoderInputFrame> InputFrame, FTexture2DRHIRef Texture);
#endif
};
