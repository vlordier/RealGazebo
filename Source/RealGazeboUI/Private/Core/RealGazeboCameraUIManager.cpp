// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Core/RealGazeboCameraUIManager.h"
#include "Core/RealGazeboUISubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "ViewerController/RealGazeboViewerDirector.h"
#include "Widgets/RealGazeboMainWidget.h"
#include "RealGazeboUI.h"

DEFINE_LOG_CATEGORY_STATIC(LogRealGazeboCameraUIManager, Log, All);

ARealGazeboCameraUIManager::ARealGazeboCameraUIManager()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 1.0f; // Update status every second

    // Set default values
    VehicleTypeImageDataTable = nullptr;
    MainWidgetClass = nullptr;
    bAutoCreateUI = true;
    bAutoAddToViewport = true;
    WidgetZOrder = 0;
    bAutoCreateViewerDirector = true;
    bAlwaysShowMouseCursor = true;

    // Initialize status
    UIStatus = TEXT("Not Started");
    WidgetInViewportStatus = false;

    // Initialize default camera presets
    CameraPresets.Empty();

    // Preset 0 - VILS (Keyboard: 1)
    FCameraPreset VILSPreset;
    VILSPreset.PresetName = TEXT("VILS");
    VILSPreset.Location = FVector(-3372.325356f, 1064.508698f, 10919.941478f);
    VILSPreset.Rotation = FRotator(-89.900002f, -91.111628f, -0.000000f);
    CameraPresets.Add(VILSPreset);

    // Preset 1 - Urban (Keyboard: 2)
    FCameraPreset UrbanPreset;
    UrbanPreset.PresetName = TEXT("Urban");
    UrbanPreset.Location = FVector(-31214.548424f, -16235.116186f, 18249.156533f);
    UrbanPreset.Rotation = FRotator(-89.900002f, 172.132914f, -0.000000f);
    CameraPresets.Add(UrbanPreset);

    // Preset 2 - C-Track (Keyboard: 3)
    FCameraPreset CTrackPreset;
    CTrackPreset.PresetName = TEXT("C-Track");
    CTrackPreset.Location = FVector(-59082.572512f, 4478.376668f, 23009.524176f);
    CTrackPreset.Rotation = FRotator(-32.180821f, -0.412282f, -0.000000f);
    CameraPresets.Add(CTrackPreset);
}

void ARealGazeboCameraUIManager::BeginPlay()
{
    Super::BeginPlay();

    // Get reference to the UI subsystem
    UISubsystem = URealGazeboUISubsystem::GetUISubsystem(this);

    if (!UISubsystem.IsValid())
    {
        UE_LOG(LogRealGazeboCameraUIManager, Error, TEXT("Failed to get RealGazeboUISubsystem! Make sure the plugin is properly loaded."));
        return;
    }

    // Validate configuration
    if (!ValidateConfiguration())
    {
        UE_LOG(LogRealGazeboCameraUIManager, Warning, TEXT("Configuration validation failed. UI will not start automatically."));
        return;
    }

    // Configure the subsystem with our settings
    ConfigureSubsystem();

    // Auto-initialize UI if enabled
    if (bAutoCreateUI)
    {
        InitializeCameraUI();
    }

    // Start status update timer
    GetWorld()->GetTimerManager().SetTimer(StatusUpdateTimer, this, &ARealGazeboCameraUIManager::UpdateStatusDisplay, 1.0f, true);
}

void ARealGazeboCameraUIManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Clean up timer
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(StatusUpdateTimer);
    }

    // Stop the UI if we started it
    if (bDidStartSubsystem && UISubsystem.IsValid())
    {
        CleanupCameraUI();
    }

    Super::EndPlay(EndPlayReason);
}

UUserWidget* ARealGazeboCameraUIManager::CreateMainWidget()
{
    if (!UISubsystem.IsValid())
    {
        UE_LOG(LogRealGazeboCameraUIManager, Error, TEXT("Cannot create widget - UI subsystem not available"));
        return nullptr;
    }

    if (!ValidateConfiguration())
    {
        UE_LOG(LogRealGazeboCameraUIManager, Error, TEXT("Cannot create widget - invalid configuration"));
        return nullptr;
    }

    // Use subsystem to create the widget
    UUserWidget* CreatedWidget = UISubsystem->CreateMainWidget(MainWidgetClass, VehicleTypeImageDataTable);
    if (CreatedWidget)
    {
        UE_LOG(LogRealGazeboCameraUIManager, Log, TEXT("Main widget created through subsystem"));
    }
    else
    {
        UE_LOG(LogRealGazeboCameraUIManager, Error, TEXT("Failed to create widget through subsystem"));
    }

    return CreatedWidget;
}

