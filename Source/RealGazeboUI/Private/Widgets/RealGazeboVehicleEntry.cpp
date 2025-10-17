// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Widgets/RealGazeboVehicleEntry.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"
#include "RealGazeboUI.h"
#include "Data/VehicleTypeImageData.h"
#include "Widgets/RealGazeboMainWidget.h"

URealGazeboVehicleEntry::URealGazeboVehicleEntry(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // Set default values
    
    // Initialize state
    bIsSelected = false;
    VehicleListItem = nullptr;
}

void URealGazeboVehicleEntry::NativeConstruct()
{
    Super::NativeConstruct();
    
    // Vehicle entry widget constructed

    // Initial display update
    UpdateDisplayFromData();

    // Initialize selection image
    UpdateSelectionImage();
}

void URealGazeboVehicleEntry::NativeDestruct()
{
    // Vehicle entry widget destructed
    
    Super::NativeDestruct();
}

void URealGazeboVehicleEntry::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    
    // Update display
    if (VehicleListItem)
    {
        UpdateDisplayFromData();
    }
}

void URealGazeboVehicleEntry::NativeOnListItemObjectSet(UObject* ListItemObject)
{
    // Force clear any previous selection state immediately to prevent recycling issues
    if (VehicleSelectImage)
    {
        VehicleSelectImage->SetBrush(FSlateBrush());
        // Clear selection image during widget recycling
    }

    // Reset internal selection state
    bIsSelected = false;

    // Set the vehicle list item data
    VehicleListItem = Cast<URealGazeboVehicleListItem>(ListItemObject);

    if (VehicleListItem)
    {
        // Widget recycled/created for vehicle

        // Update display with new data
        UpdateDisplayFromData();

        // Force update selection state based on ListView's current selection
        // This ensures proper state after recycling
        UpdateSelectionImage();

    }
    else
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("VehicleEntry[%p]: Invalid list item object"), this);
    }
}

void URealGazeboVehicleEntry::NativeOnItemSelectionChanged(bool bIsSelectedParam)
{
    FString VehicleName = VehicleListItem ? VehicleListItem->VehicleName : TEXT("Unknown");

    // Selection state changing

    // Always force clear the image before state change to prevent stale visuals
    if (VehicleSelectImage)
    {
        VehicleSelectImage->SetBrush(FSlateBrush());
        // Force clear selection image before state change
    }

    // Update internal selection state
    this->bIsSelected = bIsSelectedParam;

    // Update selection image based on ListView selection state
    UpdateSelectionImage();

    // Call Blueprint event
    OnSelectionStateChanged(this->bIsSelected);

    // Selection change completed
}

void URealGazeboVehicleEntry::UpdateDisplayFromData()
{
    if (!VehicleListItem)
    {
        return;
    }
    
    // Update non-folding panel displays
    UpdateDroneNameDisplay();
    UpdateNonFoldingBatteryDisplay();
    
    // Update folding panel displays
    UpdateVehicleIDDisplay();
    UpdatePositionText();
    UpdateBatteryDisplay();
    UpdateStatusDisplay();
    
    // Update vehicle type image
    UpdateVehicleTypeImage();
    
    // Call Blueprint event
    OnVehicleDataChanged();
}

void URealGazeboVehicleEntry::UpdateDroneNameDisplay()
{
    if (!DroneNameText || !VehicleListItem)
    {
        return;
    }
    
    // Display drone name in format: "Iris_1", "Rover_2", etc.
    FString DroneNameString = FString::Printf(TEXT("%s_%d"), 
                                            *VehicleListItem->VehicleTypeName, 
                                            VehicleListItem->VehicleID.VehicleNum);
    DroneNameText->SetText(FText::FromString(DroneNameString));
}

void URealGazeboVehicleEntry::UpdateNonFoldingBatteryDisplay()
{
    if (!NonFoldingBatteryText || !VehicleListItem)
    {
        return;
    }
    
    // Display same battery info as folding panel
    FString BatteryString = TEXT("null");
    NonFoldingBatteryText->SetText(FText::FromString(BatteryString));
}

