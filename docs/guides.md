# Perspective guides — 0.13.0

The **Perspective guides** tool provides multiple drawing assistants in the same shot. Its Tool Properties follows Krita's assistant workflow: choose a type, place handles, select an assistant, then change its display and drawing assistance. Six native guide types are available, including three-point perspective and fisheye grids added in 0.10.0. This is not complete Krita parity.

## Add and arrange guides

1. Press **G** while the canvas has focus, or choose **Toolbox → Guides** and an individual guide in **Tools**. Tool Properties switches to guide controls.
2. Choose a guide type in Tools, then click **Add** in Tool Properties.
3. Click the canvas to place the required points. The placement indicator shows progress. Escape or a right-click cancels placement.
4. Repeat to add another guide. Different guide types can coexist.

| Type | Points to place | Drawing assistance |
| --- | --- | --- |
| Perspective | Four corners in order around a convex plane | Two perspective directions through the stroke start inside that plane. |
| Vanishing point | One vanishing point | A line through the stroke start and vanishing point. |
| Two-point perspective | Two vanishing points, then a grid anchor away from their horizon | A choice of direction toward either vanishing point, plus an optional vertical ruler perpendicular to the horizon. |
| Parallel ruler | Two distinct points defining a direction | A parallel line through the stroke start. |
| Three-point perspective | Three noncollinear vanishing points, then a grid anchor away from the lines connecting them | A choice of direction toward any of the three vanishing points, useful for looking up or down at tall forms. |
| Fisheye perspective | Two opposite endpoints of an ellipse axis, then a handle establishing its perpendicular radius | A choice between two ellipse-family paths through the stroke start inside the outer ellipse, with straight paths on its center axes. |

The two-point guide's third handle is Toon Pro's grid anchor. The three-point guide uses a fourth handle as its grid anchor; the displayed planes share that anchor and converge toward its three vanishing points. These are not reproductions of Krita's center-of-vision control. Grids provide visual reference; this feature does not deform artwork or simulate a 3D scene.

For fisheye, the first two handles establish the center, axis direction and half-width. The third handle's perpendicular distance from that axis sets the half-height; moving it along the axis does not change the grid. Use equal radii for a circular boundary or unequal radii for an ellipse. The grid combines horizontal and vertical ellipse families and straight center axes inside that boundary, with adjustable density. Snapping follows a path through the exact stroke start; it is not quantized to the displayed grid lines. This native bounded model does not reproduce all of Krita's fisheye behavior beyond the outer ellipse.

Click a guide line, its handles, or its row in Tool Properties to select it. Drag a point to reshape it. Drag its cross-shaped center handle to translate the whole guide; a vanishing point's move handle sits just beside its point. Hold **Shift** during a handle drag to constrain movement to the artwork's horizontal or vertical axis. A completed drag creates one undo edit. Escape discards an unfinished drag.

For fisheye guides, the height handle moves perpendicular to the main axis. Hold **Shift** while placing or dragging that handle for a circular boundary. Moving either axis endpoint resizes/rotates the grid while preserving its perpendicular radius. Three-point guides place their move handle just beside the grid anchor so it remains accessible even when vanishing points are far away.

The guide list has direct **visibility**, **handle lock**, and **snapping** icons. Lock prevents moving handles; it does not disable drawing assistance or removal. Duplicate and Delete act on the selected guide, and Delete/Backspace works while the canvas has focus in the guide tool. Duplicates receive new identifiers and start slightly offset with unlocked handles.

## Tool Properties

The **Selected guide** tab contains its name, density, optional two-point vertical ruler, limited area, and custom color/opacity. The **All guides** tab contains shared visibility, snapping, handle display, color and opacity. Icons use the application's 600 ms tooltip delay and accessible names. In 0.37, density, area bounds and opacity use responsive label/slider/value rows; guide flags sit beside its name, and all six guide-type icons remain directly available in a 220-pixel panel. Canvas handles remain available for positioning guide geometry; names and Hex colors remain text entry.

Guides use the shared color and opacity until **Custom color** is enabled for that guide. Its custom color and opacity then replace those shared values. Color chips open a picker, and Hex fields allow precise color entry.

Enable **Limit to area** to show a rectangular region for the selected guide. Drag its two opposite handles or enter X/Y/width/height in Tool Properties. The rectangle clips the displayed grid and determines whether a stroke starting there is eligible for that guide. Snapping applies while both the input point and its projected point lie inside the rectangle. Outside it, the stroke continues freehand; returning inside resumes the chosen direction. This is not a paint mask. Perspective planes also require the stroke to start inside the plane itself; without an explicit area limit, their chosen directions can extend past the plane.

## Draw with assistance

Enable **Snap to guides** in the All guides tab of Tool Properties, then return to **Brush (B)** or **Eraser (E)**. Each guide also has its own snapping switch. New projects start with global snapping off.

At stroke start, the enabled guides supply candidate directions through that point. After a short initial movement, the closest candidate is chosen and stays fixed for the rest of that stroke. Three-point guides contribute three straight directions. Fisheye guides contribute two ellipse-family paths: the selected family stays fixed while the stroke follows its curve, or a straight center axis when appropriate. A new stroke chooses again. The dashed path preview shows the active constraint. Stroke stabilization, pressure and native brush effects remain available; the guide constrains the stroke path, not every pixel of a scattered or textured stamp.

Visibility and snapping are independent. Hiding one guide or all guide lines does not disable snapping. Turn off the guide's snapping icon or the shared snapping toggle when freehand drawing is wanted. Handle display applies while editing with the guide tool.

## Save and reuse

Guides and shared settings are shot data. Edits support undo/redo and are saved in the project and recovery snapshots. The current workspace edits the first shot; the file format preserves guides in other shots too. Guide lines, handles and snapping previews stay out of production-sheet and movie-frame PNGs.

**Save guide set** writes all current guides and shared settings to a native `.toonguides` file. **Add guide set** appends the imported guides with fresh identifiers and adopts that set's shared settings. Existing artwork is unchanged. Toon Pro does not read or write Krita `.paintingassistant` files.

Guide data was introduced in project format 5. The current project container is version 7 and reads versions 1–6; versions 1–4 acquire empty guide lists. Files upgrade only when saved. Guide sets keep their own version 1 JSON schema and atomic replacement. Sets containing three-point or fisheye types require a 0.10 or newer reader; older readers reject the unknown type safely. Invalid or oversized files are rejected before replacing the loaded guide data.

Limits: 64 guides per shot, 256 per project, 100-character guide names, 1–64 subdivisions, finite points/area bounds within ±1,000,000 artwork pixels, and 1 MiB per reusable guide-set file. Perspective corners must form a valid convex plane; parallel-ruler points must differ; two-point anchors must stay off the horizon. Three-point vanishing points must be noncollinear, with the anchor off every line connecting a pair of vanishing points. Fisheye axis endpoints must differ and its perpendicular radius must be nonzero.

Standalone ellipse and spline assistants, other curvilinear models, perspective brush-size sensors, connected grid extensions and Krita-file compatibility are not implemented in this version.

## References

- [Krita: Assistant Tool](https://docs.krita.org/en/reference_manual/tools/assistant.html) — assistant creation, saved sets, shared/custom display controls and limited areas.
- [Krita: Painting with Assistants](https://docs.krita.org/en/user_manual/painting_with_assistants.html) — guide types and assisted freehand drawing. Toon Pro's supported subset and behavior are specified above.
