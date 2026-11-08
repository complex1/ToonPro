#include "brush_settings_panel.h"
#include "brush_library.h"
#include "brush_engine.h"
#include "brush_widgets.h"
#include "brush_tip_set_widget.h"
#include "brush_surface_pattern_widget.h"
#include "advanced_brush_dynamics_widget.h"
#include "brush_response_widget.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include "canvas.h"
#include "camera_tool.h"
#include "keyframe_transform_tool.h"
#include "mask_tool.h"
#include "icons.h"
#include "value_slider.h"
#include "property_row.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QGridLayout>
#include <QListWidget>
#include <QPainter>
#include <QTabBar>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QScreen>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStackedLayout>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include <type_traits>
#include <limits>

namespace toon {
namespace {
class StrokeModeTabs final : public QTabBar {
public:
    StrokeModeTabs(bool compact, QWidget *parent) : QTabBar(parent), m_compact(compact) {}
    void syncCompactSize() {
        if (m_compact && count() == 2) setFixedWidth(tabSizeHint(0).width() + tabSizeHint(1).width());
    }
protected:
    QSize tabSizeHint(int index) const override {
        if (!m_compact) return QTabBar::tabSizeHint(index);
        return {qMax(64, fontMetrics().horizontalAdvance(tabText(index)) + 12),
            qMax(ui::metrics::tabHeight, fontMetrics().height() + 8)};
    }
    QSize minimumTabSizeHint(int index) const override { return tabSizeHint(index); }
    void changeEvent(QEvent *event) override {
        QTabBar::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) syncCompactSize();
    }
private:
    bool m_compact;
};

class StrokeStabilizationOptions final : public QWidget {
public:
    StrokeStabilizationOptions(Canvas *canvas, const QString &prefix, bool compact = false) {
        setObjectName(prefix + "Options");
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(ui::metrics::controlGap);
        auto *mode = new StrokeModeTabs(compact, this); mode->setObjectName(prefix + "Mode");
        mode->setIconSize({ui::metrics::tabIcon, ui::metrics::tabIcon});
        mode->setProperty("compactDrawingTabs", true); mode->setAccessibleName("Stroke stabilization mode");
        mode->setExpanding(true); mode->setElideMode(Qt::ElideNone); mode->setFocusPolicy(Qt::StrongFocus);
        mode->addTab(appIcon("spline"), "Smooth");
        mode->addTab(appIcon("hand"), "Stabilizer");
        mode->setTabToolTip(0, "Smooth freehand strokes with moderate trailing distance");
        mode->setTabToolTip(1, "Strong stabilization for controlled inking; the painted tip follows farther behind the pen");
        mode->setAccessibleTabName(0, "Smooth strokes"); mode->setAccessibleTabName(1, "Strong stroke stabilizer");
        if (compact) {
            // These filtering choices have no universally recognized pair of
            // icons. Short visible names make the selected behavior explicit.
            mode->setTabIcon(0, {}); mode->setTabIcon(1, {});
            mode->setTabText(0, "Smooth"); mode->setTabText(1, "Strong");
            mode->setUsesScrollButtons(false);
            mode->syncCompactSize();
        }
        layout->addWidget(mode);
        auto *deadZone = new ValueSlider("Stabilizer dead zone", 0, 20, canvas->stabilizationDeadZone(), 1);
        deadZone->setObjectName(prefix + "DeadZone");
        deadZone->setToolTip("Ignore small pen motions inside this radius, in logical view pixels; zero disables the dead zone");
        auto *deadRow = new PropertyRow("Dead zone", deadZone); layout->addWidget(deadRow);
        auto *finish = new QCheckBox("Finish stroke", this); finish->setObjectName(prefix + "Finish");
        finish->setAccessibleName("Finish stabilized stroke");
        finish->setToolTip("Finish the retained stroke tail on release; turn off to stop at the last stabilized tip");
        layout->addWidget(finish);
        const auto refresh = [canvas, mode, deadZone, deadRow, finish, compact, this] {
            const QSignalBlocker a(mode), b(finish);
            mode->setCurrentIndex(int(canvas->stabilizationMode()));
            deadZone->setValueSilently(canvas->stabilizationDeadZone());
            deadRow->setVisible(!compact && canvas->stabilizationMode() == StrokeFilterMode::Stabilizer);
            finish->setVisible(!compact);
            finish->setChecked(canvas->stabilizationFinishAtEndpoint());
            setEnabled(canvas->brush.smoothingEnabled);
        };
        connect(mode, &QTabBar::currentChanged, this, [canvas](int index) {
            canvas->setStabilizationMode(StrokeFilterMode(index));
            if (canvas->hasProject()) canvas->setFocus(Qt::OtherFocusReason);
        });
        connect(deadZone, &ValueSlider::valueChanged, canvas, &Canvas::setStabilizationDeadZone);
        connect(finish, &QCheckBox::toggled, canvas, &Canvas::setStabilizationFinishAtEndpoint);
        connect(canvas, &Canvas::brushChanged, this, refresh);
        connect(canvas, &Canvas::toolStateApplied, this, refresh);
        refresh();
    }
};

class BrushCategoryTabs : public QTabBar {
public:
    using QTabBar::QTabBar;
    QSize tabSizeHint(int) const override { return {ui::metrics::actionTarget, ui::metrics::tabHeight}; }
protected:
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
            const int step = event->key() == Qt::Key_Up ? -1 : 1;
            setCurrentIndex(qBound(0, currentIndex() + step, count() - 1)); event->accept();
        } else QTabBar::keyPressEvent(event);
    }
};
class BrushDetailLayout final : public QStackedLayout {
public:
    explicit BrushDetailLayout(QWidget *parent) : QStackedLayout(parent) { setSizeConstraint(QLayout::SetNoConstraint); }
    QSize minimumSize() const override {
        return currentWidget() ? currentWidget()->minimumSizeHint().expandedTo(currentWidget()->minimumSize()) : QSize();
    }
    QSize sizeHint() const override { return currentWidget() ? currentWidget()->sizeHint() : QSize(); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override {
        if (!currentWidget()) return 0;
        return currentWidget()->hasHeightForWidth() ? currentWidget()->heightForWidth(width) : currentWidget()->sizeHint().height();
    }
};

QFormLayout *detailForm(QWidget *parent) {
    auto *form = new QFormLayout(parent);
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(ui::metrics::controlGap); form->setVerticalSpacing(ui::metrics::controlGap);
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    return form;
}

// Each response family gets a direct tab rather than a long column of graphs.
// Only the active family participates in height-for-width measurement.
class BrushDetailPages final : public QWidget {
public:
    BrushDetailPages(const QString &id, const QString &name) {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(ui::metrics::controlGap);
        m_tabs = new QTabBar(this); m_tabs->setObjectName(id); m_tabs->setAccessibleName(name);
        m_tabs->setProperty("compactDrawingTabs", true); m_tabs->setExpanding(true);
        m_tabs->setElideMode(Qt::ElideRight); m_tabs->setFocusPolicy(Qt::StrongFocus);
        layout->addWidget(m_tabs);
        auto *pages = new QWidget(this); m_pages = new BrushDetailLayout(pages);
        auto policy = QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); policy.setHeightForWidth(true);
        pages->setSizePolicy(policy); setSizePolicy(policy); layout->addWidget(pages);
        connect(m_tabs, &QTabBar::currentChanged, this, [this, pages](int index) {
            QScrollArea *scroll = nullptr;
            for (auto *parent = parentWidget(); parent; parent = parent->parentWidget())
                if ((scroll = qobject_cast<QScrollArea *>(parent))) break;
            if (scroll && m_previous >= 0 && m_previous < m_positions.size())
                m_positions[m_previous] = scroll->verticalScrollBar()->value();
            m_pages->setCurrentIndex(index); m_previous = index;
            pages->updateGeometry(); updateGeometry();
            if (scroll) {
                const QPointer<QScrollArea> guarded(scroll);
                QTimer::singleShot(0, this, [this, guarded, index] {
                    if (guarded && m_tabs->currentIndex() == index)
                        guarded->verticalScrollBar()->setValue(m_positions.value(index));
                });
            }
        });
    }
    QFormLayout *addPage(const QString &name) {
        auto *page = new QWidget;
        auto *form = detailForm(page); m_positions.append(0); m_pages->addWidget(page);
        const int index = m_tabs->addTab(name);
        m_tabs->setTabToolTip(index, name + " settings"); m_tabs->setAccessibleTabName(index, name);
        return form;
    }
private:
    QTabBar *m_tabs;
    BrushDetailLayout *m_pages;
    QVector<int> m_positions;
    int m_previous = -1;
};
PropertyRow *propertyRow(QFormLayout *form, const QString &label, QWidget *control) {
    auto *row = new PropertyRow(label, control);
    row->setObjectName(control->objectName() + "Row");
    row->setLabelWidth(ui::metrics::labelWidth);
    form->addRow(row);
    return row;
}
void setPropertyVisible(QFormLayout *form, QWidget *control, bool visible) {
    // Numeric/choice editors now live in responsive rows. Hide the complete
    // label/editor row so conditional controls leave no orphan label or gap.
    auto *row = qobject_cast<PropertyRow *>(control->parentWidget());
    form->setRowVisible(row ? static_cast<QWidget *>(row) : control, visible);
}
void compactAction(QToolButton *button) {
    button->setProperty("compactDrawingAction", true);
    const int iconSize = ui::metrics::actionIcon;
    button->setIconSize({iconSize, iconSize});
    button->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
}
const QStringList categoryIcons{"brush", "circle", "sliders-horizontal", "sparkles", featureIconName(FeatureIcon::Texture), "combine", "palette", "droplet", featureIconName(FeatureIcon::Tilt), "split", "settings-2", featureIconName(FeatureIcon::Tone)};
const QStringList controlNames = {"Off", "Pen pressure", "Pen tilt", "Stylus wheel", "Fade", "Initial direction", "Direction", "Pen rotation"};
const QStringList maskModes = {"Multiply", "Darken", "Lighten", "Subtract", "Color burn", "Linear height", "Add", "Difference"};
const QStringList patternNames = {"Paper", "Canvas", "Checker", "Noise"};
QImage pattern(const QString &name) {
    QImage image(64, 64, QImage::Format_Grayscale8);
    quint32 random = 0x57ca1423;
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
        random = random * 1664525u + 1013904223u;
        const int grain = int((random >> 24) & 255);
        int coverage = 255;
        if (name == "Paper") coverage = 166 + grain * 89 / 255;
        else if (name == "Canvas") coverage = ((x % 8 == 0) || (y % 8 == 0)) ? 92 : 220 + grain * 35 / 255;
        else if (name == "Checker") coverage = ((x / 8 + y / 8) % 2) ? 255 : 64;
        else if (name == "Noise") coverage = grain;
        image.scanLine(y)[x] = uchar(coverage);
    }
    return image;
}
}

