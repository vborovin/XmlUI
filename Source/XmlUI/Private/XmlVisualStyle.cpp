#include "XmlVisualStyle.h"

#include "Components/Widget.h"
#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "Fonts/SlateFontInfo.h"
#include "UObject/UnrealType.h"
#include "XmlDslNode.h"
#include "XmlDslParser.h"
#include "XmlWidgets/XmlButton.h"
#include "XmlWidgets/XmlWidget.h"

namespace
{
    void CollectProperties(const FString& Tag, TArray<FName>& Out)
    {
        if (Tag == TEXT("Text"))
        {
            Out = { TEXT("Font"), TEXT("StrikeBrush"), TEXT("Margin"),
                TEXT("LineHeightPercentage"), TEXT("MinDesiredWidth"),
                TEXT("AutoWrapText"), TEXT("WrappingPolicy"),
                TEXT("TextTransformPolicy"), TEXT("OverflowPolicy") };
        }
        else if (Tag == TEXT("Button"))
        {
            Out = { TEXT("WidgetStyle"), TEXT("BackgroundColor"), TEXT("ColorAndOpacity") };
        }
        else if (Tag == TEXT("CheckBox"))
        {
            Out = { TEXT("WidgetStyle") };
        }
        else if (Tag == TEXT("ProgressBar"))
        {
            Out = { TEXT("WidgetStyle"), TEXT("BarFillType"), TEXT("BarFillStyle"), TEXT("IsMarquee") };
        }
        else if (Tag == TEXT("Image"))
        {
            Out = { TEXT("Brush") };
        }
        else if (Tag == TEXT("Border"))
        {
            Out = { TEXT("Background"), TEXT("BrushColor"), TEXT("ContentColorAndOpacity") };
        }
        Out.Add(TEXT("RenderTransform"));
        Out.Add(TEXT("RenderTransformPivot"));
        Out.Add(TEXT("Clipping"));
    }

    bool IsPersistentEditable(const FProperty* Property)
    {
        return Property && Property->HasAnyPropertyFlags(CPF_Edit)
            && !Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated);
    }

    void ApplyFontAliases(UWidget* Widget, const FXmlNodeDesc& Node)
    {
        if (Node.Tag != TEXT("Text") || !Node.Attributes.Contains(TEXT("Visual.Font")))
        {
            return;
        }
        FStructProperty* Property = FindFProperty<FStructProperty>(Widget->GetClass(), TEXT("Font"));
        if (!Property || !Property->Struct || Property->Struct->GetFName() != TEXT("SlateFontInfo"))
        {
            return;
        }
        FSlateFontInfo* Font = Property->ContainerPtrToValuePtr<FSlateFontInfo>(Widget);
        if (!Font)
        {
            return;
        }
        if (const FString* Size = Node.Attributes.Find(TEXT("FontSize")))
        {
            int32 Value = 0;
            if (UXmlDslParser::ParseInt(*Size, Value) && Value > 0)
            {
                Font->Size = Value;
            }
        }
        if (const FString* Face = Node.Attributes.Find(TEXT("Typeface")))
        {
            Font->TypefaceFontName = FName(**Face);
        }
        if (const FString* Path = Node.Attributes.Find(TEXT("FontPath")))
        {
            if (UFont* Asset = LoadObject<UFont>(nullptr, **Path))
            {
                Font->FontObject = Asset;
            }
        }
    }
}

bool FXmlVisualStyle::IsSupported(const FString& Tag, const FString& Property)
{
    TArray<FName> Names;
    CollectProperties(Tag, Names);
    return Names.Contains(FName(*Property));
}

