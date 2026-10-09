---
name: figma-to-xmlui
description: Convert selected Figma frames, component links, UI mockups, or screenshots into bakeable XmlUI XML for any Unreal Engine host project and, when the project has a C++ module, a matching BindWidget contract. Use for Figma-to-XmlUI, mockup-to-UMG, bakeable XML, or XmlUI widget-skeleton requests, including when the user provides only a design link or image. Preserve structural, informational, and interactive UI while omitting decorative canvas art. Also covers editing or round-tripping an existing Widget Blueprint: export it to XmlUI DSL, modify, and bake back.
compatibility: Requires the XmlUI plugin in an Unreal Engine project. Link mode requires a connected Figma MCP server; screenshot mode requires image-capable input. A C++ contract is optional and requires a host C++ module.
---

# Figma to XmlUI

Create an implementation-ready skeleton from a selected design, not a pixel-perfect reproduction. Keep the hierarchy that programming depends on, represent only syntax that XmlUI implements, and record every intentional approximation.

## Default Deliverables

Always produce the XML. Produce the C++ contract when the host has a C++ module and the user has not explicitly narrowed the request to XML:

1. `XmlUI_<RootName>.xml`, containing one bakeable `<XmlUI>` document.
2. When applicable, a minimal project-convention `UUserWidget`-derived contract whose `BindWidget` properties match runtime-accessed DSL node names.

Write the files into the host project when filesystem tools are available. If file writes are unavailable, return the complete XML first and the C++ contract second when one applies.

## Workflow

### 1. Inspect the Host Project

- Read the project's instructions before editing.
- Read the XmlUI settings: the base class comes exclusively from `XmlUISettings.BaseWidgetClass`, and widget types come exclusively from the `XmlUISettings.WidgetClassMap` configuration (tags that are not configured use the DSL default widgets). Find the `.uproject`, target module, export macro, naming conventions, source directories, design baseline, DPI policy, and font policy instead of importing assumptions from another project. Do not invent conventions that are absent from the configuration; everything is governed by the XmlUISettings configuration.
- Read [references/xmlui-dsl.md](references/xmlui-dsl.md) before mapping the design.
- Read [references/cpp-contract.md](references/cpp-contract.md) before generating the paired contract.
- Consult [references/examples.md](references/examples.md) only when a concrete layout or binding pattern is useful.

### 2. Acquire Reliable Design Context

For a Figma link:

- Accept both `/design/` and legacy `/file/` URLs.
- Extract the file key and selected `node-id`; convert URL form `1234-5678` to canonical `1234:5678` when the active tool expects it.
- Use the available Figma MCP metadata/design-context tool. Tool names vary by client, so prefer capabilities over a hard-coded name.
- Request the selected node first. Do not set optional traversal depth when the active tool contract forbids it; fetch specific structural child nodes only when more detail is needed.
- Use a rendered screenshot or preview as a visual cross-check when available. Structured data supplies geometry and text; the image helps distinguish real UI from decoration.

For an image:

- Read visible hierarchy, dimensions, spacing, typography, colors, and interaction cues from the image.
- Ask for frame dimensions only when scale cannot be inferred safely.

If the source cannot be accessed, request a screenshot or exported node. Do not invent inaccessible Figma data.

### 3. Establish Scope and Scale

- Implement the selected frame or component, not unrelated canvas siblings.
- Read the host project's documented design baseline or infer it from existing XmlUI files and UI conventions.
- When the source and target frames match, or the project explicitly uses design pixels as XmlUI units, preserve values 1:1.
- Otherwise compute `ScaleX = TargetWidth / SourceWidth` and `ScaleY = TargetHeight / SourceHeight`. Use a uniform scale only when the aspect ratios and project policy support it.
- If the target frame, DPI policy, or safe-area behavior is unknown, ask rather than assuming a fixed resolution or distorting the layout.
- Apply the selected policy consistently to dimensions, gaps, padding, offsets, and the host-selected text-size attribute, and record it in the generation notes.

