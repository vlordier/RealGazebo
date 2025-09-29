// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Sub-author: MinKyu Kim
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Core/RealGazeboManager.h"
#include "RealGazebo.h"
#include "Engine/World.h"
#include "GazeboBridgeSubsystem.h"
#include "RealGazeboUISubsystem.h"
#include "Vehicles/VehiclePoolManager.h"
#include "ViewerController/RealGazeboViewerDirector.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Components/Widget.h"
#include "Blueprint/UserWidget.h"

ARealGazeboManager::ARealGazeboManager()
{
    // Set this actor to call Tick() every frame
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;

    // Initialize state
    bDidStartBridge = false;
    bDidStartUI = false;

    // MainWidgetClass should be set manually by user in Details panel
    MainWidgetClass = nullptr;

    // Clear subsystem references
    BridgeSubsystem = nullptr;
    UISubsystem = nullptr;
    ViewerDirector = nullptr;
}

//----------------------------------------------------------
// Actor Lifecycle
//----------------------------------------------------------

void ARealGazeboManager::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Manager [%s]: BeginPlay"), *GetName());

    // Configuration ready for initialization

    // Get subsystem references
    if (UGameInstance* GameInstance = GetGameInstance())
    {
        BridgeSubsystem = GameInstance->GetSubsystem<UGazeboBridgeSubsystem>();
        UISubsystem = GameInstance->GetSubsystem<URealGazeboUISubsystem>();

        if (!BridgeSubsystem.IsValid())
        {
            UE_LOG(LogRealGazebo, Warning, TEXT("RealGazebo Manager [%s]: Bridge subsystem not found - Bridge features will be disabled"), *GetName());
        }

        if (!UISubsystem.IsValid())
        {
            UE_LOG(LogRealGazebo, Warning, TEXT("RealGazebo Manager [%s]: UI subsystem not found - UI features will be disabled"), *GetName());
        }
    }
    else
    {
        UE_LOG(LogRealGazebo, Error, TEXT("RealGazebo Manager [%s]: GameInstance not available"), *GetName());
        return;
    }

    // Configure subsystems with our settings
    ConfigureBridgeSubsystem();
    ConfigureUISubsystem();

    // Auto-start bridge if enabled
    if (bAutoStartBridge)
    {
        StartBridge();
    }

    // Auto-create UI if enabled
    if (bAutoCreateUI)
    {
        InitializeCameraUI();
    }

    // Start periodic status updates
    GetWorldTimerManager().SetTimer(StatusUpdateTimer, this, &ARealGazeboManager::UpdateStatusDisplay, 1.0f, true);
}

void ARealGazeboManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Manager [%s]: EndPlay"), *GetName());

    // Clear timer
    GetWorldTimerManager().ClearTimer(StatusUpdateTimer);

    // Cleanup UI
    CleanupCameraUI();

    // Stop bridge if we started it
    if (bDidStartBridge)
    {
        StopBridge();
    }

    // Clear references
    BridgeSubsystem = nullptr;
    UISubsystem = nullptr;
    ViewerDirector = nullptr;

    Super::EndPlay(EndPlayReason);
}

void ARealGazeboManager::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    // Tick logic can be added here if needed
}

#if WITH_EDITOR
void ARealGazeboManager::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    if (PropertyChangedEvent.Property != nullptr)
    {
        const FName PropertyName = PropertyChangedEvent.Property->GetFName();
        UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Manager [%s]: Property changed: %s"), *GetName(), *PropertyName.ToString());

        // Reconfigure subsystems when properties change
        if (BridgeSubsystem.IsValid() || UISubsystem.IsValid())
        {
            ConfigureBridgeSubsystem();
            ConfigureUISubsystem();
        }
    }
}
#endif

//----------------------------------------------------------
// Bridge Control API
//----------------------------------------------------------

void ARealGazeboManager::StartBridge()
{
    UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Manager [%s]: StartBridge"), *GetName());

    if (!BridgeSubsystem.IsValid())
    {
        UE_LOG(LogRealGazebo, Error, TEXT("Bridge subsystem not available"));
        BridgeStatus = TEXT("Error - No Subsystem");
        return;
    }

    // Configure Bridge settings before starting
    BridgeSubsystem->ListenPort = ListenPort;
    BridgeSubsystem->bAutoSpawnVehicles = bAutoSpawnVehicles;
    BridgeSubsystem->SetUpdateFrequency(UpdateFrequency);

    // CRITICAL: Set VehicleConfigTable so Bridge can spawn Blueprint vehicles instead of basic VehicleBasePawn
    BridgeSubsystem->VehicleConfigTable = VehicleDataTable;

    // Configure Vehicle Pool Settings
    ConfigureBridgePoolSettings();

    BridgeSubsystem->StartBridge();
    bDidStartBridge = BridgeSubsystem->IsBridgeActive();

    if (bDidStartBridge)
    {
        BridgeStatus = TEXT("Active");
        FBridgePoseData DummyData; // Create dummy data for event
        OnBridgeStarted.Broadcast(DummyData);
        UE_LOG(LogRealGazebo, Log, TEXT("Bridge started successfully"));
    }
    else
    {
        BridgeStatus = TEXT("Failed to Start");
        UE_LOG(LogRealGazebo, Warning, TEXT("Bridge failed to start"));
    }
}

