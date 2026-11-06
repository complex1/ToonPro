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
    : QDialog(parent), m_initial(project), m_creating(creating), m_shotIndex(qBound(0, shotIndex, int(project.shots.size()) - 1)), m_kind(project.kind) {
    setWindowTitle(creating ? "New project" : "Project & shot settings");
    setObjectName("projectDialog");resize(960,720);
    auto *outer=new QVBoxLayout(this);outer->setContentsMargins(12,12,12,12);outer->setSpacing(8);
    if(creating){
        m_kinds=new QTabBar(this);m_kinds->setObjectName("projectKindTabs");m_kinds->setAccessibleName("Project type");
        m_kinds->setProperty("compactDrawingTabs",true);m_kinds->setDrawBase(false);m_kinds->setExpanding(true);m_kinds->setFocusPolicy(Qt::StrongFocus);m_kinds->setIconSize({ui::metrics::tabIcon,ui::metrics::tabIcon});
        for(const auto kind:{ProjectKind::Film,ProjectKind::Series,ProjectKind::DrawingOnly}){
            const auto icon=kind==ProjectKind::Film?"clapperboard":kind==ProjectKind::Series?"layers":"brush";
            const int tab=m_kinds->addTab(appIcon(icon),projectKindName(kind));m_kinds->setTabData(tab,projectKindKey(kind));
            m_kinds->setTabToolTip(tab,kind==ProjectKind::DrawingOnly?"Backgrounds and standalone artwork, without animation setup":projectKindName(kind)+" production");
            m_kinds->setAccessibleTabName(tab,projectKindName(kind));
        }
        m_kinds->setCurrentIndex(int(m_kind));outer->addWidget(m_kinds);
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
    }else{auto *kindLabel=new QLabel(projectKindName(project.kind));kindLabel->setObjectName("projectKindLabel");outer->addWidget(kindLabel);}
    auto *body=new QHBoxLayout;body->setSpacing(12);outer->addLayout(body,1);
    auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setObjectName("projectSettingsScroll");
    auto *formWidget=new QWidget;auto *form=new QFormLayout(formWidget);form->setContentsMargins(0,0,4,0);form->setVerticalSpacing(6);form->setHorizontalSpacing(8);form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_form=form;
    scroll->setWidget(formWidget);body->addWidget(scroll,1);
    auto *previewBox=new QVBoxLayout;previewBox->setSpacing(4);body->addLayout(previewBox,1);previewBox->addWidget(new QLabel("Preview"));
    m_preview=new Canvas;m_preview->setObjectName("projectCanvasPreview");m_preview->setMinimumSize(260,220);m_preview->setTool(Canvas::Tool::Hand);m_preview->setAccessibleName("Project canvas preview");m_preview->setToolTip("Preview the canvas and production sheet; drag to pan");previewBox->addWidget(m_preview,1);
    auto line=[this,form](const QString &label,const QString &value,const char *id,int max){
        auto *widget=new QLineEdit(value);widget->setObjectName(id);widget->setMaxLength(max);widget->setAccessibleName(label);form->addRow(label,widget);connect(widget,&QLineEdit::textChanged,this,&ProjectDialog::refresh);return widget;
    };
    auto number=[form](const QString &label,double value,double minimum,double maximum,const char *id,int decimals=0){
        auto *widget=new ValueSlider(label,minimum,maximum,value,decimals);widget->setObjectName(id);auto *row=new PropertyRow(label,widget);row->setLabelWidth(100);form->addRow(row);return widget;
    };
    auto section=[form](const QString &text){auto *label=new QLabel(text);label->setObjectName("sectionLabel");form->addRow(label);return label;};
    const auto &shot=project.shots[m_shotIndex];
    m_projectSection=section(creating?"Project & first shot":"Project & current shot");
    m_movie=line("Title",project.movie,"movieTitle",200);m_number=line("Shot / cut",shot.number,"shotNumber",40);m_name=line("Shot name",shot.name,"shotName",200);
    m_episode=line("Episode",shot.episode,"episodeName",100);
    m_path=new QLineEdit;m_path->setObjectName("projectPath");m_path->setAccessibleName("Project save location");m_path->setPlaceholderText("Choose a project location on Create");
    if(creating){
        m_projectFolder=new QCheckBox("Create a project folder");m_projectFolder->setObjectName("projectFolderStorage");m_projectFolder->setChecked(true);
        m_projectFolder->setToolTip("Store artwork and reusable assets together in a portable project folder. A .toonproj path creates a standalone legacy file.");form->addRow(m_projectFolder);
        auto *location=new QWidget;auto *row=new QHBoxLayout(location);row->setContentsMargins(0,0,0,0);row->setSpacing(4);row->addWidget(m_path);
        auto *browse=new QPushButton(appIcon("folder-open"),QString());browse->setObjectName("projectBrowse");browse->setAccessibleName("Choose project save location");browse->setToolTip("Choose project save location");browse->setAutoDefault(false);browse->setFixedSize(28,28);browse->setIconSize({16,16});row->addWidget(browse);form->addRow("Save location",location);
        connect(browse,&QPushButton::clicked,this,&ProjectDialog::choosePath);
    }else{m_path->setParent(this);m_path->hide();}
    m_canvasSection=section("Canvas & timing");
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
    if(creating){
        connect(m_templates,&QTabBar::currentChanged,this,&ProjectDialog::selectTemplate);
        connect(m_kinds,&QTabBar::currentChanged,this,&ProjectDialog::selectKind);
        const int starter=m_kind==ProjectKind::DrawingOnly?1:2;
        m_templates->setCurrentIndex(starter);
        if(m_templateId.isEmpty())selectTemplate(starter);
    }
    refreshKindControls();refresh();
}

