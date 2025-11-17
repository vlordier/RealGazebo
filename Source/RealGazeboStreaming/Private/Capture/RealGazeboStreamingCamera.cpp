// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Capture/RealGazeboStreamingCamera.h"
#include "Core/RealGazeboStreamingSubsystem.h"
#include "Core/RealGazeboStreamingTypes.h"
#include "Core/RealGazeboStreamManager.h"  // For centralized stream settings
#include "Pipeline/RealGazeboStreamPipeline.h"
#include "Pipeline/RealGazeboFramePool.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"  // For TActorIterator
#include "RenderingThread.h"
#include "TextureResource.h"
#include "Async/Async.h"
#include "Vehicles/VehicleBasePawn.h"  // For vehicle ID detection
#include "Core/GazeboBridgeSubsystem.h"  // For vehicle config access

// RDG-based texture copy includes (optimization implemented 2025-11-03)
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"

// GPU fence synchronization (multi-stream safety implemented 2025-11-13)
#include "HAL/PlatformProcess.h"  // For FPlatformProcess::Sleep()

// SceneCapture pooling (optimization implemented 2025-11-03)
#include "Capture/RealGazeboSceneCapturePool.h"
#include "HAL/IConsoleManager.h"

// Stream isolation enforcement settings moved to StreamManager Blueprint properties
// No longer using console variables - user configures via StreamManager actor in editor

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

	// Deferred initialization: Wait for vehicle activation from pool.
	// Vehicle pooling sets VehicleID after BeginPlay, so we defer initialization until TickComponent.

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamingCamera: BeginPlay - waiting for vehicle activation..."));
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

	// DEFERRED INITIALIZATION: Check if vehicle is active and initialize streaming
	if (!bIsInitialized && StreamingSubsystem)
	{
		// Try to detect vehicle ID
		FVehicleID TempVehicleID;
		if (DetectVehicleID(TempVehicleID))
		{
			// Check if VehicleID is valid (not 0,0 from pooled inactive vehicle)
			const bool bValidVehicleID = (TempVehicleID.VehicleType != 0 || TempVehicleID.VehicleNum != 0);

			if (bValidVehicleID)
			{
				// Vehicle is activated - proceed with initialization
				DetectedVehicleID = TempVehicleID;

				UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamingCamera: Vehicle activated - Vehicle: %s (%s), Camera: %s, FOV: %.1f"),
					*DetectedVehicleID.ToString(), *DetectedVehicleTypeName, *CameraID, FieldOfView);

				// Initialize streaming
				InitializeStreaming();

				// Set bIsInitialized before calling StartStreaming() - this flag is checked by StartStreaming()
				bIsInitialized = true;

				// RegisterCamera() in InitializeStreaming() auto-started the pipeline if StreamManager.bAutoStartStreams is enabled.
				// We still need to call StartStreaming() on the camera component to set bIsStreaming flag for debug display and frame capture.

				// Check if StreamManager has auto-start enabled
				ARealGazeboStreamManager* StreamManager = nullptr;
				if (UWorld* World = GetWorld())
				{
					for (TActorIterator<ARealGazeboStreamManager> It(World); It; ++It)
					{
						StreamManager = *It;
						break;
					}
				}

				// If StreamManager enables auto-start, call StartStreaming() to sync camera state
				if (StreamManager && StreamManager->bAutoStartStreams)
				{
					UE_LOG(LogRealGazeboStreaming, Log,
						TEXT("StreamingCamera: Auto-start enabled - starting stream"));

					// Start streaming (this sets bIsStreaming=true and initializes frame capture)
					if (!StartStreaming())
					{
						UE_LOG(LogRealGazeboStreaming, Warning,
							TEXT("StreamingCamera: Auto-start failed"));
					}
				}
				else if (!StreamManager)
				{
					UE_LOG(LogRealGazeboStreaming, Warning,
						TEXT("StreamingCamera: No StreamManager in level - stream must be started manually"));
				}
				else
				{
					UE_LOG(LogRealGazeboStreaming, Log,
						TEXT("StreamingCamera: Auto-start disabled - stream must be started manually"));
				}
			}
		}

		// Skip rest of tick until initialized
		return;
	}

	// Capture frames at specified rate (DECOUPLED from game tick rate)
	// Use absolute time to ensure consistent frame rate regardless of game FPS
	if (bIsStreaming)
	{
		const double CurrentTime = FPlatformTime::Seconds();

		// Initialize capture time on first frame
		if (LastCaptureTime == 0.0)
		{
			LastCaptureTime = CurrentTime;
		}

		const double TimeSinceLastCapture = CurrentTime - LastCaptureTime;

		// CRITICAL DEBUG (2025-11-17): Log timing every 30 frames to verify absolute time fix
		if ((FrameSequence % 30) == 0)
		{
			UE_LOG(LogRealGazeboStreaming, Warning,
				TEXT("FRAME TIMING CHECK (ABSOLUTE): DeltaTime=%.6f | TimeSinceLastCapture=%.6f | FrameCaptureInterval=%.6f | CaptureFrameRate=%d"),
				DeltaTime, TimeSinceLastCapture, FrameCaptureInterval, CaptureFrameRate);
		}

		if (TimeSinceLastCapture >= FrameCaptureInterval)
		{
			CaptureFrame();
			// Advance capture time by interval (NOT current time) to prevent drift
			LastCaptureTime += FrameCaptureInterval;

			// If we've fallen too far behind (>1 frame interval), resync to current time
			if ((CurrentTime - LastCaptureTime) > FrameCaptureInterval)
			{
				UE_LOG(LogRealGazeboStreaming, Warning,
					TEXT("Frame capture lagging behind (%.3fms) - resyncing to current time"),
					(CurrentTime - LastCaptureTime) * 1000.0);
				LastCaptureTime = CurrentTime;
			}
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

	// Check if camera is initialized
	if (!bIsInitialized)
	{
		UE_LOG(LogRealGazeboStreaming, Error,
			TEXT("StreamingCamera: Cannot start - camera not initialized (vehicle not yet activated from pool)"));
		UE_LOG(LogRealGazeboStreaming, Error,
			TEXT("  DetectedVehicleID not set. Wait for vehicle activation or call InitializeStreaming() first."));
		return false;
	}

	// Validate CameraID is set (now always required)
	if (!ValidateCameraID())
	{
		return false;
	}

	// Start stream via subsystem
	FStreamKey StreamKey = GetStreamKey();

	UE_LOG(LogRealGazeboStreaming, Log,
		TEXT("StreamingCamera: Attempting to start stream with key: %s (VehicleID: %s, VehicleType: %s, CameraID: %s)"),
		*StreamKey.ToString(), *DetectedVehicleID.ToString(), *DetectedVehicleTypeName, *CameraID);

	if (StreamingSubsystem->StartStream(StreamKey))
	{
		bIsStreaming = true;
		FrameSequence = 0;
		LastCaptureTime = 0.0;  // Reset absolute time (will be initialized on first tick)

		UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamingCamera: Started streaming - RTSP URL: %s"), *GetRTSPURL());
		return true;
	}

	UE_LOG(LogRealGazeboStreaming, Error,
		TEXT("StreamingCamera: Subsystem->StartStream() returned FALSE for key: %s"), *StreamKey.ToString());
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
	// Simple: Just use the user-provided CameraID (always required now)
	FStreamKey StreamKey = FStreamKey(DetectedVehicleID, DetectedVehicleTypeName, CameraID);

	// DEBUG: Log detailed StreamKey for isolation verification
	UE_LOG(LogRealGazeboStreaming, Verbose,
		TEXT("GetStreamKey: %s"),
		*StreamKey.ToDebugString());

	return StreamKey;
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

bool URealGazeboStreamingCamera::ValidateCameraID() const
{
	if (CameraID.IsEmpty())
	{
		if (!bHasLoggedCameraIDError)
		{
			const FString OwnerName = GetOwner() ? GetOwner()->GetName() : TEXT("<UnknownVehicle>");
			UE_LOG(LogRealGazeboStreaming, Error,
				TEXT("STREAM ISOLATION ERROR: CameraID is REQUIRED but not set on vehicle '%s'. ")
				TEXT("Set the CameraID property in Blueprint (e.g., 'front', 'right', 'gimbal'). ")
				TEXT("RTSP URL format: rtsp://localhost:8554/{vehicle_id}/{camera_id}"),
				*OwnerName);
			bHasLoggedCameraIDError = true;
		}
		return false;
	}
	bHasLoggedCameraIDError = false;
	return true;
}

void URealGazeboStreamingCamera::UpdateStreamingSettings()
{
	if (!StreamingSubsystem)
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("StreamingCamera: Cannot update settings - no subsystem"));
		return;
	}

	// Find StreamManager to get latest settings
	ARealGazeboStreamManager* StreamManager = nullptr;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ARealGazeboStreamManager> It(World); It; ++It)
		{
			StreamManager = *It;
			break;
		}
	}

	if (!StreamManager)
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("StreamingCamera: Cannot update settings - no StreamManager in level"));
		return;
	}

	// Get updated configuration from StreamManager
	FRealGazeboStreamConfig NewConfig = StreamManager->GetDefaultStreamConfig();

	// Update local capture parameters
	const int32 OldFrameRate = CaptureFrameRate;
	const int32 OldGOPSize = GOPSize;

	CaptureFrameRate = NewConfig.FPSValue;
	FrameCaptureInterval = 1.0f / static_cast<float>(CaptureFrameRate);
	GOPSize = NewConfig.GOPSize;

	// Check if resolution changed and recreate RenderTarget if needed to ensure encoder receives correct texture dimensions
	if (RenderTarget)
	{
		const FIntPoint CurrentDimensions(RenderTarget->SizeX, RenderTarget->SizeY);
		const FIntPoint NewDimensions = NewConfig.Dimensions;

		if (CurrentDimensions != NewDimensions)
		{
			UE_LOG(LogRealGazeboStreaming, Log,
				TEXT("StreamingCamera: Resolution changed %dx%d -> %dx%d, recreating RenderTarget"),
				CurrentDimensions.X, CurrentDimensions.Y, NewDimensions.X, NewDimensions.Y);

			// Stop streaming temporarily to safely recreate RenderTarget
			const bool bWasStreaming = bIsStreaming;
			if (bWasStreaming)
			{
				StopStreaming();
			}

			// Release old render target
			RenderTarget->ReleaseResource();

			// Recreate with new dimensions
			RenderTarget->InitCustomFormat(NewDimensions.X, NewDimensions.Y, PF_B8G8R8A8, false);
			RenderTarget->ClearColor = FLinearColor::Black;
			RenderTarget->UpdateResource();

			// Update scene capture component's target reference
			if (SceneCaptureComponent)
			{
				SceneCaptureComponent->TextureTarget = RenderTarget;
			}

			// Restart streaming if it was active
			if (bWasStreaming)
			{
				// Reset scene readiness check for new resolution
				bSceneReady = false;
				FrameSequence = 0;

				StartStreaming();
			}

			UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamingCamera: RenderTarget recreated successfully"));
		}
	}

	// Update the streaming pipeline with new config
	FStreamKey StreamKey = GetStreamKey();
	if (StreamingSubsystem->UpdateStreamConfig(StreamKey, NewConfig))
	{
		UE_LOG(LogRealGazeboStreaming, Log,
			TEXT("StreamingCamera: Updated settings from StreamManager - FrameRate: %d->%d, GOPSize: %d->%d, Dimensions: %dx%d"),
			OldFrameRate, CaptureFrameRate, OldGOPSize, GOPSize, NewConfig.Dimensions.X, NewConfig.Dimensions.Y);
	}
	else
	{
		UE_LOG(LogRealGazeboStreaming, Warning, TEXT("StreamingCamera: Failed to update pipeline config"));
	}
}

