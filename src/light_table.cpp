#include "light_table.h"
#include "symbols.h"
#include "keyframe_render.h"
#include "canvas.h"
#include "effect_render.h"
#include "icons.h"
#include "property_row.h"
#include "reference_panel.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineF>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QTabletEvent>
#include <QThreadPool>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <QtMath>
#include <exception>
#include <algorithm>
#include <cmath>

namespace toon {
namespace {
constexpr int maxPins = maxLightTablePins;
constexpr qint64 maxPinBytes = maxLightTablePinBytes;
constexpr int previewExtent = maxLightTablePinDimension;
QThreadPool &snapshotPool() {
    static QThreadPool pool;
    static const bool configured = [] { pool.setMaxThreadCount(1); return true; }();
    Q_UNUSED(configured);
    return pool;
}
QString pinName(QString name) {
    name = name.simplified();
    for (qsizetype index = name.size(); index > 0; --index) {
        const auto ch = name[index - 1];
        if (ch.category() == QChar::Other_Control || ch == QChar::LineSeparator || ch == QChar::ParagraphSeparator)
            name.remove(index - 1, 1);
    }
    return name.isEmpty() ? QString("Image") : name.left(2048);
}
struct SnapshotResult { QImage image; QRectF bounds; QString error; };
SnapshotResult renderSnapshot(QVector<RasterLayer> layers, QRectF bounds, QSize frameSize,
                              const std::shared_ptr<std::atomic_bool> &cancelled) {
    if (cancelled->load(std::memory_order_relaxed)) return {};
    EffectRenderCancellationScope cancellation(cancelled.get());
    const qreal ratio = qMin(1., previewExtent / qMax(bounds.width(), bounds.height()));
    const QSize size(qMax(1, qCeil(bounds.width() * ratio)), qMax(1, qCeil(bounds.height() * ratio)));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {{}, {}, "Could not allocate the light-table preview."};
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(size.width() / bounds.width(), size.height() / bounds.height());
        painter.translate(-bounds.topLeft());
        paintLayers(painter, layers, true, nullptr, true, frameSize);
    }
    if (cancelled->load(std::memory_order_relaxed)) return {};
    if (!effectRenderError().isEmpty()) return {{}, {}, effectRenderError()};
    int left = size.width(), top = size.height(), right = -1, bottom = -1;
    for (int y = 0; y < size.height(); ++y) {
        if (cancelled->load(std::memory_order_relaxed)) return {};
        const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < size.width(); ++x) if (qAlpha(row[x])) {
            left = qMin(left, x); right = qMax(right, x); top = qMin(top, y); bottom = y;
        }
    }
    if (right < left) return {{}, {}, "This drawing has no visible pixels to pin."};
    const QRect crop(QPoint(left, top), QPoint(right, bottom));
    const QRectF cropped(bounds.x() + crop.x() * bounds.width() / size.width(),
                         bounds.y() + crop.y() * bounds.height() / size.height(),
                         crop.width() * bounds.width() / size.width(),
                         crop.height() * bounds.height() / size.height());
    return {image.copy(crop), cropped, {}};
}
QString drawingName(const QVector<RasterLayer> &layers, int index) {
    if (index < 0 || index >= layers.size()) return {};
    QStringList names{layers[index].name}; auto parent = layers[index].parentId;
    for (int depth = 0; depth < 8 && !parent.isEmpty(); ++depth) {
        bool found = false;
        for (const auto &layer : layers) if (layer.id == parent) {
            names.prepend(layer.name); parent = layer.parentId; found = true; break;
        }
        if (!found) break;
    }
    return names.join(" › ");
}
QIcon pinThumbnail(const QImage &image) {
    QIcon icon;
    for (qreal dpr : {1., 2., 3.}) {
        QPixmap pixmap(QSize(24, 24) * dpr); pixmap.setDevicePixelRatio(dpr); pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap); painter.setRenderHint(QPainter::SmoothPixmapTransform);
        // Transparent ink needs a neutral backing to remain legible on dark UI.
        for (int y = 0; y < 24; y += 4) for (int x = 0; x < 24; x += 4)
            painter.fillRect(QRect(x, y, 4, 4), ((x + y) / 4) % 2 ? QColor("#d0d0d0") : QColor("#eeeeee"));
        const QSizeF fit = image.size().scaled(QSize(24, 24), Qt::KeepAspectRatio);
        painter.drawImage(QRectF(QPointF((24 - fit.width()) / 2, (24 - fit.height()) / 2), fit), image);
        painter.end(); icon.addPixmap(pixmap);
    }
    return icon;
}
}

QTransform LightTablePin::transform() const {
    QTransform transform;
    const auto center = bounds.center();
    transform.translate(center.x() + offset.x(), center.y() + offset.y());
    transform.rotate(rotation); transform.scale(scale, scale);
    transform.translate(-center.x(), -center.y());
    return transform;
}

