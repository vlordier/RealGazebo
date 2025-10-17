// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Capture/RealGazeboStreamingCamera.h"
#include "Core/RealGazeboStreamingSubsystem.h"
#include "Core/RealGazeboStreamingLogger.h"
#include "Core/RealGazeboStreamingSettings.h"
#include "Core/RealGazeboStreamConfig.h"
#include "Pipeline/RealGazeboStreamPipeline.h"
#include "Pipeline/RealGazeboFramePool.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "Async/Async.h"
#include "Vehicles/VehicleBasePawn.h"  // For vehicle ID detection
#include "Core/GazeboBridgeSubsystem.h"  // For vehicle config access

URealGazeboStreamingCamera::URealGazeboStreamingCamera()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// Update frame capture interval
	FrameCaptureInterval = 1.0f / static_cast<float>(CaptureFrameRate);
}

void URealGazeboStreamingCamera::BeginPlay()
{
	Super::BeginPlay();

	// Get streaming subsystem
	StreamingSubsystem = URealGazeboStreamingSubsystem::GetStreamingSubsystem(this);
	if (!StreamingSubsystem)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("StreamingCamera: Failed to get streaming subsystem"));
		return;
	}

	// Get configuration from settings
	const URealGazeboStreamingSettings* Settings = URealGazeboStreamingSettings::Get();
	if (!Settings)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("StreamingCamera: Failed to get settings"));
		return;
	}

	// Detect vehicle ID from owning vehicle pawn (or use override)
	if (!DetectVehicleID(DetectedVehicleID))
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("StreamingCamera: Failed to detect vehicle ID. Make sure this component is attached to a VehicleBasePawn or set VehicleIDOverride."));
		return;
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamingCamera: BeginPlay - Vehicle: %s, Camera: %s, FOV: %.1f"),
		*DetectedVehicleID.ToString(), *CameraID, FieldOfView);

	// Initialize streaming
	InitializeStreaming();

	// Auto-start if enabled
	if (bAutoStart || Settings->bAutoStartStreaming)
	{
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamingCamera: Auto-starting stream..."));
		StartStreaming();
	}
}

void URealGazeboStreamingCamera::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Stop streaming and cleanup
	if (bIsStreaming)
	{
		StopStreaming();
	}

	ShutdownStreaming();
	StreamingSubsystem = nullptr;

	Super::EndPlay(EndPlayReason);
}

void URealGazeboStreamingCamera::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Capture frames at specified rate
	if (bIsStreaming)
	{
		FrameCaptureTimer += DeltaTime;

		if (FrameCaptureTimer >= FrameCaptureInterval)
		{
			CaptureFrame();
			FrameCaptureTimer = FMath::Fmod(FrameCaptureTimer, FrameCaptureInterval);
		}
	}

	// Draw debug info if enabled
	if (bShowDebugInfo)
	{
		DrawDebugInfo();
	}
}

bool URealGazeboStreamingCamera::StartStreaming()
{
	if (!StreamingSubsystem)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("StreamingCamera: Cannot start - no subsystem"));
		return false;
	}

	if (bIsStreaming)
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("StreamingCamera: Already streaming"));
		return true;
	}

	// Start stream via subsystem
	FStreamKey StreamKey = GetStreamKey();
	if (StreamingSubsystem->StartStream(StreamKey))
	{
		bIsStreaming = true;
		FrameSequence = 0;
		FrameCaptureTimer = 0.0f;

		UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamingCamera: Started streaming - RTSP URL: %s"), *GetRTSPURL());
		return true;
	}

	UE_LOG(LogRealGazeboStreaming, Error, TEXT("StreamingCamera: Failed to start stream"));
	return false;
}

void URealGazeboStreamingCamera::StopStreaming()
{
	if (!StreamingSubsystem || !bIsStreaming)
	{
		return;
	}

	// Stop stream via subsystem
	FStreamKey StreamKey = GetStreamKey();
	StreamingSubsystem->StopStream(StreamKey);

	bIsStreaming = false;

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamingCamera: Stopped streaming (Total frames: %llu)"), FrameSequence);
}

