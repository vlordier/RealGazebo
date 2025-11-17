// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Encoding/RealGazeboNVENCEncoder.h"
#include "Core/RealGazeboStreamingTypes.h"
#include "VideoEncoderFactory.h"
#include "CudaModule.h"
#include "RHI.h"

#if PLATFORM_DESKTOP && !PLATFORM_APPLE
#include "IVulkanDynamicRHI.h"
#endif

#if PLATFORM_WINDOWS
#include "ID3D11DynamicRHI.h"  // D3D11 only - D3D12 support removed for stability
#include "D3D11State.h"
#include "D3D11Resources.h"
#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include <VersionHelpers.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"
#endif

FRealGazeboNVENCEncoder::FRealGazeboNVENCEncoder()
{
}

FRealGazeboNVENCEncoder::~FRealGazeboNVENCEncoder()
{
	Shutdown();
}

bool FRealGazeboNVENCEncoder::Initialize(const FRealGazeboStreamConfig& Config)
{
	StreamConfig = Config;

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("NVENCEncoder: Initializing (%dx%d @ %d kbps, %d fps, GOP: %d)"),
		Config.Dimensions.X, Config.Dimensions.Y, Config.BitrateKbps, Config.FPSValue, Config.GOPSize);

	// Check if NVIDIA GPU
	if (!IsRHIDeviceNVIDIA())
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: Not an NVIDIA GPU"));
		return false;
	}

	// Get AVEncoder factory
	AVEncoder::FVideoEncoderFactory& EncoderFactory = AVEncoder::FVideoEncoderFactory::Get();
	if (!EncoderFactory.IsSetup())
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: AVEncoder factory not set up"));
		return false;
	}

	// Get available encoders
	const TArray<AVEncoder::FVideoEncoderInfo>& AvailableEncoders = EncoderFactory.GetAvailable();

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("NVENCEncoder: Found %d registered encoders"), AvailableEncoders.Num());

	if (AvailableEncoders.Num() == 0)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: No encoders registered in factory"));
		return false;
	}

	// Find H.264 encoder
	uint32 EncoderID = 0;
	bool bFoundEncoder = false;
	for (const AVEncoder::FVideoEncoderInfo& Info : AvailableEncoders)
	{
		UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("NVENCEncoder: Available encoder - ID:%u Type:%d MaxRes:%ux%u"),
			Info.ID, (int32)Info.CodecType, Info.MaxWidth, Info.MaxHeight);

		if (Info.CodecType == AVEncoder::ECodecType::H264)
		{
			EncoderID = Info.ID;
			bFoundEncoder = true;
			UE_LOG(LogRealGazeboStreaming, Log, TEXT("NVENCEncoder: Selected H.264 encoder (ID: %u, MaxRes: %ux%u)"),
				Info.ID, Info.MaxWidth, Info.MaxHeight);
			break;
		}
	}

	if (!bFoundEncoder)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: No H.264 encoder found"));
		return false;
	}

	// Ultra-low latency mode (B-frames=0, Baseline profile, GOP=1.0s)
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("NVENCEncoder: Ultra-low latency mode (B-frames=0, Baseline, GOP=%d)"), Config.GOPSize);

	// Validate resolution (NVENC hardware limits: 32x32 to 4096x4096)
	if (Config.Dimensions.X > 4096 || Config.Dimensions.Y > 4096 ||
	    Config.Dimensions.X < 32 || Config.Dimensions.Y < 32)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: Resolution %dx%d outside valid range (32x32 to 4096x4096)"),
			Config.Dimensions.X, Config.Dimensions.Y);
		return false;
	}

	// Warn if not 16-pixel aligned (suboptimal encoding efficiency)
	if (Config.Dimensions.X % 16 != 0 || Config.Dimensions.Y % 16 != 0)
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("NVENCEncoder: Resolution %dx%d not 16-aligned (may reduce efficiency)"),
			Config.Dimensions.X, Config.Dimensions.Y);
	}

	// Validate bitrate (600-8000 kbps for RTSP/RTP streaming)
	if (Config.BitrateKbps < 600 || Config.BitrateKbps > 8000)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: Bitrate %d kbps outside range (600-8000)"),
			Config.BitrateKbps);
		return false;
	}

	// Validate GOP (30-60 frames = 1.0s @ 30/60 FPS)
	if (Config.GOPSize < 30 || Config.GOPSize > 60)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: GOP %d outside range (30-60)"), Config.GOPSize);
		return false;
	}

	// Set NVENC keyframe interval to match GOP (prevents decoder errors)
	// Default NVENC = 300 frames causes "decode_slice_header error" due to DPB overflow
	IConsoleVariable* CVarKeyframeInterval = IConsoleManager::Get().FindConsoleVariable(TEXT("NVENC.KeyframeInterval"));
	if (CVarKeyframeInterval)
	{
		CVarKeyframeInterval->Set(Config.GOPSize);
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("NVENCEncoder: Keyframe interval set to %d"), Config.GOPSize);
	}
	else
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: Failed to set keyframe interval (may cause decoder errors)"));
	}

	// Create CUDA encoder input (required for NVENC)
	if (!FModuleManager::Get().IsModuleLoaded("CUDA"))
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: CUDA module not loaded"));
		return false;
	}

	CUcontext CudaContext = FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext();
	if (!CudaContext)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: No CUDA context"));
		return false;
	}

	EncoderInput = AVEncoder::FVideoEncoderInput::CreateForCUDA(CudaContext, false);
	if (!EncoderInput.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: Failed to create input"));
		return false;
	}

	// Setup encoder configuration
	AVEncoder::FVideoEncoder::FLayerConfig LayerConfig;
	LayerConfig.Width = Config.Dimensions.X;
	LayerConfig.Height = Config.Dimensions.Y;

	LayerConfig.MaxFramerate = Config.FPSValue;

	// Bitrate config (VBR for quality, clamped to 8 Mbps max)
	LayerConfig.TargetBitrate = Config.BitrateKbps * 1000;
	constexpr int32 MAX_BITRATE_BPS = 8000 * 1000;  // 8 Mbps ceiling
	LayerConfig.MaxBitrate = FMath::Min(Config.BitrateKbps * 2000, MAX_BITRATE_BPS);
	LayerConfig.RateControlMode = AVEncoder::FVideoEncoder::RateControlMode::VBR;

	// Baseline profile for maximum RTSP/RTP compatibility
	LayerConfig.H264Profile = AVEncoder::FVideoEncoder::H264Profile::BASELINE;

	// Create video encoder
	VideoEncoder = EncoderFactory.Create(EncoderID, EncoderInput, LayerConfig);
	if (!VideoEncoder.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: Failed to create encoder instance"));
		return false;
	}

	// Set packet callback
	VideoEncoder->SetOnEncodedPacket([this](uint32 LayerIndex, const TSharedPtr<AVEncoder::FVideoEncoderInputFrame> Frame,
	                                         const AVEncoder::FCodecPacket& Packet)
	{
		OnEncodedPacket(LayerIndex, Frame, Packet);
	});

	// Detect RHI type (Vulkan or D3D11 only)
	const ERHIInterfaceType RHIType = RHIGetInterfaceType();
	if (RHIType == ERHIInterfaceType::Vulkan)
	{
		EncoderInputType = EEncoderInputType::CUDA;
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("NVENCEncoder: Vulkan -> CUDA"));
	}
