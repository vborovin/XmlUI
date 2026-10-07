#include "XmlDslValidator.h"

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
            TEXT("Spacer"),
            TEXT("ProgressBar"),
        };
        return KnownTags.Contains(Tag);
    }

    void AddCommonAttributes(TSet<FString>& Allowed)
    {
        Allowed.Add(TEXT("Name"));
        Allowed.Add(TEXT("Visibility"));
        Allowed.Add(TEXT("RenderOpacity"));
        Allowed.Add(TEXT("ColorAndOpacity"));
    }

    void AddTagAttributes(const FXmlNodeDesc& Node, const bool bIsRoot, TSet<FString>& Allowed)
    {
        const FString& Tag = Node.Tag;

        if (bIsRoot && Tag == TEXT("XmlUI"))
        {
            Allowed.Add(TEXT("ParentClass"));
        }

        if (Tag == TEXT("Text"))
        {
            Allowed.Add(TEXT("Text"));
            Allowed.Add(TEXT("FontSize"));
            Allowed.Add(TEXT("ArtFontSize"));
            Allowed.Add(TEXT("FontFamily"));
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

        if (bIsRoot && Node.Tag != TEXT("XmlUI"))
        {
            OutError += FString::Printf(
                TEXT("XmlUI: document root must be <XmlUI>, got <%s>.\n"),
                *Node.Tag);
            return false;
        }

        TSet<FString> Allowed;
        AddCommonAttributes(Allowed);
        AddTagAttributes(Node, bIsRoot, Allowed);
        AddParentSlotAttributes(ParentTag, Allowed);

        for (const TPair<FString, FString>& Attribute : Node.Attributes)
        {
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
