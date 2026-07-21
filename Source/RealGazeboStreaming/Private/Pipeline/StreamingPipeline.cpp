// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Licensed under the GNU General Public License v3.0.

#include "Pipeline/StreamingPipeline.h"
#include "RTSP/RTSPServer.h"
#include "Transport/RTSPEncodedVideoSink.h"
#include "Transport/STANAG4609Sink.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

FStreamingPipeline::FStreamingPipeline(
	const FStreamIdentifier& InStreamID,
	USceneCaptureComponent2D* InSceneCapture,
	TSharedPtr<FRTSPServerWrapper> InRTSPServer)
	: StreamID(InStreamID), SceneCapture(InSceneCapture), RTSPServer(InRTSPServer)
{
	EncodedVideoFanout = MakeUnique<FEncodedVideoFanout>();

	// Automatic STANAG output is intentionally bound to one camera per vehicle
	// (default camera id: "fpv"). This avoids interleaving independent MPEG-TS
	// streams when a vehicle also exposes front/bottom/debug cameras.
	FString StanagHost;
	FString StanagCamera = TEXT("fpv");
	int32 StanagBasePort = 0;
	const TCHAR* Cmd = FCommandLine::Get();
	FParse::Value(Cmd, TEXT("RealGazeboStanagCamera="), StanagCamera);
	if (StreamID.CameraID.Equals(StanagCamera, ESearchCase::IgnoreCase) &&
		FParse::Value(Cmd, TEXT("RealGazeboStanagHost="), StanagHost) &&
		FParse::Value(Cmd, TEXT("RealGazeboStanagPort="), StanagBasePort) &&
		!StanagHost.IsEmpty())
	{
		const int32 StreamPort = StanagBasePort + static_cast<int32>(StreamID.VehicleID.VehicleNum);
		if (StreamPort > 0 && StreamPort <= 65535)
		{
			EncodedVideoFanout->AddSink(MakeShared<FSTANAG4609Sink>(StanagHost, StreamPort));
			UE_LOG(LogTemp, Log, TEXT("StreamingPipeline: STANAG %s -> udp://%s:%d"),
				*StreamID.ToString(), *StanagHost, StreamPort);
		}
	}
}

FStreamingPipeline::~FStreamingPipeline() { Shutdown(); }

bool FStreamingPipeline::Initialize(const FStreamConfig& InConfig, FString& OutErrorMessage)
{
	if (bInitialized) return true;
	Config = InConfig;
	EncoderConfig = FEncoderConfig(Config);
	if (!EncoderConfig.Validate(OutErrorMessage)) return false;
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
	if (!bInitialized) return;
	bShuttingDown.store(true);
	if (HardwareEncoder) HardwareEncoder->PrepareForShutdown();
	if (EncodingThread) EncodingThread->ClearNALEncodedCallback();
	Stop();
	CleanupComponents();
	bInitialized = false;
	bShuttingDown = false;
}