// Keep the visible setting and its source field together when defining controls.
#define FIELD(member) [](const BrushSettings &s) { return qreal(s.member); }, \
    [](BrushSettings &s, qreal value) { s.member = static_cast<std::remove_cvref_t<decltype(s.member)>>(value); }

BrushSettingsPanel::BrushSettingsPanel(Canvas *canvas, BrushLibrary *library, QWidget *parent)
    : QWidget(parent), m_canvas(canvas) {
    setObjectName("brushSettingsContent"); setMinimumWidth(220);
    setProperty("compactDrawingPanel", true);
    auto *quickLayout = new QVBoxLayout(this);
    quickLayout->setContentsMargins(ui::metrics::panelMargin, ui::metrics::panelMargin, ui::metrics::panelMargin, ui::metrics::panelMargin);
    quickLayout->setSpacing(ui::metrics::controlGap);
    auto *actions = new QHBoxLayout; actions->setSpacing(ui::metrics::controlGap);
    auto *pressure = new QToolButton(this); pressure->setObjectName("quickBrushPressureSize");
    pressure->setIcon(featureIcon(FeatureIcon::Pressure)); compactAction(pressure);
    pressure->setCheckable(true); pressure->setToolTip("Pen pressure controls brush size");
    pressure->setAccessibleName("Pressure size"); actions->addStretch();
    connect(pressure, &QToolButton::toggled, this, [this](bool enabled) {
        if (!m_refreshing) mutate([enabled](BrushSettings &b) {
            b.pressureSize = enabled;
            b.sizeDynamics.control = enabled ? BrushControl::Pressure : BrushControl::Off;
        });
    });
    m_refreshers.emplace_back([this, pressure] {
        const QSignalBlocker block(pressure);
        const auto &b = m_canvas->brush;
        pressure->setChecked(b.sizeDynamics.control == BrushControl::Pressure ||
                            (b.sizeDynamics.control == BrushControl::Off && b.pressureSize));
    });
    auto *holdShape = new QToolButton(this); holdShape->setObjectName("holdShapeToggle");
    holdShape->setIcon(appIcon("wand-sparkles")); compactAction(holdShape);
    holdShape->setCheckable(true); holdShape->setChecked(canvas->holdShapeEnabled());
    holdShape->setToolTip("Hold to shape · pause the pen for 0.7 seconds, lift, then choose a shape");
    holdShape->setAccessibleName("Hold to shape"); actions->addWidget(holdShape);
    connect(holdShape, &QToolButton::toggled, canvas, &Canvas::setHoldShapeEnabled);
    connect(canvas, &Canvas::holdShapeEnabledChanged, holdShape, &QToolButton::setChecked);
    auto *stampPlacementRow = new QWidget(this); stampPlacementRow->setObjectName("stampPlacementRow");
    auto *stampPlacementLayout = new QHBoxLayout(stampPlacementRow);
    stampPlacementLayout->setContentsMargins(0, 0, 0, 0); stampPlacementLayout->setSpacing(ui::metrics::controlGap);
    auto *stampPlacementLabel = new QLabel("Adjust stamp", stampPlacementRow);
    stampPlacementLabel->setWordWrap(true); stampPlacementLayout->addWidget(stampPlacementLabel, 1);
    auto *stampPlacement = new QToolButton(this); stampPlacement->setObjectName("stampPlacementToggle");
    stampPlacement->setIcon(appIcon("move")); stampPlacement->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon});
    stampPlacement->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    stampPlacement->setMinimumSize(64, ui::metrics::actionTarget);
    stampPlacement->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    stampPlacement->setCheckable(true);
    const QString stampPlacementHelp = "On: move, resize and rotate each stamp. Off: draw continuously. Turning off places the current stamp.";
    stampPlacement->setToolTip(stampPlacementHelp); stampPlacementLabel->setToolTip(stampPlacementHelp);
    stampPlacement->setAccessibleName("Adjust stamp"); stampPlacement->setAccessibleDescription(stampPlacementHelp);
    stampPlacementLabel->setBuddy(stampPlacement); stampPlacementLayout->addWidget(stampPlacement);
    const auto refreshStampPlacement = [canvas, stampPlacement, stampPlacementRow] {
        const QSignalBlocker block(stampPlacement);
        stampPlacement->setChecked(canvas->stampPlacementEnabled());
        stampPlacement->setText(canvas->stampPlacementEnabled() ? "On" : "Off");
        stampPlacementRow->setVisible(canvas->stampPlacementAvailable());
        stampPlacement->setVisible(canvas->stampPlacementAvailable());
        stampPlacement->setEnabled(canvas->toolAvailable());
    };
    connect(stampPlacement, &QToolButton::clicked, this, [canvas, refreshStampPlacement](bool enabled) {
        // A user mode change may place the pending impression. Restoring a
        // tool's saved preference through Canvas must never commit artwork.
        if (canvas->selectionGestureActive() || canvas->document()->strokeActive()) {
            refreshStampPlacement(); return;
        }
        if (!enabled && canvas->document()->stampPlacementActive() && !canvas->document()->applyTransform()) {
            refreshStampPlacement(); return;
        }
        canvas->setStampPlacementEnabled(enabled);
        refreshStampPlacement();
        if (canvas->hasProject()) canvas->setFocus(Qt::OtherFocusReason);
    });
    m_refreshers.emplace_back(refreshStampPlacement);
    connect(canvas, &Canvas::stampPlacementEnabledChanged, this, refreshStampPlacement);
    connect(canvas, &Canvas::toolChanged, this, refreshStampPlacement);
    connect(canvas, &Canvas::toolStateApplied, this, refreshStampPlacement);
    connect(canvas, &Canvas::toolAvailabilityChanged, this, refreshStampPlacement);
    connect(canvas->maskTool(), &MaskTool::changed, this, refreshStampPlacement);
    connect(canvas->cameraTool(), &CameraTool::changed, this, refreshStampPlacement);
    connect(canvas->keyframeTransformTool(), &KeyframeTransformTool::changed, this, refreshStampPlacement);
    auto *advanced = new QToolButton(this); advanced->setObjectName("advancedBrushSettings");
    advanced->setIcon(appIcon("wrench")); compactAction(advanced);
    advanced->setToolTip("Advanced brush settings"); advanced->setAccessibleName("Advanced brush settings");
    actions->addWidget(advanced); quickLayout->addLayout(actions);
    quickLayout->addWidget(stampPlacementRow);
    connect(advanced, &QToolButton::clicked, this, &BrushSettingsPanel::openAdvancedSettings);

    auto *quickScroll = new QScrollArea(this); quickScroll->setObjectName("quickBrushScroll");
    quickScroll->setWidgetResizable(true); quickScroll->setFrameShape(QFrame::NoFrame);
    quickScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *quickContents = new QWidget; auto *quickContentLayout = new QVBoxLayout(quickContents);
    quickContentLayout->setContentsMargins(0, 0, 0, 0); quickContentLayout->setSpacing(ui::metrics::controlGap);
    const int quickLabelWidth = qMax(ui::metrics::labelWidth, fontMetrics().horizontalAdvance("Smoothing") + 2);
    auto addQuickRow = [quickContentLayout, quickLabelWidth](const QString &label, QWidget *control) {
        auto *row = new PropertyRow(label, control);
        row->setObjectName(control->objectName() + "Row");
        row->setLabelWidth(quickLabelWidth);
        quickContentLayout->addWidget(row);
        return row;
    };
    quickScroll->setWidget(quickContents); quickLayout->addWidget(quickScroll, 1);
    auto *quickSize = number(nullptr, "Size", "quickBrushSize", 1, maxBrushSize, FIELD(size), 1, 1, " px");
    quickSize->setToolTip("Diameter for round tips; longest side for sampled tips");
    addQuickRow("Size", quickSize)->setAccessory(pressure);
    auto *quickOpacity = number(nullptr, "Opacity", "quickBrushStrokeOpacity", 0, 100, FIELD(strokeOpacity), 100, 0, " %");
    quickOpacity->setToolTip("Maximum opacity for one stroke"); addQuickRow("Opacity", quickOpacity);
    auto *quickFlow = number(nullptr, "Flow", "quickBrushFlow", 0, 100, FIELD(opacity), 100, 0, " %");
    quickFlow->setToolTip("Paint deposited by each brush stamp"); addQuickRow("Flow", quickFlow);
    auto *quickHardness = number(nullptr, "Hardness", "quickBrushHardness", 0, 100, FIELD(hardness), 100, 0, " %");
    addQuickRow("Hardness", quickHardness);
    m_refreshers.emplace_back([this, quickHardness] {
        quickHardness->setEnabled(m_canvas->brush.connected.mode == BrushConnectedMode::Off);
    });
    auto *quickSmoothing = new ValueSlider("Stroke stabilization", 0, 100, canvas->stabilization());
    quickSmoothing->setObjectName("quickBrushStabilization");
    quickSmoothing->setToolTip("0 is off; higher values stabilize more strongly with more pen lag. Distances follow the current view zoom.");
    addQuickRow("Smoothing", quickSmoothing);
    // Keep mode switching direct without pushing the five frequent numeric
    // controls below a short drawing dock. Less frequent tuning stays in General.
    actions->insertWidget(0, new StrokeStabilizationOptions(canvas, "quickBrushStabilization", true));
    quickContentLayout->addStretch();
    connect(quickSmoothing, &ValueSlider::valueChanged, canvas, &Canvas::setStabilization);
    m_refreshers.emplace_back([this, quickSmoothing] {
        const QSignalBlocker block(quickSmoothing);
        quickSmoothing->setValueSilently(m_canvas->stabilization());
        quickSmoothing->setEnabled(m_canvas->brush.smoothingEnabled);
    });
    m_quickRefreshers = std::move(m_refreshers);
    m_refreshers.clear();

    m_advancedDialog = new QDialog(this); m_advancedDialog->setObjectName("advancedBrushDialog");
    m_advancedDialog->setWindowTitle("Advanced brush settings");
    m_advancedDialog->setWindowModality(Qt::WindowModal);
    m_advancedDialog->setMinimumSize(360, 400); m_advancedDialog->resize(480, 600);
    auto *dialogLayout = new QVBoxLayout(m_advancedDialog);
    dialogLayout->setContentsMargins(ui::metrics::sectionGap, ui::metrics::sectionGap, ui::metrics::sectionGap, ui::metrics::sectionGap);
    dialogLayout->setSpacing(ui::metrics::controlGap);
    auto *layout = new QHBoxLayout; layout->setSpacing(ui::metrics::controlGap); dialogLayout->addLayout(layout, 1);
    m_category = new BrushCategoryTabs(m_advancedDialog); m_category->setObjectName("brushSettingsCategory");
    m_category->setShape(QTabBar::RoundedWest); m_category->setExpanding(false);
    m_category->setAccessibleName("Brush settings categories"); m_category->setIconSize({ui::metrics::tabIcon, ui::metrics::tabIcon});
    m_category->setProperty("compactDrawingTabs", true);
    m_category->setFocusPolicy(Qt::StrongFocus); layout->addWidget(m_category, 0, Qt::AlignTop);
    auto *content = new QVBoxLayout; content->setSpacing(4); layout->addLayout(content, 1);
    auto *title = new QLabel("General"); title->setObjectName("brushCategoryTitle"); content->addWidget(title);
    m_pages = new QStackedWidget(m_advancedDialog); m_pages->setObjectName("brushSettingsPages"); content->addWidget(m_pages, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, m_advancedDialog);
    auto *close = buttons->button(QDialogButtonBox::Close); close->setObjectName("closeAdvancedBrushSettings");
    close->setProperty("compactDrawingButton", true);
    close->setMinimumHeight(ui::metrics::actionTarget);
    close->setAutoDefault(false); close->setDefault(false);
    close->setToolTip("Close advanced settings; changes are saved as you edit");
    close->setAccessibleName("Close advanced brush settings");
    dialogLayout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, m_advancedDialog, &QDialog::reject);
    connect(m_advancedDialog, &QDialog::finished, m_canvas, [canvas] {
        if (canvas->hasProject()) canvas->setFocus(Qt::OtherFocusReason);
    });
    connect(m_category, &QTabBar::currentChanged, this, [this, title](int index) {
        m_pages->setCurrentIndex(index); title->setText(m_category->tabToolTip(index));
    });
    auto visual = [this](QFormLayout *form, BrushVisualWidget::Kind kind, const char *id) {
        auto *control = new BrushVisualWidget(kind); control->setObjectName(id); form->addRow(control);
        connect(control, &BrushVisualWidget::brushEdited, this, [this](const BrushSettings &settings) {
            if (!m_refreshing) m_canvas->setBrushSettings(settings);
        });
        m_refreshers.emplace_back([this, control] { control->setBrush(m_canvas->brush); });
        return control;
    };

    auto *general = category("General");
    auto *size = number(general, "Size", "brushSize", 1, maxBrushSize, FIELD(size), 1, 1, " px");
    size->setToolTip("Diameter for round tips; longest side for sampled tips");
    number(general, "Hardness", "brushHardness", 0, 100, FIELD(hardness), 100, 0, " %");
    choice(general, "Mode", "brushBlendMode", {"Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten",
        "Color dodge", "Color burn", "Linear dodge", "Linear burn", "Difference", "Exclusion", "Subtract", "Divide",
        "Hue", "Saturation", "Color", "Luminosity", "Dissolve"}, FIELD(blendMode));
    auto *opacity = number(general, "Opacity", "brushStrokeOpacity", 0, 100, FIELD(strokeOpacity), 100, 0, " %");
    opacity->setToolTip("Maximum opacity for one stroke");
    auto *flow = number(general, "Flow", "brushFlow", 0, 100, FIELD(opacity), 100, 0, " %");
    flow->setToolTip("Paint deposited by each brush stamp");
    visual(general, BrushVisualWidget::Kind::Spacing, "brushSpacingVisual");
    auto *spacing = number(general, "Spacing", "brushSpacing", 1, 400, FIELD(spacing), 100, 0, " %");
    spacing->setToolTip("Distance between stamps as a percentage of tip size");
    auto *smoothing = new ValueSlider("Stroke stabilization", 0, 100, canvas->stabilization());
    smoothing->setObjectName("brushStabilization");
    smoothing->setToolTip("0 is off; higher values stabilize more strongly with more pen lag. Distances follow the current view zoom.");
    propertyRow(general, "Smoothing", smoothing);
    general->addRow(new StrokeStabilizationOptions(canvas, "brushStabilization"));
    connect(smoothing, &ValueSlider::valueChanged, canvas, &Canvas::setStabilization);
    m_refreshers.emplace_back([this, smoothing] {
        const QSignalBlocker block(smoothing); smoothing->setValueSilently(m_canvas->stabilization());
        smoothing->setEnabled(m_canvas->brush.smoothingEnabled);
    });
    check(general, "Pressure size", "brushPressureSize", [](const BrushSettings &b) {
        return b.sizeDynamics.control == BrushControl::Pressure || (b.sizeDynamics.control == BrushControl::Off && b.pressureSize);
    }, [](BrushSettings &b, qreal n) { b.pressureSize = n; b.sizeDynamics.control = n ? BrushControl::Pressure : BrushControl::Off; });
    check(general, "Airbrush build-up", "brushAirbrush", FIELD(airbrush));
    auto *rate = number(general, "Rate", "brushAirbrushRate", 1, 60, FIELD(airbrushRate), 1, 0, " /s");
    rate->setToolTip("Build-up intervals per second while the pen or mouse remains pressed");
    m_refreshers.emplace_back([this, general, rate] { setPropertyVisible(general, rate, m_canvas->brush.airbrush); });

    auto *tip = category("Tip shape");
    auto *pathMode = new QTabBar; pathMode->setObjectName("brushConnectedMode");
    pathMode->setProperty("compactDrawingTabs", true);
    pathMode->setAccessibleName("Brush stroke construction");
    for (const auto &name : {"Stamps", "Ribbon", "Repeat"}) pathMode->addTab(name);
    pathMode->setExpanding(true);
    pathMode->setTabToolTip(0, "Separate brush impressions");
    pathMode->setTabToolTip(1, "Bend a continuous image strip along the drawn path");
    pathMode->setTabToolTip(2, "Join repeating modules along the drawn path");
    tip->addRow(pathMode);
    connect(pathMode, &QTabBar::currentChanged, this, [this](int value) {
        if (!m_refreshing) mutate([value](BrushSettings &b) { b.connected.mode = BrushConnectedMode(value); });
    });
    auto *repeatLength = number(tip, "Repeat length", "brushRepeatLength", .1, 16, FIELD(connected.repeatLength), 1, 2);
    repeatLength->setToolTip("Motif length relative to brush width; Ribbon also uses the source image proportions");
    check(tip, "Alternate flip", "brushAlternateRepeat", FIELD(connected.flipAlternate));
    auto *alternate = tip->parentWidget()->findChild<QCheckBox *>("brushAlternateRepeat");
    m_refreshers.emplace_back([this, tip, pathMode, repeatLength, alternate] {
        const QSignalBlocker block(pathMode); pathMode->setCurrentIndex(int(m_canvas->brush.connected.mode));
        const bool enabled = m_canvas->brush.connected.mode != BrushConnectedMode::Off;
        setPropertyVisible(tip, repeatLength, enabled); tip->setRowVisible(alternate, enabled);
    });
    m_tipPicker = new QComboBox; m_tipPicker->setObjectName("brushTipPicker"); m_tipPicker->setAccessibleName("Primary brush tip");
    m_tipPicker->setToolTip("Choose a tip shape from the brush library; keep the current settings");
    propertyRow(tip, "Tip", m_tipPicker);
    auto *tipSet = new BrushTipSetWidget; tip->addRow(tipSet);
    connect(tipSet, &BrushTipSetWidget::brushEdited, this, [this](const BrushSettings &brush) { if (!m_refreshing) m_canvas->setBrushSettings(brush); });
    m_refreshers.emplace_back([this, tipSet] { tipSet->setBrush(m_canvas->brush); });
    auto *stampSize = number(tip, "Size variation", "stampSizeVariation", 0, 100,
        [](const BrushSettings &b) { return b.sizeDynamics.jitterPerStroke ? 0. : b.sizeDynamics.jitter; },
        [](BrushSettings &b, qreal v) { b.sizeDynamics.jitter = v; b.sizeDynamics.jitterPerStroke = false; }, 100, 0, " %");
    auto *stampAngle = number(tip, "Rotation variation", "stampRotationVariation", 0, 100,
        [](const BrushSettings &b) { return b.angleDynamics.jitterPerStroke ? 0. : b.angleDynamics.jitter; },
        [](BrushSettings &b, qreal v) { b.angleDynamics.jitter = v; b.angleDynamics.jitterPerStroke = false; }, 100, 0, " %");
    auto *stampColor = number(tip, "Saturation variation", "stampSaturationVariation", 0, 100,
        [](const BrushSettings &b) { const auto &d = b.colorDynamics;
            return d.enabled && d.perTip && (b.colorTip.isNull() || d.applyToColorTip) ? d.saturation : 0.; },
        [](BrushSettings &b, qreal v) { auto &d = b.colorDynamics; d.saturation = v; d.enabled = true; d.perTip = true;
            if (!b.colorTip.isNull()) d.applyToColorTip = true; }, 100, 0, " %");
    for (auto *control : {stampSize, stampAngle, stampColor})
        control->setToolTip("Random variation between stamps within one stroke; 0 keeps this property steady");
    m_refreshers.emplace_back([this, tip, stampSize, stampAngle, stampColor] {
        for (auto *control : {stampSize, stampAngle, stampColor})
            setPropertyVisible(tip, control, m_canvas->brush.tipFrames.size() > 1);
    });
    visual(tip, BrushVisualWidget::Kind::Tip, "brushTipVisual");
    number(tip, "Angle", "brushAngle", -360, 360, FIELD(angle), 1, 0, " °");
    number(tip, "Roundness", "brushRoundness", 1, 100, FIELD(roundness), 100, 0, " %");
    number(tip, "Spacing", "brushTipSpacing", 1, 400, FIELD(spacing), 100, 0, " %");
    check(tip, "Flip X", "brushFlipX", FIELD(flipX)); check(tip, "Flip Y", "brushFlipY", FIELD(flipY));
    connect(m_tipPicker, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_refreshing || index < 0) return;
        const auto id = m_tipPicker->itemData(index).toString();
        if (id == "current") return;
        if (id == "round") { mutate([](BrushSettings &b) { b.tipMask = {}; b.colorTip = {}; b.tipFrames.clear(); b.tipOrder = BrushTipOrder::Shuffle; }); return; }
        if (m_library) for (const auto &preset : m_library->presets()) if (preset.id == id) {
            const auto shape = preset.brush;
            mutate([shape](BrushSettings &b) { b.tipMask = shape.tipMask; b.colorTip = shape.colorTip; b.tipFrames = shape.tipFrames; b.tipOrder = shape.tipOrder; }); return;
        }
    });

    auto *shape = category("Shape dynamics");
    auto *shapePages = new BrushDetailPages("brushShapeDynamicsTabs", "Shape dynamics properties");
    shape->addRow(shapePages);
    auto *sizeFields = shapePages->addPage("Size");
    dynamic(sizeFields, "Size", "size", [](BrushSettings &b) -> BrushDynamic & { return b.sizeDynamics; }, true, false, false);
    number(sizeFields, "Tilt scale", "brushTiltScale", 0, 400, FIELD(tiltScale), 100, 0, " %");
    check(sizeFields, "Brush projection", "brushProjection", FIELD(projection));
    dynamic(shapePages->addPage("Angle"), "Angle", "angle", [](BrushSettings &b) -> BrushDynamic & { return b.angleDynamics; }, false, true, false);
    dynamic(shapePages->addPage("Roundness"), "Roundness", "roundness", [](BrushSettings &b) -> BrushDynamic & { return b.roundnessDynamics; }, true, false, false);

    auto *scatter = category("Scattering");
    visual(scatter, BrushVisualWidget::Kind::Scatter, "brushScatterVisual");
    number(scatter, "Scatter", "brushScatter", 0, 1000, FIELD(scatter), 100, 0, " %");
    check(scatter, "Both axes", "brushScatterBothAxes", FIELD(scatterBothAxes));
    inputControl(scatter, "Control", "brushScatterControl", FIELD(scatterControl));
    auto *scatterFade = number(scatter, "Fade steps", "brushScatterFade", 1, 10000, FIELD(scatterFade));
    m_refreshers.emplace_back([this, scatter, scatterFade] { setPropertyVisible(scatter, scatterFade, m_canvas->brush.scatterControl == BrushControl::Fade); });
    number(scatter, "Count", "brushCount", 1, 16, FIELD(count));
    number(scatter, "Count jitter", "brushCountJitter", 0, 100, FIELD(countJitter), 100, 0, " %");
    inputControl(scatter, "Count control", "brushCountControl", FIELD(countControl));
    auto *countFade = number(scatter, "Fade steps", "brushCountFade", 1, 10000, FIELD(countFade));
    m_refreshers.emplace_back([this, scatter, countFade] { setPropertyVisible(scatter, countFade, m_canvas->brush.countControl == BrushControl::Fade); });

    auto *cluster = new AdvancedBrushDynamicsWidget(AdvancedBrushDynamicsWidget::Section::Spray);
    scatter->addRow(cluster);
    connect(cluster, &AdvancedBrushDynamicsWidget::clusterSprayEdited, this, [this](const BrushClusterSpray &value) {
        if (!m_refreshing) mutate([value](BrushSettings &b) { b.clusterSpray = value; });
    });
    m_refreshers.emplace_back([this, cluster] { cluster->setSettings(m_canvas->brush); });

    auto *texture = category("Texture");
    check(texture, "Texture", "brushTextureEnabled", FIELD(texture.enabled));
    auto *textureDetails = new QWidget; textureDetails->setObjectName("brushTextureDetails");
    texture->addRow(textureDetails); texture = detailForm(textureDetails);
    m_refreshers.emplace_back([this, textureDetails] { textureDetails->setVisible(m_canvas->brush.texture.enabled); });
    auto *patternRow = new QWidget; auto *patternLayout = new QHBoxLayout(patternRow); patternLayout->setContentsMargins(0, 0, 0, 0); patternLayout->setSpacing(4);
    m_patternPicker = new QListWidget; m_patternPicker->setObjectName("brushTexturePattern");
    m_patternPicker->setViewMode(QListView::IconMode); m_patternPicker->setMovement(QListView::Static);
    m_patternPicker->setFlow(QListView::LeftToRight); m_patternPicker->setWrapping(false);
    m_patternPicker->setIconSize({28, 28}); m_patternPicker->setGridSize({36, 36}); m_patternPicker->setFixedHeight(48);
    m_patternPicker->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_patternPicker->setAccessibleName("Brush texture patterns"); patternLayout->addWidget(m_patternPicker, 1);
    auto *import = new QToolButton; import->setObjectName("importBrushTexture"); import->setIcon(appIcon("folder-open"));
    compactAction(import); import->setToolTip("Import texture image"); import->setAccessibleName("Import texture image"); patternLayout->addWidget(import);
    texture->addRow(patternRow);
    visual(texture, BrushVisualWidget::Kind::Texture, "brushTextureVisual");
    connect(m_patternPicker, &QListWidget::currentRowChanged, this, [this](int index) {
        if (m_refreshing || index < 0) return;
        const auto name = m_patternPicker->item(index)->data(Qt::UserRole).toString();
        if (!patternNames.contains(name)) return;
        mutate([name](BrushSettings &b) { b.texture.name = name; b.texture.pattern = pattern(name); b.texture.enabled = true; });
    });
    connect(import, &QToolButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(m_advancedDialog, "Import brush texture", {}, "Images (*.png *.jpg *.jpeg *.bmp *.webp)");
        if (path.isEmpty()) return;
        QString error; if (!importTexture(path, &error)) QMessageBox::warning(m_advancedDialog, "Texture import", error);
    });
    check(texture, "Invert", "brushTextureInvert", FIELD(texture.invert));
    number(texture, "Scale", "brushTextureScale", 10, 1000, FIELD(texture.scale), 100, 0, " %");
    number(texture, "Brightness", "brushTextureBrightness", -100, 100, FIELD(texture.brightness), 100, 0, " %");
    number(texture, "Contrast", "brushTextureContrast", -100, 100, FIELD(texture.contrast), 100, 0, " %");
    check(texture, "Texture each tip", "brushTextureEachTip", FIELD(texture.eachTip));
    choice(texture, "Mode", "brushTextureMode", maskModes, FIELD(texture.mode));
    number(texture, "Depth", "brushTextureDepth", 0, 100, FIELD(texture.depth), 100, 0, " %");
    dynamic(texture, "Depth dynamics", "textureDepth", [](BrushSettings &b) -> BrushDynamic & { return b.texture.dynamics; });

    auto *dual = category("Dual brush");
    check(dual, "Dual brush", "brushDualEnabled", FIELD(dual.enabled));
    auto *dualDetails = new QWidget; dualDetails->setObjectName("brushDualDetails");
    dual->addRow(dualDetails); dual = detailForm(dualDetails);
    m_refreshers.emplace_back([this, dualDetails] { dualDetails->setVisible(m_canvas->brush.dual.enabled); });
    m_dualPicker = new QComboBox; m_dualPicker->setObjectName("brushDualTip"); m_dualPicker->setAccessibleName("Secondary brush tip");
    m_dualPicker->setToolTip("Use only the selected preset's tip shape as the secondary mask");
    propertyRow(dual, "Second tip", m_dualPicker);
    connect(m_dualPicker, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_refreshing || index < 0) return;
        const auto id = m_dualPicker->itemData(index).toString();
        if (id == "current") return;
        if (id == "round") { mutate([](BrushSettings &b) { b.dual.tipMask = {}; b.dual.name = "Round"; }); return; }
        if (m_library) for (const auto &preset : m_library->presets()) if (preset.id == id) {
            const auto mask = brushTipFrameImage(preset.brush, preset.brush.tipFrames.isEmpty() ? -1 : 0); const auto name = preset.name;
            mutate([mask, name](BrushSettings &b) { b.dual.tipMask = mask; b.dual.name = name; }); return;
        }
    });
    choice(dual, "Mode", "brushDualMode", maskModes, FIELD(dual.mode));
    number(dual, "Size", "brushDualSize", 1, 256, FIELD(dual.size), 1, 1, " px");
    number(dual, "Spacing", "brushDualSpacing", 1, 400, FIELD(dual.spacing), 100, 0, " %");
    number(dual, "Scatter", "brushDualScatter", 0, 1000, FIELD(dual.scatter), 100, 0, " %");
    number(dual, "Count", "brushDualCount", 1, 16, FIELD(dual.count));

    auto *color = category("Color dynamics");
    auto *palette = new QWidget; auto *paletteLayout = new QHBoxLayout(palette); paletteLayout->setContentsMargins(0, 0, 0, 0); paletteLayout->setSpacing(4);
    for (bool background : {false, true}) {
        auto *button = new QPushButton(background ? "BG" : "FG");
        button->setAccessibleName(background ? "Background color" : "Foreground color");
        button->setObjectName(background ? "brushBackgroundColor" : "brushForegroundColor");
        paletteLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, background] { chooseColor(background); });
        m_refreshers.emplace_back([this, button, background] {
            const auto c = background ? m_canvas->brush.backgroundColor : m_canvas->brush.color;
            QPixmap chip(14, 14); chip.fill(c); button->setIcon(QIcon(chip)); button->setToolTip((background ? "Background " : "Foreground ") + c.name().toUpper());
        });
    }
    color->addRow(palette);
    check(color, "Color dynamics", "brushColorDynamicsEnabled", FIELD(colorDynamics.enabled));
    check(color, "Vary image colors", "brushVaryImageColors", FIELD(colorDynamics.applyToColorTip));
    auto *tint = number(color, "Foreground tint", "brushColorTint", 0, 100, FIELD(colorDynamics.tintAmount), 100, 0, " %");
    tint->setToolTip("Tint the original stamp toward the foreground hue and saturation, preserving its shading and transparency");
    m_refreshers.emplace_back([this, color, tint] { setPropertyVisible(color, tint, !m_canvas->brush.colorTip.isNull()); });
    auto *imageColors = color->parentWidget()->findChild<QCheckBox *>("brushVaryImageColors");
    imageColors->setToolTip("Vary the stamp image's hue, saturation and brightness while preserving its transparency");
    check(color, "Apply per tip", "brushColorPerTip", FIELD(colorDynamics.perTip));
    auto *mix = number(color, "FG/BG jitter", "brushForegroundBackground", 0, 100, FIELD(colorDynamics.foregroundBackground), 100, 0, " %");
    auto *mixControl = inputControl(color, "Control", "brushColorControl", FIELD(colorDynamics.control));
    auto *colorFade = number(color, "Fade steps", "brushColorFade", 1, 10000, FIELD(colorDynamics.fade));
    auto *hue = number(color, "Hue jitter", "brushHueJitter", 0, 100, FIELD(colorDynamics.hue), 100, 0, " %");
    auto *saturation = number(color, "Saturation jitter", "brushSaturationJitter", 0, 100, FIELD(colorDynamics.saturation), 100, 0, " %");
    auto *brightness = number(color, "Brightness jitter", "brushBrightnessJitter", 0, 100, FIELD(colorDynamics.brightness), 100, 0, " %");
    auto *purity = number(color, "Purity", "brushColorPurity", -100, 100, FIELD(colorDynamics.purity), 100, 0, " %");
    m_refreshers.emplace_back([this, color, palette, imageColors, mix, mixControl, colorFade, hue, saturation, brightness, purity] {
        const bool embedded = !m_canvas->brush.colorTip.isNull();
        color->setRowVisible(palette, !embedded); color->setRowVisible(imageColors, embedded);
        setPropertyVisible(color, mix, !embedded); setPropertyVisible(color, mixControl, !embedded);
        setPropertyVisible(color, colorFade, !embedded && m_canvas->brush.colorDynamics.control == BrushControl::Fade);
        for (auto *control : {hue, saturation, brightness, purity})
            control->setEnabled(!embedded || m_canvas->brush.colorDynamics.applyToColorTip);
    });

    auto *transfer = category("Transfer");
    auto *transferPages = new BrushDetailPages("brushTransferTabs", "Transfer properties");
    transfer->addRow(transferPages);
    dynamic(transferPages->addPage("Opacity"), "Opacity", "opacity", [](BrushSettings &b) -> BrushDynamic & { return b.opacityDynamics; }, true, false, false);
    dynamic(transferPages->addPage("Flow"), "Flow", "flow", [](BrushSettings &b) -> BrushDynamic & { return b.flowDynamics; }, true, false, false);

    auto *speed = new AdvancedBrushDynamicsWidget(AdvancedBrushDynamicsWidget::Section::Speed);
    transferPages->addPage("Speed")->addRow(speed);
    connect(speed, &AdvancedBrushDynamicsWidget::speedDynamicsEdited, this, [this](const BrushSpeedDynamics &value) {
        if (!m_refreshing) mutate([value](BrushSettings &b) { b.speedDynamics = value; });
    });
    m_refreshers.emplace_back([this, speed] { speed->setSettings(m_canvas->brush); });

    auto *pose = category("Brush pose");
    visual(pose, BrushVisualWidget::Kind::Pose, "brushPoseVisual");
    check(pose, "Override tilt X", "brushOverrideTiltX", FIELD(pose.overrideTiltX));
    number(pose, "Tilt X", "brushPoseTiltX", -90, 90, FIELD(pose.tiltX), 1, 0, " °");
    check(pose, "Override tilt Y", "brushOverrideTiltY", FIELD(pose.overrideTiltY));
    number(pose, "Tilt Y", "brushPoseTiltY", -90, 90, FIELD(pose.tiltY), 1, 0, " °");
    check(pose, "Override rotation", "brushOverrideRotation", FIELD(pose.overrideRotation));
    number(pose, "Rotation", "brushPoseRotation", -360, 360, FIELD(pose.rotation), 1, 0, " °");
    check(pose, "Override pressure", "brushOverridePressure", FIELD(pose.overridePressure));
    number(pose, "Pressure", "brushPosePressure", 0, 100, FIELD(pose.pressure), 100, 0, " %");

    auto *symmetry = category("Symmetry");
    auto *modes = new QWidget; modes->setObjectName("brushSymmetryMode");
    auto *modeLayout = new QGridLayout(modes); modeLayout->setContentsMargins(0, 0, 0, 0); modeLayout->setSpacing(3);
    auto *modeGroup = new QButtonGroup(modes); modeGroup->setExclusive(true);
    const QStringList modeNames{"Off", "Vertical", "Horizontal", "Dual axis", "Diagonal", "Wave", "Circle", "Spiral", "Parallel lines", "Radial"};
    for (int index = 0; index < modeNames.size(); ++index) {
        auto *button = new QToolButton; button->setObjectName(QString("brushSymmetryChoice%1").arg(index));
        button->setCheckable(true); compactAction(button);
        button->setToolTip(modeNames[index]); button->setAccessibleName(modeNames[index] + " symmetry");
        QIcon icon;
        for (const qreal ratio : {1., 2., 3.}) {
            QPixmap picture(QSize(26, 26) * ratio); picture.setDevicePixelRatio(ratio); picture.fill(Qt::transparent);
            QPainter painter(&picture);
            painter.setRenderHint(QPainter::Antialiasing); painter.setPen(QPen(QColor(ui::text), 1.5));
            BrushSymmetry sample; sample.mode = SymmetryMode(index); sample.radius = 8; sample.distance = 8; sample.amplitude = 5; sample.wavelength = 15;
            if (!index) { painter.drawEllipse(QRectF(5, 5, 16, 16)); painter.drawLine(7, 19, 19, 7); }
            else painter.drawPath(brushSymmetryGuide(sample, {26, 26}));
            painter.end(); icon.addPixmap(picture);
        }
        button->setIcon(icon); modeGroup->addButton(button, index); modeLayout->addWidget(button, index / 5, index % 5);
        m_refreshers.emplace_back([this, button, index] { const QSignalBlocker block(button); button->setChecked(int(m_canvas->brush.symmetry.mode) == index); });
    }
    connect(modeGroup, &QButtonGroup::idClicked, this, [this](int index) { mutate([index](BrushSettings &b) { b.symmetry.mode = SymmetryMode(index); }); });
    symmetry->addRow(modes);
    auto *centerControl = visual(symmetry, BrushVisualWidget::Kind::Symmetry, "brushSymmetryVisual");
    connect(m_canvas, &Canvas::documentChanged, centerControl, [this, centerControl] {
        if (m_canvas->hasProject()) centerControl->setArtworkSize(QSizeF(m_canvas->document()->project().width, m_canvas->document()->project().height));
    });
    if (m_canvas->hasProject()) centerControl->setArtworkSize(QSizeF(m_canvas->document()->project().width, m_canvas->document()->project().height));
    number(symmetry, "Center X", "brushSymmetryX", 0, 100,
        [](const BrushSettings &b) { return b.symmetry.center.x(); }, [](BrushSettings &b, qreal n) { b.symmetry.center.setX(n); }, 100, 1, " %");
    number(symmetry, "Center Y", "brushSymmetryY", 0, 100,
        [](const BrushSettings &b) { return b.symmetry.center.y(); }, [](BrushSettings &b, qreal n) { b.symmetry.center.setY(n); }, 100, 1, " %");
    number(symmetry, "Angle", "brushSymmetryAngle", -360, 360, FIELD(symmetry.angle), 1, 0, " °");
    auto *symmetryCount = number(symmetry, "Count", "brushSymmetryCount", 2, 12, FIELD(symmetry.count));
    auto *symmetryRadius = number(symmetry, "Radius", "brushSymmetryRadius", 1, 8192, FIELD(symmetry.radius), 1, 0, " px");
    auto *symmetryDistance = number(symmetry, "Distance", "brushSymmetryDistance", 1, 8192, FIELD(symmetry.distance), 1, 0, " px");
    auto *symmetryAmplitude = number(symmetry, "Amplitude", "brushSymmetryAmplitude", 0, 8192, FIELD(symmetry.amplitude), 1, 0, " px");
    auto *symmetryWavelength = number(symmetry, "Wavelength", "brushSymmetryWavelength", 1, 8192, FIELD(symmetry.wavelength), 1, 0, " px");

    m_refreshers.emplace_back([this, symmetry, symmetryCount, symmetryRadius, symmetryDistance, symmetryAmplitude, symmetryWavelength] {
        const auto mode = m_canvas->brush.symmetry.mode;
        setPropertyVisible(symmetry, symmetryCount, mode == SymmetryMode::Radial || mode == SymmetryMode::ParallelLines);
        setPropertyVisible(symmetry, symmetryRadius, mode == SymmetryMode::Circle || mode == SymmetryMode::Spiral);
        setPropertyVisible(symmetry, symmetryDistance, mode == SymmetryMode::Spiral || mode == SymmetryMode::ParallelLines);
        setPropertyVisible(symmetry, symmetryAmplitude, mode == SymmetryMode::Wave);
        setPropertyVisible(symmetry, symmetryWavelength, mode == SymmetryMode::Wave);
    });

    auto *global = category("Behavior");
    check(global, "Noise", "brushNoise", FIELD(noise));
    check(global, "Wet edges", "brushWetEdges", FIELD(wetEdges));
    check(global, "Airbrush build-up", "brushBuildUp", FIELD(airbrush));
    check(global, "Smoothing", "brushSmoothingEnabled", FIELD(smoothingEnabled));
    // Catalog tools restore independent textures; the legacy preset carry-over
    // switch has no effect in this workflow and should not appear as a dead control.
    if (!m_canvas->property("toolCatalogManaged").toBool())
        check(global, "Protect texture", "brushProtectTexture", FIELD(protectTexture));

    auto *randomness = new QGroupBox("Randomness"); randomness->setObjectName("brushRandomness");
    auto *randomLayout = new QVBoxLayout(randomness); randomLayout->setContentsMargins(6, 10, 6, 6); randomLayout->setSpacing(4);
    auto *seedMode = new QTabBar(randomness); seedMode->setObjectName("brushSeedMode");
    seedMode->addTab("Per stroke"); seedMode->addTab("Fixed seed"); seedMode->setExpanding(true);
    seedMode->setProperty("compactDrawingTabs", true);
    seedMode->setAccessibleName("Brush randomness mode");
    seedMode->setTabToolTip(0, "Use a different random seed for every stroke");
    seedMode->setTabToolTip(1, "Reuse the seed for repeatable brush variation; the same path and input produce the same variation");
    randomLayout->addWidget(seedMode);
    auto *seedRow = new QWidget(randomness); auto *seedLayout = new QHBoxLayout(seedRow);
    seedLayout->setContentsMargins(0, 0, 0, 0); seedLayout->setSpacing(ui::metrics::controlGap);
    auto *seed = new ValueSlider("Random seed", 0, std::numeric_limits<quint32>::max(), m_canvas->brush.randomSeed);
    seed->setObjectName("brushRandomSeed"); seed->setSliderRange(1, 999999); seedLayout->addWidget(seed, 1);
    auto *shuffle = new QToolButton(seedRow); shuffle->setObjectName("brushReseed");
    shuffle->setIcon(appIcon("wand-sparkles")); compactAction(shuffle);
    shuffle->setToolTip("Choose another fixed random seed"); shuffle->setAccessibleName("New random seed");
    seedLayout->addWidget(shuffle); randomLayout->addWidget(seedRow); global->addRow(randomness);
    connect(seedMode, &QTabBar::currentChanged, this, [this](int index) {
        if (!m_refreshing) mutate([index](BrushSettings &b) { b.lockRandomSeed = index == 1; });
    });
    connect(seed, &ValueSlider::valueChanged, this, [this](qreal value) {
        if (!m_refreshing) mutate([value](BrushSettings &b) { b.randomSeed = quint32(value); });
    });
    connect(shuffle, &QToolButton::clicked, this, [this] {
        auto value = QRandomGenerator::global()->bounded(1u, 1000000u);
        if (value == m_canvas->brush.randomSeed) value = value == 999999u ? 1u : value + 1;
        mutate([value](BrushSettings &b) { b.randomSeed = value; });
    });
    m_refreshers.emplace_back([this, seedMode, seedRow, seed] {
        const QSignalBlocker block(seedMode); seedMode->setCurrentIndex(m_canvas->brush.lockRandomSeed ? 1 : 0);
        seedRow->setEnabled(m_canvas->brush.lockRandomSeed);
        seed->setValueSilently(m_canvas->brush.randomSeed);
    });

    auto *surface = category("Patterns and tones");
    auto *surfaceEditor = new BrushSurfacePatternWidget;
    surface->addRow(surfaceEditor);
    connect(surfaceEditor, &BrushSurfacePatternWidget::patternEdited, this, [this](const BrushSurfacePattern &value) {
        if (!m_refreshing) mutate([value](BrushSettings &b) { b.surface = value; });
    });
    m_refreshers.emplace_back([this, surfaceEditor] { surfaceEditor->setPattern(m_canvas->brush.surface); surfaceEditor->setArtworkFitAvailable(m_canvas->hasProject()); });
    connect(surfaceEditor, &BrushSurfacePatternWidget::fitToArtworkRequested, this, [this, surfaceEditor] {
        if (!m_canvas->hasProject()) return;
        // Sheet coordinates include margins and a header; brush coordinates
        // use the output origin and the current camera's inverse mapping.
        const auto frame = QPolygonF(sheetGeometry(m_canvas->project(), m_canvas->shot()).output);
        QPolygonF artwork; for (const auto &point : frame) artwork.append(m_canvas->artworkPoint(m_canvas->viewTransform().map(point)));
        surfaceEditor->fitToArtwork(artwork);
    });
    const auto discardSurfaceDraft = [this, surfaceEditor] { surfaceEditor->resetPattern(m_canvas->brush.surface); };
    connect(m_advancedDialog, &QDialog::finished, surfaceEditor, discardSurfaceDraft);
    connect(m_canvas, &Canvas::toolStateApplied, surfaceEditor, discardSurfaceDraft);
    connect(m_canvas, &Canvas::toolChanged, surfaceEditor, discardSurfaceDraft);

    m_refreshers.emplace_back([this, scatter, dual, pose] {
        const bool stamps = m_canvas->brush.connected.mode == BrushConnectedMode::Off;
        scatter->parentWidget()->setEnabled(stamps);
        dual->parentWidget()->setEnabled(stamps); pose->parentWidget()->setEnabled(stamps);
        for (const auto *name : {"brushAngle", "brushRoundness", "brushTipSpacing", "brushFlipX", "brushFlipY",
                                "brushProjection", "brushTiltScale", "brushWetEdges", "brushNoise",
                                "brushSpacing", "brushSpacingVisual", "brushTipVisual", "stampRotationVariation",
                                "angleDynamics", "roundnessDynamics", "brushHardness", "brushAirbrush", "brushAirbrushRate"})
            if (auto *control = m_advancedDialog->findChild<QWidget *>(name)) control->setEnabled(stamps);
        for (const auto *name : {"brushScatterVisual", "brushScatter", "brushScatterBothAxes", "brushScatterControl", "brushScatterFade"})
            if (auto *control = m_advancedDialog->findChild<QWidget *>(name)) control->setEnabled(stamps && !m_canvas->brush.clusterSpray.enabled);
        if (auto *control = m_advancedDialog->findChild<QWidget *>("brushTextureEachTip")) control->setEnabled(stamps);
    });
    connect(m_canvas, &Canvas::brushChanged, this, &BrushSettingsPanel::refresh);
    connect(m_canvas, &Canvas::documentChanged, this, [this] {
        if (!m_canvas->hasProject() && m_advancedDialog->isVisible()) m_advancedDialog->reject();
        setEnabled(m_canvas->hasProject());
    });
    const auto closeUnavailable = [this] {
        if (m_advancedDialog->isVisible() && (!m_canvas->toolAvailable() ||
                (m_canvas->tool() != Canvas::Tool::Brush && m_canvas->tool() != Canvas::Tool::Eraser)))
            m_advancedDialog->reject();
    };
    connect(m_canvas, &Canvas::toolChanged, this, closeUnavailable);
    connect(m_canvas, &Canvas::toolAvailabilityChanged, this, closeUnavailable);
    setEnabled(m_canvas->hasProject());
    setLibrary(library); refresh();
    title->setText(m_category->tabToolTip(m_category->currentIndex()));
}

