# Managed projects and document tabs

Local development · 5 November 2026 · Working project flow with dashboards revised against the agreed screen references. Public downloads have not been replaced. This page describes implemented controls; advanced behavior shown only in design briefs remains proposed.

## Projects and storage

The normal app now starts on **Projects**, without an open drawing or Timeline. **New project** presents three selectable cards: Drawing, Animation and Movie, followed by a project name. Its optional **Project location…** action changes the parent folder. The default is the user's Documents/Toon Pro location; **Project location** changes the default for future projects. **Open project** accepts a new-format project directory. Projects opened/created here appear as thumbnail cards with type and last-opened information. Search, name/last-opened sorting, grid/list views and All projects/Pinned/Archived filters are available. Card actions pin, archive, restore or remove a recent entry without deleting files. Reopening a project opens its dashboard, without automatically opening its documents.

A new `.toonstudio` directory contains a version-2 `project.toonstudio` marker, `project.sqlite`, independent native revisions under `documents/<item-id>/revisions/`, and imported originals under `media/<item-id>/`. There is no disk partition or reserved capacity. Names, content/asset membership, folders, tags, removal state and revision pointers belong to the catalog. Native `.toonproj` payloads reuse the existing drawing/animation engines with exactly one shot per item. Each document has its own dimensions, FPS and history; its payload retains the existing 300 MiB container/resource bounds.

An application-level lease permits one writer for a project. Another instance reports that the project is already open. Catalog operations use SQLite transactions, rollback journaling, synchronous EXTRA and thread-local connections. Native writes retain checksums, guarded save publication and cancellation. A missing catalog is reported rather than recreated as an empty database. These are local-filesystem workflows; simultaneous cloud/network editing is not implemented.

Existing version-1 projects are not converted by the new entry flow. The artist explicitly excluded legacy migration because there is no existing artwork to carry forward. Retained old source/tests and packaging fixtures are not a new migration UI.

## Dashboards

| Project | Default content | Creation |
| --- | --- | --- |
| Drawing | Drawings | New drawing |
| Animation | Shots | New animation; each item is a single shot |
| Movie | Content | New drawing, New animation, nested folders |

Drawing content opens as a thumbnail gallery; Animation opens as thumbnail rows with saved duration and FPS. Both keep folders, media filters and the asset inspector out of the content view. Movie content adds a nested folder sidebar, folder tiles and tagged content cards. All types have a separate **Assets** tab with folders, type filters, tag filtering and a selected-asset inspector. Removed items are available from the folder sidebar in Movie/Assets. Search covers names/tags; filters combine with folder scope. Movie content displays direct folder children, while All assets searches the complete library. **New folder** creates a child of the selected folder; organization does not rename media files on disk. Grid/list choices are remembered by project type and collection. Project, drawing, asset and folder cards use compact cells capped at 220 logical pixels wide; resizing the window adds columns rather than stretching sparse galleries.

Native saved first frames and imported images produce real thumbnail previews on a background worker. Only visible cards are queued, one decode/render runs at a time, the in-memory pixmap cache is bounded, and revision/source keys invalidate cached previews. Cover images and metadata use a disposable local disk cache; they are not embedded mockup artwork. Unavailable previews retain a source-kind placeholder. Video/audio gallery cards currently use source-kind icons; playback remains in the inspector. No generated audio waveform is presented as source data.

**New drawing** provides name, size presets, width/height sliders with exact entry, White/Transparent/Color background and a proportional blank-canvas preview. Custom background is a locked raster background layer; normal drawing remains on an editable layer above it. Movie-project creation also provides predefined/custom tags. The initial simple preset set is Landscape, Square, Portrait and Custom. Ratio lock and orientation swap are implemented. The dialog uses two columns with a live canvas preview; its settings scroll independently when screen height is limited, keeping Cancel/Create accessible.

**New animation** adds FPS and frame count. It creates one plain single-shot document with an assigned initial drawing on a Simple animation track. The existing cel, hold, onion-skin, playback and drawing tools remain available in the editor. Tags describe purpose; an Animatics tag does not change the native document kind.

## Open multiple documents in tabs

Ctrl/Cmd-click or Shift-click multiple cards or rows, then **Open selected**, or activate the selection. Native drawing/animation items open in separate tabs beside Dashboard. In the editor, Projects and Project dashboard navigation share this same header row with the document tabs, leaving more vertical space for the canvas. Overflow tabs use horizontal scrolling. Reopening an already-open item selects its existing tab. Each tab hosts its own Canvas, Document, undo stack, playhead, selection and drawing/view state. Switching tabs or returning to the dashboard retains unsaved work; no export/reload occurs. A tab's asterisk indicates unsaved artwork.

An active stroke, transform, selection gesture or unresolved exact-entry draft can block navigation. Closing a dirty tab uses Save/Discard/Cancel. Cancelling leaves the tab and artwork intact. Switching projects or closing the app resolves open tabs; it does not silently discard them.

