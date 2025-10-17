// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "RealGazeboStreaming.h"
#include "Core/RealGazeboStreamingLogger.h"
#include "Engine/Engine.h"

#define LOCTEXT_NAMESPACE "FRealGazeboStreamingModule"

FRealGazeboStreamingModule* FRealGazeboStreamingModule::ModuleInstance = nullptr;

void FRealGazeboStreamingModule::StartupModule()
{
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RealGazeboStreaming Module: StartupModule - Initializing hardware-accelerated video streaming"));

	ModuleInstance = this;

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RealGazeboStreaming Module: Successfully initialized"));
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RealGazeboStreaming Module: Supported encoders - NVENC (NVIDIA), AMF (AMD)"));
}

void FRealGazeboStreamingModule::ShutdownModule()
{
	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RealGazeboStreaming Module: ShutdownModule - Cleaning up video streaming system"));

	ModuleInstance = nullptr;

	UE_LOG(LogRealGazeboStreaming, Log, TEXT("RealGazeboStreaming Module: Successfully shut down"));
}

FRealGazeboStreamingModule& FRealGazeboStreamingModule::Get()
{
	return FModuleManager::LoadModuleChecked<FRealGazeboStreamingModule>("RealGazeboStreaming");
}

bool FRealGazeboStreamingModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("RealGazeboStreaming");
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRealGazeboStreamingModule, RealGazeboStreaming)
