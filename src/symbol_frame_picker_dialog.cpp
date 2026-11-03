#include "symbol_frame_picker_dialog.h"
#include "symbol_poses.h"
#include "canvas.h"
#include "document.h"
#include "effects_preview_renderer.h"
#include "icons.h"
#include "property_row.h"
#include "symbol_clips.h"
#include "symbols.h"
#include "timeline_controller.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include "value_slider.h"

#include <QCheckBox>
#include <QApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QSet>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace toon {
namespace {
constexpr int pageSize = 12;
constexpr int frameRole = Qt::UserRole;
constexpr int readyRole = Qt::UserRole + 1;
constexpr int errorRole = Qt::UserRole + 2;

const RasterLayer *layerFor(const Shot &shot, const QString &id) {
    for (const auto &layer : shot.layers) if (layer.id == id) return &layer;
    return nullptr;
}
QToolButton *pageButton(const char *name, const char *icon, const QString &help) {
    auto *button = new QToolButton;
    button->setObjectName(name);
    button->setIcon(appIcon(icon));
    button->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon});
    button->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
    button->setToolTip(help);
    button->setAccessibleName(help);
    return button;
}
QRectF previewBounds(const Project &project, const Shot &shot, int frame, QSet<QString> &seen,
                     const EffectsPreviewCancellation &cancel, int depth = 0) {
    QRectF result;
    if (depth > 4) return result;
    for (const auto &layer : shot.layers) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return {};
        if (!layer.visible || layer.opacity <= 0 || !layerPresentAtFrame(shot, layer.id, frame)) continue;
        bool visible = true;
        const auto *ancestor = &layer;
        for (int ancestry = 0; ancestor && ancestry <= 8; ++ancestry) {
            if (!ancestor->visible || ancestor->opacity <= 0
                || evaluateProperty(shot, ancestor->id, KeyframeProperty::Opacity, frame) <= 0) { visible = false; break; }
            ancestor = ancestor->parentId.isEmpty() ? nullptr : layerFor(shot, ancestor->parentId);
        }
        if (!visible) continue;
        QRectF local;
        // Conservative geometric bounds avoid rendering pixels merely to find
        // the frame. This runs on the preview worker, including vector bounds.
        for (auto tile = layer.tiles.cbegin(); tile != layer.tiles.cend(); ++tile)
            local |= QRectF(tile.key().first * tileSize, tile.key().second * tileSize, tile->width(), tile->height());
        for (const auto &object : layer.objects)
            local |= editableObjectBounds(evaluateAnimatedObject(shot, layer.id, object, frame));
        if (layer.symbol && !seen.contains(layer.symbol->symbolId)) {
            if (const auto *nested = findSymbol(project, layer.symbol->symbolId)) {
                const int sourceFrame = symbolLocalFrame(*layer.symbol, frame, nested->content.durationFrames);
                if (sourceFrame >= 0) {
                    seen.insert(nested->id);
                    local |= previewBounds(project, nested->content, sourceFrame, seen, cancel, depth + 1);
                    seen.remove(nested->id);
                }
            }
        }
        if (!local.isEmpty()) result |= worldLayerTransform(shot, layer.id, frame).mapRect(local);
    }
    return result;
}
EffectsPreviewResult renderThumbnail(const EffectsPreviewSnapshot &source, const EffectsPreviewCancellation &cancel) {
    auto snapshot = source;
    QSet<QString> seen;
    auto bounds = previewBounds(snapshot.project, snapshot.shot, snapshot.frame, seen, cancel);
    if (bounds.isEmpty() || !std::isfinite(bounds.left()) || !std::isfinite(bounds.top())
        || !std::isfinite(bounds.width()) || !std::isfinite(bounds.height()))
        bounds = QRectF(0, 0, snapshot.project.width, snapshot.project.height);
    const qreal scale = std::min(184. / bounds.width(), 120. / bounds.height());
    snapshot.view = {};
    snapshot.view.translate(96 - bounds.center().x() * scale, 64 - bounds.center().y() * scale);
    snapshot.view.scale(scale, scale);
    // Artwork coordinates stay unchanged; only the preview view moves. The
    // output origin must remain zero because sheet painting translates by it.
    snapshot.preparedGeometry = SheetGeometry{bounds, {}, bounds,
        QRectF(0, 0, snapshot.project.width, snapshot.project.height)};
    const qreal longest = std::max(bounds.width(), bounds.height());
    snapshot.playbackDivisor = longest > 1536 ? 8 : longest > 768 ? 4 : longest > 384 ? 2 : 1;
    return renderEffectsPreview(snapshot, cancel);
}
EffectsPreviewSnapshot thumbnailSnapshot(const Project &project, const Shot &content, int frame) {
    EffectsPreviewSnapshot snapshot;
    snapshot.project = project;
    snapshot.shot = content;
    snapshot.frame = frame;
    snapshot.pixelSize = {192, 128};
    snapshot.playback = true; // One bounded render, without an idle full-size refinement.
    snapshot.smoothDisplay = true;
    snapshot.applyCamera = false;
    snapshot.guides = false;
    snapshot.drawPaperShadow = false;
    // The renderer scheduler requires both prepared fields. Keep the artwork
    // origin at zero here; the worker replaces this placeholder with fitted
    // geometry without regenerating a production sheet over the checkerboard.
    const QRectF artwork(0, 0, project.width, project.height);
    snapshot.preparedGeometry = SheetGeometry{artwork, {}, artwork, artwork};
    QImage background(snapshot.pixelSize, QImage::Format_ARGB32_Premultiplied);
    background.fill(QColor(ui::base));
    QPainter painter(&background);
    for (int y = 0; y < background.height(); y += 12)
        for (int x = 0; x < background.width(); x += 12)
            if ((x / 12 + y / 12) % 2) painter.fillRect(x, y, 12, 12, QColor(ui::raised));
    painter.end();
    background.setDotsPerMeterX(snapshot.dotsPerMeterX);
    background.setDotsPerMeterY(snapshot.dotsPerMeterY);
    snapshot.preparedBackground = std::move(background);
    return snapshot;
}
} // namespace

