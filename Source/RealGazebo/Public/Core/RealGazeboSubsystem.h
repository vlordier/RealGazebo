// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GazeboBridgeSubsystem.h"
#include "RealGazeboUISubsystem.h"
#include "RealGazeboSubsystem.generated.h"

// Forward declarations
class URealGazeboSettings;
class ARealGazeboManager;


UCLASS(BlueprintType)
class REALGAZEBO_API URealGazeboSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    //----------------------------------------------------------
    // Subsystem Lifecycle
    //----------------------------------------------------------

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    //----------------------------------------------------------
    // Static Access
    //----------------------------------------------------------

    /** Get the RealGazebo master subsystem */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazebo|Subsystem", meta = (CallInEditor = "true"))
    static URealGazeboSubsystem* GetRealGazeboSubsystem(const UObject* WorldContext);

    /** Quick check if RealGazebo is available */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazebo|Subsystem", meta = (CallInEditor = "true"))
    static bool IsRealGazeboAvailable(const UObject* WorldContext);

    //----------------------------------------------------------
    // Master Control Functions
    //----------------------------------------------------------

    /** Initialize all enabled subsystems */
    UFUNCTION(BlueprintCallable, Category = "RealGazebo|Control")
    bool InitializeAllSubsystems();

    /** Shutdown all subsystems */
    UFUNCTION(BlueprintCallable, Category = "RealGazebo|Control")
    void ShutdownAllSubsystems();

    /** Check if subsystem is initialized and active */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazebo|Status")
    bool IsSubsystemActive() const;

    //----------------------------------------------------------
    // Child Subsystem Access
    //----------------------------------------------------------

    /** Get Bridge subsystem (may be null if disabled) */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazebo|Subsystems")
    UGazeboBridgeSubsystem* GetBridgeSubsystem() const;

    /** Get UI subsystem (may be null if disabled) */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazebo|Subsystems")
    URealGazeboUISubsystem* GetUISubsystem() const;

    /** Check if Bridge subsystem is available and active */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazebo|Subsystems")
    bool IsBridgeAvailable() const;

    /** Check if UI subsystem is available and active */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazebo|Subsystems")
    bool IsUIAvailable() const;

    //----------------------------------------------------------
    // Manager Registration
    //----------------------------------------------------------

    /** Register a manager actor with the subsystem */
    void RegisterManager(ARealGazeboManager* Manager);

    /** Unregister a manager actor */
    void UnregisterManager(ARealGazeboManager* Manager);

    /** Get all registered managers */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazebo|Managers")
    TArray<ARealGazeboManager*> GetRegisteredManagers() const;

    //----------------------------------------------------------
    // Unified API Functions (Bridge Operations)
    //----------------------------------------------------------

    /** Get total number of active vehicles across all managers */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazebo|Vehicles")
    int32 GetTotalActiveVehicleCount() const;

    /** Get vehicle configuration by type */
    UFUNCTION(BlueprintCallable, Category = "RealGazebo|Vehicles")
    bool GetVehicleConfiguration(uint8 VehicleType, FBridgeVehicleConfigRow& OutConfig) const;

    /** Start network communication with Gazebo */
    UFUNCTION(BlueprintCallable, Category = "RealGazebo|Network")
    bool StartNetworkCommunication();

    /** Stop network communication */
    UFUNCTION(BlueprintCallable, Category = "RealGazebo|Network")
    void StopNetworkCommunication();

    /** Check if network communication is active */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazebo|Network")
    bool IsNetworkActive() const;

    //----------------------------------------------------------
    // Unified API Functions (UI Operations)
    //----------------------------------------------------------

    /** Set camera mode across all UI components */
    UFUNCTION(BlueprintCallable, Category = "RealGazebo|Camera")
    void SetGlobalCameraMode(ERealGazeboViewerMode NewMode);

    /** Get current global camera mode */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazebo|Camera")
    ERealGazeboViewerMode GetGlobalCameraMode() const;

    /** Show/hide main UI widget */
    UFUNCTION(BlueprintCallable, Category = "RealGazebo|UI")
    void SetMainWidgetVisibility(bool bVisible);


protected:
    //----------------------------------------------------------
    // Internal State
    //----------------------------------------------------------

    /** Is subsystem currently active */
    UPROPERTY()
    bool bIsActive = false;

    /** Registered manager actors */
    UPROPERTY()
    TArray<ARealGazeboManager*> RegisteredManagers;


    /** Cached settings reference */
    UPROPERTY()
    URealGazeboSettings* CachedSettings;

    //----------------------------------------------------------
    // Internal Methods
    //----------------------------------------------------------

    /** Initialize Bridge subsystem if enabled */
    bool InitializeBridgeSubsystem();

    /** Initialize UI subsystem if enabled */
    bool InitializeUISubsystem();

    /** Update subsystem state based on component states */
    void UpdateSubsystemState();

    /** Handle Bridge subsystem events */
    UFUNCTION()
    void OnBridgeStateChanged();

    /** Handle UI subsystem events */
    UFUNCTION()
    void OnUIStateChanged();


};