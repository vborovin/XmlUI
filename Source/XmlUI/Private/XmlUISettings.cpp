#include "XmlUISettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(XmlUISettings)

UXmlUISettings::UXmlUISettings()
{
    CategoryName = TEXT("XmlUI");

    // Native UMG is the default backend. The UXml* widget classes remain available
    // as a legacy/fallback implementation when a host clears or overrides mappings.
    WidgetClassMap.Add(TEXT("Vertical"), TEXT("/Script/UMG.VerticalBox"));
    WidgetClassMap.Add(TEXT("Horizontal"), TEXT("/Script/UMG.HorizontalBox"));
    WidgetClassMap.Add(TEXT("Text"), TEXT("/Script/UMG.TextBlock"));
    WidgetClassMap.Add(TEXT("Image"), TEXT("/Script/UMG.Image"));
    WidgetClassMap.Add(TEXT("Button"), TEXT("/Script/UMG.Button"));
    WidgetClassMap.Add(TEXT("Spacer"), TEXT("/Script/UMG.Spacer"));
    WidgetClassMap.Add(TEXT("ProgressBar"), TEXT("/Script/UMG.ProgressBar"));
}
