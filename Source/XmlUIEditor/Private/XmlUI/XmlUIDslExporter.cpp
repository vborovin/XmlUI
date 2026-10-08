/* Widget Blueprint -> XmlUI DSL serializer. Mirrors FXmlUIBaker's bake direction in reverse; WidgetClassMap reverse lookup is applied first. */

#include "XmlUIDslExporter.h"

#include "XmlUIBaker.h"
#include "XmlUISettings.h"
#include "XmlWidgets/XmlButton.h"
#include "XmlWidgets/XmlPanel.h"
#include "XmlWidgets/XmlWidget.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/ContentWidget.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/MenuAnchor.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/TextWidgetTypes.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/Widget.h"
#include "DesktopPlatformModule.h"
#include "Engine/Texture2D.h"
#include "Framework/Text/TextLayout.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"
#include "Types/SlateEnums.h"
#include "ToolMenus.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

#define LOCTEXT_NAMESPACE "XmlUIDslExporter"

namespace
{
    struct FExportContext
    {
        const UWidgetBlueprint* BP = nullptr;
        const UXmlUISettings* Settings = nullptr;
        TArray<TPair<UClass*, FString>> ClassToTag;
        TArray<FString> Warnings;
    };

    // --- value formatting ---
    FString EscapeXml(const FString& In)
    {
        FString Out = In;
        Out.ReplaceInline(TEXT("&"), TEXT("&amp;"));
        Out.ReplaceInline(TEXT("<"), TEXT("&lt;"));
        Out.ReplaceInline(TEXT(">"), TEXT("&gt;"));
        Out.ReplaceInline(TEXT("\""), TEXT("&quot;"));
        Out.ReplaceInline(TEXT("'"), TEXT("&apos;"));
        return Out;
    }

    void AppendAttr(TArray<FString>& Out, const TCHAR* Key, const FString& Value)
    {
        Out.Add(FString::Printf(TEXT("%s=\"%s\""), Key, *EscapeXml(Value)));
    }

    FString ColorToHex(const FLinearColor& InColor)
    {
        const FColor Col = InColor.ToFColor(true);
        return FString::Printf(TEXT("#%02X%02X%02X%02X"), Col.A, Col.R, Col.G, Col.B);
    }

    FString MarginToString(const FMargin& InMargin)
    {
        if (InMargin.Left == InMargin.Right && InMargin.Top == InMargin.Bottom && InMargin.Left == InMargin.Top)
        {
            return FString::SanitizeFloat(InMargin.Left);
        }
        if (InMargin.Left == InMargin.Right && InMargin.Top == InMargin.Bottom)
        {
            return FString::SanitizeFloat(InMargin.Left) + TEXT(",") + FString::SanitizeFloat(InMargin.Top);
        }
        return FString::Printf(TEXT("%s,%s,%s,%s"),
            *FString::SanitizeFloat(InMargin.Left), *FString::SanitizeFloat(InMargin.Top),
            *FString::SanitizeFloat(InMargin.Right), *FString::SanitizeFloat(InMargin.Bottom));
    }

    FString Vector2DToString(const FVector2D& InValue)
    {
        return FString::SanitizeFloat(InValue.X) + TEXT(",") + FString::SanitizeFloat(InValue.Y);
    }

    FString VisibilityToString(ESlateVisibility InVisibility)
    {
        switch (InVisibility)
        {
        case ESlateVisibility::Hidden: return TEXT("hidden");
        case ESlateVisibility::Collapsed: return TEXT("collapsed");
        case ESlateVisibility::HitTestInvisible: return TEXT("hittestinvisible");
        case ESlateVisibility::SelfHitTestInvisible: return TEXT("hittestinvisible");
        default: return TEXT("visible");
        }
    }

    FString HAlignToString(EHorizontalAlignment InAlignment)
    {
        switch (InAlignment)
        {
        case HAlign_Center: return TEXT("center");
        case HAlign_Right: return TEXT("right");
        case HAlign_Fill: return TEXT("fill");
        default: return TEXT("left");
        }
    }

    FString VAlignToString(EVerticalAlignment InAlignment)
    {
        switch (InAlignment)
        {
        case VAlign_Center: return TEXT("center");
        case VAlign_Bottom: return TEXT("bottom");
        case VAlign_Fill: return TEXT("fill");
        default: return TEXT("top");
        }
    }

    FString SizeRuleToString(ESlateSizeRule::Type InRule)
    {
        return InRule == ESlateSizeRule::Fill ? TEXT("fill") : TEXT("auto");
    }

    FString JustificationToString(ETextJustify::Type InJustification)
    {
        switch (InJustification)
        {
        case ETextJustify::Center: return TEXT("center");
        case ETextJustify::Right: return TEXT("right");
        default: return TEXT("left");
        }
    }

    // UTextLayoutWidget::Justification is protected, so read it reflectively for engine TextBlock subclasses.
    ETextJustify::Type GetTextJustification(const UWidget* InWidget)
    {
        if (const FByteProperty* JustProp = FindFProperty<FByteProperty>(InWidget->GetClass(), TEXT("Justification")))
        {
            return static_cast<ETextJustify::Type>(JustProp->GetPropertyValue_InContainer(InWidget));
        }
        return ETextJustify::Left;
    }

    FString BrushToString(const FSlateBrush& InBrush)
    {
        UObject* Resource = InBrush.GetResourceObject();
        if (!Resource)
        {
            return FString();
        }
        FString Type = TEXT("Object");
        if (Resource->IsA<UTexture2D>())
        {
            Type = TEXT("Texture2D");
        }
        else if (Resource->IsA<UMaterialInterface>())
        {
            Type = TEXT("Material");
        }
        return FString::Printf(TEXT("%s\u2192%s"), *Type, *Resource->GetPathName());
    }

