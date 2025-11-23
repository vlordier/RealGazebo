// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Pipeline/StreamingPipeline.h"
#include "RTSP/RTSPServer.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/PlatformTime.h"

//----------------------------------------------------------
// Construction & Initialization
//----------------------------------------------------------

FStreamingPipeline::FStreamingPipeline(const FStreamIdentifier& InStreamID,
                                       USceneCaptureComponent2D* InSceneCapture,
                                       TSharedPtr<FRTSPServerWrapper> InRTSPServer)
	: StreamID(InStreamID)
	, SceneCapture(InSceneCapture)
	, RTSPServer(InRTSPServer)
{
	UE_LOG(LogTemp, Log, TEXT("StreamingPipeline: Created for stream %s"), *StreamID.ToString());
}

FStreamingPipeline::~FStreamingPipeline()
{
	Shutdown();
}

bool FStreamingPipeline::Initialize(const FStreamConfig& InConfig, FString& OutErrorMessage)
{
	if (bInitialized)
	{
		OutErrorMessage = TEXT("Pipeline already initialized");
		return true;
	}

	UE_LOG(LogTemp, Log, TEXT("StreamingPipeline: Initializing stream %s..."), *StreamID.ToString());

	Config = InConfig;

	// Convert to encoder config
	EncoderConfig = FEncoderConfig(Config);

	// Validate encoder config
	if (!EncoderConfig.Validate(OutErrorMessage))
	{
		UE_LOG(LogTemp, Error, TEXT("StreamingPipeline: Invalid config - %s"), *OutErrorMessage);
		return false;
	}

	// Initialize all components
	if (!InitializeComponents(OutErrorMessage))
	{
		UE_LOG(LogTemp, Error, TEXT("StreamingPipeline: Component initialization failed - %s"), *OutErrorMessage);
		CleanupComponents();
		return false;
	}

	bInitialized = true;
	UE_LOG(LogTemp, Log, TEXT("StreamingPipeline: Initialized successfully for stream %s"), *StreamID.ToString());

	return true;
}

void FStreamingPipeline::Shutdown()
{
	if (!bInitialized)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("StreamingPipeline: Shutting down stream %s..."), *StreamID.ToString());

	Stop();
	CleanupComponents();

	bInitialized = false;

	UE_LOG(LogTemp, Log, TEXT("StreamingPipeline: Shutdown complete for stream %s"), *StreamID.ToString());
}

//----------------------------------------------------------
// Internal Initialization
//----------------------------------------------------------

