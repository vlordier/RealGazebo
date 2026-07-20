// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#include "Camera/VehicleCameraComponent.h"
#include "Core/RealGazeboStreamingSubsystem.h"
#include "Vehicles/VehicleBasePawn.h"

UVehicleCameraComponent::UVehicleCameraComponent()
{
	// Enable ticking to poll for vehicle activation (VehicleID detection)
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// Manual capture mode: StreamingPipeline controls frame timing to match target FPS
	bCaptureEveryFrame = false;
	bCaptureOnMovement = false;

	// Persist rendering state to enable Virtual Shadow Map caching (UE5 optimization)
	//bAlwaysPersistRenderingState = true;
}

void UVehicleCameraComponent::BeginPlay()
{
	Super::BeginPlay();

	// Deferred initialization: VehicleID will be detected via polling in TickComponent.
	// Vehicle pooling system assigns VehicleID AFTER BeginPlay completes.
	UE_LOG(LogTemp, Log, TEXT("VehicleCameraComponent: BeginPlay - polling for vehicle activation..."));
}

void UVehicleCameraComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopStreaming();
	UnregisterFromSubsystem();

	Super::EndPlay(EndPlayReason);

	UE_LOG(LogTemp, Log, TEXT("VehicleCameraComponent: EndPlay for stream %s"),
		*GetStreamIdentifier().ToString());
}

void UVehicleCameraComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                           FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// POLLING LOOP: Wait for vehicle activation and auto-start streaming
	if (!bIsInitialized)
	{
		TryPopulateVehicleID();

		// Check if owning vehicle is active (not in pool)
		AVehicleBasePawn* VehiclePawn = Cast<AVehicleBasePawn>(GetOwner());
		const bool bVehicleReady = VehiclePawn && VehiclePawn->IsActiveVehicle();

		if (bVehicleReady && bEnableStreaming)
		{
			UE_LOG(LogTemp, Log, TEXT("VehicleCameraComponent: Vehicle activated (VehicleID: %s) - initializing stream"),
				*VehicleID.ToString());

			RegisterWithSubsystem();

			if (StartStreaming())
			{
				UE_LOG(LogTemp, Log, TEXT("VehicleCameraComponent: Stream auto-started for %s"),
					*GetStreamIdentifier().ToString());
			}

			bIsInitialized = true;

			// Stop ticking: Initialization complete, no longer need to poll
			PrimaryComponentTick.SetTickFunctionEnable(false);
		}
	}
}

//----------------------------------------------------------
// Public API
//----------------------------------------------------------

FStreamIdentifier UVehicleCameraComponent::GetStreamIdentifier() const
{
	return FStreamIdentifier(VehicleID, CameraID, VehicleTypeName);
}

bool UVehicleCameraComponent::StartStreaming()
{
	if (bIsStreaming)
	{
		return true;
	}

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

	return Subsystem->CreateStream(this);
}

void UVehicleCameraComponent::StopStreaming()
{
	if (!bIsStreaming)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World && World->GetGameInstance())
	{
		URealGazeboStreamingSubsystem* Subsystem = World->GetGameInstance()->GetSubsystem<URealGazeboStreamingSubsystem>();
		if (Subsystem)
		{
			Subsystem->DestroyStream(GetStreamIdentifier());
		}
	}

	bIsStreaming = false;
	RTSPURL = TEXT("");
}

//----------------------------------------------------------
// Internal
//----------------------------------------------------------

void UVehicleCameraComponent::TryPopulateVehicleID()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	AVehicleBasePawn* VehiclePawn = Cast<AVehicleBasePawn>(Owner);
	if (VehiclePawn && VehiclePawn->IsActiveVehicle())
	{
		// Copy VehicleID when vehicle is active (not in pool)
		// Note: 0_0 is a valid VehicleID (Type 0, Num 0)
		VehicleID = VehiclePawn->VehicleID;
		VehicleTypeName = VehiclePawn->VehicleTypeName;
	}
}

void UVehicleCameraComponent::RegisterWithSubsystem()
{
	UWorld* World = GetWorld();
	if (!World || !World->GetGameInstance())
	{
		return;
	}

	URealGazeboStreamingSubsystem* Subsystem = World->GetGameInstance()->GetSubsystem<URealGazeboStreamingSubsystem>();
	if (Subsystem)
	{
		Subsystem->RegisterCamera(this);
	}
}

void UVehicleCameraComponent::UnregisterFromSubsystem()
{
	UWorld* World = GetWorld();
	if (!World || !World->GetGameInstance())
	{
		return;
	}

	URealGazeboStreamingSubsystem* Subsystem = World->GetGameInstance()->GetSubsystem<URealGazeboStreamingSubsystem>();
	if (Subsystem)
	{
		Subsystem->UnregisterCamera(this);
	}
}
