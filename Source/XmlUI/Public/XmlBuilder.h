#pragma once

#include "CoreMinimal.h"
#include "Containers/Map.h"
#include "XmlDslNode.h"

#include "XmlBuilder.generated.h"

class UBorderSlot;
class UCanvasPanelSlot;
class UHorizontalBoxSlot;
class UOverlaySlot;
class UPanelSlot;
class UPanelWidget;
class UScrollBoxSlot;
class UUniformGridPanel;
class UUniformGridSlot;
class UUserWidget;
class UVerticalBoxSlot;
class UWidget;
class UWidgetTree;
class UWrapBox;
class UWrapBoxSlot;
class UXmlPanelSlot;
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

    static UWidget* BuildPanelNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap);

    static UWidget* BuildTextNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap);

    static UWidget* BuildImageNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap);

    static UWidget* BuildSpacerNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap);

    static UWidget* BuildButtonNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap);

    static UWidget* BuildProgressBarNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap);

    static UWidget* BuildOverlayNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap);

    static UWidget* BuildScrollBoxNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap);

    static UWidget* BuildUserWidgetNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap);

    static UWidget* BuildSizeBoxNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap);

    static UWidget* BuildCanvasNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap);

    static UWidget* BuildMenuAnchorNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap);

    static UWidget* BuildBorderNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap);

    static void AddChildrenToPanel(UWidgetTree* Tree, UPanelWidget* Panel, const TArray<FXmlNodeDesc>& Children, FString& OutError, const TMap<FString, FString>* InWidgetClassMap);

    static void ConfigureWrapBoxWidget(UWrapBox* InWrapBox, const FXmlNodeDesc& InNode, UWidgetTree* InTree, FString& OutError, const TMap<FString, FString>* InWidgetClassMap);

    static void ConfigureGridWidget(UUniformGridPanel* InGrid, const FXmlNodeDesc& InNode, UWidgetTree* InTree, FString& OutError, const TMap<FString, FString>* InWidgetClassMap);

    static void ApplyCommonAttributes(UWidget* Widget, const FXmlNodeDesc& Node);

    static void ApplySlotAttributes(UPanelSlot* Slot, const FXmlNodeDesc& Node);

    static void ApplyXmlPanelSlotAttrs(UXmlPanelSlot* Slot, const FXmlNodeDesc& Node);

    static void ApplyOverlaySlotAttrs(UOverlaySlot* Slot, const FXmlNodeDesc& Node);

    static void ApplyWrapBoxSlotAttrs(UWrapBoxSlot* Slot, const FXmlNodeDesc& Node);

    static void ApplyUniformGridSlotAttrs(UUniformGridSlot* Slot, const FXmlNodeDesc& Node);

    static void ApplyCanvasSlotAttrs(UCanvasPanelSlot* Slot, const FXmlNodeDesc& Node);

    static void ApplyBorderSlotAttrs(UBorderSlot* Slot, const FXmlNodeDesc& Node);

    static void ParseBrushFromString(FSlateBrush& OutBrush, const FString& Value);
};
