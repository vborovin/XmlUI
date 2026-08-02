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
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "XmlButton.h"
#include "XmlDslParser.h"
#include "XmlPanel.h"
#include "XmlWidget.h"
#include "Misc/FileHelper.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "UObject/UObjectGlobals.h"
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

    UWidget* Root = BuildNode(Owner->WidgetTree, RootDesc, OutError);
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
static UWidget* TryCreateMappedWidget(UWidgetTree* InTree, const FXmlNodeDesc& InNode, UClass* InExpectedClass, bool& bOutHasMapping, FString& OutError, const TMap<FString, FString>* InWidgetClassMap)
{
    bOutHasMapping = false;
    const FString* ClassPath = InWidgetClassMap ? InWidgetClassMap->Find(InNode.Tag) : nullptr;
    if (!ClassPath || ClassPath->IsEmpty())
    {
        return nullptr;
    }
    bOutHasMapping = true;
    UClass* FoundClass = LoadClass<UWidget>(nullptr, **ClassPath);
    if (!FoundClass)
    {
        OutError += FString::Printf(TEXT("XmlUI: WidgetClassMap class '%s' for tag '%s' could not be loaded\n"), **ClassPath, *InNode.Tag);
        return nullptr;
    }
    if (!FoundClass->IsChildOf(InExpectedClass))
    {
        OutError += FString::Printf(TEXT("XmlUI: WidgetClassMap class '%s' for tag '%s' is not a %s subclass\n"), **ClassPath, *InNode.Tag, *InExpectedClass->GetName());
        return nullptr;
    }
    return InTree->ConstructWidget<UWidget>(FoundClass, FName(*InNode.Name));
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
    if (bHasFontSize)
    {
        TextBlock->SetFont(FSlateFontInfo(UWidget::GetDefaultFontName(), FontSize));
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
    Image->SetBrush(InBrush);
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
    // UE 5.5's UButton has no SetContentPadding API (content padding lives in the button style).
    if (InNode.Attributes.Contains(TEXT("Text")) || InNode.Attributes.Contains(TEXT("TextColor")))
    {
        OutError += FString::Printf(TEXT("XmlUI: mapped Button '%s': Text/TextColor are not applied to mapped UButton classes (use a child Text node)\n"), *InNode.Name);
    }
}

static void ApplyMappedProgressBar(FString& OutError, UWidget* InWidget, float InPercent, const FLinearColor& InFillColor)
{
    UProgressBar* ProgressBar = Cast<UProgressBar>(InWidget);
    if (!ProgressBar)
    {
        OutError += FString::Printf(TEXT("XmlUI: mapped ProgressBar class '%s' is not a UProgressBar subclass\n"), *InWidget->GetClass()->GetName());
        return;
    }
    ProgressBar->SetPercent(InPercent);
    ProgressBar->SetFillColorAndOpacity(InFillColor);
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
    for (const FXmlNodeDesc& ChildNode : InNode.Children)
    {
        UWidget* ChildWidget = BuildNodeInternal(InTree, ChildNode, OutError, InWidgetClassMap);
        if (!ChildWidget)
        {
            continue;
        }
        UPanelSlot* Slot = InWrapBox->AddChild(ChildWidget);
        ApplySlotAttributes(Slot, ChildNode);
    }
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
    for (const FXmlNodeDesc& ChildNode : InNode.Children)
    {
        UWidget* ChildWidget = BuildNodeInternal(InTree, ChildNode, OutError, InWidgetClassMap);
        if (!ChildWidget)
        {
            continue;
        }
        UPanelSlot* Slot = InGrid->AddChild(ChildWidget);
        ApplySlotAttributes(Slot, ChildNode);
    }
    ApplyCommonAttributes(InGrid, InNode);
}

UWidget* UXmlBuilder::BuildNodeInternal(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError, const TMap<FString, FString>* InWidgetClassMap)
{
    if (!Tree)
    {
        OutError += TEXT("XmlUI: Widget tree is null\n");
        return nullptr;
    }

    UWidget* Widget = nullptr;

    if (Node.Tag == TEXT("XmlUI") || Node.Tag == TEXT("Vertical") || Node.Tag == TEXT("Horizontal"))
    {
        bool bHasMapping = false;
        UWidget* MappedWidget = TryCreateMappedWidget(Tree, Node,
            (Node.Tag == TEXT("Horizontal")) ? UHorizontalBox::StaticClass() : UVerticalBox::StaticClass(),
            bHasMapping, OutError, InWidgetClassMap);
        if (bHasMapping)
        {
            if (!MappedWidget)
            {
                return nullptr;
            }
            UPanelWidget* Panel = Cast<UPanelWidget>(MappedWidget);

            for (const FXmlNodeDesc& ChildNode : Node.Children)
            {
                UWidget* ChildWidget = BuildNodeInternal(Tree, ChildNode, OutError, InWidgetClassMap);
                if (!ChildWidget)
                {
                    continue;
                }

                UPanelSlot* Slot = Panel->AddChild(ChildWidget);
                ApplySlotAttributes(Slot, ChildNode);
            }

            ApplyCommonAttributes(Panel, Node);
            return Panel;
        }
        else
        {
            UXmlPanel* Panel = Tree->ConstructWidget<UXmlPanel>(UXmlPanel::StaticClass(), FName(*Node.Name));
            Panel->Orientation = (Node.Tag == TEXT("Horizontal")) ? Orient_Horizontal : Orient_Vertical;

            for (const FXmlNodeDesc& ChildNode : Node.Children)
            {
                UWidget* ChildWidget = BuildNodeInternal(Tree, ChildNode, OutError, InWidgetClassMap);
                if (!ChildWidget)
                {
                    continue;
                }

                UPanelSlot* Slot = Panel->AddChild(ChildWidget);
                ApplySlotAttributes(Slot, ChildNode);
            }

            Widget = Panel;
        }
    }
    else if (Node.Tag == TEXT("Text"))
    {
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
    }
    else if (Node.Tag == TEXT("Image"))
    {
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
    }
    else if (Node.Tag == TEXT("Button"))
    {
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
    }
    else if (Node.Tag == TEXT("Spacer"))
    {
        bool bHasMapping = false;
        UWidget* MappedWidget = TryCreateMappedWidget(Tree, Node, USpacer::StaticClass(), bHasMapping, OutError, InWidgetClassMap);
        if (bHasMapping)
        {
            if (!MappedWidget)
            {
                return nullptr;
            }
            float Size = 0.f;
            if (const FString* SizeValue = Node.Attributes.Find(TEXT("Size")))
            {
                UXmlDslParser::ParseFloat(*SizeValue, Size);
            }
            ApplyMappedSpacer(OutError, MappedWidget, Size);
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
    }
    else if (Node.Tag == TEXT("ProgressBar"))
    {
        bool bHasMapping = false;
        UWidget* MappedWidget = TryCreateMappedWidget(Tree, Node, UProgressBar::StaticClass(), bHasMapping, OutError, InWidgetClassMap);
        if (bHasMapping)
        {
            if (!MappedWidget)
            {
                return nullptr;
            }
            float Percent = 0.f;
            if (const FString* PercentValue = Node.Attributes.Find(TEXT("Percent")))
            {
                UXmlDslParser::ParseFloat(*PercentValue, Percent);
            }
            FLinearColor FillColor = FLinearColor::White;
            if (const FString* FillColorValue = Node.Attributes.Find(TEXT("FillColor")))
            {
                UXmlDslParser::ParseColor(*FillColorValue, FillColor);
            }
            ApplyMappedProgressBar(OutError, MappedWidget, Percent, FillColor);
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
    }
    else if (Node.Tag == TEXT("Overlay"))
    {
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
        for (const FXmlNodeDesc& Child : Node.Children)
        {
            UWidget* ChildWidget = BuildNodeInternal(Tree, Child, OutError, InWidgetClassMap);
            if (ChildWidget)
            {
                UPanelSlot* Slot = Overlay->AddChild(ChildWidget);
                ApplySlotAttributes(Slot, Child);
            }
        }
        Widget = Overlay;
    }
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
    else if (Node.Tag == TEXT("SizeBox"))
    {
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
    }
    else
    {
        OutError += FString::Printf(TEXT("XmlUI: Unknown tag '%s', skipped\n"), *Node.Tag);
        return nullptr;
    }

    if (Widget)
    {
        ApplyCommonAttributes(Widget, Node);
    }

    return Widget;
}

void UXmlBuilder::ApplyCommonAttributes(UWidget* Widget, const FXmlNodeDesc& Node)
{
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

void UXmlBuilder::ApplySlotAttributes(UPanelSlot* Slot, const FXmlNodeDesc& Node)
{
    if (UXmlPanelSlot* XmlSlot = Cast<UXmlPanelSlot>(Slot))
    {
        if (const FString* PaddingValue = Node.Attributes.Find(TEXT("Padding")))
        {
            FMargin Padding;
            if (UXmlDslParser::ParseMargin(*PaddingValue, Padding))
            {
                XmlSlot->Padding = Padding;
            }
        }
        if (const FString* HAlignValue = Node.Attributes.Find(TEXT("HAlign")))
        {
            EHorizontalAlignment HAlign = HAlign_Fill;
            if (UXmlDslParser::ParseHAlign(*HAlignValue, HAlign))
            {
                XmlSlot->HAlign = HAlign;
            }
        }
        if (const FString* VAlignValue = Node.Attributes.Find(TEXT("VAlign")))
        {
            EVerticalAlignment VAlign = VAlign_Fill;
            if (UXmlDslParser::ParseVAlign(*VAlignValue, VAlign))
            {
                XmlSlot->VAlign = VAlign;
            }
        }
        if (const FString* SizeParamValue = Node.Attributes.Find(TEXT("SizeParam")))
        {
            ESlateSizeRule::Type SizeRule = ESlateSizeRule::Automatic;
            if (UXmlDslParser::ParseSizeRule(*SizeParamValue, SizeRule))
            {
                XmlSlot->SizeParam.SizeRule = SizeRule;
            }
        }
    }
    else if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(Slot))
    {
        FMargin Padding;
        if (UXmlDslParser::ParseMargin(Node.Attributes.FindRef(TEXT("Padding")), Padding)) { OverlaySlot->SetPadding(Padding); }
        EHorizontalAlignment HAlign = HAlign_Fill;
        if (UXmlDslParser::ParseHAlign(Node.Attributes.FindRef(TEXT("HAlign")), HAlign)) { OverlaySlot->SetHorizontalAlignment(HAlign); }
        EVerticalAlignment VAlign = VAlign_Fill;
        if (UXmlDslParser::ParseVAlign(Node.Attributes.FindRef(TEXT("VAlign")), VAlign)) { OverlaySlot->SetVerticalAlignment(VAlign); }
    }
    else if (UVerticalBoxSlot* VBoxSlot = Cast<UVerticalBoxSlot>(Slot))
    {
        if (const FString* PaddingValue = Node.Attributes.Find(TEXT("Padding")))
        {
            FMargin Padding;
            if (UXmlDslParser::ParseMargin(*PaddingValue, Padding)) { VBoxSlot->SetPadding(Padding); }
        }
        if (const FString* HAlignValue = Node.Attributes.Find(TEXT("HAlign")))
        {
            EHorizontalAlignment HAlign;
            if (UXmlDslParser::ParseHAlign(*HAlignValue, HAlign)) { VBoxSlot->SetHorizontalAlignment(HAlign); }
        }
        if (const FString* VAlignValue = Node.Attributes.Find(TEXT("VAlign")))
        {
            EVerticalAlignment VAlign;
            if (UXmlDslParser::ParseVAlign(*VAlignValue, VAlign)) { VBoxSlot->SetVerticalAlignment(VAlign); }
        }
        if (const FString* SizeParamValue = Node.Attributes.Find(TEXT("SizeParam")))
        {
            ESlateSizeRule::Type SizeRule;
            if (UXmlDslParser::ParseSizeRule(*SizeParamValue, SizeRule)) { VBoxSlot->SetSize(SizeRule); }
        }
    }
    else if (UHorizontalBoxSlot* HBoxSlot = Cast<UHorizontalBoxSlot>(Slot))
    {
        if (const FString* PaddingValue = Node.Attributes.Find(TEXT("Padding")))
        {
            FMargin Padding;
            if (UXmlDslParser::ParseMargin(*PaddingValue, Padding)) { HBoxSlot->SetPadding(Padding); }
        }
        if (const FString* HAlignValue = Node.Attributes.Find(TEXT("HAlign")))
        {
            EHorizontalAlignment HAlign;
            if (UXmlDslParser::ParseHAlign(*HAlignValue, HAlign)) { HBoxSlot->SetHorizontalAlignment(HAlign); }
        }
        if (const FString* VAlignValue = Node.Attributes.Find(TEXT("VAlign")))
        {
            EVerticalAlignment VAlign;
            if (UXmlDslParser::ParseVAlign(*VAlignValue, VAlign)) { HBoxSlot->SetVerticalAlignment(VAlign); }
        }
        if (const FString* SizeParamValue = Node.Attributes.Find(TEXT("SizeParam")))
        {
            ESlateSizeRule::Type SizeRule;
            if (UXmlDslParser::ParseSizeRule(*SizeParamValue, SizeRule)) { HBoxSlot->SetSize(SizeRule); }
        }
    }
    else if (UWrapBoxSlot* WrapSlot = Cast<UWrapBoxSlot>(Slot))
    {
        if (const FString* PaddingValue = Node.Attributes.Find(TEXT("Padding")))
        {
            FMargin Padding;
            if (UXmlDslParser::ParseMargin(*PaddingValue, Padding)) { WrapSlot->SetPadding(Padding); }
        }
        if (const FString* HAlignValue = Node.Attributes.Find(TEXT("HAlign")))
        {
            EHorizontalAlignment HAlign;
            if (UXmlDslParser::ParseHAlign(*HAlignValue, HAlign)) { WrapSlot->SetHorizontalAlignment(HAlign); }
        }
        if (const FString* VAlignValue = Node.Attributes.Find(TEXT("VAlign")))
        {
            EVerticalAlignment VAlign;
            if (UXmlDslParser::ParseVAlign(*VAlignValue, VAlign)) { WrapSlot->SetVerticalAlignment(VAlign); }
        }
    }
    else if (UUniformGridSlot* GridSlot = Cast<UUniformGridSlot>(Slot))
    {
        int32 Row = 0, Column = 0;
        if (const FString* RowValue = Node.Attributes.Find(TEXT("Row"))) { UXmlDslParser::ParseInt(*RowValue, Row); }
        if (const FString* ColumnValue = Node.Attributes.Find(TEXT("Column"))) { UXmlDslParser::ParseInt(*ColumnValue, Column); }
        GridSlot->SetRow(Row);
        GridSlot->SetColumn(Column);
        // UE 5.5's UUniformGridSlot has no SetPadding; only alignment is set (slot padding is controlled by the panel SlotPadding).
        if (const FString* HAlignValue = Node.Attributes.Find(TEXT("HAlign")))
        {
            EHorizontalAlignment HAlign;
            if (UXmlDslParser::ParseHAlign(*HAlignValue, HAlign)) { GridSlot->SetHorizontalAlignment(HAlign); }
        }
        if (const FString* VAlignValue = Node.Attributes.Find(TEXT("VAlign")))
        {
            EVerticalAlignment VAlign;
            if (UXmlDslParser::ParseVAlign(*VAlignValue, VAlign)) { GridSlot->SetVerticalAlignment(VAlign); }
        }
    }
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
            // Placeholder default until the referenced asset/image provides a size.
            OutBrush.ImageSize = FVector2D(32.f, 32.f);
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
