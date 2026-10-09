#include "XmlDslValidator.h"

#include "Components/ScaleBox.h"
#include "Misc/PackageName.h"
#include "XmlDslParser.h"
#include "XmlVisualStyle.h"

namespace
{
    bool IsKnownTag(const FString& Tag)
    {
        static const TArray<FString> KnownTags =
        {
            TEXT("XmlUI"),
            TEXT("Vertical"),
            TEXT("Horizontal"),
            TEXT("Overlay"),
            TEXT("SizeBox"),
            TEXT("ScaleBox"),
            TEXT("WrapBox"),
            TEXT("Grid"),
            TEXT("ScrollBox"),
            TEXT("Canvas"),
            TEXT("MenuAnchor"),
            TEXT("Border"),
            TEXT("UserWidget"),
            TEXT("Text"),
            TEXT("Image"),
            TEXT("Button"),
            TEXT("CheckBox"),
            TEXT("Spacer"),
            TEXT("ProgressBar"),
        };
        return KnownTags.Contains(Tag);
    }

    void AddCommonAttributes(TSet<FString>& Allowed)
    {
        Allowed.Add(TEXT("Name"));
        Allowed.Add(TEXT("Class"));
        Allowed.Add(TEXT("Style"));
        Allowed.Add(TEXT("IsEnabled"));
        Allowed.Add(TEXT("Visibility"));
        Allowed.Add(TEXT("RenderOpacity"));
        Allowed.Add(TEXT("ColorAndOpacity"));
    }

    void AddTagAttributes(const FXmlNodeDesc& Node, const bool bIsRoot, TSet<FString>& Allowed)
    {
        const FString& Tag = Node.Tag;

        if (bIsRoot)
        {
            // ParentClass belongs to the Widget Blueprint document, not specifically
            // to the XmlUI/Vertical root tag. Exported WBPs may legitimately have
            // Horizontal, Overlay, Canvas, Button, etc. as their actual root widget.
            Allowed.Add(TEXT("ParentClass"));
        }

        if (Tag == TEXT("Text"))
        {
            Allowed.Add(TEXT("Text"));
            Allowed.Add(TEXT("FontSize"));
            Allowed.Add(TEXT("ArtFontSize"));
            Allowed.Add(TEXT("FontFamily"));
            Allowed.Add(TEXT("FontPath"));
            Allowed.Add(TEXT("Typeface"));
            Allowed.Add(TEXT("Color"));
            Allowed.Add(TEXT("Justification"));
            Allowed.Add(TEXT("WrapTextAt"));
            Allowed.Add(TEXT("ShadowColor"));
            Allowed.Add(TEXT("ShadowOffset"));
        }
        else if (Tag == TEXT("Image"))
        {
            Allowed.Add(TEXT("Brush"));
            Allowed.Add(TEXT("Color"));
            Allowed.Add(TEXT("DesiredSize"));
        }
        else if (Tag == TEXT("Button"))
        {
            Allowed.Add(TEXT("Text"));
            Allowed.Add(TEXT("ButtonColor"));
            Allowed.Add(TEXT("TextColor"));
            Allowed.Add(TEXT("FontFamily"));
            Allowed.Add(TEXT("Padding"));
        }
        else if (Tag == TEXT("CheckBox"))
        {
            Allowed.Add(TEXT("CheckedState"));
        }
        else if (Tag == TEXT("Spacer"))
        {
            Allowed.Add(TEXT("Size"));
        }
        else if (Tag == TEXT("ProgressBar"))
        {
            Allowed.Add(TEXT("Percent"));
            Allowed.Add(TEXT("FillColor"));
        }
        else if (Tag == TEXT("WrapBox"))
        {
            Allowed.Add(TEXT("WrapWidth"));
        }
        else if (Tag == TEXT("Grid"))
        {
            Allowed.Add(TEXT("Columns"));
            Allowed.Add(TEXT("SlotPadding"));
        }
        else if (Tag == TEXT("ScrollBox"))
        {
            Allowed.Add(TEXT("Orientation"));
        }
        else if (Tag == TEXT("UserWidget"))
        {
            Allowed.Add(TEXT("WBP"));
            Allowed.Add(TEXT("Text"));
            Allowed.Add(TEXT("Color"));
            Allowed.Add(TEXT("ArtFontSize"));
            Allowed.Add(TEXT("Justification"));
        }
        else if (Tag == TEXT("SizeBox"))
        {
            Allowed.Add(TEXT("WidthOverride"));
            Allowed.Add(TEXT("HeightOverride"));
            Allowed.Add(TEXT("MinDesiredWidth"));
            Allowed.Add(TEXT("MinDesiredHeight"));
            Allowed.Add(TEXT("MaxDesiredWidth"));
            Allowed.Add(TEXT("MaxDesiredHeight"));
        }
        else if (Tag == TEXT("ScaleBox"))
        {
            Allowed.Add(TEXT("UserDesiredWidth"));
            Allowed.Add(TEXT("UserDesiredHeight"));
            Allowed.Add(TEXT("ContentScale"));
            Allowed.Add(TEXT("Stretch"));
            Allowed.Add(TEXT("StretchDirection"));
        }
        else if (Tag == TEXT("MenuAnchor"))
        {
            Allowed.Add(TEXT("Menu"));
        }
        else if (Tag == TEXT("Border"))
        {
            Allowed.Add(TEXT("BrushColor"));
            Allowed.Add(TEXT("Padding"));
        }
    }

