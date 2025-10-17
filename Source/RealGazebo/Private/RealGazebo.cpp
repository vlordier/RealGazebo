// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "RealGazebo.h"
#include "Engine/Engine.h"

DEFINE_LOG_CATEGORY(LogRealGazebo);

#define LOCTEXT_NAMESPACE "FRealGazeboModule"

FRealGazeboModule* FRealGazeboModule::ModuleInstance = nullptr;



void FRealGazeboModule::ShutdownModule()
{
    UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Main Module: ShutdownModule - Cleaning up master coordinator"));

    ShutdownSubModules();
    ModuleInstance = nullptr;

    UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Main Module: Successfully shut down"));
}

FRealGazeboModule& FRealGazeboModule::Get()
{
    return FModuleManager::LoadModuleChecked<FRealGazeboModule>("RealGazebo");
}

bool FRealGazeboModule::IsAvailable()
{
    return FModuleManager::Get().IsModuleLoaded("RealGazebo");
}

void FRealGazeboModule::InitializeSubModules()
{
    UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Main Module: Coordinating sub-modules initialization"));

    // Initialize Bridge module first (core functionality)
    if (FModuleManager::Get().ModuleExists(TEXT("RealGazeboBridge")))
    {
        FModuleManager::Get().LoadModuleChecked<IModuleInterface>(TEXT("RealGazeboBridge"));
        if (FModuleManager::Get().IsModuleLoaded(TEXT("RealGazeboBridge")))
        {
            UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Main Module: Bridge module initialized successfully"));
        }
        else
        {
            UE_LOG(LogRealGazebo, Warning, TEXT("RealGazebo Main Module: Bridge module initialization failed"));
        }
    }
    else
    {
        UE_LOG(LogRealGazebo, Warning, TEXT("RealGazebo Main Module: Bridge module not found"));
    }

    // Initialize UI module (depends on Bridge)
    if (FModuleManager::Get().ModuleExists(TEXT("RealGazeboUI")))
    {
        FModuleManager::Get().LoadModuleChecked<IModuleInterface>(TEXT("RealGazeboUI"));
        if (FModuleManager::Get().IsModuleLoaded(TEXT("RealGazeboUI")))
        {
            UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Main Module: UI module initialized successfully"));
        }
        else
        {
            UE_LOG(LogRealGazebo, Warning, TEXT("RealGazebo Main Module: UI module initialization failed"));
        }
    }
    else
    {
        UE_LOG(LogRealGazebo, Warning, TEXT("RealGazebo Main Module: UI module not found"));
    }

    // Initialize Streaming module (future development)
    if (FModuleManager::Get().ModuleExists(TEXT("RealGazeboStreaming")))
    {
        FModuleManager::Get().LoadModuleChecked<IModuleInterface>(TEXT("RealGazeboStreaming"));
        if (FModuleManager::Get().IsModuleLoaded(TEXT("RealGazeboStreaming")))
        {
            UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Main Module: Streaming module initialized successfully"));
        }
        else
        {
            UE_LOG(LogRealGazebo, Warning, TEXT("RealGazebo Main Module: Streaming module initialization failed"));
        }
    }
    else
    {
        UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Main Module: Streaming module not available (future development)"));
    }
}

void FRealGazeboModule::ShutdownSubModules()
{
    UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Main Module: Coordinating sub-modules shutdown"));

    // Shutdown in reverse order (UI → Streaming → Bridge)

    // Shutdown UI module first
    if (FModuleManager::Get().IsModuleLoaded(TEXT("RealGazeboUI")))
    {
        FModuleManager::Get().UnloadModule(TEXT("RealGazeboUI"));
        UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Main Module: UI module shutdown completed"));
    }

    // Shutdown Streaming module
    if (FModuleManager::Get().IsModuleLoaded(TEXT("RealGazeboStreaming")))
    {
        FModuleManager::Get().UnloadModule(TEXT("RealGazeboStreaming"));
        UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Main Module: Streaming module shutdown completed"));
    }

    // Shutdown Bridge module last (core functionality)
    if (FModuleManager::Get().IsModuleLoaded(TEXT("RealGazeboBridge")))
    {
        FModuleManager::Get().UnloadModule(TEXT("RealGazeboBridge"));
        UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Main Module: Bridge module shutdown completed"));
    }

    UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Main Module: All sub-modules shutdown coordinated"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRealGazeboModule, RealGazebo)