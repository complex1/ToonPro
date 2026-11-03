# History and Auto Actions

Available in **0.50.1** from **Panels → History** and **Panels → Auto Actions**. Both are ordinary resizable docks, initially hidden beside the Layers tab group. They can float, remember their workspace placement, and hide/restore with Tab drawing mode.

## History

Click a state to return to it. Use the panel's Undo/Redo icons or the list's keyboard navigation to move through edits. A marker and bold text identify the current state; later redo states are dimmed. The clean/saved state is labeled when it is still retained. Making a new edit after going back removes the old redo branch.

This is the current document's artwork history, not the separate brush/tool organization Undo stack. History is session-only and resets when a document is loaded. The existing limits remain: up to 50 commands, with older history cleared when its memory budget is exceeded. The baseline is labeled **Oldest retained state** when earlier edits are no longer reachable. This panel does not add snapshots, a history brush or persistent version history.

Live strokes cannot be interrupted by history clicks. Pending transforms retain their Apply/Discard/Cancel decision. Other active editors must finish before history navigation. If resolving an edit removes the requested state from retained history, the app explains that instead of jumping to a different state.

## Auto Actions

1. Click **+** and name an action, such as “Ink setup”.
2. Click **Record**, then perform supported layer, fill or transform operations in the normal workspace.
3. Click **Stop**. Recording again appends steps to the same action.
4. Select the target layer in this or another project and click **Play**.

For example: record adding a drawing layer, renaming it “Line art”, setting its opacity and choosing a blend mode. Replay creates that setup relative to the currently active layer. Recorded values appear in step labels and tooltips. Uncheck a step to skip it; right-click to move it up/down or remove it. The action options menu offers Rename, Duplicate and Delete.

### Recording scope

| Supported operation | Replay behavior |
| --- | --- |
| Add drawing or notes layer | Adds a layer using the ordinary insertion rules. |
| New folder around the active layer | Groups the active layer, matching the existing New Folder action. |
| Duplicate/delete one drawing layer | Acts on the active non-folder layer; unavailable targets fail validation. |
| Select another layer | Stores the relative offset through the layer stack, not a source document ID. |
| Rename, opacity, visibility, lock, annotation status | Changes only the fields altered during recording. |
| Blend mode, alpha lock, position lock, clipping | Applies the recorded value through normal layer rules. |
| Layer effects | Applies the recorded validated effects configuration. |
| Fill selection | Uses the recorded color on the target selection, including its feathering; without a selection, fills the full drawing area. |
| Paint bucket | Uses the recorded color, click coordinates, tolerance, gap closing and sampling source on the target artwork. Reference-layer sampling uses a relative layer offset. |
| Raster move, resize and rotate | Records the final transform on Apply/Done. Reuses pixel movement, scale factors and rotation around the target layer or selection bounds. |

Brush strokes/settings, gradients, masks, selection creation/editing, perspective/warp, vector or group/multi-layer transforms, arbitrary multi-layer changes, file dialogs, exports, animation and 3D editing are not recorded. Unsupported committed artwork operations stop recording with an explanation; earlier recorded steps remain available. Undo/Redo, loading another document, or changing the shot/frame also stop recording. Tool choices and other non-artwork UI settings are not action steps.

To record a fill-and-place workflow, make a selection before pressing Record, fill it, use the ordinary raster Transform tool and press Apply/Done. You can continue with another fill or supported operation. Transform previews and Cancel do not add steps. Before playback, choose the target drawing layer and make any desired selection. Fill uses that selection; a recorded bucket click keeps its document coordinates and must fall within the target drawing area. Reference sampling requires the corresponding relative layer position to exist.

Before applying an action, the engine evaluates its enabled steps on an isolated copy of the target document. If a step fails—for example, a relative layer selection is out of range—the target artwork, selection and history stay unchanged, and the failed step is identified. A successful workflow commits as **one Undo entry**. Workflows that only select another layer do not add an empty artwork edit. Transformed pixel selections are restored alongside pixels by Undo/Redo, including changes that leave pixels visually identical. Reordering or disabling steps can change which layer later steps target; inspect their order before playing.

Actions are stored in `auto-actions.json` alongside `workspace.ini`, independently of project files. Completed steps and management edits save automatically. Libraries containing fill, bucket or transform steps use format version 2; existing version-1 layer actions remain readable. No project format change is required. The library supports 128 actions, 256 steps per action and 4 MB of JSON; invalid or unsupported library files are not overwritten. No `.atn`/`.laf` import, action sets, shortcut assignment, parameter-editing dialogs or cross-device synchronization are implemented. Deleting an action or step changes the action library and is separate from artwork Undo.

## Reference behavior

The navigation model follows [Clip Studio Paint's History palette](https://help.clip-studio.com/en-us/manual_en/270_canvas/Undo__47_Redo.htm). Record/Stop/Play, appending steps and optional step playback are informed by [Clip Studio Paint Auto Actions](https://help.clip-studio.com/en-us/manual_en/720_preferences/Auto_Actions.htm). Whole-action Undo follows the behavior described in [Adobe's Actions panel documentation](https://helpx.adobe.com/photoshop/desktop/automate-tasks/automation-settings-and-presets/use-the-actions-panel.html). These are interface references, not a claim of complete command or file compatibility. References checked 31 October 2026.
