// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRealGazebo, Log, All);



/**
 * RealGazebo Main Module - Master coordinator for multi-heterogeneous unmanned vehicle simulation
 *
 * Key Features:
 * - Coordinates RealGazeboBridge and RealGazeboUI modules
 * - Provides unified management system
 */
class REALGAZEBO_API FRealGazeboModule : public IModuleInterface
{
public:
    /** IModuleInterface implementation */
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    /** Get the module instance */
    static FRealGazeboModule& Get();

    /** Check if the module is loaded */
    static bool IsAvailable();

private:
    /** Module instance for singleton access */
    static FRealGazeboModule* ModuleInstance;

    /** Initialize sub-module coordination */
    void InitializeSubModules();

    /** Shutdown sub-module coordination */
    void ShutdownSubModules();
};