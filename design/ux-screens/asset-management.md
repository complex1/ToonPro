# Project asset management

Artist direction and proposed UX · 5 November 2026 · Design only, not implemented.

## Requested capability

Import external images, videos and audio; give them predefined/custom tags; manage them as assets. Existing drawings and video/animation clips can also be moved into Assets. This extends the earlier dashboard model while keeping content editors focused.

## Navigation and library scope

Proposed starting scope: a library within each project, available for Drawing, Animation and Movie projects. Drawing uses Drawings / Assets tabs; Animation uses Shots / Assets; Movie uses Content / Assets. The initial tab remains the simple content collection. Assets is a deliberate destination, never a permanent movie/clip-management panel inside the drawing or single-shot editor. A global cross-project library is a separate future decision.

Assets has direct Import assets and New folder actions, folder navigation, search, type filters, grid/list views and a selected-item inspector. Allow nested asset folders without prescribing a production hierarchy. Show assets of type Image, Video, Audio, editable Drawing and editable Animation distinctly. File format is descriptive metadata, not proof of codec/render compatibility. Asset status and the optional Asset tag are separate concepts: adding that tag alone does not move a document.

## Import external files

- Import assets opens multi-file selection; drag/drop files into Assets is an equivalent entry.
- Show a review dialog before import: selected filenames, media types, destination folder, predefined tag choices and custom-tag entry. Default selection covers the whole batch; selecting individual rows permits per-file tags. Explain the current scope with a short Selected files label; mixed tag states must remain representable.
- Predefined choices retain Storyboard, Animatics, Background, Asset and Character sheet. Custom labels can describe Reference, Movement, Audio, Night or any artist-defined purpose. Labels do not change media type or editing behavior.
- Copy source bytes into the project on successful import; leave external originals at their existing locations. The brief and one short dialog line should explain this ownership choice. No silent live link is created.
- Allow naming and folder/tag edits later. Duplicate filenames never overwrite each other. Detect identical content and let the artist reuse an existing entry or import a deliberate separate item rather than silently discarding work.
- Import has progress and cancellation. Validate permission, available space, supported limits and source availability; publish the batch atomically or report a clearly recoverable failure. Cancel returns to the same project/folder with no published partial batch.
- Preview failure retains the stored item and explains the unsupported decoder or media problem. Do not reject a valid original merely because no preview can be generated.

## Inspect and organize

Image preview supports zoom/fit. Video and audio offer explicit Play/Pause/Stop and seek, with duration/format metadata; never autoplay on selection, and stop when leaving Assets. Native Drawings/Animations offer Open original in their focused editor. Thumbnails, waveform displays and metadata should be prepared asynchronously with bounded caches.

Search covers asset name, tags and folder. Type filters and selected tags combine with the current folder scope. Folder movement/rename, tags and item naming must be direct and batch-capable, with appropriate undo. Folder ownership cannot create cycles or detach referenced bytes. Tags use consistent deduplication; editing a label must not silently retag unrelated items. Concrete batch/tag/storage limits remain implementation decisions.

Show project folder, source kind and whether an item is an editable native document or imported media in the inspector. Used in reports only recorded document/instance placements; manual attachments or copied images must not be claimed as automatically detected dependencies. Before real dependency tracking exists, use explicit attachment wording instead of a fabricated usage count.

## Move existing content into Assets

A drawing, native single-shot animation or imported video item offers **Move to Assets…** in its dashboard overflow/context menu; drag to Assets can be a shortcut. A focused dialog shows the selected item(s), target asset folder, existing tags and optional new tags. The source stays in place until the move succeeds. Preserve item identity, tags, editability and existing uses. The item then appears in Assets instead of the ordinary drawing/shot/content collection. Keep an Undo action and a later Move to content command with an explicit destination.

A native drawing keeps layers and source data; an animation keeps drawings, exposures, FPS, sound and other shot content. Imported video keeps its original bytes. Moving to Assets must not flatten artwork, export a video, detach an animation's source or remove existing placements. The primary action is Move to Assets. **Save a copy to Assets…** may be provided separately when the artist wants the original to remain in both collections; it must have independent identity and clear copy semantics.

Current storage cannot satisfy this just by changing an Asset tag. Implementation needs a shared item identity or a reliable resource-complete migration with atomic publication; moving the only source file without updating existing references is invalid. Catalog/resource/history changes must be recoverable together. Do not mark this as implemented by reusing the existing imported-file catalog alone.

## Use an asset in content

Keep deliberate operations separate: Open original edits an editable native source; Place a copy inserts independent content into a selected drawing or single-shot animation. For video, initially use a reference-study workflow or an explicit supported conversion, without promising native video-layer editing. For audio, use the shot's supported audio placement workflow. If no compatible document is open, choose a target deliberately rather than modifying the last hidden canvas.

Future linked placement requires stable versions, missing-resource handling and update/relink controls; it must not be implied by copy placement. Relink an unavailable original only through an explicit file choice and validation. Native editable assets must travel with all required resource dependencies when the project is copied or backed up.

## Removal and recovery

Remove from library moves to Removed and preserves bytes and existing placed/linked uses. Restore returns the entry to its earlier folder when available. Permanent deletion is a separate explicit operation that checks references; if something is in use, require replacement/detachment or reject the delete with a reason. Deleting a Home/project-list entry never deletes asset files. Export asset creates an external copy and does not change library ownership.

## Screen drafts

- 07: project Assets library with imported image/video/audio and native editable Drawing/Animation assets.
- 08: batch Import assets review, folder destination and predefined/custom tags.
- 09: Move to Assets for an existing animation, retaining its editable source.

Sample artwork, filenames, times and metadata are fictitious. Screens are generated mockups, not verified controls. Existing source already has a project asset catalog with imported copies, tags, previews, attachments and restorable removal; the new dashboard integration, native-content move, undo, usage/relink and placement contract above remain design proposals. See ../../docs/production-projects.md for current-source behavior and limits.

The [storage and application architecture proposal](../../docs/storage-and-application-architecture.md) documents recommended portable project folders, independent document storage, asset identity, shared management/editor sessions and development acceptance gates. It is a plan, not implemented behavior.
