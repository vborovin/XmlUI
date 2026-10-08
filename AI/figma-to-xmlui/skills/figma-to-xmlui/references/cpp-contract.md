# C++ Contract Reference

The C++ class establishes the binding boundary between a baked Widget Blueprint and runtime logic. Keep it minimal: bind only widgets that code reads, modifies, populates, or listens to.

## Determine Whether a Contract Applies

- Generate a contract when the host project has a C++ module and the user wants the normal XML/C++ pair.
- In a Blueprint-only project, generate XML without a C++ class. Omit root `ParentClass` unless the project provides a loadable class path; the baker then uses `BaseWidgetClass` from XmlUI settings.
- Honor an explicit XML-only request even in a C++ project.

## Resolve Host Conventions First

Read the `XmlUISettings` configuration and the host instead of copying names from examples. Derive:

| Item | Source of truth | Fallback only when the project has no convention |
|---|---|---|
| Runtime module | `.uproject`, target module, neighboring UI classes | Ask which module owns the widget |
| Export macro | Existing exported class in that module | Conventional module API macro |
| Base class | `XmlUISettings.BaseWidgetClass` (config-driven, the single source; project-independent) | `UUserWidget` |
| Class name | Neighboring UI classes | `U<RootName>Widget` |
| Header/source path | Existing UI source tree | `Source/<Module>/Public/UI/` and `Private/UI/` |
| Pointer style | Neighboring reflected classes | Raw reflected pointer initialized to `nullptr` |
| Parent class path | Resolved module and class | `/Script/<Module>.<ClassNameWithoutU>` |

Do not mechanically derive the API macro when an existing class can confirm it; module names with acronyms or custom build rules may not match a naive uppercase conversion.

## Binding Type Map

| DSL tag | C++ property type | Header for method calls in `.cpp` |
|---|---|---|
| `Text` | `UXmlTextBlock*` | `XmlWidget.h` |
| `Image` | `UXmlImage*` | `XmlWidget.h` |
| `Button` | `UXmlButton*` | `XmlButton.h` |
| `Spacer` | `UXmlSpacer*` | `XmlWidget.h` |
| `ProgressBar` | `UXmlProgressBar*` | `XmlWidget.h` |
| `XmlUI` / `Vertical` / `Horizontal` | `UXmlPanel*` | `XmlPanel.h` |
| `Overlay` | `UOverlay*` | `Components/Overlay.h` |
| `SizeBox` | `USizeBox*` | `Components/SizeBox.h` |
| `ScaleBox` | `UScaleBox*` | `Components/ScaleBox.h` |
| `WrapBox` | `UWrapBox*` | `Components/WrapBox.h` |
| `Grid` | `UUniformGridPanel*` | `Components/UniformGridPanel.h` |
| `ScrollBox` | `UScrollBox*` | `Components/ScrollBox.h` |
| `Canvas` | `UCanvasPanel*` | `Components/CanvasPanel.h` |
| `MenuAnchor` | `UMenuAnchor*` | `Components/MenuAnchor.h` |
| `Border` | `UBorder*` | `Components/Border.h` |
| `UserWidget` | `UUserWidget*` | `Blueprint/UserWidget.h` |

A host can override the binding type of any tag through `XmlUISettings.WidgetClassMap` (for example `Text` → `USampleTextBlock*`, `Vertical` → `UVerticalBox*`); prefer the configuration when generating the contract, and use the default map above only for tags that are not configured.

Text presentation remains in the DSL rather than the C++ binding contract. `FontFamily`, direct round-trip `FontPath`, and `Typeface` are baked into the text widget unless runtime code explicitly needs to change them.

Containers are usually presentation-only, but bind one when code must add, remove, or inspect children. A dynamic reward list, for example, can bind its `Horizontal` host as `UXmlPanel*`.

## Header Rules

- Include `CoreMinimal.h` and the selected base-class header.
- Keep `<ClassName>.generated.h` as the final include.
- Forward-declare pointer member types instead of including every XmlUI widget header in the public header.
- Use `UPROPERTY(meta = (BindWidget))` and make the property name exactly equal to the DSL `Name`.
- Follow neighboring classes for `protected`/`private`, raw pointers versus `TObjectPtr`, and initialization style.
- Do not add an empty constructor. Unreal supplies the base constructor path; declare one only when initialization work is required.
- Do not declare methods without providing definitions unless they are intentionally pure virtual or inline.

## Portable Sample Context

The following parseable example assumes a fictional host with these conventions:

- Module: `SampleGame`
- API macro: `SAMPLEGAME_API`
- Widget class pattern: `U<RootName>Widget`
- Public include root: `Source/SampleGame/Public/UI/`

Replace every sample convention with values discovered in the real host.