void ARealGazeboCameraUIManager::AddMainWidgetToViewport()
{
    if (!UISubsystem.IsValid())
    {
        UE_LOG(LogRealGazeboCameraUIManager, Error, TEXT("Cannot add widget to viewport - UI subsystem not available"));
        return;
    }

    UUserWidget* MainWidget = UISubsystem->GetActiveMainWidget();
    if (!MainWidget)
    {
        UE_LOG(LogRealGazeboCameraUIManager, Warning, TEXT("Cannot add to viewport - no active main widget"));
        return;
    }

    if (UISubsystem->IsWidgetInViewport(MainWidget))
    {
        UE_LOG(LogRealGazeboCameraUIManager, Log, TEXT("Widget already in viewport"));
        return;
    }

    // Use subsystem to add to viewport
    UISubsystem->AddWidgetToViewport(MainWidget, WidgetZOrder);
    UE_LOG(LogRealGazeboCameraUIManager, Log, TEXT("Widget added to viewport through subsystem with Z-order %d"), WidgetZOrder);
}

void ARealGazeboCameraUIManager::InitializeCameraUI()
{
    if (!UISubsystem.IsValid())
    {
        UE_LOG(LogRealGazeboCameraUIManager, Error, TEXT("Cannot initialize UI - subsystem not available"));
        return;
    }

    if (!ValidateConfiguration())
    {
        UE_LOG(LogRealGazeboCameraUIManager, Error, TEXT("Cannot initialize UI - invalid configuration"));
        return;
    }

    // Configure subsystem with our settings
    ConfigureSubsystem();

    // Use subsystem to initialize complete camera UI
    UISubsystem->InitializeCameraUI(
        MainWidgetClass,
        VehicleTypeImageDataTable,
        InitialCameraLocation,
        InitialCameraRotation,
        bAutoCreateViewerDirector,
        bAutoAddToViewport ? WidgetZOrder : -1 // -1 means don't add to viewport
    );

    // Configure camera presets on the viewer director
    ARealGazeboViewerDirector* Director = UISubsystem->GetViewerDirector();
    if (Director && CameraPresets.Num() > 0)
    {
        Director->SetCameraPresets(CameraPresets);
        UE_LOG(LogRealGazeboCameraUIManager, Log, TEXT("Configured %d camera presets on ViewerDirector"),
               CameraPresets.Num());
    }

    bDidStartSubsystem = true;

    // Add to viewport separately if auto-add is disabled (for manual control)
    if (!bAutoAddToViewport)
    {
        // Widget created but not added to viewport - user can control this manually
        UE_LOG(LogRealGazeboCameraUIManager, Log, TEXT("UI created but not added to viewport (auto-add disabled)"));
    }

    // Ensure mouse cursor is visible
    if (bAlwaysShowMouseCursor)
    {
        SetMouseCursorAlwaysVisible(true);
    }

    UE_LOG(LogRealGazeboCameraUIManager, Log, TEXT("Camera UI initialized through subsystem"));

    // Fire success event
    OnUICreated();
}

void ARealGazeboCameraUIManager::CleanupCameraUI()
{
    if (UISubsystem.IsValid())
    {
        UISubsystem->CleanupCameraUI();
        bDidStartSubsystem = false;
        UE_LOG(LogRealGazeboCameraUIManager, Log, TEXT("Camera UI cleaned up through subsystem"));
    }

    OnUICleanedUp();
}

bool ARealGazeboCameraUIManager::ValidateSetup()
{
    return ValidateConfiguration();
}

bool ARealGazeboCameraUIManager::IsUIActive() const
{
    return UISubsystem.IsValid() && UISubsystem->IsUIActive();
}


APlayerController* ARealGazeboCameraUIManager::GetPlayerController() const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    return World->GetFirstPlayerController();
}

bool ARealGazeboCameraUIManager::ValidateVehicleTypeImageDataTable() const
{
    if (!VehicleTypeImageDataTable)
    {
        return true; // Null is allowed (optional feature)
    }

    // Check if DataTable has the correct row structure
    if (VehicleTypeImageDataTable->GetRowStruct() != FVehicleTypeImageRow::StaticStruct())
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("CameraUIManager: VehicleTypeImageDataTable has incorrect row structure. Expected: FVehicleTypeImageRow"));
        return false;
    }

    // Check if DataTable has any rows
    TArray<FVehicleTypeImageRow*> AllRows;
    VehicleTypeImageDataTable->GetAllRows<FVehicleTypeImageRow>(TEXT("Validation"), AllRows);

    if (AllRows.Num() == 0)
    {
        UE_LOG(LogRealGazeboUI, Display, TEXT("CameraUIManager: VehicleTypeImageDataTable is empty - no vehicle type images configured"));
    }
    else
    {
        UE_LOG(LogRealGazeboUI, Display, TEXT("CameraUIManager: VehicleTypeImageDataTable validated successfully with %d vehicle types"), AllRows.Num());
    }

    return true;
}





