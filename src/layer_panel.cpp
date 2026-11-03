#include "scene3d_cache.h"
#include "layer_panel.h"
#include "symbols.h"
#include "animation.h"
#include "document.h"
#include "object_tools.h"
#include "icons.h"
#include "value_slider.h"
#include "property_row.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include <QApplication>
#include <QComboBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <functional>

namespace toon {
namespace {
constexpr int layerThumbnailExtent = ui::metrics::layerThumbnail;
class LayerPanelContent final : public QWidget {
public:
    int heightForWidth(int width) const override {
        // The responsive property row makes this layout height-for-width aware.
        // Let the tree shrink to its minimum before scrolling the whole panel,
        // so the header and footer remain pinned in a short dock.
        return layout() ? layout()->minimumHeightForWidth(width) : QWidget::heightForWidth(width);
    }
};
enum RowRole { Id = Qt::UserRole, Group, Visible, Locked, Annotation, Clipped, AlphaLocked, PositionLocked, RowColor, Thumbnail, Kind, Effects, EffectsEnabled, Mask, MaskEnabled, Adjustment };
QString idOf(const QTreeWidgetItem *item) { return item ? item->data(0, Id).toString() : QString(); }
class LayerDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override {
        return {100, qMax(ui::metrics::layerRow, option.fontMetrics.height() + 10)};
    }
    void paint(QPainter *p, const QStyleOptionViewItem &o, const QModelIndex &index) const override {
        const auto info = index.siblingAtColumn(0);
        const QRect actionRect(o.rect.center()-QPoint(ui::metrics::actionIcon/2,ui::metrics::actionIcon/2), QSize(ui::metrics::actionIcon,ui::metrics::actionIcon));
        p->save(); p->setFont(o.font);
        p->fillRect(o.rect, QColor(o.state & QStyle::State_Selected ? ui::selected : o.state & QStyle::State_MouseOver ? ui::hover : ui::base));
        const auto color = info.data(RowColor).value<QColor>();
        if (color.isValid()) { QColor tint = color; tint.setAlpha(25); p->fillRect(o.rect, tint); }
        if (index.column() == 0) {
            const bool selected = o.state & QStyle::State_Selected;
            if (color.isValid()) p->fillRect(QRect(o.rect.left() + (selected ? 3 : 0), o.rect.top(), 3, o.rect.height()), color);
            // Selection remains distinct when the tree no longer has focus.
            if (selected) p->fillRect(QRect(o.rect.left(), o.rect.top(), 2, o.rect.height()), QColor(ui::focus));
            appIcon(info.data(Visible).toBool() ? "eye" : "eye-off").paint(p, actionRect, Qt::AlignCenter,
                info.data(Visible).toBool() ? QIcon::Normal : QIcon::Disabled);
        } else if (index.column() == 2) {
            if (info.data(Locked).toBool()) appIcon("lock-keyhole").paint(p,actionRect);
            else if (info.data(PositionLocked).toBool()) appIcon("pin").paint(p,actionRect);
        } else {
            QRect thumb(o.rect.left()+2, o.rect.center().y()-layerThumbnailExtent/2, layerThumbnailExtent,layerThumbnailExtent);
            if (info.data(Group).toBool()) appIcon("folder-open").paint(p,thumb.adjusted(3,3,-3,-3));
            else if (info.data(Adjustment).toBool()) appIcon("sliders-horizontal").paint(p,thumb.adjusted(3,3,-3,-3));
            else {
                p->fillRect(thumb, QColor("#dedede"));
                for(int y=0;y<layerThumbnailExtent/4;++y) for(int x=0;x<layerThumbnailExtent/4;++x) if((x+y)%2) p->fillRect(QRect(thumb.left()+x*4,thumb.top()+y*4,4,4),QColor("#adadad"));
                p->drawImage(thumb, info.data(Thumbnail).value<QImage>());
                p->setPen(QColor(ui::border)); p->drawRect(thumb.adjusted(0,0,-1,-1));
            }
            if (info.data(Clipped).toBool()) { p->setPen(QPen(QColor(ui::accent),2)); p->drawLine(thumb.left()-2,thumb.top()+5,thumb.left()-2,thumb.bottom()-5); }
            int x = thumb.right()+7;
            const auto kind = LayerKind(info.data(Kind).toInt());
            if (kind != LayerKind::Raster) { appIcon(kind == LayerKind::Symbol ? "toon-symbol" : kind == LayerKind::Scene3D ? "box" : kind == LayerKind::Vector ? "pen-tool" : "type").paint(p,QRect(x,o.rect.center().y()-7,14,14)); x+=18; }
            if (info.data(Annotation).toBool()) { appIcon("sticky-note").paint(p,QRect(x,o.rect.center().y()-7,14,14)); x+=18; }
            p->setPen(QColor(info.data(Visible).toBool() ? ui::text : ui::muted));
            const auto nameRect=QRect(x,o.rect.top(),qMax(0,o.rect.right()-x-3),o.rect.height());
            p->drawText(nameRect,Qt::AlignVCenter|Qt::AlignLeft,o.fontMetrics.elidedText(index.data().toString(),Qt::ElideRight,nameRect.width()));
            if (info.data(AlphaLocked).toBool()) { p->setBrush(QColor(ui::panel)); p->setPen(Qt::NoPen); p->drawRect(thumb.right()-10,thumb.bottom()-10,12,12); featureIcon(FeatureIcon::AlphaLock).paint(p,QRect(thumb.right()-10,thumb.bottom()-10,12,12)); }
            const auto badge=[&](QRect bounds,const char *icon,bool enabled){p->setBrush(QColor(ui::panel));p->setPen(Qt::NoPen);p->drawRoundedRect(bounds,2,2);appIcon(icon).paint(p,bounds.adjusted(1,1,-1,-1),Qt::AlignCenter,enabled?QIcon::Normal:QIcon::Disabled);};
            if(info.data(Effects).toBool())badge(QRect(thumb.left()-1,thumb.bottom()-12,14,14),"sparkles",info.data(EffectsEnabled).toBool());
            if(info.data(Mask).toBool())badge(QRect(thumb.right()-12,thumb.top()-1,14,14),"square-dashed",info.data(MaskEnabled).toBool());
        }
        p->setPen(QColor(ui::raised)); p->drawLine(o.rect.bottomLeft(),o.rect.bottomRight());
        if (o.state & QStyle::State_HasFocus) { p->setPen(QColor(ui::focus)); p->drawRect(o.rect.adjusted(0,0,-1,-1)); }
        p->restore();
    }
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &index) const override {
        if (index.column()!=1) return nullptr;
        auto *edit=new QLineEdit(parent); edit->setObjectName("layerNameEditor"); edit->setProperty("compactDrawingField", true); edit->setMaxLength(100); edit->setAccessibleName("Layer name"); return edit;
    }
    void updateEditorGeometry(QWidget *editor,const QStyleOptionViewItem &o,const QModelIndex &index) const override {
        int offset=layerThumbnailExtent+9;
        if (index.siblingAtColumn(0).data(Annotation).toBool()) offset += 18;
        if (LayerKind(index.siblingAtColumn(0).data(Kind).toInt()) != LayerKind::Raster) offset += 18;
        editor->setGeometry(o.rect.adjusted(offset,2,-1,-2));
    }
};
}
class LayerTree final : public QTreeWidget {
public:
    explicit LayerTree(QWidget *parent=nullptr) : QTreeWidget(parent) {}
    std::function<bool(const QString &, bool)> beginEyes;
    std::function<void(const QStringList &,bool)> previewEyes;
    std::function<void(bool)> finishEyes;
    std::function<void(const QString &)> unlock;
    std::function<void(const QStringList &,const QString &,const QString &)> moveLayers;
    std::function<void()> deleteLayers;
    bool paintingEyes() const { return m_eyes; }
    void cancelEyes() { if(m_eyes)endEyes(false); }
protected:
    void startDrag(Qt::DropActions) override {
        // The document owns moves. Qt's default implementation would remove
        // model rows again after our drop handler has rebuilt their hierarchy.
        QDrag drag(this); drag.setMimeData(mimeData(selectedItems()));
        if(currentItem()) drag.setPixmap(viewport()->grab(visualItemRect(currentItem())));
        drag.exec(Qt::MoveAction);
        m_dropRect={}; viewport()->update();
    }
    void mousePressEvent(QMouseEvent *event) override {
        auto *item=itemAt(event->position().toPoint()); const auto column=columnAt(event->position().x());
        if(event->button()==Qt::LeftButton && item && column==0) {
            m_eyeValue=!item->data(0,Visible).toBool();
            if(beginEyes && beginEyes(idOf(item),m_eyeValue)) { m_eyes=true; m_seen={idOf(item)}; m_lastY=event->position().y(); }
            event->accept(); return;
        }
        if(event->button()==Qt::LeftButton && item && column==2 && item->data(0,Locked).toBool()) { if(unlock) unlock(idOf(item)); event->accept(); return; }
        QTreeWidget::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if(m_eyes) {
            if(!(event->buttons()&Qt::LeftButton)) { endEyes(false); return; }
            if(columnAt(event->position().x())==0) {
                const int y=event->position().y();
                for(int scan=qMin(y,m_lastY);scan<=qMax(y,m_lastY);++scan) if(auto *item=itemAt(QPoint(14,scan))) {
                    const auto id=idOf(item); if(!m_seen.contains(id)) m_seen.append(id);
                }
                if(previewEyes) previewEyes(m_seen,m_eyeValue); m_lastY=y;
            }
            event->accept(); return;
        }
        QTreeWidget::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if(m_eyes && event->button()==Qt::LeftButton) { endEyes(true); event->accept(); return; }
        QTreeWidget::mouseReleaseEvent(event);
    }
    void keyPressEvent(QKeyEvent *event) override {
        if(event->key()==Qt::Key_Escape && m_eyes) { endEyes(false); event->accept(); return; }
        if(event->key()==Qt::Key_Delete || event->key()==Qt::Key_Backspace) { if(deleteLayers) deleteLayers(); event->accept(); return; }
        QTreeWidget::keyPressEvent(event);
    }
    bool event(QEvent *event) override {
        if(m_eyes && (event->type()==QEvent::WindowDeactivate || event->type()==QEvent::Hide || event->type()==QEvent::UngrabMouse)) endEyes(false);
        return QTreeWidget::event(event);
    }
    bool viewportEvent(QEvent *event) override {
        if(event->type()==QEvent::ToolTip) {
            auto *help=static_cast<QHelpEvent*>(event); auto *item=itemAt(help->pos());
            if(item) {
                QString tip;
                if(columnAt(help->pos().x())==0) tip="Show/hide layer · drag down this column to show/hide more";
                else if(columnAt(help->pos().x())==2) tip=item->data(0,Locked).toBool()?"Unlock layer":"Layer lock status";
                else tip=item->toolTip(1);
                QToolTip::showText(help->globalPos(),tip,this); return true;
            }
        }
        return QTreeWidget::viewportEvent(event);
    }
    void dragEnterEvent(QDragEnterEvent *event) override { if(event->source()==this) event->acceptProposedAction(); else event->ignore(); }
    void dragMoveEvent(QDragMoveEvent *event) override {
        if(event->source()!=this) { event->ignore(); return; }
        updateDrop(event->position().toPoint()); event->acceptProposedAction(); viewport()->update();
        if(event->position().y()<20) verticalScrollBar()->setValue(verticalScrollBar()->value()-1);
        if(event->position().y()>viewport()->height()-20) verticalScrollBar()->setValue(verticalScrollBar()->value()+1);
    }
    void dragLeaveEvent(QDragLeaveEvent *event) override { m_dropRect={}; viewport()->update(); event->accept(); }
    void dropEvent(QDropEvent *event) override {
        if(event->source()!=this) { event->ignore(); return; }
        updateDrop(event->position().toPoint()); QStringList ids; for(auto *item:selectedItems()) ids.append(idOf(item));
        if(moveLayers) moveLayers(ids,m_dropParent,m_dropBefore);
        m_dropRect={}; viewport()->update(); event->setDropAction(Qt::MoveAction); event->accept();
    }
    void paintEvent(QPaintEvent *event) override {
        QTreeWidget::paintEvent(event);
        if(!m_dropRect.isEmpty()) { QPainter painter(viewport()); painter.setPen(QPen(QColor(ui::accent),3)); painter.setBrush(Qt::NoBrush);
            if(m_dropInto) painter.drawRect(m_dropRect.adjusted(1,1,-2,-2)); else painter.drawLine(m_dropRect.topLeft(),m_dropRect.topRight()); }
    }
private:
    void endEyes(bool commit) { m_eyes=false; m_seen.clear(); if(finishEyes) finishEyes(commit); }
    void updateDrop(QPoint position) {
        auto subtreeBottom=[this](QTreeWidgetItem *item){
            while(item->isExpanded()&&item->childCount())item=item->child(item->childCount()-1);
            return visualItemRect(item).bottom();
        };
        auto *item=itemAt(position); m_dropInto=false; m_dropParent.clear(); m_dropBefore.clear();
        if(!item) { // Below all roots is bottommost storage insertion.
            if(topLevelItemCount()) m_dropBefore=idOf(topLevelItem(topLevelItemCount()-1));
            const int y=topLevelItemCount()?subtreeBottom(topLevelItem(topLevelItemCount()-1)):0;
            m_dropRect=QRect(0,y,viewport()->width(),1); return;
        }
        const auto rect=visualItemRect(item); const bool above=position.y()<rect.top()+rect.height()/3;
        if(item->data(0,Group).toBool() && !above && position.y()<rect.bottom()-rect.height()/3) {
            m_dropInto=true; m_dropParent=idOf(item); m_dropRect=rect; return;
        }
        auto *parent=item->parent(); m_dropParent=idOf(parent);
        int index=parent?parent->indexOfChild(item):indexOfTopLevelItem(item);
        if(above)--index;
        for(;index>=0;--index){
            auto *candidate=parent?parent->child(index):topLevelItem(index);
            bool moving=false;for(auto *p=candidate;p;p=p->parent())moving|=p->isSelected();
            if(!moving){m_dropBefore=idOf(candidate);break;}
        }
        m_dropRect=QRect(0,above?rect.top():subtreeBottom(item),viewport()->width(),1);
    }
    bool m_eyes=false,m_eyeValue=false,m_dropInto=false;
    int m_lastY=0;
    QStringList m_seen;
    QString m_dropParent,m_dropBefore;
    QRect m_dropRect;
};

