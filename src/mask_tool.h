#pragma once
#include "layer_effects.h"
#include "raster_edit.h"
#include "mask_selection.h"
#include <QObject>
#include <QTransform>
#include <QVector>
class QPainter;
namespace toon {
class Canvas;
class MaskTool final : public QObject {
    Q_OBJECT
public:
    explicit MaskTool(Canvas *canvas);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    bool revealing() const { return m_reveal; }
    void setRevealing(bool value);
    qreal size() const { return m_size; }
    void setSize(qreal value);
    qreal strength() const { return m_strength; }
    void setStrength(qreal value);
    qreal hardness() const { return m_hardness; }
    void setHardness(qreal value);
    bool dragging() const { return m_drag; }
    QString unavailableReason() const;
    void finish();
    void finishTabletContact();
    void cancel();
    QPainterPath cursorPath() const;
signals:
    void changed();
protected:
    bool eventFilter(QObject *, QEvent *) override;
private:
    bool press(QPointF point,qreal pressure);
    void move(QPointF point,qreal pressure);
    bool dab(LayerMask &mask,QPointF point,qreal pressure,QString *error);
    QTransform worldTransform() const;
    Canvas *m_canvas;
    bool m_enabled=false,m_reveal=false,m_drag=false,m_tablet=false,m_inverted=false;
    qreal m_size=48,m_strength=1,m_hardness=.8,m_pressure=1,m_spacing=0;
    QPointF m_last;
    LayerMask m_mask;
    // Only touched tiles need floating coverage. Keep rounding out of the dab
    // accumulator; stored/project mask tiles remain Grayscale8.
    QMap<TileKey,QVector<float>> m_workingTiles;
    int m_otherMaskTiles=0;
    QTransform m_inverse;
    QRectF m_paintBounds;
    QString m_layerId;
    MaskSelectionSampler m_selectionCache;
    bool m_selectionActive=false;
};
} // namespace toon