SymbolFramePickerDialog::SymbolFramePickerDialog(TimelineController *controller, const QString &layerId, QWidget *parent, SymbolFramePickerMode mode, int poseParentFrame)
    : QDialog(parent), m_controller(controller), m_mode(mode), m_layerId(layerId), m_renderer(new EffectsPreviewRenderer(renderThumbnail, this)) {
    m_parentPlayhead = controller ? controller->currentFrame() : 0;
    m_poseParentFrame = poseParentFrame < 0 ? m_parentPlayhead : poseParentFrame;
    setObjectName("symbolFramePickerDialog");
    setWindowTitle(mode == SymbolFramePickerMode::TimedPose ? "Choose timed Symbol pose" : "Choose Symbol frame");
    setMinimumSize(260, 300);
    resize(470, 430);
    QString title = "Symbol frame";
    if (controller && controller->document()->isOpen()) {
        const auto &project = controller->document()->project();
        const auto &shot = controller->document()->activeShot();
        m_shotId = shot.id;
        if (const auto *layer = layerFor(shot, layerId); layer && layer->symbol) {
            m_original = *layer->symbol;
            if (const auto *definition = findSymbol(project, m_original.symbolId)) {
                m_valid = true;
                title = definition->name;
                m_frameCount = std::max(1, definition->content.durationFrames);
                m_selectedFrame = std::clamp(mode == SymbolFramePickerMode::TimedPose
                    ? (m_original.playback == SymbolPlayback::Poses ? evaluatedSymbolPose(m_original,m_poseParentFrame)
                        : std::max(0,symbolLocalFrame(m_original,m_poseParentFrame,m_frameCount)))
                    : m_original.firstFrame, 0, m_frameCount - 1);
                if (mode == SymbolFramePickerMode::TimedPose) title += QString(" · frame %1").arg(m_poseParentFrame + 1);
                m_previewProject.width = project.width;
                m_previewProject.height = project.height;
                m_previewProject.fps = project.fps;
                m_previewProject.symbols = project.symbols;
            }
        }
    }
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(ui::metrics::sectionGap);
    m_title = new QLabel(title);
    m_title->setObjectName("symbolFramePickerTitle");
    m_title->setTextFormat(Qt::PlainText);
    m_title->setWordWrap(true);
    layout->addWidget(m_title);

    m_frames = new QListWidget;
    m_frames->setObjectName("symbolFramePickerFrames");
    m_frames->setAccessibleName("Symbol frame previews");
    m_frames->setViewMode(QListView::IconMode);
    m_frames->setMovement(QListView::Static);
    m_frames->setResizeMode(QListView::Adjust);
    m_frames->setIconSize({96, 64});
    m_frames->setSpacing(ui::metrics::controlGap);
    m_frames->setUniformItemSizes(true);
    m_frames->setSelectionMode(QAbstractItemView::SingleSelection);
    m_frames->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_frames->setMinimumSize(0, 100);
    m_frames->setTextElideMode(Qt::ElideRight);
    m_frames->viewport()->installEventFilter(this);
    layout->addWidget(m_frames, 1);

    auto *pages = new QHBoxLayout;
    pages->setSpacing(ui::metrics::controlGap);
    m_previous = pageButton("symbolFramePickerPreviousPage", "chevron-left", "Previous page of frames");
    m_next = pageButton("symbolFramePickerNextPage", "chevron-right", "Next page of frames");
    m_pageLabel = new QLabel;
    m_pageLabel->setObjectName("symbolFramePickerPage");
    m_pageLabel->setAlignment(Qt::AlignCenter);
    m_pageLabel->setWordWrap(true);
    pages->addWidget(m_previous);
    pages->addWidget(m_pageLabel, 1);
    pages->addWidget(m_next);
    layout->addLayout(pages);

    m_frame = new ValueSlider(mode == SymbolFramePickerMode::TimedPose ? "Source pose frame" : "Starting frame", 1, m_frameCount, m_selectedFrame + 1);
    m_frame->setObjectName("symbolFramePickerFrame");
    m_frame->setAccessibleDescription(mode == SymbolFramePickerMode::TimedPose ? "Source pose held from the selected parent timeline frame" : "Source frame used at the start of this Symbol clip");
    layout->addWidget(new PropertyRow(mode == SymbolFramePickerMode::TimedPose ? "Pose" : "Starting frame", m_frame));
    m_hold = new QCheckBox("Hold selected frame");
    m_hold->setObjectName("symbolFramePickerHold");
    m_hold->setAccessibleName("Hold selected frame for the whole clip");
    m_hold->setToolTip("Keep this pose for the whole clip; turn off to play from the selected starting frame");
    m_hold->setChecked(m_original.playback == SymbolPlayback::SingleFrame);
    m_hold->setVisible(mode == SymbolFramePickerMode::ClipStart);
    layout->addWidget(m_hold);
    m_status = new QLabel;
    m_status->setObjectName("symbolFramePickerStatus");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    m_apply = buttons->addButton("Apply", QDialogButtonBox::AcceptRole);
    m_apply->setObjectName("symbolFramePickerApply");
    m_apply->setAutoDefault(false);
    m_apply->setDefault(false);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_apply, &QPushButton::clicked, this, &SymbolFramePickerDialog::apply);
    layout->addWidget(buttons);

    connect(m_previous, &QToolButton::clicked, this, [this] { setFrame(std::max(0, (m_page - 1) * pageSize)); });
    connect(m_next, &QToolButton::clicked, this, [this] { setFrame(std::min(m_frameCount - 1, (m_page + 1) * pageSize)); });
    connect(m_frame, &ValueSlider::valueChanged, this, [this](double value) { setFrame(int(value) - 1); });
    connect(m_frames, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item) setFrame(item->data(frameRole).toInt());
    });
    connect(m_frames->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { requestThumbnail(); });
    connect(m_renderer, &EffectsPreviewRenderer::completed, this, [this](const EffectsPreviewResult &result) {
        if (result.context != m_context || result.revision != m_revision || !sourceUnchanged()) return;
        m_requestActive = false;
        for (int i = 0; i < m_frames->count(); ++i) {
            auto *item = m_frames->item(i);
            if (item->data(frameRole).toInt() != m_requestedFrame) continue;
            item->setData(readyRole, true);
            item->setData(errorRole, result.error);
            if (!result.image.isNull()) item->setIcon(QIcon(QPixmap::fromImage(result.image)));
            if (!result.error.isEmpty()) item->setToolTip(item->toolTip() + "\nPreview unavailable: " + result.error);
            break;
        }
        m_requestedFrame = -1;
        updateActions();
        QTimer::singleShot(0, this, &SymbolFramePickerDialog::requestThumbnail);
    });
    if (controller) {
        connect(controller->document(), &Document::changed, this, &SymbolFramePickerDialog::checkSource);
        connect(controller->document(), &Document::loaded, this, [this] {
            m_valid = false; updateActions(); reject();
        });
        connect(controller, &TimelineController::changed, this, &SymbolFramePickerDialog::checkSource);
        connect(controller, &QObject::destroyed, this, &QDialog::reject);
    }
    showPage();
    updateActions();
}
SymbolFramePickerDialog::~SymbolFramePickerDialog() = default;

