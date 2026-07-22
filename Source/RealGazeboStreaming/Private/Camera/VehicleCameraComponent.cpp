// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#include "Camera/VehicleCameraComponent.h"
#include "Core/RealGazeboStreamingSubsystem.h"
#include "Vehicles/VehicleBasePawn.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

UVehicleCameraComponent::UVehicleCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	bCaptureEveryFrame = false;
	bCaptureOnMovement = false;
}

void UVehicleCameraComponent::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogTemp, Log, TEXT("VehicleCameraComponent: BeginPlay - polling for vehicle activation..."));
}

void UVehicleCameraComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopStreaming();
	UnregisterFromSubsystem();
	Super::EndPlay(EndPlayReason);
	UE_LOG(LogTemp, Log, TEXT("VehicleCameraComponent: EndPlay for stream %s"), *GetStreamIdentifier().ToString());
}

void UVehicleCameraComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                            FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bIsInitialized)
	{
		TryPopulateVehicleID();
		AVehicleBasePawn* VehiclePawn = Cast<AVehicleBasePawn>(GetOwner());
		const bool bVehicleReady = VehiclePawn && VehiclePawn->IsActiveVehicle();
		if (bVehicleReady && bEnableStreaming)
		{
			RegisterWithSubsystem();
			if (StartStreaming())
			{
				UE_LOG(LogTemp, Log, TEXT("VehicleCameraComponent: Stream auto-started for %s"), *GetStreamIdentifier().ToString());
			}
			bIsInitialized = true;
			PrimaryComponentTick.SetTickFunctionEnable(false);
		}
	}
}

FStreamIdentifier UVehicleCameraComponent::GetStreamIdentifier() const
{
	return FStreamIdentifier(VehicleID, CameraID, VehicleTypeName);
}

bool UVehicleCameraComponent::StartStreaming()
{
	if (bIsStreaming) return true;
	UWorld* World = GetWorld();
	if (!World || !World->GetGameInstance())
	{
		UE_LOG(LogTemp, Error, TEXT("VehicleCameraComponent: World or GameInstance not available"));
		return false;
	}
	URealGazeboStreamingSubsystem* Subsystem = World->GetGameInstance()->GetSubsystem<URealGazeboStreamingSubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("VehicleCameraComponent: Streaming subsystem not available"));
		return false;
	}

	// The subsystem ticker is currently owned by the RTSP server lifecycle.  Start
	// that backbone automatically so offscreen/headless camera streams render even
	// when the operator never opened the streaming UI. STANAG still consumes the
	// same single hardware encode through the fan-out layer.
	if (!Subsystem->IsRTSPServerRunning())
	{
		int32 RTSPPort = 8554;
		FParse::Value(FCommandLine::Get(), TEXT("RealGazeboRTSPPort="), RTSPPort);
		if (!Subsystem->StartRTSPServer(RTSPPort))
		{
			UE_LOG(LogTemp, Error, TEXT("VehicleCameraComponent: Failed to auto-start RTSP/ticker backbone on port %d"), RTSPPort);
			return false;
		}
	}
	return Subsystem->CreateStream(this);
}

void UVehicleCameraComponent::StopStreaming()
{
	if (!bIsStreaming) return;
	UWorld* World = GetWorld();
	if (World && World->GetGameInstance())
	{
		if (URealGazeboStreamingSubsystem* Subsystem = World->GetGameInstance()->GetSubsystem<URealGazeboStreamingSubsystem>())
		{
			Subsystem->DestroyStream(GetStreamIdentifier());
		}
	}
	bIsStreaming = false;
	RTSPURL = TEXT("");
}

void UVehicleCameraComponent::TryPopulateVehicleID()
{
	AActor* Owner = GetOwner();
	if (!Owner) return;
	AVehicleBasePawn* VehiclePawn = Cast<AVehicleBasePawn>(Owner);
	if (VehiclePawn && VehiclePawn->IsActiveVehicle())
	{
		VehicleID = VehiclePawn->VehicleID;
		VehicleTypeName = VehiclePawn->VehicleTypeName;
	}
}

void UVehicleCameraComponent::RegisterWithSubsystem()
{
	UWorld* World = GetWorld();
	if (!World || !World->GetGameInstance()) return;
	if (URealGazeboStreamingSubsystem* Subsystem = World->GetGameInstance()->GetSubsystem<URealGazeboStreamingSubsystem>())
	{
		Subsystem->RegisterCamera(this);
	}
}

void UVehicleCameraComponent::UnregisterFromSubsystem()
{
	UWorld* World = GetWorld();
	if (!World || !World->GetGameInstance()) return;
	if (URealGazeboStreamingSubsystem* Subsystem = World->GetGameInstance()->GetSubsystem<URealGazeboStreamingSubsystem>())
	{
		Subsystem->UnregisterCamera(this);
	}
}
