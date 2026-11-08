# Your first drawing in Toon Pro

Current local development workflow · updated 6 November 2026

This guide follows the **0.76.0 local/source build** with the Projects dashboard. The published **0.75.0** preview still uses the earlier project flow. If your app opens a movie/project setup or shows different startup controls, check your version and the matching published guide before following these steps.

You can begin with one drawing. Animation timing and movie organization are optional next steps.

## 1. Open the current build

Build from the current source using the [README](../README.md#build-the-current-source), or open the already rebuilt development application. On this macOS development checkout the application is `build/ToonPro.app`.

The opening **Projects** screen shows projects you have opened or created here. Search, sorting, Pinned and Archived help you find them. Removing an entry from the recent-project list does not delete the project's files.

## 2. Create a Drawing project

1. Click **New project**.
2. Choose the **Drawing** card.
3. Enter a project name, such as `My sketchbook`.
4. If needed, use **Project location…** to choose the parent folder. The default is your Documents/Toon Pro folder.
5. Click **Create project**.

Your new project opens to its drawing gallery. Projects use ordinary `.toonstudio` folders; they do not reserve a separate disk or partition. Storage grows as you save documents and import media.

## 3. Set up the canvas

1. Click **New drawing**.
2. Enter a drawing name.
3. Choose **Landscape**, **Square**, **Portrait**, or use **Custom** dimensions. Width and height have a slider plus an exact pixel entry.
4. Use the aspect lock if you want dimensions to stay proportional. The swap action exchanges width and height.
5. Choose **White**, **Transparent**, or **Color** for the background.
6. Check the live canvas preview, then click **Create drawing**.

The drawing opens in its own editor tab. Drawing-project setup stays focused on the canvas; Movie-project creation also includes its selected folder and optional predefined/custom tags.

## 4. Make and save a mark

Choose a group in **Toolbox**, then a brush in **Tools**. Use **Colors** to choose the foreground and **Tool Properties** for size and other drawing settings. Paint on the editable drawing layer.

While the canvas has focus, **B** selects Brush, **E** selects Eraser and **H** selects Hand. Hold Space and drag to pan; use the wheel to zoom. **Cmd/Ctrl+Z** undoes the last committed edit. Escape cancels an active drawing gesture. Keyboard text fields keep their ordinary editing behavior.

Use **Cmd/Ctrl+S** or **File → Save document** to save the active document. A tab asterisk indicates unsaved artwork. If a background color creates a locked background layer, draw on the editable layer above it.

The tool and advanced-panel documentation explains each workflow's scope; start with [drawing tools](drawing-tools.md), [layers](layers.md) and [brush settings](brush-settings.md).

## 5. Keep several drawings open

Return to **Project dashboard** from the same header row as the document tabs. Create another drawing, or select several gallery items with **Cmd/Ctrl-click** or **Shift-click** and choose **Open selected**.

Each native document retains its own canvas dimensions, artwork, undo history and view. Switching tabs does not export or reload the drawing. Reopening an already-open item selects its existing tab.

Finish or cancel an active stroke or unresolved edit before navigating. Closing a dirty tab offers **Save / Discard / Cancel**; Cancel keeps the tab open. Open documents stay loaded in memory, so the practical number depends on the artwork and computer. There is no automatic inactive-tab unloading yet.

## 6. Add reference materials

Every project has a separate **Assets** destination.

1. Open **Assets**, then **Import assets**.
2. Use **Add files…** to choose supported images, video or audio.
3. Review the file list. Remove unwanted entries and choose an asset folder.
4. Add optional predefined or custom tags, then confirm the import.

Files are copied into the project. The originals you selected remain in place. Import publishes the batch only after it succeeds; cancellation before publication leaves no visible partial batch.

Select an imported image for its preview and details. Video/audio offer deliberate Play/Pause, Stop and seek; they do not autoplay on selection. Codec support follows the runtime. Current gallery cards use images/native first frames where available and source-kind icons for video/audio.

To organize an existing native drawing or animation as an asset, use its gallery context/overflow menu and **Move to Assets…**. Choose its destination and tags, then confirm. This keeps the same editable item; it does not flatten it to an image or render it to video. Importing an asset is separate from placing it into a drawing. The new manager's asset-to-document placement UX remains future work.

## When you are ready for animation or a Movie project

| Your next task | Choose | What it means |
| --- | --- | --- |
| Practice animation poses and timing | **Animation** project → **New animation** | One independent animated shot per document, with its own FPS and frame count |
| Organize a larger collection | **Movie** project | Nested folders of drawings and single-shot animations, plus tags |
| Collect reusable reference or source material | **Assets** in any project | Imported media and native editable assets organized separately from ordinary content |

A Movie project is the organizer in this flow. Its drawing/animation documents still open in focused editors. It is not a completed multi-shot editing or delivery system. A Storyboard or Animatics tag describes purpose; it does not convert a document into a new editor mode.

## Protect the project

Save normally and use **Back up saved project…** from the project actions when you need a copy. This manual backup captures saved project state; it does not save open dirty tabs for you. A recovery prompt can offer newer unsaved work when reopening an item, but periodic recovery is not a substitute for backups.

Keep the complete `.toonstudio` folder together, including its catalog, documents and media. Do not edit its internal revision pointers by hand. Current active-project workflows use a local filesystem and one writer; simultaneous network/cloud editing is not implemented. Native document payloads retain the existing 300 MiB container limit. [Read the storage and recovery boundaries](managed-workspace.md).

## Find the next answer

- [Purpose and product vision](product-vision.md)
- [Project and asset workflow, implemented controls and limits](managed-workspace.md)
- [Animation tools and their versioned behavior](animation.md)
- [Current implementation and verification](DEVELOPMENT.md)
- [Published downloads and platform availability](https://complex1.github.io/ToonPro/download/)
- [Website manual and feature content](../website/data/manual.json)