void URealGazeboVehicleEntry::UpdateVehicleIDDisplay()
{
    if (!VehicleID || !VehicleListItem)
    {
        return;
    }
    
    // Display only vehicle number: "1", "2", "3", etc.
    FString VehicleIDString = FString::Printf(TEXT("%d"), VehicleListItem->VehicleID.VehicleNum);
    VehicleID->SetText(FText::FromString(VehicleIDString));
}

void URealGazeboVehicleEntry::UpdatePositionText()
{
    if (!PositionText || !VehicleListItem)
    {
        return;
    }
    
    // Show simplified position
    FString PositionString = FString::Printf(TEXT("%.0f, %.0f, %.0f"), 
                                           VehicleListItem->Position.X, 
                                           VehicleListItem->Position.Y, 
                                           VehicleListItem->Position.Z);
    
    PositionText->SetText(FText::FromString(PositionString));
}

void URealGazeboVehicleEntry::UpdateBatteryDisplay()
{
    if (!BatteryText || !VehicleListItem)
    {
        return;
    }
    
    // Display "null" since battery is placeholder for future implementation
    FString BatteryString = TEXT("null");
    BatteryText->SetText(FText::FromString(BatteryString));
}

void URealGazeboVehicleEntry::UpdateStatusDisplay()
{
    if (!StatusText || !VehicleListItem)
    {
        return;
    }
    
    // Display "null" since status is placeholder for future implementation
    FString FormattedStatus = TEXT("null");
    StatusText->SetText(FText::FromString(FormattedStatus));
}





FVehicleID URealGazeboVehicleEntry::GetVehicleID() const
{
    if (VehicleListItem)
    {
        return VehicleListItem->VehicleID;
    }
    return FVehicleID(0, 0);
}

void URealGazeboVehicleEntry::UpdateVehicleTypeImage()
{
    if (!VehicleTypeImage || !VehicleListItem)
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("VehicleEntry: UpdateVehicleTypeImage called but VehicleTypeImage=%s, VehicleListItem=%s"), 
               VehicleTypeImage ? TEXT("Valid") : TEXT("NULL"), 
               VehicleListItem ? TEXT("Valid") : TEXT("NULL"));
        return;
    }
    
    // Always keep the widget visible
    VehicleTypeImage->SetVisibility(ESlateVisibility::Visible);
    
    // Get the vehicle type from the vehicle list item
    uint8 VehicleTypeCode = VehicleListItem->VehicleID.VehicleType;
    FString VehicleName = VehicleListItem->VehicleName;
    
    // Update vehicle type image
    
    // Try to get image from data table first
    UTexture2D* VehicleImage = GetVehicleTypeImageFromDataTable(VehicleTypeCode);
    
    if (VehicleImage)
    {
        // Data table has a valid image for this vehicle type, use it
        VehicleTypeImage->SetBrushFromTexture(VehicleImage);
        // Set vehicle type image from data table
    }
    else
    {
        // No data table image found, trying default image
        
        // No image from data table, try to use default image
        if (DefaultVehicleImage.IsValid())
        {
            UTexture2D* DefaultImage = DefaultVehicleImage.LoadSynchronous();
            if (DefaultImage)
            {
                VehicleTypeImage->SetBrushFromTexture(DefaultImage);
                // Using default vehicle image
            }
            else
            {
                UE_LOG(LogRealGazeboUI, Error, TEXT("VehicleEntry: Default image LoadSynchronous() failed for '%s'"), *VehicleName);
            }
        }
        else if (!DefaultVehicleImage.IsNull())
        {
            // Try to load the default image if not already loaded
            UTexture2D* DefaultImage = DefaultVehicleImage.LoadSynchronous();
            if (DefaultImage)
            {
                VehicleTypeImage->SetBrushFromTexture(DefaultImage);
                // Loaded and set default vehicle image
            }
            else
            {
                UE_LOG(LogRealGazeboUI, Error, TEXT("VehicleEntry: Failed to load default image for '%s'"), *VehicleName);
            }
        }
        else
        {
            // No default image configured, keeping current image
        }
    }
}

