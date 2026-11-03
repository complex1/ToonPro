#include "symbol_library_panel.h"
#include "canvas.h"
#include "icons.h"
#include "layer_panel.h"
#include "property_row.h"
#include "symbols.h"
#include "symbol_clips.h"
#include "symbol_frame_picker_dialog.h"
#include "timeline_controller.h"
#include "value_slider.h"
#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace toon {
namespace {
class PlaybackTabs final : public QTabBar {
public:
    using QTabBar::QTabBar;
    QSize minimumSizeHint() const override { auto size=QTabBar::minimumSizeHint();size.setWidth(count()*28);return size; }
protected:
    QSize tabSizeHint(int index) const override {
        auto size = QTabBar::tabSizeHint(index); if (tabText(index).isEmpty()) size.setWidth(28); return size;
    }
    QSize minimumTabSizeHint(int index) const override { return tabSizeHint(index); }
    void resizeEvent(QResizeEvent *event) override {QTabBar::resizeEvent(event);updateLabels();}
    void changeEvent(QEvent *event) override {QTabBar::changeEvent(event);if(event->type()==QEvent::FontChange||event->type()==QEvent::StyleChange)updateLabels();}
    void tabInserted(int index) override {QTabBar::tabInserted(index);updateLabels();}
private:
    void updateLabels(){
        if(m_updating)return;
        const QStringList names{"Loop","Once","Still","Ping-pong","Poses"};int fullWidth=0;
        for(int i=0;i<count();++i)fullWidth+=QTabBar::tabSizeHint(i).width()+fontMetrics().horizontalAdvance(names.value(i))-fontMetrics().horizontalAdvance(tabText(i));
        const bool showNames=width()>=fullWidth;m_updating=true;
        for(int i=0;i<count();++i){const auto name=showNames?names.value(i):QString();if(tabText(i)!=name)setTabText(i,name);}
        m_updating=false;
    }
    bool m_updating=false;
};
QToolButton *symbolButton(QWidget *parent, const QString &id, const QString &icon, const QString &label) {
    auto *button = new QToolButton(parent);
    button->setObjectName(id); button->setIcon(appIcon(icon)); button->setIconSize({16,16});
    button->setFixedSize(28,28); button->setToolTip(label); button->setAccessibleName(label);
    button->setAutoRaise(true); return button;
}
const RasterLayer *activeSymbol(const Canvas *canvas) {
    if (!canvas->hasProject()) return nullptr;
    const int index = canvas->document()->activeLayer();
    const auto &layers = canvas->shot().layers;
    if (index < 0 || index >= layers.size() || layers[index].kind != LayerKind::Symbol || !layers[index].symbol) return nullptr;
    return &layers[index];
}
}

