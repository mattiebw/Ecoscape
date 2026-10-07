// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class EcoscapeTarget : TargetRules
{
	public EcoscapeTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		ExtraModuleNames.AddRange( new string[] { "Ecoscape" } );
		if (bBuildEditor)
			ExtraModuleNames.Add("EcoscapeEditor");
	}
}
