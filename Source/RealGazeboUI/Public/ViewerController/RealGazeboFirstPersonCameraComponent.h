// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Sub-author: MinKyu Kim
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraComponent.h"
#include "RealGazeboFirstPersonCameraComponent.generated.h"

/**
 * First Person Camera Component for RealGazebo vehicles
 *
 * Visual camera component that provides cockpit/driver view for vehicles.
 * This IS a camera component that users can see and position in Blueprint editor.
 * No dynamic camera spawning - this component IS the actual camera.
 *
 * Features:
 * - Visual camera component in Blueprint viewport
 * - Direct camera control - no actor spawning needed
 * - User-configurable camera settings (FOV, offsets, etc.)
 * - Tag-based discovery by camera controller
 * - Blueprint events for activation/deactivation
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup=(Camera), meta=(BlueprintSpawnableComponent, DisplayName="RealGazebo First Person Camera"))
class REALGAZEBOUI_API URealGazeboFirstPersonCameraComponent : public UCameraComponent
{
    GENERATED_BODY()

public:
    URealGazeboFirstPersonCameraComponent();

    //----------------------------------------------------------
    // Component Lifecycle
    //----------------------------------------------------------

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    //----------------------------------------------------------
    // Camera Settings (User Configurable)
    //----------------------------------------------------------
    // Note: Field of View is inherited from UCameraComponent as FieldOfView property

    /** Enable camera lag for smoother movement */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "First Person Camera Settings")
    bool bEnableCameraLag = false;

    /** Camera lag speed (only used if bEnableCameraLag is true) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "First Person Camera Settings", meta = (EditCondition = "bEnableCameraLag", ClampMin = "1.0", ClampMax = "20.0"))
    float CameraLagSpeed = 5.0f;

    //----------------------------------------------------------
    // Camera Control
    //----------------------------------------------------------

    /** Activate this camera (called by camera controller) */
    UFUNCTION(BlueprintCallable, Category = "First Person Camera")
    void ActivateCamera();

    /** Deactivate this camera (called by camera controller) */
    UFUNCTION(BlueprintCallable, Category = "First Person Camera")
    void DeactivateCamera();

    /** Check if camera is currently active */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "First Person Camera")
    bool IsCameraActive() const { return bCameraActive; }

    //----------------------------------------------------------
    // Blueprint Events
    //----------------------------------------------------------

    /** Called when camera is activated */
    UFUNCTION(BlueprintImplementableEvent, Category = "Events")
    void OnCameraActivated();

    /** Called when camera is deactivated */
    UFUNCTION(BlueprintImplementableEvent, Category = "Events")
    void OnCameraDeactivated();

protected:
    //----------------------------------------------------------
    // Internal State
    //----------------------------------------------------------

    /** Whether this camera is currently active */
    UPROPERTY(BlueprintReadOnly, Category = "Runtime")
    bool bCameraActive = false;

private:
    //----------------------------------------------------------
    // Internal Methods
    //----------------------------------------------------------

    /** Configure camera settings */
    void ConfigureCameraSettings();
};