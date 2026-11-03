#pragma once

#include <QFrame>
#include <QPainterPath>
#include <QVector>

class QToolButton;

namespace toon {

struct RecognizedShape;

// A transient canvas child. The owner positions/shows it after pen-up and
// resolves the pending stroke when a choice is activated or cancelled.
class ShapeChoiceBar : public QFrame {
    Q_OBJECT
public:
    explicit ShapeChoiceBar(QWidget *parent = nullptr);
    ~ShapeChoiceBar() override;

    void setChoices(const QVector<RecognizedShape> &candidates,
                    const QPainterPath &original);
    void setAvailableWidth(int width);

signals:
    // -1 restores the original stroke; nonnegative indices match candidates.
    void choiceActivated(int index);
    void cancelled();

private:
    void clearButtons();
    void arrangeButtons();
    QVector<QToolButton *> m_buttons;
    int m_availableWidth = 320;
};

} // namespace toon
