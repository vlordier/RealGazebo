// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Encoding/RealGazeboAMFEncoder.h"
#include "Core/RealGazeboStreamingTypes.h"
#include "VideoEncoderFactory.h"
#include "RHI.h"

#if PLATFORM_DESKTOP && !PLATFORM_APPLE
#include "IVulkanDynamicRHI.h"
#endif

#if PLATFORM_WINDOWS
#include "ID3D11DynamicRHI.h"  // D3D11 only - D3D12 support removed for stability
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

	// ========================================
	// ULTRA-LOW LATENCY VALIDATION
	// RealGazeboStreaming is locked to ultra-low latency RTSP/RTP streaming
	//
	// AMF Configuration (enforced by UE5.1 AVEncoder):
	// - Quality preset: Speed (low latency)
	// - B-frames: 0 (disabled by UE5 encoder preset)
	// - Profile: Baseline (forced below)
	// ========================================

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("AMFEncoder: Ultra-low latency mode enabled (B-frames=0, Baseline profile, GOP=30-60)"));

	// Validate resolution constraints
	// AMF supports max 4096x4096 (similar to NVENC hardware limits)
	if (Config.Dimensions.X > 4096 || Config.Dimensions.Y > 4096)
	{
		UE_LOG(LogRealGazeboStreaming, Error,
			TEXT("AMFEncoder: Resolution %dx%d exceeds AMF maximum of 4096x4096"),
			Config.Dimensions.X, Config.Dimensions.Y);
		return false;
	}

	// Check minimum resolution (AMF typically requires at least 32x32)
	if (Config.Dimensions.X < 32 || Config.Dimensions.Y < 32)
	{
		UE_LOG(LogRealGazeboStreaming, Error,
			TEXT("AMFEncoder: Resolution %dx%d below AMF minimum of 32x32"),
			Config.Dimensions.X, Config.Dimensions.Y);
		return false;
	}

	// Warn if resolution is not 16-pixel aligned (hardware encoders prefer aligned dimensions)
	if (Config.Dimensions.X % 16 != 0 || Config.Dimensions.Y % 16 != 0)
	{
		UE_LOG(LogRealGazeboStreaming, Warning,
			TEXT("AMFEncoder: Resolution %dx%d not 16-pixel aligned, may cause encoding inefficiency"),
			Config.Dimensions.X, Config.Dimensions.Y);
	}

	// Validate bitrate for ultra-low latency (2-8 Mbps recommended for RTSP/RTP)
	if (Config.BitrateKbps < 2000 || Config.BitrateKbps > 8000)
	{
		UE_LOG(LogRealGazeboStreaming, Error,
			TEXT("AMFEncoder: Bitrate %d kbps outside ultra-low latency range (2000-8000 kbps)"),
			Config.BitrateKbps);
		UE_LOG(LogRealGazeboStreaming, Error,
			TEXT("AMFEncoder: RealGazeboStreaming requires 2-8 Mbps for reliable RTSP/RTP transmission"));
		return false;
	}

	// Validate GOP size for ultra-low latency (30-60 frames, 1.0s @ 30/60 FPS)
	if (Config.GOPSize < 30 || Config.GOPSize > 60)
	{
		UE_LOG(LogRealGazeboStreaming, Error,
			TEXT("AMFEncoder: GOP size %d outside valid range (30-60)"),
			Config.GOPSize);
		UE_LOG(LogRealGazeboStreaming, Error,
			TEXT("AMFEncoder: Valid GOP: 30 (1.0s @ 30fps) or 60 (1.0s @ 60fps)"));
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
	// D3D12 support removed - using D3D11 only
	else if (RHIType == ERHIInterfaceType::D3D11)
	{
		EncoderInput = AVEncoder::FVideoEncoderInput::CreateForD3D11(GDynamicRHI->RHIGetNativeDevice(), false, true);
		EncoderInputType = EEncoderInputType::D3D11;
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("AMFEncoder: Using D3D11 direct path"));
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

	// CRITICAL: Force Baseline profile for maximum RTSP/RTP compatibility
	// Baseline profile (H.264 Level 4.2) is widely supported by:
	// - All RTSP/RTP clients (VLC, FFmpeg, GStreamer)
	// - Web browsers (WebRTC, MSE)
	// - Mobile devices (iOS, Android)
	// - Hardware decoders (embedded systems, robotics platforms)
	// Profile is always Baseline (only option in EH264Profile enum)
	LayerConfig.H264Profile = AVEncoder::FVideoEncoder::H264Profile::BASELINE;

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("AMFEncoder: Using Baseline profile for maximum RTSP/RTP compatibility"));

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
		// Early exit if not initialized - prevents shutdown on partially constructed encoders
		// This is critical during Editor property changes which trigger component reconstruction
		return;
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("AMFEncoder: Shutting down (Total frames encoded: %d)..."), FrameCounter);

	// Mark as not initialized immediately to prevent re-entrant shutdown calls
	bIsInitialized = false;

	// CRITICAL: Flush encoder to process all pending frames before shutdown
	// This ensures all OnFrameEncoded callbacks fire and frames are properly released
	// Without flushing, ActiveFrames will still contain unreleased frames when
	// EncoderInput is destroyed, causing assertion failures
	//
	// ALWAYS call Shutdown() if encoder is valid, even if FrameCounter==0
	// Skipping Shutdown() leaves ActiveFrames from ObtainInputFrame calls unreleased
	if (VideoEncoder.IsValid())
	{
		// Always call Shutdown() to flush pending frames and release ActiveFrames
		// This triggers OnFrameEncoded callbacks to properly release frames
		UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("AMFEncoder: Flushing pending frames..."));

		VideoEncoder->Shutdown();

		UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("AMFEncoder: Encoder shut down and flushed successfully"));

		// Clear callbacks and reset encoder
		VideoEncoder->ClearOnEncodedPacket();
		VideoEncoder.Reset();
	}

	// Now safe to release encoder input (all frames should be released by callbacks)
	if (EncoderInput.IsValid())
	{
		// Flush any remaining frames in the input pool
		EncoderInput->Flush();
		EncoderInput.Reset();
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("AMFEncoder: Shut down successfully"));
}

