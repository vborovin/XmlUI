#pragma once

#include "CoreMinimal.h"
#include "XmlDslNode.h"

#include "XmlBuilder.generated.h"

class UPanelSlot;
class UUserWidget;
class UWidget;
class UWidgetTree;
struct FSlateBrush;

UCLASS(BlueprintType)
class XMLUI_API UXmlBuilder : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "XmlUI")
    static UWidget* BuildFromString(UUserWidget* Owner, const FString& XmlContent, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "XmlUI")
    static UWidget* BuildFromFile(UUserWidget* Owner, const FString& FilePath, FString& OutError);

    static UWidget* BuildNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError);

private:
    static UWidget* BuildNodeInternal(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError);

    static void ApplyCommonAttributes(UWidget* Widget, const FXmlNodeDesc& Node);

    static void ApplySlotAttributes(UPanelSlot* Slot, const FXmlNodeDesc& Node);

    static void ParseBrushFromString(FSlateBrush& OutBrush, const FString& Value);
};
