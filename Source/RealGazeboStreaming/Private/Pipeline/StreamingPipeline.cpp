// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Licensed under the GNU General Public License v3.0.

#include "Pipeline/StreamingPipeline.h"
#include "RTSP/RTSPServer.h"
#include "Transport/RTSPEncodedVideoSink.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"

FStreamingPipeline::FStreamingPipeline(
	const FStreamIdentifier& InStreamID,
	USceneCaptureComponent2D* InSceneCapture,
	TSharedPtr<FRTSPServerWrapper> InRTSPServer)
	: StreamID(InStreamID)
	, SceneCapture(InSceneCapture)
	, RTSPServer(InRTSPServer)
{
	EncodedVideoFanout = MakeUnique<FEncodedVideoFanout>();
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
		return true;
	}

	Config = InConfig;
	EncoderConfig = FEncoderConfig(Config);
	if (!EncoderConfig.Validate(OutErrorMessage))
	{
		return false;
	}
	if (!InitializeComponents(OutErrorMessage))
	{
		CleanupComponents();
		return false;
	}
	bInitialized = true;
	return true;
}

void FStreamingPipeline::Shutdown()
{
	if (!bInitialized)
	{
		return;
	}
	bShuttingDown.store(true);
	if (HardwareEncoder)
	{
		HardwareEncoder->PrepareForShutdown();
	}
	if (EncodingThread)
	{
		EncodingThread->ClearNALEncodedCallback();
	}
	Stop();
	CleanupComponents();
	bInitialized = false;
	bShuttingDown = false;
}

bool FStreamingPipeline::InitializeComponents(FString& OutErrorMessage)
{
	const int32 Width = EncoderConfig.Width;
	const int32 Height = EncoderConfig.Height;
	const int32 PoolSize = Config.GetFramePoolSize();

	FramePool = MakeShared<FFramePool>(Width, Height, PoolSize);
	if (!FramePool->Initialize())
	{
		OutErrorMessage = TEXT("Failed to initialize frame pool");
		return false;
	}

	FrameCapture = MakeUnique<FFrameCapture>(Width, Height, FramePool);
	if (!FrameCapture->Initialize())
	{
		OutErrorMessage = TEXT("Failed to initialize frame capture");
		return false;
	}

	if (!SceneCapture.IsValid())
	{
		OutErrorMessage = TEXT("Scene capture component is not valid");
		return false;
	}

	USceneCaptureComponent2D* SceneCapturePtr = SceneCapture.Get();
	if (!SceneCapturePtr->TextureTarget)
	{
		UTextureRenderTarget2D* NewRenderTarget = NewObject<UTextureRenderTarget2D>(
			SceneCapturePtr->GetOwner(),
			MakeUniqueObjectName(
				SceneCapturePtr->GetOwner(),
				UTextureRenderTarget2D::StaticClass(),
				FName(*FString::Printf(TEXT("RT_%s"), *StreamID.ToString()))));
		if (!NewRenderTarget)
		{
			OutErrorMessage = TEXT("Failed to create TextureRenderTarget2D");
			return false;
		}
		NewRenderTarget->RenderTargetFormat = RTF_RGBA8;
		NewRenderTarget->InitCustomFormat(Width, Height, PF_B8G8R8A8, false);
		NewRenderTarget->bAutoGenerateMips = false;
		NewRenderTarget->ClearColor = FLinearColor::Black;
		NewRenderTarget->bGPUSharedFlag = true;
		NewRenderTarget->UpdateResourceImmediate(true);
		SceneCapturePtr->TextureTarget = NewRenderTarget;
	}
	else
	{
		const bool bNeedsReinit =
			SceneCapturePtr->TextureTarget->SizeX != Width ||
			SceneCapturePtr->TextureTarget->SizeY != Height ||
			SceneCapturePtr->TextureTarget->RenderTargetFormat != RTF_RGBA8 ||
			SceneCapturePtr->TextureTarget->OverrideFormat != PF_B8G8R8A8;
		if (bNeedsReinit)
		{
			SceneCapturePtr->TextureTarget->RenderTargetFormat = RTF_RGBA8;
			SceneCapturePtr->TextureTarget->InitCustomFormat(Width, Height, PF_B8G8R8A8, false);
			SceneCapturePtr->TextureTarget->bAutoGenerateMips = false;
			SceneCapturePtr->TextureTarget->bGPUSharedFlag = true;
			SceneCapturePtr->TextureTarget->UpdateResourceImmediate(true);
		}
	}

	SceneCapturePtr->bCaptureEveryFrame = false;
	SceneCapturePtr->bCaptureOnMovement = false;
	SceneCapturePtr->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

	// RTSP is optional. A STANAG recorder/gateway can run headlessly without Live555.
	if (RTSPServer && RTSPServer->IsRunning())
	{
		if (UsageEnvironment* Env = RTSPServer->GetEnvironment())
		{
			H264Source = MakeUnique<FH264StreamSource>(
				StreamID,
				*Env,
				Config.GetNALQueueSize(),
				Config.GetMaxNALSizeBytes());
			RTSPSink = MakeShared<FRTSPEncodedVideoSink>(H264Source.Get());
			EncodedVideoFanout->AddSink(RTSPSink.ToSharedRef());
		}
	}

	return true;
}

