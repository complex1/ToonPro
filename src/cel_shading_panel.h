#pragma once
#include "cel_shading.h"
#include "drawing_aids.h"
#include <QWidget>
#include <QFutureWatcher>
#include <QPointer>
#include <optional>

class QComboBox;
class QLabel;
class QToolButton;
class QTabBar;
namespace toon {
class Canvas;
class ValueSlider;
class CelShadingOverlay;
class CelShadingPanel : public QWidget {
    Q_OBJECT
public:
    explicit CelShadingPanel(Canvas *canvas, QWidget *parent=nullptr);
    ~CelShadingPanel() override;
    bool active() const { return m_active; }
    void setActive(bool active);
    bool pending() const { return !m_boundaries.isEmpty() || !m_fills.isEmpty() || m_dragging; }
    bool finishEditing();
    bool finishPersistence();
    void cancelEditing();
    bool apply();
    bool busy() const { return m_busy; }
    QString outlineLayerId() const;
    void setOutlineLayerId(const QString &id);
    bool addBoundary(const QPainterPath &path, CelShadeKind kind);
    bool addFill(QPointF point, CelShadeKind kind, QColor color);
    const QVector<CelBoundary> &boundaries() const { return m_boundaries; }
    const QVector<CelShadeFill> &fills() const { return m_fills; }
    const CelShadingResult &preview() const { return m_preview; }
    const QImage &previewComposite() const { return m_previewComposite; }
    void setBoundaryFinish(CelBoundaryFinish finish);
signals:
    void activeChanged(bool active);
    void pendingChanged(bool pending);
    void previewChanged();
protected:
    bool eventFilter(QObject *, QEvent *) override;
    void hideEvent(QHideEvent *) override;
    void showEvent(QShowEvent *) override;
private:
    friend class CelShadingOverlay;
    void refreshSources();
    void contextChanged();
    void restoreDraft(bool sourceChanged = false);
    void clearLocalDraft();
    CelShadingDraft currentDraft() const;
    bool persistDraft();
    QString unavailableReason() const;
    void notifyState();
    void renderPreview();
    void completePreview(quint64 generation, CelShadingResult result, QImage composite);
    void invalidateWorker();
    void drawStart(QPointF point);
    void drawMove(QPointF point);
    void drawFinish(QPointF point);
    void paintOverlay(QPainter &painter);
    void setStatus(const QString &message);
    void useForeground();
    QTransform canvasTransform() const;
    QPointer<Canvas> m_canvas;
    QPointer<CelShadingOverlay> m_overlay;
    QComboBox *m_outline;
    QTabBar *m_kindTabs,*m_modeTabs,*m_finishTabs;
    QToolButton *m_enable,*m_color,*m_guideColor,*m_undo,*m_apply,*m_cancel;
    ValueSlider *m_width;
    QLabel *m_status;
    QVector<CelBoundary> m_boundaries;
    QVector<CelShadeFill> m_fills;
    QVector<bool> m_operations;
    QPainterPath m_livePath;
    CelShadingResult m_preview;
    QImage m_previewComposite;
    QString m_shotId;
    int m_frame=0;
    QColor m_shadow=QColor("#77597e"),m_highlight=QColor("#fff0b5");
    QColor m_shadowGuide=QColor("#5cb8ed"),m_highlightGuide=QColor("#ef8991");
    CelBoundaryFinish m_finish=CelBoundaryFinish::Hidden;
    bool m_active=false,m_dragging=false,m_tablet=false,m_space=false,m_busy=false,m_applying=false;
    bool m_lastPending=false;
    bool m_restoring=false,m_publishing=false,m_previewDirty=false;
    QString m_contextShotId,m_contextOutlineId;
    int m_contextFrame=-1;
    std::optional<CelShadingDraft> m_persistedDraft;
    quint64 m_generation=0;
    std::shared_ptr<std::atomic_bool> m_cancelled;
};
} // namespace toon
