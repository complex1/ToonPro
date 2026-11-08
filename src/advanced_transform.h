#pragma once
#include "document.h"
#include <QPointer>
#include <QPolygonF>

namespace toon {
// A reversible, viewport-rendered transform gesture shared by raster layers,
// editable objects, folders and selection boundaries.
class AdvancedTransform : public QObject {
    Q_OBJECT
public:
    explicit AdvancedTransform(QObject *parent = nullptr);
    ~AdvancedTransform() override;
    bool begin(Document *document, const QStringList &layerIds = {}, bool selectionOnly = false);
    bool active() const { return !m_document.isNull(); }
    bool selectionOnly() const { return m_selectionOnly; }
    bool warped() const { return !m_nodes.isEmpty(); }
    bool modified() const;
    bool puppet() const { return m_puppet; }
    const PuppetMesh &puppetMesh() const { return m_puppetMesh; }
    const QVector<PuppetPin> &pins() const { return m_pins; }
    int puppetDensity() const { return m_puppetDensity; }
    QRectF bounds() const { return m_bounds; }
    QTransform matrix() const { return m_matrix; }
    QPolygonF quad() const;
    QVector<QPointF> controlPoints() const;
    QPainterPath selectionPath() const;
    const RasterLayerPreview *preview() const;
    bool setMatrix(const QTransform &matrix);
    bool setQuad(const QPolygonF &corners);
    bool setWarp(const QVector<QPointF> &nodes);
    bool beginPuppet(int density=16);
    bool setPuppetDensity(int density);
    int addPuppetPin(QPointF position);
    bool setPuppetTargets(const QVector<QPointF> &targets);
    bool removePuppetPin(int index);
    bool removePuppetPins(const QVector<int> &indices);
    bool resetPuppet();
    bool flip(bool horizontal);
    bool skew(qreal horizontal, qreal vertical);
    bool apply();
    void cancel();
signals:
    void changed();
private:
    bool fail(const QString &message) const;
    bool validatePlacement(const QTransform &matrix, const QVector<QPointF> &nodes) const;
    void refreshPreview();
    bool updatePuppet(const QVector<PuppetPin> &pins,int density);
    QPointer<Document> m_document;
    QMetaObject::Connection m_documentConnection;
    Project m_before;
    RasterSelection m_selection;
    QVector<int> m_indices;
    RasterLayerPreview m_preview;
    QRectF m_bounds;
    QTransform m_matrix;
    QVector<QPointF> m_nodes;
    PuppetMesh m_puppetMesh;
    QVector<PuppetPin> m_pins;
    int m_puppetDensity=16;
    bool m_puppet=false;
    bool m_reopenedPuppet=false;
    QVector<PuppetPin> m_initialPuppetPins;
    int m_initialPuppetDensity=16;
    RasterLayerPreview m_puppetPreviewSource;
    bool m_selectionOnly = false;
};
// Render only a preview's moving pixels. Caller handles layer compositing and
// stationary pixels. Mesh triangles are clipped before their affine mapping.
void paintAdvancedMovingLayer(QPainter &painter, const RasterLayerPreview &preview, bool smoothDisplay = false);
} // namespace toon
