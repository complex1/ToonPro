#include "character_model_panel.h"
#include "document.h"
#include "icons.h"
#include "property_row.h"
#include "reference_panel.h"
#include "ui_metrics.h"
#include "value_slider.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace toon {
namespace {
QString categoryName(ModelSheetCategory category) {
    switch (category) {
    case ModelSheetCategory::Turnaround: return "Turnarounds";
    case ModelSheetCategory::Expression: return "Expressions";
    case ModelSheetCategory::Costume: return "Costumes";
    }
    return {};
}
QString categoryIcon(ModelSheetCategory category) {
    switch (category) {
    case ModelSheetCategory::Turnaround: return "toon-turnaround";
    case ModelSheetCategory::Expression: return "toon-expression";
    case ModelSheetCategory::Costume: return "toon-costume";
    }
    return {};
}
constexpr ModelSheetCategory categories[] = {ModelSheetCategory::Turnaround, ModelSheetCategory::Expression, ModelSheetCategory::Costume};

class CharacterCategoryTabs final : public QTabBar {
public:
    using QTabBar::QTabBar;
    QSize minimumSizeHint() const override {
        auto size = QTabBar::minimumSizeHint();
        // Full names are optional decoration. They must never force a narrow
        // dock wider, including while the font or theme is being changed.
        size.setWidth(count() * qMax(ui::metrics::actionTarget, iconSize().width() + 2 * ui::metrics::controlGap));
        return size;
    }
protected:
    void resizeEvent(QResizeEvent *event) override { QTabBar::resizeEvent(event); updateLabels(); }
    void changeEvent(QEvent *event) override {
        QTabBar::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) updateLabels();
    }
    void tabInserted(int index) override { QTabBar::tabInserted(index); updateLabels(); }
private:
    void updateLabels() {
        if (m_updating) return;
        int fullWidth = 0;
        for (int index = 0; index < count(); ++index) {
            // Start with Qt's actual style size, retaining its icon, border and
            // padding metrics, and substitute the complete label's text width.
            const auto label = categoryName(categories[index]);
            fullWidth += QTabBar::tabSizeHint(index).width()
                + fontMetrics().size(Qt::TextShowMnemonic, label).width()
                - fontMetrics().size(Qt::TextShowMnemonic, tabText(index)).width();
        }
        const bool showNames = width() >= fullWidth;
        m_updating = true;
        for (int index = 0; index < count(); ++index) {
            const auto label = showNames ? categoryName(categories[index]) : QString();
            if (tabText(index) != label) setTabText(index, label);
        }
        m_updating = false;
    }
    bool m_updating = false;
};

class SheetThumbnailDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        auto thumbnail = option; initStyleOption(&thumbnail, index);
        thumbnail.text.clear(); thumbnail.features &= ~QStyleOptionViewItem::HasDisplay;
        thumbnail.decorationAlignment = Qt::AlignCenter;
        const auto *widget = thumbnail.widget;
        (widget ? widget->style() : QApplication::style())->drawControl(QStyle::CE_ItemViewItem, &thumbnail, painter, widget);
    }
};
QIcon thumbnail(const QImage &image) {
    QIcon icon;
    for (const qreal ratio : {1., 2., 3.}) {
        QPixmap pixmap(QSize(40, 40) * ratio); pixmap.setDevicePixelRatio(ratio); pixmap.fill(Qt::transparent);
        const auto size = image.size().scaled(QSize(40, 40), Qt::KeepAspectRatio);
        QPainter painter(&pixmap); painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(QRectF(QPointF((40 - size.width()) / 2.0, (40 - size.height()) / 2.0), size), image);
        painter.end(); icon.addPixmap(pixmap);
    }
    return icon;
}
QString caption(const CharacterModelSheet &sheet) {
    return QString("%1 · %2 × %3").arg(sheet.name).arg(sheet.image.width()).arg(sheet.image.height());
}
}

