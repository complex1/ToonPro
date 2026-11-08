# Fill using reference layers

The local **0.76** development build supports persistent fill-reference marks on layers and folders. Keep ink and color separate: references supply boundaries, while the active editable raster layer receives paint. Published 0.75 packages are unchanged.

## Choose the boundary source

Select **Fill**, then use the three source tabs in **Tool Properties**. Hover over an icon for its name; the **Boundaries** caption also shows the selected mode.

| Mode | Boundaries come from |
| --- | --- |
| **Whole drawing** | Visible composited layers in the current drawing. |
| **Current layer** | The active layer's rendered artwork. |
| **Reference layers** | Marked reference layers/folders, or one explicitly chosen layer. |

The source choice does not change the paint target. Other documents, project assets and inactive cels are excluded. Magic Wand and Color Range share these source choices for selection.

## Mark ink and fill below it

1. Select your **Ink** layer in Layers.
2. Click the **lighthouse beside the blend-mode control**. Its tooltip is **Mark selected layers as fill references**. Alternatively, right-click and choose **Mark as fill reference**. A lighthouse badge identifies marked rows.
3. Select an editable raster **Colors** layer below Ink.
4. Select Fill and choose **Reference layers**. The Source picker should show **Marked reference layers**.
5. Choose a color and click inside a closed ink shape. Adjust **Tolerance** or **Close gaps** when needed.

Select several rows to mark split ink layers together. A marked folder supplies its children's combined image, sampled once. Click the lighthouse again when all selected rows are marked to unmark them. Each batch mark/unmark and completed fill is one Undo operation.

## Hidden, locked and special layers

Any layer row can carry a reference mark. Marking preserves its source, locks and export behavior. Locked Ink can supply boundaries while unlocked Colors receives paint.

Marked roots supply boundaries even when hidden or faded, retaining their masks, local effects and placement. Inside a marked folder, child visibility, opacity and compositing apply. The summary reports marked and hidden sources. Root clipping and blend mode are isolated; unrelated siblings are excluded.

Renderable sources supply their current image representation. An adjustment layer alone has no independent image. Missing, unexposed or empty references produce feedback and leave paint and selection unchanged. The tool never switches automatically to Whole drawing.

The target must be a suitable visible, unlocked raster layer. Marking does not make it paintable. If a reference is also your active target, Fill edits it; select a separate Colors layer to preserve ink.

## Explicit references, quality and limits

The Source picker can still select one explicit layer or folder. Saved tools using this mode retain it. The adjacent **Use marked reference layers** lighthouse returns to the marked set. A missing explicit source requires a new choice; the tool does not select an unrelated layer.

Tolerance ranges from **0–255** and Close gaps from **0–20 px**. Existing antialiased-edge handling, selections and alpha locks apply. Sampling has a **32-million-pixel** working limit, with additional compositor guards for heavy effects. An empty selection crop may need a bounded whole-source check before filling; exceeding that combined budget reports an error.

## Save and reuse

Marks persist with the document and survive Undo/Redo, duplication, and save/reopen. Merge Down and Flatten retain a mark when consumed content was marked. Marked documents use **project format 43**; unmarked documents retain their existing format choice. Earlier builds cannot open format 43.

Tools using marked-reference mode select **tool catalog 13**. Auto Actions using marked fills or reference-mark properties select **action format 3**; older tools/actions keep their compatible formats. A recorded marked fill uses the replay drawing's current marks. Existing explicit-reference actions retain their relative-layer behavior.

This interaction was informed by [Clip Studio Paint's reference-layer guide](https://help.clip-studio.com/en-us/manual_en/180_layers/Reference_layers.htm). Toon Pro's implemented behavior is specified above; complete feature equivalence is not claimed.
