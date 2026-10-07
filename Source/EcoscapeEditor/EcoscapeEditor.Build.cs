// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class EcoscapeEditor : ModuleRules
{
	public EcoscapeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(["Core", "CoreUObject", "Engine", "Ecoscape", "UnrealEd"]);
		PrivateDependencyModuleNames.AddRange(["Slate", "SlateCore", "LevelEditor", "UMG"]);
	}
}
