/* One DSL node -> one runtime UWidget. BuildNodeInternal dispatches per tag; WidgetClassMap mapping is applied first. */
#include "XmlBuilder.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ContentWidget.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/SlateWrapperTypes.h"
#include "Components/Widget.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/MenuAnchor.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Engine/Blueprint.h"
#include "Engine/Texture2D.h"
#include "XmlWidgets/XmlButton.h"
#include "XmlDslParser.h"
#include "XmlDslValidator.h"
#include "XmlFontSizeUtil.h"
#include "XmlUISettings.h"
#include "XmlWidgets/XmlPanel.h"
#include "XmlWidgets/XmlWidget.h"
#include "Misc/FileHelper.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(XmlBuilder)

UWidget* UXmlBuilder::BuildFromString(UUserWidget* Owner, const FString& XmlContent, FString& OutError)
{
    OutError.Empty();

    if (!Owner || !Owner->WidgetTree)
    {
        OutError = TEXT("XmlUI: Owner or widget tree is null");
        return nullptr;
    }

    FXmlNodeDesc RootDesc;
    if (!UXmlDslParser::ParseXmlString(XmlContent, RootDesc, OutError))
    {
        return nullptr;
    }

    if (GetDefault<UXmlUISettings>()->bStrictValidation
        && !FXmlDslValidator::ValidateDocument(RootDesc, OutError))
    {
        return nullptr;
    }

    const UXmlUISettings* Settings = GetDefault<UXmlUISettings>();
    UWidget* Root = BuildNode(Owner->WidgetTree, RootDesc, OutError, &Settings->WidgetClassMap);
    if (!Root)
    {
        return nullptr;
    }

    if (Owner->WidgetTree->RootWidget == nullptr)
    {
        Owner->WidgetTree->RootWidget = Root;
    }
    else if (UPanelWidget* ExistingRoot = Cast<UPanelWidget>(Owner->WidgetTree->RootWidget))
    {
        ExistingRoot->AddChild(Root);
    }
    else
    {
        OutError = TEXT("XmlUI: WidgetTree root already occupied and not a panel");
        return nullptr;
    }

    return Root;
}

UWidget* UXmlBuilder::BuildFromFile(UUserWidget* Owner, const FString& FilePath, FString& OutError)
{
    OutError.Empty();

    FString XmlContent;
    if (!FFileHelper::LoadFileToString(XmlContent, *FilePath))
    {
        OutError = FString::Printf(TEXT("XmlUI: Failed to load file '%s'"), *FilePath);
        return nullptr;
    }

    return BuildFromString(Owner, XmlContent, OutError);
}

UWidget* UXmlBuilder::BuildNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap)
{
    return BuildNodeInternal(Tree, Node, OutError, InWidgetClassMap);
}

// bOutHasMapping: true when the tag has a configured mapping (even if creation failed).
// A mapped-but-invalid class reports an error and returns nullptr WITHOUT constructing the widget.
// InMappingTag overrides the lookup key; XmlUI is the root alias of Vertical and inherits its entry.
static UWidget* TryCreateMappedWidget(UWidgetTree* InTree, const FXmlNodeDesc& InNode, UClass* InExpectedClass, bool& bOutHasMapping, FString& OutError, const TMap<FString, FString>* InWidgetClassMap, const FString& InMappingTag = FString())
{
    bOutHasMapping = false;
    const FString& MappingTag = InMappingTag.IsEmpty() ? InNode.Tag : InMappingTag;

    const FString* ExplicitClassPath = InNode.Attributes.Find(TEXT("Class"));
    const bool bExplicitClass = ExplicitClassPath && !ExplicitClassPath->IsEmpty();
    const FString* ClassPath = bExplicitClass
        ? ExplicitClassPath
        : (InWidgetClassMap ? InWidgetClassMap->Find(MappingTag) : nullptr);

    if (!ClassPath || ClassPath->IsEmpty())
    {
        return nullptr;
    }

    bOutHasMapping = true;
    UClass* FoundClass = LoadClass<UWidget>(nullptr, **ClassPath);
    if (!FoundClass)
    {
        OutError += FString::Printf(
            TEXT("XmlUI: %s class '%s' for tag '%s' could not be loaded\n"),
            bExplicitClass ? TEXT("explicit") : TEXT("WidgetClassMap"),
            **ClassPath,
            *MappingTag);
        return nullptr;
    }
    if (!FoundClass->IsChildOf(InExpectedClass))
    {
        OutError += FString::Printf(
            TEXT("XmlUI: %s class '%s' for tag '%s' is not a %s subclass\n"),
            bExplicitClass ? TEXT("explicit") : TEXT("WidgetClassMap"),
            **ClassPath,
            *MappingTag,
            *InExpectedClass->GetName());
        return nullptr;
    }
    return InTree->ConstructWidget<UWidget>(FoundClass, FName(*InNode.Name));
}

// UE 5.5's UUserWidget has no SetWidgetClass: the object's class is fixed at construction,
// so the WBP class is resolved up front and passed to ConstructWidget.
static UClass* ResolveUserWidgetClass(const FString& InPath)
{
    // Class paths ("/Script/..." or a generated class ending in "_C") load directly.
    if (InPath.StartsWith(TEXT("/Script/")) || InPath.EndsWith(TEXT("_C")))
    {
        return LoadClass<UUserWidget>(nullptr, *InPath);
    }
    // Asset paths (e.g. /Game/UI/WBP_X) load the Blueprint and use its generated class.
    if (UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *InPath))
    {
        return Blueprint->GeneratedClass;
    }
    return nullptr;
}

// EStretch / EStretchDirection are UENUM namespace enums; match by name, case-insensitively
// (the exporter emits the lowercased enum name).
template<typename TEnum>
static bool ParseScaleEnum(const FString& In, TEnum& OutValue)
{
    const UEnum* Enum = StaticEnum<TEnum>();
    if (!Enum)
    {
        return false;
    }
    const FString Value = In.TrimStartAndEnd();
    const int32 ExactIndex = Enum->GetIndexByNameString(Value);
    if (ExactIndex != INDEX_NONE)
    {
        OutValue = static_cast<TEnum>(Enum->GetValueByIndex(ExactIndex));
        return true;
    }
    const FString LowerValue = Value.ToLower();
    for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
    {
        if (Enum->GetNameStringByIndex(Index).ToLower() == LowerValue)
        {
            OutValue = static_cast<TEnum>(Enum->GetValueByIndex(Index));
            return true;
        }
    }
    return false;
}

static void ApplyMappedText(FString& OutError, UWidget* InWidget, const FXmlNodeDesc& InNode)
{
    UTextBlock* TextBlock = Cast<UTextBlock>(InWidget);
    if (!TextBlock)
    {
        OutError += FString::Printf(TEXT("XmlUI: mapped Text class '%s' is not a UTextBlock subclass\n"), *InWidget->GetClass()->GetName());
        return;
    }
    if (const FString* TextValue = InNode.Attributes.Find(TEXT("Text")))
    {
        TextBlock->SetText(FText::FromString(*TextValue));
    }
    if (const FString* ColorValue = InNode.Attributes.Find(TEXT("Color")))
    {
        FLinearColor Color;
        if (UXmlDslParser::ParseColor(*ColorValue, Color))
        {
            TextBlock->SetColorAndOpacity(Color);
        }
    }
    if (const FString* JustValue = InNode.Attributes.Find(TEXT("Justification")))
    {
        ETextJustify::Type Just;
        if (UXmlDslParser::ParseJustification(*JustValue, Just))
        {
            TextBlock->SetJustification(Just);
        }
    }
    if (const FString* WrapValue = InNode.Attributes.Find(TEXT("WrapTextAt")))
    {
        float WrapAt = 0.f;
        if (UXmlDslParser::ParseFloat(*WrapValue, WrapAt) && WrapAt > 0.f)
        {
            TextBlock->SetWrapTextAt(WrapAt);
        }
    }
    if (const FString* ShadowColorValue = InNode.Attributes.Find(TEXT("ShadowColor")))
    {
        FLinearColor ShadowColor;
        if (UXmlDslParser::ParseColor(*ShadowColorValue, ShadowColor))
        {
            TextBlock->SetShadowColorAndOpacity(ShadowColor);
        }
    }
    if (const FString* ShadowOffsetValue = InNode.Attributes.Find(TEXT("ShadowOffset")))
    {
        FVector2D Offset;
        if (UXmlDslParser::ParseVector2D(*ShadowOffsetValue, Offset))
        {
            TextBlock->SetShadowOffset(Offset);
        }
    }
    FString FontFamily;
    if (const FString* FontFamilyValue = InNode.Attributes.Find(TEXT("FontFamily")))
    {
        FontFamily = *FontFamilyValue;
    }
    int32 FontSize = 16;
    bool bHasFontSize = false;
    if (const FString* FontSizeValue = InNode.Attributes.Find(TEXT("FontSize")))
    {
        if (UXmlDslParser::ParseInt(*FontSizeValue, FontSize) && FontSize > 0)
        {
            bHasFontSize = true;
        }
    }
    if (const FString* ArtFontSizeValue = InNode.Attributes.Find(TEXT("ArtFontSize")))
    {
        int32 ArtFontSize = -1;
        if (UXmlDslParser::ParseInt(*ArtFontSizeValue, ArtFontSize) && ArtFontSize > 0)
        {
            // Host widget (e.g. USampleTextBlock) Figma font-size field: write best-effort, ignore on failure
            if (FIntProperty* ArtFontProp = FindFProperty<FIntProperty>(InWidget->GetClass(), TEXT("ArtFont")))
            {
                ArtFontProp->SetPropertyValue_InContainer(InWidget, ArtFontSize);
            }
            else
            {
                FontSize = ArtFontSize; // fall back to the engine font size when there is no ArtFont field
                bHasFontSize = true;
            }
        }
    }
    if (bHasFontSize || !FontFamily.IsEmpty())
    {
        // FontFamily without FontSize must keep the mapped widget's configured default size, not force 16.
        const int32 EffectiveFontSize = bHasFontSize ? FontSize : TextBlock->GetFont().Size;
        TextBlock->SetFont(FSlateFontInfo(GetXmlFontByFamily(FontFamily), EffectiveFontSize));
    }
}

