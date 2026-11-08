#pragma once

#include "reference3d.h"

namespace toon {
struct ReferencePosePreset {
    QString id, name, category;
    bool handsOnly = false;
};
enum class ReferenceHandTarget { Both, Left, Right };
// Original authored pose recipes. These are independent of any third-party
// material library and contain no imported animation or CSP asset data.
QVector<ReferencePosePreset> referencePosePresets();
// Full-body application replaces supported body/hand overrides while retaining
// placement, shape and expression state. Hand application changes only selected
// hands. Both means every available hand; Left/Right require that mapped side.
// Mirror swaps left/right body instructions (or a selected single hand).
// Failure leaves the complete instance unchanged; callers own Undo boundaries.
bool applyReferencePosePreset(const QString &id, ReferenceInstance &instance, QString *error = nullptr,
                              ReferenceHandTarget hands = ReferenceHandTarget::Both, bool mirror = false);
struct ReferenceHandControl {
    QString id, name;
    float curl = 0, splay = 0;
    // Arbitrary FK and mixed left/right gestures need not fit this two-value
    // model. Values are projected from actual joint quaternions, not UI state.
    bool approximate = false;
};
QVector<ReferenceHandControl> referenceHandControls(const ReferenceInstance &instance,
    ReferenceHandTarget hands = ReferenceHandTarget::Both, QString *error = nullptr);
// Keys: thumb/index/middle/ring/little; vector x=curl [0,1], y=splay [-1,1].
// Only supplied fingers are replaced, atomically. Wrist, body, other fingers,
// placement, morphs and expressions retain their exact existing state.
bool setReferenceHandControls(ReferenceInstance &instance, const QHash<QString,QVector2D> &values,
    ReferenceHandTarget hands = ReferenceHandTarget::Both, QString *error = nullptr);
} // namespace toon
