// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Sub-author: MinKyu Kim
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

#include "RealGazebo.h"
#include "Engine/Engine.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "IPlatformFilePak.h"
#include "Serialization/MemoryReader.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetRegistryState.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ShaderCodeLibrary.h"

DEFINE_LOG_CATEGORY(LogRealGazebo);

#define LOCTEXT_NAMESPACE "FRealGazeboModule"

FRealGazeboModule* FRealGazeboModule::ModuleInstance = nullptr;

void FRealGazeboModule::StartupModule()
{
	UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Module: StartupModule"));
	ModuleInstance = this;

	// (a) Process paks that the engine already mounted before this module's
	//     StartupModule. UE mounts paks early in PakPlatformFile init, and
	//     OnPakFileMounted2 only fires going forward - so by the time we
	//     attach the callback below, every shipped pak has already been
	//     mounted and we'd miss them all without this scan.
	ScanContentPaksAndRegister();

	// (b) Hook the callback for any pak mounted from here on (e.g. runtime
	//     pak mounts, which are rare in our use case but cheap to support).
	PakMountedHandle = FCoreDelegates::GetOnPakFileMounted2().AddRaw(
		this, &FRealGazeboModule::OnPakFileMounted);

	UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Module: Successfully initialized"));
}

void FRealGazeboModule::ScanContentPaksAndRegister()
{
	const FString PaksDir = FPaths::ProjectContentDir() / TEXT("Paks");

	TArray<FString> PakFilenames;
	IFileManager::Get().FindFilesRecursive(PakFilenames, *PaksDir, TEXT("*.pak"), true, false);

	if (PakFilenames.Num() == 0)
	{
		return;
	}

	IPlatformFile& LowerLevel = FPlatformFileManager::Get().GetPlatformFile();

	for (const FString& PakFilename : PakFilenames)
	{
		// FPakFile is ref-counted (FRefCountBase) with a private destructor,
		// so it has to live on the heap via TRefCountPtr. We're only opening
		// to read the header - the engine already has it mounted for asset
		// serving, no conflict.
		TRefCountPtr<FPakFile> PakFile(new FPakFile(&LowerLevel, *PakFilename, /*bIsSigned=*/ false));
		if (!PakFile.IsValid() || !PakFile->IsValid())
		{
			UE_LOG(LogRealGazebo, Warning,
				TEXT("RealGazebo Module: could not open '%s' to read its mount point"),
				*PakFilename);
			continue;
		}

		RegisterMountPointForForeignPak(PakFile->GetMountPoint());
	}
}

void FRealGazeboModule::ShutdownModule()
{
	UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Module: ShutdownModule"));

	if (PakMountedHandle.IsValid())
	{
		FCoreDelegates::GetOnPakFileMounted2().Remove(PakMountedHandle);
		PakMountedHandle.Reset();
	}

	ModuleInstance = nullptr;
	UE_LOG(LogRealGazebo, Log, TEXT("RealGazebo Module: Successfully shut down"));
}

void FRealGazeboModule::OnPakFileMounted(const IPakFile& PakFile)
{
	const FString MountPoint = PakFile.PakGetMountPoint();
	RegisterMountPointForForeignPak(MountPoint);
}