CharacterModelPanel::CharacterModelPanel(Document *document, QWidget *parent)
    : QWidget(parent), m_document(document) {
    setObjectName("characterModelPanel"); setMinimumWidth(212); setProperty("compactDrawingPanel", true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(ui::metrics::panelMargin, ui::metrics::panelMargin,
                              ui::metrics::panelMargin, ui::metrics::panelMargin);
    layout->setSpacing(ui::metrics::controlGap);
    const auto button = [this](const QString &name, const QString &icon, const QString &label) {
        auto *result = new QToolButton(this); result->setObjectName(name);
        result->setIcon(appIcon(icon)); result->setAccessibleName(label); result->setToolTip(label);
        result->setProperty("compactDrawingAction", true);
        result->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon});
        result->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
        return result;
    };
    auto *characterRow = new QHBoxLayout; characterRow->setSpacing(ui::metrics::controlGap);
    m_characters = new QComboBox(this); m_characters->setObjectName("characterSelector");
    m_characters->setAccessibleName("Character"); m_characters->setToolTip("Characters shared across all shots in this project");
    m_characters->setProperty("compactDrawingField", true);
    m_characters->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_characters->setMinimumContentsLength(4); m_characters->setMinimumWidth(40);
    m_characters->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_characters->setPlaceholderText("Choose character");
    m_addCharacter = button("addCharacter", "plus", "Add character");
    m_renameCharacter = button("renameCharacter", "pencil", "Rename character");
    m_removeCharacter = button("removeCharacter", "trash", "Remove character, model sheets and colors (Undo available)");
    characterRow->addWidget(m_characters, 1); characterRow->addWidget(m_addCharacter);
    characterRow->addWidget(m_renameCharacter); characterRow->addWidget(m_removeCharacter); layout->addLayout(characterRow);
    m_categories = new CharacterCategoryTabs(this); m_categories->setObjectName("characterModelTabs");
    m_categories->setAccessibleName("Character model sheet categories");
    m_categories->setProperty("compactDrawingTabs", true); m_categories->setIconSize({ui::metrics::tabIcon, ui::metrics::tabIcon});
    m_categories->setExpanding(true); m_categories->setElideMode(Qt::ElideNone);
    m_categories->setUsesScrollButtons(false); m_categories->setFocusPolicy(Qt::StrongFocus);
    for (auto category : categories) {
        const auto index = m_categories->addTab(appIcon(categoryIcon(category)), categoryName(category));
        m_categories->setTabData(index, int(category));
        m_categories->setTabToolTip(index, categoryName(category));
        m_categories->setAccessibleTabName(index, categoryName(category));
    }
    layout->addWidget(m_categories);
    auto *sheetRow = new QHBoxLayout; sheetRow->setSpacing(ui::metrics::controlGap);
    m_import = button("importCharacterSheets", "image", "Import model sheet images");
    m_import->setToolTip("Import PNG, JPEG or BMP model sheets; embed copies up to 2048 px per side");
    m_name = new QLineEdit(this); m_name->setObjectName("characterSheetName"); m_name->setMaxLength(100);
    m_name->setPlaceholderText("Sheet name"); m_name->setAccessibleName("Model sheet name");
    m_name->setToolTip("Rename the selected model sheet"); m_name->setProperty("compactDrawingField", true);
    m_name->setMinimumWidth(0); m_name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_name->installEventFilter(this);
    m_removeSheet = button("removeCharacterSheet", "trash", "Remove model sheet (Undo available)");
    sheetRow->addWidget(m_import); sheetRow->addWidget(m_name, 1); sheetRow->addWidget(m_removeSheet); layout->addLayout(sheetRow);
    m_sheets = new QListWidget(this); m_sheets->setObjectName("characterSheetList");
    m_sheets->setAccessibleName("Character model sheets"); m_sheets->setViewMode(QListView::IconMode);
    m_sheets->setFlow(QListView::LeftToRight); m_sheets->setWrapping(false); m_sheets->setMovement(QListView::Static);
    m_sheets->setIconSize({40, 40}); m_sheets->setGridSize({48, 48}); m_sheets->setItemDelegate(new SheetThumbnailDelegate(m_sheets));
    m_sheets->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); m_sheets->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_sheets->setTextElideMode(Qt::ElideRight); m_sheets->setFixedHeight(68);
    m_sheets->setContextMenuPolicy(Qt::CustomContextMenu); layout->addWidget(m_sheets);
    m_view = new ReferenceView(this); m_view->setObjectName("characterModelView");
    m_view->setAccessibleName("Character model sheet viewer");
    m_view->setToolTip("Drag to pan; scroll to zoom; Alt-click to sample a color; right-click to organize the sheet");
    m_view->setContextMenuPolicy(Qt::CustomContextMenu); layout->addWidget(m_view, 1);
    m_zoom = new ValueSlider("Model sheet zoom", 1, 1600, 100, 1, " %", this);
    m_zoom->setObjectName("characterModelZoom"); m_zoom->setSingleStep(10);
    m_fit = button("fitCharacterSheet", "scan", "Fit model sheet");
    auto *zoomRow = new PropertyRow("Zoom", m_zoom, this); zoomRow->setObjectName("characterModelZoomRow");
    zoomRow->setAccessory(m_fit); layout->addWidget(zoomRow);

    connect(m_characters, &QComboBox::currentIndexChanged, this, [this] {
        if (m_refreshing) return;
        selectCharacter(m_characters->currentData().toString());
    });
    connect(m_categories, &QTabBar::currentChanged, this, [this] {
        commitSheetName(); m_zoom->commitEditing(); refreshSheets();
    });
    connect(m_sheets, &QListWidget::currentRowChanged, this, &CharacterModelPanel::selectSheet);
    connect(m_sheets, &QListWidget::customContextMenuRequested, this, &CharacterModelPanel::showSheetMenu);
    connect(m_view, &QWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        showSheetActions(m_view->mapToGlobal(point));
    });
    connect(m_name, &QLineEdit::editingFinished, this, &CharacterModelPanel::commitSheetName);
    connect(m_addCharacter, &QToolButton::clicked, this, [this] {
        bool accepted = false;
        const auto name = QInputDialog::getText(this, "Add character", "Name", QLineEdit::Normal, {}, &accepted);
        if (!accepted) return;
        QString error; const auto id = m_document->addCharacter(name, &error);
        if (id.isEmpty()) reportError(error); else selectCharacter(id);
    });
    connect(m_renameCharacter, &QToolButton::clicked, this, [this] {
        const auto *entry = character(); if (!entry) return;
        const auto id = entry->id; const auto original = entry->name; bool accepted = false;
        const auto name = QInputDialog::getText(this, "Rename character", "Name", QLineEdit::Normal, original, &accepted);
        if (!accepted) return;
        QString error; if (!m_document->renameCharacter(id, name, &error)) reportError(error);
    });
    connect(m_removeCharacter, &QToolButton::clicked, this, [this] {
        QString error; if (!m_document->removeCharacter(m_characterId, &error)) reportError(error);
    });
    connect(m_import, &QToolButton::clicked, this, [this] {
        const auto paths = QFileDialog::getOpenFileNames(this, "Import character model sheets", {}, "Images (*.png *.jpg *.jpeg *.bmp)");
        if (paths.isEmpty()) return;
        QString error; if (!importImages(paths, &error)) reportError(error);
    });
    connect(m_removeSheet, &QToolButton::clicked, this, [this] {
        QString error; if (!m_document->removeCharacterSheet(m_characterId, m_sheetId, &error)) reportError(error);
    });
    connect(m_zoom, &ValueSlider::valueChanged, this, [this](double value) { m_view->setZoom(value / 100); });
    connect(m_fit, &QToolButton::clicked, m_view, &ReferenceView::fitImage);
    connect(m_view, &ReferenceView::colorSampled, this, &CharacterModelPanel::colorSampled);
    const auto syncZoom = [this] {
        if (m_zoomEditing) return;
        const QSignalBlocker blocker(m_zoom); m_zoom->setValue(m_view->zoom() * 100);
    };
    connect(m_view, &ReferenceView::viewChanged, this, syncZoom);
    connect(m_zoom, &ValueSlider::editingStarted, this, [this] { m_zoomEditing = true; });
    connect(m_zoom, &ValueSlider::editingFinished, this, [this, syncZoom] { m_zoomEditing = false; syncZoom(); });
    connect(m_zoom, &ValueSlider::editingCancelled, this, [this, syncZoom] { m_zoomEditing = false; syncZoom(); });
    connect(m_document, &Document::selectionChanged, this, &CharacterModelPanel::refresh);
    connect(m_document, &Document::loaded, this, [this] {
        m_selection.clear(); m_characterId.clear(); m_sheetId.clear(); m_characterState.clear(); m_sheetState.clear();
        m_name->setModified(false); refresh();
    });
    refresh();
}

