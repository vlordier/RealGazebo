// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : MinKyu Kim
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "UObject/Package.h"

#include "Core/VehicleRegistrySubsystem.h"
#include "Data/RealGazeboVehicleData.h"

namespace VehicleRegistryTestHelpers
{
    /** Build a transient DataTable of FRealGazeboVehicleConfigRow rows for use in a single test. */
    static UDataTable* CreateTestDataTable(const TArray<FRealGazeboVehicleConfigRow>& Rows)
    {
        UDataTable* Table = NewObject<UDataTable>(
            GetTransientPackage(), UDataTable::StaticClass(), NAME_None, RF_Transient);
        Table->RowStruct = FRealGazeboVehicleConfigRow::StaticStruct();
        for (int32 Index = 0; Index < Rows.Num(); ++Index)
        {
            const FName RowName(*FString::Printf(TEXT("TestRow_%d"), Index));
            Table->AddRow(RowName, Rows[Index]);
        }
        return Table;
    }

    static FRealGazeboVehicleConfigRow MakeRow(uint8 TypeCode, const FString& Name)
    {
        FRealGazeboVehicleConfigRow Row;
        Row.VehicleTypeCode = TypeCode;
        Row.VehicleName = Name;
        return Row;
    }

    /**
     * Subsystem instances built with NewObject are never Initialize()'d, so the
     * AssetRegistry mod scan does not run. This keeps each test isolated from
     * whatever vehicle DataTables happen to exist on disk.
     *
     * UGameInstanceSubsystem has ClassWithin=GameInstance, so we manufacture a
     * throwaway GameInstance to serve as the Outer. The GameInstance is never
     * Init()'d either - it exists purely to satisfy the within constraint.
     */
    static UVehicleRegistrySubsystem* MakeFreshRegistry()
    {
        UGameInstance* TransientGI = NewObject<UGameInstance>(
            GetTransientPackage(), UGameInstance::StaticClass(), NAME_None, RF_Transient);
        UVehicleRegistrySubsystem* Registry = NewObject<UVehicleRegistrySubsystem>(
            TransientGI, UVehicleRegistrySubsystem::StaticClass(), NAME_None, RF_Transient);
        Registry->ResetForTesting();
        return Registry;
    }
}

using namespace VehicleRegistryTestHelpers;

//----------------------------------------------------------
// 1. Core-only: rows from a single core DataTable are queryable, source = "Core"
//----------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleRegistryCoreOnlyTest,
    "RealGazebo.VehicleRegistry.CoreOnly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVehicleRegistryCoreOnlyTest::RunTest(const FString& Parameters)
{
    UVehicleRegistrySubsystem* Registry = MakeFreshRegistry();

    UDataTable* CoreDT = CreateTestDataTable({
        MakeRow(1, TEXT("X500")),
        MakeRow(2, TEXT("Iris")),
        MakeRow(50, TEXT("Rover")),
    });
    Registry->RegisterCoreSource(CoreDT);

    TestEqual(TEXT("registry size"), Registry->GetVehicleCount(), 3);

    FRealGazeboVehicleConfigRow Out;
    TestTrue(TEXT("type 1 found"), Registry->GetVehicleData(1, Out));
    TestEqual(TEXT("type 1 name"), Out.VehicleName, FString(TEXT("X500")));

    TestTrue(TEXT("type 50 found"), Registry->GetVehicleData(50, Out));
    TestEqual(TEXT("type 50 name"), Out.VehicleName, FString(TEXT("Rover")));

    TestFalse(TEXT("unknown type absent"), Registry->HasVehicleData(99));

    TestEqual(TEXT("source label"),
        Registry->GetSourceForTypeCode(1).ToString(), FString(TEXT("Core")));

    return true;
}

//----------------------------------------------------------
// 2. Mod merge: core + mod with disjoint type codes -> both visible
//----------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleRegistryModMergeTest,
    "RealGazebo.VehicleRegistry.ModMerge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVehicleRegistryModMergeTest::RunTest(const FString& Parameters)
{
    UVehicleRegistrySubsystem* Registry = MakeFreshRegistry();

    UDataTable* CoreDT = CreateTestDataTable({
        MakeRow(1, TEXT("X500")),
        MakeRow(2, TEXT("Iris")),
    });
    Registry->RegisterCoreSource(CoreDT);

    UDataTable* ModDT = CreateTestDataTable({
        MakeRow(100, TEXT("SkyflyDrone")),
        MakeRow(101, TEXT("SkyflyHeavy")),
    });
    Registry->RegisterModSource_ForTesting(ModDT, TEXT("SkyflyMod"));

    TestEqual(TEXT("merged size"), Registry->GetVehicleCount(), 4);

    FRealGazeboVehicleConfigRow Out;
    TestTrue(TEXT("core row still there"), Registry->GetVehicleData(1, Out));
    TestEqual(TEXT("core source"),
        Registry->GetSourceForTypeCode(1).ToString(), FString(TEXT("Core")));

    TestTrue(TEXT("mod row visible"), Registry->GetVehicleData(100, Out));
    TestEqual(TEXT("mod row name"), Out.VehicleName, FString(TEXT("SkyflyDrone")));
    TestEqual(TEXT("mod source"),
        Registry->GetSourceForTypeCode(100).ToString(), FString(TEXT("SkyflyMod")));

    return true;
}