    // --- tag resolution (ResolveTag) ---
    // Priority 1: WidgetClassMap reverse lookup with exact class match, then the default table.
    bool ResolveTag(const UWidget* InWidget, bool bIsRoot, const FExportContext& InCtx, FString& OutTag)
    {
        const UClass* WidgetClass = InWidget->GetClass();
        int32 BestScore = TNumericLimits<int32>::Min();
        FString BestTag;
        for (const TPair<UClass*, FString>& Entry : InCtx.ClassToTag)
        {
            if (Entry.Key != WidgetClass)
            {
                continue;
            }
            int32 Score = 10;
            if (Entry.Value == TEXT("XmlUI"))
            {
                Score = bIsRoot ? 100 : 0;
            }
            else if (Entry.Value == TEXT("Vertical") || Entry.Value == TEXT("Horizontal"))
            {
                Score = 50;
            }
            if (Score > BestScore || (Score == BestScore && Entry.Value < BestTag))
            {
                BestScore = Score;
                BestTag = Entry.Value;
            }
        }
        if (!BestTag.IsEmpty())
        {
            OutTag = BestTag;
            return true;
        }

        if (WidgetClass->IsChildOf(UXmlTextBlock::StaticClass()))
        {
            OutTag = TEXT("Text");
        }
        else if (WidgetClass->IsChildOf(UXmlImage::StaticClass()))
        {
            OutTag = TEXT("Image");
        }
        else if (WidgetClass->IsChildOf(UXmlButton::StaticClass()))
        {
            OutTag = TEXT("Button");
        }
        else if (WidgetClass->IsChildOf(UXmlSpacer::StaticClass()))
        {
            OutTag = TEXT("Spacer");
        }
        else if (WidgetClass->IsChildOf(UXmlProgressBar::StaticClass()))
        {
            OutTag = TEXT("ProgressBar");
        }
        else if (WidgetClass->IsChildOf(UTextBlock::StaticClass()))
        {
            OutTag = TEXT("Text");
        }
        else if (WidgetClass->IsChildOf(UImage::StaticClass()))
        {
            OutTag = TEXT("Image");
        }
        else if (WidgetClass->IsChildOf(UButton::StaticClass()))
        {
            OutTag = TEXT("Button");
        }
        else if (WidgetClass->IsChildOf(UCheckBox::StaticClass()))
        {
            OutTag = TEXT("CheckBox");
        }
        else if (WidgetClass->IsChildOf(USpacer::StaticClass()))
        {
            OutTag = TEXT("Spacer");
        }
        else if (WidgetClass->IsChildOf(UProgressBar::StaticClass()))
        {
            OutTag = TEXT("ProgressBar");
        }
        else if (WidgetClass->IsChildOf(UVerticalBox::StaticClass()))
        {
            OutTag = bIsRoot ? TEXT("XmlUI") : TEXT("Vertical");
        }
        else if (WidgetClass->IsChildOf(UHorizontalBox::StaticClass()))
        {
            OutTag = TEXT("Horizontal");
        }
        else if (WidgetClass->IsChildOf(UOverlay::StaticClass()))
        {
            OutTag = TEXT("Overlay");
        }
        else if (WidgetClass->IsChildOf(USizeBox::StaticClass()))
        {
            OutTag = TEXT("SizeBox");
        }
        else if (WidgetClass->IsChildOf(UScaleBox::StaticClass()))
        {
            OutTag = TEXT("ScaleBox");
        }
        else if (WidgetClass->IsChildOf(UWrapBox::StaticClass()))
        {
            OutTag = TEXT("WrapBox");
        }
        else if (WidgetClass->IsChildOf(UUniformGridPanel::StaticClass()))
        {
            OutTag = TEXT("Grid");
        }
        else if (WidgetClass->IsChildOf(UScrollBox::StaticClass()))
        {
            OutTag = TEXT("ScrollBox");
        }
        else if (WidgetClass->IsChildOf(UCanvasPanel::StaticClass()))
        {
            OutTag = TEXT("Canvas");
        }
        else if (WidgetClass->IsChildOf(UMenuAnchor::StaticClass()))
        {
            OutTag = TEXT("MenuAnchor");
        }
        else if (WidgetClass->IsChildOf(UBorder::StaticClass()))
        {
            OutTag = TEXT("Border");
        }
        else if (WidgetClass->IsChildOf(UXmlPanel::StaticClass()))
        {
            const UXmlPanel* Panel = CastChecked<UXmlPanel>(InWidget);
            if (bIsRoot && Panel->Orientation == EOrientation::Orient_Vertical)
            {
                OutTag = TEXT("XmlUI");
            }
            else if (Panel->Orientation == EOrientation::Orient_Horizontal)
            {
                OutTag = TEXT("Horizontal");
            }
            else
            {
                OutTag = TEXT("Vertical");
            }
        }
        else if (WidgetClass->IsChildOf(UUserWidget::StaticClass()))
        {
            OutTag = TEXT("UserWidget");
        }
        else
        {
            return false;
        }
        return true;
    }

    UClass* GetNativeDefaultClassForTag(const FString& Tag)
    {
        if (Tag == TEXT("Text")) return UXmlTextBlock::StaticClass();
        if (Tag == TEXT("Image")) return UXmlImage::StaticClass();
        if (Tag == TEXT("Button")) return UXmlButton::StaticClass();
        if (Tag == TEXT("CheckBox")) return UCheckBox::StaticClass();
        if (Tag == TEXT("Spacer")) return UXmlSpacer::StaticClass();
        if (Tag == TEXT("ProgressBar")) return UXmlProgressBar::StaticClass();
        if (Tag == TEXT("XmlUI") || Tag == TEXT("Vertical") || Tag == TEXT("Horizontal")) return UXmlPanel::StaticClass();
        if (Tag == TEXT("Overlay")) return UOverlay::StaticClass();
        if (Tag == TEXT("SizeBox")) return USizeBox::StaticClass();
        if (Tag == TEXT("ScaleBox")) return UScaleBox::StaticClass();
        if (Tag == TEXT("WrapBox")) return UWrapBox::StaticClass();
        if (Tag == TEXT("Grid")) return UUniformGridPanel::StaticClass();
        if (Tag == TEXT("ScrollBox")) return UScrollBox::StaticClass();
        if (Tag == TEXT("Canvas")) return UCanvasPanel::StaticClass();
        if (Tag == TEXT("MenuAnchor")) return UMenuAnchor::StaticClass();
        if (Tag == TEXT("Border")) return UBorder::StaticClass();
        return nullptr;
    }

    void AppendExactClassAttr(TArray<FString>& Out, const UWidget* Widget, const FString& Tag)
    {
        // UserWidget already carries its exact native/generated class in WBP=.
        if (Tag == TEXT("UserWidget"))
        {
            return;
        }

        UClass* NativeDefault = GetNativeDefaultClassForTag(Tag);
        if (NativeDefault && Widget->GetClass() != NativeDefault)
        {
            AppendAttr(Out, TEXT("Class"), Widget->GetClass()->GetPathName());
        }
    }

    void AppendReflectedStyleAttr(TArray<FString>& Out, const UWidget* Widget)
    {
        const UObject* DefaultObject = Widget->GetClass()->GetDefaultObject();

        if (const FClassProperty* StyleClassProp = FindFProperty<FClassProperty>(Widget->GetClass(), TEXT("Style")))
        {
            UObject* StyleClassObject = StyleClassProp->GetObjectPropertyValue_InContainer(Widget);
            UObject* DefaultStyleClassObject = StyleClassProp->GetObjectPropertyValue_InContainer(DefaultObject);
            if (StyleClassObject && StyleClassObject != DefaultStyleClassObject)
            {
                AppendAttr(Out, TEXT("Style"), StyleClassObject->GetPathName());
            }
        }
        else if (const FObjectProperty* StyleObjectProp = FindFProperty<FObjectProperty>(Widget->GetClass(), TEXT("Style")))
        {
            UObject* StyleObject = StyleObjectProp->GetObjectPropertyValue_InContainer(Widget);
            UObject* DefaultStyleObject = StyleObjectProp->GetObjectPropertyValue_InContainer(DefaultObject);
            if (StyleObject && StyleObject != DefaultStyleObject)
            {
                AppendAttr(Out, TEXT("Style"), StyleObject->GetPathName());
            }
        }
    }

