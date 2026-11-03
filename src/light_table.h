#pragma once
#include "drawing_aids.h"
#include <QImage>
#include <QPointer>
#include <QTransform>
#include <QVector>
#include <QWidget>
#include <atomic>
#include <memory>

class QComboBox;
class QLabel;
class QListWidget;
class QToolButton;
class QMouseEvent;
class QTabletEvent;
namespace toon {
class Canvas;
class ValueSlider;
class LightTableOverlay;

// Embedded reference snapshots and placement are saved with the project, while
// source pixels, exports and artwork Undo remain unchanged.
class LightTablePanel : public QWidget {
    Q_OBJECT
public:
    explicit LightTablePanel(Canvas *canvas, QWidget *parent = nullptr);
    ~LightTablePanel() override;
    const QVector<LightTablePin> &pins() const { return m_pins; }
    int selectedPin() const { return m_selected; }
    bool editing() const { return m_editing; }
    bool busy() const { return m_busy; }
    bool finishPersistence();
    bool solo() const { return m_solo; }
    bool overlaysVisible() const { return m_visible; }
    bool pinLayer(const QString &shotId, const QString &layerId, QString *error = nullptr);
    bool pinCurrentLayer(QString *error = nullptr);
    bool pinImage(const QImage &image, const QString &name, QString *error = nullptr);
    bool importImage(const QString &path, QString *error = nullptr);
    bool refreshSelected(QString *error = nullptr);
    bool setPinTransform(int index, QPointF offset, qreal scale, qreal rotation);
    bool setPinOpacity(int index, qreal opacity);
    void setPinVisible(int index, bool visible);
    QTransform artworkToViewport() const;
    QPolygonF selectedViewportPolygon() const;
    QPointF rotationHandle() const;
    quint64 overlayBuildCount() const;
public slots:
    void setEditing(bool enabled);
    void setSolo(bool enabled);
    void setOverlaysVisible(bool visible);
    void selectPin(int index);
    void flip(int direction);
    void removeSelected();
    void resetSelected();
    void clearPins();
signals:
    void editingChanged(bool enabled);
    void pinsChanged();
    void busyChanged(bool busy);
    void warning(const QString &message);
protected:
    bool eventFilter(QObject *, QEvent *) override;
    void hideEvent(QHideEvent *) override;
private:
    friend class LightTableOverlay;
    LightTableState currentState() const;
    void restoreFromProject(bool reset = false);
    bool publishState();
    void commitValueEdits();
    void rebuildList();
    void syncControls();
    void updateOverlay();
    void chooseDrawing();
    void showError(const QString &message);
    bool requestSnapshot(const QString &shotId, const QString &layerId, const QString &replaceId, QString *error);
    bool addPin(LightTablePin pin, QString *error);
    bool pointerPress(QPointF position);
    void pointerMove(QPointF position);
    void pointerRelease(QPointF position);
    void cancelDrag();
    bool interactionReady() const;
    QPointer<Canvas> m_canvas;
    QPointer<LightTableOverlay> m_overlay;
    QVector<LightTablePin> m_pins;
    QListWidget *m_list;
    QLabel *m_status;
    QToolButton *m_edit, *m_show, *m_soloButton, *m_remove, *m_refresh, *m_reset, *m_previous, *m_next;
    QToolButton *m_pinCurrent, *m_pick, *m_import;
    ValueSlider *m_x, *m_y, *m_scale, *m_rotation, *m_opacity;
    int m_selected = -1;
    bool m_editing = false, m_solo = false, m_visible = true, m_busy = false, m_sync = false;
    LightTableState m_persistedState;
    bool m_restoring = false, m_publishing = false;
    quint64 m_generation = 0;
    std::shared_ptr<std::atomic_bool> m_snapshotCancelled;
    enum class Drag { None, Move, Scale, Rotate };
    Drag m_drag = Drag::None;
    LightTablePin m_dragStart;
    QPointF m_dragPoint, m_dragCenter;
    bool m_tabletContact = false, m_space = false, m_navigating = false;
};
} // namespace toon
