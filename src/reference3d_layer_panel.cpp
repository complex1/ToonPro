#include "reference3d_layer_panel.h"
#include "document.h"
#include "icons.h"
#include "reference3d_canvas.h"
#include "reference3d_layer_serialization.h"
#include "reference3d_review.h"
#include "ui_metrics.h"
#include <QHBoxLayout>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace toon {
namespace {
QToolButton *button(const QString &name,const QString &icon,const QString &description,QWidget *parent){auto *b=new QToolButton(parent);b->setObjectName(name);b->setIcon(appIcon(icon));b->setIconSize({ui::metrics::actionIcon,ui::metrics::actionIcon});b->setMinimumSize(ui::metrics::actionTarget,ui::metrics::actionTarget);b->setToolTip(description);b->setAccessibleName(description);b->setAutoRaise(true);return b;}
}
Reference3DLayerPanel::Reference3DLayerPanel(Document *document,Reference3DCanvasController *controller,QWidget *parent)
    :QWidget(parent),m_document(document),m_controller(controller){
    setObjectName("reference3DLayerPanel");setMinimumWidth(250);auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(ui::metrics::controlGap);
    auto *actions=new QHBoxLayout;actions->setContentsMargins(ui::metrics::panelMargin,0,ui::metrics::panelMargin,0);
    m_newLayer=button("referenceNewLayer","box","New 3D reference layer",this);m_newLayer->setText("New 3D");m_newLayer->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);actions->addWidget(m_newLayer);
    m_drawAbove=button("referenceDrawAbove","paintbrush","Add a drawing layer above this reference",this);m_drawAbove->setText("Draw above");m_drawAbove->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);actions->addWidget(m_drawAbove);actions->addStretch();layout->addLayout(actions);
    m_modes=new QTabBar(this);m_modes->setObjectName("referenceCanvasModes");m_modes->setAccessibleName("3D canvas edit mode");m_modes->setExpanding(true);m_modes->setIconSize({16,16});
    const QStringList names{"Translate","Rotate","Scale","Pose","Camera"},icons{"move","rotate-ccw","maximize-2","toon-character","video"};
    for(int i=0;i<names.size();++i){m_modes->addTab(appIcon(icons[i]),{});m_modes->setTabToolTip(i,names[i]+" on the drawing canvas");m_modes->setAccessibleTabName(i,names[i]);}layout->addWidget(m_modes);
    m_axes=new QTabBar(this);m_axes->setObjectName("referenceCanvasAxes");m_axes->setAccessibleName("3D manipulation axis");m_axes->setExpanding(true);
    for(const auto &name:{QString("All"),QString("X"),QString("Y"),QString("Z")}){const int i=m_axes->addTab(name);m_axes->setTabToolTip(i,i?name+" axis":"Free movement / uniform scale");m_axes->setAccessibleTabName(i,i?name+" axis":"All axes");}layout->addWidget(m_axes);
    m_editor=new Reference3DReviewWindow(this);m_editor->setCanvasPanelMode(document?document->history():nullptr,document?QSize(document->project().width,document->project().height):QSize());layout->addWidget(m_editor,1);
    connect(m_modes,&QTabBar::currentChanged,this,[this](int index){if(m_syncing||!m_controller)return;finishEditing();m_controller->setMode(Reference3DCanvasMode(index));if(auto *tabs=m_editor->findChild<QTabWidget*>("referencePropertyTabs"))tabs->setCurrentIndex(index==3?1:index==4?4:0);});
    connect(m_axes,&QTabBar::currentChanged,this,[this](int index){if(m_syncing||!m_controller)return;finishEditing();m_controller->setAxis(index-1);});
    connect(m_newLayer,&QToolButton::clicked,this,[this]{finishEditing();if(!m_document)return;ReferenceScene scene;scene.name="3D reference";QString error;const auto id=m_document->addReference3DLayer(scene,{},&error);if(id.isEmpty())emit warning(error);synchronize();});
    connect(m_drawAbove,&QToolButton::clicked,this,[this]{finishEditing();if(!m_document||m_boundLayer.isEmpty())return;const auto before=m_document->activeShot().layers[m_document->activeLayer()].id;m_document->addLayer();const auto &layer=m_document->activeShot().layers[m_document->activeLayer()];if(layer.id!=before&&layer.kind==LayerKind::Raster)emit drawAboveRequested();else emit warning("The drawing layer could not be added above this reference.");});
    connect(m_editor,&Reference3DReviewWindow::editBegan,this,&Reference3DLayerPanel::beginEditorEdit);
    connect(m_editor,&Reference3DReviewWindow::editPreviewed,this,&Reference3DLayerPanel::previewEditorEdit);
    connect(m_editor,&Reference3DReviewWindow::editEnded,this,&Reference3DLayerPanel::endEditorEdit);
    connect(m_editor,&Reference3DReviewWindow::selectionChanged,this,[this](int object,int joint){if(m_syncing||!m_controller)return;m_controller->selectObject(object);m_controller->selectJoint(joint);if(joint>=0)m_controller->setMode(Reference3DCanvasMode::Pose);});
    connect(m_editor,&Reference3DReviewWindow::sceneChanged,this,[this]{if(m_syncing||m_editing||!m_document)return;const auto &scene=m_editor->scene();QString error;const auto bound=activeReferenceId();if(!bound.isEmpty()){if(!m_document->setReference3DScene(bound,scene,&error)&&!error.isEmpty())emit warning(error);}else if(!scene.instances.isEmpty()){const auto id=m_document->addReference3DLayer(scene,{},&error);if(id.isEmpty()&&!error.isEmpty())emit warning(error);}synchronize();});
    if(document){
        connect(document,&Document::changed,this,&Reference3DLayerPanel::synchronize);connect(document,&Document::selectionChanged,this,&Reference3DLayerPanel::synchronize);connect(document,&Document::frameChanged,this,&Reference3DLayerPanel::synchronize);
        connect(document,&Document::loaded,this,[this]{m_editing=m_creating=m_rejected=false;synchronize();});
    }
    if(controller){connect(controller,&Reference3DCanvasController::selectionChanged,this,[this](int object,int joint){if(m_syncing||m_editing)return;QScopedValueRollback<bool> guard(m_syncing,true);m_editor->selectObject(object);m_editor->selectJoint(joint);});connect(controller,&Reference3DCanvasController::modeChanged,this,&Reference3DLayerPanel::synchronize);}
    if(auto *tabs=m_editor->findChild<QTabWidget*>("referencePropertyTabs"))connect(tabs,&QTabWidget::currentChanged,this,[this](int index){if(m_syncing||!m_controller||!m_controller->available())return;if(index==1)m_controller->setMode(Reference3DCanvasMode::Pose);else if(index==4)m_controller->setMode(Reference3DCanvasMode::Camera);});
    synchronize();
}
Reference3DLayerPanel::~Reference3DLayerPanel(){
    disconnect(m_editor,nullptr,this,nullptr);if(m_document)disconnect(m_document,nullptr,this,nullptr);if(m_controller)disconnect(m_controller,nullptr,this,nullptr);
    if(m_editing&&m_document&&m_document->reference3DEditActive())m_document->cancelReference3DEdit();
}
QString Reference3DLayerPanel::activeReferenceId()const{
    if(!m_document||!m_document->isOpen())return {};const auto &layers=m_document->activeShot().layers;const int at=m_document->activeLayer();return at>=0&&at<layers.size()&&layers[at].reference3d?layers[at].id:QString();
}
void Reference3DLayerPanel::synchronize(){
    if(m_syncing||m_editing)return;QScopedValueRollback<bool> guard(m_syncing,true);const bool open=m_document&&m_document->isOpen();const auto previousBinding=m_boundLayer;m_boundLayer=activeReferenceId();
    const bool bound=!m_boundLayer.isEmpty();ReferenceScene scene;if(bound)scene=*m_document->activeShot().layers[m_document->activeLayer()].reference3d;
    const bool editable=open&&(!bound||(layerCanEdit(m_document->activeShot().layers,m_document->activeLayer())&&!layerPositionLocked(m_document->activeShot().layers,m_document->activeLayer())));m_editor->setEnabled(editable);m_newLayer->setEnabled(open);m_drawAbove->setEnabled(bound);
    if(open)m_editor->setCanvasPanelMode(m_document->history(),{m_document->project().width,m_document->project().height});
    const int object=m_controller&&bound?m_controller->selectedObject():-1,joint=m_controller&&bound?m_controller->selectedJoint():-1;
    // Unrelated paint/layer changes must not cancel a model being prepared in
    // the library. Rebind only when the actual source/context changed.
    if(previousBinding!=m_boundLayer||!referenceLayerScenesEqual(scene,m_editor->scene())||object!=m_editor->selectedObject()||joint!=m_editor->selectedJoint())m_editor->syncCanvasScene(scene,object,joint);
    m_editor->setWindowModified(false);
    if(m_controller){QSignalBlocker modes(m_modes),axes(m_axes);m_modes->setCurrentIndex(int(m_controller->mode()));m_axes->setCurrentIndex(m_controller->axis()+1);}
    m_modes->setEnabled(bound);m_axes->setEnabled(bound&&m_controller&&m_controller->mode()!=Reference3DCanvasMode::Camera);
}
void Reference3DLayerPanel::beginEditorEdit(){
    if(m_syncing)return;m_editing=true;m_rejected=false;m_creating=false;
    if(!m_document||!m_document->isOpen()){m_rejected=true;emit warning("Open a drawing before placing a 3D reference.");return;}
    const auto active=activeReferenceId();if(active!=m_boundLayer){m_rejected=true;emit warning("The selected layer changed. Choose the model again.");return;}
    if(active.isEmpty()){m_creating=true;return;}QString error;if(!m_document->beginReference3DEdit(active,&error)){m_rejected=true;emit warning(error);}
}
void Reference3DLayerPanel::previewEditorEdit(){
    if(m_syncing||!m_editing||m_rejected||m_creating||!m_document)return;QString error;if(!m_document->previewReference3DScene(m_editor->scene(),&error)){m_rejected=true;emit warning(error);}
}
void Reference3DLayerPanel::endEditorEdit(bool cancelled){
    if(m_syncing||!m_editing)return;
    const int selectedObject=m_editor->selectedObject(),selectedJoint=m_editor->selectedJoint();
    if(m_document){if(m_creating){if(!cancelled&&!m_rejected&&!m_editor->scene().instances.isEmpty()){QString error;const auto id=m_document->addReference3DLayer(m_editor->scene(),{},&error);if(id.isEmpty())emit warning(error);}}
        else if(m_document->reference3DEditActive()){if(cancelled||m_rejected)m_document->cancelReference3DEdit();else{QString error;if(!m_document->previewReference3DScene(m_editor->scene(),&error)){m_document->cancelReference3DEdit();emit warning(error);}else m_document->commitReference3DEdit();}}}
    m_editing=m_creating=m_rejected=false;if(m_controller&&!cancelled){m_controller->selectObject(selectedObject);m_controller->selectJoint(selectedJoint);}synchronize();
}
void Reference3DLayerPanel::cancelEditing(){
    if(m_controller)m_controller->finishInteraction(true);m_editor->cancelPendingEdits();if(m_editing)endEditorEdit(true);
}
void Reference3DLayerPanel::finishEditing(){
    if(m_controller)m_controller->finishInteraction();m_editor->finishPendingEdits();if(m_editing)endEditorEdit(false);
}
} // namespace toon