// Best-effort reflection for UserWidget nodes: applies the text attribute set when the
// referenced class exposes matching UPROPERTYs; missing properties are skipped.
static void ApplyReflectedTextAttrs(UWidget* InWidget, const FXmlNodeDesc& InNode)
{
    if (const FString* TextValue = InNode.Attributes.Find(TEXT("Text")))
    {
        if (FTextProperty* TextProp = FindFProperty<FTextProperty>(InWidget->GetClass(), TEXT("Text")))
        {
            TextProp->SetPropertyValue_InContainer(InWidget, FText::FromString(*TextValue));
        }
    }
    if (const FString* ColorValue = InNode.Attributes.Find(TEXT("Color")))
    {
        FLinearColor Color;
        if (UXmlDslParser::ParseColor(*ColorValue, Color))
        {
            if (FStructProperty* ColorProp = FindFProperty<FStructProperty>(InWidget->GetClass(), TEXT("TextColor")))
            {
                if (ColorProp->Struct->GetFName() == TEXT("SlateColor"))
                {
                    *ColorProp->ContainerPtrToValuePtr<FSlateColor>(InWidget) = FSlateColor(Color);
                }
            }
        }
    }
    if (const FString* ArtFontSizeValue = InNode.Attributes.Find(TEXT("ArtFontSize")))
    {
        int32 ArtFontSize = -1;
        if (UXmlDslParser::ParseInt(*ArtFontSizeValue, ArtFontSize) && ArtFontSize > 0)
        {
            if (FIntProperty* FontProp = FindFProperty<FIntProperty>(InWidget->GetClass(), TEXT("ArtFontSize")))
            {
                FontProp->SetPropertyValue_InContainer(InWidget, ArtFontSize);
            }
        }
    }
    if (const FString* JustValue = InNode.Attributes.Find(TEXT("Justification")))
    {
        ETextJustify::Type Just;
        if (UXmlDslParser::ParseJustification(*JustValue, Just))
        {
            if (FByteProperty* JustProp = FindFProperty<FByteProperty>(InWidget->GetClass(), TEXT("Justification")))
            {
                JustProp->SetPropertyValue_InContainer(InWidget, static_cast<uint8>(Just));
            }
        }
    }
}

static void ApplyMappedImage(FString& OutError, UWidget* InWidget, const FXmlNodeDesc& InNode, const FSlateBrush& InBrush, const FLinearColor& InColor, const FVector2D& InDesiredSize)
{
    UImage* Image = Cast<UImage>(InWidget);
    if (!Image)
    {
        OutError += FString::Printf(TEXT("XmlUI: mapped Image class '%s' is not a UImage subclass\n"), *InWidget->GetClass()->GetName());
        return;
    }
    if (InBrush.GetResourceObject())
    {
        FSlateBrush FinalBrush = InBrush;
        if (!InDesiredSize.IsZero())
        {
            // Serialize the desired size into the brush so the exporter can read it back.
            FinalBrush.ImageSize = InDesiredSize;
        }
        Image->SetBrush(FinalBrush);
    }
    else
    {
        // Pure color: encode the tint into the brush so the exporter can read it back,
        // but preserve an explicit color brush from the DSL (Brush="#AARRGGBB" without Color).
        FSlateBrush SolidBrush = InBrush;
        if (SolidBrush.TintColor.GetSpecifiedColor() == FLinearColor::White)
        {
            SolidBrush.TintColor = InColor;
        }
        Image->SetBrush(SolidBrush);
    }
    Image->SetColorAndOpacity(InColor);
    Image->SetDesiredSizeOverride(InDesiredSize);
}

static void ApplyMappedButton(FString& OutError, UWidget* InWidget, const FXmlNodeDesc& InNode, const FLinearColor& InButtonColor)
{
    UButton* Button = Cast<UButton>(InWidget);
    if (!Button)
    {
        OutError += FString::Printf(TEXT("XmlUI: mapped Button class '%s' is not a UButton subclass\n"), *InWidget->GetClass()->GetName());
        return;
    }
    Button->SetColorAndOpacity(InButtonColor);
    // Text shorthand is materialized as an ordinary child Text widget by BuildButtonNode.
}

static void ApplyMappedProgressBar(FString& OutError, UWidget* InWidget, const FXmlNodeDesc& InNode)
{
    UProgressBar* ProgressBar = Cast<UProgressBar>(InWidget);
    if (!ProgressBar)
    {
        OutError += FString::Printf(TEXT("XmlUI: mapped ProgressBar class '%s' is not a UProgressBar subclass\n"), *InWidget->GetClass()->GetName());
        return;
    }
    if (const FString* PercentValue = InNode.Attributes.Find(TEXT("Percent")))
    {
        float Percent = 0.f;
        if (UXmlDslParser::ParseFloat(*PercentValue, Percent))
        {
            ProgressBar->SetPercent(Percent);
        }
    }
    if (const FString* FillColorValue = InNode.Attributes.Find(TEXT("FillColor")))
    {
        FLinearColor FillColor;
        if (UXmlDslParser::ParseColor(*FillColorValue, FillColor))
        {
            ProgressBar->SetFillColorAndOpacity(FillColor);
        }
    }
}

static void ApplyMappedSpacer(FString& OutError, UWidget* InWidget, float InSize)
{
    USpacer* Spacer = Cast<USpacer>(InWidget);
    if (!Spacer)
    {
        OutError += FString::Printf(TEXT("XmlUI: mapped Spacer class '%s' is not a USpacer subclass\n"), *InWidget->GetClass()->GetName());
        return;
    }
    Spacer->SetSize(FVector2D(InSize, InSize));
}

void UXmlBuilder::AddChildrenToPanel(UWidgetTree* Tree, UPanelWidget* Panel, const TArray<FXmlNodeDesc>& Children, FString& OutError, const TMap<FString, FString>* InWidgetClassMap)
{
    for (const FXmlNodeDesc& ChildNode : Children)
    {
        UWidget* ChildWidget = BuildNodeInternal(Tree, ChildNode, OutError, InWidgetClassMap);
        if (!ChildWidget)
        {
            continue;
        }
        UPanelSlot* Slot = Panel->AddChild(ChildWidget);
        ApplySlotAttributes(Slot, ChildNode);
    }
}

void UXmlBuilder::ConfigureWrapBoxWidget(UWrapBox* InWrapBox, const FXmlNodeDesc& InNode, UWidgetTree* InTree, FString& OutError, const TMap<FString, FString>* InWidgetClassMap)
{
    if (const FString* WrapWidthValue = InNode.Attributes.Find(TEXT("WrapWidth")))
    {
        float WrapWidth = 0.f;
        if (UXmlDslParser::ParseFloat(*WrapWidthValue, WrapWidth) && WrapWidth > 0.f)
        {
            InWrapBox->SetWrapSize(WrapWidth);
            InWrapBox->SetExplicitWrapSize(true);
        }
    }
    AddChildrenToPanel(InTree, InWrapBox, InNode.Children, OutError, InWidgetClassMap);
    ApplyCommonAttributes(InWrapBox, InNode);
}

void UXmlBuilder::ConfigureGridWidget(UUniformGridPanel* InGrid, const FXmlNodeDesc& InNode, UWidgetTree* InTree, FString& OutError, const TMap<FString, FString>* InWidgetClassMap)
{
    if (const FString* ColumnsValue = InNode.Attributes.Find(TEXT("Columns")))
    {
        int32 Columns = 1;
        if (UXmlDslParser::ParseInt(*ColumnsValue, Columns) && Columns > 0)
        {
            // Columns is parsed for DSL documentation only; the runtime grid is derived from child slot Row/Column values, so set Row/Column on every child.
        }
    }
    if (const FString* SlotPaddingValue = InNode.Attributes.Find(TEXT("SlotPadding")))
    {
        FMargin SlotPadding;
        if (UXmlDslParser::ParseMargin(*SlotPaddingValue, SlotPadding))
        {
            InGrid->SetSlotPadding(SlotPadding);
        }
    }
    AddChildrenToPanel(InTree, InGrid, InNode.Children, OutError, InWidgetClassMap);
    ApplyCommonAttributes(InGrid, InNode);
}