    void AddParentSlotAttributes(const FString* ParentTag, TSet<FString>& Allowed)
    {
        if (!ParentTag)
        {
            return;
        }

        if (*ParentTag == TEXT("XmlUI")
            || *ParentTag == TEXT("Vertical")
            || *ParentTag == TEXT("Horizontal")
            || *ParentTag == TEXT("ScrollBox"))
        {
            Allowed.Add(TEXT("Padding"));
            Allowed.Add(TEXT("HAlign"));
            Allowed.Add(TEXT("VAlign"));
            Allowed.Add(TEXT("SizeParam"));
        }
        else if (*ParentTag == TEXT("Overlay") || *ParentTag == TEXT("WrapBox"))
        {
            Allowed.Add(TEXT("Padding"));
            Allowed.Add(TEXT("HAlign"));
            Allowed.Add(TEXT("VAlign"));
        }
        else if (*ParentTag == TEXT("Grid"))
        {
            Allowed.Add(TEXT("Row"));
            Allowed.Add(TEXT("Column"));
            Allowed.Add(TEXT("HAlign"));
            Allowed.Add(TEXT("VAlign"));
        }
        else if (*ParentTag == TEXT("Canvas"))
        {
            Allowed.Add(TEXT("Position"));
            Allowed.Add(TEXT("Size"));
            Allowed.Add(TEXT("Anchors"));
            Allowed.Add(TEXT("Alignment"));
            Allowed.Add(TEXT("ZOrder"));
            Allowed.Add(TEXT("AutoSize"));
        }
        else if (*ParentTag == TEXT("Border"))
        {
            Allowed.Add(TEXT("HAlign"));
            Allowed.Add(TEXT("VAlign"));
        }
        else if (*ParentTag == TEXT("UserWidget"))
        {
            Allowed.Add(TEXT("SlotName"));
        }
    }

    FString FormatAllowedAttributes(const TSet<FString>& Allowed)
    {
        TArray<FString> Sorted;
        Sorted.Reserve(Allowed.Num());
        for (const FString& Attribute : Allowed)
        {
            Sorted.Add(Attribute);
        }
        Sorted.Sort();
        return FString::Join(Sorted, TEXT(", "));
    }

    bool FailValue(
        const FXmlNodeDesc& Node,
        const FString& AttributeName,
        const FString& AttributeValue,
        const TCHAR* Expected,
        FString& OutError)
    {
        OutError += FString::Printf(
            TEXT("XmlUI: invalid value '%s' for attribute '%s' on <%s Name=\"%s\">; expected %s.\n"),
            *AttributeValue,
            *AttributeName,
            *Node.Tag,
            *Node.Name,
            Expected);
        return false;
    }

    bool ParseFiniteFloat(const FString& Value, float& OutValue)
    {
        return UXmlDslParser::ParseFloat(Value, OutValue) && FMath::IsFinite(OutValue);
    }

    bool ParseFiniteFloatInRange(const FString& Value, const float Min, const float Max)
    {
        float Parsed = 0.0f;
        return ParseFiniteFloat(Value, Parsed) && Parsed >= Min && Parsed <= Max;
    }

    bool ParseFiniteFloatAtLeast(const FString& Value, const float Min)
    {
        float Parsed = 0.0f;
        return ParseFiniteFloat(Value, Parsed) && Parsed >= Min;
    }

    bool ParseIntAtLeast(const FString& Value, const int32 Min)
    {
        int32 Parsed = 0;
        return UXmlDslParser::ParseInt(Value, Parsed) && Parsed >= Min;
    }

