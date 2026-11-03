#include "symbol_editor_dialog.h"
#include "brush_library.h"
#include "canvas.h"
#include "document.h"
#include "drawing_panels.h"
#include "drawing_tool_controller.h"
#include "icons.h"
#include "property_row.h"
#include "symbols.h"
#include "timeline_controller.h"
#include "timeline_panel.h"
#include "tool_catalog.h"
#include "tool_navigation_panel.h"
#include "tool_session.h"
#include "value_slider.h"
#include <QApplication>
#include <QAction>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScopedValueRollback>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace toon {
namespace {
QToolButton *editorButton(QWidget *parent,const QString &id,const QString &icon,const QString &name){
    auto *button=new QToolButton(parent);button->setObjectName(id);button->setIcon(appIcon(icon));button->setIconSize({16,16});
    button->setFixedSize(28,28);button->setAutoRaise(true);button->setToolTip(name);button->setAccessibleName(name);return button;
}
QScrollArea *scrollPage(QWidget *content){
    auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);scroll->setWidget(content);return scroll;
}
}
SymbolEditorDialog::SymbolEditorDialog(Document *document,const QString &symbolId,Canvas *sourceCanvas,ToolCatalog *catalog,QWidget *parent)
    :QDialog(parent),m_source(document),m_symbolId(symbolId),m_canvas(new Canvas(this)),m_timeline(new TimelineController(m_canvas,this)){
    m_timeline->setProperty("symbolEditor",true);
    setObjectName("symbolEditorDialog");setWindowTitle("Edit symbol");setModal(true);resize(1180,760);setMinimumSize(840,540);
    auto *outer=new QVBoxLayout(this);outer->setContentsMargins(8,6,8,6);outer->setSpacing(5);
    QString error;Project draft;
    if(document&&document->isOpen()){
        if(const auto *definition=findSymbol(document->project(),symbolId)){
            m_original=definition->content;setWindowTitle("Symbols / "+definition->name);
            draft=editProjectForSymbol(document->project(),symbolId,&error);m_valid=error.isEmpty()&&!draft.shots.isEmpty();
        }else error="This symbol is no longer available.";
    }else error="Open a project before editing a symbol.";
    if(m_valid)m_canvas->setProject(draft);
    auto *header=new QHBoxLayout;header->setSpacing(5);
    auto *breadcrumb=new QLabel(windowTitle());breadcrumb->setObjectName("symbolEditorBreadcrumb");breadcrumb->setTextFormat(Qt::PlainText);
    breadcrumb->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);breadcrumb->setToolTip(windowTitle());header->addWidget(breadcrumb,1);
    m_duration=new ValueSlider("Symbol duration in frames",1,1000000,m_valid?m_original.durationFrames:1);m_duration->setObjectName("symbolDuration");
    m_duration->setSliderRange(1,240);auto *durationRow=new PropertyRow("Frames",m_duration);durationRow->setMaximumWidth(250);header->addWidget(durationRow);
    auto *undo=editorButton(this,"symbolEditorUndo","undo-2","Undo symbol edit");auto *redo=editorButton(this,"symbolEditorRedo","redo-2","Redo symbol edit");
    auto *undoAction=new QAction("Undo symbol edit",this);undoAction->setShortcut(QKeySequence::Undo);undoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    auto *redoAction=new QAction("Redo symbol edit",this);redoAction->setShortcut(QKeySequence::Redo);redoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    auto *history=m_canvas->document()->history();undoAction->setEnabled(history->canUndo());redoAction->setEnabled(history->canRedo());
    connect(history,&QUndoStack::canUndoChanged,undoAction,&QAction::setEnabled);connect(history,&QUndoStack::canRedoChanged,redoAction,&QAction::setEnabled);
    connect(undoAction,&QAction::triggered,this,[this]{if(m_timeline->prepareTimelineEdit())m_canvas->document()->history()->undo();});
    connect(redoAction,&QAction::triggered,this,[this]{if(m_timeline->prepareTimelineEdit())m_canvas->document()->history()->redo();});
    // Canvas scope leaves normal text-field Undo intact inside property editors.
    m_canvas->addAction(undoAction);m_canvas->addAction(redoAction);undo->setDefaultAction(undoAction);redo->setDefaultAction(redoAction);
    undoAction->setIcon(appIcon("undo-2"));redoAction->setIcon(appIcon("redo-2"));
    undo->setAccessibleName("Undo symbol edit");redo->setAccessibleName("Redo symbol edit");header->addWidget(undo);header->addWidget(redo);
    auto *fit=editorButton(this,"symbolEditorFit","scan","Fit symbol canvas");header->addWidget(fit);
    auto *timelineToggle=editorButton(this,"symbolEditorTimeline","clapperboard","Show symbol timeline");timelineToggle->setCheckable(true);timelineToggle->setChecked(m_valid&&m_original.durationFrames>1);header->addWidget(timelineToggle);
    outer->addLayout(header);
    auto *vertical=new QSplitter(Qt::Vertical);vertical->setObjectName("symbolEditorVertical");vertical->setChildrenCollapsible(false);outer->addWidget(vertical,1);
    auto *workspace=new QSplitter(Qt::Horizontal);workspace->setObjectName("symbolEditorWorkspace");workspace->setChildrenCollapsible(false);vertical->addWidget(workspace);
    QWidget *tools=nullptr;
    if(catalog){
        m_libraryDirectory=std::make_unique<QTemporaryDir>();
        if(m_libraryDirectory->isValid()&&QFile::copy(catalog->path(),m_libraryDirectory->filePath("tools.json"))){
            const auto sourceBrushes=QFileInfo(catalog->path()).dir().filePath("brushes.json");
            if(QFile::exists(sourceBrushes))QFile::copy(sourceBrushes,m_libraryDirectory->filePath("brushes.json"));
            auto *library=new BrushLibrary(m_libraryDirectory->filePath("brushes.json"),this);QString ignored;
            if(library->load(&ignored)){
                auto *localCatalog=new ToolCatalog(library,m_libraryDirectory->filePath("tools.json"),this);
                if(localCatalog->load(&ignored)){
                    for(const auto &tool:catalog->tools())if(localCatalog->tool(tool.id))localCatalog->updateToolState(tool.id,tool.state);
                    localCatalog->selectEntry(catalog->selectedEntryId());localCatalog->activateTool(catalog->activeToolId());
                    m_session=new ToolSession(m_canvas,localCatalog,this);
                    tools=new QWidget;auto *toolLayout=new QHBoxLayout(tools);toolLayout->setContentsMargins(0,0,0,0);toolLayout->setSpacing(3);
                    auto *groups=new ToolNavigationPanel(localCatalog,true);groups->setFixedWidth(38);groups->setPreviewDocument(m_canvas->document());
                    auto *list=new ToolNavigationPanel(localCatalog,false);list->setPreviewDocument(m_canvas->document());list->setIconsOnly(true);
                    toolLayout->addWidget(groups);toolLayout->addWidget(list,1);
                    // This dialog edits artwork. Library organization remains in its main workspace.
                    for(auto *button:tools->findChildren<QToolButton *>())if(button->objectName().endsWith("Add")||button->objectName().endsWith("Remove")||button->objectName().endsWith("More"))button->hide();
                }
            }
        }
    }
    if(!tools)tools=drawingPanel(m_canvas,"toolsPanel");
    tools->setObjectName("symbolEditorTools");tools->setMinimumWidth(140);tools->setMaximumWidth(280);workspace->addWidget(tools);
    workspace->addWidget(m_canvas);m_canvas->setObjectName("symbolEditorCanvas");m_canvas->setMinimumWidth(180);
    auto *inspectors=new QTabWidget;inspectors->setObjectName("symbolEditorInspectors");inspectors->setMinimumWidth(230);inspectors->setMaximumWidth(380);
    inspectors->setDocumentMode(true);inspectors->setIconSize({16,16});inspectors->setUsesScrollButtons(true);
    inspectors->addTab(drawingPanel(m_canvas,"propertiesPanel"),appIcon("sliders-horizontal"),"Properties");
    inspectors->addTab(drawingPanel(m_canvas,"layersPanel"),appIcon("layers"),"Layers");
    inspectors->addTab(scrollPage(drawingPanel(m_canvas,"colorsPanel")),appIcon("palette"),"Colors");
    auto *frameProperties=new FramePropertiesPanel(m_timeline);
    if(auto *tabs=frameProperties->findChild<QTabBar *>("framePropertiesTabs")){tabs->setTabVisible(2,false);tabs->setTabVisible(3,false);}
    inspectors->addTab(frameProperties,appIcon("clapperboard"),"Frames");
    const QStringList tabLabels{"Tool properties","Symbol layers","Drawing colors","Drawing and keyframe properties"};
    for(int i=0;i<inspectors->count();++i){inspectors->setTabToolTip(i,tabLabels[i]);inspectors->tabBar()->setAccessibleTabName(i,tabLabels[i]);}
    workspace->addWidget(inspectors);workspace->setStretchFactor(1,1);workspace->setSizes({190,680,260});
    auto *timelinePanel=new TimelinePanel(m_timeline);timelinePanel->setMinimumHeight(160);vertical->addWidget(timelinePanel);vertical->setStretchFactor(0,1);vertical->setSizes({530,180});
    for(const auto *id:{"timelineImportAudio","timelineAudioScrub","timelineAudioImportStatus"})if(auto *control=timelinePanel->findChild<QWidget *>(id))control->hide();
    timelinePanel->setVisible(timelineToggle->isChecked());
    m_error=new QLabel;m_error->setObjectName("symbolEditorError");m_error->setTextFormat(Qt::PlainText);m_error->setWordWrap(true);m_error->hide();outer->addWidget(m_error);
    m_buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);m_buttons->button(QDialogButtonBox::Save)->setText("Save changes");
    m_buttons->button(QDialogButtonBox::Save)->setObjectName("symbolEditorSave");m_buttons->button(QDialogButtonBox::Save)->setEnabled(m_valid);
    m_buttons->button(QDialogButtonBox::Cancel)->setObjectName("symbolEditorCancel");outer->addWidget(m_buttons);
    for(auto *button:m_buttons->buttons())if(auto *push=qobject_cast<QPushButton *>(button)){push->setAutoDefault(false);push->setDefault(false);}
    connect(m_buttons,&QDialogButtonBox::accepted,this,&SymbolEditorDialog::accept);connect(m_buttons,&QDialogButtonBox::rejected,this,&SymbolEditorDialog::reject);
    connect(fit,&QToolButton::clicked,m_canvas,&Canvas::fitSheet);
    connect(timelineToggle,&QToolButton::toggled,timelinePanel,&QWidget::setVisible);
    connect(m_duration,&ValueSlider::editingFinished,this,&SymbolEditorDialog::updateDuration);
    connect(m_duration,&ValueSlider::editingFinished,this,[this,timelineToggle]{if(m_canvas->hasProject()&&m_canvas->shot().durationFrames>1)timelineToggle->setChecked(true);});
    connect(m_canvas->document(),&Document::changed,this,&SymbolEditorDialog::refreshDuration);
    connect(m_canvas->document(),&Document::warning,this,&SymbolEditorDialog::showError);
    connect(m_canvas,&Canvas::actionBlocked,this,[this](const QString &message){showError(message);});
    if(m_canvas->tool()!=Canvas::Tool::Brush)m_canvas->setTool(Canvas::Tool::Brush);
    if(sourceCanvas){m_canvas->applyBrushSettings(sourceCanvas->brush,sourceCanvas->stabilization());m_canvas->setCanvasSmoothing(sourceCanvas->canvasSmoothing());}
    showError(error);
    QTimer::singleShot(0,this,[this]{m_canvas->fitSheet();m_canvas->setFocus(Qt::OtherFocusReason);});
}
SymbolEditorDialog::~SymbolEditorDialog(){delete m_session;}
void SymbolEditorDialog::showError(const QString &message){m_error->setText(message);m_error->setVisible(!message.isEmpty());}
void SymbolEditorDialog::refreshDuration(){if(m_refreshing||!m_valid||m_duration->editingActive())return;const QScopedValueRollback<bool> guard(m_refreshing,true);m_duration->setValueSilently(m_canvas->shot().durationFrames);}
void SymbolEditorDialog::updateDuration(){
    if(m_refreshing||!m_valid)return;
    const int frames=qRound(m_duration->value());if(frames==m_canvas->shot().durationFrames)return;
    if(!m_timeline->prepareTimelineEdit()){refreshDuration();return;}
    Project candidate=m_canvas->project();candidate.shots[0].durationFrames=frames;
    const auto error=candidate.validate();if(!error.isEmpty()){showError("Keep the duration long enough for existing drawings and keyframes. "+error);refreshDuration();return;}
    m_canvas->document()->replaceProject(candidate,"Change symbol duration");showError({});refreshDuration();
}
bool SymbolEditorDialog::applyChanges(){
    if(m_applying||!m_valid||!m_source)return false;
    const QScopedValueRollback<bool> guard(m_applying,true);
    m_duration->commitEditing();m_timeline->setPlaying(false);m_timeline->finishEditing();
    m_canvas->finishStroke();
    if(!m_canvas->resolvePendingTransform())return false;
    m_canvas->drawingTools()->finishGesture();m_canvas->document()->commitLayerPropertyEdit();
    if(m_canvas->document()->editingGestureActive()||m_canvas->drawingTools()->active()){
        showError("Finish or cancel the current drawing gesture before saving the symbol.");return false;
    }
    QString error;
    if(!updateSymbolContent(m_source,m_symbolId,m_canvas->shot(),m_original,&error)){showError(error);return false;}
    m_original=m_canvas->shot();m_canvas->document()->history()->setClean();showError({});return true;
}
void SymbolEditorDialog::accept(){if(applyChanges())QDialog::accept();}
void SymbolEditorDialog::reject(){m_timeline->setPlaying(false);m_timeline->cancelEditing();m_canvas->drawingTools()->cancel();m_canvas->document()->cancelStroke();QDialog::reject();}
void SymbolEditorDialog::done(int result){
    // Capture ownership before hiding the native modal. Destruction can leave
    // Qt temporarily without an active QWidget even while our app stays active.
    m_closedWithFocus=QApplication::activeWindow()==this;
    QDialog::done(result);
}
void SymbolEditorDialog::keyPressEvent(QKeyEvent *event){
    if(event->key()==Qt::Key_Escape&&m_canvas->hasProject()&&(m_canvas->document()->editingGestureActive()||m_canvas->drawingTools()->active()||m_canvas->holdShapePending())){
        m_timeline->cancelEditing();m_canvas->drawingTools()->cancel();m_canvas->document()->cancelStroke();event->accept();return;
    }
    QDialog::keyPressEvent(event);
}
} // namespace toon