bool FRealGazeboAMFEncoder::EncodeTextureFrame(FTexture2DRHIRef SourceTexture, TSharedPtr<FEncodedFrameData> OutEncodedFrame, int64 TimestampUs)
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
	// D3D12 support removed - using D3D11 only
	if (RHIType == ERHIInterfaceType::D3D11)
	{
		ID3D11Texture2D* D3D11Texture = static_cast<ID3D11Texture2D*>(SourceTexture->GetNativeResource());
		InputFrame->SetTexture(D3D11Texture, [](ID3D11Texture2D* NativeTexture) { /* Released by RHI */ });
	}
	else
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("AMFEncoder: Unsupported RHI type on Windows. Only D3D11 is supported."));
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
	// CRITICAL FIX (Bug #AMF-REGISTRY-WIPE):
	// Same issue as NVENC - AVEncoder factory clears registry on module shutdown.
	// EncoderAMF only registers once via FCoreDelegates::OnPostEngineInit which never re-fires.
	// Apply the same fix: reload module if registry is empty but AMD GPU is present.

	AVEncoder::FVideoEncoderFactory& EncoderFactory = AVEncoder::FVideoEncoderFactory::Get();

	// First check: Is factory set up?
	if (!EncoderFactory.IsSetup())
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("AMFEncoder::IsAvailable: AVEncoder factory not set up"));
		return false;
	}

	// Second check: Query available encoders
	const TArray<AVEncoder::FVideoEncoderInfo>& AvailableEncoders = EncoderFactory.GetAvailable();

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("AMFEncoder::IsAvailable: Factory has %d registered encoder(s)"),
		AvailableEncoders.Num());

	// CRITICAL FIX: If registry is empty but we have AMD GPU, force re-register
	if (AvailableEncoders.Num() == 0)
	{
		// Check if we have AMD GPU hardware
		const FString AdapterName = GRHIAdapterName;
		const FString AdapterNameLower = AdapterName.ToLower();

		bool bIsAMD = AdapterNameLower.Contains(TEXT("amd")) ||
		              AdapterNameLower.Contains(TEXT("radeon")) ||
		              AdapterNameLower.Contains(TEXT("ati"));

		if (bIsAMD)
		{
			UE_LOG(LogRealGazeboStreaming, Warning,
				TEXT("AMFEncoder::IsAvailable: Empty encoder registry detected, but AMD GPU present (%s)"),
				*AdapterName);
			UE_LOG(LogRealGazeboStreaming, Warning,
				TEXT("AMFEncoder::IsAvailable: Attempting to reload EncoderAMF module to force re-registration..."));

			// Force reload the EncoderAMF module to trigger AMF registration
			FModuleManager& ModuleManager = FModuleManager::Get();

			// Unload EncoderAMF if it's loaded (to reset state)
			if (ModuleManager.IsModuleLoaded("EncoderAMF"))
			{
				UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("AMFEncoder::IsAvailable: Unloading EncoderAMF module..."));
				ModuleManager.UnloadModule("EncoderAMF");
			}

			// Reload EncoderAMF (triggers StartupModule -> re-registration)
			UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("AMFEncoder::IsAvailable: Loading EncoderAMF module..."));
			if (ModuleManager.LoadModule("EncoderAMF"))
			{
				UE_LOG(LogRealGazeboStreaming, Log, TEXT("AMFEncoder::IsAvailable: EncoderAMF module reloaded successfully"));

				// Re-query encoder registry after reload
				const TArray<AVEncoder::FVideoEncoderInfo>& UpdatedEncoders = EncoderFactory.GetAvailable();
				UE_LOG(LogRealGazeboStreaming, Log, TEXT("AMFEncoder::IsAvailable: Factory now has %d registered encoder(s)"),
					UpdatedEncoders.Num());
			}
			else
			{
				UE_LOG(LogRealGazeboStreaming, Error,
					TEXT("AMFEncoder::IsAvailable: Failed to reload EncoderAMF module"));
				return false;
			}
		}
		else
		{
			UE_LOG(LogRealGazeboStreaming, Warning,
				TEXT("AMFEncoder::IsAvailable: Empty encoder registry and non-AMD GPU (%s) - AMF not available"),
				*AdapterName);
			return false;
		}
	}

	// Log all available encoders for debugging (helps diagnose cross-platform issues)
	const TArray<AVEncoder::FVideoEncoderInfo>& FinalEncoders = EncoderFactory.GetAvailable();
	for (const AVEncoder::FVideoEncoderInfo& Info : FinalEncoders)
	{
		UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("  - Encoder ID:%u, Type:%d, MaxRes:%ux%u"),
			Info.ID, (int32)Info.CodecType, Info.MaxWidth, Info.MaxHeight);
	}

	// Third check: Does factory have H.264 encoder?
	bool bH264Available = EncoderFactory.HasEncoderForCodec(AVEncoder::ECodecType::H264);

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("AMFEncoder::IsAvailable: H.264 encoder = %s"),
		bH264Available ? TEXT("AVAILABLE") : TEXT("NOT AVAILABLE"));

	// Fourth check: Is this actually an AMD GPU? (prevents false positives on NVIDIA systems)
	if (bH264Available)
	{
		// Check GPU vendor to ensure AMF is appropriate
		const FString AdapterName = GRHIAdapterName;
		const FString AdapterNameLower = AdapterName.ToLower();

		bool bIsAMD = AdapterNameLower.Contains(TEXT("amd")) ||
		              AdapterNameLower.Contains(TEXT("radeon")) ||
		              AdapterNameLower.Contains(TEXT("ati"));

		if (!bIsAMD)
		{
			UE_LOG(LogRealGazeboStreaming, Warning,
				TEXT("AMFEncoder::IsAvailable: H.264 encoder available but GPU is not AMD (%s) - AMF may not work"),
				*AdapterName);
			// Still return true - let Initialize() fail gracefully if AMF doesn't work
		}
		else
		{
			UE_LOG(LogRealGazeboStreaming, Log, TEXT("AMFEncoder::IsAvailable: AMD GPU detected (%s) - AMF should work"),
				*AdapterName);
		}
	}

	return bH264Available;
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