    // --- per-widget attribute export (Append*Attrs) ---
    void AppendTextAttrs(TArray<FString>& Out, const UWidget* Widget)
    {
        if (const UXmlTextBlock* XmlText = Cast<UXmlTextBlock>(Widget))
        {
            const UXmlTextBlock* Default = GetDefault<UXmlTextBlock>(Widget->GetClass());
            if (!XmlText->Text.IsEmpty()) AppendAttr(Out, TEXT("Text"), XmlText->Text.ToString());
            if (XmlText->FontSize != Default->FontSize) AppendAttr(Out, TEXT("FontSize"), FString::FromInt(XmlText->FontSize));
            if (XmlText->ArtFontSize != Default->ArtFontSize) AppendAttr(Out, TEXT("ArtFontSize"), FString::FromInt(XmlText->ArtFontSize));
            if (!XmlText->FontFamily.IsEmpty()) AppendAttr(Out, TEXT("FontFamily"), XmlText->FontFamily);
            if (XmlText->Color != Default->Color) AppendAttr(Out, TEXT("Color"), ColorToHex(XmlText->Color));
            if (XmlText->Justification != Default->Justification) AppendAttr(Out, TEXT("Justification"), JustificationToString(XmlText->Justification));
            if (XmlText->WrapTextAt != Default->WrapTextAt) AppendAttr(Out, TEXT("WrapTextAt"), FString::SanitizeFloat(XmlText->WrapTextAt));
            if (XmlText->ShadowColor != Default->ShadowColor) AppendAttr(Out, TEXT("ShadowColor"), ColorToHex(XmlText->ShadowColor));
            if (XmlText->ShadowOffset != Default->ShadowOffset) AppendAttr(Out, TEXT("ShadowOffset"), Vector2DToString(XmlText->ShadowOffset));
        }
        else if (const UTextBlock* EngineText = Cast<UTextBlock>(Widget))
        {
            const UTextBlock* Default = GetDefault<UTextBlock>(Widget->GetClass());
            if (!EngineText->GetText().IsEmpty()) AppendAttr(Out, TEXT("Text"), EngineText->GetText().ToString());
            if (EngineText->GetFont().Size != Default->GetFont().Size) AppendAttr(Out, TEXT("FontSize"), FString::FromInt(EngineText->GetFont().Size));
            if (const UObject* FontObject = EngineText->GetFont().FontObject)
            {
                // GetDefaultFontName() is a package path while GetPathName() appends ".ObjectName"; strip it before comparing.
                FString FontPackagePath = FontObject->GetPathName();
                if (const int32 DotIndex = FontPackagePath.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd); DotIndex != INDEX_NONE)
                {
                    FontPackagePath.LeftInline(DotIndex);
                }
                if (FontPackagePath != UWidget::GetDefaultFontName())
                {
                    // Reverse-lookup non-default fonts through FontFamilyMap (value = asset path -> key = Figma family name).
                    const FString FullFontPath = FontObject->GetPathName();
                    for (const TPair<FString, FString>& Pair : GetDefault<UXmlUISettings>()->FontFamilyMap)
                    {
                        if (Pair.Value == FullFontPath || Pair.Value == FontPackagePath)
                        {
                            AppendAttr(Out, TEXT("FontFamily"), Pair.Key);
                            break;
                        }
                    }
                }
            }
            if (FIntProperty* ArtFontProp = FindFProperty<FIntProperty>(Widget->GetClass(), TEXT("ArtFont")))
            {
                const int32 ArtFontSize = ArtFontProp->GetPropertyValue_InContainer(Widget);
                if (ArtFontSize != -1) AppendAttr(Out, TEXT("ArtFontSize"), FString::FromInt(ArtFontSize));
            }
            const FLinearColor Color = EngineText->GetColorAndOpacity().GetSpecifiedColor();
            if (Color != Default->GetColorAndOpacity().GetSpecifiedColor()) AppendAttr(Out, TEXT("Color"), ColorToHex(Color));
            if (GetTextJustification(EngineText) != GetTextJustification(Default)) AppendAttr(Out, TEXT("Justification"), JustificationToString(GetTextJustification(EngineText)));
            if (EngineText->GetWrapTextAt() != Default->GetWrapTextAt()) AppendAttr(Out, TEXT("WrapTextAt"), FString::SanitizeFloat(EngineText->GetWrapTextAt()));
            if (EngineText->GetShadowColorAndOpacity() != Default->GetShadowColorAndOpacity()) AppendAttr(Out, TEXT("ShadowColor"), ColorToHex(EngineText->GetShadowColorAndOpacity()));
            if (EngineText->GetShadowOffset() != Default->GetShadowOffset()) AppendAttr(Out, TEXT("ShadowOffset"), Vector2DToString(EngineText->GetShadowOffset()));
        }
    }

    static void AppendImageColorAttr(TArray<FString>& Out, const FSlateBrush& Brush, const FLinearColor& FallbackColor)
    {
        const FLinearColor Tint = Brush.TintColor.GetSpecifiedColor();
        if (Tint != FLinearColor::White)
        {
            AppendAttr(Out, TEXT("Color"), ColorToHex(Tint));
        }
        else if (FallbackColor != FLinearColor::White)
        {
            // Assets baked before the tint was mirrored into the brush keep the color here.
            AppendAttr(Out, TEXT("Color"), ColorToHex(FallbackColor));
        }
    }

    void AppendImageAttrs(TArray<FString>& Out, const UWidget* Widget)
    {
        if (const UXmlImage* XmlImage = Cast<UXmlImage>(Widget))
        {
            if (XmlImage->Brush.GetResourceObject())
            {
                AppendAttr(Out, TEXT("Brush"), BrushToString(XmlImage->Brush));
            }
            else
            {
                AppendImageColorAttr(Out, XmlImage->Brush, XmlImage->Color);
            }
            const UXmlImage* Default = GetDefault<UXmlImage>(Widget->GetClass());
            if (XmlImage->DesiredSize != Default->DesiredSize) AppendAttr(Out, TEXT("DesiredSize"), Vector2DToString(XmlImage->DesiredSize));
        }
        else if (const UImage* EngineImage = Cast<UImage>(Widget))
        {
            const FSlateBrush& Brush = EngineImage->GetBrush();
            if (Brush.GetResourceObject())
            {
                AppendAttr(Out, TEXT("Brush"), BrushToString(Brush));
                if (const UTexture2D* Tex = Cast<UTexture2D>(Brush.GetResourceObject()))
                {
                    const FVector2D RealSize(static_cast<float>(Tex->GetSizeX()), static_cast<float>(Tex->GetSizeY()));
                    const FVector2D ImageSize(Brush.ImageSize.X, Brush.ImageSize.Y);
                    if (ImageSize != RealSize)
                    {
                        // The brush image size differs from the texture's real size, so it was overridden via DesiredSize.
                        AppendAttr(Out, TEXT("DesiredSize"), Vector2DToString(ImageSize));
                    }
                }
            }
            else
            {
                AppendImageColorAttr(Out, Brush, EngineImage->GetColorAndOpacity());
            }
            // DesiredSizeOverride is applied to the Slate widget at runtime and is not serialized, so it cannot be read back.
        }
    }

    void AppendButtonAttrs(TArray<FString>& Out, const UWidget* Widget)
    {
        if (const UXmlButton* XmlButton = Cast<UXmlButton>(Widget))
        {
            const UXmlButton* Default = GetDefault<UXmlButton>(Widget->GetClass());
            if (!XmlButton->Text.IsEmpty()) AppendAttr(Out, TEXT("Text"), XmlButton->Text.ToString());
            if (XmlButton->ButtonColor != Default->ButtonColor) AppendAttr(Out, TEXT("ButtonColor"), ColorToHex(XmlButton->ButtonColor));
            if (XmlButton->TextColor != Default->TextColor) AppendAttr(Out, TEXT("TextColor"), ColorToHex(XmlButton->TextColor));
            if (!XmlButton->FontFamily.IsEmpty()) AppendAttr(Out, TEXT("FontFamily"), XmlButton->FontFamily);
            if (XmlButton->ContentPadding != Default->ContentPadding) AppendAttr(Out, TEXT("Padding"), MarginToString(XmlButton->ContentPadding));
        }
        else if (const UButton* EngineButton = Cast<UButton>(Widget))
        {
            const UButton* Default = GetDefault<UButton>(Widget->GetClass());
            if (EngineButton->GetColorAndOpacity() != Default->GetColorAndOpacity()) AppendAttr(Out, TEXT("ButtonColor"), ColorToHex(EngineButton->GetColorAndOpacity()));
            // Text/TextColor/Padding are not applied to mapped UButton classes by the baker.
        }
    }

    void AppendCheckBoxAttrs(TArray<FString>& Out, const UWidget* Widget)
    {
        const UCheckBox* CheckBox = Cast<UCheckBox>(Widget);
        if (!CheckBox)
        {
            return;
        }

        const UCheckBox* Default = GetDefault<UCheckBox>(Widget->GetClass());
        const ECheckBoxState State = CheckBox->GetCheckedState();
        if (State != Default->GetCheckedState())
        {
            const TCHAR* StateText =
                State == ECheckBoxState::Checked ? TEXT("checked") :
                State == ECheckBoxState::Undetermined ? TEXT("undetermined") :
                TEXT("unchecked");
            AppendAttr(Out, TEXT("CheckedState"), StateText);
        }
    }

    void AppendSpacerAttrs(TArray<FString>& Out, const UWidget* Widget)
    {
        if (const UXmlSpacer* XmlSpacer = Cast<UXmlSpacer>(Widget))
        {
            const UXmlSpacer* Default = GetDefault<UXmlSpacer>(Widget->GetClass());
            if (XmlSpacer->Size != Default->Size) AppendAttr(Out, TEXT("Size"), FString::SanitizeFloat(XmlSpacer->Size));
        }
        else if (const USpacer* EngineSpacer = Cast<USpacer>(Widget))
        {
            const USpacer* Default = GetDefault<USpacer>(Widget->GetClass());
            const FVector2D Size = EngineSpacer->GetSize();
            if (Size.X != Default->GetSize().X) AppendAttr(Out, TEXT("Size"), FString::SanitizeFloat(Size.X));
        }
    }

    void AppendProgressBarAttrs(TArray<FString>& Out, const UWidget* Widget)
    {
        if (const UXmlProgressBar* XmlBar = Cast<UXmlProgressBar>(Widget))
        {
            const UXmlProgressBar* Default = GetDefault<UXmlProgressBar>(Widget->GetClass());
            if (XmlBar->Percent != Default->Percent) AppendAttr(Out, TEXT("Percent"), FString::SanitizeFloat(XmlBar->Percent));
            if (XmlBar->FillColor != Default->FillColor) AppendAttr(Out, TEXT("FillColor"), ColorToHex(XmlBar->FillColor));
        }
        else if (const UProgressBar* EngineBar = Cast<UProgressBar>(Widget))
        {
            const UProgressBar* Default = GetDefault<UProgressBar>(Widget->GetClass());
            if (EngineBar->GetPercent() != Default->GetPercent()) AppendAttr(Out, TEXT("Percent"), FString::SanitizeFloat(EngineBar->GetPercent()));
            if (EngineBar->GetFillColorAndOpacity() != Default->GetFillColorAndOpacity()) AppendAttr(Out, TEXT("FillColor"), ColorToHex(EngineBar->GetFillColorAndOpacity()));
        }
    }

    void AppendSizeBoxAttrs(TArray<FString>& Out, const UWidget* Widget)
    {
        const USizeBox* SizeBox = Cast<USizeBox>(Widget);
        if (!SizeBox)
        {
            return;
        }
        if (SizeBox->IsWidthOverride()) AppendAttr(Out, TEXT("WidthOverride"), FString::SanitizeFloat(SizeBox->GetWidthOverride()));
        if (SizeBox->IsHeightOverride()) AppendAttr(Out, TEXT("HeightOverride"), FString::SanitizeFloat(SizeBox->GetHeightOverride()));
        if (SizeBox->IsMinDesiredWidthOverride()) AppendAttr(Out, TEXT("MinDesiredWidth"), FString::SanitizeFloat(SizeBox->GetMinDesiredWidth()));
        if (SizeBox->IsMinDesiredHeightOverride()) AppendAttr(Out, TEXT("MinDesiredHeight"), FString::SanitizeFloat(SizeBox->GetMinDesiredHeight()));
        if (SizeBox->IsMaxDesiredWidthOverride()) AppendAttr(Out, TEXT("MaxDesiredWidth"), FString::SanitizeFloat(SizeBox->GetMaxDesiredWidth()));
        if (SizeBox->IsMaxDesiredHeightOverride()) AppendAttr(Out, TEXT("MaxDesiredHeight"), FString::SanitizeFloat(SizeBox->GetMaxDesiredHeight()));
    }

    FString StretchToString(EStretch::Type InStretch)
    {
        switch (InStretch)
        {
        case EStretch::None: return TEXT("none");
        case EStretch::Fill: return TEXT("fill");
        case EStretch::ScaleToFit: return TEXT("scaletofit");
        case EStretch::ScaleToFitX: return TEXT("scaletofitx");
        case EStretch::ScaleToFitY: return TEXT("scaletofity");
        case EStretch::ScaleToFill: return TEXT("scaletofill");
        case EStretch::ScaleBySafeZone: return TEXT("scalebysafezone");
        case EStretch::UserSpecified: return TEXT("userspecified");
        case EStretch::UserSpecifiedWithClipping: return TEXT("userspecifiedwithclipping");
        default: return FString::FromInt(static_cast<int32>(InStretch));
        }
    }

    FString StretchDirectionToString(EStretchDirection::Type InDirection)
    {
        switch (InDirection)
        {
        case EStretchDirection::Both: return TEXT("both");
        case EStretchDirection::DownOnly: return TEXT("downonly");
        case EStretchDirection::UpOnly: return TEXT("uponly");
        default: return FString::FromInt(static_cast<int32>(InDirection));
        }
    }

    // Engine UScaleBox has no UserDesiredWidth/UserDesiredHeight/ContentScale properties; read them
    // reflectively so host ScaleBox subclasses that carry them round-trip, and skip when absent.
    void AppendReflectedScaleBoxAttr(TArray<FString>& Out, const UWidget* Widget, const TCHAR* PropertyName)
    {
        if (const FFloatProperty* FloatProp = FindFProperty<FFloatProperty>(Widget->GetClass(), PropertyName))
        {
            const float Value = FloatProp->GetPropertyValue_InContainer(Widget);
            if (Value != 0.f) AppendAttr(Out, PropertyName, FString::SanitizeFloat(Value));
        }
        else if (const FStructProperty* StructProp = FindFProperty<FStructProperty>(Widget->GetClass(), PropertyName))
        {
            if (StructProp->Struct->GetFName() == TEXT("Vector2D"))
            {
                const FVector2D Value = *StructProp->ContainerPtrToValuePtr<FVector2D>(Widget);
                if (Value != FVector2D::ZeroVector) AppendAttr(Out, PropertyName, Vector2DToString(Value));
            }
        }
    }

    void AppendScaleBoxAttrs(TArray<FString>& Out, const UWidget* Widget)
    {
        const UScaleBox* ScaleBox = Cast<UScaleBox>(Widget);
        if (!ScaleBox)
        {
            return;
        }
        AppendReflectedScaleBoxAttr(Out, Widget, TEXT("UserDesiredWidth"));
        AppendReflectedScaleBoxAttr(Out, Widget, TEXT("UserDesiredHeight"));
        AppendReflectedScaleBoxAttr(Out, Widget, TEXT("ContentScale"));
        const UScaleBox* Default = GetDefault<UScaleBox>(Widget->GetClass());
        if (ScaleBox->GetStretch() != Default->GetStretch()) AppendAttr(Out, TEXT("Stretch"), StretchToString(ScaleBox->GetStretch()));
        if (ScaleBox->GetStretchDirection() != Default->GetStretchDirection()) AppendAttr(Out, TEXT("StretchDirection"), StretchDirectionToString(ScaleBox->GetStretchDirection()));
    }

    void AppendWrapBoxAttrs(TArray<FString>& Out, const UWidget* Widget)
    {
        const UWrapBox* WrapBox = Cast<UWrapBox>(Widget);
        if (!WrapBox)
        {
            return;
        }
        if (WrapBox->UseExplicitWrapSize())
        {
            AppendAttr(Out, TEXT("WrapWidth"), FString::SanitizeFloat(WrapBox->GetWrapSize()));
        }
    }

    void AppendGridAttrs(TArray<FString>& Out, const UWidget* Widget)
    {
        const UUniformGridPanel* Grid = Cast<UUniformGridPanel>(Widget);
        if (!Grid)
        {
            return;
        }
        const UUniformGridPanel* Default = GetDefault<UUniformGridPanel>(Widget->GetClass());
        if (Grid->GetSlotPadding() != Default->GetSlotPadding()) AppendAttr(Out, TEXT("SlotPadding"), MarginToString(Grid->GetSlotPadding()));
    }

    static FString ClassPathToAssetPath(const FString& ClassPath)
    {
        // Blueprint generated class: strip the "_C" suffix to recover the asset path.
        return ClassPath.EndsWith(TEXT("_C")) ? ClassPath.LeftChop(2) : ClassPath;
    }

    void AppendUserWidgetAttrs(TArray<FString>& Out, const UWidget* Widget)
    {
        const UUserWidget* UserWidget = Cast<UUserWidget>(Widget);
        if (!UserWidget)
        {
            return;
        }
        const UClass* WidgetClass = UserWidget->GetClass();
        // The abstract base UUserWidget carries no referenced widget class, so no WBP attribute is emitted.
        if (WidgetClass == UUserWidget::StaticClass())
        {
            return;
        }
        AppendAttr(Out, TEXT("WBP"), ClassPathToAssetPath(WidgetClass->GetPathName()));
        const UUserWidget* Default = GetDefault<UUserWidget>(UserWidget->GetClass());
        if (const FTextProperty* TextProp = FindFProperty<FTextProperty>(UserWidget->GetClass(), TEXT("Text")))
        {
            const FText Text = TextProp->GetPropertyValue_InContainer(UserWidget);
            const FText DefaultText = TextProp->GetPropertyValue_InContainer(Default);
            if (!Text.IsEmpty() && !Text.EqualTo(DefaultText))
            {
                AppendAttr(Out, TEXT("Text"), Text.ToString());
            }
        }
        if (const FStructProperty* ColorProp = FindFProperty<FStructProperty>(UserWidget->GetClass(), TEXT("TextColor")))
        {
            if (ColorProp->Struct->GetFName() == TEXT("SlateColor"))
            {
                const FSlateColor* Color = ColorProp->ContainerPtrToValuePtr<FSlateColor>(UserWidget);
                const FSlateColor* DefaultColor = ColorProp->ContainerPtrToValuePtr<FSlateColor>(Default);
                if (*Color != *DefaultColor)
                {
                    AppendAttr(Out, TEXT("Color"), ColorToHex(Color->GetSpecifiedColor()));
                }
            }
        }
        if (const FIntProperty* FontProp = FindFProperty<FIntProperty>(UserWidget->GetClass(), TEXT("ArtFontSize")))
        {
            const int32 ArtFontSize = FontProp->GetPropertyValue_InContainer(UserWidget);
            const int32 DefaultArtFontSize = FontProp->GetPropertyValue_InContainer(Default);
            if (ArtFontSize != -1 && ArtFontSize != DefaultArtFontSize)
            {
                AppendAttr(Out, TEXT("ArtFontSize"), FString::FromInt(ArtFontSize));
            }
        }
        if (GetTextJustification(UserWidget) != GetTextJustification(Default))
        {
            AppendAttr(Out, TEXT("Justification"), JustificationToString(GetTextJustification(UserWidget)));
        }
    }

    void AppendMenuAnchorAttrs(TArray<FString>& Out, const UWidget* Widget)
    {
        const UMenuAnchor* Anchor = Cast<UMenuAnchor>(Widget);
        if (!Anchor)
        {
            return;
        }
        if (Anchor->MenuClass)
        {
            AppendAttr(Out, TEXT("Menu"), ClassPathToAssetPath(Anchor->MenuClass->GetPathName()));
        }
    }

    void AppendBorderAttrs(TArray<FString>& Out, const UWidget* Widget)
    {
        const UBorder* Border = Cast<UBorder>(Widget);
        if (!Border)
        {
            return;
        }
        const UBorder* Default = GetDefault<UBorder>(Widget->GetClass());
        if (Border->GetBrushColor() != Default->GetBrushColor()) AppendAttr(Out, TEXT("BrushColor"), ColorToHex(Border->GetBrushColor()));
        if (Border->GetPadding() != Default->GetPadding()) AppendAttr(Out, TEXT("Padding"), MarginToString(Border->GetPadding()));
    }

    void AppendScrollBoxAttrs(TArray<FString>& Out, const UWidget* Widget)
    {
        const UScrollBox* ScrollBox = Cast<UScrollBox>(Widget);
        if (!ScrollBox)
        {
            return;
        }
        const UScrollBox* Default = GetDefault<UScrollBox>(Widget->GetClass());
        if (ScrollBox->GetOrientation() != Default->GetOrientation())
        {
            AppendAttr(Out, TEXT("Orientation"), ScrollBox->GetOrientation() == EOrientation::Orient_Horizontal ? TEXT("horizontal") : TEXT("vertical"));
        }
    }

    void AppendClassAttrs(TArray<FString>& Out, const UWidget* Widget, const FString& Tag)
    {
        if (Tag == TEXT("Text"))
        {
            AppendTextAttrs(Out, Widget);
        }
        else if (Tag == TEXT("Image"))
        {
            AppendImageAttrs(Out, Widget);
        }
        else if (Tag == TEXT("Button"))
        {
            AppendButtonAttrs(Out, Widget);
        }
        else if (Tag == TEXT("CheckBox"))
        {
            AppendCheckBoxAttrs(Out, Widget);
        }
        else if (Tag == TEXT("Spacer"))
        {
            AppendSpacerAttrs(Out, Widget);
        }
        else if (Tag == TEXT("ProgressBar"))
        {
            AppendProgressBarAttrs(Out, Widget);
        }
        else if (Tag == TEXT("SizeBox"))
        {
            AppendSizeBoxAttrs(Out, Widget);
        }
        else if (Tag == TEXT("ScaleBox"))
        {
            AppendScaleBoxAttrs(Out, Widget);
        }
        else if (Tag == TEXT("WrapBox"))
        {
            AppendWrapBoxAttrs(Out, Widget);
        }
        else if (Tag == TEXT("Grid"))
        {
            AppendGridAttrs(Out, Widget);
        }
        else if (Tag == TEXT("UserWidget"))
        {
            AppendUserWidgetAttrs(Out, Widget);
        }
        else if (Tag == TEXT("ScrollBox"))
        {
            AppendScrollBoxAttrs(Out, Widget);
        }
        else if (Tag == TEXT("MenuAnchor"))
        {
            AppendMenuAnchorAttrs(Out, Widget);
        }
        else if (Tag == TEXT("Border"))
        {
            AppendBorderAttrs(Out, Widget);
        }
    }

    // --- slot attribute export (AppendSlotAttrs) ---
    void AppendSlotAttrs(TArray<FString>& Out, const UWidget* Child)
    {
        const UPanelSlot* RawSlot = Child->Slot;
        if (!RawSlot)
        {
            return;
        }

        if (const UXmlPanelSlot* XmlSlot = Cast<UXmlPanelSlot>(RawSlot))
        {
            const UXmlPanelSlot* Default = GetDefault<UXmlPanelSlot>(RawSlot->GetClass());
            if (XmlSlot->Padding != Default->Padding) AppendAttr(Out, TEXT("Padding"), MarginToString(XmlSlot->Padding));
            if (XmlSlot->HAlign != Default->HAlign) AppendAttr(Out, TEXT("HAlign"), HAlignToString(XmlSlot->HAlign));
            if (XmlSlot->VAlign != Default->VAlign) AppendAttr(Out, TEXT("VAlign"), VAlignToString(XmlSlot->VAlign));
            if (XmlSlot->SizeParam.SizeRule != Default->SizeParam.SizeRule) AppendAttr(Out, TEXT("SizeParam"), SizeRuleToString(XmlSlot->SizeParam.SizeRule));
        }
        else if (const UVerticalBoxSlot* VBoxSlot = Cast<UVerticalBoxSlot>(RawSlot))
        {
            const UVerticalBoxSlot* Default = GetDefault<UVerticalBoxSlot>(RawSlot->GetClass());
            if (VBoxSlot->GetPadding() != Default->GetPadding()) AppendAttr(Out, TEXT("Padding"), MarginToString(VBoxSlot->GetPadding()));
            if (VBoxSlot->GetHorizontalAlignment() != Default->GetHorizontalAlignment()) AppendAttr(Out, TEXT("HAlign"), HAlignToString(VBoxSlot->GetHorizontalAlignment()));
            if (VBoxSlot->GetVerticalAlignment() != Default->GetVerticalAlignment()) AppendAttr(Out, TEXT("VAlign"), VAlignToString(VBoxSlot->GetVerticalAlignment()));
            if (VBoxSlot->GetSize().SizeRule != Default->GetSize().SizeRule) AppendAttr(Out, TEXT("SizeParam"), SizeRuleToString(VBoxSlot->GetSize().SizeRule));
        }
        else if (const UHorizontalBoxSlot* HBoxSlot = Cast<UHorizontalBoxSlot>(RawSlot))
        {
            const UHorizontalBoxSlot* Default = GetDefault<UHorizontalBoxSlot>(RawSlot->GetClass());
            if (HBoxSlot->GetPadding() != Default->GetPadding()) AppendAttr(Out, TEXT("Padding"), MarginToString(HBoxSlot->GetPadding()));
            if (HBoxSlot->GetHorizontalAlignment() != Default->GetHorizontalAlignment()) AppendAttr(Out, TEXT("HAlign"), HAlignToString(HBoxSlot->GetHorizontalAlignment()));
            if (HBoxSlot->GetVerticalAlignment() != Default->GetVerticalAlignment()) AppendAttr(Out, TEXT("VAlign"), VAlignToString(HBoxSlot->GetVerticalAlignment()));
            if (HBoxSlot->GetSize().SizeRule != Default->GetSize().SizeRule) AppendAttr(Out, TEXT("SizeParam"), SizeRuleToString(HBoxSlot->GetSize().SizeRule));
        }
        else if (const UScrollBoxSlot* ScrollSlot = Cast<UScrollBoxSlot>(RawSlot))
        {
            const UScrollBoxSlot* Default = GetDefault<UScrollBoxSlot>(RawSlot->GetClass());
            if (ScrollSlot->GetPadding() != Default->GetPadding()) AppendAttr(Out, TEXT("Padding"), MarginToString(ScrollSlot->GetPadding()));
            if (ScrollSlot->GetHorizontalAlignment() != Default->GetHorizontalAlignment()) AppendAttr(Out, TEXT("HAlign"), HAlignToString(ScrollSlot->GetHorizontalAlignment()));
            if (ScrollSlot->GetVerticalAlignment() != Default->GetVerticalAlignment()) AppendAttr(Out, TEXT("VAlign"), VAlignToString(ScrollSlot->GetVerticalAlignment()));
            if (ScrollSlot->GetSize().SizeRule != Default->GetSize().SizeRule) AppendAttr(Out, TEXT("SizeParam"), SizeRuleToString(ScrollSlot->GetSize().SizeRule));
        }
        else if (const UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(RawSlot))
        {
            const UOverlaySlot* Default = GetDefault<UOverlaySlot>(RawSlot->GetClass());
            if (OverlaySlot->GetPadding() != Default->GetPadding()) AppendAttr(Out, TEXT("Padding"), MarginToString(OverlaySlot->GetPadding()));
            if (OverlaySlot->GetHorizontalAlignment() != Default->GetHorizontalAlignment()) AppendAttr(Out, TEXT("HAlign"), HAlignToString(OverlaySlot->GetHorizontalAlignment()));
            if (OverlaySlot->GetVerticalAlignment() != Default->GetVerticalAlignment()) AppendAttr(Out, TEXT("VAlign"), VAlignToString(OverlaySlot->GetVerticalAlignment()));
        }
        else if (const UWrapBoxSlot* WrapSlot = Cast<UWrapBoxSlot>(RawSlot))
        {
            const UWrapBoxSlot* Default = GetDefault<UWrapBoxSlot>(RawSlot->GetClass());
            if (WrapSlot->GetPadding() != Default->GetPadding()) AppendAttr(Out, TEXT("Padding"), MarginToString(WrapSlot->GetPadding()));
            if (WrapSlot->GetHorizontalAlignment() != Default->GetHorizontalAlignment()) AppendAttr(Out, TEXT("HAlign"), HAlignToString(WrapSlot->GetHorizontalAlignment()));
            if (WrapSlot->GetVerticalAlignment() != Default->GetVerticalAlignment()) AppendAttr(Out, TEXT("VAlign"), VAlignToString(WrapSlot->GetVerticalAlignment()));
        }
        else if (const UUniformGridSlot* GridSlot = Cast<UUniformGridSlot>(RawSlot))
        {
            const UUniformGridSlot* Default = GetDefault<UUniformGridSlot>(RawSlot->GetClass());
            if (GridSlot->GetRow() != Default->GetRow()) AppendAttr(Out, TEXT("Row"), FString::FromInt(GridSlot->GetRow()));
            if (GridSlot->GetColumn() != Default->GetColumn()) AppendAttr(Out, TEXT("Column"), FString::FromInt(GridSlot->GetColumn()));
            if (GridSlot->GetHorizontalAlignment() != Default->GetHorizontalAlignment()) AppendAttr(Out, TEXT("HAlign"), HAlignToString(GridSlot->GetHorizontalAlignment()));
            if (GridSlot->GetVerticalAlignment() != Default->GetVerticalAlignment()) AppendAttr(Out, TEXT("VAlign"), VAlignToString(GridSlot->GetVerticalAlignment()));
        }
        else if (const UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(RawSlot))
        {
            const UCanvasPanelSlot* Default = GetDefault<UCanvasPanelSlot>(RawSlot->GetClass());
            if (CanvasSlot->GetPosition() != Default->GetPosition()) AppendAttr(Out, TEXT("Position"), Vector2DToString(CanvasSlot->GetPosition()));
            if (CanvasSlot->GetSize() != Default->GetSize()) AppendAttr(Out, TEXT("Size"), Vector2DToString(CanvasSlot->GetSize()));
            const FAnchors Anchors = CanvasSlot->GetAnchors();
            if (Anchors != Default->GetAnchors())
            {
                if (Anchors.Minimum == Anchors.Maximum)
                {
                    AppendAttr(Out, TEXT("Anchors"), Vector2DToString(Anchors.Minimum));
                }
                else
                {
                    AppendAttr(Out, TEXT("Anchors"), Vector2DToString(Anchors.Minimum) + TEXT(",") + Vector2DToString(Anchors.Maximum));
                }
            }
            if (CanvasSlot->GetAlignment() != Default->GetAlignment()) AppendAttr(Out, TEXT("Alignment"), Vector2DToString(CanvasSlot->GetAlignment()));
            if (CanvasSlot->GetZOrder() != Default->GetZOrder()) AppendAttr(Out, TEXT("ZOrder"), FString::FromInt(CanvasSlot->GetZOrder()));
            if (CanvasSlot->GetAutoSize() != Default->GetAutoSize()) AppendAttr(Out, TEXT("AutoSize"), CanvasSlot->GetAutoSize() ? TEXT("true") : TEXT("false"));
        }
        else if (const UBorderSlot* BorderSlot = Cast<UBorderSlot>(RawSlot))
        {
            const UBorderSlot* Default = GetDefault<UBorderSlot>(RawSlot->GetClass());
            if (BorderSlot->GetHorizontalAlignment() != Default->GetHorizontalAlignment()) AppendAttr(Out, TEXT("HAlign"), HAlignToString(BorderSlot->GetHorizontalAlignment()));
            if (BorderSlot->GetVerticalAlignment() != Default->GetVerticalAlignment()) AppendAttr(Out, TEXT("VAlign"), VAlignToString(BorderSlot->GetVerticalAlignment()));
        }
    }

    // --- serializer (ExportNode) ---
    FString ExportNode(const UWidget* InWidget, bool bIsRoot, FExportContext& InCtx, const FString& InIndent, const FString& InExtraAttr = FString())
    {
        FString Tag;
        if (!ResolveTag(InWidget, bIsRoot, InCtx, Tag))
        {
            InCtx.Warnings.Add(FString::Printf(TEXT("XmlUI: unsupported widget class '%s' ('%s'), node skipped"),
                *InWidget->GetClass()->GetName(), *InWidget->GetName()));
            return FString();
        }
        // XmlUI is the root alias of Vertical: a vertical root is exported as XmlUI (Horizontal is unchanged).
        if (bIsRoot && Tag == TEXT("Vertical"))
        {
            Tag = TEXT("XmlUI");
        }

        TArray<FString> Attrs;
        if (bIsRoot)
        {
            const UClass* ParentClass = InCtx.BP->ParentClass;
            if (ParentClass && ParentClass != UUserWidget::StaticClass())
            {
                AppendAttr(Attrs, TEXT("ParentClass"), ParentClass->GetPathName());
            }
        }
        AppendAttr(Attrs, TEXT("Name"), InWidget->GetName());
        AppendExactClassAttr(Attrs, InWidget, Tag);

        const UWidget* ClassDefault = GetDefault<UWidget>(InWidget->GetClass());
        AppendReflectedStyleAttr(Attrs, InWidget);
        if (InWidget->GetIsEnabled() != ClassDefault->GetIsEnabled())
        {
            AppendAttr(Attrs, TEXT("IsEnabled"), InWidget->GetIsEnabled() ? TEXT("true") : TEXT("false"));
        }
        if (InWidget->GetVisibility() != ClassDefault->GetVisibility())
        {
            AppendAttr(Attrs, TEXT("Visibility"), VisibilityToString(InWidget->GetVisibility()));
        }
        if (InWidget->GetRenderOpacity() != 1.f)
        {
            AppendAttr(Attrs, TEXT("RenderOpacity"), FString::SanitizeFloat(InWidget->GetRenderOpacity()));
        }

        AppendClassAttrs(Attrs, InWidget, Tag);

        if (!bIsRoot)
        {
            AppendSlotAttrs(Attrs, InWidget);
        }

        TArray<const UWidget*> Children;
        TMap<const UWidget*, FString> ChildSlotNames;
        if (const UPanelWidget* Panel = Cast<UPanelWidget>(InWidget))
        {
            const int32 NumChildren = Panel->GetChildrenCount();
            for (int32 Index = 0; Index < NumChildren; ++Index)
            {
                Children.Add(Panel->GetChildAt(Index));
            }
        }
        else if (const UContentWidget* ContentWidget = Cast<UContentWidget>(InWidget))
        {
            if (UWidget* Content = ContentWidget->GetContent())
            {
                Children.Add(Content);
            }
        }
        else if (const UUserWidget* UserWidget = Cast<UUserWidget>(InWidget))
        {
            // Named-slot content is held by the instance's bindings outside the outer tree hierarchy, so it is
            // unreachable from the root panel walk; emit it here with the slot name.
            if (const UWidgetBlueprintGeneratedClass* BGClass = Cast<UWidgetBlueprintGeneratedClass>(UserWidget->GetClass()))
            {
                for (const TPair<FName, FGuid>& NamedSlot : BGClass->NamedSlotsWithID)
                {
                    if (UWidget* Content = UserWidget->GetContentForSlot(NamedSlot.Key))
                    {
                        Children.Add(Content);
                        ChildSlotNames.Add(Content, NamedSlot.Key.ToString());
                    }
                }
            }
        }

        if (!InExtraAttr.IsEmpty())
        {
            Attrs.Add(InExtraAttr);
        }

        const FString AttrText = FString::Join(Attrs, TEXT(" "));
        const FString OpenTag = TEXT("<") + Tag + (Attrs.Num() > 0 ? TEXT(" ") + AttrText : FString());

        if (Children.Num() == 0)
        {
            return InIndent + OpenTag + TEXT("/>");
        }

        FString Result = InIndent + OpenTag + TEXT(">\n");
        const FString ChildIndent = InIndent + TEXT("  ");
        for (const UWidget* Child : Children)
        {
            const FString* SlotName = ChildSlotNames.Find(Child);
            const FString ExtraAttr = SlotName ? FString::Printf(TEXT("SlotName=\"%s\""), *EscapeXml(*SlotName)) : FString();
            const FString ChildNode = ExportNode(Child, false, InCtx, ChildIndent, ExtraAttr);
            if (!ChildNode.IsEmpty())
            {
                Result += ChildNode + TEXT("\n");
            }
        }
        Result += InIndent + TEXT("</") + Tag + TEXT(">");
        return Result;
    }

    bool ConvertUassetToGamePath(const FString& InFilePath, FString& OutAssetPath)
    {
        FString FullPath = FPaths::ConvertRelativePathToFull(InFilePath);
        FPaths::NormalizeFilename(FullPath);
        FString ContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
        FPaths::NormalizeFilename(ContentDir);
        if (!FullPath.StartsWith(ContentDir, ESearchCase::IgnoreCase))
        {
            return false;
        }
        FString Relative = FullPath.RightChop(ContentDir.Len());
        Relative = FPaths::GetBaseFilename(Relative, false);
        OutAssetPath = TEXT("/Game/") + Relative;
        return true;
    }

    // Mirrors FXmlUIBaker's asset name sanitizer so the console bake produces the same output path as the dialog.
    FString SanitizeBakeAssetName(const FString& InName)
    {
        FString Result = InName;
        for (TCHAR& Ch : Result)
        {
            if (Ch == TEXT(' ') || Ch == TEXT('-') || Ch == TEXT('.'))
            {
                Ch = TEXT('_');
            }
        }
        return Result;
    }
}

