#include "character_recolor_dialog.h"
#include "document.h"
#include "icons.h"
#include "property_row.h"
#include "reference_panel.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QSet>
#include <QSplitter>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrentRun>

namespace toon {
// Keep ReferenceView's navigation image fixed while comparing. Switching Before
// and After therefore preserves both zoom and pan, instead of resetting the view.
class RecolorPreviewView final : public ReferenceView {
public:
    using ReferenceView::ReferenceView;
    void setPair(const QImage &before, const QImage &after) {
        m_after = after; setImage(before); update();
    }
    void showAfter(bool value) { m_showAfter = value; update(); }
protected:
    void paintEvent(QPaintEvent *event) override {
        if (!m_showAfter || m_after.isNull()) { ReferenceView::paintEvent(event); return; }
        QPainter painter(this); painter.fillRect(rect(), QColor(ui::canvas));
        painter.setTransform(imageTransform()); painter.fillRect(m_after.rect(), Qt::white);
        painter.setRenderHint(QPainter::SmoothPixmapTransform); painter.drawImage(QPointF(), m_after);
    }
private:
    QImage m_after;
    bool m_showAfter = true;
};

namespace {
QString targetKey(const QString &shotId, const QString &layerId) { return shotId + "/" + layerId; }
QString itemKey(const QListWidgetItem *item) {
    return item ? targetKey(item->data(Qt::UserRole).toString(), item->data(Qt::UserRole + 1).toString()) : QString();
}
QString drawingName(const Shot &shot, const RasterLayer &layer) {
    QStringList path{layer.name}; QString parent = layer.parentId; QSet<QString> seen;
    while (!parent.isEmpty() && !seen.contains(parent)) {
        seen.insert(parent); bool found = false;
        for (const auto &entry : shot.layers) if (entry.id == parent) {
            path.prepend(entry.name); parent = entry.parentId; found = true; break;
        }
        if (!found) break;
    }
    return shot.number + " · " + path.join(" / ");
}
QComboBox *combo(QWidget *parent, const QString &id, const QString &name) {
    auto *widget = new QComboBox(parent); widget->setObjectName(id); widget->setAccessibleName(name);
    widget->setProperty("compactDrawingField", true); widget->setMinimumContentsLength(5);
    widget->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    widget->setMinimumWidth(0); widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); return widget;
}
QToolButton *action(QWidget *parent, const QString &id, const QString &icon, const QString &name) {
    auto *button = new QToolButton(parent); button->setObjectName(id); button->setIcon(appIcon(icon));
    button->setAccessibleName(name); button->setToolTip(name); button->setProperty("compactDrawingAction", true);
    button->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon});
    button->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget); return button;
}
}

