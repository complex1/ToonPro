#include "production_panel.h"
#include "document.h"
#include "icons.h"
#include "project_assets_panel.h"
#include "shots_panel.h"
#include "ui_metrics.h"
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace toon {
namespace {
QString stageLabel(const QString &key) {
    auto label = key; if (!label.isEmpty()) label[0] = label[0].toUpper();
    return label;
}
}
ProductionPanel::ProductionPanel(Document *document, std::function<bool()> prepare, QWidget *parent)
    : QWidget(parent), m_document(document) {
    setObjectName("productionPanel");
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(12,12,12,12); layout->setSpacing(8);
    auto *header = new QHBoxLayout; m_title = new QLabel("Project"); m_title->setObjectName("productionTitle");
    header->addWidget(m_title,1);
    m_open = new QPushButton(appIcon("brush"), "Open drawing"); m_open->setObjectName("productionOpenDrawing");
    m_open->setToolTip("Open the active artwork in the drawing workspace"); m_open->setAccessibleName(m_open->toolTip());
    m_open->setIconSize({ui::metrics::actionIcon,ui::metrics::actionIcon}); header->addWidget(m_open); layout->addLayout(header);
    m_tabs = new QTabWidget; m_tabs->setObjectName("productionTabs"); m_tabs->setIconSize({16,16}); layout->addWidget(m_tabs,1);
    auto *planning = new QWidget; auto *planningLayout = new QVBoxLayout(planning); planningLayout->setContentsMargins(4,8,4,4);
    auto *filters = new QHBoxLayout;
    m_episodeFilter = new QComboBox; m_episodeFilter->setObjectName("productionEpisodeFilter"); m_episodeFilter->setAccessibleName("Filter episodes");
    m_stageFilter = new QComboBox; m_stageFilter->setObjectName("productionStageFilter"); m_stageFilter->setAccessibleName("Filter production stage");
    m_stageFilter->addItem("All stages",QString()); for (const auto &stage: productionStages()) m_stageFilter->addItem(stageLabel(stage),stage);
    filters->addWidget(m_episodeFilter,1); filters->addWidget(m_stageFilter,1); filters->addStretch(); planningLayout->addLayout(filters);
    auto *split = new QSplitter; split->setObjectName("productionSplitter"); planningLayout->addWidget(split,1);
    m_shots = new ShotsPanel(document,[this,prepare]{ return finishEditing() && (!prepare || prepare()); }); split->addWidget(m_shots);
    auto *details = new QWidget; auto *form = new QFormLayout(details); form->setContentsMargins(10,4,4,4); form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_target = new QLabel; m_target->setWordWrap(true); m_target->setObjectName("productionTarget"); form->addRow(m_target);
    m_episodeLabel = new QLabel("Episode");
    m_episode = new QLineEdit; m_episode->setObjectName("productionEpisode"); m_episode->setMaxLength(100); m_episode->setPlaceholderText("Episode 01"); m_episode->setAccessibleName("Episode");
    form->addRow(m_episodeLabel,m_episode);
    m_stage = new QComboBox; m_stage->setObjectName("productionStage"); m_stage->setAccessibleName("Production stage");
    for (const auto &stage : productionStages()) m_stage->addItem(stageLabel(stage),stage);
    form->addRow("Stage",m_stage);
    m_notes = new QPlainTextEdit; m_notes->setObjectName("productionNotes"); m_notes->setAccessibleName("Shot or artwork production notes");
    m_notes->setPlaceholderText("Action, dialogue, camera notes or painting brief"); m_notes->setMinimumHeight(120); m_notes->installEventFilter(this);
    form->addRow("Notes",m_notes);
    auto *notesSave = new QPushButton(appIcon("check"),"Save notes"); notesSave->setObjectName("productionSaveNotes");
    notesSave->setToolTip("Save notes and production stage as one undoable change"); form->addRow(notesSave);
    auto *notesDiscard = new QPushButton("Revert notes"); notesDiscard->setObjectName("productionRevertNotes");
    notesDiscard->setToolTip("Discard the unfinished notes and reload this shot's saved production details");form->addRow(notesDiscard);
    auto *scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setWidget(details); scroll->setMinimumWidth(220); split->addWidget(scroll); split->setStretchFactor(0,3); split->setStretchFactor(1,2);
    m_tabs->addTab(planning,appIcon("clapperboard"),"Shots");
    m_assets = new ProjectAssetsPanel(document,prepare,this); m_tabs->addTab(m_assets,appIcon("folder-open"),"Assets");
    m_status = new QLabel; m_status->setObjectName("productionStatus"); m_status->setWordWrap(true); m_status->setMinimumHeight(fontMetrics().height()+4); layout->addWidget(m_status);
    connect(m_open,&QPushButton::clicked,this,[this,prepare]{if(finishEditing() && m_assets->finishEditing() && (!prepare||prepare()))emit drawingRequested();});
    connect(m_shots,&ShotsPanel::exportRequested,this,&ProductionPanel::exportRequested);
    connect(m_episode,&QLineEdit::textEdited,this,[this]{if(!m_refreshing)m_dirty=true;});
    connect(m_notes,&QPlainTextEdit::textChanged,this,[this]{if(!m_refreshing)m_dirty=true;});
    connect(m_episode,&QLineEdit::editingFinished,this,[this]{if(!m_refreshing)finishEditing();});
    connect(m_stage,&QComboBox::activated,this,[this]{m_dirty=true;finishEditing();});
    connect(notesSave,&QPushButton::clicked,this,[this]{finishEditing();});
    connect(notesDiscard,&QPushButton::clicked,this,[this]{m_dirty=false;status({});refresh();});
    const auto filter = [this]{if(!m_refreshing)m_shots->setProductionFilters(m_episodeFilter->currentData().toString(),m_stageFilter->currentData().toString());};
    connect(m_episodeFilter,&QComboBox::currentIndexChanged,this,filter);
    connect(m_stageFilter,&QComboBox::currentIndexChanged,this,filter);
    connect(document,&Document::loaded,this,[this]{m_dirty=false;m_id.clear();m_episodeFilter->setCurrentIndex(0);m_stageFilter->setCurrentIndex(0);m_shots->setProductionFilters({},{});refresh();});
    connect(document,&Document::changed,this,[this]{if(isVisible()&&!m_committing)refresh();});
    connect(document,&Document::shotChanged,this,[this]{if(isVisible()&&!m_committing)refresh();});
    refresh();
}
void ProductionPanel::showEvent(QShowEvent *event) { QWidget::showEvent(event); refresh(); }
bool ProductionPanel::eventFilter(QObject *watched,QEvent *event) {
    if(watched==m_notes && event->type()==QEvent::FocusOut)
        QTimer::singleShot(0,this,[this]{if(m_dirty)finishEditing();});
    return QWidget::eventFilter(watched,event);
}
void ProductionPanel::status(const QString &text) { m_status->setText(text); m_status->setAccessibleName(text); }
bool ProductionPanel::finishEditing() {
    if (!m_dirty) return true;
    if (!m_document || !m_document->isOpen() || m_document->editingGestureActive()) {
        status("Finish the active drawing edit before saving production notes."); return false;
    }
    const Shot *target = nullptr;
    for(const auto &shot:m_document->project().shots)if(shot.id==m_id){target=&shot;break;}
    if(!target || target->episode!=m_originalEpisode || target->productionStage!=m_originalStage || target->productionNotes!=m_originalNotes){
        status("This shot changed while notes were being edited. Copy your draft before reloading the project.");return false;
    }
    if(m_notes->toPlainText().size()>16000){status("Keep production notes within 16,000 characters.");return false;}
    const QScopedValueRollback guard(m_committing,true);
    if(!m_document->setShotProduction(m_id,m_episode->text(),m_stage->currentData().toString(),m_notes->toPlainText())){
        status("Could not save production notes. Your draft is retained.");return false;
    }
    m_dirty=false; status({}); refresh(); return true;
}
void ProductionPanel::refresh() {
    if(!m_document || !m_document->isOpen()){m_open->setEnabled(false);return;}
    const auto &project=m_document->project();const auto &shot=m_document->activeShot();
    const QScopedValueRollback guard(m_refreshing,true);
    const bool drawing=project.kind==ProjectKind::DrawingOnly,series=project.kind==ProjectKind::Series;
    m_title->setText(project.movie+" · "+projectKindName(project.kind));m_title->setTextFormat(Qt::PlainText);
    m_tabs->setTabText(0,drawing?"Artworks":"Shots");
    m_episode->setVisible(series);m_episodeLabel->setVisible(series);m_episodeFilter->setVisible(series);
    m_open->setText(drawing?"Open drawing":"Open shot");m_open->setEnabled(true);
    const QString previous=m_episodeFilter->currentData().toString();QStringList episodes;
    for(const auto &s:project.shots)if(!s.episode.isEmpty()&&!episodes.contains(s.episode))episodes.append(s.episode);
    episodes.sort(Qt::CaseInsensitive);m_episodeFilter->clear();m_episodeFilter->addItem("All episodes",QString());
    for(const auto &episode:episodes)m_episodeFilter->addItem(episode,episode);
    m_episodeFilter->setCurrentIndex(qMax(0,m_episodeFilter->findData(previous)));
    m_shots->setProductionFilters(series?m_episodeFilter->currentData().toString():QString(),m_stageFilter->currentData().toString());
    if(m_dirty)return;
    m_id=shot.id;m_originalEpisode=shot.episode;m_originalStage=shot.productionStage;m_originalNotes=shot.productionNotes;
    m_target->setText(shot.number+" · "+shot.name);m_target->setTextFormat(Qt::PlainText);
    m_episode->setText(shot.episode);m_stage->setCurrentIndex(m_stage->findData(shot.productionStage));m_notes->setPlainText(shot.productionNotes);
}
void ProductionPanel::setProjectRoot(const QString &root){m_assets->setProjectRoot(root);}
void ProductionPanel::showAssets(){if(finishEditing())m_tabs->setCurrentWidget(m_assets);}
}
