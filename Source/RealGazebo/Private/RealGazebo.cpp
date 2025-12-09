// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#include "RealGazebo.h"
#include "Engine/Engine.h"

DEFINE_LOG_CATEGORY(LogRealGazebo);

#define LOCTEXT_NAMESPACE "FRealGazeboModule"

FRealGazeboModule* FRealGazeboModule::ModuleInstance = nullptr;

void FRealGazeboModule::StartupModule()
{
	UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Module: StartupModule"));
	ModuleInstance = this;
	UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Module: Successfully initialized"));
}

void FRealGazeboModule::ShutdownModule()
{
	UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Module: ShutdownModule"));
	ModuleInstance = nullptr;
	UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Module: Successfully shut down"));
}

FRealGazeboModule& FRealGazeboModule::Get()
{
	return FModuleManager::LoadModuleChecked<FRealGazeboModule>("RealGazebo");
}

bool FRealGazeboModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("RealGazebo");
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRealGazeboModule, RealGazebo)