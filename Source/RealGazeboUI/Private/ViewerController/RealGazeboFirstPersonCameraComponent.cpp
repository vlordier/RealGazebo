// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Sub-author: MinKyu Kim
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "ViewerController/RealGazeboFirstPersonCameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "RealGazeboUI.h"

URealGazeboFirstPersonCameraComponent::URealGazeboFirstPersonCameraComponent()
{
    // Set this component to be ticked every frame
    PrimaryComponentTick.bCanEverTick = false;

    // Set default component transform (cockpit position)
    SetRelativeLocation(FVector(100.0f, 0.0f, 50.0f)); // Forward and slightly up from vehicle center
    SetRelativeRotation(FRotator::ZeroRotator);

    // Set component tag for discovery by camera controller
    ComponentTags.Add(FName("FirstPersonCamera"));

    // Camera component settings (inherited from UCameraComponent)
    SetFieldOfView(90.0f); // Default FOV
    bUsePawnControlRotation = false; // Don't use pawn's control rotation

    // Start inactive
    SetActive(false);
    bCameraActive = false;

    UE_LOG(LogRealGazeboUI, Log, TEXT("FirstPersonCameraComponent: Constructor completed"));
}

void URealGazeboFirstPersonCameraComponent::BeginPlay()
{
    Super::BeginPlay();

    // Configure camera settings
    ConfigureCameraSettings();

    UE_LOG(LogRealGazeboUI, Log, TEXT("FirstPersonCameraComponent: Initialized on %s"), GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"));
}

void URealGazeboFirstPersonCameraComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Deactivate if currently active
    if (bCameraActive)
    {
        DeactivateCamera();
    }

    Super::EndPlay(EndPlayReason);
}

void URealGazeboFirstPersonCameraComponent::ActivateCamera()
{
    // Activate this camera component
    SetActive(true);
    bCameraActive = true;

    // Set as view target - use the owner pawn
    if (UWorld* World = GetWorld())
    {
        if (APlayerController* PC = World->GetFirstPlayerController())
        {
            if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
            {
                PC->SetViewTargetWithBlend(OwnerPawn, 0.5f);
            }
        }
    }

    // Fire Blueprint event
    OnCameraActivated();

    UE_LOG(LogRealGazeboUI, Log, TEXT("FirstPersonCameraComponent: Camera activated on %s"), GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"));
}

void URealGazeboFirstPersonCameraComponent::DeactivateCamera()
{
    // Deactivate this camera component
    SetActive(false);
    bCameraActive = false;

    // Fire Blueprint event
    OnCameraDeactivated();

    UE_LOG(LogRealGazeboUI, Log, TEXT("FirstPersonCameraComponent: Camera deactivated on %s"), GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"));
}

void URealGazeboFirstPersonCameraComponent::ConfigureCameraSettings()
{
    // Configure camera settings directly on this component
    // FOV is already set via SetFieldOfView in constructor or can be changed via Blueprint

    // Apply camera lag settings if enabled
    if (bEnableCameraLag)
    {
        // Camera lag would be handled by the ViewerDirector or camera controller
        // For now, just log the settings
        UE_LOG(LogRealGazeboUI, Log, TEXT("FirstPersonCameraComponent: Camera lag enabled with speed %.1f"), CameraLagSpeed);
    }

    UE_LOG(LogRealGazeboUI, Log, TEXT("FirstPersonCameraComponent: Camera settings configured (FOV: %.1f)"), FieldOfView);
}