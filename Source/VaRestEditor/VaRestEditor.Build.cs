// Copyright 2024/2025 Vladimir Alyamkin, Mauro Leoci. All Rights Reserved.

using UnrealBuildTool;

public class VaRestEditor : ModuleRules
{
	public VaRestEditor(ReadOnlyTargetRules Target) : base(Target)
	{
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        DefaultBuildSettings = BuildSettingsVersion.V5;

		PrivateIncludePaths.AddRange(
			new string[] {
				"VaRestEditor/Private",
			});

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
                "VaRest"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                "InputCore",
                "AssetTools",
                "UnrealEd",     // for FAssetEditorManager
                "KismetWidgets",
                "KismetCompiler",
                "BlueprintGraph",
                "GraphEditor",
                "Kismet",       // for FWorkflowCentricApplication
                "PropertyEditor",
                "Sequencer",
                "DetailCustomizations",
                "Settings",
                "RenderCore",
                "EditorFramework"
			});

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			});
	}
}
