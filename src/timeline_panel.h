#pragma once
#include <QWidget>
#include <QString>
#include <QPointer>
#include "cel_edit.h"

class QLabel;
class QLineEdit;
class QGridLayout;
class QBoxLayout;
class QMenu;
class QToolButton;
class QTabBar;
class QStackedWidget;
namespace toon {
class TimelineController;
class TimelineGrid;
class ValueSlider;
class DrawingTemplateLibrary;

class TimelinePanel final : public QWidget {
    Q_OBJECT
public:
    explicit TimelinePanel(TimelineController *controller,QWidget *parent=nullptr,DrawingTemplateLibrary *templates=nullptr);
    ~TimelinePanel() override;
protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
private:
    void refresh();
    void arrangeControls();
    void refreshOptions();
    void refreshReviewMode();
    void addTrack();
    void addFolder(const QString &parentId);
    void editRange();
    void editOnionSkin();
    void rippleTime();
    QPointer<TimelineController> m_controller;
    DrawingTemplateLibrary *m_templates;
    TimelineGrid *m_grid;
    QTabBar *m_reviewModes;
    QStackedWidget *m_reviewPages;
    ValueSlider *m_frame,*m_zoom;
    QToolButton *m_previousFrame,*m_nextFrame,*m_previousDrawing,*m_nextDrawing,*m_play,*m_loop,*m_onion;
    QToolButton *m_add,*m_blank,*m_duplicate,*m_remove,*m_range,*m_rangeSettings,*m_audioImport,*m_audioCancel;
    QToolButton *m_onionSettings,*m_ripple,*m_audioScrub;
    QToolButton *m_fitShot,*m_fitSelection,*m_centerFrame=nullptr,*m_more=nullptr;
    QWidget *m_transportRow,*m_editingRow,*m_navigationRow,*m_rangeRow;
    QGridLayout *m_headerLayout,*m_footerLayout;
    QBoxLayout *m_editingLayout;
    QMenu *m_options;
    QToolButton *m_previewQuality,*m_reuse,*m_addCue,*m_addFolder,*m_clearSolo;
    QLabel *m_rangeLabel,*m_audioStatus=nullptr;
    int m_frameCount=-1,m_displayedFrame=-1,m_arrangementMode=-1;
    bool m_refreshing=false,m_settingFrame=false,m_arrangingControls=false;
};

class FramePropertiesPanel final : public QWidget {
    Q_OBJECT
public:
    explicit FramePropertiesPanel(TimelineController *controller,QWidget *parent=nullptr);
protected:
    bool eventFilter(QObject *watched,QEvent *event) override;
private:
    void refresh();
    void commitTiming();
    void commitName();
    void commitEditors();
    void cancelEditors();
    TimelineController *m_controller;
    QLabel *m_context;
    QWidget *m_celActions;
    QVector<TimelineCelRef> m_displayedCels;
    bool m_multi=false,m_bulkHoldEdited=false;
    QLineEdit *m_name;
    ValueSlider *m_start,*m_hold;
    QString m_exposureId,m_drawingId,m_displayedName;
    int m_displayedStart=-1,m_displayedHold=-1,m_frameCount=-1;
    bool m_refreshing=false,m_resolving=false;
};
} // namespace toon
