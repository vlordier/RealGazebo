// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#include "Core/RealGazeboUISubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "ViewerController/RealGazeboViewerDirector.h"
#include "Widgets/RealGazeboMainWidget.h"
#include "Data/VehicleTypeImageData.h"
#include "RealGazeboUI.h"

DEFINE_LOG_CATEGORY_STATIC(LogRealGazeboUISubsystem, Log, All);

URealGazeboUISubsystem::URealGazeboUISubsystem()
{
}

void URealGazeboUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UIStatus = TEXT("Initialized");
    UE_LOG(LogRealGazeboUISubsystem, Log, TEXT("RealGazebo UI Subsystem initialized"));
}

void URealGazeboUISubsystem::Deinitialize()
{
    // Cleanup all UI elements
    InternalCleanup();

    UIStatus = TEXT("Deinitialized");
    UE_LOG(LogRealGazeboUISubsystem, Log, TEXT("RealGazebo UI Subsystem deinitialized"));

    Super::Deinitialize();
}

bool URealGazeboUISubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    return true;
}

UUserWidget* URealGazeboUISubsystem::CreateMainWidget(TSubclassOf<UUserWidget> WidgetClass, UDataTable* VehicleTypeImageDataTable)
{
    if (!WidgetClass)
    {
        UE_LOG(LogRealGazeboUISubsystem, Warning, TEXT("Cannot create widget - no widget class specified"));
        OnUISetupFailed.Broadcast(TEXT("No widget class specified"));
        return nullptr;
    }

    APlayerController* PC = GetPlayerController();
    if (!PC)
    {
        UE_LOG(LogRealGazeboUISubsystem, Warning, TEXT("Cannot create widget - no player controller available"));
        OnUISetupFailed.Broadcast(TEXT("No player controller available"));
        return nullptr;
    }

    // Create the widget
    UUserWidget* NewWidget = CreateWidget<UUserWidget>(PC, WidgetClass);
    if (!NewWidget)
    {
        UE_LOG(LogRealGazeboUISubsystem, Error, TEXT("Failed to create widget from class"));
        OnUISetupFailed.Broadcast(TEXT("Failed to create widget from class"));
        return nullptr;
    }

    // Configure with vehicle type data if provided
    if (VehicleTypeImageDataTable)
    {
        ConfigureWidgetWithVehicleData(NewWidget, VehicleTypeImageDataTable);
    }

    // Setup viewer director integration if we have one
    if (ViewerDirector)
    {
        SetupViewerDirectorIntegration(NewWidget);
    }

    // Set as active main widget
    ActiveMainWidget = NewWidget;

    UpdateStatusDisplay();
    UE_LOG(LogRealGazeboUISubsystem, Log, TEXT("Main widget created successfully"));

    return NewWidget;
}

void URealGazeboUISubsystem::AddWidgetToViewport(UUserWidget* Widget, int32 ZOrder)
{
    if (!Widget)
    {
        UE_LOG(LogRealGazeboUISubsystem, Warning, TEXT("Cannot add null widget to viewport"));
        return;
    }

    if (WidgetsInViewport.Contains(Widget))
    {
        UE_LOG(LogRealGazeboUISubsystem, Warning, TEXT("Widget is already in viewport"));
        return;
    }

    Widget->AddToViewport(ZOrder);
    WidgetsInViewport.Add(Widget);

    UpdateStatusDisplay();
    UE_LOG(LogRealGazeboUISubsystem, Log, TEXT("Widget added to viewport with Z-order: %d"), ZOrder);
}

void URealGazeboUISubsystem::RemoveWidgetFromViewport(UUserWidget* Widget)
{
    if (!Widget)
    {
        return;
    }

    if (WidgetsInViewport.Contains(Widget))
    {
        Widget->RemoveFromParent();
        WidgetsInViewport.Remove(Widget);

        UpdateStatusDisplay();
        UE_LOG(LogRealGazeboUISubsystem, Log, TEXT("Widget removed from viewport"));
    }
}

bool URealGazeboUISubsystem::IsWidgetInViewport(UUserWidget* Widget) const
{
    return Widget && WidgetsInViewport.Contains(Widget);
}

