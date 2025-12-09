// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ViewerController/RealGazeboFirstPersonCameraComponent.h"
#include "ViewerController/RealGazeboThirdPersonCameraComponent.h"
#include "RealGazeboCameraControllerComponent.generated.h"

// Forward declarations
class ARealGazeboViewerDirector;

/**
 * Camera mode enumeration for RealGazebo camera system
 */
UENUM(BlueprintType)
enum class ECameraMode : uint8
{
    None            UMETA(DisplayName = "None"),
    FirstPerson     UMETA(DisplayName = "First Person"),
    ThirdPerson     UMETA(DisplayName = "Third Person")
};

/**
 * Camera Controller Component for RealGazebo vehicles
 *
 * Manages first-person and third-person camera components on a vehicle.
 * Discovers camera components by tags and provides unified interface
 * for the ViewerDirector system.
 *
 * Features:
 * - Automatic discovery of camera components by tags
 * - Unified interface for camera switching
 * - Integration with ViewerDirector system
 * - Blueprint-accessible camera control functions
 * - Event system for camera state changes
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup=(Camera), meta=(BlueprintSpawnableComponent, DisplayName="RealGazebo Camera Controller"))
class REALGAZEBOUI_API URealGazeboCameraControllerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    URealGazeboCameraControllerComponent();

    //----------------------------------------------------------
    // Component Lifecycle
    //----------------------------------------------------------

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    //----------------------------------------------------------
    // Camera Control Interface
    //----------------------------------------------------------

    /** Show first person camera */
    UFUNCTION(BlueprintCallable, Category = "Camera Controller")
    void ShowFirstPersonCamera();

    /** Show third person camera */
    UFUNCTION(BlueprintCallable, Category = "Camera Controller")
    void ShowThirdPersonCamera();

    /** Disable all cameras */
    UFUNCTION(BlueprintCallable, Category = "Camera Controller")
    void DisableAllCameras();

    /** Get current camera mode */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Camera Controller")
    ECameraMode GetCurrentCameraMode() const { return CurrentCameraMode; }

    //----------------------------------------------------------
    // Camera Component Access
    //----------------------------------------------------------

    /** Get first person camera component */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Camera Controller")
    URealGazeboFirstPersonCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }

    /** Get third person camera component */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Camera Controller")
    URealGazeboThirdPersonCameraComponent* GetThirdPersonCameraComponent() const { return ThirdPersonCameraComponent; }

    /** Check if first person camera is available */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Camera Controller")
    bool HasFirstPersonCamera() const { return FirstPersonCameraComponent != nullptr; }

    /** Check if third person camera is available */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Camera Controller")
    bool HasThirdPersonCamera() const { return ThirdPersonCameraComponent != nullptr; }

    //----------------------------------------------------------
    // ViewerDirector Integration
    //----------------------------------------------------------

    /** Interface for ViewerDirector - equivalent to old ARealGazeboVehicleCamera::ShowFirstPersonCamera */
    void ActivateFirstPersonCamera();

    /** Interface for ViewerDirector - equivalent to old ARealGazeboVehicleCamera::ShowThirdPersonCamera */
    void ActivateThirdPersonCamera();

    /** Interface for ViewerDirector - disable all cameras */
    void DeactivateAllCameras();

    //----------------------------------------------------------
    // Component Discovery Settings
    //----------------------------------------------------------

    /** Tag to look for when finding first person camera components */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Discovery Settings")
    FName FirstPersonCameraTag = FName("FirstPersonCamera");

    /** Tag to look for when finding third person camera components */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Discovery Settings")
    FName ThirdPersonCameraTag = FName("ThirdPersonCamera");

    /** Auto-discover camera components on BeginPlay */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Discovery Settings")
    bool bAutoDiscoverCameras = true;

    //----------------------------------------------------------
    // Blueprint Events
    //----------------------------------------------------------

    /** Called when camera mode changes */
    UFUNCTION(BlueprintImplementableEvent, Category = "Events")
    void OnCameraModeChanged(ECameraMode NewMode, ECameraMode PreviousMode);

    /** Called when first person camera is activated */
    UFUNCTION(BlueprintImplementableEvent, Category = "Events")
    void OnFirstPersonCameraActivated();

    /** Called when third person camera is activated */
    UFUNCTION(BlueprintImplementableEvent, Category = "Events")
    void OnThirdPersonCameraActivated();

    /** Called when all cameras are deactivated */
    UFUNCTION(BlueprintImplementableEvent, Category = "Events")
    void OnAllCamerasDeactivated();

protected:
    //----------------------------------------------------------
    // Internal State
    //----------------------------------------------------------

    /** Reference to first person camera component */
    UPROPERTY(BlueprintReadOnly, Category = "Runtime")
    TObjectPtr<URealGazeboFirstPersonCameraComponent> FirstPersonCameraComponent;

    /** Reference to third person camera component */
    UPROPERTY(BlueprintReadOnly, Category = "Runtime")
    TObjectPtr<URealGazeboThirdPersonCameraComponent> ThirdPersonCameraComponent;

    /** Current active camera mode */
    UPROPERTY(BlueprintReadOnly, Category = "Runtime")
    ECameraMode CurrentCameraMode = ECameraMode::None;

private:
    //----------------------------------------------------------
    // Internal Methods
    //----------------------------------------------------------

    /** Discover camera components by tags */
    void DiscoverCameraComponents();

    /** Set camera mode and fire events */
    void SetCameraMode(ECameraMode NewMode);

    /** Validate discovered camera components */
    bool ValidateCameraComponents() const;
};