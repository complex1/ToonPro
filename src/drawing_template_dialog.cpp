#include "drawing_template_dialog.h"
#include "drawing_template_library.h"
#include "icons.h"
#include "ui_theme.h"
#include <QApplication>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>

namespace toon {
namespace {
QToolButton *button(const char *id,const char *icon,const QString &help) {
    auto *result=new QToolButton; result->setObjectName(id); result->setIcon(appIcon(icon)); result->setIconSize({20,20});
    result->setFixedSize(32,32); result->setToolTip(help); result->setAccessibleName(help); return result;
}
QString builtInId(DrawingTemplate type) { return QString("builtin:%1").arg(int(type)); }
}
DrawingTemplateWidget::DrawingTemplateWidget(DrawingTemplateLibrary *library,QWidget *parent)
    : QWidget(parent),m_library(library) {
    setObjectName("drawingTemplateEditor"); setMinimumWidth(240);
    auto *layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0); layout->setSpacing(6);
    m_presets=new QComboBox; m_presets->setObjectName("animationDrawingTemplate"); m_presets->setAccessibleName("Drawing template");
    m_presets->setToolTip("Choose a built-in or saved drawing template"); m_presets->setMinimumWidth(0);
    m_presets->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); m_presets->setMinimumContentsLength(8); layout->addWidget(m_presets);
    auto *caption=new QLabel("Layers · top to bottom"); caption->setStyleSheet(QString("color: %1;").arg(ui::muted)); layout->addWidget(caption);
    m_layers=new QTableWidget(0,2); m_layers->setObjectName("drawingTemplateLayers"); m_layers->setAccessibleName("Drawing template layer stack");
    m_layers->setHorizontalHeaderLabels({"Layer","Type"}); m_layers->verticalHeader()->hide();
    m_layers->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch); m_layers->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Fixed);
    m_layers->setColumnWidth(1,90); m_layers->verticalHeader()->setDefaultSectionSize(32);
    m_layers->setSelectionBehavior(QAbstractItemView::SelectRows); m_layers->setSelectionMode(QAbstractItemView::SingleSelection);
    m_layers->setEditTriggers(QAbstractItemView::DoubleClicked|QAbstractItemView::EditKeyPressed|QAbstractItemView::SelectedClicked);
    m_layers->setShowGrid(false); m_layers->setMinimumHeight(144); m_layers->setMaximumHeight(248); layout->addWidget(m_layers);
    auto *actions=new QHBoxLayout; actions->setSpacing(4);
    m_add=button("drawingTemplateAddLayer","plus","Add a layer to this template");
    m_remove=button("drawingTemplateRemoveLayer","trash","Remove the selected template layer");
    m_up=button("drawingTemplateLayerUp","arrow-up","Move the selected template layer up");
    m_down=button("drawingTemplateLayerDown","arrow-down","Move the selected template layer down");
    actions->addWidget(m_add); actions->addWidget(m_remove); actions->addStretch(); actions->addWidget(m_up); actions->addWidget(m_down); layout->addLayout(actions);
    auto *saveRow=new QHBoxLayout; saveRow->setSpacing(4);
    m_name=new QLineEdit; m_name->setObjectName("drawingTemplateName"); m_name->setMaxLength(100); m_name->setPlaceholderText("Preset name");
    m_name->setAccessibleName("Saved drawing template name"); m_name->setToolTip("Name for a reusable drawing template"); m_name->setMinimumWidth(0);
    m_save=button("drawingTemplateSave","save","Save template to your global library");
    m_delete=button("drawingTemplateDelete","trash","Remove the selected saved template; drawings already created are kept");
    saveRow->addWidget(m_name,1); saveRow->addWidget(m_save); saveRow->addWidget(m_delete); layout->addLayout(saveRow);
    m_status=new QLabel; m_status->setObjectName("drawingTemplateStatus"); m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true);
    m_status->setMinimumHeight(fontMetrics().height()*2); m_status->setStyleSheet(QString("color: %1;").arg(ui::muted)); layout->addWidget(m_status);
    connect(m_presets,&QComboBox::currentIndexChanged,this,[this]{if(!m_refreshing)choosePreset();});
    connect(m_layers,&QTableWidget::itemChanged,this,[this]{changeDraft();});
    connect(m_layers,&QTableWidget::itemSelectionChanged,this,&DrawingTemplateWidget::updateActions);
    connect(m_name,&QLineEdit::textChanged,this,&DrawingTemplateWidget::updateActions);
    connect(m_add,&QToolButton::clicked,this,[this]{
        finishLayerEdit(); auto layers=templateSlots(); layers.append({QUuid::createUuid().toString(QUuid::WithoutBraces),"Drawing",LayerKind::Raster});
        rebuildLayers(layers); changeDraft();
    });
    connect(m_remove,&QToolButton::clicked,this,[this]{
        finishLayerEdit(); const int row=m_layers->currentRow(); auto layers=templateSlots(); if(row<0||layers.size()<=1)return;
        layers.removeAt(layers.size()-1-row); rebuildLayers(layers,qMin(row,int(layers.size())-1)); changeDraft();
    });
    connect(m_up,&QToolButton::clicked,this,[this]{moveLayer(-1);});
    connect(m_down,&QToolButton::clicked,this,[this]{moveLayer(1);});
    connect(m_save,&QToolButton::clicked,this,[this]{
        finishLayerEdit();
        if(!m_library)return; const auto name=m_name->text().trimmed(); QString error;
        if(!m_library->saveTemplate(name,templateSlots(),&error)){showStatus(error);updateActions();return;}
        for(const auto &entry:m_library->entries())if(entry.name.compare(name,Qt::CaseInsensitive)==0)m_selectedPreset=entry.id;
        refreshPresets(); showStatus("Template saved."); updateActions();
    });
    connect(m_delete,&QToolButton::clicked,this,[this]{
        if(!m_library||m_selectedPreset.startsWith("builtin:")||m_selectedPreset.isEmpty())return;
        QString error; if(!m_library->removeTemplate(m_selectedPreset,&error)){showStatus(error);updateActions();return;}
        m_selectedPreset.clear(); refreshPresets(); showStatus("Saved template removed. This draft is still available."); updateActions();
    });
    if(m_library)connect(m_library,&DrawingTemplateLibrary::changed,this,&DrawingTemplateWidget::refreshPresets);
    refreshPresets(); setBuiltIn(DrawingTemplate::Anime);
    if(m_library&&!m_library->loadError().isEmpty())showStatus(m_library->loadError());
}
QVector<DrawingTemplateSlot> DrawingTemplateWidget::templateSlots() const {
    QVector<DrawingTemplateSlot> layers;
    for(int row=m_layers->rowCount()-1;row>=0;--row){
        const auto *item=m_layers->item(row,0); const auto *type=qobject_cast<QComboBox*>(m_layers->cellWidget(row,1));
        if(item&&type)layers.append({item->data(Qt::UserRole).toString(),item->text().trimmed(),LayerKind(type->currentData().toInt())});
    }
    return layers;
}
bool DrawingTemplateWidget::validTemplate() const { return validateDrawingTemplate(templateSlots()).isEmpty(); }
void DrawingTemplateWidget::setBuiltIn(DrawingTemplate type) {
    m_selectedPreset=builtInId(type); refreshPresets(); choosePreset();
}
void DrawingTemplateWidget::setTemplateSlots(const QVector<DrawingTemplateSlot> &layers) {
    m_selectedPreset.clear(); refreshPresets(); rebuildLayers(layers); updateActions(); emit changed();
}
void DrawingTemplateWidget::refreshPresets() {
    QScopedValueRollback<bool> guard(m_refreshing,true); m_presets->clear();
    m_presets->addItem("Simple",builtInId(DrawingTemplate::Simple)); m_presets->addItem("Rough → Clean-up",builtInId(DrawingTemplate::Rough));
    m_presets->addItem("Anime",builtInId(DrawingTemplate::Anime));
    if(m_library)for(const auto &entry:m_library->entries())m_presets->addItem(entry.name,entry.id);
    m_presets->addItem("Custom",QString()); m_presets->setCurrentIndex(m_presets->findData(m_selectedPreset)); updateActions();
}
void DrawingTemplateWidget::choosePreset() {
    m_selectedPreset=m_presets->currentData().toString();
    if(m_selectedPreset.startsWith("builtin:")) {
        rebuildLayers(drawingTemplateSlots(DrawingTemplate(m_selectedPreset.mid(8).toInt()))); m_name->clear();
    } else if(m_library)for(const auto &entry:m_library->entries())if(entry.id==m_selectedPreset){rebuildLayers(entry.layers);m_name->setText(entry.name);break;}
    showStatus({}); updateActions(); emit changed();
}
void DrawingTemplateWidget::rebuildLayers(const QVector<DrawingTemplateSlot> &layers,int selectedRow) {
    QScopedValueRollback<bool> guard(m_refreshing,true); m_layers->setRowCount(0);
    for(auto it=layers.crbegin();it!=layers.crend();++it){
        const int row=m_layers->rowCount(); m_layers->insertRow(row);
        auto *item=new QTableWidgetItem(it->name); item->setData(Qt::UserRole,it->id); item->setToolTip("Rename this template layer"); m_layers->setItem(row,0,item);
        auto *type=new QComboBox; type->setObjectName("drawingTemplateLayerType"); type->addItem("Raster",int(LayerKind::Raster)); type->addItem("Vector",int(LayerKind::Vector));
        type->setCurrentIndex(type->findData(int(it->kind))); type->setAccessibleName("Template layer type"); type->setToolTip("Raster pixels or editable vector brush strokes");
        m_layers->setCellWidget(row,1,type); connect(type,&QComboBox::currentIndexChanged,this,[this]{changeDraft();});
    }
    if(!layers.isEmpty())m_layers->setCurrentCell(qBound(0,selectedRow,int(layers.size())-1),0); updateActions();
}
void DrawingTemplateWidget::changeDraft() {
    if(m_refreshing)return;
    // Preserve a saved preset identity while editing so Save updates it by name.
    if(m_selectedPreset.startsWith("builtin:")){m_selectedPreset.clear();refreshPresets();}
    showStatus(validateDrawingTemplate(templateSlots())); updateActions(); emit changed();
}
void DrawingTemplateWidget::moveLayer(int direction) {
    finishLayerEdit(); const int row=m_layers->currentRow(),target=row+direction; auto layers=templateSlots();
    if(row<0||target<0||target>=layers.size())return;
    std::swap(layers[layers.size()-1-row],layers[layers.size()-1-target]); rebuildLayers(layers,target); changeDraft();
}
void DrawingTemplateWidget::finishLayerEdit() {
    if (auto *focus=QApplication::focusWidget(); focus && m_layers->isAncestorOf(focus)) {
        focus->clearFocus(); m_layers->setFocus(Qt::OtherFocusReason);
    }
}
void DrawingTemplateWidget::showStatus(const QString &message) {
    const auto text=message.isEmpty() && m_library && !m_library->writable() ? m_library->loadError() : message;
    m_status->setText(text); m_status->setToolTip(text);
}
void DrawingTemplateWidget::updateActions() {
    const int row=m_layers->currentRow(),count=m_layers->rowCount(); const bool canSave=m_library&&m_library->writable();
    m_add->setEnabled(count<maxDrawingLayers-1); m_remove->setEnabled(count>1&&row>=0);
    m_up->setEnabled(row>0); m_down->setEnabled(row>=0&&row<count-1);
    m_save->setEnabled(canSave&&validTemplate()&&!m_name->text().trimmed().isEmpty());
    m_delete->setEnabled(canSave&&!m_selectedPreset.isEmpty()&&!m_selectedPreset.startsWith("builtin:"));
    m_name->setEnabled(canSave);
}
} // namespace toon
