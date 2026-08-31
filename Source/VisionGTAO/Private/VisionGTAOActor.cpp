// Copyright (c) 2026 Darlington Group LLC. Licensed under the MIT License.

#include "VisionGTAOActor.h"

#include "Components/BillboardComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/Package.h"

namespace
{
	void SetVisionGTAOInt(const TCHAR* Name, const int32 Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByGameSetting);
		}
	}

	void SetVisionGTAOFloat(const TCHAR* Name, const float Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByGameSetting);
		}
	}
}

AVisionGTAOActor::AVisionGTAOActor()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* const SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

#if WITH_EDITORONLY_DATA
	SpriteComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	SpriteComponent->SetupAttachment(RootComponent);

	if (!IsRunningCommandlet() && SpriteComponent)
	{
		struct FConstructorStatics
		{
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> PluginSprite;
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> EngineSprite;
			FName Category;
			FText DisplayName;

			FConstructorStatics()
				: PluginSprite(TEXT("/VisionGTAO/EditorResources/T_ICO_XeGTAO"))
				, EngineSprite(TEXT("/Engine/EditorResources/S_Actor"))
				, Category(TEXT("VisionGTAO"))
				, DisplayName(NSLOCTEXT("SpriteCategory", "VisionGTAO", "Vision GTAO"))
			{
			}
		};
		static FConstructorStatics ConstructorStatics;

		UTexture2D* const SpriteTexture = ConstructorStatics.PluginSprite.Get() ? ConstructorStatics.PluginSprite.Get() : ConstructorStatics.EngineSprite.Get();
		SpriteComponent->Sprite = SpriteTexture;
		SpriteComponent->SpriteInfo.Category = ConstructorStatics.Category;
		SpriteComponent->SpriteInfo.DisplayName = ConstructorStatics.DisplayName;
		SpriteComponent->bIsScreenSizeScaled = true;
	}

	bIsSpatiallyLoaded = false;
#endif
}

void AVisionGTAOActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplySettings();
}

void AVisionGTAOActor::BeginPlay()
{
	Super::BeginPlay();
	ApplySettings();
}

void AVisionGTAOActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (!IsPlayInEditorWorld())
	{
		SetEnabled(false);
	}

	Super::EndPlay(EndPlayReason);
}

void AVisionGTAOActor::Destroyed()
{
	if (!IsPlayInEditorWorld())
	{
		SetEnabled(false);
	}

	Super::Destroyed();
}

#if WITH_EDITOR
void AVisionGTAOActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	ApplySettings();
}
#endif

void AVisionGTAOActor::ApplySettings() const
{
	SetVisionGTAOInt(TEXT("r.VisionGTAO.UseEngineHook"), static_cast<int32>(IntegrationMode));
	SetEnabled(bEnabled);
	SetVisionGTAOFloat(TEXT("r.VisionGTAO.Radius"), FMath::Max(Radius, 0.0f));
	SetVisionGTAOFloat(TEXT("r.VisionGTAO.ThinOccluderCompensation"), FMath::Clamp(ThinOccluderCompensation, 0.0f, 4.0f));
	SetVisionGTAOFloat(TEXT("r.VisionGTAO.Intensity"), FMath::Clamp(Intensity, 0.0f, 4.0f));
	SetVisionGTAOFloat(TEXT("r.VisionGTAO.Contrast"), FMath::Clamp(Contrast, 0.01f, 4.0f));
	SetVisionGTAOFloat(TEXT("r.VisionGTAO.MinimumVisibility"), FMath::Clamp(MinimumVisibility, 0.0f, 1.0f));
	SetVisionGTAOFloat(TEXT("r.VisionGTAO.FadeOutDistance"), FMath::Max(FadeOutDistance, 0.0f));
	SetVisionGTAOFloat(TEXT("r.VisionGTAO.FadeOutRadius"), FMath::Max(FadeOutRadius, 1.0f));
	SetVisionGTAOInt(TEXT("r.VisionGTAO.Quality"), static_cast<int32>(Quality));
	SetVisionGTAOInt(TEXT("r.VisionGTAO.Denoise"), bDenoise ? 1 : 0);
	SetVisionGTAOFloat(TEXT("r.VisionGTAO.LuminanceInfluence"), FMath::Clamp(LuminanceInfluence, 0.0f, 1.0f));
	SetVisionGTAOInt(TEXT("r.VisionGTAO.Color.Enable"), bColoredPenumbra ? 1 : 0);
	SetVisionGTAOFloat(TEXT("r.VisionGTAO.Color.TintR"), FMath::Max(PenumbraColor.R, 0.0f));
	SetVisionGTAOFloat(TEXT("r.VisionGTAO.Color.TintG"), FMath::Max(PenumbraColor.G, 0.0f));
	SetVisionGTAOFloat(TEXT("r.VisionGTAO.Color.TintB"), FMath::Max(PenumbraColor.B, 0.0f));
	SetVisionGTAOFloat(TEXT("r.VisionGTAO.Color.Saturation"), FMath::Max(PenumbraSaturation, 0.0f));
	SetVisionGTAOFloat(TEXT("r.VisionGTAO.Color.Intensity"), FMath::Max(PenumbraIntensity, 0.0f));
}

bool AVisionGTAOActor::IsPlayInEditorWorld() const
{
	const UWorld* World = GetWorld();
	return (World && World->WorldType == EWorldType::PIE) || GetPackage()->HasAnyPackageFlags(PKG_PlayInEditor);
}

void AVisionGTAOActor::SetEnabled(bool bValue) const
{
	SetVisionGTAOInt(TEXT("r.VisionGTAO.Enable"), bValue ? 1 : 0);
}
