#pragma once
#include "motion_study.h"
#include <QWidget>
#include <QElapsedTimer>
#include <memory>
#include <atomic>
class QLabel;class QListWidget;class QTabBar;class QTimer;class QToolButton;class QTemporaryDir;class QCloseEvent;
namespace toon {
class ValueSlider;
class MotionStudyWindow final:public QWidget {
    Q_OBJECT
public:
    explicit MotionStudyWindow(QWidget *parent=nullptr);
    ~MotionStudyWindow() override;
    void importVideoFile(const QString &path);
    void saveStudyAs(const QString &path);
    bool openStudyFolder(const QString &path);
    const MotionStudy &study() const {return m_study;}
signals:
    void studyLoaded(bool success);
    void studySaved(bool success);
    void insertRequested(const toon::MotionStudy &,const QString &frames,bool reference);
protected:
    void closeEvent(QCloseEvent *)override;
private:
    void importVideo();void openStudy();void saveStudy();void render();void rebuild();bool discard();
    MotionStudy m_study;QString m_frames,m_saved;
    std::shared_ptr<QTemporaryDir> m_cache;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QLabel *m_preview,*m_info,*m_status;
    QListWidget *m_strip;QTabBar *m_mode;ValueSlider *m_frame;QTimer *m_timer;
    QToolButton *m_play;QWidget *m_controls;
    bool m_dirty=false,m_busy=false,m_refreshing=false;
    QElapsedTimer m_clock;double m_playOffset=0;
};
}
