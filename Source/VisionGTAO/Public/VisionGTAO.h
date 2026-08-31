// Copyright (c) 2026 Darlington Group LLC. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FVisionGTAOViewExtension;

// This owns the shader path and hooks the plugin into the renderer.
class FVisionGTAOModule : public IModuleInterface
{
	TSharedPtr<FVisionGTAOViewExtension, ESPMode::ThreadSafe> ViewExtension;
	FDelegateHandle PostEngineInitHandle;
	FDelegateHandle PostOpaqueRenderHandle;

public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void StartRendering();
};