void BrushSettingsPanel::openAdvancedSettings() {
    if (!isEnabled() || !m_canvas->hasProject() || !m_canvas->toolAvailable()) return;
    if (!m_advancedDialog->isVisible()) {
        if (auto *display = screen())
            m_advancedDialog->resize(m_advancedDialog->size().boundedTo(display->availableGeometry().size() - QSize(40, 60)));
        // Keep the existing tab and scroll positions, but refresh settings changed
        // by the compact controls or by another tool since the dialog was closed.
        m_advancedInitialized = false;
        refresh();
        m_advancedDialog->open();
    }
    m_advancedDialog->raise();
    m_advancedDialog->activateWindow();
}

QFormLayout *BrushSettingsPanel::category(const QString &name) {
    auto *contents = new QWidget; auto *layout = new QVBoxLayout(contents);
    layout->setContentsMargins(0, 0, 4, 4); layout->setSpacing(4);
    auto *form = new QFormLayout; form->setHorizontalSpacing(4); form->setVerticalSpacing(4);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    layout->addLayout(form); layout->addStretch();
    auto *scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); scroll->setWidget(contents);
    const int index = m_pages->addWidget(scroll);
    m_category->addTab(appIcon(categoryIcons.value(index, "settings-2")), QString());
    m_category->setTabToolTip(index, name); m_category->setAccessibleTabName(index, name);
    return form;
}

