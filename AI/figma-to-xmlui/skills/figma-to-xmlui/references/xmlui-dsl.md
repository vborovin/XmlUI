# XmlUI DSL Reference

This reference mirrors the current implementation in:

- `Plugins/XmlUI/Source/XmlUI/Private/XmlDslParser.cpp`
- `Plugins/XmlUI/Source/XmlUI/Private/XmlBuilder.cpp`
- `Plugins/XmlUI/Source/XmlUIEditor/Private/XmlUI/XmlUIBaker.cpp`

Treat this file as the syntax source of truth. The parser and builder are permissive: many invalid attributes are ignored rather than reported, so generation must self-validate.

## Supported Tags

| Tag | Unreal type | Children | Behavior |
|---|---|---:|---|
| `XmlUI` | `UXmlPanel` | many | Required conventional root; vertical orientation |
| `Vertical` | `UXmlPanel` | many | Vertical stack |
| `Horizontal` | `UXmlPanel` | many | Horizontal row |
| `Overlay` | `UOverlay` | many | Children occupy stacked slots |
| `SizeBox` | `USizeBox` | one | Extra children are ignored with a diagnostic |
| `ScaleBox` | `UScaleBox` | one | Scales the single child; extra children are ignored with a diagnostic |
| `Text` | `UXmlTextBlock` | none | Slate text wrapper |
| `Image` | `UXmlImage` | none | Solid brush, texture, or material |
| `Button` | `UXmlButton` | zero or one | Uses built-in text when childless; extra children are ignored |
| `Spacer` | `UXmlSpacer` | none | Square desired size; the panel's main axis uses the relevant dimension |
| `ProgressBar` | `UXmlProgressBar` | none | Left-to-right fill |
| `WrapBox` | `UWrapBox` | many | Wrapping container; `WrapWidth` (float) sets the explicit wrap width; child slots use `Padding`/`HAlign`/`VAlign` |
| `Grid` | `UUniformGridPanel` | many | Equal-width grid; `Columns` (int) is documentation/validation; child slots use `HAlign`/`VAlign` plus `Row` (int) / `Column` (int) |
| `ScrollBox` | `UScrollBox` | many | Scrollable container; `Orientation` (`vertical`/`horizontal`) sets the scroll direction; child slots use `Padding`/`HAlign`/`VAlign`/`SizeParam` |
| `Canvas` | `UCanvasPanel` | many | Absolute-positioning container; child slots use `Position`/`Size`/`Anchors`/`Alignment`/`ZOrder`/`AutoSize` |
| `MenuAnchor` | `UMenuAnchor` | zero or one | Popup anchor; `Menu` (class path or asset path) selects the popup widget class; extra children are ignored |
| `Border` | `UBorder` | zero or one | Single-child background; `BrushColor` (color) and `Padding` (margin); extra children are ignored |
| `UserWidget` | `UUserWidget` | none | Nested Widget Blueprint reference; `WBP` (class path or asset path) selects the referenced widget class; children are ignored with a diagnostic |

There is no `Input`, `Slider`, `ListView`, or animation tag. An unknown child tag is skipped. An unknown root tag prevents a usable root from being built.

## Widget Class Mapping (WidgetClassMap)

`XmlUISettings.WidgetClassMap` maps DSL tags to host widget class paths (for example `Text` → `/Script/SampleGame.SampleTextBlock`). A mapped class only needs to derive from the corresponding standard UMG widget (`UTextBlock`, `UImage`, `UButton`, `UProgressBar`, `USpacer`, `UWrapBox`, `UUniformGridPanel`, and so on); the builder then configures it with the standard APIs. Tags that are not configured use the default controls listed in this reference. Common attributes such as `ColorAndOpacity` apply only to the plugin's default wrapper widgets; when a tag is mapped to a host widget, use the tag-specific attribute (such as `Color`) instead. `XmlUI` is the root alias of `Vertical`: its control mapping inherits the `Vertical` entry of `WidgetClassMap`, so it must not be configured with its own entry.

In `Config/DefaultXmlUI.ini` the map must be written as a single native map value (not per-line array syntax; per-line `Key=`/`Value=` pairs are rejected by `LoadConfig` for `TMap` properties):

```ini
[/Script/XmlUIEditor.XmlUISettings]
WidgetClassMap=(("Text","/Script/SampleGame.SampleTextBlock"),("Image","/Script/SampleGame.SampleImage"))
```

## Required Names

Editor baking recursively requires `Name` on every node. A name must:

- Be non-empty.
- Contain only `A-Z`, `a-z`, `0-9`, and `_`.
- Be globally unique within the document.