// --- dialog / console entry points ---
void FXmlUIDslExporter::RunExportFromDialog()
{
    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (!DesktopPlatform)
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoDesktopPlatform", "XmlUI: Failed to get the desktop platform module (IDesktopPlatform)"));
        return;
    }

    TArray<FString> OutFiles;
    if (!DesktopPlatform->OpenFileDialog(nullptr, TEXT("XmlUI: Select a Widget Blueprint asset"), FPaths::ProjectContentDir(), TEXT(""),
        TEXT("Widget Blueprint|*.uasset|All files|*.*"), EFileDialogFlags::None, OutFiles))
    {
        return;
    }
    if (OutFiles.Num() == 0)
    {
        return;
    }

    FString WbpAssetPath;
    if (!ConvertUassetToGamePath(OutFiles[0], WbpAssetPath))
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("NotInContent", "XmlUI: The selected asset is not inside the project Content directory: {0}"),
            FText::FromString(OutFiles[0])));
        return;
    }

    TArray<FString> OutSaveFiles;
    if (!DesktopPlatform->SaveFileDialog(nullptr, TEXT("XmlUI: Select the output DSL file"), FPaths::ProjectDir(), TEXT(""),
        TEXT("XmlUI DSL|*.xml|All files|*.*"), EFileDialogFlags::None, OutSaveFiles))
    {
        return;
    }
    if (OutSaveFiles.Num() == 0)
    {
        return;
    }

    const FString OutXmlPath = OutSaveFiles[0];
    if (ExportWbpToDslFile(WbpAssetPath, OutXmlPath))
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("ExportSucceed", "XmlUI: Export succeeded: {0}"), FText::FromString(OutXmlPath)));
    }
    else
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("ExportFailed", "XmlUI: Export failed: {0}"), FText::FromString(WbpAssetPath)));
    }
}