void ProjectDialog::setFocusedDocumentControls(){
    setWindowTitle("Drawing and timing settings");
    m_form->setRowVisible(m_movie,false);m_form->setRowVisible(m_number,false);m_form->setRowVisible(m_episode,false);
    m_projectSection->setText("Document");
    if(auto *label=findChild<QLabel *>("projectKindLabel"))label->hide();
}
void ProjectDialog::selectTemplate(int index){
    if(!m_creating||m_refreshing||index<0||index>=projectStarterTemplates().size())return;
    const auto &info=projectStarterTemplates()[index];auto starter=projectFromStarterTemplate(info.id,m_kind);if(!starter)return;
    {
        const QScopedValueRollback<bool> guard(m_refreshing,true);
        m_initial=std::move(*starter);m_shotIndex=0;m_templateId=info.id;const auto &shot=m_initial.shots[0];const auto &sheet=shot.sheet;
        m_width->setValueSilently(m_initial.width);m_height->setValueSilently(m_initial.height);m_fps->setValueSilently(m_initial.fps);m_frames->setValueSilently(shot.durationFrames);m_aspect=double(m_initial.width)/m_initial.height;
        m_transparent->setChecked(m_initial.transparent);m_left->setValueSilently(sheet.left);m_right->setValueSilently(sheet.right);m_top->setValueSilently(sheet.top);m_bottom->setValueSilently(sheet.bottom);m_header->setValueSilently(sheet.header);m_overscan->setValueSilently(sheet.overscan);m_safe->setChecked(sheet.safeArea);m_showHeader->setChecked(sheet.showHeader);m_showGuides->setChecked(sheet.showGuides);m_framePreset->setCurrentIndex(0);
        if(!m_workspaceChosen)m_workspace->setCurrentIndex(m_workspace->findData(info.workspaceId));
        const auto summary=m_kind==ProjectKind::DrawingOnly&&info.id=="illustration"?QString("Plain canvas · Colors, Sketch and Ink layers"):info.summary;
        m_templateSummary->setText(summary);m_templateSummary->setToolTip(summary);
    }
    refresh();
}
void ProjectDialog::selectKind(int index) {
    if(!m_creating||m_refreshing||index<0)return;
    const auto selected=projectKindFromKey(m_kinds->tabData(index).toString());
    if(!selected||*selected==m_kind)return;
    commitNumbers();
    const auto old=m_kind;
    const auto width=m_width->value(),height=m_height->value(),fps=m_fps->value();
    const bool transparent=m_transparent->isChecked();
    m_kind=*selected;
    if(m_kind==ProjectKind::DrawingOnly||old==ProjectKind::DrawingOnly){
        if(m_kind==ProjectKind::DrawingOnly)m_productionTemplateId=m_templateId;
        const auto target=m_kind==ProjectKind::DrawingOnly
            ? (m_templateId=="blank"?QString("blank"):QString("illustration")):m_productionTemplateId;
        int next=0;
        {
            const QSignalBlocker block(m_templates);
            for(int i=0;i<m_templates->count();++i){
                const auto id=m_templates->tabData(i).toString();
                m_templates->setTabVisible(i,m_kind!=ProjectKind::DrawingOnly||id=="blank"||id=="illustration");
                if(id==target)next=i;
            }
            m_templates->setCurrentIndex(next);
        }
        m_workspaceChosen=false;
        selectTemplate(next);
        // Changing the production type must not discard the artist's canvas
        // dimensions, background choice or exact numeric entry.
        const QScopedValueRollback<bool> guard(m_refreshing,true);
        m_width->setValueSilently(width);m_height->setValueSilently(height);m_fps->setValueSilently(fps);
        m_transparent->setChecked(transparent);m_aspect=width/height;
        if(m_kind==ProjectKind::DrawingOnly){
            if(m_movie->text()=="Untitled movie")m_movie->setText("Untitled drawing");
            if(m_number->text()=="C001")m_number->setText("D001");
            if(m_name->text()=="First shot")m_name->setText("Background");
        }else{
            if(m_movie->text()=="Untitled drawing")m_movie->setText("Untitled movie");
            if(m_number->text()=="D001")m_number->setText("C001");
            if(m_name->text()=="Background")m_name->setText("First shot");
        }
    }
    m_initial.kind=m_kind;
    refreshKindControls();refresh();
}
void ProjectDialog::refreshKindControls() {
    const bool drawing=m_kind==ProjectKind::DrawingOnly;
    m_projectSection->setText(drawing?(m_creating?"Project & first drawing":"Project & current drawing")
                                    :(m_creating?"Project & first shot":"Project & current shot"));
    m_canvasSection->setText(drawing?"Canvas":"Canvas & timing");
    m_form->setRowVisible(m_number,!drawing);
    m_form->setRowVisible(m_episode,m_kind==ProjectKind::Series);
    if(m_creating&&m_kind==ProjectKind::Series&&m_episode->text().isEmpty()){
        const QSignalBlocker block(m_episode);m_episode->setText("Episode 01");
    }
    if(auto *label=qobject_cast<QLabel *>(m_form->labelForField(m_name)))label->setText(drawing?"Drawing name":"Shot name");
    m_name->setAccessibleName(drawing?"Drawing name":"Shot name");
    m_form->setRowVisible(m_fps->parentWidget(),!drawing);
    m_form->setRowVisible(m_frames->parentWidget(),!drawing);
    m_form->setRowVisible(m_duration,!drawing);
    if(m_templates){
        const QSignalBlocker block(m_templates);
        for(int i=0;i<m_templates->count();++i){const auto id=m_templates->tabData(i).toString();m_templates->setTabVisible(i,!drawing||id=="blank"||id=="illustration");}
    }
}
void ProjectDialog::choosePath() {
    const bool folder=m_projectFolder&&m_projectFolder->isChecked();
    const auto path=QFileDialog::getSaveFileName(this,folder?"Create project folder":"Save new project",
        m_path->text().trimmed().isEmpty()?(folder?"Untitled.toonstudio":"Untitled.toonproj"):m_path->text(),
        folder?"Toon Pro project folder (*.toonstudio)":"Toon Pro project (*.toonproj)");
    if(!path.isEmpty())m_path->setText(path);
}
Project ProjectDialog::resultProject() const {
    auto project=m_initial;project.kind=m_kind;project.movie=m_movie->text().trimmed();project.width=m_width->value();project.height=m_height->value();project.fps=m_fps->value();project.transparent=m_transparent->isChecked();
    auto &shot=project.shots[m_shotIndex];shot.number=m_number->text().trimmed();shot.name=m_name->text().trimmed();shot.durationFrames=m_frames->value();
    shot.episode=m_episode->text().trimmed();
    if(m_creating&&m_kind==ProjectKind::DrawingOnly)shot.durationFrames=1;
    auto &sheet=shot.sheet;sheet.left=m_left->value();sheet.right=m_right->value();sheet.top=m_top->value();sheet.bottom=m_bottom->value();sheet.header=m_header->value();sheet.overscan=m_overscan->value();sheet.safeArea=m_safe->isChecked();sheet.showHeader=m_showHeader->isChecked();sheet.showGuides=m_showGuides->isChecked();
    if(m_creating)for(auto &track:shot.animationTracks)for(auto &exposure:track.exposures)
        exposure.durationFrames=m_templateId=="storyboard"?shot.durationFrames:std::min(exposure.durationFrames,shot.durationFrames-exposure.startFrame);
    return project;
}
bool ProjectDialog::projectFolderRequested() const {
    return m_creating&&m_projectFolder&&m_projectFolder->isChecked()&&!m_path->text().trimmed().endsWith(".toonproj",Qt::CaseInsensitive);
}
QString ProjectDialog::filePath() const {
    const auto path=m_path->text().trimmed();const auto suffix=projectFolderRequested()?QString(".toonstudio"):QString(".toonproj");
    return path.endsWith(suffix,Qt::CaseInsensitive)?path:path+suffix;
}
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
        choosePath();if(m_path->text().trimmed().isEmpty())return;
    }
    accept();
}
} // namespace toon
