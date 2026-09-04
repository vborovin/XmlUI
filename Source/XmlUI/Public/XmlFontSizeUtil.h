#pragma once
#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Containers/Map.h"
#include "Engine/Font.h"
#include "UObject/UObjectGlobals.h"
#include "XmlUISettings.h"

// Figma art font size -> engine font size mapping
inline int GetXmlFontSizeByArtFontSize(int ArtFontSize)
{
    static const TMap<int, int> ArtFont2FontMap{
        {10,7},{13,10},{14,10},{16,12},{26,17},{28,19},{30,20},{32,22},{34,23},{36,25},{38,26},
        {40,28},{42,29},{46,33},{52,36},{54,39},{56,40},{58,42},{62,44},{64,46},{68,50},{70,50},
        {74,54},{76,55},{78,56},{80,58},{84,60},{86,62},{90,64},{96,68},{128,94},{200,150},{220,165},{260,194}};
    if (ArtFont2FontMap.Contains(ArtFontSize))
    {
        return ArtFont2FontMap[ArtFontSize];
    }
    TPair<int,int> MinArtFont2Font {0,0};
    TPair<int,int> MaxArtFont2Font {0,0};
    for (auto& ArtFont2Font : ArtFont2FontMap)
    {
        MaxArtFont2Font = ArtFont2Font;
        if (ArtFont2Font.Key > ArtFontSize)
        {
            break;
        }
        else
        {
            MinArtFont2Font = ArtFont2Font;
        }
    }
    const float Factor = static_cast<float>(ArtFontSize - MinArtFont2Font.Key) / static_cast<float>(MaxArtFont2Font.Key - MinArtFont2Font.Key);
    return FMath::Lerp(static_cast<float>(MinArtFont2Font.Value), static_cast<float>(MaxArtFont2Font.Value), Factor);
}

// Resolves a Figma font family name to a UFont via UXmlUISettings::FontFamilyMap.
// Falls back to the engine default widget font with a warning when unmapped or unloadable.
inline UFont* GetXmlFontByFamily(const FString& FontFamily)
{
    // Static caches: default font loads once and each family resolves once (restart to pick up FontFamilyMap edits).
    static UFont* DefaultFont = LoadObject<UFont>(nullptr, *UWidget::GetDefaultFontName());
    if (FontFamily.IsEmpty())
    {
        return DefaultFont;
    }
    static TMap<FString, UFont*> FamilyToFont; // nullptr entries keep warnings one-shot
    if (UFont* const* Cached = FamilyToFont.Find(FontFamily))
    {
        return *Cached ? *Cached : DefaultFont;
    }
    const UXmlUISettings* Settings = GetDefault<UXmlUISettings>();
    const FString* FontPath = Settings->FontFamilyMap.Find(FontFamily);
    UFont* Resolved = nullptr;
    if (!FontPath || FontPath->IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("XmlUI: Font family '%s' is not mapped in XmlUISettings.FontFamilyMap; using the default widget font"), *FontFamily);
    }
    else
    {
        Resolved = LoadObject<UFont>(nullptr, **FontPath);
        if (!Resolved)
        {
            UE_LOG(LogTemp, Warning, TEXT("XmlUI: Failed to load font '%s' for family '%s'; using the default widget font"), **FontPath, *FontFamily);
        }
    }
    FamilyToFont.Add(FontFamily, Resolved);
    return Resolved ? Resolved : DefaultFont;
}