void URealGazeboUISubsystem::InitializeCameraUI(TSubclassOf<UUserWidget> WidgetClass, UDataTable* VehicleTypeImageDataTable,
                                              const FVector& InitialCameraLocation, const FRotator& InitialCameraRotation,
                                              bool bCreateViewerDirector, int32 WidgetZOrder)
{
    // Cleanup any existing UI first
    CleanupCameraUI();

    // Create viewer director if requested
    if (bCreateViewerDirector)
    {
        CreateViewerDirector(InitialCameraLocation, InitialCameraRotation);
    }

    // Create main widget
    UUserWidget* MainWidget = CreateMainWidget(WidgetClass, VehicleTypeImageDataTable);
    if (!MainWidget)
    {
        OnUISetupFailed.Broadcast(TEXT("Failed to create main widget"));
        return;
    }

    // Add to viewport
    AddWidgetToViewport(MainWidget, WidgetZOrder);

    // Ensure mouse cursor is visible
    EnsureMouseCursorVisible();

    UIStatus = TEXT("Camera UI Active");
    OnUICreated.Broadcast();
    UE_LOG(LogRealGazeboUISubsystem, Log, TEXT("Camera UI initialized successfully"));
}

void URealGazeboUISubsystem::CleanupCameraUI()
{
    InternalCleanup();
    OnUICleanedUp.Broadcast();
    UE_LOG(LogRealGazeboUISubsystem, Log, TEXT("Camera UI cleaned up"));
}

ARealGazeboViewerDirector* URealGazeboUISubsystem::CreateViewerDirector(const FVector& InitialLocation, const FRotator& InitialRotation)
{
    if (ViewerDirector)
    {
        UE_LOG(LogRealGazeboUISubsystem, Warning, TEXT("Viewer director already exists"));
        return ViewerDirector;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogRealGazeboUISubsystem, Error, TEXT("Cannot create viewer director - no world available"));
        return nullptr;
    }

    FActorSpawnParameters SpawnParams;
    // Note: No fixed Name set. A hard-coded name collides on PIE re-runs because a
    // Destroy()'d director keeps its name until GC, causing a fatal "cannot generate
    // unique name" error. Let the engine auto-assign a unique name instead.
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    ViewerDirector = World->SpawnActor<ARealGazeboViewerDirector>(
        ARealGazeboViewerDirector::StaticClass(),
        InitialLocation,
        InitialRotation,
        SpawnParams
    );

    if (ViewerDirector)
    {
        // Configure viewer director settings using public method
        ViewerDirector->SetInitialCameraSettings(InitialLocation, InitialRotation);

        UE_LOG(LogRealGazeboUISubsystem, Log, TEXT("Viewer director created at location: %s"), *InitialLocation.ToString());
    }
    else
    {
        UE_LOG(LogRealGazeboUISubsystem, Error, TEXT("Failed to spawn viewer director"));
    }

    return ViewerDirector;
}

void URealGazeboUISubsystem::SetMouseCursorAlwaysVisible(bool bVisible)
{
    bMouseCursorAlwaysVisible = bVisible;

    if (bVisible)
    {
        EnsureMouseCursorVisible();
    }

    UE_LOG(LogRealGazeboUISubsystem, Log, TEXT("Mouse cursor always visible set to: %s"), bVisible ? TEXT("true") : TEXT("false"));
}

void URealGazeboUISubsystem::EnsureMouseCursorVisible()
{
    APlayerController* PC = GetPlayerController();
    if (PC)
    {
        PC->bShowMouseCursor = true;
        PC->bEnableClickEvents = true;
        PC->bEnableMouseOverEvents = true;

        // Set input mode for UI interaction
        FInputModeGameAndUI InputMode;
        InputMode.SetHideCursorDuringCapture(false);
        PC->SetInputMode(InputMode);

        UE_LOG(LogRealGazeboUISubsystem, Log, TEXT("Mouse cursor visibility ensured"));
    }
}

bool URealGazeboUISubsystem::IsUIActive() const
{
    return ActiveMainWidget != nullptr && WidgetsInViewport.Num() > 0;
}

bool URealGazeboUISubsystem::ValidateUISetup(TSubclassOf<UUserWidget> WidgetClass, UDataTable* VehicleTypeImageDataTable) const
{
    // Check widget class
    if (!WidgetClass)
    {
        UE_LOG(LogRealGazeboUISubsystem, Warning, TEXT("Widget class is not specified"));
        return false;
    }

    // Check vehicle type data table if provided
    if (VehicleTypeImageDataTable && !ValidateVehicleTypeImageDataTable(VehicleTypeImageDataTable))
    {
        return false;
    }

    // Check if player controller is available
    if (!GetPlayerController())
    {
        UE_LOG(LogRealGazeboUISubsystem, Warning, TEXT("No player controller available"));
        return false;
    }

    return true;
}