Use PascalCase beginning with a letter because names used by `BindWidget` must also be legal C++ identifiers. Prefer semantic names for runtime widgets, such as `PlayerNameText`, `BtnClaim`, `RewardsRow`, and `HpBar`. Numbered names are acceptable for purely structural repeated spacers, such as `Space1`.

## Attribute Reference

Attribute names and tag names are case-sensitive.

### Common Widget Attributes

| Attribute | Accepted values | Notes |
|---|---|---|
| `Visibility` | `Visible`, `Hidden`, `Collapsed`, `HitTestInvisible` | Compared case-insensitively; other values are ignored |
| `RenderOpacity` | float | Passed to `SetRenderOpacity` |

`ColorAndOpacity` is recognized only for `Text`, `Image`, `ProgressBar`, and `Button`. It maps respectively to text color, image color multiplier, fill color, and button color. It has no effect on layout containers, `SizeBox`, or `Spacer`.

### Text

| Attribute | Type | Notes |
|---|---|---|
| `Text` | string | Escape XML-reserved characters |
| `ArtFontSize` | integer | Uses `GetXmlFontSizeByArtFontSize` and its built-in `ArtFont2FontMap`; takes precedence when non-negative |
| `FontSize` | integer | Direct engine size; use when the host does not adopt the built-in art-size lookup |
| `Color` | color | Text color |
| `Justification` | `Left`, `Center`, `Right` | Case-insensitive |
| `WrapTextAt` | float | Wrap width |
| `ShadowColor` | color | Shadow tint |
| `ShadowOffset` | vector | `X,Y`; blur is unsupported |

The project font comes from Unreal's default widget font. XmlUI has no per-node font-family, weight, letter-spacing, outline, or rich-text support.

Choose the size attribute from host conventions. `ArtFontSize` is a plugin-provided convenience mapping, not a universal Figma scale. A project with a different typography policy should convert the source size itself and emit `FontSize`; inspect existing XML or ask before choosing.

### Image

| Attribute | Type | Notes |
|---|---|---|
| `Brush` | color or object reference | See Brush Values below |
| `Color` | color | Multiplies the brush; leave white for runtime texture replacement |
| `DesiredSize` | vector | Desired-size override, not a guaranteed allocated size |

Use `SizeBox` around an image when width or height must be enforced by layout.

### Button

| Attribute | Type | Notes |
|---|---|---|
| `Text` | string | Used only when the button has no custom child |
| `ButtonColor` | color | Normal tint; pressed and disabled colors are derived |
| `TextColor` | color | Built-in text/foreground color |
| `Padding` | margin | Button content padding |

A button can contain one custom child, such as an `Image` or a layout container. When a `Button` is itself a child of `XmlUI`, `Vertical`, `Horizontal`, or `Overlay`, the same `Padding` attribute is also read as parent-slot padding. Wrap the button in a `SizeBox` or another container when content padding and outer spacing must differ.

### Spacer

| Attribute | Type | Notes |
|---|---|---|
| `Size` | float | Produces desired size `Size,Size` |

Use a spacer in a `Vertical` for vertical gaps and in a `Horizontal` for horizontal gaps. `SizeParam="Fill"` on a spacer can consume remaining room in an XmlUI panel.

### ProgressBar

| Attribute | Type | Notes |
|---|---|---|
| `Percent` | float | Generate values in the `0..1` range |
| `FillColor` | color | Fill tint |

XmlUI does not expose background style, bar direction, marquee mode, or a size attribute. Use `SizeBox` for fixed dimensions.

### SizeBox

| Attribute | Type |
|---|---|
| `WidthOverride` | float |
| `HeightOverride` | float |
| `MinDesiredWidth` | float |
| `MinDesiredHeight` | float |
| `MaxDesiredWidth` | float |
| `MaxDesiredHeight` | float |

Only the first child is used. Slot attributes on that child are not applied by the builder.

### ScaleBox

| Attribute | Type | Notes |
|---|---|---|
| `UserDesiredWidth` | float | Not present on engine `UScaleBox`; emitted only when a host `ScaleBox` subclass carries it |
| `UserDesiredHeight` | float | Same as `UserDesiredWidth` |
| `ContentScale` | vector | Same as `UserDesiredWidth` |
| `Stretch` | enum | `None`, `Fill`, `ScaleToFit`, `ScaleToFitX`, `ScaleToFitY`, `ScaleToFill`, `ScaleBySafeZone`, `UserSpecified`, `UserSpecifiedWithClipping`; case-insensitive |
| `StretchDirection` | enum | `Both`, `DownOnly`, `UpOnly`; case-insensitive |

