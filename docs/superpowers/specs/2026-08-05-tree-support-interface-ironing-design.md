# Tree support interface ironing for Slim/Strong/Hybrid

Date: 2026-08-05
Status: approved (pending implementation)

## Problem

"Support interface ironing" (`support_ironing` config option) has no effect for
tree support styles Slim, Strong, and Hybrid (`SupportMaterialStyle::smsTreeSlim`,
`smsTreeStrong`, `smsTreeHybrid`), even though the GUI checkbox is enabled for
them and the underlying config values (`support_ironing`, `ironing_pattern`,
`ironing_flow`, `ironing_spacing`) are populated for every support style. It
works only for Tree Organic (`smsTreeOrganic`).

## Root cause

`PrintObject::_generate_support_material()` (`PrintObject.cpp:4463-4474`) routes
every tree style through `TreeSupport::generate()`, which forks on style
(`TreeSupport.cpp:1719`):

- **Organic** → `generate_tree_support_3D()` → `TreeSupport3D.cpp:3605` →
  the shared `generate_support_toolpaths()` in `SupportCommon.cpp:1421`, also
  used by classic Grid/Snug supports. This function implements ironing: it
  captures the top-contact-layer polygons (`SupportCommon.cpp:1646-1650`) and
  fills them with an `erIroning` extrusion pass gated on
  `support_params.ironing` (`SupportCommon.cpp:1902-1933`).
- **Slim / Strong / Hybrid / Default** → `TreeSupport::generate_toolpaths()`
  (`TreeSupport.cpp:1355`), a separate, custom per-`area_group` toolpath
  writer (`BaseType` / `RoofType` / `FloorType` / `Roof1stLayer`). It never
  reads `m_support_params.ironing` or the other ironing fields — there is no
  ironing code path in this function at all.

`SupportLayer::Roof1stLayer` (`Layer.hpp:324`, built from
`ts_layer->roof_1st_layer`, `TreeSupport.cpp:2380-2384`) is the
model-contacting interface layer in the normal-tree path — the exact
analogue of `top_contact_layer` in `SupportCommon.cpp`. It's handled at
`TreeSupport.cpp:1535-1552`.

The GUI checkbox (`ConfigManipulation.cpp:878-882`) is gated only on
`have_raft || support_interface_top_layers > 0`, not on support style, so
today the option can be toggled on for Slim/Strong/Hybrid and silently does
nothing.

## Change

Add an ironing pass to `TreeSupport::generate_toolpaths()`, on the
`SupportLayer::Roof1stLayer` branch, modeled on
`SupportCommon.cpp:1902-1933`:

1. Gate on `m_support_params.ironing`.
2. After the existing Roof1stLayer fill/perimeter generation
   (`TreeSupport.cpp:1535-1552`), build a `Fill` using
   `m_support_params.ironing_pattern`, angle = the layer's
   `support_interface_angle`, spacing = `m_support_params.ironing_spacing`.
3. Emit the fill as `ExtrusionRole::erIroning` at
   `m_support_params.ironing_flow`, appended to
   `ts_layer->support_fills.entities`.

No changes to `PrintConfig.{cpp,hpp}`, `SupportParameters.hpp`, or
`ConfigManipulation.cpp` — the config plumbing already exists for every
style; only the missing consumer in the normal-tree toolpath writer is
added.

Open item to verify while implementing: whether `Roof1stLayer` can ever have
tree-support geometry above it that needs clipping the way
`SupportCommon.cpp` clips against the upper support layer. By construction
it's the topmost support layer under the model, so this is expected to be a
no-op, but must be confirmed against Hybrid's raft-first-layer-expansion
branch (`TreeSupport.cpp:2331-2350`) before assuming no clip is needed.

## Testing (TDD)

All new cases live in `tests/fff_print/test_support_material.cpp`
(`[SupportMaterial]` tag), written and confirmed red before the production
change, then made green.

1. **Positive, in-memory** — `GENERATE` over `smsTreeSlim`, `smsTreeStrong`,
   `smsTreeHybrid`. Slice a mesh with a flat overhang guaranteeing a roof,
   with `enable_support=1`, `support_type=tree`, `support_style=<style>`,
   `support_interface_top_layers>0`, `support_ironing=1`. Flatten each
   support layer's `support_fills` and assert at least one entity has
   `role() == erIroning`.
2. **Negative, in-memory** — same setup per style with `support_ironing=0`;
   assert no entity anywhere in support fills has `role() == erIroning`.
3. **Organic regression guard** — same positive assertion with
   `support_style=organic`, protecting the pre-existing shared-path
   behavior from being broken by this change.
4. **G-code level** — slice the Slim case via the existing `slice()` helper;
   assert the emitted g-code contains a `;TYPE:Ironing` comment
   (`GCode.cpp:8431`'s `"Ironing"` role label), for end-to-end confidence
   the toolpath reaches the writer.

Test names are plain behavioral sentences per `tests/AGENTS.md`. Assert the
defining property (presence/absence of an `erIroning` entity or gcode
token), not incidental geometry or exact extrusion counts.

## Out of scope

- GUI changes (checkbox already works correctly across styles).
- New config options.
- Any change to Grid/Snug/Organic support behavior.

## Contribution conventions for this change

- TDD order: tests first (red), then the production fix (green), no
  refactor beyond what's needed.
- Follow `AGENTS.md` review-focus checklist: no behavior change when
  `support_ironing` is disabled (covered by the negative test),
  cross-platform (pure C++ core logic, no platform-specific code), no
  profile/format/version-migration impact (no new config keys, no 3mf
  compatibility concerns).
- Commit messages: no AI co-author trailer.
- Never stage `.claude/` or other Claude-tooling files/folders.
- PR description follows `.github/pull_request_template.md`
  (Description / Screenshots — n/a, no UI change / Tests — reference the
  new test cases and manual slice verification).
