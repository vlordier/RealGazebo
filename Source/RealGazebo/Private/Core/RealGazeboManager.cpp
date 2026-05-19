// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#include "Core/RealGazeboManager.h"
#include "RealGazebo.h"
#include "Engine/World.h"
#include "GazeboBridgeSubsystem.h"
#include "RealGazeboUISubsystem.h"
#include "Vehicles/VehiclePoolManager.h"
#include "Vehicles/VehicleBasePawn.h"
#include "ViewerController/RealGazeboViewerDirector.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Components/Widget.h"
#include "Blueprint/UserWidget.h"
#include "Core/VehicleRegistrySubsystem.h"
#include "Data/RealGazeboVehicleData.h"
#include "Data/VehicleTypeImageData.h"

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

    // Initialize default camera presets
    CameraPresets.Empty();

    // Preset 0 - C-Track (Keyboard: 1)
    FCameraPreset CTrackPreset;
    CTrackPreset.PresetName = TEXT("C-Track");
    CTrackPreset.Location = FVector(-49985.707615f, -6242.985621f, 22005.642773f);
    CTrackPreset.Rotation = FRotator(-32.369293f, 0.734925f, 0.0f);
    CameraPresets.Add(CTrackPreset);
    
    // Preset 1 - VILS (Keyboard: 2)
    FCameraPreset VILSPreset;
    VILSPreset.PresetName = TEXT("VILS");
    VILSPreset.Location = FVector(-2492.325356f, 1164.508698f, 6482.866281f);
    VILSPreset.Rotation = FRotator(-90.0f, 179.0f, 90.0f);
    CameraPresets.Add(VILSPreset);

    // Preset 2 - Urban (Keyboard: 3)
    FCameraPreset UrbanPreset;
    UrbanPreset.PresetName = TEXT("Urban");
    UrbanPreset.Location = FVector(-31214.548424f, -17165.116186f, 14063.83629f);
    UrbanPreset.Rotation = FRotator(-90.0f, 82.0f, 90.0f);
    CameraPresets.Add(UrbanPreset);

    // Preset 3 - BeltWay Camera (Keyboard: 4)
    FCameraPreset BeltWayCamera;
    BeltWayCamera.PresetName = TEXT("BeltWay Camera");
    BeltWayCamera.Location = FVector(-10369.975632f, 6390.696626f, 32605.926715f);
    BeltWayCamera.Rotation = FRotator(-90.0f, 180.0f, 0.0f);
    CameraPresets.Add(BeltWayCamera);

    // Preset 4 - DirtRoad Camera (Keyboard: 5)
    FCameraPreset DirtRoadCamera;
    DirtRoadCamera.PresetName = TEXT("DirtRoad Camera");
    DirtRoadCamera.Location = FVector(-8113.290445f, 7729.819206f, 1092.474938f);
    DirtRoadCamera.Rotation = FRotator(-16.04874f, -18.656375f, 0.0f);
    CameraPresets.Add(DirtRoadCamera);

    // Preset 5 - Forest Camera (Keyboard: 6)
    FCameraPreset ForestCamera;
    ForestCamera.PresetName = TEXT("Forest Camera");
    ForestCamera.Location = FVector(2213.234064f, 22609.036801f, 487.442638f);
    ForestCamera.Rotation = FRotator(-2.762647f, 77.978291f, 0.0f);
    CameraPresets.Add(ForestCamera);

    // Preset 6 - Lake Camera (Keyboard: 7)
    FCameraPreset LakeCamera;
    LakeCamera.PresetName = TEXT("Lake Camera");
    LakeCamera.Location = FVector(-28342.004076f, 20093.002073f, 2293.668522f);
    LakeCamera.Rotation = FRotator(-12.077954f, 76.339822f, 0.0f);
    CameraPresets.Add(LakeCamera);
}

//----------------------------------------------------------
// Actor Lifecycle
//----------------------------------------------------------

void ARealGazeboManager::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Manager: Initializing [%s]"), *GetName());

    // Get subsystem references
    if (UGameInstance* GameInstance = GetGameInstance())
    {
        BridgeSubsystem = GameInstance->GetSubsystem<UGazeboBridgeSubsystem>();
        UISubsystem = GameInstance->GetSubsystem<URealGazeboUISubsystem>();

        if (!BridgeSubsystem.IsValid())
        {
            UE_LOG(LogRealGazebo, Warning, TEXT("Bridge subsystem not found - Bridge features disabled"));
        }

        if (!UISubsystem.IsValid())
        {
            UE_LOG(LogRealGazebo, Warning, TEXT("UI subsystem not found - UI features disabled"));
        }

        // Register this actor's user-assigned DataTable as the authoritative core source
        // for the vehicle registry. Mod DataTables discovered via AssetRegistry will be
        // merged on top, with this core source taking precedence on VehicleTypeCode conflicts.
        if (UVehicleRegistrySubsystem* Registry = GameInstance->GetSubsystem<UVehicleRegistrySubsystem>())
        {
            Registry->RegisterCoreSource(UnifiedVehicleDataTable);

            // Push the current registry state down to the bridge subsystem so spawn
            // lookups hit the merged set instead of just the legacy DataTable.
            SyncBridgeSubsystemFromRegistry();

            // The mod AssetRegistry scan may still be in flight - re-sync when it completes.
            Registry->OnRegistryUpdated.AddUObject(this, &ARealGazeboManager::HandleRegistryUpdated);
        }
    }
    else
    {
        UE_LOG(LogRealGazebo, Error, TEXT("GameInstance not available"));
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
    UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Manager: Shutting down"));

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

    // Reconfigure subsystems when properties change
    if (PropertyChangedEvent.Property != nullptr && (BridgeSubsystem.IsValid() || UISubsystem.IsValid()))
    {
        ConfigureBridgeSubsystem();
        ConfigureUISubsystem();
    }
}
#endif


