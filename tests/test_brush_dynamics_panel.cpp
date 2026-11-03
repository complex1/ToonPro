#include "brush_response_widget.h"
#include "brush_settings_panel.h"
#include "canvas.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include "value_slider.h"
#include "property_row.h"
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QMouseEvent>
#include <QScrollArea>
#include <QSignalSpy>
#include <QSlider>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QToolButton>
#include <QtTest>
#include <functional>
#include <limits>

using namespace toon;
class BrushDynamicsPanelTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); qRegisterMetaType<BrushResponseWidget::Response>(); }

    void responseKeyboardPresetsAndDrag() {
        BrushResponseWidget graph; graph.resize(260, 142); graph.show(); graph.setFocus();
        QSignalSpy edits(&graph, &BrushResponseWidget::responseEdited);
        QTest::keyClick(&graph, Qt::Key_Up); QCOMPARE(graph.response()[2], .51); QCOMPARE(edits.count(), 1);
        QTest::keyClick(&graph, Qt::Key_Right); QCOMPARE(graph.activeHandle(), 3);
        QTest::keyClick(&graph, Qt::Key_Down, Qt::ShiftModifier); QCOMPARE(graph.response()[3], .65);
        QVERIFY(graph.accessibleDescription().contains("Point 4"));
        graph.findChild<QToolButton *>("responseInvert")->click();
        const BrushResponseWidget::Response inverse{1, .75, .5, .25, 0}; QCOMPARE(graph.response(), inverse);
        const auto beforeDrag = graph.response();
        QTest::mousePress(&graph, Qt::LeftButton, Qt::NoModifier, {130, 20});
        QVERIFY(graph.response()[2] > .8);
        QTest::keyClick(&graph, Qt::Key_Escape); QCOMPARE(graph.response(), beforeDrag);
        QTest::mouseRelease(&graph, Qt::LeftButton, Qt::NoModifier, {130, 90}); QCOMPARE(graph.response(), beforeDrag);
        const auto beforeRefresh = edits.count(); graph.setResponse({0, .1, .4, .8, 1});
        QCOMPARE(edits.count(), beforeRefresh);
        graph.setResponse({0, .1, std::numeric_limits<qreal>::quiet_NaN(), .8, 1});
        QCOMPARE(graph.response()[2], .4);
        QTest::mousePress(&graph, Qt::LeftButton, Qt::NoModifier, {130, 40});
        QEvent lost(QEvent::UngrabMouse); QApplication::sendEvent(&graph, &lost);
        const auto interrupted = graph.response();
        QMouseEvent move(QEvent::MouseMove, QPointF(130, 90), graph.mapToGlobal(QPoint(130, 90)),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&graph, &move); QCOMPARE(graph.response(), interrupted);
    }

    void dynamicsBindToSixIndependentResponses() {
        Canvas canvas; canvas.setProject(Project::create()); BrushSettingsPanel panel(&canvas);
        panel.show(); panel.openAdvancedSettings();
        struct Target { const char *prefix; std::function<BrushDynamic &(BrushSettings &)> field; };
        const Target targets[]{
            {"size", [](BrushSettings &b) -> BrushDynamic & { return b.sizeDynamics; }},
            {"angle", [](BrushSettings &b) -> BrushDynamic & { return b.angleDynamics; }},
            {"roundness", [](BrushSettings &b) -> BrushDynamic & { return b.roundnessDynamics; }},
            {"opacity", [](BrushSettings &b) -> BrushDynamic & { return b.opacityDynamics; }},
            {"flow", [](BrushSettings &b) -> BrushDynamic & { return b.flowDynamics; }},
            {"textureDepth", [](BrushSettings &b) -> BrushDynamic & { return b.texture.dynamics; }}
        };
        for (const auto &target : targets) {
            const QString prefix(target.prefix);
            auto *control = panel.findChild<QComboBox *>(prefix + "Control"); QVERIFY(control);
            control->setCurrentIndex(control->findData(int(BrushControl::Pressure)));
            auto *curve = panel.findChild<BrushResponseWidget *>(prefix + "Response"); QVERIFY(curve);
            curve->findChild<QToolButton *>("responseSoft")->click();
            QCOMPARE(target.field(canvas.brush).response[1], .5);
            auto *jitter = panel.findChild<ValueSlider *>(prefix + "Jitter"); QVERIFY(jitter);
            auto *perStroke = panel.findChild<QToolButton *>(prefix + "JitterPerStroke"); QVERIFY(perStroke);
            QVERIFY(!perStroke->isEnabled()); jitter->setValue(40); QVERIFY(perStroke->isEnabled());
            perStroke->click(); QVERIFY(target.field(canvas.brush).jitterPerStroke);
            jitter->setValue(0); QVERIFY(!perStroke->isEnabled()); QVERIFY(target.field(canvas.brush).jitterPerStroke);
        }
        QCOMPARE(canvas.document()->history()->count(), 0);
    }

    void rotationSelectorsKeepTheirEnumIdentity() {
        Canvas canvas; canvas.setProject(Project::create()); BrushSettingsPanel panel(&canvas);
        panel.show(); panel.openAdvancedSettings();
        const QStringList names{"sizeControl", "angleControl", "roundnessControl", "opacityControl", "flowControl",
            "textureDepthControl", "brushScatterControl", "brushCountControl", "brushColorControl"};
        for (const auto &name : names) {
            auto *control = panel.findChild<QComboBox *>(name); QVERIFY(control);
            const int index = control->findData(int(BrushControl::Rotation)); QVERIFY(index >= 0);
            QCOMPARE(index, name == "angleControl" ? 7 : 5); control->setCurrentIndex(index);
            QCOMPARE(control->currentData().toInt(), 7); QCOMPARE(control->currentText(), QString("Pen rotation"));
        }
        const auto &b = canvas.brush;
        for (auto control : {b.sizeDynamics.control, b.angleDynamics.control, b.roundnessDynamics.control,
             b.opacityDynamics.control, b.flowDynamics.control, b.texture.dynamics.control,
             b.scatterControl, b.countControl, b.colorDynamics.control}) QCOMPARE(control, BrushControl::Rotation);
        QVERIFY(!b.pressureSize);
    }

    void randomModeAndExactSeedPreservePendingEntry() {
        Canvas canvas; canvas.setProject(Project::create());
        auto brush = canvas.brush; brush.randomSeed = 4000000000u; canvas.setBrushSettings(brush);
        BrushSettingsPanel panel(&canvas); panel.show(); panel.openAdvancedSettings();
        panel.findChild<QTabBar *>("brushSettingsCategory")->setCurrentIndex(10);
        auto *mode = panel.findChild<QTabBar *>("brushSeedMode"); QVERIFY(mode);
        auto *seed = panel.findChild<ValueSlider *>("brushRandomSeed"); QVERIFY(seed);
        auto *reseed = panel.findChild<QToolButton *>("brushReseed"); QVERIFY(reseed);
        QCOMPARE(seed->value(), 4000000000.); QVERIFY(!seed->isEnabled());
        QCOMPARE(seed->findChild<QSlider *>()->maximum(), 999999);
        mode->setCurrentIndex(1); QVERIFY(canvas.brush.lockRandomSeed); QVERIFY(seed->isEnabled());
        auto *number = seed->findChild<QDoubleSpinBox *>(); auto *edit = number->findChild<QLineEdit *>();
        QCOMPARE(number->width(), 48); QVERIFY(number->suffix().isEmpty());
        edit->setFocus(); edit->selectAll(); QTest::keyClicks(edit, "876543");
        auto updated = canvas.brush; updated.opacity = .42; canvas.setBrushSettings(updated);
        QCOMPARE(edit->text(), QString("876543")); QCOMPARE(canvas.brush.randomSeed, 4000000000u);
        QTest::keyClick(edit, Qt::Key_Return); QCOMPARE(canvas.brush.randomSeed, 876543u);
        const auto previous = canvas.brush.randomSeed; reseed->click();
        QVERIFY(canvas.brush.randomSeed != previous); QVERIFY(canvas.brush.randomSeed >= 1 && canvas.brush.randomSeed <= 999999);
        mode->setCurrentIndex(0); QVERIFY(!canvas.brush.lockRandomSeed); QVERIFY(!reseed->isEnabled());
        QCOMPARE(canvas.document()->history()->count(), 0);
    }

    void quickPanelRemainsCompactAndAdvancedFitsNarrowWindow() {
        Canvas canvas; canvas.setProject(Project::create()); BrushSettingsPanel panel(&canvas);
        panel.resize(240, 500); panel.show(); QTest::qWait(1);
        const auto controls = panel.findChildren<ValueSlider *>(); int visible = 0;
        for (const auto *control : controls) if (control->isVisibleTo(&panel)) ++visible;
        QCOMPARE(visible, 5);
        for (const auto *curve : panel.findChildren<BrushResponseWidget *>()) QVERIFY(!curve->isVisibleTo(&panel));
        panel.openAdvancedSettings(); auto *dialog = panel.findChild<QDialog *>("advancedBrushDialog");
        dialog->resize(360, 560);
        auto *categories = panel.findChild<QTabBar *>("brushSettingsCategory");
        auto *pages = panel.findChild<QStackedWidget *>("brushSettingsPages");
        for (const int index : {2, 4, 7, 10}) {
            categories->setCurrentIndex(index); QTest::qWait(1);
            auto *scroll = qobject_cast<QScrollArea *>(pages->currentWidget()); QVERIFY(scroll);
            QVERIFY(scroll->widget()->width() <= scroll->viewport()->width());
            QCOMPARE(dialog->width(), 360); QCOMPARE(panel.width(), 240);
            for (auto *slider : scroll->findChildren<ValueSlider *>()) {
                if (!slider->isVisibleTo(scroll)) continue;
                QVERIFY2(slider->findChild<QSlider *>()->width() >= 74, qPrintable(slider->objectName()));
                QCOMPARE(slider->findChild<QDoubleSpinBox *>()->width(), 48);
            }
        }
        for (auto *button : dialog->findChildren<QToolButton *>()) {
            if (!button->objectName().startsWith("response") && !button->objectName().endsWith("JitterPerStroke") &&
                button->objectName() != "brushReseed") continue;
            QVERIFY2(!button->accessibleName().isEmpty(), qPrintable(button->objectName()));
            QVERIFY2(!button->toolTip().isEmpty(), qPrintable(button->objectName()));
            QCOMPARE(button->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay, nullptr, button), 600);
            if (!button->objectName().startsWith("response")) {
                QCOMPARE(button->size(), QSize(ui::metrics::actionTarget, ui::metrics::actionTarget));
                QCOMPARE(button->iconSize(), QSize(ui::metrics::actionIcon, ui::metrics::actionIcon));
            }
        }
    }

    void compactDynamicsKeepConditionalRowsAndResponseKeyboardEditing() {
        Canvas canvas; canvas.setProject(Project::create()); BrushSettingsPanel panel(&canvas);
        panel.show(); panel.openAdvancedSettings();
        auto *dialog = panel.findChild<QDialog *>("advancedBrushDialog"); QVERIFY(dialog);
        dialog->resize(360, 420); dialog->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(dialog));
        auto *categories = panel.findChild<QTabBar *>("brushSettingsCategory"); categories->setCurrentIndex(2);
        auto *control = panel.findChild<QComboBox *>("sizeControl"); QVERIFY(control);
        auto *fade = panel.findChild<ValueSlider *>("sizeFade"); QVERIFY(fade);
        auto *fadeRow = qobject_cast<PropertyRow *>(fade->parentWidget()); QVERIFY(fadeRow);
        auto *jitter = panel.findChild<ValueSlider *>("sizeJitter"); QVERIFY(jitter);
        auto *jitterRow = qobject_cast<PropertyRow *>(jitter->parentWidget()); QVERIFY(jitterRow);
        auto *perStroke = panel.findChild<QToolButton *>("sizeJitterPerStroke"); QVERIFY(perStroke);
        QCOMPARE(perStroke->parentWidget(), jitterRow);
        auto *response = panel.findChild<BrushResponseWidget *>("sizeResponse"); QVERIFY(response);
        control->setCurrentIndex(control->findData(int(BrushControl::Off)));
        QVERIFY(fadeRow->isHidden()); QVERIFY(response->isHidden());
        control->setCurrentIndex(control->findData(int(BrushControl::Fade)));
        QVERIFY(!fadeRow->isHidden()); QVERIFY(!response->isHidden());
        control->setCurrentIndex(control->findData(int(BrushControl::Pressure)));
        QVERIFY(fadeRow->isHidden()); QVERIFY(!response->isHidden());
        response->setFocus(); QTRY_VERIFY(response->hasFocus());
        const qreal previous = canvas.brush.sizeDynamics.response[2];
        QTest::keyClick(response, Qt::Key_Up);
        QCOMPARE(canvas.brush.sizeDynamics.response[2], previous + .01);
        auto *pages = panel.findChild<QStackedWidget *>("brushSettingsPages");
        auto *scroll = qobject_cast<QScrollArea *>(pages->currentWidget()); QVERIFY(scroll);
        scroll->ensureWidgetVisible(response); QTest::qWait(5);
        for (auto *button : response->findChildren<QToolButton *>()) {
            QVERIFY(button->width() >= button->fontMetrics().horizontalAdvance(button->text()) + 8);
            QVERIFY(response->rect().contains(QRect(button->mapTo(response, QPoint()), button->size())));
        }
        QCOMPARE(canvas.document()->history()->count(), 0); dialog->close();
    }

    void dynamicsPreviews() {
        const auto output = qEnvironmentVariable("TOON_UI_PREVIEW");
        if (output.isEmpty()) QSKIP("Set TOON_UI_PREVIEW to save dynamics previews.");
        QVERIFY(QDir().mkpath(output));
        Canvas canvas; canvas.setProject(Project::create());
        auto brush = canvas.brush; brush.sizeDynamics.control = BrushControl::Pressure;
        brush.sizeDynamics.response = {0, .5, .707, .866, 1}; brush.sizeDynamics.jitter = .15;
        brush.sizeDynamics.jitterPerStroke = true; brush.opacityDynamics.control = BrushControl::Pressure;
        brush.lockRandomSeed = true; brush.randomSeed = 24681; canvas.setBrushSettings(brush);
        BrushSettingsPanel panel(&canvas); panel.resize(240, 500); panel.show(); panel.openAdvancedSettings();
        auto *dialog = panel.findChild<QDialog *>("advancedBrushDialog"); dialog->resize(440, 680);
        auto *categories = panel.findChild<QTabBar *>("brushSettingsCategory");
        for (int index : {2, 7, 10}) {
            categories->setCurrentIndex(index); QTest::qWait(1);
            QVERIFY(dialog->grab().save(QDir(output).filePath(QString("brush-dynamics-%1.png").arg(index))));
        }
    }
};
QTEST_MAIN(BrushDynamicsPanelTest)
#include "test_brush_dynamics_panel.moc"