QWidget *BrushSettingsPanel::number(QFormLayout *form, const QString &label, const char *id,
    qreal minimum, qreal maximum, Reader read, Writer write, qreal scale, int decimals, const QString &suffix) {
    auto *control = new ValueSlider(label, minimum, maximum, read(m_canvas->brush) * scale, decimals, suffix);
    control->setObjectName(id); control->setToolTip(label);
    if (form) propertyRow(form, label, control);
    connect(control, &ValueSlider::valueChanged, this, [this, write, scale](qreal value) {
        if (!m_refreshing) mutate([&](BrushSettings &b) { write(b, value / scale); });
    });
    m_refreshers.emplace_back([this, control, read, scale] {
        const QSignalBlocker block(control); control->setValueSilently(read(m_canvas->brush) * scale);
    });
    return control;
}

void BrushSettingsPanel::check(QFormLayout *form, const QString &label, const char *id, Reader read, Writer write) {
    auto *control = new QCheckBox(label); control->setObjectName(id); control->setMinimumHeight(28); form->addRow(control);
    connect(control, &QCheckBox::toggled, this, [this, write](bool value) {
        if (!m_refreshing) mutate([&](BrushSettings &b) { write(b, value); });
    });
    m_refreshers.emplace_back([this, control, read] {
        const QSignalBlocker block(control); control->setChecked(read(m_canvas->brush) != 0);
    });
}

