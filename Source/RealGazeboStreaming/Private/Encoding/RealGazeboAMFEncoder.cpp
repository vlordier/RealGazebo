// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Encoding/RealGazeboAMFEncoder.h"
#include "Core/RealGazeboStreamingLogger.h"
#include "VideoEncoderFactory.h"
#include "RHI.h"

#if PLATFORM_DESKTOP && !PLATFORM_APPLE
#include "IVulkanDynamicRHI.h"
#endif

#if PLATFORM_WINDOWS
#include "ID3D12DynamicRHI.h"
#include "D3D11State.h"
#include "D3D11Resources.h"
#endif

FRealGazeboAMFEncoder::FRealGazeboAMFEncoder()
{
}

FRealGazeboAMFEncoder::~FRealGazeboAMFEncoder()
{
	Shutdown();
}

bool FRealGazeboAMFEncoder::Initialize(const FRealGazeboStreamConfig& Config)
{
	StreamConfig = Config;

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("AMFEncoder: Initializing (%dx%d @ %d kbps, %d fps, GOP: %d)"),
		Config.Dimensions.X, Config.Dimensions.Y, Config.BitrateKbps, Config.FPSValue, Config.GOPSize);

	// Check if AMD GPU
	if (!IsRHIDeviceAMD())
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("AMFEncoder: Not an AMD GPU"));
		return false;
	}

	// Get AVEncoder factory
	AVEncoder::FVideoEncoderFactory& EncoderFactory = AVEncoder::FVideoEncoderFactory::Get();
	if (!EncoderFactory.IsSetup())
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("AMFEncoder: AVEncoder factory not set up"));
		return false;
	}

	// Get available encoders
	const TArray<AVEncoder::FVideoEncoderInfo>& AvailableEncoders = EncoderFactory.GetAvailable();

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("AMFEncoder: Found %d registered encoders"), AvailableEncoders.Num());

	if (AvailableEncoders.Num() == 0)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("AMFEncoder: No encoders registered in factory"));
		return false;
	}

	// Find H.264 encoder
	uint32 EncoderID = 0;
	bool bFoundEncoder = false;
	for (const AVEncoder::FVideoEncoderInfo& Info : AvailableEncoders)
	{
		UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("AMFEncoder: Available encoder - ID:%u Type:%d MaxRes:%ux%u"),
			Info.ID, (int32)Info.CodecType, Info.MaxWidth, Info.MaxHeight);

		if (Info.CodecType == AVEncoder::ECodecType::H264)
		{
			EncoderID = Info.ID;
			bFoundEncoder = true;
			UE_LOG(LogRealGazeboStreaming, Log, TEXT("AMFEncoder: Selected H.264 encoder (ID: %u, MaxRes: %ux%u)"),
				Info.ID, Info.MaxWidth, Info.MaxHeight);
			break;
		}
	}

	if (!bFoundEncoder)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("AMFEncoder: No H.264 encoder found"));
		return false;
	}

	// Create encoder input based on RHI type (AMD uses direct texture passthrough)
	const ERHIInterfaceType RHIType = RHIGetInterfaceType();

	if (RHIType == ERHIInterfaceType::Vulkan)
	{
#if PLATFORM_DESKTOP && !PLATFORM_APPLE
		IVulkanDynamicRHI* DynamicRHI = GetDynamicRHI<IVulkanDynamicRHI>();
		AVEncoder::FVulkanDataStruct VulkanData = {
			DynamicRHI->RHIGetVkInstance(),
			DynamicRHI->RHIGetVkPhysicalDevice(),
			DynamicRHI->RHIGetVkDevice()
		};
		EncoderInput = AVEncoder::FVideoEncoderInput::CreateForVulkan(&VulkanData, false);
		EncoderInputType = EEncoderInputType::Vulkan;
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("AMFEncoder: Using Vulkan direct path"));
#else
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("AMFEncoder: Vulkan not supported on this platform"));
		return false;
#endif
	}
