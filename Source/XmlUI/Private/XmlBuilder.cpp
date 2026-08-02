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
#include "XmlButton.h"
#include "XmlDslParser.h"
#include "XmlPanel.h"
#include "XmlWidget.h"
#include "Misc/FileHelper.h"
#include "Styling/SlateBrush.h"
#include "UObject/UObjectGlobals.h"

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

UWidget* UXmlBuilder::BuildNode(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError)
{
    return BuildNodeInternal(Tree, Node, OutError);
}

UWidget* UXmlBuilder::BuildNodeInternal(UWidgetTree* Tree, const FXmlNodeDesc& Node, FString& OutError)
{
    if (!Tree)
    {
        OutError += TEXT("XmlUI: Widget tree is null\n");
        return nullptr;
    }

    UWidget* Widget = nullptr;

    if (Node.Tag == TEXT("XmlUI") || Node.Tag == TEXT("Vertical") || Node.Tag == TEXT("Horizontal"))
    {
        UXmlPanel* Panel = Tree->ConstructWidget<UXmlPanel>(UXmlPanel::StaticClass(), FName(*Node.Name));
        Panel->Orientation = (Node.Tag == TEXT("Horizontal")) ? Orient_Horizontal : Orient_Vertical;

        for (const FXmlNodeDesc& ChildNode : Node.Children)
        {
            UWidget* ChildWidget = BuildNodeInternal(Tree, ChildNode, OutError);
            if (!ChildWidget)
            {
                continue;
            }

            UPanelSlot* Slot = Panel->AddChild(ChildWidget);
            ApplySlotAttributes(Slot, ChildNode);
        }

        Widget = Panel;
    }
    else if (Node.Tag == TEXT("Text"))
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
    else if (Node.Tag == TEXT("Image"))
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
    else if (Node.Tag == TEXT("Button"))
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
            UWidget* ChildWidget = BuildNodeInternal(Tree, Node.Children[0], OutError);
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
    else if (Node.Tag == TEXT("Spacer"))
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
    else if (Node.Tag == TEXT("ProgressBar"))
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
    else if (Node.Tag == TEXT("Overlay"))
    {
        UOverlay* Overlay = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), FName(*Node.Name));
        for (const FXmlNodeDesc& Child : Node.Children)
        {
            UWidget* ChildWidget = BuildNodeInternal(Tree, Child, OutError);
            if (ChildWidget)
            {
                UPanelSlot* Slot = Overlay->AddChild(ChildWidget);
                ApplySlotAttributes(Slot, Child);
            }
        }
        Widget = Overlay;
    }
    else if (Node.Tag == TEXT("SizeBox"))
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
            UWidget* ChildWidget = BuildNodeInternal(Tree, Node.Children[0], OutError);
            if (ChildWidget) { SizeBox->AddChild(ChildWidget); }
        }
        Widget = SizeBox;
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