ModelSheetCategory CharacterModelPanel::selectedCategory() const {
    return ModelSheetCategory(m_categories->tabData(m_categories->currentIndex()).toInt());
}
const CharacterModel *CharacterModelPanel::character() const {
    if (m_document->isOpen()) for (const auto &entry : m_document->project().characters)
        if (entry.id == m_characterId) return &entry;
    return nullptr;
}
const CharacterModelSheet *CharacterModelPanel::sheet() const {
    if (const auto *entry = character()) for (const auto &candidate : entry->sheets)
        if (candidate.id == m_sheetId) return &candidate;
    return nullptr;
}
QString CharacterModelPanel::selectionKey() const { return m_characterId + ':' + QString::number(int(selectedCategory())); }

void CharacterModelPanel::selectCharacter(const QString &id) {
    if (id == m_characterId) return;
    bool found = false;
    if (m_document->isOpen()) for (const auto &entry : m_document->project().characters) if (entry.id == id) { found = true; break; }
    if (!found) return;
    commitSheetName(); m_zoom->commitEditing(); m_characterId = id;
    { const QSignalBlocker block(m_characters); m_characters->setCurrentIndex(m_characters->findData(id)); }
    refreshSheets();
    emit characterSelected(m_characterId);
}
void CharacterModelPanel::selectCategory(ModelSheetCategory category) {
    for (int index = 0; index < m_categories->count(); ++index)
        if (m_categories->tabData(index).toInt() == int(category)) { m_categories->setCurrentIndex(index); return; }
}
void CharacterModelPanel::setSampling(bool enabled) { m_view->setSampling(enabled); }
void CharacterModelPanel::reportError(const QString &error) {
    if (!error.isEmpty()) QMessageBox::warning(this, "Character model sheets", error);
}

