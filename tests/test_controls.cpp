#include "value_slider.h"
#include "canvas.h"
#include "drawing_panels.h"
#include "icons.h"
#include "main_window.h"
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSlider>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QSignalBlocker>
#include <QtTest>
#include <memory>

using namespace toon;
class ControlsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void dragAndNumericEntryShareOneValue() {
        ValueSlider control("Size", 1, 256, 12, 1, " px"); control.resize(300, 36); control.show();
        auto *track = control.findChild<QSlider *>();
        auto *number = control.findChild<QDoubleSpinBox *>();
        QSignalSpy changed(&control, &ValueSlider::valueChanged);
        QSignalSpy committed(&control, &ValueSlider::editingFinished);
        track->setSliderDown(true);
        track->setValue(285); track->setValue(632);
        QCOMPARE(control.value(), 63.2); QCOMPARE(number->value(), 63.2);
        QCOMPARE(changed.count(), 2); QCOMPARE(committed.count(), 0);
        track->setSliderDown(false);
        QCOMPARE(committed.count(), 1);
        number->setFocus(); number->selectAll();
        QTest::keyClicks(number, "42.7");
        QCOMPARE(control.value(), 63.2); // Partial typing never moves the slider.
        QTest::keyClick(number, Qt::Key_Return);
        QCOMPARE(control.value(), 42.7); QCOMPARE(track->value(), 427);
        QCOMPARE(committed.count(), 2);
        QTest::keyClick(number, Qt::Key_Tab);
        QCOMPARE(committed.count(), 2);
    }
    void boundsKeyboardAndBlockedRefresh() {
        ValueSlider control("Rotation", -180, 180, 0, 1, " °");
        control.setSingleStep(15);
        auto *track = control.findChild<QSlider *>();
        QSignalSpy changed(&control, &ValueSlider::valueChanged);
        QSignalSpy committed(&control, &ValueSlider::editingFinished);
        control.setValue(999); QCOMPARE(control.value(), 180); QCOMPARE(committed.count(), 0);
        { const QSignalBlocker block(&control); control.setValue(-179.5); }
        QCOMPARE(changed.count(), 1); QCOMPARE(track->value(), -1795);
        QTest::keyClick(track, Qt::Key_Right);
        QCOMPARE(control.value(), -164.5); QCOMPARE(committed.count(), 1);
        control.setValue(-999); QCOMPARE(control.value(), -180);
        QCOMPARE(committed.count(), 1);
    }
    void panelEditsReachBrushAndCommitOneOpacityUndo() {
        Canvas canvas; canvas.setProject(Project::create());
        std::unique_ptr<QWidget> properties(drawingPanel(&canvas, "propertiesPanel"));
        std::unique_ptr<QWidget> layers(drawingPanel(&canvas, "layersPanel"));
        auto *size = properties->findChild<ValueSlider *>("quickBrushSize");
        auto *flow = properties->findChild<ValueSlider *>("quickBrushFlow");
        size->findChild<QDoubleSpinBox *>()->setValue(73.5);
        flow->findChild<QDoubleSpinBox *>()->setValue(42);
        QCOMPARE(canvas.brush.size, 73.5); QCOMPARE(canvas.brush.opacity, .42);
        auto *opacity = layers->findChild<ValueSlider *>("layerOpacity");
        QVERIFY(opacity);
        auto *track = opacity->findChild<QSlider *>();
        auto *doc = canvas.document();
        track->setSliderDown(true);
        QVERIFY(doc->layerPropertyEditActive());
        track->setSliderDown(false);
        QVERIFY(!doc->layerPropertyEditActive());
        QCOMPARE(doc->history()->count(), 0);
        track->setSliderDown(true);
        track->setValue(70); track->setValue(35);
        QCOMPARE(doc->history()->count(), 0);
        track->setSliderDown(false);
        QCOMPARE(doc->history()->count(), 1);
        QCOMPARE(doc->project().shots.first().layers.first().opacity, .35);
        doc->history()->undo();
        QCOMPARE(opacity->value(), 100);
        QCOMPARE(doc->project().shots.first().layers.first().opacity, 1);
        opacity->findChild<QDoubleSpinBox *>()->setValue(62);
        QCOMPARE(doc->project().shots.first().layers.first().opacity, .62);
        QCOMPARE(doc->history()->count(), 1);
    }
    void escapeRestoresGestureDespiteModelEcho() {
        ValueSlider control("Opacity", 0, 100, 75);
        control.resize(240, 32); control.show();
        auto *track = control.findChild<QSlider *>();
        auto *number = control.findChild<QDoubleSpinBox *>();
        connect(&control, &ValueSlider::valueChanged, &control, &ValueSlider::setValueSilently);
        QSignalSpy starts(&control, &ValueSlider::editingStarted);
        QSignalSpy finishes(&control, &ValueSlider::editingFinished);
        QSignalSpy cancels(&control, &ValueSlider::editingCancelled);
        track->setSliderDown(true); track->setValue(25);
        QTest::keyClick(track, Qt::Key_Escape);
        track->setSliderDown(false);
        QCOMPARE(control.value(), 75.); QCOMPARE(starts.count(), 1);
        QCOMPARE(finishes.count(), 0); QCOMPARE(cancels.count(), 1);
        number->setFocus(); number->selectAll(); QTest::keyClicks(number, "42");
        control.setValueSilently(75); // Unrelated refresh must retain unfinished text.
        QCOMPARE(number->findChild<QLineEdit *>()->text(), QString("42"));
        QCOMPARE(control.value(), 75.);
        QTest::keyClick(number, Qt::Key_Escape);
        QTest::keyClick(number, Qt::Key_Tab);
        QCOMPARE(control.value(), 75.);
        QCOMPARE(number->findChild<QLineEdit *>()->text(), QString("75"));
        QCOMPARE(finishes.count(), 0); QCOMPARE(cancels.count(), 2);
    }
    void narrowedTrackRetainsExactRangeAndOriginalValue() {
        ValueSlider control("Position", -1000000, 1000000, 9000, 1);
        control.setSliderRange(-1920, 1920);
        auto *track = control.findChild<QSlider *>();
        auto *number = control.findChild<QDoubleSpinBox *>();
        QCOMPARE(number->value(), 9000.); QCOMPARE(track->maximum(), 19200);
        QSignalSpy changes(&control, &ValueSlider::valueChanged);
        track->setSliderDown(true); track->setValue(5000);
        QCOMPARE(control.value(), 500.);
        control.cancelEditing(); QCOMPARE(control.value(), 9000.);
        number->setValue(12345.6); QCOMPARE(control.value(), 12345.6);
        QCOMPARE(changes.count(), 3);
        const int before = changes.count();
        control.setValueSilently(-9876.5);
        QCOMPARE(changes.count(), before); QCOMPARE(control.value(), -9876.5);
    }
    void lostReleaseAndDisabledControlsFinishOnce() {
        ValueSlider control("Opacity", 0, 100, 75);
        auto *track = control.findChild<QSlider *>();
        QSignalSpy finishes(&control, &ValueSlider::editingFinished);
        track->setSliderDown(true); track->setValue(20);
        QEvent ungrab(QEvent::UngrabMouse); QApplication::sendEvent(track, &ungrab);
        QCOMPARE(finishes.count(), 1);
        track->setSliderDown(false); QCOMPARE(finishes.count(), 1);
        track->setSliderDown(true); track->setValue(30);
        control.setEnabled(false); QCOMPARE(finishes.count(), 2);
        QCOMPARE(control.value(), 30.);
    }
    void iconsRenderFromEmbeddedVectors() {
        for (const auto *name : {"brush", "eraser", "hand", "palette", "plus", "file-plus", "folder-open",
                 "save", "undo-2", "redo-2", "scan", "rotate-ccw", "layers", "arrow-up", "arrow-down",
                 "trash", "settings-2", "panels-top-left"}) {
            const auto icon = appIcon(name);
            QVERIFY2(!icon.isNull(), name);
            const auto pixmap = icon.pixmap(QSize(24, 24), 2.0);
            QCOMPARE(pixmap.size(), QSize(48, 48)); QCOMPARE(pixmap.devicePixelRatio(), 2.0);
            const auto image = pixmap.toImage(); bool visible = false;
            for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
                visible |= image.pixelColor(x, y).alpha() > 0;
            QVERIFY2(visible, name);
            QVERIFY(icon.pixmap(QSize(24, 24), 2.0, QIcon::Disabled).toImage() != image);
        }
        QVERIFY(appIcon("missing-icon").isNull());
    }
    void inspectorScrollPreservesValuesAndNumericDrafts() {
        QWidget host; auto *layout = new QVBoxLayout(&host);
        auto *search = new QLineEdit; layout->addWidget(search);
        auto *scroll = new QScrollArea; scroll->setWidgetResizable(true); layout->addWidget(scroll);
        auto *body = new QWidget; auto *rows = new QVBoxLayout(body);
        auto *control = new ValueSlider("Opacity", 0, 100, 50); rows->addWidget(control); rows->addStretch();
        body->setMinimumHeight(800); scroll->setWidget(body);
        host.resize(300, 240); host.show(); QVERIFY(QTest::qWaitForWindowExposed(&host));
        host.activateWindow(); QApplication::setActiveWindow(&host); search->setFocus(); QTRY_VERIFY(search->hasFocus());
        auto *track = control->findChild<QSlider *>(); auto *number = control->findChild<QDoubleSpinBox *>();
        auto *editor = number->findChild<QLineEdit *>();
        QSignalSpy changed(control, &ValueSlider::valueChanged), started(control, &ValueSlider::editingStarted);
        QSignalSpy finished(control, &ValueSlider::editingFinished);
        const auto wheel = [](QWidget *target) {
            // Exercise window hit testing and wheel propagation in logical coordinates. Qt 6.10's
            // QWindow test hook interprets its position as native pixels at 2x,
            // which otherwise sends this event to the search field above it.
            const QPoint position = target->rect().center();
            QWheelEvent event(target->mapTo(target->window(), position), target->mapToGlobal(position), {}, {0, -120},
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QSpontaneKeyEvent::setSpontaneous(&event);
            qApp->notify(target->window()->windowHandle(), &event);
        };
        for (QWidget *target : {static_cast<QWidget *>(track), static_cast<QWidget *>(number)}) {
            scroll->verticalScrollBar()->setValue(0); wheel(target);
            QVERIFY(scroll->verticalScrollBar()->value() > 0); QCOMPARE(control->value(), 50.);
            QVERIFY(search->hasFocus()); QCOMPARE(changed.count(), 0); QCOMPARE(started.count(), 0); QCOMPARE(finished.count(), 0);
        }
        scroll->verticalScrollBar()->setValue(0); track->setFocus(); QTRY_VERIFY(track->hasFocus()); wheel(track);
        QVERIFY(control->value() < 50.); QCOMPARE(started.count(), 1); QCOMPARE(finished.count(), 1);
        QCOMPARE(scroll->verticalScrollBar()->value(), 0);
        control->setValueSilently(50); number->setFocus(); QTRY_VERIFY(number->hasFocus()); number->selectAll();
        QTest::keyClicks(number, "73"); const int changes = changed.count(), commits = finished.count();
        scroll->verticalScrollBar()->setValue(0); wheel(number);
        QCOMPARE(editor->text(), QString("73")); QCOMPARE(control->value(), 50.); QVERIFY(number->hasFocus());
        QVERIFY(scroll->verticalScrollBar()->value() > 0); QCOMPARE(changed.count(), changes); QCOMPARE(finished.count(), commits);
        QTest::keyClick(number, Qt::Key_Return); QCOMPARE(control->value(), 73.); QCOMPARE(finished.count(), commits + 1);
    }
};
QTEST_MAIN(ControlsTest)
#include "test_controls.moc"
