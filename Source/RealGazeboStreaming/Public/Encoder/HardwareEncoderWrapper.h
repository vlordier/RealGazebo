// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "StreamingTypes.h"
#include "EncoderConfig.h"
#include "RHI.h"

// Forward declare CUDA types to avoid heavyweight includes in the header
struct CUctx_st;
using CUcontext = CUctx_st*;
struct CUarray_st;
using CUarray = CUarray_st*;
struct CUmipmappedArray_st;
using CUmipmappedArray = CUmipmappedArray_st*;
struct CUextMemory_st;
using CUexternalMemory = CUextMemory_st*;

/**
 * FCachedCUDATexture
 *
 * Cached CUDA resources for a single pooled texture.
 * Import once, reuse for all frames using that texture.
 *
 * CRITICAL FIX: Vulkan textures use VK_IMAGE_TILING_OPTIMAL which stores pixels
 * in a GPU-optimized swizzled pattern. When imported into CUDA via external memory,
 * the tiling is preserved and cannot be read linearly by NVENC.
 *
 * Solution: We create a LINEAR staging CUarray (not from external memory) and
 * copy from the tiled external array to the linear array using cuMemcpy2D.
 * NVENC receives the linear array which it can process correctly.
 */
struct FCachedCUDATexture
{
	// --- External memory resources (tiled layout from Vulkan) ---
	CUexternalMemory ExternalMemory = nullptr;
	CUmipmappedArray MipArray = nullptr;
	CUarray TiledArray = nullptr;  // Renamed: this is the TILED array from Vulkan

	// --- Linear staging array (created via cuArrayCreate, NOT external) ---
	CUarray LinearArray = nullptr;  // NEW: Linear array for NVENC input

	bool bValid = false;
	int32 Width = 0;
	int32 Height = 0;
};

// Forward declarations
namespace AVEncoder
{
	class FVideoEncoder;
	class FVideoEncoderInput;
	class FVideoEncoderInputFrame;
	class FCodecPacket;  // UE 5.1 defines this as class, not struct
	enum class EVideoFrameFormat;
}

/**
 * FEncodedNALUnit
 *
 * Represents a single encoded NAL unit ready for RTSP streaming.
 * Contains H.264 bitstream data + metadata.
 */
struct FEncodedNALUnit
{
	/** NAL unit data (H.264 bitstream with start code 0x00000001) */
	TArray<uint8> Data;

	/** NAL unit type (1=P-frame, 5=IDR/keyframe, 7=SPS, 8=PPS, etc.) */
	uint8 NALType = 0;

	/** Is this a keyframe (IDR)? */
	bool bIsKeyframe = false;

	/** Presentation timestamp (PTS) in milliseconds */
	uint64 TimestampMs = 0;

	/** Frame number */
	uint64 FrameNumber = 0;

	/** Get NAL unit size in bytes */
	int32 GetSize() const { return Data.Num(); }

	/** Is this NAL unit valid? */
	bool IsValid() const { return Data.Num() > 0; }

	/** Is this SPS? */
	bool IsSPS() const { return NALType == 7; }

	/** Is this PPS? */
	bool IsPPS() const { return NALType == 8; }

	/** Is this a slice (actual video data)? */
	bool IsSlice() const { return NALType >= 1 && NALType <= 5; }
};

/**
 * FHardwareEncoderWrapper
 *
 * Per-stream isolated hardware encoder wrapper.
 * Wraps Unreal's AVEncoder API for NVENC/AMF encoding.
 *
 * CRITICAL: Each stream MUST have its own encoder instance!
 * Never share encoders between streams - causes state pollution.
 *
 * Features:
 * - Auto-detect NVENC (NVIDIA) or AMF (AMD)
 * - Zero-copy GPU encoding
 * - H.264 Baseline profile
 * - Ultra-low latency preset
 * - Outputs NAL units for RTSP streaming
 *
 * Encoding Pipeline:
 * 1. Initialize() - Creates encoder, registers input format
 * 2. EncodeFrame() - Submits GPU texture to encoder
 * 3. GetEncodedData() - Retrieves encoded NAL units
 * 4. Shutdown() - Cleans up encoder resources
 */
class FHardwareEncoderWrapper
{
public:
	//----------------------------------------------------------
	// Construction & Initialization
	//----------------------------------------------------------

	/** Constructor */
	FHardwareEncoderWrapper();

	/** Destructor */
	~FHardwareEncoderWrapper();

	/**
	 * Initialize hardware encoder with configuration.
	 *
	 * @param Config - Encoder configuration
	 * @param OutErrorMessage - Error message if failed
	 * @return True if initialized successfully
	 */
	bool Initialize(const FEncoderConfig& Config, FString& OutErrorMessage);

	/** Shutdown encoder and release resources */
	void Shutdown();

	//----------------------------------------------------------
	// Encoding Operations
	//----------------------------------------------------------