AVEncoder::FVideoEncoder::H264Profile FRealGazeboAMFEncoder::GetH264Profile() const
{
	// Always return Baseline - only supported profile for ultra-low latency RTSP/RTP streaming
	return AVEncoder::FVideoEncoder::H264Profile::BASELINE;
}

void FRealGazeboAMFEncoder::OnEncodedPacket(uint32 LayerIndex, const TSharedPtr<AVEncoder::FVideoEncoderInputFrame> Frame,
                                              const AVEncoder::FCodecPacket& Packet)
{
	FScopeLock Lock(&EncodedDataMutex);

	// CRITICAL FIX (Bug #8): Validate packet data pointer before memcpy
	// AVEncoder can provide valid TSharedPtr with null Data.Get() during encoder errors
	if (Packet.Data.IsValid() && Packet.DataSize > 0 && Packet.Data.Get() != nullptr)
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
		UE_LOG(LogRealGazeboStreaming, Warning,
			TEXT("AMFEncoder: Received invalid packet (DataValid: %s, DataSize: %d, DataPtr: %s)"),
			Packet.Data.IsValid() ? TEXT("YES") : TEXT("NO"),
			Packet.DataSize,
			(Packet.Data.IsValid() && Packet.Data.Get() != nullptr) ? TEXT("VALID") : TEXT("NULL"));
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
