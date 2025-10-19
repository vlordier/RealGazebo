// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Encoding/RealGazeboNVENCEncoder.h"
#include "Core/RealGazeboStreamingLogger.h"
#include "VideoEncoderFactory.h"
#include "CudaModule.h"
#include "RHI.h"

#if PLATFORM_DESKTOP && !PLATFORM_APPLE
#include "IVulkanDynamicRHI.h"
#endif

#if PLATFORM_WINDOWS
#include "ID3D12DynamicRHI.h"
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

	// Create encoder input for CUDA (NVIDIA path)
	// Check if CUDA module is loaded
	if (!FModuleManager::Get().IsModuleLoaded("CUDA"))
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: CUDA module is not loaded"));
		return false;
	}

	CUcontext CudaContext = FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext();
	if (!CudaContext)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: Failed to get CUDA context"));
		return false;
	}

	EncoderInput = AVEncoder::FVideoEncoderInput::CreateForCUDA(CudaContext, false);
	if (!EncoderInput.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: Failed to create CUDA encoder input"));
		return false;
	}

	// Setup encoder configuration
	AVEncoder::FVideoEncoder::FLayerConfig LayerConfig;
	LayerConfig.Width = Config.Dimensions.X;
	LayerConfig.Height = Config.Dimensions.Y;
	LayerConfig.MaxFramerate = Config.FPSValue;
	LayerConfig.TargetBitrate = Config.BitrateKbps * 1000; // Convert to bps
	LayerConfig.MaxBitrate = Config.BitrateKbps * 1000 * 2; // Allow up to 2x for peaks
	LayerConfig.H264Profile = GetH264Profile();
	LayerConfig.RateControlMode = AVEncoder::FVideoEncoder::RateControlMode::CBR;

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

	// Determine encoder input type based on RHI
	const ERHIInterfaceType RHIType = RHIGetInterfaceType();
	if (RHIType == ERHIInterfaceType::Vulkan)
	{
		EncoderInputType = EEncoderInputType::CUDA; // Vulkan -> CUDA
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("NVENCEncoder: Using Vulkan -> CUDA path"));
	}
#if PLATFORM_WINDOWS
	else if (RHIType == ERHIInterfaceType::D3D11)
	{
		EncoderInputType = EEncoderInputType::D3D11; // D3D11 direct
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("NVENCEncoder: Using D3D11 -> CUDA path"));
	}
	else if (RHIType == ERHIInterfaceType::D3D12)
	{
		EncoderInputType = EEncoderInputType::CUDA; // D3D12 -> CUDA
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("NVENCEncoder: Using D3D12 -> CUDA path"));
	}
#endif
	else
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: Unsupported RHI type"));
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

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("NVENCEncoder: Shutting down (Total frames encoded: %d)..."), FrameCounter);

	// CRITICAL: Flush encoder to process all pending frames before shutdown
	// This ensures all OnFrameEncoded callbacks fire and frames are properly released
	// Without flushing, ActiveFrames will still contain unreleased frames when
	// EncoderInput is destroyed, causing assertion failures
	if (VideoEncoder.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("NVENCEncoder: Flushing pending frames..."));

		// Call VideoEncoder->Shutdown() which internally flushes the encoder
		// This will block until all pending frames are encoded and callbacks are fired
		VideoEncoder->Shutdown();

		// Now safe to clear callbacks and reset encoder
		VideoEncoder->ClearOnEncodedPacket();
		VideoEncoder.Reset();

		UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("NVENCEncoder: Encoder shut down and flushed"));
	}

	// Now safe to release encoder input (all frames should be released by callbacks)
	if (EncoderInput.IsValid())
	{
		// Flush any remaining frames in the input pool
		EncoderInput->Flush();
		EncoderInput.Reset();
	}

	bIsInitialized = false;

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("NVENCEncoder: Shut down successfully"));
}

bool FRealGazeboNVENCEncoder::EncodeTextureFrame(FTexture2DRHIRef SourceTexture, TSharedPtr<FEncodedFrameData> OutEncodedFrame, double Timestamp)
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
	if (RHIType == ERHIInterfaceType::D3D12)
	{
		SetTextureCUDAD3D12(InputFrame, SourceTexture);
	}
	else if (RHIType == ERHIInterfaceType::D3D11)
	{
		// D3D11: Pass texture directly (no CUDA conversion needed for now)
		// TODO: Implement D3D11 -> CUDA path if needed
		ID3D11Texture2D* D3D11Texture = static_cast<ID3D11Texture2D*>(SourceTexture->GetNativeResource());
		InputFrame->SetTexture(D3D11Texture, [](ID3D11Texture2D* NativeTexture) { /* Released by RHI */ });
	}
