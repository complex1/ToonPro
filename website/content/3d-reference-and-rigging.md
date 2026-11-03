---
title: "3D drawing references and posing"
description: "Place models, build a rest skeleton, correct weights and pose a reference for hand drawing."
category: "Anime production"
order: 100
---

Toon Pro's 3D workflow supports drawing references: compose a scene, pose a model and draw on an artwork layer above it. The reference geometry, rig handles and weight colors are excluded from production-sheet and movie exports. This is useful for checking anatomy and perspective while keeping the final picture hand drawn.

3D viewport rendering is currently provided by the Apple silicon Mac build. Other builds can report that the renderer is unavailable. Check the capabilities of the download you are using before planning a workflow that depends on the 3D view.

## Add a model to the shot

Open **3D Library** and browse **Starter** or **My Assets**. Double-click a model or use **Add** to place it in the shot's shared reference scene. The library supports names, search, tags and favorites. Imported assets are copied into the local library; placing one also gives the project the model resources it needs.

Imports support a bounded static subset of **GLB/glTF 2.0**: triangle geometry, transformed nodes, base colors and supported textures. Unsupported skins, animations, morph targets, compressed features and alpha-blended materials are rejected explicitly. Blender, FBX, VRM and OBJ are not direct import formats. Export a supported static glTF asset from the source application when appropriate; importing an existing rig does not currently preserve its skinning.

Removing a library entry does not remove models already embedded in projects. Conversely, editing a placed model's rig does not modify its library source. Duplicate instances can be posed independently.

## Arrange objects and the reference camera

Choose **3D Object**, **3D Camera** or **3D Rig** in the 3D tool group. Their Properties expose Model, Camera and Rig controls rather than the drawing brush settings.

Object controls provide move, rotate and scale, exact axis values, reset, duplicate, delete, visibility and locks. The camera provides orbit, pan, dolly, fit all or selected, and perspective or orthographic projection. Use fixed views for construction and a perspective camera to judge the final pose. Camera lock protects an established composition.

Choose an ordinary raster or vector layer above the reference to draw. Brush and pixel tools do not paint on model geometry. If you still have the reference layer selected, the forbidden cursor and bottom message explain why a drawing action cannot proceed.

## Build a rest skeleton

Open **3D Rig → Rest** on the selected instance. Create a root skeleton or start from the 22-joint humanoid template. The template is an initial arrangement, not automatic detection of anatomical joints.

Add children or new roots, rename joints by double-clicking their names, and use the hierarchy-aware parent list. Delete removes the selected branch. Place joints with the canvas handles and XYZ controls. **Front**, **Side** and **Top** switch the saved camera to orthographic inspection views; camera lock still applies.

Rest positions are absolute within the model's local coordinates. Moving or reparenting a joint leaves its children where they were, so inspect the chain after changing parentage. Shift slows canvas movement and Escape cancels a pending drag.

Mirror duplicates an unpaired branch across the model's center plane and links corresponding joints. Mirror the parent first when an off-center unpaired parent would otherwise make the relationship ambiguous. The mirrored-edit toggle then keeps paired position edits coordinated.

## Bind and correct weights

In **3D Rig → Weights**, click the link icon to bind the mesh to its rest joints. The background operation generates initial distance-based weights. It is a starting point for correction, not a guarantee of production-quality deformation.

Choose a joint to view its influence: blue means none, progressing through cyan and yellow to red for full influence. The **Add**, **Subtract** and **Smooth** brush tabs affect front-visible vertices under the brush. Pressure multiplies strength. Hidden rear vertices are protected.

A vertex retains no more than four normalized influences. To reduce its only influence, first give it another one. Smooth averages influence among visible vertices under the brush; it does not follow a sophisticated anatomical or topology-aware rule. Sparse meshes limit the precision of a painted boundary.

Use **Bend check** to temporarily test a selected axis. Painting is disabled while bent; reset returns to rest. This inspection angle is not a saved pose. Binding locks rest structure until you unbind. The scissors action removes weights and the current pose to allow Rest editing again, with Undo available.

## Pose with FK and two-bone IK

Open **3D Rig → Pose** on a bound instance. **FK** rotates a selected joint and its descendants. Choose an axis for the canvas ring, or enter exact local angles. Root translation and **Body position** move the skeleton as a whole.

**IK** needs an endpoint with a parent and grandparent forming two valid bones. Create its control with the link icon or by dragging the endpoint. **Target** places the endpoint; **Pole** decides the bend direction. An unreachable request stays visible with a reach or limit message while the chain uses its reachable solution.

**Pin** keeps an IK target fixed in model-local coordinates as the body or parents move. Moving the whole object moves its pins too; this is not a world-space floor constraint. FK controls directly driven by pinned chains remain unavailable until unpinned.

Enable **Joint limits** and set per-axis minimum and maximum values when a joint should not rotate freely. Mirror swaps the current pose across paired joints. Reset selected and Reset all remove the relevant pose changes while retaining suitable rig structure and limits.

## Save and draw from the pose

The current pose, weights and skeleton are saved on the placed instance. Switching to Rest or Weights temporarily displays rest geometry; returning to Pose or drawing above the scene restores the pose. There is no named reusable pose library or general model/rig animation in this workflow. Reference camera animation is available separately in Frame Properties.

After posing, lock the model or camera as needed, select your drawing layer and begin the hand-drawn pass. Use [Light Table](cel-paint-and-light-table.md) for comparisons between finished drawings rather than expecting the 3D reference to appear in final exports.
