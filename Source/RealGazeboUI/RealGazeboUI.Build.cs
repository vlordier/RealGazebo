using UnrealBuildTool;
using System.IO;

public class RealGazeboUI : ModuleRules
{
	public RealGazeboUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Public include paths
        	PublicIncludePaths.AddRange(
            new string[] {
			Path.Combine(ModuleDirectory, "Public", "Core"),
			Path.Combine(ModuleDirectory, "Public", "Data"),
			Path.Combine(ModuleDirectory, "Public", "ViewerController"),
			Path.Combine(ModuleDirectory, "Public", "Widgets")
			}
		);

		// Core runtime dependencies
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"UMG",
			"Slate",
			"SlateCore",
			"RealGazeboBridge"
		});

		// Private implementation dependencies
		PrivateDependencyModuleNames.AddRange(new string[] {
			"ApplicationCore"
		});
		
		// Editor-specific dependencies
		if (Target.bBuildEditor)
		{
			PublicDependencyModuleNames.AddRange(new string[] {
				"UnrealEd",
				"ToolMenus"
			});
			
			PrivateDependencyModuleNames.AddRange(new string[] {
				"Projects"
			});
		}
		
		// No platform-specific dependencies needed for UI widgets
		
		DynamicallyLoadedModuleNames.AddRange(new string[] {
			// Reserved for future dynamic module loading
		});
		
		// Optimization settings
		OptimizeCode = CodeOptimization.InShippingBuildsOnly;
		
		// Define preprocessor macros
		PublicDefinitions.AddRange(new string[] {
			"REALGAZEBO_UI_MODULE=1"
		});
		
		if (Target.Configuration == UnrealTargetConfiguration.Debug ||
			Target.Configuration == UnrealTargetConfiguration.DebugGame)
		{
			PublicDefinitions.Add("REALGAZEBO_UI_DEBUG=1");
		}
	}
}