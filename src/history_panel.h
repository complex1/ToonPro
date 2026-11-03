#pragma once

#include <QPointer>
#include <QVector>
#include <QWidget>

class QListWidget;
class QToolButton;
class QUndoCommand;
namespace toon {
class Document;

// Navigation is a request: MainWindow resolves pending canvas previews before
// changing the document stack. This panel never bypasses that workflow.
class HistoryPanel final : public QWidget {
    Q_OBJECT
public:
    explicit HistoryPanel(Document *document, QWidget *parent = nullptr);
    QSize sizeHint() const override { return {240, 260}; }
    void setInteractionEnabled(bool enabled);
signals:
    void requestHistoryIndex(int index);
    void navigationBlocked(const QString &reason);
private:
    void refresh();
    void refreshStates();
    void requestIndex(int index);
    bool mayRequest() const;
    QPointer<Document> m_document;
    QListWidget *m_list = nullptr;
    QToolButton *m_undo = nullptr, *m_redo = nullptr;
    QVector<const QUndoCommand *> m_commands;
    QVector<QString> m_labels;
    bool m_interactionEnabled = true;
    bool m_retainedBaseline = false;
    int m_lastIndex = 0;
};
} // namespace toon
