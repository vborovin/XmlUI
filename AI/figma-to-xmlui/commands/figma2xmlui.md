---
description: Convert a selected Figma frame or UI screenshot into bakeable XmlUI XML and, when applicable, a host-adapted Unreal C++ BindWidget contract.
argument-hint: <Figma URL, screenshot, or design description>
---

# Generate XmlUI from Figma

This file is a compatibility wrapper for manual slash-command invocation. Use the bundled `figma-to-xmlui` skill at `skills/figma-to-xmlui/SKILL.md` as the sole workflow and syntax source of truth. Process `$ARGUMENTS` as the design source; if `$ARGUMENTS` is empty, use the image attached to the current request. Ask for a Figma selection link or screenshot only when neither is available.

Complete the skill end to end:

1. Inspect the host project's module and existing XmlUI conventions.
2. Acquire and cross-check the selected design context without inventing inaccessible data.
3. Write the bakeable XML and, when the host has a C++ module, the paired contract using that project's conventions.
4. Validate names, tags, attributes, bindings, class path, and expected bake asset name.
5. Report created paths, unresolved assets/styles, and any compile or bake step that was not performed.

Honor an explicit request for XML only. In a Blueprint-only host, omit the C++ contract and rely on the configured `BaseWidgetClass`; otherwise keep the XML and C++ contract together in one change.