void FRealGazeboModule::RegisterMountPointForForeignPak(const FString& RawMountPoint)
{
	// Mount points we encounter in practice:
	//   "../../../<ThisProject>/Content/"          ← base game, UE registers /Game/
	//   "../../../<ThisProject>/Plugins/<P>/..."   ← same-project plugin, UE handles
	//   "../../../<ForeignProject>/Plugins/..."    ← FOREIGN: this is what we fix
	//   "../../../Engine/Plugins/.../"             ← engine plugin
	//
	// Strategy: peel off "../" prefixes, take the first remaining path segment
	// as the project name, and if it is foreign register "/<Name>/" so the
	// asset registry recognises that namespace.

	FString Path = RawMountPoint;

	while (Path.StartsWith(TEXT("../"), ESearchCase::CaseSensitive))
	{
		Path.RemoveAt(0, 3);
	}
	if (Path.StartsWith(TEXT("/")))
	{
		Path.RemoveAt(0, 1);
	}
	if (Path.IsEmpty())
	{
		return;
	}

	int32 SlashIdx = INDEX_NONE;
	if (!Path.FindChar(TEXT('/'), SlashIdx))
	{
		return;
	}
	const FString ForeignProjectName = Path.Left(SlashIdx);

	// Skip anything the engine already covers.
	if (ForeignProjectName == FApp::GetProjectName()
		|| ForeignProjectName.Equals(TEXT("Engine"), ESearchCase::IgnoreCase)
		|| ForeignProjectName.Equals(TEXT("Game"), ESearchCase::IgnoreCase))
	{
		return;
	}

	const FString MountRoot = FString::Printf(TEXT("/%s/"), *ForeignProjectName);

	if (FPackageName::MountPointExists(MountRoot))
	{
		return;
	}

	// Map the mount root to the pak's own embedded directory string. PakPlatformFile
	// redirects file lookups against this virtual path back into the pak's IoStore
	// container - the directory does not need to exist on disk.
	const FString TargetDir = RawMountPoint;

	FPackageName::RegisterMountPoint(MountRoot, TargetDir);

	UE_LOG(LogRealGazebo, Log,
		TEXT("RealGazebo Module: registered foreign pak mount root '%s' -> '%s' (pak mount '%s')"),
		*MountRoot, *TargetDir, *RawMountPoint);

	// Registering the mount point only declares the namespace - AssetRegistry
	// has no idea what assets live there until it scans. Force a synchronous
	// scan now so the registry picks up the mod's DataTables/Blueprints.
	if (FAssetRegistryModule* AssetRegistryModule = FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry")))
	{
		IAssetRegistry& AssetRegistry = AssetRegistryModule->Get();
		TArray<FString> PathsToScan;
		PathsToScan.Add(MountRoot);
		AssetRegistry.ScanPathsSynchronous(PathsToScan, /*bForceRescan=*/ true);

		UE_LOG(LogRealGazebo, Log,
			TEXT("RealGazebo Module: AssetRegistry synchronous scan complete for '%s'"),
			*MountRoot);

		// In a cooked build, ScanPathsSynchronous only walks the file system -
		// the mod's actual assets live inside an IoStore container, so the
		// scan picks up nothing. UE writes a cooked-state AssetRegistry chunk
		// next to the DLC's pak; we load that chunk and merge it into the
		// main asset registry via AppendState. After merge, the registry
		// knows about the DLC's UDataTable / UBlueprint rows.
		LoadAndAppendDlcAssetRegistry(AssetRegistry, MountRoot, ForeignProjectName);
	}
	else
	{
		UE_LOG(LogRealGazebo, Warning,
			TEXT("RealGazebo Module: AssetRegistry module unavailable, mod assets under '%s' won't be discovered until next scan"),
			*MountRoot);
	}
}

void FRealGazeboModule::LoadAndAppendDlcAssetRegistry(IAssetRegistry& AssetRegistry, const FString& MountRoot, const FString& ForeignProjectName)
{
	// UE's DLC cook places the cooked AssetRegistry chunk somewhere under the
	// pak's mount root. The exact location varies by UE version and cook
	// settings; try a small set of common candidates before giving up.
	TArray<FString> Candidates;
	Candidates.Add(MountRoot + TEXT("AssetRegistry.bin"));
	// Under "<MountRoot>/<DLCPluginName>/AssetRegistry.bin" when cooked as a
	// plugin DLC (we don't know the plugin name here, so try a generic find
	// against everything one level down).
	{
		const FString PaksDir = FPaths::ProjectContentDir() / TEXT("Paks");
		TArray<FString> AllPaks;
		IFileManager::Get().FindFilesRecursive(AllPaks, *PaksDir, TEXT("*.pak"), true, false);
		IPlatformFile& LowerLevel = FPlatformFileManager::Get().GetPlatformFile();
		for (const FString& PakFilename : AllPaks)
		{
			TRefCountPtr<FPakFile> PakFile(new FPakFile(&LowerLevel, *PakFilename, /*bIsSigned=*/ false));
			if (!PakFile.IsValid() || !PakFile->IsValid())
			{
				continue;
			}
			const FString PakMount = PakFile->GetMountPoint();
			// Only inspect paks whose mount starts with this foreign project
			if (!PakMount.Contains(ForeignProjectName))
			{
				continue;
			}
			// PakFile exposes an iterator over the DirectoryIndex - walk it and
			// keep any entry that ends with AssetRegistry.bin. FFilenameIterator
			// only yields entries whose filename is actually present (i.e. the
			// pak shipped with an unpruned DirectoryIndex, which DLC cooks do).
			for (FPakFile::FFilenameIterator It(*PakFile, /*bIncludeDeleted=*/ false); It; ++It)
			{
				const FString& Entry = It.Filename();
				if (Entry.EndsWith(TEXT("AssetRegistry.bin"), ESearchCase::IgnoreCase))
				{
					Candidates.AddUnique(PakMount / Entry);
				}
			}
		}
	}

	for (const FString& CandidatePath : Candidates)
	{
		TArray<uint8> Buffer;
		if (!FFileHelper::LoadFileToArray(Buffer, *CandidatePath))
		{
			continue;
		}

		UE_LOG(LogRealGazebo, Log,
			TEXT("RealGazebo Module: found DLC AssetRegistry chunk at '%s' (%d bytes)"),
			*CandidatePath, Buffer.Num());

		FMemoryReader Reader(Buffer);
		FAssetRegistryState DLCState;
		FAssetRegistrySerializationOptions Options;
		// Match the options used at cook time. Cooked DLC registry is the
		// runtime form, not the development form, so do not call ModifyForDevelopment.
		if (DLCState.Serialize(Reader, Options))
		{
			AssetRegistry.AppendState(DLCState);
			UE_LOG(LogRealGazebo, Log,
				TEXT("RealGazebo Module: appended DLC AssetRegistry state for mount root '%s'"),
				*MountRoot);

			// Even with assets visible to AR + IoStore feeding payload bytes,
			// the cooked material shader maps reference hashes that live in a
			// per-plugin shader library. Without that library open the engine
			// falls back to the default material and the mod vehicle ends up
			// without textures. Register each plugin's mount root and open
			// its shader library to fix that.
			RegisterDlcPluginMountsAndShaders(DLCState, ForeignProjectName);
			return;
		}

		UE_LOG(LogRealGazebo, Warning,
			TEXT("RealGazebo Module: failed to deserialize DLC AssetRegistry at '%s'"),
			*CandidatePath);
	}

	UE_LOG(LogRealGazebo, Warning,
		TEXT("RealGazebo Module: no DLC AssetRegistry chunk found for mount root '%s' - mod assets may remain invisible to AssetRegistry"),
		*MountRoot);
}

void FRealGazeboModule::RegisterDlcPluginMountsAndShaders(FAssetRegistryState& DLCState, const FString& ForeignProjectName)
{
	// Step 1: scrape every distinct top-level mount root out of the package
	// names we just merged. A package path like "/VehicleMod/Models/Foo"
	// implies a plugin mount root "/VehicleMod/".
	TSet<FString> PluginNames;
	DLCState.EnumerateAllAssets([&PluginNames](const FAssetData& AssetData)
	{
		const FString PackageName = AssetData.PackageName.ToString();
		if (PackageName.Len() < 2 || PackageName[0] != TEXT('/'))
		{
			return;
		}
		int32 SecondSlash = INDEX_NONE;
		if (!PackageName.Mid(1).FindChar(TEXT('/'), SecondSlash))
		{
			return;
		}
		// SecondSlash is relative to Mid(1), so the plugin name spans
		// indices [1 .. 1 + SecondSlash) of the original PackageName.
		FString PluginName = PackageName.Mid(1, SecondSlash);
		if (!PluginName.IsEmpty())
		{
			PluginNames.Add(MoveTemp(PluginName));
		}
	});

	for (const FString& PluginName : PluginNames)
	{
		// Skip namespaces the engine already owns or that we registered as
		// the foreign project root. /Game/ and /Engine/ should never appear
		// in a DLC chunk, but guard anyway.
		if (PluginName.Equals(TEXT("Engine"), ESearchCase::IgnoreCase)
			|| PluginName.Equals(TEXT("Game"), ESearchCase::IgnoreCase)
			|| PluginName.Equals(ForeignProjectName, ESearchCase::IgnoreCase))
		{
			continue;
		}

		const FString PluginMountRoot = FString::Printf(TEXT("/%s/"), *PluginName);
		const FString PluginContentDir = FString::Printf(
			TEXT("../../../%s/Plugins/%s/Content/"), *ForeignProjectName, *PluginName);

		// Step 2: register the plugin's own mount root if no one has yet.
		// Most asset lookups already work via IoStore content ids, but
		// FPackageName::GetLocalFullPath and a few fallback paths need this.
		if (!FPackageName::MountPointExists(PluginMountRoot))
		{
			FPackageName::RegisterMountPoint(PluginMountRoot, PluginContentDir);
			UE_LOG(LogRealGazebo, Log,
				TEXT("RealGazebo Module: registered DLC plugin mount root '%s' -> '%s'"),
				*PluginMountRoot, *PluginContentDir);
		}

		// Step 3: open the plugin's shader code library. UE auto-opens these
		// for plugins enumerated by FPluginManager; this DLC was cooked in a
		// foreign project so its plugin is invisible here and the auto-open
		// never happens, leaving material shader maps unresolvable.
		if (FShaderCodeLibrary::OpenLibrary(PluginName, PluginContentDir, /*bMonolithicOnly=*/ true))
		{
			UE_LOG(LogRealGazebo, Log,
				TEXT("RealGazebo Module: opened DLC shader code library '%s' from '%s'"),
				*PluginName, *PluginContentDir);
		}
		else
		{
			UE_LOG(LogRealGazebo, Warning,
				TEXT("RealGazebo Module: failed to open DLC shader code library '%s' from '%s' - materials may fall back to default"),
				*PluginName, *PluginContentDir);
		}
	}
}

FRealGazeboModule& FRealGazeboModule::Get()
{
	return FModuleManager::LoadModuleChecked<FRealGazeboModule>("RealGazebo");
}

bool FRealGazeboModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("RealGazebo");
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRealGazeboModule, RealGazebo)