bool FStreamingPipeline::InitializeComponents(FString& OutErrorMessage)
{
	const int32 Width = EncoderConfig.Width;
	const int32 Height = EncoderConfig.Height;
	const int32 FPS = EncoderConfig.FrameRate;

	// Create frame pool (per-stream, FPS-aware sizing)
	// Pool size auto-calculated to maintain ~130ms buffer time
	const int32 PoolSize = Config.GetFramePoolSize();

	UE_LOG(LogTemp, Log, TEXT("StreamingPipeline: Creating frame pool %dx%d, %d frames (FPS=%d)..."),
		Width, Height, PoolSize, FPS);
	FramePool = MakeShared<FFramePool>(Width, Height, PoolSize);
	if (!FramePool->Initialize())
	{
		OutErrorMessage = TEXT("Failed to initialize frame pool");
		return false;
	}

	// Create frame capture (per-stream, with isolated GPU fence)
	UE_LOG(LogTemp, Log, TEXT("StreamingPipeline: Creating frame capture..."));
	FrameCapture = MakeUnique<FFrameCapture>(Width, Height, FramePool);
	if (!FrameCapture->Initialize())
	{
		OutErrorMessage = TEXT("Failed to initialize frame capture");
		return false;
	}

	// Create hardware encoder (per-stream instance)
	UE_LOG(LogTemp, Log, TEXT("StreamingPipeline: Creating hardware encoder..."));
	HardwareEncoder = MakeShared<FHardwareEncoderWrapper>();
	if (!HardwareEncoder->Initialize(EncoderConfig, OutErrorMessage))
	{
		return false;
	}

	// Create encoding thread (per-stream, FPS-aware queue size)
	// CRITICAL: Pass FramePool so encoding thread can release frames after encoding
	// Without this, frame pool will exhaust and streaming will stop!
	const int32 MaxQueueSize = Config.GetFrameQueueSize();
	UE_LOG(LogTemp, Log, TEXT("StreamingPipeline: Creating encoding thread (MaxQueueSize=%d)..."), MaxQueueSize);
	EncodingThread = MakeUnique<FEncodingThread>(StreamID, HardwareEncoder, FramePool, MaxQueueSize);

	// Register NAL callback
	EncodingThread->SetNALEncodedCallback(
		FEncodingThread::FOnNALUnitsEncoded::CreateRaw(this, &FStreamingPipeline::OnNALUnitsEncoded)
	);

	// Validate SceneCapture weak pointer
	if (!SceneCapture.IsValid())
	{
		OutErrorMessage = TEXT("Scene capture component is not valid");
		return false;
	}

	// AUTO-CREATE or VALIDATE TextureTarget for SceneCapture
	// This is CRITICAL - without a properly sized RenderTarget, streaming won't work
	USceneCaptureComponent2D* SceneCapturePtr = SceneCapture.Get();
	if (!SceneCapturePtr->TextureTarget)
	{
		// Create new RenderTarget2D with correct dimensions from FStreamConfig
		UE_LOG(LogTemp, Log, TEXT("StreamingPipeline: Creating TextureTarget %dx%d for stream %s"),
			Width, Height, *StreamID.ToString());

		UTextureRenderTarget2D* NewRenderTarget = NewObject<UTextureRenderTarget2D>(
			SceneCapturePtr->GetOwner(),
			MakeUniqueObjectName(SceneCapturePtr->GetOwner(), UTextureRenderTarget2D::StaticClass(),
				FName(*FString::Printf(TEXT("RT_%s"), *StreamID.ToString())))
		);

		if (!NewRenderTarget)
		{
			OutErrorMessage = TEXT("Failed to create TextureRenderTarget2D");
			return false;
		}

		// Initialize with dimensions from FStreamConfig
		// Force exact BGRA8 to match the frame pool (PF_B8G8R8A8) and avoid channel swizzle
		// Use InitCustomFormat with PF_B8G8R8A8 for explicit 8-bit BGRA format
		NewRenderTarget->RenderTargetFormat = RTF_RGBA8;
		NewRenderTarget->InitCustomFormat(Width, Height, PF_B8G8R8A8, false);
		NewRenderTarget->bAutoGenerateMips = false;
		NewRenderTarget->ClearColor = FLinearColor::Black;
		NewRenderTarget->bGPUSharedFlag = true;  // Enable external memory sharing for CUDA
		NewRenderTarget->UpdateResourceImmediate(true);

		// Assign to SceneCapture
		SceneCapturePtr->TextureTarget = NewRenderTarget;

		UE_LOG(LogTemp, Log, TEXT("StreamingPipeline: TextureTarget created and assigned for stream %s"),
			*StreamID.ToString());
	}
	else
	{
		// Validate existing TextureTarget has correct dimensions and format
		const int32 TargetWidth = SceneCapturePtr->TextureTarget->SizeX;
		const int32 TargetHeight = SceneCapturePtr->TextureTarget->SizeY;
		// Check if format matches expected BGRA8 (RTF_RGBA8 maps to PF_B8G8R8A8)
		const bool bFormatMismatch =
			SceneCapturePtr->TextureTarget->RenderTargetFormat != RTF_RGBA8 ||
			SceneCapturePtr->TextureTarget->OverrideFormat != PF_B8G8R8A8;

		if (TargetWidth != Width || TargetHeight != Height || bFormatMismatch)
		{
			UE_LOG(LogTemp, Warning, TEXT("StreamingPipeline: Reinitializing TextureTarget %dx%d -> %dx%d (Format fix=%s) for stream %s"),
				TargetWidth, TargetHeight, Width, Height,
				bFormatMismatch ? TEXT("Yes") : TEXT("No"),
				*StreamID.ToString());

			// Reinitialize existing render target to guaranteed BGRA8
			SceneCapturePtr->TextureTarget->RenderTargetFormat = RTF_RGBA8;
			SceneCapturePtr->TextureTarget->InitCustomFormat(Width, Height, PF_B8G8R8A8, false);
			SceneCapturePtr->TextureTarget->bAutoGenerateMips = false;
			SceneCapturePtr->TextureTarget->bGPUSharedFlag = true;
			SceneCapturePtr->TextureTarget->UpdateResourceImmediate(true);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("StreamingPipeline: TextureTarget already correct size %dx%d for stream %s"),
				Width, Height, *StreamID.ToString());
		}
	}

	// Configure SceneCapture for manual capture (not every frame)
	SceneCapturePtr->bCaptureEveryFrame = false;
	SceneCapturePtr->bCaptureOnMovement = false;

	// CRITICAL: Set CaptureSource to FinalColorLDR for proper H.264 encoding
	// - SCS_FinalColorLDR: Post-processed, tone-mapped, 8-bit color (PERFECT for H.264)
	// - SCS_SceneColorHDR (default): Raw HDR values, can exceed 0-1 range, causes artifacts
	SceneCapturePtr->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

	UE_LOG(LogTemp, Log, TEXT("StreamingPipeline: SceneCapture configured - CaptureSource=FinalColorLDR, Format=RGBA8"));

	// Create H.264 stream source (per-instance NAL queue)
	if (!RTSPServer || !RTSPServer->IsRunning())
	{
		OutErrorMessage = TEXT("RTSP server not running");
		return false;
	}

	// Get Live555 usage environment from RTSP server
	UsageEnvironment* Env = RTSPServer->GetEnvironment();
	if (!Env)
	{
		OutErrorMessage = TEXT("RTSP server environment not available");
		return false;
	}

	// Create H264 stream source with per-instance NAL queue (FPS and resolution aware)
	const int32 MaxNALQueueSize = Config.GetNALQueueSize();
	const int32 MaxNALSizeBytes = Config.GetMaxNALSizeBytes();
	UE_LOG(LogTemp, Log, TEXT("StreamingPipeline: Creating H264 stream source (NALQueue=%d, MaxNALSize=%d KB)..."),
		MaxNALQueueSize, MaxNALSizeBytes / 1024);
	H264Source = MakeUnique<FH264StreamSource>(StreamID, *Env, MaxNALQueueSize, MaxNALSizeBytes);

	UE_LOG(LogTemp, Log, TEXT("StreamingPipeline: All components initialized for stream %s"), *StreamID.ToString());
	return true;
}

