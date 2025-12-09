// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#include "Core/RealGazeboSettings.h"
#include "RealGazebo.h"

#if WITH_EDITOR
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

#define LOCTEXT_NAMESPACE "RealGazeboSettings"

URealGazeboSettings::URealGazeboSettings()
{
	// Default initialization handled by UPROPERTY defaults in header
	// Constructor provided for linker vtable generation
}

#if WITH_EDITOR
FText URealGazeboSettings::GetSectionText() const
{
    return LOCTEXT("RealGazeboSettingsDisplayName", "RealGazebo");
}

FText URealGazeboSettings::GetSectionDescription() const
{
    return LOCTEXT("RealGazeboSettingsDescription", "Configuration settings for RealGazebo unified management system");
}
#endif

URealGazeboSettings* URealGazeboSettings::GetRealGazeboSettings()
{
    return GetMutableDefault<URealGazeboSettings>();
}


void URealGazeboSettings::ResetToDefaults()
{
    // General settings
    bEnableRealGazebo = true;
    bEnableDebugMode = false;
    MaxVehicles = 65536; // Protocol maximum: 256 types x 256 instances

    // Bridge settings
    bEnableBridge = true;
    DefaultVehicleConfigTable = nullptr;
    UDPListenPort = 5005;
    bAutoStartUDP = true;

    // UI settings
    bEnableUI = true;
    DefaultCameraMode = 0; // Manual mode
    bEnableCameraShortcuts = true;
    bEnableMainWidget = true;

    // Performance settings
    VehicleUpdateRate = 30.0f;
    bEnableObjectPooling = true;
    PoolSizePerType = 20; // Increased from 5 to support more vehicles per type

    // Debug settings
    bShowPerformanceStats = false;
    bShowVehicleDebug = false;
    bShowNetworkDebug = false;

#if WITH_EDITOR
    if (GIsEditor)
    {
        SaveConfig();
        UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo settings reset to defaults"));
    }
#endif
}


#undef LOCTEXT_NAMESPACE