UWidget* UXmlBuilder::BuildPanelNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap)
{
    UWidget* Widget = nullptr;

    if (Node.Tag == TEXT("XmlUI") || Node.Tag == TEXT("Vertical") || Node.Tag == TEXT("Horizontal"))
    {
        // XmlUI is the root alias of Vertical: its class mapping follows the Vertical entry.
        const FString MappingTag = (Node.Tag == TEXT("XmlUI")) ? TEXT("Vertical") : Node.Tag;
        bool bHasMapping = false;
        UWidget* MappedWidget = TryCreateMappedWidget(Tree, Node,
            (Node.Tag == TEXT("Horizontal")) ? UHorizontalBox::StaticClass() : UVerticalBox::StaticClass(),
            bHasMapping, OutError, InWidgetClassMap, MappingTag);
        if (bHasMapping)
        {
            if (!MappedWidget)
            {
                return nullptr;
            }
            UPanelWidget* Panel = Cast<UPanelWidget>(MappedWidget);

            AddChildrenToPanel(Tree, Panel, Node.Children, OutError, InWidgetClassMap);

            ApplyCommonAttributes(Panel, Node);
            return Panel;
        }
        else
        {
            UXmlPanel* Panel = Tree->ConstructWidget<UXmlPanel>(UXmlPanel::StaticClass(), FName(*Node.Name));
            Panel->Orientation = (Node.Tag == TEXT("Horizontal")) ? Orient_Horizontal : Orient_Vertical;

            AddChildrenToPanel(Tree, Panel, Node.Children, OutError, InWidgetClassMap);

            Widget = Panel;
        }
    }

    if (Widget)
    {
        ApplyCommonAttributes(Widget, Node);
    }

    return Widget;
}

UWidget* UXmlBuilder::BuildTextNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap)
{
    UWidget* Widget = nullptr;

    bool bHasMapping = false;
    UWidget* MappedWidget = TryCreateMappedWidget(Tree, Node, UTextBlock::StaticClass(), bHasMapping, OutError, InWidgetClassMap);
    if (bHasMapping)
    {
        if (!MappedWidget)
        {
            return nullptr;
        }
        ApplyMappedText(OutError, MappedWidget, Node);
        ApplyCommonAttributes(MappedWidget, Node);
        return MappedWidget;
    }
    else
    {
        UXmlTextBlock* TextBlock = Tree->ConstructWidget<UXmlTextBlock>(UXmlTextBlock::StaticClass(), FName(*Node.Name));

        if (const FString* TextValue = Node.Attributes.Find(TEXT("Text")))
        {
            TextBlock->SetXmlText(FText::FromString(*TextValue));
        }
        if (const FString* FontSizeValue = Node.Attributes.Find(TEXT("FontSize")))
        {
            int32 FontSize = 0;
            if (UXmlDslParser::ParseInt(*FontSizeValue, FontSize))
            {
                TextBlock->FontSize = FontSize;
            }
        }
        if (const FString* ArtFontSizeValue = Node.Attributes.Find(TEXT("ArtFontSize")))
        {
            int32 ArtFontSize = -1;
            if (UXmlDslParser::ParseInt(*ArtFontSizeValue, ArtFontSize))
            {
                TextBlock->ArtFontSize = ArtFontSize;
            }
        }
        if (const FString* FontFamilyValue = Node.Attributes.Find(TEXT("FontFamily")))
        {
            TextBlock->FontFamily = *FontFamilyValue;
        }
        if (const FString* ColorValue = Node.Attributes.Find(TEXT("Color")))
        {
            FLinearColor Color;
            if (UXmlDslParser::ParseColor(*ColorValue, Color))
            {
                TextBlock->SetXmlColor(Color);
            }
        }
        if (const FString* JustificationValue = Node.Attributes.Find(TEXT("Justification")))
        {
            ETextJustify::Type Justification = ETextJustify::Left;
            if (UXmlDslParser::ParseJustification(*JustificationValue, Justification))
            {
                TextBlock->Justification = Justification;
            }
        }
        if (const FString* WrapTextAtValue = Node.Attributes.Find(TEXT("WrapTextAt")))
        {
            float WrapTextAt = 0.f;
            if (UXmlDslParser::ParseFloat(*WrapTextAtValue, WrapTextAt))
            {
                TextBlock->WrapTextAt = WrapTextAt;
            }
        }
        if (const FString* ShadowColorValue = Node.Attributes.Find(TEXT("ShadowColor")))
        {
            FLinearColor ShadowColor;
            if (UXmlDslParser::ParseColor(*ShadowColorValue, ShadowColor))
            {
                TextBlock->ShadowColor = ShadowColor;
            }
        }
        if (const FString* ShadowOffsetValue = Node.Attributes.Find(TEXT("ShadowOffset")))
        {
            FVector2D ShadowOffset;
            if (UXmlDslParser::ParseVector2D(*ShadowOffsetValue, ShadowOffset))
            {
                TextBlock->ShadowOffset = ShadowOffset;
            }
        }

        Widget = TextBlock;
    }

    if (Widget)
    {
        ApplyCommonAttributes(Widget, Node);
    }

    return Widget;
}

UWidget* UXmlBuilder::BuildImageNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap)
{
    UWidget* Widget = nullptr;

    bool bHasMapping = false;
    UWidget* MappedWidget = TryCreateMappedWidget(Tree, Node, UImage::StaticClass(), bHasMapping, OutError, InWidgetClassMap);
    if (bHasMapping)
    {
        if (!MappedWidget)
        {
            return nullptr;
        }
        FSlateBrush Brush;
        if (const FString* BrushValue = Node.Attributes.Find(TEXT("Brush")))
        {
            ParseBrushFromString(Brush, *BrushValue);
        }
        FLinearColor Color = FLinearColor::White;
        if (const FString* ColorValue = Node.Attributes.Find(TEXT("Color")))
        {
            UXmlDslParser::ParseColor(*ColorValue, Color);
        }
        FVector2D DesiredSize = FVector2D::ZeroVector;
        if (const FString* DesiredSizeValue = Node.Attributes.Find(TEXT("DesiredSize")))
        {
            UXmlDslParser::ParseVector2D(*DesiredSizeValue, DesiredSize);
        }
        ApplyMappedImage(OutError, MappedWidget, Node, Brush, Color, DesiredSize);
        ApplyCommonAttributes(MappedWidget, Node);
        return MappedWidget;
    }
    else
    {
        UXmlImage* Image = Tree->ConstructWidget<UXmlImage>(UXmlImage::StaticClass(), FName(*Node.Name));

        if (const FString* BrushValue = Node.Attributes.Find(TEXT("Brush")))
        {
            ParseBrushFromString(Image->Brush, *BrushValue);
        }
        if (const FString* ColorValue = Node.Attributes.Find(TEXT("Color")))
        {
            FLinearColor Color;
            if (UXmlDslParser::ParseColor(*ColorValue, Color))
            {
                Image->Color = Color;
                if (!Image->Brush.GetResourceObject() && Image->Brush.TintColor.GetSpecifiedColor() == FLinearColor::White)
                {
                    // Mirror the mapped-image path: pure-color brushes carry the tint in the brush.
                    Image->Brush.TintColor = Color;
                }
            }
        }
        if (const FString* DesiredSizeValue = Node.Attributes.Find(TEXT("DesiredSize")))
        {
            FVector2D DesiredSize;
            if (UXmlDslParser::ParseVector2D(*DesiredSizeValue, DesiredSize))
            {
                Image->DesiredSize = DesiredSize;
            }
        }

        Widget = Image;
    }

    if (Widget)
    {
        ApplyCommonAttributes(Widget, Node);
    }

    return Widget;
}

UWidget* UXmlBuilder::BuildSpacerNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap)
{
    UWidget* Widget = nullptr;

    bool bHasMapping = false;
    UWidget* MappedWidget = TryCreateMappedWidget(Tree, Node, USpacer::StaticClass(), bHasMapping, OutError, InWidgetClassMap);
    if (bHasMapping)
    {
        if (!MappedWidget)
        {
            return nullptr;
        }
        if (const FString* SizeValue = Node.Attributes.Find(TEXT("Size")))
        {
            float Size = 0.f;
            if (UXmlDslParser::ParseFloat(*SizeValue, Size))
            {
                ApplyMappedSpacer(OutError, MappedWidget, Size);
            }
        }
        ApplyCommonAttributes(MappedWidget, Node);
        return MappedWidget;
    }
    else
    {
        UXmlSpacer* Spacer = Tree->ConstructWidget<UXmlSpacer>(UXmlSpacer::StaticClass(), FName(*Node.Name));

        if (const FString* SizeValue = Node.Attributes.Find(TEXT("Size")))
        {
            float Size = 0.f;
            if (UXmlDslParser::ParseFloat(*SizeValue, Size))
            {
                Spacer->Size = Size;
            }
        }

        Widget = Spacer;
    }

    if (Widget)
    {
        ApplyCommonAttributes(Widget, Node);
    }

    return Widget;
}

