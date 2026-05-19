// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Sub-author: MinKyu Kim
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#include "Core/VehicleRegistrySubsystem.h"

#include "RealGazebo.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "Engine/GameInstance.h"
#include "Vehicles/VehicleBasePawn.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"

static const FName CoreSourceLabel(TEXT("Core"));

//----------------------------------------------------------
// Subsystem lifecycle
//----------------------------------------------------------

void UVehicleRegistrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UE_LOG(LogRealGazebo, Log, TEXT("VehicleRegistry: Initializing"));

    if (IAssetRegistry* AssetRegistry = GetAssetRegistry())
    {
        if (AssetRegistry->IsLoadingAssets())
        {
            FilesLoadedHandle = AssetRegistry->OnFilesLoaded().AddUObject(
                this, &UVehicleRegistrySubsystem::OnAssetRegistryFilesLoaded);
        }
        else
        {
            // AssetRegistry has finished its initial scan. We still don't run
            // the mod scan here - that has to wait until a core source has been
            // registered, otherwise the core asset itself looks like a mod
            // (its CoreAssetPaths entry hasn't been added yet) and gets
            // misclassified.
            bAssetRegistryReady = true;
        }
    }
    else
    {
        UE_LOG(LogRealGazebo, Warning, TEXT("VehicleRegistry: AssetRegistry unavailable, mod scan skipped"));
    }
}

void UVehicleRegistrySubsystem::Deinitialize()
{
    if (FilesLoadedHandle.IsValid())
    {
        if (IAssetRegistry* AssetRegistry = GetAssetRegistry())
        {
            AssetRegistry->OnFilesLoaded().Remove(FilesLoadedHandle);
        }
        FilesLoadedHandle.Reset();
    }

    VehicleMap.Empty();
    SourceMap.Empty();
    ModConflictBlocklist.Empty();
    CoreAssetPaths.Empty();
    bModScanCompleted = false;
    bAssetRegistryReady = false;
    bCoreRegistered = false;

    Super::Deinitialize();
}

//----------------------------------------------------------
// Registration
//----------------------------------------------------------

void UVehicleRegistrySubsystem::RegisterCoreSource(UDataTable* CoreDataTable)
{
    if (!CoreDataTable)
    {
        UE_LOG(LogRealGazebo, Warning, TEXT("VehicleRegistry: RegisterCoreSource called with null table"));
        return;
    }

    if (CoreDataTable->GetRowStruct() != FRealGazeboVehicleConfigRow::StaticStruct())
    {
        UE_LOG(LogRealGazebo, Error, TEXT("VehicleRegistry: '%s' has row struct %s, expected FRealGazeboVehicleConfigRow - skipping"),
               *CoreDataTable->GetName(),
               CoreDataTable->GetRowStruct() ? *CoreDataTable->GetRowStruct()->GetName() : TEXT("<null>"));
        return;
    }

    const FSoftObjectPath CorePath(CoreDataTable);
    CoreAssetPaths.Add(CorePath);

    MergeDataTable(CoreDataTable, CoreSourceLabel, /*bFromCore=*/ true);
    bCoreRegistered = true;

    UE_LOG(LogRealGazebo, Log, TEXT("VehicleRegistry: Core source '%s' registered (%d total type codes)"),
           *CoreDataTable->GetName(), VehicleMap.Num());

    // Now that we know which asset is the core, the mod scan can safely run
    // (or has been waiting for this). If the AssetRegistry was already loaded
    // back in Initialize, this is where the scan actually fires - the core path
    // is now in CoreAssetPaths so the core asset itself is correctly excluded.
    TryRunModScan();

    OnRegistryUpdated.Broadcast();
}

void UVehicleRegistrySubsystem::TryRunModScan()
{
    if (bModScanCompleted || !bAssetRegistryReady || !bCoreRegistered)
    {
        return;
    }

    ScanForModDataTables();
    bModScanCompleted = true;
}

//----------------------------------------------------------
// Lookup API
//----------------------------------------------------------

bool UVehicleRegistrySubsystem::HasVehicleData(uint8 VehicleTypeCode) const
{
    return VehicleMap.Contains(VehicleTypeCode);
}

bool UVehicleRegistrySubsystem::GetVehicleData(uint8 VehicleTypeCode, FRealGazeboVehicleConfigRow& OutData) const
{
    if (const FRealGazeboVehicleConfigRow* Row = VehicleMap.Find(VehicleTypeCode))
    {
        OutData = *Row;
        return true;
    }
    return false;
}

TSoftClassPtr<AVehicleBasePawn> UVehicleRegistrySubsystem::GetVehicleSoftClass(uint8 VehicleTypeCode) const
{
    if (const FRealGazeboVehicleConfigRow* Row = VehicleMap.Find(VehicleTypeCode))
    {
        if (!Row->VehiclePawnClassSoft.IsNull())
        {
            return Row->VehiclePawnClassSoft;
        }
        // Legacy fallback: synthesize soft pointer from the hard-ref field.
        if (Row->VehiclePawnClass)
        {
            return TSoftClassPtr<AVehicleBasePawn>(Row->VehiclePawnClass);
        }
    }
    return TSoftClassPtr<AVehicleBasePawn>();
}

