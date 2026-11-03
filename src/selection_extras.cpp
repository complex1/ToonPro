#include "selection_extras.h"
#include "ui_metrics.h"
#include "canvas.h"
#include "raster_tools.h"
#include "value_slider.h"
#include "property_row.h"
#include "icons.h"
#include <QComboBox>
#include <QLabel>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QStackedLayout>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace toon {
namespace {
class SelectionDetailLayout final : public QStackedLayout {
public:
    explicit SelectionDetailLayout(QWidget *parent) : QStackedLayout(parent) { setSizeConstraint(QLayout::SetNoConstraint); }
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
}
SelectionExtras::SelectionExtras(Canvas *canvas, QWidget *parent) : QWidget(parent), m_canvas(canvas) {
    setObjectName("selectionExtras");
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(4); layout->setAlignment(Qt::AlignTop);
    auto *tabs = new QTabBar; tabs->setObjectName("selectionDetailTabs"); tabs->setAccessibleName("Selection settings categories");
    tabs->setProperty("compactDrawingTabs", true); tabs->setIconSize({ui::metrics::tabIcon, ui::metrics::tabIcon});
    tabs->setExpanding(true); tabs->setElideMode(Qt::ElideRight); tabs->setUsesScrollButtons(false); tabs->setFocusPolicy(Qt::StrongFocus);
    tabs->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    tabs->addTab(appIcon("square-dashed"), "Selection"); tabs->addTab(appIcon("eye"), "Display");
    tabs->setTabToolTip(0, "Selection edge and saved selections"); tabs->setAccessibleTabName(0, "Selection settings");
    tabs->setTabToolTip(1, "Selection border and outside shading"); tabs->setAccessibleTabName(1, "Selection display");
    layout->addWidget(tabs);
    auto *pages = new QWidget; pages->setObjectName("selectionDetailPages");
    auto *pagesLayout = new SelectionDetailLayout(pages);
    auto policy = QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); policy.setHeightForWidth(true);
    pages->setSizePolicy(policy); setSizePolicy(policy); layout->addWidget(pages);
    auto *selectionPage = new QWidget; auto *selectionLayout = new QVBoxLayout(selectionPage);
    selectionLayout->setContentsMargins(0, 0, 0, 0); selectionLayout->setSpacing(ui::metrics::controlGap); selectionLayout->setAlignment(Qt::AlignTop);
    auto *displayPage = new QWidget; auto *displayLayout = new QVBoxLayout(displayPage);
    displayLayout->setContentsMargins(0, 0, 0, 0); displayLayout->setSpacing(ui::metrics::controlGap); displayLayout->setAlignment(Qt::AlignTop);
    pagesLayout->addWidget(selectionPage); pagesLayout->addWidget(displayPage);
    connect(tabs, &QTabBar::currentChanged, this, [this, pages, pagesLayout](int index) {
        pagesLayout->setCurrentIndex(index); pages->updateGeometry(); updateGeometry();
    });
    m_feather = new ValueSlider("Selection feather", 0, 100, 0, 0, " px"); m_feather->setObjectName("selectionFeather"); m_feather->setToolTip("Soften the selection edge; drag the slider or enter a radius");
    auto *featherRow = new PropertyRow("Feather", m_feather); featherRow->setObjectName("selectionFeatherRow"); selectionLayout->addWidget(featherRow);
    auto action = [](const QString &icon, const QString &name, const char *id) {
        auto *button = new QToolButton; button->setIcon(appIcon(icon));
        button->setProperty("compactDrawingAction", true);
        button->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon});
        button->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
        button->setObjectName(id); button->setToolTip(name); button->setAccessibleName(name); return button;
    };
    auto *savedRow = new QHBoxLayout; savedRow->setSpacing(4);
    m_saved = new QComboBox; m_saved->setObjectName("savedSelectionList"); m_saved->setAccessibleName("Saved selections");
    m_saved->setProperty("compactDrawingField", true);
    m_saved->setToolTip("Choose a saved selection, or type a name to save the current selection");
    m_saved->setEditable(true); m_saved->setInsertPolicy(QComboBox::NoInsert); m_saved->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_saved->setMinimumWidth(0); m_saved->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); m_saved->setMinimumContentsLength(3);
    m_saved->lineEdit()->setPlaceholderText("Selection name");
    savedRow->addWidget(m_saved, 1);
    m_save = action("save", "Save or replace named selection", "saveNamedSelection"); savedRow->addWidget(m_save);
    m_load = action("folder-open", "Load named selection", "loadNamedSelection"); savedRow->addWidget(m_load);
    m_delete = action("trash", "Delete named selection", "deleteNamedSelection"); savedRow->addWidget(m_delete); selectionLayout->addLayout(savedRow);
    auto *displayActions = new QWidget; auto *display = new QHBoxLayout(displayActions); display->setContentsMargins(0, 0, 0, 0); display->setSpacing(4);
    m_hatching = action("eye", "Shade outside the selection", "selectionHatching"); m_hatching->setCheckable(true); display->addWidget(m_hatching);
    m_animation = action("circle-dashed", "Animate the selection border", "selectionAnimation"); m_animation->setCheckable(true); display->addWidget(m_animation);
    m_shade = new ValueSlider("Selection shade opacity", 0, 100, 100, 0, "%"); m_shade->setObjectName("selectionShadeOpacity"); m_shade->setToolTip("Outside shade strength; drag the slider or enter a percentage");
    auto *shadeRow = new PropertyRow("", m_shade); shadeRow->setObjectName("selectionShadeRow"); shadeRow->setAccessibleName("Selection display");
    shadeRow->setLabelWidth(0); shadeRow->setAccessory(displayActions); displayLayout->addWidget(shadeRow);
    connect(m_feather, &ValueSlider::valueChanged, this, [this](qreal radius) { RasterTools::featherSelection(m_canvas->document(), radius); });
    connect(m_save, &QToolButton::clicked, this, [this] { RasterTools::saveSelection(m_canvas->document(), m_saved->currentText()); });
    connect(m_load, &QToolButton::clicked, this, [this] { RasterTools::loadSelection(m_canvas->document(), m_saved->currentData().toString()); });
    connect(m_saved, &QComboBox::activated, this, [this] { RasterTools::loadSelection(m_canvas->document(), m_saved->currentData().toString()); });
    connect(m_delete, &QToolButton::clicked, this, [this] { RasterTools::deleteSelection(m_canvas->document(), m_saved->currentData().toString()); });
    connect(m_hatching, &QToolButton::toggled, m_canvas, &Canvas::setSelectionFocusOverlay);
    connect(m_animation, &QToolButton::toggled, m_canvas, &Canvas::setSelectionAnimated);
    connect(m_shade, &ValueSlider::valueChanged, this, [this](qreal value) { m_canvas->setSelectionShadeOpacity(value / 100); });
    connect(m_canvas, &Canvas::selectionUiChanged, this, &SelectionExtras::refresh);
    connect(m_canvas->document(), &Document::rasterSelectionChanged, this, &SelectionExtras::refresh);
    connect(m_canvas->document(), &Document::changed, this, &SelectionExtras::refresh);
    connect(m_canvas->document(), &Document::transformChanged, this, &SelectionExtras::refresh);
    refresh();
}
void SelectionExtras::refresh() {
    auto *document = m_canvas->document(); const bool open = document->isOpen();
    const bool editable = open && !document->editingGestureActive() && !document->layerPropertyEditActive();
    m_feather->setEnabled(editable && document->rasterSelection().active);
    m_feather->setValueSilently(document->rasterSelection().featherRadius);
    m_save->setEnabled(editable && document->rasterSelection().active && !document->rasterSelection().path.isEmpty());
    const QString currentId = m_saved->currentData().toString(), currentText = m_saved->currentText();
    QStringList ids, names;
    if (open) for (const auto &selection : document->activeShot().savedSelections) { ids.append(selection.id); names.append(selection.name); }
    bool same = m_saved->count() == ids.size();
    for (int i = 0; i < m_saved->count() && same; ++i) same = m_saved->itemData(i).toString() == ids[i] && m_saved->itemText(i) == names[i];
    if (!same) {
        const QSignalBlocker block(m_saved); m_saved->clear();
        for (int i = 0; i < ids.size(); ++i) m_saved->addItem(names[i], ids[i]);
        int selected = ids.indexOf(currentId);
        if (selected < 0) selected = names.indexOf(currentText);
        m_saved->setCurrentIndex(selected);
        if (selected < 0) m_saved->setEditText(currentText);
    }
    m_saved->setEnabled(editable); m_load->setEnabled(editable && !ids.isEmpty()); m_delete->setEnabled(editable && !ids.isEmpty());
    const QSignalBlocker hatchBlock(m_hatching), animationBlock(m_animation);
    m_hatching->setChecked(m_canvas->selectionFocusOverlay()); m_animation->setChecked(m_canvas->selectionAnimated());
    m_shade->setValueSilently(m_canvas->selectionShadeOpacity() * 100); m_shade->setEnabled(m_canvas->selectionFocusOverlay());
}
}
