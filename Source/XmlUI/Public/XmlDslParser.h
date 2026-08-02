#pragma once

#include "CoreMinimal.h"
#include "Math/Color.h"
#include "Math/Vector2D.h"
#include "Layout/Margin.h"
#include "Types/SlateEnums.h"
#include "Framework/Text/TextLayout.h"
#include "Components/SlateWrapperTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "XmlDslNode.h"
#include "XmlDslParser.generated.h"

UCLASS(BlueprintType)
class XMLUI_API UXmlDslParser : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "XmlUI")
    static bool ParseXmlString(const FString& XmlContent, FXmlNodeDesc& OutRoot, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "XmlUI")
    static bool ParseXmlFile(const FString& FilePath, FXmlNodeDesc& OutRoot, FString& OutError);

    static bool ParseColor(const FString& In, FLinearColor& OutColor);

    static bool ParseMargin(const FString& In, FMargin& OutMargin);

    static bool ParseFloat(const FString& In, float& OutValue);

    static bool ParseInt(const FString& In, int32& OutValue);

    static bool ParseVector2D(const FString& In, FVector2D& OutValue);

    static bool ParseHAlign(const FString& In, EHorizontalAlignment& OutHAlign);

    static bool ParseVAlign(const FString& In, EVerticalAlignment& OutVAlign);

    static bool ParseJustification(const FString& In, ETextJustify::Type& OutJustification);

    static bool ParseSizeRule(const FString& In, ESlateSizeRule::Type& OutRule);
};