bool FXmlUIDslExporter::ExportWbpToDslFile(const FString& WbpAssetPath, const FString& OutXmlPath)
{
    UWidgetBlueprint* BP = Cast<UWidgetBlueprint>(StaticLoadObject(UWidgetBlueprint::StaticClass(), nullptr, *WbpAssetPath, nullptr, LOAD_NoWarn));
    if (!BP)
    {
        UE_LOG(LogTemp, Error, TEXT("XmlUI: Failed to load Widget Blueprint '%s'"), *WbpAssetPath);
        return false;
    }

    UWidgetTree* Tree = BP->WidgetTree;
    const UWidget* Root = Tree ? Tree->RootWidget : nullptr;
    if (!Root)
    {
        UE_LOG(LogTemp, Error, TEXT("XmlUI: Widget Blueprint '%s' has no root widget"), *WbpAssetPath);
        return false;
    }

    FExportContext Ctx;
    Ctx.BP = BP;
    Ctx.Settings = GetDefault<UXmlUISettings>();
    TMap<UClass*, FString> ClassToFirstTag;
    for (const TPair<FString, FString>& Pair : Ctx.Settings->WidgetClassMap)
    {
        if (Pair.Key == TEXT("XmlUI"))
        {
            // XmlUI is the root alias of Vertical and does not participate in reverse lookup.
            continue;
        }
        if (UClass* MappedClass = LoadClass<UWidget>(nullptr, *Pair.Value))
        {
            if (const FString* ExistingTag = ClassToFirstTag.Find(MappedClass))
            {
                UE_LOG(LogTemp, Warning, TEXT("XmlUI: WidgetClassMap maps class %s to multiple tags (%s, %s); exports use the lexicographically smaller one"),
                    *MappedClass->GetName(), **ExistingTag, *Pair.Key);
            }
            else
            {
                ClassToFirstTag.Emplace(MappedClass, Pair.Key);
            }
            Ctx.ClassToTag.Emplace(MappedClass, Pair.Key);
        }
    }

    const FString Body = ExportNode(Root, true, Ctx, FString());
    if (Body.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("XmlUI: Root widget '%s' of '%s' is not a supported XmlUI widget"),
            *Root->GetName(), *WbpAssetPath);
        return false;
    }

    for (const FString& Warning : Ctx.Warnings)
    {
        UE_LOG(LogTemp, Warning, TEXT("%s"), *Warning);
    }

    const FString FileContent = Body + TEXT("\n");
    if (!FFileHelper::SaveStringToFile(FileContent, *OutXmlPath, FFileHelper::EEncodingOptions::ForceUTF8))
    {
        UE_LOG(LogTemp, Error, TEXT("XmlUI: Failed to write DSL file '%s'"), *OutXmlPath);
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("XmlUI: Export succeeded: %s"), *OutXmlPath);
    return true;
}