void FStreamingPipeline::CleanupComponents()
{
	// Cleanup in reverse order
	H264Source.Reset();
	EncodingThread.Reset();
	HardwareEncoder.Reset();
	FrameCapture.Reset();
	FramePool.Reset();

	UE_LOG(LogTemp, Log, TEXT("StreamingPipeline: Components cleaned up for stream %s"), *StreamID.ToString());
}

//----------------------------------------------------------
// Stream Control
//----------------------------------------------------------

bool FStreamingPipeline::Start(FString& OutRTSPURL)
{
	if (!bInitialized)
	{
		UE_LOG(LogTemp, Error, TEXT("StreamingPipeline: Cannot start - not initialized"));
		return false;
	}

	if (bStreaming)
	{
		UE_LOG(LogTemp, Warning, TEXT("StreamingPipeline: Already streaming"));
		OutRTSPURL = RTSPURL;
		return true;
	}

	UE_LOG(LogTemp, Log, TEXT("StreamingPipeline: Starting stream %s..."), *StreamID.ToString());

	// Start encoding thread
	if (!EncodingThread->Start())
	{
		UE_LOG(LogTemp, Error, TEXT("StreamingPipeline: Failed to start encoding thread"));
		return false;
	}

	// Add stream to RTSP server
	if (!RTSPServer->AddStream(StreamID, H264Source.Get(), OutRTSPURL))
	{
		UE_LOG(LogTemp, Error, TEXT("StreamingPipeline: Failed to add stream to RTSP server"));
		EncodingThread->Stop();
		return false;
	}

	RTSPURL = OutRTSPURL;
	bStreaming = true;
	StartTime = FDateTime::Now();

	UE_LOG(LogTemp, Log, TEXT("StreamingPipeline: Stream started - %s"), *RTSPURL);
	return true;
}

void FStreamingPipeline::Stop()
{
	if (!bStreaming)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("StreamingPipeline: Stopping stream %s..."), *StreamID.ToString());

	// Stop encoding thread
	if (EncodingThread)
	{
		EncodingThread->Stop();
	}

	// Remove from RTSP server
	if (RTSPServer)
	{
		RTSPServer->RemoveStream(StreamID);
	}

	bStreaming = false;

	UE_LOG(LogTemp, Log, TEXT("StreamingPipeline: Stream stopped for %s"), *StreamID.ToString());
}

//----------------------------------------------------------
// Frame Capture
//----------------------------------------------------------