void FStreamingPipeline::CleanupComponents()
{
	if (EncodingThread)
	{
		EncodingThread->Shutdown();
		EncodingThread.Reset();
	}
	if (EncodedVideoFanout)
	{
		EncodedVideoFanout->StopAll();
	}
	RTSPSink.Reset();
	H264Source.Reset();
	HardwareEncoder.Reset();
	FrameCapture.Reset();
	FramePool.Reset();
}

bool FStreamingPipeline::Start(FString& OutRTSPURL)
{
	OutRTSPURL.Empty();
	if (!bInitialized)
	{
		return false;
	}
	if (bStreaming)
	{
		OutRTSPURL = RTSPURL;
		return true;
	}

	if (!HardwareEncoder)
	{
		FString EncoderError;
		HardwareEncoder = MakeShared<FHardwareEncoderWrapper>();
		if (!HardwareEncoder->Initialize(EncoderConfig, EncoderError))
		{
			UE_LOG(LogTemp, Error, TEXT("StreamingPipeline: encoder init failed: %s"), *EncoderError);
			HardwareEncoder.Reset();
			return false;
		}
	}

	if (!EncodingThread)
	{
		EncodingThread = MakeUnique<FEncodingThread>(
			StreamID,
			HardwareEncoder,
			FramePool,
			Config.GetFrameQueueSize());
		EncodingThread->SetNALEncodedCallback(
			FEncodingThread::FOnNALUnitsEncoded::CreateRaw(this, &FStreamingPipeline::OnNALUnitsEncoded));
	}

	if (!EncodingThread->Start())
	{
		return false;
	}

	if (H264Source && RTSPServer)
	{
		if (!RTSPServer->AddStream(StreamID, H264Source.Get(), OutRTSPURL))
		{
			EncodingThread->Stop();
			return false;
		}
		RTSPURL = OutRTSPURL;
	}

	FString SinkError;
	if (EncodedVideoFanout && !EncodedVideoFanout->StartAll(SinkError))
	{
		UE_LOG(LogTemp, Error, TEXT("StreamingPipeline: sink startup failed: %s"), *SinkError);
		if (RTSPServer && H264Source)
		{
			RTSPServer->RemoveStream(StreamID);
		}
		EncodingThread->Stop();
		return false;
	}

	bStreaming = true;
	StartTime = FDateTime::Now();
	return true;
}

void FStreamingPipeline::Stop()
{
	if (!bStreaming)
	{
		return;
	}
	if (EncodedVideoFanout)
	{
		EncodedVideoFanout->StopAll();
	}
	if (EncodingThread)
	{
		EncodingThread->Stop();
	}
	if (RTSPServer && H264Source)
	{
		RTSPServer->RemoveStream(StreamID);
	}
	bStreaming = false;
}

bool FStreamingPipeline::CaptureFrame()
{
	if (!bStreaming || !FrameCapture || !EncodingThread || !EncodedVideoFanout)
	{
		return false;
	}

	// Render only when any transport wants frames. STANAG/recording sinks can
	// demand frames continuously even with zero RTSP/browser viewers.
	if (!EncodedVideoFanout->WantsFrames())
	{
		return false;
	}

	const double CurrentTime = FPlatformTime::Seconds();
	const double TargetFrameInterval = 1.0 / Config.GetFrameRateValue();
	if (CurrentTime - LastCaptureTime < TargetFrameInterval)
	{
		return false;
	}
	if (!SceneCapture.IsValid() || !SceneCapture->TextureTarget)
	{
		CaptureFailureCount++;
		return false;
	}

	USceneCaptureComponent2D* SceneCapturePtr = SceneCapture.Get();
	SceneCapturePtr->CaptureScene();

	FCaptureFrame CapturedFrame;
	if (!FrameCapture->CaptureFrame(SceneCapturePtr, CapturedFrame))
	{
		CaptureFailureCount++;
		return false;
	}
	if (!EncodingThread->QueueFrame(CapturedFrame))
	{
		if (FramePool && CapturedFrame.PooledFrame)
		{
			FramePool->ReleaseFrame(CapturedFrame.PooledFrame);
		}
		CaptureFailureCount++;
		return false;
	}

	TotalFramesCaptured++;
	LastCaptureTime = CurrentTime;
	FrameCounter++;
	return true;
}

