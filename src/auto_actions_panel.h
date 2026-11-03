#pragma once
#include <QPointer>
#include <QWidget>

class QComboBox;
class QLabel;
class QListWidget;
class QStackedWidget;
class QToolButton;
namespace toon {
class AutoActions;
class AutoActionsPanel final : public QWidget {
    Q_OBJECT
public:
    explicit AutoActionsPanel(AutoActions *actions, QWidget *parent = nullptr);
    QString selectedActionId() const;
    void selectAction(const QString &id);
    void setInteractionEnabled(bool enabled);
    QSize sizeHint() const override { return {250, 300}; }
signals:
    // The workspace resolves pending editing/playback before invoking play().
    void playRequested(const QString &id);
private:
    void refresh();
    void refreshSteps();
    void refreshState();
    void createAction();
    void renameAction();
    void showOptions();
    void showStepMenu(const QPoint &position);
    bool canManage() const;
    QPointer<AutoActions> m_actions;
    QComboBox *m_selector = nullptr;
    QListWidget *m_steps = nullptr;
    QStackedWidget *m_body = nullptr;
    QLabel *m_empty = nullptr;
    QLabel *m_status = nullptr;
    QToolButton *m_add = nullptr, *m_more = nullptr;
    QToolButton *m_record = nullptr, *m_stop = nullptr, *m_play = nullptr;
    bool m_interactionEnabled = true;
    QString m_message;
};
}
