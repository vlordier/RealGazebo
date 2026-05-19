// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Sub-author: MinKyu Kim
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/DataTable.h"
#include "GazeboBridgeTypes.h"
#include <atomic>  // C++11 atomic operations for thread-safe state management
#include "GazeboBridgeSubsystem.generated.h"

// Forward declarations
class UDataStreamProcessor;
class UVehiclePoolManager;
class UWorld;
class APlayerCameraManager;

/**
 * Subsystem for managing PX4-Gazebo bridge
 * 
 * Key Features:
 * - Centralized vehicle data management
 * - Object pooling for memory management
 * - Simple rendering (all vehicles visible)
 * - Batch processing for network data
 */
UCLASS()
class REALGAZEBOBRIDGE_API UGazeboBridgeSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    UGazeboBridgeSubsystem();

    //----------------------------------------------------------
    // Subsystem Interface
    //----------------------------------------------------------
    
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

    //----------------------------------------------------------
    // Configuration
    //----------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge|Configuration")
    UDataTable* VehicleConfigTable;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge|Network")
    int32 ListenPort = 5005;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge|Network") 
    FString ServerIPAddress = TEXT("");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bridge|Performance")
    bool bAutoSpawnVehicles = true;



    //----------------------------------------------------------
    // Runtime Control
    //----------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "Bridge|Control")
    void StartBridge();

    UFUNCTION(BlueprintCallable, Category = "Bridge|Control")
    void StopBridge();

    UFUNCTION(BlueprintCallable, Category = "Bridge|Control")
    bool IsBridgeActive() const;

    UFUNCTION(BlueprintCallable, Category = "Bridge|Control")
    void ClearAllVehicles();

    /** Remove/despawn a specific vehicle by ID (used when receiving MessageID=4 destroy command) */
    UFUNCTION(BlueprintCallable, Category = "Bridge|Control")
    void RemoveVehicle(const FVehicleID& VehicleID);

    //----------------------------------------------------------
    // Vehicle Management
    //----------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "Bridge|Vehicles")
    int32 GetTotalVehicleCount() const;

    UFUNCTION(BlueprintCallable, Category = "Bridge|Vehicles")
    int32 GetActiveVehicleCount() const;

    UFUNCTION(BlueprintCallable, Category = "Bridge|Vehicles")
    int32 GetVisibleVehicleCount() const;

    UFUNCTION(BlueprintCallable, Category = "Bridge|Vehicles")
    FVehicleRuntimeData GetVehicleData(const FVehicleID& VehicleID) const;

    UFUNCTION(BlueprintCallable, Category = "Bridge|Vehicles")
    TArray<FVehicleID> GetAllVehicleIDs() const;

    /** Find all vehicles with a specific VehicleNum (regardless of VehicleType)
     * Useful for: debugging, collision detection, UI display, cleanup operations
     * Example: FindVehiclesByNum(0) returns all vehicles with Num=0 (different Types can coexist)
     */
    UFUNCTION(BlueprintCallable, Category = "Bridge|Vehicles")
    TArray<FVehicleID> FindVehiclesByNum(uint8 VehicleNum) const;

    UFUNCTION(BlueprintCallable, Category = "Bridge|Configuration")
    bool GetVehicleConfig(uint8 VehicleType, FBridgeVehicleConfigRow& OutConfig) const;

    /** Get vehicle configuration from DataTable (for internal use) */
    const FBridgeVehicleConfigRow* GetVehicleConfigInternal(uint8 VehicleType) const;

    /**
     * Push the merged vehicle config set from a higher-level orchestrator
     * (typically ARealGazeboManager pulling from UVehicleRegistrySubsystem).
     *
     * Configs pushed here take precedence over VehicleConfigTable. The legacy
     * VehicleConfigTable path remains as a fallback so that ARealGazeboBridgeManager
     * users (who don't push) continue to work without the mod registry.
     */
    void PushVehicleConfigs(const TMap<uint8, FBridgeVehicleConfigRow>& InConfigs);

    /** Discard the pushed cache so lookups fall back to VehicleConfigTable. */
    void ClearPushedVehicleConfigs();

    /** Get vehicle pool manager for configuration */
    UFUNCTION(BlueprintCallable, Category = "Bridge|Access")
    UVehiclePoolManager* GetVehiclePoolManager() const { return VehiclePool; }

    /** Get data stream processor for configuration */
    UFUNCTION(BlueprintCallable, Category = "Bridge|Access")
    UDataStreamProcessor* GetDataStreamProcessor() const { return StreamProcessor; }

    /** Set update frequency (called by RealGazeboBridgeManager) */
    void SetUpdateFrequency(float Frequency) { ConfiguredUpdateFrequency = FMath::Clamp(Frequency, 10.0f, 120.0f); }

    /** Get current update frequency */
    UFUNCTION(BlueprintCallable, Category = "Bridge|Status")
    float GetUpdateFrequency() const { return ConfiguredUpdateFrequency; }

    //----------------------------------------------------------
    // Events (for Blueprint integration)
    //----------------------------------------------------------

    UPROPERTY(BlueprintAssignable, Category = "Bridge|Events")
    FOnVehicleDataReceived OnVehicleSpawned;

    UPROPERTY(BlueprintAssignable, Category = "Bridge|Events")
    FOnVehicleDataReceived OnVehicleUpdated;

    //----------------------------------------------------------
    // Performance Monitoring
    //----------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "Bridge|Performance")
    void GetPerformanceStats(int32& OutTotalVehicles, int32& OutVisibleVehicles, 
                           float& OutAverageUpdateTime, float& OutMemoryUsageMB) const;

    //----------------------------------------------------------
    // Static Access
    //----------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "Bridge|Access", meta = (CallInEditor = "true"))
    static UGazeboBridgeSubsystem* GetBridgeSubsystem(const UObject* WorldContext);

