---
title: "Cel paint checks, shading and Light Table"
description: "Inspect finished flats, separate shadow regions and compare drawings without changing their sources."
category: "Anime production"
order: 80
---

These three workflows help finish a hand-drawn cel. **Cel Paint Check** inspects raster coloring. **Cel Shading** builds regions from the existing outline and temporary colored boundaries. **References → Light Table** places adjustable snapshots over the canvas for proportion and motion checks.

They have different commit rules. Paint Check repairs are previewed before applying. Shading drafts and Light Table pins can be saved unfinished. Reference overlays never become part of the movie image merely because they are visible on the canvas.

## Inspect a finished color pass

Open **Panels → Cel Paint Check**. In **Drawings**, check the raster paint layers you want to inspect. The list includes static artwork and layers inside other drawings of the current shot, allowing you to check several cels without exposing each one manually.

Choose the approved character and lighting palette if you want palette checking. You can also leave that check off and inspect coverage problems alone. Tolerance, repairable hole size and stray-pixel size let you adapt the check to the artwork's scale.

Click **Check**. The **Problems** tab lists suspected enclosed holes, disconnected stray regions, possible edge-reaching fill leaks and colors outside the palette. Previous and Next visit the issues and their drawing exposures. The canvas highlights the area while the panel shows the isolated drawing.

Treat each result as a question about the drawing. A detached sparkle is not necessarily a stray pixel; an open mouth can be intentional negative space; a background meeting the frame edge is not automatically a leak. The checks cannot infer your intended character silhouette.

## Preview only the repairs you want

Check the issues you agree with, then use **Preview**. The repair behavior depends on the issue:

- Small holes receive the current foreground color.
- Checked stray or leak regions are erased.
- Off-palette pixels map to the nearest approved RGB color.

Choose the correct foreground before filling holes, especially when checking different materials together. **Show original** compares the proposed result to its source. Apply commits the checked batch as one Undo operation; Cancel discards the proposal.

The check examines raw raster paint inside the artwork frame. It does not inspect composited effects, vector paths or overscan. Partly transparent edges and local antialias mixtures are excluded from flat-color checking. Large or complex batches are bounded; split work into smaller sets when the tool reports a limit.

Editing a source drawing or its palette invalidates older results. Rerun the check rather than treating yesterday's list as current. Pending repair previews must be resolved before leaving their context; choosing Cancel in that transition decision keeps the preview available.

## Build shadow and highlight regions

Open **Panels → Cel Shading** and choose a transparent raster outline layer. It can remain locked because the shading workflow reads the outline instead of painting on it.

1. Enable the boundary tool and choose **Shadow** or **Highlight**.
2. In **Boundary** mode, draw colored guide lines across the enclosed character forms. Guide tint is a working aid, separate from the final shade color.
3. Choose the fill color, switch to **Fill**, and click a region closed by the original outline plus the guides.
4. Choose the boundary finish: disappear at its centerline, or take the adjoining region color across its guide width.
5. Inspect the preview, then **Apply**.

Apply creates separate **Shadows** and **Highlights** raster layers below the outline in the same drawing folder. The line art remains unchanged. One Undo removes the generated result. Regions reaching the frame edge are rejected to prevent an open boundary from flooding the picture.

Each unfinished draft belongs to a shot, outline layer and frame. Save retains guides, fills, colors, modes and operation order. You can change drawings and return to that draft later. If its outline changes, the preview refreshes; inspect the new result before applying. A missing or unavailable source leaves the draft stored and explains why Apply is blocked.

The shading panel's own step-back removes the latest unfinished guide or fill. Artwork Undo preserves current drafts rather than serving as their step history. Applying clears the working draft; undoing the resulting paint does not reconstruct the cleared draft.

## Shift and trace with Light Table

![A pinned drawing with opacity and temporary alignment controls in Light Table.](/assets/screenshots/light-table.webp)

*A pinned drawing with opacity and temporary alignment controls in Light Table.*

Open **References → Light Table**. Pin the current layer, choose a drawing or folder from another shot, or import an image. Each pin is a snapshot, clearly separated from the source drawing.

Enable **Move/rotate/scale** and use its handles to align the selected snapshot. Position, uniform scale, angle and opacity also have exact controls. Reset restores placement. Then exit pin editing before drawing on the current artwork.

For a head turn, pin the previous drawing, align the eye line and temporarily rotate the reference to compare the jaw. This changes the reference placement only; it does not rotate the original cel. Use visibility checks to combine references, or Previous/Next to flip through pins in solo view.

**Refresh** deliberately updates a document pin from its source. Later edits do not silently replace its snapshot. If the source drawing is deleted, the snapshot remains usable, though refresh requires that source to exist again. Removing a pin leaves the source untouched.

Up to eight fitted snapshots share the Light Table budget. Their images, placement, opacity, visibility and selection are saved with the project. They remain separate from artwork Undo and from PNG or movie output. This makes Light Table suitable for unfinished proportion checks that you want to reopen tomorrow without accidentally exporting the references.
