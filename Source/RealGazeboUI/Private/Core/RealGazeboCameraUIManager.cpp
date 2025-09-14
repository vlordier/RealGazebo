// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Sub-author: MinKyu Kim
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "Core/RealGazeboCameraUIManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "ViewerController/RealGazeboViewerDirector.h"
#include "Widgets/RealGazeboMainWidget.h"
#include "RealGazeboUI.h"

ARealGazeboCameraUIManager::ARealGazeboCameraUIManager()
{
    PrimaryActorTick.bCanEverTick = false;
    PrimaryActorTick.bStartWithTickEnabled = false;

    // Set default values
    VehicleTypeImageDataTable = nullptr;
    MainWidgetClass = nullptr;
    MainWidget = nullptr;
    ViewerDirector = nullptr;

    bAutoCreateUI = true;
    bAutoAddToViewport = true;
    WidgetZOrder = 0;
    bWidgetInViewport = false;
    bAlwaysShowMouseCursor = true;
}

void ARealGazeboCameraUIManager::BeginPlay()
{
    Super::BeginPlay();

    // Ensure mouse cursor is visible from the start
    EnsureMouseCursorVisible();

    // Auto-initialize UI if enabled
    if (bAutoCreateUI)
    {
        InitializeCameraUI();
    }
}

void ARealGazeboCameraUIManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Clean up UI
    InternalCleanup();

    Super::EndPlay(EndPlayReason);
}

UUserWidget* ARealGazeboCameraUIManager::CreateMainWidget()
{
    // Validate prerequisites
    if (!MainWidgetClass)
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("CameraUIManager: Cannot create widget - MainWidgetClass is not set"));
        OnUISetupFailed(TEXT("MainWidgetClass is not configured"));
        return nullptr;
    }

    APlayerController* PlayerController = GetPlayerController();
    if (!PlayerController)
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("CameraUIManager: Cannot create widget - No PlayerController found"));
        OnUISetupFailed(TEXT("No PlayerController available"));
        return nullptr;
    }

    // Clean up existing widget if any
    if (MainWidget)
    {
        InternalCleanup();
    }

    // Create the main widget
    MainWidget = CreateWidget<UUserWidget>(PlayerController, MainWidgetClass);
    if (!MainWidget)
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("CameraUIManager: Failed to create MainWidget instance"));
        OnUISetupFailed(TEXT("Failed to create widget instance"));
        return nullptr;
    }

    // Configure the widget with our settings
    ConfigureMainWidget(MainWidget);

    UE_LOG(LogRealGazeboUI, Display, TEXT("CameraUIManager: Main widget created successfully"));
    return MainWidget;
}

void ARealGazeboCameraUIManager::AddMainWidgetToViewport()
{
    if (!MainWidget)
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("CameraUIManager: Cannot add to viewport - MainWidget is null"));
        OnUISetupFailed(TEXT("MainWidget is null - call CreateMainWidget first"));
        return;
    }

    if (bWidgetInViewport)
    {
        UE_LOG(LogRealGazeboUI, Display, TEXT("CameraUIManager: Widget already in viewport"));
        return;
    }

    // Add to viewport with specified Z-order
    MainWidget->AddToViewport(WidgetZOrder);
    bWidgetInViewport = true;

    UE_LOG(LogRealGazeboUI, Display, TEXT("CameraUIManager: Widget added to viewport with Z-order %d"), WidgetZOrder);
}

void ARealGazeboCameraUIManager::InitializeCameraUI()
{
    // Validate setup first
    if (!ValidateSetup())
    {
        OnUISetupFailed(TEXT("Setup validation failed"));
        return;
    }

    // Create ViewerDirector first if enabled
    if (bAutoCreateViewerDirector)
    {
        CreateViewerDirector();
    }

    // Create the main widget (now ViewerDirector is available for integration)
    UUserWidget* CreatedWidget = CreateMainWidget();
    if (!CreatedWidget)
    {
        // Error already logged and event called in CreateMainWidget
        return;
    }

    // Add to viewport if enabled
    if (bAutoAddToViewport)
    {
        AddMainWidgetToViewport();
    }

    // Ensure mouse cursor is visible after UI creation
    EnsureMouseCursorVisible();

    // Fire success event
    OnUICreated();
    UE_LOG(LogRealGazeboUI, Display, TEXT("CameraUIManager: Camera UI initialized successfully"));
}

void ARealGazeboCameraUIManager::CleanupCameraUI()
{
    InternalCleanup();
    OnUICleanedUp();
    UE_LOG(LogRealGazeboUI, Display, TEXT("CameraUIManager: Camera UI cleaned up"));
}

bool ARealGazeboCameraUIManager::ValidateSetup()
{
    // Check MainWidgetClass
    if (!MainWidgetClass)
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("CameraUIManager: MainWidgetClass is not set"));
        return false;
    }

    // Check PlayerController availability
    if (!GetPlayerController())
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("CameraUIManager: No PlayerController available"));
        return false;
    }

    // Validate VehicleTypeImageDataTable if provided
    if (VehicleTypeImageDataTable && !ValidateVehicleTypeImageDataTable())
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("CameraUIManager: VehicleTypeImageDataTable validation failed"));
        return false;
    }

    return true;
}

bool ARealGazeboCameraUIManager::IsUIActive() const
{
    return MainWidget != nullptr && bWidgetInViewport;
}