void URealGazeboStreamingCamera::PauseStreaming()
{
	if (!StreamingSubsystem || !bIsStreaming)
	{
		return;
	}

	FStreamKey StreamKey = GetStreamKey();
	StreamingSubsystem->PauseStream(StreamKey);

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamingCamera: Paused streaming"));
}

void URealGazeboStreamingCamera::ResumeStreaming()
{
	if (!StreamingSubsystem)
	{
		return;
	}

	FStreamKey StreamKey = GetStreamKey();
	StreamingSubsystem->ResumeStream(StreamKey);

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamingCamera: Resumed streaming"));
}

bool URealGazeboStreamingCamera::IsStreaming() const
{
	return bIsStreaming;
}

EStreamState URealGazeboStreamingCamera::GetStreamState() const
{
	if (!StreamingSubsystem)
	{
		return EStreamState::Stopped;
	}

	FStreamKey StreamKey = GetStreamKey();
	return StreamingSubsystem->GetStreamState(StreamKey);
}

FString URealGazeboStreamingCamera::GetRTSPURL() const
{
	if (!StreamingSubsystem)
	{
		return TEXT("");
	}

	FStreamKey StreamKey = GetStreamKey();
	return StreamingSubsystem->GetRTSPURL(StreamKey);
}

FStreamKey URealGazeboStreamingCamera::GetStreamKey() const
{
	return FStreamKey(DetectedVehicleID, DetectedVehicleTypeName, CameraID);
}

bool URealGazeboStreamingCamera::GetStreamingStats(FStreamingStats& OutStats) const
{
	if (!StreamingSubsystem)
	{
		return false;
	}

	FStreamKey StreamKey = GetStreamKey();
	return StreamingSubsystem->GetStreamStats(StreamKey, OutStats);
}

void URealGazeboStreamingCamera::InitializeStreaming()
{
	if (!StreamingSubsystem)
	{
		return;
	}

	// Get stream configuration from settings
	const URealGazeboStreamingSettings* Settings = URealGazeboStreamingSettings::Get();
	if (!Settings)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("StreamingCamera: Cannot get settings"));
		return;
	}

	// Create stream configuration
	FRealGazeboStreamConfig Config;
	Config.AspectRatio = Settings->DefaultAspectRatio;
	Config.Resolution = Settings->DefaultResolution;
	Config.FrameRate = Settings->DefaultFrameRate;
	Config.Quality = Settings->DefaultQuality;
	Config.EncodingProfile = EH264Profile::Main;
	Config.GOPSize = 60;  // 2 seconds at 30fps
	Config.bEnableAdaptiveQuality = true;
	Config.UpdateComputedValues();

	// Update capture frame rate from config
	CaptureFrameRate = Config.FPSValue;
	FrameCaptureInterval = 1.0f / static_cast<float>(CaptureFrameRate);

	// Create scene capture component if not already created
	if (!SceneCaptureComponent)
	{
		AActor* Owner = GetOwner();
		if (!Owner)
		{
			UE_LOG(LogRealGazeboStreaming, Error, TEXT("StreamingCamera: No owner actor"));
			return;
		}

		SceneCaptureComponent = NewObject<USceneCaptureComponent2D>(Owner, TEXT("StreamingSceneCapture"));
		if (!SceneCaptureComponent)
		{
			UE_LOG(LogRealGazeboStreaming, Error, TEXT("StreamingCamera: Failed to create scene capture component"));
			return;
		}

		SceneCaptureComponent->RegisterComponent();
		SceneCaptureComponent->AttachToComponent(this, FAttachmentTransformRules::SnapToTargetIncludingScale);
	}

	// Create render target
	RenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("StreamingRenderTarget"));
	if (!RenderTarget)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("StreamingCamera: Failed to create render target"));
		return;
	}

	// Configure render target (BGRA8 format for capture)
	RenderTarget->InitCustomFormat(Config.Dimensions.X, Config.Dimensions.Y, PF_B8G8R8A8, false);
	RenderTarget->ClearColor = FLinearColor::Black;
	RenderTarget->bAutoGenerateMips = false;
	RenderTarget->UpdateResource();

	// Configure scene capture
	SceneCaptureComponent->TextureTarget = RenderTarget;
	SceneCaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	SceneCaptureComponent->bCaptureEveryFrame = false;  // Manual capture
	SceneCaptureComponent->bCaptureOnMovement = false;
	SceneCaptureComponent->FOVAngle = FieldOfView;

	// Register camera with subsystem
	if (!StreamingSubsystem->RegisterCamera(this, Config))
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("StreamingCamera: Failed to register with subsystem"));
		return;
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamingCamera: Initialized (%dx%d @ %d fps)"),
		Config.Dimensions.X, Config.Dimensions.Y, Config.FPSValue);
}

