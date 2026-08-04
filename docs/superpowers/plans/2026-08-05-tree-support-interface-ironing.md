# Tree Support Interface Ironing (Slim/Strong/Hybrid) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `support_ironing` actually iron the model-contacting interface layer for tree support styles Slim, Strong, and Hybrid, matching the behavior Tree Organic already has.

**Architecture:** `TreeSupport::generate_toolpaths()` (`src/libslic3r/Support/TreeSupport.cpp`) gets an ironing pass on its `SupportLayer::Roof1stLayer` branch — the model-contacting interface layer, analogous to `top_contact_layer` in the shared `SupportCommon.cpp` path that Organic already uses. No config, GUI, or G-code-emission changes: `m_support_params.ironing*` fields and `GCode::extrude_support()`'s `erIroning` handling already work generically; only the missing toolpath-generation consumer is added.

**Tech Stack:** C++17, Catch2 (existing `fff_print` test suite).

## Global Constraints

- No new config options, GUI changes, or `SupportCommon.cpp`/`GCode.cpp` changes — scope is limited to `TreeSupport::generate_toolpaths()`. (spec: "Out of scope")
- Existing Grid/Snug/Organic/disabled-ironing behavior must not change. (spec: "Out of scope", "Testing")
- TDD order: write the tests first (task 1, red for the new positive case), then the production fix (task 2, green), no unrelated refactor. (spec: "Contribution conventions")
- Commit messages carry no AI co-author trailer.
- Never stage `.claude/` or other Claude-tooling files/folders in any commit.
- Follow `tests/AGENTS.md`: behavioral test names, `[SupportMaterial]` tag, assert the defining property (presence/absence of an `erIroning` entity or `support ironing` g-code token), not incidental geometry or counts.

---

### Task 1: Add failing/guard tests for tree-support interface ironing

**Files:**
- Modify: `tests/fff_print/test_support_material.cpp`

**Interfaces:**
- Consumes: `Slic3r::Test::init_and_process_print` and `Slic3r::Test::slice` (`tests/fff_print/test_helpers.hpp:84`, `:93`), `Slic3r::Test::layers_with_role` (`test_helpers.hpp:116`), `Slic3r::Test::TestMesh::overhang` (`test_helpers.hpp:41`).
- Produces: a local `has_support_ironing(const Print &print)` helper other tasks/tests in this file may reuse.

No new includes are needed beyond what the file already has (`Layer.hpp` pulls in `ExtrusionEntityCollection.hpp`, which declares `ExtrusionEntity::role()` and `ExtrusionRole::erIroning`).

- [ ] **Step 1: Add the shared helper and the four test cases**

Append to `tests/fff_print/test_support_material.cpp` (after the existing `TEST_CASE`s, before the closing of the file):

```cpp
// True when any support extrusion anywhere in the object is an ironing pass.
static bool has_support_ironing(const Print &print)
{
    for (const SupportLayer *layer : print.objects().front()->support_layers())
        for (const ExtrusionEntity *entity : layer->support_fills.flatten().entities)
            if (entity->role() == erIroning)
                return true;
    return false;
}

TEST_CASE("Support interface ironing irons the roof of normal tree styles", "[SupportMaterial]")
{
    const std::string style = GENERATE("tree_slim", "tree_strong", "tree_hybrid");
    CAPTURE(style);

    Slic3r::Print print;
    Slic3r::Test::init_and_process_print({ TestMesh::overhang }, print, {
        { "enable_support",               1 },
        { "support_type",                 "tree(auto)" },
        { "support_style",                style },
        { "support_interface_top_layers", 2 },
        { "support_ironing",              1 }
    });

    REQUIRE(has_support_ironing(print));
}

TEST_CASE("Support interface ironing off produces no ironing for normal tree styles", "[SupportMaterial]")
{
    const std::string style = GENERATE("tree_slim", "tree_strong", "tree_hybrid");
    CAPTURE(style);

    Slic3r::Print print;
    Slic3r::Test::init_and_process_print({ TestMesh::overhang }, print, {
        { "enable_support",               1 },
        { "support_type",                 "tree(auto)" },
        { "support_style",                style },
        { "support_interface_top_layers", 2 },
        { "support_ironing",              0 }
    });

    REQUIRE_FALSE(has_support_ironing(print));
}

// Regression guard: Organic already ironed its roof before this change; must keep doing so.
TEST_CASE("Support interface ironing still irons the roof of organic tree support", "[SupportMaterial][Regression]")
{
    Slic3r::Print print;
    Slic3r::Test::init_and_process_print({ TestMesh::overhang }, print, {
        { "enable_support",               1 },
        { "support_type",                 "tree(auto)" },
        { "support_style",                "organic" },
        { "support_interface_top_layers", 2 },
        { "support_ironing",              1 }
    });

    REQUIRE(has_support_ironing(print));
}

TEST_CASE("Support interface ironing reaches the g-code for tree slim support", "[SupportMaterial]")
{
    const std::string gcode = Slic3r::Test::slice({ TestMesh::overhang }, {
        { "enable_support",               1 },
        { "support_type",                 "tree(auto)" },
        { "support_style",                "tree_slim" },
        { "support_interface_top_layers", 2 },
        { "support_ironing",              1 }
    });

    REQUIRE(! Slic3r::Test::layers_with_role(gcode, "support ironing").empty());
}
```

