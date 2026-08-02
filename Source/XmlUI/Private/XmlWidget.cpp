#include "XmlWidget.h"

#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "XmlFontSizeUtil.h"
#include "UObject/UObjectGlobals.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(XmlWidget)

TSharedRef<SWidget> UXmlWidget::RebuildWidget()
{
    MySlateWidget = BuildSlateWidget();
    return MySlateWidget.ToSharedRef();
}

void UXmlWidget::ReleaseSlateResources(bool bReleaseChildren)
{
    MySlateWidget.Reset();
    Super::ReleaseSlateResources(bReleaseChildren);
}

UFont* GetXmlProjectFont()
{
    static UFont* ProjectFont = LoadObject<UFont>(nullptr, *UWidget::GetDefaultFontName());
    return ProjectFont;
}

FSlateFontInfo UXmlTextBlock::BuildFontInfo() const
{
    const int32 EffectiveSize = (ArtFontSize >= 0) ? GetXmlFontSizeByArtFontSize(ArtFontSize) : FontSize;
    return FSlateFontInfo(GetXmlProjectFont(), EffectiveSize);
}

TSharedRef<SWidget> UXmlTextBlock::BuildSlateWidget()
{
    MyTextBlock = SNew(STextBlock)
        .Text(Text)
        .Font(BuildFontInfo())
        .ColorAndOpacity(Color)
        .ShadowColorAndOpacity(ShadowColor)
        .ShadowOffset(ShadowOffset)
        .Justification(Justification)
        .WrapTextAt(WrapTextAt);
    return MyTextBlock.ToSharedRef();
}

void UXmlTextBlock::SynchronizeProperties()
{
    Super::SynchronizeProperties();
    MyTextBlock->SetText(Text);
    MyTextBlock->SetFont(BuildFontInfo());
    MyTextBlock->SetColorAndOpacity(Color);
    MyTextBlock->SetShadowColorAndOpacity(ShadowColor);
    MyTextBlock->SetShadowOffset(ShadowOffset);
    MyTextBlock->SetJustification(Justification);
    MyTextBlock->SetWrapTextAt(WrapTextAt);
}

void UXmlTextBlock::ReleaseSlateResources(bool bReleaseChildren)
{
    MyTextBlock.Reset();
    Super::ReleaseSlateResources(bReleaseChildren);
}

void UXmlTextBlock::SetXmlText(const FText& InText)
{
    Text = InText;
    if (MyTextBlock.IsValid())
    {
        MyTextBlock->SetText(Text);
    }
}

void UXmlTextBlock::SetXmlColor(const FLinearColor& InColor)
{
    Color = InColor;
    if (MyTextBlock.IsValid())
    {
        MyTextBlock->SetColorAndOpacity(Color);
    }
}

TSharedRef<SWidget> UXmlImage::BuildSlateWidget()
{
    MyImage = SNew(SImage)
        .Image(&Brush)
        .ColorAndOpacity(Color);
    if (DesiredSize != FVector2D::ZeroVector)
    {
        MyImage->SetDesiredSizeOverride(DesiredSize);
    }
    return MyImage.ToSharedRef();
}

void UXmlImage::SynchronizeProperties()
{
    Super::SynchronizeProperties();
    MyImage->SetImage(&Brush);
    MyImage->SetColorAndOpacity(Color);
    if (DesiredSize != FVector2D::ZeroVector)
    {
        MyImage->SetDesiredSizeOverride(DesiredSize);
    }
}

void UXmlImage::ReleaseSlateResources(bool bReleaseChildren)
{
    MyImage.Reset();
    Super::ReleaseSlateResources(bReleaseChildren);
}

void UXmlImage::SetXmlTexture(UTexture2D* Texture)
{
    Brush.SetResourceObject(Texture);
    Brush.TintColor = FLinearColor::White;
    if (MyImage.IsValid())
    {
        MyImage->SetImage(&Brush);
    }
}

TSharedRef<SWidget> UXmlSpacer::BuildSlateWidget()
{
    MySpacer = SNew(SSpacer)
        .Size(FVector2D(Size, Size));
    return MySpacer.ToSharedRef();
}

void UXmlSpacer::SynchronizeProperties()
{
    Super::SynchronizeProperties();
    MySpacer->SetSize(FVector2D(Size, Size));
}

void UXmlSpacer::ReleaseSlateResources(bool bReleaseChildren)
{
    MySpacer.Reset();
    Super::ReleaseSlateResources(bReleaseChildren);
}

TSharedRef<SWidget> UXmlProgressBar::BuildSlateWidget()
{
    MyProgressBar = SNew(SProgressBar)
        .Percent(Percent)
        .FillColorAndOpacity(FillColor)
        .BarFillType(EProgressBarFillType::LeftToRight);
    return MyProgressBar.ToSharedRef();
}

void UXmlProgressBar::SynchronizeProperties()
{
    Super::SynchronizeProperties();
    MyProgressBar->SetPercent(Percent);
    MyProgressBar->SetFillColorAndOpacity(FillColor);
}

void UXmlProgressBar::ReleaseSlateResources(bool bReleaseChildren)
{
    MyProgressBar.Reset();
    Super::ReleaseSlateResources(bReleaseChildren);
}

void UXmlProgressBar::SetXmlPercent(float InPercent)
{
    Percent = InPercent;
    if (MyProgressBar.IsValid())
    {
        MyProgressBar->SetPercent(Percent);
    }
}