#if PLATFORM_WINDOWS
	else if (RHIType == ERHIInterfaceType::D3D11)
	{
		EncoderInputType = EEncoderInputType::D3D11;
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("NVENCEncoder: D3D11 -> CUDA"));
	}
#endif
	else
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: Unsupported RHI (need Vulkan or D3D11)"));
		return false;
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("NVENCEncoder: Initialized successfully"));
	bIsInitialized = true;
	return true;
}

void FRealGazeboNVENCEncoder::Shutdown()
{
	if (!bIsInitialized)
	{
		return;
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("NVENCEncoder: Shutting down (%d frames)"), FrameCounter);
	bIsInitialized = false;

	// Shutdown sequence (prevents buffer crash):
	// 1. Clear callback, 2. Flush input, 3. Destroy encoder, 4. Destroy input
	if (VideoEncoder.IsValid())
	{
		VideoEncoder->ClearOnEncodedPacket();
	}

	if (EncoderInput.IsValid())
	{
		EncoderInput->Flush();  // Release pending frames before encoder shutdown
	}

	if (VideoEncoder.IsValid())
	{
		VideoEncoder->Shutdown();
		VideoEncoder.Reset();
	}

	if (EncoderInput.IsValid())
	{
		EncoderInput.Reset();
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("NVENCEncoder: Shutdown complete"));
}

