// Copyright (c) 2026 Darlington Group LLC. Licensed under the MIT License.

#include "VisionGTAOViewExtension.h"

#include "Engine/World.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RendererInterface.h"
#include "SceneRendering.h"
#include "SceneTextures.h"
#include "ScreenPass.h"
#include "ShaderParameterStruct.h"
#include "Substrate/Substrate.h"

// I used ScreenSpaceFogScattering as a reference for the renderer wiring, then kept the GTAO pass separate.
DECLARE_GPU_STAT_NAMED(VisionGTAO, TEXT("Vision GTAO"));

static TAutoConsoleVariable<int32> CVarVisionGTAOEnable(
	TEXT("r.VisionGTAO.Enable"),
	1,
	TEXT("Enables Vision GTAO."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarVisionGTAORadius(
	TEXT("r.VisionGTAO.Radius"),
	32.0f,
	TEXT("World-space GTAO radius in centimeters."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarVisionGTAOThinOccluderCompensation(
	TEXT("r.VisionGTAO.ThinOccluderCompensation"),
	1.0f,
	TEXT("Reduces GTAO from detached foreground depth layers."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarVisionGTAOIntensity(
	TEXT("r.VisionGTAO.Intensity"),
	1.0f,
	TEXT("GTAO intensity from 0 to 4."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarVisionGTAOContrast(
	TEXT("r.VisionGTAO.Contrast"),
	1.0f,
	TEXT("Controls GTAO contrast."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarVisionGTAOMinimumVisibility(
	TEXT("r.VisionGTAO.MinimumVisibility"),
	0.0f,
	TEXT("Darkest visibility GTAO can output."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarVisionGTAOFadeOutDistance(
	TEXT("r.VisionGTAO.FadeOutDistance"),
	8000.0f,
	TEXT("Distance where GTAO is fully faded out in centimeters."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarVisionGTAOFadeOutRadius(
	TEXT("r.VisionGTAO.FadeOutRadius"),
	2000.0f,
	TEXT("Distance used to fade GTAO out in centimeters."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarVisionGTAOQuality(
	TEXT("r.VisionGTAO.Quality"),
	2,
	TEXT("GTAO quality from 0 to 3. 2 is all you really need per my observations. 3 just seems like a computational waste really."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarVisionGTAODenoise(
	TEXT("r.VisionGTAO.Denoise"),
	1,
	TEXT("Enables the XeGTAO edge-aware filter."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarVisionGTAOLuminanceInfluence(
	TEXT("r.VisionGTAO.LuminanceInfluence"),
	1.0f,
	TEXT("Reduces GTAO in dark scene color. 0 disables it and 1 fully applies it."),
	ECVF_RenderThreadSafe
);

// Keep the stock hook on by default so this still works for people without a custom engine build.
static TAutoConsoleVariable<int32> CVarVisionGTAOUseEngineHook(
	TEXT("r.VisionGTAO.UseEngineHook"),
	0,
	TEXT("Uses the optional engine hook instead of Unreal's post opaque renderer delegate. The engine hook requires the included Unreal Engine patch."),
	ECVF_RenderThreadSafe
);

// ----------------------------------------------------------------------------------------------------
// Color penumbra settings

// I split this into three CVars so the actor can drive the color without any weird parsing.

static TAutoConsoleVariable<int32> CVarVisionGTAOColorEnable(
	TEXT("r.VisionGTAO.Color.Enable"),
	0,
	TEXT("Enables the colored AO penumbra."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarVisionGTAOColorTintR(
	TEXT("r.VisionGTAO.Color.TintR"),
	1.0f,
	TEXT("Red channel of the AO penumbra tint."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarVisionGTAOColorTintG(
	TEXT("r.VisionGTAO.Color.TintG"),
	1.0f,
	TEXT("Green channel of the AO penumbra tint."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarVisionGTAOColorTintB(
	TEXT("r.VisionGTAO.Color.TintB"),
	1.0f,
	TEXT("Blue channel of the AO penumbra tint."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarVisionGTAOColorSaturation(
	TEXT("r.VisionGTAO.Color.Saturation"),
	3.15f,
	TEXT("Colored AO saturation."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarVisionGTAOColorIntensity(
	TEXT("r.VisionGTAO.Color.Intensity"),
	1.0f,
	TEXT("Colored penumbra intensity."),
	ECVF_RenderThreadSafe
);

// We need a local depth texture because this pass only cares about the current view.
class FVisionGTAOPrepareDepthCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FVisionGTAOPrepareDepthCS);
	SHADER_USE_PARAMETER_STRUCT(FVisionGTAOPrepareDepthCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
		SHADER_PARAMETER(FIntPoint, ViewRectMin)
		SHADER_PARAMETER(FIntPoint, ViewRectSize)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputDepthTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("COMPUTE_SHADER"), 1);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 8);
		OutEnvironment.SetDefine(TEXT("VISION_GTAO_PREPARE_DEPTH"), 1);
	}
};

// This is the actual XeGTAO pass.
class FVisionGTAOCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FVisionGTAOCS);
	SHADER_USE_PARAMETER_STRUCT(FVisionGTAOCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)
		SHADER_PARAMETER(FIntPoint, ViewRectMin)
		SHADER_PARAMETER(FIntPoint, ViewRectSize)
		SHADER_PARAMETER(float, Radius)
		SHADER_PARAMETER(float, ThinOccluderCompensation)
		SHADER_PARAMETER(int32, Quality)
		SHADER_PARAMETER(uint32, NoiseIndex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, WorkingDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputEdges)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("COMPUTE_SHADER"), 1);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 8);
		OutEnvironment.SetDefine(TEXT("VISION_GTAO_MAIN"), 1);
	}
};

// Clean up the raw AO without wiping out the small details.
class FVisionGTAODenoiseCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FVisionGTAODenoiseCS);
	SHADER_USE_PARAMETER_STRUCT(FVisionGTAODenoiseCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, ViewRectSize)
		SHADER_PARAMETER(float, DenoiseBlurBeta)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InputTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InputEdges)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("COMPUTE_SHADER"), 1);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 8);
		OutEnvironment.SetDefine(TEXT("VISION_GTAO_DENOISE"), 1);
	}
};

// This is where the regular controls and distance fade are applied.
class FVisionGTAOResolveCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FVisionGTAOResolveCS);
	SHADER_USE_PARAMETER_STRUCT(FVisionGTAOResolveCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, ViewRectMin)
		SHADER_PARAMETER(FIntPoint, ViewRectSize)
		SHADER_PARAMETER(float, Intensity)
		SHADER_PARAMETER(float, Contrast)
		SHADER_PARAMETER(float, MinimumVisibility)
		SHADER_PARAMETER(float, FadeOutDistance)
		SHADER_PARAMETER(float, FadeOutRadius)
		SHADER_PARAMETER(float, LuminanceInfluence)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, WorkingDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InputTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("COMPUTE_SHADER"), 1);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 8);
		OutEnvironment.SetDefine(TEXT("VISION_GTAO_RESOLVE"), 1);
	}
};

// These are the scene color formats I've tested this pass with.
enum class EVisionGTAOSceneColorFormat : int32
{
	R8G8B8A8,
	A2B10G10R10,
	FloatR11G11B10,
	FloatRGB,
	FloatRGBA,
	A32B32G32R32F,
	Count
};

// D3D12 will reject the PSO if this does not match the real scene color format.
class FVisionGTAOSceneColorFormatDimension : SHADER_PERMUTATION_INT("VISION_GTAO_SCENE_COLOR_FORMAT", static_cast<int32>(EVisionGTAOSceneColorFormat::Count));

static EVisionGTAOSceneColorFormat GetVisionGTAOSceneColorFormat(EPixelFormat PixelFormat)
{
	switch (PixelFormat)
	{
	case PF_R8G8B8A8:       return EVisionGTAOSceneColorFormat::R8G8B8A8;
	case PF_A2B10G10R10:    return EVisionGTAOSceneColorFormat::A2B10G10R10;
	case PF_FloatR11G11B10: return EVisionGTAOSceneColorFormat::FloatR11G11B10;
	case PF_FloatRGB:       return EVisionGTAOSceneColorFormat::FloatRGB;
	case PF_FloatRGBA:      return EVisionGTAOSceneColorFormat::FloatRGBA;
	case PF_A32B32G32R32F:  return EVisionGTAOSceneColorFormat::A32B32G32R32F;
	default:                return EVisionGTAOSceneColorFormat::Count;
	}
}

static EPixelFormat GetVisionGTAOSceneColorPixelFormat(EVisionGTAOSceneColorFormat SceneColorFormat)
{
	switch (SceneColorFormat)
	{
	case EVisionGTAOSceneColorFormat::R8G8B8A8:       return PF_R8G8B8A8;
	case EVisionGTAOSceneColorFormat::A2B10G10R10:    return PF_A2B10G10R10;
	case EVisionGTAOSceneColorFormat::FloatR11G11B10: return PF_FloatR11G11B10;
	case EVisionGTAOSceneColorFormat::FloatRGB:       return PF_FloatRGB;
	case EVisionGTAOSceneColorFormat::FloatRGBA:      return PF_FloatRGBA;
	case EVisionGTAOSceneColorFormat::A32B32G32R32F:  return PF_A32B32G32R32F;
	default:                                          return PF_Unknown;
	}
}

// Multiply the finished AO into the already lit scene color.
class FVisionGTAOCompositePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FVisionGTAOCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FVisionGTAOCompositePS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<FVisionGTAOSceneColorFormatDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, ViewRectMin)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InputTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputSceneColor)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		OutEnvironment.SetRenderTargetOutputFormat(0, GetVisionGTAOSceneColorPixelFormat(static_cast<EVisionGTAOSceneColorFormat>(PermutationVector.Get<FVisionGTAOSceneColorFormatDimension>())));
	}
};

// Apply the colored penumbra before the regular AO multiplication.
class FVisionGTAOColorPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FVisionGTAOColorPS);
	SHADER_USE_PARAMETER_STRUCT(FVisionGTAOColorPS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<FVisionGTAOSceneColorFormatDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, Saturation)
		SHADER_PARAMETER(float, Intensity)
		SHADER_PARAMETER(FVector3f, TintColor)
		SHADER_PARAMETER(FIntPoint, ViewRectMin)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InputAO)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputSceneColor)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		OutEnvironment.SetRenderTargetOutputFormat(0, GetVisionGTAOSceneColorPixelFormat(static_cast<EVisionGTAOSceneColorFormat>(PermutationVector.Get<FVisionGTAOSceneColorFormatDimension>())));
	}
};

IMPLEMENT_GLOBAL_SHADER(FVisionGTAOPrepareDepthCS, "/Plugin/VisionGTAO/Private/VisionGTAO.usf"     , "PrepareDepthCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVisionGTAOCS            , "/Plugin/VisionGTAO/Private/VisionGTAO.usf"     , "MainCS"        , SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVisionGTAODenoiseCS     , "/Plugin/VisionGTAO/Private/VisionGTAO.usf"     , "DenoiseCS"     , SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVisionGTAOResolveCS     , "/Plugin/VisionGTAO/Private/VisionGTAO.usf"     , "ResolveCS"     , SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVisionGTAOCompositePS   , "/Plugin/VisionGTAO/Private/VisionGTAOApply.usf", "CompositePS"   , SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FVisionGTAOColorPS       , "/Plugin/VisionGTAO/Private/VisionGTAOColor.usf", "ColorizePS"    , SF_Pixel);

FVisionGTAOViewExtension::FVisionGTAOViewExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister)
{
}

void FVisionGTAOViewExtension::Deactivate()
{
	bActive.Store(false);
}

void FVisionGTAOViewExtension::PostRenderOpaqueLighting_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)
{
	if (CVarVisionGTAOUseEngineHook.GetValueOnRenderThread() == 0)
	{
		return;
	}

	Render_RenderThread(GraphBuilder, InView, SceneTextures);
}

