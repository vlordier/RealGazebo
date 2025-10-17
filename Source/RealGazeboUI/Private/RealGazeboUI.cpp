// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "RealGazeboUI.h"
#include "Engine/Engine.h"

#define LOCTEXT_NAMESPACE "FRealGazeboUIModule"

DEFINE_LOG_CATEGORY(LogRealGazeboUI);

FRealGazeboUIModule* FRealGazeboUIModule::ModuleInstance = nullptr;

void FRealGazeboUIModule::StartupModule()
{
	UE_LOG(LogRealGazeboUI, Log, TEXT("RealGazeboUI Module: StartupModule - Initializing UI components"));

	ModuleInstance = this;

	// Register UI components
	RegisterUIComponents();

	UE_LOG(LogRealGazeboUI, Log, TEXT("RealGazeboUI Module: Successfully initialized"));
}

void FRealGazeboUIModule::ShutdownModule()
{
	UE_LOG(LogRealGazeboUI, Log, TEXT("RealGazeboUI Module: ShutdownModule - Cleaning up UI components"));

	// Unregister UI components
	UnregisterUIComponents();

	ModuleInstance = nullptr;

	UE_LOG(LogRealGazeboUI, Log, TEXT("RealGazeboUI Module: Successfully shut down"));
}

FRealGazeboUIModule& FRealGazeboUIModule::Get()
{
	return FModuleManager::LoadModuleChecked<FRealGazeboUIModule>("RealGazeboUI");
}

bool FRealGazeboUIModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("RealGazeboUI");
}

void FRealGazeboUIModule::RegisterUIComponents()
{
	// Register custom widget classes for Blueprint use
	// This ensures our widgets are available in Blueprint editor

	UE_LOG(LogRealGazeboUI, Verbose, TEXT("Registering UI components"));

	// Note: Widget classes are automatically registered through their UCLASS macros
	// Additional registration logic can be added here if needed
}

void FRealGazeboUIModule::UnregisterUIComponents()
{
	// Cleanup any registered components
	UE_LOG(LogRealGazeboUI, Verbose, TEXT("Unregistering UI components"));

	// Cleanup logic can be added here if needed
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRealGazeboUIModule, RealGazeboUI)