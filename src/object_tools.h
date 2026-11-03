#pragma once
#include "editable_object.h"
#include "raster.h"
#include "project.h"
#include <QPointer>
namespace toon {
class Document;
class ObjectTools {
public:
    explicit ObjectTools(Document *document);
    ~ObjectTools();
    bool beginEdit(const QString &id);
    bool beginLayerEdit(const QString &label = "Edit vector paths");
    bool previewObjects(const QVector<EditableObject> &objects);
    bool previewEdit(const EditableObject &object);
    void finishEdit();
    void cancelEdit();
    bool editActive() const { return m_before.has_value(); }
    static bool addLayer(Document *document, LayerKind kind);
    static QString addPath(Document *document, const QVector<BezierNode> &nodes, bool closed,
                           QColor stroke, qreal strokeWidth, QColor fill = {});
    static QString addText(Document *document, EditableObject text);
    static bool updateObject(Document *document, const EditableObject &object,
                             const QString &label = "Edit object");
    static bool removeObject(Document *document, const QString &id);
    static bool simplify(Document *document, qreal tolerance, const QString &id = {});
    // Segment erasing splits open/closed paths at the intersected cubic segment.
    static bool eraseAt(Document *document, QPointF point, qreal radius, bool segments = false);
    static bool erasePath(Document *document, const QPainterPath &sweep, qreal radius, bool segments = false);
    static QString hitTest(const RasterLayer &layer, QPointF point, qreal tolerance = 5);
    static const EditableObject *object(const RasterLayer &layer, const QString &id);
    static bool rasterize(Document *document);
    static bool rasterizeLayer(Document *document) { return rasterize(document); }
private:
    static QString addObject(Document *document, EditableObject object);
    QPointer<Document> m_document;
    std::optional<Project> m_before;
    QString m_editId, m_layerId, m_label = "Edit object";
    bool m_dirty = false;
    bool m_positionWarningShown = false;
    bool m_animationWarningShown = false;
};
} // namespace toon