void ARealGazeboManager::StartBridge()
{
    if (!BridgeSubsystem.IsValid())
    {
        UE_LOG(LogRealGazebo, Error, TEXT("Cannot start bridge - Bridge subsystem not available"));
        BridgeStatus = TEXT("Error - No Subsystem");
        return;
    }

    // Vehicle config flows through UVehicleRegistrySubsystem (pushed in BeginPlay
    // via SyncBridgeSubsystemFromRegistry). The legacy VehicleConfigTable slot is
    // left untouched here - the bridge subsystem reads its pushed cache first,
    // so we no longer need to materialize a converted DataTable per StartBridge call.

    BridgeSubsystem->ListenPort = ListenPort;
    BridgeSubsystem->ServerIPAddress = ServerIPAddress;
    BridgeSubsystem->bAutoSpawnVehicles = bAutoSpawnVehicles;
    BridgeSubsystem->SetUpdateFrequency(UpdateFrequency);

    // Configure pool settings
    ConfigureBridgePoolSettings();

    // Start the bridge
    BridgeSubsystem->StartBridge();

    bDidStartBridge = true;
    BridgeStatus = TEXT("Active");
    OnBridgeStarted.Broadcast(FBridgePoseData());

    UE_LOG(LogRealGazebo, Log, TEXT("Bridge started on port %d"), ListenPort);
}

