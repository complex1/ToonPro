#include "brush_management_panel.h"
#include "brush_engine.h"
#include "brush_panel.h"
#include "canvas.h"
#include "icons.h"
#include "property_row.h"
#include "tool_catalog.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include <QAction>
#include <QButtonGroup>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QResizeEvent>
#include <QSet>
#include <QTabBar>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace toon {
namespace {
constexpr QSize previewSize{ui::metrics::previewWidth, ui::metrics::previewHeight};

class ElidedLabel final : public QLabel {
public:
    ElidedLabel() {
        setTextFormat(Qt::PlainText);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setMinimumHeight(qMax(ui::metrics::actionTarget, fontMetrics().height() + 2));
    }
protected:
    void changeEvent(QEvent *event) override {
        QLabel::changeEvent(event);
        if (event->type() == QEvent::FontChange) {
            setMinimumHeight(qMax(ui::metrics::actionTarget, fontMetrics().height() + 2));
            updateGeometry();
        }
    }
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setPen(palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::WindowText));
        painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter,
                         fontMetrics().elidedText(text(), Qt::ElideRight, width()));
    }
};

class BrushList final : public QListWidget {
public:
    using QListWidget::QListWidget;
protected:
    void changeEvent(QEvent *event) override {
        QListWidget::changeEvent(event);
        if (event->type() != QEvent::FontChange && event->type() != QEvent::StyleChange) return;
        for (int row = 0; row < count(); ++row)
            item(row)->setSizeHint({0, qMax(ui::metrics::toolRow, fontMetrics().height() + ui::metrics::sectionGap)});
    }
};

class MetadataPages final : public QStackedWidget {
public:
    using QStackedWidget::QStackedWidget;
    QSize minimumSizeHint() const override { return currentWidget() ? currentWidget()->minimumSizeHint() : QSize(); }
    QSize sizeHint() const override { return currentWidget() ? currentWidget()->sizeHint() : QSize(); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override {
        if (!currentWidget()) return 0;
        const int height = currentWidget()->heightForWidth(width);
        return height >= 0 ? height : currentWidget()->sizeHint().height();
    }
};

class BrushTypeChoices final : public QWidget {
public:
    BrushTypeChoices() {
        m_grid = new QGridLayout(this); m_grid->setContentsMargins(0,0,0,0);
        m_grid->setSpacing(ui::metrics::controlGap);
    }
    void addChoice(QToolButton *choice) { m_choices.append(choice); arrange(); }
protected:
    void resizeEvent(QResizeEvent *event) override { QWidget::resizeEvent(event); arrange(); }
    void changeEvent(QEvent *event) override {
        QWidget::changeEvent(event);
        if (event->type() == QEvent::FontChange)
            QTimer::singleShot(0,this,[this]{arrange();});
    }
private:
    void arrange() {
        int widthPerChoice = 0;
        for (auto *choice : m_choices)
            widthPerChoice = qMax(widthPerChoice, choice->fontMetrics().horizontalAdvance(choice->text())
                                 + ui::metrics::primaryIcon + ui::metrics::controlGap + 2 * ui::metrics::sectionGap);
        const int columns = width() >= 2 * widthPerChoice + ui::metrics::controlGap ? 2 : 1;
        if (columns == m_columns && m_grid->count() == m_choices.size()) return;
        m_columns = columns;
        for (int i = 0; i < m_choices.size(); ++i) m_grid->addWidget(m_choices[i], i / columns, i % columns);
        m_grid->setColumnStretch(0,1); m_grid->setColumnStretch(1,columns == 2 ? 1 : 0);
        updateGeometry();
    }
    QGridLayout *m_grid;
    QList<QToolButton *> m_choices;
    int m_columns = 0;
};

class DetailsScroll final : public QScrollArea {
public:
    using QScrollArea::QScrollArea;
    QSize minimumSizeHint() const override { return {0, 56}; }
    QSize sizeHint() const override {
        if (!widget()) return QScrollArea::sizeHint();
        const int height = widget()->heightForWidth(qMax(1, viewport()->width()));
        return {240, qMax(widget()->minimumSizeHint().height(), height)};
    }
};

QIcon typePlaceholder(BrushType type) {
    static QHash<int, QIcon> placeholders;
    if (placeholders.contains(int(type))) return placeholders.value(int(type));
    QIcon icon;
    for (const qreal ratio : {1., 2., 3.}) {
        QPixmap image(previewSize * ratio); image.setDevicePixelRatio(ratio); image.fill(Qt::transparent);
        QPainter painter(&image);
        appIcon(brushTypeIcon(type)).paint(&painter, QRect(13, 2, 20, 20));
        painter.end(); icon.addPixmap(image);
    }
    placeholders.insert(int(type), icon);
    return icon;
}

void addPropertyRow(QVBoxLayout *layout, const QString &text, QWidget *control) {
    layout->addWidget(new PropertyRow(text, control));
}

constexpr auto types = {BrushType::Pen, BrushType::Ink, BrushType::Soft, BrushType::Spray,
                        BrushType::Stamp, BrushType::ColoredStamp, BrushType::Custom, BrushType::DynamicStamp};
QToolButton *button(const char *id, const char *icon, const QString &help) {
    auto *b = new QToolButton; b->setObjectName(id); b->setIcon(appIcon(icon)); b->setIconSize({ui::metrics::actionIcon,ui::metrics::actionIcon});
    b->setProperty("compactDrawingAction", true);
    b->setFixedSize(ui::metrics::actionTarget,ui::metrics::actionTarget); b->setToolTip(help); b->setAccessibleName(help); return b;
}
QLineEdit *field(const char *id, const QString &hint) {
    auto *f = new QLineEdit; f->setObjectName(id); f->setPlaceholderText(hint); f->setAccessibleName(hint);
    f->setProperty("compactDrawingField", true); f->setMinimumWidth(0); return f;
}
QComboBox *combo(const char *id, const QString &help) {
    auto *c = new QComboBox; c->setObjectName(id); c->setAccessibleName(help); c->setToolTip(help);
    c->setProperty("compactDrawingField", true); c->setMinimumWidth(0); c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); c->setMinimumContentsLength(5); return c;
}
QLabel *label(const QString &text = {}) { auto *l = new QLabel(text); l->setTextFormat(Qt::PlainText); l->setWordWrap(true); return l; }
QStringList tagsFromText(const QString &value) {
    QStringList tags; for (const auto &tag : value.split(',')) if (!tag.trimmed().isEmpty()) tags.append(tag.trimmed()); return tags;
}
bool imageFile(const QString &path, QImage &image, QString *error) {
    const auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (QFileInfo(path).size() > 32 * 1024 * 1024) return fail("Choose an image smaller than 32 MB.");
    QImageReader reader(path); reader.setAutoTransform(true);
    const auto size = reader.size();
    if (size.isEmpty() || size.width() > 8192 || size.height() > 8192 || qint64(size.width()) * size.height() > 32 * 1024 * 1024)
        return fail("Choose an image up to 8192 × 8192 and 32 million pixels.");
    if (size.width() > BrushLibrary::maxTipSide || size.height() > BrushLibrary::maxTipSide)
        reader.setScaledSize(size.scaled(BrushLibrary::maxTipSide, BrushLibrary::maxTipSide, Qt::KeepAspectRatio).expandedTo({1,1}));
    image = reader.read();
    if (image.isNull()) return fail("Could not read this image: " + reader.errorString());
    return true;
}
bool tipImage(const QImage &source, bool colored, QImage &result, QString *error) {
    const auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (source.isNull()) return fail("Choose a stamp image first.");
    auto image = source;
    if (image.width() > BrushLibrary::maxTipSide || image.height() > BrushLibrary::maxTipSide)
        image = image.scaled(image.size().scaled(BrushLibrary::maxTipSide, BrushLibrary::maxTipSide, Qt::KeepAspectRatio).expandedTo({1,1}), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    image = image.convertToFormat(QImage::Format_ARGB32);
    bool translucent = false;
    for (int y = 0; y < image.height() && !translucent; ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) if (qAlpha(row[x]) < 255) { translucent = true; break; }
    }
    // Transparent artwork uses its alpha as the silhouette. Opaque scans use
    // dark ink on white paper as coverage. Colored stamps retain original RGBA.
    result = QImage(image.size(), colored ? QImage::Format_ARGB32_Premultiplied : QImage::Format_Grayscale8);
    if (result.isNull()) return fail("There is not enough memory to create this stamp.");
    bool visible = false;
    for (int y = 0; y < image.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const int coverage = colored || translucent ? qAlpha(row[x]) : 255 - qGray(row[x]);
            visible |= coverage > 0;
            if (colored) reinterpret_cast<QRgb *>(result.scanLine(y))[x] = qPremultiply(row[x]);
            else result.scanLine(y)[x] = uchar(coverage);
        }
    }
    return visible || fail("The stamp has no visible pixels.");
}
void choices(QComboBox *c, const QString &all, QStringList values) {
    const auto selected = c->currentData().toString(); const QSignalBlocker blocker(c);
    values.removeDuplicates(); values.sort(Qt::CaseInsensitive); c->clear(); c->addItem(all, QString());
    for (const auto &value : values) if (!value.isEmpty()) c->addItem(value, value);
    // Keep a filter that has become empty, so deletion does not unexpectedly
    // expose and select unrelated brushes underneath the Delete button.
    if (!selected.isEmpty() && c->findData(selected) < 0) c->addItem(selected, selected);
    c->setCurrentIndex(qMax(0, c->findData(selected)));
}
QListWidgetItem *findRow(QListWidget *list, const QString &id) {
    for (int row = 0; row < list->count(); ++row) if (list->item(row)->data(Qt::UserRole).toString() == id) return list->item(row);
    return nullptr;
}
}