QComboBox *BrushSettingsPanel::choice(QFormLayout *form, const QString &label, const char *id,
    const QStringList &labels, Reader read, Writer write) {
    auto *control = new QComboBox; control->setObjectName(id); control->setAccessibleName(label); control->addItems(labels);
    control->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); control->setMinimumContentsLength(8);
    propertyRow(form, label, control);
    connect(control, &QComboBox::currentIndexChanged, this, [this, write](int index) {
        if (!m_refreshing && index >= 0) mutate([&](BrushSettings &b) { write(b, index); });
    });
    m_refreshers.emplace_back([this, control, read] {
        const QSignalBlocker block(control); control->setCurrentIndex(qBound(0, int(read(m_canvas->brush)), control->count() - 1));
    });
    return control;
}

QComboBox *BrushSettingsPanel::inputControl(QFormLayout *form, const QString &label, const char *id,
    Reader read, Writer write, bool direction) {
    auto *control = new QComboBox; control->setObjectName(id); control->setAccessibleName(label);
    control->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); control->setMinimumContentsLength(8);
    for (int index = 0; index < controlNames.size(); ++index) {
        if (!direction && (index == int(BrushControl::InitialDirection) || index == int(BrushControl::Direction))) continue;
        control->addItem(controlNames[index], index);
    }
    control->setToolTip("Choose a pen input. Rotation requires a stylus that reports barrel rotation.");
    propertyRow(form, label, control);
    connect(control, &QComboBox::currentIndexChanged, this, [this, control, write](int index) {
        if (!m_refreshing && index >= 0) mutate([&](BrushSettings &b) { write(b, control->itemData(index).toInt()); });
    });
    m_refreshers.emplace_back([this, control, read] {
        const QSignalBlocker block(control); control->setCurrentIndex(control->findData(int(read(m_canvas->brush))));
    });
    return control;
}

