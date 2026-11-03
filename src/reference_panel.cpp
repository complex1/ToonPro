#include "reference_panel.h"
#include "character_color_panel.h"
#include "character_model_panel.h"
#include "light_table.h"
#include "document.h"
#include "icons.h"
#include "property_row.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include "value_slider.h"
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <cmath>

namespace toon {
namespace {
class ReferenceCollectionTabs final : public QTabBar {
public:
    using QTabBar::QTabBar;
    QSize minimumSizeHint() const override {
        auto size = QTabBar::minimumSizeHint();
        size.setWidth(count() * ui::metrics::actionTarget); return size;
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
        const QStringList names{"Images", "Model Sheets", "Color Models", "Light Table"};
        int fullWidth = 0;
        for (int index = 0; index < count(); ++index)
            fullWidth += QTabBar::tabSizeHint(index).width()
                + fontMetrics().size(Qt::TextShowMnemonic, names[index]).width()
                - fontMetrics().size(Qt::TextShowMnemonic, tabText(index)).width();
        m_updating = true;
        for (int index = 0; index < count(); ++index)
            setTabText(index, width() >= fullWidth ? names[index] : QString());
        m_updating = false;
    }
    bool m_updating = false;
};
class ReferencePages final : public QStackedWidget {
public:
    using QStackedWidget::QStackedWidget;
    QSize minimumSizeHint() const override { return currentWidget() ? currentWidget()->minimumSizeHint() : QSize(); }
    QSize sizeHint() const override { return currentWidget() ? currentWidget()->sizeHint() : QSize(); }
};
class ReferenceCaption final : public QLabel {
public:
    explicit ReferenceCaption(QWidget *parent) : QLabel(parent) {
        setTextFormat(Qt::PlainText);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        updateHeight();
    }
protected:
    void changeEvent(QEvent *event) override {
        QLabel::changeEvent(event);
        if (event->type() == QEvent::FontChange) updateHeight();
    }
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(contentsRect(), Qt::AlignLeading | Qt::AlignVCenter,
                         fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()));
    }
private:
    void updateHeight() { setFixedHeight(qMax(ui::metrics::actionTarget, fontMetrics().height() + 2)); }
};

class ReferenceThumbnailDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        auto thumbnail = option;
        initStyleOption(&thumbnail, index);
        // Keep names in the model for keyboard navigation and accessibility,
        // while the strip itself shows only the image and selection state.
        thumbnail.text.clear();
        thumbnail.features &= ~QStyleOptionViewItem::HasDisplay;
        thumbnail.decorationAlignment = Qt::AlignCenter;
        const auto *widget = thumbnail.widget;
        (widget ? widget->style() : QApplication::style())->drawControl(QStyle::CE_ItemViewItem, &thumbnail, painter, widget);
    }
};