void ARealGazeboCameraUIManager::ConfigureMainWidget(UUserWidget* Widget)
{
    if (!Widget)
    {
        return;
    }

    // Setup VehicleTypeImageDataTable integration
    SetupVehicleTypeImages(Widget);

    // Setup ViewerDirector integration
    SetupViewerDirectorIntegration(Widget);

    // Ensure mouse cursor is visible after widget configuration
    EnsureMouseCursorVisible();

    // Additional widget configuration can be added here
    // For example: setting update intervals, themes, etc.
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

void ARealGazeboCameraUIManager::SetupVehicleTypeImages(UUserWidget* Widget)
{
    if (!Widget)
    {
        return;
    }

    // If we have a VehicleTypeImageDataTable, configure the widget to use it
    if (VehicleTypeImageDataTable)
    {
        // Try to cast to RealGazeboMainWidget and configure it
        if (URealGazeboMainWidget* MainWidgetInstance = Cast<URealGazeboMainWidget>(Widget))
        {
            MainWidgetInstance->SetVehicleTypeImageDataTable(VehicleTypeImageDataTable);
            UE_LOG(LogRealGazeboUI, Display, TEXT("CameraUIManager: Configured MainWidget with VehicleTypeImageDataTable"));
        }
        else
        {
            // For custom widgets that might inherit from RealGazeboMainWidget or have similar functionality
            // This allows for flexibility with custom widget implementations
            UE_LOG(LogRealGazeboUI, Display, TEXT("CameraUIManager: Custom widget detected - DataTable configuration may need custom implementation"));
        }
    }
    else
    {
        UE_LOG(LogRealGazeboUI, Display, TEXT("CameraUIManager: No VehicleTypeImageDataTable provided - widgets will use default images"));
    }
}

void ARealGazeboCameraUIManager::SetupViewerDirectorIntegration(UUserWidget* Widget)
{
    if (!Widget)
    {
        return;
    }

    // Try to cast to RealGazeboMainWidget and set ViewerDirector reference
    if (URealGazeboMainWidget* MainWidgetInstance = Cast<URealGazeboMainWidget>(Widget))
    {
        if (ViewerDirector)
        {
            MainWidgetInstance->SetViewerDirector(ViewerDirector);
            UE_LOG(LogRealGazeboUI, Display, TEXT("CameraUIManager: Configured MainWidget with ViewerDirector for camera integration"));
        }
        else
        {
            UE_LOG(LogRealGazeboUI, Warning, TEXT("CameraUIManager: ViewerDirector not available - camera integration disabled"));
        }
    }
    else
    {
        // For custom widgets that might not inherit from RealGazeboMainWidget
        UE_LOG(LogRealGazeboUI, Display, TEXT("CameraUIManager: Custom widget detected - ViewerDirector integration may need custom implementation"));
    }
}

void ARealGazeboCameraUIManager::CreateViewerDirector()
{
    if (ViewerDirector)
    {
        UE_LOG(LogRealGazeboUI, Display, TEXT("CameraUIManager: ViewerDirector already exists"));
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("CameraUIManager: Cannot create ViewerDirector - No World available"));
        return;
    }

    // Spawn ViewerDirector
    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = this;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.Name = FName("RealGazeboViewerDirector");

    ViewerDirector = World->SpawnActor<ARealGazeboViewerDirector>(ARealGazeboViewerDirector::StaticClass(),
                                                                  GetActorLocation(),
                                                                  GetActorRotation(),
                                                                  SpawnParams);
    if (ViewerDirector)
    {
        // Configure ViewerDirector with our camera settings
        ViewerDirector->SetInitialCameraSettings(InitialCameraLocation, InitialCameraRotation);

        UE_LOG(LogRealGazeboUI, Display, TEXT("CameraUIManager: ViewerDirector created and configured with camera settings"));

        // Ensure mouse cursor stays visible even with ViewerDirector camera controls
        EnsureMouseCursorVisible();
    }
    else
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("CameraUIManager: Failed to create ViewerDirector"));
    }
}

void ARealGazeboCameraUIManager::InternalCleanup()
{
    // Remove from viewport if present
    if (MainWidget && bWidgetInViewport)
    {
        MainWidget->RemoveFromParent();
        bWidgetInViewport = false;
    }

    // Clean up ViewerDirector
    if (ViewerDirector)
    {
        ViewerDirector->Destroy();
        ViewerDirector = nullptr;
    }

    // Clear widget reference
    MainWidget = nullptr;
}

//----------------------------------------------------------
// Mouse Cursor Control Functions
//----------------------------------------------------------

void ARealGazeboCameraUIManager::EnsureMouseCursorVisible()
{
    if (!bAlwaysShowMouseCursor)
    {
        UE_LOG(LogRealGazeboUI, Display, TEXT("CameraUIManager: Mouse cursor control disabled - bAlwaysShowMouseCursor is false"));
        return;
    }

    APlayerController* PlayerController = GetPlayerController();
    if (!PlayerController)
    {
        UE_LOG(LogRealGazeboUI, Warning, TEXT("CameraUIManager: Cannot control mouse cursor - No PlayerController available"));
        return;
    }

    // Set mouse cursor to always visible
    PlayerController->SetShowMouseCursor(true);
    PlayerController->bEnableClickEvents = true;
    PlayerController->bEnableMouseOverEvents = true;

    UE_LOG(LogRealGazeboUI, Display, TEXT("CameraUIManager: Mouse cursor set to always visible"));
}

void ARealGazeboCameraUIManager::SetMouseCursorAlwaysVisible(bool bVisible)
{
    bAlwaysShowMouseCursor = bVisible;

    if (bVisible)
    {
        EnsureMouseCursorVisible();
        UE_LOG(LogRealGazeboUI, Display, TEXT("CameraUIManager: Mouse cursor always visible enabled"));
    }
    else
    {
        UE_LOG(LogRealGazeboUI, Display, TEXT("CameraUIManager: Mouse cursor always visible disabled"));
    }
}