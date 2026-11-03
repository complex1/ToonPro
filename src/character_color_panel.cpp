#include "character_color_panel.h"
#include "color_panel.h"
#include "document.h"
#include "icons.h"
#include "property_row.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace toon {
namespace {
constexpr CharacterColorCategory categories[] = {CharacterColorCategory::Skin, CharacterColorCategory::Hair,
    CharacterColorCategory::Costume, CharacterColorCategory::Shadow};
QString categoryName(CharacterColorCategory category) {
    switch (category) {
    case CharacterColorCategory::Skin: return "Skin";
    case CharacterColorCategory::Hair: return "Hair";
    case CharacterColorCategory::Costume: return "Costume";
    case CharacterColorCategory::Shadow: return "Shadow";
    }
    return {};
}
QString categoryIcon(CharacterColorCategory category) {
    switch (category) {
    case CharacterColorCategory::Skin: return "toon-skin";
    case CharacterColorCategory::Hair: return "toon-hair";
    case CharacterColorCategory::Costume: return "toon-costume";
    case CharacterColorCategory::Shadow: return "toon-shadow";
    }
    return {};
}
QColor normalized(const QColor &color) { return QColor::fromRgba(color.rgba()); }
QString hex(const QColor &color) { return color.name(color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb).toUpper(); }
QIcon colorChip(const QColor &color) {
    QIcon icon;
    for (qreal ratio : {1., 2., 3.}) {
        QPixmap pixmap(QSize(24, 24) * ratio); pixmap.setDevicePixelRatio(ratio); pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        for (int y = 1; y < 23; y += 5) for (int x = 1; x < 23; x += 5)
            painter.fillRect(QRect(x, y, qMin(5, 23 - x), qMin(5, 23 - y)), ((x + y) / 5) % 2 ? QColor("#9a9a9a") : QColor("#dedede"));
        painter.fillRect(QRect(1, 1, 22, 22), color);
        painter.setPen(QColor(ui::border)); painter.drawRect(QRect(0, 0, 23, 23)); painter.end();
        // Swatches are color references: Qt's generated selection/disabled
        // variants must not tint their pixels. The row carries selection state.
        for (auto mode : {QIcon::Normal, QIcon::Active, QIcon::Selected, QIcon::Disabled})
            for (auto state : {QIcon::Off, QIcon::On}) icon.addPixmap(pixmap, mode, state);
    }
    return icon;
}
class ColorCategoryTabs final : public QTabBar {
public:
    using QTabBar::QTabBar;
    QSize minimumSizeHint() const override {
        auto result = QTabBar::minimumSizeHint();
        result.setWidth(count() * ui::metrics::actionTarget); return result;
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
        for (int index = 0; index < count(); ++index)
            fullWidth += QTabBar::tabSizeHint(index).width()
                + fontMetrics().size(Qt::TextShowMnemonic, categoryName(categories[index])).width()
                - fontMetrics().size(Qt::TextShowMnemonic, tabText(index)).width();
        const bool names = width() >= fullWidth; m_updating = true;
        for (int index = 0; index < count(); ++index) {
            const auto name = names ? categoryName(categories[index]) : QString();
            if (tabText(index) != name) setTabText(index, name);
        }
        m_updating = false;
    }
    bool m_updating = false;
};
class ColorList final : public QListWidget {
public:
    using QListWidget::QListWidget;
    void setEmptyText(const QString &text) { if (text != m_emptyText) { m_emptyText = text; viewport()->update(); } }
protected:
    void paintEvent(QPaintEvent *event) override {
        QListWidget::paintEvent(event);
        if (count()) return;
        QPainter painter(viewport()); painter.setPen(QColor(ui::muted));
        painter.drawText(viewport()->rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap, m_emptyText);
    }
private:
    QString m_emptyText;
};
class ColorVariantTabs final : public QTabBar {
public:
    using QTabBar::QTabBar;
    QSize minimumSizeHint() const override {
        auto size = QTabBar::minimumSizeHint(); size.setWidth(64); return size;
    }
protected:
    QSize tabSizeHint(int index) const override {
        auto size = QTabBar::tabSizeHint(index);
        const auto name = tabText(index);
        // Preserve the short lighting names at their natural width. Long custom
        // names may elide, but still fit beside the two scroll arrows.
        const bool standard = name == "Day" || name == "Night" || name == "Sunset" || name == "Interior";
        if (!standard) {
            const int arrows = 2 * style()->pixelMetric(QStyle::PM_TabBarScrollButtonWidth, nullptr, this);
            size.setWidth(qMin(size.width(), qMin(160, qMax(64, width() - arrows))));
        }
        return size;
    }
    QSize minimumTabSizeHint(int index) const override { return tabSizeHint(index); }
};
class ColorRowDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        auto size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(ui::metrics::layerRow, option.fontMetrics.height() + 12)); return size;
    }
};
bool editSwatchColor(QWidget *parent, const QString &name, const QColor &original, QColor *result) {
    QDialog dialog(parent); dialog.setObjectName("characterColorEditor"); dialog.setWindowTitle("Edit " + name);
    dialog.setProperty("compactDrawingPanel", true); dialog.resize(300, 320);
    auto *layout = new QVBoxLayout(&dialog); layout->setSpacing(ui::metrics::controlGap);
    auto *picker = new ColorPicker(&dialog); picker->setObjectName("characterColorPicker");
    picker->setColor(original); layout->addWidget(picker, 1);
    auto *hexRow = new QHBoxLayout; hexRow->setSpacing(ui::metrics::controlGap);
    auto *preview = new QLabel(&dialog); preview->setObjectName("characterColorPreview");
    preview->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget); preview->setAccessibleName("Edited color preview");
    auto *entry = new QLineEdit(hex(original), &dialog); entry->setObjectName("characterColorHex");
    entry->setAccessibleName("Hex color"); entry->setToolTip("Color as #RRGGBB or #AARRGGBB (alpha first)"); entry->setMaxLength(9);
    entry->setProperty("compactDrawingField", true);
    hexRow->addWidget(preview); hexRow->addWidget(new QLabel("Hex", &dialog)); hexRow->addWidget(entry, 1); layout->addLayout(hexRow);
    auto *opacity = new ValueSlider("Opacity", 0, 100, original.alphaF() * 100, 1, " %", &dialog);
    opacity->setObjectName("characterColorOpacity"); layout->addWidget(new PropertyRow("Opacity", opacity, &dialog));
    auto *error = new QLabel(&dialog); error->setObjectName("characterColorEditorError");
    error->setTextFormat(Qt::PlainText); error->setFixedHeight(error->fontMetrics().height() + 4); layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName("characterColorEditorButtons"); layout->addWidget(buttons);
    QColor current = original;
    const auto sync = [&] {
        picker->setColor(current);
        { const QSignalBlocker block(opacity); opacity->setValue(current.alphaF() * 100); }
        entry->setText(hex(current)); entry->setModified(false); error->clear();
        preview->setPixmap(colorChip(current).pixmap(QSize(24, 24), dialog.devicePixelRatioF()));
        preview->setToolTip(hex(current));
    };
    const auto readHex = [&] {
        const auto text = entry->text().trimmed();
        static const QRegularExpression syntax("^#(?:[0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$");
        if (!syntax.match(text).hasMatch()) { error->setText("Use #RRGGBB or #AARRGGBB."); return false; }
        current = QColor(text); sync(); return true;
    };
    QObject::connect(entry, &QLineEdit::editingFinished, &dialog, [&] { if (entry->isModified()) readHex(); });
    QObject::connect(picker, &ColorPicker::colorChanged, &dialog, [&](const QColor &color) { current = normalized(color); sync(); });
    QObject::connect(opacity, &ValueSlider::valueChanged, &dialog, [&](double value) {
        current.setAlpha(qRound(value * 255 / 100));
        picker->setColor(current); entry->setText(hex(current)); entry->setModified(false); error->clear();
        preview->setPixmap(colorChip(current).pixmap(QSize(24, 24), dialog.devicePixelRatioF())); preview->setToolTip(hex(current));
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        opacity->commitEditing();
        if (!readHex()) { entry->setFocus(); entry->selectAll(); return; }
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    sync();
    if (dialog.exec() != QDialog::Accepted) return false;
    *result = normalized(current); return true;
}
}

