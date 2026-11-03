#include "auto_actions_panel.h"
#include "auto_actions.h"
#include "icons.h"
#include "ui_theme.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <utility>

namespace toon {
namespace {
QString number(qreal value) { return QString::number(value,'g',5); }
QStringList stepDetails(const AutoActionStep &step) {
    const auto &p=step.parameters;QStringList lines;
    if(step.operation=="fill" || step.operation=="bucket") {
        const QColor color(p["color"].toString());
        lines << QString("%1: %2").arg(step.operation=="fill"?"Fill":"Bucket",color.name());
        if(color.alpha()!=255)lines << "Color opacity: "+number(color.alphaF()*100)+"%";
        if(step.operation=="fill")lines << "Current selection or drawing area";
        else {
            lines << QString("Point: %1, %2 px").arg(number(p["x"].toDouble()),number(p["y"].toDouble()));
            const int source=p["source"].toInt();
            lines << (source==0?"Sample: Active layer":source==1?"Sample: All layers":QString("Sample: Reference layer %1%2").arg(p["referenceOffset"].toInt()>=0?"+":"").arg(p["referenceOffset"].toInt()));
            lines << "Tolerance: "+QString::number(p["tolerance"].toInt());
            lines << "Close gaps: "+QString::number(p["gapClosing"].toInt())+" px";
        }
    } else if(step.operation=="transform") {
        lines << QString("Move: %1, %2 px").arg(number(p["translationX"].toDouble()),number(p["translationY"].toDouble()));
        lines << QString("Scale: %1% × %2%").arg(number(p["scaleX"].toDouble()*100),number(p["scaleY"].toDouble()*100));
        lines << "Rotate: "+number(p["rotation"].toDouble())+QString::fromUtf8("°");
        lines << "Around current layer or selection bounds";
    } else if(step.operation=="selectRelative") {
        const int offset=p["offset"].toInt();
        lines << QString("Select %1 layer(s) %2").arg(std::abs(offset)).arg(offset>0?"above":"below");
    } else if(step.operation=="newLayer") lines << (p["annotation"].toBool()?"New notes layer":"New drawing layer");
    else if(step.operation=="properties") {
        if(p.contains("name"))lines << "Name: "+p["name"].toString();
        if(p.contains("opacity"))lines << "Opacity: "+number(p["opacity"].toDouble()*100)+"%";
        if(p.contains("blend"))if(const auto mode=layerBlendModeFromKey(p["blend"].toString()))lines << "Blend: "+layerBlendModeName(*mode);
        for(const auto &[key,label]:{std::pair{"visible","Visibility"},{"locked","Lock all"},{"alphaLocked","Lock transparent pixels"},
                {"positionLocked","Lock position"},{"clipped","Clip to layer below"},{"annotation","Production notes"}})
            if(p.contains(key))lines << QString("%1: %2").arg(label,p[key].toBool()?"On":"Off");
        if(p.contains("effects")) {
            LayerEffects effects;QString error;
            if(layerEffectsFromJson(p["effects"].toObject(),&effects,&error)) {
                if(!effects.enabled)lines << "Effects: Off";
                else {
                    if(effects.blurEnabled)lines << "Blur: "+number(effects.blurRadius)+" px";
                    if(effects.shadowEnabled)lines << QString("Shadow: %1 px · offset %2, %3 · %4% · %5")
                        .arg(number(effects.shadowRadius),number(effects.shadowOffset.x()),number(effects.shadowOffset.y()),number(effects.shadowOpacity*100),effects.shadowColor.name());
                    if(effects.glowEnabled)lines << QString("Glow: %1 px · %2% · %3").arg(number(effects.glowRadius),number(effects.glowOpacity*100),effects.glowColor.name());
                    if(effects.colorEnabled)lines << QString("Color: exposure %1 · contrast %2 · saturation %3 · hue %4")
                        .arg(number(effects.exposure),number(effects.contrast),number(effects.saturation),number(effects.hue));
                    if(!effects.blurEnabled&&!effects.shadowEnabled&&!effects.glowEnabled&&!effects.colorEnabled)lines << "Effects: None";
                }
            }
        }
    }
    if(lines.isEmpty())lines << step.label;
    return lines;
}
QIcon stopIcon() {
    QIcon icon;
    for(int ratio : {1,2,3}) {
        QPixmap pixmap(18*ratio,18*ratio);pixmap.setDevicePixelRatio(ratio);pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);painter.setPen(QPen(QColor(ui::text),1.6));painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(4,4,10,10));painter.end();icon.addPixmap(pixmap);
    }
    return icon;
}
QToolButton *button(QWidget *parent,const char *id,const QIcon &icon,const QString &help) {
    auto *button=new QToolButton(parent);button->setObjectName(id);button->setIcon(icon);
    button->setIconSize({18,18});button->setFixedSize(28,28);button->setToolTip(help);button->setAccessibleName(help.section('\n',0,0));
    button->setStyleSheet("QToolButton { padding: 0px; min-width: 26px; max-width: 26px; min-height: 26px; max-height: 26px; }");
    return button;
}
}
AutoActionsPanel::AutoActionsPanel(AutoActions *actions,QWidget *parent):QWidget(parent),m_actions(actions) {
    setObjectName("autoActionsPanelContent");setAccessibleName("Auto Actions");setMinimumSize(190,150);
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(4,4,4,4);layout->setSpacing(4);
    auto *header=new QHBoxLayout;header->setSpacing(4);
    m_selector=new QComboBox(this);m_selector->setObjectName("autoActionSelector");m_selector->setAccessibleName("Saved actions");
    m_selector->setToolTip("Choose a saved action");m_selector->setMinimumWidth(0);m_selector->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
    m_selector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);m_selector->setMinimumContentsLength(1);
    m_add=button(this,"autoActionAdd",appIcon("plus"),"New action");
    m_more=button(this,"autoActionMore",appIcon("ellipsis"),"Action options");
    header->addWidget(m_selector,1);header->addWidget(m_add);header->addWidget(m_more);layout->addLayout(header);
    m_body=new QStackedWidget(this);m_body->setMinimumSize(0,40);
    m_steps=new QListWidget(m_body);m_steps->setObjectName("autoActionSteps");m_steps->setAccessibleName("Recorded steps");
    m_steps->setAccessibleDescription("Checked steps will play. Right-click a step to move or remove it.");
    m_steps->setFrameShape(QFrame::NoFrame);m_steps->setMinimumSize(0,40);m_steps->setWordWrap(false);
    m_steps->setUniformItemSizes(true);m_steps->setTextElideMode(Qt::ElideRight);
    m_steps->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);m_steps->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_steps->setContextMenuPolicy(Qt::CustomContextMenu);
    m_empty=new QLabel("No recorded steps",m_body);m_empty->setObjectName("autoActionEmpty");m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);m_empty->setStyleSheet(QString("color: %1;").arg(ui::muted));
    m_body->addWidget(m_steps);m_body->addWidget(m_empty);layout->addWidget(m_body,1);
    auto *controls=new QHBoxLayout;controls->setSpacing(4);
    m_record=button(this,"autoActionRecord",appIcon("circle"),"Record operations (append to this action)\nSupports layers, properties, effects, fills, buckets and raster move/scale/rotate.\nApply a transform to record it. Freehand drawing and other unsupported edits stop recording.");m_record->setCheckable(true);
    m_stop=button(this,"autoActionStop",stopIcon(),"Stop recording");
    m_play=button(this,"autoActionPlay",appIcon("play"),"Play enabled steps on the active layer");
    controls->addWidget(m_record);controls->addWidget(m_stop);controls->addWidget(m_play);controls->addStretch();layout->addLayout(controls);
    m_status=new QLabel(this);m_status->setObjectName("autoActionStatus");m_status->setWordWrap(true);m_status->setTextFormat(Qt::PlainText);
    m_status->setStyleSheet(QString("color: %1;").arg(ui::muted));m_status->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);layout->addWidget(m_status);
    connect(m_selector,&QComboBox::currentIndexChanged,this,[this]{m_message.clear();refreshSteps();});
    connect(m_add,&QToolButton::clicked,this,&AutoActionsPanel::createAction);
    connect(m_more,&QToolButton::clicked,this,&AutoActionsPanel::showOptions);
    connect(m_steps,&QWidget::customContextMenuRequested,this,&AutoActionsPanel::showStepMenu);
    connect(m_steps,&QListWidget::itemChanged,this,[this](QListWidgetItem *item){
        if(canManage())m_actions->setStepEnabled(selectedActionId(),item->data(Qt::UserRole).toInt(),item->checkState()==Qt::Checked);
        else refreshSteps();
    });
    connect(m_record,&QToolButton::clicked,this,[this]{
        if(canManage())m_actions->startRecording(selectedActionId());refreshState();
    });
    connect(m_stop,&QToolButton::clicked,this,[this]{if(m_actions)m_actions->stopRecording();});
    connect(m_play,&QToolButton::clicked,this,[this]{if(canManage()&&m_play->isEnabled())emit playRequested(selectedActionId());});
    if(m_actions) {
        connect(m_actions,&AutoActions::changed,this,&AutoActionsPanel::refresh);
        connect(m_actions,&AutoActions::stateChanged,this,&AutoActionsPanel::refreshState);
        connect(m_actions,&AutoActions::status,this,[this](const QString &message){m_message=message;refreshState();});
        connect(m_actions,&QObject::destroyed,this,[this]{refresh();});
    }
    refresh();
}
QString AutoActionsPanel::selectedActionId() const {return m_selector->currentData().toString();}
void AutoActionsPanel::selectAction(const QString &id) {
    if(m_actions&&(m_actions->recording()||m_actions->playing()))return;
    const int index=m_selector->findData(id);if(index>=0)m_selector->setCurrentIndex(index);
}
void AutoActionsPanel::setInteractionEnabled(bool enabled) {
    if(m_interactionEnabled==enabled)return;m_interactionEnabled=enabled;refreshState();
}
bool AutoActionsPanel::canManage() const {return m_actions&&m_interactionEnabled&&!m_actions->recording()&&!m_actions->playing();}
void AutoActionsPanel::refresh() {
    QString id=selectedActionId();const QSignalBlocker block(m_selector);m_selector->clear();
    if(m_actions){for(const auto &action:m_actions->actions())m_selector->addItem(action.name,action.id);
        if(m_actions->recording())id=m_actions->recordingId();}
    int index=m_selector->findData(id);if(index<0&&m_selector->count())index=0;m_selector->setCurrentIndex(index);
    refreshSteps();
}
void AutoActionsPanel::refreshSteps() {
    const int selected=m_steps->currentRow();const QSignalBlocker block(m_steps);m_steps->clear();
    const auto *action=m_actions?m_actions->action(selectedActionId()):nullptr;
    if(action)for(int i=0;i<action->steps.size();++i){const auto &step=action->steps[i];const auto details=stepDetails(step);
        auto *item=new QListWidgetItem(details.join(" · "),m_steps);
        item->setData(Qt::UserRole,i);item->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable|Qt::ItemIsUserCheckable);
        item->setCheckState(step.enabled?Qt::Checked:Qt::Unchecked);item->setToolTip(details.join("\n"));
        item->setSizeHint({0,26});if(!step.enabled)item->setForeground(QColor(ui::muted));}
    if(m_steps->count())m_steps->setCurrentRow(qBound(0,selected,m_steps->count()-1));
    m_empty->setText(action?"No recorded steps":"Create an action with +");m_body->setCurrentWidget(m_steps->count()?static_cast<QWidget*>(m_steps):m_empty);
    refreshState();
}
void AutoActionsPanel::refreshState() {
    const bool recording=m_actions&&m_actions->recording(),playing=m_actions&&m_actions->playing();
    const auto *action=m_actions?m_actions->action(selectedActionId()):nullptr;const bool manage=canManage();
    const bool ready=m_actions&&m_actions->availabilityReason().isEmpty();
    m_selector->setEnabled(manage&&m_selector->count()>0);m_add->setEnabled(manage);m_more->setEnabled(manage&&action);
    m_steps->setEnabled(manage);m_record->setEnabled(manage&&action&&ready);m_record->setChecked(recording);
    m_stop->setEnabled(recording);m_play->setEnabled(manage&&ready&&action&&std::any_of(action->steps.cbegin(),action->steps.cend(),[](const auto &s){return s.enabled;}));
    QString status=m_message;
    if(recording)status=QString("Recording · %1 steps").arg(action?action->steps.size():0);
    else if(playing)status="Playing…";
    m_status->setText(status);m_status->setToolTip(status);m_status->setVisible(!status.isEmpty());
}
void AutoActionsPanel::createAction() {
    if(!canManage())return;bool accepted=false;
    const auto name=QInputDialog::getText(this,"New action","Name",QLineEdit::Normal,"New action",&accepted).trimmed();
    if(accepted&&!name.isEmpty()&&canManage()){const auto id=m_actions->create(name);if(!id.isEmpty())selectAction(id);}
}
void AutoActionsPanel::renameAction() {
    if(!canManage())return;const auto id=selectedActionId();const auto *action=m_actions->action(id);if(!action)return;
    bool accepted=false;const auto name=QInputDialog::getText(this,"Rename action","Name",QLineEdit::Normal,action->name,&accepted).trimmed();
    if(accepted&&!name.isEmpty()&&canManage())m_actions->rename(id,name);
}
void AutoActionsPanel::showOptions() {
    if(!canManage()||selectedActionId().isEmpty())return;const auto id=selectedActionId();QMenu menu(this);
    auto *rename=menu.addAction(appIcon("pencil"),"Rename");auto *duplicate=menu.addAction(appIcon("copy"),"Duplicate");menu.addSeparator();
    auto *remove=menu.addAction(appIcon("trash"),"Delete action");const auto chosen=menu.exec(m_more->mapToGlobal(QPoint(0,m_more->height())));
    if(!canManage()||!m_actions->action(id))return;
    if(chosen==rename)renameAction();else if(chosen==duplicate){const auto copy=m_actions->duplicate(id);if(!copy.isEmpty())selectAction(copy);}
    else if(chosen==remove)m_actions->remove(id);
}
void AutoActionsPanel::showStepMenu(const QPoint &position) {
    if(!canManage())return;auto *item=m_steps->itemAt(position);if(!item)return;m_steps->setCurrentItem(item);
    const auto id=selectedActionId();const int index=item->data(Qt::UserRole).toInt();QMenu menu(this);
    auto *up=menu.addAction(appIcon("arrow-up"),"Move up");up->setEnabled(index>0);
    auto *down=menu.addAction(appIcon("arrow-down"),"Move down");down->setEnabled(index+1<m_steps->count());menu.addSeparator();
    auto *remove=menu.addAction(appIcon("trash"),"Remove step");const auto chosen=menu.exec(m_steps->viewport()->mapToGlobal(position));
    if(!canManage()||selectedActionId()!=id)return;
    if(chosen==up){m_actions->moveStep(id,index,index-1);m_steps->setCurrentRow(index-1);}
    else if(chosen==down){m_actions->moveStep(id,index,index+1);m_steps->setCurrentRow(index+1);}
    else if(chosen==remove)m_actions->removeStep(id,index);
}
}