void ARealGazeboManager::StopBridge()
{
    UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Manager [%s]: StopBridge"), *GetName());

    if (BridgeSubsystem.IsValid())
    {
        BridgeSubsystem->StopBridge();
        bDidStartBridge = false;
        BridgeStatus = TEXT("Stopped");
        FBridgePoseData DummyData; // Create dummy data for event
        OnBridgeStopped.Broadcast(DummyData);
    }
}

bool ARealGazeboManager::IsBridgeActive() const
{
    return BridgeSubsystem.IsValid() && BridgeSubsystem->IsBridgeActive();
}

void ARealGazeboManager::ClearAllVehicles()
{
    if (BridgeSubsystem.IsValid())
    {
        BridgeSubsystem->ClearAllVehicles();
        ActiveVehiclesCount = 0;
    }
}

//----------------------------------------------------------
// UI Control API
//----------------------------------------------------------

UUserWidget* ARealGazeboManager::CreateMainWidget()
{
    if (!UISubsystem.IsValid())
    {
        UE_LOG(LogRealGazebo, Warning, TEXT("UI subsystem not available - UI features disabled"));
        UIStatus = TEXT("No UI Subsystem");
        return nullptr;
    }

    if (!MainWidgetClass)
    {
        UE_LOG(LogRealGazebo, Warning, TEXT("MainWidgetClass not set - UI creation skipped"));
        UIStatus = TEXT("No Widget Class Set");
        return nullptr;
    }

    // Create widget through player controller
    if (APlayerController* PC = GetPlayerController())
    {
        UUserWidget* CreatedWidget = CreateWidget<UUserWidget>(PC, MainWidgetClass);
        if (CreatedWidget)
        {
            UE_LOG(LogRealGazebo, Log, TEXT("Main widget created successfully"));
            UIStatus = TEXT("Widget Created");
            return CreatedWidget;
        }
        else
        {
            UE_LOG(LogRealGazebo, Error, TEXT("Failed to create widget from MainWidgetClass"));
            UIStatus = TEXT("Widget Creation Failed");
            return nullptr;
        }
    }
    else
    {
        UE_LOG(LogRealGazebo, Error, TEXT("Player controller not available"));
        UIStatus = TEXT("No Player Controller");
        return nullptr;
    }
}

void ARealGazeboManager::AddMainWidgetToViewport()
{
    // This method would be called after CreateMainWidget succeeds
    // For now, just update status - actual widget management depends on the created widget
    WidgetInViewportStatus = true;
    UIStatus = TEXT("Widget in Viewport");
    UE_LOG(LogRealGazebo, Log, TEXT("Widget added to viewport (placeholder implementation)"));
}

void ARealGazeboManager::InitializeCameraUI()
{
    UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Manager [%s]: InitializeCameraUI"), *GetName());

    if (!UISubsystem.IsValid())
    {
        UE_LOG(LogRealGazebo, Error, TEXT("Cannot initialize Camera UI - UI subsystem not available"));
        return;
    }

    // CRITICAL: Use UISubsystem InitializeCameraUI to create ViewerDirector + MainWidget together
    UISubsystem->InitializeCameraUI(
        MainWidgetClass,
        VehicleTypeImageDataTable,
        InitialCameraLocation,
        InitialCameraRotation,
        bAutoCreateViewerDirector,
        bAutoAddToViewport ? WidgetZOrder : -1 // -1 means don't add to viewport
    );

    // Get the ViewerDirector reference from UISubsystem (it creates it automatically)
    ViewerDirector = UISubsystem->GetViewerDirector();
    if (ViewerDirector.IsValid())
    {
        UE_LOG(LogRealGazebo, Log, TEXT("ViewerDirector created: %s"), *ViewerDirector->GetName());
    }
    else
    {
        UE_LOG(LogRealGazebo, Warning, TEXT("ViewerDirector not available from UI subsystem"));
    }

    // Check if widget was created successfully
    UUserWidget* CreatedWidget = UISubsystem->GetActiveMainWidget();
    if (CreatedWidget)
    {
        UE_LOG(LogRealGazebo, Log, TEXT("Main widget created successfully"));
    }
    else
    {
        UE_LOG(LogRealGazebo, Warning, TEXT("Failed to create main widget"));
    }

    // Set mouse cursor visibility through UISubsystem
    UISubsystem->SetMouseCursorAlwaysVisible(bAlwaysShowMouseCursor);

    bDidStartUI = true;
    UIStatus = CreatedWidget ? TEXT("UI Active") : TEXT("UI Partial (No Widget)");

    // Note: OnUICreated() is a BlueprintImplementableEvent, so it's implemented in Blueprint
    // OnUICreated();
}

