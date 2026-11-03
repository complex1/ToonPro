# Original anime brush library plan

Research from 29 October 2026; implementation status updated 30 October 2026. Current application: **Toon Pro 0.45.0**; research baseline was 0.41.1.

The catalog contains **200 designs across 15 families**, centered on anime backgrounds, character cleanup and manga marks. **All 200 are now built-in defaults across 15 subject groups**, including all 32 pilot recipes. Together with the unchanged 19 starters, the app has **219 built-in brushes**. The last 34 specialized designs ship in 0.49.0. Real-artwork and physical Huion review remain open; the pilot is a useful feedback set within the installed collection.

The complete catalog is available as [a sortable CSV](brush-catalog.csv) and [structured JSON](brush-catalog.json). Each entry records its intended mark, candidate dynamics, engine dependencies, implementation/asset status, runtime preset ID where installed, development wave and pilot membership. These are status/index files, not importable brush packs. Actual numeric recipes and artwork generators are in [catalog_brushes.cpp](../src/catalog_brushes.cpp).

Recipe revision 2 (0.42.1) addresses the artist's first brush review: outlined patterns on transparency, smoother continuous texture/cloud coverage, and foliage variation between stamps within a single stroke. Existing untouched settings upgrade once; artist edits and saved artwork stay intact. Version 0.45 adds the 54 Wave 2 designs, multiple-tip selection, anchors, larger primary sizes and a further quality pass on seven grass/foliage defaults.

## What to adopt from other painting apps

