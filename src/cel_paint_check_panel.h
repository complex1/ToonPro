#pragma once
#include "cel_paint_check.h"
#include <QWidget>
#include <QPointer>
#include <QFutureWatcher>

class QComboBox;
class QLabel;
class QListWidget;
class QToolButton;
class QTabWidget;
namespace toon {
class Canvas;
class Document;
class ReferenceView;
class ValueSlider;
class CelPaintCheckPanel : public QWidget {
    Q_OBJECT
public:
    explicit CelPaintCheckPanel(Canvas *canvas,QWidget *parent=nullptr);
    ~CelPaintCheckPanel() override;
    bool pending() const;
    bool busy() const { return m_checking || m_preparing; }
    const CelPaintCheckResult &inspection() const { return m_result; }
    const CelPaintRepairResult &repairs() const { return m_repairs; }
    bool finishEditing();
    QString selectedCharacterId() const { return m_paletteCharacterId; }
    QString selectedVariantId() const { return m_paletteVariantId; }
    bool selectPalette(const QString &characterId, const QString &variantId = {});
public slots:
    void startCheck();
    void previewRepairs();
    bool applyRepairs();
    void cancelEditing();
signals:
    void pendingChanged(bool pending);
    void inspectionFinished();
    void previewFinished();
protected:
    bool eventFilter(QObject *,QEvent *) override;
    void hideEvent(QHideEvent *) override;
    void showEvent(QShowEvent *) override;
private:
    void finishCheck();
    void finishRepair();
    void refreshSources();
    int paletteIndex(const QString &characterId, const QString &variantId) const;
    void syncPaletteSelection();
    void invalidate();
    void showProblem(bool navigate=true);
    void updateActions();
    void setPending(bool value);
    void discardRepairs();
    void status(const QString &message);
    QPointer<Canvas> m_canvas;
    QPointer<Document> m_document;
    QListWidget *m_targets,*m_problems;
    QComboBox *m_palette;
    ValueSlider *m_tolerance,*m_holes,*m_strays;
    QTabWidget *m_tabs;
    ReferenceView *m_preview;
    QLabel *m_status,*m_caption;
    QToolButton *m_check,*m_previous,*m_next,*m_previewButton,*m_apply,*m_cancel,*m_original;
    QPointer<QWidget> m_overlay;
    QFutureWatcher<CelPaintCheckResult> m_checkWorker;
    QFutureWatcher<CelPaintRepairResult> m_repairWorker;
    CelPaintCancellation m_checkCancellation,m_repairCancellation;
    CelPaintCheckResult m_result;
    CelPaintRepairResult m_repairs;
    quint64 m_revision=0,m_checkRevision=0,m_repairRevision=0;
    bool m_checking=false,m_preparing=false,m_pending=false,m_refreshing=false,m_navigating=false;
    QString m_paletteCharacterId,m_paletteVariantId;
    QRect m_highlightBounds;
    QImage m_highlightMask;
};
}