bool FStreamingPipeline::CaptureFrame()
{
	if (!bStreaming || !FrameCapture || !EncodingThread)
	{
		return false;
	}

	// FPS LIMITING: Only capture at configured frame rate
	// This prevents unnecessary GPU load from capturing at game tick rate (120 FPS)
	const double CurrentTime = FPlatformTime::Seconds();
	const double TargetFrameInterval = 1.0 / Config.GetFrameRateValue();
	const double TimeSinceLastCapture = CurrentTime - LastCaptureTime;

	if (TimeSinceLastCapture < TargetFrameInterval)
	{
		// Too soon - skip this frame
		return false;
	}

	// Validate scene capture
	if (!SceneCapture.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("StreamingPipeline: Scene capture no longer valid for stream %s"),
			*StreamID.ToString());
		CaptureFailureCount++;
		return false;
	}

	USceneCaptureComponent2D* SceneCapturePtr = SceneCapture.Get();

	// Validate TextureTarget exists
	if (!SceneCapturePtr->TextureTarget)
	{
		UE_LOG(LogTemp, Warning, TEXT("StreamingPipeline: TextureTarget is null for stream %s"),
			*StreamID.ToString());
		CaptureFailureCount++;
		return false;
	}

	// TRIGGER SCENE CAPTURE - This actually renders the scene to TextureTarget
	// Without this call, TextureTarget would be empty/stale!
	SceneCapturePtr->CaptureScene();

	// Capture frame from TextureTarget to pooled GPU texture
	FCaptureFrame CapturedFrame;
	if (!FrameCapture->CaptureFrame(SceneCapturePtr, CapturedFrame))
	{
		CaptureFailureCount++;
		return false;
	}

	// Queue frame for encoding
	if (!EncodingThread->QueueFrame(CapturedFrame))
	{
		UE_LOG(LogTemp, Warning, TEXT("StreamingPipeline: Failed to queue frame for encoding"));
		// CRITICAL FIX: Release frame back to pool when queue is full
		// Without this, pool will exhaust when encoding is slower than capture!
		if (FramePool && CapturedFrame.PooledFrame)
		{
			FramePool->ReleaseFrame(CapturedFrame.PooledFrame);
		}
		return false;
	}

	TotalFramesCaptured++;
	LastCaptureTime = CurrentTime;
	FrameCounter++;

	return true;
}

//----------------------------------------------------------
// Callbacks
//----------------------------------------------------------

void FStreamingPipeline::OnNALUnitsEncoded(const TArray<FEncodedNALUnit>& NALUnits)
{
	if (!H264Source)
	{
		return;
	}

	// Forward encoded NAL units to H.264 stream source
	// H264Source has its own per-instance NAL queue - no crosstalk!
	H264Source->PushNALUnits(NALUnits);
}

//----------------------------------------------------------
// Configuration
//----------------------------------------------------------

bool FStreamingPipeline::UpdateConfiguration(const FStreamConfig& NewConfig, FString& OutErrorMessage)
{
	UE_LOG(LogTemp, Log, TEXT("StreamingPipeline: Updating configuration for stream %s..."), *StreamID.ToString());

	// Updating config requires restart
	const bool bWasStreaming = bStreaming;
	FString OldRTSPURL = RTSPURL;

	if (bWasStreaming)
	{
		Stop();
	}

	// Reinitialize with new config
	Shutdown();

	if (!Initialize(NewConfig, OutErrorMessage))
	{
		return false;
	}

	// Restart if was streaming
	if (bWasStreaming)
	{
		FString NewRTSPURL;
		if (!Start(NewRTSPURL))
		{
			OutErrorMessage = TEXT("Failed to restart stream after config update");
			return false;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("StreamingPipeline: Configuration updated for stream %s"), *StreamID.ToString());
	return true;
}

//----------------------------------------------------------
// Status
//----------------------------------------------------------

EEncoderType FStreamingPipeline::GetEncoderType() const
{
	return HardwareEncoder ? HardwareEncoder->GetEncoderType() : EEncoderType::Unknown;
}

FStreamInfo FStreamingPipeline::GetStreamInfo() const
{
	FStreamInfo Info;
	Info.StreamID = StreamID;
	Info.RTSPURL = RTSPURL;
	Info.State = bStreaming ? EStreamState::Active : (bInitialized ? EStreamState::Idle : EStreamState::Error);
	Info.Config = Config;
	Info.EncoderType = GetEncoderType();
	// Statistics not yet implemented:
	// - ConnectedClients: Requires tracking RTSP session count in RTSPServer (Live555 ClientSession tracking)
	// - ActualFPS: Requires calculating delta between FrameCounter over time window (rolling average)
	// - AvgEncodingTimeMs: Requires timing encoder EncodeFrame() call and averaging over window
	Info.ConnectedClients = 0;
	Info.TotalFramesEncoded = TotalFramesCaptured;
	Info.ActualFPS = 0.0f;
	Info.AvgEncodingTimeMs = 0.0f;
	Info.CreatedTime = StartTime;

	return Info;
}

FString FStreamingPipeline::GetStatsString() const
{
	return FString::Printf(
		TEXT("Pipeline [%s]: Initialized=%s, Streaming=%s, Captured=%llu, Failures=%llu, Encoder=%s"),
		*StreamID.ToString(),
		bInitialized.load() ? TEXT("Yes") : TEXT("No"),
		bStreaming.load() ? TEXT("Yes") : TEXT("No"),
		TotalFramesCaptured.load(),
		CaptureFailureCount.load(),
		*EncoderTypeToString(GetEncoderType())
	);
}
