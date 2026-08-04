#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Blueprint/UserWidget.h"

#include "XmlUISettings.generated.h"

UCLASS(config = XmlUI, defaultconfig)
class XMLUI_API UXmlUISettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UXmlUISettings();

    UPROPERTY(config, EditAnywhere, Category = "XmlUI")
    TSoftClassPtr<UUserWidget> BaseWidgetClass;

    UPROPERTY(config, EditAnywhere, Category = "XmlUI")
    FString XmlRootPath;

    UPROPERTY(config, EditAnywhere, Category = "XmlUI")
    FString BakedBlueprintOutputPath = TEXT("/Game/UI");

    /** Optional: DSL tag -> widget class path mapping (e.g. "Text" -> "/Script/SampleGame.SampleTextBlock").
        Passed to UXmlBuilder::BuildNode at bake time; tags without a mapping fall back to the plugin defaults. Project-agnostic: host adaptation is fully driven by this config. */
    UPROPERTY(config, EditAnywhere, Category = "XmlUI")
    TMap<FString, FString> WidgetClassMap;

    /** Maps Figma font family names to host font asset paths (e.g. "PingFang SC" -> "/Game/UI/Fonts/PingFang.PingFang"). */
    UPROPERTY(config, EditAnywhere, Category = "XmlUI")
    TMap<FString, FString> FontFamilyMap;
};