UWidget* UXmlBuilder::BuildButtonNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap)
{
    UWidget* Widget = nullptr;

    bool bHasMapping = false;
    UWidget* MappedWidget = TryCreateMappedWidget(Tree, Node, UButton::StaticClass(), bHasMapping, OutError, InWidgetClassMap);
    if (bHasMapping)
    {
        if (!MappedWidget)
        {
            return nullptr;
        }
        UButton* Button = Cast<UButton>(MappedWidget);
        FLinearColor ButtonColor = FLinearColor::White;
        if (const FString* ButtonColorValue = Node.Attributes.Find(TEXT("ButtonColor")))
        {
            UXmlDslParser::ParseColor(*ButtonColorValue, ButtonColor);
        }
        ApplyMappedButton(OutError, Button, Node, ButtonColor);

        if (Node.Children.Num() > 0)
        {
            UWidget* ChildWidget = BuildNodeInternal(Tree, Node.Children[0], OutError, InWidgetClassMap);
            if (ChildWidget)
            {
                Button->AddChild(ChildWidget);
            }

            if (Node.Children.Num() > 1)
            {
                OutError += FString::Printf(TEXT("XmlUI: Button '%s' has more than one child, ignoring extras\n"), *Node.Name);
            }
        }
        else if (const FString* TextValue = Node.Attributes.Find(TEXT("Text")))
        {
            // Preserve the convenient <Button Text="..."/> shorthand while still baking
            // a stock UButton: materialize the label as a real Text child in the tree.
            FXmlNodeDesc LabelNode;
            LabelNode.Tag = TEXT("Text");
            LabelNode.Name = Node.Name + TEXT("_Label");
            LabelNode.Attributes.Add(TEXT("Name"), LabelNode.Name);
            LabelNode.Attributes.Add(TEXT("Text"), *TextValue);
            LabelNode.Attributes.Add(TEXT("Justification"), TEXT("Center"));
            if (const FString* TextColorValue = Node.Attributes.Find(TEXT("TextColor")))
            {
                LabelNode.Attributes.Add(TEXT("Color"), *TextColorValue);
            }
            if (const FString* FontFamilyValue = Node.Attributes.Find(TEXT("FontFamily")))
            {
                LabelNode.Attributes.Add(TEXT("FontFamily"), *FontFamilyValue);
            }

            if (UWidget* LabelWidget = BuildTextNode(Tree, LabelNode, OutError, InWidgetClassMap))
            {
                Button->AddChild(LabelWidget);
            }
        }

        ApplyCommonAttributes(Button, Node);
        return Button;
    }
    else
    {
        UXmlButton* Button = Tree->ConstructWidget<UXmlButton>(UXmlButton::StaticClass(), FName(*Node.Name));

        if (const FString* TextValue = Node.Attributes.Find(TEXT("Text")))
        {
            Button->Text = FText::FromString(*TextValue);
        }
        if (const FString* ButtonColorValue = Node.Attributes.Find(TEXT("ButtonColor")))
        {
            FLinearColor ButtonColor;
            if (UXmlDslParser::ParseColor(*ButtonColorValue, ButtonColor))
            {
                Button->ButtonColor = ButtonColor;
            }
        }
        if (const FString* TextColorValue = Node.Attributes.Find(TEXT("TextColor")))
        {
            FLinearColor TextColor;
            if (UXmlDslParser::ParseColor(*TextColorValue, TextColor))
            {
                Button->TextColor = TextColor;
            }
        }
        if (const FString* FontFamilyValue = Node.Attributes.Find(TEXT("FontFamily")))
        {
            Button->FontFamily = *FontFamilyValue;
        }
        if (const FString* PaddingValue = Node.Attributes.Find(TEXT("Padding")))
        {
            FMargin Padding;
            if (UXmlDslParser::ParseMargin(*PaddingValue, Padding))
            {
                Button->ContentPadding = Padding;
            }
        }

        if (Node.Children.Num() > 0)
        {
            UWidget* ChildWidget = BuildNodeInternal(Tree, Node.Children[0], OutError, InWidgetClassMap);
            if (ChildWidget)
            {
                Button->AddChild(ChildWidget);
            }

            if (Node.Children.Num() > 1)
            {
                OutError += FString::Printf(TEXT("XmlUI: Button '%s' has more than one child, ignoring extras\n"), *Node.Name);
            }
        }

        Widget = Button;
    }

    if (Widget)
    {
        ApplyCommonAttributes(Widget, Node);
    }

    return Widget;
}

UWidget* UXmlBuilder::BuildCheckBoxNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap)
{
    bool bHasMapping = false;
    UWidget* MappedWidget = TryCreateMappedWidget(Tree, Node, UCheckBox::StaticClass(), bHasMapping, OutError, InWidgetClassMap);
    if (bHasMapping && !MappedWidget)
    {
        return nullptr;
    }

    UCheckBox* CheckBox = MappedWidget
        ? Cast<UCheckBox>(MappedWidget)
        : Tree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), FName(*Node.Name));
    if (!CheckBox)
    {
        return nullptr;
    }

    if (const FString* CheckedStateValue = Node.Attributes.Find(TEXT("CheckedState")))
    {
        const FString State = CheckedStateValue->TrimStartAndEnd().ToLower();
        if (State == TEXT("checked") || State == TEXT("true"))
        {
            CheckBox->SetIsChecked(true);
        }
        else if (State == TEXT("unchecked") || State == TEXT("false"))
        {
            CheckBox->SetIsChecked(false);
        }
        else if (State == TEXT("undetermined"))
        {
            CheckBox->SetCheckedState(ECheckBoxState::Undetermined);
        }
    }

    if (Node.Children.Num() > 0)
    {
        if (UWidget* ChildWidget = BuildNodeInternal(Tree, Node.Children[0], OutError, InWidgetClassMap))
        {
            CheckBox->AddChild(ChildWidget);
        }
        if (Node.Children.Num() > 1)
        {
            OutError += FString::Printf(TEXT("XmlUI: CheckBox '%s' has more than one child, ignoring extras\n"), *Node.Name);
        }
    }

    ApplyCommonAttributes(CheckBox, Node);
    return CheckBox;
}

UWidget* UXmlBuilder::BuildProgressBarNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap)
{
    UWidget* Widget = nullptr;

    bool bHasMapping = false;
    UWidget* MappedWidget = TryCreateMappedWidget(Tree, Node, UProgressBar::StaticClass(), bHasMapping, OutError, InWidgetClassMap);
    if (bHasMapping)
    {
        if (!MappedWidget)
        {
            return nullptr;
        }
        ApplyMappedProgressBar(OutError, MappedWidget, Node);
        ApplyCommonAttributes(MappedWidget, Node);
        return MappedWidget;
    }
    else
    {
        UXmlProgressBar* ProgressBar = Tree->ConstructWidget<UXmlProgressBar>(UXmlProgressBar::StaticClass(), FName(*Node.Name));

        if (const FString* PercentValue = Node.Attributes.Find(TEXT("Percent")))
        {
            float Percent = 0.f;
            if (UXmlDslParser::ParseFloat(*PercentValue, Percent))
            {
                ProgressBar->Percent = Percent;
            }
        }
        if (const FString* FillColorValue = Node.Attributes.Find(TEXT("FillColor")))
        {
            FLinearColor FillColor;
            if (UXmlDslParser::ParseColor(*FillColorValue, FillColor))
            {
                ProgressBar->FillColor = FillColor;
            }
        }

        Widget = ProgressBar;
    }

    if (Widget)
    {
        ApplyCommonAttributes(Widget, Node);
    }

    return Widget;
}

UWidget* UXmlBuilder::BuildOverlayNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap)
{
    UWidget* Widget = nullptr;

    bool bHasMapping = false;
    UWidget* MappedWidget = TryCreateMappedWidget(Tree, Node, UOverlay::StaticClass(), bHasMapping, OutError, InWidgetClassMap);
    UOverlay* Overlay = nullptr;
    if (bHasMapping)
    {
        if (!MappedWidget)
        {
            return nullptr;
        }
        Overlay = Cast<UOverlay>(MappedWidget);
    }
    else
    {
        Overlay = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), FName(*Node.Name));
    }
    AddChildrenToPanel(Tree, Overlay, Node.Children, OutError, InWidgetClassMap);
    Widget = Overlay;

    if (Widget)
    {
        ApplyCommonAttributes(Widget, Node);
    }

    return Widget;
}

UWidget* UXmlBuilder::BuildScrollBoxNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap)
{
    UWidget* Widget = nullptr;

    bool bHasMapping = false;
    UWidget* MappedWidget = TryCreateMappedWidget(Tree, Node, UScrollBox::StaticClass(), bHasMapping, OutError, InWidgetClassMap);
    if (bHasMapping && !MappedWidget)
    {
        return nullptr;
    }
    UScrollBox* ScrollBox = MappedWidget ? Cast<UScrollBox>(MappedWidget) : Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), FName(*Node.Name));
    if (const FString* OrientationValue = Node.Attributes.Find(TEXT("Orientation")))
    {
        const FString LowerValue = OrientationValue->ToLower();
        if (LowerValue == TEXT("horizontal"))
        {
            ScrollBox->SetOrientation(EOrientation::Orient_Horizontal);
        }
        else if (LowerValue == TEXT("vertical"))
        {
            ScrollBox->SetOrientation(EOrientation::Orient_Vertical);
        }
    }
    AddChildrenToPanel(Tree, ScrollBox, Node.Children, OutError, InWidgetClassMap);
    Widget = ScrollBox;

    if (Widget)
    {
        ApplyCommonAttributes(Widget, Node);
    }

    return Widget;
}

