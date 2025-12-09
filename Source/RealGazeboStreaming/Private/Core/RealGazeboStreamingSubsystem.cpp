// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#include "Core/RealGazeboStreamingSubsystem.h"
#include "Camera/VehicleCameraComponent.h"
#include "Pipeline/StreamingPipeline.h"
#include "RTSP/RTSPServer.h"
#include "Engine/World.h"
#include "Containers/Ticker.h"

//----------------------------------------------------------
// USubsystem Interface
//----------------------------------------------------------

void URealGazeboStreamingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingSubsystem: Initializing..."));

	// DefaultStreamConfig already defaults to XGA 1024x768 @ 30fps via member initialization

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingSubsystem: Initialized"));
}

void URealGazeboStreamingSubsystem::Deinitialize()
{
	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingSubsystem: Deinitializing..."));

	DestroyAllStreams();
	StopRTSPServer();
	UnbindFromWorldTick();

	Super::Deinitialize();

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingSubsystem: Deinitialized"));
}

//----------------------------------------------------------
// RTSP Server Management
//----------------------------------------------------------

bool URealGazeboStreamingSubsystem::StartRTSPServer(int32 Port)
{
	if (RTSPServer && RTSPServer->IsRunning())
	{
		UE_LOG(LogTemp, Warning, TEXT("RealGazeboStreamingSubsystem: RTSP server already running"));
		return true;
	}

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingSubsystem: Starting RTSP server on port %d..."), Port);

	RTSPServer = MakeShared<FRTSPServerWrapper>();

	FString ErrorMessage;
	if (!RTSPServer->Start(Port, &ErrorMessage))
	{
		UE_LOG(LogTemp, Error, TEXT("RealGazeboStreamingSubsystem: Failed to start RTSP server - %s"), *ErrorMessage);
		RTSPServer.Reset();
		return false;
	}

	// Start capturing frames: Register with engine ticker for per-frame updates
	BindToWorldTick();

	OnStreamingStarted.Broadcast();

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingSubsystem: RTSP server started on rtsp://localhost:%d"), Port);
	return true;
}

void URealGazeboStreamingSubsystem::StopRTSPServer()
{
	if (!RTSPServer)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingSubsystem: Stopping RTSP server..."));

	DestroyAllStreams();

	RTSPServer->Stop();
	RTSPServer.Reset();

	UnbindFromWorldTick();

	OnStreamingStopped.Broadcast();

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingSubsystem: RTSP server stopped"));
}

bool URealGazeboStreamingSubsystem::IsRTSPServerRunning() const
{
	return RTSPServer && RTSPServer->IsRunning();
}

//----------------------------------------------------------
// Stream Management
//----------------------------------------------------------

bool URealGazeboStreamingSubsystem::CreateStream(UVehicleCameraComponent* Camera)
{
	return CreateStreamWithConfig(Camera, DefaultStreamConfig);
}

bool URealGazeboStreamingSubsystem::CreateStreamWithConfig(UVehicleCameraComponent* Camera, const FStreamConfig& Config)
{
	if (!Camera)
	{
		UE_LOG(LogTemp, Error, TEXT("RealGazeboStreamingSubsystem: Cannot create stream - null camera"));
		return false;
	}

	if (!IsRTSPServerRunning())
	{
		UE_LOG(LogTemp, Error, TEXT("RealGazeboStreamingSubsystem: Cannot create stream - RTSP server not running"));
		return false;
	}

	const FStreamIdentifier StreamID = Camera->GetStreamIdentifier();

	// Check if stream already exists
	if (ActiveStreams.Contains(StreamID))
	{
		UE_LOG(LogTemp, Warning, TEXT("RealGazeboStreamingSubsystem: Stream already exists: %s"), *StreamID.ToString());
		return true;
	}

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingSubsystem: Creating stream %s..."), *StreamID.ToString());

	// Create isolated streaming pipeline: Encoder + Frame Pool + NAL Queue + RTSP Source
	TSharedPtr<FStreamingPipeline> Pipeline = MakeShared<FStreamingPipeline>(StreamID, Camera, RTSPServer);

	FString ErrorMessage;
	if (!Pipeline->Initialize(Config, ErrorMessage))
	{
		UE_LOG(LogTemp, Error, TEXT("RealGazeboStreamingSubsystem: Failed to initialize pipeline - %s"), *ErrorMessage);
		return false;
	}

	// Start encoding and register with RTSP server
	FString RTSPURL;
	if (!Pipeline->Start(RTSPURL))
	{
		UE_LOG(LogTemp, Error, TEXT("RealGazeboStreamingSubsystem: Failed to start stream"));
		return false;
	}

	// Update camera component with runtime status
	Camera->bIsStreaming = true;
	Camera->RTSPURL = RTSPURL;

	// Register pipeline in active streams map
	ActiveStreams.Add(StreamID, Pipeline);

	// Broadcast event
	OnStreamCreated.Broadcast(StreamID.VehicleID, StreamID.CameraID, RTSPURL);

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingSubsystem: Stream created - %s"), *RTSPURL);
	return true;
}

