// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Sub-author: MinKyu Kim
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "RealGazeboBridge.h"
#include "Engine/Engine.h"

DEFINE_LOG_CATEGORY(LogRealGazeboBridge);

#define LOCTEXT_NAMESPACE "FRealGazeboBridgeModule"

FRealGazeboBridgeModule* FRealGazeboBridgeModule::ModuleInstance = nullptr;

void FRealGazeboBridgeModule::StartupModule()
{
    ModuleInstance = this;
}

void FRealGazeboBridgeModule::ShutdownModule()
{
    UE_LOG(LogRealGazeboBridge, Log, TEXT("RealGazeboBridge Module shutdown"));
    ModuleInstance = nullptr;
}

FRealGazeboBridgeModule& FRealGazeboBridgeModule::Get()
{
    return FModuleManager::LoadModuleChecked<FRealGazeboBridgeModule>("RealGazeboBridge");
}

bool FRealGazeboBridgeModule::IsAvailable()
{
    return FModuleManager::Get().IsModuleLoaded("RealGazeboBridge");
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRealGazeboBridgeModule, RealGazeboBridge)