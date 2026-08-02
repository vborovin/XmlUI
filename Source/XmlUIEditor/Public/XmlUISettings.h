#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Blueprint/UserWidget.h"

#include "XmlUISettings.generated.h"

UCLASS(config = XmlUI, defaultconfig)
class UXmlUISettings : public UDeveloperSettings
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
};
