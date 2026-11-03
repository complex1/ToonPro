#pragma once

#include <QToolBar>
#include "ui_metrics.h"

namespace toon {

// A full-height workspace column. Normal panels use QDockWidget so that their
// width, tabs and floating layout remain independent from these narrow rails.
class IconColumn final : public QToolBar {
    Q_OBJECT
public:
    static constexpr int Width = ui::metrics::railWidth;
    static constexpr int ContentWidth = Width - 4;

    explicit IconColumn(const QString &title, QWidget *content, QWidget *parent = nullptr);
};

} // namespace toon
