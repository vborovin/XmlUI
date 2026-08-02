#include "XmlButton.h"

#include "Components/SlateWrapperTypes.h"
#include "Engine/Font.h"
#include "Styling/CoreStyle.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "XmlFontSizeUtil.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(XmlButton)

TSharedRef<SWidget> UXmlButton::RebuildWidget()
{
    UWidget* ContentWidget = GetContentSlot() ? GetContentSlot()->Content : nullptr;
    if (ContentWidget)
    {
        MyLabel.Reset();
    }
    else
    {
        MyLabel = SNew(STextBlock)
            .Text(Text)
            .Font(FSlateFontInfo(GetXmlProjectFont(), 16))
            .ColorAndOpacity(TextColor)
            .Justification(ETextJustify::Center);
    }

    const TSharedRef<SWidget> ButtonContent = ContentWidget ? ContentWidget->TakeWidget() : MyLabel.ToSharedRef();

    MyButton = SNew(SButton)
        .OnClicked(BIND_UOBJECT_DELEGATE(FOnClicked, SlateHandleClicked))
        .ButtonStyle(&GetButtonStyle())
        .ButtonColorAndOpacity(ButtonColor)
        .ForegroundColor(TextColor)
        .ContentPadding(ContentPadding)
        .Content()[ ButtonContent ];

    return MyButton.ToSharedRef();
}

void UXmlButton::SynchronizeProperties()
{
    Super::SynchronizeProperties();

    if (MyLabel.IsValid())
    {
        MyLabel->SetText(Text);
    }

    if (MyButton.IsValid())
    {
        MyButton->SetButtonStyle(&GetButtonStyle());
    }
}

void UXmlButton::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);

    MyButton.Reset();
    MyLabel.Reset();
}

void UXmlButton::SetXmlButtonText(const FText& InText)
{
    Text = InText;

    if (MyLabel.IsValid())
    {
        MyLabel->SetText(InText);
    }

    if (MyButton.IsValid())
    {
        MyButton->SetButtonStyle(&GetButtonStyle());
    }
}

void UXmlButton::SetXmlButtonColor(const FLinearColor& InColor)
{
    ButtonColor = InColor;

    if (MyButton.IsValid())
    {
        MyButton->SetButtonStyle(&GetButtonStyle());
    }
}

FReply UXmlButton::SlateHandleClicked()
{
    OnClicked.Broadcast();
    return FReply::Handled();
}

const FButtonStyle& UXmlButton::GetButtonStyle()
{
    const FButtonStyle& BaseStyle = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
    ButtonStyleCache = BaseStyle;

    const FLinearColor PressedColor = ButtonColor * 0.75f;
    const FLinearColor DisabledColor = ButtonColor * 0.4f;

    ButtonStyleCache.Normal.TintColor = FSlateColor(ButtonColor);
    ButtonStyleCache.Hovered.TintColor = FSlateColor(ButtonColor);
    ButtonStyleCache.Pressed.TintColor = FSlateColor(PressedColor);
    ButtonStyleCache.Disabled.TintColor = FSlateColor(DisabledColor);

    return ButtonStyleCache;
}
