#pragma once

#include "CoreMinimal.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/SlateWrapperTypes.h"
#include "Layout/ArrangedChildren.h"
#include "Layout/BasicLayoutWidgetSlot.h"
#include "Layout/Children.h"
#include "Layout/Geometry.h"
#include "Layout/Margin.h"
#include "Misc/Attribute.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SPanel.h"

#include "XmlPanel.generated.h"

class SXmlPanel : public SPanel
{
    SLATE_DECLARE_WIDGET_API(SXmlPanel, SPanel, XMLUI_API)

public:
    class FSlot : public TBasicLayoutWidgetSlot<FSlot>
    {
    public:
        SLATE_SLOT_BEGIN_ARGS(FSlot, TBasicLayoutWidgetSlot<FSlot>)
            TOptional<FSlateChildSize> _SizeParam;
            FSlotArguments& SizeParam(FSlateChildSize InSizeParam)
            {
                _SizeParam = InSizeParam;
                return Me();
            }
        SLATE_SLOT_END_ARGS()

        void Construct(const FChildren& SlotOwner, FSlotArguments&& InArgs);

        FSlateChildSize GetSizeParam() const
        {
            return SizeParam.Get();
        }

        void SetSizeParam(FSlateChildSize InSizeParam)
        {
            SizeParam = InSizeParam;
        }

    private:
        TAttribute<FSlateChildSize> SizeParam;
    };

    SLATE_BEGIN_ARGS(SXmlPanel)
        : _Orientation(EOrientation::Orient_Vertical)
        {
            _Visibility = EVisibility::SelfHitTestInvisible;
        }
        SLATE_ATTRIBUTE(EOrientation, Orientation)
        SLATE_SLOT_ARGUMENT(FSlot, Slots)
    SLATE_END_ARGS()

    SXmlPanel();

    void Construct(const FArguments& InArgs);

    using FScopedWidgetSlotArguments = TPanelChildren<FSlot>::FScopedWidgetSlotArguments;

    FScopedWidgetSlotArguments AddSlot()
    {
        return FScopedWidgetSlotArguments(MakeUnique<FSlot>(), this->Children, INDEX_NONE);
    }

    int32 RemoveSlot(const TSharedRef<SWidget>& SlotWidget)
    {
        return Children.Remove(SlotWidget);
    }

    void ClearChildren()
    {
        Children.Empty();
    }

    void SetOrientation(EOrientation InOrientation);

    virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
    virtual FChildren* GetChildren() override;

protected:
    virtual FVector2D ComputeDesiredSize(float) const override;

private:
    TAttribute<EOrientation> Orientation;

    TPanelChildren<FSlot> Children;
};

UCLASS(BlueprintType)
class XMLUI_API UXmlPanel : public UPanelWidget
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    TEnumAsByte<EOrientation> Orientation = EOrientation::Orient_Vertical;

public:
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
    virtual UClass* GetSlotClass() const override;
    virtual void OnSlotAdded(UPanelSlot* Slot) override;
    virtual void OnSlotRemoved(UPanelSlot* Slot) override;

    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void SynchronizeProperties() override;

    TSharedPtr<SXmlPanel> MyPanel;
};

UCLASS(BlueprintType)
class XMLUI_API UXmlPanelSlot : public UPanelSlot
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    FMargin Padding;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    TEnumAsByte<EHorizontalAlignment> HAlign = HAlign_Fill;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    TEnumAsByte<EVerticalAlignment> VAlign = VAlign_Fill;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="XmlUI")
    FSlateChildSize SizeParam = FSlateChildSize(ESlateSizeRule::Automatic);

public:
    void BuildSlot(TSharedRef<SXmlPanel> XmlPanel);

    virtual void SynchronizeProperties() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
    SXmlPanel::FSlot* Slot;
};
