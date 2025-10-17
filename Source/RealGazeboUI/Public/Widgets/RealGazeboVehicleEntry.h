// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/IUserObjectListEntry.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Engine/DataTable.h"
#include "Data/RealGazeboVehicleListItem.h"
#include "Data/VehicleTypeImageData.h"
#include "RealGazeboVehicleEntry.generated.h"

/**
 * Individual vehicle entry widget for the ListView
 * 
 * Displays:
 * - Vehicle icon and name
 * - Real-time position
 * - Battery level (placeholder)
 * - Connection status
 * - Visual status indicators
 */

UCLASS(BlueprintType, Blueprintable)
class REALGAZEBOUI_API URealGazeboVehicleEntry : public UUserWidget, public IUserObjectListEntry
{
    GENERATED_BODY()

public:
    URealGazeboVehicleEntry(const FObjectInitializer& ObjectInitializer);

protected:
    //----------------------------------------------------------
    // Widget Lifecycle
    //----------------------------------------------------------

    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

    //----------------------------------------------------------
    // IUserObjectListEntry Interface
    //----------------------------------------------------------

    virtual void NativeOnListItemObjectSet(UObject* ListItemObject) override;
    virtual void NativeOnItemSelectionChanged(bool bIsSelectedParam) override;

    //----------------------------------------------------------
    // UI Components (Blueprint Bindable)
    //----------------------------------------------------------

    /** Drone name display, Non-folding panel */
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> DroneNameText;

    /** Battery percentage text, Non-folding panel */
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> NonFoldingBatteryText;

    /** Vehicle ID display, Folding panel */
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> VehicleID;

    /** Position display, Folding panel */
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> PositionText;

    /** Battery percentage text, Folding panel */
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> BatteryText;

    /** Status text display, Folding panel */
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> StatusText;

    /** Vehicle type image display, Folding panel */
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UImage> VehicleTypeImage;

    /** Vehicle selection image, positioned over VehicleTypeImage, Folding panel */
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UImage> VehicleSelectImage;

public:
    //----------------------------------------------------------
    // Configuration
    //----------------------------------------------------------

    /** Default image to display when no specific vehicle type image is found */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration")
    TSoftObjectPtr<UTexture2D> DefaultVehicleImage;

    /** Image to display when vehicle is selected */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration")
    TSoftObjectPtr<UTexture2D> SelectedImage;

    /** Image to display when vehicle is not selected (transparent/empty) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration")
    TSoftObjectPtr<UTexture2D> UnselectedImage;


    //----------------------------------------------------------
    // Data Access
    //----------------------------------------------------------

    /** Get the associated vehicle list item */
    UFUNCTION(BlueprintCallable, Category = "Vehicle Entry")
    URealGazeboVehicleListItem* GetVehicleListItem() const { return VehicleListItem; }

    /** Check if this entry is currently selected */
    UFUNCTION(BlueprintCallable, Category = "Vehicle Entry")
    bool IsEntrySelected() const { return bIsSelected; }

    /** Get vehicle ID */
    UFUNCTION(BlueprintCallable, Category = "Vehicle Entry")
    FVehicleID GetVehicleID() const;

    //----------------------------------------------------------
    // Manager Integration
    //----------------------------------------------------------

    /** Set vehicle type image data table (called by manager) */
    UFUNCTION(BlueprintCallable, Category = "Vehicle Entry|Manager")
    void SetVehicleTypeImageDataTable(UDataTable* DataTable) { VehicleTypeImageDataTable = DataTable; }

    //----------------------------------------------------------
    // Manual Updates
    //----------------------------------------------------------





    //----------------------------------------------------------
    // Events (Blueprint Implementable)
    //----------------------------------------------------------

    /** Called when the vehicle data changes */
    UFUNCTION(BlueprintImplementableEvent, Category = "Events")
    void OnVehicleDataChanged();

    /** Called when selection state changes */
    UFUNCTION(BlueprintImplementableEvent, Category = "Events")
    void OnSelectionStateChanged(bool bSelected);


protected:
    //----------------------------------------------------------
    // Internal Data
    //----------------------------------------------------------

    /** Reference to the vehicle list item data */
    UPROPERTY()
    TObjectPtr<URealGazeboVehicleListItem> VehicleListItem;

    /** Current ListView selection state */
    bool bIsSelected;

    /** Data table containing vehicle type images (set by manager) */
    UPROPERTY()
    TObjectPtr<UDataTable> VehicleTypeImageDataTable;



    //----------------------------------------------------------
    // Internal Methods
    //----------------------------------------------------------

    /** Update all display elements from vehicle data */
    void UpdateDisplayFromData();

    /** Update drone name display (non-folding panel) */
    void UpdateDroneNameDisplay();

    /** Update non-folding battery display */
    void UpdateNonFoldingBatteryDisplay();

    /** Update vehicle ID display */
    void UpdateVehicleIDDisplay();

    /** Update position text display */
    void UpdatePositionText();

    /** Update battery display */
    void UpdateBatteryDisplay();

    /** Update status display */
    void UpdateStatusDisplay();

    /** Update vehicle type image display */
    void UpdateVehicleTypeImage();

    /** Update selection image based on ListView selection state */
    void UpdateSelectionImage();

    /** Get vehicle type image from data table by vehicle type code */
    UTexture2D* GetVehicleTypeImageFromDataTable(uint8 VehicleTypeCode) const;



};