BrushManagementPanel::BrushManagementPanel(Canvas *canvas, BrushLibrary *library, ToolCatalog *catalog, QWidget *parent)
    : QWidget(parent), m_canvas(canvas), m_library(library), m_catalog(catalog) {
    setObjectName("brushManagement"); setMinimumWidth(240); setFocusPolicy(Qt::StrongFocus);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(ui::metrics::panelMargin,ui::metrics::panelMargin,ui::metrics::panelMargin,ui::metrics::panelMargin); layout->setSpacing(ui::metrics::controlGap);
    auto *actions = new QHBoxLayout; actions->setSpacing(2);
    m_add = button("brushManagerAdd", "plus", "New brush");
    m_import = button("brushManagerImport", "folder-open", "Import Photoshop brushes or a stamp image");
    m_duplicate = button("brushManagerDuplicate", "copy", "Duplicate selected brush");
    m_remove = button("brushManagerDelete", "trash", "Delete selected brushes from all groups · Undo to restore");
    for (auto *b : {m_add,m_import,m_duplicate,m_remove}) actions->addWidget(b);
    actions->addStretch();
    auto *undo = button("brushManagerUndo", "undo-2", "Undo brush organization");
    auto *redo = button("brushManagerRedo", "redo-2", "Redo brush organization");
    actions->addWidget(undo); actions->addWidget(redo); layout->addLayout(actions);
    connect(undo,&QToolButton::clicked,this,[this]{m_catalog->history()->undo();});
    connect(redo,&QToolButton::clicked,this,[this]{m_catalog->history()->redo();});
    connect(catalog->history(),&QUndoStack::canUndoChanged,undo,&QWidget::setEnabled);
    connect(catalog->history(),&QUndoStack::canRedoChanged,redo,&QWidget::setEnabled);
    undo->setEnabled(catalog->history()->canUndo()); redo->setEnabled(catalog->history()->canRedo());
    auto *search = new QHBoxLayout; search->setSpacing(ui::metrics::controlGap);
    m_tags = combo("brushTagFilter", "Filter brushes by tag");
    m_search = field("brushSearch", "Search brushes"); m_search->setClearButtonEnabled(true);
    for (auto *b : m_search->findChildren<QToolButton *>()) { b->setToolTip("Clear brush search"); b->setAccessibleName(b->toolTip()); }
    search->addWidget(m_tags,2); search->addWidget(m_search,3); layout->addLayout(search);
    auto *filters = new QHBoxLayout; filters->setSpacing(ui::metrics::controlGap);
    m_categories = combo("brushCategoryFilter", "Filter brushes by category");
    m_types = combo("brushTypeFilter", "Filter brushes by type"); m_types->addItem("All types", -1);
    for (auto type : types) m_types->addItem(appIcon(brushTypeIcon(type)),brushTypeName(type),int(type));
    filters->addWidget(m_categories); filters->addWidget(m_types); layout->addLayout(filters);
    m_list = new BrushList; m_list->setObjectName("managedBrushes"); m_list->setAccessibleName("Brush library");
    m_list->setIconSize(previewSize); m_list->setUniformItemSizes(true); m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); m_list->setTextElideMode(Qt::ElideRight); m_list->setMinimumHeight(96);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu); layout->addWidget(m_list,1);
    m_empty = label("No matching brushes"); m_empty->setObjectName("brushManagerEmpty"); layout->addWidget(m_empty);
    auto *heading = new QHBoxLayout; heading->setSpacing(4); m_summary = new ElidedLabel;
    m_summary->setObjectName("brushManagerSummary"); heading->addWidget(m_summary,1);
    m_use = button("brushManagerUse", "brush", "Use selected brush"); m_advanced = button("brushManagerAdvanced", "wrench", "Edit advanced brush settings");
    heading->addWidget(m_use); heading->addWidget(m_advanced); layout->addLayout(heading);
    auto *metadataTabs = new QTabBar; metadataTabs->setObjectName("brushManagerMetadataTabs");
    metadataTabs->setProperty("compactDrawingTabs", true); metadataTabs->setIconSize({ui::metrics::tabIcon, ui::metrics::tabIcon});
    metadataTabs->setExpanding(true); metadataTabs->setFocusPolicy(Qt::StrongFocus);
    metadataTabs->setAccessibleName("Selected brush details");
    metadataTabs->addTab(appIcon("brush"), "Brush"); metadataTabs->addTab(appIcon("folder-open"), "Organize");
    metadataTabs->setTabToolTip(0, "Edit the selected brush name and category"); metadataTabs->setAccessibleTabName(0, "Brush details");
    metadataTabs->setTabToolTip(1, "Manage tags and add selected brushes to a tool group"); metadataTabs->setAccessibleTabName(1, "Organize brushes");
    layout->addWidget(metadataTabs);
    auto *detailScroll = new DetailsScroll; detailScroll->setObjectName("brushManagerDetailsScroll");
    detailScroll->setFrameShape(QFrame::NoFrame); detailScroll->setWidgetResizable(true);
    detailScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); detailScroll->setMinimumHeight(56);
    detailScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *pages = new MetadataPages; pages->setObjectName("brushManagerMetadataPages"); m_details = pages;
    auto *brushDetails = new QWidget; pages->addWidget(brushDetails);
    auto *detail = new QVBoxLayout(brushDetails); detail->setContentsMargins(0,0,0,0); detail->setSpacing(ui::metrics::controlGap);
    detail->setAlignment(Qt::AlignTop);
    m_name = field("brushManagerName", "Brush name"); m_name->setMaxLength(120); addPropertyRow(detail,"Name",m_name);
    m_category = combo("brushManagerCategory", "Brush category"); m_category->setEditable(true); m_category->setInsertPolicy(QComboBox::NoInsert); m_category->lineEdit()->setMaxLength(80);
    addPropertyRow(detail,"Category",m_category);
    auto *organization = new QWidget; pages->addWidget(organization);
    detail = new QVBoxLayout(organization); detail->setContentsMargins(0,0,0,0); detail->setSpacing(ui::metrics::controlGap);
    detail->setAlignment(Qt::AlignTop);
    m_tagSummary = new ElidedLabel; m_tagSummary->setObjectName("brushManagerTags"); detail->addWidget(m_tagSummary);
    auto *tagRow = new QHBoxLayout; tagRow->setSpacing(4); m_tag = field("brushManagerTag", "Tag"); m_tag->setMaxLength(ToolCatalog::maxTagLength);
    auto *addTag = button("brushManagerAddTag", "plus", "Add tag to selected brushes");
    auto *removeTag = button("brushManagerRemoveTag", "minus", "Remove tag from selected brushes");
    tagRow->addWidget(m_tag,1); tagRow->addWidget(addTag); tagRow->addWidget(removeTag); detail->addLayout(tagRow);
    auto *groupRow = new QHBoxLayout; groupRow->setSpacing(4); m_group = combo("brushManagerGroup", "Destination tool group");
    m_assign = button("brushManagerAssignGroup", "folder-open", "Add selected brushes to this tool group");
    groupRow->addWidget(m_group,1); groupRow->addWidget(m_assign); detail->addLayout(groupRow);
    detailScroll->setWidget(m_details); layout->addWidget(detailScroll);
    connect(metadataTabs, &QTabBar::currentChanged, pages, [pages, detailScroll](int index) {
        pages->setCurrentIndex(index); pages->updateGeometry(); detailScroll->updateGeometry();
    });
    auto *status = new QHBoxLayout; status->setSpacing(4);
    m_error = new ElidedLabel;
    m_error->setObjectName("brushManagerError"); m_error->setAccessibleName("Brush management status"); status->addWidget(m_error,1);
    m_importDetails = button("brushManagerImportDetails", "ellipsis", "Show brush import details");
    m_importDetails->hide(); status->addWidget(m_importDetails); layout->addLayout(status);
    connect(m_importDetails,&QToolButton::clicked,this,[this]{
        QMessageBox report(QMessageBox::Information,"Brush import",m_error->text(),QMessageBox::Ok,this);
        report.setDetailedText(m_importReport);report.exec();
    });
    connect(m_add,&QToolButton::clicked,this,[this]{createDialog();});
    connect(m_import,&QToolButton::clicked,this,&BrushManagementPanel::importDialog);
    connect(m_duplicate,&QToolButton::clicked,this,&BrushManagementPanel::duplicateSelected);
    connect(m_remove,&QToolButton::clicked,this,&BrushManagementPanel::deleteSelected);
    connect(m_use,&QToolButton::clicked,this,[this]{const auto ids=selectedIds();if(ids.size()==1&&!m_catalog->activateTool(ids.first()))setError("Finish the current edit before switching brushes.");});
    connect(m_advanced,&QToolButton::clicked,this,[this]{const auto ids=selectedIds();if(ids.size()==1)emit advancedSettingsRequested(ids.first());});
    connect(m_list,&QListWidget::itemDoubleClicked,this,[this]{m_use->click();});
    connect(m_list,&QListWidget::itemSelectionChanged,this,&BrushManagementPanel::refreshDetails);
    connect(m_search,&QLineEdit::textChanged,this,&BrushManagementPanel::filter);
    for (auto *c : {m_tags,m_categories,m_types}) connect(c,&QComboBox::currentIndexChanged,this,&BrushManagementPanel::filter);
    connect(m_name,&QLineEdit::editingFinished,this,[this]{
        if(m_refreshing||m_inspected.size()!=1)return;
        if(!m_catalog->renameTool(m_inspected.first(),m_name->text()))setError("Enter a brush name from 1 to 120 characters.");
    });
    const auto categoryEdit = [this] {
        if(m_refreshing||m_inspected.isEmpty())return;
        if(!m_catalog->setBrushCategory(m_inspected,m_category->currentText()))setError("Could not change the category.");
    };
    connect(m_category->lineEdit(),&QLineEdit::editingFinished,this,categoryEdit);
    connect(m_category,&QComboBox::activated,this,categoryEdit);
    connect(addTag,&QToolButton::clicked,this,[this]{editTag(true);});
    connect(removeTag,&QToolButton::clicked,this,[this]{editTag(false);});
    connect(m_tag,&QLineEdit::returnPressed,this,[this]{editTag(true);});
    connect(m_assign,&QToolButton::clicked,this,[this]{
        const auto ids=selectedIds(); const auto *group=m_catalog->entry(m_group->currentData().toString());
        if(ids.isEmpty()||!group||!group->group)return;
        auto tools=group->tools; for(const auto &id:ids)if(!tools.contains(id))tools.append(id);
        if(!m_catalog->setEntryTools(group->id,tools))setError("Could not add brushes to this group.");
    });
    connect(m_list,&QWidget::customContextMenuRequested,this,[this](const QPoint &point){
        if(auto *row=m_list->itemAt(point);row&&!row->isSelected())m_list->setCurrentItem(row);
        QMenu menu(this); menu.setToolTipsVisible(true);
        for(auto *b:{m_use,m_advanced,m_duplicate,m_remove}) {
            auto *a=menu.addAction(b->icon(),b->toolTip());a->setEnabled(b->isEnabled());connect(a,&QAction::triggered,b,&QToolButton::click);
        }
        menu.exec(m_list->viewport()->mapToGlobal(point));
    });
    connect(catalog,&ToolCatalog::changed,this,&BrushManagementPanel::refresh);
    connect(catalog,&ToolCatalog::settingsChanged,this,[this](const QString &id){m_previews.remove(id);m_previewTimer.start();});
    connect(catalog,&ToolCatalog::warning,this,&BrushManagementPanel::setError);
    connect(canvas,&Canvas::documentChanged,this,[this]{if(isVisible())m_previewTimer.start();});
    m_previewTimer.setInterval(8);m_previewTimer.setSingleShot(true);
    connect(&m_previewTimer,&QTimer::timeout,this,&BrushManagementPanel::refreshPreviews);
    connect(m_list->verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{m_previewTimer.start();});
    refresh();focusBrush(catalog->activeToolId());
}
BrushManagementPanel::~BrushManagementPanel() {
    // QWidget hides focused editors after our members have been destroyed.
    // Their editingFinished signals must not commit against a dead inspector.
    m_previewTimer.stop();
    for (auto *child : findChildren<QObject *>()) QObject::disconnect(child,nullptr,this,nullptr);
}
QStringList BrushManagementPanel::selectedIds() const {
    QStringList result;for(auto *row:m_list->selectedItems())if(!row->isHidden())result.append(row->data(Qt::UserRole).toString());return result;
}
void BrushManagementPanel::setError(const QString &message) {
    m_error->setText(message);m_error->setToolTip(message);m_error->setAccessibleDescription(message);
    m_importReport.clear();m_importDetails->hide();
}
void BrushManagementPanel::refresh() {
    if(m_refreshing)return; QScopedValueRollback guard(m_refreshing,true); const QSignalBlocker listBlock(m_list);
    QSet<QString> ids;QStringList tags,categories;
    for(const auto &tool:m_catalog->availableTools())if(tool.engine==Canvas::Tool::Brush){
        ids.insert(tool.id);tags+=tool.tags;if(!tool.category.isEmpty())categories+=tool.category;
        auto *row=findRow(m_list,tool.id);if(!row){row=new QListWidgetItem(m_list);row->setData(Qt::UserRole,tool.id);}
        row->setSizeHint({0,qMax(ui::metrics::toolRow,m_list->fontMetrics().height()+ui::metrics::sectionGap)});
        const auto description=brushTypeName(tool.brushType)+(tool.category.isEmpty()?QString():" · "+tool.category);
        row->setText(tool.name);row->setToolTip(tool.name+"\n"+description+(tool.tags.isEmpty()?QString():"\n"+tool.tags.join(", ")));
        row->setData(Qt::AccessibleTextRole,tool.name+", "+description);
        if(m_previews.contains(tool.id))row->setIcon(m_previews.value(tool.id));else row->setIcon(typePlaceholder(tool.brushType));
    }
    for(int i=m_list->count()-1;i>=0;--i)if(!ids.contains(m_list->item(i)->data(Qt::UserRole).toString()))delete m_list->takeItem(i);
    for(auto it=m_previews.begin();it!=m_previews.end();)if(!ids.contains(it.key()))it=m_previews.erase(it);else++it;
    choices(m_tags,"All tags",tags);choices(m_categories,"All categories",categories);
    const auto currentGroup=m_group->currentData().toString();const QSignalBlocker groupBlock(m_group);m_group->clear();
    for(const auto &entry:m_catalog->availableEntries())if(entry.group)m_group->addItem(entry.name,entry.id);
    int group=m_group->findData(currentGroup.isEmpty()?m_catalog->selectedEntryId():currentGroup);
    if(group<0)group=m_group->findData("group/drawing");m_group->setCurrentIndex(qMax(0,group));
    m_add->setEnabled(m_catalog->writable());m_import->setEnabled(m_catalog->writable()&&m_library->writable());
    m_refreshing=false;filter();m_previewTimer.start();
}
void BrushManagementPanel::filter() {
    if(m_refreshing)return;
    {const QSignalBlocker blocker(m_list);const auto query=m_search->text().trimmed();
        for(int i=0;i<m_list->count();++i){auto *row=m_list->item(i);const auto *tool=m_catalog->tool(row->data(Qt::UserRole).toString());
            const bool show=tool&&(query.isEmpty()||(tool->name+" "+tool->category+" "+tool->tags.join(' ')+" "+brushTypeName(tool->brushType)).contains(query,Qt::CaseInsensitive))
                &&(m_tags->currentData().toString().isEmpty()||tool->tags.contains(m_tags->currentData().toString(),Qt::CaseInsensitive))
                &&(m_categories->currentData().toString().isEmpty()||tool->category.compare(m_categories->currentData().toString(),Qt::CaseInsensitive)==0)
                &&(m_types->currentData().toInt()<0||int(tool->brushType)==m_types->currentData().toInt());
            row->setHidden(!show);if(!show)row->setSelected(false);
        }
    }
    bool any=false;for(int i=0;i<m_list->count();++i)any|=!m_list->item(i)->isHidden();m_empty->setVisible(!any);
    refreshDetails();m_previewTimer.start();
}
void BrushManagementPanel::refreshDetails() {
    if(m_refreshing)return; QScopedValueRollback guard(m_refreshing,true);
    const auto ids=selectedIds();const bool targetChanged=ids!=m_inspected;m_inspected=ids;
    const bool writable=m_catalog->writable(),single=ids.size()==1;
    m_details->setEnabled(writable&&!ids.isEmpty());m_remove->setEnabled(writable&&!ids.isEmpty());
    m_duplicate->setEnabled(writable&&single);m_use->setEnabled(single);m_advanced->setEnabled(single&&m_canvas->hasProject());
    m_assign->setEnabled(!ids.isEmpty()&&m_group->count()>0);m_name->setEnabled(single);
    if(ids.isEmpty()){
        m_summary->setText("Select a brush");m_summary->setToolTip({});m_summary->setAccessibleName("Select a brush");
        m_name->clear();m_category->clear();m_tagSummary->clear();m_tagSummary->setToolTip({});m_tagSummary->setAccessibleName("Brush tags");return;
    }
    const auto *first=m_catalog->tool(ids.first());if(!first)return;
    m_summary->setText(single?brushTypeName(first->brushType)+(first->category.isEmpty()?QString():" · "+first->category):QString("%1 brushes selected").arg(ids.size()));
    m_summary->setToolTip(m_summary->text()); m_summary->setAccessibleName(m_summary->text());
    if(targetChanged||!m_name->hasFocus())m_name->setText(single?first->name:QString());
    QString category=first->category;QStringList allTags;bool mixed=false;
    for(const auto &id:ids)if(const auto *tool=m_catalog->tool(id)){mixed|=tool->category!=category;allTags+=tool->tags;}
    allTags.removeDuplicates();allTags.sort(Qt::CaseInsensitive);m_tagSummary->setText(allTags.isEmpty()?"No tags":allTags.join(" · "));
    m_tagSummary->setToolTip(m_tagSummary->text()); m_tagSummary->setAccessibleName("Brush tags: "+m_tagSummary->text());
    if(targetChanged||(!m_category->hasFocus()&&!m_category->lineEdit()->hasFocus())) {
        QStringList categories;for(const auto &tool:m_catalog->availableTools())if(!tool.category.isEmpty())categories.append(tool.category);
        categories.removeDuplicates();categories.sort(Qt::CaseInsensitive);const QSignalBlocker blocker(m_category);
        m_category->clear();m_category->addItems(categories);m_category->setEditText(mixed?QString():category);
        m_category->lineEdit()->setPlaceholderText(mixed?"Mixed categories":"Category");
    }
}
void BrushManagementPanel::refreshPreviews() {
    // Populate a few previews per event-loop turn; metadata edits and drawing
    // do not regenerate an entire imported pack. No document signal is used.
    if(!isVisible()||m_canvas->document()->strokeActive())return;
    int built=0;
    for(int i=0;i<m_list->count();++i){auto *row=m_list->item(i);const auto id=row->data(Qt::UserRole).toString();
        if(row->isHidden()||m_previews.contains(id)||!m_list->visualItemRect(row).intersects(m_list->viewport()->rect()))continue;
        const auto *tool=m_catalog->tool(id);if(!tool)continue;
        auto brush=tool->state.brush;const auto oldSize=brush.size;brush.size=qBound(12.,oldSize,32.);brush.dual.size*=brush.size/qMax(1.,oldSize);
        brush.color=QColor(ui::text);brush.backgroundColor=QColor(ui::base);
        auto image=brushStrokePreview(brush,previewSize*4,QColor(ui::base)).scaled(previewSize*2,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        image.setDevicePixelRatio(2);QIcon icon(QPixmap::fromImage(image));m_previews.insert(id,icon);row->setIcon(icon);
        if(++built==3){m_previewTimer.start();break;}
    }
}
void BrushManagementPanel::showEvent(QShowEvent *event) {QWidget::showEvent(event);refresh();m_previewTimer.start();}
void BrushManagementPanel::focusBrush(const QString &id) {
    m_search->clear();m_tags->setCurrentIndex(0);m_categories->setCurrentIndex(0);m_types->setCurrentIndex(0);
    if(auto *row=findRow(m_list,id)){m_list->setCurrentItem(row,QItemSelectionModel::ClearAndSelect);m_list->scrollToItem(row);}
    refreshDetails();m_previewTimer.start();
}
QString BrushManagementPanel::addBrush(const QString &name,BrushType type,const QString &category,const QStringList &tags,
                                     const QString &destination,const QImage &image,bool fromCurrent,QString *error) {
    ToolState state;
    if(fromCurrent){state.brush=m_canvas->brush;state.smoothing=m_canvas->stabilization();}
    else{state.brush=brushSettingsForType(type);state.smoothing=brushTypeSmoothing(type);}
    if(type==BrushType::Stamp||type==BrushType::ColoredStamp){
        if(!image.isNull()){
            QImage tip;if(!tipImage(image,type==BrushType::ColoredStamp,tip,error))return {};
            state.brush.tipMask={};state.brush.colorTip={};state.brush.tipFrames.clear();
            if(type==BrushType::ColoredStamp)state.brush.colorTip=tip;else state.brush.tipMask=tip;
        }else if((type==BrushType::Stamp&&state.brush.tipMask.isNull())||(type==BrushType::ColoredStamp&&state.brush.colorTip.isNull())){
            if(error)*error="Choose a stamp image first.";return {};
        }
    }
    state.brush.color=BrushSettings{}.color;state.brush.backgroundColor=BrushSettings{}.backgroundColor;
    if(type==BrushType::DynamicStamp){
        if(!fromCurrent || (state.brush.tipMask.isNull()&&state.brush.colorTip.isNull())){
            if(error)*error="Choose between 2 and 16 stamp images.";return {};
        }
        state.options["stampPlacement"]=false;
    }
    return createBrush(name,type,state,category,tags,destination,error);
}
QString BrushManagementPanel::createBrush(const QString &name,BrushType type,const ToolState &state,const QString &category,
                                        const QStringList &tags,const QString &destination,QString *error) {
    QString reason;
    const auto failure = connect(m_catalog,&ToolCatalog::warning,this,[&reason](const QString &message){reason=message;});
    const auto id=m_catalog->createBrush(name,type,state,category,tags,destination);
    disconnect(failure);
    if(id.isEmpty()){
        if(error)*error=reason.isEmpty()?"Could not create this brush. Check its name, tags and library capacity.":reason;
        return {};
    }
    focusBrush(id);setError({});return id;
}
QString BrushManagementPanel::addDynamicStampBrush(const QString &name,const QVector<QImage> &images,bool preserveColors,
                                                  const QString &category,const QStringList &tags,const QString &destination,QString *error) {
    if(images.size()<2 || images.size()>16){if(error)*error="Choose between 2 and 16 stamp images.";return {};}
    QVector<QImage> tips;tips.reserve(images.size());
    for(const auto &source:images){
        auto image=source;
        if(image.width()>512 || image.height()>512)image=image.scaled(512,512,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        QImage tip;if(!tipImage(image,preserveColors,tip,error))return {};
        tips.append(tip);
    }
    ToolState state{brushSettingsForType(BrushType::DynamicStamp),brushTypeSmoothing(BrushType::DynamicStamp),{}};
    if(!setBrushTipImages(state.brush,tips,{},error))return {};
    state.brush.colorDynamics.applyToColorTip=preserveColors;
    state.options["stampPlacement"]=false;
    return createBrush(name,BrushType::DynamicStamp,state,category,tags,destination,error);
}
bool BrushManagementPanel::importDynamicStampBrush(const QStringList &paths,const QString &name,bool preserveColors,
                                                  const QString &category,const QStringList &tags,const QString &destination,QString *error) {
    if(paths.size()<2 || paths.size()>16){if(error)*error="Choose between 2 and 16 stamp images.";return false;}
    QVector<QImage> images;images.reserve(paths.size());
    for(const auto &path:paths){
        QImage image;if(!imageFile(path,image,error))return false;
        if(image.width()>512 || image.height()>512)image=image.scaled(512,512,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        images.append(image);
    }
    return !addDynamicStampBrush(name,images,preserveColors,category,tags,destination,error).isEmpty();
}
bool BrushManagementPanel::importImageBrush(const QString &path,const QString &name,BrushType type,const QString &category,const QStringList &tags,QString *error) {
    if(type!=BrushType::Stamp&&type!=BrushType::ColoredStamp){if(error)*error="Choose Stamp or Colored stamp for an image brush.";return false;}
    QImage image;if(!imageFile(path,image,error))return false;
    return !addBrush(name,type,category,tags,{},image,false,error).isEmpty();
}
bool BrushManagementPanel::importAbrFile(const QString &path,QString *error) {
    QString report;QStringList ids;if(!importAbrIntoLibrary(m_library,path,&report,error,&ids))return false;
    if(!ids.isEmpty())focusBrush("brush/"+ids.first());
    setError(QString("Imported %1 brushes").arg(ids.size()));
    m_importReport=report;m_importDetails->setVisible(!report.isEmpty());return true;
}
void BrushManagementPanel::duplicateSelected() {
    const auto ids=selectedIds();if(ids.size()!=1)return;const auto *tool=m_catalog->tool(ids.first());if(!tool)return;
    const auto id=m_catalog->createBrush(tool->name+" copy",tool->brushType,tool->state,tool->category,tool->tags);
    if(id.isEmpty())setError("Could not duplicate this brush.");else focusBrush(id);
}
void BrushManagementPanel::deleteSelected() {
    const auto ids=selectedIds();if(ids.isEmpty())return;
    if(!m_catalog->deleteBrushes(ids))setError("Could not delete these brushes. Finish the current edit first.");
    else setError(QString("Deleted %1 brushes · Undo to restore").arg(ids.size()));
}
void BrushManagementPanel::editTag(bool add) {
    const auto ids=selectedIds();if(ids.isEmpty())return;
    const bool ok=add?m_catalog->addToolTag(ids,m_tag->text()):m_catalog->removeToolTag(ids,m_tag->text());
    if(!ok)setError("Enter a tag of 1–40 characters; each brush supports up to 32 tags.");else{m_tag->clear();setError({});}
}
void BrushManagementPanel::importDialog() {
    const auto path=QFileDialog::getOpenFileName(this,"Import brushes",{},"Brushes and images (*.abr *.png *.jpg *.jpeg *.webp *.bmp);;Photoshop brushes (*.abr);;Images (*.png *.jpg *.jpeg *.webp *.bmp)");
    if(path.isEmpty())return;
    if(QFileInfo(path).suffix().compare("abr",Qt::CaseInsensitive)==0){QString error;if(!importAbrFile(path,&error))setError(error);}
    else createDialog(path);
}
void BrushManagementPanel::createDialog(const QString &imagePath) {
    QDialog dialog(this);dialog.setObjectName("newBrushDialog");dialog.setWindowTitle("New brush");dialog.resize(380,480);dialog.setMinimumSize(320,360);
    auto *dialogLayout=new QVBoxLayout(&dialog);dialogLayout->setContentsMargins(ui::metrics::sectionGap,ui::metrics::sectionGap,ui::metrics::sectionGap,ui::metrics::sectionGap);dialogLayout->setSpacing(ui::metrics::controlGap);
    auto *tabs=new QTabBar;tabs->setObjectName("newBrushSource");tabs->setIconSize({ui::metrics::tabIcon,ui::metrics::tabIcon});tabs->setExpanding(false);
    tabs->setProperty("compactDrawingTabs",true);
    tabs->addTab(appIcon("plus"),"New brush");tabs->addTab(appIcon("brush"),"Current brush");
    tabs->setTabToolTip(0,"Create a brush from a type or stamp image");tabs->setTabToolTip(1,"Save a copy of the current brush settings");
    tabs->setAccessibleName("New brush source");dialogLayout->addWidget(tabs);
    auto *scroll=new QScrollArea;scroll->setObjectName("newBrushFormScroll");scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);scroll->setMinimumSize(0,0);
    auto *body=new QWidget;auto *layout=new QVBoxLayout(body);layout->setContentsMargins(0,0,ui::metrics::controlGap,0);layout->setSpacing(ui::metrics::controlGap);
    scroll->setWidget(body);dialogLayout->addWidget(scroll,1);
    auto *typeGrid=new BrushTypeChoices;typeGrid->setObjectName("newBrushTypeChoices");auto *group=new QButtonGroup(&dialog);int index=0;
    const QStringList keys{"Pen","Ink","Soft","Spray","Stamp","ColoredStamp","Custom","DynamicStamp"};
    for(auto type:types){auto *b=new QToolButton;b->setObjectName("brushType_"+keys[index]);b->setText(brushTypeName(type));b->setIcon(appIcon(brushTypeIcon(type)));
        b->setIconSize({ui::metrics::primaryIcon,ui::metrics::primaryIcon});b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);b->setCheckable(true);b->setMinimumHeight(ui::metrics::actionTarget);
        b->setProperty("compactDrawingAction",true);b->setMinimumWidth(0);
        b->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);b->setToolTip(brushTypeName(type)+" brush");b->setAccessibleName(b->toolTip());
        group->addButton(b,int(type));typeGrid->addChoice(b);++index;}
    group->button(int(BrushType::Ink))->setChecked(true);layout->addWidget(typeGrid);
    auto *name=field("newBrushName","Brush name");name->setMaxLength(120);name->setText("New ink");addPropertyRow(layout,"Name",name);
    auto *category=field("newBrushCategory","Category");category->setMaxLength(80);addPropertyRow(layout,"Category",category);
    auto *tags=field("newBrushTags","Tags, separated by commas");addPropertyRow(layout,"Tags",tags);
    auto *destination=combo("newBrushGroup","Add to tool group");destination->addItem("No group",QString());
    for(const auto &entry:m_catalog->availableEntries())if(entry.group)destination->addItem(entry.name,entry.id);
    destination->setCurrentIndex(qMax(0,destination->findData(m_group->currentData())));addPropertyRow(layout,"Group",destination);
    auto *tipRow=new QHBoxLayout;tipRow->setSpacing(4);auto *choose=button("newBrushImage","image","Choose stamp image");
    auto *preview=label("Choose a stamp image");preview->setObjectName("newBrushImagePreview");preview->setMinimumHeight(64);tipRow->addWidget(choose);tipRow->addWidget(preview,1);layout->addLayout(tipRow);
    auto *stampSet=new QWidget;stampSet->setObjectName("newBrushStampSet");
    auto *stampLayout=new QVBoxLayout(stampSet);stampLayout->setContentsMargins(0,0,0,0);stampLayout->setSpacing(4);
    auto *colorMode=new QTabBar;colorMode->setObjectName("newBrushStampColorMode");colorMode->setAccessibleName("Stamp image colors");
    colorMode->setExpanding(false);colorMode->setIconSize({ui::metrics::tabIcon,ui::metrics::tabIcon});colorMode->setProperty("compactDrawingTabs",true);
    colorMode->addTab(appIcon("brush"),"Foreground");colorMode->addTab(appIcon("palette"),"Image colors");
    colorMode->setTabToolTip(0,"Use the foreground color: transparent images use alpha; opaque images use dark ink as coverage");
    colorMode->setTabToolTip(1,"Preserve image colors and transparency; saturation varies between stamps");stampLayout->addWidget(colorMode);
    auto *stampActions=new QHBoxLayout;stampActions->setSpacing(4);
    auto *addImages=button("newBrushStampAdd","plus","Add stamp images · 2–16 tips, fitted to 512 pixels each");
    auto *removeImage=button("newBrushStampRemove","trash","Remove selected stamp image");
    auto *stampCount=label();stampCount->setObjectName("newBrushStampCount");stampActions->addWidget(addImages);stampActions->addWidget(removeImage);stampActions->addWidget(stampCount);stampActions->addStretch();stampLayout->addLayout(stampActions);
    auto *stampImages=new QListWidget;stampImages->setObjectName("newBrushStampImages");stampImages->setAccessibleName("Dynamic stamp images");
    stampImages->setViewMode(QListView::IconMode);stampImages->setFlow(QListView::LeftToRight);stampImages->setWrapping(false);
    stampImages->setMovement(QListView::Static);stampImages->setIconSize({42,42});stampImages->setGridSize({50,50});stampImages->setFixedHeight(74);
    stampImages->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);stampLayout->addWidget(stampImages);layout->addWidget(stampSet);
    auto *error=label();error->setObjectName("newBrushError");dialogLayout->addWidget(error);layout->addStretch();
    QImage image;QString sourcePath;
    QVector<QImage> stampSources;QStringList stampNames;
    const auto refreshStampImages=[&]{
        const int selected=stampImages->currentRow();stampImages->clear();
        for(int i=0;i<stampSources.size();++i){
            QImage tip;if(!tipImage(stampSources[i],colorMode->currentIndex()==1,tip,nullptr))tip=stampSources[i];
            if(tip.format()==QImage::Format_Grayscale8){
                QImage colored(tip.size(),QImage::Format_ARGB32_Premultiplied);const auto color=QColor(ui::text);
                for(int y=0;y<tip.height();++y)for(int x=0;x<tip.width();++x)
                    reinterpret_cast<QRgb *>(colored.scanLine(y))[x]=qPremultiply(qRgba(color.red(),color.green(),color.blue(),tip.constScanLine(y)[x]));
                tip=colored;
            }
            const auto thumbnail=tip.scaled(84,84,Qt::KeepAspectRatio,Qt::SmoothTransformation);
            QPixmap pixmap=QPixmap::fromImage(thumbnail);pixmap.setDevicePixelRatio(2);
            auto *item=new QListWidgetItem(QIcon(pixmap),{},stampImages);item->setToolTip(stampNames[i]);item->setData(Qt::AccessibleTextRole,stampNames[i]);
        }
        if(!stampSources.isEmpty())stampImages->setCurrentRow(qBound(0,selected,int(stampSources.size())-1));
        stampCount->setText(QString("%1 / 16 tips").arg(stampSources.size()));
        addImages->setEnabled(stampSources.size()<16);removeImage->setEnabled(!stampSources.isEmpty());
    };
    connect(colorMode,&QTabBar::currentChanged,&dialog,[&]{refreshStampImages();});
    connect(removeImage,&QToolButton::clicked,&dialog,[&]{const int row=stampImages->currentRow();if(row<0)return;stampSources.removeAt(row);stampNames.removeAt(row);refreshStampImages();error->clear();});
    connect(addImages,&QToolButton::clicked,&dialog,[&]{
        const auto paths=QFileDialog::getOpenFileNames(&dialog,"Add stamp images",sourcePath,"Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)");
        if(paths.isEmpty())return;
        if(stampSources.size()+paths.size()>16){error->setText("A brush supports up to 16 stamp images.");return;}
        QVector<QImage> loaded;
        for(const auto &path:paths){
            QImage source;QString message;if(!imageFile(path,source,&message)){error->setText(message);return;}
            if(source.width()>512 || source.height()>512)source=source.scaled(512,512,Qt::KeepAspectRatio,Qt::SmoothTransformation);
            QImage tip;if(!tipImage(source,colorMode->currentIndex()==1,tip,&message)){error->setText(QFileInfo(path).fileName()+": "+message);return;}
            loaded.append(source);
        }
        const int firstAdded=stampSources.size();stampSources+=loaded;for(const auto &path:paths)stampNames.append(QFileInfo(path).fileName());
        sourcePath=paths.last();refreshStampImages();stampImages->setCurrentRow(firstAdded);error->clear();
        if(!name->isModified())name->setText(QFileInfo(paths.first()).completeBaseName()+" stamps");
    });
    const auto loadImage=[&](const QString &path){QString message;QImage loaded;if(!imageFile(path,loaded,&message)){error->setText(message);return;}
        image=loaded;sourcePath=path;error->clear();preview->setPixmap(QPixmap::fromImage(image.scaled(200,64,Qt::KeepAspectRatio,Qt::SmoothTransformation)));preview->setToolTip(QFileInfo(path).fileName());
        if(!name->isModified())name->setText(QFileInfo(path).completeBaseName());};
    connect(choose,&QToolButton::clicked,&dialog,[&]{const auto path=QFileDialog::getOpenFileName(&dialog,"Stamp image",sourcePath,"Images (*.png *.jpg *.jpeg *.webp *.bmp)");if(!path.isEmpty())loadImage(path);});
    const auto updateType=[&]{const auto type=BrushType(group->checkedId());const bool sampled=tabs->currentIndex()==0&&(type==BrushType::Stamp||type==BrushType::ColoredStamp);
        choose->setVisible(sampled);preview->setVisible(sampled);
        stampSet->setVisible(tabs->currentIndex()==0&&type==BrushType::DynamicStamp);
        if(tabs->currentIndex()==0&&!name->isModified()&&image.isNull()&&stampSources.isEmpty())name->setText("New "+brushTypeName(type).toLower());};
    connect(group,&QButtonGroup::idClicked,&dialog,[&](int){updateType();});
    connect(tabs,&QTabBar::currentChanged,&dialog,[&](int current){
        for(auto *b:group->buttons())b->setEnabled(current==0);
        if(current==1){const auto *tool=m_catalog->tool(m_catalog->activeToolId());
            group->button(int(tool&&tool->engine==Canvas::Tool::Brush?tool->brushType:inferredBrushType("",m_canvas->brush)))->setChecked(true);
            if(!name->isModified())name->setText(tool?tool->name+" copy":"Current brush copy");
        }updateType();
    });
    if(!imagePath.isEmpty()){group->button(int(BrushType::Stamp))->setChecked(true);loadImage(imagePath);}refreshStampImages();updateType();
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);auto *create=buttons->button(QDialogButtonBox::Ok);
    create->setText("Create");create->setObjectName("createBrush");
    for(auto *action:buttons->buttons())action->setProperty("compactDrawingButton",true);
    dialogLayout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{QString message;
        const auto type=BrushType(group->checkedId());
        const auto id=tabs->currentIndex()==0&&type==BrushType::DynamicStamp
            ?addDynamicStampBrush(name->text(),stampSources,colorMode->currentIndex()==1,category->text(),tagsFromText(tags->text()),destination->currentData().toString(),&message)
            :addBrush(name->text(),type,category->text(),tagsFromText(tags->text()),destination->currentData().toString(),tabs->currentIndex()==1?QImage():image,tabs->currentIndex()==1,&message);
        if(id.isEmpty())error->setText(message);else dialog.accept();});
    dialog.exec();
}
} // namespace toon