static FAutoConsoleCommand GXmlUIExportWbpCommand(
    TEXT("XmlUI.ExportWbp"),
    TEXT("Export a Widget Blueprint to an XmlUI DSL file. Usage: XmlUI.ExportWbp Wbp=<asset path> Out=<output xml path>"),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        const FString Cmd = FString::Join(Args, TEXT(" "));
        FString WbpPath;
        FString OutPath;
        FParse::Value(*Cmd, TEXT("Wbp="), WbpPath);
        FParse::Value(*Cmd, TEXT("Out="), OutPath);
        if (WbpPath.IsEmpty() || OutPath.IsEmpty())
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI.ExportWbp: missing arguments; usage: XmlUI.ExportWbp Wbp=<asset path> Out=<output xml path>"));
            return;
        }
        if (FXmlUIDslExporter::ExportWbpToDslFile(WbpPath, OutPath))
        {
            UE_LOG(LogTemp, Log, TEXT("XmlUI.ExportWbp: export succeeded: %s"), *OutPath);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI.ExportWbp: export failed (see log for details)"));
        }
    }));

static FAutoConsoleCommand GXmlUIBakeDslCommand(
    TEXT("XmlUI.BakeDsl"),
    TEXT("Bake an XmlUI DSL file into a Widget Blueprint asset. Usage: XmlUI.BakeDsl File=<xml path> [Out=/Game/UI/W_Name]"),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        const FString Cmd = FString::Join(Args, TEXT(" "));
        FString FilePath;
        FString RequestedOutAssetPath;
        FParse::Value(*Cmd, TEXT("File="), FilePath);
        FParse::Value(*Cmd, TEXT("Out="), RequestedOutAssetPath);
        if (FilePath.IsEmpty())
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI.BakeDsl: missing arguments; usage: XmlUI.BakeDsl File=<xml path> [Out=/Game/UI/W_Name]"));
            return;
        }

        const FString OutAssetPath = RequestedOutAssetPath.IsEmpty()
            ? FString::Printf(TEXT("%s/WBP_%s"),
                *GetDefault<UXmlUISettings>()->BakedBlueprintOutputPath, *SanitizeBakeAssetName(FPaths::GetBaseFilename(FilePath)))
            : RequestedOutAssetPath;
        FString OutError;
        UWidgetBlueprint* BP = FXmlUIBaker::BakeDslToWidgetBlueprint(FilePath, OutAssetPath, OutError);
        if (BP)
        {
            UE_LOG(LogTemp, Log, TEXT("XmlUI.BakeDsl: bake succeeded: %s"), *OutAssetPath);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI.BakeDsl: bake failed: %s"), *OutError);
        }
    }));

#undef LOCTEXT_NAMESPACE