TSubclassOf<AVehicleBasePawn> UVehicleRegistrySubsystem::GetVehicleClassLoaded(uint8 VehicleTypeCode)
{
    const FRealGazeboVehicleConfigRow* Row = VehicleMap.Find(VehicleTypeCode);
    if (!Row)
    {
        return nullptr;
    }

    // Prefer the soft pointer (mod-friendly), fall back to legacy hard ref.
    if (!Row->VehiclePawnClassSoft.IsNull())
    {
        if (UClass* Loaded = Row->VehiclePawnClassSoft.LoadSynchronous())
        {
            return Loaded;
        }
        UE_LOG(LogRealGazebo, Warning, TEXT("VehicleRegistry: type %d soft class failed to load (%s)"),
               VehicleTypeCode, *Row->VehiclePawnClassSoft.ToString());
    }

    if (Row->VehiclePawnClass)
    {
        return Row->VehiclePawnClass;
    }

    UE_LOG(LogRealGazebo, Warning, TEXT("VehicleRegistry: type %d has no pawn class configured"), VehicleTypeCode);
    return nullptr;
}

TArray<uint8> UVehicleRegistrySubsystem::GetAllVehicleTypeCodes(bool bUIOnly) const
{
    TArray<uint8> Result;
    Result.Reserve(VehicleMap.Num());

    for (const TPair<uint8, FRealGazeboVehicleConfigRow>& Pair : VehicleMap)
    {
        if (bUIOnly && !Pair.Value.bShowInUI)
        {
            continue;
        }
        Result.Add(Pair.Key);
    }
    Result.Sort();
    return Result;
}

UTexture2D* UVehicleRegistrySubsystem::GetVehicleImage(uint8 VehicleTypeCode)
{
    const FRealGazeboVehicleConfigRow* Row = VehicleMap.Find(VehicleTypeCode);
    if (!Row || Row->VehicleImage.IsNull())
    {
        return nullptr;
    }
    return Row->VehicleImage.LoadSynchronous();
}

FName UVehicleRegistrySubsystem::GetSourceForTypeCode(uint8 VehicleTypeCode) const
{
    if (const FName* Source = SourceMap.Find(VehicleTypeCode))
    {
        return *Source;
    }
    return NAME_None;
}

//----------------------------------------------------------
// Internal: row merge with conflict policy
//----------------------------------------------------------

UVehicleRegistrySubsystem::EAddRowResult UVehicleRegistrySubsystem::AddRow(
    const FRealGazeboVehicleConfigRow& Row, FName Source, bool bFromCore)
{
    const uint8 Code = Row.VehicleTypeCode;

    if (!bFromCore && Code == 0)
    {
        UE_LOG(LogRealGazebo, Warning,
               TEXT("VehicleRegistry: mod '%s' supplied row with VehicleTypeCode=0 (reserved default) - rejected"),
               *Source.ToString());
        return EAddRowResult::RejectedInvalidTypeCode;
    }

    if (ModConflictBlocklist.Contains(Code))
    {
        // Two prior mods already collided on this code. Only the core can rescue it.
        if (!bFromCore)
        {
            UE_LOG(LogRealGazebo, Warning,
                   TEXT("VehicleRegistry: code %d is blocked due to mod-mod conflict, mod '%s' rejected"),
                   Code, *Source.ToString());
            return EAddRowResult::RejectedByModConflict;
        }
        // Core overrides the blocklist.
        ModConflictBlocklist.Remove(Code);
    }

    if (const FName* ExistingSource = SourceMap.Find(Code))
    {
        const bool bExistingIsCore = (*ExistingSource == CoreSourceLabel);

        if (bExistingIsCore)
        {
            // Core already owns this code; reject any mod attempting to redefine it.
            if (!bFromCore)
            {
                UE_LOG(LogRealGazebo, Error,
                       TEXT("VehicleRegistry: mod '%s' tried to override core type code %d - rejected"),
                       *Source.ToString(), Code);
                return EAddRowResult::RejectedByCorePrecedence;
            }
            // Core re-registration (e.g. user reassigned slot): overwrite is fine.
            VehicleMap.Add(Code, Row);
            return EAddRowResult::Added;
        }

        // Existing row is from a mod.
        if (bFromCore)
        {
            // Core arrives after mod for the same code: core wins, evict mod row.
            UE_LOG(LogRealGazebo, Warning,
                   TEXT("VehicleRegistry: core overrides mod '%s' for type code %d"),
                   *ExistingSource->ToString(), Code);
            VehicleMap.Add(Code, Row);
            SourceMap.Add(Code, CoreSourceLabel);
            return EAddRowResult::Added;
        }

        // Two mods collide. Reject both and remember the collision.
        UE_LOG(LogRealGazebo, Error,
               TEXT("VehicleRegistry: mod-mod conflict on type code %d between '%s' and '%s' - both rejected"),
               Code, *ExistingSource->ToString(), *Source.ToString());
        VehicleMap.Remove(Code);
        SourceMap.Remove(Code);
        ModConflictBlocklist.Add(Code);
        return EAddRowResult::RejectedByModConflict;
    }

    // Fresh code.
    VehicleMap.Add(Code, Row);
    SourceMap.Add(Code, Source);
    return EAddRowResult::Added;
}