    bool SplitCommaValues(const FString& InValue, TArray<FString>& OutParts)
    {
        FString Value = InValue.TrimStartAndEnd();
        const bool bStartsParen = Value.StartsWith(TEXT("("));
        const bool bEndsParen = Value.EndsWith(TEXT(")"));
        if (bStartsParen != bEndsParen)
        {
            return false;
        }
        if (bStartsParen)
        {
            if (Value.Len() < 2)
            {
                return false;
            }
            Value = Value.Mid(1, Value.Len() - 2);
        }

        Value.ParseIntoArray(OutParts, TEXT(","), false);
        for (FString& Part : OutParts)
        {
            Part = Part.TrimStartAndEnd();
            if (Part.IsEmpty())
            {
                return false;
            }
        }
        return true;
    }

    bool ParseStrictVector2D(const FString& Value)
    {
        TArray<FString> Parts;
        if (!SplitCommaValues(Value, Parts) || Parts.Num() != 2)
        {
            return false;
        }

        float X = 0.0f;
        float Y = 0.0f;
        return ParseFiniteFloat(Parts[0], X) && ParseFiniteFloat(Parts[1], Y);
    }

    bool ParseStrictMargin(const FString& Value)
    {
        TArray<FString> Parts;
        if (!SplitCommaValues(Value, Parts)
            || (Parts.Num() != 1 && Parts.Num() != 2 && Parts.Num() != 4))
        {
            return false;
        }

        for (const FString& Part : Parts)
        {
            float Parsed = 0.0f;
            if (!ParseFiniteFloat(Part, Parsed))
            {
                return false;
            }
        }
        return true;
    }

    bool IsHexDigit(const TCHAR Ch)
    {
        return (Ch >= TEXT('0') && Ch <= TEXT('9'))
            || (Ch >= TEXT('a') && Ch <= TEXT('f'))
            || (Ch >= TEXT('A') && Ch <= TEXT('F'));
    }

    bool ParseStrictColor(const FString& InValue)
    {
        const FString Value = InValue.TrimStartAndEnd();
        if (Value.StartsWith(TEXT("#")))
        {
            if (Value.Len() != 7 && Value.Len() != 9)
            {
                return false;
            }
            for (int32 Index = 1; Index < Value.Len(); ++Index)
            {
                if (!IsHexDigit(Value[Index]))
                {
                    return false;
                }
            }
            return true;
        }

        TArray<FString> Parts;
        if (!SplitCommaValues(Value, Parts) || (Parts.Num() != 3 && Parts.Num() != 4))
        {
            return false;
        }

        for (const FString& Part : Parts)
        {
            float Component = 0.0f;
            if (!ParseFiniteFloat(Part, Component) || Component < 0.0f || Component > 255.0f)
            {
                return false;
            }
        }
        return true;
    }

    bool ParseStrictAnchors(const FString& Value)
    {
        TArray<FString> Parts;
        if (!SplitCommaValues(Value, Parts) || (Parts.Num() != 2 && Parts.Num() != 4))
        {
            return false;
        }

        for (const FString& Part : Parts)
        {
            float Parsed = 0.0f;
            if (!ParseFiniteFloat(Part, Parsed))
            {
                return false;
            }
        }
        return true;
    }

    bool IsOneOf(const FString& Value, std::initializer_list<const TCHAR*> Allowed)
    {
        const FString Lower = Value.TrimStartAndEnd().ToLower();
        for (const TCHAR* Candidate : Allowed)
        {
            if (Lower == Candidate)
            {
                return true;
            }
        }
        return false;
    }

    bool ParseStrictBool(const FString& Value)
    {
        return IsOneOf(Value, { TEXT("true"), TEXT("false") });
    }

    template<typename TEnum>
    bool ParseEnumName(const FString& InValue)
    {
        const UEnum* Enum = StaticEnum<TEnum>();
        if (!Enum)
        {
            return false;
        }

        const FString Value = InValue.TrimStartAndEnd();
        if (Enum->GetIndexByNameString(Value) != INDEX_NONE)
        {
            return true;
        }

        const FString Lower = Value.ToLower();
        for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
        {
            if (Enum->GetNameStringByIndex(Index).ToLower() == Lower)
            {
                return true;
            }
        }
        return false;
    }

