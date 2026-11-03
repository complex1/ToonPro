#include "timeline_solo.h"
#include <QHash>

namespace toon {
QSet<QString> timelineSoloKeptLayerIds(const QVector<RasterLayer> &layers,
                                     const QSet<QString> &roots) {
    QHash<QString,int> indices;
    QHash<QString,QVector<int>> children;
    QHash<QString,int> lastBase;
    QVector<int> clippingBase(layers.size(),-1), siblingPosition(layers.size());
    for (int i=0;i<layers.size();++i) {
        const auto &layer=layers[i]; indices.insert(layer.id,i);
        auto &siblings=children[layer.parentId]; siblingPosition[i]=siblings.size(); siblings.append(i);
        if (layer.clipped) clippingBase[i]=lastBase.value(layer.parentId,-1);
        else lastBase.insert(layer.parentId,i);
    }
    QSet<int> kept, expanded;
    QVector<int> pending;
    const auto keep=[&](int index) {
        if (index>=0 && !kept.contains(index)) { kept.insert(index); pending.append(index); }
    };
    // Explicitly required artwork folders include their entire subtree. Merely
    // retaining an ancestor must not accidentally bring all its siblings back.
    const auto subtree=[&](int root) {
        QVector<int> queue{root};
        for (int at=0;at<queue.size();++at) {
            const int index=queue[at];
            if (index<0 || expanded.contains(index)) continue;
            expanded.insert(index); keep(index);
            queue+=children.value(layers[index].id);
        }
    };
    for (const auto &id:roots) if (indices.contains(id)) subtree(indices.value(id));
    if (roots.isEmpty() || kept.isEmpty()) {
        QSet<QString> all; for (const auto &layer:layers) all.insert(layer.id); return all;
    }
    for (int at=0;at<pending.size();++at) {
        const int index=pending[at]; const auto &layer=layers[index];
        if (!layer.parentId.isEmpty()) keep(indices.value(layer.parentId,-1));
        if (layer.clipped) subtree(clippingBase[index]);
        // An adjustment has no visible content without its lower backdrop.
        if (layer.adjustment && !layer.clipped) {
            const auto siblings=children.value(layer.parentId);
            for (int i=0;i<siblingPosition[index];++i) subtree(siblings[i]);
        }
    }
    QSet<QString> result;
    for (int index:kept) result.insert(layers[index].id);
    return result;
}
QVector<RasterLayer> timelineSoloPreviewLayers(const QVector<RasterLayer> &layers,
                                             const QSet<QString> &kept) {
    auto result=layers;
    for (int i=0;i<layers.size();++i)
        if (layers[i].visible && !kept.contains(layers[i].id)) result[i].visible=false;
    return result;
}
}