bool FStreamingPipeline::InitializeComponents(FString& OutErrorMessage)
{
	const int32 Width = EncoderConfig.Width;
	const int32 Height = EncoderConfig.Height;
	FramePool = MakeShared<FFramePool>(Width, Height, Config.GetFramePoolSize());
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

	USceneCaptureComponent2D* Capture = SceneCapture.Get();
	if (!Capture->TextureTarget)
	{
		UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(
			Capture->GetOwner(),
			MakeUniqueObjectName(Capture->GetOwner(), UTextureRenderTarget2D::StaticClass(),
				FName(*FString::Printf(TEXT("RT_%s"), *StreamID.ToString()))));
		if (!RT)
		{
			OutErrorMessage = TEXT("Failed to create TextureRenderTarget2D");
			return false;
		}
		RT->RenderTargetFormat = RTF_RGBA8;
		RT->InitCustomFormat(Width, Height, PF_B8G8R8A8, false);
		RT->bAutoGenerateMips = false;
		RT->ClearColor = FLinearColor::Black;
		RT->bGPUSharedFlag = true;
		RT->UpdateResourceImmediate(true);
		Capture->TextureTarget = RT;
	}
	else
	{
		const bool bNeedsReinit = Capture->TextureTarget->SizeX != Width ||
			Capture->TextureTarget->SizeY != Height ||
			Capture->TextureTarget->RenderTargetFormat != RTF_RGBA8 ||
			Capture->TextureTarget->OverrideFormat != PF_B8G8R8A8;
		if (bNeedsReinit)
		{
			Capture->TextureTarget->RenderTargetFormat = RTF_RGBA8;
			Capture->TextureTarget->InitCustomFormat(Width, Height, PF_B8G8R8A8, false);
			Capture->TextureTarget->bAutoGenerateMips = false;
			Capture->TextureTarget->bGPUSharedFlag = true;
			Capture->TextureTarget->UpdateResourceImmediate(true);
		}
	}
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

	if (RTSPServer && RTSPServer->IsRunning())
	{
		if (UsageEnvironment* Env = RTSPServer->GetEnvironment())
		{
			H264Source = MakeUnique<FH264StreamSource>(StreamID, *Env, Config.GetNALQueueSize(), Config.GetMaxNALSizeBytes());
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
		if (RTSPSink) EncodedVideoFanout->RemoveSink(RTSPSink->GetName());
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
	if (!bInitialized) return false;
	if (bStreaming)
	{
		OutRTSPURL = RTSPURL;
		return true;
	}
	if (!HardwareEncoder)
	{
		FString Error;
		HardwareEncoder = MakeShared<FHardwareEncoderWrapper>();
		if (!HardwareEncoder->Initialize(EncoderConfig, Error))
		{
			UE_LOG(LogTemp, Error, TEXT("StreamingPipeline encoder init failed: %s"), *Error);
			HardwareEncoder.Reset();
			return false;
		}
	}
	if (!EncodingThread)
	{
		EncodingThread = MakeUnique<FEncodingThread>(StreamID, HardwareEncoder, FramePool, Config.GetFrameQueueSize());
		EncodingThread->SetNALEncodedCallback(
			FEncodingThread::FOnNALUnitsEncoded::CreateRaw(this, &FStreamingPipeline::OnNALUnitsEncoded));
	}
	if (!EncodingThread->Start()) return false;

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
		UE_LOG(LogTemp, Error, TEXT("StreamingPipeline sink startup failed: %s"), *SinkError);
		if (RTSPServer && H264Source) RTSPServer->RemoveStream(StreamID);
		EncodingThread->Stop();
		return false;
	}
	bStreaming = true;
	StartTime = FDateTime::Now();
	return true;
}

void FStreamingPipeline::Stop()
{
	if (!bStreaming) return;
	if (EncodedVideoFanout) EncodedVideoFanout->StopAll();
	if (EncodingThread) EncodingThread->Stop();
	if (RTSPServer && H264Source) RTSPServer->RemoveStream(StreamID);
	bStreaming = false;
}

bool FStreamingPipeline::CaptureFrame()
{
	if (!bStreaming || !FrameCapture || !EncodingThread || !EncodedVideoFanout || !EncodedVideoFanout->WantsFrames())
		return false;
	const double Now = FPlatformTime::Seconds();
	if (Now - LastCaptureTime < 1.0 / Config.GetFrameRateValue()) return false;
	if (!SceneCapture.IsValid() || !SceneCapture->TextureTarget)
	{
		CaptureFailureCount++;
		return false;
	}
	USceneCaptureComponent2D* Capture = SceneCapture.Get();
	Capture->CaptureScene();
	FCaptureFrame Frame;
	if (!FrameCapture->CaptureFrame(Capture, Frame))
	{
		CaptureFailureCount++;
		return false;
	}
	if (!EncodingThread->QueueFrame(Frame))
	{
		if (FramePool && Frame.PooledFrame) FramePool->ReleaseFrame(Frame.PooledFrame);
		CaptureFailureCount++;
		return false;
	}
	TotalFramesCaptured++;
	LastCaptureTime = Now;
	FrameCounter++;
	return true;
}

FEncodedVideoMetadata FStreamingPipeline::BuildMetadata(const TArray<FEncodedNALUnit>& NALUnits) const
{
	FEncodedVideoMetadata M;
	M.FrameNumber = NALUnits.Num() > 0 ? NALUnits[0].FrameNumber : FrameCounter.load();
	M.TimestampUs = NALUnits.Num() > 0 ? NALUnits[0].TimestampMs * 1000ULL
		: static_cast<uint64>(FPlatformTime::Seconds() * 1000000.0);
	if (!SceneCapture.IsValid()) return M;

	const USceneCaptureComponent2D* Capture = SceneCapture.Get();
	const FRotator Sensor = Capture->GetRelativeRotation();
	M.SensorRelativeRollDeg = Sensor.Roll;
	M.SensorRelativePitchDeg = Sensor.Pitch;
	M.SensorRelativeYawDeg = Sensor.Yaw;
	M.bHasSensorAttitude = true;
	M.HorizontalFovDeg = Capture->FOVAngle;
	const double Aspect = EncoderConfig.Height > 0
		? static_cast<double>(EncoderConfig.Width) / static_cast<double>(EncoderConfig.Height) : 1.0;
	const double HalfH = FMath::DegreesToRadians(M.HorizontalFovDeg * 0.5);
	M.VerticalFovDeg = FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(HalfH) / Aspect));
	M.bHasFieldOfView = true;

	M.LocalPositionCm = Capture->GetComponentLocation();
	M.bHasLocalPosition = true;

	if (const AActor* Owner = Capture->GetOwner())
	{
		const FRotator R = Owner->GetActorRotation();
		M.PlatformRollDeg = R.Roll;
		M.PlatformPitchDeg = R.Pitch;
		M.PlatformHeadingDeg = R.Yaw;
		M.bHasPlatformAttitude = true;
	}
	return M;
}

