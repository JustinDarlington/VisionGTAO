// Copyright (c) 2026 Darlington Group LLC. Licensed under the MIT License.

using UnrealBuildTool;
using System.IO;

// This is the runtime renderer module for the whole plugin.
public class VisionGTAO : ModuleRules
{
	public VisionGTAO(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.AddRange(
			new string[]
			{
				Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Private"),
				Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Internal")
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Projects",
				"RenderCore",
				"Renderer",
				"RHI"
			}
		);
	}
}