```xml
<XmlUI Name="ProfileCard" ParentClass="/Script/SampleGame.ProfileCardWidget">
    <Vertical Name="ContentColumn">
        <Image Name="HeadIcon" Brush="#FF8C6A4A"/>
        <Text Name="PlayerNameText" Text="Player" ArtFontSize="48"/>
        <Button Name="BtnInspect" Text="Inspect"/>
    </Vertical>
</XmlUI>
```

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ProfileCardWidget.generated.h"

class UXmlButton;
class UXmlImage;
class UXmlTextBlock;

UCLASS(BlueprintType)
class SAMPLEGAME_API UProfileCardWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    UPROPERTY(meta = (BindWidget))
    UXmlImage* HeadIcon = nullptr;

    UPROPERTY(meta = (BindWidget))
    UXmlTextBlock* PlayerNameText = nullptr;

    UPROPERTY(meta = (BindWidget))
    UXmlButton* BtnInspect = nullptr;
};
```

Static nodes such as `ContentColumn`, backgrounds, frames, and spacers are omitted because this example does not access them at runtime.

## Runtime Implementation Pattern

Add a `.cpp` only when the requested artifact includes actual logic. Include concrete widget headers there because method calls require complete types.

```cpp
#include "UI/ProfileCardWidget.h"

#include "XmlButton.h"
#include "XmlWidget.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ProfileCardWidget)

void UProfileCardWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();

    if (BtnInspect)
    {
        BtnInspect->OnClicked.AddDynamic(this, &UProfileCardWidget::HandleInspectClicked);
    }
}
```

Add the matching declarations to the header; the handler needs `UFUNCTION()` for `AddDynamic`:

```cpp
virtual void NativeOnInitialized() override;

UFUNCTION()
void HandleInspectClicked();
```

Do not generate speculative business logic. If behavior is unknown, keep the contract to properties and place a short TODO in the class, such as `// TODO(programmer): bind reward data and claim action.`

## Runtime APIs

| Widget | API | Important behavior |
|---|---|---|
| `UXmlTextBlock` | `SetXmlText(const FText&)` | Convert `FString` with `FText::FromString` |
| `UXmlTextBlock` | `SetXmlColor(const FLinearColor&)` | Updates stored and live Slate color |
| `UXmlImage` | `SetXmlTexture(UTexture2D*)` | Replaces the resource and clears `Brush.TintColor`; separate `Color` still multiplies the image |
| `UXmlButton` | `SetXmlButtonText(const FText&)` | Affects the built-in label only; custom-child buttons have no built-in label |
| `UXmlButton` | `SetXmlButtonColor(const FLinearColor&)` | Updates button tint/style |
| `UXmlButton` | `OnClicked` | Dynamic multicast delegate |
| `UXmlProgressBar` | `SetXmlPercent(float)` | Use values in the `0..1` range |

Example text update:

```cpp
PlayerNameText->SetXmlText(FText::FromString(PlayerName));
LevelText->SetXmlText(FText::FromString(FString::Printf(TEXT("LV.%02d"), Level)));
```

## ParentClass, Dependency, and Build Order

The XML root references the generated class without the Unreal `U` prefix — the C++ class `U<ClassName>` maps to `/Script/<Module>.<ClassName>` (the leading `U` is dropped; UClass::GetPathName() and the XmlUI exporter never emit it). A doubled prefix such as `/Script/SampleGame.USampleWidget` fails the bake with "Failed to load ParentClass '...'; check the path" even though the class is fully registered — the path string, not the class, is wrong. In the sample host this is:

```xml
<XmlUI Name="ProfileCard" ParentClass="/Script/SampleGame.ProfileCardWidget"/>
```

Add `XmlUI` to the host module's dependency list when its C++ code exposes or calls XmlUI types. Use a public dependency when XmlUI types appear in that module's public headers; otherwise prefer a private dependency.

Compile the editor target before baking so `LoadClass<UUserWidget>` can resolve `ParentClass`.

The binding contract is exact:

```text
DSL Name == UPROPERTY member name
DSL tag  == compatible UPROPERTY type
```

A mismatch can compile the C++ class but fail Widget Blueprint compilation or leave an expected binding unresolved.

## Contract Checklist

- Contract omitted cleanly for Blueprint-only or XML-only output.
- Real module name, API macro, base class, class name, and source path.
- `generated.h` included last.
- Forward declarations in the header; concrete headers in `.cpp` when methods are called.
- Only runtime-accessed widgets bound, including containers only when needed.
- Property names and types match DSL exactly.
- No empty constructor or undefined method declarations.
- `ParentClass` points to the compiled class.
- Host module depends on `XmlUI` at the correct visibility.
- Event handlers are `UFUNCTION` when bound through dynamic delegates.
