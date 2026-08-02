<p align="center">
  <picture>
    <source srcset="./Resources/Brand/xmlui-wordmark-dark.svg" media="(prefers-color-scheme: dark)">
    <source srcset="./Resources/Brand/xmlui-wordmark-light.svg" media="(prefers-color-scheme: light)">
    <img src="./Resources/Brand/xmlui-wordmark-light.svg" width="520" alt="XmlUI">
  </picture>
</p>

<p align="center">One XML DSL generates Widget Blueprints and the corresponding C++ code structure.</p>

<p align="center">
  <img alt="Version" src="https://img.shields.io/badge/version-0.1.0-8b5cf6?style=flat-square" />
  <img alt="Unreal Engine" src="https://img.shields.io/badge/Unreal%20Engine-5.5-0E1128?style=flat-square" />
  <img alt="License" src="https://img.shields.io/badge/license-MPL--2.0-orange?style=flat-square" />
</p>

<p align="center">
  <a href="./README.md">English</a> |
  <a href="./README_zh.md">简体中文</a>
</p>

---

### Overview

XmlUI is a cross-project declarative UI plugin: define your UI in one XML DSL, bake it into a Widget Blueprint asset with a single menu command in the editor, and get the matching C++ code structure (`ParentClass` + `BindWidget`). UI and code share one source — no manual widget layout or event wiring.

```mermaid
flowchart LR
    XML["One XML DSL"] --> WBP["Widget Blueprint asset"]
    XML --> CPP["Matching C++ code structure<br/>ParentClass + BindWidget"]
```

### Why XmlUI?

**Built for AI — precise and fast.** The mainstream approach makes the model drive Unreal MCP and write Python/Lua editor scripts to build UMG node by node — long pipelines, slow execution, and unstable results. XmlUI's DSL is an AI-native declarative interface: the UI is just an XML document produced in one pass, no scripts to debug, and the baked WBP is precise and predictable — generation becomes several times more efficient, letting AI genuinely power UMG development.

**One DSL, two artifacts.** The same XML defines both the WBP layout and its C++ binding structure, so UI and code always correspond and never drift apart; during development you can also preview directly from XML without waiting for a bake.

**No host-project lock-in.** The plugin source does not depend on any specific game module; module names, API macros, base classes, design resolution, fonts, and source layout are all up to the host project.

**Keeps the Unreal workflow intact.** Baked Widget Blueprints flow through the normal UMG and asset management pipeline, and C++ keeps using `BindWidget`, events, and runtime data injection — just like hand-authored UI.

**Optional Figma assistance.** The bundled Agent Skill turns Figma selections or screenshots into XmlUI skeletons and host-adapted C++ contracts.

> [!TIP]
> XmlUI depends solely on Unreal Engine modules. Once the source plugin is rebuilt inside the target project, build IDs, absolute paths, and module data from the previous host are completely gone.

### Quick Start

**1. Install the source plugin**

Copy this directory to:

```text
<TargetProject>/Plugins/XmlUI/
```

A source package should include `XmlUI.uplugin`, `Source/`, `Config/`, `Resources/`, optionally `AI/`, and the documentation.

> [!WARNING]
> Do not copy `Binaries/` or `Intermediate/` between projects. They contain host- and toolchain-specific generated data and must be rebuilt by the target project.

**2. Regenerate and compile the Editor Target**

Regenerate the project files and compile with the target project's own Unreal toolchain. If a host C++ module uses XmlUI types, add the dependency to its `.Build.cs`:

```csharp
PublicDependencyModuleNames.Add("XmlUI");
```

Use `PrivateDependencyModuleNames` when XmlUI is referenced only from `.cpp` files; use a public dependency when public headers expose XmlUI types.

**3. Write the DSL**

```xml
<XmlUI Name="RewardCard">
    <Vertical Name="ContentColumn" Padding="16,12">
        <Text Name="TitleText" Text="Gift Pack" ArtFontSize="26" Color="#FFFFFFFF" Justification="Center"/>
        <Spacer Name="SpaceTitleToBar" Size="12"/>
        <ProgressBar Name="RewardBar" Percent="0.6" FillColor="#FF00C853"/>
        <Spacer Name="SpaceBarToButton" Size="16"/>
        <Button Name="BtnClaim" Text="Claim" ButtonColor="#FF263238" TextColor="#FFFFFFFF" HAlign="Center"/>
    </Vertical>
</XmlUI>
```

**4. Bake the Widget Blueprint**

Level Editor main menu → **XmlUI** → **XmlUI: Bake DSL to Widget Blueprint**.

By default, `RewardCard.xml` bakes to `/Game/UI/WBP_RewardCard`. The baker never overwrites an existing asset, so delete the old asset or pick a different output name before rebaking.

For a quick preview during development, you can also build the UI directly at runtime via `UXmlBuilder::BuildFromString`:

```cpp
FString Error;
UWidget* Root = UXmlBuilder::BuildFromString(this, XmlContent, Error);
```

### DSL Reference

**Tags**

| Category | Tags | Behavior |
|---|---|---|
| Root/linear containers | `XmlUI`, `Vertical`, `Horizontal` | Build `UXmlPanel`; `XmlUI` and `Vertical` are vertical |
| Stacking container | `Overlay` | Builds `UOverlay` with child alignment and padding |
| Single-child container | `SizeBox` | Fixes or constrains desired size; only the first child is used |
| Elements | `Text`, `Image`, `Button` | Text, image/color block, and button |
| Helpers | `Spacer`, `ProgressBar` | Spacing and left-to-right progress |