void URealGazeboStreamingCamera::ShutdownStreaming()
{
	if (!StreamingSubsystem)
	{
		return;
	}

	// Unregister camera from subsystem
	StreamingSubsystem->UnregisterCamera(this);

	// Cleanup scene capture
	if (SceneCaptureComponent)
	{
		SceneCaptureComponent->TextureTarget = nullptr;
		SceneCaptureComponent->DestroyComponent();
		SceneCaptureComponent = nullptr;
	}

	// Cleanup render target
	if (RenderTarget)
	{
		RenderTarget->ReleaseResource();
		RenderTarget = nullptr;
	}

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamingCamera: Shut down"));
}

bool URealGazeboStreamingCamera::DetectVehicleID(FVehicleID& OutVehicleID)
{
	// Check if override is set (not 0,0)
	if (VehicleIDOverride.VehicleType != 0 || VehicleIDOverride.VehicleNum != 0)
	{
		OutVehicleID = VehicleIDOverride;

		// Get vehicle type name from config for override
		if (UGazeboBridgeSubsystem* BridgeSubsystem = UGazeboBridgeSubsystem::GetBridgeSubsystem(this))
		{
			if (const FBridgeVehicleConfigRow* Config = BridgeSubsystem->GetVehicleConfigInternal(VehicleIDOverride.VehicleType))
			{
				DetectedVehicleTypeName = Config->VehicleName;
			}
		}

		UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamingCamera: Using override Vehicle ID: %s (%s)"),
			*OutVehicleID.ToString(), *DetectedVehicleTypeName);
		return true;
	}

	// Try to get vehicle ID from owning VehicleBasePawn
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("StreamingCamera: No owner actor"));
		return false;
	}

	// Cast to VehicleBasePawn
	AVehicleBasePawn* VehiclePawn = Cast<AVehicleBasePawn>(OwnerActor);
	if (!VehiclePawn)
	{
		UE_LOG(LogRealGazeboStreaming, Warning,
			TEXT("StreamingCamera: Owner is not a VehicleBasePawn (%s). Set VehicleIDOverride or attach to a VehicleBasePawn."),
			*OwnerActor->GetClass()->GetName());
		return false;
	}

	// Get vehicle ID from pawn
	OutVehicleID = VehiclePawn->VehicleID;

	// Get vehicle type name from bridge config
	if (UGazeboBridgeSubsystem* BridgeSubsystem = UGazeboBridgeSubsystem::GetBridgeSubsystem(this))
	{
		if (const FBridgeVehicleConfigRow* Config = BridgeSubsystem->GetVehicleConfigInternal(OutVehicleID.VehicleType))
		{
			DetectedVehicleTypeName = Config->VehicleName;
			UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamingCamera: Detected Vehicle ID from pawn: %s (%s)"),
				*OutVehicleID.ToString(), *DetectedVehicleTypeName);
		}
		else
		{
			UE_LOG(LogRealGazeboStreaming, Warning, TEXT("StreamingCamera: Could not find vehicle config for type %d"), OutVehicleID.VehicleType);
		}
	}
	else
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("StreamingCamera: Could not access GazeboBridgeSubsystem for vehicle type name"));
	}

	return true;
}

