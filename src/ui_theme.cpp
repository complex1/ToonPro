#include "ui_theme.h"
#include "ui_metrics.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QEvent>
#include <QPalette>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTabBar>
#include <QTabletEvent>
#include <QToolButton>
#include <QToolTip>
#include <utility>

namespace toon {
namespace {

QString plainLabel(QString value)
{
    value.replace("&&", "\x01");
    value.remove('&');
    value.replace('\x01', '&');
    return value.trimmed();
}

void completeWidgetHelp(QWidget *widget)
{
    if (auto *tabs = qobject_cast<QTabBar *>(widget)) {
        for (int index = 0; index < tabs->count(); ++index) {
            auto label = plainLabel(tabs->tabText(index));
            if (label.isEmpty()) label = tabs->tabToolTip(index).section('\n', 0, 0).trimmed();
            if (label.isEmpty()) continue;
            if (tabs->tabToolTip(index).isEmpty()) tabs->setTabToolTip(index, label);
            if (tabs->accessibleTabName(index).isEmpty()) tabs->setAccessibleTabName(index, label);
        }
    }
    auto *button = qobject_cast<QAbstractButton *>(widget);
    if (!button) return;
    QString label = plainLabel(button->text());
    if (auto *tool = qobject_cast<QToolButton *>(button)) {
        if (auto *action = tool->defaultAction()) {
            label = plainLabel(action->text());
            // Qt's built-in clear action intentionally has no text. Give the
            // generated search-field button the same discoverable help as ours.
            if (label.isEmpty() && action->objectName() == "_q_qlineeditclearaction")
                label = "Clear text";
            if (button->toolTip().isEmpty()) button->setToolTip(action->toolTip());
        }
    }
    if (label.isEmpty()) {
        if (button->objectName() == "ScrollLeftButton") label = "Previous tabs";
        else if (button->objectName() == "ScrollRightButton") label = "Next tabs";
        else if (button->objectName() == "qt_toolbar_ext_button") label = "More toolbar actions";
        else if (button->objectName() == "qt_menubar_ext_button") label = "More menus";
        else if (button->objectName() == "qt_dockwidget_closebutton" ||
                 button->objectName() == "qt_dockwidget_floatbutton") {
            // Qt may create or repolish title-bar controls after the panel is
            // first assembled. Complete their help through the shared path too.
            if (auto *dock = qobject_cast<QDockWidget *>(button->parentWidget())) {
                auto title = dock->property("panelLabel").toString();
                if (title.isEmpty()) title = plainLabel(dock->windowTitle());
                if (!title.isEmpty())
                    label = button->objectName() == "qt_dockwidget_closebutton"
                        ? QString("Hide %1 panel").arg(title)
                        : QString("Float or dock %1 panel").arg(title);
            }
        }
    }
    if (button->toolTip().isEmpty() && !label.isEmpty()) button->setToolTip(label);
    if (button->accessibleName().isEmpty()) {
        if (label.isEmpty()) label = button->toolTip();
        if (!label.isEmpty()) button->setAccessibleName(label);
    }
}

class WorkspaceStyle final : public QProxyStyle {
public:
    WorkspaceStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {}
    int styleHint(StyleHint hint, const QStyleOption *option = nullptr,
                  const QWidget *widget = nullptr, QStyleHintReturn *returnData = nullptr) const override
    {
        if (hint == SH_ToolTip_WakeUpDelay) return ui::tooltipDelayMs;
        // Qt's fast reshow grace period otherwise makes neighboring icons open
        // instantly. Every new hover should receive the same deliberate delay.
        if (hint == SH_ToolTip_FallAsleepDelay) return 0;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
    int pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr,
                    const QWidget *widget = nullptr) const override
    {
        if (metric == PM_TabBarScrollButtonWidth)
            return widget && (widget->property("compactDrawingTabs").toBool() ||
                              widget->property("compactDockTabs").toBool())
                ? ui::metrics::actionTarget : 30;
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
    void polish(QWidget *widget) override
    {
        QProxyStyle::polish(widget);
        completeWidgetHelp(widget);
    }
};

class WorkspaceHelpFilter final : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::Polish:
        case QEvent::Show:
        case QEvent::ActionChanged:
        case QEvent::ToolTipChange:
            if (!m_completing) {
                m_completing = true;
                completeWidgetHelp(qobject_cast<QWidget *>(watched));
                m_completing = false;
            }
            break;
        case QEvent::TabletPress:
            m_tabletDown = true;
            QToolTip::hideText();
            break;
        case QEvent::TabletMove:
            // Some tablet drivers lose the release when the pen crosses a dock
            // or leaves range. Hover must restore help without another click.
            if (static_cast<QTabletEvent *>(event)->buttons() == Qt::NoButton)
                m_tabletDown = false;
            break;
        case QEvent::TabletRelease:
        case QEvent::TabletLeaveProximity:
        case QEvent::ApplicationDeactivate:
            m_tabletDown = false;
            break;
        case QEvent::MouseButtonPress:
        case QEvent::KeyPress:
        case QEvent::Wheel:
            QToolTip::hideText();
            break;
        case QEvent::ToolTip:
            completeWidgetHelp(qobject_cast<QWidget *>(watched));
            if (m_tabletDown || QApplication::mouseButtons() != Qt::NoButton) {
                QToolTip::hideText();
                event->ignore();
                return true;
            }
            break;
        default: break;
        }
        return false;
    }
private:
    bool m_completing = false;
    bool m_tabletDown = false;
};
}