void URealGazeboStreamingCamera::InitializeStreaming()
{
	if (!StreamingSubsystem)
	{
		return;
	}

	// Get stream configuration from StreamManager (centralized control)
	// Find the StreamManager actor in the world - it provides centralized settings for all cameras
	ARealGazeboStreamManager* StreamManager = nullptr;
	if (UWorld* World = GetWorld())
	{
		// Find the first StreamManager actor in the level
		for (TActorIterator<ARealGazeboStreamManager> It(World); It; ++It)
		{
			StreamManager = *It;
			break;  // Use first found
		}
	}

	FRealGazeboStreamConfig Config;
	if (StreamManager)
	{
		// Get configuration from StreamManager (user-controllable at runtime)
		Config = StreamManager->GetDefaultStreamConfig();
		UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamingCamera: Using StreamManager settings"));
	}
	else
	{
		// Fallback to ultra-low latency defaults if no StreamManager in level
		// Only Resolution and FrameRate are configurable - everything else is auto-computed
		Config.Resolution = RealGazeboStreamingConstants::DEFAULT_RESOLUTION;
		Config.FrameRate = RealGazeboStreamingConstants::DEFAULT_FRAME_RATE;
		Config.UpdateComputedValues();  // Auto-compute bitrate, GOP, profile, etc.

		UE_LOG(LogRealGazeboStreaming, Warning,
			TEXT("StreamingCamera: No StreamManager found, using ultra-low latency defaults (720p @ 30fps, auto-computed bitrate/GOP)"));
	}

	// Update capture frame rate and GOP size from config
	CaptureFrameRate = Config.FPSValue;
	FrameCaptureInterval = 1.0f / static_cast<float>(CaptureFrameRate);
	GOPSize = Config.GOPSize;

	// CRITICAL DEBUG (2025-11-17): Log FPS configuration to diagnose 3fps vs 30fps issue
	UE_LOG(LogRealGazeboStreaming, Warning,
		TEXT("StreamingCamera: CAPTURE FPS CONFIG - FPSValue: %d | CaptureFrameRate: %d | FrameCaptureInterval: %.6f | GOP: %d"),
		Config.FPSValue, CaptureFrameRate, FrameCaptureInterval, GOPSize);

	// Acquire scene capture component from pool (optimization implemented 2025-11-03)
	// This reuses components instead of creating new UObjects, reducing allocation overhead
	// and garbage collection pressure during vehicle spawn/despawn operations.
	if (!SceneCaptureComponent)
	{
		AActor* Owner = GetOwner();
		if (!Owner)
		{
			UE_LOG(LogRealGazeboStreaming, Error, TEXT("StreamingCamera: No owner actor"));
			return;
		}

		// Acquire component from the subsystem's pool
		TSharedPtr<FRealGazeboSceneCapturePool> Pool = StreamingSubsystem->GetSceneCapturePool();
		if (!Pool.IsValid())
		{
			UE_LOG(LogRealGazeboStreaming, Error, TEXT("StreamingCamera: SceneCapture pool not available"));
			return;
		}

		SceneCaptureComponent = Pool->Acquire(Owner);
		if (!SceneCaptureComponent)
		{
			UE_LOG(LogRealGazeboStreaming, Error, TEXT("StreamingCamera: Failed to acquire scene capture component from pool"));
			return;
		}

		// Attach component to this camera component
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
	// Note: bAutoGenerateMips is set via InitCustomFormat's last parameter (already false above)
	RenderTarget->InitCustomFormat(Config.Dimensions.X, Config.Dimensions.Y, PF_B8G8R8A8, false);
	RenderTarget->ClearColor = FLinearColor::Black;
	RenderTarget->UpdateResource();

	// Configure scene capture
	SceneCaptureComponent->TextureTarget = RenderTarget;
	SceneCaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	SceneCaptureComponent->bCaptureEveryFrame = false;  // Manual capture via CaptureScene()
	SceneCaptureComponent->bCaptureOnMovement = false;

	// Disable VSM persistence to prevent pool overflow with 10+ cameras (2025-11-14).
	// Enabling this improves VSM caching but causes Virtual Shadow Map page pool overflow with many concurrent streams.
	// Trade-off: Slightly lower rendering performance vs. no VSM overflow.
	SceneCaptureComponent->bAlwaysPersistRenderingState = false;

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

	// Release scene capture component back to pool (optimization implemented 2025-11-03)
	// Instead of destroying the component, we return it to the pool for reuse by other cameras.
	// This reduces UObject allocation overhead and garbage collection pressure.
	if (SceneCaptureComponent)
	{
		// Clear the texture target before releasing
		SceneCaptureComponent->TextureTarget = nullptr;

		// Return component to pool for reuse
		TSharedPtr<FRealGazeboSceneCapturePool> Pool = StreamingSubsystem->GetSceneCapturePool();
		if (Pool.IsValid())
		{
			Pool->Release(SceneCaptureComponent);
		}
		else
		{
			// Fallback: Destroy component if pool is not available
			UE_LOG(LogRealGazeboStreaming, Warning, TEXT("StreamingCamera: Pool unavailable, destroying component"));
			SceneCaptureComponent->DestroyComponent();
		}

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
	// Get vehicle ID from owning VehicleBasePawn
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
			TEXT("StreamingCamera: Owner is not a VehicleBasePawn (%s). Attach this component to a VehicleBasePawn to enable auto-detection."),
			*OwnerActor->GetClass()->GetName());
		return false;
	}

	// Get vehicle ID from pawn
	OutVehicleID = VehiclePawn->VehicleID;

	// Construct vehicle display name from DataTable config (same logic as SetVehicleDisplayName())
	// This ensures consistent naming even if SetVehicleDisplayName() hasn't been called yet
	// Format: "vehiclename_vehicleID" (e.g., "iris_1", "x500_2", "lc_62_1")
	if (const UGazeboBridgeSubsystem* BridgeSubsystem = UGazeboBridgeSubsystem::GetBridgeSubsystem(this))
	{
		if (const FBridgeVehicleConfigRow* Config = BridgeSubsystem->GetVehicleConfigInternal(VehiclePawn->VehicleType))
		{
			const FString VehicleNameLower = Config->VehicleName.ToLower();
			DetectedVehicleTypeName = FString::Printf(TEXT("%s_%d"), *VehicleNameLower, OutVehicleID.VehicleNum);

			UE_LOG(LogRealGazeboStreaming, Log, TEXT("StreamingCamera: Detected Vehicle from DataTable: %s (VehicleID: %s, Type: %d)"),
				*DetectedVehicleTypeName, *OutVehicleID.ToString(), VehiclePawn->VehicleType);
		}
		else
		{
			// Fallback if no config found
			DetectedVehicleTypeName = FString::Printf(TEXT("vehicle_%d_%d"), VehiclePawn->VehicleType, OutVehicleID.VehicleNum);
			UE_LOG(LogRealGazeboStreaming, Warning,
				TEXT("StreamingCamera: No vehicle config found for type %d, using fallback name: %s"),
				VehiclePawn->VehicleType, *DetectedVehicleTypeName);
		}
	}
	else
	{
		// Fallback if subsystem not available
		DetectedVehicleTypeName = FString::Printf(TEXT("vehicle_%d_%d"), VehiclePawn->VehicleType, OutVehicleID.VehicleNum);
		UE_LOG(LogRealGazeboStreaming, Warning,
			TEXT("StreamingCamera: Bridge subsystem not available, using fallback name: %s"),
			*DetectedVehicleTypeName);
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

	// Check encoder backpressure before capturing to avoid wasting game thread time when queues are full.
	// Checks both encoding and RTSP queues (improved 2025-11-07).
	FStreamKey StreamKey = GetStreamKey();

	// CRITICAL DEBUG (2025-11-17): Log StreamKey hash on EVERY frame to detect stream mixing
	// If both cameras show the same hash, they're pushing frames to the SAME NAL queue (stream mixing)
	// If hashes are different, stream isolation is working correctly
	const uint32 StreamKeyHash = GetTypeHash(StreamKey);
	if ((FrameSequence % 30) == 0)  // Log every 30 frames (1 second at 30fps)
	{
		UE_LOG(LogRealGazeboStreaming, Warning,
			TEXT("STREAM ISOLATION CHECK: Frame %llu | %s | Hash=0x%08X"),
			FrameSequence, *StreamKey.ToDebugString(), StreamKeyHash);
	}

	if (StreamingSubsystem->IsStreamBackpressured(StreamKey))
	{
		// During backpressure, skip non-keyframes to reduce load while maintaining stream health.
		const bool bIsKeyFrame = (FrameSequence % GOPSize) == 0;
		if (!bIsKeyFrame)
		{
			FrameSequence++;
			return;
		}
		// Keyframes must always be captured to maintain stream stability.
		UE_LOG(LogRealGazeboStreaming, Log,
			TEXT("StreamingCamera [%s]: Capturing keyframe %llu despite backpressure"),
			*StreamKey.ToString(), FrameSequence);
	}

	// Request keyframe at GOP intervals for stream seekability and error recovery.
	if ((FrameSequence % GOPSize) == 0 && FrameSequence > 0)
	{
		StreamingSubsystem->RequestKeyFrame(StreamKey);
	}

	// Capture scene to render target
	SceneCaptureComponent->CaptureScene();

	// Delay streaming until scene has loaded to prevent clients from seeing black frames (2025-11-12).
	// First 10 frames test scene geometry rendering. After 10 frames, assume scene is ready (timeout fallback).
	if (FrameSequence < 10 && !bSceneReady)
	{
		if (FrameSequence == 9)
		{
			bSceneReady = true;
			UE_LOG(LogRealGazeboStreaming, Log,
				TEXT("StreamingCamera [%s]: Scene ready after %llu test frames"),
				*StreamKey.ToString(), FrameSequence + 1);
		}
		else
		{
			FrameSequence++;
			return;
		}
	}

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

	// Metadata with microsecond-precision timing (UE5.1 AVEncoder standard)
	const uint64 CurrentFrameNumber = FrameSequence;
	const int64 CaptureStartTimeUs = RealGazeboStreamingTime::GetTimeMicroseconds();

	// Get render target resource ON GAME THREAD
	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		UE_LOG(LogRealGazeboStreaming, Error, TEXT("StreamingCamera: No render target resource"));
		return;
	}

	// Use RDG-based texture copy for ~5-8% CPU reduction and 1-2ms latency improvement.
	// RDG eliminates background thread spawning and allows automatic batching (2025-11-03).

	// Capture subsystem as raw pointer for thread safety
	URealGazeboStreamingSubsystem* SubsystemPtr = StreamingSubsystem;

	ENQUEUE_RENDER_COMMAND(CaptureTextureFrameRDG)(
		[SubsystemPtr, StreamKey, RTResource, CaptureStartTimeUs, CurrentFrameNumber](FRHICommandListImmediate& RHICmdList)
		{
			if (!RTResource || !SubsystemPtr)
			{
				return;
			}

			// Get the source RHI texture from the render target.
			FTexture2DRHIRef SourceTexture = RTResource->GetRenderTargetTexture();
			if (!SourceTexture.IsValid())
			{
				return;
			}

			// Create an RDG builder to manage the texture copy operation.
			FRDGBuilder GraphBuilder(RHICmdList);

			// Register the source texture as an external RDG texture resource.
			FRDGTextureRef SourceRDGTexture = GraphBuilder.RegisterExternalTexture(
				CreateRenderTarget(SourceTexture, TEXT("StreamingCameraSource")));

			// Create an output texture descriptor with flags compatible with hardware encoders.
			FRDGTextureDesc OutputDesc = FRDGTextureDesc::Create2D(
				SourceRDGTexture->Desc.Extent,
				PF_B8G8R8A8,
				FClearValueBinding::None,
				TexCreate_RenderTargetable | TexCreate_ShaderResource);

			// Add platform-specific flags required for hardware encoder interoperability.
			const ERHIInterfaceType RHIType = RHIGetInterfaceType();
			if (RHIType == ERHIInterfaceType::Vulkan)
			{
				// On Linux with Vulkan, use external memory for NVENC interop.
				OutputDesc.Flags |= TexCreate_External;
			}
			else if (RHIType == ERHIInterfaceType::D3D11 || RHIType == ERHIInterfaceType::D3D12)
			{
				// On Windows with DirectX, use shared handles for NVENC and AMF.
				OutputDesc.Flags |= TexCreate_Shared;
			}

			FRDGTextureRef OutputRDGTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("StreamingCameraOutput"));

			// Add a texture copy pass. The RDG system will automatically batch this with other
			// operations in the same frame if multiple cameras are capturing simultaneously.
			AddCopyTexturePass(GraphBuilder, SourceRDGTexture, OutputRDGTexture, FRHICopyTextureInfo());

			// Queue the output texture for extraction to a pooled render target.
			TRefCountPtr<IPooledRenderTarget> OutputRT;
			GraphBuilder.QueueTextureExtraction(OutputRDGTexture, &OutputRT);

			// Execute the RDG graph. This will be batched if multiple cameras capture in the same frame.
			GraphBuilder.Execute();

			// Extract the final RHI texture for submission to the encoder.
			// Use GetRHI() instead of deprecated GetRenderTargetItem()
			FTexture2DRHIRef OutputTexture = OutputRT->GetRHI()->GetTexture2D();

			// Submit texture directly to encoder queue without GPU fence synchronization.
			// CRITICAL DESIGN (verified 2025-11-15):
			// - GPU fence polling caused 50ms timeouts under multi-stream load (render thread stalls)
			// - Old working system had NO GPU fence - relied on natural GPU command ordering
			// - NVENC/AMF handle DMA synchronization internally via their own GPU fences
			// - UE5.1 AVEncoder already manages texture lifetime via TSharedPtr reference counting
			//
			// WHY THIS IS SAFE:
			// 1. GraphBuilder.Execute() submits RDG pass to GPU command queue
			// 2. SubmitTextureFrame() increments texture TSharedPtr refcount
			// 3. Encoder accesses texture AFTER RDG pass completes (GPU command ordering)
			// 4. NVENC/AMF insert their own GPU waits before DMA copy (hardware encoder sync)
			// 5. Texture is released only after encoder finishes (refcount decrements in OnEncodedPacket callback)
			//
			// PERFORMANCE: Eliminates 50ms render thread stalls, allows true async streaming
			SubsystemPtr->SubmitTextureFrame(StreamKey, OutputTexture, CaptureStartTimeUs, CurrentFrameNumber);
		});
	FrameSequence++;

	// Log capture progress periodically with StreamKey for isolation verification
	if ((FrameSequence % 100) == 0)
	{
		UE_LOG(LogRealGazeboStreaming, Log,
			TEXT("StreamingCamera [%s]: Captured %llu frames"), *StreamKey.ToString(), FrameSequence);
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
			TEXT("Queue: E=%d R=%d\n")
			TEXT("Bitrate: %.2f Mbps\n")
			TEXT("Encoded: %lld frames"),
			Stats.EncodingQueueDepth,
			Stats.RTSPQueueDepth,
			Stats.CurrentBitrateMbps,
			Stats.TotalFramesEncoded
		);
	}

	GEngine->AddOnScreenDebugMessage(
		static_cast<int32>(GetUniqueID()),  // Cast to int32 for UE5.1
		0.0f,  // No timeout (updated every frame)
		StatusColor,
		DebugText
	);
}
