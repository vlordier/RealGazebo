// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Sub-author: MinKyu Kim
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "ViewerController/RealGazeboThirdPersonCameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Components/StaticMeshComponent.h"
#include "CollisionQueryParams.h"
#include "RealGazeboUI.h"

URealGazeboThirdPersonCameraComponent::URealGazeboThirdPersonCameraComponent()
{
    // Set this component to NOT be ticked (spring arm handles this)
    PrimaryComponentTick.bCanEverTick = false;

    // Set component tag for discovery by camera controller
    ComponentTags.Add(FName("ThirdPersonCamera"));

    // Configure spring arm settings
    ConfigureSpringArmSettings();

    // Create internal camera component
    CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    CameraComponent->SetupAttachment(this);
    CameraComponent->bUsePawnControlRotation = false;

    // Start inactive
    bCameraActive = false;

    UE_LOG(LogRealGazeboUI, Log, TEXT("ThirdPersonCameraComponent: Constructor completed"));
}

void URealGazeboThirdPersonCameraComponent::BeginPlay()
{
    Super::BeginPlay();

    // Configure camera and spring arm settings
    ConfigureCameraSettings();

    UE_LOG(LogRealGazeboUI, Log, TEXT("ThirdPersonCameraComponent: Initialized on %s"), GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"));
}

void URealGazeboThirdPersonCameraComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Deactivate if currently active
    if (bCameraActive)
    {
        DeactivateCamera();
    }

    Super::EndPlay(EndPlayReason);
}


void URealGazeboThirdPersonCameraComponent::ActivateCamera()
{
    if (!CameraComponent)
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("ThirdPersonCameraComponent: No internal camera component available"));
        return;
    }

    // Activate the internal camera component
    CameraComponent->SetActive(true);
    bCameraActive = true;

    // Set as view target using the internal camera
    if (UWorld* World = GetWorld())
    {
        if (APlayerController* PC = World->GetFirstPlayerController())
        {
            if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
            {
                // Set view target to the pawn, but the active camera will be our internal camera component
                PC->SetViewTargetWithBlend(OwnerPawn, 0.5f);
            }
        }
    }

    // Fire Blueprint event
    OnCameraActivated();

    UE_LOG(LogRealGazeboUI, Log, TEXT("ThirdPersonCameraComponent: Camera activated on %s"), GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"));
}

void URealGazeboThirdPersonCameraComponent::DeactivateCamera()
{
    if (CameraComponent)
    {
        // Deactivate the internal camera component
        CameraComponent->SetActive(false);
    }

    bCameraActive = false;

    // Fire Blueprint event
    OnCameraDeactivated();

    UE_LOG(LogRealGazeboUI, Log, TEXT("ThirdPersonCameraComponent: Camera deactivated on %s"), GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"));
}

void URealGazeboThirdPersonCameraComponent::ConfigureForVehicleType(float ArmLength, const FVector& ArmOffset)
{
    // Configure spring arm settings (inherited from USpringArmComponent)
    TargetArmLength = ArmLength;
    SetRelativeLocation(ArmOffset);

    UE_LOG(LogRealGazeboUI, Log, TEXT("ThirdPersonCameraComponent: Configured for vehicle type (Length: %.1f, Offset: %s)"),
           ArmLength, *ArmOffset.ToString());
}

void URealGazeboThirdPersonCameraComponent::ConfigureCameraSettings()
{
    if (CameraComponent)
    {
        // Configure internal camera settings
        CameraComponent->SetFieldOfView(CameraFieldOfView);

        UE_LOG(LogRealGazeboUI, Log, TEXT("ThirdPersonCameraComponent: Camera settings configured (FOV: %.1f)"), CameraFieldOfView);
    }
    else
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("ThirdPersonCameraComponent: No internal camera component to configure"));
    }
}

void URealGazeboThirdPersonCameraComponent::ConfigureSpringArmSettings()
{
    // Configure spring arm settings (inherited from USpringArmComponent)
    SetRelativeLocation(FVector(0.0f, 0.0f, 50.0f));
    SetRelativeRotation(FRotator(-20.0f, 0.0f, 0.0f));
    TargetArmLength = 400.0f;
    bEnableCameraLag = true;
    bEnableCameraRotationLag = true;
    CameraLagSpeed = 10.0f;
    CameraRotationLagSpeed = 10.0f;
    bDoCollisionTest = false;  // Disable collision so camera can move freely through walls
    ProbeSize = 0.0f;         // No collision probe sphere

    // Spring arm inheritance settings for vehicle following
    bInheritPitch = true;
    bInheritYaw = true;
    bInheritRoll = false; // Usually don't want roll for chase cameras

    UE_LOG(LogRealGazeboUI, Log, TEXT("ThirdPersonCameraComponent: Spring arm settings configured"));
}