#endif

	// Set frame metadata
	const bool bForceKeyFrame = bRequestKeyFrame.load() || (FrameCounter % StreamConfig.GOPSize) == 0;
	InputFrame->SetTimestampUs(static_cast<int64>(Timestamp * 1000000));
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

	// CRITICAL: Set callback to release frame after encoding completes
	// Without this, frames accumulate in ActiveFrames and cause shutdown assertion failures
	EncodeOptions.OnFrameEncoded = [](const TSharedPtr<AVEncoder::FVideoEncoderInputFrame> CompletedFrame)
	{
		if (CompletedFrame.IsValid())
		{
			// Release frame back to pool (removes from ActiveFrames tracking)
			CompletedFrame->Release();
		}
	};

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
		OutEncodedFrame->CaptureTimestamp = Timestamp;
		OutEncodedFrame->FrameNumber = FrameCounter;
		OutEncodedFrame->bIsKeyFrame = bForceKeyFrame;
		OutEncodedFrame->PresentationTimeUs = static_cast<int64>(Timestamp * 1000000);
		OutEncodedFrame->EncodingTimestamp = FPlatformTime::Seconds();
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

	// Update encoder layer config
	AVEncoder::FVideoEncoder::FLayerConfig LayerConfig = VideoEncoder->GetLayerConfig(0);
	LayerConfig.TargetBitrate = NewBitrateKbps * 1000;
	LayerConfig.MaxBitrate = NewBitrateKbps * 1000 * 2;
	VideoEncoder->UpdateLayerConfig(0, LayerConfig);

	UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("NVENCEncoder: Bitrate updated to %d kbps"), NewBitrateKbps);
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
	// Check if AVEncoder factory is available
	AVEncoder::FVideoEncoderFactory& EncoderFactory = AVEncoder::FVideoEncoderFactory::Get();
	if (!EncoderFactory.IsSetup())
	{
		return false;
	}

	// Check if H.264 encoder is available
	return EncoderFactory.HasEncoderForCodec(AVEncoder::ECodecType::H264);
}

bool FRealGazeboNVENCEncoder::IsRHIDeviceNVIDIA() const
{
	// Check GPU adapter name (not RHI type!)
	// GDynamicRHI->GetName() returns "Vulkan" or "D3D11", not GPU vendor
	// GRHIAdapterName contains actual GPU name like "NVIDIA GeForce RTX 3080"
	const FString AdapterName = GRHIAdapterName;
	const FString AdapterNameLower = AdapterName.ToLower();

	return AdapterNameLower.Contains(TEXT("nvidia")) ||
	       AdapterNameLower.Contains(TEXT("geforce")) ||
	       AdapterNameLower.Contains(TEXT("quadro")) ||
	       AdapterNameLower.Contains(TEXT("tesla"));
}

bool FRealGazeboNVENCEncoder::IsRHIDeviceAMD() const
{
	// Check GPU adapter name (not RHI type!)
	const FString AdapterName = GRHIAdapterName;
	const FString AdapterNameLower = AdapterName.ToLower();

	return AdapterNameLower.Contains(TEXT("amd")) ||
	       AdapterNameLower.Contains(TEXT("radeon")) ||
	       AdapterNameLower.Contains(TEXT("ati"));
}

AVEncoder::FVideoEncoder::H264Profile FRealGazeboNVENCEncoder::GetH264Profile() const
{
	switch (StreamConfig.EncodingProfile)
	{
	case EH264Profile::Baseline:
		return AVEncoder::FVideoEncoder::H264Profile::BASELINE;
	case EH264Profile::Main:
		return AVEncoder::FVideoEncoder::H264Profile::MAIN;
	case EH264Profile::High:
		return AVEncoder::FVideoEncoder::H264Profile::HIGH;
	default:
		return AVEncoder::FVideoEncoder::H264Profile::MAIN;
	}
}