SymbolLibraryPanel::SymbolLibraryPanel(Canvas *canvas, TimelineController *timeline, ToolCatalog *, QWidget *parent)
    : QWidget(parent), m_canvas(canvas), m_timeline(timeline) {
    setObjectName("symbolLibraryContent"); setMinimumWidth(0);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(6,5,6,5); layout->setSpacing(4);
    auto *actions = new QHBoxLayout; actions->setSpacing(2);
    m_add = symbolButton(this,"symbolNew","plus","New symbol");
    m_convert = symbolButton(this,"symbolConvert","layers","Convert selected layers to a symbol");
    m_insert = symbolButton(this,"symbolInsert","square-plus","Add an instance to this shot");
    m_edit = symbolButton(this,"symbolEdit","pencil","Edit symbol artwork");
    auto *more = symbolButton(this,"symbolMore","ellipsis","More symbol actions");
    for (auto *button : {m_add,m_convert,m_insert,m_edit}) actions->addWidget(button);
    actions->addStretch(); actions->addWidget(more); layout->addLayout(actions);
    auto *menu = new QMenu(more); menu->setObjectName("symbolMenu");
    auto action = [&](const QString &id,const QString &icon,const QString &label) {
        auto *result=menu->addAction(appIcon(icon),label); result->setObjectName(id); return result;
    };
    m_rename=action("symbolRename","pencil","Rename symbol");
    m_duplicate=action("symbolDuplicate","copy","Duplicate symbol");
    m_remove=action("symbolDelete","trash","Delete unused symbol");
    menu->addSeparator();
    m_unique=action("symbolMakeUnique","copy","Make selected instance unique");
    m_break=action("symbolBreakApart","split","Break apart at current frame");
    more->setMenu(menu); more->setPopupMode(QToolButton::InstantPopup);
    m_search=new QLineEdit; m_search->setObjectName("symbolSearch"); m_search->setPlaceholderText("Search symbols");
    m_search->setAccessibleName("Search symbols"); m_search->setClearButtonEnabled(true); layout->addWidget(m_search);
    m_list=new QListWidget; m_list->setObjectName("symbolList"); m_list->setAccessibleName("Movie symbol library");
    m_list->setIconSize({20,20}); m_list->setTextElideMode(Qt::ElideRight); m_list->setMinimumHeight(72);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); layout->addWidget(m_list,1);
    m_empty=new QLabel("Create a symbol or convert selected layers."); m_empty->setObjectName("symbolEmpty");
    m_empty->setWordWrap(true); layout->addWidget(m_empty);
    m_instance=new QWidget; m_instance->setObjectName("symbolInstanceProperties");
    auto *properties=new QVBoxLayout(m_instance); properties->setContentsMargins(0,4,0,0); properties->setSpacing(4);
    auto *instanceHeader=new QHBoxLayout;
    m_instanceName=new QLabel; m_instanceName->setTextFormat(Qt::PlainText); m_instanceName->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    m_transform=symbolButton(this,"symbolTransform","move","Move, resize or rotate this instance");
    auto *clipProperties=symbolButton(this,"symbolClipPropertiesButton","sliders-horizontal","Open symbol clip timing and slip controls");
    auto *pickFrame=symbolButton(this,"symbolPickFrame","image","Choose a symbol frame visually");
    instanceHeader->addWidget(m_instanceName,1); instanceHeader->addWidget(pickFrame); instanceHeader->addWidget(clipProperties); instanceHeader->addWidget(m_transform); properties->addLayout(instanceHeader);
    clipProperties->setVisible(timeline); pickFrame->setVisible(timeline);
    connect(clipProperties,&QToolButton::clicked,this,[this]{const auto id=activeInstanceId();if(m_timeline&&!id.isEmpty()&&prepare())m_timeline->selectSymbolClip(id);});
    connect(pickFrame,&QToolButton::clicked,this,[this]{const auto id=activeInstanceId();if(m_timeline&&!id.isEmpty()&&prepare())openSymbolFramePicker(m_timeline,id,this);});
    m_playback=new PlaybackTabs; m_playback->setProperty("compactDrawingTabs",true); m_playback->setObjectName("symbolPlayback"); m_playback->setDrawBase(false); m_playback->setExpanding(true);
    const QStringList modes{"Loop","Once","Still","Ping-pong","Poses"};
    const QStringList icons{"repeat","play","image","arrow-left-right","toon-symbol-pose"};
    for(int i=0;i<modes.size();++i){m_playback->addTab(appIcon(icons[i]),modes[i]);m_playback->setTabToolTip(i,modes[i]+" playback");m_playback->setAccessibleTabName(i,modes[i]+" playback");}
    m_playback->setTabToolTip(1,"Play once, then hold the last frame");
    m_playback->setTabToolTip(2,"Hold the chosen first frame");
    m_playback->setTabToolTip(3,"Play forwards and backwards repeatedly");
    m_playback->setIconSize({14,14}); m_playback->setUsesScrollButtons(false); properties->addWidget(m_playback);
    m_start=new ValueSlider("Instance start frame",1,1000000,1); m_start->setObjectName("symbolStartFrame"); m_start->setSliderRange(1,240);
    m_first=new ValueSlider("First symbol frame",1,1,1); m_first->setObjectName("symbolFirstFrame");
    m_speed=new ValueSlider("Playback speed in percent",1,10000,100); m_speed->setObjectName("symbolSpeed"); m_speed->setSliderRange(1,400);
    properties->addWidget(new PropertyRow("Start",m_start)); properties->addWidget(new PropertyRow("First frame",m_first));
    properties->addWidget(new PropertyRow("Speed",m_speed)); layout->addWidget(m_instance);
    m_error=new QLabel; m_error->setObjectName("symbolError"); m_error->setTextFormat(Qt::PlainText); m_error->setWordWrap(true); m_error->hide(); layout->addWidget(m_error);
    connect(m_search,&QLineEdit::textChanged,this,&SymbolLibraryPanel::filter);
    connect(m_list,&QListWidget::currentItemChanged,this,[this]{if(!m_refreshing)refresh();});
    connect(m_list,&QListWidget::itemDoubleClicked,this,[this]{edit();});
    connect(m_add,&QToolButton::clicked,this,&SymbolLibraryPanel::createSymbol);
    connect(m_convert,&QToolButton::clicked,this,[this]{
        const auto *layers=window()->findChild<LayerPanel *>();
        QStringList ids=layers?layers->selectedLayerIds():QStringList{};
        if(ids.isEmpty()&&m_canvas->hasProject()&&m_canvas->document()->activeLayer()>=0)ids<<m_canvas->shot().layers[m_canvas->document()->activeLayer()].id;
        convertLayers(ids);
    });
    connect(m_insert,&QToolButton::clicked,this,&SymbolLibraryPanel::insert);
    connect(m_edit,&QToolButton::clicked,this,&SymbolLibraryPanel::edit);
    connect(m_rename,&QAction::triggered,this,&SymbolLibraryPanel::rename);
    connect(m_duplicate,&QAction::triggered,this,&SymbolLibraryPanel::duplicate);
    connect(m_remove,&QAction::triggered,this,&SymbolLibraryPanel::remove);
    connect(m_unique,&QAction::triggered,this,&SymbolLibraryPanel::makeUnique);
    connect(m_break,&QAction::triggered,this,&SymbolLibraryPanel::breakApart);
    connect(m_transform,&QToolButton::clicked,this,[this]{if(prepare())emit transformRequested();});
    connect(m_playback,&QTabBar::currentChanged,this,[this]{if(!m_refreshing)commitPlayback();});
    for(auto *field:{m_start,m_first,m_speed}){
        connect(field,&ValueSlider::editingStarted,this,[this]{if(!m_refreshing){m_editLayerId=activeInstanceId();if(m_timeline)m_timeline->setPendingEdits(this,true);}});
        connect(field,&ValueSlider::editingFinished,this,&SymbolLibraryPanel::commitPlayback);
        connect(field,&ValueSlider::editingCancelled,this,&SymbolLibraryPanel::cancelEditors);
    }
    connect(canvas->document(),&Document::changed,this,&SymbolLibraryPanel::refresh);
    connect(canvas->document(),&Document::loaded,this,&SymbolLibraryPanel::refresh);
    connect(canvas->document(),&Document::selectionChanged,this,&SymbolLibraryPanel::refresh);
    if(timeline){
        connect(timeline,&TimelineController::editResolutionRequested,this,&SymbolLibraryPanel::commitEditors);
        connect(timeline,&TimelineController::editCancellationRequested,this,&SymbolLibraryPanel::cancelEditors);
        connect(timeline,&TimelineController::changed,this,&SymbolLibraryPanel::refresh);
    }
    refresh();
}