void ARealGazeboManager::CleanupCameraUI()
{
    UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Manager [%s]: CleanupCameraUI"), *GetName());

    if (UISubsystem.IsValid())
    {
        UISubsystem->CleanupCameraUI();
    }

    // Clear ViewerDirector reference (UISubsystem handles destruction)
    ViewerDirector = nullptr;

    // Update status
    WidgetInViewportStatus = false;
    bDidStartUI = false;
    UIStatus = TEXT("Cleaned Up");
    OnUICleanedUp();
}

void ARealGazeboManager::SetMouseCursorAlwaysVisible(bool bVisible)
{
    if (APlayerController* PC = GetPlayerController())
    {
        PC->bShowMouseCursor = bVisible;
        PC->bEnableClickEvents = bVisible;
        PC->bEnableMouseOverEvents = bVisible;
    }
}

//----------------------------------------------------------
// Status Information API
//----------------------------------------------------------

int32 ARealGazeboManager::GetActiveVehicleCount() const
{
    if (BridgeSubsystem.IsValid())
    {
        return BridgeSubsystem->GetActiveVehicleCount();
    }
    return 0;
}

void ARealGazeboManager::GetNetworkStatistics(int32& OutValidPackets, int32& OutInvalidPackets, float& OutPacketsPerSecond) const
{
    // Implementation would get stats from bridge subsystem
    OutValidPackets = 0;
    OutInvalidPackets = 0;
    OutPacketsPerSecond = 0.0f;
}

UUserWidget* ARealGazeboManager::GetMainWidget() const
{
    // Implementation would return the created widget
    return nullptr;
}

bool ARealGazeboManager::IsUIActive() const
{
    return bDidStartUI && UISubsystem.IsValid();
}

ARealGazeboViewerDirector* ARealGazeboManager::GetViewerDirector() const
{
    return ViewerDirector.Get();
}

bool ARealGazeboManager::ValidateSetup()
{
    // Validate essential data tables
    if (!ValidateVehicleDataTable())
    {
        return false;
    }

    // Validate UI configuration if enabled
    if (bAutoCreateUI && !ValidateUIConfiguration())
    {
        return false;
    }

    // Validate Bridge subsystem availability
    if (!ValidateBridgeSubsystem())
    {
        return false;
    }

    return true;
}

//----------------------------------------------------------
// Camera Control API
//----------------------------------------------------------

void ARealGazeboManager::SetCameraMode(int32 NewMode)
{
    if (ViewerDirector.IsValid())
    {
        // Implementation would set camera mode through viewer director
        UE_LOG(LogRealGazebo, Log, TEXT("Setting camera mode to: %d"), NewMode);
    }
}

int32 ARealGazeboManager::GetCameraMode() const
{
    if (ViewerDirector.IsValid())
    {
        // Implementation would get camera mode from viewer director
        return 0; // Default mode
    }
    return 0;
}

void ARealGazeboManager::SetMainWidgetVisibility(bool bVisible)
{
    if (bVisible)
    {
        if (!bDidStartUI)
        {
            InitializeCameraUI();
        }
        AddMainWidgetToViewport();
    }
    else
    {
        CleanupCameraUI();
    }
}

//----------------------------------------------------------
// Configuration Methods
//----------------------------------------------------------

void ARealGazeboManager::ConfigureBridgeSubsystem()
{
    if (!BridgeSubsystem.IsValid())
    {
        return;
    }

    UE_LOG(LogRealGazebo, Log, TEXT("Configuring Bridge Subsystem"));

    // Configure bridge subsystem with our settings
    // Note: Actual configuration would depend on BridgeSubsystem's API
    ConfigureVehiclePoolSettings();
    ConfigureNetworkProcessingSettings();
    ConfigurePerformanceAndDebugSettings();
}

void ARealGazeboManager::ConfigureUISubsystem()
{
    if (!UISubsystem.IsValid())
    {
        return;
    }

    UE_LOG(LogRealGazebo, Log, TEXT("Configuring UI Subsystem"));

    // Configure UI subsystem with our settings
    // Note: Actual configuration would depend on UISubsystem's API
}