void UVehicleRegistrySubsystem::MergeDataTable(UDataTable* DataTable, FName Source, bool bFromCore)
{
    if (!DataTable)
    {
        return;
    }

    int32 Added = 0;
    int32 Rejected = 0;

    const TArray<FName> RowNames = DataTable->GetRowNames();
    for (const FName& RowName : RowNames)
    {
        const FRealGazeboVehicleConfigRow* RowPtr =
            DataTable->FindRow<FRealGazeboVehicleConfigRow>(RowName, TEXT("VehicleRegistry::MergeDataTable"));
        if (!RowPtr)
        {
            continue;
        }

        const EAddRowResult Result = AddRow(*RowPtr, Source, bFromCore);
        if (Result == EAddRowResult::Added)
        {
            ++Added;
        }
        else
        {
            ++Rejected;
        }
    }

    UE_LOG(LogRealGazebo, Log,
           TEXT("VehicleRegistry: merged '%s' (%s) - added=%d rejected=%d"),
           *DataTable->GetName(),
           bFromCore ? TEXT("core") : TEXT("mod"),
           Added, Rejected);
}

//----------------------------------------------------------
// AssetRegistry scan for mod DataTables
//----------------------------------------------------------

void UVehicleRegistrySubsystem::ScanForModDataTables()
{
    IAssetRegistry* AssetRegistry = GetAssetRegistry();
    if (!AssetRegistry)
    {
        return;
    }

    FARFilter Filter;
    Filter.ClassPaths.Add(UDataTable::StaticClass()->GetClassPathName());
    Filter.bRecursiveClasses = true;
    Filter.bRecursivePaths = true;
    // Empty PackagePaths means "scan everywhere AssetRegistry knows about" -
    // including /Game, plugin content roots, and any pak mount points.

    TArray<FAssetData> AssetList;
    AssetRegistry->GetAssets(Filter, AssetList);

    int32 ModTablesScanned = 0;
    for (const FAssetData& Asset : AssetList)
    {
        const FSoftObjectPath AssetPath = Asset.ToSoftObjectPath();
        if (CoreAssetPaths.Contains(AssetPath))
        {
            // Skip the user's core asset - we already merged it from RegisterCoreSource.
            continue;
        }

        UDataTable* DataTable = Cast<UDataTable>(Asset.GetAsset());
        if (!DataTable || DataTable->GetRowStruct() != FRealGazeboVehicleConfigRow::StaticStruct())
        {
            continue;
        }

        // Use the package name (e.g. "/SkyflyMod/DataTables/DT_Skyfly") as a stable mod label.
        const FName ModLabel = Asset.PackageName;
        MergeDataTable(DataTable, ModLabel, /*bFromCore=*/ false);
        ++ModTablesScanned;
    }

    UE_LOG(LogRealGazebo, Log, TEXT("VehicleRegistry: mod scan complete - %d mod DataTable(s) found"),
           ModTablesScanned);
}

void UVehicleRegistrySubsystem::OnAssetRegistryFilesLoaded()
{
    bAssetRegistryReady = true;

    // If the core source already arrived while the registry was still loading,
    // the scan was deferred. Run it now.
    if (!bModScanCompleted && bCoreRegistered)
    {
        ScanForModDataTables();
        bModScanCompleted = true;
        OnRegistryUpdated.Broadcast();
    }
}

IAssetRegistry* UVehicleRegistrySubsystem::GetAssetRegistry() const
{
    if (FAssetRegistryModule* Module = FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry")))
    {
        return &Module->Get();
    }
    return nullptr;
}

//----------------------------------------------------------
// Test hooks
//----------------------------------------------------------

void UVehicleRegistrySubsystem::RegisterModSource_ForTesting(UDataTable* ModDataTable, FName ModLabel)
{
    if (!ModDataTable)
    {
        return;
    }
    if (ModDataTable->GetRowStruct() != FRealGazeboVehicleConfigRow::StaticStruct())
    {
        return;
    }
    MergeDataTable(ModDataTable, ModLabel, /*bFromCore=*/ false);
    OnRegistryUpdated.Broadcast();
}

void UVehicleRegistrySubsystem::ResetForTesting()
{
    VehicleMap.Empty();
    SourceMap.Empty();
    ModConflictBlocklist.Empty();
    CoreAssetPaths.Empty();
    bModScanCompleted = false;
    bAssetRegistryReady = false;
    bCoreRegistered = false;
}

//----------------------------------------------------------
// Static accessor
//----------------------------------------------------------

UVehicleRegistrySubsystem* UVehicleRegistrySubsystem::Get(const UObject* WorldContext)
{
    if (!WorldContext)
    {
        return nullptr;
    }

    if (UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr)
    {
        if (UGameInstance* GameInstance = World->GetGameInstance())
        {
            return GameInstance->GetSubsystem<UVehicleRegistrySubsystem>();
        }
    }
    return nullptr;
}
