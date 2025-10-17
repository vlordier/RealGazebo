// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "ViewerController/RealGazeboViewerDirector.h"
#include "ViewerController/RealGazeboBlueprintLib.h"
#include "ViewerController/RealGazeboCameraControllerComponent.h"
#include "Vehicles/VehicleBasePawn.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "RealGazeboUI.h"

ARealGazeboViewerDirector::ARealGazeboViewerDirector()
{
    PrimaryActorTick.bCanEverTick = false; // No longer need ticking

    // Initialize state - start with no specific mode until user input
    CurrentMode = ERealGazeboViewerMode::Manual; // Default to manual but don't use original pawn yet
    CurrentVehicle = nullptr;
    OriginalPawn = nullptr;
}

void ARealGazeboViewerDirector::BeginPlay()
{
    Super::BeginPlay();

    // Store the original UE5 DefaultPawn immediately
    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    if (PC && PC->GetPawn())
    {
        OriginalPawn = PC->GetPawn();
        UE_LOG(LogRealGazeboUI, Log, TEXT("ViewerDirector: Stored original UE5 DefaultPawn: %s"), *OriginalPawn->GetName());

        // Disable collision immediately so manual camera can fly through walls freely
        OriginalPawn->SetActorEnableCollision(false);
        if (UPrimitiveComponent* RootComp = Cast<UPrimitiveComponent>(OriginalPawn->GetRootComponent()))
        {
            RootComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            RootComp->SetCollisionResponseToAllChannels(ECR_Ignore);
            UE_LOG(LogRealGazeboUI, Log, TEXT("ViewerDirector: Disabled DefaultPawn collision for free flight"));
        }
    }

    InitializeForBeginPlay();
}

void ARealGazeboViewerDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Restore original pawn visibility and possession if needed
    if (OriginalPawn)
    {
        OriginalPawn->SetActorHiddenInGame(false);
        SwitchPlayerPawn(OriginalPawn);
        UE_LOG(LogRealGazeboUI, Log, TEXT("ViewerDirector: Restored original pawn on EndPlay"));
        OriginalPawn = nullptr;
    }

    Super::EndPlay(EndPlayReason);
}

void ARealGazeboViewerDirector::InitializeForBeginPlay()
{
    // Discover vehicles
    DiscoverVehicles();

    // Setup input bindings (only M/F/B keys needed now)
    SetupInputBindings();

    // Don't start in manual mode - let user start with UE5's default pawn
    // ManualCameraPawn will be created lazily when first needed

    UE_LOG(LogRealGazeboUI, Log, TEXT("ViewerDirector: Initialized with %d vehicles"), AvailableVehicles.Num());
}

void ARealGazeboViewerDirector::SetupInputBindings()
{
    URealGazeboBlueprintLib::EnableInput(this);

    // Bind camera mode switching keys
    URealGazeboBlueprintLib::BindActionToKey(TEXT("RealGazebo_ManualCamera"), EKeys::M, this, &ARealGazeboViewerDirector::InputManualCamera);
    URealGazeboBlueprintLib::BindActionToKey(TEXT("RealGazebo_FirstPersonCamera"), EKeys::F, this, &ARealGazeboViewerDirector::InputFirstPersonCamera);
    URealGazeboBlueprintLib::BindActionToKey(TEXT("RealGazebo_ThirdPersonCamera"), EKeys::B, this, &ARealGazeboViewerDirector::InputThirdPersonCamera);

    UE_LOG(LogRealGazeboUI, Log, TEXT("ViewerDirector: Input bindings setup complete"));
}

void ARealGazeboViewerDirector::DiscoverVehicles()
{
    AvailableVehicles.Empty();

    TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AVehicleBasePawn::StaticClass(), FoundActors);

    for (AActor* Actor : FoundActors)
    {
        if (AVehicleBasePawn* Vehicle = Cast<AVehicleBasePawn>(Actor))
        {
            AvailableVehicles.Add(Vehicle);
        }
    }

    UE_LOG(LogRealGazeboUI, Log, TEXT("ViewerDirector: Discovered %d vehicles"), AvailableVehicles.Num());
}

void ARealGazeboViewerDirector::SetCameraMode(ERealGazeboViewerMode NewMode)
{
    if (CurrentMode == NewMode)
    {
        return;
    }

    UE_LOG(LogRealGazeboUI, Log, TEXT("ViewerDirector: Switching from %d to %d"), (int32)CurrentMode, (int32)NewMode);

    CurrentMode = NewMode;

    // Switch to new mode
    switch (CurrentMode)
    {
    case ERealGazeboViewerMode::Manual:
        SwitchToManualMode();
        break;
    case ERealGazeboViewerMode::FirstPerson:
        SwitchToFirstPersonMode();
        break;
    case ERealGazeboViewerMode::ThirdPerson:
        SwitchToThirdPersonMode();
        break;
    }
}