void ARealGazeboManager::ConfigureVehiclePoolSettings()
{
    UE_LOG(LogRealGazebo, Verbose, TEXT("Configuring Vehicle Pool Settings"));
    // Implementation would configure pool management settings in bridge subsystem
}

void ARealGazeboManager::ConfigureNetworkProcessingSettings()
{
    UE_LOG(LogRealGazebo, Verbose, TEXT("Configuring Network Processing Settings"));
    // Implementation would configure network settings in bridge subsystem
}

void ARealGazeboManager::ConfigurePerformanceAndDebugSettings()
{
    UE_LOG(LogRealGazebo, Verbose, TEXT("Configuring Performance and Debug Settings"));
    // Implementation would configure performance settings in bridge subsystem
}

APlayerController* ARealGazeboManager::GetPlayerController() const
{
    return UGameplayStatics::GetPlayerController(this, 0);
}

void ARealGazeboManager::ConfigureBridgePoolSettings()
{
    if (!BridgeSubsystem.IsValid())
    {
        return;
    }

    // Configure VehiclePoolManager through Bridge subsystem
    if (UVehiclePoolManager* PoolManager = BridgeSubsystem->GetVehiclePoolManager())
    {
        PoolManager->MaxActorsPerType = MaxActorsPerType;
        PoolManager->InitialPoolSize = InitialPoolSize;
        PoolManager->bAutoExpandPools = bAutoExpandPools;
        PoolManager->PoolExpansionSize = PoolExpansionSize;
        PoolManager->bAutoShrinkPools = bAutoShrinkPools;
        PoolManager->UnusedActorTimeout = UnusedActorTimeout;

        UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Manager [%s]: Configured pool settings - MaxActorsPerType: %d, InitialPoolSize: %d"),
               *GetName(), MaxActorsPerType, InitialPoolSize);
    }
}

//----------------------------------------------------------
// Validation Methods
//----------------------------------------------------------

bool ARealGazeboManager::ValidateVehicleDataTable() const
{
    if (!VehicleDataTable)
    {
        UE_LOG(LogRealGazebo, Warning, TEXT("RealGazebo Manager [%s]: VehicleDataTable not set - vehicles will use fallback configuration"), *GetName());
        return true; // Non-critical - system can work with fallback
    }

    // Check if DataTable uses the correct row structure
    if (VehicleDataTable->GetRowStruct() != FBridgeVehicleConfigRow::StaticStruct())
    {
        UE_LOG(LogRealGazebo, Error, TEXT("RealGazebo Manager [%s]: VehicleDataTable must use FBridgeVehicleConfigRow structure"), *GetName());
        return false;
    }

    // Check if DataTable has at least one row
    if (VehicleDataTable->GetRowNames().Num() == 0)
    {
        UE_LOG(LogRealGazebo, Warning, TEXT("RealGazebo Manager [%s]: VehicleDataTable is empty - no vehicle configurations found"), *GetName());
        return true; // Non-critical - system can work with fallback
    }

    return true;
}

bool ARealGazeboManager::ValidateUIConfiguration() const
{
    if (!MainWidgetClass)
    {
        UE_LOG(LogRealGazebo, Error, TEXT("RealGazebo Manager [%s]: MainWidgetClass not set but Auto Create UI is enabled"), *GetName());
        return false;
    }

    if (!VehicleTypeImageDataTable)
    {
        UE_LOG(LogRealGazebo, Warning, TEXT("RealGazebo Manager [%s]: VehicleTypeImageDataTable not set - UI will use default icons"), *GetName());
        // Non-critical - UI can work with defaults
    }

    return true;
}

bool ARealGazeboManager::ValidateBridgeSubsystem() const
{
    if (!BridgeSubsystem.IsValid())
    {
        UE_LOG(LogRealGazebo, Error, TEXT("RealGazebo Manager [%s]: Bridge subsystem not available"), *GetName());
        return false;
    }

    return true;
}

//----------------------------------------------------------

void ARealGazeboManager::UpdateStatusDisplay()
{
    // Update active vehicle count
    ActiveVehiclesCount = GetActiveVehicleCount();

    // Update bridge status
    if (BridgeSubsystem.IsValid())
    {
        if (BridgeSubsystem->IsBridgeActive())
        {
            BridgeStatus = TEXT("Active");
        }
        else
        {
            BridgeStatus = bDidStartBridge ? TEXT("Stopped") : TEXT("Not Started");
        }
    }
    else
    {
        BridgeStatus = TEXT("No Subsystem");
    }

    // Update UI status
    if (UISubsystem.IsValid())
    {
        if (bDidStartUI)
        {
            UIStatus = TEXT("Active");
        }
        else
        {
            UIStatus = TEXT("Not Started");
        }
    }
    else
    {
        UIStatus = TEXT("No Subsystem");
    }
}