void URealGazeboStreamingCamera::CaptureFrame()
{
	if (!bIsStreaming)
	{
		return;
	}

	if (!SceneCaptureComponent || !RenderTarget || !StreamingSubsystem)
	{
		return;
	}

	// Capture scene to render target
	SceneCaptureComponent->CaptureScene();

	// Get stream key
	FStreamKey StreamKey = GetStreamKey();

	// Verify hardware encoder is available (NVENC/AMF)
	if (!StreamingSubsystem->SupportsTextureEncoding(StreamKey))
	{
		// Hardware encoding only - no software fallback
		UE_LOG(LogRealGazeboStreaming, Error,
			TEXT("StreamingCamera: Hardware encoder not available for stream %s. Only NVENC/AMF supported."),
			*StreamKey.ToString());
		StopStreaming();  // Stop streaming if hardware encoder unavailable
		return;
	}

	// Metadata
	const uint64 CurrentFrameNumber = FrameSequence;
	const double CaptureStartTime = FPlatformTime::Seconds();

	// Get render target resource ON GAME THREAD
	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("StreamingCamera: No render target resource"));
		return;
	}

	// RENDER THREAD: Get RHI texture and submit to encoder (zero-copy)
	ENQUEUE_RENDER_COMMAND(CaptureTextureFrame)(
		[StreamingSubsystem = StreamingSubsystem, StreamKey, RTResource, CaptureStartTime, CurrentFrameNumber](FRHICommandListImmediate& RHICmdList)
		{
			if (!RTResource || !StreamingSubsystem)
			{
				return;
			}

			// Get RHI texture reference from render target
			FTexture2DRHIRef RHITexture = RTResource->GetRenderTargetTexture();
			if (!RHITexture.IsValid())
			{
				return;
			}

			// Submit texture directly to encoding thread (zero-copy)
			StreamingSubsystem->SubmitTextureFrame(StreamKey, RHITexture, CaptureStartTime, CurrentFrameNumber);
		});

	FrameSequence++;

	// Log capture progress periodically
	if ((FrameSequence % 100) == 0)
	{
		UE_LOG(LogRealGazeboStreaming, Verbose, TEXT("StreamingCamera: Captured %llu frames (Hardware Encoding)"), FrameSequence);
	}
}

void URealGazeboStreamingCamera::DrawDebugInfo()
{
	if (!GEngine)
	{
		return;
	}

	FStreamingStats Stats;
	const bool bHasStats = GetStreamingStats(Stats);

	const FString StatusText = bIsStreaming ? TEXT("STREAMING") : TEXT("STOPPED");
	const FColor StatusColor = bIsStreaming ? FColor::Green : FColor::Red;

	FString DebugText = FString::Printf(
		TEXT("=== Camera: %s ===\n")
		TEXT("Status: %s\n")
		TEXT("RTSP URL: %s\n")
		TEXT("Frame: %llu\n")
		TEXT("FOV: %.1f\n"),
		*GetStreamKey().ToString(),
		*StatusText,
		*GetRTSPURL(),
		FrameSequence,
		FieldOfView
	);

	if (bHasStats)
	{
		DebugText += FString::Printf(
			TEXT("Latency: %.1f ms\n")
			TEXT("Queue: E=%d R=%d\n")
			TEXT("Bitrate: %.2f Mbps\n")
			TEXT("Dropped: %llu"),
			Stats.TotalLatencyMs,
			Stats.EncodingQueueDepth,
			Stats.RTSPQueueDepth,
			Stats.CurrentBitrateMbps,
			Stats.TotalFramesDropped
		);
	}

	GEngine->AddOnScreenDebugMessage(
		static_cast<int32>(GetUniqueID()),  // Cast to int32 for UE5.1
		0.0f,  // No timeout (updated every frame)
		StatusColor,
		DebugText
	);
}