class LightTableOverlay final : public QWidget {
public:
    LightTableOverlay(LightTablePanel *panel, Canvas *canvas) : QWidget(canvas), m_panel(panel) {
        setObjectName("lightTableOverlay");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        setGeometry(canvas->rect());
        show();
    }
    void invalidate() {
        m_cacheDirty = true;
        if (m_panel->m_pins.isEmpty() || !m_panel->m_visible) m_cache = {};
        update();
    }
    void checkView() {
        if (m_panel->artworkToViewport() != m_view) invalidate();
    }
    quint64 buildCount() const { return m_buildCount; }
protected:
    void paintEvent(QPaintEvent *) override {
        if (!m_panel->m_canvas || !m_panel->m_canvas->hasProject() || !m_panel->m_visible || m_panel->m_pins.isEmpty()) return;
        QPainter painter(this); painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setRenderHint(QPainter::Antialiasing);
        // Pen redraws reuse this viewport composite. Only a pin/view change
        // resamples snapshots; the 16 MP budget also bounds Retina allocations.
        const qreal pixels = qMax(1., qreal(width()) * height());
        const qreal ratio = qMin(devicePixelRatioF(), std::sqrt(16000000. / pixels));
        const QSize cacheSize(qMax(1, qCeil(width() * ratio)), qMax(1, qCeil(height() * ratio)));
        if (m_cacheDirty || m_cache.size() != cacheSize || m_cache.devicePixelRatioF() != ratio) {
            if (m_cache.size() != cacheSize) m_cache = QPixmap(cacheSize);
            m_cache.setDevicePixelRatio(ratio); m_cache.fill(Qt::transparent);
            QPainter composite(&m_cache); composite.setRenderHint(QPainter::SmoothPixmapTransform);
            const auto view = m_panel->artworkToViewport(); m_view = view;
            ++m_buildCount;
            for (int index = 0; index < m_panel->m_pins.size(); ++index) {
                const auto &pin = m_panel->m_pins[index];
                if (!pin.visible || (m_panel->m_solo && index != m_panel->m_selected)) continue;
                composite.save(); composite.setTransform(pin.transform() * view); composite.setOpacity(pin.opacity);
                composite.drawImage(pin.bounds, pin.image); composite.restore();
            }
            m_cacheDirty = false;
        }
        painter.drawPixmap(QPointF(), m_cache);
        if (!m_panel->m_editing || m_panel->m_selected < 0) return;
        const auto &pin = m_panel->m_pins[m_panel->m_selected];
        if (!pin.visible) return;
        const auto polygon = m_panel->selectedViewportPolygon();
        if (polygon.isEmpty()) return;
        QPen border(QColor(ui::focus), 1, Qt::DashLine); border.setCosmetic(true);
        painter.setPen(border); painter.setBrush(Qt::NoBrush); painter.drawPolygon(polygon);
        painter.setPen(QPen(QColor(ui::focus), 1)); painter.setBrush(QColor(ui::base));
        for (int i = 0; i < 4; ++i) painter.drawRect(QRectF(polygon[i] - QPointF(4, 4), QSizeF(8, 8)));
        const auto handle = m_panel->rotationHandle();
        painter.drawLine((polygon[0] + polygon[1]) / 2, handle);
        painter.drawEllipse(handle, 5, 5);
    }
private:
    LightTablePanel *m_panel;
    QPixmap m_cache;
    bool m_cacheDirty = true;
    QTransform m_view;
    quint64 m_buildCount = 0;
};

