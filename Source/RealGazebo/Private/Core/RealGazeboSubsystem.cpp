// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "Core/RealGazeboSubsystem.h"
#include "Core/RealGazeboSettings.h"
#include "Core/RealGazeboManager.h"
#include "RealGazebo.h"
#include "Engine/World.h"

//----------------------------------------------------------
// Subsystem Lifecycle
//----------------------------------------------------------

void URealGazeboSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Master Subsystem: Initialize"));

    // Initialize state
    bIsActive = false;
    RegisteredManagers.Empty();

    // Cache settings
    CachedSettings = URealGazeboSettings::GetRealGazeboSettings();

    // Auto-initialize if enabled
    if (CachedSettings && CachedSettings->bEnableRealGazebo)
    {
        UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Master Subsystem: Auto-initialization disabled - managers will handle subsystem initialization"));
        // Note: Subsystem auto-initialization disabled - let managers handle their own initialization
        bIsActive = true;
    }
}

void URealGazeboSubsystem::Deinitialize()
{
    UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Master Subsystem: Deinitialize"));

    bIsActive = false;
    ShutdownAllSubsystems();

    // Clear all references
    RegisteredManagers.Empty();
    CachedSettings = nullptr;

    Super::Deinitialize();
}

//----------------------------------------------------------
// Static Access
//----------------------------------------------------------

URealGazeboSubsystem* URealGazeboSubsystem::GetRealGazeboSubsystem(const UObject* WorldContext)
{
    if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull))
    {
        return World->GetSubsystem<URealGazeboSubsystem>();
    }
    return nullptr;
}

bool URealGazeboSubsystem::IsRealGazeboAvailable(const UObject* WorldContext)
{
    URealGazeboSubsystem* Subsystem = GetRealGazeboSubsystem(WorldContext);
    return Subsystem && Subsystem->IsSubsystemActive();
}

bool URealGazeboSubsystem::IsSubsystemActive() const
{
    return bIsActive;
}

//----------------------------------------------------------
// Master Control Functions
//----------------------------------------------------------

bool URealGazeboSubsystem::InitializeAllSubsystems()
{
    UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Master Subsystem: InitializeAllSubsystems"));

    if (bIsActive)
    {
        UE_LOG(LogRealGazebo, Warning, TEXT("Master subsystem already active"));
        return true;
    }

    // Validate settings are available
    if (!CachedSettings)
    {
        UE_LOG(LogRealGazebo, Error, TEXT("RealGazebo Master Subsystem: Settings not available - cannot initialize"));
        return false;
    }

    bool bSuccess = true;

    // Initialize Bridge subsystem if enabled
    if (CachedSettings->bEnableBridge)
    {
        bSuccess &= InitializeBridgeSubsystem();
    }

    // Initialize UI subsystem if enabled
    if (CachedSettings->bEnableUI)
    {
        bSuccess &= InitializeUISubsystem();
    }

    if (bSuccess)
    {
        bIsActive = true;
        UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Master Subsystem: All subsystems initialized successfully"));
    }
    else
    {
        bIsActive = false;
        UE_LOG(LogRealGazebo, Error, TEXT("RealGazebo Master Subsystem: Failed to initialize some subsystems"));
    }

    return bSuccess;
}

void URealGazeboSubsystem::ShutdownAllSubsystems()
{
    UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Master Subsystem: ShutdownAllSubsystems"));

    bIsActive = false;

    // Shutdown Bridge subsystem
    if (UGazeboBridgeSubsystem* Bridge = GetBridgeSubsystem())
    {
        // Bridge has its own shutdown logic
    }

    // Shutdown UI subsystem
    if (URealGazeboUISubsystem* UI = GetUISubsystem())
    {
        // UI has its own shutdown logic
    }
}


//----------------------------------------------------------
// Child Subsystem Access
//----------------------------------------------------------

UGazeboBridgeSubsystem* URealGazeboSubsystem::GetBridgeSubsystem() const
{
    if (UWorld* World = GetWorld())
    {
        if (UGameInstance* GameInstance = World->GetGameInstance())
        {
            return GameInstance->GetSubsystem<UGazeboBridgeSubsystem>();
        }
    }
    return nullptr;
}

URealGazeboUISubsystem* URealGazeboSubsystem::GetUISubsystem() const
{
    if (UWorld* World = GetWorld())
    {
        if (UGameInstance* GameInstance = World->GetGameInstance())
        {
            return GameInstance->GetSubsystem<URealGazeboUISubsystem>();
        }
    }
    return nullptr;
}

bool URealGazeboSubsystem::IsBridgeAvailable() const
{
    UGazeboBridgeSubsystem* Bridge = GetBridgeSubsystem();
    return Bridge != nullptr;
}

bool URealGazeboSubsystem::IsUIAvailable() const
{
    URealGazeboUISubsystem* UI = GetUISubsystem();
    return UI != nullptr;
}

//----------------------------------------------------------
// Manager Registration
//----------------------------------------------------------

