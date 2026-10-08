# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Exact per-node `Class` override for self-contained native/project UMG round-trips.
- Standard UMG `UTextBlock`, `UImage`, `UButton`, `UVerticalBox`, and `UHorizontalBox` export support.
- `CheckBox` DSL/build/export support, including checked-state round-trip.
- Reflected `Style` and `IsEnabled` round-trip for host/CommonUI widget subclasses.
- Optional `Out=/Game/...` override for the `XmlUI.BakeDsl` console command.

### Changed
- WBP → DSL export preserves concrete host widget classes when they differ from XmlUI's built-in wrapper classes.

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
