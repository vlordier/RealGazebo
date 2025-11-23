// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#include "RealGazeboStreaming.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FRealGazeboStreamingModule"

void FRealGazeboStreamingModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreaming: Module Starting..."));
	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreaming: Zero-Copy GPU Encoding Enabled"));
	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreaming: Live555 RTSP Server Ready"));
	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreaming: Hardware Encoder Support (NVENC/AMF) Loaded"));

#if REALGAZEBO_STREAMING_DEBUG
	UE_LOG(LogTemp, Warning, TEXT("RealGazeboStreaming: Debug Mode Enabled"));
#endif

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreaming: Module Started Successfully"));
}

void FRealGazeboStreamingModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreaming: Module Shutting Down..."));

	OnModuleShutdown();

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreaming: Module Shutdown Complete"));
}

void FRealGazeboStreamingModule::OnModuleShutdown()
{
	// Cleanup module-specific resources
	// Note: Subsystems and actors cleanup themselves automatically

	UE_LOG(LogTemp, Log, TEXT("RealGazeboStreaming: Cleaning up module resources"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRealGazeboStreamingModule, RealGazeboStreaming)