UWidget* UXmlBuilder::BuildUserWidgetNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap)
{
    UWidget* Widget = nullptr;

    bool bHasMapping = false;
    UWidget* MappedWidget = TryCreateMappedWidget(Tree, Node, UUserWidget::StaticClass(), bHasMapping, OutError, InWidgetClassMap);
    if (bHasMapping && !MappedWidget)
    {
        return nullptr;
    }
    UClass* WidgetClass = UUserWidget::StaticClass();
    if (const FString* WbpValue = Node.Attributes.Find(TEXT("WBP")))
    {
        WidgetClass = ResolveUserWidgetClass(*WbpValue);
        if (!WidgetClass || !WidgetClass->IsChildOf(UUserWidget::StaticClass()))
        {
            OutError += FString::Printf(TEXT("XmlUI: UserWidget '%s' could not resolve WBP class '%s'\n"), *Node.Name, **WbpValue);
            return nullptr;
        }
    }
    UUserWidget* UserWidget = nullptr;
    if (bHasMapping)
    {
        UserWidget = Cast<UUserWidget>(MappedWidget);
    }
    else
    {
        // Asset trees (baking) need designer-template semantics: an uninitialized instance so
        // SetContentForSlot records a NamedSlotBinding instead of resolving a live slot widget.
        // Runtime trees keep eager Initialize so the nested widget renders. UWidgetBlueprint is
        // editor-only (Blutility); detect the asset tree by outer class name to avoid the module dependency.
        bool bBuildingAssetTree = false;
#if WITH_EDITOR
        bBuildingAssetTree = Tree->GetOuter() && Tree->GetOuter()->GetClass()->GetName() == TEXT("WidgetBlueprint");
#endif
        UserWidget = bBuildingAssetTree
            ? Cast<UUserWidget>(Tree->ConstructWidget<UWidget>(WidgetClass, FName(*Node.Name)))
            : Tree->ConstructWidget<UUserWidget>(WidgetClass, FName(*Node.Name));
    }
    if (!UserWidget)
    {
        OutError += FString::Printf(TEXT("XmlUI: UserWidget '%s' failed to construct (provide a valid WBP attribute or a WidgetClassMap mapping)\n"), *Node.Name);
        return nullptr;
    }
    // NamedSlotsWithID is editor-only data; ask the runtime interface for instance-fillable slot names instead.
    TArray<FName> AvailableSlots;
    UserWidget->GetSlotNames(AvailableSlots);
    for (const FXmlNodeDesc& ChildNode : Node.Children)
    {
        const FString* SlotName = ChildNode.Attributes.Find(TEXT("SlotName"));
        if (!SlotName || SlotName->IsEmpty())
        {
            OutError += FString::Printf(TEXT("XmlUI: UserWidget '%s' child '%s' has no SlotName, ignored\n"), *Node.Name, *ChildNode.Name);
            continue;
        }
        if (!AvailableSlots.Contains(FName(**SlotName)))
        {
            OutError += FString::Printf(TEXT("XmlUI: UserWidget '%s' child '%s' names unknown SlotName '%s' (not an exposed named slot), ignored\n"), *Node.Name, *ChildNode.Name, **SlotName);
            continue;
        }
        UWidget* ChildWidget = BuildNodeInternal(Tree, ChildNode, OutError, InWidgetClassMap);
        if (ChildWidget)
        {
            const FName SlotFName(**SlotName);
            UserWidget->SetContentForSlot(SlotFName, ChildWidget);
            if (UserWidget->GetContentForSlot(SlotFName) != ChildWidget)
            {
                OutError += FString::Printf(TEXT("XmlUI: UserWidget '%s' slot '%s' rejected child '%s' (slot not present on the constructed instance)\n"), *Node.Name, **SlotName, *ChildNode.Name);
            }
        }
    }
#if WITH_EDITOR
    UserWidget->AssignGUIDToBindings();
#endif
    Widget = UserWidget;

    if (Widget)
    {
        ApplyReflectedTextAttrs(UserWidget, Node);
        ApplyCommonAttributes(Widget, Node);
    }

    return Widget;
}

UWidget* UXmlBuilder::BuildSizeBoxNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap)
{
    UWidget* Widget = nullptr;

    bool bHasMapping = false;
    UWidget* MappedWidget = TryCreateMappedWidget(Tree, Node, USizeBox::StaticClass(), bHasMapping, OutError, InWidgetClassMap);
    if (bHasMapping)
    {
        if (!MappedWidget)
        {
            return nullptr;
        }
        USizeBox* SizeBox = Cast<USizeBox>(MappedWidget);
        float Value = 0.f;
        if (UXmlDslParser::ParseFloat(Node.Attributes.FindRef(TEXT("WidthOverride")), Value)) { SizeBox->SetWidthOverride(Value); }
        if (UXmlDslParser::ParseFloat(Node.Attributes.FindRef(TEXT("HeightOverride")), Value)) { SizeBox->SetHeightOverride(Value); }
        if (UXmlDslParser::ParseFloat(Node.Attributes.FindRef(TEXT("MinDesiredWidth")), Value)) { SizeBox->SetMinDesiredWidth(Value); }
        if (UXmlDslParser::ParseFloat(Node.Attributes.FindRef(TEXT("MinDesiredHeight")), Value)) { SizeBox->SetMinDesiredHeight(Value); }
        if (UXmlDslParser::ParseFloat(Node.Attributes.FindRef(TEXT("MaxDesiredWidth")), Value)) { SizeBox->SetMaxDesiredWidth(Value); }
        if (UXmlDslParser::ParseFloat(Node.Attributes.FindRef(TEXT("MaxDesiredHeight")), Value)) { SizeBox->SetMaxDesiredHeight(Value); }
        if (Node.Children.Num() > 1)
        {
            OutError += FString::Printf(TEXT("XmlUI: SizeBox '%s' has more than one child, ignoring the extras."), *Node.Name);
        }
        if (Node.Children.Num() >= 1)
        {
            UWidget* ChildWidget = BuildNodeInternal(Tree, Node.Children[0], OutError, InWidgetClassMap);
            if (ChildWidget) { SizeBox->AddChild(ChildWidget); }
        }
        Widget = SizeBox;
    }
    else
    {
        USizeBox* SizeBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(*Node.Name));
        float Value = 0.f;
        if (UXmlDslParser::ParseFloat(Node.Attributes.FindRef(TEXT("WidthOverride")), Value)) { SizeBox->SetWidthOverride(Value); }
        if (UXmlDslParser::ParseFloat(Node.Attributes.FindRef(TEXT("HeightOverride")), Value)) { SizeBox->SetHeightOverride(Value); }
        if (UXmlDslParser::ParseFloat(Node.Attributes.FindRef(TEXT("MinDesiredWidth")), Value)) { SizeBox->SetMinDesiredWidth(Value); }
        if (UXmlDslParser::ParseFloat(Node.Attributes.FindRef(TEXT("MinDesiredHeight")), Value)) { SizeBox->SetMinDesiredHeight(Value); }
        if (UXmlDslParser::ParseFloat(Node.Attributes.FindRef(TEXT("MaxDesiredWidth")), Value)) { SizeBox->SetMaxDesiredWidth(Value); }
        if (UXmlDslParser::ParseFloat(Node.Attributes.FindRef(TEXT("MaxDesiredHeight")), Value)) { SizeBox->SetMaxDesiredHeight(Value); }
        if (Node.Children.Num() > 1)
        {
            OutError += FString::Printf(TEXT("XmlUI: SizeBox '%s' has more than one child, ignoring the extras."), *Node.Name);
        }
        if (Node.Children.Num() >= 1)
        {
            UWidget* ChildWidget = BuildNodeInternal(Tree, Node.Children[0], OutError, InWidgetClassMap);
            if (ChildWidget) { SizeBox->AddChild(ChildWidget); }
        }
        Widget = SizeBox;
    }

    if (Widget)
    {
        ApplyCommonAttributes(Widget, Node);
    }

    return Widget;
}