bool FRealGazeboNVENCEncoder::EncodeTextureFrame(FTexture2DRHIRef SourceTexture, TSharedPtr<FEncodedFrameData> OutEncodedFrame, int64 TimestampUs)
{
	if (!bIsInitialized || !VideoEncoder.IsValid() || !SourceTexture.IsValid() || !OutEncodedFrame.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("NVENCEncoder: Invalid state for encoding"));
		return false;
	}

	// Obtain input frame from encoder
	TSharedPtr<AVEncoder::FVideoEncoderInputFrame> InputFrame = EncoderInput->ObtainInputFrame();
	if (!InputFrame.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("NVENCEncoder: Failed to obtain input frame"));
		return false;
	}

	// Set frame dimensions
	InputFrame->SetWidth(SourceTexture->GetSizeX());
	InputFrame->SetHeight(SourceTexture->GetSizeY());

	// Set texture based on RHI type
	const ERHIInterfaceType RHIType = RHIGetInterfaceType();

#if PLATFORM_DESKTOP && !PLATFORM_APPLE
	if (RHIType == ERHIInterfaceType::Vulkan)
	{
		SetTextureCUDAVulkan(InputFrame, SourceTexture);
	}
#endif
#if PLATFORM_WINDOWS
	// D3D12 support removed - using D3D11 only
	if (RHIType == ERHIInterfaceType::D3D11)
	{
		// D3D11: Pass texture directly to NVENC encoder
		ID3D11Texture2D* D3D11Texture = static_cast<ID3D11Texture2D*>(SourceTexture->GetNativeResource());
		InputFrame->SetTexture(D3D11Texture, [](ID3D11Texture2D* NativeTexture) { /* Released by RHI */ });
	}
	else
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: Unsupported RHI type on Windows. Only D3D11 is supported."));
		return false;
	}
#endif

	// Set frame metadata
	const bool bForceKeyFrame = bRequestKeyFrame.load() || (FrameCounter % StreamConfig.GOPSize) == 0;
	InputFrame->SetTimestampUs(TimestampUs);
	InputFrame->SetFrameID(FrameCounter);

	// Clear previous encoded data
	{
		FScopeLock Lock(&EncodedDataMutex);
		bHasEncodedData = false;
		LatestEncodedData.Empty();
	}

	// Encode the frame
	AVEncoder::FVideoEncoder::FEncodeOptions EncodeOptions;
	EncodeOptions.bForceKeyFrame = bForceKeyFrame;

	// NOTE: Frame release is handled in OnEncodedPacket() callback (not here)
	// The SetOnEncodedPacket() callback (set during Initialize()) receives the Frame parameter
	// and calls Frame->Release() to decrement ActiveFrames count

	VideoEncoder->Encode(InputFrame, EncodeOptions);

	// Wait briefly for encoded data (callback should fire quickly)
	const double WaitStartTime = FPlatformTime::Seconds();
	const double MaxWaitTime = 0.05; // 50ms max wait

	while (!bHasEncodedData && (FPlatformTime::Seconds() - WaitStartTime) < MaxWaitTime)
	{
		FPlatformProcess::Sleep(0.001f); // 1ms sleep
	}

	// Copy encoded data to output
	{
		FScopeLock Lock(&EncodedDataMutex);

		if (!bHasEncodedData || LatestEncodedData.Num() == 0)
		{
			UE_LOG(LogRealGazeboStreaming, Warning, TEXT("NVENCEncoder: No encoded data received for frame %d"), FrameCounter);
			return false;
		}

		// Copy encoded data
		OutEncodedFrame->EncodedData = LatestEncodedData;
		OutEncodedFrame->Dimensions = FIntPoint(SourceTexture->GetSizeX(), SourceTexture->GetSizeY());
		OutEncodedFrame->CaptureTimestampUs = TimestampUs;
		OutEncodedFrame->FrameNumber = FrameCounter;
		OutEncodedFrame->bIsKeyFrame = bForceKeyFrame;
		OutEncodedFrame->PresentationTimeUs = TimestampUs;
		OutEncodedFrame->EncodingTimestampUs = RealGazeboStreamingTime::GetTimeMicroseconds();
	}

	bRequestKeyFrame.store(false);
	FrameCounter++;

	return true;
}

void FRealGazeboNVENCEncoder::RequestKeyFrame()
{
	bRequestKeyFrame.store(true);
	UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("NVENCEncoder: Keyframe requested"));
}