void BrushSettingsPanel::dynamic(QFormLayout *form, const QString &label, const char *id,
    std::function<BrushDynamic &(BrushSettings &)> field, bool minimum, bool direction, bool framed) {
    QWidget *group = framed ? static_cast<QWidget *>(new QGroupBox(label)) : new QWidget;
    group->setAccessibleName(label + " dynamics");
    auto *fields = new QFormLayout(group);
    fields->setRowWrapPolicy(QFormLayout::WrapAllRows);
    if (framed) fields->setContentsMargins(4, 8, 4, 4); else fields->setContentsMargins(0, 0, 0, 0);
    fields->setHorizontalSpacing(4); fields->setVerticalSpacing(4);
    fields->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    const QByteArray prefix(id);
    group->setObjectName(prefix + "Dynamics");
    auto *jitter = number(fields, "Jitter", (prefix + "Jitter").constData(), 0, 100,
        [field](BrushSettings b) { return field(b).jitter; }, [field](BrushSettings &b, qreal n) { field(b).jitter = n; }, 100, 0, " %");
    auto *perStroke = new QToolButton; perStroke->setObjectName(prefix + "JitterPerStroke");
    perStroke->setIcon(appIcon("spline")); compactAction(perStroke);
    perStroke->setCheckable(true); perStroke->setAccessibleName(label + " jitter once per stroke");
    perStroke->setToolTip("Once per stroke: keep one random variation along the stroke. Off: vary every stamp.");
    qobject_cast<PropertyRow *>(jitter->parentWidget())->setAccessory(perStroke);
    connect(perStroke, &QToolButton::toggled, this, [this, field](bool checked) {
        if (!m_refreshing) mutate([field, checked](BrushSettings &b) { field(b).jitterPerStroke = checked; });
    });
    inputControl(fields, "Control", (prefix + "Control").constData(),
        [field, prefix](BrushSettings b) {
            const auto control = field(b).control;
            return int(prefix == "size" && control == BrushControl::Off && b.pressureSize ? BrushControl::Pressure : control);
        }, [field, prefix](BrushSettings &b, qreal n) {
            field(b).control = static_cast<BrushControl>(int(n));
            if (prefix == "size") b.pressureSize = field(b).control == BrushControl::Pressure;
        }, direction);
    auto *response = new BrushResponseWidget; response->setObjectName(prefix + "Response");
    response->setAccessibleName(label + " input response"); fields->addRow(response);
    connect(response, &BrushResponseWidget::responseEdited, this, [this, field](const BrushResponseWidget::Response &curve) {
        if (!m_refreshing) mutate([field, curve](BrushSettings &b) { field(b).response = curve; });
    });
    if (minimum) number(fields, "Minimum", (prefix + "Minimum").constData(), prefix == "roundness" ? 1 : 0, 100,
        [field](BrushSettings b) { return field(b).minimum; }, [field](BrushSettings &b, qreal n) { field(b).minimum = n; }, 100, 0, " %");
    auto *fade = number(fields, "Fade steps", (prefix + "Fade").constData(), 1, 10000,
        [field](BrushSettings b) { return field(b).fade; }, [field](BrushSettings &b, qreal n) { field(b).fade = qRound(n); });
    m_refreshers.emplace_back([this, prefix, field, fields, fade, response, perStroke] {
        auto b = m_canvas->brush; const auto &dynamics = field(b);
        setPropertyVisible(fields, fade, dynamics.control == BrushControl::Fade);
        const bool hasControl = dynamics.control != BrushControl::Off || (prefix == "size" && b.pressureSize);
        setPropertyVisible(fields, response, hasControl); response->setResponse(dynamics.response);
        const QSignalBlocker block(perStroke); perStroke->setChecked(dynamics.jitterPerStroke);
        perStroke->setEnabled(dynamics.jitter > 0);
    });
    form->addRow(group);
}

