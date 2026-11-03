#pragma once
#include "project.h"
#include <QDialog>
#include <QPointer>
#include <memory>

class QLabel;
class QTemporaryDir;
class QDialogButtonBox;
namespace toon {
class Canvas;
class Document;
class TimelineController;
class ToolCatalog;
class ToolSession;
class ValueSlider;

// A local document and history keep unfinished edits out of every linked instance.
class SymbolEditorDialog final : public QDialog {
    Q_OBJECT
public:
    SymbolEditorDialog(Document *document, const QString &symbolId, Canvas *sourceCanvas = nullptr,
                       ToolCatalog *catalog = nullptr, QWidget *parent = nullptr);
    ~SymbolEditorDialog() override;
    Canvas *canvas() const { return m_canvas; }
    TimelineController *timeline() const { return m_timeline; }
    bool applyChanges();
    bool closedWithFocus() const { return m_closedWithFocus; }
public slots:
    void accept() override;
    void reject() override;
    void done(int result) override;
protected:
    void keyPressEvent(QKeyEvent *event) override;
private:
    void showError(const QString &message);
    void updateDuration();
    void refreshDuration();
    QPointer<Document> m_source;
    QString m_symbolId;
    Shot m_original;
    Canvas *m_canvas;
    TimelineController *m_timeline;
    ToolSession *m_session = nullptr;
    ValueSlider *m_duration;
    QLabel *m_error;
    QDialogButtonBox *m_buttons;
    std::unique_ptr<QTemporaryDir> m_libraryDirectory;
    bool m_valid = false, m_refreshing = false, m_applying = false;
    bool m_closedWithFocus = false;
};
}
