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
    QRectF bounds() const { return m_bounds; }
    QTransform matrix() const { return m_matrix; }
    QPolygonF quad() const;
    QVector<QPointF> controlPoints() const;
    QPainterPath selectionPath() const;
    const RasterLayerPreview *preview() const;
    bool setMatrix(const QTransform &matrix);
    bool setQuad(const QPolygonF &corners);
    bool setWarp(const QVector<QPointF> &nodes);
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
    QPointer<Document> m_document;
    QMetaObject::Connection m_documentConnection;
    Project m_before;
    RasterSelection m_selection;
    QVector<int> m_indices;
    RasterLayerPreview m_preview;
    QRectF m_bounds;
    QTransform m_matrix;
    QVector<QPointF> m_nodes;
    bool m_selectionOnly = false;
};
// Render only a preview's moving pixels. Caller handles layer compositing and
// stationary pixels. Mesh triangles are clipped before their affine mapping.
void paintAdvancedMovingLayer(QPainter &painter, const RasterLayerPreview &preview, bool smoothDisplay = false);
} // namespace toon
