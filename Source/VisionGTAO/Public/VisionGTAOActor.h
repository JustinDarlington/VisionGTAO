// Copyright (c) 2026 Darlington Group LLC. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VisionGTAOActor.generated.h"

class UBillboardComponent;

UENUM(BlueprintType)
enum class EVisionGTAOIntegrationMode : uint8
{
	PostOpaque UMETA(DisplayName = "Stock Engine (Post Opaque)"),
	EngineHook UMETA(DisplayName = "Engine Hook")
};

UENUM(BlueprintType)
enum class EVisionGTAOQuality : uint8
{
	Low,
	Medium,
	High,
	Cinematic
};

// This actor controls Vision GTAO for every rendered view.
UCLASS(Blueprintable, ClassGroup = Rendering, meta = (DisplayName = "Vision GTAO"))
class VISIONGTAO_API AVisionGTAOActor : public AActor
{
	GENERATED_BODY()

public:
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> SpriteComponent;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision GTAO")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision GTAO", meta = (ToolTip = "The stock mode works without engine changes. The engine hook requires the included Unreal Engine patch."))
	EVisionGTAOIntegrationMode IntegrationMode = EVisionGTAOIntegrationMode::PostOpaque;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision GTAO", meta = (ClampMin = "0.0", Units = "cm"))
	float Radius = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision GTAO", meta = (ClampMin = "0.0", ClampMax = "4.0", ToolTip = "Reduces artifacts from masked screens and detached foreground surfaces. 0 keeps the original behavior."))
	float ThinOccluderCompensation = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision GTAO", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float Intensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision GTAO", meta = (ClampMin = "0.01", ClampMax = "4.0"))
	float Contrast = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision GTAO", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinimumVisibility = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision GTAO", meta = (ClampMin = "0.0", Units = "cm"))
	float FadeOutDistance = 8000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision GTAO", meta = (ClampMin = "1.0", Units = "cm"))
	float FadeOutRadius = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision GTAO")
	EVisionGTAOQuality Quality = EVisionGTAOQuality::High;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision GTAO")
	bool bDenoise = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision GTAO", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LuminanceInfluence = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colored Penumbra")
	bool bColoredPenumbra = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colored Penumbra")
	FLinearColor PenumbraColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colored Penumbra", meta = (ClampMin = "0.0"))
	float PenumbraSaturation = 3.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colored Penumbra", meta = (ClampMin = "0.0"))
	float PenumbraIntensity = 1.0f;

public:
	AVisionGTAOActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Destroyed() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION(BlueprintCallable, Category = "Vision GTAO")
	void ApplySettings() const;

private:
	bool IsPlayInEditorWorld() const;
	void SetEnabled(bool bValue) const;
};