void ARealGazeboManager::StopBridge()
{
    if (BridgeSubsystem.IsValid())
    {
        BridgeSubsystem->StopBridge();
        bDidStartBridge = false;
        BridgeStatus = TEXT("Stopped");
        OnBridgeStopped.Broadcast(FBridgePoseData());
        UE_LOG(LogRealGazebo, Log, TEXT("Bridge stopped"));
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
    if (!UISubsystem.IsValid())
    {
        UE_LOG(LogRealGazebo, Error, TEXT("Cannot initialize UI - UI subsystem not available"));
        return;
    }

    // Vehicle icons flow through UVehicleRegistrySubsystem -> UISubsystem image map
    // (pushed in BeginPlay via SyncBridgeSubsystemFromRegistry). Widgets call
    // UISubsystem->GetVehicleImage directly, so we no longer hand a DataTable here.
    UISubsystem->InitializeCameraUI(
        MainWidgetClass,
        /*VehicleTypeImageDataTable=*/ nullptr,
        InitialCameraLocation,
        InitialCameraRotation,
        bAutoCreateViewerDirector,
        bAutoAddToViewport ? WidgetZOrder : -1
    );

    ViewerDirector = UISubsystem->GetViewerDirector();

    // Configure camera presets on the viewer director
    if (ViewerDirector.IsValid() && CameraPresets.Num() > 0)
    {
        ViewerDirector->SetCameraPresets(CameraPresets);
        UE_LOG(LogRealGazebo, Log, TEXT("Configured %d camera presets on ViewerDirector"),
               CameraPresets.Num());
    }

    UISubsystem->SetMouseCursorAlwaysVisible(bAlwaysShowMouseCursor);

    bDidStartUI = true;
    UUserWidget* CreatedWidget = UISubsystem->GetActiveMainWidget();
    UIStatus = CreatedWidget ? TEXT("UI Active") : TEXT("UI Partial (No Widget)");

    UE_LOG(LogRealGazebo, Log, TEXT("Camera UI initialized: Widget=%s, ViewerDirector=%s"),
           CreatedWidget ? TEXT("OK") : TEXT("FAILED"),
           ViewerDirector.IsValid() ? TEXT("OK") : TEXT("FAILED"));
}

void ARealGazeboManager::CleanupCameraUI()
{
    if (UISubsystem.IsValid())
    {
        UISubsystem->CleanupCameraUI();
    }

    ViewerDirector = nullptr;
    WidgetInViewportStatus = false;
    bDidStartUI = false;
    UIStatus = TEXT("Cleaned Up");
    OnUICleanedUp();

    UE_LOG(LogRealGazebo, Log, TEXT("Camera UI cleaned up"));
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
// Registry synchronization
//----------------------------------------------------------

void ARealGazeboManager::SyncBridgeSubsystemFromRegistry()
{
    UVehicleRegistrySubsystem* Registry = nullptr;
    if (UGameInstance* GameInstance = GetGameInstance())
    {
        Registry = GameInstance->GetSubsystem<UVehicleRegistrySubsystem>();
    }
    if (!Registry)
    {
        return;
    }

    const TArray<uint8> TypeCodes = Registry->GetAllVehicleTypeCodes(/*bUIOnly=*/ false);

    TMap<uint8, FBridgeVehicleConfigRow> BridgeConfigs;
    TMap<uint8, TSoftObjectPtr<UTexture2D>> ImageMap;
    BridgeConfigs.Reserve(TypeCodes.Num());
    ImageMap.Reserve(TypeCodes.Num());

    for (uint8 Code : TypeCodes)
    {
        FRealGazeboVehicleConfigRow Unified;
        if (!Registry->GetVehicleData(Code, Unified))
        {
            continue;
        }

        FBridgeVehicleConfigRow BridgeRow = Unified.ToBridgeConfigRow();

        // The Bridge row only carries the hard TSubclassOf. If the unified row used
        // the soft pointer (the recommended path for mod content), resolve it now so
        // the spawn pipeline (VehiclePoolManager::GetVehicleClass) finds a valid class.
        if (!BridgeRow.VehiclePawnClass && !Unified.VehiclePawnClassSoft.IsNull())
        {
            BridgeRow.VehiclePawnClass = Unified.VehiclePawnClassSoft.LoadSynchronous();
        }

        BridgeConfigs.Add(Code, BridgeRow);
        ImageMap.Add(Code, Unified.VehicleImage);
    }

    if (BridgeSubsystem.IsValid())
    {
        BridgeSubsystem->PushVehicleConfigs(BridgeConfigs);
    }
    if (UISubsystem.IsValid())
    {
        UISubsystem->PushVehicleImageMap(ImageMap);
    }
}

void ARealGazeboManager::HandleRegistryUpdated()
{
    SyncBridgeSubsystemFromRegistry();
}

//----------------------------------------------------------
// Validation Methods
//----------------------------------------------------------

bool ARealGazeboManager::ValidateVehicleDataTable() const
{
    if (!UnifiedVehicleDataTable)
    {
        UE_LOG(LogRealGazebo, Warning, TEXT("UnifiedVehicleDataTable not set - using fallback configuration"));
        return true;
    }

    if (UnifiedVehicleDataTable->GetRowStruct() != FRealGazeboVehicleConfigRow::StaticStruct())
    {
        UE_LOG(LogRealGazebo, Error, TEXT("UnifiedVehicleDataTable must use FRealGazeboVehicleConfigRow structure"));
        return false;
    }

    if (UnifiedVehicleDataTable->GetRowNames().Num() == 0)
    {
        UE_LOG(LogRealGazebo, Warning, TEXT("UnifiedVehicleDataTable is empty"));
        return true;
    }

    return true;
}

bool ARealGazeboManager::ValidateUIConfiguration() const
{
    if (!MainWidgetClass)
    {
        UE_LOG(LogRealGazebo, Error, TEXT("MainWidgetClass not set but Auto Create UI is enabled"));
        return false;
    }

    if (!UnifiedVehicleDataTable)
    {
        UE_LOG(LogRealGazebo, Warning, TEXT("UnifiedVehicleDataTable not set - UI will use default icons"));
        return true;
    }

    // Check if any rows have vehicle images configured
    TArray<FName> RowNames = UnifiedVehicleDataTable->GetRowNames();
    for (const FName& RowName : RowNames)
    {
        if (const FRealGazeboVehicleConfigRow* Row = UnifiedVehicleDataTable->FindRow<FRealGazeboVehicleConfigRow>(RowName, TEXT("")))
        {
            if (Row->HasVehicleImage())
            {
                return true;
            }
        }
    }

    UE_LOG(LogRealGazebo, Warning, TEXT("No vehicle images configured - UI will use default icons"));
    return true;
}

bool ARealGazeboManager::ValidateBridgeSubsystem() const
{
    if (!BridgeSubsystem.IsValid())
    {
        UE_LOG(LogRealGazebo, Error, TEXT("Bridge subsystem not available"));
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

            // Update widget viewport status dynamically
            UUserWidget* MainWidget = UISubsystem->GetActiveMainWidget();
            WidgetInViewportStatus = (MainWidget != nullptr && MainWidget->IsInViewport());
        }
        else
        {
            UIStatus = TEXT("Not Started");
            WidgetInViewportStatus = false;
        }
    }
    else
    {
        UIStatus = TEXT("No Subsystem");
        WidgetInViewportStatus = false;
    }
}