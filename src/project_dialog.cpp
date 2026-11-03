#include "project_dialog.h"
#include "canvas.h"
#include "icons.h"
#include "project_templates.h"
#include "property_row.h"
#include "ui_metrics.h"
#include "value_slider.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QTabBar>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace toon {
ProjectDialog::ProjectDialog(const Project &project, bool creating, QWidget *parent, int shotIndex)
    : QDialog(parent), m_initial(project), m_creating(creating), m_shotIndex(qBound(0, shotIndex, int(project.shots.size()) - 1)) {
    setWindowTitle(creating ? "New project" : "Project & shot settings");
    setObjectName("projectDialog");resize(960,720);
    auto *outer=new QVBoxLayout(this);outer->setContentsMargins(12,12,12,12);outer->setSpacing(8);
    if(creating){
        m_templates=new QTabBar(this);m_templates->setObjectName("projectTemplateTabs");m_templates->setAccessibleName("Project template");
        m_templates->setProperty("compactDrawingTabs",true);m_templates->setDrawBase(false);m_templates->setExpanding(true);m_templates->setUsesScrollButtons(true);m_templates->setFocusPolicy(Qt::StrongFocus);m_templates->setIconSize({ui::metrics::tabIcon,ui::metrics::tabIcon});
        for(const auto &item:projectStarterTemplates()){
            const int tab=m_templates->addTab(appIcon(item.icon),item.name);m_templates->setTabData(tab,item.id);m_templates->setTabToolTip(tab,item.name+" · "+item.summary);m_templates->setAccessibleTabName(tab,item.name+" project template");
        }
        outer->addWidget(m_templates);
        m_templateSummary=new QLabel; m_templateSummary->setObjectName("projectTemplateSummary");m_templateSummary->setWordWrap(true);outer->addWidget(m_templateSummary);
        auto *workspaceRow=new QHBoxLayout;workspaceRow->setContentsMargins(0,0,0,0);workspaceRow->addWidget(new QLabel("Workspace"));
        m_workspace=new QComboBox;m_workspace->setObjectName("projectWorkspace");m_workspace->setAccessibleName("Workspace after creating the project");m_workspace->setToolTip("Panel layout only; changing workspace does not change your document");
        m_workspace->addItem("Keep current",QString());m_workspace->addItem("Illustration","illustration");m_workspace->addItem("Anime","anime");m_workspace->addItem("Storyboarding","storyboard");m_workspace->addItem("Animatics","animatic");workspaceRow->addWidget(m_workspace,1);outer->addLayout(workspaceRow);
        connect(m_workspace,&QComboBox::currentIndexChanged,this,[this]{if(!m_refreshing)m_workspaceChosen=true;});
    }
    auto *body=new QHBoxLayout;body->setSpacing(12);outer->addLayout(body,1);
    auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setObjectName("projectSettingsScroll");
    auto *formWidget=new QWidget;auto *form=new QFormLayout(formWidget);form->setContentsMargins(0,0,4,0);form->setVerticalSpacing(6);form->setHorizontalSpacing(8);form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    scroll->setWidget(formWidget);body->addWidget(scroll,1);
    auto *previewBox=new QVBoxLayout;previewBox->setSpacing(4);body->addLayout(previewBox,1);previewBox->addWidget(new QLabel("Preview"));
    m_preview=new Canvas;m_preview->setObjectName("projectCanvasPreview");m_preview->setMinimumSize(260,220);m_preview->setTool(Canvas::Tool::Hand);m_preview->setAccessibleName("Project canvas preview");m_preview->setToolTip("Preview the canvas and production sheet; drag to pan");previewBox->addWidget(m_preview,1);
    auto line=[this,form](const QString &label,const QString &value,const char *id,int max){
        auto *widget=new QLineEdit(value);widget->setObjectName(id);widget->setMaxLength(max);widget->setAccessibleName(label);form->addRow(label,widget);connect(widget,&QLineEdit::textChanged,this,&ProjectDialog::refresh);return widget;
    };
    auto number=[form](const QString &label,double value,double minimum,double maximum,const char *id,int decimals=0){
        auto *widget=new ValueSlider(label,minimum,maximum,value,decimals);widget->setObjectName(id);auto *row=new PropertyRow(label,widget);row->setLabelWidth(100);form->addRow(row);return widget;
    };
    auto section=[form](const QString &text){auto *label=new QLabel(text);label->setObjectName("sectionLabel");form->addRow(label);};
    const auto &shot=project.shots[m_shotIndex];
    section(creating?"Project & first shot":"Project & current shot");
    m_movie=line("Title",project.movie,"movieTitle",200);m_number=line("Shot / cut",shot.number,"shotNumber",40);m_name=line("Shot name",shot.name,"shotName",200);
    m_path=new QLineEdit;m_path->setObjectName("projectPath");m_path->setAccessibleName("Project save location");m_path->setPlaceholderText("Choose a .toonproj file on Create");
    if(creating){
        auto *location=new QWidget;auto *row=new QHBoxLayout(location);row->setContentsMargins(0,0,0,0);row->setSpacing(4);row->addWidget(m_path);
        auto *browse=new QPushButton(appIcon("folder-open"),QString());browse->setObjectName("projectBrowse");browse->setAccessibleName("Choose project save location");browse->setToolTip("Choose project save location");browse->setAutoDefault(false);browse->setFixedSize(28,28);browse->setIconSize({16,16});row->addWidget(browse);form->addRow("Save location",location);
        connect(browse,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getSaveFileName(this,"Save new project",{},"Toon Pro project (*.toonproj)");if(!path.isEmpty())m_path->setText(path);});
    }else{m_path->setParent(this);m_path->hide();}
    section("Canvas & timing");
    m_framePreset=new QComboBox;m_framePreset->setObjectName("framePreset");m_framePreset->setAccessibleName("Canvas dimensions preset");m_framePreset->addItems({"Custom","Full HD · 1920 × 1080","HD · 1280 × 720","4K UHD · 3840 × 2160","Portrait · 1080 × 1920","Square · 2048 × 2048"});form->addRow("Dimensions",m_framePreset);
    m_width=number("Width · px",project.width,64,8192,"frameWidth");m_height=number("Height · px",project.height,64,8192,"frameHeight");m_width->setSliderRange(64,4096);m_height->setSliderRange(64,4096);m_aspect=double(project.width)/project.height;
    const QString reframeHelp="Changing dimensions reframes existing artwork without scaling its pixels.";m_width->setToolTip(reframeHelp);m_height->setToolTip(reframeHelp);
    m_lock=new QCheckBox("Lock aspect ratio");m_lock->setObjectName("projectAspectLock");form->addRow(m_lock);
    m_fps=number("Rate · FPS",project.fps,1,120,"frameRate",3);m_frames=number("Frames",shot.durationFrames,1,1000000,"durationFrames");m_frames->setSliderRange(1,3600);
    m_duration=new QLabel;m_duration->setObjectName("projectDuration");form->addRow(m_duration);
    m_transparent=new QCheckBox("Transparent artwork background");m_transparent->setObjectName("projectTransparent");m_transparent->setChecked(project.transparent);form->addRow(m_transparent);
    section("Production sheet");
    const auto &sheet=shot.sheet;
    m_showHeader=new QCheckBox("Production header");m_showHeader->setObjectName("projectShowHeader");m_showHeader->setChecked(sheet.showHeader);m_showHeader->setToolTip("Include project and shot details above the artwork in the canvas and sheet export");form->addRow(m_showHeader);
    m_header=number("Header · px",sheet.header,0,4096,"headerHeight");m_header->setToolTip("Minimum header height; the header grows to fit the project and shot details when enabled.");
    m_showGuides=new QCheckBox("Frame guides");m_showGuides->setObjectName("projectShowGuides");m_showGuides->setChecked(sheet.showGuides);m_showGuides->setToolTip("Show output frame, drawing boundary and registration marks on the production sheet");form->addRow(m_showGuides);
    m_safe=new QCheckBox("90% safe area");m_safe->setObjectName("projectSafeArea");m_safe->setChecked(sheet.safeArea);form->addRow(m_safe);
    m_overscan=number("Overscan · px",sheet.overscan,0,4096,"overscan");m_overscan->setToolTip("Extra drawing area on each side of the output frame");m_overscan->setSliderRange(0,1024);
    m_left=number("Left · px",sheet.left,0,4096,"leftMargin");m_right=number("Right · px",sheet.right,0,4096,"rightMargin");m_top=number("Top · px",sheet.top,0,4096,"topMargin");m_bottom=number("Bottom · px",sheet.bottom,0,4096,"bottomMargin");
    for(auto *control:{m_left,m_right,m_top,m_bottom,m_header})control->setSliderRange(0,512);
    auto *profile=new QLabel("sRGB · 8-bit RGBA");profile->setObjectName("muted");form->addRow(profile);
    m_validation=new QLabel;m_validation->setObjectName("validation");m_validation->setWordWrap(true);outer->addWidget(m_validation);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Cancel);m_submit=buttons->addButton(creating?"Create project":"Apply settings",QDialogButtonBox::AcceptRole);m_submit->setObjectName("submitProject");m_submit->setDefault(true);outer->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);connect(m_submit,&QPushButton::clicked,this,&ProjectDialog::submit);
    connect(m_framePreset,&QComboBox::activated,this,[this](int index){
        const QSize sizes[]={{0,0},{1920,1080},{1280,720},{3840,2160},{1080,1920},{2048,2048}};if(index<=0||index>=6)return;
        const QSignalBlocker w(m_width),h(m_height);m_width->setValue(sizes[index].width());m_height->setValue(sizes[index].height());m_aspect=double(m_width->value())/m_height->value();refresh();
    });
    connect(m_width,&ValueSlider::valueChanged,this,[this](double value){if(m_refreshing)return;if(m_lock->isChecked()){const QSignalBlocker block(m_height);m_height->setValue(qRound(value/m_aspect));}m_framePreset->setCurrentIndex(0);refresh();});
    connect(m_height,&ValueSlider::valueChanged,this,[this](double value){if(m_refreshing)return;if(m_lock->isChecked()){const QSignalBlocker block(m_width);m_width->setValue(qRound(value*m_aspect));}m_framePreset->setCurrentIndex(0);refresh();});
    connect(m_lock,&QCheckBox::toggled,this,[this]{m_aspect=double(m_width->value())/m_height->value();});
    for(auto *control:{m_frames,m_overscan,m_left,m_right,m_top,m_bottom,m_header,m_fps})connect(control,&ValueSlider::valueChanged,this,&ProjectDialog::refresh);
    for(auto *control:{m_safe,m_transparent,m_showHeader,m_showGuides})connect(control,&QCheckBox::toggled,this,&ProjectDialog::refresh);
    if(creating){connect(m_templates,&QTabBar::currentChanged,this,&ProjectDialog::selectTemplate);m_templates->setCurrentIndex(2);}else refresh();
}

