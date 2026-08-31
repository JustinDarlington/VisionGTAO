// Copyright (c) 2026 Darlington Group LLC. Licensed under the MIT License.

#include "VisionGTAO.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "RenderingThread.h"
#include "RendererInterface.h"
#include "ShaderCore.h"
#include "VisionGTAOViewExtension.h"

IMPLEMENT_MODULE(FVisionGTAOModule, VisionGTAO)

void FVisionGTAOModule::StartupModule()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VisionGTAO"));
	checkf(Plugin.IsValid(), TEXT("VisionGTAO plugin information is unavailable."));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/VisionGTAO"), FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));

	if (GEngine)
	{
		StartRendering();
	}
	else
	{
		PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FVisionGTAOModule::StartRendering);
	}
}

void FVisionGTAOModule::ShutdownModule()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}

	if (PostOpaqueRenderHandle.IsValid() && FModuleManager::Get().IsModuleLoaded(TEXT("Renderer")))
	{
		IRendererModule& RendererModule = FModuleManager::GetModuleChecked<IRendererModule>(TEXT("Renderer"));
		RendererModule.RemovePostOpaqueRenderDelegate(PostOpaqueRenderHandle);
		PostOpaqueRenderHandle.Reset();
	}

	if (ViewExtension.IsValid())
	{
		ViewExtension->Deactivate();
		FlushRenderingCommands();
		ViewExtension.Reset();
	}
}

void FVisionGTAOModule::StartRendering()
{
	if (ViewExtension.IsValid())
	{
		return;
	}

	ViewExtension = FSceneViewExtensions::NewExtension<FVisionGTAOViewExtension>();
	IRendererModule& RendererModule = FModuleManager::LoadModuleChecked<IRendererModule>(TEXT("Renderer"));
	PostOpaqueRenderHandle = RendererModule.RegisterPostOpaqueRenderDelegate(FPostOpaqueRenderDelegate::CreateSP(ViewExtension.ToSharedRef(), &FVisionGTAOViewExtension::RenderPostOpaque_RenderThread));
}