QString SymbolLibraryPanel::currentSymbolId() const {const auto *item=m_list->currentItem();return item?item->data(Qt::UserRole).toString():QString{};}
QString SymbolLibraryPanel::activeInstanceId() const {const auto *layer=activeSymbol(m_canvas);return layer?layer->id:QString{};}
void SymbolLibraryPanel::setCurrentSymbol(const QString &id){for(int i=0;i<m_list->count();++i)if(m_list->item(i)->data(Qt::UserRole).toString()==id){m_list->setCurrentRow(i);return;}}
void SymbolLibraryPanel::filter(){
    int shown=0;QListWidgetItem *first=nullptr;
    for(int i=0;i<m_list->count();++i){auto *item=m_list->item(i);const bool visible=item->text().contains(m_search->text(),Qt::CaseInsensitive);item->setHidden(!visible);shown+=visible;if(visible&&!first)first=item;}
    // An invisible selection must never leave Insert targeting a hidden result.
    if((m_list->currentItem()&&m_list->currentItem()->isHidden())||(!m_list->currentItem()&&first))m_list->setCurrentItem(first);
    m_empty->setVisible(!shown);m_empty->setText(m_list->count()?"No matching symbols.":"Create a symbol or convert selected layers.");
}
void SymbolLibraryPanel::showError(const QString &message){m_error->setText(message);m_error->setVisible(!message.isEmpty());}