void URealGazeboSubsystem::RegisterManager(ARealGazeboManager* Manager)
{
    if (!Manager)
    {
        UE_LOG(LogRealGazebo, Warning, TEXT("Attempted to register null manager"));
        return;
    }

    if (!RegisteredManagers.Contains(Manager))
    {
        RegisteredManagers.Add(Manager);
        UE_LOG(LogRealGazebo, Log, TEXT("Registered RealGazebo Manager: %s"), *Manager->GetName());

    }
}

void URealGazeboSubsystem::UnregisterManager(ARealGazeboManager* Manager)
{
    if (RegisteredManagers.Remove(Manager) > 0)
    {
        UE_LOG(LogRealGazebo, Log, TEXT("Unregistered RealGazebo Manager: %s"),
               Manager ? *Manager->GetName() : TEXT("NULL"));

    }
}

TArray<ARealGazeboManager*> URealGazeboSubsystem::GetRegisteredManagers() const
{
    return RegisteredManagers;
}

//----------------------------------------------------------
// Unified API Functions (Bridge Operations)
//----------------------------------------------------------

int32 URealGazeboSubsystem::GetTotalActiveVehicleCount() const
{
    int32 TotalCount = 0;

    // Sum up vehicle counts from all registered managers
    for (ARealGazeboManager* Manager : RegisteredManagers)
    {
        if (Manager)
        {
            TotalCount += Manager->GetActiveVehicleCount();
        }
    }

    return TotalCount;
}

bool URealGazeboSubsystem::GetVehicleConfiguration(uint8 VehicleType, FBridgeVehicleConfigRow& OutConfig) const
{
    if (UGazeboBridgeSubsystem* Bridge = GetBridgeSubsystem())
    {
        return Bridge->GetVehicleConfig(VehicleType, OutConfig);
    }
    return false;
}

bool URealGazeboSubsystem::StartNetworkCommunication()
{
    if (UGazeboBridgeSubsystem* Bridge = GetBridgeSubsystem())
    {
        Bridge->StartBridge();
        return Bridge->IsBridgeActive();
    }
    return false;
}

void URealGazeboSubsystem::StopNetworkCommunication()
{
    if (UGazeboBridgeSubsystem* Bridge = GetBridgeSubsystem())
    {
        Bridge->StopBridge();
    }
}

bool URealGazeboSubsystem::IsNetworkActive() const
{
    if (UGazeboBridgeSubsystem* Bridge = GetBridgeSubsystem())
    {
        return Bridge->IsBridgeActive();
    }
    return false;
}

//----------------------------------------------------------
// Unified API Functions (UI Operations)
//----------------------------------------------------------

void URealGazeboSubsystem::SetGlobalCameraMode(ERealGazeboViewerMode NewMode)
{
    // Set camera mode for all registered managers
    for (ARealGazeboManager* Manager : RegisteredManagers)
    {
        if (Manager)
        {
            Manager->SetCameraMode(static_cast<int32>(NewMode));
        }
    }

}

ERealGazeboViewerMode URealGazeboSubsystem::GetGlobalCameraMode() const
{
    // Return the camera mode from the first available manager
    for (ARealGazeboManager* Manager : RegisteredManagers)
    {
        if (Manager)
        {
            return static_cast<ERealGazeboViewerMode>(Manager->GetCameraMode());
        }
    }

    return ERealGazeboViewerMode::Manual;
}

void URealGazeboSubsystem::SetMainWidgetVisibility(bool bVisible)
{
    for (ARealGazeboManager* Manager : RegisteredManagers)
    {
        if (Manager)
        {
            Manager->SetMainWidgetVisibility(bVisible);
        }
    }
}

//----------------------------------------------------------
// Internal Methods
//----------------------------------------------------------

bool URealGazeboSubsystem::InitializeBridgeSubsystem()
{
    UGazeboBridgeSubsystem* Bridge = GetBridgeSubsystem();
    if (!Bridge)
    {
        UE_LOG(LogRealGazebo, Error, TEXT("Bridge subsystem not found"));
        return false;
    }

    UE_LOG(LogRealGazebo, Log, TEXT("Bridge subsystem initialized successfully"));
    return true;
}

bool URealGazeboSubsystem::InitializeUISubsystem()
{
    URealGazeboUISubsystem* UI = GetUISubsystem();
    if (!UI)
    {
        UE_LOG(LogRealGazebo, Error, TEXT("UI subsystem not found"));
        return false;
    }

    UE_LOG(LogRealGazebo, Log, TEXT("UI subsystem initialized successfully"));
    return true;
}

void URealGazeboSubsystem::UpdateSubsystemState()
{
    // Simple state update - just check if subsystems are available
    bIsActive = IsBridgeAvailable() && IsUIAvailable();
}

void URealGazeboSubsystem::OnBridgeStateChanged()
{
    // Handle Bridge subsystem state changes
    UpdateSubsystemState();
}

void URealGazeboSubsystem::OnUIStateChanged()
{
    // Handle UI subsystem state changes
    UpdateSubsystemState();
}

