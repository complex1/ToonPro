#include "drawing_reuse_dialog.h"
#include "animation.h"
#include "canvas.h"
#include "cel_edit.h"
#include "document.h"
#include "effects_preview_renderer.h"
#include "icons.h"
#include "property_row.h"
#include "timeline_controller.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include "value_slider.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QKeySequence>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace toon {
namespace {
constexpr int drawingRole = Qt::UserRole;
constexpr int useCountRole = Qt::UserRole + 1;
constexpr int searchRole = Qt::UserRole + 2;
constexpr int thumbnailRole = Qt::UserRole + 3;
const AnimationTrack *trackFor(const Shot &shot, const QString &id) {
    for (const auto &track : shot.animationTracks) if (track.id == id) return &track;
    return nullptr;
}
const RasterLayer *layerFor(const Shot &shot, const QString &id) {
    for (const auto &layer : shot.layers) if (layer.id == id) return &layer;
    return nullptr;
}
QImage thumbnailBackground(QSize size) {
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(ui::base));
    QPainter painter(&image);
    for (int y = 0; y < size.height(); y += 12)
        for (int x = 0; x < size.width(); x += 12)
            if ((x / 12 + y / 12) % 2) painter.fillRect(x, y, 12, 12, QColor(ui::raised));
    return image;
}
EffectsPreviewSnapshot thumbnailSnapshot(const Project &project, const Shot &source,
                                        const AnimationTrack &track, const AnimationDrawing &drawing, int frame) {
    EffectsPreviewSnapshot snapshot;
    snapshot.project.width = project.width;
    snapshot.project.height = project.height;
    snapshot.project.fps = project.fps;
    snapshot.project.symbols = project.symbols;
    snapshot.shot.id = source.id;
    snapshot.shot.durationFrames = source.durationFrames;
    QSet<QString> included{track.rootGroupLayerId, drawing.groupLayerId};
    for (int i = 0; i < source.layers.size(); ++i)
        if (layerIsDescendant(source.layers, i, drawing.groupLayerId)) included.insert(source.layers[i].id);
    for (const auto &layer : source.layers) if (included.contains(layer.id)) {
        auto copy = layer;
        // Inspect the drawing even when its track or drawing folder is hidden.
        // Individual artwork-layer visibility still belongs to the drawing.
        if (copy.id == track.rootGroupLayerId || copy.id == drawing.groupLayerId) copy.visible = true;
        snapshot.shot.layers.append(std::move(copy));
    }
    for (const auto &animation : source.layerAnimations)
        if (included.contains(animation.layerId)) snapshot.shot.layerAnimations.append(animation);
    auto isolated = track;
    isolated.drawings = {drawing};
    isolated.exposures = {{QStringLiteral("thumbnail-exposure"), drawing.id, 0, source.durationFrames}};
    snapshot.shot.animationTracks = {isolated};
    snapshot.frame = frame;
    snapshot.pixelSize = {192, 128};
    snapshot.playback = true; // Explicit reduction, without an idle exact refinement.
    const int longest = std::max(project.width, project.height);
    snapshot.playbackDivisor = longest > 1536 ? 8 : longest > 768 ? 4 : longest > 384 ? 2 : 1;
    snapshot.smoothDisplay = true;
    snapshot.applyCamera = false;
    snapshot.guides = false;
    snapshot.drawPaperShadow = false;
    const qreal scale = std::min(184. / project.width, 120. / project.height);
    snapshot.view.translate((192 - project.width * scale) / 2, (128 - project.height * scale) / 2);
    snapshot.view.scale(scale, scale);
    const QRectF artwork(0, 0, project.width, project.height);
    snapshot.preparedGeometry = SheetGeometry{artwork, {}, artwork, artwork};
    snapshot.preparedBackground = thumbnailBackground(snapshot.pixelSize);
    snapshot.preparedBackground.setDotsPerMeterX(snapshot.dotsPerMeterX);
    snapshot.preparedBackground.setDotsPerMeterY(snapshot.dotsPerMeterY);
    return snapshot;
}
}

