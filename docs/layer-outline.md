# Layer outline

The local **0.76** build can add a live border around a layer's artwork. It follows the visible alpha shape, including transparent holes, while preserving the original pixels and editable objects. Published 0.75 packages do not include it.

1. Select an artwork layer or folder in **Layers**.
2. Open **Panels → Effects** and select the **Outline** tab. Hover over a tab icon to see its name.
3. Enable **Outline** with the eye switch beside its heading.
4. Drag **Width**, or enter an exact value, and click the **Color** chip to choose the border color.

Width ranges from **0–128 source pixels**, with a **3 px white** default. Zero width hides the border. The layer's transforms and canvas zoom scale its displayed width. Color is opaque RGB; the layer's own opacity affects the finished composition.

The border updates as the artwork changes. Disable Outline to hide it while retaining its settings, or use the Effects header switch to bypass all enabled color, lighting and outline effects. Each completed width gesture or color choice is one Undo action. Cancelling the color dialog restores the previous value. Save and reopen retain the settings, including custom values while disabled.

## What gets outlined

- **Raster, vector and text artwork:** the effect follows their rendered alpha silhouette. Vector/text source objects remain editable.
- **Folders:** the effect follows the combined visible children, so touching opaque shapes share an outer contour.
- **Masks:** hidden areas are removed before the outline is calculated.
- **Shadow and Glow:** these are calculated after Outline, so they include the border.

An opaque photograph has a rectangular alpha silhouette and receives a rectangular border. Remove its background first to outline a subject. Use [line and tone conversion](line-and-tone-conversion.md) to extract internal lines from a photograph. A border outside the drawing's output frame is cropped by that frame.

The processing order is **mask → color → blur → outline → shadow/glow → opacity, blend and clipping**. Canvas, Navigator and exported frames use the shared compositor. Existing background previews provide reduced drafts where supported, followed by full-quality refinement. Adjustment, Notes and direct 3D-reference layers do not expose Outline.

## Scope and storage

This is a smooth outer border with static width and color. Watercolor borders, inside/center alignment, a separate Reflect opacity option and outline-specific animation channels are outside the implemented scope. Uniform translucent artwork retains its interior opacity; the border uses source coverage, with a small neighborhood to keep antialiased fringes from weakening its edge.

Nondefault outline settings select **project format 42**, including when the effect is disabled. Earlier builds cannot open that format. Projects without outline settings retain their existing format selection. No extra source image or effect resource is stored. Source artwork, masks and retained Puppet Warp data keep their existing ownership.

The CPU renderer uses a linear-time distance transform independent of the requested width. Outline scratch/output allocations are capped at **128 MiB**, alongside the existing compositor's region and working-memory limits. Cancellation never publishes a partial outline. Production-image responsiveness and additional platform packaging still need artist acceptance.

The workflow was informed by Clip Studio Paint's [official Border effect guide](https://support.clip-studio.com/en-us/faq/articles/20200008). This implementation covers the controls described above; it does not claim complete feature equivalence. The algorithm reference is Felzenszwalb and Huttenlocher's [Distance Transforms of Sampled Functions](https://cs.brown.edu/~pff/papers/dt-final.pdf). Toon Pro's implementation is original C++ and adds no external dependency.
