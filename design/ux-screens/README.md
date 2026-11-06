# Toon Pro screen design series

Started 5 November 2026. Status: UX exploration, before implementation.

Design each screen with the artist, save a versioned image and its prompt, and record its intended interactions. Images are proposals, not application screenshots or verified functionality. Existing application code is unchanged by this series.

Shared direction: native desktop UI; Spectrum-inspired neutral charcoal surfaces and silver emphasis; recognizable icons; short visible labels for important actions/navigation; accessible controls with 600 ms delayed tooltips. The artwork provides the color. Follow ../../docs/ui-guidelines.md and ../../src/ui_theme.h.

| Screen | Folder | Status |
| --- | --- | --- |
| 01 · Project dashboard | [01-project-dashboard](01-project-dashboard/) | Image draft for artist review |
| 02 · New project | [02-new-project](02-new-project/) | Drawing / Animation / Movie chooser |
| 03 · Drawing project | [03-drawing-project](03-drawing-project/) | Drawing collection and New drawing |
| 04 · Animation project | [04-animation-project](04-animation-project/) | Single-shot animation list and New animation |
| 05 · Movie project | [05-movie-project](05-movie-project/) | Nested folders, mixed documents and tags |
| 06 · New drawing | [06-new-drawing](06-new-drawing/) | Name, canvas, background; Movie-only tags |
| 07 · Assets library | [07-assets-library](07-assets-library/) | Imported media, native artwork, folders, tags and previews |
| 08 · Import assets | [08-import-assets](08-import-assets/) | Batch import, destination and predefined/custom tags |
| 09 · Move to Assets | [09-move-to-assets](09-move-to-assets/) | Move existing content while retaining editable source |

On launch, the intended first surface is the project dashboard. It shows saved projects and direct creation/open actions. It does not initially open the drawing editor or Timeline. Additional screens will be defined one at a time with the artist; the earlier implementation work is paused.

The artist-defined [project flow and content model](project-flow.md) supersedes the earlier direct-to-document entry recommendation. New project selects Drawing, Animation or Movie. Opening a project enters its own dashboard before opening a drawing or single-shot editor. Screens 02–05 use 01 as a style reference; screen 06 builds on the Drawing/Movie dashboards. Each screen folder retains its image, full prompt and brief.

The [asset-management design](asset-management.md) adds an optional per-project Assets destination for imported images/video/audio and native drawings/animations moved into the library. Import, organizational move, copied placement and source editing have distinct semantics. Current implementation is not changed by these mockups.

The [storage and application architecture proposal](../../docs/storage-and-application-architecture.md) documents recommended portable project folders, independent document storage, asset identity, shared management/editor sessions and development acceptance gates. It is a plan, not implemented behavior.