    bool ParseStrictBrush(const FString& Value)
    {
        const int32 ArrowIndex = Value.Find(TEXT("→"));
        if (ArrowIndex == INDEX_NONE)
        {
            return ParseStrictColor(Value);
        }

        const FString ObjectPath = Value.RightChop(ArrowIndex + 1).TrimStartAndEnd();
        return !ObjectPath.IsEmpty() && FPackageName::IsValidObjectPath(ObjectPath);
    }

    bool ValidateAttributeValue(
        const FXmlNodeDesc& Node,
        const FString* ParentTag,
        const FString& AttributeName,
        const FString& AttributeValue,
        FString& OutError)
    {
        // Parent slot semantics take precedence where an attribute name is overloaded.
        // In particular, Size on a Canvas child is FVector2D, while Spacer.Size is a scalar.
        if (ParentTag && *ParentTag == TEXT("Canvas"))
        {
            if (AttributeName == TEXT("Position") || AttributeName == TEXT("Size") || AttributeName == TEXT("Alignment"))
            {
                return ParseStrictVector2D(AttributeValue)
                    || FailValue(Node, AttributeName, AttributeValue, TEXT("two finite numbers: X,Y"), OutError);
            }
            if (AttributeName == TEXT("Anchors"))
            {
                return ParseStrictAnchors(AttributeValue)
                    || FailValue(Node, AttributeName, AttributeValue, TEXT("two or four finite numbers: X,Y or MinX,MinY,MaxX,MaxY"), OutError);
            }
            if (AttributeName == TEXT("ZOrder"))
            {
                int32 Ignored = 0;
                return UXmlDslParser::ParseInt(AttributeValue, Ignored)
                    || FailValue(Node, AttributeName, AttributeValue, TEXT("an integer"), OutError);
            }
            if (AttributeName == TEXT("AutoSize"))
            {
                return ParseStrictBool(AttributeValue)
                    || FailValue(Node, AttributeName, AttributeValue, TEXT("'true' or 'false'"), OutError);
            }
        }

        if (AttributeName == TEXT("Padding") || AttributeName == TEXT("SlotPadding"))
        {
            return ParseStrictMargin(AttributeValue)
                || FailValue(Node, AttributeName, AttributeValue, TEXT("1, 2, or 4 finite margin numbers"), OutError);
        }
        if (AttributeName == TEXT("HAlign"))
        {
            return IsOneOf(AttributeValue, { TEXT("left"), TEXT("center"), TEXT("right"), TEXT("fill") })
                || FailValue(Node, AttributeName, AttributeValue, TEXT("left, center, right, or fill"), OutError);
        }
        if (AttributeName == TEXT("VAlign"))
        {
            return IsOneOf(AttributeValue, { TEXT("top"), TEXT("center"), TEXT("bottom"), TEXT("fill") })
                || FailValue(Node, AttributeName, AttributeValue, TEXT("top, center, bottom, or fill"), OutError);
        }
        if (AttributeName == TEXT("SizeParam"))
        {
            return IsOneOf(AttributeValue, { TEXT("auto"), TEXT("fill") })
                || FailValue(Node, AttributeName, AttributeValue, TEXT("auto or fill"), OutError);
        }
        if (AttributeName == TEXT("Row") || AttributeName == TEXT("Column"))
        {
            return ParseIntAtLeast(AttributeValue, 0)
                || FailValue(Node, AttributeName, AttributeValue, TEXT("a non-negative integer"), OutError);
        }
        if (AttributeName == TEXT("SlotName"))
        {
            return !AttributeValue.TrimStartAndEnd().IsEmpty()
                || FailValue(Node, AttributeName, AttributeValue, TEXT("a non-empty named-slot name"), OutError);
        }

        if (AttributeName == TEXT("Class") || AttributeName == TEXT("Style"))
        {
            return !AttributeValue.TrimStartAndEnd().IsEmpty()
                || FailValue(Node, AttributeName, AttributeValue, TEXT("a non-empty Unreal class/object path"), OutError);
        }
        if (AttributeName == TEXT("FontPath"))
        {
            return FPackageName::IsValidObjectPath(AttributeValue.TrimStartAndEnd())
                || FailValue(Node, AttributeName, AttributeValue, TEXT("a valid Unreal font object path"), OutError);
        }
        if (AttributeName == TEXT("IsEnabled"))
        {
            return ParseStrictBool(AttributeValue)
                || FailValue(Node, AttributeName, AttributeValue, TEXT("'true' or 'false'"), OutError);
        }
        if (AttributeName == TEXT("CheckedState"))
        {
            return IsOneOf(AttributeValue,
                { TEXT("checked"), TEXT("unchecked"), TEXT("undetermined"), TEXT("true"), TEXT("false") })
                || FailValue(Node, AttributeName, AttributeValue,
                    TEXT("checked, unchecked, undetermined, true, or false"), OutError);
        }
        if (AttributeName == TEXT("Visibility"))
        {
            return IsOneOf(AttributeValue,
                { TEXT("visible"), TEXT("hidden"), TEXT("collapsed"), TEXT("hittestinvisible") })
                || FailValue(Node, AttributeName, AttributeValue,
                    TEXT("visible, hidden, collapsed, or hittestinvisible"), OutError);
        }
        if (AttributeName == TEXT("RenderOpacity"))
        {
            return ParseFiniteFloatInRange(AttributeValue, 0.0f, 1.0f)
                || FailValue(Node, AttributeName, AttributeValue, TEXT("a finite number in the range 0..1"), OutError);
        }
        if (AttributeName == TEXT("ColorAndOpacity")
            || AttributeName == TEXT("Color")
            || AttributeName == TEXT("ShadowColor")
            || AttributeName == TEXT("ButtonColor")
            || AttributeName == TEXT("TextColor")
            || AttributeName == TEXT("FillColor")
            || AttributeName == TEXT("BrushColor"))
        {
            return ParseStrictColor(AttributeValue)
                || FailValue(Node, AttributeName, AttributeValue,
                    TEXT("#RRGGBB, #AARRGGBB, or 3/4 components in the range 0..255"), OutError);
        }

        if (AttributeName == TEXT("FontSize") || AttributeName == TEXT("ArtFontSize"))
        {
            return ParseIntAtLeast(AttributeValue, 1)
                || FailValue(Node, AttributeName, AttributeValue, TEXT("a positive integer"), OutError);
        }
        if (AttributeName == TEXT("Justification"))
        {
            return IsOneOf(AttributeValue, { TEXT("left"), TEXT("center"), TEXT("right") })
                || FailValue(Node, AttributeName, AttributeValue, TEXT("left, center, or right"), OutError);
        }
        if (AttributeName == TEXT("WrapTextAt"))
        {
            return ParseFiniteFloatAtLeast(AttributeValue, 0.0f)
                || FailValue(Node, AttributeName, AttributeValue, TEXT("a non-negative finite number"), OutError);
        }
        if (AttributeName == TEXT("ShadowOffset")
            || AttributeName == TEXT("DesiredSize")
            || AttributeName == TEXT("ContentScale"))
        {
            return ParseStrictVector2D(AttributeValue)
                || FailValue(Node, AttributeName, AttributeValue, TEXT("two finite numbers: X,Y"), OutError);
        }
        if (AttributeName == TEXT("Brush"))
        {
            return ParseStrictBrush(AttributeValue)
                || FailValue(Node, AttributeName, AttributeValue,
                    TEXT("a color or a resource object path such as Texture2D→/Game/UI/T_Icon.T_Icon"), OutError);
        }
        if (AttributeName == TEXT("Percent"))
        {
            return ParseFiniteFloatInRange(AttributeValue, 0.0f, 1.0f)
                || FailValue(Node, AttributeName, AttributeValue, TEXT("a finite number in the range 0..1"), OutError);
        }
        if (AttributeName == TEXT("WrapWidth"))
        {
            float Parsed = 0.0f;
            return (ParseFiniteFloat(AttributeValue, Parsed) && Parsed > 0.0f)
                || FailValue(Node, AttributeName, AttributeValue, TEXT("a positive finite number"), OutError);
        }
        if (AttributeName == TEXT("Columns"))
        {
            return ParseIntAtLeast(AttributeValue, 1)
                || FailValue(Node, AttributeName, AttributeValue, TEXT("a positive integer"), OutError);
        }
        if (AttributeName == TEXT("Orientation"))
        {
            return IsOneOf(AttributeValue, { TEXT("horizontal"), TEXT("vertical") })
                || FailValue(Node, AttributeName, AttributeValue, TEXT("horizontal or vertical"), OutError);
        }
        if (AttributeName == TEXT("WidthOverride")
            || AttributeName == TEXT("HeightOverride")
            || AttributeName == TEXT("MinDesiredWidth")
            || AttributeName == TEXT("MinDesiredHeight")
            || AttributeName == TEXT("MaxDesiredWidth")
            || AttributeName == TEXT("MaxDesiredHeight")
            || AttributeName == TEXT("UserDesiredWidth")
            || AttributeName == TEXT("UserDesiredHeight"))
        {
            return ParseFiniteFloatAtLeast(AttributeValue, 0.0f)
                || FailValue(Node, AttributeName, AttributeValue, TEXT("a non-negative finite number"), OutError);
        }
        if (AttributeName == TEXT("Stretch"))
        {
            return ParseEnumName<EStretch::Type>(AttributeValue)
                || FailValue(Node, AttributeName, AttributeValue, TEXT("a valid EStretch enum name"), OutError);
        }
        if (AttributeName == TEXT("StretchDirection"))
        {
            return ParseEnumName<EStretchDirection::Type>(AttributeValue)
                || FailValue(Node, AttributeName, AttributeValue, TEXT("a valid EStretchDirection enum name"), OutError);
        }
        if (AttributeName == TEXT("Size") && Node.Tag == TEXT("Spacer"))
        {
            return ParseFiniteFloatAtLeast(AttributeValue, 0.0f)
                || FailValue(Node, AttributeName, AttributeValue, TEXT("a non-negative finite number"), OutError);
        }
        if (AttributeName == TEXT("ParentClass")
            || AttributeName == TEXT("WBP")
            || AttributeName == TEXT("Menu"))
        {
            return !AttributeValue.TrimStartAndEnd().IsEmpty()
                || FailValue(Node, AttributeName, AttributeValue, TEXT("a non-empty Unreal class/asset path"), OutError);
        }

        // Text, FontFamily, Name and other free-form string values need no type validation.
        return true;
    }