- [ ] **Step 2: Build the fff_print test target**

Run: `cmake --build build --config RelWithDebInfo --target fff_print_tests`
Expected: builds cleanly (the test file compiles against existing helpers only).

- [ ] **Step 3: Run the new cases and confirm the right ones fail**

Run: `ctest --test-dir build -R "Support interface ironing" --output-on-failure`

Expected:
- `Support interface ironing irons the roof of normal tree styles` — **FAILS** for all three GENERATEd styles (`REQUIRE(has_support_ironing(print))` is false — no `erIroning` entity exists yet for Slim/Strong/Hybrid).
- `Support interface ironing off produces no ironing for normal tree styles` — **PASSES** already (nothing irons regardless of style when the flag is off).
- `Support interface ironing still irons the roof of organic tree support` — **PASSES** already (Organic's existing behavior, unaffected by this change).
- `Support interface ironing reaches the g-code for tree slim support` — **FAILS** (no `support ironing` g-code label emitted yet).

If any of the two "should already pass" cases fail, the mesh/config combination isn't producing a roof at all (e.g. `TestMesh::overhang` sliced flat) — adjust `support_interface_top_layers` or swap the mesh before continuing; do not proceed to Task 2 until the pre-existing-behavior cases are green and only the two new-behavior cases are red.

- [ ] **Step 4: Commit**

```bash
git add tests/fff_print/test_support_material.cpp
git commit -m "test: cover support interface ironing for tree Slim/Strong/Hybrid"
```

---

### Task 2: Implement the ironing pass in TreeSupport::generate_toolpaths()

**Files:**
- Modify: `src/libslic3r/Support/TreeSupport.cpp:1535-1556` (the `SupportLayer::Roof1stLayer` branch inside `generate_toolpaths()`)

**Interfaces:**
- Consumes: `m_support_params.ironing` / `.ironing_pattern` / `.ironing_spacing` / `.ironing_flow` (`SupportParameters.hpp:65-68`, already populated for every support style), `m_support_params.support_interface_angle(int)` (already used at `TreeSupport.cpp:1516`), the file-local `fill_expolygons_generate_paths(ExtrusionEntitiesPtr&, ExPolygons&, Fill*, const FillParams&, ExtrusionRole, const Flow&)` (`TreeSupport.cpp:1230-1244`), `bbox_object` (captured by the enclosing `[&]` lambda, declared at `TreeSupport.cpp:1477`).
- Produces: `erIroning`-role entities appended to `ts_layer->support_fills.entities` for the Roof1stLayer area of every tree style (previously Organic-only via the separate `SupportCommon.cpp` path).

- [ ] **Step 1: Insert the ironing pass**

In `src/libslic3r/Support/TreeSupport.cpp`, locate this exact block (currently at lines 1535-1556):

```cpp
                    if (area_group.type == SupportLayer::Roof1stLayer) {
                        // roof_1st_layer
                        // ORCA: Roof1stLayer may be printed with base material when it acts as a contact layer.
                        bool interface_as_base = area_group.interface_as_base;
                        fill_params.density = interface_density;
                        // Note: spacing means the separation between two lines as if they are tightly extruded
                        filler_Roof1stLayer->spacing = interface_flow.spacing();
                        filler_Roof1stLayer->angle = m_support_params.support_interface_angle(area_group.interface_id);
                        fill_params.dont_sort = true;
                        filler_Roof1stLayer->fixed_angle = (m_object_config->support_interface_pattern == smipRectilinearInterlaced ||
                                                            m_object_config->support_interface_pattern == smipRectilinear);
                        Flow interface_base_flow = interface_as_base ? support_flow : interface_flow;
                        ExtrusionRole interface_role = interface_as_base ? erSupportMaterial : erSupportMaterialInterface;
                        // generate a perimeter first to support interface better
                        ExtrusionEntityCollection* temp_support_fills = new ExtrusionEntityCollection();
                        make_perimeter_and_infill(temp_support_fills->entities, poly, 1, interface_base_flow, interface_role,
                            filler_Roof1stLayer.get(), interface_density, false);
                        temp_support_fills->no_sort = true; // make sure loops are first
                        if (!temp_support_fills->entities.empty())
                            ts_layer->support_fills.entities.push_back(temp_support_fills);
                        else
                            delete temp_support_fills;
                    } else if (area_group.type == SupportLayer::FloorType) {
```

Replace it with (the only change is the new `if (m_support_params.ironing)` block inserted right before the closing `}` of the `Roof1stLayer` branch):

```cpp
                    if (area_group.type == SupportLayer::Roof1stLayer) {
                        // roof_1st_layer
                        // ORCA: Roof1stLayer may be printed with base material when it acts as a contact layer.
                        bool interface_as_base = area_group.interface_as_base;
                        fill_params.density = interface_density;
                        // Note: spacing means the separation between two lines as if they are tightly extruded
                        filler_Roof1stLayer->spacing = interface_flow.spacing();
                        filler_Roof1stLayer->angle = m_support_params.support_interface_angle(area_group.interface_id);
                        fill_params.dont_sort = true;
                        filler_Roof1stLayer->fixed_angle = (m_object_config->support_interface_pattern == smipRectilinearInterlaced ||
                                                            m_object_config->support_interface_pattern == smipRectilinear);
                        Flow interface_base_flow = interface_as_base ? support_flow : interface_flow;
                        ExtrusionRole interface_role = interface_as_base ? erSupportMaterial : erSupportMaterialInterface;
                        // generate a perimeter first to support interface better
                        ExtrusionEntityCollection* temp_support_fills = new ExtrusionEntityCollection();
                        make_perimeter_and_infill(temp_support_fills->entities, poly, 1, interface_base_flow, interface_role,
                            filler_Roof1stLayer.get(), interface_density, false);
                        temp_support_fills->no_sort = true; // make sure loops are first
                        if (!temp_support_fills->entities.empty())
                            ts_layer->support_fills.entities.push_back(temp_support_fills);
                        else
                            delete temp_support_fills;

                        // ORCA: Iron the roof's topmost interface layer (the surface touching the model),
                        // mirroring SupportCommon.cpp's organic-tree ironing pass over top_contact_layer.
                        if (m_support_params.ironing) {
                            ExPolygons polys_to_iron = offset_ex(poly, -0.5 * interface_flow.scaled_spacing());
                            if (!polys_to_iron.empty()) {
                                std::unique_ptr<Fill> filler_ironing(Fill::new_from_type(m_support_params.ironing_pattern));
                                filler_ironing->set_bounding_box(bbox_object);
                                filler_ironing->angle   = m_support_params.support_interface_angle(area_group.interface_id);
                                filler_ironing->spacing = m_support_params.ironing_spacing;
                                FillParams ironing_params;
                                ironing_params.density     = 1.f;
                                ironing_params.dont_adjust = true;
                                fill_expolygons_generate_paths(ts_layer->support_fills.entities, polys_to_iron,
                                    filler_ironing.get(), ironing_params, erIroning, m_support_params.ironing_flow);
                            }
                        }
                    } else if (area_group.type == SupportLayer::FloorType) {
```

- [ ] **Step 2: Build**

Run: `cmake --build build --config RelWithDebInfo --target fff_print_tests`
Expected: builds cleanly.

- [ ] **Step 3: Run the ironing tests and confirm all four pass**

Run: `ctest --test-dir build -R "Support interface ironing" --output-on-failure`
Expected: all 4 test cases (across all GENERATEd styles) PASS.

- [ ] **Step 4: Commit**

```bash
git add src/libslic3r/Support/TreeSupport.cpp
git commit -m "fix: iron support interface roof for tree Slim/Strong/Hybrid"
```

---

### Task 3: Full regression pass

**Files:** none (verification only).

**Interfaces:** none.

- [ ] **Step 1: Run the full fff_print suite**

Run: `ctest --test-dir build --output-on-failure -R fff_print`
Expected: all tests pass, in particular every existing `[SupportMaterial]` case (raft layers, enforced support layers, contact-distance `SCENARIO`, the second-slice regression test) — confirming Grid/Snug/Organic/disabled-ironing paths are unchanged.

- [ ] **Step 2: Slice a manual sanity check (optional but recommended before opening the PR)**

Use the `run` skill or the built app to slice a model with an overhang using `support_style = tree_slim` (and `tree_strong`, `tree_hybrid`) and `support_ironing` on, and visually confirm ironing lines appear on the support roof beneath the model — the same visual result Organic already produces.

- [ ] **Step 3: Prepare the PR**

Fill `.github/pull_request_template.md`: describe the fix (link to the root-cause analysis — Organic uses `SupportCommon.cpp`'s shared ironing pass, Slim/Strong/Hybrid use `TreeSupport::generate_toolpaths()` which lacked one), no UI screenshots needed (no GUI change), and list the four new test cases plus the manual slice check under "Tests". Do not stage `.claude/` or other Claude-tooling files/folders. No AI co-author trailer on any commit.
