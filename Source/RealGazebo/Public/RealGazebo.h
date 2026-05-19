// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Delegates/IDelegateInstance.h"

class IPakFile;
class IAssetRegistry;

DECLARE_LOG_CATEGORY_EXTERN(LogRealGazebo, Log, All);



/**
 * RealGazebo Main Module
 *
 * Purpose:
 * - Provides unified dependency on RealGazeboBridge and RealGazeboUI
 * - Registers foreign-project mount points so DLC paks cooked in a separate
 *   Unreal project (e.g. a modder cooks `VehicleMod` in `RealGazeboVehicle`
 *   while the game ships as `C_Track`) become discoverable to the asset
 *   registry when they land in <Game>/Content/Paks/.
 * - Engine automatically loads all modules based on .uplugin file
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

	/** Handle for the OnPakFileMounted2 callback so we can detach on shutdown. */
	FDelegateHandle PakMountedHandle;

	/**
	 * Called every time a pak file is mounted. For paks cooked in a different
	 * Unreal project the mount point looks like "../../../<OtherProject>/Plugins/",
	 * which no UE root maps to by default - we register the mount root here so
	 * the assets become reachable.
	 */
	void OnPakFileMounted(const IPakFile& PakFile);

	/**
	 * Parses a raw pak mount path and, if it points into a foreign project root
	 * that the engine has not already wired up, registers the corresponding
	 * "/<ProjectName>/" mount point with FPackageName.
	 */
	void RegisterMountPointForForeignPak(const FString& RawMountPoint);

	/**
	 * Opens every pak file currently sitting in <Game>/Content/Paks/ (typically
	 * mounted by the engine at boot, before our module loaded) and registers
	 * any foreign mount roots it finds. The callback alone is not enough:
	 * UE mounts paks during PakPlatformFile init which runs before plugin
	 * modules start up, so OnPakFileMounted2 never fires for those.
	 */
	void ScanContentPaksAndRegister();

	/**
	 * In a cooked build, ScanPathsSynchronous cannot see assets living inside
	 * an IoStore container. Instead we locate the DLC's cooked AssetRegistry
	 * chunk (placed alongside the pak during cook), deserialise it, and merge
	 * it into the main IAssetRegistry via AppendState so the DLC's UDataTable
	 * and UBlueprint records become queryable.
	 */
	void LoadAndAppendDlcAssetRegistry(IAssetRegistry& AssetRegistry, const FString& MountRoot, const FString& ForeignProjectName);

	/**
	 * Walks the freshly-deserialised DLC state to find every distinct plugin
	 * mount root the cooked assets address themselves with (e.g. "/VehicleMod/"),
	 * then for each one:
	 *   - registers FPackageName mount point so GetLocalFullPath et al resolve
	 *   - opens the matching shader code library so material shader maps can
	 *     find their hashes (otherwise they fall back to default material and
	 *     the vehicle ends up looking untextured)
	 *
	 * Both are normally done by the engine when the plugin lives inside the
	 * shipping project, but since this DLC was cooked from a separate UE
	 * project there is no uplugin registered in the base game.
	 */
	void RegisterDlcPluginMountsAndShaders(class FAssetRegistryState& DLCState, const FString& ForeignProjectName);
};