void BrushSettingsPanel::mutate(const Edit &edit) {
    auto brush = m_canvas->brush; edit(brush);
    if (brush.texture.enabled && brush.texture.pattern.isNull()) { brush.texture.pattern = pattern("Paper"); brush.texture.name = "Paper"; }
    m_canvas->setBrushSettings(brush);
}

void BrushSettingsPanel::setLibrary(BrushLibrary *library) {
    if (m_library == library) { refresh(); return; }
    if (m_library) disconnect(m_library, nullptr, this, nullptr);
    m_library = library;
    if (m_library) connect(m_library, &BrushLibrary::changed, this, &BrushSettingsPanel::refresh);
    refresh();
}

void BrushSettingsPanel::refreshTips() {
    for (bool dual : {false, true}) {
        auto *picker = dual ? m_dualPicker : m_tipPicker;
        if (!picker) continue;
        const QSignalBlocker block(picker); picker->clear(); picker->addItem("Round", "round");
        const auto &current = dual ? m_canvas->brush.dual.tipMask : m_canvas->brush.tipMask;
        const auto colorTip = dual ? QImage() : m_canvas->brush.colorTip;
        int selection = current.isNull() && colorTip.isNull() ? 0 : -1;
        if (m_library) for (const auto &preset : m_library->presets()) {
            if (preset.brush.tipMask.isNull() && (dual || preset.brush.colorTip.isNull())) continue;
            picker->addItem(preset.name, preset.id);
            if (selection < 0 && preset.brush.tipMask == current && (dual || preset.brush.colorTip == colorTip)) selection = picker->count() - 1;
        }
        if (selection < 0) {
            const auto title = dual && !m_canvas->brush.dual.name.isEmpty() ? m_canvas->brush.dual.name
                : !colorTip.isNull() ? QString("Current colored stamp") : QString("Current sampled tip");
            picker->addItem(title, "current"); selection = picker->count() - 1;
        }
        picker->setCurrentIndex(selection);
        picker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); picker->setMinimumContentsLength(8);
    }
}

