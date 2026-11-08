#pragma once
#include "drawing_guide.h"
#include <QWidget>
#include <functional>

class QLabel;
class QLineEdit;
class QTabBar;
class QTabWidget;
class QToolButton;
class QTreeWidget;

namespace toon {
class Canvas;
class ValueSlider;
class GuidePanel final : public QWidget {
    Q_OBJECT
public:
    explicit GuidePanel(Canvas *canvas, QWidget *parent = nullptr);
signals:
    void detectPerspectiveRequested();
private:
    void refresh();
    void refreshEnabled();
    void editSelected(const std::function<void(DrawingGuide &)> &edit);
    void editSettings(const std::function<void(GuideSettings &)> &edit);
    void loadSet();
    void saveSet();
    Canvas *m_canvas;
    QWidget *m_content;
    QTabBar *m_types;
    QLabel *m_typeName;
    QLabel *m_placement;
    QTreeWidget *m_list;
    QToolButton *m_add;
    QToolButton *m_cancel;
    QToolButton *m_duplicate;
    QToolButton *m_remove;
    QToolButton *m_load;
    QToolButton *m_save;
    QToolButton *m_detect;
    QTabWidget *m_properties;
    QWidget *m_selected;
    QLineEdit *m_name;
    ValueSlider *m_density;
    QToolButton *m_vertical;
    QToolButton *m_limited;
    QToolButton *m_customColor;
    QToolButton *m_color;
    QLineEdit *m_colorHex;
    ValueSlider *m_opacity;
    QWidget *m_colorControls;
    QWidget *m_areaControls;
    ValueSlider *m_area[4];
    QToolButton *m_visible;
    QToolButton *m_snapping;
    QToolButton *m_handles;
    QToolButton *m_globalColor;
    QLineEdit *m_globalHex;
    ValueSlider *m_globalOpacity;
    bool m_refreshing = false;
};
} // namespace toon