void ProjectDialog::selectTemplate(int index){
    if(!m_creating||m_refreshing||index<0||index>=projectStarterTemplates().size())return;
    const auto &info=projectStarterTemplates()[index];auto starter=projectFromStarterTemplate(info.id);if(!starter)return;
    {
        const QScopedValueRollback<bool> guard(m_refreshing,true);
        m_initial=std::move(*starter);m_shotIndex=0;m_templateId=info.id;const auto &shot=m_initial.shots[0];const auto &sheet=shot.sheet;
        m_width->setValueSilently(m_initial.width);m_height->setValueSilently(m_initial.height);m_fps->setValueSilently(m_initial.fps);m_frames->setValueSilently(shot.durationFrames);m_aspect=double(m_initial.width)/m_initial.height;
        m_transparent->setChecked(m_initial.transparent);m_left->setValueSilently(sheet.left);m_right->setValueSilently(sheet.right);m_top->setValueSilently(sheet.top);m_bottom->setValueSilently(sheet.bottom);m_header->setValueSilently(sheet.header);m_overscan->setValueSilently(sheet.overscan);m_safe->setChecked(sheet.safeArea);m_showHeader->setChecked(sheet.showHeader);m_showGuides->setChecked(sheet.showGuides);m_framePreset->setCurrentIndex(0);
        if(!m_workspaceChosen)m_workspace->setCurrentIndex(m_workspace->findData(info.workspaceId));
        m_templateSummary->setText(info.summary);m_templateSummary->setToolTip(info.summary);
    }
    refresh();
}
Project ProjectDialog::resultProject() const {
    auto project=m_initial;project.movie=m_movie->text().trimmed();project.width=m_width->value();project.height=m_height->value();project.fps=m_fps->value();project.transparent=m_transparent->isChecked();
    auto &shot=project.shots[m_shotIndex];shot.number=m_number->text().trimmed();shot.name=m_name->text().trimmed();shot.durationFrames=m_frames->value();
    auto &sheet=shot.sheet;sheet.left=m_left->value();sheet.right=m_right->value();sheet.top=m_top->value();sheet.bottom=m_bottom->value();sheet.header=m_header->value();sheet.overscan=m_overscan->value();sheet.safeArea=m_safe->isChecked();sheet.showHeader=m_showHeader->isChecked();sheet.showGuides=m_showGuides->isChecked();
    if(m_creating)for(auto &track:shot.animationTracks)for(auto &exposure:track.exposures)
        exposure.durationFrames=m_templateId=="storyboard"?shot.durationFrames:std::min(exposure.durationFrames,shot.durationFrames-exposure.startFrame);
    return project;
}
QString ProjectDialog::filePath() const {const auto path=m_path->text().trimmed();return path.endsWith(".toonproj",Qt::CaseInsensitive)?path:path+".toonproj";}
QString ProjectDialog::requestedWorkspaceId() const {return m_creating&&m_workspace?m_workspace->currentData().toString():QString();}
QString ProjectDialog::requestedLayerId() const {return m_creating?starterDrawingLayerId(m_initial,m_templateId):QString();}
void ProjectDialog::done(int result){
    // Capture focus ownership before the native modal is hidden/destroyed.
    // The caller restores its editor only after this dialog leaves scope.
    m_closedWithFocus=QApplication::activeWindow()==this;
    QDialog::done(result);
}
void ProjectDialog::refresh(){
    if(m_refreshing)return;const auto project=resultProject();const auto error=project.validate();m_validation->setText(error);m_validation->setVisible(!error.isEmpty());m_submit->setEnabled(error.isEmpty());
    m_header->setEnabled(m_showHeader->isChecked());m_safe->setEnabled(m_showGuides->isChecked());
    m_duration->setText(QString("%1 seconds").arg(project.shots[m_shotIndex].durationFrames/project.fps,0,'f',3));
    if(error.isEmpty())m_preview->setProject(project,m_shotIndex);
}
void ProjectDialog::commitNumbers(){for(auto *control:{m_width,m_height,m_frames,m_fps,m_left,m_right,m_top,m_bottom,m_header,m_overscan})control->commitEditing();}
void ProjectDialog::submit(){
    commitNumbers();if(!resultProject().validate().isEmpty())return;
    if(m_creating&&m_path->text().trimmed().isEmpty()){
        const auto path=QFileDialog::getSaveFileName(this,"Save new project","Untitled.toonproj","Toon Pro project (*.toonproj)");if(path.isEmpty())return;m_path->setText(path);
    }
    accept();
}
} // namespace toon