Only the first child is used. Slot attributes on that child are not applied by the builder.

### ScrollBox

| Attribute | Type | Notes |
|---|---|---|
| `Orientation` | `vertical` or `horizontal` | Optional; defaults to vertical |

A scrollable container with any number of children. Children use the `Padding`/`HAlign`/`VAlign`/`SizeParam` slot attributes.

### Canvas

An absolute-positioning container with no widget-level attributes; all layout comes from the child slot attributes documented in [Canvas Slot Attributes](#canvas-slot-attributes).

### MenuAnchor

| Attribute | Type | Notes |
|---|---|---|
| `Menu` | class path or asset path | Optional. Resolved exactly like `UserWidget`'s `WBP` (see below); when omitted the anchor has no popup class |

A single-child container; extra children are ignored. The child's slot has no settable attributes, so slot attributes on the child are ignored.

### Border

| Attribute | Type | Notes |
|---|---|---|
| `BrushColor` | color | Background tint of the border brush |
| `Padding` | margin | Padding between the border edge and the single child |

A single-child container; extra children are ignored. The child's slot supports `Padding`/`HAlign`/`VAlign`. As with `Button`, when a `Border` is itself a child of a linear or overlay panel, its `Padding` attribute is also read as parent-slot padding.

### UserWidget

| Attribute | Type | Notes |
|---|---|---|
| `WBP` | class path or asset path | Optional. A `/Script/<Module>.<Class>` or `_C`-suffixed path is loaded as a class directly; any other path is loaded as a Widget Blueprint asset and its generated class is used. Omit to construct a plain user widget placeholder |

A nested Widget Blueprint reference with no children; extra children are ignored with a diagnostic.

## Slot Attributes

Slot attributes belong to a child node and are interpreted according to its parent.

| Parent | `Padding` | `HAlign` | `VAlign` | `SizeParam` |
|---|---:|---:|---:|---:|
| `XmlUI` / `Vertical` / `Horizontal` | yes | yes | yes | `Auto` or `Fill` |
| `Overlay` | yes | yes | yes | ignored |
| `SizeBox` / `ScaleBox` / `Button` | ignored on child | ignored | ignored | ignored |
| `WrapBox` | yes | yes | yes | — |
| `Grid` | no — Grid slots have no padding (no SetPadding); use the container-level `SlotPadding` instead | yes | yes | — |
| `ScrollBox` | yes | yes | yes | `Auto` or `Fill` |
| `MenuAnchor` | ignored | ignored | ignored | ignored |
| `Border` | yes | yes | yes | ignored |

`HAlign` accepts `Left`, `Center`, `Right`, and `Fill`. `VAlign` accepts `Top`, `Center`, `Bottom`, and `Fill`. Values are case-insensitive.

The root has no parent slot, so `Padding`, `HAlign`, `VAlign`, and `SizeParam` on `<XmlUI>` have no effect. Wrap content in a child and apply slot spacing to that child.

### Canvas Slot Attributes

Canvas children use the following slot attributes instead of the table above.

| Attribute | Type | Notes |
|---|---|---|
| `Position` | vector | `X,Y` position of the child in canvas coordinates |
| `Size` | vector | `X,Y` allocated size |
| `Anchors` | vector | `X,Y` (uniform) or `X0,Y0,X1,Y1` (minimum, maximum); see below |
| `Alignment` | vector | `X,Y` pivot, each in `0..1` |
| `ZOrder` | integer | Render order; higher values draw on top |
| `AutoSize` | `true` or `false` | Use the child's desired size when `true` |

`Anchors` with two values sets both the minimum and maximum to the same point (`X,Y`). With four values the first pair is the minimum and the second pair is the maximum (`X0,Y0,X1,Y1`), so `0,0,1,1` stretches the child across the canvas.

## Value Formats

### Colors

The parser accepts:

- `#RRGGBB`, treated as opaque.
- `#AARRGGBB`, with alpha first.
- `R,G,B` or `R,G,B,A`, optionally wrapped in parentheses, using `0..255` channels.

Generate hexadecimal colors consistently. Convert Figma `#RRGGBBAA` to XmlUI `#AARRGGBB`. For Figma float channels, round each channel multiplied by 255 to a two-digit hex byte.

Examples:

| Figma | XmlUI |
|---|---|
| `#FFFFFF` | `#FFFFFFFF` or `#FFFFFF` |
| `#26323880` | `#80263238` |
| black at 10% alpha | `#1A000000` |

### Margins

`Padding` accepts:

- `A`: all sides.
- `H,V`: horizontal and vertical.
- `L,T,R,B`: individual sides.

Parentheses are optional. Three-value margins are invalid.

### Vectors

`DesiredSize`, `ShadowOffset`, `Position`, `Size`, and `Alignment` require exactly `X,Y`. `Anchors` accepts `X,Y` (uniform) or `X0,Y0,X1,Y1` (minimum, maximum).

### Brush Values

A `Brush` containing the Unicode arrow `→` is treated as an Unreal object reference. Text before the arrow is only a hint and is ignored by the loader; text after it must be a loadable object path.

```xml
<Image Name="IconImage" Brush="Texture2D→/Game/UI/Icons/T_Icon.T_Icon"/>
```

The actual syntax uses the single Unicode right-arrow character shown in the source and examples, not the two ASCII characters `-` and `>`. If loading fails, the builder reports no hard error and the image has no resource. Do not generate speculative paths. Prefer a solid placeholder:

```xml
<Image Name="IconImage" Brush="#FF5A7AC0" DesiredSize="96,96"/>
```

Record `#FF5A7AC0 -> intended T_Icon -> UXmlImage::SetXmlTexture` in the generation note. `SetXmlTexture` clears the brush's placeholder tint, but `Image Color` remains a separate multiplier; keep `Color` white or omit it for replaceable textures.

## Figma Mapping

| Figma concept | XmlUI pattern |
|---|---|
| Vertical Auto Layout | `Vertical` |
| Horizontal Auto Layout | `Horizontal` |
| Overlay, badge, mask-like stack | `Overlay` |
| Fixed frame | `SizeBox` containing the mapped child |
| Text | `Text` with `ArtFontSize`, `Color`, and justification |
| Solid rectangle or divider | `Image` with a color `Brush` |
| Imported/runtime image | `Image` with a real object path or solid placeholder |
| Text button | Childless `Button` |
| Icon/custom-content button | `Button` with one child |
| Progress/health bar | `ProgressBar`, usually wrapped in `SizeBox` |
| Auto Layout gap | `Spacer` or child slot `Padding` |
| Repeated list/grid | Named host container plus one representative item; runtime population noted |
| Wrapping/grouped container | `WrapBox`; repeated tag groups and variable-width items flow onto new lines |
| Equal-width grid | `Grid`; icon matrices and N-column layouts, with children placed via `Row`/`Column` |
| Free positioning | `Canvas` with `Position`/`Size`/`Anchors` child slots; or local `Overlay` with alignment/padding when only a few layers need alignment |

Do not create nodes for page-level decorative fills, glows, watermarks, design annotations, or mock-only content unless they define the selected component's real surface or behavior.

## Unsupported Visuals

Document rather than silently approximating:

- Rounded corners and custom clipping.
- Gradients.
- Strokes and text outlines.
- Blur, backdrop blur, and shadow blur radius.
- Per-node font family/weight and letter spacing.
- Rich text with mixed styles.
- Arbitrary transforms, rotation, and masking.
- Animation and interaction state styling beyond `UXmlButton` defaults.

Use a simple approximation only when it preserves useful structure, and state the exact omitted Figma value in the generation note.

## XML and Bake Behavior

- Put comments inside `<XmlUI>`. A comment before the root can fail `FXmlFile` parsing in this workflow.
- Avoid `--` inside XML comments because XML forbids double hyphens in comment bodies.
- Escape `&` as `&amp;`, `<` as `&lt;`, `>` as `&gt;`, `"` as `&quot;`, and `'` as `&apos;` when needed in attributes.
- The baker validates names but does not validate the root tag, known attributes, or successfully parsed attribute values.
- Unknown attributes and malformed values are usually ignored.
- Unknown child tags are skipped, potentially producing an incomplete but successfully saved Widget Blueprint.
- `ParentClass` on the root overrides `BaseWidgetClass` and must use `/Script/<Module>.<ClassNameWithoutU>`.
- Output is `<BakedBlueprintOutputPath>/WBP_<sanitized complete XML basename>`. The baker replaces spaces, hyphens, and dots with underscores, so `XmlUI_ProfileCard.xml` produces `WBP_XmlUI_ProfileCard`.
- Existing assets are not overwritten.

## Final Checklist

- One `<XmlUI>` root; generation note inside it.
- Supported, correctly cased tags and attributes only.
- Every node named legally and uniquely.
- One child maximum for `SizeBox`, `ScaleBox`, `Button`, `MenuAnchor`, and `Border`.
- `#AARRGGBB`, valid vectors/margins, and XML-escaped text.
- No root slot attributes relied upon.
- No speculative object paths.
- Unsupported details and runtime work listed explicitly.
