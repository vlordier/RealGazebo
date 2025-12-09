// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
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
 * Cached CUDA resources for a single pooled texture (NVIDIA GPUs on Linux only).
 * Textures are imported once and reused for all frames to minimize overhead.
 *
 * CRITICAL VULKAN TILING FIX:
 * Vulkan uses VK_IMAGE_TILING_OPTIMAL which stores pixels in a GPU-optimized swizzled pattern
 * for fast rendering. When imported into CUDA via external memory, this tiling is preserved
 * but NVENC cannot read swizzled data - it requires linear memory layout.
 *
 * Solution: Two-stage approach:
 * 1. Import Vulkan texture to TiledArray (preserves swizzled layout)
 * 2. Create LinearArray with linear memory layout
 * 3. Use cuMemcpy2D to copy from TiledArray -> LinearArray
 * 4. Feed LinearArray to NVENC for encoding
 *
 * This ensures NVENC receives correctly formatted linear memory it can process.
 */
struct FCachedCUDATexture
{
	//----------------------------------------------------------
	// External Memory Resources (Tiled Layout from Vulkan)
	//----------------------------------------------------------
	CUexternalMemory ExternalMemory = nullptr;  // Vulkan->CUDA memory handle
	CUmipmappedArray MipArray = nullptr;         // Mipmap array wrapper
	CUarray TiledArray = nullptr;                // Swizzled/tiled array from Vulkan

	//----------------------------------------------------------
	// Linear Staging Array (Created via cuArrayCreate)
	//----------------------------------------------------------
	CUarray LinearArray = nullptr;  // Linear array for NVENC input (correct layout)

	//----------------------------------------------------------
	// Metadata
	//----------------------------------------------------------
	bool bValid = false;   // Is this cache entry valid?
	int32 Width = 0;       // Texture width in pixels
	int32 Height = 0;      // Texture height in pixels
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
 * Represents a single encoded NAL (Network Abstraction Layer) unit ready for RTSP streaming.
 * NAL units are the atomic packets of H.264 video, containing either video frames or codec metadata.
 *
 * NAL Unit Types:
 * - Type 1-4: P-frames and B-frames (predictive/bi-directional frames)
 * - Type 5: IDR frames (keyframes, complete independent frames)
 * - Type 7: SPS (Sequence Parameter Set) - codec initialization data
 * - Type 8: PPS (Picture Parameter Set) - frame-level codec parameters
 */
struct FEncodedNALUnit
{
	/** H.264 bitstream data with Annex-B start code (0x00000001) prefix */
	TArray<uint8> Data;

	/** NAL unit type extracted from H.264 header (1-5=frames, 7=SPS, 8=PPS) */
	uint8 NALType = 0;

	/** Is this a keyframe (IDR frame)? Keyframes can be decoded independently */
	bool bIsKeyframe = false;

	/** Presentation timestamp in milliseconds - when this frame should be displayed */
	uint64 TimestampMs = 0;

	/** Sequential frame number since stream started */
	uint64 FrameNumber = 0;

	/** Get NAL unit size in bytes including start code */
	int32 GetSize() const { return Data.Num(); }

	/** Is this NAL unit valid and ready for streaming? */
	bool IsValid() const { return Data.Num() > 0; }

	/** Is this a Sequence Parameter Set (codec initialization)? */
	bool IsSPS() const { return NALType == 7; }

	/** Is this a Picture Parameter Set (frame-level parameters)? */
	bool IsPPS() const { return NALType == 8; }

	/** Is this a video slice (actual encoded frame data)? */
	bool IsSlice() const { return NALType >= 1 && NALType <= 5; }
};

/**
 * FHardwareEncoderWrapper
 *
 * Per-stream hardware video encoder wrapper using GPU acceleration.
 * Wraps Unreal Engine's AVEncoder API to provide NVENC (NVIDIA) or AMF (AMD) encoding.
 *
 * CRITICAL ISOLATION REQUIREMENT:
 * Each stream MUST have its own dedicated encoder instance. Never share encoders between
 * streams as this causes encoder state pollution, leading to corrupted frames and crosstalk.
 *
 * Supported Hardware:
 * - NVENC: NVIDIA GPUs (GTX 1000+ series, all RTX cards)
 *   - Windows: Direct3D 11/12 texture input
 *   - Linux: CUDA texture input via Vulkan->CUDA interop
 * - AMF: AMD GPUs (RX 400+ series, all Radeon 5000/6000/7000)
 *   - Windows: Direct3D 11/12 texture input
 *   - Linux: Vulkan texture input
 *
 * Features:
 * - Automatic GPU detection and encoder selection
 * - Zero-copy GPU pipeline (texture never leaves GPU memory)
 * - H.264 Baseline profile (maximum client compatibility)
 * - Ultra-low latency preset (<16ms encoding time per frame)
 * - Outputs NAL units ready for RTSP/RTP streaming
 *
 * Encoding Pipeline:
 * 1. Initialize() - Detect GPU, create encoder, configure parameters
 * 2. EncodeFrame() - Submit GPU texture to hardware encoder
 * 3. GetEncodedData() - Retrieve encoded NAL units from output queue
 * 4. Shutdown() - Stop encoder and release all GPU resources
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
