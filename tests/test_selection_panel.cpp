#include "brush_settings_panel.h"
#include "canvas.h"
#include "drawing_panels.h"
#include "layer_panel.h"
#include "main_window.h"
#include "selection_panel.h"
#include "property_row.h"
#include "value_slider.h"
#include "ui_metrics.h"

#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QSlider>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>
#include <memory>

using namespace toon;
namespace {
Project paintedProject() {
    auto project = Project::create();
    QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::transparent);
    QPainter painter(&tile); painter.fillRect(QRect(44, 44, 40, 30), QColor("#5daca0")); painter.end();
    project.shots.first().layers.first().tiles[{1, 1}] = tile;
    return project;
}
QPainterPath paintedSelection() { QPainterPath path; path.addRect(QRectF(300, 300, 40, 30)); return path; }
ValueSlider *number(QWidget &panel, const char *id) {
    return panel.findChild<ValueSlider *>(id);
}
}
class SelectionPanelTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }

    void toolsSwitchPropertiesAndKeepBrushPanelDiscoverable() {
        Canvas canvas; canvas.setProject(paintedProject());
        std::unique_ptr<QWidget> properties(drawingPanel(&canvas, "propertiesPanel"));
        std::unique_ptr<QWidget> tools(drawingPanel(&canvas, "toolsPanel"));
        auto *stack = qobject_cast<QStackedWidget *>(properties.get()); QVERIFY(stack);
        auto *brush = properties->findChild<BrushSettingsPanel *>(); QVERIFY(brush);
        QCOMPARE(stack->currentWidget(), brush);
        for (const auto *id : {"toolBrush", "toolEraser", "toolRectangle", "toolLasso", "toolTransform", "toolHand"}) {
            auto *button = tools->findChild<QToolButton *>(id); QVERIFY(button); QVERIFY(!button->icon().isNull());
        }
        tools->findChild<QToolButton *>("toolRectangle")->click();
        QCOMPARE(canvas.tool(), Canvas::Tool::RectangleSelect);
        QVERIFY(qobject_cast<SelectionPanel *>(stack->currentWidget()));
        tools->findChild<QToolButton *>("toolLasso")->click();
        QCOMPARE(canvas.tool(), Canvas::Tool::LassoSelect);
        QVERIFY(qobject_cast<SelectionPanel *>(stack->currentWidget()));
        tools->findChild<QToolButton *>("toolTransform")->click();
        QVERIFY(canvas.document()->transformActive());
        tools->findChild<QToolButton *>("toolHand")->click();
        QVERIFY(!canvas.document()->transformActive());
        QCOMPARE(stack->currentWidget(), brush);
        QVERIFY(!tools->findChild<QToolButton *>("toolTransform")->isChecked());
    }

    void selectionActionsAreTransientAndReportEmptySelection() {
        Canvas canvas; SelectionPanel panel(&canvas);
        QVERIFY(!panel.findChild<QToolButton *>("selectAllPixels")->isEnabled());
        canvas.setProject(paintedProject()); auto *doc = canvas.document();
        const auto original = doc->project().toJson();
        panel.findChild<QToolButton *>("selectAllPixels")->click();
        QVERIFY(doc->rasterSelection().active);
        QVERIFY(doc->rasterSelection().path.contains(QPointF(320, 315)));
        panel.findChild<QToolButton *>("invertPixelSelection")->click();
        QVERIFY(doc->rasterSelection().active); QVERIFY(doc->rasterSelection().path.isEmpty());
        QCOMPARE(panel.findChild<QLabel *>("pixelSelectionStatus")->text(), QString("Empty selection"));
        panel.findChild<QToolButton *>("deselectPixels")->click();
        QVERIFY(!doc->rasterSelection().active);
        QCOMPARE(panel.findChild<QLabel *>("pixelSelectionStatus")->text(), QString("No selection"));
        doc->setRasterSelection(paintedSelection());
        QVERIFY(panel.findChild<QLabel *>("pixelSelectionStatus")->text().contains("40 × 30"));
        QCOMPARE(doc->project().toJson(), original); QCOMPARE(doc->history()->count(), 0);
        QVERIFY(doc->history()->isClean());
    }

    void transformControlsPreviewApplyOneUndoAndCancel() {
        Canvas canvas; canvas.setProject(paintedProject());
        auto *doc = canvas.document(); doc->setRasterSelection(paintedSelection());
        SelectionPanel panel(&canvas); LayerPanel layers(doc);
        const auto original = doc->project().shots.first().layers.first().tiles;
        panel.findChild<QPushButton *>("beginPixelTransform")->click();
        QVERIFY(doc->transformActive());
        QVERIFY(!layers.findChild<QTreeWidget *>("layerTree")->isEnabled());
        QVERIFY(!panel.findChild<QToolButton *>("selectAllPixels")->isEnabled());
        number(panel, "transformX")->setValue(12.5);
        number(panel, "transformY")->setValue(-3);
        number(panel, "transformWidth")->setValue(150);
        QCOMPARE(doc->transformParameters().translation, QPointF(12.5, -3));
        QCOMPARE(doc->transformParameters().scaleX, 1.5); QCOMPARE(doc->transformParameters().scaleY, 1.5);
        QCOMPARE(number(panel, "transformHeight")->value(), 150.0);
        panel.findChild<QToolButton *>("transformAspectLink")->click();
        number(panel, "transformHeight")->setValue(75);
        number(panel, "transformRotation")->setValue(30);
        QCOMPARE(doc->transformParameters().scaleX, 1.5); QCOMPARE(doc->transformParameters().scaleY, .75);
        QCOMPARE(doc->transformParameters().rotation, 30.0);
        QCOMPARE(doc->project().shots.first().layers.first().tiles, original); QCOMPARE(doc->history()->count(), 0);
        QVERIFY(doc->displayProject().shots.first().layers.first().tiles != original);
        panel.findChild<QPushButton *>("applyPixelTransform")->click();
        QVERIFY(!doc->transformActive()); QCOMPARE(doc->history()->count(), 1);
        QVERIFY(layers.findChild<QTreeWidget *>("layerTree")->isEnabled());
        const auto applied = doc->project().shots.first().layers.first().tiles; QVERIFY(applied != original);
        doc->history()->undo(); QCOMPARE(doc->project().shots.first().layers.first().tiles, original);
        QCOMPARE(doc->rasterSelection().path, paintedSelection());
        doc->history()->redo(); QCOMPARE(doc->project().shots.first().layers.first().tiles, applied);
        panel.findChild<QPushButton *>("beginPixelTransform")->click();
        QVERIFY(doc->transformActive()); number(panel, "transformX")->setValue(40);
        panel.findChild<QPushButton *>("cancelPixelTransform")->click();
        QVERIFY(!doc->transformActive()); QCOMPARE(doc->project().shots.first().layers.first().tiles, applied);
        QCOMPARE(doc->history()->count(), 1);
        QVERIFY(layers.findChild<QTreeWidget *>("layerTree")->isEnabled());
    }

    void invalidLinkedScaleRestoresLastAcceptedNumbers() {
        Canvas canvas; canvas.setProject(paintedProject()); SelectionPanel panel(&canvas);
        panel.findChild<QPushButton *>("beginPixelTransform")->click();
        QVERIFY(canvas.document()->transformActive());
        auto *link = panel.findChild<QToolButton *>("transformAspectLink");
        link->click(); number(panel, "transformHeight")->setValue(1600); link->click();
        QCOMPARE(canvas.document()->transformParameters().scaleY, 16.0);
        number(panel, "transformWidth")->setValue(1600); // Linked height would exceed the engine's 16× limit.
        QCOMPARE(canvas.document()->transformParameters().scaleX, 1.0);
        QCOMPARE(canvas.document()->transformParameters().scaleY, 16.0);
        QCOMPARE(number(panel, "transformWidth")->value(), 100.0);
        QCOMPARE(number(panel, "transformHeight")->value(), 1600.0);
        QCOMPARE(canvas.document()->history()->count(), 0);
        canvas.document()->cancelTransform();
    }

    void sliderAndExactEntryShareLiveTransformPreview() {
        Canvas canvas; canvas.setProject(paintedProject()); SelectionPanel panel(&canvas);
        panel.resize(240, 620); panel.show();
        QTest::qWait(20);
        panel.findChild<QPushButton *>("beginPixelTransform")->click();
        auto *doc = canvas.document(); QVERIFY(doc->transformActive());
        for (const auto *id : {"transformX", "transformY", "transformWidth", "transformHeight", "transformRotation"}) {
            auto *control = number(panel, id); QVERIFY2(control, id);
            QVERIFY(control->findChild<QSlider *>("valueSliderTrack"));
            auto *field = control->findChild<QDoubleSpinBox *>("valueSliderNumber"); QVERIFY(field);
            QVERIFY(field->width() <= 80);
        }
        auto *width = number(panel, "transformWidth");
        auto *track = width->findChild<QSlider *>("valueSliderTrack");
        QCOMPARE(track->maximum(), 4000);
        QCOMPARE(width->findChild<QDoubleSpinBox *>()->maximum(), 1600.0);
        auto *x = number(panel, "transformX");
        QCOMPARE(x->findChild<QSlider *>()->maximum(), qRound(doc->drawingBounds().width() * 10));
        QCOMPARE(x->findChild<QDoubleSpinBox *>()->maximum(), 32768.0);
        track->setSliderDown(true); track->setValue(1250);
        QCOMPARE(doc->transformParameters().scaleX, 1.25);
        QCOMPARE(doc->transformParameters().scaleY, 1.25);
        QCOMPARE(number(panel, "transformHeight")->value(), 125.0);
        track->setValue(1500); track->setSliderDown(false);
        QCOMPARE(doc->history()->count(), 0);
        auto *rotation = number(panel, "transformRotation");
        auto *field = rotation->findChild<QDoubleSpinBox *>("valueSliderNumber");
        panel.findChild<QScrollArea *>("selectionPropertiesScroll")->ensureWidgetVisible(field);
        field->setFocus(); QTRY_VERIFY(field->hasFocus());
        field->selectAll(); QTest::keyClicks(field, "12.5");
        QTest::keyClick(field, Qt::Key_Tab);
        QCOMPARE(doc->transformParameters().rotation, 12.5);
        QCOMPARE(rotation->findChild<QSlider *>("valueSliderTrack")->value(), 125);
        panel.findChild<QPushButton *>("applyPixelTransform")->click();
        QCOMPARE(doc->history()->count(), 1); QVERIFY(!doc->transformActive());
        doc->history()->undo();
        QCOMPARE(doc->project().shots.first().layers.first().tiles, paintedProject().shots.first().layers.first().tiles);
    }

    void narrowPropertiesFit_data() {
        QTest::addColumn<int>("width"); QTest::addColumn<int>("height");
        QTest::newRow("220-tall") << 220 << 480;
        QTest::newRow("220-short") << 220 << 200;
        QTest::newRow("240-tall") << 240 << 480;
        QTest::newRow("260-tall") << 260 << 480;
        QTest::newRow("240-short") << 240 << 200;
        QTest::newRow("260-short") << 260 << 200;
    }
    void narrowPropertiesFit() {
        QFETCH(int, width); QFETCH(int, height);
        Canvas canvas; canvas.setProject(paintedProject());
        canvas.document()->setRasterSelection(paintedSelection()); canvas.setTool(Canvas::Tool::Transform);
        QVERIFY(canvas.document()->transformActive());
        std::unique_ptr<QWidget> wrapper(drawingPanel(&canvas, "propertiesPanel"));
        wrapper->resize(width, height); wrapper->show(); QTest::qWait(20);
        auto *panel = wrapper->findChild<SelectionPanel *>(); QVERIFY(panel);
        auto *scroll = panel->findChild<QScrollArea *>("selectionPropertiesScroll"); QVERIFY(scroll);
        const auto *content = scroll->widget();
        const auto detail = QString("wrapper=%1 panel=%2 viewport=%3 content=%4 min=%5")
            .arg(wrapper->width()).arg(panel->width()).arg(scroll->viewport()->width())
            .arg(content->width()).arg(content->minimumSizeHint().width());
        QCOMPARE(wrapper->width(), width);
        QVERIFY2(panel->width() <= width, qPrintable(detail));
        QVERIFY2(content->width() <= scroll->viewport()->width(), qPrintable(detail));
        QVERIFY2(content->minimumSizeHint().width() <= scroll->viewport()->width(), qPrintable(detail));
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        for (const auto *id : {"selectAllPixels", "invertPixelSelection", "deselectPixels", "transformX", "transformY",
                               "transformWidth", "transformHeight", "transformRotation", "applyPixelTransform", "cancelPixelTransform"}) {
            const auto *control = panel->findChild<QWidget *>(id); QVERIFY(control);
            if (!control->isVisibleTo(panel)) continue;
            const int x = control->mapTo(panel, QPoint()).x();
            QVERIFY2(x >= ui::metrics::panelMargin && x + control->width() <= panel->width() - ui::metrics::panelMargin, qPrintable(QString::fromLatin1(id) + " " + detail));
        }
        for (auto *control : panel->findChildren<ValueSlider *>())
            if (control->isVisibleTo(panel)) QVERIFY2(control->findChild<QSlider *>()->width() >= 74, qPrintable(control->objectName()));
        const auto apply = panel->findChild<QPushButton *>("applyPixelTransform");
        const auto cancel = panel->findChild<QPushButton *>("cancelPixelTransform");
        for (const auto *button : {apply, cancel}) {
            const QRect bounds(button->mapTo(panel, QPoint()), button->size());
            QVERIFY2(panel->rect().contains(bounds), qPrintable(detail));
            QVERIFY(!button->visibleRegion().isEmpty());
        }
        const auto footerPosition = apply->mapTo(panel, QPoint());
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        QCOMPARE(apply->mapTo(panel, QPoint()), footerPosition);
        if (height == 200) QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
        if (width == 260 && height == 480) {
            number(*panel, "transformX")->setValue(18);
            number(*panel, "transformRotation")->setValue(15);
            QDir().mkpath("test-artifacts");
            QVERIFY(wrapper->grab().save("test-artifacts/selection-properties.png"));
        }
        canvas.document()->cancelTransform();
    }

    void transformDraftSurvivesResponsiveLayoutAndRefresh() {
        Canvas canvas; canvas.setProject(paintedProject());
        auto *doc = canvas.document(); doc->setRasterSelection(paintedSelection());
        SelectionPanel panel(&canvas); panel.resize(300, 220); panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel)); panel.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&panel));
        panel.findChild<QPushButton *>("beginPixelTransform")->click();
        auto *scroll = panel.findChild<QScrollArea *>("selectionPropertiesScroll");
        auto *field = number(panel, "transformWidth")->findChild<QDoubleSpinBox *>();
        scroll->ensureWidgetVisible(field); field->setFocus(); QTRY_VERIFY(field->hasFocus());
        field->selectAll(); QTest::keyClicks(field, "127.");
        const auto draft = field->findChild<QLineEdit *>()->text();
        auto *row = panel.findChild<PropertyRow *>("transformWidthRow"); QVERIFY(row);
        // Compact rows now fit inline at the normal 220 px dock minimum. Use
        // a narrower component stress case to exercise wrapping with a draft.
        panel.setMinimumWidth(190); panel.resize(190, 220); QCoreApplication::processEvents();
        QCOMPARE(panel.width(), 190);
        QVERIFY(field->hasFocus()); QCOMPARE(field->findChild<QLineEdit *>()->text(), draft);
        QVERIFY(row->labelWidget()->geometry().bottom() < row->control()->geometry().top());
        doc->transformChanged(); QCoreApplication::processEvents();
        QCOMPARE(field->findChild<QLineEdit *>()->text(), draft);
        QCOMPARE(doc->transformParameters().scaleX, 1.0); QCOMPARE(doc->history()->count(), 0);
        QTest::keyClick(field, Qt::Key_Return);
        QCOMPARE(doc->transformParameters().scaleX, 1.27); QVERIFY(doc->transformActive());
        panel.resize(300, 220); QCoreApplication::processEvents();
        QVERIFY(field->hasFocus()); QVERIFY(row->labelWidget()->geometry().intersects(row->control()->geometry()) == false);
        QCOMPARE(row->labelWidget()->geometry().center().y(), row->control()->geometry().center().y());
        panel.findChild<QPushButton *>("applyPixelTransform")->click();
        QVERIFY(!doc->transformActive()); QCOMPARE(doc->history()->count(), 1);
        doc->history()->undo(); QCOMPARE(doc->project().shots.first().layers.first().tiles, paintedProject().shots.first().layers.first().tiles);
    }

    void contextualActionsAndLargerStatusKeepCompletionReachable() {
        Canvas canvas; canvas.setProject(paintedProject());
        auto *doc = canvas.document(); doc->setRasterSelection(paintedSelection());
        SelectionPanel panel(&canvas); panel.resize(220, 240); panel.show(); QTest::qWait(20);
        auto *actions = panel.findChild<QWidget *>("selectionActionRow"); QVERIFY(actions);
        auto *begin = panel.findChild<QPushButton *>("beginPixelTransform"); QVERIFY(begin);
        auto *status = panel.findChild<QLabel *>("pixelSelectionStatus"); QVERIFY(status);
        QVERIFY(actions->isVisibleTo(&panel)); QVERIFY(begin->text().isEmpty());
        QVERIFY(!begin->accessibleName().isEmpty()); QCOMPARE(begin->size(), QSize(28, 28));
        QVERIFY(actions->rect().contains(QRect(begin->mapTo(actions, QPoint()), begin->size())));
        auto font = panel.font(); font.setPixelSize(24); panel.setFont(font); QTest::qWait(20);
        QVERIFY(status->height() >= status->fontMetrics().height() + 2);
        const auto statusHeight = status->height(); doc->clearRasterSelection();
        QCOMPARE(status->height(), statusHeight); QCOMPARE(status->accessibleName(), status->text());
        doc->setRasterSelection(paintedSelection()); begin->click(); QTest::qWait(20);
        QVERIFY(doc->transformActive()); QVERIFY(!actions->isVisibleTo(&panel));
        auto *scroll = panel.findChild<QScrollArea *>("selectionPropertiesScroll");
        auto *apply = panel.findChild<QPushButton *>("applyPixelTransform");
        auto *cancel = panel.findChild<QPushButton *>("cancelPixelTransform");
        const auto footer = apply->mapTo(&panel, QPoint());
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        QCOMPARE(apply->mapTo(&panel, QPoint()), footer);
        for (auto *button : {apply, cancel}) QVERIFY(panel.rect().contains(QRect(button->mapTo(&panel, QPoint()), button->size())));
        // Display preferences remain reachable while transforming pixels.
        auto *tabs = panel.findChild<QTabBar *>("selectionDetailTabs"); QVERIFY(tabs);
        tabs->setCurrentIndex(1); scroll->ensureWidgetVisible(tabs); QTest::qWait(20);
        QVERIFY(panel.findChild<QToolButton *>("selectionAnimation")->isVisibleTo(&panel));
        number(panel, "transformX")->setValue(25); cancel->click();
        QVERIFY(!doc->transformActive()); QVERIFY(actions->isVisibleTo(&panel));
        QCOMPARE(doc->history()->count(), 0);
    }

    void compactSelectionPreviews() {
        if (qEnvironmentVariableIsEmpty("TOON_UI_PREVIEW")) QSKIP("Preview output is opt-in");
        const auto output = qEnvironmentVariable("TOON_UI_PREVIEW"); QVERIFY(QDir().mkpath(output));
        Canvas canvas; canvas.setProject(paintedProject()); canvas.document()->setRasterSelection(paintedSelection());
        SelectionPanel panel(&canvas); panel.resize(240, 400); panel.show(); QTest::qWait(20);
        QVERIFY(panel.grab().save(output + "/selection-properties.png"));
        panel.findChild<QTabBar *>("selectionDetailTabs")->setCurrentIndex(1); QTest::qWait(20);
        QVERIFY(panel.grab().save(output + "/selection-display.png"));
        panel.findChild<QPushButton *>("beginPixelTransform")->click(); panel.resize(220, 240); QTest::qWait(20);
        QVERIFY(panel.grab().save(output + "/transform-short.png"));
        panel.findChild<QScrollArea *>("selectionPropertiesScroll")->verticalScrollBar()->setValue(10000); QTest::qWait(20);
        QVERIFY(panel.grab().save(output + "/transform-short-bottom.png"));
        auto font = panel.font(); font.setPixelSize(24); panel.setFont(font); panel.resize(240, 400); QTest::qWait(20);
        QVERIFY(panel.grab().save(output + "/transform-larger-font.png"));
        canvas.document()->cancelTransform();
    }
};
QTEST_MAIN(SelectionPanelTests)
#include "test_selection_panel.moc"