URealGazeboUISubsystem* URealGazeboUISubsystem::GetUISubsystem(const UObject* WorldContext)
{
    if (!WorldContext)
    {
        return nullptr;
    }

    if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull))
    {
        if (UGameInstance* GameInstance = World->GetGameInstance())
        {
            return GameInstance->GetSubsystem<URealGazeboUISubsystem>();
        }
    }

    return nullptr;
}

void URealGazeboUISubsystem::ConfigureWidgetWithVehicleData(UUserWidget* Widget, UDataTable* VehicleTypeImageDataTable)
{
    if (!Widget || !VehicleTypeImageDataTable)
    {
        return;
    }

    // Use reflection to find and set VehicleTypeImageDataTable property
    if (FObjectProperty* Property = FindFProperty<FObjectProperty>(Widget->GetClass(), TEXT("VehicleTypeImageDataTable")))
    {
        Property->SetObjectPropertyValue_InContainer(Widget, VehicleTypeImageDataTable);
        UE_LOG(LogRealGazeboUISubsystem, Log, TEXT("Widget configured with vehicle type image data"));
    }
    else
    {
        UE_LOG(LogRealGazeboUISubsystem, Warning, TEXT("Widget does not have VehicleTypeImageDataTable property"));
    }
}

void URealGazeboUISubsystem::SetupViewerDirectorIntegration(UUserWidget* Widget)
{
    if (!Widget || !ViewerDirector)
    {
        return;
    }

    // Try to cast to RealGazeboMainWidget and set ViewerDirector reference
    if (URealGazeboMainWidget* MainWidgetInstance = Cast<URealGazeboMainWidget>(Widget))
    {
        MainWidgetInstance->SetViewerDirector(ViewerDirector);
        UE_LOG(LogRealGazeboUISubsystem, Log, TEXT("Widget configured with viewer director integration"));
    }
    else
    {
        UE_LOG(LogRealGazeboUISubsystem, Warning, TEXT("Widget is not RealGazeboMainWidget - ViewerDirector integration not available"));
    }
}

APlayerController* URealGazeboUISubsystem::GetPlayerController() const
{
    if (UWorld* World = GetWorld())
    {
        return World->GetFirstPlayerController();
    }
    return nullptr;
}

bool URealGazeboUISubsystem::ValidateVehicleTypeImageDataTable(UDataTable* DataTable) const
{
    if (!DataTable)
    {
        return false;
    }

    // Check if DataTable uses the correct row structure
    if (DataTable->GetRowStruct() != FVehicleTypeImageRow::StaticStruct())
    {
        UE_LOG(LogRealGazeboUISubsystem, Warning, TEXT("DataTable must use FVehicleTypeImageRow structure"));
        return false;
    }

    // Check if DataTable has at least one row
    if (DataTable->GetRowNames().Num() == 0)
    {
        UE_LOG(LogRealGazeboUISubsystem, Warning, TEXT("DataTable is empty - no vehicle type configurations found"));
        return false;
    }

    return true;
}

void URealGazeboUISubsystem::UpdateStatusDisplay()
{
    if (IsUIActive())
    {
        UIStatus = FString::Printf(TEXT("Active - Widgets: %d | Main Widget: %s"),
                                  WidgetsInViewport.Num(),
                                  ActiveMainWidget ? TEXT("Set") : TEXT("None"));
    }
    else
    {
        UIStatus = TEXT("Inactive");
    }
}

void URealGazeboUISubsystem::InternalCleanup()
{
    // Remove all widgets from viewport
    for (UUserWidget* Widget : WidgetsInViewport)
    {
        if (Widget)
        {
            Widget->RemoveFromParent();
        }
    }
    WidgetsInViewport.Empty();

    // Clear active main widget
    ActiveMainWidget = nullptr;

    // Destroy viewer director if we created it
    if (ViewerDirector)
    {
        ViewerDirector->Destroy();
        ViewerDirector = nullptr;
    }

    UIStatus = TEXT("Cleaned Up");
}