CharacterRecolorDialog::CharacterRecolorDialog(Document *document, const QString &characterId,
                                             const QString &targetVariantId, QWidget *parent)
    : QDialog(parent), m_document(document) {
    setObjectName("characterRecolorDialog"); setWindowTitle("Recolor character drawings");
    setProperty("compactDrawingPanel", true); setModal(true); resize(780, 500); setMinimumSize(520, 480);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(8, 8, 8, 8); layout->setSpacing(8);
    m_splitter = new QSplitter(Qt::Horizontal, this); m_splitter->setObjectName("characterRecolorSplitter");
    m_splitter->setChildrenCollapsible(false);
    auto *controls = new QWidget(m_splitter); controls->setMinimumWidth(210);
    auto *inputs = new QVBoxLayout(controls); inputs->setContentsMargins(0, 0, 0, 0); inputs->setSpacing(4);
    m_characters = combo(controls, "characterRecolorCharacter", "Character to recolor");
    m_source = combo(controls, "characterRecolorSourcePalette", "Source palette already used in the drawings");
    m_target = combo(controls, "characterRecolorTargetPalette", "Replacement palette");
    inputs->addWidget(new PropertyRow("Character", m_characters));
    inputs->addWidget(new PropertyRow("From", m_source)); inputs->addWidget(new PropertyRow("To", m_target));
    m_source->setToolTip("The palette already used in the selected drawings. Colors match by shared skin, hair, costume and shadow roles.");
    m_target->setToolTip("Replace matching role colors with this palette. Other colors and pixel transparency stay unchanged.");
    m_scope = combo(controls, "characterRecolorScope", "Shots containing drawings to recolor");
    m_scope->addItem("Current shot"); m_scope->addItem("All shots");
    inputs->addWidget(new PropertyRow("Drawings", m_scope));
    auto *selection = new QHBoxLayout; selection->setSpacing(4);
    auto *selectionLabel = new QLabel("Selected drawings", controls); selection->addWidget(selectionLabel, 1);
    m_all = action(controls, "characterRecolorSelectAll", "check", "Select all available drawings");
    m_none = action(controls, "characterRecolorSelectNone", "x", "Clear drawing selection");
    selection->addWidget(m_all); selection->addWidget(m_none); inputs->addLayout(selection);
    m_drawings = new QListWidget(controls); m_drawings->setObjectName("characterRecolorDrawings");
    m_drawings->setAccessibleName("Drawings to recolor"); m_drawings->setMinimumSize(180, 96);
    m_drawings->setIconSize({24, 24});
    m_drawings->setTextElideMode(Qt::ElideRight);
    m_drawings->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_drawings->setToolTip("Check the raster paint drawings to change, including drawings on other frames. Each row is a source drawing, before layer effects.");
    inputs->addWidget(m_drawings, 1);
    auto *review = new QWidget(m_splitter); review->setMinimumSize(160, 144);
    auto *previewLayout = new QVBoxLayout(review); previewLayout->setContentsMargins(4, 0, 0, 0); previewLayout->setSpacing(4);
    auto *previewHeader = new QHBoxLayout; previewHeader->setSpacing(4);
    m_compare = new QTabBar(review); m_compare->setObjectName("characterRecolorCompare");
    m_compare->setAccessibleName("Compare original and recolored drawing"); m_compare->setProperty("compactDrawingTabs", true);
    m_compare->addTab("Before"); m_compare->addTab("After"); m_compare->setCurrentIndex(1); m_compare->setExpanding(false);
    m_compare->setTabToolTip(0, "Original source drawing"); m_compare->setTabToolTip(1, "Recolored source drawing; artwork changes only after Apply");
    previewHeader->addWidget(m_compare); previewHeader->addStretch();
    auto *fit = action(review, "characterRecolorFit", "maximize-2", "Fit drawing preview");
    previewHeader->addWidget(fit); previewLayout->addLayout(previewHeader);
    m_view = new RecolorPreviewView(review); m_view->setObjectName("characterRecolorView");
    m_view->setAccessibleName("Recolor preview"); m_view->setEmptyText("Choose drawings, then Preview");
    m_view->setToolTip("Drag to pan; scroll to zoom. Compare Before and After without changing the artwork.");
    previewLayout->addWidget(m_view, 1);
    m_caption = new QLabel(review); m_caption->setObjectName("characterRecolorCaption");
    m_caption->setTextFormat(Qt::PlainText); m_caption->setWordWrap(true); previewLayout->addWidget(m_caption);
    m_splitter->setStretchFactor(0, 0); m_splitter->setStretchFactor(1, 1); m_splitter->setSizes({260, 500});
    layout->addWidget(m_splitter, 1);
    m_status = new QLabel(this); m_status->setObjectName("characterRecolorStatus"); m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true); m_status->setAccessibleName("Recolor status"); layout->addWidget(m_status);
    auto *buttons = new QHBoxLayout; buttons->setSpacing(4);
    m_preview = new QPushButton(appIcon("eye"), "Preview", this); m_preview->setObjectName("characterRecolorPreview");
    m_preview->setToolTip("Prepare matching-color replacements for checked drawings without editing the project");
    m_preview->setAccessibleName("Preview recoloring"); m_preview->setAutoDefault(false); buttons->addWidget(m_preview); buttons->addStretch();
    m_cancel = new QPushButton("Cancel", this); m_cancel->setObjectName("characterRecolorCancel"); m_cancel->setAutoDefault(false);
    m_apply = new QPushButton(appIcon("check"), "Apply", this); m_apply->setObjectName("characterRecolorApply");
    m_apply->setToolTip("Apply this preview to all checked drawings as one undoable edit"); m_apply->setAccessibleName("Apply recoloring");
    m_apply->setAutoDefault(false); buttons->addWidget(m_cancel); buttons->addWidget(m_apply); layout->addLayout(buttons);
    for (auto *button : {m_preview, m_apply, m_cancel}) button->setMinimumHeight(ui::metrics::actionTarget);

    {
        QScopedValueRollback<bool> refreshing(m_refreshing, true);
        if (m_document && m_document->isOpen()) for (const auto &entry : m_document->project().characters)
            m_characters->addItem(entry.name, entry.id);
        const int selected = m_characters->findData(characterId);
        m_characters->setCurrentIndex(selected >= 0 ? selected : (m_characters->count() ? 0 : -1));
        refreshPalettes({}, targetVariantId); refreshDrawings(true);
    }
    connect(m_preview, &QPushButton::clicked, this, &CharacterRecolorDialog::preview);
    connect(m_apply, &QPushButton::clicked, this, &CharacterRecolorDialog::applyPreview);
    connect(m_cancel, &QPushButton::clicked, this, &CharacterRecolorDialog::reject);
    connect(m_all, &QToolButton::clicked, this, [this] { checkDrawings(true); });
    connect(m_none, &QToolButton::clicked, this, [this] { checkDrawings(false); });
    connect(fit, &QToolButton::clicked, m_view, &ReferenceView::fitImage);
    connect(m_compare, &QTabBar::currentChanged, this, [this](int index) { m_view->showAfter(index == 1); });
    connect(m_drawings, &QListWidget::currentRowChanged, this, [this] { showDrawing(); });
    connect(m_drawings, &QListWidget::itemChanged, this, [this] { if (!m_refreshing) invalidate(); });
    connect(m_characters, &QComboBox::currentIndexChanged, this, [this] {
        if (m_refreshing) return;
        { QScopedValueRollback<bool> refreshing(m_refreshing, true); refreshPalettes(); }
        invalidate();
    });
    for (auto *palette : {m_source, m_target}) connect(palette, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_refreshing) invalidate();
    });
    connect(m_scope, &QComboBox::currentIndexChanged, this, [this] {
        if (m_refreshing) return;
        { QScopedValueRollback<bool> refreshing(m_refreshing, true); refreshDrawings(); }
        invalidate();
    });
    connect(&m_worker, &QFutureWatcher<RecolorResult>::finished, this, &CharacterRecolorDialog::finishPreview);
    if (m_document) {
        connect(m_document, &Document::changed, this, &CharacterRecolorDialog::refreshDocument);
        connect(m_document, &Document::loaded, this, &CharacterRecolorDialog::refreshDocument);
        connect(m_document, &Document::shotChanged, this, &CharacterRecolorDialog::refreshDocument);
        connect(m_document, &QObject::destroyed, this, [this] { invalidate("The project is no longer available."); });
    }
    invalidate();
}

