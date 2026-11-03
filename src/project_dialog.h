#pragma once
#include "project.h"
#include <QDialog>
class QLineEdit;
class QCheckBox;
class QLabel;
class QPushButton;
class QComboBox;
class QTabBar;

namespace toon {
class Canvas;
class ValueSlider;
class ProjectDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProjectDialog(const Project &project, bool creating, QWidget *parent = nullptr, int shotIndex = 0);
    Project resultProject() const;
    QString filePath() const;
    QString requestedWorkspaceId() const;
    QString requestedLayerId() const;
    QString selectedTemplateId() const { return m_templateId; }
    bool closedWithFocus() const { return m_closedWithFocus; }
public slots:
    void done(int result) override;
private:
    void refresh();
    void submit();
    void selectTemplate(int index);
    void commitNumbers();
    Project m_initial;
    bool m_creating;
    bool m_refreshing=false,m_workspaceChosen=false,m_closedWithFocus=false;
    int m_shotIndex = 0;
    double m_aspect = 16.0 / 9;
    QString m_templateId;
    QLineEdit *m_movie, *m_number, *m_name, *m_path;
    ValueSlider *m_width, *m_height, *m_frames;
    ValueSlider *m_left, *m_right, *m_top, *m_bottom, *m_header, *m_overscan;
    ValueSlider *m_fps;
    QCheckBox *m_lock, *m_transparent, *m_safe, *m_showHeader, *m_showGuides;
    QLabel *m_validation, *m_duration, *m_templateSummary=nullptr;
    QComboBox *m_framePreset=nullptr,*m_workspace=nullptr;
    QTabBar *m_templates=nullptr;
    QPushButton *m_submit;
    Canvas *m_preview;
};
} // namespace toon