### 4. Separate Structure from Decoration

Keep nodes that provide at least one of these functions:

- Layout or grouping: panels, cards, rows, columns, modal surfaces, repeated-item hosts.
- Information: titles, labels, values, status, meaningful icons.
- Interaction: buttons, tabs represented as buttons, functional image buttons, and runtime-updated controls.

Omit visual-only canvas material such as watermarks, art typography, glows, particles, vignettes, presentation annotations, and mock data used only to fill space. Keep a background when it defines the selected component's actual surface or interaction boundary; omit it when it is merely the page behind that component.

When intent is ambiguous, prefer the smallest useful structure and list the omission in the generation notes. Never emit unsupported tags for controls such as inputs or sliders; represent only a useful primitive skeleton and record the missing behavior.

### 5. Map to XmlUI

- Use only `XmlUI`, `Vertical`, `Horizontal`, `Overlay`, `SizeBox`, `ScaleBox`, `Text`, `Image`, `Button`, `Spacer`, `ProgressBar`, `WrapBox`, `Grid`, `ScrollBox`, `Canvas`, `MenuAnchor`, `Border`, and `UserWidget`.
- `UserWidget` nodes accept `Text`, `Color`, `ArtFontSize`, and `Justification` when the referenced widget class exposes matching UPROPERTYs (best-effort reflection; see xmlui-dsl.md).
- Nested WBP placeholders accept slot children via `SlotName="..."` on each child (inserted into the nested widget's UNamedSlot; see xmlui-dsl.md). Only reference Widget Blueprints with slot children — native C++ `UUserWidget` subclasses export no slot children — and keep slot names unique per node.
- The default mapping of layout containers (such as `Vertical` → `UVerticalBox`) is decided by the host's `WidgetClassMap` configuration; the Skill does not need to care about concrete classes when generating XML.
- Prefer Figma Auto Layout hierarchy over absolute coordinates.
- Use `Overlay` for genuine stacking or limited local positioning, not as a substitute for every layout.
- Use `SizeBox` when a dimension must be fixed; `DesiredSize` on `Image` is only a desired size.
- For repeated lists or grids, emit one representative item plus a named host container when runtime code needs to populate it. Do not unroll arbitrary mock data.
- Preserve native font outlines, button states and background brushes using exported `Visual.*` attributes; see `references/visual-styles.md`.
- Use a distinct solid-color `Brush` placeholder when an image asset has not been imported. Record the intended asset and replacement API in the XML note instead of inventing an object path.

### 6. Write the XML

Use this document shape; the generation note belongs inside the root because the Unreal XML parser can reject a leading comment:

```xml
<XmlUI Name="RootName">
    <!-- Generation notes
    Source: selected Figma node or screenshot description
    Scale: source dimensions and applied factor
    Omitted: decorative or out-of-scope nodes
    Unsupported: exact styles or controls that XmlUI cannot represent
    Assets: placeholder color -> intended asset -> runtime replacement
    Programming: handlers, dynamic content, and remaining decisions
    -->
    <Vertical Name="ContentColumn">
        ...
    </Vertical>
</XmlUI>
```

- Give every node a globally unique PascalCase `Name` containing only letters, digits, and underscores and starting with a letter. The baker allows a broader first character, but C++ identifiers do not.
- Follow the host font policy: when the host configures `XmlUISettings.FontFamilyMap`, write `FontFamily="Figma font name"` on `Text`/`Button` nodes and let the mapping resolve the font asset; use `Typeface` on `Text` when the source requires a specific face/weight. `FontPath` is primarily a WBP round-trip fallback for a direct UFont asset with no family alias. Use `ArtFontSize` only when the project adopts XmlUI's built-in art-size lookup; otherwise convert with the project's policy and use `FontSize`. Use `#AARRGGBB` for colors with alpha.
- Escape XML attribute text, especially `&`, `<`, `>`, and quotes.
- Do not put root spacing in root slot attributes. Wrap the content and apply spacing to that child.
- Add root `ParentClass` only after resolving a real, loadable host class; otherwise let the baker use `BaseWidgetClass`. Writing it means writing the class name WITHOUT the C++ `U` prefix: a class `USampleWidget` in C++ is `/Script/SampleGame.SampleWidget` in XML. `UClass::GetPathName()` omits the `U` and the plugin exporter writes it that way; a U-prefixed path fails the bake with "Failed to load ParentClass '...'; check the path (format /Script/<Module>.<ClassName>)". When unsure, check an existing exported WBP's `ParentClass` line.
- Do not include Markdown fences or prose in the `.xml` file.

### 7. Write the C++ Contract

- Resolve the actual module, API macro, class name, and source path from the host project.
- Base the contract class on `XmlUISettings.BaseWidgetClass` (default `UUserWidget`; a host may configure `USampleWidget` or similar).
- Prefer the `XmlUISettings.WidgetClassMap` mapping (tag → class) for member binding types; use the default map in [references/cpp-contract.md](references/cpp-contract.md) for tags that are not configured.
- When the host configures `BaseWidgetClass`, set the root XML `ParentClass` to that configured class path; otherwise let the baker use `BaseWidgetClass`.
- Bind only widgets that runtime code reads, updates, populates, or listens to. Static presentation nodes do not need properties.
- Bind a container when runtime code needs it, such as a dynamic reward-row host.
- Keep the header minimal. Add a `.cpp` only for real method or event-handler implementations; do not add a constructor that performs no work.
- For a Blueprint-only project, omit the C++ contract and omit `ParentClass` unless the user or project settings provide a loadable class path; the baker will use `BaseWidgetClass`.

Follow all type and include rules in [references/cpp-contract.md](references/cpp-contract.md).

### 8. Update an Existing Widget Blueprint

Bake (DSL→WBP) and Export (WBP→DSL) are inverse operations, so an existing Widget Blueprint can be updated incrementally:

1. Export the existing WBP to DSL: `XmlUI.ExportWbp Wbp=<asset path> Out=<xml path>` (or the editor menu "XmlUI: Export WBP to DSL").
2. Modify the XML as needed.
3. Bake it back: `XmlUI.BakeDsl File=<xml path>` (or the editor menu).

Important constraint: the baker does not overwrite an existing asset. Before writing back to the same asset name, delete or rename the old asset first, or choose a new output path. The exported content includes the asset's embedded `XmlUI.SourceDsl` metadata for cross-checking.

### 9. Validate and Report

Before finishing, verify:

- The XML has exactly one `<XmlUI>` root and the generation note is inside it.
- Every tag and attribute exists in the DSL reference; unknown syntax can be ignored silently by the baker.
- Names are non-empty, legal, globally unique, and identical to corresponding C++ property names.
- Colors, vectors, margins, numeric ranges, child counts, and XML escaping are valid.
- When a C++ contract is generated, `ParentClass`, module API macro, includes, forward declarations, and binding types match the host project.
- No unavailable asset path or unsupported visual feature is presented as complete.

Compile the contract before baking when `ParentClass` references it, but follow project safety instructions and never run update, cleanup, or revert scripts as a build shortcut. The editor baker creates `WBP_<sanitized complete XML basename>` under `BakedBlueprintOutputPath`, replacing spaces, hyphens, and dots with underscores; for example, `XmlUI_ProfileCard.xml` becomes `WBP_XmlUI_ProfileCard`. It does not overwrite an existing asset.

For headless round-trip editing or batch verification, `XmlUI.ExportWbp` exports an existing Widget Blueprint back to DSL for side-by-side comparison and `XmlUI.BakeDsl` runs the same bake from the console; baked assets also carry the `XmlUI.SourceDsl` package metadata for cross-checking.

Finish with the XML path, optional C++ path, expected bake path, resolved host conventions, unresolved assets/styles, and verification not performed. Keep the summary concise because the files are the deliverable.