CharacterColorPanel::CharacterColorPanel(Document *document, QWidget *parent)
    : QWidget(parent), m_document(document) {
    setObjectName("characterColorPanel"); setMinimumWidth(212); setProperty("compactDrawingPanel", true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(ui::metrics::panelMargin, ui::metrics::panelMargin, ui::metrics::panelMargin, ui::metrics::panelMargin);
    layout->setSpacing(ui::metrics::controlGap);
    const auto action = [this](const QString &id, const QString &icon, const QString &label) {
        auto *button = new QToolButton(this); button->setObjectName(id); button->setIcon(appIcon(icon));
        button->setAccessibleName(label); button->setToolTip(label); button->setProperty("compactDrawingAction", true);
        button->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon}); button->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
        return button;
    };
    auto *characterRow = new QHBoxLayout; characterRow->setSpacing(ui::metrics::controlGap);
    m_characters = new QComboBox(this); m_characters->setObjectName("colorModelCharacterSelector");
    m_characters->setAccessibleName("Character"); m_characters->setProperty("compactDrawingField", true);
    m_characters->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); m_characters->setMinimumContentsLength(4);
    m_characters->setMinimumWidth(40); m_characters->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_characters->setPlaceholderText("Choose character");
    m_addCharacter = action("addColorModelCharacter", "plus", "Add character");
    m_renameCharacter = action("renameColorModelCharacter", "pencil", "Rename character");
    m_removeCharacter = action("removeColorModelCharacter", "trash", "Remove character, model sheets and colors (Undo available)");
    characterRow->addWidget(m_characters, 1); characterRow->addWidget(m_addCharacter);
    characterRow->addWidget(m_renameCharacter); characterRow->addWidget(m_removeCharacter); layout->addLayout(characterRow);
    auto *variantRow = new QHBoxLayout; variantRow->setSpacing(ui::metrics::controlGap);
    m_variants = new ColorVariantTabs(this); m_variants->setObjectName("characterColorVariantTabs");
    m_variants->setAccessibleName("Character lighting palettes"); m_variants->setProperty("compactDrawingTabs", true);
    m_variants->setExpanding(false); m_variants->setUsesScrollButtons(true); m_variants->setElideMode(Qt::ElideRight);
    m_variants->setFocusPolicy(Qt::StrongFocus); m_variants->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_addVariant = action("addCharacterColorVariant", "plus", "Duplicate current lighting palette");
    m_variantMore = action("characterColorVariantOptions", "ellipsis", "Lighting palette options");
    auto *variantMenu = new QMenu(m_variantMore); variantMenu->setObjectName("characterColorVariantMenu");
    m_duplicateVariant = variantMenu->addAction(appIcon("copy"), "Duplicate palette…"); m_duplicateVariant->setObjectName("duplicateCharacterColorVariant");
    m_renameVariant = variantMenu->addAction(appIcon("pencil"), "Rename palette…"); m_renameVariant->setObjectName("renameCharacterColorVariant");
    m_removeVariant = variantMenu->addAction(appIcon("trash"), "Remove palette"); m_removeVariant->setObjectName("removeCharacterColorVariant");
    m_removeVariant->setToolTip("Remove this lighting palette; Undo restores it");
    variantMenu->addSeparator();
    m_standardVariants = variantMenu->addAction(appIcon("sun"), "Add missing lighting palettes"); m_standardVariants->setObjectName("addStandardCharacterColorVariants");
    m_standardVariants->setToolTip("Create missing Night, Sunset and Interior palettes as editable copies of Day");
    m_variantMore->setMenu(variantMenu); m_variantMore->setPopupMode(QToolButton::InstantPopup);
    variantRow->addWidget(m_variants, 1); variantRow->addWidget(m_addVariant); variantRow->addWidget(m_variantMore); layout->addLayout(variantRow);
    m_categories = new ColorCategoryTabs(this); m_categories->setObjectName("characterColorTabs");
    m_categories->setAccessibleName("Character color categories"); m_categories->setProperty("compactDrawingTabs", true);
    m_categories->setIconSize({ui::metrics::tabIcon, ui::metrics::tabIcon}); m_categories->setExpanding(true);
    m_categories->setElideMode(Qt::ElideNone); m_categories->setUsesScrollButtons(false); m_categories->setFocusPolicy(Qt::StrongFocus);
    for (auto category : categories) {
        const auto index = m_categories->addTab(appIcon(categoryIcon(category)), categoryName(category));
        m_categories->setTabData(index, int(category)); m_categories->setTabToolTip(index, categoryName(category));
        m_categories->setAccessibleTabName(index, categoryName(category));
    }
    layout->addWidget(m_categories);
    auto *actions = new QHBoxLayout; actions->setSpacing(ui::metrics::controlGap);
    m_add = action("addCharacterColor", "plus", "Add current foreground color");
    m_update = action("updateCharacterColor", "save", "Replace selected color with the foreground color");
    m_edit = action("editCharacterColor", "palette", "Edit selected color");
    m_remove = action("removeCharacterColor", "trash", "Remove selected color (Undo available)");
    m_recolor = action("recolorCharacterDrawings", "paint-bucket", "Recolor drawings…");
    m_recolor->setToolTip("Preview this character's lighting palette on selected drawings before applying");
    actions->addWidget(m_add); actions->addWidget(m_update); actions->addWidget(m_edit); actions->addWidget(m_recolor); actions->addStretch(); actions->addWidget(m_remove); layout->addLayout(actions);
    m_colors = new ColorList(this); m_colors->setObjectName("characterColorList"); m_colors->setAccessibleName("Named character colors");
    m_colors->setToolTip("Click a color or press Enter to use it; right-click to organize it");
    m_colors->setIconSize({24, 24}); m_colors->setItemDelegate(new ColorRowDelegate(m_colors)); m_colors->setTextElideMode(Qt::ElideRight);
    m_colors->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); m_colors->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_colors->setContextMenuPolicy(Qt::CustomContextMenu); m_colors->setMinimumHeight(80); layout->addWidget(m_colors, 1);
    m_name = new QLineEdit(this); m_name->setObjectName("characterColorName"); m_name->setMaxLength(100);
    m_name->setPlaceholderText("Color name"); m_name->setAccessibleName("Character color name");
    m_name->setToolTip("Rename the selected color in all lighting palettes; names and categories are shared"); m_name->setProperty("compactDrawingField", true);
    m_name->setMinimumWidth(0); m_name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); m_name->installEventFilter(this); layout->addWidget(m_name);

    connect(m_characters, &QComboBox::currentIndexChanged, this, [this] { if (!m_refreshing) selectCharacter(m_characters->currentData().toString()); });
    connect(m_categories, &QTabBar::currentChanged, this, [this](int index) {
        if (!m_refreshing && index >= 0) selectCategory(CharacterColorCategory(m_categories->tabData(index).toInt()));
    });
    connect(m_variants, &QTabBar::currentChanged, this, [this](int index) {
        if (!m_refreshing && index >= 0) selectVariant(m_variants->tabData(index).toString());
    });
    connect(m_addVariant, &QToolButton::clicked, this, &CharacterColorPanel::duplicateVariant);
    connect(m_duplicateVariant, &QAction::triggered, this, &CharacterColorPanel::duplicateVariant);
    connect(m_renameVariant, &QAction::triggered, this, &CharacterColorPanel::renameVariant);
    connect(m_removeVariant, &QAction::triggered, this, &CharacterColorPanel::removeVariant);
    connect(m_standardVariants, &QAction::triggered, this, &CharacterColorPanel::addStandardVariants);
    connect(m_colors, &QListWidget::currentRowChanged, this, &CharacterColorPanel::selectColor);
    connect(m_colors, &QListWidget::itemClicked, this, [this] { useSelectedColor(); });
    connect(m_colors, &QListWidget::itemActivated, this, [this] { useSelectedColor(); });
    connect(m_colors, &QListWidget::customContextMenuRequested, this, &CharacterColorPanel::showColorMenu);
    connect(m_name, &QLineEdit::editingFinished, this, &CharacterColorPanel::commitName);
    connect(m_add, &QToolButton::clicked, this, &CharacterColorPanel::addColor);
    connect(m_update, &QToolButton::clicked, this, &CharacterColorPanel::replaceColor);
    connect(m_edit, &QToolButton::clicked, this, &CharacterColorPanel::editColor);
    connect(m_remove, &QToolButton::clicked, this, &CharacterColorPanel::removeColor);
    connect(m_recolor, &QToolButton::clicked, this, [this] {
        if (ready() && finishEditing() && character()) emit recolorRequested(m_characterId, m_variantId);
    });
    connect(m_addCharacter, &QToolButton::clicked, this, [this] {
        if (!ready() || !finishEditing()) return;
        bool accepted = false; const auto name = QInputDialog::getText(this, "Add character", "Name", QLineEdit::Normal, {}, &accepted);
        if (!accepted) return;
        QString error; const auto id = m_document->addCharacter(name, &error);
        if (id.isEmpty()) reportError(error); else selectCharacter(id);
    });
    connect(m_renameCharacter, &QToolButton::clicked, this, [this] {
        if (!ready() || !finishEditing()) return;
        const auto *entry = character(); if (!entry) return;
        const auto id = entry->id, previous = entry->name; bool accepted = false;
        const auto name = QInputDialog::getText(this, "Rename character", "Name", QLineEdit::Normal, previous, &accepted);
        if (!accepted) return;
        QString error; if (!m_document->renameCharacter(id, name, &error)) reportError(error);
    });
    connect(m_removeCharacter, &QToolButton::clicked, this, [this] {
        if (!ready() || !finishEditing()) return;
        QString error; if (!m_document->removeCharacter(m_characterId, &error)) reportError(error);
    });
    connect(m_document, &Document::selectionChanged, this, &CharacterColorPanel::refresh);
    connect(m_document, &Document::loaded, this, [this] {
        m_characterId.clear(); m_colorId.clear(); m_variantId.clear(); m_characterState.clear(); m_colorState.clear(); m_selection.clear();
        m_variantState.clear(); m_variantSelections.clear(); m_paletteColors.clear();
        m_name->setModified(false); refresh();
    });
    refresh();
}