**Common attributes**

| Scope | Attributes |
|---|---|
| Common | `Name`, `Visibility`, `RenderOpacity` |
| Text | `Text`, `FontSize`, `ArtFontSize`, `Color`, `Justification`, `WrapTextAt`, `ShadowColor`, `ShadowOffset` |
| Image | `Brush`, `Color`, `DesiredSize` |
| Button | `Text`, `ButtonColor`, `TextColor`, `Padding` |
| Progress | `Percent`, `FillColor` |
| Linear slots | `Padding`, `HAlign`, `VAlign`, `SizeParam` |

- Colors accept `#RRGGBB`, `#AARRGGBB`, and `(R,G,B,A)`; when alpha is involved, prefer `#AARRGGBB`.
- Resource brushes require an actual object path, e.g. `Texture2D→/Game/UI/T_Icon.T_Icon`. Until the asset is imported, use a solid-color placeholder.
- `ArtFontSize` uses the plugin's built-in Figma-size lookup; projects that do not adopt this mapping should convert sizes themselves and use `FontSize` instead.

See the [XmlUI DSL Reference](./AI/figma-to-xmlui/skills/figma-to-xmlui/references/xmlui-dsl.md) for the full behavior and edge cases.

### C++ Binding

The root node binds to a host C++ class via `ParentClass`; every `Name` in the XML maps to a `BindWidget` property in C++, and the two must correspond one-to-one.

`SampleGame` below only illustrates the path format; substitute your actual host module and class names.

```xml
<XmlUI Name="ProfileCard" ParentClass="/Script/SampleGame.ProfileCardWidget">
    <Text Name="PlayerNameText" Text="Player" FontSize="24"/>
</XmlUI>
```

The property name and type must match the DSL exactly:

```cpp
UPROPERTY(meta = (BindWidget))
UXmlTextBlock* PlayerNameText = nullptr;
```

Without `ParentClass`, the baker falls back to the configured `BaseWidgetClass`, so Blueprint-only projects can generate their UI purely from XML.

### Configuration

Plugin defaults live in `Config/DefaultXmlUI.ini`; a host project can override the same section in its own `Config/DefaultXmlUI.ini`:

```ini
[/Script/XmlUIEditor.XmlUISettings]
BaseWidgetClass=/Script/UMG.UserWidget
XmlRootPath=
BakedBlueprintOutputPath=/Game/UI
```

| Key | Purpose | Default |
|---|---|---|
| `BaseWidgetClass` | Default parent class for baked Widget Blueprints | `/Script/UMG.UserWidget` |
| `XmlRootPath` | Initial directory for the XML file picker | Empty; falls back to the project root |
| `BakedBlueprintOutputPath` | Output directory for Widget Blueprints | `/Game/UI` |

### AI-Assisted Workflow

`AI/figma-to-xmlui/` contains an Agent Skills-compatible Figma conversion workflow for Claude Code, OpenCode, and screenshot-based input.

```mermaid
flowchart LR
    Figma["Figma link / screenshot"] --> Skill["figma-to-xmlui Skill"]
    Skill --> Inspect["Resolve host module<br/>scale and font policy"]
    Inspect --> XML["XmlUI XML"]
    Inspect --> CPP["Optional C++ BindWidget contract"]
    XML --> Bake["Widget Blueprint bake"]
```

Load it locally in Claude Code:

```powershell
claude --plugin-dir "Plugins/XmlUI/AI/figma-to-xmlui"
```

See the [Figma to XmlUI README](./AI/figma-to-xmlui/README.md) for detailed installation, Figma MCP, and Skill instructions.

### File Structure

```text
XmlUI/
├── AI/figma-to-xmlui/             Figma MCP + Agent Skill workflow
├── Config/DefaultXmlUI.ini        Plugin defaults
├── Resources/Brand/               Light/dark SVG wordmarks for the README
├── Source/XmlUI/                  Runtime: DSL parsing and UI widgets
├── Source/XmlUIEditor/            Editor: settings and Widget Blueprint baking
├── XmlUI.uplugin                  Plugin descriptor and module declarations
├── README.md                      English (primary)
└── README_zh.md                   Simplified Chinese
```

### Requirements

- Unreal Engine 5.5 serves as the current development and verification baseline.
- To use another engine version, recompile in the target project and verify API compatibility.
- The Figma AI workflow is optional — the core features do not depend on MCP.

<details>
<summary>Known limitations and baking notes</summary>

- There are currently no tags for `Canvas`, `Border`, input fields, sliders, list views, gradients, rounded corners, blur, or animations.
- Every baked node must have a non-empty, legal, globally unique `Name`.
- Keep XML generation notes inside the `<XmlUI>` root node; a comment before the root can break Unreal's XML parser.
- The root has no parent slot, so root `Padding`, `HAlign`, `VAlign`, and `SizeParam` have no effect.
- Unknown tags are skipped and unknown or malformed attributes are generally ignored, so a successful bake does not guarantee a complete layout.
- `Button` and `SizeBox` use at most one child; extra children are ignored.

</details>
