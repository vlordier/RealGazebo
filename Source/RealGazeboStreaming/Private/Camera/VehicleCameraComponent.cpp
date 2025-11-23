// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Camera/VehicleCameraComponent.h"
#include "Core/RealGazeboStreamingSubsystem.h"
#include "Vehicles/VehicleBasePawn.h"

UVehicleCameraComponent::UVehicleCameraComponent()
{
	// Enable ticking for polling vehicle activation
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	bCaptureEveryFrame = false;
	bCaptureOnMovement = false;
}

void UVehicleCameraComponent::BeginPlay()
{
	Super::BeginPlay();

	// Deferred initialization via polling in TickComponent.
	// Vehicle pooling sets VehicleID AFTER BeginPlay.
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

	// POLLING: Check if vehicle is active and initialize streaming
	if (!bIsInitialized)
	{
		TryPopulateVehicleID();

		// Check if VehicleID is valid (not 0_0)
		const bool bValidVehicleID = (VehicleID.VehicleType != 0 || VehicleID.VehicleNum != 0);

		if (bValidVehicleID && bEnableStreaming)
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

			// Disable ticking after initialization
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
	if (VehiclePawn)
	{
		// Only use if VehicleID is valid (not 0_0)
		if (VehiclePawn->VehicleID.VehicleNum != 0 || VehiclePawn->VehicleID.VehicleType != 0)
		{
			VehicleID = VehiclePawn->VehicleID;
			VehicleTypeName = VehiclePawn->VehicleTypeName;
		}
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
