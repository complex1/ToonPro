# 06 · New drawing setup

Version 1 · 5 November 2026 · Design only, not implemented.

## Purpose and variants

New drawing opens a focused setup dialog over its project dashboard. Both contexts use the same Name, Canvas size, Background, Preview, Cancel and Create drawing controls. Drawing projects omit production organization. Movie projects additionally show the current folder and optional predefined/custom tags.

| Image | Context |
| --- | --- |
| [drawing-project-v1.png](drawing-project-v1.png) | Character Studies Drawing project; no tags or folder fields |
| [movie-project-v1.png](movie-project-v1.png) | Moonlit Journey / Opening / Station; predefined and custom tags |

## Proposed controls

- Name: required, editable, initially a suggested unused name; sample mockups use Expression Study and Station Background. Creation must not overwrite an existing drawing when names collide.
- Canvas size: direct Landscape, Square, Portrait and Custom presets; editable width/height in pixels, each using the shared slider plus compact exact-entry pattern. This draft shows Landscape at 1920 × 1080. Choosing dimensions that differ from a preset selects Custom. Aspect lock preserves the current ratio; Swap orientation exchanges width and height. Preset dimensions beyond the depicted example remain recommendations to specify during implementation.
- Background: White, Transparent or Color. White is selected in both images. Transparent previews a checkerboard that never becomes painted/exported pixels. Color opens a focused color picker and shows the chosen swatch; its color should update the preview live. These settings describe artwork background, independently of dark application chrome.
- Preview: a blank proportional canvas with dimensions; responds to size/background changes without creating content or changing project history.
- Destination in Movie: read-only current folder breadcrumb. Creation uses that selected folder; choose a different folder in the dashboard before opening setup. No second mandatory location step.
- Movie tags: optional multiple selections from Storyboard, Animatics, Background, Asset and Character sheet, plus custom tags. The example checks Background and adds Night. Tag purpose is independent of document type: Animatics on an image does not turn it into an animation.
- Custom tags: type a label and use Add or Enter; deduplicate labels consistently and show each added label once with a Remove action. Enter in the tag input adds the tag without triggering Create. Escape in the input cancels its draft; Escape in the dialog cancels creation. Tags remain draft state until Create succeeds. On Create, normalize any nonempty unfinished tag entry into the draft tag set and validate it so typed tags are not silently lost.
- Create drawing: validate name and supported canvas/resource limits, create exactly one blank drawing in this project/current folder with its selected background and tags, then open its drawing editor. On success, the dashboard gains the drawing. No animation track, FPS, scene/clip controls or production header is added by default.
- Cancel / close: return to the same project, folder, search and scroll state; create no item, file or tag-library mutation. Validation errors stay inline and retain all entered values. A failed create must not publish a partial drawing.

## Implementation considerations

These are proposed interactions, not proven runtime features. Per-drawing canvas dimensions/background must be supported by the future document model; the existing shared project dimensions cannot be silently changed when adding one drawing. Concrete dimension/resource/tag limits and save behavior will be specified during implementation. An image cannot verify gestures, focus or previews.

Use accessible names and selected states, keyboard navigation, shared 600 ms hover help and visible focus. Numeric values retain exact entry. Preview checkerboards must remain distinguishable from artwork. Color selection and transparent/invalid states will be additional states if the artist requests them.

Generated with built-in image_gen. Complete prompts: [Drawing-project variant](prompt-drawing-project-v1.txt), [Movie-project variant](prompt-movie-project-v1.txt). Reference images are the existing Drawing/Movie dashboard mockups; all names/artwork are fictitious placeholders.
