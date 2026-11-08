#pragma once
#include <QPointer>
#include <QWidget>

class QTabBar;
class QToolButton;
namespace toon {
class Document;
class Reference3DCanvasController;
class Reference3DReviewWindow;
class Reference3DLayerPanel final:public QWidget {
    Q_OBJECT
public:
    Reference3DLayerPanel(Document *,Reference3DCanvasController *,QWidget *parent=nullptr);
    ~Reference3DLayerPanel() override;
    Reference3DReviewWindow *editor() const { return m_editor; }
    void finishEditing();
    void cancelEditing();
signals:
    void drawAboveRequested();
    void warning(const QString &message);
private:
    void synchronize();
    void beginEditorEdit();
    void previewEditorEdit();
    void endEditorEdit(bool cancelled);
    QString activeReferenceId() const;
    QPointer<Document> m_document;
    QPointer<Reference3DCanvasController> m_controller;
    Reference3DReviewWindow *m_editor=nullptr;
    QTabBar *m_modes=nullptr,*m_axes=nullptr;
    QToolButton *m_newLayer=nullptr,*m_drawAbove=nullptr;
    QString m_boundLayer;
    bool m_syncing=false,m_editing=false,m_creating=false,m_rejected=false;
};
} // namespace toon
