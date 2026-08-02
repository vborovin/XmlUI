# Focused Examples

Use these examples as patterns, not as fixed designs. Preserve the current task's hierarchy, measurements, names, and project conventions.

For parseable code samples, this file uses a fictional `SampleGame` module with `SAMPLEGAME_API`, a `U<RootName>Widget` class convention, 1:1 design units, and XmlUI's built-in `ArtFontSize` mapping. Replace these assumptions with conventions discovered in the host project.

## Card with Root Spacing and Runtime Controls

Input: a dark 400 x 200 reward card with 16 px horizontal and 12 px vertical inset, a centered 26 px title, a 60% green progress bar, and a centered claim button. The card has a 12 px radius, which XmlUI cannot represent.

`XmlUI_RewardCard.xml`:

```xml
<XmlUI Name="RewardCard" ParentClass="/Script/SampleGame.RewardCardWidget">
    <!-- Generation notes
    Source: reward card mockup, 400 x 200
    Scale: source pixels preserved 1:1 by the sample host's design policy
    Omitted: none
    Unsupported: card corner radius 12 px
    Assets: none
    Programming: bind BtnClaim click behavior and update RewardBar at runtime
    -->
    <SizeBox Name="CardBox" WidthOverride="400" HeightOverride="200">
        <Overlay Name="CardOverlay">
            <Image Name="CardBackground" Brush="#FF181B20" HAlign="Fill" VAlign="Fill"/>
            <Vertical Name="ContentColumn" Padding="16,12" HAlign="Fill" VAlign="Fill">
                <Text Name="TitleText" Text="Gift Pack" ArtFontSize="26" Color="#FFFFFFFF" Justification="Center"/>
                <Spacer Name="SpaceTitleToBar" Size="12"/>
                <SizeBox Name="RewardBarBox" HeightOverride="20">
                    <ProgressBar Name="RewardBar" Percent="0.6" FillColor="#FF00C853"/>
                </SizeBox>
                <Spacer Name="SpaceBarToButton" Size="16"/>
                <SizeBox Name="ClaimButtonBox" HAlign="Center">
                    <Button Name="BtnClaim" Text="Claim" ButtonColor="#FF263238" TextColor="#FFFFFFFF" Padding="16,10"/>
                </SizeBox>
            </Vertical>
        </Overlay>
    </SizeBox>
</XmlUI>
```

Why the wrappers matter:

- `CardBox` enforces the 400 x 200 component bounds; `CardOverlay` layers its structural surface and content.
- Root `Padding` would be ignored, so the inset is placed on `ContentColumn`, an `Overlay` child slot.
- `RewardBarBox` gives the progress bar a fixed height.
- `ClaimButtonBox` carries outer alignment while `BtnClaim Padding` remains button content padding instead of also becoming a panel-slot margin.

Matching header:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RewardCardWidget.generated.h"

class UXmlButton;
class UXmlProgressBar;

UCLASS(BlueprintType)
class SAMPLEGAME_API URewardCardWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    UPROPERTY(meta = (BindWidget))
    UXmlProgressBar* RewardBar = nullptr;

    UPROPERTY(meta = (BindWidget))
    UXmlButton* BtnClaim = nullptr;

    // TODO(programmer): bind reward state and claim behavior.
};
```

`TitleText` is static in this example, so it does not need a binding.

## Runtime-Populated Horizontal List

Input: a daily-reward dialog shows seven mock reward boxes. Runtime data owns the real item count.

Generate a host plus one representative item instead of seven copied nodes:

```xml
<XmlUI Name="DailyRewardDialog" ParentClass="/Script/SampleGame.DailyRewardDialogWidget">
    <!-- Generation notes
    Source: daily reward dialog
    Scale: source pixels preserved 1:1 by the sample host's design policy
    Omitted: full-page presentation backdrop and fake player avatars
    Unsupported: beveled title text and 8 px dialog corner radius
    Assets: #FF5A7AC0 -> reward icon -> runtime UXmlImage::SetXmlTexture
    Programming: populate RewardsRow from reward data; bind close and claim actions
    -->
    <Vertical Name="DialogColumn">
        <Text Name="TitleText" Text="Daily Reward" ArtFontSize="52" Justification="Center"/>
        <Spacer Name="SpaceToRewards" Size="24"/>
        <Horizontal Name="RewardsRow">
            <Vertical Name="RewardItemTemplate">
                <Image Name="RewardIcon" Brush="#FF5A7AC0" DesiredSize="128,128"/>
                <Text Name="RewardAmountText" Text="x1" ArtFontSize="28" Justification="Center"/>
            </Vertical>
        </Horizontal>
        <Spacer Name="SpaceToActions" Size="24"/>
        <Horizontal Name="ActionRow" HAlign="Center">
            <SizeBox Name="CloseButtonBox">
                <Button Name="BtnClose" Text="Close" Padding="20,10"/>
            </SizeBox>
            <Spacer Name="SpaceActions" Size="16"/>
            <SizeBox Name="ClaimButtonBox">
                <Button Name="BtnClaim" Text="Claim" Padding="20,10"/>
            </SizeBox>
        </Horizontal>
    </Vertical>
</XmlUI>
```

Bind the host because code populates it, and bind the two buttons because code handles them:

```cpp
class UXmlButton;
class UXmlPanel;

UPROPERTY(meta = (BindWidget))
UXmlPanel* RewardsRow = nullptr;

UPROPERTY(meta = (BindWidget))
UXmlButton* BtnClose = nullptr;

UPROPERTY(meta = (BindWidget))
UXmlButton* BtnClaim = nullptr;
```

Whether the representative child remains in the final baked tree or is removed before runtime population is a programming decision; call it out rather than assuming a list framework that XmlUI does not provide.

## Runtime Texture Placeholder

Use a solid brush when the asset is unknown:

```xml
<Image Name="HeadIcon" Brush="#FF8C6A4A" DesiredSize="256,256"/>
```

Record its mapping in the generation note, then replace it in code:

```cpp
if (HeadIcon)
{
    HeadIcon->SetXmlTexture(Texture);
}
```

Do not set a non-white `Color` on a runtime-replaced image unless the texture should remain tinted. `SetXmlTexture` clears the brush tint but not the image's separate color multiplier.
