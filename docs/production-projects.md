# Production projects in 0.76

**Local implementation update:** the normal app now uses the [managed-project dashboard and document-tab workflow](managed-workspace.md). This page retains the earlier version-1 project/editor behavior for reference; its Film/Series creation flow is not the new default entry. No legacy migration is being implemented.

**Scope note:** this is the retained version-1 production foundation. The new normal entry, typed dashboards, independent native documents and editor tabs are described in [managed workspace](managed-workspace.md). Older future-work lists here do not reintroduce legacy migration into the current scope.

This first production-workspace pass adds Project Home, project types, portable project folders, a project asset catalog and shot/artwork planning. Drawing and animation continue to use the existing editors. It does not yet add a storyboard grid, a movie editing timeline or a global media library.

## Create a project

Choose **New project** from Project Home or the File menu. The first tabs select the production type:

| Type | Starting workflow |
| --- | --- |
| Film | A movie with shots, scene labels and production notes. Existing Blank, Illustration, Anime, Storyboard and Animatic starters remain available. |
| Series | The Film workflow plus episode labels and episode filtering. The first shot starts with Episode 01. Episodes are labels within one ordered shot collection, not separate timelines or project files. |
| Drawing only | Background paintings and standalone artwork. Blank and Illustration starters are available; the first drawing has one frame and no animation track or camera keys. Timing controls are hidden and Illustration is the suggested workspace. |

Project type and canvas starter are separate choices. Blank creates one drawing layer. Illustration creates Colors, Sketch and Ink layers. Drawing-only starters use a plain canvas without production margins, a header or frame guides; the canvas and sheet controls remain editable. Changing type preserves the entered title, location, canvas dimensions and background choice.

**Create a project folder** is enabled for new projects. Choose a new destination name ending in .toonstudio; Toon Pro creates a directory at that location. Existing destinations are preserved rather than replaced. Uncheck this option to create a standalone .toonproj, or explicitly enter a .toonproj path.

New folder-based Film and Series projects open the Project workspace. Drawing-only projects open the drawing canvas with the selected illustration-oriented arrangement. Opening an existing Drawing-only project selects Illustration. Project settings display the existing type without offering a destructive type conversion.

## Project Home

Use the Home icon in the project toolbar or **File → Project Home**.

- **Recent**, **Pinned** and **Archived** organize the local project list.
- Select a project and choose **Open**, or activate its row.
- **Open project…** accepts a standalone .toonproj or a package's project.toonstudio marker.
- **Open folder…** opens a portable project directory.
- **Continue · project name** returns to the currently open drawing.
- Pin keeps a project easy to find. Archive moves its entry to Archived; Restore brings it back.
- The trash icon removes an entry from Home only. It does not delete, move or archive files on disk.

Home remembers up to 256 locations in local workspace preferences. It does not scan the computer for projects. Missing locations stay visible and cannot be opened from the stale entry; use Open at the new location to remember it again. Reopening an archived project returns it to Recent.

Only one production document is active in this window. Opening another project uses the existing Save / Discard / Cancel protection for unfinished artwork.

## Move between planning and drawing

The project toolbar and File menu provide **Project Home**, **Project workspace** and **Drawing workspace**. The Project workspace has **Shots** and **Assets** tabs; Drawing-only projects call the first tab **Artworks**.

Opening Home or the Project workspace temporarily hides the drawing panels. Returning to Drawing restores their arrangement. Workspace changes resolve pending notes and asset metadata, preserve transform decisions, and wait for an asset import to finish or be cancelled. Switching views does not add artwork history entries.

### Shots, artworks and planning

The Shots/Artworks collection reuses the existing add, duplicate, delete and reorder commands. Numbers and names can be renamed inline. Deletion is undoable and the collection retains at least one item.

Film and Series use scene labels; filtering a scene does not rearrange movie order. Series additionally exposes an Episode field and episode filter. All project types can filter by production stage.

Select an item to edit its **Stage** and **Notes**. Available stages are Planning, Storyboard, Layout, Animation, Cleanup, Color, Composite and Approved. These are planning labels: choosing Approved does not freeze artwork, render a version or prevent later edits.

Notes can hold an action description, dialogue, camera instructions or a painting brief. **Save notes** commits the selected item's episode, stage and notes as one undoable artwork-document change. **Revert notes** discards the unfinished draft. Valid notes also resolve when leaving the editor or navigating. A conflicting external edit preserves the draft and reports the conflict.

**Open shot** or **Open drawing** enters the selected artwork. Drawing-only projects can contain multiple artworks; their duration column is hidden. Canvas dimensions and color settings remain project-wide.

## Import and organize assets

Open **Project workspace → Assets**. A standalone .toonproj first offers **Create project folder…**, which saves a separate portable copy and switches to it.

Use the import **+** to select one or more files. Import copies source bytes into the project; originals remain untouched. Copying runs in the background and can be cancelled. The catalog changes only after the complete batch succeeds. Duplicate filenames receive separate asset identities and never overwrite one another.

