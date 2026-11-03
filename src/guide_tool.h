#pragma once
#include "guide_geometry.h"
#include <QObject>
#include <QLineF>
#include <QTransform>
#include <QImage>
class QPainter;
namespace toon {
class Canvas;
class GuideTool : public QObject {
public:
    explicit GuideTool(Canvas *canvas);
    GuideType creationType = GuideType::Perspective;
    bool creating = false;
    QString selectedId;
    QVector<DrawingGuide> guides() const;
    GuideSettings settings() const;
    void synchronize();
    void beginCreation(GuideType type);
    void cancel();
    void cancelDrag();
    void select(const QString &id);
    void addDefault(GuideType type);
    void updateGuide(const DrawingGuide &guide);
    void removeSelected();
    void duplicateSelected();
    void setSettings(const GuideSettings &settings);
    void replaceSet(QVector<DrawingGuide> guides, const GuideSettings &settings, bool append);
    void paint(QPainter &painter) const;
    QString toolTipAt(QPointF position) const;
    void press(QPointF position, Qt::KeyboardModifiers modifiers);
    void move(QPointF position, Qt::KeyboardModifiers modifiers);
    void release(QPointF position, Qt::KeyboardModifiers modifiers);
    bool dragging() const { return m_dragHandle != -1; }
    quint64 cacheRenderCount() const { return m_cacheRenderCount; }
    void beginSnap(QPointF artworkPoint);
    std::optional<QPointF> snap(QPointF artworkPoint);
    QVector<QPointF> strokePoints(QPointF filteredPoint);
    void endSnap();
private:
    void commit(const QVector<DrawingGuide> &guides, const GuideSettings &settings, const QString &label);
    QTransform transform() const;
    QRectF visibleBounds() const;
    QPointF moveHandle(const DrawingGuide &guide) const;
    int handleAt(const DrawingGuide &guide, QPointF position) const;
    void appendGuide(DrawingGuide guide);
    Canvas *m_canvas;
    QVector<DrawingGuide> m_cached;
    GuideSettings m_settings;
    QVector<QPointF> m_pending;
    QPointF m_hover;
    Qt::KeyboardModifiers m_hoverModifiers = Qt::NoModifier;
    std::optional<DrawingGuide> m_preview;
    DrawingGuide m_before;
    QPointF m_dragStart;
    int m_dragHandle = -1; // -2 whole guide, >=0 point, count/count+1 area corners
    QPointF m_snapStart;
    struct SnapCandidate { GuideConstraint constraint; std::optional<QRectF> area; };
    QVector<SnapCandidate> m_snapCandidates;
    std::optional<SnapCandidate> m_snapLine;
    QPointF m_lastStrokePoint;
    bool m_snapActive = false;
    struct LineCache {
        QImage image;
        QVector<DrawingGuide> guides;
        GuideSettings settings;
        QTransform mapping;
        QRectF bounds;
    };
    const QImage &cachedLines(LineCache &cache, const QVector<DrawingGuide> &guides,
                             const GuideSettings &settings, const QTransform &mapping,
                             QRectF bounds, bool selected = false) const;
    mutable LineCache m_backgroundCache, m_selectedCache;
    // Background-only rebuilds, to verify independence from active handle drags.
    mutable quint64 m_cacheRenderCount = 0;
};
} // namespace toon
