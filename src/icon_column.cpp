#include "icon_column.h"

#include <QAction>

namespace toon {

IconColumn::IconColumn(const QString &title, QWidget *content, QWidget *parent)
    : QToolBar(title, parent)
{
    setProperty("panelType", "iconColumn");
    setAccessibleName(title + " column");
    setAccessibleDescription("Fixed-width icon column. Drag its top handle to the left or right side of the workspace.");
    setToolTip("Drag " + title + " column to the left or right side");
    setOrientation(Qt::Vertical);
    setAllowedAreas(Qt::LeftToolBarArea | Qt::RightToolBarArea);
    setMovable(true);
    setFloatable(false);
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setIconSize(QSize(ui::metrics::primaryIcon, ui::metrics::primaryIcon));
    setFixedWidth(Width);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    // Override the normal toolbar's padding without affecting child controls.
    setStyleSheet("QToolBar[panelType=\"iconColumn\"] { padding: 2px; spacing: 0px; }");
    content->setFixedWidth(ContentWidth);
    content->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    addWidget(content);
    toggleViewAction()->setToolTip("Show or hide " + title + " column");

    // A restored workspace must never turn an icon column into a horizontal bar.
    connect(this, &QToolBar::orientationChanged, this, [this](Qt::Orientation orientation) {
        if (orientation != Qt::Vertical) setOrientation(Qt::Vertical);
    });
}

} // namespace toon
