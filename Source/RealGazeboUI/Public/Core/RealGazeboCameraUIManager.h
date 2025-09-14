// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Sub-author: MinKyu Kim
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/DataTable.h"
#include "Widgets/RealGazeboMainWidget.h"
#include "Data/VehicleTypeImageData.h"
#include "RealGazeboCameraUIManager.generated.h"

/**
 * User-friendly RealGazebo Camera UI Manager Actor for plug-and-play usage
 *
 * This provides drag-and-drop camera UI setup with vehicle type image integration.
 * Similar to RealGazeboManager for consistent user experience.
 *
 * Key Features:
 * - Drag-and-drop into level
 * - Visual configuration in Details panel
 * - Auto-create main UI widget
 * - VehicleTypeImageDataTable integration for vehicle icons
 * - Blueprint-friendly setup functions
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "RealGazebo Camera UI Manager"))
class REALGAZEBOUI_API ARealGazeboCameraUIManager : public AActor
{
    GENERATED_BODY()

public:
    ARealGazeboCameraUIManager();

    //----------------------------------------------------------
    // Core UI Settings - Essential Configuration
    //----------------------------------------------------------

    /** Vehicle Type Images DataTable for UI icons and vehicle configuration */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealGazeboCamera UI",
              meta = (DisplayName = "Vehicle Type Images DataTable", DisplayPriority = "1",
                     ToolTip = "DataTable containing vehicle type codes and their corresponding images"))
    TObjectPtr<UDataTable> VehicleTypeImageDataTable;

    /** Main Widget Class to create and manage */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealGazeboCamera UI",
              meta = (DisplayName = "Main Widget Class", DisplayPriority = "2",
                     ToolTip = "Widget class to create for the main UI"))
    TSubclassOf<UUserWidget> MainWidgetClass;
    

    //----------------------------------------------------------
    // Runtime Settings - UI Behavior
    //----------------------------------------------------------

    /** Auto-create and show UI when level begins */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealGazeboCamera UI|Runtime",
              meta = (DisplayName = "Auto Create UI", DisplayPriority = "1"))
    bool bAutoCreateUI = true;

    /** Auto-add widget to viewport */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealGazeboCamera UI|Runtime",
              meta = (DisplayName = "Auto Add to Viewport", DisplayPriority = "2"))
    bool bAutoAddToViewport = true;

    /** Z-order for the widget when added to viewport */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealGazeboCamera UI|Runtime",
              meta = (DisplayName = "Widget Z-Order", DisplayPriority = "3", ClampMin = "0"))
    int32 WidgetZOrder = 0;

    /** Auto-create viewer director for camera control */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealGazeboCamera UI|Runtime",
              meta = (DisplayName = "Auto Create Viewer Director", DisplayPriority = "4"))
    bool bAutoCreateViewerDirector = true;

    /** Always show mouse cursor regardless of camera mode or UI state */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealGazeboCamera UI|Mouse Control",
              meta = (DisplayName = "Always Show Mouse Cursor", DisplayPriority = "1",
                     ToolTip = "Keep mouse cursor visible at all times for UI interaction"))
    bool bAlwaysShowMouseCursor = true;

    //----------------------------------------------------------
    // Camera Settings
    //----------------------------------------------------------

    /** Initial location for DefaultPawn when system starts */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealGazeboCamera UI|Camera Settings",
              meta = (DisplayName = "Initial Camera Location", DisplayPriority = "1",
                     ToolTip = "Starting position for DefaultPawn camera"))
    FVector InitialCameraLocation = FVector(0.0f, 0.0f, 500.0f);

    /** Initial rotation for DefaultPawn when system starts */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealGazeboCamera UI|Camera Settings",
              meta = (DisplayName = "Initial Camera Rotation", DisplayPriority = "2",
                     ToolTip = "Starting rotation for DefaultPawn camera"))
    FRotator InitialCameraRotation = FRotator(-20.0f, 0.0f, 0.0f);

    //----------------------------------------------------------
    // Blueprint Functions - Easy Setup
    //----------------------------------------------------------

    /** Create the main widget with proper configuration */
    UFUNCTION(BlueprintCallable, Category = "RealGazeboCamera UI",
              meta = (DisplayName = "Create Main Widget", CallInEditor = "true"))
    UUserWidget* CreateMainWidget();

    /** Add the main widget to viewport */
    UFUNCTION(BlueprintCallable, Category = "RealGazeboCamera UI",
              meta = (DisplayName = "Add Widget to Viewport", CallInEditor = "true"))
    void AddMainWidgetToViewport();

    /** Complete UI setup in one call - creates widget and adds to viewport */
    UFUNCTION(BlueprintCallable, Category = "RealGazeboCamera UI",
              meta = (DisplayName = "Initialize Camera UI", CallInEditor = "true"))
    void InitializeCameraUI();

    /** Remove widget from viewport and cleanup */
    UFUNCTION(BlueprintCallable, Category = "RealGazeboCamera UI")
    void CleanupCameraUI();

    //----------------------------------------------------------
    // Mouse Cursor Control Functions
    //----------------------------------------------------------

    /** Ensure mouse cursor is always visible */
    UFUNCTION(BlueprintCallable, Category = "RealGazeboCamera UI|Mouse Control",
              meta = (DisplayName = "Ensure Mouse Cursor Visible", CallInEditor = "true"))
    void EnsureMouseCursorVisible();

    /** Force mouse cursor visibility state */
    UFUNCTION(BlueprintCallable, Category = "RealGazeboCamera UI|Mouse Control",
              meta = (DisplayName = "Set Mouse Cursor Always Visible"))
    void SetMouseCursorAlwaysVisible(bool bVisible);

    //----------------------------------------------------------
    // Configuration & Validation
    //----------------------------------------------------------

    /** Validate the current setup and return status */
    UFUNCTION(BlueprintCallable, Category = "RealGazeboCamera UI|Validation",
              meta = (DisplayName = "Validate Setup"))
    bool ValidateSetup();

    /** Get the currently created main widget (if any) */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazeboCamera UI")
    UUserWidget* GetMainWidget() const { return MainWidget; }

    /** Check if UI is currently active */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazeboCamera UI")
    bool IsUIActive() const;

    /** Get the created viewer director (if any) */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazeboCamera UI")
    class ARealGazeboViewerDirector* GetViewerDirector() const { return ViewerDirector; }

    //----------------------------------------------------------
    // Events (Blueprint Implementable)
    //----------------------------------------------------------

    /** Called when UI is successfully created */
    UFUNCTION(BlueprintImplementableEvent, Category = "Events")
    void OnUICreated();

    /** Called when UI setup fails */
    UFUNCTION(BlueprintImplementableEvent, Category = "Events")
    void OnUISetupFailed(const FString& ErrorMessage);

    /** Called when UI is cleaned up */
    UFUNCTION(BlueprintImplementableEvent, Category = "Events")
    void OnUICleanedUp();