void BrushSettingsPanel::refresh() {
    if (m_refreshing) return;
    m_refreshing = true;
    for (const auto &update : m_quickRefreshers) update();
    // Hidden advanced editors (including sampled-tip lists and texture previews)
    // do not need rebuilding for every quick adjustment while drawing.
    if (m_advancedInitialized && !m_advancedDialog->isVisible()) {
        m_refreshing = false;
        return;
    }
    for (const auto &update : m_refreshers) update();
    refreshTips();
    if (m_patternPicker) {
        const QSignalBlocker block(m_patternPicker); m_patternPicker->clear();
        const auto &texture = m_canvas->brush.texture;
        int selected = -1;
        for (const auto &name : patternNames) {
            auto *item = new QListWidgetItem(QIcon(QPixmap::fromImage(pattern(name))), QString(), m_patternPicker);
            item->setData(Qt::UserRole, name); item->setToolTip(name); item->setData(Qt::AccessibleTextRole, name);
            if (name == texture.name && texture.pattern == pattern(name)) selected = m_patternPicker->count() - 1;
        }
        if (selected < 0 && !texture.pattern.isNull()) {
            const QString label = texture.name.isEmpty() ? "Imported pattern" : texture.name;
            auto *item = new QListWidgetItem(QIcon(QPixmap::fromImage(texture.pattern)), QString(), m_patternPicker);
            item->setData(Qt::UserRole, "custom"); item->setToolTip(label); item->setData(Qt::AccessibleTextRole, label); selected = m_patternPicker->count() - 1;
        }
        m_patternPicker->setCurrentRow(selected < 0 ? 0 : selected);
    }
    m_advancedInitialized = true;
    m_refreshing = false;
}

bool BrushSettingsPanel::importTexture(const QString &path, QString *error) {
    if (error) error->clear();
    const QFileInfo info(path);
    if (!info.isFile() || info.size() <= 0 || info.size() > 16 * 1024 * 1024) {
        if (error) *error = "Choose an image file up to 16 MiB."; return false;
    }
    QImageReader reader(path); const auto size = reader.size();
    if (!size.isValid() || size.width() > 2048 || size.height() > 2048) {
        if (error) *error = "Texture images must be at most 2048 × 2048 pixels."; return false;
    }
    const auto image = reader.read();
    if (image.isNull()) { if (error) *error = "Could not read the texture image: " + reader.errorString(); return false; }
    if (image.size() != size) { if (error) *error = "The decoded texture dimensions do not match the image header."; return false; }
    QImage mask(size, QImage::Format_Grayscale8);
    if (mask.isNull()) { if (error) *error = "Not enough memory for the texture."; return false; }
    for (int y = 0; y < size.height(); ++y) for (int x = 0; x < size.width(); ++x) {
        const auto pixel = image.pixelColor(x, y);
        mask.scanLine(y)[x] = uchar(qGray(pixel.rgb()) * pixel.alpha() / 255);
    }
    const auto name = info.completeBaseName().left(120);
    mutate([mask, name](BrushSettings &b) { b.texture.enabled = true; b.texture.pattern = mask; b.texture.name = name; });
    return true;
}

void BrushSettingsPanel::chooseColor(bool background) {
    const auto chosen = QColorDialog::getColor(background ? m_canvas->brush.backgroundColor : m_canvas->brush.color,
        m_advancedDialog, background ? "Background color" : "Foreground color");
    if (chosen.isValid()) mutate([chosen, background](BrushSettings &b) { if (background) b.backgroundColor = chosen; else b.color = chosen; });
}

#undef FIELD
} // namespace toon
