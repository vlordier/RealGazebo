// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Core/RealGazeboStreamingManager.h"
#include "Core/RealGazeboStreamingSubsystem.h"
#include "Camera/VehicleCameraComponent.h"

ARealGazeboStreamingManager::ARealGazeboStreamingManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 1.0f; // Update stats every second

	// Set default calculated bitrate
	UpdateCalculatedBitrate();
}

void ARealGazeboStreamingManager::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingManager: BeginPlay"));

	// Update bitrate display
	UpdateCalculatedBitrate();

	// Auto-start streaming if enabled
	if (bAutoStartStreaming)
	{
		StartStreaming();
	}
}

void ARealGazeboStreamingManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingManager: EndPlay"));

	StopStreaming();

	Super::EndPlay(EndPlayReason);
}

void ARealGazeboStreamingManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Update runtime stats
	URealGazeboStreamingSubsystem* Subsystem = GetStreamingSubsystem();
	if (Subsystem)
	{
		bIsServerRunning = Subsystem->IsRTSPServerRunning();
		ActiveStreamCount = Subsystem->GetActiveStreamCount();
	}
}

//----------------------------------------------------------
// API
//----------------------------------------------------------

bool ARealGazeboStreamingManager::StartStreaming()
{
	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingManager: Starting streaming..."));

	URealGazeboStreamingSubsystem* Subsystem = GetStreamingSubsystem();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("RealGazeboStreamingManager: Streaming subsystem not available"));
		return false;
	}

	// Set default configuration in subsystem
	FStreamConfig DefaultConfig = GetDefaultStreamConfig();
	Subsystem->SetDefaultStreamConfig(DefaultConfig);

	// Start RTSP server
	if (!Subsystem->StartRTSPServer(RTSPPort))
	{
		UE_LOG(LogTemp, Error, TEXT("RealGazeboStreamingManager: Failed to start RTSP server"));
		return false;
	}

	// Start all camera streams
	StartAllCameraStreams();

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingManager: Streaming started on rtsp://localhost:%d"), RTSPPort);
	return true;
}

void ARealGazeboStreamingManager::StopStreaming()
{
	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingManager: Stopping streaming..."));

	URealGazeboStreamingSubsystem* Subsystem = GetStreamingSubsystem();
	if (Subsystem)
	{
		Subsystem->StopRTSPServer();
	}

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingManager: Streaming stopped"));
}

FStreamConfig ARealGazeboStreamingManager::GetDefaultStreamConfig() const
{
	FStreamConfig Config;
	Config.Resolution = DefaultResolution;
	Config.FrameRate = DefaultFrameRate;

	// All other settings are hardcoded for ultra-low latency:
	// - Preset: UltraLowLatency
	// - Profile: Baseline
	// - bZeroCopy: true
	// - Bitrate: Auto-calculated via Config.GetBitrate() based on resolution + FPS
	// - GOP: Auto-calculated via Config.GetGOPSize() as FPS/2 for 0.5s keyframe interval

	return Config;
}

TArray<FString> ARealGazeboStreamingManager::GetActiveStreamURLs() const
{
	TArray<FString> URLs;

	URealGazeboStreamingSubsystem* Subsystem = GetStreamingSubsystem();
	if (!Subsystem)
	{
		return URLs;
	}

	TArray<FStreamIdentifier> ActiveStreams = Subsystem->GetActiveStreams();
	for (const FStreamIdentifier& StreamID : ActiveStreams)
	{
		FStreamInfo Info = Subsystem->GetStreamInfo(StreamID);
		if (!Info.RTSPURL.IsEmpty())
		{
			URLs.Add(Info.RTSPURL);
		}
	}

	return URLs;
}

//----------------------------------------------------------
// Internal
//----------------------------------------------------------

URealGazeboStreamingSubsystem* ARealGazeboStreamingManager::GetStreamingSubsystem() const
{
	UWorld* World = GetWorld();
	if (!World || !World->GetGameInstance())
	{
		return nullptr;
	}

	return World->GetGameInstance()->GetSubsystem<URealGazeboStreamingSubsystem>();
}

void ARealGazeboStreamingManager::UpdateCalculatedBitrate()
{
	FStreamConfig Config = GetDefaultStreamConfig();
	CalculatedBitrate = Config.GetBitrate();

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingManager: Auto-calculated bitrate = %d kbps (%s @ %dfps)"),
		CalculatedBitrate,
		*StreamResolutionToString(DefaultResolution),
		static_cast<int32>(DefaultFrameRate));
}

void ARealGazeboStreamingManager::StartAllCameraStreams()
{
	URealGazeboStreamingSubsystem* Subsystem = GetStreamingSubsystem();
	if (!Subsystem || !Subsystem->IsRTSPServerRunning())
	{
		return;
	}

	// Get all registered cameras from subsystem
	TArray<TWeakObjectPtr<UVehicleCameraComponent>> RegisteredCameras = Subsystem->GetRegisteredCameras();

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingManager: Starting streams for %d registered cameras..."),
		RegisteredCameras.Num());

	int32 StartedCount = 0;
	int32 SkippedCount = 0;

	// Create stream for each registered camera that has streaming enabled
	for (const TWeakObjectPtr<UVehicleCameraComponent>& CameraPtr : RegisteredCameras)
	{
		if (UVehicleCameraComponent* Camera = CameraPtr.Get())
		{
			if (Camera->bEnableStreaming)
			{
				// Use default configuration from manager
				if (Subsystem->CreateStream(Camera))
				{
					StartedCount++;
					UE_LOG(LogTemp, Log, TEXT("  Started stream: %s"), *Camera->GetStreamIdentifier().ToString());
				}
			}
			else
			{
				SkippedCount++;
				UE_LOG(LogTemp, Log, TEXT("  Skipped (streaming disabled): %s"), *Camera->GetStreamIdentifier().ToString());
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreamingManager: Stream startup complete - Started: %d, Skipped: %d"),
		StartedCount, SkippedCount);
}
