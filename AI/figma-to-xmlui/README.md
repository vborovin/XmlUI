# Figma to XmlUI

Converts a selected Figma frame or UI screenshot into portable XmlUI artifacts for an Unreal Engine host project:

- A bakeable XmlUI DSL file.
- In C++ hosts, a minimal Unreal `BindWidget` contract for the widgets used at runtime.

The result is an implementation skeleton, not a pixel-perfect final UI. It preserves layout, information, and interaction structure while documenting assets and visual effects that XmlUI cannot represent.

## Package Layout

```text
figma-to-xmlui/
|-- .claude-plugin/plugin.json
|-- .mcp.json
|-- commands/figma2xmlui.md                 # Legacy slash-command wrapper
|-- skills/figma-to-xmlui/
|   |-- SKILL.md                            # Workflow and non-negotiable rules
|   |-- references/
|   |   |-- cpp-contract.md                 # Unreal binding conventions
|   |   |-- examples.md                     # Focused output examples
|   |   `-- xmlui-dsl.md                    # Implementation-derived DSL reference
|   `-- evals/evals.json                    # Skill Creator-compatible evaluations
`-- README.md
```

`SKILL.md` is the source of truth. The command file only provides a stable manual entry point and intentionally does not duplicate the workflow.

## Requirements

- An Unreal Engine project with `Plugins/XmlUI` enabled.
- A compatible Unreal Engine toolchain for compilation and editor baking; the plugin currently targets UE 5.5.
- A Figma MCP connection for link-based input, or image-capable model input for screenshots.
- A C++ game module when generating a paired contract class. Blueprint-only projects can generate XML without one.

The workflow does not hard-code a host module, API macro, class naming scheme, source directory, design resolution, or font family. It derives those conventions from the target `.uproject`, `.Build.cs` files, existing widgets, XmlUI settings, and project instructions; when a scale policy cannot be inferred safely, it asks for the target frame dimensions.

`ArtFontSize` uses XmlUI's built-in Figma-to-engine lookup. Projects that do not adopt that lookup should apply their own conversion and emit `FontSize` instead; the Skill inspects existing XML and project guidance before choosing either attribute.

## Install

### Claude Code Plugin

Load this checkout for one session from the host project root:

```powershell
claude --plugin-dir "Plugins/XmlUI/AI/figma-to-xmlui"
```

Invoke the compatibility command with a link:

```text
/figma-to-xmlui:figma2xmlui https://www.figma.com/design/<file-key>/<file-name>?node-id=1234-5678
```

Claude can also trigger the bundled skill automatically from a natural-language request. Run `/reload-plugins` after changing plugin components during development.

### Standalone Skill

Copy the entire `skills/figma-to-xmlui/` directory, including `references/`, to one of these locations:

| Client | Project | User |
|---|---|---|
| Claude Code | `.claude/skills/figma-to-xmlui/` | `~/.claude/skills/figma-to-xmlui/` |
| OpenCode | `.opencode/skills/figma-to-xmlui/` | `~/.config/opencode/skills/figma-to-xmlui/` |

The Skill follows the Agent Skills specification: its directory name matches the `name` field, required trigger information is in frontmatter, and detailed material is progressively disclosed through one-level `references/` links.

## Configure Figma

The bundled `.mcp.json` targets the Figma desktop server at `http://127.0.0.1:3845/mcp`. Enable it in Figma Desktop by opening a design file, entering Dev Mode, and selecting **Enable desktop MCP server**.

Figma recommends its hosted server for the broadest availability. To use it with Claude Code, replace the URL in `.mcp.json`:

```json
{
  "mcpServers": {
    "figma": {
      "type": "http",
      "url": "https://mcp.figma.com/mcp"
    }
  }
}
```

The hosted server uses browser OAuth. Do not put access tokens or other secrets in this repository.

OpenCode does not consume a Claude plugin's `.mcp.json`. Configure the hosted server in `opencode.json` or `opencode.jsonc` instead:

```json
{
  "$schema": "https://opencode.ai/config.json",
  "mcp": {
    "figma": {
      "type": "remote",
      "url": "https://mcp.figma.com/mcp",
      "enabled": true
    }
  }
}
```

Run `opencode mcp auth figma` if the OAuth flow does not start automatically. Screenshot mode does not require MCP.

## Use

Provide one of the following:

- A link copied from the exact Figma frame or component to implement.
- A screenshot or exported image, ideally with its frame dimensions.
- A design description plus explicit dimensions when no image is available.

In a C++ host project, the workflow normally writes:

```text
<XmlRootPath or selected directory>/XmlUI_<RootName>.xml
Source/<HostModule>/<PublicOrPrivate>/<ProjectUIPath>/<ContractClass>.h
```

It may also add a `.cpp` when the requested contract includes runtime methods or event handlers. In a Blueprint-only project, or when the user explicitly asks for XML only, it omits the C++ contract and uses the configured `BaseWidgetClass` unless a loadable `ParentClass` is supplied.

## Verify

1. Compile the C++ contract before baking whenever the XML sets `ParentClass`. Follow the project's approved editor-build command; do not use update, cleanup, or revert scripts as a build shortcut.
2. In Unreal Editor, choose **XmlUI > XmlUI: Bake DSL to Widget Blueprint** and select the XML file. To round-trip a baked asset, choose **XmlUI > XmlUI: Export WBP to DSL** on the `.uasset`, or run the `XmlUI.ExportWbp` console command.
3. Derive the asset name from the sanitized complete XML basename; spaces, hyphens, and dots become underscores. For example, `XmlUI_ProfileCard.xml` bakes to `/Game/UI/WBP_XmlUI_ProfileCard` with the default settings.
4. Delete or rename an existing asset at that path before rebaking; the baker does not overwrite assets.
5. Open the generated Widget Blueprint and check for missing or type-mismatched `BindWidget` members.
6. Verify runtime text, image, progress, and click updates in the owning screen.

## Important Limits

- XmlUI has no input, slider, list-view, gradient, rounded-corner, stroke, blur, or animation tag.
- XML generation notes must be inside the `<XmlUI>` root; a leading comment can make the Unreal parser reject the file.
- Root slot attributes such as `Padding`, `HAlign`, `VAlign`, and `SizeParam` have no effect because the root has no parent slot.
- Unknown tags are skipped and unknown or malformed attributes are generally ignored, so a successful bake is not sufficient validation.
- An object brush needs a real Unreal object path such as `Texture2D→/Game/UI/T_Icon.T_Icon`. When an asset is not imported yet, use a distinct solid-color brush and record the intended asset in the generation notes.

See `skills/figma-to-xmlui/references/xmlui-dsl.md` for the complete implementation-derived behavior.

## Skill Maintenance

The evaluation file follows Anthropic Skill Creator's current schema and lives inside the Skill directory so it travels with standalone installations. When changing behavior, update the eval prompts and their objective `expectations`, compare the revised skill against the previous version, and review generated XML/C++ artifacts before accepting the change.

Authoring references:

- https://github.com/anthropics/skills/tree/main/skills/skill-creator
- https://agentskills.io/specification