QIcon referenceThumbnail(const QImage &image) {
    QIcon icon;
    for (const qreal ratio : {1., 2., 3.}) {
        QPixmap pixmap(QSize(40, 40) * ratio); pixmap.setDevicePixelRatio(ratio); pixmap.fill(Qt::transparent);
        const auto size = image.size().scaled(QSize(40, 40), Qt::KeepAspectRatio);
        QPainter painter(&pixmap); painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(QRectF(QPointF((40 - size.width()) / 2.0, (40 - size.height()) / 2.0), size), image);
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}
QString referenceCaption(const ReferenceImage &reference) {
    return QString("%1 · %2 × %3").arg(reference.name).arg(reference.image.width()).arg(reference.image.height());
}
}
ReferenceView::ReferenceView(QWidget *parent) : QWidget(parent) {
    setObjectName("referenceView"); setAccessibleName("Reference image viewer");
    setMinimumSize(128, 96); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus); setCursor(Qt::OpenHandCursor);
    setToolTip("Drag to pan; scroll to zoom; Alt-click to sample a color");
}
void ReferenceView::setImage(const QImage &image) {
    if (m_image.cacheKey() == image.cacheKey()) return;
    m_image = image; m_dragging = false; m_samplingGesture = false; setCursor(m_sampling ? Qt::CrossCursor : Qt::OpenHandCursor); fitImage();
}
void ReferenceView::setEmptyText(const QString &text) { m_emptyText = text; update(); }
QTransform ReferenceView::imageTransform() const {
    QTransform t;
    t.translate(width() / 2.0, height() / 2.0);
    t.scale(m_zoom, m_zoom); t.translate(-m_center.x(), -m_center.y());
    return t;
}
void ReferenceView::fitImage() {
    m_zoom = m_image.isNull() ? 1 : qBound(.01, qMin((width() - 20.0) / m_image.width(), (height() - 20.0) / m_image.height()), 16.0);
    m_center = QPointF(m_image.width() / 2.0, m_image.height() / 2.0);
    m_fit = true; update(); emit viewChanged();
}
void ReferenceView::zoomAt(qreal zoom, QPointF position) {
    if (m_image.isNull() || !std::isfinite(zoom)) return;
    const auto before = imageTransform().inverted().map(position);
    m_zoom = qBound(.01, zoom, 16.0);
    m_center += before - imageTransform().inverted().map(position);
    m_fit = false; update(); emit viewChanged();
}
void ReferenceView::setZoom(qreal zoom) { zoomAt(zoom, QRectF(rect()).center()); }
void ReferenceView::paintEvent(QPaintEvent *) {
    QPainter p(this); p.fillRect(rect(), QColor(ui::canvas));
    if (m_image.isNull()) {
        p.setPen(QColor(ui::muted));
        p.drawText(rect().adjusted(ui::metrics::sectionGap, ui::metrics::sectionGap,
                                  -ui::metrics::sectionGap, -ui::metrics::sectionGap),
                   Qt::AlignCenter | Qt::TextWordWrap, m_emptyText);
        return;
    }
    p.setTransform(imageTransform());
    p.fillRect(m_image.rect(), Qt::white);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QPointF(), m_image);
}
void ReferenceView::resizeEvent(QResizeEvent *) { if (m_fit) fitImage(); }
void ReferenceView::setSampling(bool enabled) {
    m_sampling = enabled; m_dragging = false; m_samplingGesture = false;
    setCursor(m_sampling ? Qt::CrossCursor : Qt::OpenHandCursor);
}
void ReferenceView::sampleAt(QPointF position) {
    if (m_image.isNull()) return;
    const auto point = imageTransform().inverted().map(position);
    const QPoint pixel(int(std::floor(point.x())), int(std::floor(point.y())));
    if (m_image.rect().contains(pixel)) emit colorSampled(m_image.pixelColor(pixel));
}
void ReferenceView::mousePressEvent(QMouseEvent *event) {
    if (m_image.isNull() || (event->button() != Qt::LeftButton && event->button() != Qt::MiddleButton)) return;
    if (event->button() == Qt::LeftButton && (m_sampling || event->modifiers().testFlag(Qt::AltModifier))) {
        m_samplingGesture = true; m_dragging = false; setFocus(); setCursor(Qt::CrossCursor); sampleAt(event->position()); event->accept(); return;
    }
    m_dragging = true; m_last = event->position(); setFocus(); setCursor(Qt::ClosedHandCursor); event->accept();
}
void ReferenceView::mouseMoveEvent(QMouseEvent *event) {
    if (m_samplingGesture) { sampleAt(event->position()); event->accept(); return; }
    if (!m_dragging) return;
    m_center += (m_last - event->position()) / m_zoom; m_last = event->position();
    m_fit = false; update(); emit viewChanged();
}
void ReferenceView::mouseReleaseEvent(QMouseEvent *) { m_dragging = false; m_samplingGesture = false; setCursor(m_sampling ? Qt::CrossCursor : Qt::OpenHandCursor); }
void ReferenceView::wheelEvent(QWheelEvent *event) {
    const auto delta = event->angleDelta().y() ? event->angleDelta().y() : event->pixelDelta().y();
    zoomAt(m_zoom * std::pow(1.0015, delta), event->position()); event->accept();
}
void ReferenceView::focusOutEvent(QFocusEvent *event) {
    m_dragging = false; m_samplingGesture = false; setCursor(m_sampling ? Qt::CrossCursor : Qt::OpenHandCursor); QWidget::focusOutEvent(event);
}