UWidget* UXmlBuilder::BuildScaleBoxNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap)
{
    UWidget* Widget = nullptr;

    bool bHasMapping = false;
    UWidget* MappedWidget = TryCreateMappedWidget(Tree, Node, UScaleBox::StaticClass(), bHasMapping, OutError, InWidgetClassMap);
    if (bHasMapping)
    {
        if (!MappedWidget)
        {
            return nullptr;
        }
        UScaleBox* ScaleBox = Cast<UScaleBox>(MappedWidget);
        float Value = 0.f;
        if (UXmlDslParser::ParseFloat(Node.Attributes.FindRef(TEXT("UserDesiredWidth")), Value))
        {
            if (FFloatProperty* WidthProp = FindFProperty<FFloatProperty>(ScaleBox->GetClass(), TEXT("UserDesiredWidth"))) { WidthProp->SetPropertyValue_InContainer(ScaleBox, Value); }
        }
        if (UXmlDslParser::ParseFloat(Node.Attributes.FindRef(TEXT("UserDesiredHeight")), Value))
        {
            if (FFloatProperty* HeightProp = FindFProperty<FFloatProperty>(ScaleBox->GetClass(), TEXT("UserDesiredHeight"))) { HeightProp->SetPropertyValue_InContainer(ScaleBox, Value); }
        }
        FVector2D ContentScale;
        if (UXmlDslParser::ParseVector2D(Node.Attributes.FindRef(TEXT("ContentScale")), ContentScale))
        {
            if (FStructProperty* ScaleProp = FindFProperty<FStructProperty>(ScaleBox->GetClass(), TEXT("ContentScale")))
            {
                if (ScaleProp->Struct->GetFName() == TEXT("Vector2D")) { *ScaleProp->ContainerPtrToValuePtr<FVector2D>(ScaleBox) = ContentScale; }
            }
        }
        EStretch::Type Stretch = EStretch::None;
        if (ParseScaleEnum(Node.Attributes.FindRef(TEXT("Stretch")), Stretch)) { ScaleBox->SetStretch(Stretch); }
        EStretchDirection::Type StretchDirection = EStretchDirection::Both;
        if (ParseScaleEnum(Node.Attributes.FindRef(TEXT("StretchDirection")), StretchDirection)) { ScaleBox->SetStretchDirection(StretchDirection); }
        if (Node.Children.Num() > 1)
        {
            OutError += FString::Printf(TEXT("XmlUI: ScaleBox '%s' has more than one child, ignoring the extras."), *Node.Name);
        }
        if (Node.Children.Num() >= 1)
        {
            UWidget* ChildWidget = BuildNodeInternal(Tree, Node.Children[0], OutError, InWidgetClassMap);
            if (ChildWidget) { ScaleBox->AddChild(ChildWidget); }
        }
        Widget = ScaleBox;
    }
    else
    {
        UScaleBox* ScaleBox = Tree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), FName(*Node.Name));
        float Value = 0.f;
        if (UXmlDslParser::ParseFloat(Node.Attributes.FindRef(TEXT("UserDesiredWidth")), Value))
        {
            if (FFloatProperty* WidthProp = FindFProperty<FFloatProperty>(ScaleBox->GetClass(), TEXT("UserDesiredWidth"))) { WidthProp->SetPropertyValue_InContainer(ScaleBox, Value); }
        }
        if (UXmlDslParser::ParseFloat(Node.Attributes.FindRef(TEXT("UserDesiredHeight")), Value))
        {
            if (FFloatProperty* HeightProp = FindFProperty<FFloatProperty>(ScaleBox->GetClass(), TEXT("UserDesiredHeight"))) { HeightProp->SetPropertyValue_InContainer(ScaleBox, Value); }
        }
        FVector2D ContentScale;
        if (UXmlDslParser::ParseVector2D(Node.Attributes.FindRef(TEXT("ContentScale")), ContentScale))
        {
            if (FStructProperty* ScaleProp = FindFProperty<FStructProperty>(ScaleBox->GetClass(), TEXT("ContentScale")))
            {
                if (ScaleProp->Struct->GetFName() == TEXT("Vector2D")) { *ScaleProp->ContainerPtrToValuePtr<FVector2D>(ScaleBox) = ContentScale; }
            }
        }
        EStretch::Type Stretch = EStretch::None;
        if (ParseScaleEnum(Node.Attributes.FindRef(TEXT("Stretch")), Stretch)) { ScaleBox->SetStretch(Stretch); }
        EStretchDirection::Type StretchDirection = EStretchDirection::Both;
        if (ParseScaleEnum(Node.Attributes.FindRef(TEXT("StretchDirection")), StretchDirection)) { ScaleBox->SetStretchDirection(StretchDirection); }
        if (Node.Children.Num() > 1)
        {
            OutError += FString::Printf(TEXT("XmlUI: ScaleBox '%s' has more than one child, ignoring the extras."), *Node.Name);
        }
        if (Node.Children.Num() >= 1)
        {
            UWidget* ChildWidget = BuildNodeInternal(Tree, Node.Children[0], OutError, InWidgetClassMap);
            if (ChildWidget) { ScaleBox->AddChild(ChildWidget); }
        }
        Widget = ScaleBox;
    }

    if (Widget)
    {
        ApplyCommonAttributes(Widget, Node);
    }

    return Widget;
}

UWidget* UXmlBuilder::BuildCanvasNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap)
{
    UWidget* Widget = nullptr;

    bool bHasMapping = false;
    UWidget* MappedWidget = TryCreateMappedWidget(Tree, Node, UCanvasPanel::StaticClass(), bHasMapping, OutError, InWidgetClassMap);
    if (bHasMapping && !MappedWidget)
    {
        return nullptr;
    }
    UCanvasPanel* Canvas = MappedWidget ? Cast<UCanvasPanel>(MappedWidget) : Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), FName(*Node.Name));
    AddChildrenToPanel(Tree, Canvas, Node.Children, OutError, InWidgetClassMap);
    Widget = Canvas;

    if (Widget)
    {
        ApplyCommonAttributes(Widget, Node);
    }

    return Widget;
}

UWidget* UXmlBuilder::BuildMenuAnchorNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap)
{
    UWidget* Widget = nullptr;

    bool bHasMapping = false;
    UWidget* MappedWidget = TryCreateMappedWidget(Tree, Node, UMenuAnchor::StaticClass(), bHasMapping, OutError, InWidgetClassMap);
    if (bHasMapping && !MappedWidget)
    {
        return nullptr;
    }
    UMenuAnchor* Anchor = MappedWidget ? Cast<UMenuAnchor>(MappedWidget) : Tree->ConstructWidget<UMenuAnchor>(UMenuAnchor::StaticClass(), FName(*Node.Name));
    if (const FString* MenuValue = Node.Attributes.Find(TEXT("Menu")))
    {
        UClass* MenuClass = ResolveUserWidgetClass(*MenuValue);
        if (!MenuClass || !MenuClass->IsChildOf(UUserWidget::StaticClass()))
        {
            OutError += FString::Printf(TEXT("XmlUI: MenuAnchor '%s' could not resolve Menu class '%s'\n"), *Node.Name, **MenuValue);
            return nullptr;
        }
        Anchor->MenuClass = MenuClass;
    }
    if (Node.Children.Num() > 0)
    {
        UWidget* ChildWidget = BuildNodeInternal(Tree, Node.Children[0], OutError, InWidgetClassMap);
        if (ChildWidget)
        {
            Anchor->SetContent(ChildWidget);
        }
        if (Node.Children.Num() > 1)
        {
            OutError += FString::Printf(TEXT("XmlUI: MenuAnchor '%s' has more than one child, ignoring extras\n"), *Node.Name);
        }
    }
    Widget = Anchor;

    if (Widget)
    {
        ApplyCommonAttributes(Widget, Node);
    }

    return Widget;
}

UWidget* UXmlBuilder::BuildBorderNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap)
{
    UWidget* Widget = nullptr;

    bool bHasMapping = false;
    UWidget* MappedWidget = TryCreateMappedWidget(Tree, Node, UBorder::StaticClass(), bHasMapping, OutError, InWidgetClassMap);
    if (bHasMapping && !MappedWidget)
    {
        return nullptr;
    }
    UBorder* Border = MappedWidget ? Cast<UBorder>(MappedWidget) : Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(*Node.Name));
    if (const FString* BrushColorValue = Node.Attributes.Find(TEXT("BrushColor")))
    {
        FLinearColor BrushColor;
        if (UXmlDslParser::ParseColor(*BrushColorValue, BrushColor))
        {
            Border->SetBrushColor(BrushColor);
        }
    }
    if (const FString* PaddingValue = Node.Attributes.Find(TEXT("Padding")))
    {
        FMargin Padding;
        if (UXmlDslParser::ParseMargin(*PaddingValue, Padding))
        {
            Border->SetPadding(Padding);
        }
    }
    if (Node.Children.Num() > 0)
    {
        const FXmlNodeDesc& ChildNode = Node.Children[0];
        UWidget* ChildWidget = BuildNodeInternal(Tree, ChildNode, OutError, InWidgetClassMap);
        if (ChildWidget)
        {
            UPanelSlot* Slot = Border->SetContent(ChildWidget);
            ApplySlotAttributes(Slot, ChildNode);
        }
        if (Node.Children.Num() > 1)
        {
            OutError += FString::Printf(TEXT("XmlUI: Border '%s' has more than one child, ignoring extras\n"), *Node.Name);
        }
    }
    Widget = Border;

    if (Widget)
    {
        ApplyCommonAttributes(Widget, Node);
    }

    return Widget;
}