bool SymbolFramePickerDialog::sameContext() const {
    if (!m_valid || !m_controller || !m_controller->document()->isOpen()) return false;
    const auto &shot = m_controller->document()->activeShot();
    const auto *layer = layerFor(shot, m_layerId);
    return shot.id == m_shotId && layer && layer->symbol;
}
bool SymbolFramePickerDialog::sourceUnchanged() const {
    if (m_stale || !sameContext()) return false;
    const auto &project = m_controller->document()->project();
    const auto *layer = layerFor(m_controller->document()->activeShot(), m_layerId);
    return *layer->symbol == m_original
        && (m_mode != SymbolFramePickerMode::TimedPose || m_controller->currentFrame() == m_parentPlayhead)
        && project.width == m_previewProject.width && project.height == m_previewProject.height
        && project.fps == m_previewProject.fps
        && project.symbols.constData() == m_previewProject.symbols.constData();
}
void SymbolFramePickerDialog::checkSource() {
    if (m_applying) return;
    if (!sameContext()) {
        m_valid = false; updateActions(); reject(); return;
    }
    if (!sourceUnchanged()) {
        m_stale = true;
        ++m_context; m_renderer->invalidate(); m_requestActive = false;
    }
    updateActions();
}
void SymbolFramePickerDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    checkSource();
    QTimer::singleShot(0, this, &SymbolFramePickerDialog::requestThumbnail);
}
bool SymbolFramePickerDialog::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_frames->viewport() && event->type() == QEvent::Resize)
        QTimer::singleShot(0, this, &SymbolFramePickerDialog::requestThumbnail);
    return QDialog::eventFilter(watched, event);
}
void SymbolFramePickerDialog::done(int result) {
    m_closedWithFocus = QApplication::activeWindow() == this;
    ++m_context; m_renderer->invalidate(); m_requestActive = false;
    QDialog::done(result);
}
void SymbolFramePickerDialog::setFrame(int frame) {
    if (!sourceUnchanged()) return;
    m_selectedFrame = std::clamp(frame, 0, m_frameCount - 1);
    m_frame->setValueSilently(m_selectedFrame + 1);
    if (m_page != m_selectedFrame / pageSize) showPage();
    else {
        const QSignalBlocker block(m_frames);
        m_frames->setCurrentRow(m_selectedFrame - m_page * pageSize);
        m_frames->scrollToItem(m_frames->currentItem());
    }
    updateActions();
    requestThumbnail();
}
void SymbolFramePickerDialog::showPage() {
    ++m_context; m_renderer->invalidate(); m_requestActive = false; m_requestedFrame = -1;
    m_page = m_selectedFrame / pageSize;
    const QSignalBlocker block(m_frames);
    // A page is the entire thumbnail cache. A million-frame definition still
    // owns at most twelve items and one running / one pending worker snapshot.
    m_frames->clear();
    if (!m_valid) return;
    const int first = m_page * pageSize, end = std::min(first + pageSize, m_frameCount);
    const int itemWidth = std::max(108, fontMetrics().horizontalAdvance(QString("Frame %1").arg(m_frameCount)) + 12);
    for (int frame = first; frame < end; ++frame) {
        const QString label = QString("Frame %1").arg(frame + 1);
        auto *item = new QListWidgetItem(appIcon("image"), label, m_frames);
        item->setData(frameRole, frame);
        item->setData(readyRole, false);
        item->setData(Qt::AccessibleTextRole, label);
        item->setToolTip(label + (m_mode == SymbolFramePickerMode::TimedPose
            ? QString(" · Hold from timeline frame %1 until the next pose key").arg(m_poseParentFrame+1)
            : QString(" · Choose as the clip's starting frame")));
        item->setSizeHint({itemWidth, 72 + fontMetrics().height()});
    }
    m_frames->setCurrentRow(m_selectedFrame - first);
    m_frames->scrollToItem(m_frames->currentItem());
    m_pageLabel->setText(QString("%1–%2 of %3").arg(first + 1).arg(end).arg(m_frameCount));
    QTimer::singleShot(0, this, &SymbolFramePickerDialog::requestThumbnail);
}
void SymbolFramePickerDialog::updateActions() {
    const bool ready = sourceUnchanged();
    QString reason = ready ? symbolClipEditError(m_controller->document()->activeShot(), m_layerId) : QString();
    if (ready && reason.isEmpty() && m_mode == SymbolFramePickerMode::TimedPose
        && (m_poseParentFrame < 0 || m_poseParentFrame >= m_controller->frameCount()
            || (!symbolPoseKeyAtParent(m_original,m_poseParentFrame)
                && (m_poseParentFrame < m_original.startFrame
                    || (m_original.durationFrames >= 0 && m_poseParentFrame >= qint64(m_original.startFrame) + m_original.durationFrames)))))
        reason = "Choose a frame inside this Symbol clip to set a pose.";
    m_frames->setEnabled(ready);
    m_frame->setEnabled(ready);
    m_hold->setEnabled(ready && reason.isEmpty());
    m_previous->setEnabled(ready && m_page > 0);
    m_next->setEnabled(ready && (m_page + 1) * pageSize < m_frameCount);
    m_apply->setEnabled(ready && reason.isEmpty());
    m_apply->setToolTip(reason.isEmpty() ? (m_mode == SymbolFramePickerMode::TimedPose
        ? QString("Hold this pose from frame %1 until the next pose key").arg(m_poseParentFrame+1)
        : QString("Apply this starting frame to the whole Symbol clip")) : reason);
    if (!m_valid) showStatus("This Symbol instance is no longer available.");
    else if (m_stale) showStatus("The Symbol or its playback changed. Reopen the frame picker to continue.");
    else if (!reason.isEmpty()) showStatus(reason);
    else {
        const auto *item = m_frames->currentItem();
        const auto error = item ? item->data(errorRole).toString() : QString();
        showStatus(error.isEmpty() ? QString() : "Preview unavailable: " + error);
    }
}
void SymbolFramePickerDialog::requestThumbnail() {
    if (!isVisible() || m_requestActive || !sourceUnchanged()) return;
    auto *candidate = m_frames->currentItem();
    if (candidate && candidate->data(readyRole).toBool()) candidate = nullptr;
    for (int i = 0; !candidate && i < m_frames->count(); ++i) {
        auto *item = m_frames->item(i);
        if (!item->data(readyRole).toBool() && m_frames->visualItemRect(item).intersects(m_frames->viewport()->rect())) candidate = item;
    }
    if (!candidate) return;
    const auto *definition = findSymbol(m_previewProject, m_original.symbolId);
    if (!definition) return;
    m_requestedFrame = candidate->data(frameRole).toInt();
    auto snapshot = thumbnailSnapshot(m_previewProject, definition->content, m_requestedFrame);
    snapshot.context = m_context; snapshot.revision = ++m_revision;
    m_requestActive = true;
    m_renderer->request(std::move(snapshot));
}
void SymbolFramePickerDialog::apply() {
    m_frame->commitEditing();
    checkSource();
    if (!sourceUnchanged() || !m_controller->prepareTimelineEdit()) return;
    checkSource();
    if (!sourceUnchanged()) return;
    const auto reason = symbolClipEditError(m_controller->document()->activeShot(), m_layerId);
    if (!reason.isEmpty()) { updateActions(); return; }
    if (m_mode == SymbolFramePickerMode::TimedPose) {
        if (m_poseParentFrame < 0 || m_poseParentFrame >= m_controller->frameCount()
            || (!symbolPoseKeyAtParent(m_original,m_poseParentFrame)
                && (m_poseParentFrame < m_original.startFrame
                    || (m_original.durationFrames >= 0 && m_poseParentFrame >= qint64(m_original.startFrame) + m_original.durationFrames)))) { updateActions(); return; }
        QString error; const QScopedValueRollback<bool> guard(m_applying,true);
        if (setSymbolPose(m_controller->document(),m_layerId,m_poseParentFrame,m_selectedFrame,&error)) {
            m_controller->selectSymbolPoseLayer(m_layerId);
            m_controller->setSelectedKeys({{m_layerId,KeyframeProperty::PositionX,{},m_poseParentFrame,TimelineKeyKind::SymbolPose}});
            accept();
        } else showStatus(error);
        return;
    }
    auto instance = m_original;
    instance.firstFrame = m_selectedFrame;
    if (instance.playback != SymbolPlayback::Poses) instance.playbackOffset = 0;
    if (m_hold->isChecked()) instance.playback = SymbolPlayback::SingleFrame;
    else if (instance.playback == SymbolPlayback::SingleFrame) instance.playback = SymbolPlayback::Loop;
    QString error;
    const QScopedValueRollback<bool> guard(m_applying, true);
    if (setSymbolInstance(m_controller->document(), m_layerId, instance, &error)) accept();
    else showStatus(error);
}
void SymbolFramePickerDialog::showStatus(const QString &message) {
    m_status->setText(message);
    m_status->setVisible(!message.isEmpty());
}
void openSymbolFramePicker(TimelineController *controller, const QString &layerId, QWidget *parent, SymbolFramePickerMode mode, int poseParentFrame) {
    if (!controller || !controller->prepareTimelineEdit()) return;
    QPointer<TimelineController> owner(controller);
    QPointer<QWidget> origin(parent);
    bool returnFocus = false;
    {
        SymbolFramePickerDialog dialog(controller, layerId, parent, mode, poseParentFrame);
        dialog.exec();
        returnFocus = dialog.closedWithFocus();
    }
    if (!owner) return;
    auto *target = owner->canvas()->isVisible() ? static_cast<QWidget *>(owner->canvas()) : origin.data();
    if (!target || !target->isVisible()) return;
    // Some Qt platforms leave no active QWidget after destroying a modal.
    // Restore only focus owned by this dialog, and never activate another OS
    // window or override a different window chosen while it was closing.
    if (returnFocus && !QApplication::activeWindow()) QApplication::setActiveWindow(target->window());
    if (target->window()->isActiveWindow()) target->setFocus(Qt::OtherFocusReason);
}
} // namespace toon
