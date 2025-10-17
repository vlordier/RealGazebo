// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/DataTable.h"
#include "RealGazeboSettings.generated.h"


UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "RealGazebo"))
class REALGAZEBO_API URealGazeboSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    URealGazeboSettings();

    //----------------------------------------------------------
    // General Settings
    //----------------------------------------------------------

    /** Enable RealGazebo unified management system */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "General", meta = (DisplayName = "Enable RealGazebo"))
    bool bEnableRealGazebo = true;

    /** Enable debug logging and visualization */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "General", meta = (DisplayName = "Enable Debug Mode"))
    bool bEnableDebugMode = false;

    /** Maximum number of simultaneous vehicles (UDP protocol limit) */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "General", meta = (DisplayName = "Max Vehicles", ClampMin = "1", ClampMax = "255"))
    int32 MaxVehicles = 255;

    //----------------------------------------------------------
    // Bridge Settings
    //----------------------------------------------------------

    /** Enable Bridge component by default */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bridge", meta = (DisplayName = "Enable Bridge"))
    bool bEnableBridge = true;

    /** Default vehicle configuration DataTable */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bridge", meta = (DisplayName = "Vehicle Config DataTable", EditCondition = "bEnableBridge"))
    TSoftObjectPtr<UDataTable> DefaultVehicleConfigTable;

    /** UDP listen port for Gazebo communication */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bridge|Network", meta = (DisplayName = "UDP Listen Port", ClampMin = "1024", ClampMax = "65535", EditCondition = "bEnableBridge"))
    int32 UDPListenPort = 5005;

    /** Enable automatic UDP receiver startup */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bridge|Network", meta = (DisplayName = "Auto Start UDP", EditCondition = "bEnableBridge"))
    bool bAutoStartUDP = true;

    //----------------------------------------------------------
    // UI Settings
    //----------------------------------------------------------

    /** Enable UI component by default */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "UI", meta = (DisplayName = "Enable UI"))
    bool bEnableUI = true;

    /** Default camera mode on startup (0=Manual, 1=FollowVehicle, 2=BackView) */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "UI|Camera", meta = (DisplayName = "Default Camera Mode", EditCondition = "bEnableUI", ClampMin="0", ClampMax="2"))
    int32 DefaultCameraMode = 0;

    /** Enable camera mode keyboard shortcuts (M/F/B keys) */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "UI|Camera", meta = (DisplayName = "Enable Camera Shortcuts", EditCondition = "bEnableUI"))
    bool bEnableCameraShortcuts = true;

    /** Enable main UI widget by default */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "UI|Interface", meta = (DisplayName = "Enable Main Widget", EditCondition = "bEnableUI"))
    bool bEnableMainWidget = true;

    //----------------------------------------------------------
    // Performance Settings
    //----------------------------------------------------------

    /** Target update frequency for vehicle data (Hz) */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Performance", meta = (DisplayName = "Vehicle Update Rate", ClampMin = "1", ClampMax = "120"))
    float VehicleUpdateRate = 30.0f;

    /** Enable object pooling for vehicles */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Performance", meta = (DisplayName = "Enable Object Pooling"))
    bool bEnableObjectPooling = true;

    /** Maximum pool size per vehicle type */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Performance", meta = (DisplayName = "Pool Size Per Type", ClampMin = "1", ClampMax = "50", EditCondition = "bEnableObjectPooling"))
    int32 PoolSizePerType = 5;

    //----------------------------------------------------------
    // Developer Settings
    //----------------------------------------------------------

    /** Show performance statistics in debug mode */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Debug", meta = (DisplayName = "Show Performance Stats", EditCondition = "bEnableDebugMode"))
    bool bShowPerformanceStats = false;

    /** Show vehicle debug information */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Debug", meta = (DisplayName = "Show Vehicle Debug", EditCondition = "bEnableDebugMode"))
    bool bShowVehicleDebug = false;

    /** Show network debug information */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Debug", meta = (DisplayName = "Show Network Debug", EditCondition = "bEnableDebugMode && bEnableBridge"))
    bool bShowNetworkDebug = false;

    //----------------------------------------------------------
    // UDeveloperSettings Interface
    //----------------------------------------------------------

    virtual FName GetCategoryName() const override { return FName("Plugins"); }
    virtual FName GetSectionName() const override { return FName("RealGazebo"); }

#if WITH_EDITOR
    virtual FText GetSectionText() const override;
    virtual FText GetSectionDescription() const override;
#endif

    //----------------------------------------------------------
    // Utility Functions
    //----------------------------------------------------------

    /** Get the global RealGazebo settings */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazebo|Settings", CallInEditor)
    static URealGazeboSettings* GetRealGazeboSettings();

    /** Reset all settings to defaults */
    UFUNCTION(BlueprintCallable, Category = "RealGazebo|Settings", CallInEditor)
    void ResetToDefaults();

};