CharacterRecolorDialog::~CharacterRecolorDialog() {
    if (m_cancellation) m_cancellation->store(true);
}
const CharacterModel *CharacterRecolorDialog::character() const {
    if (m_document && m_document->isOpen()) for (const auto &entry : m_document->project().characters)
        if (entry.id == m_characters->currentData().toString()) return &entry;
    return nullptr;
}
void CharacterRecolorDialog::refreshPalettes(const QString &sourceId, const QString &targetId, bool defaultTarget) {
    m_source->clear(); m_target->clear();
    const auto *model = character(); if (!model) return;
    for (auto *palette : {m_source, m_target}) {
        palette->addItem("Day", QString());
        for (const auto &variant : model->colorVariants) palette->addItem(variant.name, variant.id);
    }
    m_source->setCurrentIndex(qMax(0, m_source->findData(sourceId)));
    int targetIndex = m_target->findData(targetId);
    if (defaultTarget && targetId.isEmpty() && m_target->count() > 1) targetIndex = 1;
    m_target->setCurrentIndex(qMax(0, targetIndex));
}
void CharacterRecolorDialog::refreshDrawings(bool initial) {
    QSet<QString> checked;
    for (int i = 0; i < m_drawings->count(); ++i) if (m_drawings->item(i)->checkState() == Qt::Checked)
        checked.insert(itemKey(m_drawings->item(i)));
    const auto current = itemKey(m_drawings->currentItem()); m_drawings->clear();
    if (!m_document || !m_document->isOpen()) return;
    for (const auto &shot : m_document->project().shots) {
        if (m_scope->currentIndex() == 0 && shot.id != m_document->activeShotId()) continue;
        for (int index = 0; index < shot.layers.size(); ++index) {
            const auto &layer = shot.layers[index];
            if (layer.group || layer.kind != LayerKind::Raster || layer.scene3d || layer.adjustment) continue;
            auto *item = new QListWidgetItem(drawingName(shot, layer), m_drawings);
            item->setData(Qt::UserRole, shot.id); item->setData(Qt::UserRole + 1, layer.id);
            item->setData(Qt::UserRole + 2, item->text());
            const auto error = characterRecolorTargetError(shot, index);
            item->setToolTip(item->text() + (error.isEmpty() ? QString() : "\n" + error));
            const bool active = shot.id == m_document->activeShotId() && index == m_document->activeLayer();
            item->setCheckState(error.isEmpty() && (checked.contains(itemKey(item)) || (initial && active)) ? Qt::Checked : Qt::Unchecked);
            if (!error.isEmpty()) item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsUserCheckable);
            if (itemKey(item) == current || (initial && active)) m_drawings->setCurrentItem(item);
        }
    }
}
void CharacterRecolorDialog::refreshDocument() {
    if (m_applying) return;
    const auto characterId = m_characters->currentData().toString();
    const auto sourceId = m_source->currentData().toString(), targetId = m_target->currentData().toString();
    {
        QScopedValueRollback<bool> refreshing(m_refreshing, true); m_characters->clear();
        if (m_document && m_document->isOpen()) for (const auto &model : m_document->project().characters)
            m_characters->addItem(model.name, model.id);
        const int index = m_characters->findData(characterId); m_characters->setCurrentIndex(index);
        refreshPalettes(sourceId, targetId, false); refreshDrawings();
    }
    invalidate("The project changed. Preview the drawings again.");
}
void CharacterRecolorDialog::invalidate(const QString &message) {
    ++m_revision; if (m_cancellation) m_cancellation->store(true);
    m_result = {}; m_previewKey.clear(); m_view->setPair({}, {}); m_caption->clear();
    {
        QScopedValueRollback<bool> refreshing(m_refreshing, true);
        for (int i = 0; i < m_drawings->count(); ++i) {
            auto *item = m_drawings->item(i); item->setText(item->data(Qt::UserRole + 2).toString()); item->setIcon({});
        }
    }
    m_status->setText(message.isEmpty() ? "Choose drawings, then Preview. Other colors and transparency stay unchanged." : message);
    updateActions();
}
void CharacterRecolorDialog::checkDrawings(bool checked) {
    {
        QScopedValueRollback<bool> refreshing(m_refreshing, true);
        for (int i = 0; i < m_drawings->count(); ++i) {
            auto *item = m_drawings->item(i); if (item->flags().testFlag(Qt::ItemIsEnabled)) item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        }
    }
    invalidate();
}
void CharacterRecolorDialog::preview() {
    if (m_busy) return;
    if (!m_document || !m_document->isOpen() || m_document->editingGestureActive() || m_document->layerPropertyEditActive()) {
        invalidate("Finish the current drawing or property edit first."); return;
    }
    const auto *model = character();
    if (!model || m_source->currentIndex() < 0 || m_target->currentIndex() < 0) { invalidate("Choose a character and two palettes."); return; }
    RecolorSettings settings{*model, m_source->currentData().toString(), m_target->currentData().toString(),
                             QSize(m_document->project().width, m_document->project().height)};
    QVector<RecolorTarget> targets;
    for (int i = 0; i < m_drawings->count(); ++i) {
        const auto *item = m_drawings->item(i); if (item->checkState() != Qt::Checked) continue;
        for (const auto &shot : m_document->project().shots) if (shot.id == item->data(Qt::UserRole).toString())
            for (int index = 0; index < shot.layers.size(); ++index) if (shot.layers[index].id == item->data(Qt::UserRole + 1).toString()) {
                const auto error = characterRecolorTargetError(shot, index);
                if (!error.isEmpty()) { invalidate(error); return; }
                targets.append({shot.id, shot.layers[index].id, item->data(Qt::UserRole + 2).toString(), shot.layers[index]});
            }
    }
    if (targets.isEmpty()) { invalidate("Select at least one available raster drawing."); return; }
    invalidate("Preparing recolor preview…"); m_workerRevision = m_revision; m_busy = true;
    m_cancellation = std::make_shared<std::atomic_bool>(false); const auto cancellation = m_cancellation;
    updateActions();
    m_worker.setFuture(QtConcurrent::run([targets = std::move(targets), settings, cancellation]() mutable {
        return prepareCharacterRecolor(std::move(targets), settings, cancellation);
    }));
}
void CharacterRecolorDialog::finishPreview() {
    m_busy = false;
    const auto prepared = m_worker.result();
    if (m_workerRevision != m_revision || prepared.cancelled || (m_cancellation && m_cancellation->load()) || !m_document) {
        updateActions(); emit previewFinished(); return;
    }
    m_result = prepared;
    if (!m_result.error.isEmpty()) m_status->setText(m_result.error);
    else if (!m_result.changedPixels) m_status->setText(m_result.matchedPixels
        ? "The matching colors already use this palette. No changes to apply."
        : "No source palette colors matched. Check the From palette and selected drawings.");
    else {
        int changed = 0; for (const auto &drawing : m_result.drawings) if (drawing.changedPixels) ++changed;
        m_status->setText(QString("%1 drawings · %2 pixels will change. Apply creates one Undo.").arg(changed).arg(m_result.changedPixels)
            + (m_result.ambiguousEdgePixels ? QString(" %1 ambiguous edge pixels remain unchanged.").arg(m_result.ambiguousEdgePixels) : QString()));
    }
    {
        QScopedValueRollback<bool> refreshing(m_refreshing, true); QListWidgetItem *first = nullptr;
        for (int i = 0; i < m_drawings->count(); ++i) {
            auto *item = m_drawings->item(i);
            for (const auto &drawing : m_result.drawings) if (itemKey(item) == targetKey(drawing.target.shotId, drawing.target.layerId)) {
                if (!first) first = item;
                item->setText(item->data(Qt::UserRole + 2).toString() + (drawing.changedPixels ? QString(" · %1 pixels").arg(drawing.changedPixels) : " · unchanged"));
                item->setIcon(QPixmap::fromImage(drawing.after.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                break;
            }
        }
        bool currentPrepared = false;
        for (const auto &drawing : m_result.drawings) if (itemKey(m_drawings->currentItem()) == targetKey(drawing.target.shotId, drawing.target.layerId)) currentPrepared = true;
        if (!currentPrepared && first) m_drawings->setCurrentItem(first);
    }
    showDrawing(); updateActions(); emit previewFinished();
}
void CharacterRecolorDialog::showDrawing() {
    const auto key = itemKey(m_drawings->currentItem());
    for (const auto &drawing : m_result.drawings) if (key == targetKey(drawing.target.shotId, drawing.target.layerId)) {
        if (key != m_previewKey) { m_view->setPair(drawing.before, drawing.after); m_previewKey = key; }
        m_caption->setText("Source drawing · " + drawing.target.name + QString(" · %1 matching / %2 changed pixels").arg(drawing.matchedPixels).arg(drawing.changedPixels));
        return;
    }
    m_previewKey.clear(); m_view->setPair({}, {}); m_caption->clear();
}
void CharacterRecolorDialog::updateActions() {
    bool checked = false, available = false;
    for (int i = 0; i < m_drawings->count(); ++i) {
        const auto *item = m_drawings->item(i); available |= item->flags().testFlag(Qt::ItemIsEnabled);
        checked |= item->checkState() == Qt::Checked;
    }
    const bool ready = m_document && m_document->isOpen() && character();
    m_preview->setEnabled(ready && checked && !m_busy);
    m_apply->setEnabled(ready && !m_busy && m_result.error.isEmpty() && !m_result.cancelled && m_result.changedPixels > 0);
    m_compare->setEnabled(!m_result.drawings.isEmpty() && m_result.error.isEmpty());
    m_all->setEnabled(available); m_none->setEnabled(checked);
    m_preview->setText(m_busy ? "Preparing…" : "Preview");
}
bool CharacterRecolorDialog::applyPreview() {
    if (m_busy || m_result.changedPixels <= 0) return false;
    QString error; bool applied = false;
    { QScopedValueRollback<bool> applying(m_applying, true); applied = applyCharacterRecolor(m_document, m_result, &error); }
    if (!applied) { invalidate(error); return false; }
    accept(); return true;
}
void CharacterRecolorDialog::reject() {
    if (m_cancellation) m_cancellation->store(true);
    ++m_revision; m_result = {}; QDialog::reject();
}
void CharacterRecolorDialog::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    if (!m_splitter) return;
    const auto direction = width() < 640 ? Qt::Vertical : Qt::Horizontal;
    if (m_splitter->orientation() != direction) {
        m_splitter->setOrientation(direction);
        m_splitter->setSizes(direction == Qt::Vertical ? QList<int>{230, 230} : QList<int>{260, width() - 280});
    }
}
} // namespace toon