void FRealGazeboNVENCEncoder::UpdateBitrate(int32 NewBitrateKbps)
{
	if (!bIsInitialized || !VideoEncoder.IsValid())
	{
		return;
	}

	StreamConfig.BitrateKbps = NewBitrateKbps;

	AVEncoder::FVideoEncoder::FLayerConfig LayerConfig = VideoEncoder->GetLayerConfig(0);
	LayerConfig.TargetBitrate = NewBitrateKbps * 1000;
	constexpr int32 MAX_BITRATE_BPS = 8000 * 1000;
	LayerConfig.MaxBitrate = FMath::Min(NewBitrateKbps * 2000, MAX_BITRATE_BPS);
	VideoEncoder->UpdateLayerConfig(0, LayerConfig);

	UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("NVENCEncoder: Bitrate = %d kbps"), NewBitrateKbps);
}

bool FRealGazeboNVENCEncoder::GetSPS(TArray<uint8>& OutSPS)
{
	FScopeLock Lock(&EncodedDataMutex);
	if (bHasSPSPPS && CachedSPS.Num() > 0)
	{
		OutSPS = CachedSPS;
		return true;
	}
	return false;
}

bool FRealGazeboNVENCEncoder::GetPPS(TArray<uint8>& OutPPS)
{
	FScopeLock Lock(&EncodedDataMutex);
	if (bHasSPSPPS && CachedPPS.Num() > 0)
	{
		OutPPS = CachedPPS;
		return true;
	}
	return false;
}

bool FRealGazeboNVENCEncoder::IsAvailable()
{
	// Fix for PIE encoder registry wipe: Reload EncoderNVENC module if empty but NVIDIA GPU present
	AVEncoder::FVideoEncoderFactory& EncoderFactory = AVEncoder::FVideoEncoderFactory::Get();

	if (!EncoderFactory.IsSetup())
	{
		return false;
	}

	const TArray<AVEncoder::FVideoEncoderInfo>& AvailableEncoders = EncoderFactory.GetAvailable();

	// If registry empty but NVIDIA GPU present, reload module
	if (AvailableEncoders.Num() == 0)
	{
		const FString AdapterNameLower = GRHIAdapterName.ToLower();
		bool bIsNVIDIA = AdapterNameLower.Contains(TEXT("nvidia")) ||
		                 AdapterNameLower.Contains(TEXT("geforce")) ||
		                 AdapterNameLower.Contains(TEXT("quadro")) ||
		                 AdapterNameLower.Contains(TEXT("tesla"));

		if (bIsNVIDIA)
		{
			FModuleManager& ModuleManager = FModuleManager::Get();
			if (ModuleManager.IsModuleLoaded("EncoderNVENC"))
			{
				ModuleManager.UnloadModule("EncoderNVENC");
			}

			if (!ModuleManager.LoadModule("EncoderNVENC"))
			{
				UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: Failed to reload module"));
				return false;
			}
		}
		else
		{
			return false;
		}
	}

	return EncoderFactory.HasEncoderForCodec(AVEncoder::ECodecType::H264);
}

bool FRealGazeboNVENCEncoder::IsRHIDeviceNVIDIA() const
{
	const FString AdapterNameLower = GRHIAdapterName.ToLower();
	return AdapterNameLower.Contains(TEXT("nvidia")) ||
	       AdapterNameLower.Contains(TEXT("geforce")) ||
	       AdapterNameLower.Contains(TEXT("quadro")) ||
	       AdapterNameLower.Contains(TEXT("tesla"));
}

AVEncoder::FVideoEncoder::H264Profile FRealGazeboNVENCEncoder::GetH264Profile() const
{
	return AVEncoder::FVideoEncoder::H264Profile::BASELINE;
}

void FRealGazeboNVENCEncoder::OnEncodedPacket(uint32 LayerIndex, const TSharedPtr<AVEncoder::FVideoEncoderInputFrame> Frame,
                                               const AVEncoder::FCodecPacket& Packet)
{
	// Release frame (prevents ActiveFrames assertion on shutdown)
	if (Frame.IsValid())
	{
		Frame->Release();
	}

	FScopeLock Lock(&EncodedDataMutex);

	// Validate packet data before memcpy
	if (Packet.Data.IsValid() && Packet.DataSize > 0 && Packet.Data.Get() != nullptr)
	{
		LatestEncodedData.SetNum(Packet.DataSize);
		FMemory::Memcpy(LatestEncodedData.GetData(), Packet.Data.Get(), Packet.DataSize);
		bHasEncodedData = true;

		// Extract SPS/PPS from first keyframe
		if (Packet.IsKeyFrame && !bHasSPSPPS)
		{
			ParseAndCacheSPSPPS(Packet.Data.Get(), Packet.DataSize);
		}
	}
	else
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("NVENCEncoder: Invalid packet (size: %d)"), Packet.DataSize);
	}
}