void ARealGazeboViewerDirector::SetCurrentVehicle(AVehicleBasePawn* Vehicle)
{
    if (CurrentVehicle == Vehicle)
    {
        return;
    }

    // Disable cameras on previous vehicle if using component system
    if (CurrentVehicle)
    {
        URealGazeboCameraControllerComponent* PreviousCameraController = FindCameraControllerForVehicle(CurrentVehicle);
        if (PreviousCameraController)
        {
            PreviousCameraController->DeactivateAllCameras();
        }
    }

    CurrentVehicle = Vehicle;

    if (CurrentVehicle)
    {
        UE_LOG(LogRealGazeboUI, Log, TEXT("ViewerDirector: Set current vehicle to %s"), *CurrentVehicle->GetName());

        // If we're in first or third person mode, update camera
        if (CurrentMode == ERealGazeboViewerMode::FirstPerson)
        {
            SwitchToFirstPersonMode();
        }
        else if (CurrentMode == ERealGazeboViewerMode::ThirdPerson)
        {
            SwitchToThirdPersonMode();
        }
    }
    else
    {
        UE_LOG(LogRealGazeboUI, Log, TEXT("ViewerDirector: Cleared current vehicle"));

        // Fall back to manual mode if no vehicle
        if (CurrentMode != ERealGazeboViewerMode::Manual)
        {
            SetCameraMode(ERealGazeboViewerMode::Manual);
        }
    }
}

void ARealGazeboViewerDirector::SetCurrentVehicleByID(const FVehicleID& VehicleID)
{
    AVehicleBasePawn* FoundVehicle = nullptr;

    for (AVehicleBasePawn* Vehicle : AvailableVehicles)
    {
        if (Vehicle && Vehicle->VehicleID == VehicleID)
        {
            FoundVehicle = Vehicle;
            break;
        }
    }

    SetCurrentVehicle(FoundVehicle);
}

void ARealGazeboViewerDirector::SetInitialCameraSettings(const FVector& Location, const FRotator& Rotation)
{
    InitialCameraLocation = Location;
    InitialCameraRotation = Rotation;

    // Apply the initial position to the DefaultPawn immediately if available
    if (OriginalPawn)
    {
        FVector TargetLocation = GetActorLocation() + InitialCameraLocation;
        OriginalPawn->SetActorLocation(TargetLocation);
        OriginalPawn->SetActorRotation(InitialCameraRotation);

        // Also update the PlayerController's control rotation for camera view
        APlayerController* PC = GetWorld()->GetFirstPlayerController();
        if (PC)
        {
            PC->SetControlRotation(InitialCameraRotation);
            UE_LOG(LogRealGazeboUI, Log, TEXT("ViewerDirector: Updated PlayerController control rotation to match pawn rotation"));
        }

        UE_LOG(LogRealGazeboUI, Log, TEXT("ViewerDirector: Applied initial camera settings to DefaultPawn - Location: %s, Rotation: %s"),
               *TargetLocation.ToString(), *InitialCameraRotation.ToString());
    }
    else
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("ViewerDirector: Updated initial camera settings but DefaultPawn not available yet - Location: %s, Rotation: %s"),
               *Location.ToString(), *Rotation.ToString());
    }
}

void ARealGazeboViewerDirector::RefreshVehicleList()
{
    DiscoverVehicles();
}

void ARealGazeboViewerDirector::SwitchToManualMode()
{
    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    if (!PC || !OriginalPawn)
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("ViewerDirector: Cannot switch to manual mode - no PlayerController or OriginalPawn"));
        return;
    }

    // Get current camera transform to position manual camera at current view
    FTransform CurrentCameraTransform = GetCurrentCameraTransform();

    // Position the original pawn at the current camera location
    OriginalPawn->SetActorLocation(CurrentCameraTransform.GetLocation());
    OriginalPawn->SetActorRotation(CurrentCameraTransform.GetRotation().Rotator());
    OriginalPawn->SetActorHiddenInGame(false); // Ensure it's visible

    // Disable collision so manual camera can fly through walls freely
    OriginalPawn->SetActorEnableCollision(false);
    if (UPrimitiveComponent* RootComp = Cast<UPrimitiveComponent>(OriginalPawn->GetRootComponent()))
    {
        RootComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        RootComp->SetCollisionResponseToAllChannels(ECR_Ignore);
        UE_LOG(LogRealGazeboUI, Log, TEXT("ViewerDirector: Disabled collision for manual camera - free flight enabled"));
    }

    // Switch to the original pawn only if not already possessed
    if (PC->GetPawn() != OriginalPawn)
    {
        SwitchPlayerPawn(OriginalPawn);
        UE_LOG(LogRealGazeboUI, Log, TEXT("ViewerDirector: Switched possession to original DefaultPawn for manual camera at current view location"));
    }

    // Ensure PlayerController's control rotation matches the camera view
    PC->SetControlRotation(CurrentCameraTransform.GetRotation().Rotator());

    UE_LOG(LogRealGazeboUI, Display, TEXT("ViewerDirector: Switched to Manual Camera mode using original DefaultPawn at %s"),
           *CurrentCameraTransform.GetLocation().ToString());
}