const CharacterModel *CharacterColorPanel::character() const {
    if (m_document->isOpen()) for (const auto &entry : m_document->project().characters) if (entry.id == m_characterId) return &entry;
    return nullptr;
}
const CharacterColorSwatch *CharacterColorPanel::swatch() const {
    for (const auto &color : m_paletteColors) if (color.id == m_colorId) return &color;
    return nullptr;
}
CharacterColorCategory CharacterColorPanel::selectedCategory() const { return m_category; }
QString CharacterColorPanel::selectionKey() const { return m_characterId + ':' + m_variantId + ':' + QString::number(int(selectedCategory())); }
void CharacterColorPanel::selectCategory(CharacterColorCategory category) {
    const auto setTab = [this](CharacterColorCategory selected) {
        const QSignalBlocker block(m_categories);
        for (int index = 0; index < m_categories->count(); ++index)
            if (m_categories->tabData(index).toInt() == int(selected)) { m_categories->setCurrentIndex(index); return; }
    };
    if (category == m_category) { setTab(category); return; }
    if (int(category) < 0 || int(category) > 3) return;
    if (!finishEditing()) { setTab(m_category); return; }
    m_category = category; setTab(category); refreshColors();
}
void CharacterColorPanel::selectCharacter(const QString &id) {
    if (id == m_characterId) return;
    bool found = false;
    if (m_document->isOpen()) for (const auto &entry : m_document->project().characters) if (entry.id == id) { found = true; break; }
    if (!found) return;
    if (!finishEditing()) {
        const QSignalBlocker block(m_characters); m_characters->setCurrentIndex(m_characters->findData(m_characterId)); return;
    }
    m_name->setModified(false); m_characterId = id;
    { const QSignalBlocker block(m_characters); m_characters->setCurrentIndex(m_characters->findData(id)); }
    refreshVariants(); refreshColors(); refreshActions(); emit characterSelected(id); emit paletteSelected(id, m_variantId);
}
void CharacterColorPanel::selectVariant(const QString &id) {
    const auto setTab = [this](const QString &selected) {
        const QSignalBlocker block(m_variants);
        for (int i = 0; i < m_variants->count(); ++i) if (m_variants->tabData(i).toString() == selected) { m_variants->setCurrentIndex(i); break; }
    };
    if (id == m_variantId) { setTab(id); return; }
    const auto *entry = character(); if (!entry) return;
    bool found = id.isEmpty();
    for (const auto &variant : entry->colorVariants) if (variant.id == id) { found = true; break; }
    if (!found) { setTab(m_variantId); return; }
    if (!finishEditing()) { setTab(m_variantId); return; }
    m_variantId = id; m_variantSelections.insert(m_characterId, id); setTab(id);
    refreshColors(); refreshActions(); emit paletteSelected(m_characterId, id);
}
void CharacterColorPanel::setCurrentColor(const QColor &color) {
    if (!color.isValid()) return;
    const auto next = normalized(color); if (next == m_currentColor) return;
    m_currentColor = next; refreshActions();
}
void CharacterColorPanel::refresh() {
    const auto previous = m_characterId; QStringList state;
    if (m_document->isOpen()) for (const auto &entry : m_document->project().characters) state << entry.id << entry.name;
    if (state != m_characterState) {
        m_characterState = state; m_refreshing = true; const QSignalBlocker block(m_characters); m_characters->clear();
        if (m_document->isOpen()) for (const auto &entry : m_document->project().characters) m_characters->addItem(entry.name, entry.id);
        auto index = m_characters->findData(m_characterId); if (index < 0) index = m_characters->count() ? 0 : -1;
        m_characters->setCurrentIndex(index); m_characterId = m_characters->currentData().toString(); m_refreshing = false;
    }
    refreshVariants(); refreshColors(); refreshActions();
    if (previous != m_characterId) emit characterSelected(m_characterId);
}
void CharacterColorPanel::refreshVariants() {
    const auto *entry = character(); QStringList state{m_characterId};
    if (entry) for (const auto &variant : entry->colorVariants) state << variant.id << variant.name;
    const auto desired = m_variantSelections.value(m_characterId);
    QString selected;
    if (entry) for (const auto &variant : entry->colorVariants) if (variant.id == desired) { selected = desired; break; }
    m_variantId = selected;
    if (state == m_variantState) return;
    m_variantState = state; const QSignalBlocker block(m_variants);
    while (m_variants->count()) m_variants->removeTab(0);
    const auto add = [this](const QString &name, const QString &id) {
        const int index = m_variants->addTab(name); m_variants->setTabData(index, id);
        m_variants->setAccessibleTabName(index, name + " lighting palette");
        m_variants->setTabToolTip(index, name + " · Shared across shots\nEdit its colors independently; color names and categories are shared");
        if (id == m_variantId) m_variants->setCurrentIndex(index);
    };
    add("Day", {}); if (entry) for (const auto &variant : entry->colorVariants) add(variant.name, variant.id);
}
void CharacterColorPanel::refreshColors() {
    const auto *entry = character(); const auto category = selectedCategory();
    const auto colors = entry ? characterVariantColors(*entry, m_variantId) : QVector<CharacterColorSwatch>{};
    QStringList state{m_characterId, m_variantId, QString::number(int(category))};
    for (const auto &color : colors) if (color.category == category)
        state << color.id << color.name << color.color.name(QColor::HexArgb);
    if (state == m_colorState) return;
    const bool sameGroup = m_colorState.size() >= 3 && state.mid(0, 3) == m_colorState.mid(0, 3);
    const auto scroll = sameGroup ? m_colors->verticalScrollBar()->value() : 0;
    bool selectedExists = false;
    for (const auto &color : colors) if (color.id == m_colorId) { selectedExists = true; break; }
    // A removed role or a replaced document cannot keep a rename draft attached
    // to some other color. Explicit navigation resolves drafts before this point.
    if (!selectedExists) { m_name->setModified(false); m_colorId.clear(); }
    m_paletteColors = colors; m_colorState = state; const auto selected = m_selection.value(selectionKey());
    const QSignalBlocker block(m_colors); m_colors->clear(); int selectedRow = 0;
    for (const auto &color : colors) if (color.category == category) {
        auto *item = new QListWidgetItem(colorChip(color.color), color.name, m_colors);
        item->setData(Qt::UserRole, color.id); item->setData(Qt::UserRole + 1, color.color);
        const auto description = color.name + " · " + hex(color.color);
        item->setData(Qt::AccessibleTextRole, description); item->setToolTip(description + "\nClick to use; right-click to organize");
        if (color.id == selected) selectedRow = m_colors->count() - 1;
    }
    if (m_colors->count()) m_colors->setCurrentRow(selectedRow);
    static_cast<ColorList *>(m_colors)->setEmptyText(!m_document->isOpen() ? "Open a project" : !entry ? "Add a character" :
        QString("Add %1 colors").arg(categoryName(category).toLower()));
    selectColor(); m_colors->verticalScrollBar()->setValue(scroll);
}
void CharacterColorPanel::refreshActions() {
    const auto *entry = character(); const auto *color = swatch();
    m_addCharacter->setEnabled(m_document->isOpen()); m_characters->setEnabled(m_document->isOpen());
    m_renameCharacter->setEnabled(entry); m_removeCharacter->setEnabled(entry); m_categories->setEnabled(entry);
    m_variants->setEnabled(entry); m_variantMore->setEnabled(entry);
    const bool canAddVariant = entry && entry->colorVariants.size() < maxCharacterColorVariants;
    m_addVariant->setEnabled(canAddVariant); m_duplicateVariant->setEnabled(canAddVariant);
    m_renameVariant->setEnabled(entry && !m_variantId.isEmpty()); m_removeVariant->setEnabled(entry && !m_variantId.isEmpty());
    QStringList missing{"Night", "Sunset", "Interior"};
    if (entry) for (const auto &variant : entry->colorVariants)
        for (int i = missing.size() - 1; i >= 0; --i) if (missing[i].compare(variant.name, Qt::CaseInsensitive) == 0) missing.removeAt(i);
    m_standardVariants->setEnabled(canAddVariant && !missing.isEmpty() && entry->colorVariants.size() + missing.size() <= maxCharacterColorVariants);
    m_add->setEnabled(entry); m_update->setEnabled(color && normalized(color->color) != m_currentColor);
    m_edit->setEnabled(color); m_remove->setEnabled(color); m_name->setEnabled(color);
    m_recolor->setEnabled(entry && !entry->colors.isEmpty() && !entry->colorVariants.isEmpty());
    m_characters->setToolTip(entry ? entry->name + " · Shared across all shots" : "Characters shared across all shots in this project");
    m_add->setToolTip("Add current foreground as a shared color in every lighting palette · " + hex(m_currentColor));
    m_remove->setToolTip("Remove color from all lighting palettes (Undo available)");
    m_update->setToolTip("Replace selected color with foreground · " + hex(m_currentColor));
}
void CharacterColorPanel::selectColor() {
    const auto *item = m_colors->currentItem(); const auto id = item ? item->data(Qt::UserRole).toString() : QString();
    m_selection.insert(selectionKey(), id);
    if (id != m_colorId && !finishEditing()) {
        m_selection.insert(selectionKey(), m_colorId);
        const QSignalBlocker block(m_colors);
        for (int row = 0; row < m_colors->count(); ++row) if (m_colors->item(row)->data(Qt::UserRole).toString() == m_colorId) { m_colors->setCurrentRow(row); break; }
        return;
    }
    m_colorId = id; m_selection.insert(selectionKey(), id);
    if (!m_name->isModified()) resetName();
    refreshActions();
}
void CharacterColorPanel::useSelectedColor() { if (const auto *color = swatch()) emit colorSelected(color->color); }
void CharacterColorPanel::resetName() {
    const auto *color = swatch(); m_name->setText(color ? color->name : QString()); m_name->setModified(false);
    if (!m_name->hasFocus()) m_name->setCursorPosition(0);
    m_name->setToolTip(color ? color->name + " · " + hex(color->color) + "\nNames and categories are shared across all lighting palettes" : "Character color name");
}
bool CharacterColorPanel::finishEditing() {
    const auto draft = m_name->text();
    const auto cursor = m_name->cursorPosition();
    const auto selection = m_name->selectionStart();
    const auto length = m_name->selectedText().size();
    if (commitName()) return true;
    m_name->setText(draft); m_name->setModified(true);
    m_name->setCursorPosition(cursor);
    if (selection >= 0) m_name->setSelection(selection, length);
    m_name->setFocus(Qt::OtherFocusReason);
    return false;
}
bool CharacterColorPanel::commitName() {
    if (m_committingName) return true;
    if (!m_name->isModified()) return true;
    const auto *color = swatch(); if (!color) { resetName(); return false; }
    QScopedValueRollback<bool> committing(m_committingName, true);
    const auto copy = *color; const auto name = m_name->text(); m_name->setModified(false);
    QString error;
    if (!m_document->updateCharacterColor(m_characterId, copy.id, name, copy.category, copy.color, &error, m_variantId)) {
        reportError(error); m_name->setText(name); m_name->setModified(true); return false;
    }
    return true;
}
bool CharacterColorPanel::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_name && event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        resetName(); event->accept(); return true;
    }
    return QWidget::eventFilter(watched, event);
}
bool CharacterColorPanel::ready() {
    if (!m_document->isOpen()) { reportError("Open a project before editing character colors."); return false; }
    if (m_document->editingGestureActive() || m_document->layerPropertyEditActive()) {
        reportError("Finish the current edit before changing character colors."); return false;
    }
    return true;
}
void CharacterColorPanel::reportError(const QString &error) { if (!error.isEmpty()) QMessageBox::warning(this, "Character color models", error); }
void CharacterColorPanel::addColor() {
    if (!ready() || !finishEditing() || !character()) return;
    const auto characterId = m_characterId; const auto category = selectedCategory(); const auto foreground = m_currentColor;
    bool accepted = false; const auto name = QInputDialog::getText(this, "Add character color", "Name", QLineEdit::Normal, {}, &accepted);
    if (!accepted) return;
    QString error; const auto id = m_document->addCharacterColor(characterId, name, category, foreground, &error);
    if (id.isEmpty()) { reportError(error); return; }
    for (int row = 0; row < m_colors->count(); ++row) if (m_colors->item(row)->data(Qt::UserRole).toString() == id) m_colors->setCurrentRow(row);
}
void CharacterColorPanel::replaceColor() {
    if (!ready() || !finishEditing()) return;
    const auto *color = swatch(); if (!color) return;
    const auto copy = *color; QString error;
    if (!m_document->updateCharacterColor(m_characterId, copy.id, copy.name, copy.category, m_currentColor, &error, m_variantId)) reportError(error);
}
void CharacterColorPanel::editColor() {
    if (!ready() || !finishEditing()) return;
    const auto *color = swatch(); if (!color) return;
    const auto copy = *color; const auto characterId = m_characterId, variantId = m_variantId; QColor updated;
    if (!editSwatchColor(this, copy.name, copy.color, &updated)) return;
    QString error; if (!m_document->updateCharacterColor(characterId, copy.id, copy.name, copy.category, updated, &error, variantId)) reportError(error);
}
void CharacterColorPanel::removeColor() {
    if (!ready() || !finishEditing()) return;
    QString error; if (!m_document->removeCharacterColor(m_characterId, m_colorId, &error)) reportError(error);
}
void CharacterColorPanel::showColorMenu(const QPoint &point) {
    if (!finishEditing()) return;
    auto *item = m_colors->itemAt(point); if (!item) return; m_colors->setCurrentItem(item);
    const auto *color = swatch(); if (!color) return;
    const auto copy = *color; const auto characterId = m_characterId, variantId = m_variantId;
    QMenu menu(this); auto *use = menu.addAction(appIcon("palette"), "Use color"); connect(use, &QAction::triggered, this, &CharacterColorPanel::useSelectedColor);
    auto *edit = menu.addAction(appIcon("pencil"), "Edit color"); connect(edit, &QAction::triggered, this, &CharacterColorPanel::editColor);
    auto *rename = menu.addAction("Rename"); connect(rename, &QAction::triggered, this, [this] { m_name->setFocus(); m_name->selectAll(); });
    auto *move = menu.addMenu("Move to"); move->setToolTip("Change this color's category in all lighting palettes");
    for (auto category : categories) {
        auto *action = move->addAction(appIcon(categoryIcon(category)), categoryName(category)); action->setCheckable(true);
        action->setChecked(category == copy.category); action->setEnabled(category != copy.category);
        connect(action, &QAction::triggered, this, [this, characterId, variantId, copy, category] {
            if (!ready() || !finishEditing()) return;
            QString error; if (!m_document->updateCharacterColor(characterId, copy.id, copy.name, category, copy.color, &error, variantId)) reportError(error);
        });
    }
    menu.addSeparator(); auto *remove = menu.addAction(appIcon("trash"), "Remove color from all lighting palettes");
    remove->setToolTip("Remove color from all lighting palettes (Undo available)"); connect(remove, &QAction::triggered, this, &CharacterColorPanel::removeColor);
    menu.exec(m_colors->viewport()->mapToGlobal(point));
}
void CharacterColorPanel::duplicateVariant() {
    if (!ready() || !finishEditing() || !character()) return;
    const auto characterId = m_characterId, sourceId = m_variantId;
    const auto sourceName = m_variants->tabText(m_variants->currentIndex());
    bool accepted = false;
    const auto name = QInputDialog::getText(this, "Duplicate lighting palette", "Name", QLineEdit::Normal,
        sourceName + " copy", &accepted);
    if (!accepted) return;
    QString error; const auto id = m_document->addCharacterColorVariant(characterId, name, sourceId, &error);
    if (id.isEmpty()) reportError(error); else selectVariant(id);
}
void CharacterColorPanel::renameVariant() {
    if (!ready() || !finishEditing() || m_variantId.isEmpty()) return;
    const auto characterId = m_characterId, variantId = m_variantId;
    bool accepted = false;
    const auto name = QInputDialog::getText(this, "Rename lighting palette", "Name", QLineEdit::Normal,
        m_variants->tabText(m_variants->currentIndex()), &accepted);
    if (!accepted) return;
    QString error; if (!m_document->renameCharacterColorVariant(characterId, variantId, name, &error)) reportError(error);
}
void CharacterColorPanel::removeVariant() {
    if (!ready() || !finishEditing() || m_variantId.isEmpty()) return;
    QString error; if (!m_document->removeCharacterColorVariant(m_characterId, m_variantId, &error)) reportError(error);
}
void CharacterColorPanel::addStandardVariants() {
    if (!ready() || !finishEditing() || !character()) return;
    QString error; if (!m_document->addStandardCharacterColorVariants(m_characterId, &error)) reportError(error);
}
} // namespace toon
