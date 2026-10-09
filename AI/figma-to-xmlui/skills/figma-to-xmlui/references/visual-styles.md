# Native UMG visual styles in XmlUI

`Visual.<PropertyName>` serializes approved native visual UPROPERTYs using Unreal
`FProperty::ExportText_InContainer` and `ImportText_Direct`.
It does **not** use JSON or a handwritten representation of Slate structures.
An exporter-generated value is preferable to writing raw Unreal struct text.
The exporter XML-escapes nested quotes and ampersands.

## Allowlisted properties

- Every tag: `RenderTransform`, `RenderTransformPivot`, `Clipping`.
- Text: `Font`, `StrikeBrush`, `Margin`, `LineHeightPercentage`,
  `MinDesiredWidth`, `AutoWrapText`, `WrappingPolicy`,
  `TextTransformPolicy`, `OverflowPolicy`.
- Button: `WidgetStyle`, `BackgroundColor`, `ColorAndOpacity`.
- CheckBox: `WidgetStyle`.
- ProgressBar: `WidgetStyle`, `BarFillType`, `BarFillStyle`, `IsMarquee`.
- Image: `Brush`.
- Border: `Background`, `BrushColor`, `ContentColorAndOpacity`.

Only persistent editable properties on the target class are supported. Unsupported,
missing, or unparseable visual attributes abort the build/bake with diagnostics.

## Precedence

`Style` remains an independent CommonUI/host style-class reference.
Concise fields are applied before `Visual.*`, so the full native structure is
authoritative except for `FontSize`, `FontPath` and `Typeface`, which explicitly
override those subfields in `Visual.Font`.

Example syntax, **not** a valid literal without expanding each field:

```xml
<Button Name="Action" Class="/Script/UMG.Button"
        Visual.WidgetStyle="(Normal=...,Hovered=...,Pressed=...,Disabled=...)"/>
```

Generate the real value by exporting an existing WBP. These attributes preserve
a curated subset of native UMG visual state, not arbitrary Blueprint bindings,
animations, every CommonUI-specific style, or all possible Slate properties.
UE5.8 compilation and editor round-trip verification remain necessary.