FEncodedVideoMetadata FStreamingPipeline::BuildMetadata(const TArray<FEncodedNALUnit>& NALUnits) const
{
	FEncodedVideoMetadata Metadata;
	Metadata.FrameNumber = NALUnits.Num() > 0 ? NALUnits[0].FrameNumber : FrameCounter.load();
	Metadata.TimestampUs = NALUnits.Num() > 0 ? NALUnits[0].TimestampMs * 1000ULL
		: static_cast<uint64>(FPlatformTime::Seconds() * 1000000.0);

	if (SceneCapture.IsValid())
	{
		const USceneCaptureComponent2D* Capture = SceneCapture.Get();
		const FRotator SensorRelative = Capture->GetRelativeRotation();
		Metadata.SensorRelativeRollDeg = SensorRelative.Roll;
		Metadata.SensorRelativePitchDeg = SensorRelative.Pitch;
		Metadata.SensorRelativeYawDeg = SensorRelative.Yaw;
		Metadata.bHasSensorAttitude = true;

		Metadata.HorizontalFovDeg = Capture->FOVAngle;
		const double Aspect = EncoderConfig.Height > 0
			? static_cast<double>(EncoderConfig.Width) / static_cast<double>(EncoderConfig.Height)
			: 1.0;
		const double HalfHFovRad = FMath::DegreesToRadians(Metadata.HorizontalFovDeg * 0.5);
		Metadata.VerticalFovDeg = FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(HalfHFovRad) / Aspect));
		Metadata.bHasFieldOfView = true;

		if (const AActor* Owner = Capture->GetOwner())
		{
			const FRotator PlatformRotation = Owner->GetActorRotation();
			Metadata.PlatformRollDeg = PlatformRotation.Roll;
			Metadata.PlatformPitchDeg = PlatformRotation.Pitch;
			Metadata.PlatformHeadingDeg = PlatformRotation.Yaw;
			Metadata.bHasPlatformAttitude = true;
		}
	}
	return Metadata;
}

void FStreamingPipeline::OnNALUnitsEncoded(const TArray<FEncodedNALUnit>& NALUnits)
{
	if (bShuttingDown.load() || !EncodedVideoFanout || NALUnits.IsEmpty())
	{
		return;
	}
	for (const FEncodedNALUnit& NAL : NALUnits)
	{
		if (NAL.Data.Num() > 0 && !NAL.Data.GetData())
		{
			return;
		}
	}
	EncodedVideoFanout->Push(NALUnits, BuildMetadata(NALUnits));
}

void FStreamingPipeline::AddEncodedVideoSink(const TSharedRef<IEncodedVideoSink>& Sink)
{
	if (!EncodedVideoFanout)
	{
		EncodedVideoFanout = MakeUnique<FEncodedVideoFanout>();
	}
	EncodedVideoFanout->AddSink(Sink);
	if (bStreaming)
	{
		FString Error;
		if (!Sink->Start(Error))
		{
			UE_LOG(LogTemp, Error, TEXT("StreamingPipeline: failed to hot-start sink '%s': %s"), *Sink->GetName(), *Error);
		}
	}
}

void FStreamingPipeline::RemoveEncodedVideoSink(const FString& SinkName)
{
	if (EncodedVideoFanout)
	{
		EncodedVideoFanout->RemoveSink(SinkName);
	}
}

bool FStreamingPipeline::UpdateConfiguration(const FStreamConfig& NewConfig, FString& OutErrorMessage)
{
	const bool bWasStreaming = bStreaming;
	if (bWasStreaming)
	{
		Stop();
	}
	Shutdown();
	if (!Initialize(NewConfig, OutErrorMessage))
	{
		return false;
	}
	if (bWasStreaming)
	{
		FString IgnoredURL;
		if (!Start(IgnoredURL))
		{
			OutErrorMessage = TEXT("Failed to restart stream after config update");
			return false;
		}
	}
	return true;
}

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
	Info.ConnectedClients = (H264Source && H264Source->HasActiveClient()) ? 1 : 0;
	Info.TotalFramesEncoded = TotalFramesCaptured;
	Info.ActualFPS = 0.0f;
	Info.AvgEncodingTimeMs = 0.0f;
	Info.CreatedTime = StartTime;
	return Info;
}

FString FStreamingPipeline::GetStatsString() const
{
	return FString::Printf(
		TEXT("Pipeline [%s]: Initialized=%s, Streaming=%s, Sinks=%d, Captured=%llu, Failures=%llu, Encoder=%s"),
		*StreamID.ToString(),
		bInitialized.load() ? TEXT("Yes") : TEXT("No"),
		bStreaming.load() ? TEXT("Yes") : TEXT("No"),
		EncodedVideoFanout ? EncodedVideoFanout->NumSinks() : 0,
		TotalFramesCaptured.load(),
		CaptureFailureCount.load(),
		*EncoderTypeToString(GetEncoderType()));
}