The initial tab host reuses the existing Qt MainWindow editor as an embedded widget, with project navigation and production layouts disabled. Tool/theme engines remain in this repository. Multiple open documents remain loaded; no automatic inactive-tab eviction or large-production RAM budget is implemented yet. Tool libraries use the application's editor profile; per-tab brush/tool instances are not a new cross-tab live organization service.

Focused editors have no Shots collection or Scene/Movie review navigation. Their controller rejects sequence-review activation and document commands reject shot collection mutations. Storyboarding/Animatics layout presets cannot be applied through this host, and restoring a saved layout cannot reopen its Shots dock. Video/audio export dialogs are locked to current-shot scope. Standard drawing exports and advanced shot-local capabilities remain available.

## Asset management

In **Assets**, **Import assets** opens a two-column review with Add files, removable file rows showing type/size, a destination-folder choice and predefined/custom batch tags. Confirming the import copies the chosen files. Import streams copied originals into generated project-owned paths on a worker, retains external originals, and publishes catalog rows only when the batch succeeds. Cancel before publication leaves no visible partial batch. Duplicate filenames get different identities. The first UI applies tags to the batch; per-file import tagging, content-hash duplicate reuse and drag/drop import remain later refinements.

Image previews load asynchronously with a bounded display size and stale-selection rejection. Video/audio have explicit Play/Pause, Stop and seek with a duration readout using Qt Multimedia; selection never autoplays, and leaving the dashboard stops preview. Codec support depends on the runtime. A retained media file can remain available even when preview decoding fails. Native items can be opened and edited in their original tabs; imported media selection currently previews it rather than converting it to native content.

Use an item's context menu for **Edit tags / folder**, **Rename**, **Move to Assets / Content**, **Remove** and **Restore**. The predefined tag choices are Storyboard, Animatics, Background, Asset and Character sheet; custom tags use Add or Return and removable chips. The asset inspector edits a selected asset’s tags directly; Return adds a tag without activating a parent dialog’s primary action. Multiple selected items can move or change tags together. Batch moves keep each item's own tags and optionally add new ones. Explicit batch tag editing replaces the chosen items' tag sets and labels that behavior; it does not yet show mixed-state tag editing.

The focused **Move to Assets** dialog shows a selected-item summary, destination folder and tags. It changes collection/folder/tag metadata for the same item ID and native/media path. It does not flatten, render or duplicate the editable document. Open tabs remain valid. Move back is available when the destination project's content type accepts the item. **Undo organization** reverses recorded item metadata/membership/removal changes separately from brush-stroke history. Folder rename/reparent, redo and copy-to-library commands are not implemented yet.

Remove keeps original bytes and native revisions; Restore reverses the catalog state. No permanent-delete UI is provided. Automatic usage detection, live linked placement, relinking/version pinning and a global cross-project library remain unimplemented; existing rendered copies must not be claimed as live dependencies.

## Save, recovery and backup

**Save document** writes only the active native item's complete snapshot, then advances its saved catalog pointer. Saves compare the expected prior revision; a stale worker cannot replace a newer saved revision. Cancel/failure retains the previous saved revision and dirty session. Newer in-memory edits are not marked clean merely because an older snapshot finished.

Periodic recovery runs on a background worker for dirty tabs between committed gestures. Its pointer is separate from explicit Save. Reopening an item with recovery asks whether to restore it. Discarding unsaved edits clears its recovery pointer. After successful saves, normal retention keeps the current explicit save, previous explicit save and latest recovery; publication precedes pruning. Unknown files left by a process interruption are not aggressively garbage-collected in this first version.

**Back up project** captures saved catalog state with SQLite VACUUM INTO while project writes are serialized, copies referenced document/media files, compares checksums and publishes a new backup directory. It does not silently save open dirty tabs or remove the source. The resulting copy opens independently. It is a manual saved-project backup, not cloud backup, an incremental service or an approved-revision delivery system.

Power-loss guarantees across every filesystem, unwritable-drive emergency recovery, project-size/free-space dashboards and automatic garbage collection still require dedicated work. Safe publication/cancellation and local failure checks do not establish universal hardware durability. Physical tablet feel and production-scale memory/playback acceptance remain separate.

## Verification

The current evidence is recorded in [DEVELOPMENT](DEVELOPMENT.md#managed-projects-and-document-tabs--5-november-2026). The focused suite covers independent canvas sizes, custom background render, one-shot/type constraints, cancelled/stale save, recovery, bounded retention, absent-catalog handling, nested folders/tags, ID-preserving moves/organization undo, atomic cancelled imports, original-byte retention, writer lease, backup reopen, multiple tab history/view retention, creation dialogs, independent animation timing, export scope and Cancel on dirty close.

The broader [storage/application proposal](storage-and-application-architecture.md) remains the architecture direction, with legacy migration removed from the current scope and multiple retained tabs added. Later production features are not implied by this first implementation.
