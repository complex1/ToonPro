#pragma once
#include "timeline_cues.h"
#include <QWidget>
#include <optional>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QTabBar;
class QToolButton;
namespace toon {
class TimelineController;
class ValueSlider;
class TimelineCuePanel final : public QWidget {
    Q_OBJECT
public:
    explicit TimelineCuePanel(TimelineController *controller, QWidget *parent = nullptr);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    enum Field { Name = 1, Start = 2, Hold = 4, Kind = 8, Speaker = 16, Text = 32 };
    const TimelineCue *selectedCue() const;
    void refresh();
    void refreshList();
    void beginEdit(int field);
    void commitEditors();
    void cancelEditors();
    void chooseColor();
    TimelineController *m_controller;
    QWidget *m_controls, *m_speakerRow;
    QLineEdit *m_search, *m_name, *m_speaker;
    QListWidget *m_list;
    QTabBar *m_kind;
    ValueSlider *m_start, *m_hold;
    QPlainTextEdit *m_text;
    QLabel *m_textLabel, *m_status;
    QToolButton *m_add, *m_delete, *m_previous, *m_next, *m_color;
    QString m_id, m_shotId, m_listShotId, m_listSearch;
    QVector<TimelineCue> m_listCues;
    std::optional<TimelineCue> m_snapshot;
    int m_dirty = 0;
    bool m_refreshing = false, m_resolving = false;
};
} // namespace toon
