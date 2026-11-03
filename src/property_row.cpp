#include "property_row.h"
#include "ui_metrics.h"
#include <QLabel>
#include <QLayout>
#include <QStyle>
#include <QVector>
#include <algorithm>

namespace toon {
namespace {
constexpr int horizontalGap = ui::metrics::controlGap;
constexpr int verticalGap = ui::metrics::controlGap;
constexpr int minimumControlWidth = ui::metrics::editorMinimumWidth;
constexpr int minimumRowHeight = ui::metrics::propertyHeight;

QSize editorSize(const QLayoutItem *item) {
    return item->sizeHint().expandedTo(item->minimumSize()).boundedTo(item->maximumSize());
}
}

class PropertyRowLayout final : public QLayout {
public:
    PropertyRowLayout(QWidget *parent, QLabel *label, QWidget *control)
        : QLayout(parent), m_label(label) {
        setContentsMargins(0, 0, 0, 0);
        setSpacing(0);
        addWidget(label);
        addWidget(control);
    }
    ~PropertyRowLayout() override {
        while (auto *item = takeAt(0)) delete item;
    }
    void addItem(QLayoutItem *item) override { m_items.append(item); }
    int count() const override { return m_items.size(); }
    QLayoutItem *itemAt(int index) const override { return m_items.value(index); }
    QLayoutItem *takeAt(int index) override {
        if (index < 0 || index >= count()) return nullptr;
        return m_items.takeAt(index);
    }
    Qt::Orientations expandingDirections() const override { return Qt::Horizontal; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override {
        if (count() < 2) return 0;
        const auto margins = contentsMargins();
        width = qMax(0, width - margins.left() - margins.right());
        return contentHeight(width) + margins.top() + margins.bottom();
    }
    QSize sizeHint() const override {
        if (count() < 2) return {};
        const int width = labelBandWidth() + horizontalGap
            + qMax(controlMinimumWidth(), editorSize(m_items[1]).width());
        const auto margins = contentsMargins();
        return QSize(width + margins.left() + margins.right(),
                     contentHeight(width) + margins.top() + margins.bottom());
    }
    QSize minimumSize() const override {
        if (count() < 2) return {};
        const auto margins = contentsMargins();
        // The parent chooses the height with heightForWidth. A long label must
        // never turn into a large minimum dock width.
        return QSize(controlMinimumWidth() + margins.left() + margins.right(),
                     controlHeight() + margins.top() + margins.bottom());
    }
    void setGeometry(const QRect &rect) override {
        QLayout::setGeometry(rect);
        if (count() < 2) return;
        const QRect area = contentsRect();
        const bool inlineRow = fitsInline(area.width());
        const int bandWidth = inlineRow ? labelBandWidth() : area.width();
        const auto accessory = accessorySize();
        const int labelWidth = qMax(0, bandWidth - (hasAccessory() ? accessory.width() + horizontalGap : 0));
        const int labelHeight = labelHeightForWidth(labelWidth);
        const int bandHeight = qMax(labelHeight, accessory.height());
        const int height = inlineRow ? qMax(controlHeight(), bandHeight) : bandHeight;
        const int top = area.top() + (inlineRow ? qMax(0, (area.height() - height) / 2) : 0);
        place(m_items[0], QRect(area.left(), top, labelWidth, height), area);
        if (hasAccessory()) {
            place(m_items[2], QRect(area.left() + (m_label->text().isEmpty() ? 0 : bandWidth - accessory.width()),
                  top + (height - accessory.height()) / 2, accessory.width(), accessory.height()), area);
        }
        if (inlineRow) {
            place(m_items[1], QRect(area.left() + bandWidth + horizontalGap,
                  top + (height - controlHeight()) / 2,
                  qMax(0, area.width() - bandWidth - horizontalGap), controlHeight()), area);
        } else {
            place(m_items[1], QRect(area.left(), top + bandHeight + verticalGap,
                                  area.width(), controlHeight()), area);
        }
    }
    void setLabelWidth(int width) {
        m_labelWidth = qMax(0, width);
        invalidate();
    }
    void setAccessory(QWidget *accessory) {
        if (count() > 2 && m_items[2]->widget() == accessory) return;
        if (count() > 2) {
            auto *item = takeAt(2);
            if (item->widget()) item->widget()->hide();
            delete item;
        }
        if (accessory) {
            addWidget(accessory);
            accessory->show();
        }
        invalidate();
    }

private:
    bool hasAccessory() const { return count() > 2 && !m_items[2]->isEmpty(); }
    QSize accessorySize() const { return hasAccessory() ? editorSize(m_items[2]) : QSize(0, 0); }
    int labelBandWidth() const {
        // A custom channel selector can supply the complete label. Do not
        // reserve an invisible label or gap before that accessory.
        if (m_label->text().isEmpty() && hasAccessory())
            return qMax(m_labelWidth, accessorySize().width());
        int naturalWidth = 0;
        for (const auto &line : m_label->text().split('\n'))
            naturalWidth = qMax(naturalWidth, m_label->fontMetrics().horizontalAdvance(line));
        // Allow glyph bearings without truncating a full-width label.
        naturalWidth += 2;
        if (hasAccessory()) naturalWidth += horizontalGap + accessorySize().width();
        return qMax(m_labelWidth, naturalWidth);
    }
    int controlMinimumWidth() const {
        return qMax(minimumControlWidth, m_items[1]->minimumSize().width());
    }
    int controlHeight() const { return qMax(minimumRowHeight, editorSize(m_items[1]).height()); }
    int labelHeightForWidth(int width) const {
        return qMax(m_label->fontMetrics().height(), m_label->heightForWidth(qMax(1, width)));
    }
    bool fitsInline(int width) const {
        return width >= labelBandWidth() + horizontalGap + controlMinimumWidth();
    }
    int contentHeight(int width) const {
        const int bandWidth = fitsInline(width) ? labelBandWidth() : width;
        const auto accessory = accessorySize();
        const int textWidth = qMax(1, bandWidth - (hasAccessory() ? accessory.width() + horizontalGap : 0));
        const int bandHeight = qMax(labelHeightForWidth(textWidth), accessory.height());
        return fitsInline(width) ? qMax(controlHeight(), bandHeight)
                                 : bandHeight + verticalGap + controlHeight();
    }
    void place(QLayoutItem *item, const QRect &logical, const QRect &area) {
        item->setGeometry(QStyle::visualRect(parentWidget()->layoutDirection(), area, logical));
    }
    QVector<QLayoutItem *> m_items;
    QLabel *m_label;
    int m_labelWidth = ui::metrics::labelWidth;
};

PropertyRow::PropertyRow(const QString &label, QWidget *control, QWidget *parent)
    : QWidget(parent), m_label(new QLabel(label, this)), m_control(control) {
    Q_ASSERT(control);
    setObjectName("propertyRow");
    setAccessibleName(label);
    auto policy = QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    m_label->setObjectName("propertyRowLabel");
    m_label->setTextFormat(Qt::PlainText);
    m_label->setWordWrap(true);
    m_label->setAlignment(Qt::AlignLeading | Qt::AlignVCenter);
    m_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_label->setBuddy(control);
    if (control->accessibleName().isEmpty()) control->setAccessibleName(label);
    setFocusProxy(control);
    m_rowLayout = new PropertyRowLayout(this, m_label, control);
}
QWidget *PropertyRow::control() const { return m_control; }
QLabel *PropertyRow::labelWidget() const { return m_label; }
void PropertyRow::setLabelWidth(int width) {
    m_rowLayout->setLabelWidth(width);
    updateGeometry();
}
void PropertyRow::setAccessory(QWidget *accessory) {
    m_rowLayout->setAccessory(accessory);
    updateGeometry();
}
} // namespace toon