public:
    //----------------------------------------------------------
    // Internal Vehicle Data Management 
    //----------------------------------------------------------

    /** Main vehicle data storage - optimized for fast lookup */
    TMap<FVehicleID, FVehicleRuntimeData> VehicleDataMap;

    /** Update vehicle data from network (called by DataStreamProcessor) */
    void UpdateVehicleData(const FBridgePoseData& PoseData);
    void UpdateVehicleMotorData(const FBridgeMotorSpeedData& MotorData);
    void UpdateVehicleServoData(const FBridgeServoData& ServoData);
    void UpdateVehicleAdditionalData(const FBridgeAdditionalData& AdditionalData);

    /** Batch update system */
    void BatchUpdateVehicles();

protected:
    //----------------------------------------------------------
    // Core Components
    //----------------------------------------------------------

    UPROPERTY()
    TObjectPtr<UDataStreamProcessor> StreamProcessor;

    UPROPERTY()
    TObjectPtr<UVehiclePoolManager> VehiclePool;

    //----------------------------------------------------------
    // Performance Optimization
    //----------------------------------------------------------


    /** Performance counters */
    mutable float LastUpdateTime = 0.0f;
    mutable int32 FrameCounter = 0;
    mutable float AverageFrameTime = 0.0f;

    /** Memory usage tracking */
    mutable float MemoryUsageMB = 0.0f;

    //----------------------------------------------------------
    // Internal Methods
    //----------------------------------------------------------

    /** Initialize network processing */
    void InitializeNetworkProcessing();

    /** Cleanup network processing */
    void ShutdownNetworkProcessing();



    /** Spawn visual pawn for vehicle */
    void SpawnVehiclePawn(const FVehicleID& VehicleID, FVehicleRuntimeData& VehicleData);

    /** Release visual pawn for vehicle */
    void ReleaseVehiclePawn(FVehicleRuntimeData& VehicleData);

    //----------------------------------------------------------
    // Event Handlers
    //----------------------------------------------------------

    UFUNCTION()
    void OnPoseDataReceived(const FBridgePoseData& PoseData);

    UFUNCTION()
    void OnMotorSpeedDataReceived(const FBridgeMotorSpeedData& MotorData);

    UFUNCTION()
    void OnServoDataReceived(const FBridgeServoData& ServoData);

    UFUNCTION()
    void OnAdditionalDataReceived(const FBridgeAdditionalData& AdditionalData);

private:
    /**
     * In-memory cache pushed by ARealGazeboManager from the central
     * UVehicleRegistrySubsystem (core DT + merged mod DTs). When non-empty,
     * GetVehicleConfigInternal hits this map instead of scanning VehicleConfigTable,
     * which gives mod rows a way to be visible without touching the core asset.
     */
    TMap<uint8, FBridgeVehicleConfigRow> PushedVehicleConfigs;

    /** Bridge active state (thread-safe atomic) */
    std::atomic<bool> bIsBridgeActive;

    /** Cached world reference */
    TWeakObjectPtr<UWorld> CachedWorld;

    /** Update timer handle */
    FTimerHandle UpdateTimerHandle;

    /** Performance monitoring */
    mutable double LastPerformanceCheck = 0.0;

    /** Update frequency configured by RealGazeboBridgeManager */
    float ConfiguredUpdateFrequency = 60.0f;
};