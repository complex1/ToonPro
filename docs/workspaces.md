# Project types, starters and workspaces

**Local implementation update:** the normal app now uses the [managed-project dashboard and document-tab workflow](managed-workspace.md). This page retains the earlier version-1 project/editor behavior for reference; its Film/Series creation flow is not the new default entry. No legacy migration is being implemented.

**Scope note:** the presets below describe retained editor panel arrangements. The new normal entry uses managed project dashboards and separate document tabs; see [managed workspace](managed-workspace.md). Its focused editors enforce single-document authoring independently of layout restoration.

A **project type** selects Film, Series or Drawing only. A **project starter** creates canvas settings, the first shot/artwork and its initial drawing layers. A **workspace** arranges the editor's panels. Choose the starter and panel arrangement independently: an illustration can use the Anime workspace, and an existing movie can switch workspaces without replacing its drawings.

The starter/layout foundation arrived in 0.74. Current source 0.76 adds Project Home, portable project folders, production planning and project Assets. See [production projects](production-projects.md) for those controls and their limits. The published 0.75 app does not include the 0.76 project-management additions.

## Create a project

Open **File → New project** or use New project on Home. Film is initially selected with the Anime starter. Film and Series provide five template tabs, a live canvas preview and editable project, timing and production-sheet settings. Series adds an Episode label.

Choose **Drawing only** for backgrounds or standalone paintings. It offers Blank and Illustration, starts with one frame and no animation tracks, and hides timing/shot-number fields. It suggests Illustration and opens directly on the canvas. Drawing-only Illustration defaults to 1920 × 1080 with editable dimensions; changing production type preserves dimensions already entered. New drawings have no production margins/header/guides unless those settings are explicitly enabled.

**Create a project folder** defaults on and creates a new .toonstudio directory with artwork and Assets storage. Uncheck it or explicitly enter a .toonproj path for a standalone file. Folder-based Film/Series projects open Project workspace after creation; Open shot enters the drawing editor. Existing project-folder destinations are preserved. The table below describes Film/Series starter defaults.

| Starter | Output dimensions | Timing at 24 FPS | Initial artwork | Suggested workspace |
| --- | --- | --- | --- | --- |
| Blank | 1920 × 1080 | 1 frame | One raster layer, Drawing | Illustration |
| Illustration | 2048 × 2048 | 1 frame | Colors, Sketch and Ink raster layers | Illustration |
| Anime | 1920 × 1080 | 120 frames / 5 seconds | Static Background plus a Character animation track; Drawing 1 is exposed for the first 2 frames | Anime |
| Storyboard | 1920 × 1080 | 72 frames / 3 seconds | Static Background plus a Boards animation track; Drawing 1 fills the shot's duration | Storyboarding |
| Animatic | 1920 × 1080 | 240 frames / 10 seconds | Static Background plus a Boards animation track; Drawing 1 is exposed for the first 48 frames | Animatics |

Every starter creates one shot or artwork. The Character track uses the Anime layer template: Rough, Sketch, Colors, Shadow, Highlights and Line Art. The Boards tracks use Rough, Sketch and Clean-up. These are editable drawing layers inside a drawing folder and track folder. Creation selects Drawing for Blank, Sketch for Illustration, or Rough for the animation starters.

All starters begin with a white artwork background, sRGB and 8-bit RGBA. **Transparent artwork background** is editable. Production-sheet export continues to use white paper; movie-frame export follows the artwork transparency setting. No starter imports audio, creates motion keys or generates artwork. The Storyboard starter creates one held board within a shot; it does not generate a multi-panel page grid. Use the existing Shots and Timeline controls to extend the project.

### Sheet defaults

| Starter | Margins: left / right / top / bottom | Overscan on each side | Minimum header | Header and frame guides |
| --- | --- | --- | --- | --- |
| Blank, Illustration, Animatic | 0 / 0 / 0 / 0 px | 0 px | 0 px | Off |
| Anime | 100 / 100 / 60 / 100 px | 120 px | 160 px | On |
| Storyboard | 80 / 80 / 40 / 80 px | 0 px | 140 px | On |