	/**
	 * Encode a frame from GPU texture.
	 * This is the zero-copy path - texture stays on GPU.
	 *
	 * @param Texture - GPU texture to encode (from frame pool)
	 * @param FrameNumber - Frame sequence number
	 * @param bForceKeyframe - Force this frame to be a keyframe
	 * @return True if frame submitted successfully
	 */
	bool EncodeFrame(FRHITexture* Texture, uint64 FrameNumber, bool bForceKeyframe = false);

	/**
	 * Retrieve encoded NAL units.
	 * Call this after EncodeFrame() to get output data.
	 *
	 * @param OutNALUnits - Output NAL units
	 * @return True if data retrieved successfully
	 */
	bool GetEncodedData(TArray<FEncodedNALUnit>& OutNALUnits);

	/** Force next frame to be a keyframe (IDR) */
	void ForceKeyframe();

	//----------------------------------------------------------
	// Status & Information
	//----------------------------------------------------------

	/** Is encoder initialized and ready? */
	bool IsReady() const { return bInitialized; }

	/** Get detected encoder type */
	EEncoderType GetEncoderType() const { return EncoderType; }

	/** Get encoder configuration */
	const FEncoderConfig& GetConfig() const { return Config; }

	/** Get encoding statistics string */
	FString GetStatsString() const;

private:
	//----------------------------------------------------------
	// Internal Initialization
	//----------------------------------------------------------

	/** Detect and create appropriate hardware encoder */
	bool CreateHardwareEncoder(FString& OutErrorMessage);

	/** Register encoder callbacks */
	void RegisterEncoderCallbacks();

	/** Map a Vulkan texture to a CUarray for NVENC (Linux, NVIDIA) */
	bool SetCUDATextureFromVulkan(FRHITexture* Texture,
		TSharedPtr<AVEncoder::FVideoEncoderInputFrame> InputFrame,
		FString& OutErrorMessage);

	//----------------------------------------------------------
	// Encoder Callbacks
	//----------------------------------------------------------

	/** Called when encoder outputs data (UE 5.1 callback signature) */
	void OnEncodedFrame(uint32 LayerIndex, const TSharedPtr<AVEncoder::FVideoEncoderInputFrame> Frame, const AVEncoder::FCodecPacket& Packet);

	//----------------------------------------------------------
	// NAL Unit Processing
	//----------------------------------------------------------

	/** Parse encoded bitstream into NAL units */
	void ParseNALUnits(const uint8* BitstreamData, uint32 BitstreamSize, TArray<FEncodedNALUnit>& OutNALUnits);

	/** Extract NAL unit type from H.264 header */
	uint8 ExtractNALType(const uint8* Data, uint32 Size) const;

	//----------------------------------------------------------
	// Configuration
	//----------------------------------------------------------

	/** Encoder configuration */
	FEncoderConfig Config;

	/** Detected encoder type */
	EEncoderType EncoderType = EEncoderType::Unknown;

	/** Cached CUDA context when using NVENC on Linux */
	CUcontext CudaContext = nullptr;

	/** Cache of CUDA imports per texture (import once, reuse) */
	TMap<FRHITexture*, FCachedCUDATexture> CUDATextureCache;

	/** Mutex for CUDA cache access */
	FCriticalSection CUDACacheMutex;

	/** Clear the CUDA texture cache (called on shutdown) */
	void ClearCUDATextureCache();

	//----------------------------------------------------------
	// AVEncoder Instances (PER-STREAM - NOT SHARED!)
	//----------------------------------------------------------

	/** AVEncoder video encoder input (provides frames to encoder) */
	TSharedPtr<AVEncoder::FVideoEncoderInput> VideoEncoderInput;

	/** AVEncoder video encoder instance (UNIQUE per stream!) */
	TUniquePtr<AVEncoder::FVideoEncoder> VideoEncoder;

	//----------------------------------------------------------
	// State
	//----------------------------------------------------------

	/** Is encoder initialized? */
	std::atomic<bool> bInitialized{false};

	/** Is encoder shutting down? */
	std::atomic<bool> bShuttingDown{false};

	/** Force next frame to be keyframe? */
	std::atomic<bool> bForceNextKeyframe{false};

	/** Current frame number */
	std::atomic<uint64> CurrentFrameNumber{0};

	/** Using CUDA mode for encoding? (NVENC on Linux) */
	bool bUseCUDAMode = false;

	//----------------------------------------------------------
	// Output Queue (PER-STREAM - NOT SHARED!)
	//----------------------------------------------------------

	/** Queue of encoded NAL units (PER-INSTANCE - not static!) */
	TQueue<FEncodedNALUnit> NALOutputQueue;

	/** Mutex for NAL queue access */
	FCriticalSection NALQueueMutex;

	//----------------------------------------------------------
	// Statistics
	//----------------------------------------------------------

	/** Total frames encoded */
	std::atomic<uint64> TotalFramesEncoded{0};

	/** Total keyframes encoded */
	std::atomic<uint64> TotalKeyframesEncoded{0};

	/** Total bytes output */
	std::atomic<uint64> TotalBytesOutput{0};

	/** Encoding failures */
	std::atomic<uint64> EncodingFailureCount{0};

	/** Last encode timestamp */
	double LastEncodeTime = 0.0;
};
