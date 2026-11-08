#pragma once
#include "reference3d.h"

namespace toon {
struct ReferencePetPosePreset { QString id, name, kind; };
// Static study poses sampled from the actual bundled animal's authored rig.
// These are species-specific snapshots, not humanoid retargeting or playback.
QVector<ReferencePetPosePreset> referencePetPosePresets(const ReferenceInstance &instance);
// Replaces native node pose overrides atomically, retaining placement, asset,
// expressions and morph state. Callers own the Undo boundary.
bool applyReferencePetPosePreset(const QString &id, ReferenceInstance &instance, QString *error = nullptr);
} // namespace toon