protected:
    //----------------------------------------------------------
    // Actor Lifecycle
    //----------------------------------------------------------

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    //----------------------------------------------------------
    // Internal Data
    //----------------------------------------------------------

    /** Reference to the created main widget */
    UPROPERTY()
    TObjectPtr<UUserWidget> MainWidget;

    /** Reference to the created viewer director */
    UPROPERTY()
    TObjectPtr<class ARealGazeboViewerDirector> ViewerDirector;

    /** Track if widget is currently added to viewport */
    bool bWidgetInViewport = false;

private:
    //----------------------------------------------------------
    // Internal Methods
    //----------------------------------------------------------

    /** Configure the created widget with our settings */
    void ConfigureMainWidget(UUserWidget* Widget);

    /** Get the player controller for widget operations */
    class APlayerController* GetPlayerController() const;

    /** Validate DataTable structure and contents */
    bool ValidateVehicleTypeImageDataTable() const;

    /** Setup widget with VehicleTypeImageDataTable reference */
    void SetupVehicleTypeImages(UUserWidget* Widget);

    /** Setup widget with ViewerDirector reference for camera integration */
    void SetupViewerDirectorIntegration(UUserWidget* Widget);

    /** Create viewer director for camera control */
    void CreateViewerDirector();

    /** Internal cleanup without events */
    void InternalCleanup();
};