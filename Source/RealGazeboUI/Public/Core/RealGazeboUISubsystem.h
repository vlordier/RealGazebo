// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Sub-author: MinKyu Kim
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/DataTable.h"
#include "Data/VehicleTypeImageData.h"
#include "RealGazeboUISubsystem.generated.h"

// Forward declarations
class UUserWidget;
class ARealGazeboViewerDirector;
class APlayerController;

/**
 * Subsystem for managing RealGazebo UI operations
 *
 * Key Features:
 * - Centralized UI widget management
 * - Camera UI integration with viewer director
 * - Vehicle type image data management
 * - Persistent across level changes
 * - Automatic initialization and cleanup
 */
UCLASS()
class REALGAZEBOUI_API URealGazeboUISubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    URealGazeboUISubsystem();

    //----------------------------------------------------------
    // Subsystem Interface
    //----------------------------------------------------------

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

    //----------------------------------------------------------
    // UI Widget Management
    //----------------------------------------------------------

    /** Create main widget with specified class and configuration */
    UFUNCTION(BlueprintCallable, Category = "RealGazebo UI|Widget Management")
    UUserWidget* CreateMainWidget(TSubclassOf<UUserWidget> WidgetClass, UDataTable* VehicleTypeImageDataTable = nullptr);

    /** Add widget to viewport with specified Z-order */
    UFUNCTION(BlueprintCallable, Category = "RealGazebo UI|Widget Management")
    void AddWidgetToViewport(UUserWidget* Widget, int32 ZOrder = 0);

    /** Remove widget from viewport */
    UFUNCTION(BlueprintCallable, Category = "RealGazebo UI|Widget Management")
    void RemoveWidgetFromViewport(UUserWidget* Widget);

    /** Get the currently active main widget */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazebo UI|Widget Management")
    UUserWidget* GetActiveMainWidget() const { return ActiveMainWidget; }

    /** Check if a widget is currently in viewport */
    UFUNCTION(BlueprintCallable, Category = "RealGazebo UI|Widget Management")
    bool IsWidgetInViewport(UUserWidget* Widget) const;

    //----------------------------------------------------------
    // Camera UI Management
    //----------------------------------------------------------

    /** Initialize complete camera UI system */
    UFUNCTION(BlueprintCallable, Category = "RealGazebo UI|Camera Management")
    void InitializeCameraUI(TSubclassOf<UUserWidget> WidgetClass, UDataTable* VehicleTypeImageDataTable,
                           const FVector& InitialCameraLocation = FVector(0.0f, 0.0f, 500.0f),
                           const FRotator& InitialCameraRotation = FRotator(-20.0f, 0.0f, 0.0f),
                           bool bCreateViewerDirector = true, int32 WidgetZOrder = 0);

    /** Cleanup camera UI system */
    UFUNCTION(BlueprintCallable, Category = "RealGazebo UI|Camera Management")
    void CleanupCameraUI();

    /** Get the created viewer director */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazebo UI|Camera Management")
    ARealGazeboViewerDirector* GetViewerDirector() const { return ViewerDirector; }

    /** Create viewer director for camera control */
    UFUNCTION(BlueprintCallable, Category = "RealGazebo UI|Camera Management")
    ARealGazeboViewerDirector* CreateViewerDirector(const FVector& InitialLocation, const FRotator& InitialRotation);

    //----------------------------------------------------------
    // Mouse and Input Management
    //----------------------------------------------------------

    /** Set mouse cursor always visible state */
    UFUNCTION(BlueprintCallable, Category = "RealGazebo UI|Input Management")
    void SetMouseCursorAlwaysVisible(bool bVisible);

    /** Ensure mouse cursor is visible for UI interaction */
    UFUNCTION(BlueprintCallable, Category = "RealGazebo UI|Input Management")
    void EnsureMouseCursorVisible();

    //----------------------------------------------------------
    // Status Information
    //----------------------------------------------------------

    /** Check if UI system is active */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazebo UI|Status")
    bool IsUIActive() const;

    /** Get current UI status string */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazebo UI|Status")
    FString GetUIStatus() const { return UIStatus; }

    /** Validate UI setup and configuration */
    UFUNCTION(BlueprintCallable, Category = "RealGazebo UI|Status")
    bool ValidateUISetup(TSubclassOf<UUserWidget> WidgetClass, UDataTable* VehicleTypeImageDataTable) const;

    //----------------------------------------------------------
    // Events (for Blueprint integration)
    //----------------------------------------------------------

    /** Called when UI is successfully created */
    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnUICreated);
    UPROPERTY(BlueprintAssignable, Category = "RealGazebo UI|Events")
    FOnUICreated OnUICreated;

    /** Called when UI setup fails */
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnUISetupFailed, const FString&, ErrorMessage);
    UPROPERTY(BlueprintAssignable, Category = "RealGazebo UI|Events")
    FOnUISetupFailed OnUISetupFailed;

    /** Called when UI is cleaned up */
    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnUICleanedUp);
    UPROPERTY(BlueprintAssignable, Category = "RealGazebo UI|Events")
    FOnUICleanedUp OnUICleanedUp;

    //----------------------------------------------------------
    // Static Access
    //----------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "RealGazebo UI|Access", meta = (CallInEditor = "true"))
    static URealGazeboUISubsystem* GetUISubsystem(const UObject* WorldContext);

protected:
    //----------------------------------------------------------
    // Core Data
    //----------------------------------------------------------

    /** Currently active main widget */
    UPROPERTY()
    TObjectPtr<UUserWidget> ActiveMainWidget;

    /** Reference to the created viewer director */
    UPROPERTY()
    TObjectPtr<ARealGazeboViewerDirector> ViewerDirector;

    /** Track widgets currently in viewport */
    UPROPERTY()
    TSet<TObjectPtr<UUserWidget>> WidgetsInViewport;

    /** Current UI status for debugging */
    FString UIStatus = TEXT("Not Initialized");

    /** Track if mouse cursor is always visible */
    bool bMouseCursorAlwaysVisible = false;

    //----------------------------------------------------------
    // Internal Methods
    //----------------------------------------------------------

    /** Configure widget with vehicle type image data */
    void ConfigureWidgetWithVehicleData(UUserWidget* Widget, UDataTable* VehicleTypeImageDataTable);

    /** Setup widget with viewer director integration */
    void SetupViewerDirectorIntegration(UUserWidget* Widget);

    /** Get player controller for UI operations */
    APlayerController* GetPlayerController() const;

    /** Validate vehicle type image data table */
    bool ValidateVehicleTypeImageDataTable(UDataTable* DataTable) const;

    /** Update status display */
    void UpdateStatusDisplay();

    /** Internal cleanup without events */
    void InternalCleanup();

private:
    /** Cached world reference */
    TWeakObjectPtr<UWorld> CachedWorld;
};