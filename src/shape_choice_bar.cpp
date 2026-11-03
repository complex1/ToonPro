#include "shape_choice_bar.h"

#include "icons.h"
#include "shape_recognition.h"
#include "ui_theme.h"

#include <QPainter>
#include <QToolButton>
#include <QTransform>
#include <algorithm>
#include <cmath>

namespace toon {
namespace {
constexpr int buttonWidth = 44;
constexpr int buttonHeight = 40;
constexpr int margin = 5;
constexpr int spacing = 4;

QPainterPath previewPath(const QPainterPath &path, const QRectF &area) {
    if (path.isEmpty()) return {};
    const auto bounds = path.boundingRect();
    if (!std::isfinite(bounds.left()) || !std::isfinite(bounds.top())
        || !std::isfinite(bounds.width()) || !std::isfinite(bounds.height())) return {};
    const qreal scale = std::min(area.width() / std::max(1., bounds.width()),
                                 area.height() / std::max(1., bounds.height()));
    QTransform transform;
    transform.translate(area.center().x(), area.center().y());
    transform.scale(scale, scale);
    transform.translate(-bounds.center().x(), -bounds.center().y());
    return transform.map(path);
}

class ShapePreviewButton final : public QToolButton {
public:
    ShapePreviewButton(const QPainterPath &path, bool original, QWidget *parent)
        : QToolButton(parent), m_original(original),
          m_path(previewPath(path, QRectF(8, 8, 28, original ? 20 : 24))),
          m_originalIcon(original ? appIcon("undo-2") : QIcon()) {
        setFixedSize(buttonWidth, buttonHeight);
        setCheckable(true);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        QToolButton::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(isEnabled() ? ui::text : ui::disabled),
                            1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(m_path);
        if (m_original) m_originalIcon.paint(&painter, QRect(29, 26, 12, 12));
    }

private:
    const bool m_original;
    const QPainterPath m_path;
    const QIcon m_originalIcon;
};
}

ShapeChoiceBar::ShapeChoiceBar(QWidget *parent) : QFrame(parent) {
    setObjectName("shapeChoiceBar");
    setAccessibleName("Recognized shape choices");
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::ArrowCursor);
    setAttribute(Qt::WA_StyledBackground);
    // Keep ignored tablet events inside the bar so Qt can synthesize ordinary
    // button clicks instead of propagating them into the drawing canvas.
    setAttribute(Qt::WA_NoMousePropagation);
    setStyleSheet(QStringLiteral(
        "QFrame#shapeChoiceBar { background: %1; border: 1px solid %2; border-radius: 6px; }"
        "QFrame#shapeChoiceBar QToolButton { padding: 0px; min-width: 0px; min-height: 0px; }")
        .arg(ui::panel, ui::border));
    arrangeButtons();
    hide();
}

ShapeChoiceBar::~ShapeChoiceBar() {
    // Connections never retain the candidate vector or a drawing document.
    // Disconnect before QWidget tears down the buttons and this value's storage.
    for (auto *button : m_buttons) QObject::disconnect(button, nullptr, this, nullptr);
}

void ShapeChoiceBar::clearButtons() {
    for (auto *button : m_buttons) {
        QObject::disconnect(button, nullptr, this, nullptr);
        delete button;
    }
    m_buttons.clear();
}

void ShapeChoiceBar::setChoices(const QVector<RecognizedShape> &candidates,
                                const QPainterPath &original) {
    clearButtons();
    auto addChoice = [this](const QPainterPath &path, const QString &name, int index) {
        auto *button = new ShapePreviewButton(path, index == -1, this);
        button->setObjectName(index == -1 ? "shapeChoiceOriginal" : QString("shapeChoice%1").arg(index));
        button->setToolTip(name);
        button->setAccessibleName(name);
        button->setProperty("shapeChoiceIndex", index);
        button->setChecked(index == 0);
        m_buttons.append(button);
        connect(button, &QToolButton::clicked, this, [this, button, index] {
            for (auto *choice : m_buttons) choice->setChecked(choice == button);
            // The owner may hide, reset or destroy this bar from the callback.
            emit choiceActivated(index);
        });
        button->show();
    };
    addChoice(original, tr("Original stroke"), -1);
    for (int i = 0; i < candidates.size(); ++i)
        addChoice(candidates[i].path, candidates[i].name, i);

    auto *cancel = new QToolButton(this);
    cancel->setObjectName("shapeChoiceCancel");
    cancel->setIcon(appIcon("x"));
    cancel->setIconSize({18, 18});
    cancel->setFixedSize(buttonWidth, buttonHeight);
    cancel->setFocusPolicy(Qt::NoFocus);
    cancel->setToolTip(tr("Cancel stroke (Esc)"));
    cancel->setAccessibleName(tr("Cancel stroke"));
    m_buttons.append(cancel);
    connect(cancel, &QToolButton::clicked, this, &ShapeChoiceBar::cancelled);
    cancel->show();
    arrangeButtons();
}

void ShapeChoiceBar::setAvailableWidth(int width) {
    m_availableWidth = std::max(buttonWidth + 2 * margin, width);
    arrangeButtons();
}

void ShapeChoiceBar::arrangeButtons() {
    const int count = int(m_buttons.size());
    const int columns = std::max(1, std::min(count,
        (m_availableWidth - 2 * margin + spacing) / (buttonWidth + spacing)));
    const int rows = std::max(1, (count + columns - 1) / columns);
    for (int i = 0; i < count; ++i)
        m_buttons[i]->move(margin + (i % columns) * (buttonWidth + spacing),
                           margin + (i / columns) * (buttonHeight + spacing));
    setFixedSize(2 * margin + columns * buttonWidth + (columns - 1) * spacing,
                 2 * margin + rows * buttonHeight + (rows - 1) * spacing);
}

} // namespace toon
