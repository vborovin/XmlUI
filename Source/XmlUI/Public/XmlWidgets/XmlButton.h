#pragma once

#include "CoreMinimal.h"
#include "Components/ContentWidget.h"
#include "Styling/SlateTypes.h"

#include "XmlButton.generated.h"

class SButton;
class STextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnXmlButtonClicked);

UCLASS(BlueprintType)
class XMLUI_API UXmlButton : public UContentWidget
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable, Category="XmlUI")
    FOnXmlButtonClicked OnClicked;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    FText Text;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    FLinearColor ButtonColor = FLinearColor(0.15f, 0.15f, 0.2f, 1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    FLinearColor TextColor = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    FString FontFamily;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI|Visual")
    FButtonStyle WidgetStyle;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI|Visual")
    bool bUseVisualWidgetStyle = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    FMargin ContentPadding = FMargin(8.f, 4.f);

public:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void SynchronizeProperties() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

    UFUNCTION(BlueprintCallable, Category="XmlUI")
    void SetXmlButtonText(const FText& InText);

    UFUNCTION(BlueprintCallable, Category="XmlUI")
    void SetXmlButtonColor(const FLinearColor& InColor);

protected:
    FReply SlateHandleClicked();

    const FButtonStyle& GetButtonStyle();

    TSharedPtr<SButton> MyButton;

    TSharedPtr<STextBlock> MyLabel;

    FButtonStyle ButtonStyleCache;
};
