#pragma once
#include "CoreMinimal.h"

class UWidgetBlueprint;

class FXmlUIBaker
{
public:
    static void RegisterMenus();
    static void RunBakeFromDialog();
    static UWidgetBlueprint* BakeDslToWidgetBlueprint(const FString& DslFilePath, const FString& OutAssetPath, FString& OutError);
};