void applyTheme()
{
    // Stylesheet-backed controls must receive parent/application font changes
    // as ordinary widgets do, including the artist's larger-font preferences.
    QApplication::setAttribute(Qt::AA_UseStyleSheetPropagationInWidgetStyles, true);
    QApplication::setStyle(new WorkspaceStyle);
    // Include buttons in this application's keyboard traversal even when the
    // macOS default only includes text/list controls. Individual focus policies
    // still keep pen clicks from taking focus; no system preference is changed.
    QApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(ui::panel));
    palette.setColor(QPalette::WindowText, QColor(ui::text));
    palette.setColor(QPalette::Base, QColor(ui::base));
    palette.setColor(QPalette::AlternateBase, QColor(ui::raised));
    palette.setColor(QPalette::Text, QColor(ui::text));
    palette.setColor(QPalette::PlaceholderText, QColor(ui::muted));
    palette.setColor(QPalette::Button, QColor(ui::raised));
    palette.setColor(QPalette::ButtonText, QColor(ui::text));
    palette.setColor(QPalette::Highlight, QColor(ui::selected));
    palette.setColor(QPalette::HighlightedText, QColor(ui::text));
    palette.setColor(QPalette::Accent, QColor(ui::accent));
    palette.setColor(QPalette::Link, QColor(ui::accentHover));
    palette.setColor(QPalette::Light, QColor(ui::border));
    palette.setColor(QPalette::Mid, QColor(ui::border));
    palette.setColor(QPalette::Dark, QColor(ui::canvas));
    palette.setColor(QPalette::ToolTipBase, QColor(ui::raised));
    palette.setColor(QPalette::ToolTipText, QColor(ui::text));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, QColor(ui::disabled));
    qApp->setPalette(palette);

    QString css = QStringLiteral(R"(
        QMainWindow::separator { background: @base; width: 4px; height: 4px; }
        QToolBar { border: none; background: @panel; spacing: 2px; padding: 3px; }
        QToolBar::separator { background: @border; width: 1px; margin: 6px 5px; }
        QToolButton { color: @text; background: transparent; border: 1px solid transparent;
                      padding: 3px; border-radius: 3px; min-width: 20px; min-height: 20px; }
        QToolButton:hover { background: @hover; }
        QToolButton:pressed { background: @border; }
        QToolButton:checked { background: @selected; border-color: @disabled; }
        QToolButton:focus { border-color: @focus; }
        QToolButton:disabled { color: @disabled; }
        QToolButton::menu-indicator { image: none; }
        /* Qt positions these using PM_TabBarScrollButtonWidth. Generic button
           padding must not enlarge them beyond their reserved tab-bar slots. */
        QTabBar QToolButton#ScrollLeftButton, QTabBar QToolButton#ScrollRightButton {
            padding: 0; min-width: 0; min-height: 0;
        }
        QPushButton { color: @text; background: @raised; padding: 6px 12px;
                      border: 1px solid @border; border-radius: 5px; min-height: 20px; }
        QPushButton:hover { background: @hover; }
        QPushButton:checked { background: @selected; border-color: @accent; }
        QPushButton:default, QPushButton#submitProject { background: @selected; border-color: @accent; }
        QPushButton:focus { border-color: @focus; }
        QPushButton:disabled { color: @disabled; background: @panel; }
        QSlider::groove:horizontal { height: 3px; background: @canvas; border-radius: 1px; }
        QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
        QSlider::handle:horizontal { width: 10px; margin: -4px 0; background: @accent; border: 1px solid @accent; border-radius: 5px; }
        QSlider::handle:horizontal:hover { background: @focus; }
        QSlider::handle:horizontal:focus { border: 2px solid @focus; }
        QSlider::handle:horizontal:disabled { background: @disabled; border-color: @disabled; }
        QDockWidget::title { padding: 4px 8px; background: @raised; }
        QDockWidget::close-button, QDockWidget::float-button { padding: 3px; border-radius: 3px; }
        QDockWidget::close-button:hover, QDockWidget::float-button:hover { background: @hover; }
        QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { color: @text; background: @base;
            border: 1px solid @border; border-radius: 3px; min-height: 20px; padding: 3px 6px; }
        QDoubleSpinBox#valueSliderNumber { padding: 3px 4px; }
        /* The spin box owns the border and padding; its internal text editor
           must not add a second text-field box to the row height. */
        QDoubleSpinBox#valueSliderNumber QLineEdit {
            border: none; padding: 0; min-height: 0; background: transparent;
        }
        QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: @focus; }
        QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { color: @disabled; }
        QComboBox QAbstractItemView { background: @raised; color: @text; selection-background-color: @selected; }
        QTabWidget::pane { border: 0; }
        QTabBar { background: @base; }
        QTabBar::tab { color: @muted; background: @base; border: 1px solid transparent;
            border-bottom: 2px solid transparent; min-width: 22px; min-height: 24px; padding: 5px 7px; }
        QTabBar::tab:hover { color: @text; background: @hover; }
        QTabBar::tab:selected { color: @text; background: @panel; border-bottom-color: @accent; }
        QTabBar::tab:focus { border-color: @focus; }
        QListView, QTreeView { background: @base; border: 1px solid @border; border-radius: 4px; outline: none; }
        QListView::item, QTreeView::item { padding: 3px; border: 1px solid transparent; }
        QListView::item:hover, QTreeView::item:hover { background: @hover; }
        QListView::item:selected, QTreeView::item:selected { background: @selected; border-color: transparent; }
        QListView::item:focus, QTreeView::item:focus { border-color: @focus; }
        QListWidget#availableTools::indicator {
            width: 14px; height: 14px; border: 1px solid @muted;
            border-radius: 2px; background: @base;
        }
        QListWidget#availableTools::indicator:checked {
            background: @accent; image: url(:/icons/lucide/check.svg);
        }
        QMenu { background: @raised; color: @text; border: 1px solid @border; padding: 5px; }
        QMenu::item { padding: 6px 22px 6px 10px; border-radius: 3px; }
        QMenu::item:selected { background: @selected; }
        QMenu::item:disabled { color: @disabled; }
        QMenu::separator { background: @border; height: 1px; margin: 5px; }
        QScrollBar:vertical { background: @base; width: 9px; margin: 0; }
        QScrollBar::handle:vertical { background: @border; min-height: 24px; border-radius: 4px; }
        QScrollBar::handle:vertical:hover { background: @disabled; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }
        QToolTip { color: @text; background: @raised; border: 1px solid @border;
                   padding: 5px 8px; border-radius: 4px; }
        QLabel#muted { color: @muted; }
        QLabel#sectionLabel { color: @muted; margin-top: 14px; font-size: 11px; font-weight: bold; }
        QLabel#dialogHeading { font-size: 25px; font-weight: bold; }
        QLabel#validation { color: #e8b395; }
        QStatusBar { color: @muted; background: @base; }
        QFrame#selectionContextBar { background: @panel; border: 1px solid @border; border-radius: 8px; }
        QFrame#selectionContextBar QToolButton { min-width: 18px; min-height: 18px; padding: 0; }
        QFrame#selectionContextBar QToolButton#contextDragGrip { min-width: 0; padding: 0; }
        QFrame#selectionContextBar QToolButton#contextTransform,
        QFrame#selectionContextBar QToolButton#contextApply,
        QToolButton[primary="true"] { background: @selected; border-color: @accent; }
        /* Explicit component roles keep compact drawing styles out of modal
           editors and unrelated panels that happen to share QObject parents. */
        QToolButton[compactDrawingAction="true"] {
            padding: 0; min-width: @actionInnerpx; min-height: @actionInnerpx;
        }
        QPushButton[compactDrawingButton="true"] {
            min-height: @buttonInnerpx; padding: 3px 8px; border-radius: 3px;
        }
        QLineEdit[compactDrawingField="true"], QComboBox[compactDrawingField="true"] {
            padding: 3px @controlGappx;
        }
        QDockWidget[compactDrawingPanel="true"]::title { padding: 2px 6px; }
        QTabBar[compactDrawingTabs="true"]::tab,
        QTabBar[compactDockTabs="true"]::tab {
            min-height: @tabInnerpx; min-width: 20px; padding: 3px 5px;
        }
        QTabBar[compactDrawingTabs="true"]::tab:selected,
        QTabBar[compactDockTabs="true"]::tab:selected {
            background: @selected; border-bottom-color: @accent;
        }
        QTabBar[compactDrawingTabs="true"] QToolButton#ScrollLeftButton,
        QTabBar[compactDrawingTabs="true"] QToolButton#ScrollRightButton,
        QTabBar[compactDockTabs="true"] QToolButton#ScrollLeftButton,
        QTabBar[compactDockTabs="true"] QToolButton#ScrollRightButton {
            padding: 0; min-width: 0; min-height: 0;
        }
    )");
    css.replace("@actionInner", QString::number(ui::metrics::actionTarget - 2));
    css.replace("@buttonInner", QString::number(ui::metrics::actionTarget - 8));
    css.replace("@controlGap", QString::number(ui::metrics::controlGap));
    css.replace("@tabInner", QString::number(ui::metrics::tabHeight - 9));
    for (const auto &token : {std::pair{"accentHover", ui::accentHover}, {"selected", ui::selected},
            {"disabled", ui::disabled}, {"canvas", ui::canvas}, {"panel", ui::panel}, {"base", ui::base},
            {"raised", ui::raised}, {"hover", ui::hover}, {"border", ui::border}, {"text", ui::text},
            {"muted", ui::muted}, {"accent", ui::accent}, {"focus", ui::focus}})
        css.replace(QStringLiteral("@") + QLatin1String(token.first), QLatin1String(token.second));
    qApp->setStyleSheet(css);
    if (!qApp->findChild<QObject *>(QStringLiteral("workspaceHelpFilter"))) {
        auto *filter = new WorkspaceHelpFilter(qApp);
        filter->setObjectName(QStringLiteral("workspaceHelpFilter"));
        qApp->installEventFilter(filter);
    }
}
}
