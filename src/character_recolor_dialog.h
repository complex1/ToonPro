#pragma once

#include "character_recolor.h"
#include <QDialog>
#include <QFutureWatcher>
#include <QPointer>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSplitter;
class QTabBar;
class QToolButton;

namespace toon {
class Document;
class RecolorPreviewView;

class CharacterRecolorDialog final : public QDialog {
    Q_OBJECT
public:
    explicit CharacterRecolorDialog(Document *document, const QString &characterId,
                                    const QString &targetVariantId = {}, QWidget *parent = nullptr);
    ~CharacterRecolorDialog() override;
    bool busy() const { return m_busy; }
    const RecolorResult &previewResult() const { return m_result; }
public slots:
    void preview();
    bool applyPreview();
    void reject() override;
signals:
    void previewFinished();
protected:
    void resizeEvent(QResizeEvent *event) override;
private:
    void refreshDocument();
    void refreshPalettes(const QString &sourceId = {}, const QString &targetId = {}, bool defaultTarget = true);
    void refreshDrawings(bool initial = false);
    void invalidate(const QString &message = {});
    void finishPreview();
    void showDrawing();
    void updateActions();
    void checkDrawings(bool checked);
    const CharacterModel *character() const;

    QPointer<Document> m_document;
    QComboBox *m_characters, *m_source, *m_target, *m_scope;
    QListWidget *m_drawings;
    QTabBar *m_compare;
    RecolorPreviewView *m_view;
    QLabel *m_status, *m_caption;
    QPushButton *m_preview, *m_apply, *m_cancel;
    QToolButton *m_all, *m_none;
    QSplitter *m_splitter = nullptr;
    QFutureWatcher<RecolorResult> m_worker;
    RecolorCancellation m_cancellation;
    RecolorResult m_result;
    quint64 m_revision = 0, m_workerRevision = 0;
    bool m_busy = false, m_refreshing = false, m_applying = false;
    QString m_previewKey;
};
} // namespace toon