The library offers:

- Type and folder filters, plus search across names, tags and folders.
- A selected asset's Name, comma-separated Tags and Folder.
- A new-folder action. Folder names may contain paths such as Backgrounds/Station.
- Image previews and explicit Play/Pause/Stop for audio and video.
- **Assets** and **Removed** tabs, with removal and restoration actions.
- Reload for refreshing the catalog after another operation changes it.

Folders are catalog organization; moving an entry between folders does not move or rename its stored file. Metadata edits save to the asset catalog when committed, separately from artwork Undo and .toonproj saves.

Imported files are classified as Image, Video, Audio, Effect, Model or File using extension/MIME information. Classification does **not** establish engine compatibility. A model file in this catalog is not automatically imported into the 3D engine, and an effect asset is not automatically applied to a layer. The current effect classification recognizes .toonfx, .cube, .3dl and .lut; the remaining types follow their file extension/MIME classification.

Image previews are prepared in the background. Audio/video playback depends on the installed or bundled multimedia decoder. Preview failure keeps the asset and reports the reason. Selecting another asset or leaving the panel stops media preview; imports and preview work do not run through live pen-sample handlers.

### Attach versus place a copy

**Attach** records the asset against the current shot or artwork for production reference. The **Attached to** list opens those targets. This is an explicit planning relationship, not automatic discovery of all artwork that resembles or was previously copied from the file.

**Place a copy** is available for images and audio:

- Images become independent raster artwork through the existing image-placement workflow. Images larger than the canvas fit within it while preserving proportions, then open transform handles. The current placement limit is 64 million decoded pixels.
- Audio enters the existing shot audio import/timeline workflow.
- Video, effect, model and generic file entries can be stored, organized and attached, but cannot be placed through this button in this pass.

Placement does not create a live dependency on the catalog file and does not automatically add an attachment. Editing the placed drawing or audio does not edit the stored asset. Use Attach deliberately when that production relationship is useful.

**Remove** moves a library entry to Removed. Stored asset bytes, external originals, explicit attachment metadata and already placed artwork remain intact. Restore returns an available asset to Assets. Permanent deletion, attachment removal, replacing originals and relinking missing media are not yet provided.

## Save, copy and move projects

A portable project directory contains:

    My film.toonstudio/
      project.toonstudio
      project.toonproj
      catalog.json
      assets/

The project.toonstudio marker identifies the folder. The project.toonproj file contains the existing editable production document. The catalog.json file stores media organization and attachment metadata. The assets directory holds independent original-byte copies under stable generated names.

Use **Save project** for artwork and production notes. Asset catalog changes save independently. **File → Save project folder as…** creates a new package from the current artwork and, when present, copies the existing asset catalog and media, including Removed entries. Save As for an open package uses the same package-copy flow. The source production remains unchanged and the successful copy becomes the active project.

Package creation prepares a complete folder before publishing it. Failure or cancellation before publication leaves no destination package. A cancellation that arrives after atomic publication cannot undo an already successful save. Choose a new location outside the source package; existing destinations and nested copies are rejected.

Move or back up the **whole directory**, then open it using Open folder or its marker. Relative asset paths remain valid after relocation. Moving only project.toonproj transfers artwork but not the asset catalog. Symbolic links are rejected for package-owned files and asset folders. Missing media remains visible in a valid catalog, but preview, placement, restoration or package copying can report that the file is unavailable.

New production types and shot planning metadata use project format 38 when required. Older projects without those fields open as Film with Planning as the default stage. Earlier application builds cannot safely read format-38 projects.

## Current limits and deferred work

The asset catalog supports up to 10,000 entries, including Removed assets, 2,048 virtual folders and 16 MiB of metadata. Each asset permits 32 tags of up to 80 characters. Catalog names support 256 characters; the current Name editor accepts 200. Folder paths support 240 characters. Episode names support 100 characters and production notes 16,000.

Media copying uses a bounded 1 MiB buffer, with a 16 TiB per-file limit and available disk space governing total storage. This does not establish video codec, image-decoder or 3D-engine support. The editable project.toonproj still has its existing 300 MiB container limit and existing drawing/resource budgets. A project folder does not yet provide lazy shot loading or remove those artwork limits.

The following remain planned:

- Dedicated storyboard panels/pages and automatic storyboard-to-animatic promotion.
- Script editing, task checklists and a nested sequence/episode production hierarchy.
- A global media library shared across projects, asset versions and relinking.
- Automatic dependency tracking, live linked artwork and approved revision pinning.
- A separate movie editor with cross-shot audio, editorial trims and transitions.
- Lazy loading of shot artwork and larger production-document storage.

Existing shot timelines, Scene/Movie review and exports remain available. They retain their documented capabilities; this organizational foundation does not turn movie review into a new editorial timeline.
