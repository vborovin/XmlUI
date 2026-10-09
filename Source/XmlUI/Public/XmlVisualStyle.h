#pragma once

#include "CoreMinimal.h"

class UWidget;
struct FXmlNodeDesc;

/** Curated visual-property round-trip shared by validation, baking and runtime building. */
struct XMLUI_API FXmlVisualStyle
{
    static bool IsSupported(const FString& Tag, const FString& Property);
    static bool Apply(UWidget* Widget, const FXmlNodeDesc& Node, FString& OutError);
    static void Export(const UWidget* Widget, const FString& Tag, TArray<TPair<FString, FString>>& OutAttributes);
};