    bool ValidateNode(
        const FXmlNodeDesc& Node,
        const FString* ParentTag,
        const bool bIsRoot,
        FString& OutError)
    {
        if (!IsKnownTag(Node.Tag))
        {
            OutError += FString::Printf(
                TEXT("XmlUI: unknown tag '%s' on widget '%s'. Strict validation aborted the build.\n"),
                *Node.Tag,
                Node.Name.IsEmpty() ? TEXT("<unnamed>") : *Node.Name);
            return false;
        }

        // Any supported widget may be the document root. This is required for
        // lossless WBP -> DSL -> WBP round-trips: a Widget Blueprint can have a
        // HorizontalBox, CanvasPanel, Button, etc. as its real root widget.

        TSet<FString> Allowed;
        AddCommonAttributes(Allowed);
        AddTagAttributes(Node, bIsRoot, Allowed);
        AddParentSlotAttributes(ParentTag, Allowed);

        for (const TPair<FString, FString>& Attribute : Node.Attributes)
        {
            if (bIsRoot && Attribute.Key.StartsWith(TEXT("Default.")))
            {
                const FString PropertyName = Attribute.Key.RightChop(8);
                if (PropertyName.IsEmpty())
                {
                    OutError += FString::Printf(
                        TEXT("XmlUI: empty Blueprint default property name on <%s Name=\"%s\">; use Default.<PropertyName>.\n"),
                        *Node.Tag,
                        *Node.Name);
                    return false;
                }
                continue;
            }

            if (Attribute.Key.StartsWith(TEXT("Visual.")))
            {
                const FString PropertyName = Attribute.Key.RightChop(7);
                if (!FXmlVisualStyle::IsSupported(Node.Tag, PropertyName)
                    || Attribute.Value.IsEmpty())
                {
                    OutError += FString::Printf(
                        TEXT("XmlUI: invalid or unsupported visual attribute '%s' on <%s Name='%s'>\n"),
                        *Attribute.Key, *Node.Tag, *Node.Name);
                    return false;
                }
                continue;
            }
            if (!Allowed.Contains(Attribute.Key))
            {
                OutError += FString::Printf(
                    TEXT("XmlUI: unknown attribute '%s' on <%s Name=\"%s\">. Allowed attributes: %s\n"),
                    *Attribute.Key,
                    *Node.Tag,
                    *Node.Name,
                    *FormatAllowedAttributes(Allowed));
                return false;
            }

            if (!ValidateAttributeValue(Node, ParentTag, Attribute.Key, Attribute.Value, OutError))
            {
                return false;
            }
        }

        for (const FXmlNodeDesc& Child : Node.Children)
        {
            if (!ValidateNode(Child, &Node.Tag, false, OutError))
            {
                return false;
            }
        }

        return true;
    }
}

bool FXmlDslValidator::ValidateDocument(const FXmlNodeDesc& Root, FString& OutError)
{
    return ValidateNode(Root, nullptr, true, OutError);
}