void FStreamingPipeline::OnNALUnitsEncoded(const TArray<FEncodedNALUnit>& NALUnits)
{
	if (bShuttingDown.load() || !EncodedVideoFanout || NALUnits.IsEmpty()) return;
	for (const FEncodedNALUnit& NAL : NALUnits)
		if (NAL.Data.Num() > 0 && !NAL.Data.GetData()) return;
	EncodedVideoFanout->Push(NALUnits, BuildMetadata(NALUnits));
}

void FStreamingPipeline::AddEncodedVideoSink(const TSharedRef<IEncodedVideoSink>& Sink)
{
	if (!EncodedVideoFanout) EncodedVideoFanout = MakeUnique<FEncodedVideoFanout>();
	EncodedVideoFanout->AddSink(Sink);
	if (bStreaming)
	{
		FString Error;
		if (!Sink->Start(Error))
			UE_LOG(LogTemp, Error, TEXT("Failed to hot-start sink '%s': %s"), *Sink->GetName(), *Error);
	}
}

void FStreamingPipeline::RemoveEncodedVideoSink(const FString& SinkName)
{
	if (EncodedVideoFanout) EncodedVideoFanout->RemoveSink(SinkName);
}

bool FStreamingPipeline::UpdateConfiguration(const FStreamConfig& NewConfig, FString& OutErrorMessage)
{
	const bool bWasStreaming = bStreaming;
	if (bWasStreaming) Stop();
	Shutdown();
	if (!Initialize(NewConfig, OutErrorMessage)) return false;
	if (bWasStreaming)
	{
		FString Url;
		if (!Start(Url))
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
	return FString::Printf(TEXT("Pipeline [%s]: Initialized=%s, Streaming=%s, Sinks=%d, Captured=%llu, Failures=%llu, Encoder=%s"),
		*StreamID.ToString(), bInitialized.load() ? TEXT("Yes") : TEXT("No"),
		bStreaming.load() ? TEXT("Yes") : TEXT("No"), EncodedVideoFanout ? EncodedVideoFanout->NumSinks() : 0,
		TotalFramesCaptured.load(), CaptureFailureCount.load(), *EncoderTypeToString(GetEncoderType()));
}