DrawingReuseDialog::DrawingReuseDialog(TimelineController *controller, QWidget *parent)
    : QDialog(parent), m_controller(controller), m_renderer(new EffectsPreviewRenderer(this)) {
    setObjectName("drawingReuseDialog");
    setWindowTitle("Reuse drawing");
    setMinimumSize(260, 300);
    resize(420, 430);
    if (controller->document()->isOpen()) m_shotId = controller->document()->activeShot().id;
    m_trackId = controller->selectedTrackId();
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(ui::metrics::sectionGap);
    m_target = new QLabel;
    m_target->setObjectName("drawingReuseTarget");
    m_target->setTextFormat(Qt::PlainText);
    m_target->setWordWrap(true);
    layout->addWidget(m_target);
    auto *searchRow = new QHBoxLayout;
    searchRow->setSpacing(ui::metrics::controlGap);
    m_search = new QLineEdit;
    m_search->setObjectName("drawingReuseSearch");
    m_search->setMinimumWidth(0);
    m_search->setPlaceholderText("Search drawings");
    m_search->setAccessibleName("Search drawings by name or ID");
    m_search->setClearButtonEnabled(true);
    for (auto *button : m_search->findChildren<QToolButton *>()) {
        button->setObjectName("drawingReuseSearchClear");
        button->setAccessibleName("Clear search"); button->setToolTip("Clear search");
        button->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon});
    }
    searchRow->addWidget(m_search, 1);
    m_remove = new QToolButton;
    m_remove->setObjectName("drawingReuseRemoveUnused");
    m_remove->setIcon(appIcon("trash"));
    m_remove->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon});
    m_remove->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
    m_remove->setToolTip("Delete unused drawing · Undo: Ctrl/Cmd+Z");
    m_remove->setAccessibleName("Delete unused drawing");
    searchRow->addWidget(m_remove);
    layout->addLayout(searchRow);
    m_drawings = new QListWidget;
    m_drawings->setObjectName("drawingReuseList");
    m_drawings->setAccessibleName("Existing drawings in this track");
    m_drawings->setIconSize({72, 48});
    m_drawings->setMinimumWidth(0);
    m_drawings->setMinimumHeight(100);
    m_drawings->setTextElideMode(Qt::ElideRight);
    m_drawings->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_drawings->setSelectionMode(QAbstractItemView::SingleSelection);
    m_drawings->setUniformItemSizes(true);
    m_drawings->viewport()->installEventFilter(this);
    layout->addWidget(m_drawings, 1);
    m_hold = new ValueSlider("Hold in frames", 1, std::max(1, controller->frameCount()), 1);
    m_hold->setObjectName("drawingReuseHold");
    m_hold->setSliderRange(1, std::min(24, std::max(1, controller->frameCount())));
    layout->addWidget(new PropertyRow("Hold", m_hold));
    auto *shared = new QLabel("Shared drawing · changes appear in every use.");
    shared->setObjectName("drawingReuseSharedNote");
    shared->setWordWrap(true);
    shared->setStyleSheet(QString("color: %1;").arg(ui::muted));
    layout->addWidget(shared);
    m_status = new QLabel;
    m_status->setObjectName("drawingReuseStatus");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    m_status->setMinimumHeight(fontMetrics().height());
    layout->addWidget(m_status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    m_reuse = buttons->addButton("Reuse", QDialogButtonBox::AcceptRole);
    m_reuse->setObjectName("drawingReuseConfirm");
    m_reuse->setDefault(false);
    m_reuse->setAutoDefault(false);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_reuse, &QPushButton::clicked, this, &DrawingReuseDialog::reuse);
    layout->addWidget(buttons);

    connect(m_search, &QLineEdit::textChanged, this, &DrawingReuseDialog::filter);
    connect(m_drawings, &QListWidget::itemSelectionChanged, this, [this] {
        if (m_refreshing) return;
        showStatus({}); updateActions(); requestThumbnail();
    });
    connect(m_drawings, &QListWidget::itemDoubleClicked, this, [this] { reuse(); });
    connect(m_drawings->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { requestThumbnail(); });
    connect(m_remove, &QToolButton::clicked, this, &DrawingReuseDialog::removeUnused);
    connect(controller->document(), &Document::warning, this, &DrawingReuseDialog::showStatus);
    connect(controller->document(), &Document::changed, this, &DrawingReuseDialog::refresh);
    connect(controller->document(), &Document::loaded, this, &QDialog::reject);
    connect(controller, &TimelineController::changed, this, [this] {
        if (!sameContext()) { reject(); return; }
        if (m_previewFrame != m_controller->currentFrame()) { refresh(); return; }
        const auto *track = trackFor(m_controller->document()->activeShot(), m_trackId);
        const auto *root = track ? layerFor(m_controller->document()->activeShot(), track->rootGroupLayerId) : nullptr;
        const QString target = QString("%1 · Frame %2").arg(root ? root->name : QString("Drawings")).arg(m_controller->currentFrame() + 1);
        m_target->setText(target); updateActions();
    });
    auto *undo = new QShortcut(QKeySequence::Undo, this);
    auto *redo = new QShortcut(QKeySequence::Redo, this);
    connect(undo, &QShortcut::activated, this, [this] { if (sameContext()) m_controller->document()->history()->undo(); });
    connect(redo, &QShortcut::activated, this, [this] { if (sameContext()) m_controller->document()->history()->redo(); });
    connect(m_renderer, &EffectsPreviewRenderer::completed, this, [this](const EffectsPreviewResult &result) {
        if (result.context != m_context || result.revision != m_revision) return;
        const auto id = m_requestedDrawing; m_requestActive = false; m_requestedDrawing.clear();
        for (int i = 0; i < m_drawings->count(); ++i) {
            auto *item = m_drawings->item(i);
            if (item->data(drawingRole).toString() != id) continue;
            item->setData(thumbnailRole, true);
            if (!result.image.isNull()) item->setIcon(QIcon(QPixmap::fromImage(result.image)));
            else if (!result.error.isEmpty()) item->setToolTip(item->toolTip() + "\nPreview unavailable: " + result.error);
            break;
        }
        QTimer::singleShot(0, this, &DrawingReuseDialog::requestThumbnail);
    });
    refresh();
}
DrawingReuseDialog::~DrawingReuseDialog() = default;

