#include "XmlDslParser.h"

#include "XmlFile.h"
#include "XmlNode.h"
#include "Misc/FileHelper.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(XmlDslParser)

bool UXmlDslParser::ParseXmlString(const FString& XmlContent, FXmlNodeDesc& OutRoot, FString& OutError)
{
    FXmlFile XmlFile;
    if (!XmlFile.LoadFile(XmlContent, EConstructMethod::ConstructFromBuffer))
    {
        OutError = XmlFile.GetLastError();
        return false;
    }

    const FXmlNode* RootNode = XmlFile.GetRootNode();
    if (RootNode == nullptr)
    {
        OutError = TEXT("XML root node is empty");
        return false;
    }

    TFunction<void(const FXmlNode*, FXmlNodeDesc&)> ConvertNode;
    ConvertNode = [&ConvertNode](const FXmlNode* InNode, FXmlNodeDesc& OutDesc)
    {
        OutDesc.Tag = InNode->GetTag();
        OutDesc.Name = InNode->GetAttribute(TEXT("Name"));

        const TArray<FXmlAttribute>& Attributes = InNode->GetAttributes();
        for (const FXmlAttribute& Attr : Attributes)
        {
            OutDesc.Attributes.Add(Attr.GetTag(), Attr.GetValue());
        }

        const TArray<FXmlNode*>& Children = InNode->GetChildrenNodes();
        for (const FXmlNode* Child : Children)
        {
            FXmlNodeDesc ChildDesc;
            ConvertNode(Child, ChildDesc);
            OutDesc.Children.Add(MoveTemp(ChildDesc));
        }
    };

    ConvertNode(RootNode, OutRoot);
    return true;
}

bool UXmlDslParser::ParseXmlFile(const FString& FilePath, FXmlNodeDesc& OutRoot, FString& OutError)
{
    FString XmlContent;
    if (!FFileHelper::LoadFileToString(XmlContent, *FilePath))
    {
        OutError = FString::Printf(TEXT("Failed to load xml file: %s"), *FilePath);
        return false;
    }

    return ParseXmlString(XmlContent, OutRoot, OutError);
}

bool UXmlDslParser::ParseColor(const FString& In, FLinearColor& OutColor)
{
    FString ColorStr = In.TrimStartAndEnd();

    if (ColorStr.StartsWith(TEXT("#")))
    {
        if (ColorStr.Len() != 7 && ColorStr.Len() != 9)
        {
            return false;
        }

        if (ColorStr.Len() == 1 + 8)
        {
            const FString Hex = ColorStr.Mid(1);
            const FColor Color = FColor::FromHex(Hex.Mid(2) + Hex.Mid(0, 2));
            OutColor = FLinearColor(Color);
            return true;
        }

        const FColor Color = FColor::FromHex(ColorStr);
        OutColor = FLinearColor(Color);
        return true;
    }

    if (ColorStr.StartsWith(TEXT("(")) && ColorStr.EndsWith(TEXT(")")))
    {
        ColorStr = ColorStr.Mid(1, ColorStr.Len() - 2);
    }

    TArray<FString> Parts;
    ColorStr.ParseIntoArray(Parts, TEXT(","), true);
    if (Parts.Num() < 3 || Parts.Num() > 4)
    {
        return false;
    }

    float R = 0.0f;
    float G = 0.0f;
    float B = 0.0f;
    float A = 255.0f;
    if (!ParseFloat(Parts[0], R) || !ParseFloat(Parts[1], G) || !ParseFloat(Parts[2], B))
    {
        return false;
    }
    if (Parts.Num() == 4 && !ParseFloat(Parts[3], A))
    {
        return false;
    }

    FColor Color(
        static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(R, 0.0f, 255.0f))),
        static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(G, 0.0f, 255.0f))),
        static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(B, 0.0f, 255.0f))),
        static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(A, 0.0f, 255.0f))));
    OutColor = FLinearColor(Color);
    return true;
}

