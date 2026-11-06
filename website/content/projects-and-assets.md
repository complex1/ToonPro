---
title: Projects, drawing tabs & Assets
description: Start with a drawing, open independent documents in tabs, and organize Movie folders and editable project Assets.
category: Start here
order: 15
status: development
version: Local development · 0.76
---

This guide covers the **new managed-project workflow in local development**. It is not included in the published 0.75.0 downloads. If your build opens the earlier movie-project setup, use [Your first project](getting-started.md). Download availability is listed on the [downloads page](/download/).

## Choose the work you want to do

The app opens on **Projects**, where compact cards show projects you have opened or created. Use Search, sort by last opened or name, and switch between grid and list. A project's menu offers Pin and Archive. Removing a recent entry keeps the project files.

Choose **New project**, enter a name and select a type:

| Type | What the dashboard contains | Your next action |
| --- | --- | --- |
| Drawing | A simple collection of drawing documents | New drawing |
| Animation | Independent animations, each containing one shot | New animation |
| Movie | Folders, drawings and independent animated shots | Create a folder, drawing or animation |

Each project also has its own **Assets** destination. Begin with Drawing if you want a sketchbook or illustration project. Animation is useful for a collection of movements or short studies. Movie adds organization when your story has more pieces to manage.

Project type is an organizational choice. A Movie project does not turn the drawing editor into a sequence-editing timeline.

## Start with a drawing

1. Open a **Drawing** project and choose **New drawing**.
2. Give the drawing a name.
3. Choose **Landscape**, **Square**, **Portrait** or **Custom**. Width and Height each have a slider and an exact-entry field.
4. Use the lock to retain the current aspect ratio while changing dimensions. Use Swap to exchange width and height.
5. Choose **White**, **Transparent** or **Color** for the artwork background. The live preview shows the canvas proportions and selected background.
6. Choose **Create drawing** to open the document in its own editor tab.

In a Movie project, the setup also shows the current folder and offers optional predefined and custom tags. Drawing-project setup stays focused on the name, canvas and background.

The initial presets are 1920 × 1080 landscape, 2048 × 2048 square and 1080 × 1920 portrait. They are starting points; choose dimensions for the actual work before you begin.

For the first marks, choose an editable raster layer, select Round Ink or Round Sketch, choose a foreground color and draw. Save with **Cmd/Ctrl+S**. [Drawing workspace](drawing-workspace.md) explains the tools and panel arrangements in the public-preview toolset; the managed editor adds the focused document boundary described here.

## Keep several documents open

Ctrl/Cmd-click or Shift-click native drawings or animations on the dashboard, then choose **Open selected**. Each opens in its own tab, retaining its artwork, Undo history, playhead and canvas view. Opening a document that is already open selects its existing tab.

Projects and Project dashboard navigation share one header row with the document tabs. Return to the dashboard to choose another drawing; the existing tabs stay loaded. A star beside a name means the document has unsaved artwork changes.

Finish an active stroke, transform or numeric edit before switching tabs. An unfinished gesture can prevent navigation so it is not lost when the canvas is hidden.

Closing a dirty tab offers **Save**, **Discard** or **Cancel**. Cancel keeps that tab and its artwork open. Multiple documents stay in memory; automatic eviction of inactive tabs and production-scale memory budgets are not implemented.

## Animate one shot

An Animation project lists independent shots, with thumbnail, saved duration and frame rate where a preview is available. Choose **New animation** and set the canvas, background, frame rate and frame count.

The new document has one shot and a Simple animation drawing track. Create cels, adjust holds, use onion skin and play back the current shot with the existing animation tools. [Frame-by-frame animation](frame-by-frame-animation.md) explains drawings, frames and exposure timing.

The linked tool guides show public-preview controls. Managed editors omit the Shots collection and Storyboarding/Animatics layouts, and their settings command is **Drawing & timing settings…**.

The managed editor keeps animation focused on that shot. Its video and audio export scopes are locked to the current shot. Movie/Scene sequence review and shot-collection sections belong to the retained earlier infrastructure, rather than this focused editor.

## Organize a Movie

A Movie project opens on Content. Create folders and nested folders to match the way you think: Opening, Backgrounds, Characters, or a sequence you have named yourself. There is no required episode/scene hierarchy.

Select a folder before creating a drawing or animation to put the new item there. Folder tiles show direct child folders; the folder sidebar lets you move through the hierarchy.

Tags describe purpose. The predefined choices are **Storyboard**, **Animatics**, **Background**, **Asset** and **Character sheet**. Add a custom tag with **Add** or Return; remove a custom tag using its chip, or uncheck a predefined tag. An Animatics tag does not change the document into another native type.

Search names and tags, filter by tag, and choose grid or list. Compact cells retain their size as the window grows, allowing more columns. The ordinary Drawing and Animation content dashboards keep folder controls out of the way; their separate Assets destination supports asset folders.

## Import reference media

Open **Assets → Import assets**. The review dialog lets you:

1. Add supported image, video or audio files.
2. Review filenames, types and sizes; remove anything you do not want to import.
3. Choose the destination asset folder.
4. Apply predefined or custom tags to the batch.
5. Confirm the import.

Files are copied into the project. The originals stay where they are. Import publishes the batch when it succeeds, with progress and cancellation during copying. Two source files with the same filename receive different identities.

Select an asset to inspect it. Image and native-document previews are prepared in the background. Video and audio have explicit Play/Pause, Stop and seek controls; selection does not autoplay. Decoder support depends on the runtime. Video/audio gallery cards currently use type icons, rather than generated waveforms or video poster thumbnails.

<figure><img src="/assets/screenshots/project-assets.webp" alt="Actual local-development Assets library with type filters, real asset previews and a tag inspector"><figcaption>Actual local-development app · project-owned assets and direct tag editing</figcaption></figure>

The initial import applies tags to the batch. Per-file import tags, drag/drop importing, image preview zoom, automatic usage tracking and live linked placement are not part of this workflow yet.

## Keep assets editable

Select a native drawing or single-shot animation and choose **Move to Assets…** from its menu. Review the item summary, choose an asset folder and confirm its tags.

This changes organization for the same item. A drawing keeps its layers and source data; an animation keeps its drawings, timing and supported shot content. It does not flatten the drawing or export a replacement video. An already-open tab remains valid.

Open a native asset to edit the original in a document tab. Imported media opens in the inspector; selecting it does not silently convert it into a native drawing. Use **Move to Content…** where the destination accepts the native item. **Undo organization** is separate from brush-stroke Undo.

## Save and return safely

**Save document** writes the active document. Periodic recovery can retain newer unsaved changes; reopening an item with recovery asks whether to restore them. Recovery is useful protection, not a substitute for intentional saves.

A project is a normal `.toonstudio` folder containing its catalog, native documents and copied media. It does not reserve a disk partition. Use the project's **Back up saved project…** action for a copy of saved data. The backup does not silently save dirty document tabs.

**Remove** retains an item's bytes. **Restore** is available from Removed in the Movie/Assets folder sidebar. There is no permanent-delete UI. Folder rename/reparent, organization redo, global asset libraries and advanced relinking remain future work.

## Choose the next step

- [Brushes & dynamics](brushes-and-dynamics.md) for the marks and tools.
- [Frame-by-frame animation](frame-by-frame-animation.md) for your first moving study.
- [References & characters](references-and-characters.md) for model sheets and palettes.
- [Product vision](/vision/) for the longer-term direction.
- [Workflow comparison](/compare/) for the fit and trade-offs relative to other tools.

The illustrated tool guides describe the public-preview controls. This development guide supplies the new project, document-tab and Assets context around them.
