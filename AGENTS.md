# AGENTS.md — XmlUI Plugin

Declarative UMG plugin: an XML DSL is parsed into `FXmlNodeDesc`, then either baked into a Widget Blueprint asset with a matching C++ `BindWidget` structure (the main story: "one XML DSL → WBP + corresponding C++ code structure"), or built into a runtime `UWidget` tree for quick preview. Cross-project and host-agnostic: it must never reference any host-game module — keep it that way. Open source on GitHub: `Dqz00116/XmlUI`, MPL-2.0.

## Read these first
- Host-agnostic rule: this repo must stay free of any host-project specifics (module names, build scripts, VCS details, machine paths). When the plugin is integrated into a game project, follow that project's own instruction files for host-side workflows.
- `README.md` (English, primary) / `README_zh.md` (Simplified Chinese) — they are line-for-line mirrors (same line count and blank-line positions); always update both, plus the language-switcher links.
- DSL spec: `AI/figma-to-xmlui/skills/figma-to-xmlui/references/xmlui-dsl.md`. The skill's `SKILL.md` is the source of truth for the Figma→XmlUI workflow — update it (and `references/cpp-contract.md`) when builder behavior changes.

## Version control (git)
- The plugin folder is its own git repo: branch `main`, remote `origin` = `git@github.com:Dqz00116/XmlUI.git` (SSH). Commit and push from this directory.
- `LICENSE` (MPL-2.0) is repo-owned — do not delete or change it without the owner's approval.
- Push to GitHub only after the user explicitly approves; the user reviews before pushing.
- `.gitignore` excludes `Binaries/`, `Intermediate/`, `DerivedDataCache/`, `Saved/`, `.svn/`, `.vs/`, `.vscode/`, `*.user` — never `git add` build artifacts.

## Version policy
- The plugin version is declared in `XmlUI.uplugin` (`VersionName`; `Version` is the integer counterpart) and mirrored as the shield badge at the top of both READMEs.
- A version bump must change `.uplugin` and the README badge together in one change — never one without the other.
- `CreatedBy` / `CreatedByURL` in `.uplugin` identify the author; keep them accurate.

## Layout
- `Source/XmlUI` — Runtime module; deps: Core, CoreUObject, Engine, UMG, Slate, SlateCore, InputCore, XmlParser. LoadingPhase Default.
- `Source/XmlUIEditor` — Editor module; deps: XmlUI + UnrealEd, ToolMenus, UMGEditor, Kismet, AssetTools, DesktopPlatform, LevelEditor, DeveloperSettings. Baker lives in `Private/XmlUI/`.
- `Config/DefaultXmlUI.ini` — `UXmlUISettings` (UDeveloperSettings, category `XmlUI`): `BaseWidgetClass`, `XmlRootPath`, `BakedBlueprintOutputPath` (default `/Game/UI`), `WidgetClassMap`.
- No `Content/` (`CanContainContent: false`), no tests, no plugin-local build scripts.

## Data flow
XML DSL → `UXmlDslParser::ParseXmlString` (wraps `FXmlFile`) → `FXmlNodeDesc`
→ `UXmlBuilder::BuildNode` → runtime widget tree (preview), or
→ `FXmlUIBaker::BakeDslToWidgetBlueprint` → WBP asset (editor menu, ToolMenus).

WBP asset → `FXmlUIDslExporter` → DSL (editor menu or the `XmlUI.ExportWbp` console command); baked assets carry the package metadata `XmlUI.SourceDsl`/`XmlUI.SourceHash`/`XmlUI.BakeVersion` — the original DSL text plus its MD5 — as a baseline for future incremental updates.

Tags handled: `XmlUI`/`Vertical`/`Horizontal`→`UXmlPanel`, `Overlay`→`UOverlay`, `SizeBox`→`USizeBox`, `Text`, `Image`, `Button` (max 1 child), `Spacer`, `ProgressBar`, `WrapBox`→`UWrapBox`, `Grid`→`UUniformGridPanel`, `ScrollBox`→`UScrollBox`, `Canvas`→`UCanvasPanel`, `MenuAnchor`→`UMenuAnchor`, `Border`→`UBorder`, `UserWidget` (nested WBP reference). Unknown tags are skipped with an error string.

## Quirks — do not "fix" these
- `FXmlNodeDesc::Children` is intentionally **not** a `UPROPERTY` (UHT cannot reflect recursive arrays).
- API macro is `XMLUI_API` (all caps, not `XmlUI_API`).
- `UXmlBuilder` is a static-only `UObject` (namespace wrapper for Blueprint exposure) — no instance state.
- Color parsing supports `#RRGGBB`, `#AARRGGBB` (channel order swap), and `(R,G,B,A)` float form.
- Brush attributes use the Unicode arrow `→` as type/asset delimiter: `Texture2D→/Game/UI/Tex.Tex`.
- `SXmlPanel` uses modern Slate registration: `SLATE_DECLARE_WIDGET_API` + `SLATE_ADD_PANELCHILDREN_DEFINITION`; generated headers via `UE_INLINE_GENERATED_CPP_BY_NAME`.
- Code comments are English; `README.md` is English with `README_zh.md` mirroring it in Simplified Chinese.
- Code comments are English and minimal: keep a comment only when it carries contract or warning information the code cannot express; never restate the code.

## Build & verify
- No build scripts in the plugin — it compiles as part of whichever host project integrates it; there is no standalone build.
- No automated tests exist in this repo; verification is a clean compile in the integrating project plus a manual editor check (the bake menu appears via ToolMenus in the editor).
