#pragma once
#include "brush_settings.h"
#include <QWidget>
class QListWidget;
class QLabel;
class QTabBar;
class QToolButton;
namespace toon {
class TipAnchorWidget;
class ValueSlider;
class BrushTipSetWidget : public QWidget {
    Q_OBJECT
public:
    explicit BrushTipSetWidget(QWidget *parent = nullptr);
    ~BrushTipSetWidget() override;
    void setBrush(const BrushSettings &brush);
    bool addImages(const QStringList &paths, QString *error = nullptr);
signals:
    void brushEdited(const toon::BrushSettings &brush);
private:
    void refresh();
    void removeTip();
    void moveTip(int delta);
    BrushSettings m_brush;
    QListWidget *m_list;
    QLabel *m_count;
    QTabBar *m_order;
    TipAnchorWidget *m_anchor;
    ValueSlider *m_weight;
    QToolButton *m_add, *m_remove, *m_left, *m_right;
    bool m_refreshing = false;
};
}
