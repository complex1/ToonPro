#pragma once
#include <QFrame>
#include <QList>

class QAction;
class QMenu;
class QToolButton;

namespace toon {
class Canvas;
class Document;

class SelectionContextBar : public QFrame {
    Q_OBJECT
public:
    explicit SelectionContextBar(Canvas *canvas);
    bool positionPinned() const { return m_pinned; }
public slots:
    void updatePosition();
    void resetPosition();
    void setPositionPinned(bool pinned);
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    void refresh();
    void addResizeMenu(QMenu *menu, const QString &title, const QString &id, int direction);
    QPoint clampPosition(QPoint position) const;
    Canvas *m_canvas;
    Document *m_document;
    QToolButton *m_grip;
    QList<QToolButton *> m_modes;
    QToolButton *m_transform;
    QToolButton *m_invert;
    QToolButton *m_deselect;
    QToolButton *m_apply;
    QToolButton *m_cancel;
    QToolButton *m_more;
    QMenu *m_moreMenu;
    QAction *m_fill;
    QAction *m_clear;
    QAction *m_expand;
    QAction *m_contract;
    QAction *m_selectAll;
    QAction *m_focus;
    QAction *m_pin;
    bool m_pinned = false;
    bool m_dragging = false;
    QPoint m_pinnedPoint;
    QPoint m_dragOrigin;
    QPoint m_dragPosition;
};
} // namespace toon
