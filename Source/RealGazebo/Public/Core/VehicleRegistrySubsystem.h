// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Sub-author: MinKyu Kim
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UObject/SoftObjectPath.h"
#include "Data/RealGazeboVehicleData.h"
#include "VehicleRegistrySubsystem.generated.h"

class UDataTable;
class UTexture2D;
class AVehicleBasePawn;
class IAssetRegistry;
struct FAssetData;

/**
 * Centralized in-memory registry for vehicle configurations.
 *
 * Sources merged at runtime:
 *   1. Core DataTable - registered by ARealGazeboManager / ARealGazeboBridgeManager in BeginPlay
 *      (the asset assigned by the user in the actor's Details panel slot)
 *   2. Mod DataTables  - discovered via AssetRegistry by filtering for any UDataTable whose
 *      RowStruct == FRealGazeboVehicleConfigRow, excluding the core asset path
 *
 * Conflict resolution (key = VehicleTypeCode, the network protocol identifier):
 *   - Core vs Mod  : core wins, mod row is rejected with an error log
 *   - Mod  vs Mod  : both rejected, the type code is left empty
 *   - VehicleTypeCode == 0 from a mod is rejected (reserved default)
 *
 * Lookup callers (BridgeSubsystem, VehiclePoolManager, MainWidget, etc.) talk to this
 * subsystem instead of touching DataTables directly.
 */
UCLASS(BlueprintType)
class REALGAZEBO_API UVehicleRegistrySubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    //----------------------------------------------------------
    // Subsystem Interface
    //----------------------------------------------------------

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    //----------------------------------------------------------
    // Registration (called by manager actors during BeginPlay)
    //----------------------------------------------------------

    /**
     * Register the user-assigned DataTable as the authoritative core source.
     * Rows from this table take precedence over any mod row sharing the same VehicleTypeCode.
     * Safe to call multiple times - subsequent calls re-scan and rebuild.
     */
    UFUNCTION(BlueprintCallable, Category = "RealGazebo|Registry")
    void RegisterCoreSource(UDataTable* CoreDataTable);

    //----------------------------------------------------------
    // Lookup API
    //----------------------------------------------------------

    /** True if a row exists for this type code. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazebo|Registry")
    bool HasVehicleData(uint8 VehicleTypeCode) const;

    /** Copy the merged row by value. Returns false if not registered. */
    UFUNCTION(BlueprintCallable, Category = "RealGazebo|Registry")
    bool GetVehicleData(uint8 VehicleTypeCode, FRealGazeboVehicleConfigRow& OutData) const;

    /** Returns the soft class pointer (mod-friendly). Caller is responsible for async/sync loading. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazebo|Registry")
    TSoftClassPtr<AVehicleBasePawn> GetVehicleSoftClass(uint8 VehicleTypeCode) const;

    /** Convenience: synchronously load and return the resolved class. Used by spawn paths. */
    UFUNCTION(BlueprintCallable, Category = "RealGazebo|Registry")
    TSubclassOf<AVehicleBasePawn> GetVehicleClassLoaded(uint8 VehicleTypeCode);

    /** Returns all registered type codes. If bUIOnly, filters to rows where bShowInUI == true. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazebo|Registry")
    TArray<uint8> GetAllVehicleTypeCodes(bool bUIOnly = false) const;

    /** Sync-load and return the icon texture for a type code (used by UI widget). */
    UFUNCTION(BlueprintCallable, Category = "RealGazebo|Registry")
    UTexture2D* GetVehicleImage(uint8 VehicleTypeCode);

    /** Diagnostic: returns "Core" or the mod pak/source label that contributed this row. NAME_None if unregistered. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazebo|Registry")
    FName GetSourceForTypeCode(uint8 VehicleTypeCode) const;

    /** Diagnostic: total number of registered type codes. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazebo|Registry")
    int32 GetVehicleCount() const { return VehicleMap.Num(); }

    //----------------------------------------------------------
    // Events
    //----------------------------------------------------------

    /**
     * Fired when the registry contents change.
     *
     * Two practical firings happen during normal startup:
     *   1. Right after RegisterCoreSource (the core DataTable just landed)
     *   2. Right after the AssetRegistry mod scan completes (mod DataTables added)
     *
     * The second firing matters because the mod scan can finish *after*
     * ARealGazeboManager::BeginPlay has already pushed the core-only set
     * to UGazeboBridgeSubsystem. Listening here lets the manager re-push
     * the now-complete merged set.
     */
    DECLARE_MULTICAST_DELEGATE(FOnRegistryUpdated);
    FOnRegistryUpdated OnRegistryUpdated;

    //----------------------------------------------------------
    // Test / debug hooks
    //----------------------------------------------------------

    /**
     * Test-only: register a DataTable as if it came from a mod source with the given label.
     * Production code does not call this - mod sources arrive via AssetRegistry scan.
     */
    void RegisterModSource_ForTesting(UDataTable* ModDataTable, FName ModLabel);

    /** Test-only: wipe registry state so tests can start clean. */
    void ResetForTesting();

    //----------------------------------------------------------
    // Static Access
    //----------------------------------------------------------

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RealGazebo|Registry", meta = (WorldContext = "WorldContext"))
    static UVehicleRegistrySubsystem* Get(const UObject* WorldContext);

private:
    /** Outcome of attempting to add a single row from a source. */
    enum class EAddRowResult : uint8
    {
        Added,
        RejectedByCorePrecedence,
        RejectedByModConflict,
        RejectedInvalidTypeCode,
    };

    /** Add a single row, applying conflict policy. */
    EAddRowResult AddRow(const FRealGazeboVehicleConfigRow& Row, FName Source, bool bFromCore);

    /** Iterate a DataTable's rows of type FRealGazeboVehicleConfigRow and add each. */
    void MergeDataTable(UDataTable* DataTable, FName Source, bool bFromCore);

    /** Scan AssetRegistry for mod DataTables and merge them. Excludes paths in CoreAssetPaths. */
    void ScanForModDataTables();

    /** Bound to IAssetRegistry::OnFilesLoaded. */
    void OnAssetRegistryFilesLoaded();

    /** Returns the AssetRegistry interface, or nullptr if module unavailable. */
    IAssetRegistry* GetAssetRegistry() const;

private:
    /** Merged data: VehicleTypeCode -> row. */
    TMap<uint8, FRealGazeboVehicleConfigRow> VehicleMap;

    /** Diagnostic: VehicleTypeCode -> source label ("Core" or mod identifier). */
    TMap<uint8, FName> SourceMap;

    /** Type codes that are currently disabled because two mods both claim them. */
    TSet<uint8> ModConflictBlocklist;

    /** Asset paths registered as core - excluded from mod scan to avoid double-counting. */
    TSet<FSoftObjectPath> CoreAssetPaths;

    /** True once the AssetRegistry mod scan has run at least once. */
    bool bModScanCompleted = false;

    /**
     * AssetRegistry has finished its initial asset discovery and is safe to query.
     * Set either inside Initialize (if registry was already loaded) or from the
     * OnFilesLoaded callback (if registry was still loading).
     */
    bool bAssetRegistryReady = false;

    /** True once a core DataTable has been registered via RegisterCoreSource. */
    bool bCoreRegistered = false;

    /** Handle to the OnFilesLoaded delegate so we can unbind on Deinitialize. */
    FDelegateHandle FilesLoadedHandle;

    /**
     * Runs the mod scan if and only if both the core source has been registered
     * AND the AssetRegistry is ready. Without the core, we have no CoreAssetPaths
     * to exclude, so the core asset would be misclassified as a mod.
     */
    void TryRunModScan();
};
