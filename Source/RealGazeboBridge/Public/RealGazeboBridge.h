// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Sub-author: MinKyu Kim
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRealGazeboBridge, Log, All);

/**
 * RealGazebo Bridge Module - PX4-Gazebo simulation bridge
 * 
 * Key Features:
 * - Subsystem-based architecture
 * - Object pooling for memory management
 * - Batch processing for network data
 * - Simple rendering (all vehicles visible like original RealGazebo)
 */
class REALGAZEBOBRIDGE_API FRealGazeboBridgeModule : public IModuleInterface
{
public:
    /** IModuleInterface implementation */
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    /** Get the module instance */
    static FRealGazeboBridgeModule& Get();

    /** Check if the module is loaded */
    static bool IsAvailable();

private:
    /** Module instance for singleton access */
    static FRealGazeboBridgeModule* ModuleInstance;
};