UWidget* UXmlBuilder::BuildNodeInternal(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap)
{
    if (!Tree)
    {
        OutError += TEXT("XmlUI: Widget tree is null\n");
        return nullptr;
    }

    if (Node.Tag == TEXT("XmlUI") || Node.Tag == TEXT("Vertical") || Node.Tag == TEXT("Horizontal"))
    {
        return BuildPanelNode(Tree, Node, OutError, InWidgetClassMap);
    }
    else if (Node.Tag == TEXT("Text")) { return BuildTextNode(Tree, Node, OutError, InWidgetClassMap); }
    else if (Node.Tag == TEXT("Image")) { return BuildImageNode(Tree, Node, OutError, InWidgetClassMap); }
    else if (Node.Tag == TEXT("Button")) { return BuildButtonNode(Tree, Node, OutError, InWidgetClassMap); }
    else if (Node.Tag == TEXT("CheckBox")) { return BuildCheckBoxNode(Tree, Node, OutError, InWidgetClassMap); }
    else if (Node.Tag == TEXT("Spacer")) { return BuildSpacerNode(Tree, Node, OutError, InWidgetClassMap); }
    else if (Node.Tag == TEXT("ProgressBar")) { return BuildProgressBarNode(Tree, Node, OutError, InWidgetClassMap); }
    else if (Node.Tag == TEXT("Overlay")) { return BuildOverlayNode(Tree, Node, OutError, InWidgetClassMap); }
    else if (Node.Tag == TEXT("WrapBox"))
    {
        bool bHasMapping = false;
        UWidget* MappedWidget = TryCreateMappedWidget(Tree, Node, UWrapBox::StaticClass(), bHasMapping, OutError, InWidgetClassMap);
        if (bHasMapping && !MappedWidget)
        {
            return nullptr;
        }
        UWrapBox* WrapBox = MappedWidget ? Cast<UWrapBox>(MappedWidget) : Tree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), FName(*Node.Name));
        ConfigureWrapBoxWidget(WrapBox, Node, Tree, OutError, InWidgetClassMap);
        return WrapBox;
    }
    else if (Node.Tag == TEXT("Grid"))
    {
        bool bHasMapping = false;
        UWidget* MappedWidget = TryCreateMappedWidget(Tree, Node, UUniformGridPanel::StaticClass(), bHasMapping, OutError, InWidgetClassMap);
        if (bHasMapping && !MappedWidget)
        {
            return nullptr;
        }
        UUniformGridPanel* Grid = MappedWidget ? Cast<UUniformGridPanel>(MappedWidget) : Tree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(), FName(*Node.Name));
        ConfigureGridWidget(Grid, Node, Tree, OutError, InWidgetClassMap);
        return Grid;
    }
    else if (Node.Tag == TEXT("ScrollBox")) { return BuildScrollBoxNode(Tree, Node, OutError, InWidgetClassMap); }
    else if (Node.Tag == TEXT("UserWidget")) { return BuildUserWidgetNode(Tree, Node, OutError, InWidgetClassMap); }
    else if (Node.Tag == TEXT("SizeBox")) { return BuildSizeBoxNode(Tree, Node, OutError, InWidgetClassMap); }
    else if (Node.Tag == TEXT("ScaleBox")) { return BuildScaleBoxNode(Tree, Node, OutError, InWidgetClassMap); }
    else if (Node.Tag == TEXT("Canvas")) { return BuildCanvasNode(Tree, Node, OutError, InWidgetClassMap); }
    else if (Node.Tag == TEXT("MenuAnchor")) { return BuildMenuAnchorNode(Tree, Node, OutError, InWidgetClassMap); }
    else if (Node.Tag == TEXT("Border")) { return BuildBorderNode(Tree, Node, OutError, InWidgetClassMap); }
    else
    {
        OutError += FString::Printf(TEXT("XmlUI: Unknown tag '%s', skipped\n"), *Node.Tag);
        return nullptr;
    }
}