void FRealGazeboNVENCEncoder::ParseAndCacheSPSPPS(const uint8* Data, int32 Size)
{
	// Parse Annex-B stream: SPS=0x67 (type 7), PPS=0x68 (type 8)

	int32 Offset = 0;
	while (Offset + 4 < Size)
	{
		// Look for start code 00 00 00 01
		if (Data[Offset] == 0x00 && Data[Offset + 1] == 0x00 &&
			Data[Offset + 2] == 0x00 && Data[Offset + 3] == 0x01)
		{
			Offset += 4; // Skip start code

			if (Offset >= Size)
				break;

			const uint8 NALType = Data[Offset] & 0x1F;

			// Find next start code to get NAL unit size
			int32 NALEnd = Offset + 1;
			while (NALEnd + 3 < Size)
			{
				if (Data[NALEnd] == 0x00 && Data[NALEnd + 1] == 0x00 &&
					(Data[NALEnd + 2] == 0x00 || Data[NALEnd + 2] == 0x01))
				{
					break;
				}
				NALEnd++;
			}

			const int32 NALSize = NALEnd - Offset;

			if (NALType == 7) // SPS
			{
				CachedSPS.SetNum(NALSize);
				FMemory::Memcpy(CachedSPS.GetData(), &Data[Offset], NALSize);
				UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("NVENCEncoder: Extracted SPS (%d bytes)"), NALSize);
			}
			else if (NALType == 8) // PPS
			{
				CachedPPS.SetNum(NALSize);
				FMemory::Memcpy(CachedPPS.GetData(), &Data[Offset], NALSize);
				UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("NVENCEncoder: Extracted PPS (%d bytes)"), NALSize);
			}

			if (CachedSPS.Num() > 0 && CachedPPS.Num() > 0)
			{
				bHasSPSPPS = true;
				UE_LOG(LogRealGazeboStreaming, Warning, TEXT("NVENCEncoder: *** SPS/PPS CACHED SUCCESSFULLY *** (SPS: %d bytes, PPS: %d bytes)"),
					CachedSPS.Num(), CachedPPS.Num());
				return;
			}

			Offset = NALEnd;
		}
		else
		{
			Offset++;
		}
	}
}

