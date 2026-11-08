#include "document.h"
#include <QSet>

namespace toon {
bool Document::setFillReferences(const QStringList &ids, bool reference) {
    if (!m_open || editingGestureActive() || layerPropertyEditActive() || ids.isEmpty()) return false;
    const QSet<QString> requested(ids.cbegin(), ids.cend());
    auto after = m_project;
    auto &layers = after.shots[activeShotIndex()].layers;
    QSet<QString> found;
    bool changed = false;
    // A reference is read-only sampling metadata. Frozen ink and folders may
    // be marked without unlocking or changing their visibility/content.
    for (auto &layer : layers) if (requested.contains(layer.id)) {
        found.insert(layer.id);
        if (layer.fillReference != reference) { layer.fillReference = reference; changed = true; }
    }
    if (found != requested) { emit warning("A reference layer is no longer available."); return false; }
    return !changed || commit(m_project, after, reference ? "Mark fill reference" : "Unmark fill reference");
}
}