bool DrawingReuseDialog::sameContext() const {
    return m_controller->document()->isOpen() && m_controller->document()->activeShot().id == m_shotId
        && m_controller->selectedTrackId() == m_trackId
        && trackFor(m_controller->document()->activeShot(), m_trackId);
}
void DrawingReuseDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    QTimer::singleShot(0, this, &DrawingReuseDialog::requestThumbnail);
}
bool DrawingReuseDialog::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_drawings->viewport() && event->type() == QEvent::Resize)
        QTimer::singleShot(0, this, &DrawingReuseDialog::requestThumbnail);
    return QDialog::eventFilter(watched, event);
}
void DrawingReuseDialog::refresh() {
    if (!sameContext()) { m_reuse->setEnabled(false); m_remove->setEnabled(false); if (isVisible()) reject(); return; }
    const QScopedValueRollback<bool> guard(m_refreshing, true);
    const auto selected = m_drawings->currentItem() ? m_drawings->currentItem()->data(drawingRole).toString() : QString();
    const auto &shot = m_controller->document()->activeShot();
    const auto *track = trackFor(shot, m_trackId);
    m_previewFrame = m_controller->currentFrame();
    const auto *root = layerFor(shot, track->rootGroupLayerId);
    m_target->setText(QString("%1 · Frame %2").arg(root ? root->name : QString("Drawings")).arg(m_controller->currentFrame() + 1));
    ++m_context; m_renderer->invalidate(); m_requestActive = false; m_requestedDrawing.clear();
    const QSignalBlocker block(m_drawings);
    m_drawings->clear();
    QString current = selected;
    if (current.isEmpty()) if (const auto *drawing = drawingAtFrame(*track, m_controller->currentFrame())) current = drawing->id;
    for (const auto &drawing : track->drawings) {
        const auto *group = layerFor(shot, drawing.groupLayerId);
        const auto label = drawingDisplayId(*track, drawing.id);
        const auto name = group ? group->name : QString("Drawing");
        const int uses = std::count_if(track->exposures.cbegin(), track->exposures.cend(), [&](const auto &cel) { return cel.drawingId == drawing.id; });
        const QString usage = uses ? QString("%1 %2").arg(uses).arg(uses == 1 ? "use" : "uses") : QString("Unused");
        auto *item = new QListWidgetItem(appIcon("image"), label + " · " + name + "\n" + usage, m_drawings);
        item->setData(drawingRole, drawing.id); item->setData(useCountRole, uses);
        item->setData(searchRole, label + " " + name + " " + usage); item->setData(thumbnailRole, false);
        item->setData(Qt::AccessibleTextRole, label + ", " + name + ", " + usage);
        item->setToolTip(label + " · " + name + "\n" + usage + " · Shared artwork");
        item->setSizeHint({0, std::max(56, fontMetrics().height() * 2 + 8)});
        if (drawing.id == current) m_drawings->setCurrentItem(item);
    }
    filter();
}
void DrawingReuseDialog::filter() {
    const auto query = m_search->text().trimmed();
    QListWidgetItem *first = nullptr;
    for (int i = 0; i < m_drawings->count(); ++i) {
        auto *item = m_drawings->item(i);
        const bool shown = item->data(searchRole).toString().contains(query, Qt::CaseInsensitive);
        item->setHidden(!shown); if (shown && !first) first = item;
    }
    if (!m_drawings->currentItem() || m_drawings->currentItem()->isHidden()) m_drawings->setCurrentItem(first);
    showStatus(first ? QString() : m_drawings->count() ? QString("No matching drawings.") : QString("No drawings in this track."));
    updateActions();
    QTimer::singleShot(0, this, &DrawingReuseDialog::requestThumbnail);
}
void DrawingReuseDialog::updateActions() {
    const auto *item = m_drawings->currentItem();
    const bool selected = sameContext() && item && !item->isHidden();
    const auto reason = selected ? celDrawingEditError(m_controller->document()->activeShot(), m_trackId, item->data(drawingRole).toString()) : QString();
    const bool editable = selected && !m_controller->playing() && reason.isEmpty();
    m_reuse->setEnabled(editable);
    m_reuse->setToolTip(reason.isEmpty() ? QString("Add another exposure of this drawing at the current frame") : reason);
    m_remove->setEnabled(editable && item->data(useCountRole).toInt() == 0);
}
void DrawingReuseDialog::requestThumbnail() {
    if (!isVisible() || m_requestActive || !sameContext()) return;
    QListWidgetItem *candidate = nullptr;
    auto *current = m_drawings->currentItem();
    if (current && !current->isHidden() && !current->data(thumbnailRole).toBool()) candidate = current;
    for (int i = 0; !candidate && i < m_drawings->count(); ++i) {
        auto *item = m_drawings->item(i);
        if (!item->isHidden() && !item->data(thumbnailRole).toBool()
            && m_drawings->visualItemRect(item).intersects(m_drawings->viewport()->rect())) candidate = item;
    }
    if (!candidate) return;
    const auto id = candidate->data(drawingRole).toString();
    const auto &shot = m_controller->document()->activeShot();
    const auto *track = trackFor(shot, m_trackId);
    for (const auto &drawing : track->drawings) if (drawing.id == id) {
        auto snapshot = thumbnailSnapshot(m_controller->document()->project(), shot, *track, drawing, m_controller->currentFrame());
        snapshot.context = m_context; snapshot.revision = ++m_revision;
        m_requestActive = true; m_requestedDrawing = id;
        m_renderer->request(std::move(snapshot)); return;
    }
}
void DrawingReuseDialog::reuse() {
    if (!sameContext() || !m_reuse->isEnabled()) return;
    const auto *item = m_drawings->currentItem(); if (!item) return;
    const auto id = item->data(drawingRole).toString();
    m_hold->commitEditing(); showStatus({});
    if (m_controller->exposeExistingDrawing(id, qRound(m_hold->value()))) accept();
    else if (m_status->text().isEmpty()) showStatus("This drawing cannot be reused at the current frame.");
}
void DrawingReuseDialog::removeUnused() {
    if (!sameContext() || !m_remove->isEnabled()) return;
    const auto *item = m_drawings->currentItem(); if (!item) return;
    const auto id = item->data(drawingRole).toString(); showStatus({});
    if (!m_controller->removeUnusedDrawing(id) && m_status->text().isEmpty())
        showStatus("This drawing cannot be deleted.");
}
void DrawingReuseDialog::showStatus(const QString &message) {
    m_status->setText(message); m_status->setToolTip(message); m_status->setAccessibleName(message);
}
} // namespace toon
