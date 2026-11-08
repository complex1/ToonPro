# Detect perspective from an image

The local **0.76** build can propose editable perspective guides from a selected layer or a local image file. Detection runs on the computer using native image analysis; it does not require an AI model, GPU, account or upload. Published 0.75 packages are unchanged.

## Open and detect

Select a layer and choose **Edit → Detect perspective…**. The same action is available in the layer context menu and through the **sparkles** button in Guides Tool Properties.

The dialog opens on **Selected layer**. Locked artwork can be analyzed without unlocking it. Folders include their visible children; a selected branch retains its ancestor transforms, masks and effects. A clipped source includes the clipping backdrop, and an adjustment includes its affected lower siblings. The source caption identifies those contextual captures. If a parent folder is itself clipped, select that folder to capture its complete stack.

Choose **Image file** or the folder button to read a local image instead. Files are fitted and centered in the drawing for the proposed guide placement. The file remains unchanged and is not inserted into the layer stack or saved as an external dependency. For guides that match a particular placed image exactly, import the image first, position it, then detect from its layer.

## Review the proposal

- **Auto / One-point / Two-point:** choose the intended model. The tool proposes only supported directions. Parallel evidence creates ordinary parallel rulers; it is not converted into a fictional distant vanishing point.
- **Detail:** adjust edge sensitivity with the slider or exact entry. It reruns detection on the source preview.
- **Directions:** check the vanishing-point or parallel families to include. Auto supports up to two finite vanishing points, One-point one, and Two-point requires two. Weak alternatives remain available for review.
- **Density:** change the number of divisions in the proposed guides. Distant vanishing points can make a low-density grid sparse; increase Density or use the existing guide controls after adding.

The preview shows detected source edges and the proposed grid. Click an edge in **Adjust** mode to exclude or restore it and refit the candidates. Drag a vanishing-point circle or the anchor cross to correct placement. **Fit image** restores the source view; **Fit guides** includes chosen off-image vanishing points. The wheel zooms, and middle-button or Space-drag pans.

Use **Region** and drag around the building, room or other area to analyze. A region limits detection evidence; the resulting guides can extend across the drawing. **Reset** clears the region, exclusions and manual corrections and detects again. Changing sources or restarting detection also clears an unfinished manual line pair.

Status reports **Strong match**, **Review suggested**, or an insufficient-evidence message. These describe detected support, not a calibrated accuracy percentage. Two selected directions may describe a facade plane; their connecting line is not automatically a recovered ground-plane horizon.

## Correct with two lines

When the image has weak or stylized perspective, choose **Two lines**. Drag two distinct straight edges that should share a vanishing direction. Their intersection creates a manual candidate. Parallel lines create a parallel ruler. Repeat for another direction if needed, then return to **Adjust** to move the point. Repeating the same line is rejected because it cannot locate a unique point. Escape clears an unfinished manual pair.

## Add and edit

Click **Add guides** to append the accepted geometry in one Undo operation. Source pixels and editable objects stay unchanged. The guides use the existing canvas handles, visibility, locking, density and snapping controls. Global snapping retains its previous setting; enable it in Guides when wanted.

Guides save with the drawing and remain independent after acceptance. Moving or deleting the source image later does not regenerate or move them automatically. They do not appear in exported artwork. Cancel adds no guides or history. Changing the document, source selection or frame invalidates the proposal so a stale worker cannot add guides to another drawing.

## Scope and limits

Detection uses straight-edge families and robust fitting. Architecture, rooms and streets are useful starting subjects. Organic shapes, lens distortion, texture, mixed perspective systems or intentionally distorted backgrounds may need a region, manual lines or ordinary guide editing. Three-point and fisheye guides remain available manually; automatic three-point/fisheye inference and linked regeneration recipes are not implemented.

File sources accept supported Qt image formats up to **32 MiB**, **16 megapixels** and **16,384 pixels per side**. Read-only previews use bounded copies up to **1600 pixels on the longer side** and approximately **2 megapixels**. The detector also bounds edge, segment and candidate counts, supports cancellation and uses existing compositor guards for heavy layer effects. It does not promise a fixed completion time for arbitrary production stacks.

Accepted geometry uses existing guide storage and limits: **64 guides per drawing**, **256 per project**, and finite coordinates within **±1,000,000 artwork pixels**. No new image resource or project format is needed for ordinary detected guides.

The implementation uses original C++ edge/Hough analysis and robust fitting with no added dependency. [OpenCV's Hough guide](https://docs.opencv.org/4.x/d9/db0/tutorial_hough_lines.html) documents the underlying straight-line detection approach. Synthetic fixtures and one architectural-photo sanity review support the current local implementation; artist production-image, physical-pen and additional-platform acceptance remain open.
