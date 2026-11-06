# 01 · Project dashboard

Version 1 · 5 November 2026 · Proposed UX, not implemented.

## Requested purpose

The first screen after launching Toon Pro. Show the artist's multiple projects, make it easy to resume an existing one, and provide clear project creation. Keep the canvas and Timeline out of the startup screen.

## Visual direction

Neutral dark desktop UI using the existing theme roles: background #222222, panel #282828, raised #303030, border #444444, text #e8e8e8, muted #b5b5b5 and silver accent #bdbdbd. Thin recognizable icons, modest corner radii, readable typography and generous space between project thumbnails. No saturated blue chrome, decorative dashboard metrics, account prompts or promotional hero.

## Proposed interactions

- New project: opens a creation screen to be designed next with the artist.
- Open project: opens an existing saved project from disk.
- Project card: opens that project; its exact destination will be specified with the following screens.
- Search projects: filters saved project names.
- All projects / Pinned / Archived: direct collection tabs. Archive affects dashboard organization, not files on disk.
- Last opened: orders the project collection.
- Grid/List icons: alternate views of the same projects.
- Card overflow: occasional project actions, to be specified later.

A static image cannot demonstrate focus, keyboard behavior or tooltip timing. Future implementation must provide accessible names, visible keyboard focus, comfortable hit targets and the shared 600 ms tooltip delay.

## Image contents

Six fictitious projects demonstrate a populated dashboard. Original anime-style thumbnail artwork and project names are visual placeholders; they are not the artist's actual saved files. Empty, loading and missing-file states remain to be designed after feedback. The next screen is not yet prescribed.

Generation: built-in image_gen tool. See prompt-v1.txt for the complete prompt and dashboard-v1.png for the generated image.