#if PLATFORM_WINDOWS
	else if (RHIType == ERHIInterfaceType::D3D11)
	{
		EncoderInput = AVEncoder::FVideoEncoderInput::CreateForD3D11(GDynamicRHI->RHIGetNativeDevice(), false, true);
		EncoderInputType = EEncoderInputType::D3D11;
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("AMFEncoder: Using D3D11 direct path"));
	}
	else if (RHIType == ERHIInterfaceType::D3D12)
	{
		EncoderInput = AVEncoder::FVideoEncoderInput::CreateForD3D12(GDynamicRHI->RHIGetNativeDevice(), false, false);
		EncoderInputType = EEncoderInputType::D3D12;
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("AMFEncoder: Using D3D12 direct path"));
	}
#endif
	else
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("AMFEncoder: Unsupported RHI type"));
		return false;
	}

	if (!EncoderInput.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("AMFEncoder: Failed to create encoder input"));
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
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("AMFEncoder: Failed to create encoder instance"));
		return false;
	}

	// Set packet callback
	VideoEncoder->SetOnEncodedPacket([this](uint32 LayerIndex, const TSharedPtr<AVEncoder::FVideoEncoderInputFrame> Frame,
	                                         const AVEncoder::FCodecPacket& Packet)
	{
		OnEncodedPacket(LayerIndex, Frame, Packet);
	});

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("AMFEncoder: Initialized successfully"));
	bIsInitialized = true;
	return true;
}

void FRealGazeboAMFEncoder::Shutdown()
{
	if (!bIsInitialized)
	{
		return;
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("AMFEncoder: Shutting down (Total frames encoded: %d)..."), FrameCounter);

	// CRITICAL: Flush encoder to process all pending frames before shutdown
	// This ensures all OnFrameEncoded callbacks fire and frames are properly released
	// Without flushing, ActiveFrames will still contain unreleased frames when
	// EncoderInput is destroyed, causing assertion failures
	if (VideoEncoder.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("AMFEncoder: Flushing pending frames..."));

		// Call VideoEncoder->Shutdown() which internally flushes the encoder
		// This will block until all pending frames are encoded and callbacks are fired
		VideoEncoder->Shutdown();

		// Now safe to clear callbacks and reset encoder
		VideoEncoder->ClearOnEncodedPacket();
		VideoEncoder.Reset();

		UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("AMFEncoder: Encoder shut down and flushed"));
	}

	// Now safe to release encoder input (all frames should be released by callbacks)
	if (EncoderInput.IsValid())
	{
		// Flush any remaining frames in the input pool
		EncoderInput->Flush();
		EncoderInput.Reset();
	}

	bIsInitialized = false;

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("AMFEncoder: Shut down successfully"));
}

bool FRealGazeboAMFEncoder::EncodeTextureFrame(FTexture2DRHIRef SourceTexture, TSharedPtr<FEncodedFrameData> OutEncodedFrame, double Timestamp)
{
	if (!bIsInitialized || !VideoEncoder.IsValid() || !SourceTexture.IsValid() || !OutEncodedFrame.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("AMFEncoder: Invalid state for encoding"));
		return false;
	}

	// Obtain input frame from encoder
	TSharedPtr<AVEncoder::FVideoEncoderInputFrame> InputFrame = EncoderInput->ObtainInputFrame();
	if (!InputFrame.IsValid())
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("AMFEncoder: Failed to obtain input frame"));
		return false;
	}

	// Set frame dimensions
	InputFrame->SetWidth(SourceTexture->GetSizeX());
	InputFrame->SetHeight(SourceTexture->GetSizeY());

	// Set texture based on RHI type (AMD uses direct texture passthrough)
	const ERHIInterfaceType RHIType = RHIGetInterfaceType();

#if PLATFORM_DESKTOP && !PLATFORM_APPLE
	if (RHIType == ERHIInterfaceType::Vulkan)
	{
		const VkImage VulkanImage = GetIVulkanDynamicRHI()->RHIGetVkImage(SourceTexture.GetReference());
		InputFrame->SetTexture(VulkanImage, [](VkImage NativeTexture) { /* Released by RHI */ });
	}
