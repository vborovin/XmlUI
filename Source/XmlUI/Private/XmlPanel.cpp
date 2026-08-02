#include "XmlPanel.h"

#include "Components/Widget.h"
#include "Layout/ArrangedChildren.h"
#include "Widgets/SNullWidget.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(XmlPanel)

SLATE_IMPLEMENT_WIDGET(SXmlPanel)
void SXmlPanel::PrivateRegisterAttributes(FSlateAttributeInitializer& AttributeInitializer)
{
    FSlateWidgetSlotAttributeInitializer Initializer = SLATE_ADD_PANELCHILDREN_DEFINITION(AttributeInitializer, Children);
    FSlot::RegisterAttributes(Initializer);
}

SXmlPanel::SXmlPanel()
    : Orientation(EOrientation::Orient_Vertical)
    , Children(this, GET_MEMBER_NAME_CHECKED(SXmlPanel, Children))
{
    SetCanTick(false);
    bCanSupportFocus = false;
}

void SXmlPanel::FSlot::Construct(const FChildren& SlotOwner, FSlotArguments&& InArgs)
{
    TBasicLayoutWidgetSlot<FSlot>::Construct(SlotOwner, MoveTemp(InArgs));
    if (InArgs._SizeParam.IsSet())
    {
        SizeParam = MoveTemp(InArgs._SizeParam.GetValue());
    }
}

void SXmlPanel::Construct(const FArguments& InArgs)
{
    Orientation = InArgs._Orientation;

    Children.AddSlots(MoveTemp(const_cast<TArray<FSlot::FSlotArguments>&>(InArgs._Slots)));
}

void SXmlPanel::SetOrientation(EOrientation InOrientation)
{
    if (Orientation.Get() != InOrientation)
    {
        Orientation = InOrientation;
        Invalidate(EInvalidateWidgetReason::Layout);
    }
}

void SXmlPanel::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
    const bool bVertical = (Orientation.Get() == EOrientation::Orient_Vertical);

    const FVector2f AllottedSize = FVector2f(AllottedGeometry.GetLocalSize());
    const float MainAxisTotal = bVertical ? AllottedSize.Y : AllottedSize.X;
    const float CrossAxisTotal = bVertical ? AllottedSize.X : AllottedSize.Y;

    float AutoTotal = 0.f;
    float FillWeightTotal = 0.f;
    for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
    {
        const FSlot& CurChild = Children[ChildIndex];
        const TSharedRef<SWidget>& CurWidget = CurChild.GetWidget();
        if (CurWidget->GetVisibility() == EVisibility::Collapsed)
        {
            continue;
        }

        const FMargin Padding = CurChild.GetPadding();
        const FSlateChildSize SizeParam = CurChild.GetSizeParam();
        if (SizeParam.SizeRule == ESlateSizeRule::Automatic)
        {
            const FVector2f ChildDesiredSize = CurWidget->GetDesiredSize();
            AutoTotal += bVertical ? ChildDesiredSize.Y : ChildDesiredSize.X;
        }
        else
        {
            FillWeightTotal += FMath::Max(SizeParam.Value, 0.f);
        }
        AutoTotal += bVertical
            ? Padding.GetTotalSpaceAlong<Orient_Vertical>()
            : Padding.GetTotalSpaceAlong<Orient_Horizontal>();
    }

    const float Remaining = FMath::Max(0.f, MainAxisTotal - AutoTotal);

    float MainAxisOffset = 0.f;
    for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
    {
        const FSlot& CurChild = Children[ChildIndex];
        const TSharedRef<SWidget>& CurWidget = CurChild.GetWidget();
        if (CurWidget->GetVisibility() == EVisibility::Collapsed)
        {
            continue;
        }

        const FVector2f ChildDesiredSize = CurWidget->GetDesiredSize();
        const FMargin Padding = CurChild.GetPadding();
        const FSlateChildSize SizeParam = CurChild.GetSizeParam();

        float MainAxisSize = 0.f;
        if (SizeParam.SizeRule == ESlateSizeRule::Automatic)
        {
            MainAxisSize = bVertical ? ChildDesiredSize.Y : ChildDesiredSize.X;
        }
        else if (FillWeightTotal > 0.f)
        {
            MainAxisSize = Remaining * FMath::Max(SizeParam.Value, 0.f) / FillWeightTotal;
        }
        MainAxisSize = FMath::Max(0.f, MainAxisSize);

        float CrossAxisSize = bVertical
            ? (CurChild.GetHorizontalAlignment() == HAlign_Fill ? CrossAxisTotal : ChildDesiredSize.X)
            : (CurChild.GetVerticalAlignment() == VAlign_Fill ? CrossAxisTotal : ChildDesiredSize.Y);
        CrossAxisSize = FMath::Max(0.f, CrossAxisSize);

        const float MainAxisPadding = bVertical ? Padding.Top : Padding.Left;
        const float MainAxisTrailingPadding = bVertical ? Padding.Bottom : Padding.Right;

        float CrossAxisOffset = 0.f;
        if (bVertical)
        {
            switch (CurChild.GetHorizontalAlignment())
            {
            case HAlign_Center:
                CrossAxisOffset = (CrossAxisTotal - CrossAxisSize) * 0.5f;
                break;
            case HAlign_Right:
                CrossAxisOffset = CrossAxisTotal - CrossAxisSize - Padding.Right;
                break;
            default: // HAlign_Fill / HAlign_Left
                CrossAxisOffset = Padding.Left;
                break;
            }
        }
        else
        {
            switch (CurChild.GetVerticalAlignment())
            {
            case VAlign_Center:
                CrossAxisOffset = (CrossAxisTotal - CrossAxisSize) * 0.5f;
                break;
            case VAlign_Bottom:
                CrossAxisOffset = CrossAxisTotal - CrossAxisSize - Padding.Bottom;
                break;
            default: // VAlign_Fill / VAlign_Top
                CrossAxisOffset = Padding.Top;
                break;
            }
        }

        const FVector2f ChildOffset = bVertical
            ? FVector2f(CrossAxisOffset, MainAxisOffset + MainAxisPadding)
            : FVector2f(MainAxisOffset + MainAxisPadding, CrossAxisOffset);
        const FVector2f ChildSize = bVertical
            ? FVector2f(CrossAxisSize, MainAxisSize)
            : FVector2f(MainAxisSize, CrossAxisSize);

        ArrangedChildren.AddWidget(AllottedGeometry.MakeChild(CurWidget, ChildOffset, ChildSize));

        MainAxisOffset += MainAxisPadding + MainAxisSize + MainAxisTrailingPadding;
    }
}