bool CharacterModelPanel::importImages(const QStringList &paths, QString *error) {
    const auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (!m_document->isOpen()) return fail("Open a project before importing character model sheets.");
    if (!character()) return fail("Add or select a character before importing model sheets.");
    if (m_document->editingGestureActive() || m_document->layerPropertyEditActive())
        return fail("Finish the current edit before importing character model sheets.");
    if (paths.isEmpty()) return fail("Choose one or more model sheet images.");
    qsizetype count = 0; qint64 pixels = 0;
    for (const auto &entry : m_document->project().characters) for (const auto &image : entry.sheets) {
        ++count; pixels += qint64(image.image.width()) * image.image.height();
    }
    if (paths.size() > maxProjectCharacterSheets - count)
        return fail(QString("A project supports up to %1 character model sheet images.").arg(maxProjectCharacterSheets));
    QVector<CharacterModelSheet> images; images.reserve(paths.size());
    for (const auto &path : paths) {
        CharacterModelSheet image; image.category = selectedCategory();
        if (!readReferenceImage(path, &image.image, &image.name, error)) return false;
        pixels += qint64(image.image.width()) * image.image.height();
        if (pixels > maxProjectCharacterSheetPixels)
            return fail("Character model sheets exceed the project's 64 megapixel image budget. Import smaller images or remove unused sheets.");
        images.append(std::move(image));
    }
    if (!m_document->addCharacterSheets(m_characterId, images, error)) return false;
    m_sheets->setCurrentRow(m_sheets->count() - 1);
    return true;
}

