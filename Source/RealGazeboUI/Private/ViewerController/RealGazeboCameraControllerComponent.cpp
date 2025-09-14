// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Sub-author: MinKyu Kim
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "ViewerController/RealGazeboCameraControllerComponent.h"
#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "RealGazeboUI.h"

URealGazeboCameraControllerComponent::URealGazeboCameraControllerComponent()
{
    // Set this component to be ticked every frame
    PrimaryComponentTick.bCanEverTick = false;

    // Set component tag for discovery by ViewerDirector
    ComponentTags.Add(FName("CameraController"));
}

void URealGazeboCameraControllerComponent::BeginPlay()
{
    Super::BeginPlay();

    // Discover camera components if enabled
    if (bAutoDiscoverCameras)
    {
        DiscoverCameraComponents();
    }

    // Validate discovered components
    if (ValidateCameraComponents())
    {
        UE_LOG(LogRealGazeboUI, Log, TEXT("CameraControllerComponent: Initialized on %s with %s%s cameras"),
               GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"),
               FirstPersonCameraComponent ? TEXT("FP ") : TEXT(""),
               ThirdPersonCameraComponent ? TEXT("TP") : TEXT(""));
    }
    else
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("CameraControllerComponent: No camera components found on %s"),
               GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"));
    }
}

void URealGazeboCameraControllerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Deactivate all cameras before cleanup
    DeactivateAllCameras();

    Super::EndPlay(EndPlayReason);
}

void URealGazeboCameraControllerComponent::ShowFirstPersonCamera()
{
    if (!FirstPersonCameraComponent)
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("CameraControllerComponent: No first person camera available on %s"),
               GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"));
        return;
    }

    // Deactivate other cameras
    if (ThirdPersonCameraComponent)
    {
        ThirdPersonCameraComponent->DeactivateCamera();
    }

    // Activate first person camera
    FirstPersonCameraComponent->ActivateCamera();

    // Update camera mode
    SetCameraMode(ECameraMode::FirstPerson);

    UE_LOG(LogRealGazeboUI, Log, TEXT("CameraControllerComponent: Activated first person camera on %s"),
           GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"));
}

void URealGazeboCameraControllerComponent::ShowThirdPersonCamera()
{
    if (!ThirdPersonCameraComponent)
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("CameraControllerComponent: No third person camera available on %s"),
               GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"));
        return;
    }

    // Deactivate other cameras
    if (FirstPersonCameraComponent)
    {
        FirstPersonCameraComponent->DeactivateCamera();
    }

    // Activate third person camera
    ThirdPersonCameraComponent->ActivateCamera();

    // Update camera mode
    SetCameraMode(ECameraMode::ThirdPerson);

    UE_LOG(LogRealGazeboUI, Log, TEXT("CameraControllerComponent: Activated third person camera on %s"),
           GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"));
}

void URealGazeboCameraControllerComponent::DisableAllCameras()
{
    // Deactivate all cameras
    if (FirstPersonCameraComponent)
    {
        FirstPersonCameraComponent->DeactivateCamera();
    }

    if (ThirdPersonCameraComponent)
    {
        ThirdPersonCameraComponent->DeactivateCamera();
    }

    // Update camera mode
    SetCameraMode(ECameraMode::None);

    UE_LOG(LogRealGazeboUI, Log, TEXT("CameraControllerComponent: Deactivated all cameras on %s"),
           GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"));
}

void URealGazeboCameraControllerComponent::ActivateFirstPersonCamera()
{
    // Interface method for ViewerDirector compatibility
    ShowFirstPersonCamera();
}

void URealGazeboCameraControllerComponent::ActivateThirdPersonCamera()
{
    // Interface method for ViewerDirector compatibility
    ShowThirdPersonCamera();
}

void URealGazeboCameraControllerComponent::DeactivateAllCameras()
{
    // Interface method for ViewerDirector compatibility
    DisableAllCameras();
}

void URealGazeboCameraControllerComponent::DiscoverCameraComponents()
{
    if (!GetOwner())
    {
        return;
    }

    // Reset references
    FirstPersonCameraComponent = nullptr;
    ThirdPersonCameraComponent = nullptr;

    // Get all actor components
    TArray<UActorComponent*> ActorComponents;
    GetOwner()->GetComponents<UActorComponent>(ActorComponents);

    // Also search through attached scene components
    if (USceneComponent* RootComp = GetOwner()->GetRootComponent())
    {
        TArray<USceneComponent*> AttachedComponents = RootComp->GetAttachChildren();
        for (USceneComponent* Child : AttachedComponents)
        {
            ActorComponents.Add(Child);
        }
        ActorComponents.Add(RootComp); // Include root component
    }

    int32 FoundCameras = 0;

    for (UActorComponent* Component : ActorComponents)
    {
        if (!Component)
        {
            continue;
        }

        // Check for first person camera component
        if (Component->ComponentHasTag(FirstPersonCameraTag))
        {
            if (URealGazeboFirstPersonCameraComponent* FPCamera = Cast<URealGazeboFirstPersonCameraComponent>(Component))
            {
                FirstPersonCameraComponent = FPCamera;
                FoundCameras++;
                UE_LOG(LogRealGazeboUI, Log, TEXT("CameraControllerComponent: Found first person camera component: %s"),
                       *Component->GetName());
            }
        }

        // Check for third person camera component
        if (Component->ComponentHasTag(ThirdPersonCameraTag))
        {
            if (URealGazeboThirdPersonCameraComponent* TPCamera = Cast<URealGazeboThirdPersonCameraComponent>(Component))
            {
                ThirdPersonCameraComponent = TPCamera;
                FoundCameras++;
                UE_LOG(LogRealGazeboUI, Log, TEXT("CameraControllerComponent: Found third person camera component: %s"),
                       *Component->GetName());
            }
        }
    }

    UE_LOG(LogRealGazeboUI, Log, TEXT("CameraControllerComponent: Camera discovery complete - found %d camera components on %s"),
           FoundCameras, GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"));
}

void URealGazeboCameraControllerComponent::SetCameraMode(ECameraMode NewMode)
{
    ECameraMode PreviousMode = CurrentCameraMode;
    CurrentCameraMode = NewMode;

    // Fire Blueprint events
    if (PreviousMode != NewMode)
    {
        OnCameraModeChanged(NewMode, PreviousMode);

        switch (NewMode)
        {
        case ECameraMode::FirstPerson:
            OnFirstPersonCameraActivated();
            break;
        case ECameraMode::ThirdPerson:
            OnThirdPersonCameraActivated();
            break;
        case ECameraMode::None:
            OnAllCamerasDeactivated();
            break;
        }
    }
}

bool URealGazeboCameraControllerComponent::ValidateCameraComponents() const
{
    bool bHasAnyCamera = (FirstPersonCameraComponent != nullptr) || (ThirdPersonCameraComponent != nullptr);

    if (!bHasAnyCamera)
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("CameraControllerComponent: No camera components found. Make sure to add camera components with correct tags."));
        return false;
    }

    return true;
}