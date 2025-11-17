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

#if PLATFORM_DESKTOP && !PLATFORM_APPLE
#include "vulkan/vulkan_core.h"
#include "IVulkanDynamicRHI.h"
#endif

#if PLATFORM_WINDOWS
#include "ID3D11DynamicRHI.h"  // D3D11 support for Windows hardware encoding
#endif

namespace AVEncoder
{
	class FVideoEncoder;
	class FVideoEncoderInput;
	class FVideoEncoderInputFrame;
	class FCodecPacket;
}

/**
 * AMD AMF Hardware Encoder
 *
 * H.264 hardware encoder using AMD AMF via UE5's AVEncoder API.
 * Supports direct GPU texture input via Vulkan/DirectX for zero-copy encoding.
 *
 * Platform Support:
 * - Linux + Vulkan: Direct Vulkan texture input (VkImage)
 * - Windows + D3D11: Direct D3D11 texture input (ID3D11Texture2D)
 *
 * Features:
 * - Hardware accelerated encoding on AMD GPUs
 * - Direct GPU texture input (no CPU readback)
 * - Low latency (<5ms encode time)
 * - Dynamic bitrate adjustment
 * - Keyframe on demand
 * - Configurable H.264 profile (Baseline/Main/High)
 * - GOP size control
 */
class REALGAZEBOSTREAMING_API FRealGazeboAMFEncoder : public IRealGazeboHardwareEncoder
{
public:
	FRealGazeboAMFEncoder();
	virtual ~FRealGazeboAMFEncoder();

	//~ Begin IRealGazeboHardwareEncoder Interface
	virtual bool Initialize(const FRealGazeboStreamConfig& Config) override;
	virtual void Shutdown() override;
	virtual bool EncodeTextureFrame(FTexture2DRHIRef SourceTexture, TSharedPtr<FEncodedFrameData> OutEncodedFrame, int64 TimestampUs) override;
	virtual void RequestKeyFrame() override;
	virtual void UpdateBitrate(int32 NewBitrateKbps) override;
	virtual FString GetEncoderName() const override { return TEXT("AMF"); }
	virtual bool IsHardwareAccelerated() const override { return true; }
	virtual bool SupportsTextureEncoding() const override { return true; }
	virtual bool GetSPS(TArray<uint8>& OutSPS) override;
	virtual bool GetPPS(TArray<uint8>& OutPPS) override;
	//~ End IRealGazeboHardwareEncoder Interface

	/**
	 * Check if AMF is available on this system
	 * @return True if AMD GPU with AMF support is present
	 */
	static bool IsAvailable();

private:
	/** Input type for platform-specific texture handling */
	enum class EEncoderInputType : uint8
	{
		Vulkan,    // Vulkan image (Linux)
		D3D11      // D3D11 texture (Windows)
	};

	/** AVEncoder instance */
	TUniquePtr<AVEncoder::FVideoEncoder> VideoEncoder;

	/** AVEncoder input */
	TSharedPtr<AVEncoder::FVideoEncoderInput> EncoderInput;

	/** Input type used by encoder */
	EEncoderInputType EncoderInputType = EEncoderInputType::Vulkan;

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
};
