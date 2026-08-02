#pragma once

#include "CoreMinimal.h"
#include "Containers/Map.h"
#include "XmlDslNode.h"

#include "XmlBuilder.generated.h"

class UPanelSlot;
class UUniformGridPanel;
class UUserWidget;
class UWidget;
class UWidgetTree;
class UWrapBox;
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

    static UWidget* BuildNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap = nullptr);

private:
    static UWidget* BuildNodeInternal(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap);

    static void ConfigureWrapBoxWidget(UWrapBox* InWrapBox, const FXmlNodeDesc& InNode, UWidgetTree* InTree, FString& OutError, const TMap<FString, FString>* InWidgetClassMap);

    static void ConfigureGridWidget(UUniformGridPanel* InGrid, const FXmlNodeDesc& InNode, UWidgetTree* InTree, FString& OutError, const TMap<FString, FString>* InWidgetClassMap);

    static void ApplyCommonAttributes(UWidget* Widget, const FXmlNodeDesc& Node);

    static void ApplySlotAttributes(UPanelSlot* Slot, const FXmlNodeDesc& Node);

    static void ParseBrushFromString(FSlateBrush& OutBrush, const FString& Value);
};
