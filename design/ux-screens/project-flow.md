# Project navigation and content model

Artist direction · 5 November 2026 · Design only; not implemented.

This updates the earlier Editor/Studio entry proposal. Launch opens Projects. New project asks for Drawing, Animation or Movie; opening a project always opens its own dashboard before its content editor. Multiple projects can exist.

| Project type | Project dashboard | Primary creation |
| --- | --- | --- |
| Drawing | A simple collection of drawing documents | New drawing |
| Animation | A list of animation/shot documents, each containing exactly one shot | New animation |
| Movie | Nested user-created folders containing drawing/image and single-shot animation documents | New folder, New drawing, New animation |

The Animation project is a collection; each animation document is still one shot. Opening a document enters its focused drawing or single-shot editor. Project collections, folders and organizational controls belong to dashboards, not to the drawing canvas or shot timeline.

Movie folders can contain other folders at arbitrary nesting levels as the UX direction. Do not impose a mandatory episode/scene hierarchy. Actual storage/resource limits must be specified honestly during implementation; the design does not establish unlimited technical capacity.

When creating content in a Movie project, offer predefined tags Storyboard, Animatics, Background, Asset and Character sheet, plus custom tags. Tags can be combined on an item and edited later. Folder hierarchy determines location; tags describe purpose. The proposed starting behavior is flat searchable labels without automatic inheritance; inherited or hierarchical tag taxonomies have not been requested or decided. Folder selection scopes creation and browsing. Drawing and Animation dashboards remain simple; tags/folders are not added there in this draft.

A Movie dashboard is an organizer. It does not imply a multi-shot editorial timeline inside the focused animation editor. Sequence editing and animation-creation details will be designed with the artist in later screens. Drawing creation is specified in [screen 06](06-new-drawing/brief.md).

## Navigation

Projects → New project → choose type and name → corresponding project dashboard.

Projects → open existing project → corresponding project dashboard.

Drawing dashboard → New/open drawing → drawing editor.

Animation dashboard → New/open animation → single-shot animation editor.

Movie dashboard → choose/create nested folder → New/open drawing or animation → corresponding focused editor.

New drawing setup in Movie includes tags at creation; see [screen 06](06-new-drawing/brief.md) for both Drawing- and Movie-project variants. New animation setup remains a later screen. Project type choice is not a workspace layout or a production-stage tag.

## Mockups in this iteration

02 shows New project with Drawing selected; 03 shows a populated Drawing project; 04 shows a populated Animation project; 05 shows a Movie folder with subfolders and mixed tagged content. Sample names, artwork, times and durations are fictitious. These images do not demonstrate live controls, file compatibility or implemented limits.

## New drawing setup

From a Drawing dashboard, New drawing asks for name, canvas dimensions and background, with a live blank-canvas preview. From a Movie dashboard, the same dialog also shows the selected folder and optional predefined/custom tags. Create drawing opens the new document in its drawing editor. Cancel returns to the dashboard without creating content. The setup does not add animation timing or movie/scene/clip sections. See screen 06 for the full proposed interaction and validation contract.

## Assets extension

The artist also requested external asset management and moving existing drawings/video clips into Assets. Proposed project tabs are Drawings / Assets, Shots / Assets and Content / Assets respectively. Each project retains its simple default content dashboard. Assets provides nested folders, media previews, type/search filters, predefined/custom tags and import of images, videos and audio. Native drawings and single-shot animations can move into this library while retaining editability and existing uses; this is distinct from adding an Asset tag or exporting a flattened copy. See [asset-management.md](asset-management.md) and screens 07–09 for the full design contract.

The earlier omission of tags/folders from simple Drawing/Animation content creation remains; their separate Assets destination supports asset organization across project types. Per-project scope is a proposed starting choice; a global library is not yet specified. All are design proposals, not implemented behavior.

The [storage and application architecture proposal](../../docs/storage-and-application-architecture.md) documents recommended portable project folders, independent document storage, asset identity, shared management/editor sessions and development acceptance gates. It is a plan, not implemented behavior.

## Document tabs and implementation update

The artist requested dashboard multi-selection opening drawings and single-shot animations in separate editor tabs. Switching tabs preserves each document, undo, timing and canvas view; closing a dirty tab resolves Save/Discard/Cancel. Legacy migration was explicitly excluded. See [current local implementation](../../docs/managed-workspace.md) for delivered controls and differences from the visual drafts.