//----------------------------------------------------------
// Missing Method Implementations
//----------------------------------------------------------

void ARealGazeboCameraUIManager::ConfigureSubsystem()
{
    if (!UISubsystem.IsValid())
    {
        return;
    }

    // Configure mouse cursor settings
    UISubsystem->SetMouseCursorAlwaysVisible(bAlwaysShowMouseCursor);

    UE_LOG(LogRealGazeboCameraUIManager, Log, TEXT("Subsystem configured - WidgetClass: %s, DataTable: %s"),
           MainWidgetClass ? *MainWidgetClass->GetName() : TEXT("None"),
           VehicleTypeImageDataTable ? *VehicleTypeImageDataTable->GetName() : TEXT("None"));
}

UUserWidget* ARealGazeboCameraUIManager::GetMainWidget() const
{
    if (UISubsystem.IsValid())
    {
        return UISubsystem->GetActiveMainWidget();
    }
    return nullptr;
}

ARealGazeboViewerDirector* ARealGazeboCameraUIManager::GetViewerDirector() const
{
    if (UISubsystem.IsValid())
    {
        return UISubsystem->GetViewerDirector();
    }
    return nullptr;
}

bool ARealGazeboCameraUIManager::ValidateConfiguration() const
{
    // Check MainWidgetClass
    if (!MainWidgetClass)
    {
        UE_LOG(LogRealGazeboCameraUIManager, Warning, TEXT("MainWidgetClass is not set"));
        return false;
    }

    // Check PlayerController availability
    if (!GetPlayerController())
    {
        UE_LOG(LogRealGazeboCameraUIManager, Warning, TEXT("No PlayerController available"));
        return false;
    }

    // Validate VehicleTypeImageDataTable if provided
    if (!ValidateVehicleTypeImageDataTable())
    {
        return false;
    }

    // Validate camera configuration
    if (!ValidateCameraConfiguration())
    {
        return false;
    }

    return true;
}

bool ARealGazeboCameraUIManager::ValidateWidgetConfiguration() const
{
    if (!MainWidgetClass)
    {
        UE_LOG(LogRealGazeboCameraUIManager, Warning, TEXT("MainWidgetClass is not configured"));
        return false;
    }

    return true;
}

bool ARealGazeboCameraUIManager::ValidateCameraConfiguration() const
{
    // Validate camera location and rotation values are reasonable
    if (InitialCameraLocation.IsZero() && InitialCameraRotation.IsZero())
    {
        UE_LOG(LogRealGazeboCameraUIManager, Warning, TEXT("Camera location and rotation are both zero - may not be intended"));
    }

    return true;
}

void ARealGazeboCameraUIManager::UpdateStatusDisplay()
{
    if (IsUIActive())
    {
        UUserWidget* MainWidget = GetMainWidget();
        ARealGazeboViewerDirector* Director = GetViewerDirector();
        WidgetInViewportStatus = UISubsystem.IsValid() && UISubsystem->IsWidgetInViewport(MainWidget);
        UIStatus = FString::Printf(TEXT("Active - Widget: %s | Director: %s"),
                                  MainWidget ? TEXT("Created") : TEXT("None"),
                                  Director ? TEXT("Created") : TEXT("None"));
    }
    else
    {
        UIStatus = TEXT("Inactive");
        WidgetInViewportStatus = false;
    }
}

void ARealGazeboCameraUIManager::SetMouseCursorAlwaysVisible(bool bVisible)
{
    bAlwaysShowMouseCursor = bVisible;

    if (UISubsystem.IsValid())
    {
        UISubsystem->SetMouseCursorAlwaysVisible(bVisible);
    }

    UE_LOG(LogRealGazeboCameraUIManager, Log, TEXT("Mouse cursor always visible set to: %s"), bVisible ? TEXT("true") : TEXT("false"));
}

#if WITH_EDITOR
void ARealGazeboCameraUIManager::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    // Re-configure subsystem when properties change in editor
    if (UISubsystem.IsValid() && PropertyChangedEvent.Property)
    {
        ConfigureSubsystem();
    }
}
#endif

void ARealGazeboCameraUIManager::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    // Tick is used for status updates via timer
}