// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRealGazeboUI, Log, All);

/**
 * RealGazebo UI Module
 * Provides UI components for vehicle data visualization and management
 */
class FRealGazeboUIModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Get the module instance */
	static FRealGazeboUIModule& Get();

	/** Check if the module is loaded */
	static bool IsAvailable();

private:
	/** Module instance for singleton access */
	static FRealGazeboUIModule* ModuleInstance;

	/** Handle module startup */
	void RegisterUIComponents();

	/** Handle module shutdown */
	void UnregisterUIComponents();
};