LayerPanel::LayerPanel(Document *document,QWidget *parent) : QScrollArea(parent),m_document(document),m_content(new LayerPanelContent) {
    setObjectName("layerContent"); setProperty("compactDrawingPanel", true); setWidgetResizable(true); setFrameShape(QFrame::NoFrame); setMinimumSize(220,220);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *layout=new QVBoxLayout(m_content); layout->setContentsMargins(ui::metrics::panelMargin,ui::metrics::panelMargin,ui::metrics::panelMargin,ui::metrics::panelMargin); layout->setSpacing(ui::metrics::controlGap);
    auto *header=new QHBoxLayout; header->setSpacing(ui::metrics::controlGap);
    m_blend=new QComboBox; m_blend->setObjectName("layerBlendMode"); m_blend->setProperty("compactDrawingField", true); m_blend->setAccessibleName("Layer blending mode"); m_blend->setToolTip("Blending mode · use the wheel or arrow keys to preview");
    m_blend->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); m_blend->setMinimumContentsLength(7); m_blend->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
    for(auto mode:{LayerBlendMode::Normal,LayerBlendMode::Multiply,LayerBlendMode::Screen,LayerBlendMode::Overlay,LayerBlendMode::Darken,LayerBlendMode::Lighten,LayerBlendMode::ColorDodge,LayerBlendMode::ColorBurn,LayerBlendMode::HardLight,LayerBlendMode::SoftLight,LayerBlendMode::Difference,LayerBlendMode::Exclusion}) m_blend->addItem(layerBlendModeName(mode),int(mode));
    header->addWidget(m_blend,1); layout->addLayout(header);
    m_opacity=new ValueSlider("Layer opacity",0,100,100,0,"%"); m_opacity->setObjectName("layerOpacity");
    m_opacity->setToolTip("Layer opacity · drag the slider or enter a percentage");
    auto *opacityRow=new PropertyRow("Opacity",m_opacity,m_content); opacityRow->setLabelWidth(48); layout->addWidget(opacityRow);
    auto button=[this](QHBoxLayout *row,const char *id,const char *icon,const QString &tip,bool check=false) {
        auto *b=new QToolButton(m_content); b->setObjectName(id); b->setProperty("compactDrawingAction", true); b->setIcon(appIcon(icon)); b->setIconSize({ui::metrics::actionIcon,ui::metrics::actionIcon}); b->setFixedSize(ui::metrics::actionTarget,ui::metrics::actionTarget);
        b->setToolTip(tip); b->setAccessibleName(tip); b->setCheckable(check); row->addWidget(b); return b;
    };
    auto *flags=new QHBoxLayout; flags->setSpacing(1);
    m_locked=button(flags,"layerLocked","lock-keyhole","Lock all layer edits",true);
    m_alphaLocked=button(flags,"layerAlphaLocked","blend","Lock transparent pixels",true);
    m_alphaLocked->setIcon(featureIcon(FeatureIcon::AlphaLock));
    m_positionLocked=button(flags,"layerPositionLocked","pin","Lock layer position",true);
    m_clipped=button(flags,"layerClipped","link-2","Clip to the layer below",true);
    m_clipped->setIcon(featureIcon(FeatureIcon::Clipping));
    m_annotation=button(flags,"layerAnnotation","sticky-note","Notes layer — exclude from movie exports",true);
    m_merge=button(flags,"mergeLayerDown","combine","Merge down"); flags->addStretch();
    m_more=button(flags,"layerMore","ellipsis","More layer actions"); layout->addLayout(flags);
    m_tree=new LayerTree; m_tree->setObjectName("layerTree"); m_tree->setAccessibleName("Layers and folders");
    m_tree->setColumnCount(3); m_tree->setHeaderHidden(true); m_tree->setTreePosition(1); m_tree->setIndentation(12);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection); m_tree->setEditTriggers(QAbstractItemView::DoubleClicked|QAbstractItemView::EditKeyPressed);
    m_tree->setUniformRowHeights(true); m_tree->setMinimumHeight(70); m_tree->setItemDelegate(new LayerDelegate(m_tree));
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); m_tree->setMouseTracking(true);
    m_tree->setStyleSheet("QTreeView { border: none; border-radius: 0; } QTreeView::item { padding: 0; border: none; }");
    m_tree->header()->setStretchLastSection(false); m_tree->header()->setSectionResizeMode(0,QHeaderView::Fixed); m_tree->setColumnWidth(0,ui::metrics::actionTarget);
    m_tree->header()->setSectionResizeMode(1,QHeaderView::Stretch); m_tree->header()->setSectionResizeMode(2,QHeaderView::Fixed); m_tree->setColumnWidth(2,ui::metrics::actionTarget);
    m_tree->setDragEnabled(true); m_tree->setAcceptDrops(true); m_tree->setDragDropMode(QAbstractItemView::InternalMove); m_tree->setDefaultDropAction(Qt::MoveAction); m_tree->setDropIndicatorShown(false);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu); layout->addWidget(m_tree,1);
    auto *footer=new QHBoxLayout; footer->setSpacing(1);
    m_group=button(footer,"addLayerGroup","folder-open","New folder from selected layers");
    m_add=button(footer,"addLayer","file-plus","New raster layer");
    m_importImage=button(footer,"importLayerImage","image","Import image as artwork layer");
    m_importImage->setEnabled(false);
    auto *vector=button(footer,"addVectorLayer","pen-tool","New vector layer");
    connect(vector,&QToolButton::clicked,this,[this]{ObjectTools::addLayer(m_document,LayerKind::Vector);});
    m_notes=button(footer,"addNotesLayer","sticky-note","New notes layer");
    m_duplicate=button(footer,"duplicateLayer","layers","Duplicate selected layers"); footer->addStretch();
    m_remove=button(footer,"removeLayer","trash","Delete selected layers · undo to restore"); layout->addLayout(footer); setWidget(m_content);
    m_thumbnailTimer=new QTimer(this); m_thumbnailTimer->setSingleShot(true); m_thumbnailTimer->setInterval(33);
    connect(m_thumbnailTimer,&QTimer::timeout,this,&LayerPanel::refreshThumbnails);
    connect(document,&Document::selectionChanged,this,[this]{
        if(!m_document->layerPropertyEditActive()){m_opacity->cancelEditing();m_tree->cancelEyes();}
        refresh();
    });
    connect(document,&Document::changed,this,[this] { m_dirty=true; refreshEnabled(); if(!m_thumbnailTimer->isActive()) m_thumbnailTimer->start(); if(m_document->layerPropertyEditActive()) refresh(); });
    connect(document,&Document::transformChanged,this,&LayerPanel::refreshEnabled);
    connect(document,&Document::frameChanged,this,&LayerPanel::refresh);
    connect(document,&Document::animationSelectionChanged,this,&LayerPanel::refresh);
    connect(m_tree,&QTreeWidget::currentItemChanged,this,[this](QTreeWidgetItem *item) {
        if(m_updating || !item || !m_document->isOpen()) return;
        if(!m_presentLayers.contains(idOf(item))) { refresh(); return; }
        const auto &layers=m_document->activeShot().layers;
        m_selecting=true; for(int i=0;i<layers.size();++i) if(layers[i].id==idOf(item)) { m_document->selectLayer(i); break; } m_selecting=false;
    });
    connect(m_tree,&QTreeWidget::itemSelectionChanged,this,[this]{
        if(m_updating)return;
        const auto selected=m_tree->selectedItems();
        if(!selected.isEmpty() && (!m_tree->currentItem() || !m_tree->currentItem()->isSelected()))
            m_tree->setCurrentItem(selected.first(),1,QItemSelectionModel::NoUpdate);
        refreshEnabled();
        emit selectedLayersChanged();
    });
    connect(m_tree,&QTreeWidget::itemChanged,this,[this](QTreeWidgetItem *item,int column) {
        if(m_updating || column!=1 || !m_document->isOpen()) return;
        const auto &layer=m_document->activeShot().layers[m_document->activeLayer()];
        if(layer.id==idOf(item)) m_document->changeLayer(item->text(1),layer.visible,layer.locked,layer.annotation,layer.opacity);
        refresh();
    });
    connect(m_blend,&QComboBox::currentIndexChanged,this,[this](int index){ if(!m_updating && index>=0) m_document->setLayerBlendMode(LayerBlendMode(m_blend->itemData(index).toInt())); });
    connect(m_opacity,&ValueSlider::editingStarted,this,[this]{m_document->beginLayerPropertyEdit();});
    connect(m_opacity,&ValueSlider::valueChanged,this,[this](double value){m_document->previewLayerOpacity(value/100);});
    connect(m_opacity,&ValueSlider::editingFinished,document,&Document::commitLayerPropertyEdit);
    connect(m_opacity,&ValueSlider::editingCancelled,document,&Document::cancelLayerPropertyEdit);
    connect(m_locked,&QToolButton::clicked,this,[this](bool v){m_document->setLayersLocked(selectedLayerIds(),v);});
    connect(m_positionLocked,&QToolButton::clicked,document,&Document::setLayerPositionLocked);
    connect(m_alphaLocked,&QToolButton::clicked,document,&Document::setLayerAlphaLocked);
    connect(m_clipped,&QToolButton::clicked,document,&Document::setLayerClipped);
    connect(m_annotation,&QToolButton::clicked,this,[this](bool value){const auto l=m_document->activeShot().layers[m_document->activeLayer()]; m_document->changeLayer(l.name,l.visible,l.locked,value,l.opacity);});
    connect(m_add,&QToolButton::clicked,this,[this]{m_document->addLayer();});
    connect(m_notes,&QToolButton::clicked,this,[this]{m_document->addLayer(true);});
    connect(m_group,&QToolButton::clicked,this,[this]{m_document->groupLayers(selectedLayerIds());});
    connect(m_duplicate,&QToolButton::clicked,this,[this]{m_document->duplicateLayers(selectedLayerIds());});
    connect(m_remove,&QToolButton::clicked,this,[this]{m_document->deleteLayers(selectedLayerIds());});
    connect(m_merge,&QToolButton::clicked,document,&Document::mergeDown);
    connect(m_tree,&QWidget::customContextMenuRequested,this,&LayerPanel::showContextMenu);
    connect(m_more,&QToolButton::clicked,this,[this]{showContextMenu(QPoint(-1,-1));});
    m_tree->beginEyes=[this](const QString &id,bool value){if(!m_document->beginLayerPropertyEdit("Layer visibility")) return false; m_document->previewLayersVisible({id},value); return true;};
    m_tree->previewEyes=[this](const QStringList &ids,bool value){m_document->previewLayersVisible(ids,value);};
    m_tree->finishEyes=[this](bool commit){if(commit)m_document->commitLayerPropertyEdit();else m_document->cancelLayerPropertyEdit();};
    m_tree->unlock=[this](const QString &id){m_document->setLayersLocked({id},false);};
    m_tree->deleteLayers=[this]{m_document->deleteLayers(selectedLayerIds());};
    m_tree->moveLayers=[this](const QStringList &ids,const QString &parent,const QString &before){m_document->moveLayers(ids,parent,before);};
    connect(document,&QObject::destroyed,this,[this]{
        m_thumbnailTimer->stop();
        for(auto *child:findChildren<QObject*>())child->disconnect(this);
        m_opacity->disconnect();
        m_tree->beginEyes={};m_tree->previewEyes={};m_tree->finishEyes={};m_tree->unlock={};m_tree->moveLayers={};m_tree->deleteLayers={};
        m_content->setEnabled(false);
    });
    refresh();
}
LayerPanel::~LayerPanel(){
    m_thumbnailTimer->stop();
    if(m_document)m_document->disconnect(this);
    for(auto *child:findChildren<QObject*>())child->disconnect(this);
    m_opacity->cancelEditing();
    if(m_document)m_document->cancelLayerPropertyEdit();
    m_tree->beginEyes={};m_tree->previewEyes={};m_tree->finishEyes={};m_tree->unlock={};m_tree->moveLayers={};m_tree->deleteLayers={};
}
QStringList LayerPanel::selectedLayerIds() const { QStringList ids; for(auto *item:m_tree->selectedItems())if(m_presentLayers.contains(idOf(item)))ids.append(idOf(item)); return ids; }
void LayerPanel::setImportImageAction(QAction *action) {
    m_importImage->setDefaultAction(action);
}
QTreeWidgetItem *LayerPanel::itemForId(const QString &id) const { return m_items.value(id); }
void LayerPanel::refreshEnabled() {
    if(m_updating)return;
    const bool editable=m_document&&m_document->isOpen()&&!m_document->editingGestureActive(); m_content->setEnabled(editable);
    if(!editable)return;
    const auto &shot=m_document->activeShot();const auto &layers=shot.layers; const auto &active=layers[m_document->activeLayer()];
    const bool locked=m_lockedLayers.contains(active.id),drawingContainer=m_drawingContainers.contains(active.id);
    const auto ids=selectedLayerIds();const bool single=ids.size()==1,animated=m_animationLayers.contains(active.id);
    m_blend->setEnabled(single&&!locked&&!drawingContainer); m_opacity->setEnabled(single&&!locked&&!drawingContainer);
    m_positionLocked->setEnabled(single&&!locked); m_clipped->setEnabled(single&&!locked&&!active.group&&!active.adjustment); m_alphaLocked->setEnabled(single&&!locked&&!active.group&&!active.adjustment&&active.kind==LayerKind::Raster);
    m_annotation->setEnabled(single&&!locked&&!animated&&!active.adjustment); m_locked->setEnabled(!ids.isEmpty());
    const int capacity=shot.animationTracks.isEmpty()?maxDrawingLayers:maxAnimationLayers;
    const bool room=layers.size()<capacity&&m_scopeNodes.value(m_scopes.value(active.id))<maxDrawingLayers;
    const bool trackContainer=m_containers.contains(active.id)&&!drawingContainer;
    m_add->setEnabled(room&&!locked&&!trackContainer); m_notes->setEnabled(m_add->isEnabled()&&!animated);
    findChild<QToolButton*>("addVectorLayer")->setEnabled(m_add->isEnabled());
    bool structural=!ids.isEmpty(),sameParent=true;
    for(const auto &id:ids){structural&=!m_containers.contains(id);sameParent&=m_parents.value(id)==active.parentId;}
    m_group->setEnabled(room&&structural&&sameParent&&!locked);
    QSet<QString> family;QStringList pending=structural?ids:QStringList();
    while(!pending.isEmpty()){const auto id=pending.takeLast();if(family.contains(id))continue;family.insert(id);pending+=m_children.value(id);}
    QHash<QString,int> extraNodes,removedArtwork;bool familyLocked=false;
    for(const auto &id:family){++extraNodes[m_scopes.value(id)];familyLocked|=m_lockedLayers.contains(id);
        const int index=m_layerIndices.value(id,-1);if(index>=0&&index<layers.size()&&!layers[index].group&&!layers[index].adjustment)++removedArtwork[m_scopes.value(id)];}
    bool duplicateRoom=layers.size()+family.size()<=capacity;
    for(auto it=extraNodes.cbegin();it!=extraNodes.cend();++it)duplicateRoom&=m_scopeNodes.value(it.key())+it.value()<=maxDrawingLayers;
    m_duplicate->setEnabled(structural&&duplicateRoom);
    int remaining=0;bool retainsDrawing=true;
    for(auto it=m_scopeArtwork.cbegin();it!=m_scopeArtwork.cend();++it){const int count=it.value()-removedArtwork.value(it.key());remaining+=count;if(!it.key().isEmpty()&&count<1)retainsDrawing=false;}
    m_remove->setEnabled(structural&&remaining>0&&retainsDrawing&&!familyLocked);
    int below=-1;for(int i=m_document->activeLayer()-1;i>=0;--i)if(layers[i].parentId==active.parentId){below=i;break;}
    m_merge->setEnabled(single&&!locked&&!active.group&&!active.adjustment&&below>=0&&!layers[below].adjustment);
}
void LayerPanel::refresh() {
    if(m_updating)return;
    if(!m_document||!m_document->isOpen()) {m_items.clear();m_presentLayers.clear();m_presentIndices.clear();m_tree->clear();m_structure.clear();m_dirty=true;refreshEnabled();return;}
    const auto &shot=m_document->activeShot(); const auto &layers=shot.layers; const auto active=layers[m_document->activeLayer()].id;
    QStringList drawings;for(const auto &track:shot.animationTracks){drawings.append(track.id);const auto *drawing=drawingAtFrame(track,m_document->currentFrame());drawings.append(drawing?drawing->id:QString());}
    // Frame, layer and timeline selection signals can describe the same view.
    // Holds also retain the same rows: neither case needs another tree walk.
    if(!m_dirty&&m_refreshedActive==active&&m_displayDrawings==drawings&&idOf(m_tree->currentItem())==active)return;
    m_updating=true;const QSignalBlocker blocker(m_tree),blendBlock(m_blend);
    m_dirty=false;m_refreshedActive=active;m_displayDrawings=drawings;
    m_presentLayers=activeLayerIds(shot,m_document->currentFrame());
    m_presentIndices.clear();m_layerIndices.clear();m_parents.clear();m_children.clear();m_scopes.clear();m_scopeNodes.clear();m_scopeArtwork.clear();
    m_animationLayers.clear();m_containers.clear();m_drawingContainers.clear();m_lockedLayers.clear();m_annotationLayers.clear();
    for(const auto &track:shot.animationTracks){m_containers.insert(track.rootGroupLayerId);for(const auto &drawing:track.drawings){m_containers.insert(drawing.groupLayerId);m_drawingContainers.insert(drawing.groupLayerId);}}
    const auto owners=animationOwners(shot);
    for(int i=0;i<layers.size();++i){const auto &layer=layers[i];m_layerIndices.insert(layer.id,i);m_parents.insert(layer.id,layer.parentId);m_children[layer.parentId].append(layer.id);
        if(m_presentLayers.contains(layer.id))m_presentIndices.append(i);
        const auto owner=owners.value(layer.id);m_scopes.insert(layer.id,owner.drawingId);
        if(!owner.trackId.isEmpty())m_animationLayers.insert(layer.id);
        if(owner.trackId.isEmpty()||!owner.drawingId.isEmpty()){++m_scopeNodes[owner.drawingId];if(!layer.group&&!layer.adjustment)++m_scopeArtwork[owner.drawingId];}}
    std::function<void(const QString&,bool,bool)> inherit=[&](const QString &parent,bool locked,bool annotation){for(const auto &id:m_children.value(parent)){
        const auto &layer=layers[m_layerIndices.value(id)];const bool childLocked=locked||layer.locked,childAnnotation=annotation||layer.annotation;
        if(childLocked)m_lockedLayers.insert(id);if(childAnnotation)m_annotationLayers.insert(id);inherit(id,childLocked,childAnnotation);
    }};inherit({},false,false);
    QStringList structure; for(const auto &layer:layers)structure.append(layer.id+":"+layer.parentId);
    auto selected=selectedLayerIds();const auto current=idOf(m_tree->currentItem());const int scroll=m_tree->verticalScrollBar()->value();
    if(structure!=m_structure){
        for(QTreeWidgetItemIterator it(m_tree);*it;++it)if((*it)->data(0,Group).toBool())m_expanded[idOf(*it)]=(*it)->isExpanded();
        m_items.clear();m_tree->clear();m_structure=structure;
        std::function<void(QString,QTreeWidgetItem*)> append=[&](QString parentId,QTreeWidgetItem *parent){const auto children=m_children.value(parentId);for(auto it=children.crbegin();it!=children.crend();++it){const auto &layer=layers[m_layerIndices.value(*it)];
            auto *item=parent?new QTreeWidgetItem(parent):new QTreeWidgetItem(m_tree);item->setData(0,Id,layer.id);item->setData(0,Group,layer.group);
            m_items.insert(layer.id,item);
            if(layer.group){append(layer.id,item);item->setExpanded(m_expanded.value(layer.id,true));}
        }};append({},nullptr);
    }
    for(const auto &layer:layers)if(auto *item=itemForId(layer.id)){
        const bool present=m_presentLayers.contains(layer.id);item->setHidden(!present);
        if(!present){item->setSelected(false);continue;}
        item->setText(1,layer.name);item->setData(0,Kind,int(layer.kind));item->setData(0,Group,layer.group);item->setData(0,Visible,layer.visible);item->setData(0,Locked,layer.locked);
        item->setData(0,Annotation,layer.annotation);item->setData(0,Clipped,layer.clipped);item->setData(0,AlphaLocked,layer.alphaLocked);item->setData(0,PositionLocked,layer.positionLocked);item->setData(0,RowColor,layer.rowColor);
        const bool effects=layer.effects.blurEnabled||layer.effects.shadowEnabled||layer.effects.glowEnabled||layer.effects.colorEnabled;
        item->setData(0,Effects,effects);item->setData(0,EffectsEnabled,layer.effects.enabled);item->setData(0,Mask,layer.mask.has_value());item->setData(0,MaskEnabled,layer.mask&&layer.mask->enabled);item->setData(0,Adjustment,layer.adjustment);
        QStringList details;if(layer.mask)details.append(layer.mask->enabled?"Mask":"Mask off");if(effects)details.append(layer.effects.enabled?"Effects":"Effects off");
        const QString detail=details.isEmpty()?QString():" · "+details.join(" · ");
        item->setToolTip(1,QString("%1 · %2 · %3 · %4%%5\nDouble-click to rename").arg(layer.name,layer.adjustment?"Color adjustment":layer.group?"Folder":layerKindKey(layer.kind),layerBlendModeName(layer.blendMode)).arg(qRound(layer.opacity*100)).arg(detail));
        item->setData(1,Qt::AccessibleTextRole,layer.name+detail+(layer.adjustment?" · Color adjustment":""));item->setData(0,Qt::AccessibleTextRole,layer.visible?"Visible":"Hidden");item->setData(2,Qt::AccessibleTextRole,layer.locked?"Locked":"Unlocked");
        auto flags=Qt::ItemIsEnabled|Qt::ItemIsSelectable;
        if(!m_containers.contains(layer.id))flags|=Qt::ItemIsDragEnabled;
        if(!m_lockedLayers.contains(layer.id))flags|=Qt::ItemIsEditable;
        if(layer.group&&(!m_containers.contains(layer.id)||m_drawingContainers.contains(layer.id)))flags|=Qt::ItemIsDropEnabled;item->setFlags(flags);
    }
    if(!m_selecting){
        if(active!=current&&!selected.contains(active))selected={active};
        if(selected.isEmpty())selected={active};
        for(auto *item:m_tree->selectedItems())item->setSelected(false);
        for(const auto &id:selected)if(m_presentLayers.contains(id))if(auto *item=itemForId(id))item->setSelected(true);
        m_tree->setCurrentItem(m_presentLayers.contains(active)?itemForId(active):nullptr,1,QItemSelectionModel::NoUpdate);
    }
    m_tree->verticalScrollBar()->setValue(scroll);
    const auto &layer=layers[m_document->activeLayer()];
    m_blend->setCurrentIndex(m_blend->findData(int(layer.blendMode)));m_opacity->setValueSilently(layer.opacity*100);
    m_locked->setChecked(layer.locked);m_positionLocked->setChecked(layer.positionLocked);m_alphaLocked->setChecked(layer.alphaLocked);m_clipped->setChecked(layer.clipped);m_annotation->setChecked(layer.annotation);
    QString base;for(int i=m_document->activeLayer()-1;i>=0;--i)if(layers[i].parentId==layer.parentId&&!layers[i].clipped){base=layers[i].name;break;}
    m_clipped->setToolTip(base.isEmpty()?"Clip to layer below · No base layer":"Clip to layer below · "+base);
    m_updating=false;refreshEnabled();if(!m_thumbnailTimer->isActive())m_thumbnailTimer->start();
}
void LayerPanel::refreshThumbnails(){
    if(!m_document||!m_document->isOpen())return;
    // A timeout scheduled before pen-down may still arrive during the stroke.
    // Its terminal changed notification schedules the completed preview.
    if(m_document->strokeActive())return;
    const auto &layers=m_document->activeShot().layers;
    for(const int index:m_presentIndices){if(index>=layers.size())continue;const auto &layer=layers[index];auto *item=itemForId(layer.id);if(!item||layer.group||!m_presentLayers.contains(layer.id))continue;
        const auto &project=m_document->project();const auto &sheet=m_document->activeShot().sheet;const qreal extra=sheet.overscan;
        QRectF bounds(-extra,-extra,project.width+2*extra,project.height+2*extra);
        if(m_annotationLayers.contains(layer.id))bounds.adjust(-sheet.left,-sheet.top,sheet.right,sheet.bottom);
        if(layer.scene3d){
            const auto image=scene3DPreview(*layer.scene3d,{project.width,project.height});
            if(!image.isNull()&&m_thumbnailKeys.value(layer.id)!=quint64(image.cacheKey())){
                m_thumbnailKeys[layer.id]=image.cacheKey();item->setData(0,Thumbnail,image.scaled(64,64,Qt::KeepAspectRatio,Qt::SmoothTransformation));
            }continue;
        }
        if(layer.symbol){
            QVector<RasterLayer> copy{layer};QString error;
            copy[0].visible=true;copy[0].opacity=1;copy[0].parentId.clear();copy[0].renderTransform={};
            Shot source;source.layers=copy;
            if(resolveSymbolLayers(project,source,m_document->currentFrame(),copy,bounds,&error)) {
                quint64 key=qHashMulti(copy[0].tiles.size()+1,bounds.x(),bounds.y(),bounds.width(),bounds.height());
                for(auto it=copy[0].tiles.cbegin();it!=copy[0].tiles.cend();++it)key=(key*1099511628211ULL)^quint64(it->cacheKey())^quint64(qHash(it.key()));
                if(m_thumbnailKeys.value(layer.id)!=key||item->data(0,Thumbnail).value<QImage>().isNull()) {
                    m_thumbnailKeys[layer.id]=key;item->setData(0,Thumbnail,renderLayerThumbnail(copy[0],bounds,QSize(64,64)));
                }
            }
            continue;
        }
        quint64 key=qHashMulti(layer.tiles.size()+1,bounds.x(),bounds.y(),bounds.width(),bounds.height());for(auto it=layer.tiles.cbegin();it!=layer.tiles.cend();++it)key=(key*1099511628211ULL)^quint64(it.value().cacheKey())^quint64(qHash(it.key()));
        if(m_thumbnailKeys.value(layer.id)==key&&!item->data(0,Thumbnail).value<QImage>().isNull())continue;
        m_thumbnailKeys[layer.id]=key;item->setData(0,Thumbnail,renderLayerThumbnail(layer,bounds,QSize(64,64)));
    }
}
void LayerPanel::showContextMenu(const QPoint &position){
    if(!m_document||!m_document->isOpen()||m_document->editingGestureActive())return;
    if(position.x()>=0)if(auto *item=m_tree->itemAt(position))if(!item->isSelected())m_tree->setCurrentItem(item,1,QItemSelectionModel::ClearAndSelect);
    const auto ids=selectedLayerIds();const auto &active=m_document->activeShot().layers[m_document->activeLayer()];
    QMenu menu(this);menu.setObjectName("layerContextMenu");menu.setToolTipsVisible(true);
    auto action=[&](const QString &name,const char *icon,const std::function<void()> &fn,bool enabled=true){auto *a=menu.addAction(appIcon(icon),name);a->setEnabled(enabled);connect(a,&QAction::triggered,this,fn);return a;};
    action("Rename","pen-tool",[this]{m_tree->editItem(m_tree->currentItem(),1);},ids.size()==1&&!active.locked);
    action("Duplicate","layers",[this,ids]{m_document->duplicateLayers(ids);},m_duplicate->isEnabled());
    action("New folder from selection","folder-open",[this,ids]{m_document->groupLayers(ids);},m_group->isEnabled());
    if(property("symbolsEnabled").toBool())action("Convert to symbol…","toon-symbol",[this,ids]{emit convertToSymbolRequested(ids);},!ids.isEmpty());
    if(property("symbolsEnabled").toBool()&&ids.size()==1&&active.symbol){
        action("Edit symbol…","pen-tool",[this]{emit symbolActionRequested("edit");});
        action("Make unique","copy",[this]{emit symbolActionRequested("unique");},!active.locked);
        action("Break apart at current frame","layers",[this]{emit symbolActionRequested("break-apart");},!active.locked);
    }
    menu.addSeparator();
    action("Merge Down","combine",[this]{m_document->mergeDown();},m_merge->isEnabled());
    action("Flatten Image","layers",[this]{m_document->flattenImage();},m_document->activeShot().animationTracks.isEmpty() && std::none_of(m_document->activeShot().layers.cbegin(),m_document->activeShot().layers.cend(),[](const RasterLayer &l){return l.symbol.has_value();}));
    auto *raster=action("Rasterize","image",[this]{ObjectTools::rasterize(m_document);},ids.size()==1&&layerHasEditableObjects(active)&&layerCanEdit(m_document->activeShot().layers,m_document->activeLayer()));
    raster->setToolTip("Convert editable vector paths or text to pixels · undo restores the source");
    auto *colors=menu.addMenu(appIcon("palette"),"Change Row Color");
    const QList<QPair<QString,QColor>> swatches={{"None",{}},{"Red",QColor("#e66b6b")},{"Orange",QColor("#e5a05a")},{"Yellow",QColor("#d5be59")},{"Green",QColor("#77bfaa")},{"Blue",QColor("#6995fe")},{"Violet",QColor("#b28de3")}};
    for(const auto &swatch:swatches){QPixmap icon(14,14);icon.fill(swatch.second.isValid()?swatch.second:QColor(ui::base));auto *a=colors->addAction(QIcon(icon),swatch.first);a->setCheckable(true);a->setChecked(active.rowColor==swatch.second);connect(a,&QAction::triggered,this,[this,ids,color=swatch.second]{m_document->setLayersRowColor(ids,color);});}
    menu.addSeparator();
    action("Move up","arrow-up",[this]{m_document->moveLayer(1);},ids.size()==1&&!m_containers.contains(active.id));
    action("Move down","arrow-down",[this]{m_document->moveLayer(-1);},ids.size()==1&&!m_containers.contains(active.id));
    action("Move to top level","arrow-left-right",[this,ids]{m_document->moveLayers(ids,{},{});},!active.parentId.isEmpty()&&!m_animationLayers.contains(active.id));
    action("New text layer","type",[this]{ObjectTools::addLayer(m_document,LayerKind::Text);},m_add->isEnabled()&&!m_animationLayers.contains(active.id));
    action("New notes layer","sticky-note",[this]{m_document->addLayer(true);},m_notes->isEnabled());
    menu.addSeparator();action("Delete","trash",[this,ids]{m_document->deleteLayers(ids);},m_remove->isEnabled());
    menu.exec(position.x()<0?m_more->mapToGlobal(QPoint(0,m_more->height())):m_tree->viewport()->mapToGlobal(position));
}
} // namespace toon