#endif
#if PLATFORM_WINDOWS
	if (RHIType == ERHIInterfaceType::D3D11)
	{
		ID3D11Texture2D* D3D11Texture = static_cast<ID3D11Texture2D*>(SourceTexture->GetNativeResource());
		InputFrame->SetTexture(D3D11Texture, [](ID3D11Texture2D* NativeTexture) { /* Released by RHI */ });
	}
	else if (RHIType == ERHIInterfaceType::D3D12)
	{
		ID3D12Resource* D3D12Texture = static_cast<ID3D12Resource*>(SourceTexture->GetNativeResource());
		InputFrame->SetTexture(D3D12Texture, [](ID3D12Resource* NativeTexture) { /* Released by RHI */ });
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
			UE_LOG(LogRealGazeboStreaming, Warning, TEXT("AMFEncoder: No encoded data received for frame %d"), FrameCounter);
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

void FRealGazeboAMFEncoder::RequestKeyFrame()
{
	bRequestKeyFrame.store(true);
	UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("AMFEncoder: Keyframe requested"));
}

void FRealGazeboAMFEncoder::UpdateBitrate(int32 NewBitrateKbps)
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

	UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("AMFEncoder: Bitrate updated to %d kbps"), NewBitrateKbps);
}

bool FRealGazeboAMFEncoder::GetSPS(TArray<uint8>& OutSPS)
{
	FScopeLock Lock(&EncodedDataMutex);
	if (bHasSPSPPS && CachedSPS.Num() > 0)
	{
		OutSPS = CachedSPS;
		return true;
	}
	return false;
}

bool FRealGazeboAMFEncoder::GetPPS(TArray<uint8>& OutPPS)
{
	FScopeLock Lock(&EncodedDataMutex);
	if (bHasSPSPPS && CachedPPS.Num() > 0)
	{
		OutPPS = CachedPPS;
		return true;
	}
	return false;
}

bool FRealGazeboAMFEncoder::IsAvailable()
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

bool FRealGazeboAMFEncoder::IsRHIDeviceAMD() const
{
	// Check GPU adapter name (not RHI type!)
	const FString AdapterName = GRHIAdapterName;
	const FString AdapterNameLower = AdapterName.ToLower();

	return AdapterNameLower.Contains(TEXT("amd")) ||
	       AdapterNameLower.Contains(TEXT("radeon")) ||
	       AdapterNameLower.Contains(TEXT("ati"));
}

bool FRealGazeboAMFEncoder::IsRHIDeviceNVIDIA() const
{
	// Check GPU adapter name (not RHI type!)
	const FString AdapterName = GRHIAdapterName;
	const FString AdapterNameLower = AdapterName.ToLower();

	return AdapterNameLower.Contains(TEXT("nvidia")) ||
	       AdapterNameLower.Contains(TEXT("geforce")) ||
	       AdapterNameLower.Contains(TEXT("quadro")) ||
	       AdapterNameLower.Contains(TEXT("tesla"));
}

AVEncoder::FVideoEncoder::H264Profile FRealGazeboAMFEncoder::GetH264Profile() const
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

void FRealGazeboAMFEncoder::OnEncodedPacket(uint32 LayerIndex, const TSharedPtr<AVEncoder::FVideoEncoderInputFrame> Frame,
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
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("AMFEncoder: Received empty packet"));
	}
}

void FRealGazeboAMFEncoder::ParseAndCacheSPSPPS(const uint8* Data, int32 Size)
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
				UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("AMFEncoder: Extracted SPS (%d bytes)"), NALSize);
			}
			else if (NALType == 8) // PPS
			{
				CachedPPS.SetNum(NALSize);
				FMemory::Memcpy(CachedPPS.GetData(), &Data[Offset], NALSize);
				UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("AMFEncoder: Extracted PPS (%d bytes)"), NALSize);
			}

			if (CachedSPS.Num() > 0 && CachedPPS.Num() > 0)
			{
				bHasSPSPPS = true;
				UE_LOG(LogRealGazeboStreaming, Log, TEXT("AMFEncoder: SPS/PPS cached successfully"));
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