LightTablePanel::LightTablePanel(Canvas *canvas, QWidget *parent)
    : QWidget(parent), m_canvas(canvas), m_overlay(new LightTableOverlay(this, canvas)) {
    setObjectName("lightTablePanel"); setMinimumWidth(212); setProperty("compactDrawingPanel", true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4); layout->setSpacing(4);
    const auto button = [this](const char *name, const char *icon, const QString &label, bool checkable = false) {
        auto *result = new QToolButton(this); result->setObjectName(name); result->setIcon(appIcon(icon));
        result->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon});
        result->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
        result->setToolTip(label); result->setAccessibleName(label); result->setCheckable(checkable);
        result->setProperty("compactDrawingAction", true); return result;
    };
    auto *addRow = new QHBoxLayout; addRow->setSpacing(4);
    m_pinCurrent = button("lightTablePinCurrent", "pin", "Pin current layer or drawing as a snapshot");
    m_pick = button("lightTablePickDrawing", "layers", "Choose a drawing from any shot");
    m_import = button("lightTableImport", "image", "Pin image file");
    m_remove = button("lightTableRemove", "trash", "Remove selected pin (source stays unchanged)");
    addRow->addWidget(m_pinCurrent); addRow->addWidget(m_pick); addRow->addWidget(m_import);
    addRow->addStretch(); addRow->addWidget(m_remove); layout->addLayout(addRow);
    m_list = new QListWidget(this); m_list->setObjectName("lightTablePins");
    m_list->setAccessibleName("Pinned drawing snapshots"); m_list->setIconSize({24, 24});
    m_list->setMinimumHeight(70); m_list->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); layout->addWidget(m_list, 1);
    auto *viewRow = new QHBoxLayout; viewRow->setSpacing(4);
    m_edit = button("lightTableEdit", "move", "Move, rotate or scale the selected pin; Escape returns to drawing", true);
    m_show = button("lightTableShow", "eye", "Show light-table overlays", true); m_show->setChecked(true);
    m_soloButton = button("lightTableSolo", "focus", "Show only the selected pin while flipping", true);
    m_previous = button("lightTablePrevious", "chevron-left", "Previous pinned drawing");
    m_next = button("lightTableNext", "chevron-right", "Next pinned drawing");
    m_refresh = button("lightTableRefresh", "repeat", "Refresh snapshot from its source drawing");
    m_reset = button("lightTableReset", "rotate-ccw", "Reset selected pin position, size and angle");
    viewRow->addWidget(m_edit); viewRow->addWidget(m_show); viewRow->addWidget(m_soloButton);
    viewRow->addWidget(m_previous); viewRow->addWidget(m_next); viewRow->addStretch(); layout->addLayout(viewRow);
    auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); scroll->setMinimumHeight(80);
    auto *properties = new QWidget(scroll); auto *propertyLayout = new QVBoxLayout(properties);
    propertyLayout->setContentsMargins(0, 0, 0, 0); propertyLayout->setSpacing(4);
    const auto value = [&](const QString &name, const QString &label, double low, double high, double initial, int decimals) {
        auto *control = new ValueSlider(label, low, high, initial, decimals, {}, properties);
        control->setObjectName(name); propertyLayout->addWidget(new PropertyRow(label, control, properties)); return control;
    };
    m_opacity = value("lightTableOpacity", "Opacity", 0, 100, 45, 0);
    m_x = value("lightTableX", "X", -100000, 100000, 0, 1); m_x->setSliderRange(-2000, 2000);
    m_y = value("lightTableY", "Y", -100000, 100000, 0, 1); m_y->setSliderRange(-2000, 2000);
    m_scale = value("lightTableScale", "Scale", 1, 2000, 100, 1); m_scale->setSliderRange(10, 400);
    m_rotation = value("lightTableRotation", "Angle", -180, 180, 0, 1);
    propertyLayout->addStretch(); scroll->setWidget(properties); layout->addWidget(scroll, 1);
    auto *bottom = new QHBoxLayout; bottom->setSpacing(4);
    m_status = new QLabel("Pin a drawing to trace", this); m_status->setObjectName("lightTableStatus");
    m_status->setWordWrap(true); m_status->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bottom->addWidget(m_status, 1); bottom->addWidget(m_refresh); bottom->addWidget(m_reset); layout->addLayout(bottom);
    connect(m_pinCurrent, &QToolButton::clicked, this, [this] { QString error; if (!pinCurrentLayer(&error)) showError(error); });
    connect(m_pick, &QToolButton::clicked, this, &LightTablePanel::chooseDrawing);
    connect(m_import, &QToolButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, "Pin reference image", {}, "Images (*.png *.jpg *.jpeg *.bmp)");
        if (!path.isEmpty()) { QString error; if (!importImage(path, &error)) showError(error); }
    });
    connect(m_remove, &QToolButton::clicked, this, &LightTablePanel::removeSelected);
    connect(m_edit, &QToolButton::toggled, this, &LightTablePanel::setEditing);
    connect(m_show, &QToolButton::toggled, this, &LightTablePanel::setOverlaysVisible);
    connect(m_soloButton, &QToolButton::toggled, this, &LightTablePanel::setSolo);
    connect(m_previous, &QToolButton::clicked, this, [this] { flip(-1); });
    connect(m_next, &QToolButton::clicked, this, [this] { flip(1); });
    connect(m_reset, &QToolButton::clicked, this, &LightTablePanel::resetSelected);
    connect(m_refresh, &QToolButton::clicked, this, [this] { QString error; if (!refreshSelected(&error)) showError(error); });
    connect(m_list, &QListWidget::currentRowChanged, this, &LightTablePanel::selectPin);
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (!m_sync) setPinVisible(m_list->row(item), item->checkState() == Qt::Checked);
    });
    const auto transformEdited = [this] {
        if (!m_sync) setPinTransform(m_selected, {m_x->value(), m_y->value()}, m_scale->value() / 100, m_rotation->value());
    };
    for (auto *control : {m_x, m_y, m_scale, m_rotation}) connect(control, &ValueSlider::valueChanged, this, transformEdited);
    connect(m_opacity, &ValueSlider::valueChanged, this, [this](double opacity) {
        if (!m_sync) setPinOpacity(m_selected, opacity / 100);
    });
    connect(this, &LightTablePanel::pinsChanged, this, [this] { publishState(); });
    for (auto *control : {m_opacity, m_x, m_y, m_scale, m_rotation}) {
        connect(control, &ValueSlider::editingFinished, this, [this] { publishState(); });
        connect(control, &ValueSlider::editingCancelled, this, [this] { publishState(); });
    }
    canvas->installEventFilter(this);
    connect(canvas, &Canvas::viewChanged, this, &LightTablePanel::updateOverlay);
    connect(canvas, &Canvas::cameraViewChanged, this, &LightTablePanel::updateOverlay);
    connect(canvas, &Canvas::frameViewChanged, this, &LightTablePanel::updateOverlay);
    connect(canvas, &Canvas::toolChanged, this, [this] { setEditing(false); });
    connect(canvas->document(), &Document::loaded, this, [this] { restoreFromProject(true); });
    connect(canvas->document(), &Document::drawingAidsChanged, this, [this] { restoreFromProject(); });
    connect(canvas->document(), &Document::shotChanged, this, [this] { setEditing(false); updateOverlay(); });
    connect(canvas->document(), &Document::frameChanged, this, [this] { setEditing(false); updateOverlay(); });
    connect(canvas->document(), &Document::changed, this, [this] {
        if (!m_canvas || m_canvas->document()->loading() || m_canvas->document()->strokeActive()) return;
        syncControls(); if (m_overlay) m_overlay->checkView(); publishState();
    });
    restoreFromProject(true);
}
LightTablePanel::~LightTablePanel() {
    if (m_snapshotCancelled) m_snapshotCancelled->store(true, std::memory_order_relaxed);
    if (m_canvas) { m_canvas->removeEventFilter(this); if (m_editing) m_canvas->refreshLayerSelectionContext(); }
    delete m_overlay.data();
}
LightTableState LightTablePanel::currentState() const {
    LightTableState state;
    state.pins = m_pins; state.solo = m_solo; state.visible = m_visible;
    if (m_selected >= 0 && m_selected < m_pins.size()) state.selectedPinId = m_pins[m_selected].id;
    return state;
}
void LightTablePanel::restoreFromProject(bool reset) {
    if (m_publishing || !m_canvas) return;
    if (!m_canvas->hasProject()) { syncControls(); return; }
    const auto state = m_canvas->project().drawingAids.lightTable;
    // Changes in another drawing aid must not overwrite a pin/number preview.
    if (!reset && lightTableStateEqual(state, m_persistedState)) return;
    QScopedValueRollback<bool> restoring(m_restoring, true);
    if (m_snapshotCancelled) m_snapshotCancelled->store(true, std::memory_order_relaxed);
    ++m_generation; m_busy = false; emit busyChanged(false);
    for (auto *control : {m_opacity, m_x, m_y, m_scale, m_rotation}) control->cancelEditing();
    setEditing(false); m_pins = state.pins; m_solo = state.solo; m_visible = state.visible; m_selected = -1;
    for (int index = 0; index < m_pins.size(); ++index)
        if (m_pins[index].id == state.selectedPinId) { m_selected = index; break; }
    m_persistedState = state;
    m_status->setText(m_pins.isEmpty() ? "Pin a drawing to trace" : "Snapshot · saved with project");
    rebuildList();
}
bool LightTablePanel::publishState() {
    if (m_restoring || m_publishing || !m_canvas || !m_canvas->hasProject() || m_drag != Drag::None) return true;
    if (m_canvas->document()->loading()) return true;
    for (auto *control : {m_opacity, m_x, m_y, m_scale, m_rotation}) if (control->editingActive()) return true;
    const auto state = currentState();
    if (lightTableStateEqual(state, m_persistedState)) return true;
    auto aids = m_canvas->project().drawingAids; aids.lightTable = state;
    QScopedValueRollback<bool> publishing(m_publishing, true);
    QString error;
    if (!m_canvas->document()->setDrawingAids(aids, &error)) { showError(error); return false; }
    m_persistedState = state;
    return true;
}
void LightTablePanel::commitValueEdits() {
    for (auto *control : {m_opacity, m_x, m_y, m_scale, m_rotation}) control->commitEditing();
}
bool LightTablePanel::finishPersistence() {
    if (!m_canvas || !m_canvas->hasProject()) return true;
    if (m_busy) { showError("Wait for the light-table snapshot to finish, then save again."); return false; }
    commitValueEdits();
    // A Save invoked during reference manipulation keeps the latest visible pose.
    m_drag = Drag::None; m_tabletContact = false;
    if (m_editing) m_canvas->setCursor(Qt::OpenHandCursor);
    return publishState();
}
bool LightTablePanel::interactionReady() const {
    return m_canvas && m_canvas->hasProject() && !m_canvas->document()->editingGestureActive()
        && !m_canvas->document()->layerPropertyEditActive() && !m_canvas->animationPlayback();
}
quint64 LightTablePanel::overlayBuildCount() const { return m_overlay ? m_overlay->buildCount() : 0; }
void LightTablePanel::showError(const QString &message) {
    if (message.isEmpty()) return;
    m_status->setText(message); emit warning(message);
}
QTransform LightTablePanel::artworkToViewport() const {
    if (!m_canvas || !m_canvas->hasProject()) return {};
    auto view = m_canvas->viewTransform();
    const auto origin = sheetGeometry(m_canvas->project(), m_canvas->shot()).output.topLeft();
    view.translate(origin.x(), origin.y()); return m_canvas->cameraTransform() * view;
}
QPolygonF LightTablePanel::selectedViewportPolygon() const {
    if (m_selected < 0 || m_selected >= m_pins.size()) return {};
    const auto &pin = m_pins[m_selected];
    return (pin.transform() * artworkToViewport()).map(QPolygonF(pin.bounds));
}
QPointF LightTablePanel::rotationHandle() const {
    const auto polygon = selectedViewportPolygon(); if (polygon.size() < 4) return {};
    const auto midpoint = (polygon[0] + polygon[1]) / 2;
    auto direction = midpoint - (polygon[2] + polygon[3]) / 2;
    const auto length = std::hypot(direction.x(), direction.y());
    return midpoint + (length > .001 ? direction * (24 / length) : QPointF(0, -24));
}
void LightTablePanel::updateOverlay() {
    if (!m_canvas || !m_overlay) return;
    m_overlay->setGeometry(m_canvas->rect()); m_overlay->raise(); m_overlay->invalidate();
}
void LightTablePanel::syncControls() {
    m_sync = true;
    const bool selected = m_selected >= 0 && m_selected < m_pins.size();
    m_edit->setChecked(m_editing); m_show->setChecked(m_visible); m_soloButton->setChecked(m_solo);
    const bool ready = interactionReady();
    m_pinCurrent->setEnabled(ready && !m_busy && m_pins.size() < maxPins);
    m_pick->setEnabled(ready && !m_busy && m_pins.size() < maxPins);
    m_import->setEnabled(ready && !m_busy && m_pins.size() < maxPins);
    m_edit->setEnabled(selected && ready && m_visible && m_pins[m_selected].visible);
    m_remove->setEnabled(selected); m_reset->setEnabled(selected);
    m_refresh->setEnabled(selected && ready && !m_busy && !m_pins[m_selected].layerId.isEmpty());
    m_previous->setEnabled(m_pins.size() > 1); m_next->setEnabled(m_pins.size() > 1);
    for (auto *control : {m_opacity, m_x, m_y, m_scale, m_rotation}) control->setEnabled(selected);
    if (selected) {
        const auto &pin = m_pins[m_selected];
        m_opacity->setValueSilently(pin.opacity * 100); m_x->setValueSilently(pin.offset.x());
        m_y->setValueSilently(pin.offset.y()); m_scale->setValueSilently(pin.scale * 100);
        m_rotation->setValueSilently(pin.rotation);
    }
    m_sync = false;
}
void LightTablePanel::rebuildList() {
    m_sync = true; const QSignalBlocker blocker(m_list); m_list->clear();
    for (const auto &pin : m_pins) {
        auto *item = new QListWidgetItem(pinThumbnail(pin.image), pin.name, m_list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable); item->setCheckState(pin.visible ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(pin.name + "\nSnapshot · source unchanged · saved with project");
        item->setSizeHint({0, qMax(32, fontMetrics().height() + 8)});
    }
    m_list->setCurrentRow(m_selected); m_sync = false; syncControls(); updateOverlay(); emit pinsChanged();
}
bool LightTablePanel::addPin(LightTablePin pin, QString *error) {
    const auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (pin.image.isNull() || !pin.bounds.isValid()) return fail("The pinned image is empty.");
    if (m_pins.size() >= maxPins) return fail("A light table can hold up to eight snapshots. Remove a pin first.");
    qint64 bytes = pin.image.sizeInBytes(); for (const auto &existing : m_pins) bytes += existing.image.sizeInBytes();
    if (bytes > maxPinBytes) return fail("The light table preview budget is full. Remove a pin first.");
    commitValueEdits();
    cancelDrag();
    pin.id = QUuid::createUuid().toString(QUuid::WithoutBraces); pin.name = pinName(pin.name); m_pins.append(std::move(pin));
    m_selected = m_pins.size() - 1; m_status->setText("Snapshot · saved with project"); rebuildList(); return true;
}
bool LightTablePanel::pinCurrentLayer(QString *error) {
    if (!m_canvas || !m_canvas->hasProject()) { if (error) *error = "Open a project first."; return false; }
    const auto &document = *m_canvas->document();
    if (document.activeLayer() < 0 || document.activeLayer() >= document.activeShot().layers.size()) return false;
    return pinLayer(document.activeShotId(), document.activeShot().layers[document.activeLayer()].id, error);
}
bool LightTablePanel::pinLayer(const QString &shotId, const QString &layerId, QString *error) {
    return requestSnapshot(shotId, layerId, {}, error);
}
bool LightTablePanel::requestSnapshot(const QString &shotId, const QString &layerId, const QString &replaceId, QString *error) {
    const auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (!interactionReady()) return fail("Finish the current drawing or transform before pinning a snapshot.");
    if (m_busy) return fail("A snapshot is still being prepared.");
    if (replaceId.isEmpty() && m_pins.size() >= maxPins) return fail("A light table can hold up to eight snapshots. Remove a pin first.");
    const auto &project = m_canvas->project();
    const Shot *sourceShot = nullptr;
    for (const auto &shot : project.shots) if (shot.id == shotId) { sourceShot = &shot; break; }
    if (!sourceShot) return fail("The source shot is no longer available.");
    int sourceIndex = -1;
    for (int index = 0; index < sourceShot->layers.size(); ++index) if (sourceShot->layers[index].id == layerId) { sourceIndex = index; break; }
    if (sourceIndex < 0) return fail("The source drawing is no longer available.");
    const auto &source = sourceShot->layers[sourceIndex];
    if (source.kind == LayerKind::Scene3D || source.adjustment) return fail("Choose a drawing layer or drawing folder to pin.");
    QVector<RasterLayer> layers;
    for (int index = 0; index < sourceShot->layers.size(); ++index) {
        if (index != sourceIndex && (!source.group || !layerIsDescendant(sourceShot->layers, index, layerId))) continue;
        auto layer = sourceShot->layers[index];
        if (layer.kind == LayerKind::Scene3D) return fail("Choose a drawing without 3D reference layers.");
        if (index == sourceIndex) { layer.parentId.clear(); layer.visible = true; layer.clipped = false; }
        layers.append(std::move(layer));
    }
    const QRectF bounds(-sourceShot->sheet.overscan, -sourceShot->sheet.overscan,
                        project.width + 2 * sourceShot->sheet.overscan, project.height + 2 * sourceShot->sheet.overscan);
    const QString name = sourceShot->number + " · " + drawingName(sourceShot->layers, sourceIndex);
    const quint64 generation = m_generation;
    const auto cancelled = std::make_shared<std::atomic_bool>(false);
    m_snapshotCancelled = cancelled;
    m_busy = true; m_status->setText("Preparing snapshot…"); syncControls(); emit busyChanged(true);
    auto *watcher = new QFutureWatcher<SnapshotResult>(this);
    connect(watcher, &QFutureWatcher<SnapshotResult>::finished, this, [this, watcher, generation, shotId, layerId, name, replaceId] {
        const auto result = watcher->result(); watcher->deleteLater();
        if (generation != m_generation) return;
        m_busy = false; emit busyChanged(false);
        if (!result.error.isEmpty()) { showError(result.error); syncControls(); return; }
        if (!replaceId.isEmpty()) {
            if (m_drag != Drag::None && m_selected >= 0 && m_pins[m_selected].id == replaceId) cancelDrag();
            for (auto &pin : m_pins) if (pin.id == replaceId) {
                qint64 bytes = result.image.sizeInBytes();
                for (const auto &existing : m_pins) if (existing.id != replaceId) bytes += existing.image.sizeInBytes();
                if (bytes > maxPinBytes) { showError("The light table preview budget is full."); syncControls(); return; }
                pin.image = result.image; pin.bounds = result.bounds; pin.name = pinName(name);
                m_status->setText("Snapshot refreshed"); rebuildList(); return;
            }
            syncControls(); return;
        }
        LightTablePin pin; pin.name = name; pin.shotId = shotId; pin.layerId = layerId;
        pin.image = result.image; pin.bounds = result.bounds;
        QString error; if (!addPin(std::move(pin), &error)) { showError(error); syncControls(); }
    });
    Project symbolSource;symbolSource.width=project.width;symbolSource.height=project.height;
    symbolSource.symbols=project.symbols;
    const int sourceFrame=shotId==m_canvas->document()->activeShotId()?m_canvas->document()->currentFrame():0;
    const Shot shotSnapshot=*sourceShot;
    watcher->setFuture(QtConcurrent::run(&snapshotPool(), [layers = std::move(layers), symbolSource, shotSnapshot, sourceFrame, bounds, size = QSize(project.width, project.height), cancelled]() mutable {
        try {
            QString error;
            const bool containsSymbol=std::any_of(layers.cbegin(),layers.cend(),[](const RasterLayer &layer){return layer.symbol.has_value();});
            if(containsSymbol){
                if(!applyLayerAnimations(shotSnapshot,sourceFrame,layers,bounds,&error))return SnapshotResult{{},{},error};
                for(auto &layer:layers)if(layer.parentId.isEmpty())layer.renderTransform=worldLayerTransform(shotSnapshot,layer.id,sourceFrame);
            }
            if(!resolveSymbolLayers(symbolSource,shotSnapshot,sourceFrame,layers,bounds,&error))return SnapshotResult{{},{},error};
            return renderSnapshot(layers, bounds, size, cancelled);
        }
        catch (const std::exception &) { return SnapshotResult{{}, {}, "Could not prepare the light-table snapshot."}; }
    }));
    return true;
}
bool LightTablePanel::pinImage(const QImage &image, const QString &name, QString *error) {
    if (!interactionReady()) { if (error) *error = "Finish the current drawing or transform before pinning an image."; return false; }
    if (image.isNull() || image.width() > 16384 || image.height() > 16384 || qint64(image.width()) * image.height() > 16000000) {
        if (error) *error = "Choose an image up to 16 million pixels and 16,384 pixels per side."; return false;
    }
    LightTablePin pin; pin.name = name.trimmed().isEmpty() ? "Image" : name.trimmed();
    pin.image = image.size().width() > previewExtent || image.size().height() > previewExtent
        ? image.scaled(previewExtent, previewExtent, Qt::KeepAspectRatio, Qt::SmoothTransformation) : image;
    pin.image = pin.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    pin.bounds = QRectF(QPointF(), image.size());
    return addPin(std::move(pin), error);
}
bool LightTablePanel::importImage(const QString &path, QString *error) {
    QImage image; QString name;
    if (!readReferenceImage(path, &image, &name, error)) return false;
    return pinImage(image, name, error);
}
bool LightTablePanel::refreshSelected(QString *error) {
    if (m_selected < 0 || m_selected >= m_pins.size()) return false;
    const auto &pin = m_pins[m_selected];
    if (pin.layerId.isEmpty()) { if (error) *error = "Imported images are snapshots; import again to refresh them."; return false; }
    return requestSnapshot(pin.shotId, pin.layerId, pin.id, error);
}
void LightTablePanel::chooseDrawing() {
    if (!interactionReady()) return;
    QDialog dialog(this); dialog.setWindowTitle("Pin drawing snapshot"); dialog.setMinimumWidth(300);
    auto *layout = new QVBoxLayout(&dialog);
    auto *shots = new QComboBox(&dialog); shots->setAccessibleName("Source shot");
    auto *drawings = new QComboBox(&dialog); drawings->setAccessibleName("Drawing or layer");
    drawings->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); drawings->setMinimumContentsLength(12);
    for (const auto &shot : m_canvas->project().shots) shots->addItem(shot.number + " · " + shot.name, shot.id);
    shots->setCurrentIndex(m_canvas->document()->activeShotIndex());
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    const auto refresh = [&] {
        drawings->clear();
        if (!m_canvas || !m_canvas->hasProject()) return;
        for (const auto &shot : m_canvas->project().shots) if (shot.id == shots->currentData().toString()) {
            for (int index = 0; index < shot.layers.size(); ++index) {
                const auto &layer = shot.layers[index];
                if (layer.kind != LayerKind::Scene3D && !layer.adjustment)
                    drawings->addItem(appIcon(layer.group ? "folder-open" : "image"), drawingName(shot.layers, index), layer.id);
            }
        }
    };
    connect(shots, &QComboBox::currentIndexChanged, &dialog, refresh); refresh();
    layout->addWidget(new PropertyRow("Shot", shots, &dialog)); layout->addWidget(new PropertyRow("Drawing", drawings, &dialog));
    layout->addWidget(buttons); connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        QString error; if (!pinLayer(shots->currentData().toString(), drawings->currentData().toString(), &error)) showError(error);
    }
}
void LightTablePanel::setEditing(bool enabled) {
    if (m_sync) return;
    enabled = enabled && isVisible() && interactionReady() && m_selected >= 0 && m_selected < m_pins.size()
        && m_visible && m_pins[m_selected].visible;
    if (m_editing != enabled) {
        cancelDrag(); m_space = false; m_navigating = false; m_editing = enabled;
        if (m_canvas) {
            if (enabled) { m_canvas->setFocus(Qt::OtherFocusReason); m_canvas->setCursor(Qt::OpenHandCursor); }
            else m_canvas->refreshLayerSelectionContext();
        }
        emit editingChanged(enabled);
    }
    syncControls(); updateOverlay();
}
void LightTablePanel::setSolo(bool enabled) { m_solo = enabled; syncControls(); updateOverlay(); emit pinsChanged(); }
void LightTablePanel::setOverlaysVisible(bool visible) {
    m_visible = visible; if (!visible) setEditing(false); syncControls(); updateOverlay(); emit pinsChanged();
}
void LightTablePanel::selectPin(int index) {
    if (m_sync) return;
    commitValueEdits();
    cancelDrag(); m_selected = index >= 0 && index < m_pins.size() ? index : -1;
    if (m_selected < 0 || !m_pins[m_selected].visible) setEditing(false);
    if (m_list->currentRow() != m_selected) { const QSignalBlocker blocker(m_list); m_list->setCurrentRow(m_selected); }
    syncControls(); updateOverlay(); emit pinsChanged();
}
void LightTablePanel::flip(int direction) {
    if (m_pins.isEmpty()) return;
    setSolo(true); selectPin((m_selected + (direction >= 0 ? 1 : m_pins.size() - 1)) % m_pins.size());
}
void LightTablePanel::removeSelected() {
    if (m_selected < 0 || m_selected >= m_pins.size()) return;
    commitValueEdits();
    cancelDrag(); m_pins.removeAt(m_selected); m_selected = qMin(m_selected, int(m_pins.size()) - 1);
    if (m_selected < 0) setEditing(false); rebuildList();
}
void LightTablePanel::resetSelected() {
    if (m_selected < 0 || m_selected >= m_pins.size()) return;
    cancelDrag(); setPinTransform(m_selected, {}, 1, 0);
}
void LightTablePanel::clearPins() {
    if (m_snapshotCancelled) m_snapshotCancelled->store(true, std::memory_order_relaxed);
    ++m_generation; m_busy = false; emit busyChanged(false);
    setEditing(false); m_pins.clear(); m_selected = -1; m_status->setText("Pin a drawing to trace"); rebuildList();
}
bool LightTablePanel::setPinTransform(int index, QPointF offset, qreal scale, qreal rotation) {
    if (index < 0 || index >= m_pins.size() || !std::isfinite(offset.x()) || !std::isfinite(offset.y())
        || !std::isfinite(scale) || !std::isfinite(rotation) || std::abs(offset.x()) > 100000
        || std::abs(offset.y()) > 100000 || scale < .01 || scale > 20) return false;
    auto &pin = m_pins[index]; pin.offset = offset; pin.scale = scale; pin.rotation = std::remainder(rotation, 360.);
    syncControls(); updateOverlay(); emit pinsChanged(); return true;
}
bool LightTablePanel::setPinOpacity(int index, qreal opacity) {
    if (index < 0 || index >= m_pins.size() || !std::isfinite(opacity)) return false;
    m_pins[index].opacity = qBound(0., opacity, 1.); syncControls(); updateOverlay(); emit pinsChanged(); return true;
}
void LightTablePanel::setPinVisible(int index, bool visible) {
    if (index < 0 || index >= m_pins.size()) return;
    m_pins[index].visible = visible; if (index == m_selected && !visible) setEditing(false);
    if (auto *item = m_list->item(index)) { const QSignalBlocker blocker(m_list); item->setCheckState(visible ? Qt::Checked : Qt::Unchecked); }
    syncControls(); updateOverlay(); emit pinsChanged();
}
bool LightTablePanel::pointerPress(QPointF position) {
    if (!m_editing || !interactionReady() || m_selected < 0) return false;
    const auto polygon = selectedViewportPolygon(); if (polygon.size() < 4) return true;
    m_dragStart = m_pins[m_selected]; m_dragPoint = m_canvas->artworkPoint(position);
    m_dragCenter = m_dragStart.bounds.center() + m_dragStart.offset;
    if (QLineF(position, rotationHandle()).length() <= 12) m_drag = Drag::Rotate;
    else {
        for (int index = 0; index < 4; ++index) if (QLineF(position, polygon[index]).length() <= 12) m_drag = Drag::Scale;
        if (m_drag == Drag::None && polygon.containsPoint(position, Qt::OddEvenFill)) m_drag = Drag::Move;
    }
    m_canvas->setCursor(m_drag == Drag::Move ? Qt::ClosedHandCursor : Qt::CrossCursor);
    return true;
}
void LightTablePanel::pointerMove(QPointF position) {
    if (!m_editing || m_selected < 0) return;
    if (m_drag == Drag::None) { m_canvas->setCursor(Qt::OpenHandCursor); return; }
    const auto point = m_canvas->artworkPoint(position);
    auto offset = m_dragStart.offset; auto scale = m_dragStart.scale; auto rotation = m_dragStart.rotation;
    if (m_drag == Drag::Move) offset += point - m_dragPoint;
    if (m_drag == Drag::Scale) {
        const auto oldLength = QLineF(m_dragCenter, m_dragPoint).length();
        if (oldLength > .0001) scale = qBound(.01, scale * QLineF(m_dragCenter, point).length() / oldLength, 20.);
    }
    if (m_drag == Drag::Rotate) {
        const auto before = m_dragPoint - m_dragCenter, after = point - m_dragCenter;
        rotation += qRadiansToDegrees(std::atan2(after.y(), after.x()) - std::atan2(before.y(), before.x()));
    }
    setPinTransform(m_selected, offset, scale, rotation);
}
void LightTablePanel::pointerRelease(QPointF position) {
    pointerMove(position); m_drag = Drag::None; m_tabletContact = false; publishState();
    if (m_editing && m_canvas) m_canvas->setCursor(Qt::OpenHandCursor);
}
void LightTablePanel::cancelDrag() {
    if (m_drag != Drag::None && m_selected >= 0 && m_selected < m_pins.size()) m_pins[m_selected] = m_dragStart;
    m_drag = Drag::None; m_tabletContact = false;
}
void LightTablePanel::hideEvent(QHideEvent *event) {
    setEditing(false);
    QWidget::hideEvent(event);
}
bool LightTablePanel::eventFilter(QObject *watched, QEvent *event) {
    if (watched != m_canvas) return QWidget::eventFilter(watched, event);
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show || event->type() == QEvent::DevicePixelRatioChange) updateOverlay();
    if (!m_editing) return QWidget::eventFilter(watched, event);
    if (event->type() == QEvent::Hide) { setEditing(false); return false; }
    if (event->type() == QEvent::FocusOut || event->type() == QEvent::WindowDeactivate) {
        cancelDrag(); m_space = false; m_navigating = false; syncControls(); updateOverlay(); return false;
    }
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Space) {
            if (!key->isAutoRepeat()) m_space = event->type() == QEvent::KeyPress;
            if (m_drag != Drag::None) { cancelDrag(); syncControls(); updateOverlay(); }
            return false; // Canvas owns navigation until its pointer release.
        }
        if (event->type() == QEvent::KeyPress) {
            if (key->key() == Qt::Key_Escape) { setEditing(false); event->accept(); return true; }
            if (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right) {
                flip(key->key() == Qt::Key_Left ? -1 : 1); event->accept(); return true;
            }
        }
    }
    if (event->type() == QEvent::TabletPress || event->type() == QEvent::TabletMove || event->type() == QEvent::TabletRelease) {
        auto *tablet = static_cast<QTabletEvent *>(event);
        if (m_navigating || m_space) {
            if (event->type() == QEvent::TabletPress) m_navigating = true;
            if (event->type() == QEvent::TabletRelease) m_navigating = false;
            return false;
        }
        if (event->type() == QEvent::TabletPress) {
            if (tablet->button() != Qt::LeftButton) return false;
            m_tabletContact = true; pointerPress(tablet->position());
        } else if (event->type() == QEvent::TabletRelease) {
            if (m_tabletContact) pointerRelease(tablet->position());
        } else if (m_tabletContact && tablet->buttons().testFlag(Qt::LeftButton)) {
            pointerMove(tablet->position());
        } else {
            // A lost release ends at the last contact sample, never at a
            // distant hover position. Subsequent hover only updates cursor.
            m_drag = Drag::None; m_tabletContact = false; publishState(); m_canvas->setCursor(Qt::OpenHandCursor);
        }
        event->accept(); return true;
    }
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (m_tabletContact || mouse->source() != Qt::MouseEventNotSynthesized) { event->accept(); return true; }
        if (m_navigating || m_space || mouse->button() == Qt::MiddleButton || mouse->buttons().testFlag(Qt::MiddleButton)) {
            if (event->type() == QEvent::MouseButtonPress) {
                if (m_drag != Drag::None) { cancelDrag(); syncControls(); updateOverlay(); }
                m_navigating = true;
            }
            if (event->type() == QEvent::MouseButtonRelease) m_navigating = false;
            return false;
        }
        if (mouse->button() == Qt::RightButton || mouse->buttons().testFlag(Qt::RightButton)) return false;
        if (event->type() == QEvent::MouseButtonPress) {
            if (mouse->button() != Qt::LeftButton) return false;
            pointerPress(mouse->position());
        } else if (event->type() == QEvent::MouseMove) {
            if (mouse->buttons().testFlag(Qt::LeftButton)) pointerMove(mouse->position());
            else { m_drag = Drag::None; publishState(); m_canvas->setCursor(Qt::OpenHandCursor); }
        } else if (mouse->button() == Qt::LeftButton) pointerRelease(mouse->position());
        else return false;
        event->accept(); return true;
    }
    return QWidget::eventFilter(watched, event);
}
} // namespace toon
