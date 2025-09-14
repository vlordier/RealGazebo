// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Sub-author: MinKyu Kim
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/ListView.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "GazeboBridgeTypes.h"
#include "GazeboBridgeSubsystem.h"
#include "Data/RealGazeboVehicleListItem.h"
#include "RealGazeboMainWidget.generated.h"

// Forward declarations
class UGazeboBridgeSubsystem;
class ARealGazeboViewerDirector;

/**
 * Main RealGazebo monitoring widget with vehicle ListView
 *
 * Features:
 * - Real-time vehicle monitoring with ListView
 * - Connection status tracking
 * - Dynamic vehicle addition/removal with recycling support
 * - ListView native selection with image-based visual feedback
 * - Optimized updates (30Hz default)
 */
UCLASS(BlueprintType, Blueprintable)
class REALGAZEBOUI_API URealGazeboMainWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    URealGazeboMainWidget(const FObjectInitializer& ObjectInitializer);

protected:
    //----------------------------------------------------------
    // Widget Lifecycle
    //----------------------------------------------------------

    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

    //----------------------------------------------------------
    // UI Components (Blueprint Bindable)
    //----------------------------------------------------------

    /** Main vehicle ListView */
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UListView> VehicleListView;

public:
    //----------------------------------------------------------
    // Configuration
    //----------------------------------------------------------

    /** Update frequency for vehicle data refresh (Hz) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration")
    float UpdateFrequency = 30.0f;

    /** Maximum number of vehicles to display */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration")
    int32 MaxDisplayVehicles = 256;


    //----------------------------------------------------------
    // Vehicle Management
    //----------------------------------------------------------

    /** Get all current vehicle list items */
    UFUNCTION(BlueprintCallable, Category = "Vehicles")
    TArray<URealGazeboVehicleListItem*> GetAllVehicleItems() const;

    /** Get vehicle item by ID */
    UFUNCTION(BlueprintCallable, Category = "Vehicles")
    URealGazeboVehicleListItem* GetVehicleItem(const FVehicleID& VehicleID) const;

    /** Get selected vehicle items */
    UFUNCTION(BlueprintCallable, Category = "Vehicles")
    TArray<URealGazeboVehicleListItem*> GetSelectedVehicleItems() const;

    /** Clear all vehicles from the list */
    UFUNCTION(BlueprintCallable, Category = "Vehicles")
    void ClearAllVehicles();

    /** Refresh vehicle list from subsystem */
    UFUNCTION(BlueprintCallable, Category = "Vehicles")
    void RefreshVehicleList();

    //----------------------------------------------------------
    // Vehicle Selection Management
    //----------------------------------------------------------

    /** Set the currently selected vehicle (single selection) */
    UFUNCTION(BlueprintCallable, Category = "Vehicle Selection")
    void SetSelectedVehicle(FVehicleID VehicleID);

    /** Clear vehicle selection */
    UFUNCTION(BlueprintCallable, Category = "Vehicle Selection")
    void ClearVehicleSelection();

    /** Get the currently selected vehicle ID */
    UFUNCTION(BlueprintCallable, Category = "Vehicle Selection")
    FVehicleID GetSelectedVehicleID() const { return CurrentSelectedVehicleID; }

    /** Get the vehicle ListView widget */
    UFUNCTION(BlueprintCallable, Category = "Vehicle List")
    UListView* GetVehicleListView() const { return VehicleListView; }

    //----------------------------------------------------------
    // Manager Integration
    //----------------------------------------------------------

    /** Configure vehicle type images for all vehicle entry widgets (called by manager) */
    UFUNCTION(BlueprintCallable, Category = "Vehicle List|Manager")
    void SetVehicleTypeImageDataTable(UDataTable* DataTable);

    /** Set the ViewerDirector reference for camera integration (called by manager) */
    UFUNCTION(BlueprintCallable, Category = "Vehicle List|Manager")
    void SetViewerDirector(ARealGazeboViewerDirector* InViewerDirector);

    //----------------------------------------------------------
    // Events (Blueprint Implementable)
    //----------------------------------------------------------

    /** Called when a vehicle is selected in the list */
    UFUNCTION(BlueprintImplementableEvent, Category = "Events")
    void OnVehicleSelected(URealGazeboVehicleListItem* SelectedVehicle);

    /** Called when camera target changes for first/third person modes */
    UFUNCTION(BlueprintImplementableEvent, Category = "Events|Camera")
    void OnCameraTargetChanged(URealGazeboVehicleListItem* TargetVehicle);

    /** Called when the connection status changes */
    UFUNCTION(BlueprintImplementableEvent, Category = "Events")
    void OnConnectionStatusChanged(bool bIsConnected, const FString& StatusMessage);

protected:
    //----------------------------------------------------------
    // Internal Data
    //----------------------------------------------------------

    /** Reference to the bridge subsystem */
    TWeakObjectPtr<UGazeboBridgeSubsystem> BridgeSubsystem;

    /** Reference to the viewer director for camera integration */
    TWeakObjectPtr<ARealGazeboViewerDirector> ViewerDirector;

    /** Map of vehicle IDs to list items for fast lookup */
    TMap<FVehicleID, TObjectPtr<URealGazeboVehicleListItem>> VehicleItemMap;

    /** Timer handle for regular updates */
    FTimerHandle UpdateTimerHandle;

    /** Last known vehicle count for change detection */
    int32 LastVehicleCount;

    /** Last connection status */
    bool bLastConnectionStatus;

    /** Currently selected vehicle ID for single selection */
    FVehicleID CurrentSelectedVehicleID;

    /** Data table containing vehicle type images (set by manager) */
    UPROPERTY()
    TObjectPtr<UDataTable> VehicleTypeImageDataTable;

    //----------------------------------------------------------
    // Internal Methods
    //----------------------------------------------------------

    /** Initialize subsystem reference and connections */
    void InitializeSubsystemConnection();

    /** Main update loop - refresh vehicle data */
    void UpdateVehicleData();

    /** Add a new vehicle to the list */
    URealGazeboVehicleListItem* AddVehicleToList(const FVehicleID& VehicleID, const FVehicleRuntimeData& RuntimeData);

    /** Remove a vehicle from the list */
    void RemoveVehicleFromList(const FVehicleID& VehicleID);

    /** Update existing vehicle in the list */
    void UpdateVehicleInList(const FVehicleID& VehicleID, const FVehicleRuntimeData& RuntimeData);


    /** Get vehicle type name from config */
    FString GetVehicleTypeName(uint8 VehicleType) const;

    /** Generate vehicle display name */
    FString GenerateVehicleDisplayName(const FVehicleID& VehicleID, uint8 VehicleType) const;

    /** Find vehicle pawn by ID for camera integration */
    class AVehicleBasePawn* FindVehiclePawnByID(const FVehicleID& VehicleID) const;


    //----------------------------------------------------------
    // ListView Event Handlers
    //----------------------------------------------------------

    /** Handle vehicle item selection */
    UFUNCTION()
    void OnVehicleItemSelectionChanged(UObject* SelectedItem);

    /** Handle entry widget generation/recycling for proper selection state */
    void OnEntryWidgetGenerated(UUserWidget& GeneratedWidget);


};