void ARealGazeboViewerDirector::SwitchToFirstPersonMode()
{
    // Auto-select first available vehicle if none selected
    if (!CurrentVehicle)
    {
        if (AvailableVehicles.Num() > 0)
        {
            SetCurrentVehicle(AvailableVehicles[0]);
            UE_LOG(LogRealGazeboUI, Log, TEXT("ViewerDirector: Auto-selected first available vehicle %s for First Person camera"), *CurrentVehicle->GetName());
        }
        else
        {
            UE_LOG(LogRealGazeboUI, Warning, TEXT("ViewerDirector: Cannot switch to First Person - no vehicles available"));
            return;
        }
    }

    // Hide original pawn when switching to vehicle mode
    if (OriginalPawn)
    {
        OriginalPawn->SetActorHiddenInGame(true);
        UE_LOG(LogRealGazeboUI, Log, TEXT("ViewerDirector: Hidden original DefaultPawn when switching to vehicle mode"));
    }

    // Switch to vehicle pawn
    SwitchPlayerPawn(CurrentVehicle);

    // Use component-based camera system
    URealGazeboCameraControllerComponent* CameraController = FindCameraControllerForVehicle(CurrentVehicle);
    if (CameraController)
    {
        CameraController->ActivateFirstPersonCamera();
        UE_LOG(LogRealGazeboUI, Display, TEXT("ViewerDirector: Switched to First Person Camera for vehicle %s"), *CurrentVehicle->GetName());
        return;
    }

    // No camera system found - fall back to manual mode
    UE_LOG(LogRealGazeboUI, Warning, TEXT("ViewerDirector: No camera controller component found for vehicle %s"), *CurrentVehicle->GetName());
    SetCameraMode(ERealGazeboViewerMode::Manual);
}

void ARealGazeboViewerDirector::SwitchToThirdPersonMode()
{
    // Auto-select first available vehicle if none selected
    if (!CurrentVehicle)
    {
        if (AvailableVehicles.Num() > 0)
        {
            SetCurrentVehicle(AvailableVehicles[0]);
            UE_LOG(LogRealGazeboUI, Log, TEXT("ViewerDirector: Auto-selected first available vehicle %s for Third Person camera"), *CurrentVehicle->GetName());
        }
        else
        {
            UE_LOG(LogRealGazeboUI, Warning, TEXT("ViewerDirector: Cannot switch to Third Person - no vehicles available"));
            return;
        }
    }

    // Hide original pawn when switching to vehicle mode
    if (OriginalPawn)
    {
        OriginalPawn->SetActorHiddenInGame(true);
        UE_LOG(LogRealGazeboUI, Log, TEXT("ViewerDirector: Hidden original DefaultPawn when switching to vehicle mode"));
    }

    // Switch to vehicle pawn
    SwitchPlayerPawn(CurrentVehicle);

    // Use component-based camera system
    URealGazeboCameraControllerComponent* CameraController = FindCameraControllerForVehicle(CurrentVehicle);
    if (CameraController)
    {
        CameraController->ActivateThirdPersonCamera();
        UE_LOG(LogRealGazeboUI, Display, TEXT("ViewerDirector: Switched to Third Person Camera for vehicle %s"), *CurrentVehicle->GetName());
        return;
    }

    // No camera system found - fall back to manual mode
    UE_LOG(LogRealGazeboUI, Warning, TEXT("ViewerDirector: No camera controller component found for vehicle %s"), *CurrentVehicle->GetName());
    SetCameraMode(ERealGazeboViewerMode::Manual);
}


void ARealGazeboViewerDirector::SwitchPlayerPawn(APawn* NewPawn)
{
    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    if (PC && NewPawn)
    {
        PC->Possess(NewPawn);
        UE_LOG(LogRealGazeboUI, Log, TEXT("ViewerDirector: Switched player pawn to %s"), *NewPawn->GetName());
    }
}

URealGazeboCameraControllerComponent* ARealGazeboViewerDirector::FindCameraControllerForVehicle(AVehicleBasePawn* Vehicle) const
{
    if (!Vehicle)
    {
        return nullptr;
    }

    // Look for camera controller component
    return Vehicle->FindComponentByClass<URealGazeboCameraControllerComponent>();
}

FTransform ARealGazeboViewerDirector::GetCurrentCameraTransform() const
{
    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    if (!PC)
    {
        // Fallback to default transform
        return FTransform(InitialCameraRotation, GetActorLocation() + InitialCameraLocation, FVector::OneVector);
    }

    // Get current camera transform from player controller
    FVector CameraLocation;
    FRotator CameraRotation;
    PC->GetPlayerViewPoint(CameraLocation, CameraRotation);

    return FTransform(CameraRotation, CameraLocation, FVector::OneVector);
}

//----------------------------------------------------------
// Input Handlers
//----------------------------------------------------------

void ARealGazeboViewerDirector::InputManualCamera()
{
    SetCameraMode(ERealGazeboViewerMode::Manual);
}

void ARealGazeboViewerDirector::InputFirstPersonCamera()
{
    SetCameraMode(ERealGazeboViewerMode::FirstPerson);
}

void ARealGazeboViewerDirector::InputThirdPersonCamera()
{
    SetCameraMode(ERealGazeboViewerMode::ThirdPerson);
}
