// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Ecoscape : ModuleRules
{
	public Ecoscape(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(["Core", "CoreUObject", "Engine", "InputCore", "PhysicsCore", "UMG", "AIModule", "GameplayTasks"]);
		PrivateDependencyModuleNames.AddRange(["ProceduralMeshComponent", "GeometryFramework", "GeometryScriptingCore", "NavigationSystem", "VorbisAudioDecoder"]);
		
		if (Target.bBuildEditor) 
		{
			PublicDependencyModuleNames.Add("UnrealEd");
			PrivateDependencyModuleNames.Add("MessageLog");
		}
	}
}