#if PLATFORM_DESKTOP && !PLATFORM_APPLE
void FRealGazeboNVENCEncoder::SetTextureCUDAVulkan(TSharedPtr<AVEncoder::FVideoEncoderInputFrame> InputFrame, FTexture2DRHIRef Texture)
{
	IVulkanDynamicRHI* VulkanRHI = GetIVulkanDynamicRHI();

	const FVulkanRHIAllocationInfo TextureAllocationInfo = VulkanRHI->RHIGetAllocationInfo(Texture.GetReference());
	VkDevice Device = VulkanRHI->RHIGetVkDevice();

#if PLATFORM_WINDOWS
	HANDLE Handle;
	bool bUseNTHandle = IsWindows8OrGreater();

	{
		VkMemoryGetWin32HandleInfoKHR MemoryGetHandleInfoKHR = {};
		MemoryGetHandleInfoKHR.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
		MemoryGetHandleInfoKHR.pNext = NULL;
		MemoryGetHandleInfoKHR.memory = TextureAllocationInfo.Handle;
		MemoryGetHandleInfoKHR.handleType = bUseNTHandle ? VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT : VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT;

		PFN_vkGetMemoryWin32HandleKHR GetMemoryWin32HandleKHR = (PFN_vkGetMemoryWin32HandleKHR)VulkanRHI->RHIGetVkDeviceProcAddr("vkGetMemoryWin32HandleKHR");
		VERIFYVULKANRESULT_EXTERNAL(GetMemoryWin32HandleKHR(Device, &MemoryGetHandleInfoKHR, &Handle));
	}

	FCUDAModule::CUDA().cuCtxPushCurrent(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext());

	CUexternalMemory MappedExternalMemory = nullptr;

	{
		CUDA_EXTERNAL_MEMORY_HANDLE_DESC CudaExtMemHandleDesc = {};
		CudaExtMemHandleDesc.type = bUseNTHandle ? CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32 : CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT;
		CudaExtMemHandleDesc.handle.win32.name = NULL;
		CudaExtMemHandleDesc.handle.win32.handle = Handle;
		CudaExtMemHandleDesc.size = TextureAllocationInfo.Offset + TextureAllocationInfo.Size;

		CUresult Result = FCUDAModule::CUDA().cuImportExternalMemory(&MappedExternalMemory, &CudaExtMemHandleDesc);
		if (Result != CUDA_SUCCESS)
		{
			UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: Failed to import external memory from Vulkan (error: %d)"), Result);
		}
	}

	Handle = bUseNTHandle ? Handle : NULL;
#else
	void* Handle = nullptr;
	int Fd;

	{
		VkMemoryGetFdInfoKHR MemoryGetFdInfoKHR = {};
		MemoryGetFdInfoKHR.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
		MemoryGetFdInfoKHR.pNext = NULL;
		MemoryGetFdInfoKHR.memory = TextureAllocationInfo.Handle;
		MemoryGetFdInfoKHR.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;

		PFN_vkGetMemoryFdKHR FPGetMemoryFdKHR = (PFN_vkGetMemoryFdKHR)VulkanRHI->RHIGetVkDeviceProcAddr("vkGetMemoryFdKHR");
		VERIFYVULKANRESULT_EXTERNAL(FPGetMemoryFdKHR(Device, &MemoryGetFdInfoKHR, &Fd));
	}

	FCUDAModule::CUDA().cuCtxPushCurrent(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext());

	CUexternalMemory MappedExternalMemory = nullptr;

	{
		CUDA_EXTERNAL_MEMORY_HANDLE_DESC CudaExtMemHandleDesc = {};
		CudaExtMemHandleDesc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD;
		CudaExtMemHandleDesc.handle.fd = Fd;
		CudaExtMemHandleDesc.size = TextureAllocationInfo.Offset + TextureAllocationInfo.Size;

		CUresult Result = FCUDAModule::CUDA().cuImportExternalMemory(&MappedExternalMemory, &CudaExtMemHandleDesc);
		if (Result != CUDA_SUCCESS)
		{
			UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: Failed to import external memory from Vulkan (error: %d)"), Result);
		}
	}
#endif

	CUmipmappedArray MappedMipArray = nullptr;
	CUarray MappedArray = nullptr;

	{
		CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC MipmapDesc = {};
		MipmapDesc.numLevels = 1;
		MipmapDesc.offset = TextureAllocationInfo.Offset;
		MipmapDesc.arrayDesc.Width = Texture->GetSizeX();
		MipmapDesc.arrayDesc.Height = Texture->GetSizeY();
		MipmapDesc.arrayDesc.Depth = 0;
		MipmapDesc.arrayDesc.NumChannels = 4;
		MipmapDesc.arrayDesc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
		MipmapDesc.arrayDesc.Flags = CUDA_ARRAY3D_SURFACE_LDST | CUDA_ARRAY3D_COLOR_ATTACHMENT;

		CUresult Result = FCUDAModule::CUDA().cuExternalMemoryGetMappedMipmappedArray(&MappedMipArray, MappedExternalMemory, &MipmapDesc);
		if (Result != CUDA_SUCCESS)
		{
			UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: Failed to bind mipmappedArray (error: %d)"), Result);
		}
	}

	CUresult MipMapArrGetLevelErr = FCUDAModule::CUDA().cuMipmappedArrayGetLevel(&MappedArray, MappedMipArray, 0);
	if (MipMapArrGetLevelErr != CUDA_SUCCESS)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: Failed to bind to mip 0"));
	}

	FCUDAModule::CUDA().cuCtxPopCurrent(NULL);

	InputFrame->SetTexture(MappedArray, AVEncoder::FVideoEncoderInputFrame::EUnderlyingRHI::Vulkan, Handle,
		[MappedArray, MappedMipArray, MappedExternalMemory](CUarray NativeTexture) {
		FCUDAModule::CUDA().cuCtxPushCurrent(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext());

		if (MappedArray)
		{
			FCUDAModule::CUDA().cuArrayDestroy(MappedArray);
		}

		if (MappedMipArray)
		{
			FCUDAModule::CUDA().cuMipmappedArrayDestroy(MappedMipArray);
		}

		if (MappedExternalMemory)
		{
			FCUDAModule::CUDA().cuDestroyExternalMemory(MappedExternalMemory);
		}

		FCUDAModule::CUDA().cuCtxPopCurrent(NULL);
	});
}
#endif
