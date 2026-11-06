---
title: "Your first project"
description: "Choose a starter, understand shots and production sheets, and save your first drawing."
category: "Start here"
order: 10
---

**Build covered: published 0.75.0 preview.** If your app opens the new Projects gallery with Drawing / Animation / Movie cards, use [Projects, drawing tabs & Assets](projects-and-assets.md), the local-development guide. The new flow is not in the current public downloads.


Toon Pro brings drawing and animation into one movie project. You can use that project for a single illustration, one animated cut, or several shots. Start with the canvas and drawing structure that suit your work; you can arrange the panels independently afterward.

This guide takes you from the opening window to a saved drawing and two different image exports. You do not need to set up an animation timeline to paint an illustration.

## Install a test build

Choose a package for your computer when it is available on the Downloads page. Each release identifies the platforms that passed its checks; a listed build target does not mean its download is ready.

| Computer | Minimum build target | Package | 3D reference view |
| --- | --- | --- | --- |
| Mac with Apple Silicon | macOS 13 | DMG or ZIP | Available |
| Mac with Intel processor | macOS 13 | DMG or ZIP | Unavailable in this build |
| Windows PC, x64 | Windows 10 version 1809 or Windows 11 | Portable ZIP | Unavailable in this build |

These minimums follow the packaged Qt runtime; they are not a claim that every OS version has been tested. The release notes record the actual checks. [Qt platform requirements](https://doc.qt.io/qt-6.10/supported-platforms.html)

**On Mac:** open the DMG, drag **ToonPro.app** to Applications, eject the image and open the copied app. If you choose the ZIP, extract it and move the app to Applications. Save your work and quit any older copy before replacing it. Current test builds are ad-hoc signed and are not notarized. If macOS blocks a preview you trust, Apple's per-app approval is under **System Settings → Privacy & Security → Open Anyway** after the first launch attempt. [Apple's opening instructions](https://support.apple.com/102445)

**On Windows:** use **Extract All** on the ZIP, keep the extracted folder intact and open **bin → ToonPro.exe**. Do not launch inside the ZIP or move the executable away from its DLLs and plugins. Qt, the media libraries and required Microsoft runtime DLLs are bundled; you do not need to install the development tools. Current Windows previews are unsigned and may trigger a publisher warning. “Portable ZIP” describes the package: your settings and brush libraries are still saved in your user profile.

The Intel Mac and Windows builds retain drawing, animation, audio and project storage. Choose the Apple Silicon build when your workflow needs the rendered 3D reference view.

## Choose a project starter

Open **File → New movie project…**. The starter tabs change the initial canvas, timing, production paper and layers. The preview shows how those settings fit together.

| Starter | Best starting point | Initial setup |
| --- | --- | --- |
| Blank | A plain drawing or imported image | 1920 × 1080, one frame, one raster Drawing layer |
| Illustration | A square finished illustration | 2048 × 2048, one frame, Colors, Sketch and Ink layers |
| Anime | A hand-drawn animated cut | 1920 × 1080, 120 frames, Background and a Character drawing track |
| Storyboard | A held board with production information | 1920 × 1080, 72 frames, Background and a Boards track |
| Animatic | A longer timed board | 1920 × 1080, 240 frames, Background and a Boards track |

The starting frame rate is 24 FPS. These are editable starting settings, so choose your actual output dimensions, FPS and duration before drawing. Duration is entered in frames, with the corresponding time shown for context. Every starter creates a movie containing its first shot.

Enter the movie title, cut number and shot name. Choose a save location for the `.toonproj` file. The starter's suggested workspace is a panel arrangement, not a restriction on what the document can contain. Choose **Keep current** if you already have a layout you like.

The Anime drawing template contains Rough, Sketch, Colors, Shadow, Highlights and Line Art. Its first drawing initially holds for two frames. Storyboard's first board covers the selected shot duration; Animatic starts with a 48-frame hold, shortened if the shot is shorter. No starter creates artwork, imports sound or adds motion keys for you.

## Understand the picture and the paper

The **output frame** is the picture intended for movie delivery. **Overscan** is drawable space around it, useful for backgrounds that will be reframed. Production-sheet **margins** and the **header** provide room for shot information and notes.

Blank, Illustration and Animatic start without those surrounding sheet elements. Anime starts with overscan, margins, a production header and frame guides. Storyboard starts with margins and a header. The **Production header** and **Frame guides** switches are independent. Turning off the header also removes the space reserved for it.

Choose **Transparent artwork background** when the output should retain transparency. Production-sheet paper remains white. If you intend to pan a camera across a wide background, provide enough drawable space before painting it; moving a camera does not create new raster pixels outside the drawing extent.

## Make the first marks

1. Create the project and select an editable raster layer in **Layers**. With the Anime starter, Rough is a useful first target.
2. Select **Drawing** in **Toolbox**, then choose **Round Ink** or **Round Sketch** in **Tools**.
3. Pick a foreground color in **Colors**. Adjust **Size** and **Smoothing** in **Properties**; begin with modest smoothing so your pen does not feel delayed.
4. Draw on the canvas. The pointer outlines the current tip, so a stamp can show a different shape from a round pen.
5. Press **Cmd/Ctrl+Z** to undo a stroke. Save with **Cmd/Ctrl+S**.

Hover an unfamiliar icon briefly to see its tooltip. Numeric properties offer both a slider and a small exact-entry box. Enter commits a typed value; Escape cancels an unfinished numeric edit. A compact box can still contain a value longer than three digits.

If the canvas shows a forbidden pointer, read the message at the bottom. A hidden or locked layer, an empty drawing interval, or a tool that requires another layer type can block painting. The message identifies the context to change.

## Save, reopen and deliver

**File → Save project** preserves editable layers, shots and supported embedded resources. **Save project as…** creates a separate project file. Keep intentional milestone copies when you need a long-term version history: Undo is session history, not an archive of every saved version.

Try both still-image exports:

- **Export movie frame PNG…** produces the output picture. It excludes the production header, Notes layers and editor references.
- **Export production sheet PNG…** includes the production paper and its information, with Notes where present.

Reopen the `.toonproj` through **File → Open project…** to continue editing. Exported PNGs are deliverables; they do not replace the editable project.

Use **File → Project & shot settings…** to revise the open document's dimensions, FPS, background, shot metadata or sheet settings. This dialog does not rerun a starter or replace your layers. Changing output dimensions reframes existing artwork; it does not resample it into a newly scaled drawing.

Continue with [Drawing workspace](drawing-workspace.md) to arrange controls, or [Frame-by-frame animation](frame-by-frame-animation.md) when you are ready to create another drawing.
