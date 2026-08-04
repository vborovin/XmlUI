#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Framework/Text/TextLayout.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#include "XmlWidget.generated.h"

UCLASS(Abstract, BlueprintType)
class XMLUI_API UXmlWidget : public UWidget
{
    GENERATED_BODY()

public:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

    template<typename T>
    TSharedPtr<T> GetSlate() const
    {
        return StaticCastSharedPtr<T>(MySlateWidget);
    }

protected:
    TSharedPtr<SWidget> MySlateWidget;

    virtual TSharedRef<SWidget> BuildSlateWidget() PURE_VIRTUAL(UXmlWidget::BuildSlateWidget, return SNullWidget::NullWidget;);
};

UCLASS(BlueprintType)
class XMLUI_API UXmlTextBlock : public UXmlWidget
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    FText Text;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    int32 FontSize = 16;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    int32 ArtFontSize = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    FString FontFamily;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    FLinearColor Color = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    TEnumAsByte<ETextJustify::Type> Justification = ETextJustify::Left;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    float WrapTextAt = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    FLinearColor ShadowColor = FLinearColor::Transparent;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    FVector2D ShadowOffset = FVector2D::ZeroVector;

    UFUNCTION(BlueprintCallable, Category="XmlUI")
    void SetXmlText(const FText& InText);

    UFUNCTION(BlueprintCallable, Category="XmlUI")
    void SetXmlColor(const FLinearColor& InColor);

    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
    virtual TSharedRef<SWidget> BuildSlateWidget() override;
    virtual void SynchronizeProperties() override;

    TSharedPtr<STextBlock> MyTextBlock;

private:
    FSlateFontInfo BuildFontInfo() const;
};

class UTexture2D;

UCLASS(BlueprintType)
class XMLUI_API UXmlImage : public UXmlWidget
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI", meta=(DisplayThumbnail="true", AllowedClasses="Texture2D,MaterialInterface"))
    FSlateBrush Brush;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    FLinearColor Color = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    FVector2D DesiredSize = FVector2D::ZeroVector;

    UFUNCTION(BlueprintCallable, Category="XmlUI")
    void SetXmlTexture(UTexture2D* Texture);

    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
    virtual TSharedRef<SWidget> BuildSlateWidget() override;
    virtual void SynchronizeProperties() override;

    TSharedPtr<SImage> MyImage;
};

UCLASS(BlueprintType)
class XMLUI_API UXmlSpacer : public UXmlWidget
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    float Size = 16.f;

    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
    virtual TSharedRef<SWidget> BuildSlateWidget() override;
    virtual void SynchronizeProperties() override;

    TSharedPtr<SSpacer> MySpacer;
};

UCLASS(BlueprintType)
class XMLUI_API UXmlProgressBar : public UXmlWidget
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    float Percent = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    FLinearColor FillColor = FLinearColor::White;

    UFUNCTION(BlueprintCallable, Category="XmlUI")
    void SetXmlPercent(float InPercent);

    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
    virtual TSharedRef<SWidget> BuildSlateWidget() override;
    virtual void SynchronizeProperties() override;

    TSharedPtr<SProgressBar> MyProgressBar;
};