ReferencePanel::ReferencePanel(Document *document, QWidget *parent) : QWidget(parent), m_document(document) {
    setObjectName("referenceContent"); setMinimumWidth(220);
    setProperty("compactDrawingPanel", true);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(0);
    auto *tabs = new ReferenceCollectionTabs(this); tabs->setObjectName("referenceTabs");
    tabs->setUsesScrollButtons(false); tabs->setElideMode(Qt::ElideNone);
    tabs->setAccessibleName("Reference collections"); tabs->setProperty("compactDrawingTabs", true);
    tabs->setExpanding(true); tabs->setFocusPolicy(Qt::StrongFocus);
    tabs->setIconSize({ui::metrics::tabIcon, ui::metrics::tabIcon});
    tabs->addTab(appIcon("image"), "Images"); tabs->addTab(appIcon("toon-character"), "Model Sheets");
    tabs->addTab(appIcon("palette"), "Color Models");
    tabs->setTabToolTip(0, "Reference images for the current shot"); tabs->setAccessibleTabName(0, "Shot reference images");
    tabs->setTabToolTip(1, "Character model sheets shared across every shot in this project");
    tabs->setAccessibleTabName(1, "Character model sheets");
    tabs->setTabToolTip(2, "Character color models shared across every shot in this project");
    tabs->setAccessibleTabName(2, "Character color models"); outer->addWidget(tabs);
    auto *pages = new ReferencePages(this); pages->setObjectName("referencePages"); outer->addWidget(pages, 1);
    auto *images = new QWidget(pages); images->setObjectName("shotReferencesPage"); pages->addWidget(images);
    auto *layout = new QVBoxLayout(images);
    layout->setContentsMargins(ui::metrics::panelMargin, ui::metrics::panelMargin,
                              ui::metrics::panelMargin, ui::metrics::panelMargin);
    layout->setSpacing(ui::metrics::controlGap);
    auto *actions = new QHBoxLayout; actions->setSpacing(ui::metrics::controlGap);
    auto action = [this](const QString &label, const QString &icon, const QString &id) {
        auto *button = new QToolButton(this); button->setObjectName(id);
        button->setIcon(appIcon(icon)); button->setToolTip(label); button->setAccessibleName(label);
        button->setProperty("compactDrawingAction", true);
        button->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon});
        button->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
        return button;
    };
    m_add = action("Import reference image", "image", "importReference");
    m_add->setToolTip("Import PNG, JPEG or BMP; embed a copy up to 2048 px per side");
    m_remove = action("Remove reference", "trash", "removeReference");
    m_name = new ReferenceCaption(this); m_name->setObjectName("referenceName");
    actions->addWidget(m_add); actions->addWidget(m_name, 1); actions->addWidget(m_remove);
    m_fit = action("Fit reference", "scan", "fitReference");
    layout->addLayout(actions);
    m_list = new QListWidget(this); m_list->setObjectName("referenceList"); m_list->setAccessibleName("Reference images");
    m_list->setViewMode(QListView::IconMode); m_list->setFlow(QListView::LeftToRight); m_list->setWrapping(false);
    m_list->setMovement(QListView::Static); m_list->setIconSize(QSize(40, 40)); m_list->setGridSize(QSize(48, 48));
    m_list->setItemDelegate(new ReferenceThumbnailDelegate(m_list));
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setFixedHeight(68); layout->addWidget(m_list);
    m_view = new ReferenceView(this); layout->addWidget(m_view, 1);
    connect(m_view, &ReferenceView::colorSampled, this, &ReferencePanel::colorSampled);
    m_zoom = new ValueSlider("Reference zoom", 1, 1600, 100, 1, " %", this);
    m_zoom->setObjectName("referenceZoom"); m_zoom->setSingleStep(10);
    auto *zoomRow = new PropertyRow("Zoom", m_zoom, this); zoomRow->setObjectName("referenceZoomRow");
    zoomRow->setAccessory(m_fit); layout->addWidget(zoomRow);
    connect(m_add, &QToolButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, "Import reference image", {}, "Images (*.png *.jpg *.jpeg *.bmp)");
        if (path.isEmpty()) return;
        QString error;
        if (!importImage(path, &error)) QMessageBox::warning(this, "Could not import reference", error);
    });
    connect(m_remove, &QToolButton::clicked, this, [this] { m_document->removeReference(m_selectedId); });
    connect(m_list, &QListWidget::currentRowChanged, this, &ReferencePanel::selectReference);
    connect(m_zoom, &ValueSlider::valueChanged, this, [this](double value) { m_view->setZoom(value / 100); });
    connect(m_fit, &QToolButton::clicked, m_view, &ReferenceView::fitImage);
    const auto syncZoom = [this] {
        if (m_zoomEditing) return;
        const QSignalBlocker block(m_zoom); m_zoom->setValue(m_view->zoom() * 100);
    };
    connect(m_view, &ReferenceView::viewChanged, this, syncZoom);
    connect(m_zoom, &ValueSlider::editingStarted, this, [this] { m_zoomEditing = true; });
    connect(m_zoom, &ValueSlider::editingFinished, this, [this, syncZoom] { m_zoomEditing = false; syncZoom(); });
    connect(m_zoom, &ValueSlider::editingCancelled, this, [this, syncZoom] { m_zoomEditing = false; syncZoom(); });
    connect(m_document, &Document::selectionChanged, this, &ReferencePanel::refresh);
    // Keep separate viewers so switching collections retains each navigation state.
    m_characters = new CharacterModelPanel(document, pages); pages->addWidget(m_characters);
    connect(m_characters, &CharacterModelPanel::colorSampled, this, &ReferencePanel::colorSampled);
    m_characterColors = new CharacterColorPanel(document, pages); pages->addWidget(m_characterColors);
    connect(m_characterColors, &CharacterColorPanel::colorSelected, this, &ReferencePanel::colorSampled);
    connect(m_characters, &CharacterModelPanel::characterSelected, m_characterColors, &CharacterColorPanel::selectCharacter);
    connect(m_characterColors, &CharacterColorPanel::characterSelected, m_characters, &CharacterModelPanel::selectCharacter);
    connect(tabs, &QTabBar::currentChanged, pages, [pages](int index) {
        pages->setCurrentIndex(index); pages->updateGeometry();
    });
    refresh();
}
void ReferencePanel::setCurrentColor(const QColor &color) { m_characterColors->setCurrentColor(color); }
void ReferencePanel::attachCanvas(Canvas *canvas) {
    if (!canvas || m_lightTable) return;
    auto *tabs = findChild<QTabBar *>("referenceTabs");
    auto *pages = findChild<QStackedWidget *>("referencePages");
    m_lightTable = new LightTablePanel(canvas, pages);
    pages->addWidget(m_lightTable);
    const int index = tabs->addTab(appIcon("toon-light-table"), "Light Table");
    tabs->setTabToolTip(index, "Pin drawings over the canvas; shift, rotate and scale references without changing their source");
    tabs->setAccessibleTabName(index, "Light Table and Shift-and-Trace");
    connect(tabs, &QTabBar::currentChanged, m_lightTable, [this, index](int current) {
        if (current != index) m_lightTable->setEditing(false);
    });
}
bool ReferencePanel::finishEditing() { return m_characters->finishEditing() && m_characterColors->finishEditing(); }
void ReferencePanel::setSampling(bool enabled) { m_view->setSampling(enabled); m_characters->setSampling(enabled); }
bool readReferenceImage(const QString &path, QImage *output, QString *outputName, QString *error) {
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    const QFileInfo file(path);
    if (!file.isFile() || file.size() > 32LL * 1024 * 1024) return fail("Choose an image file no larger than 32 MB.");
    QImageReader reader(path); reader.setAutoTransform(true); reader.setDecideFormatFromContent(true);
    const auto format = reader.format().toLower();
    if (format != "png" && format != "jpeg" && format != "jpg" && format != "bmp")
        return fail("Choose a PNG, JPEG or BMP reference image.");
    const auto sourceSize = reader.size();
    if (!sourceSize.isValid() || sourceSize.width() > 16384 || sourceSize.height() > 16384
        || qint64(sourceSize.width()) * sourceSize.height() > 16000000)
        return fail("Reference source images must contain at most 16 million pixels, with no side above 16,384 pixels.");
    if (sourceSize.width() > 2048 || sourceSize.height() > 2048)
        reader.setScaledSize(sourceSize.scaled(QSize(2048, 2048), Qt::KeepAspectRatio));
    auto image = reader.read();
    if (image.isNull()) return fail("Could not read the reference image: " + reader.errorString());
    // Some image handlers cannot downscale during decoding.
    if (image.width() > 2048 || image.height() > 2048)
        image = image.scaled(QSize(2048, 2048), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const auto base = file.completeBaseName().trimmed();
    if (outputName) *outputName = base.isEmpty() ? QString("Reference") : base.left(100);
    if (output) *output = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    return true;
}
bool ReferencePanel::importImage(const QString &path, QString *error) {
    if (!m_document->isOpen()) { if (error) *error = "Open a project before importing a reference."; return false; }
    QImage image; QString name;
    if (!readReferenceImage(path, &image, &name, error)) return false;
    if (!m_document->addReference(name, image, error)) return false;
    m_list->setCurrentRow(m_list->count() - 1);
    return true;
}
void ReferencePanel::refresh() {
    const bool open = m_document->isOpen(); m_add->setEnabled(open);
    QStringList state;
    if (open) for (const auto &reference : m_document->activeShot().references)
        state << reference.id << reference.name << QString::number(reference.image.cacheKey());
    if (m_refreshed && state == m_referenceState) return;
    m_refreshed = true; m_referenceState = state;
    const auto selected = m_selectedId;
    const QSignalBlocker block(m_list);
    m_list->clear();
    int selectedRow = 0;
    if (open) for (const auto &reference : m_document->activeShot().references) {
        auto *item = new QListWidgetItem(referenceThumbnail(reference.image), reference.name, m_list);
        item->setData(Qt::UserRole, reference.id); item->setToolTip(referenceCaption(reference));
        item->setData(Qt::AccessibleTextRole, reference.name);
        if (reference.id == selected) selectedRow = m_list->count() - 1;
    }
    if (m_list->count()) m_list->setCurrentRow(selectedRow);
    // A single image already fills the viewer. Reserve the strip for choosing
    // between references; keep its model so selection, Undo and names survive.
    m_list->setVisible(m_list->count() > 1);
    selectReference();
}
void ReferencePanel::selectReference() {
    const auto *item = m_list->currentItem();
    const auto selected = item ? item->data(Qt::UserRole).toString() : QString();
    if (selected != m_selectedId) m_zoom->commitEditing();
    m_selectedId = selected;
    m_remove->setEnabled(item); m_zoom->setEnabled(item); m_fit->setEnabled(item);
    if (m_document->isOpen()) for (const auto &reference : m_document->activeShot().references)
        if (reference.id == m_selectedId) {
            m_view->setImage(reference.image);
            const auto caption = referenceCaption(reference);
            m_name->setText(caption); m_name->setToolTip(caption); m_name->setAccessibleName(caption);
            return;
        }
    m_name->clear(); m_name->setToolTip({}); m_name->setAccessibleName({}); m_view->setImage({});
}
} // namespace toon