//----------------------------------------------------------
// 3. Core wins on conflict: mod tries to override a core type code, gets rejected
//----------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleRegistryCoreWinsConflictTest,
    "RealGazebo.VehicleRegistry.CoreWinsConflict",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVehicleRegistryCoreWinsConflictTest::RunTest(const FString& Parameters)
{
    UVehicleRegistrySubsystem* Registry = MakeFreshRegistry();

    UDataTable* CoreDT = CreateTestDataTable({ MakeRow(1, TEXT("X500_Core")) });
    Registry->RegisterCoreSource(CoreDT);

    // Mod claims the same type code with a different name - should be rejected.
    AddExpectedError(TEXT("tried to override core type code"),
        EAutomationExpectedErrorFlags::Contains, 1);

    UDataTable* ModDT = CreateTestDataTable({ MakeRow(1, TEXT("X500_ModOverride")) });
    Registry->RegisterModSource_ForTesting(ModDT, TEXT("BadMod"));

    TestEqual(TEXT("count still 1"), Registry->GetVehicleCount(), 1);

    FRealGazeboVehicleConfigRow Out;
    TestTrue(TEXT("type 1 still found"), Registry->GetVehicleData(1, Out));
    TestEqual(TEXT("core name preserved"),
        Out.VehicleName, FString(TEXT("X500_Core")));
    TestEqual(TEXT("source still Core"),
        Registry->GetSourceForTypeCode(1).ToString(), FString(TEXT("Core")));

    return true;
}

//----------------------------------------------------------
// 4. Mod-mod conflict: both mods rejected, type code disabled
//----------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleRegistryModModConflictTest,
    "RealGazebo.VehicleRegistry.ModModConflict",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVehicleRegistryModModConflictTest::RunTest(const FString& Parameters)
{
    UVehicleRegistrySubsystem* Registry = MakeFreshRegistry();

    UDataTable* CoreDT = CreateTestDataTable({ MakeRow(1, TEXT("CoreVehicle")) });
    Registry->RegisterCoreSource(CoreDT);

    AddExpectedError(TEXT("mod-mod conflict on type code"),
        EAutomationExpectedErrorFlags::Contains, 1);

    UDataTable* ModA = CreateTestDataTable({ MakeRow(200, TEXT("ModA_Drone")) });
    UDataTable* ModB = CreateTestDataTable({ MakeRow(200, TEXT("ModB_Drone")) });
    Registry->RegisterModSource_ForTesting(ModA, TEXT("ModA"));
    Registry->RegisterModSource_ForTesting(ModB, TEXT("ModB"));

    // Core row is untouched.
    TestTrue(TEXT("core row safe"), Registry->HasVehicleData(1));
    // Disputed type code is blocked.
    TestFalse(TEXT("conflicted code rejected"), Registry->HasVehicleData(200));

    return true;
}

//----------------------------------------------------------
// 5. Invalid type code: mod row with VehicleTypeCode == 0 rejected (reserved default)
//----------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleRegistryRejectsZeroTypeCodeTest,
    "RealGazebo.VehicleRegistry.RejectsZeroTypeCode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVehicleRegistryRejectsZeroTypeCodeTest::RunTest(const FString& Parameters)
{
    UVehicleRegistrySubsystem* Registry = MakeFreshRegistry();

    AddExpectedError(TEXT("VehicleTypeCode=0"),
        EAutomationExpectedErrorFlags::Contains, 1);

    UDataTable* ModDT = CreateTestDataTable({ MakeRow(0, TEXT("LazyMod")) });
    Registry->RegisterModSource_ForTesting(ModDT, TEXT("LazyMod"));

    TestFalse(TEXT("zero code rejected"), Registry->HasVehicleData(0));
    TestEqual(TEXT("empty registry"), Registry->GetVehicleCount(), 0);

    return true;
}

//----------------------------------------------------------
// 6. Out-of-order registration: mod arrives before core, core still wins
//----------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleRegistryCoreLateArrivalTest,
    "RealGazebo.VehicleRegistry.CoreLateArrival",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVehicleRegistryCoreLateArrivalTest::RunTest(const FString& Parameters)
{
    UVehicleRegistrySubsystem* Registry = MakeFreshRegistry();

    // Simulate AssetRegistry mod scan completing before the manager BeginPlay'd.
    UDataTable* ModDT = CreateTestDataTable({ MakeRow(5, TEXT("Mod_Provisional")) });
    Registry->RegisterModSource_ForTesting(ModDT, TEXT("EarlyMod"));

    TestEqual(TEXT("mod row temporarily owns code 5"),
        Registry->GetSourceForTypeCode(5).ToString(), FString(TEXT("EarlyMod")));

    // Now the user's core asset arrives. It must evict the mod row for code 5.
    AddExpectedError(TEXT("core overrides mod"),
        EAutomationExpectedErrorFlags::Contains, 1);

    UDataTable* CoreDT = CreateTestDataTable({ MakeRow(5, TEXT("Core_Authoritative")) });
    Registry->RegisterCoreSource(CoreDT);

    FRealGazeboVehicleConfigRow Out;
    TestTrue(TEXT("type 5 still present"), Registry->GetVehicleData(5, Out));
    TestEqual(TEXT("core name took over"),
        Out.VehicleName, FString(TEXT("Core_Authoritative")));
    TestEqual(TEXT("source now Core"),
        Registry->GetSourceForTypeCode(5).ToString(), FString(TEXT("Core")));

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