void CharacterModelPanel::refresh() {
    const bool open = m_document->isOpen();
    m_addCharacter->setEnabled(open); m_characters->setEnabled(open);
    QStringList state;
    if (open) for (const auto &entry : m_document->project().characters) state << entry.id << entry.name;
    if (state != m_characterState) {
        m_refreshing = true; m_characterState = state;
        const QSignalBlocker block(m_characters); m_characters->clear();
        if (open) for (const auto &entry : m_document->project().characters) m_characters->addItem(entry.name, entry.id);
        auto index = m_characters->findData(m_characterId); if (index < 0) index = m_characters->count() ? 0 : -1;
        m_characters->setCurrentIndex(index); m_characterId = m_characters->currentData().toString();
        m_refreshing = false;
    }
    const auto *entry = character();
    m_characters->setToolTip(entry ? entry->name + " · Shared across all shots" : "Characters shared across all shots in this project");
    m_renameCharacter->setEnabled(entry); m_removeCharacter->setEnabled(entry);
    m_import->setEnabled(entry); m_categories->setEnabled(entry);
    refreshSheets();
}
void CharacterModelPanel::refreshSheets() {
    const auto *entry = character(); const auto category = selectedCategory();
    QStringList state{m_characterId, QString::number(int(category))};
    if (entry) for (const auto &image : entry->sheets) if (image.category == category)
        state << image.id << image.name << QString::number(image.image.cacheKey());
    if (state == m_sheetState) return;
    m_sheetState = state;
    const auto selected = m_selection.value(selectionKey());
    const QSignalBlocker block(m_sheets); m_sheets->clear(); int selectedRow = 0;
    if (entry) for (const auto &image : entry->sheets) if (image.category == category) {
        auto *item = new QListWidgetItem(thumbnail(image.image), image.name, m_sheets);
        item->setData(Qt::UserRole, image.id); item->setData(Qt::AccessibleTextRole, image.name);
        item->setToolTip(caption(image) + "\nRight-click to move to another category");
        if (image.id == selected) selectedRow = m_sheets->count() - 1;
    }
    if (m_sheets->count()) m_sheets->setCurrentRow(selectedRow);
    m_sheets->setVisible(m_sheets->count() > 1);
    m_view->setEmptyText(!m_document->isOpen() ? "Open a project" : !entry ? "Add a character" :
        QString("Add %1 images").arg(categoryName(category).toLower()));
    selectSheet();
}
void CharacterModelPanel::selectSheet() {
    const auto *item = m_sheets->currentItem();
    const auto id = item ? item->data(Qt::UserRole).toString() : QString();
    // Committing the previous name refreshes the document synchronously. Keep
    // the intended thumbnail selection available to that nested refresh.
    m_selection.insert(selectionKey(), id);
    if (id != m_sheetId) { commitSheetName(); m_zoom->commitEditing(); m_name->setModified(false); }
    m_sheetId = id; m_selection.insert(selectionKey(), id);
    const auto *image = sheet();
    m_removeSheet->setEnabled(image); m_fit->setEnabled(image); m_zoom->setEnabled(image); m_name->setEnabled(image);
    m_view->setImage(image ? image->image : QImage());
    if (!m_name->isModified()) resetSheetName();
}
void CharacterModelPanel::resetSheetName() {
    const auto *image = sheet(); m_name->setText(image ? image->name : QString());
    m_name->setToolTip(image ? caption(*image) + "\nRename the selected model sheet" : "Model sheet name");
    m_name->setModified(false);
    if (!m_name->hasFocus()) m_name->setCursorPosition(0);
}
bool CharacterModelPanel::finishEditing() {
    const auto text = m_name->text(); const auto cursor = m_name->cursorPosition();
    if (commitSheetName()) return true;
    m_name->setText(text); m_name->setModified(true); m_name->setCursorPosition(cursor);
    m_name->setFocus(); return false;
}
bool CharacterModelPanel::commitSheetName() {
    if (!m_name->isModified()) return true;
    // Undo, removal or loading another project can invalidate an inline draft.
    // It must never rename a different character's newly selected image.
    if (!sheet()) { resetSheetName(); return true; }
    const auto text = m_name->text(); m_name->setModified(false);
    QString error;
    if (!m_document->renameCharacterSheet(m_characterId, m_sheetId, text, &error)) {
        resetSheetName(); reportError(error); return false;
    }
    return true;
}
bool CharacterModelPanel::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_name && event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        resetSheetName(); event->accept(); return true;
    }
    return QWidget::eventFilter(watched, event);
}
void CharacterModelPanel::showSheetMenu(const QPoint &position) {
    auto *item = m_sheets->itemAt(position); if (!item) return;
    m_sheets->setCurrentItem(item);
    showSheetActions(m_sheets->viewport()->mapToGlobal(position));
}
void CharacterModelPanel::showSheetActions(const QPoint &globalPosition) {
    if (!sheet()) return;
    const auto characterId = m_characterId, sheetId = m_sheetId;
    QMenu menu(this);
    auto *rename = menu.addAction(appIcon("pencil"), "Rename");
    connect(rename, &QAction::triggered, this, [this] { m_name->setFocus(); m_name->selectAll(); });
    auto *move = menu.addMenu("Move to");
    for (auto category : categories) {
        auto *action = move->addAction(appIcon(categoryIcon(category)), categoryName(category));
        action->setCheckable(true); action->setChecked(category == selectedCategory());
        action->setEnabled(category != selectedCategory());
        connect(action, &QAction::triggered, this, [this, characterId, sheetId, category] {
            QString error; if (!m_document->setCharacterSheetCategory(characterId, sheetId, category, &error)) reportError(error);
        });
    }
    menu.addSeparator();
    auto *remove = menu.addAction(appIcon("trash"), "Remove model sheet");
    connect(remove, &QAction::triggered, this, [this, characterId, sheetId] {
        QString error; if (!m_document->removeCharacterSheet(characterId, sheetId, &error)) reportError(error);
    });
    menu.exec(globalPosition);
}
} // namespace toon