void URealGazeboStreamingSubsystem::DestroyStream(const FStreamIdentifier& StreamID)
{
	TSharedPtr<FStreamingPipeline>* Pipeline = ActiveStreams.Find(StreamID);
	if (!Pipeline)
	{
		UE_LOG(LogTemp, Warning, TEXT("RealGazeboStreamingSubsystem: Stream not found: %s"), *StreamID.ToString());
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingSubsystem: Destroying stream %s..."), *StreamID.ToString());

	// Stop and shutdown pipeline
	(*Pipeline)->Stop();
	(*Pipeline)->Shutdown();

	// Remove from map
	ActiveStreams.Remove(StreamID);

	// Broadcast event
	OnStreamDestroyed.Broadcast(StreamID.VehicleID, StreamID.CameraID);

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingSubsystem: Stream destroyed: %s"), *StreamID.ToString());
}

void URealGazeboStreamingSubsystem::DestroyAllStreams()
{
	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingSubsystem: Destroying all streams (%d active)..."), ActiveStreams.Num());

	TArray<FStreamIdentifier> StreamIDs;
	ActiveStreams.GetKeys(StreamIDs);

	for (const FStreamIdentifier& StreamID : StreamIDs)
	{
		DestroyStream(StreamID);
	}

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingSubsystem: All streams destroyed"));
}

//----------------------------------------------------------
// Camera Registration
//----------------------------------------------------------

void URealGazeboStreamingSubsystem::RegisterCamera(UVehicleCameraComponent* Camera)
{
	if (!Camera)
	{
		return;
	}

	RegisteredCameras.AddUnique(Camera);

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingSubsystem: Registered camera %s"), *Camera->GetStreamIdentifier().ToString());
}

void URealGazeboStreamingSubsystem::UnregisterCamera(UVehicleCameraComponent* Camera)
{
	if (!Camera)
	{
		return;
	}

	RegisteredCameras.Remove(Camera);

	// Destroy stream if active
	DestroyStream(Camera->GetStreamIdentifier());

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingSubsystem: Unregistered camera %s"), *Camera->GetStreamIdentifier().ToString());
}

//----------------------------------------------------------
// Query
//----------------------------------------------------------

FStreamInfo URealGazeboStreamingSubsystem::GetStreamInfo(const FStreamIdentifier& StreamID) const
{
	const TSharedPtr<FStreamingPipeline>* Pipeline = ActiveStreams.Find(StreamID);
	if (Pipeline)
	{
		return (*Pipeline)->GetStreamInfo();
	}

	return FStreamInfo(); // Empty info
}

TArray<FStreamIdentifier> URealGazeboStreamingSubsystem::GetActiveStreams() const
{
	TArray<FStreamIdentifier> StreamIDs;
	ActiveStreams.GetKeys(StreamIDs);
	return StreamIDs;
}

int32 URealGazeboStreamingSubsystem::GetActiveStreamCount() const
{
	return ActiveStreams.Num();
}

//----------------------------------------------------------
// Configuration
//----------------------------------------------------------

void URealGazeboStreamingSubsystem::SetDefaultStreamConfig(const FStreamConfig& Config)
{
	DefaultStreamConfig = Config;
	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingSubsystem: Default config updated to %s @ %dfps"),
		*StreamResolutionToString(Config.Resolution), Config.GetFrameRateValue());
}

//----------------------------------------------------------
// Internal
//----------------------------------------------------------

bool URealGazeboStreamingSubsystem::TickStreams(float DeltaTime)
{
	// Capture frames for all active streams
	for (auto& Pair : ActiveStreams)
	{
		if (Pair.Value)
		{
			Pair.Value->CaptureFrame();
		}
	}

	// Return true to continue ticking
	return true;
}

void URealGazeboStreamingSubsystem::BindToWorldTick()
{
	if (TickDelegateHandle.IsValid())
	{
		return;
	}

	// Use FTSTicker for UE 5.1 (OnWorldTickStart doesn't exist)
	TickDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &URealGazeboStreamingSubsystem::TickStreams)
	);

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingSubsystem: Bound to ticker"));
}

void URealGazeboStreamingSubsystem::UnbindFromWorldTick()
{
	if (!TickDelegateHandle.IsValid())
	{
		return;
	}

	FTSTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
	TickDelegateHandle.Reset();
	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingSubsystem: Unbound from ticker"));
}