void FRealGazeboNVENCEncoder::OnEncodedPacket(uint32 LayerIndex, const TSharedPtr<AVEncoder::FVideoEncoderInputFrame> Frame,
                                               const AVEncoder::FCodecPacket& Packet)
{
	FScopeLock Lock(&EncodedDataMutex);

	// Store encoded data from the packet
	if (Packet.Data.IsValid() && Packet.DataSize > 0)
	{
		LatestEncodedData.SetNum(Packet.DataSize);
		FMemory::Memcpy(LatestEncodedData.GetData(), Packet.Data.Get(), Packet.DataSize);
		bHasEncodedData = true;

		// Extract SPS/PPS from keyframes if not yet cached
		if (Packet.IsKeyFrame && !bHasSPSPPS)
		{
			ParseAndCacheSPSPPS(Packet.Data.Get(), Packet.DataSize);
		}
	}
	else
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("NVENCEncoder: Received empty packet"));
	}
}

void FRealGazeboNVENCEncoder::ParseAndCacheSPSPPS(const uint8* Data, int32 Size)
{
	// Parse H.264 Annex-B stream for SPS/PPS NAL units
	// NAL unit format: 00 00 00 01 [NAL type + data] 00 00 00 01 [next NAL]
	// SPS NAL type: 0x67 (type 7)
	// PPS NAL type: 0x68 (type 8)

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
				UE_LOG(LogRealGazeboStreaming, Log, TEXT("NVENCEncoder: SPS/PPS cached successfully"));
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

#if PLATFORM_WINDOWS
void FRealGazeboNVENCEncoder::SetTextureCUDAD3D12(TSharedPtr<AVEncoder::FVideoEncoderInputFrame> InputFrame, FTexture2DRHIRef Texture)
{
	ID3D12Resource* NativeD3D12Resource = GetID3D12DynamicRHI()->RHIGetResource(Texture);
	const int64 TextureMemorySize = GetID3D12DynamicRHI()->RHIGetResourceMemorySize(Texture);

	check(!GetID3D12DynamicRHI()->RHIIsResourcePlaced(Texture));

	TRefCountPtr<ID3D12Device> OwnerDevice;
	HRESULT QueryResult;
	if ((QueryResult = NativeD3D12Resource->GetDevice(IID_PPV_ARGS(OwnerDevice.GetInitReference()))) != S_OK)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: Failed to get D3D12 device (error: %d)"), QueryResult);
		return;
	}

	HANDLE D3D12TextureHandle;
	if ((QueryResult = OwnerDevice->CreateSharedHandle(NativeD3D12Resource, NULL, GENERIC_ALL, NULL, &D3D12TextureHandle)) != S_OK)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: Failed to get D3D12 texture handle (error: %d)"), QueryResult);
		return;
	}

	FCUDAModule::CUDA().cuCtxPushCurrent(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext());

	CUexternalMemory MappedExternalMemory = nullptr;

	{
		CUDA_EXTERNAL_MEMORY_HANDLE_DESC CudaExtMemHandleDesc = {};
		CudaExtMemHandleDesc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE;
		CudaExtMemHandleDesc.handle.win32.name = NULL;
		CudaExtMemHandleDesc.handle.win32.handle = D3D12TextureHandle;
		CudaExtMemHandleDesc.size = TextureMemorySize;
		CudaExtMemHandleDesc.flags |= CUDA_EXTERNAL_MEMORY_DEDICATED;

		CUresult Result = FCUDAModule::CUDA().cuImportExternalMemory(&MappedExternalMemory, &CudaExtMemHandleDesc);
		if (Result != CUDA_SUCCESS)
		{
			UE_LOG(LogRealGazeboStreaming, Error, TEXT("NVENCEncoder: Failed to import external memory from D3D12 (error: %d)"), Result);
		}
	}

	CUmipmappedArray MappedMipArray = nullptr;
	CUarray MappedArray = nullptr;

	{
		CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC MipmapDesc = {};
		MipmapDesc.numLevels = 1;
		MipmapDesc.offset = 0;
		MipmapDesc.arrayDesc.Width = Texture->GetSizeX();
		MipmapDesc.arrayDesc.Height = Texture->GetSizeY();
		MipmapDesc.arrayDesc.Depth = 1;
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

	InputFrame->SetTexture(MappedArray, AVEncoder::FVideoEncoderInputFrame::EUnderlyingRHI::D3D12, D3D12TextureHandle,
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