bool FXmlVisualStyle::Apply(UWidget* Widget, const FXmlNodeDesc& Node, FString& OutError)
{
    if (!Widget)
    {
        return false;
    }

    TArray<FString> Keys;
    for (const TPair<FString, FString>& Attribute : Node.Attributes)
    {
        if (Attribute.Key.StartsWith(TEXT("Visual.")))
        {
            Keys.Add(Attribute.Key);
        }
    }
    Keys.Sort();

    for (const FString& Key : Keys)
    {
        const FString PropertyName = Key.RightChop(7);
        if (!IsSupported(Node.Tag, PropertyName))
        {
            OutError += FString::Printf(TEXT("XmlUI: visual style error: unsupported attribute '%s' on '%s'\n"),
                *Key, *Node.Name);
            return false;
        }
        FProperty* Property = FindFProperty<FProperty>(Widget->GetClass(), FName(*PropertyName));
        if (!IsPersistentEditable(Property))
        {
            OutError += FString::Printf(TEXT("XmlUI: visual style error: property '%s' not editable on '%s'\n"),
                *PropertyName, *Widget->GetClass()->GetPathName());
            return false;
        }
        void* Value = Property->ContainerPtrToValuePtr<void>(Widget);
        if (!Property->ImportText_Direct(*Node.Attributes.FindChecked(Key), Value, Widget, PPF_None))
        {
            OutError += FString::Printf(TEXT("XmlUI: visual style error: failed to import '%s' on '%s'\n"),
                *Key, *Node.Name);
            return false;
        }
        if (Key == TEXT("Visual.Font"))
        {
            if (UXmlTextBlock* Text = Cast<UXmlTextBlock>(Widget))
            {
                Text->bUseVisualFont = true;
            }
        }
        else if (Key == TEXT("Visual.WidgetStyle"))
        {
            if (UXmlButton* Button = Cast<UXmlButton>(Widget))
            {
                Button->bUseVisualWidgetStyle = true;
            }
            else if (UXmlProgressBar* Bar = Cast<UXmlProgressBar>(Widget))
            {
                Bar->bUseVisualWidgetStyle = true;
            }
        }
    }
    ApplyFontAliases(Widget, Node);

    // Direct reflected assignment updates the UPROPERTY but does not invoke the
    // UTextBlock font-change notification. Let CommonUI/UMG refresh its Slate font.
    if (Node.Attributes.Contains(TEXT("Visual.Font")))
    {
        if (UTextBlock* Text = Cast<UTextBlock>(Widget))
        {
            Text->SetFont(Text->GetFont());
        }
    }
    return true;
}

void FXmlVisualStyle::Export(const UWidget* Widget, const FString& Tag,
    TArray<TPair<FString, FString>>& OutAttributes)
{
    if (!Widget)
    {
        return;
    }
    const UObject* Default = Widget->GetClass()->GetDefaultObject();
    if (!Default)
    {
        return;
    }
    TArray<FName> Names;
    CollectProperties(Tag, Names);
    for (const FName Name : Names)
    {
        const FProperty* Property = FindFProperty<FProperty>(Widget->GetClass(), Name);
        if (!IsPersistentEditable(Property))
        {
            continue;
        }

        // Text and state brushes must be self-contained: a delta against the
        // native class default serializes e.g. Font as "(Size=20)" and loses
        // the font object, typeface, outline and rendering settings.
        const bool bSnapshot =
            (Tag == TEXT("Text") && Name == TEXT("Font"))
            || (Tag == TEXT("Button") && Name == TEXT("WidgetStyle"))
            || (Tag == TEXT("CheckBox") && Name == TEXT("WidgetStyle"))
            || (Tag == TEXT("ProgressBar") && Name == TEXT("WidgetStyle"))
            || (Tag == TEXT("Image") && Name == TEXT("Brush"))
            || (Tag == TEXT("Border") && Name == TEXT("Background"));
        if (!bSnapshot && Property->Identical_InContainer(Widget, Default))
        {
            continue;
        }

        FString Value;
        // No Delta: export a full Unreal property value, not only fields that
        // happen to differ from the source engine's native CDO.
        Property->ExportText_InContainer(0, Value, Widget, nullptr,
            const_cast<UWidget*>(Widget), PPF_None);
        OutAttributes.Emplace(FString(TEXT("Visual.")) + Name.ToString(), MoveTemp(Value));
    }
}