bool UXmlDslParser::ParseMargin(const FString& In, FMargin& OutMargin)
{
    FString MarginStr = In.TrimStartAndEnd();
    if (MarginStr.StartsWith(TEXT("(")) && MarginStr.EndsWith(TEXT(")")))
    {
        MarginStr = MarginStr.Mid(1, MarginStr.Len() - 2);
    }

    TArray<FString> Parts;
    MarginStr.ParseIntoArray(Parts, TEXT(","), true);

    if (Parts.Num() == 1)
    {
        float X = 0.0f;
        if (!ParseFloat(Parts[0], X))
        {
            return false;
        }
        OutMargin = FMargin(X);
        return true;
    }

    if (Parts.Num() == 2)
    {
        float H = 0.0f;
        float V = 0.0f;
        if (!ParseFloat(Parts[0], H) || !ParseFloat(Parts[1], V))
        {
            return false;
        }
        OutMargin = FMargin(H, V);
        return true;
    }

    if (Parts.Num() == 4)
    {
        float L = 0.0f;
        float T = 0.0f;
        float R = 0.0f;
        float B = 0.0f;
        if (!ParseFloat(Parts[0], L) || !ParseFloat(Parts[1], T) || !ParseFloat(Parts[2], R) || !ParseFloat(Parts[3], B))
        {
            return false;
        }
        OutMargin = FMargin(L, T, R, B);
        return true;
    }

    return false;
}

bool UXmlDslParser::ParseFloat(const FString& In, float& OutValue)
{
    const FString Trimmed = In.TrimStartAndEnd();
    return LexTryParseString(OutValue, *Trimmed);
}

bool UXmlDslParser::ParseInt(const FString& In, int32& OutValue)
{
    const FString Trimmed = In.TrimStartAndEnd();
    return LexTryParseString(OutValue, *Trimmed);
}

bool UXmlDslParser::ParseVector2D(const FString& In, FVector2D& OutValue)
{
    const FString Trimmed = In.TrimStartAndEnd();
    TArray<FString> Parts;
    Trimmed.ParseIntoArray(Parts, TEXT(","), true);
    if (Parts.Num() != 2)
    {
        return false;
    }

    float X = 0.0f;
    float Y = 0.0f;
    if (!ParseFloat(Parts[0], X) || !ParseFloat(Parts[1], Y))
    {
        return false;
    }

    OutValue = FVector2D(X, Y);
    return true;
}

bool UXmlDslParser::ParseHAlign(const FString& In, EHorizontalAlignment& OutHAlign)
{
    const FString Value = In.TrimStartAndEnd().ToLower();
    if (Value == TEXT("left"))
    {
        OutHAlign = HAlign_Left;
        return true;
    }
    if (Value == TEXT("center"))
    {
        OutHAlign = HAlign_Center;
        return true;
    }
    if (Value == TEXT("right"))
    {
        OutHAlign = HAlign_Right;
        return true;
    }
    if (Value == TEXT("fill"))
    {
        OutHAlign = HAlign_Fill;
        return true;
    }
    return false;
}

bool UXmlDslParser::ParseVAlign(const FString& In, EVerticalAlignment& OutVAlign)
{
    const FString Value = In.TrimStartAndEnd().ToLower();
    if (Value == TEXT("top"))
    {
        OutVAlign = VAlign_Top;
        return true;
    }
    if (Value == TEXT("center"))
    {
        OutVAlign = VAlign_Center;
        return true;
    }
    if (Value == TEXT("bottom"))
    {
        OutVAlign = VAlign_Bottom;
        return true;
    }
    if (Value == TEXT("fill"))
    {
        OutVAlign = VAlign_Fill;
        return true;
    }
    return false;
}

bool UXmlDslParser::ParseJustification(const FString& In, ETextJustify::Type& OutJustification)
{
    const FString Value = In.TrimStartAndEnd().ToLower();
    if (Value == TEXT("left"))
    {
        OutJustification = ETextJustify::Left;
        return true;
    }
    if (Value == TEXT("center"))
    {
        OutJustification = ETextJustify::Center;
        return true;
    }
    if (Value == TEXT("right"))
    {
        OutJustification = ETextJustify::Right;
        return true;
    }
    return false;
}

bool UXmlDslParser::ParseSizeRule(const FString& In, ESlateSizeRule::Type& OutRule)
{
    const FString Value = In.TrimStartAndEnd().ToLower();
    if (Value == TEXT("auto"))
    {
        OutRule = ESlateSizeRule::Automatic;
        return true;
    }
    if (Value == TEXT("fill"))
    {
        OutRule = ESlateSizeRule::Fill;
        return true;
    }
    return false;
}