Blank, Illustration and Animatic therefore have a sheet exactly the requested output size. Their plain canvas has no production text, boundary outline or registration marks. The **90% safe area** starts off for every starter.

The **Production header** and **Frame guides** switches are independent. When enabled, the header grows beyond its minimum height to fit the movie title, cut number and shot name. Disabling it removes the header's space as well as its text. Frame guides control the output boundary, drawing boundary, registration marks and optional safe area. Drawing/production guides created with the guide tools remain separate objects.

Switching starter tabs preserves the entered movie title, cut number, shot name and save location. Canvas, timing, sheet settings and starter layers change to the chosen defaults and remain customizable. The suggested workspace follows the starter until the artist explicitly selects a workspace, including **Keep current**. Keep current retains the current panel arrangement when the project is created.

Changing the initial duration keeps the starter exposure within the shot. Storyboard's first board continues to cover the entire chosen duration. Anime and Animatic retain their respective 2- and 48-frame initial holds, shortened when the shot is shorter. Frames displayed to the artist start at 1.

**File → Project & shot settings** edits the open project's dimensions/FPS/background and the current shot's metadata, duration and sheet settings. It displays the saved production type without conversion controls, does not offer starter tabs and does not replace existing layers. Drawing-only settings hide timing and use drawing names. A newly added shot inherits the current shot's sheet settings and duration through the existing Shots workflow.

## Production browsing and the drawing editor

The project toolbar and File menu provide **Project Home**, **Project workspace** and **Drawing workspace**. Home remembers Recent/Pinned/Archived entries. Project workspace contains Shots/Artworks planning and the project Assets catalog. Returning to Drawing restores the editor's dock arrangement; these navigation actions are separate from the named panel-layout presets below.

Open projects still use one active document in the window. Drafts, imports and unfinished drawing gestures resolve before a transition. A shot's episode, stage and notes belong to the project; Home organization and editor layouts remain local workspace preferences.

## Choose a workspace

Use **View → Workspace** or the Workspace button in the top toolbar. All presets retain the Toolbox icon column and Quick Access toolbar. Other panels remain available from **Panels**.

| Workspace | Arrangement and purpose |
| --- | --- |
| Illustration | Drawing tools, brush sizes and properties at the left; Colors, References, Navigator and Layers at the right. Timeline and Frame Properties are hidden to give the canvas more height. |
| Anime | Tools and brush sizes share tabs above Properties. References, Colors, Layers and Frame Properties remain available at the right, with Timeline below the canvas for drawing exposure and motion work. |
| Storyboarding | Shots, Tools and brush sizes share the left column, with Shots initially raised and Properties below. References, Colors and Layers support drawing and shot organization. Timeline starts hidden. |
| Animatics | Shots, Tools, brush sizes and Properties share tabs at the left. Frame Properties and References share an inspector area at the right, alongside Layers. A taller bottom Timeline gives timing, audio and camera work more room. |
| Compact drawing | Drawing controls use the existing drawer, while Colors, References, Navigator and Layers remain available. Open the drawer to adjust tools without reserving a permanent drawing-controls column. |

Anime targets roughly 24% of window height for Timeline; Animatics targets 30%, bounded to 180–340 pixels. Qt can adjust these sizes to accommodate the available window and panel minimum sizes. Presets arrange existing panels rather than introducing separate document modes.

Switching workspaces pauses playback and returns to the normal workspace if Tab drawing mode is active. It preserves project contents, editing history and the current drawing context. Restoring the Frame Properties category does not activate a camera tool or change the timeline selection.

## Save a personal arrangement

Arrange, resize, tab or float the panels, then use these commands in **View → Workspace**:

| Command | Behavior |
| --- | --- |
| Save workspace as… | Save the current arrangement under a new name and make it the active custom workspace. |
| Update saved workspace | Replace the active custom workspace's stored arrangement. Built-in presets cannot be overwritten. |
| Reload workspace | Restore the active built-in preset or custom saved arrangement. |
| Previous layout | Return to the arrangement before the last successful workspace switch or reload. Repeating it swaps the two arrangements; it is not a multi-step history. |
| Manage workspaces… | Select a custom workspace to use, rename or delete. Double-clicking a row or pressing Return/Enter on the selected row also uses it. Return inside an inline rename commits only the name. |

Custom workspace names are unique without regard to letter case, contain 1–100 characters on one line, and cannot use built-in names. Up to 64 custom workspaces can be stored. Renaming or removing a saved entry updates the workspace library immediately. Removing the active entry retains the current panel arrangement as an unnamed current layout.

A saved workspace contains dock and toolbar arrangement/visibility, tab groups, panel sizes and floating states, Compact drawing mode, the drawing drawer's open state/page, and the selected Frame Properties category. It does **not** capture artwork, active brush/tool choices, tool groups/tags, Quick Access contents, canvas zoom/rotation, shortcuts or main-window size/position. The ordinary session settings still remember the last window geometry and current arrangement separately.

The named library is stored locally in `workspace.ini`, independently of `.toonproj` files. Invalid, unsupported or concurrently changed library data is preserved and reported instead of being overwritten. A saved layout that cannot be restored leaves the prior panel arrangement in place. Opening **Manage workspaces** reloads the named library.

Finish or cancel active pen gestures, transforms, selections and unfinished exact-entry values before switching, saving, updating or managing layouts. The existing drawing-workspace guard keeps these operations from silently committing a draft or moving controls during a stroke.

## Project compatibility

Header and frame-guide visibility are explicit sheet settings in project format **37**. They default to on when opening earlier files, including the legacy behavior where a zero minimum header height still grows to fit production text. Only a false visibility flag is written; projects retaining both defaults continue to use the format required by their other features. The same rule covers sheet settings inside Symbol definitions.

Files using either new visibility flag require a reader supporting format 37. Named workspaces use their own versioned local library and do not change the project file format. The 0.74 app build and 126 regression suites pass. Ten native Mac suites and ten offscreen Retina suites each pass 145 checks, including New Project and workspace dialog focus. The native Return-key fix also passes both affected regression suites and native/Retina follow-ups. See the [release checklist](TODO.md) and [development log](DEVELOPMENT.md).

## Reference decisions

Clip Studio Paint separates document purposes and exposes canvas/output-frame settings, templates and production information when creating a document. Toon Pro uses that distinction for editable starter choices, while keeping its existing movie-with-shots structure. [Clip Studio Paint: Creating a New Canvas](https://help.clip-studio.com/en-us/manual_en/210_file/Creating_a_New_Canvas.htm)

Clip Studio Paint supports naming, switching, reloading and managing workspaces; Krita describes workspaces as saved docker arrangements and treats multi-window sessions separately. Toon Pro follows the named-layout workflow with a focused local scope, preserving tool choices and main-window geometry outside each saved layout. [Clip Studio Paint: Register and manage your workspace](https://help.clip-studio.com/en-us/manual_en/690_interface/Register_and_manage_your_workspace.htm), [Krita: Workspaces](https://docs.krita.org/en/reference_manual/resource_management/resource_workspace.html)

Storyboard Pro distinguishes drawing-oriented workspaces from timeline-oriented animatic workspaces. Its Timeline brings together panel timing, sound and camera work. This informed the Toon Pro layout choices: Storyboarding prioritizes the canvas and Shots, while Animatics gives Timeline and its inspector more space. These are layout decisions using Toon Pro's existing tools, not a claim of equivalent Storyboard Pro functionality. [Storyboard Pro 20: About Workspaces](https://docs.toonboom.com/help/storyboard-pro-20/storyboard/interface/about-workspace.html), [Storyboard Pro 25: Timeline View](https://docs.toonboom.com/help/storyboard-pro-25/storyboard/reference/views/timeline-view.html)