void FVisionGTAOViewExtension::RenderPostOpaque_RenderThread(FPostOpaqueRenderParameters& Parameters)
{
	if (CVarVisionGTAOUseEngineHook.GetValueOnRenderThread() != 0 || !Parameters.GraphBuilder || !Parameters.View || !Parameters.SceneTexturesUniformParams)
	{
		return;
	}

	Render_RenderThread(*Parameters.GraphBuilder, *Parameters.View, Parameters.SceneTexturesUniformParams);
}

void FVisionGTAOViewExtension::Render_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures) const
{
	check(IsInRenderingThread());

	if (!bActive.Load() || CVarVisionGTAOEnable.GetValueOnRenderThread() == 0 || !InView.Family || !InView.Family->EngineShowFlags.Lighting || InView.Family->EngineShowFlags.PathTracing || InView.Family->UseDebugViewPS())
	{
		return;
	}

	const FViewInfo& View = static_cast<const FViewInfo&>(InView);
	const FSceneTextures* ViewSceneTextures = View.GetSceneTexturesChecked();

	if (!ViewSceneTextures || ViewSceneTextures->Config.ShadingPath != EShadingPath::Deferred || !ViewSceneTextures->Depth.Target || !ViewSceneTextures->Color.Target || View.ViewRect.IsEmpty())
	{
		return;
	}

	const bool bUseSubstrate = Substrate::IsSubstrateEnabled() && !Substrate::IsSubstrateBlendableGBufferEnabled(View.GetShaderPlatform());

	if (!bUseSubstrate && !ViewSceneTextures->GBufferA)
	{
		return;
	}

	const FIntPoint SceneColorExtent = ViewSceneTextures->Color.Target->Desc.Extent;
	const FIntPoint SceneDepthExtent = ViewSceneTextures->Depth.Target->Desc.Extent;
	const FIntPoint NormalExtent = bUseSubstrate ? SceneColorExtent : ViewSceneTextures->GBufferA->Desc.Extent;

	if (View.ViewRect.Min.X < 0 || View.ViewRect.Min.Y < 0 || View.ViewRect.Max.X > SceneColorExtent.X || View.ViewRect.Max.Y > SceneColorExtent.Y
		|| View.ViewRect.Max.X > SceneDepthExtent.X || View.ViewRect.Max.Y > SceneDepthExtent.Y || View.ViewRect.Max.X > NormalExtent.X || View.ViewRect.Max.Y > NormalExtent.Y)
	{
		return;
	}

	const float Radius = FMath::Max(CVarVisionGTAORadius.GetValueOnRenderThread(), 0.0f);
	const float ThinOccluderCompensation = FMath::Clamp(CVarVisionGTAOThinOccluderCompensation.GetValueOnRenderThread(), 0.0f, 4.0f);
	const float Intensity = FMath::Clamp(CVarVisionGTAOIntensity.GetValueOnRenderThread(), 0.0f, 4.0f);
	const float Contrast = FMath::Clamp(CVarVisionGTAOContrast.GetValueOnRenderThread(), 0.01f, 4.0f);
	const float MinimumVisibility = FMath::Clamp(CVarVisionGTAOMinimumVisibility.GetValueOnRenderThread(), 0.0f, 1.0f);
	const float FadeOutDistance = FMath::Max(CVarVisionGTAOFadeOutDistance.GetValueOnRenderThread(), 0.0f);

	if (Radius <= 0.0f || Intensity <= 0.0f || FadeOutDistance <= 0.0f)
	{
		return;
	}

	const FIntPoint ViewSize = View.ViewRect.Size();
	const float FadeOutRadius = FMath::Clamp(CVarVisionGTAOFadeOutRadius.GetValueOnRenderThread(), 1.0f, FadeOutDistance);
	const float LuminanceInfluence = FMath::Clamp(CVarVisionGTAOLuminanceInfluence.GetValueOnRenderThread(), 0.0f, 1.0f);
	const int32 Quality = FMath::Clamp(CVarVisionGTAOQuality.GetValueOnRenderThread(), 0, 3);
	const bool bDenoise = CVarVisionGTAODenoise.GetValueOnRenderThread() != 0;
	const uint32 NoiseIndex = View.CachedViewUniformShaderParameters && IsTemporalAccumulationBasedMethod(View.AntiAliasingMethod) && View.bAllowTemporalJitter && View.TemporalJitterSequenceLength > 1 ? View.CachedViewUniformShaderParameters->StateFrameIndex % 64u : 0u;
	const EVisionGTAOSceneColorFormat SceneColorFormat = GetVisionGTAOSceneColorFormat(ViewSceneTextures->Color.Target->Desc.Format);
	TRDGUniformBufferRef<FSubstrateGlobalUniformParameters> SubstrateParameters = View.SubstrateViewData.SubstrateGlobalUniformParameters;

	if (SceneColorFormat == EVisionGTAOSceneColorFormat::Count)
	{
		return;
	}

	RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
	RDG_EVENT_SCOPE_STAT(GraphBuilder, VisionGTAO, "VisionGTAO %dx%d", ViewSize.X, ViewSize.Y);

	FRDGTextureDesc DepthDesc = FRDGTextureDesc::Create2D(ViewSize, PF_R32_FLOAT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef WorkingDepth = GraphBuilder.CreateTexture(DepthDesc, TEXT("VisionGTAO.Depth"));

	FVisionGTAOPrepareDepthCS::FParameters* PrepareDepthParameters = GraphBuilder.AllocParameters<FVisionGTAOPrepareDepthCS::FParameters>();
	PrepareDepthParameters->View = View.ViewUniformBuffer;
	PrepareDepthParameters->SceneTextures.SceneTextures = SceneTextures;
	PrepareDepthParameters->ViewRectMin        = View.ViewRect.Min;
	PrepareDepthParameters->ViewRectSize       = ViewSize;
	PrepareDepthParameters->OutputDepthTexture = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(WorkingDepth, 0));

	TShaderMapRef<FVisionGTAOPrepareDepthCS> PrepareDepthShader(View.ShaderMap);
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("PrepareDepth"), PrepareDepthShader, PrepareDepthParameters, FComputeShaderUtils::GetGroupCount(ViewSize, 8));

	FRDGTextureDesc GTAODesc = FRDGTextureDesc::Create2D(ViewSize, PF_R16F, FClearValueBinding::White, TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureDesc EdgeDesc = FRDGTextureDesc::Create2D(ViewSize, PF_R8, FClearValueBinding::White, TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef RawGTAO   = GraphBuilder.CreateTexture(GTAODesc, TEXT("VisionGTAO.Raw"));
	FRDGTextureRef GTAOEdges = GraphBuilder.CreateTexture(EdgeDesc, TEXT("VisionGTAO.Edges"));

	FVisionGTAOCS::FParameters* GTAOParameters = GraphBuilder.AllocParameters<FVisionGTAOCS::FParameters>();
	GTAOParameters->View = View.ViewUniformBuffer;
	GTAOParameters->SceneTextures.SceneTextures = SceneTextures;
	GTAOParameters->Substrate           = SubstrateParameters;
	GTAOParameters->ViewRectMin         = View.ViewRect.Min;
	GTAOParameters->ViewRectSize        = ViewSize;
	GTAOParameters->Radius              = Radius;
	GTAOParameters->ThinOccluderCompensation = ThinOccluderCompensation;
	GTAOParameters->Quality             = Quality;
	GTAOParameters->NoiseIndex          = NoiseIndex;
	GTAOParameters->WorkingDepthTexture = WorkingDepth;
	GTAOParameters->OutputTexture       = GraphBuilder.CreateUAV(RawGTAO);
	GTAOParameters->OutputEdges         = GraphBuilder.CreateUAV(GTAOEdges);

	TShaderMapRef<FVisionGTAOCS> GTAOShader(View.ShaderMap);
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Main"), GTAOShader, GTAOParameters, FComputeShaderUtils::GetGroupCount(ViewSize, 8));

	FRDGTextureRef FilteredGTAO = RawGTAO;

	if (bDenoise)
	{
		TShaderMapRef<FVisionGTAODenoiseCS> DenoiseShader(View.ShaderMap);

		for (int32 PassIndex = 0; PassIndex < 2; ++PassIndex)
		{
			FRDGTextureRef DenoisedGTAO = GraphBuilder.CreateTexture(GTAODesc, PassIndex == 0 ? TEXT("VisionGTAO.DenoisedInterim") : TEXT("VisionGTAO.Denoised"));
			FVisionGTAODenoiseCS::FParameters* DenoiseParameters = GraphBuilder.AllocParameters<FVisionGTAODenoiseCS::FParameters>();
			DenoiseParameters->ViewRectSize    = ViewSize;
			DenoiseParameters->DenoiseBlurBeta = PassIndex == 0 ? 1.2f / 5.0f : 1.2f;
			DenoiseParameters->InputTexture    = FilteredGTAO;
			DenoiseParameters->InputEdges      = GTAOEdges;
			DenoiseParameters->OutputTexture   = GraphBuilder.CreateUAV(DenoisedGTAO);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Denoise%d", PassIndex), DenoiseShader, DenoiseParameters, FComputeShaderUtils::GetGroupCount(ViewSize, 8));
			FilteredGTAO = DenoisedGTAO;
		}
	}

	FRDGTextureRef SceneColorCopy = GraphBuilder.CreateTexture(ViewSceneTextures->Color.Target->Desc, TEXT("VisionGTAO.SceneColorCopy"));
	AddCopyTexturePass(GraphBuilder, ViewSceneTextures->Color.Target, SceneColorCopy, View.ViewRect.Min, View.ViewRect.Min, ViewSize);
	FRDGTextureRef FinalGTAO = GraphBuilder.CreateTexture(GTAODesc, TEXT("VisionGTAO.Final"));

	FVisionGTAOResolveCS::FParameters* ResolveParameters = GraphBuilder.AllocParameters<FVisionGTAOResolveCS::FParameters>();
	ResolveParameters->ViewRectMin         = View.ViewRect.Min;
	ResolveParameters->ViewRectSize        = ViewSize;
	ResolveParameters->Intensity           = Intensity;
	ResolveParameters->Contrast            = Contrast;
	ResolveParameters->MinimumVisibility   = MinimumVisibility;
	ResolveParameters->FadeOutDistance     = FadeOutDistance;
	ResolveParameters->FadeOutRadius       = FadeOutRadius;
	ResolveParameters->LuminanceInfluence  = LuminanceInfluence;
	ResolveParameters->WorkingDepthTexture = WorkingDepth;
	ResolveParameters->InputTexture        = FilteredGTAO;
	ResolveParameters->SceneColorTexture   = SceneColorCopy;
	ResolveParameters->OutputTexture       = GraphBuilder.CreateUAV(FinalGTAO);

	TShaderMapRef<FVisionGTAOResolveCS> ResolveShader(View.ShaderMap);
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Resolve"), ResolveShader, ResolveParameters, FComputeShaderUtils::GetGroupCount(ViewSize, 8));

	FScreenPassTextureViewport Viewport(ViewSceneTextures->Color.Target, View.ViewRect);
	TShaderMapRef<FScreenPassVS> VertexShader(View.ShaderMap);
	FRDGTextureRef CompositeInputSceneColor = SceneColorCopy;

	if (CVarVisionGTAOColorEnable.GetValueOnRenderThread() != 0 && CVarVisionGTAOColorIntensity.GetValueOnRenderThread() > 0.0f)
	{
		FVisionGTAOColorPS::FParameters* ColorParameters = GraphBuilder.AllocParameters<FVisionGTAOColorPS::FParameters>();
		ColorParameters->Saturation       = FMath::Max(CVarVisionGTAOColorSaturation.GetValueOnRenderThread(), 0.0f);
		ColorParameters->Intensity        = FMath::Max(CVarVisionGTAOColorIntensity.GetValueOnRenderThread(), 0.0f);
		ColorParameters->TintColor        = FVector3f(CVarVisionGTAOColorTintR.GetValueOnRenderThread(), CVarVisionGTAOColorTintG.GetValueOnRenderThread(), CVarVisionGTAOColorTintB.GetValueOnRenderThread());
		ColorParameters->ViewRectMin      = View.ViewRect.Min;
		ColorParameters->InputAO          = FinalGTAO;
		ColorParameters->InputSceneColor  = SceneColorCopy;
		ColorParameters->RenderTargets[0] = FRenderTargetBinding(ViewSceneTextures->Color.Target, ERenderTargetLoadAction::ELoad);

		FVisionGTAOColorPS::FPermutationDomain ColorPermutationVector;
		ColorPermutationVector.Set<FVisionGTAOSceneColorFormatDimension>(static_cast<int32>(SceneColorFormat));
		TShaderMapRef<FVisionGTAOColorPS> ColorShader(View.ShaderMap, ColorPermutationVector);
		AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("ColoredPenumbra"), View, Viewport, Viewport, ColorShader, ColorParameters);

		CompositeInputSceneColor = GraphBuilder.CreateTexture(ViewSceneTextures->Color.Target->Desc, TEXT("VisionGTAO.ColoredSceneColorCopy"));
		AddCopyTexturePass(GraphBuilder, ViewSceneTextures->Color.Target, CompositeInputSceneColor, View.ViewRect.Min, View.ViewRect.Min, ViewSize);
	}

	FVisionGTAOCompositePS::FParameters* CompositeParameters = GraphBuilder.AllocParameters<FVisionGTAOCompositePS::FParameters>();
	CompositeParameters->ViewRectMin      = View.ViewRect.Min;
	CompositeParameters->InputTexture     = FinalGTAO;
	CompositeParameters->InputSceneColor  = CompositeInputSceneColor;
	CompositeParameters->RenderTargets[0] = FRenderTargetBinding(ViewSceneTextures->Color.Target, ERenderTargetLoadAction::ELoad);

	FVisionGTAOCompositePS::FPermutationDomain CompositePermutationVector;
	CompositePermutationVector.Set<FVisionGTAOSceneColorFormatDimension>(static_cast<int32>(SceneColorFormat));
	TShaderMapRef<FVisionGTAOCompositePS> CompositeShader(View.ShaderMap, CompositePermutationVector);
	AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("Composite"), View, Viewport, Viewport, VertexShader, CompositeShader, CompositeParameters);
}

int32 FVisionGTAOViewExtension::GetPriority() const
{
	return -100;
}

bool FVisionGTAOViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	const UWorld* World = Context.GetWorld();
	return bActive.Load() && World && !World->bIsTearingDown;
}