| Reference | Relevant behavior | Toon Pro design decision |
| --- | --- | --- |
| Photoshop | Shape, color and transfer dynamics separate size/orientation, color variation and paint delivery. Variation can occur per stamp or per stroke; compatible hardware is needed for pen-specific inputs. | Keep these channels independent. Use small color ranges for foliage and predictable pressure for cleanup ink. [Adobe brush dynamics](https://helpx.adobe.com/photoshop/desktop/apply-painting-techniques/fill-objects-selections-layers/brush-dynamics-settings.html). |
| Photoshop custom tips | An image can become a brush tip; the custom-tip process converts color to grayscale. | Author coverage masks separately from preserved-color stamps. Do not assume that an image-derived Photoshop tip retains its original colors. [Adobe custom tips](https://helpx.adobe.com/photoshop/desktop/apply-painting-techniques/brushes-presets/create-brush-tip-image.html). |
| Krita image hoses | A brush can choose among several images randomly, sequentially, or according to input such as angle or pressure. | Give grass, birds and trees genuinely different silhouettes. This requires multiple-tip support in Toon Pro. [Krita animated brushes](https://docs.krita.org/en/tutorials/krita-brush-tips/animated_brushes.html). |
| Krita spray | Spray controls distinguish distribution, density/count, particle shape, size, rotation and color. | Fine mist, coarse spatter and clustered particles should produce different marks. Advanced distribution controls are a separate extension. [Krita spray engine](https://docs.krita.org/en/reference_manual/brushes/brush_engines/spray_brush_engine.html). |
| Clip Studio Paint decoration | Spaced stamps, sprayed marks and ribbons serve different purposes. Multi-tip strokes can use ordered or random sequences. | Separate isolated stamps from continuous rope, ivy and chain strokes; ribbons need connected geometry. [Official decoration tutorial](https://tips.clip-studio.com/en-us/articles/679), [Stroke settings](https://help.clip-studio.com/en-us/manual_en/810_subtools/S.htm). |
| Clip Studio Paint tones | Screentones expose frequency, density, angle and pattern, with masks controlling coverage. | Give true manga tones their own stable pattern behavior. A random-dot spray is useful stippling, but is not a regular screentone. [Screentones](https://help.clip-studio.com/en-us/manual_en/540_comic/Screentones.htm). |
| Procreate Brush Studio | Shape, grain, color changes per stamp/per stroke, pressure and tilt are distinct controls; speed-based behavior is also available. | Keep natural variation controlled and allow consistent color within a stroke. Speed response needs a new input sensor in Toon Pro. [Brush Studio settings](https://help.procreate.com/procreate/handbook/5.3/brushes/brush-studio-settings). |

These references inform behavior and workflow. Shipped masks, textures and recipes are original Toon Pro code-generated assets; seven colored stamps use layered procedural illustrations. No external brush pack is bundled.

## Catalog families

| Family | Planned brushes | Examples and intended use |
| --- | ---: | --- |
| Line art and ink | 18 | Clean Contour, Tapered Hairline, Fine Facial Detail, Dry Manga Nib, Cloth Crease. |
| Sketch and dry media | 12 | Construction Lead, Rough Layout Pencil, Soft Graphite Side, Powder Charcoal, Dry Pastel. |
| Soft paint and spray | 14 | Velvet Shade, Fine Aerosol, Soft Oval, Granular Spray, Coarse Spatter. |
| Grass and ground cover | 16 | Short Grass Tuft, Wind Grass, Clover Patch, Meadow Tuft Set, Reed Bed, Moss Fringe. |
| Trees and foliage | 18 | Broadleaf Cluster, Conifer Branch, Bare Twig, Bamboo Leaf Set, canopy and whole-tree sets. |
| Clouds and atmosphere | 14 | Cloud Edge Soft, Wispy Cloud, Ground Mist, cumulus masses, smoke and storm undersides. |
| Ground rock and terrain | 14 | Stone Grain, Sand Speckle, Gravel Scatter, boulders, cliff facets, mud and snow crust. |
| Walls and construction surfaces | 16 | Plaster Grain, Brick Surface, concrete, wood, asphalt, peeling paint, tatami and fabric. |
| Architecture and repeating details | 12 | Window Row, balconies, fence posts, rooflines, railings, perspective brick and stone patterns. |
| Water and reflections | 12 | Calm Ripples, Broken Reflection, Foam Flecks, rain rings, splash crowns, caustics and shore foam. |
| Manga tones and hatching | 12 | Parallel Hatch, Stipple Shadow, dot/line/cross tones, engraved shading and curved hatching. |
| Manga effects and decoration | 10 | Action Streak, impact flecks, speed textures, stars, flowers, rope and chain borders. |
| Weather and particles | 10 | Fine/heavy rain, snow, embers, ash, dust motes, fireflies, petals and debris. |
| Dynamic subject systems | 12 | Bird flocks, butterflies, fish, feathers, Skyline Builder, House Row Builder, Forest Builder. |
| Painted color stamps | 10 | Painted leaves, flowers, pebbles, branches, moss, clouds, trees, buildings, foam and ground patches. |
| **Total** | **200** | Names and per-brush dependencies are in the linked catalog. |

“Dynamic” should also be a collection that refers to brushes across these families. A grass brush can appear under both Grass and Dynamic without becoming two independent presets. Type describes how it paints; category describes the subject; tags support combinations such as `Foliage`, `Monochrome`, `Color variation`, `Multiple tips` and `Direction`.

## Existing engine support and required extensions

**All 200 recipes now ship as defaults.** Their original artwork and tuned settings are compiled into the app. The 34 specialized recipes use the connected-strip, surface-pattern and dynamics extensions added in 0.49.0. Physical-tablet and artistic acceptance remain separate from automated rendering/persistence checks.

| Area | Current support | Remaining limits / acceptance |
| --- | --- | --- |
| Size, rotation and paint dynamics | Pressure, tilt, direction, supported pen rotation, response curves, seeded jitter, opacity/flow variation, timestamped speed response. | Physical Huion acceptance; no default depends on barrel rotation. |
| Color | Foreground/background mixing and HSV variation; opt-in source HSV and foreground hue/saturation tint preserving colored-stamp value shading. | Artistic review of tinted palettes; no physical paint mixing simulation. |
| Tip artwork | Up to 16 grayscale or colored primary frames in a bounded shared atlas; weighted random, ordered, shuffled, pressure/direction selection and per-tip anchors. | Further specialized subject authoring; a dual brush remains a separate secondary coverage mask. |
| Size | Up to **1,024 artwork pixels** for primary size; secondary masks remain capped at 256. Tip sets use images up to 512 pixels each. | Sizes beyond 1,024 require separate rendering and memory work. |
| Patterns | Canvas texture, full-color tiled surfaces, path-deformed joined ribbons/repeats and explicit perspective quads. | Plane mapping is independent of guide/3D camera systems; very sharp strip bends compress motifs. |
| Manga tones | Dot, line, cross, diamond, fixed noise and fading-dot surfaces with independent frequency/density/angle and stable phase. | Frequency uses artwork pixels, not print LPI; final-size production-art review remains open. |
| Spray | Scatter/count, size/angle variation, airbrush buildup and seeded clusters with radial/angular controls. | Recipe tuning against artist drawings and physical pen delivery. |

Code reference: [brush settings](../src/brush_settings.h), [brush rendering](../src/brush_engine.cpp), [setting validation](../src/brush_settings.cpp), and [existing starter recipes](../src/standard_brushes.cpp). No fluid diffusion or physical bristle simulation is implied by soft/wash brush names.

Catalog dynamics describe candidate controls, not a finished numeric recipe. Smooth shading does not automatically use scatter, and an individual wall-crack stamp does not automatically use canvas-aligned texture. Legacy texture-tile angles remain authored into the asset; the newer surface mode provides its own independent rotation. First-wave single stamps remain within 256 pixels. Forest builders in Wave 2 place anchored silhouettes with deliberate overlap or gaps; fitted continuous joins belong to Wave 3.

## How the important families should behave

**Grass and foliage.** First author clean single tufts and clusters for the pilot. The later sets should contain several deliberately different silhouettes, with a shared root anchor and similar visual weight. Pressure may change height or paint strength; small size and color changes introduce variation. Keep roots on their intended ground line and avoid arbitrary upside-down flips. Separate whole-tree placement from brushes that build a canopy piece by piece.

**Birds and dynamic objects.** A flock should select among original wing poses, with bounded size variation and mild angle changes. No-immediate-repeat prevents a run of identical birds. Keep tip choice, scale and angle tied to a saved seed so Undo, reopening and vector replay reproduce the same flock. This is a sequence of 2D stamps, not an animated bird simulation.

**Buildings and walls.** Keep buildings upright with a common baseline. Skyline/house builders choose among authored modules; they do not generate arbitrary 3D architecture. Window rows and roof segments need stable spacing and joints. Perspective walls require an explicit plane or guide mapping; ordinary size jitter cannot make a wall recede correctly. Surface grain and cracks remain separate tools so the artist can control wear independently.

**Clouds and water.** Pair a controllable mass brush with edge and detail brushes. Cloud stamps need soft interiors, irregular silhouettes and no rectangular halos. Water reflections stay mostly horizontal; foam and droplets use different shapes and spacing. Larger assets must retain useful detail at their actual painted size, rather than relying on a high-resolution source while rendering at 256 pixels.

**Line art and manga.** Cleanup brushes prioritize steady contours, predictable pressure and sharp ends. Do not add random angle/size/color just to advertise dynamics. Stipple is intentionally irregular; screentones keep a regular grid with density independent from spacing. Review tones at final export size as well as the working zoom, including overlaps that can produce interference. [CSP tone display options](https://help.clip-studio.com/en-us/manual_en/720_preferences/Preferences.htm).

**Soft and spray.** Separate continuous soft shading, stationary airbrush buildup, fine grain and large opaque spatter. A compact particle mask can produce many visible particles without multiplying stamp count excessively. A more complex preset is worthwhile only if its mark and responsiveness improve on the simpler recipe.

## First collection of 32 brushes

| Area | Brushes |
| --- | --- |
| Character ink — 6 | Clean Contour; Tapered Hairline; Fine Facial Detail; Cloth Crease; Dry Manga Nib; Bold Silhouette. |
| Soft and spray — 5 | Velvet Shade; Edge Glaze; Fine Aerosol; Granular Spray; Soft Oval. |
| Grass — 4 | Short Grass Tuft; Wind Grass; Clover Patch; Moss Fringe. |
| Trees — 4 | Broadleaf Cluster; Conifer Branch; Distant Canopy; Bare Twig. |
| Sky — 3 | Cloud Edge Soft; Wispy Cloud; Ground Mist. |
| Ground — 2 | Stone Grain; Sand Speckle. |
| Surfaces — 3 | Plaster Grain; Brick Surface; Wood Grain. |
| Water — 3 | Calm Ripples; Broken Reflection; Foam Flecks. |
| Manga — 2 | Parallel Hatch; Stipple Shadow. |

This pilot uses the current tip-size range and rendering behaviors. Test it on one character cleanup drawing, one grassy/tree-lined background, and one street with sky and water. Adjust recipes from those drawings before producing the rest of the family. The 32 are a quality-review batch, not a requirement to install every brush into the active Drawing group.

## Library organization and performance

The current library permits **128 custom presets**; built-ins are separate. The tool catalog permits **512 total tools**, including nonbrush tools. Version 0.42 introduces catalog brushes into subject groups instead of Drawing. Existing resource accounting is defined in [BrushLibrary](../src/brush_library.h) and [ToolCatalog](../src/tool_catalog.h); nominal limits are not a promise that every permitted configuration performs well.

The installed set has category/type/tag/search filters, real stroke previews and a Dynamic tag for deliberate variation. Existing categories, edited settings, group placement and deleted-preset records survive updates. Optional pack installation and a separate Dynamic placement collection remain future work; the tag already filters the same definitions without duplicate presets.

Brush Management, Add Tools and navigation now generate visible previews lazily and pause while drawing. Navigation caches stroke samples across metadata and artwork changes; individual brush edits invalidate their own samples. Automatic tool-catalog saving waits for an active drawing/edit gesture to end. The small compiled resource set is initialized once and shares QImage storage across preset copies; tool-file serialization deduplicates images by content. Lazy decoding/eviction for future larger packs remains pending. See [preview paths](../src/tool_navigation_panel.cpp), [manager previews](../src/brush_management_panel.cpp) and [performance acceptance](performance.md).

## Authoring and acceptance

Each proposed pack needs a versioned manifest with stable preset/asset IDs, family, type, tags, required engine capabilities, original author/provenance, native settings and preview references. Store lossless grayscale coverage masks, transparent RGBA color stamps and separate seamless texture tiles. Record a root anchor for grounded subjects. Share source families across genuinely different recipes; do not inflate the count with renamed copies.

For multi-tip brushes, start with a small varied set, such as 6–12 silhouettes, and expand only when repetition remains visible. Store selection behavior and seeds explicitly. Extend project/vector serialization with migration and resource-budget checks so a saved drawing retains its appearance even after a global pack is updated or removed.

A brush is finished only after:

- Its single stamp, normal stroke and pressure variation are previewed using the actual engine.
- Slow lines, fast circles, light/heavy contact, taps, erasing, Undo and reopening behave correctly on raster and supported vector strokes.
- Transparent edges are clean; pattern seams and phase are stable; intended negative spaces survive small sizes.
- Fixed seeds reproduce the same tip choices and colors; new seeds produce useful variation without changing prior strokes.
- Ink has no unwanted stamp seams; scenery avoids obvious repeated silhouettes; colored stamps preserve their stated color mode.
- Catalog opening/search and warm/cold brush switching are measured with the full installed set; previews pause during pen contact.
- Brush latency and memory pass the documented workloads at normal and Retina scale, followed by an actual Huion drawing session. Synthetic event timings are not physical pen latency.

## Development order

1. **Catalog foundation:** optional packs, shared resources, filtered/lazy previews and migration that preserves the artist's library. Keep existing resource bounds until measurements justify a specific change.
2. **Wave 1:** author and refine the 32-brush pilot, then the remaining current-engine proposals. There are 112 current-engine entries in total, including the pilot.
3. **Wave 2:** multiple tips, root anchors and bounded larger stamps; then author the 54 entries requiring those features. Birds, varied foliage and large clouds become practical here.
4. **Wave 3:** fitted repeats, ribbons, full-color patterns and proper tone controls; then the 29 dependent entries, including joined architecture details.
5. **Wave 4:** perspective pattern mapping, colored-stamp tinting, advanced spray distribution and speed response; then the 5 specialized entries that require these extensions.

Wave 1's 112 brushes shipped in 0.42.0. Wave 2's 54 brushes, tip sets, anchors and larger primary sizes are implemented in 0.45.0. Waves 3–4 ship in 0.49.0: 29 and 5 designs respectively, using original procedural artwork and the implemented controls described in [brush settings](brush-settings.md#connected-brushes-patterns-and-specialized-dynamics-in-0490). Physical Huion and artistic acceptance of the installed collection remain open.
