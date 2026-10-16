# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Experimental whole-subtree deletion in Structural Re-bake: remove an existing nested container and every descendant only when the parent panel is supported, the entire subtree is absent from the target XML, and all descendant dependency checks pass. Evacuate every removed UObject to the transient package before pruning each widget GUID; verify remaining identities and compiled hierarchy. Regression covers recursive deletion, a descendant animation binding and rejected partial subtree deletion.
- Experimental guarded deletion of unreferenced leaf Text/Image/Spacer widgets in existing Vertical/Horizontal/Overlay panels. Preflight detects native/BindWidget properties, Blueprint graph variables/pins, UMG designer bindings, animation tracks and ambiguous GetWidgetFromName calls; referenced widgets and subtree deletion are rejected.
- Deleted widget GUIDs are removed before compilation, while all surviving UObject identities/GUIDs are verified; rollback retains complete original asset backup. Added regression tests for positive leaf deletion, designer/animation dependency refusal and subtree deletion rejection.
- Experimental cross-panel leaf reparenting in Structural Re-bake: move existing Text/Image/Spacer between existing VerticalBox/HorizontalBox/Overlay panels while preserving widget UObject identities and GUIDs. Copy destination slot from the staged WBP, detach all moves first, reject non-leaf reparenting and non-slot property changes.
- Preview now reports moved widgets explicitly rather than as unrelated removal/addition; regression covers a VerticalBox-to-Overlay transfer, identity preservation, and move+property conflict rejection.
- Explicit `XmlUI.AdoptBaseline` for unmanaged or duplicated WBP copies: require confirmed user action, successful disk round-trip, missing metadata and a persistent backup before writing source metadata.
- Reject unmanaged previews as not ready for Apply; remove lingering tool-owned temporary uasset files after editor asset deletion to reduce AssetRegistry re-discovery warnings.
- Limited structural `XmlUI.RebakeApply` support: append/insert leaf Text/Image/Spacer nodes and reorder existing children in VerticalBox/HorizontalBox/Overlay while preserving previous widget and slot object identities. Fail closed on deletes, reparenting, class changes, mixed property/structure edits, and unsupported panels; verify compiled tree and keep file backup.
- Structural patch regression test for insert, reorder, identity preservation and deletion rejection.
- Name-based structural diffs now distinguish reorder from property changes.
- Experimental `XmlUI.RebakeApply` command: repeat preview/conflict checks, reject structural and unsupported changes, copy allowlisted staged properties into existing WBP widgets without replacing them, compile/verify/save with a persistent .uasset backup and best-effort rollback.
- Editor-only `XmlUI.RebakePreview` dry-run: stage candidate through bake/compile/save/reload, report normalized widget/property diff, compare saved XmlUI baseline to detect manual changes, never modify source WBP.
- Automated diff regression test (`XmlUI.RebakePreview.Diff`).
- Editor-only `XmlUI.ValidateRoundTrip` console command: export, bake, compile, save, reload and compare the serialized DSL surface with widget/attribute diagnostics; verify reloaded compiled text fonts.
- Automated node comparison regression test (`XmlUI.RoundTrip.NodeCompare`).
- Allowlisted native visual properties round-trip through `Visual.*` DSL attributes.
- Export full visual font, state-style and brush snapshots rather than class-default deltas, and refresh text font changes through UMG.
- Native-parent editable Widget Blueprint defaults round-trip as root `Default.<PropertyName>` attributes.
- Native UMG is now the default backend for Vertical/Horizontal/Text/Image/Button/Spacer/ProgressBar; legacy `UXml*` widgets remain available as fallback.
- Runtime `BuildFromString` now honors the same `WidgetClassMap` as editor baking.
- Native `UButton` preserves the `Text=\"...\"` shorthand by materializing a real Text child widget.
- Optional strict DSL validation (enabled by default) that rejects unknown tags, attributes, and malformed typed values before runtime construction or editor baking.
- Slot-aware attribute validation: child attributes are checked against the actual parent container semantics (Canvas, Grid, Overlay, linear panels, etc.).
- Typed value validation for ranges, colors, vectors/margins, alignments, booleans, ScaleBox enums, and brush resource-path syntax.