UTexture2D* URealGazeboVehicleEntry::GetVehicleTypeImageFromDataTable(uint8 VehicleTypeCode) const
{
    if (!VehicleTypeImageDataTable)
    {
        // DataTable is NULL
        return nullptr;
    }
    
    // Get all rows from the data table
    TArray<FVehicleTypeImageRow*> AllRows;
    VehicleTypeImageDataTable->GetAllRows<FVehicleTypeImageRow>(TEXT("GetVehicleTypeImageFromDataTable"), AllRows);
    
    // Search data table for vehicle type
    
    // Find the row with matching vehicle type code
    for (const FVehicleTypeImageRow* Row : AllRows)
    {
        if (Row && Row->VehicleTypeCode == VehicleTypeCode)
        {
            // Found matching row
            
            // Load the soft object pointer
            if (Row->VehicleImage.IsValid())
            {
                UTexture2D* LoadedImage = Row->VehicleImage.LoadSynchronous();
                // Image already loaded
                return LoadedImage;
            }
            else if (!Row->VehicleImage.IsNull())
            {
                // Try to load if not already loaded
                UTexture2D* LoadedImage = Row->VehicleImage.LoadSynchronous();
                // Loading image synchronously
                return LoadedImage;
            }
            else
            {
                // Row found but VehicleImage is NULL
            }
            break;
        }
    }
    
    // No matching row found
    return nullptr;
}





void URealGazeboVehicleEntry::UpdateSelectionImage()
{
    if (!VehicleSelectImage)
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("VehicleEntry[%p]: UpdateSelectionImage called but VehicleSelectImage is NULL"), this);
        return;
    }

    FString VehicleName = VehicleListItem ? VehicleListItem->VehicleName : TEXT("Unknown");

    // Always force clear the image first to prevent state persistence issues
    VehicleSelectImage->SetBrush(FSlateBrush());

    if (bIsSelected)
    {
        // Vehicle is selected by ListView, show selected image
        if (!SelectedImage.IsNull())
        {
            UTexture2D* SelectionTexture = SelectedImage.LoadSynchronous();
            if (SelectionTexture)
            {
                // Create a new brush to ensure clean state
                FSlateBrush NewBrush;
                NewBrush.SetResourceObject(SelectionTexture);
                NewBrush.DrawAs = ESlateBrushDrawType::Image;
                NewBrush.Tiling = ESlateBrushTileType::NoTile;

                VehicleSelectImage->SetBrush(NewBrush);

                // Applied SELECTED image

                // Selection image applied
            }
            else
            {
                UE_LOG(LogRealGazeboUI, Warning, TEXT("VehicleEntry[%p]: Failed to load SelectedImage for '%s'"), this, *VehicleName);
            }
        }
        else
        {
            UE_LOG(LogRealGazeboUI, Warning, TEXT("VehicleEntry[%p]: No SelectedImage configured for '%s' - using empty brush"), this, *VehicleName);
        }
    }
    else
    {
        // Vehicle is not selected, show unselected image or keep clear
        if (!UnselectedImage.IsNull())
        {
            UTexture2D* UnselectedTexture = UnselectedImage.LoadSynchronous();
            if (UnselectedTexture)
            {
                // Create a new brush for unselected state
                FSlateBrush NewBrush;
                NewBrush.SetResourceObject(UnselectedTexture);
                NewBrush.DrawAs = ESlateBrushDrawType::Image;
                NewBrush.Tiling = ESlateBrushTileType::NoTile;

                VehicleSelectImage->SetBrush(NewBrush);
                // Applied UNSELECTED image
            }
            else
            {
                UE_LOG(LogRealGazeboUI, Warning, TEXT("VehicleEntry[%p]: Failed to load UnselectedImage for '%s'"), this, *VehicleName);
            }
        }
        else
        {
            // Cleared selection image
        }
    }
}