FVector2D SXmlPanel::ComputeDesiredSize(float) const
{
    const bool bVertical = (Orientation.Get() == EOrientation::Orient_Vertical);

    float DesiredCrossSize = 0.f;
    float DesiredMainSize = 0.f;
    for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
    {
        const FSlot& CurChild = Children[ChildIndex];
        const TSharedRef<SWidget>& CurWidget = CurChild.GetWidget();
        if (CurWidget->GetVisibility() == EVisibility::Collapsed)
        {
            continue;
        }

        const FVector2f CurDesiredSize = CurWidget->GetDesiredSize();
        const FMargin Padding = CurChild.GetPadding();
        if (bVertical)
        {
            DesiredCrossSize = FMath::Max(DesiredCrossSize, CurDesiredSize.X + Padding.Left + Padding.Right);
            DesiredMainSize += CurDesiredSize.Y + Padding.Top + Padding.Bottom;
        }
        else
        {
            DesiredCrossSize = FMath::Max(DesiredCrossSize, CurDesiredSize.Y + Padding.Top + Padding.Bottom);
            DesiredMainSize += CurDesiredSize.X + Padding.Left + Padding.Right;
        }
    }

    return bVertical ? FVector2D(DesiredCrossSize, DesiredMainSize) : FVector2D(DesiredMainSize, DesiredCrossSize);
}

FChildren* SXmlPanel::GetChildren()
{
    return &Children;
}

TSharedRef<SWidget> UXmlPanel::RebuildWidget()
{
    MyPanel = SNew(SXmlPanel)
        .Orientation(Orientation);

    for (UPanelSlot* PanelSlot : Slots)
    {
        if (UXmlPanelSlot* TypedSlot = Cast<UXmlPanelSlot>(PanelSlot))
        {
            TypedSlot->Parent = this;
            TypedSlot->BuildSlot(MyPanel.ToSharedRef());
        }
    }

    return MyPanel.ToSharedRef();
}

void UXmlPanel::SynchronizeProperties()
{
    Super::SynchronizeProperties();

    if (MyPanel.IsValid())
    {
        MyPanel->SetOrientation(Orientation);
    }
}

void UXmlPanel::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);

    MyPanel.Reset();
}

UClass* UXmlPanel::GetSlotClass() const
{
    return UXmlPanelSlot::StaticClass();
}

void UXmlPanel::OnSlotAdded(UPanelSlot* InSlot)
{
    if (MyPanel.IsValid())
    {
        CastChecked<UXmlPanelSlot>(InSlot)->BuildSlot(MyPanel.ToSharedRef());
    }
}

void UXmlPanel::OnSlotRemoved(UPanelSlot* InSlot)
{
    if (MyPanel.IsValid() && InSlot->Content)
    {
        TSharedPtr<SWidget> Widget = InSlot->Content->GetCachedWidget();
        if (Widget.IsValid())
        {
            MyPanel->RemoveSlot(Widget.ToSharedRef());
        }
    }
}

void UXmlPanelSlot::BuildSlot(TSharedRef<SXmlPanel> XmlPanel)
{
    XmlPanel->AddSlot().Expose(Slot).Padding(Padding).HAlign(HAlign).VAlign(VAlign).SizeParam(SizeParam)
        [
            Content == nullptr ? SNullWidget::NullWidget : Content->TakeWidget()
        ];

    SynchronizeProperties();
}

void UXmlPanelSlot::SynchronizeProperties()
{
    Super::SynchronizeProperties();

    if (Slot)
    {
        Slot->SetPadding(Padding);
        Slot->SetHorizontalAlignment(HAlign);
        Slot->SetVerticalAlignment(VAlign);
        Slot->SetSizeParam(SizeParam);
    }
}

void UXmlPanelSlot::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);

    Slot = nullptr;
}
