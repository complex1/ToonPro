---
title: "References and character libraries"
description: "Keep model sheets and approved character colors available across shots, then preview palette changes."
category: "Anime production"
order: 70
---

References keeps visual decisions near the drawing without placing them into movie output. Its tabs distinguish shot reference images, project-wide character sheets, character color models and Light Table snapshots. Choose the kind of reference that matches how you intend to reuse it.

## Reference image or artwork layer?

Use **References → Images** for pictures you want to look at while drawing. They belong to the current shot and remain separate from exported artwork. Pan, zoom and fit the viewer to inspect details. When Eyedropper is selected, clicking or dragging over a reference samples its pixels; Alt/Option-click also provides temporary sampling in supported reference viewers.

Use **File → Import image as artwork layer…**, or the Layers import icon, when the image should become part of the actual picture. That command creates a raster layer, preserves transparency, centers the image and fits an oversized import inside the output frame. Transform opens immediately for positioning. Cancelling that positioning leaves the imported layer; Undo removes the import itself.

This distinction matters for backgrounds: a photo in References will not suddenly appear in an export. A background image placed as artwork will, subject to its layer visibility and the output frame.

## Create a character's model-sheet collection

1. Open **Panels → References → Model Sheets**.
2. Use the character-row **+** and give the character a recognizable name.
3. Select **Turnarounds**, **Expressions** or **Costumes**.
4. Import one or several images, then name the selected sheet clearly.
5. Use its thumbnail to return to it while drawing another shot.

The character library is shared by every shot in the movie project. Changing shots preserves the current model-sheet browsing context, including its zoom and pan. A single sheet uses the full viewer rather than a redundant strip of one thumbnail.

The pencil renames a character. The character-row trash removes that character and its sheets, with document Undo. Right-click a sheet or thumbnail to rename, move it to another category, or remove it. Imported copies are embedded in the project, so the original image file is not required when reopening. Source files are not overwritten.

For a consistent production set, use names such as “Front neutral,” “Three-quarter smile” and “Winter coat.” Categories provide the broad structure; names identify the drawing you need. Model sheets are shared within a movie, not automatically across every project on the computer.

## Define approved character colors

![A character’s Night color variant organized by material and shading role.](/assets/screenshots/character-colors.webp)

*A character’s Night color variant organized by material and shading role.*

Open **References → Color Models** and choose the same character. **Skin**, **Hair**, **Costume** and **Shadow** organize named swatches. Select the drawing foreground in Colors, then use the color-row **+** to save it as a role such as “Skin base” or “Coat shadow.”

Click a swatch, or select it and press Enter, to copy its exact color and opacity into the drawing foreground. Merely browsing a character or category does not change your paint color. **Replace** updates the selected swatch from the foreground; **Edit color** opens the picker for a deliberate adjustment.

Rename the role in its name field. Right-click to move it to another category or remove it. Character colors use document Undo and are stored with the movie. They are separate from the global named palettes in **Colors**, which remain local artist preferences.

For hexadecimal entry, color editing accepts `#RRGGBB` and `#AARRGGBB`; the latter places alpha first. Use clear role names rather than putting a lighting condition into every swatch name, because lighting palettes already organize that distinction.

## Make Day, Night, Sunset and Interior palettes

The lighting tabs sit above the category tabs. **Day** is the base palette. Newly created characters also receive Night, Sunset and Interior, initially copied from Day. They are editable starting copies, not automatic lighting filters.

Choose Night and change the role colors to your approved night treatment. Editing a color affects only that palette. Renaming or moving the role changes its shared identity across palettes. Adding a new role initializes it in every palette, and removing it removes the role from every palette.

The palette-row **+** or **Duplicate palette…** makes another variation. Rename and Remove apply to extra palettes while Day remains. For an older character, **Lighting palette options → Add missing lighting palettes** adds missing standard copies without replacing existing ones.

The selected variant stays remembered per character while the project is open. Reopening starts on Day with all saved variations still available. Choosing Night does not recolor pixels you already painted; use the explicit batch workflow below.

## Recolor existing drawings with a preview

![Preview a Night palette on selected drawings before applying recoloring.](/assets/screenshots/character-recolor.webp)

*Preview a Night palette on selected drawings before applying recoloring.*

1. Choose the character and open **Recolor drawings…** in Color Models.
2. Set **From** to the palette already used and **To** to its replacement.
3. Choose **Current shot** or **All shots**, then check the raster drawings to change. Only the active drawing starts checked.
4. Click **Preview** and inspect each prepared drawing using **Before / After**. Pan and zoom to examine edge colors.
5. Choose **Apply** for one batch Undo operation, or Cancel to leave the artwork untouched.

The preview shows raw paint before other layers, masks, effects and animation transforms. Color roles map source RGB values to target RGB values while preserving painted alpha. Unrelated colors, broad blends and ambiguous mixtures are left alone. If two roles share one source color but require different replacement colors, the tool reports the ambiguity rather than guessing.

Define genuinely different variant colors before expecting a visible change. Hidden, locked, Notes and unsupported non-raster targets are unavailable. A later palette edit does not automatically recolor the drawing again; repeat this deliberate preview operation when needed. [Cel Paint Check](cel-paint-and-light-table.md) can then compare the finished flats against the chosen approved variant.
