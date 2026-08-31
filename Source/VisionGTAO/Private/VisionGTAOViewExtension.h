// Copyright (c) 2026 Darlington Group LLC. Licensed under the MIT License.

#pragma once

#include "SceneViewExtension.h"

class FPostOpaqueRenderParameters;

// This is where Vision GTAO hooks into either renderer path.
class VISIONGTAO_API FVisionGTAOViewExtension : public FSceneViewExtensionBase
{
	TAtomic<bool> bActive = true;

public:
	FVisionGTAOViewExtension(const FAutoRegister& AutoRegister);

	void Deactivate();

	virtual void PostRenderOpaqueLighting_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures);
	void RenderPostOpaque_RenderThread(FPostOpaqueRenderParameters& Parameters);

private:
	void Render_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures) const;

public:
	virtual int32 GetPriority() const override;

protected:
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
};