### Fixed
- UE5.8 UMG compiler ensure on guarded leaf deletion: `UWidgetTree::RemoveWidget()` detached the widget from the hierarchy but kept its UObject under the source `WidgetTree`. Move the removed object into the transient package with a unique name before pruning `WidgetVariableNameToGuidMap`; reject if source ownership remains. Regression verifies that the detached object no longer belongs to the source tree.
- Fix incomplete rollback at `TestFailAt=AfterMutation`: after verifying the restored .uasset bytes, clear the source package's dirty flag before `ReloadPackages`, restoring it if reload fails. No package dirty state is cleared when disk recovery is unverified.
- Require a genuine structural diff for dev-only `TestFailAt` scenarios; no-op XML must not be used as evidence that Structural Re-bake rollback works.
- Fix UE5.8 compile error in Structural Re-bake rollback verification: `FMetaData::GetValue` requires mutable metadata.
- Failed Re-bake Apply restores the complete backed-up .uasset, verifies the bytes, reloads the WBP and checks its DSL, source metadata and widget/animation GUID map; incomplete recovery is now reported as an error.
- Dev-only `TestFailAt=AfterMutation|AfterCompile|AfterSave` fault injection for testing rollback before compilation, after compilation and after saving.
- Added byte-for-byte backup-restore regression assertions.
- Fixed the actual UE5.8 handled ensure in `FWidgetBlueprintCompilerContext::ValidateAndFixUpVariableGuids`: Structural Re-bake now assigns stable GUIDs to newly inserted UWidgets before compile, preserving existing widget/animation GUIDs and restoring the map on rollback. This also repairs missing GUIDs left by previous XmlUI structural Apply versions.
- Structural Apply marks the Blueprint as modified and performs one explicit full compile; the earlier change to `MarkBlueprintAsStructurallyModified` alone did not address the missing-GUID ensure.

## [1.0.0] - 2026-09-09

### Added
- `ScaleBox` tag support.
- `FontFamily` attribute for `Text`/`Button`, resolved via the host-configured `FontFamilyMap` setting.
- `UserWidget` tag `SlotName` children: fill named slots of nested Widget Blueprints, validated against exposed slots at bake time.
- Reflected text attributes with exporter round-trip support.

### Changed
- Bakes now fail when required `BindWidget`s are missing.
- Config section renamed from `XmlUIEditor` to `XmlUI` (noted for 0.1.0 upgraders).

### Fixed
- `FString::ToString` compile error in the DSL exporter font lookup.
- Bake compile failure leaving `OutError` empty on `BindWidget` mismatch.
- Font export guard/caching issues.
- C4706 assignment-in-conditional warning in the font family cache.
- Nested `UserWidget`s in asset trees are now constructed uninitialized so `SetContentForSlot` records bindings.
- Bake errors are appended instead of discarding accumulated builder diagnostics.

## [0.2.0] - 2026-08-03

### Added
- `WidgetClassMap` tag mapping, `WrapBox`/`Grid` tags.
- WBP → DSL export (editor menu and `XmlUI.ExportWbp` console command); baked assets carry `XmlUI.SourceDsl`/`XmlUI.SourceHash`/`XmlUI.BakeVersion` package metadata.
- `Canvas`, `MenuAnchor`, `Border` tags.
- Documented WBP ↔ DSL round-trip workflow in SKILL.md.

### Changed
- Refactored builder/exporter for readability; fixed `XmlUI`/`Vertical` mapping ambiguity.

## [0.1.0] - 2026-08-02

### Added
- Initial open source release: XML DSL → `FXmlNodeDesc` parsing, runtime widget tree builder (`UXmlBuilder`), WBP baker (`FXmlUIBaker`), and the Figma → XmlUI skill.

[1.0.0]: https://github.com/Dqz00116/XmlUI/releases/tag/v1.0.0
[0.2.0]: https://github.com/Dqz00116/XmlUI/compare/v0.1.0...563747f
[0.1.0]: https://github.com/Dqz00116/XmlUI/releases/tag/v0.1.0