void UXmlBuilder::ApplyCommonAttributes(UWidget* Widget, const FXmlNodeDesc& Node)
{
    if (const FString* StyleValue = Node.Attributes.Find(TEXT("Style")))
    {
        if (FClassProperty* StyleClassProp = FindFProperty<FClassProperty>(Widget->GetClass(), TEXT("Style")))
        {
            if (UClass* StyleClass = LoadClass<UObject>(nullptr, **StyleValue))
            {
                StyleClassProp->SetObjectPropertyValue_InContainer(Widget, StyleClass);
            }
        }
        else if (FObjectProperty* StyleObjectProp = FindFProperty<FObjectProperty>(Widget->GetClass(), TEXT("Style")))
        {
            if (UObject* StyleObject = LoadObject<UObject>(nullptr, **StyleValue))
            {
                StyleObjectProp->SetObjectPropertyValue_InContainer(Widget, StyleObject);
            }
        }
    }

    if (const FString* IsEnabledValue = Node.Attributes.Find(TEXT("IsEnabled")))
    {
        const FString LowerValue = IsEnabledValue->TrimStartAndEnd().ToLower();
        if (LowerValue == TEXT("true") || LowerValue == TEXT("1"))
        {
            Widget->SetIsEnabled(true);
        }
        else if (LowerValue == TEXT("false") || LowerValue == TEXT("0"))
        {
            Widget->SetIsEnabled(false);
        }
    }

    if (const FString* VisibilityValue = Node.Attributes.Find(TEXT("Visibility")))
    {
        const FString LowerValue = VisibilityValue->ToLower();
        if (LowerValue == TEXT("visible"))
        {
            Widget->SetVisibility(ESlateVisibility::Visible);
        }
        else if (LowerValue == TEXT("hidden"))
        {
            Widget->SetVisibility(ESlateVisibility::Hidden);
        }
        else if (LowerValue == TEXT("collapsed"))
        {
            Widget->SetVisibility(ESlateVisibility::Collapsed);
        }
        else if (LowerValue == TEXT("hittestinvisible"))
        {
            Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
    }

    if (const FString* ColorAndOpacityValue = Node.Attributes.Find(TEXT("ColorAndOpacity")))
    {
        FLinearColor Color;
        if (UXmlDslParser::ParseColor(*ColorAndOpacityValue, Color))
        {
            if (UXmlTextBlock* TextBlock = Cast<UXmlTextBlock>(Widget))
            {
                TextBlock->SetXmlColor(Color);
            }
            else if (UXmlImage* Image = Cast<UXmlImage>(Widget))
            {
                Image->Color = Color;
            }
            else if (UXmlProgressBar* ProgressBar = Cast<UXmlProgressBar>(Widget))
            {
                ProgressBar->FillColor = Color;
            }
            else if (UXmlButton* Button = Cast<UXmlButton>(Widget))
            {
                Button->ButtonColor = Color;
            }
        }
    }

    if (const FString* RenderOpacityValue = Node.Attributes.Find(TEXT("RenderOpacity")))
    {
        float Opacity = 1.f;
        if (UXmlDslParser::ParseFloat(*RenderOpacityValue, Opacity))
        {
            Widget->SetRenderOpacity(Opacity);
        }
    }
}

// Border owns Padding as a widget property; writing it into a parent slot would duplicate it on export.
static const FString* FindSlotPadding(const FXmlNodeDesc& Node)
{
    return Node.Tag == TEXT("Border") ? nullptr : Node.Attributes.Find(TEXT("Padding"));
}

void UXmlBuilder::ApplyXmlPanelSlotAttrs(UXmlPanelSlot* Slot, const FXmlNodeDesc& Node)
{
    if (const FString* PaddingValue = FindSlotPadding(Node))
    {
        FMargin Padding;
        if (UXmlDslParser::ParseMargin(*PaddingValue, Padding))
        {
            Slot->Padding = Padding;
        }
    }
    if (const FString* HAlignValue = Node.Attributes.Find(TEXT("HAlign")))
    {
        EHorizontalAlignment HAlign = HAlign_Fill;
        if (UXmlDslParser::ParseHAlign(*HAlignValue, HAlign))
        {
            Slot->HAlign = HAlign;
        }
    }
    if (const FString* VAlignValue = Node.Attributes.Find(TEXT("VAlign")))
    {
        EVerticalAlignment VAlign = VAlign_Fill;
        if (UXmlDslParser::ParseVAlign(*VAlignValue, VAlign))
        {
            Slot->VAlign = VAlign;
        }
    }
    if (const FString* SizeParamValue = Node.Attributes.Find(TEXT("SizeParam")))
    {
        ESlateSizeRule::Type SizeRule = ESlateSizeRule::Automatic;
        if (UXmlDslParser::ParseSizeRule(*SizeParamValue, SizeRule))
        {
            Slot->SizeParam.SizeRule = SizeRule;
        }
    }
}

void UXmlBuilder::ApplyOverlaySlotAttrs(UOverlaySlot* Slot, const FXmlNodeDesc& Node)
{
    if (const FString* PaddingValue = FindSlotPadding(Node))
    {
        FMargin Padding;
        if (UXmlDslParser::ParseMargin(*PaddingValue, Padding)) { Slot->SetPadding(Padding); }
    }
    EHorizontalAlignment HAlign = HAlign_Fill;
    if (UXmlDslParser::ParseHAlign(Node.Attributes.FindRef(TEXT("HAlign")), HAlign)) { Slot->SetHorizontalAlignment(HAlign); }
    EVerticalAlignment VAlign = VAlign_Fill;
    if (UXmlDslParser::ParseVAlign(Node.Attributes.FindRef(TEXT("VAlign")), VAlign)) { Slot->SetVerticalAlignment(VAlign); }
}

// File-local template (kept out of the header): merges the byte-identical
// UVerticalBoxSlot / UHorizontalBoxSlot / UScrollBoxSlot attribute logic.
template<typename TSlot>
static void ApplyBoxSlotAttrs(TSlot* Slot, const FXmlNodeDesc& Node)
{
    if (const FString* PaddingValue = FindSlotPadding(Node))
    {
        FMargin Padding;
        if (UXmlDslParser::ParseMargin(*PaddingValue, Padding)) { Slot->SetPadding(Padding); }
    }
    if (const FString* HAlignValue = Node.Attributes.Find(TEXT("HAlign")))
    {
        EHorizontalAlignment HAlign;
        if (UXmlDslParser::ParseHAlign(*HAlignValue, HAlign)) { Slot->SetHorizontalAlignment(HAlign); }
    }
    if (const FString* VAlignValue = Node.Attributes.Find(TEXT("VAlign")))
    {
        EVerticalAlignment VAlign;
        if (UXmlDslParser::ParseVAlign(*VAlignValue, VAlign)) { Slot->SetVerticalAlignment(VAlign); }
    }
    if (const FString* SizeParamValue = Node.Attributes.Find(TEXT("SizeParam")))
    {
        ESlateSizeRule::Type SizeRule;
        if (UXmlDslParser::ParseSizeRule(*SizeParamValue, SizeRule)) { Slot->SetSize(SizeRule); }
    }
}

void UXmlBuilder::ApplyWrapBoxSlotAttrs(UWrapBoxSlot* Slot, const FXmlNodeDesc& Node)
{
    if (const FString* PaddingValue = FindSlotPadding(Node))
    {
        FMargin Padding;
        if (UXmlDslParser::ParseMargin(*PaddingValue, Padding)) { Slot->SetPadding(Padding); }
    }
    if (const FString* HAlignValue = Node.Attributes.Find(TEXT("HAlign")))
    {
        EHorizontalAlignment HAlign;
        if (UXmlDslParser::ParseHAlign(*HAlignValue, HAlign)) { Slot->SetHorizontalAlignment(HAlign); }
    }
    if (const FString* VAlignValue = Node.Attributes.Find(TEXT("VAlign")))
    {
        EVerticalAlignment VAlign;
        if (UXmlDslParser::ParseVAlign(*VAlignValue, VAlign)) { Slot->SetVerticalAlignment(VAlign); }
    }
}

void UXmlBuilder::ApplyUniformGridSlotAttrs(UUniformGridSlot* Slot, const FXmlNodeDesc& Node)
{
    int32 Row = 0, Column = 0;
    if (const FString* RowValue = Node.Attributes.Find(TEXT("Row"))) { UXmlDslParser::ParseInt(*RowValue, Row); }
    if (const FString* ColumnValue = Node.Attributes.Find(TEXT("Column"))) { UXmlDslParser::ParseInt(*ColumnValue, Column); }
    Slot->SetRow(Row);
    Slot->SetColumn(Column);
    // UE 5.5's UUniformGridSlot has no SetPadding; only alignment is set (slot padding is controlled by the panel SlotPadding).
    if (const FString* HAlignValue = Node.Attributes.Find(TEXT("HAlign")))
    {
        EHorizontalAlignment HAlign;
        if (UXmlDslParser::ParseHAlign(*HAlignValue, HAlign)) { Slot->SetHorizontalAlignment(HAlign); }
    }
    if (const FString* VAlignValue = Node.Attributes.Find(TEXT("VAlign")))
    {
        EVerticalAlignment VAlign;
        if (UXmlDslParser::ParseVAlign(*VAlignValue, VAlign)) { Slot->SetVerticalAlignment(VAlign); }
    }
}

void UXmlBuilder::ApplyCanvasSlotAttrs(UCanvasPanelSlot* Slot, const FXmlNodeDesc& Node)
{
    if (const FString* PositionValue = Node.Attributes.Find(TEXT("Position")))
    {
        FVector2D Position;
        if (UXmlDslParser::ParseVector2D(*PositionValue, Position)) { Slot->SetPosition(Position); }
    }
    if (const FString* SizeValue = Node.Attributes.Find(TEXT("Size")))
    {
        FVector2D Size;
        if (UXmlDslParser::ParseVector2D(*SizeValue, Size)) { Slot->SetSize(Size); }
    }
    if (const FString* AnchorsValue = Node.Attributes.Find(TEXT("Anchors")))
    {
        TArray<FString> Parts;
        AnchorsValue->TrimStartAndEnd().ParseIntoArray(Parts, TEXT(","), true);
        FAnchors Anchors;
        if (Parts.Num() == 2)
        {
            FVector2D Value;
            if (UXmlDslParser::ParseVector2D(*AnchorsValue, Value))
            {
                Anchors.Minimum = Value;
                Anchors.Maximum = Value;
                Slot->SetAnchors(Anchors);
            }
        }
        else if (Parts.Num() == 4)
        {
            float MinX = 0.f, MinY = 0.f, MaxX = 0.f, MaxY = 0.f;
            if (UXmlDslParser::ParseFloat(Parts[0], MinX) && UXmlDslParser::ParseFloat(Parts[1], MinY)
                && UXmlDslParser::ParseFloat(Parts[2], MaxX) && UXmlDslParser::ParseFloat(Parts[3], MaxY))
            {
                Anchors.Minimum = FVector2D(MinX, MinY);
                Anchors.Maximum = FVector2D(MaxX, MaxY);
                Slot->SetAnchors(Anchors);
            }
        }
    }
    if (const FString* AlignmentValue = Node.Attributes.Find(TEXT("Alignment")))
    {
        FVector2D Alignment;
        if (UXmlDslParser::ParseVector2D(*AlignmentValue, Alignment)) { Slot->SetAlignment(Alignment); }
    }
    if (const FString* ZOrderValue = Node.Attributes.Find(TEXT("ZOrder")))
    {
        int32 ZOrder = 0;
        if (UXmlDslParser::ParseInt(*ZOrderValue, ZOrder)) { Slot->SetZOrder(ZOrder); }
    }
    if (const FString* AutoSizeValue = Node.Attributes.Find(TEXT("AutoSize")))
    {
        const FString LowerValue = AutoSizeValue->ToLower();
        if (LowerValue == TEXT("true")) { Slot->SetAutoSize(true); }
        else if (LowerValue == TEXT("false")) { Slot->SetAutoSize(false); }
    }
}

void UXmlBuilder::ApplyBorderSlotAttrs(UBorderSlot* Slot, const FXmlNodeDesc& Node)
{
    // UBorderSlot::SetPadding syncs back into UBorder::Padding; OnSlotAdded already copied it from the Border.
    if (const FString* HAlignValue = Node.Attributes.Find(TEXT("HAlign")))
    {
        EHorizontalAlignment HAlign;
        if (UXmlDslParser::ParseHAlign(*HAlignValue, HAlign)) { Slot->SetHorizontalAlignment(HAlign); }
    }
    if (const FString* VAlignValue = Node.Attributes.Find(TEXT("VAlign")))
    {
        EVerticalAlignment VAlign;
        if (UXmlDslParser::ParseVAlign(*VAlignValue, VAlign)) { Slot->SetVerticalAlignment(VAlign); }
    }
}

void UXmlBuilder::ApplySlotAttributes(UPanelSlot* Slot, const FXmlNodeDesc& Node)
{
    if (UXmlPanelSlot* XmlSlot = Cast<UXmlPanelSlot>(Slot)) { ApplyXmlPanelSlotAttrs(XmlSlot, Node); }
    else if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(Slot)) { ApplyOverlaySlotAttrs(OverlaySlot, Node); }
    else if (UVerticalBoxSlot* VBoxSlot = Cast<UVerticalBoxSlot>(Slot)) { ApplyBoxSlotAttrs(VBoxSlot, Node); }
    else if (UHorizontalBoxSlot* HBoxSlot = Cast<UHorizontalBoxSlot>(Slot)) { ApplyBoxSlotAttrs(HBoxSlot, Node); }
    else if (UScrollBoxSlot* ScrollSlot = Cast<UScrollBoxSlot>(Slot)) { ApplyBoxSlotAttrs(ScrollSlot, Node); }
    else if (UWrapBoxSlot* WrapSlot = Cast<UWrapBoxSlot>(Slot)) { ApplyWrapBoxSlotAttrs(WrapSlot, Node); }
    else if (UUniformGridSlot* GridSlot = Cast<UUniformGridSlot>(Slot)) { ApplyUniformGridSlotAttrs(GridSlot, Node); }
    else if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot)) { ApplyCanvasSlotAttrs(CanvasSlot, Node); }
    else if (UBorderSlot* BorderSlot = Cast<UBorderSlot>(Slot)) { ApplyBorderSlotAttrs(BorderSlot, Node); }
}

void UXmlBuilder::ParseBrushFromString(FSlateBrush& OutBrush, const FString& Value)
{
    // "→" separates an optional resource type hint from the object path, e.g. "Texture2D→/Game/UI/Tex.Tex".
    const int32 ArrowIndex = Value.Find(TEXT("→"));
    if (ArrowIndex != INDEX_NONE)
    {
        const FString ObjectPath = Value.RightChop(ArrowIndex + 1).TrimStartAndEnd();
        if (UObject* Resource = LoadObject<UObject>(nullptr, *ObjectPath))
        {
            OutBrush.SetResourceObject(Resource);
            OutBrush.DrawAs = ESlateBrushDrawType::Image;
            if (UTexture2D* Tex = Cast<UTexture2D>(Resource))
            {
                OutBrush.ImageSize = FVector2D(static_cast<float>(Tex->GetSizeX()), static_cast<float>(Tex->GetSizeY()));
            }
            else
            {
                // Placeholder default until the referenced asset provides a size.
                OutBrush.ImageSize = FVector2D(32.f, 32.f);
            }
        }
        return;
    }

    FLinearColor Color;
    if (UXmlDslParser::ParseColor(Value, Color))
    {
        OutBrush.TintColor = Color;
        OutBrush.DrawAs = ESlateBrushDrawType::Image;
        // Placeholder default until the referenced asset/image provides a size.
        OutBrush.ImageSize = FVector2D(32.f, 32.f);
    }
}
