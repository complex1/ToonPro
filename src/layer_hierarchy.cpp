#include "raster.h"
#include <QSet>

namespace toon {
namespace {
int findLayer(const QVector<RasterLayer> &layers, const QString &id) {
    for (int i = 0; i < layers.size(); ++i) if (layers[i].id == id) return i;
    return -1;
}
}
bool layerIsDescendant(const QVector<RasterLayer> &layers, int index, const QString &ancestorId) {
    if (index < 0 || index >= layers.size() || ancestorId.isEmpty()) return false;
    QSet<QString> visited;
    QString parent = layers[index].parentId;
    while (!parent.isEmpty() && !visited.contains(parent)) {
        if (parent == ancestorId) return true;
        visited.insert(parent);
        const int at = findLayer(layers, parent);
        if (at < 0) return false;
        parent = layers[at].parentId;
    }
    return false;
}
bool layerCanPaint(const QVector<RasterLayer> &layers, int index) {
    return index >= 0 && index < layers.size() && layers[index].kind == LayerKind::Raster && !layers[index].adjustment && !layers[index].puppetWarp && layerCanEdit(layers, index);
}
bool layerCanEdit(const QVector<RasterLayer> &layers, int index) {
    if (index < 0 || index >= layers.size() || layers[index].group) return false;
    QSet<QString> visited;
    while (index >= 0) {
        const auto &layer = layers[index];
        if (!layer.visible || layer.locked || visited.contains(layer.id)) return false;
        visited.insert(layer.id);
        if (layer.parentId.isEmpty()) return true;
        index = findLayer(layers, layer.parentId);
    }
    return false;
}
bool layerIsAnnotation(const QVector<RasterLayer> &layers, int index) {
    QSet<QString> visited;
    while (index >= 0 && index < layers.size()) {
        const auto &layer = layers[index];
        if (layer.annotation) return true;
        if (layer.parentId.isEmpty() || visited.contains(layer.id)) return false;
        visited.insert(layer.id);
        index = findLayer(layers, layer.parentId);
    }
    return false;
}
bool layerPositionLocked(const QVector<RasterLayer> &layers, int index) {
    QSet<QString> visited;
    while (index >= 0 && index < layers.size()) {
        const auto &layer = layers[index];
        if (layer.locked || layer.positionLocked || visited.contains(layer.id)) return true;
        if (layer.parentId.isEmpty()) return false;
        visited.insert(layer.id);
        index = findLayer(layers, layer.parentId);
    }
    return true; // Invalid ancestry must not permit destructive movement.
}
} // namespace toon