void SymbolLibraryPanel::refresh(){
    if(m_refreshing)return;
    const QScopedValueRollback<bool> guard(m_refreshing,true);
    const bool open=m_canvas->hasProject(); const auto *instance=activeSymbol(m_canvas);
    if(!m_editLayerId.isEmpty()&&m_editLayerId!=activeInstanceId())cancelEditors();
    const bool editing=m_start->editingActive()||m_first->editingActive()||m_speed->editingActive();
    QString chosen=currentSymbolId();
    const QString instanceSymbol=instance?instance->symbol->symbolId:QString{};
    if(instanceSymbol!=m_lastInstanceSymbol&&!instanceSymbol.isEmpty())chosen=instanceSymbol;
    m_lastInstanceSymbol=instanceSymbol;
    const QSignalBlocker block(m_list);
    const auto &definitions=m_canvas->project().symbols;
    const bool listChanged=!open||m_displayedDefinitions.constData()!=definitions.constData()||m_displayedShots.constData()!=m_canvas->project().shots.constData();
    if(listChanged){
    m_useCounts.clear();if(open)for(const auto *shot:projectArtworkShots(m_canvas->project()))for(const auto &layer:shot->layers)if(layer.symbol)++m_useCounts[layer.symbol->symbolId];
    for(int i=m_list->count()-1;i>=0;--i)if(!open||!findSymbol(m_canvas->project(),m_list->item(i)->data(Qt::UserRole).toString()))delete m_list->takeItem(i);
    if(open)for(const auto &definition:definitions){
        QListWidgetItem *item=nullptr;for(int i=0;i<m_list->count();++i)if(m_list->item(i)->data(Qt::UserRole).toString()==definition.id){item=m_list->item(i);break;}
        if(!item){item=new QListWidgetItem(appIcon("toon-symbol"),definition.name,m_list);item->setData(Qt::UserRole,definition.id);item->setSizeHint({0,30});}
        item->setText(definition.name);item->setToolTip(QString("%1\n%2 frames · %3 linked instances").arg(definition.name).arg(definition.content.durationFrames).arg(m_useCounts.value(definition.id)));
    }
    m_displayedDefinitions=definitions;m_displayedShots=m_canvas->project().shots;
    }
    if(!chosen.isEmpty())setCurrentSymbol(chosen);else if(m_list->count()&&!m_list->currentItem())m_list->setCurrentRow(0);
    filter();
    const auto *selected=open?findSymbol(m_canvas->project(),currentSymbolId()):nullptr;
    const bool available=open&&!m_canvas->document()->editingGestureActive()&&(!m_timeline||!m_timeline->playing());
    m_add->setEnabled(available&&definitions.size()<maxProjectSymbols);m_convert->setEnabled(available&&!m_canvas->shot().layers.isEmpty());
    for(auto *button:{m_insert,m_edit})button->setEnabled(available&&selected);
    for(auto *action:{m_rename,m_duplicate})action->setEnabled(available&&selected);
    const int uses=selected?m_useCounts.value(selected->id):0;
    m_remove->setEnabled(available&&selected&&uses==0);m_remove->setToolTip(uses?"Used symbols must be detached or removed from their shots first.":"Delete unused symbol");
    const bool editable=available&&instance&&symbolClipEditError(m_canvas->shot(),instance->id).isEmpty();
    m_unique->setEnabled(editable);m_break->setEnabled(editable);m_instance->setVisible(instance);m_instance->setEnabled(editable);
    if(!instance)return;
    const auto *definition=findSymbol(m_canvas->project(),instanceSymbol);
    m_instanceName->setText(definition?definition->name:QStringLiteral("Missing symbol"));m_instanceName->setToolTip(m_instanceName->text());
    m_transform->setEnabled(editable&&!layerPositionLocked(m_canvas->shot().layers,m_canvas->document()->activeLayer()));
    if(!editing){
        const int ownerDuration=m_canvas->shot().durationFrames,symbolDuration=definition?definition->content.durationFrames:1;
        m_start->setRange(1,std::max({1,ownerDuration,instance->symbol->startFrame+1}));
        if(m_ownerDuration!=ownerDuration){m_ownerDuration=ownerDuration;m_start->setSliderRange(1,std::min(240,ownerDuration));}
        if(m_symbolDuration!=symbolDuration){m_symbolDuration=symbolDuration;m_first->setRange(1,symbolDuration);m_first->setSliderRange(1,std::min(240,symbolDuration));}
        m_start->setValueSilently(instance->symbol->startFrame+1);m_first->setValueSilently(instance->symbol->firstFrame+1);m_speed->setValueSilently(instance->symbol->speed*100);
        m_playback->setCurrentIndex(int(instance->symbol->playback));
    }
    m_speed->setEnabled(instance->symbol->playback!=SymbolPlayback::SingleFrame && instance->symbol->playback!=SymbolPlayback::Poses);
}
bool SymbolLibraryPanel::prepare(){
    if(m_transitionGuard&&!m_transitionGuard())return false;
    if(!m_canvas->hasProject())return false;
    if(m_timeline&&!m_timeline->prepareTimelineEdit())return false;
    m_canvas->finishStroke();
    if(!m_canvas->resolvePendingTransform())return false;
    if(m_canvas->document()->editingGestureActive()){showError("Finish the current edit before changing symbols.");return false;}
    showError({});return true;
}
QString SymbolLibraryPanel::askName(const QString &title,const QString &initial){QInputDialog dialog(this);dialog.setObjectName("symbolNameDialog");dialog.setWindowTitle(title);dialog.setLabelText("Name");dialog.setTextValue(initial);dialog.setInputMode(QInputDialog::TextInput);if(dialog.exec()!=QDialog::Accepted)return {};return dialog.textValue().trimmed();}
void SymbolLibraryPanel::createSymbol(){if(!prepare())return;const auto name=askName("New symbol","Symbol");if(name.isEmpty())return;QString error;const auto id=createEmptySymbol(m_canvas->document(),name,&error);showError(error);if(!id.isEmpty()){m_search->clear();setCurrentSymbol(id);emit editRequested(id);}}
void SymbolLibraryPanel::convertLayers(const QStringList &ids){if(!prepare())return;if(ids.isEmpty()){showError("Select the layers to include in the symbol.");return;}const auto name=askName("Create symbol from layers","Symbol");if(name.isEmpty())return;QString error;const auto id=convertLayersToSymbol(m_canvas->document(),ids,name,&error);showError(error);if(!id.isEmpty())setCurrentSymbol(id);}
void SymbolLibraryPanel::insert(){const auto id=currentSymbolId();if(id.isEmpty()||!prepare())return;QString error;insertSymbolInstance(m_canvas->document(),id,&error);showError(error);}
void SymbolLibraryPanel::edit(){const auto id=currentSymbolId();if(!id.isEmpty()&&prepare())emit editRequested(id);}
void SymbolLibraryPanel::editCurrentInstance(){const auto *instance=activeSymbol(m_canvas);const auto id=instance?instance->symbol->symbolId:QString{};if(!id.isEmpty()&&prepare())emit editRequested(id);}
void SymbolLibraryPanel::rename(){const auto id=currentSymbolId();if(!prepare())return;const auto *definition=findSymbol(m_canvas->project(),id);if(!definition)return;const auto name=askName("Rename symbol",definition->name);if(name.isEmpty())return;QString error;renameSymbol(m_canvas->document(),id,name,&error);showError(error);}
void SymbolLibraryPanel::duplicate(){const auto id=currentSymbolId();if(!prepare())return;QString error;const auto duplicateId=duplicateSymbol(m_canvas->document(),id,&error);showError(error);if(!duplicateId.isEmpty()){m_search->clear();setCurrentSymbol(duplicateId);}}
void SymbolLibraryPanel::remove(){const auto id=currentSymbolId();if(!prepare())return;QString error;removeSymbol(m_canvas->document(),id,&error);showError(error);}
void SymbolLibraryPanel::makeUnique(){const auto id=activeInstanceId();if(!prepare())return;QString error;const auto symbol=makeSymbolInstanceUnique(m_canvas->document(),id,&error);showError(error);if(!symbol.isEmpty())setCurrentSymbol(symbol);}
void SymbolLibraryPanel::breakApart(){const auto id=activeInstanceId();if(!prepare())return;QString error;breakApartSymbolInstance(m_canvas->document(),id,&error);showError(error);}
void SymbolLibraryPanel::commitPlayback(){
    if(m_refreshing||m_resolving)return;
    const auto *layer=activeSymbol(m_canvas);if(!layer||(!m_editLayerId.isEmpty()&&m_editLayerId!=layer->id)){cancelEditors();return;}
    SymbolInstance instance=*layer->symbol;const QString id=layer->id;
    instance.playback=SymbolPlayback(m_playback->currentIndex());instance.startFrame=qRound(m_start->value())-1;
    const int first=qRound(m_first->value())-1;if(first!=instance.firstFrame && instance.playback!=SymbolPlayback::Poses)instance.playbackOffset=0;
    instance.firstFrame=first;instance.speed=m_speed->value()/100.;
    m_editLayerId.clear();if(m_timeline)m_timeline->setPendingEdits(this,false);
    QString error=symbolClipEditError(m_canvas->shot(),id);if(error.isEmpty())setSymbolInstance(m_canvas->document(),id,instance,&error);showError(error);refresh();
}
void SymbolLibraryPanel::commitEditors(){if(m_resolving)return;{const QScopedValueRollback<bool> guard(m_resolving,true);for(auto *field:{m_start,m_first,m_speed})field->commitEditing();}if(!m_editLayerId.isEmpty())commitPlayback();}
void SymbolLibraryPanel::cancelEditors(){if(m_resolving)return;{const QScopedValueRollback<bool> guard(m_resolving,true);for(auto *field:{m_start,m_first,m_speed})field->cancelEditing();m_editLayerId.clear();if(m_timeline)m_timeline->setPendingEdits(this,false);}refresh();}
} // namespace toon
