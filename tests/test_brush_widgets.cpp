#include "brush_widgets.h"
#include "brush_settings_panel.h"
#include "value_slider.h"
#include "canvas.h"
#include "ui_theme.h"
#include <QDoubleSpinBox>
#include <QFocusEvent>
#include <QHideEvent>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTabBar>
#include <QToolButton>
#include <QtTest>
using namespace toon;
class BrushWidgetsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void keyboardEditsSpatialFieldsWithoutTouchingOtherSettings() {
        using Kind = BrushVisualWidget::Kind;
        BrushSettings b; b.size = 73; b.color = QColor("#774433");
        for (Kind kind : {Kind::Tip, Kind::Spacing, Kind::Scatter, Kind::Pose, Kind::Symmetry, Kind::Texture}) {
            BrushVisualWidget widget(kind); widget.setBrush(b); widget.resize(200, widget.height()); widget.show();
            QSignalSpy edits(&widget, &BrushVisualWidget::brushEdited);
            QTest::keyClick(&widget, Qt::Key_Right);
            QCOMPARE(edits.count(), 1); QCOMPARE(widget.brush().size, b.size); QCOMPARE(widget.brush().color, b.color);
            switch (kind) {
            case Kind::Tip: QCOMPARE(widget.brush().angle, 1); break;
            case Kind::Spacing: QCOMPARE(widget.brush().spacing, .03); break;
            case Kind::Scatter: QCOMPARE(widget.brush().count, 2); break;
            case Kind::Pose: QCOMPARE(widget.brush().pose.tiltX, 1); QVERIFY(widget.brush().pose.overrideTiltX); QVERIFY(!widget.brush().pose.overrideTiltY); break;
            case Kind::Symmetry: QCOMPARE(widget.brush().symmetry.center, QPointF(.51, .5)); break;
            case Kind::Texture: QCOMPARE(widget.brush().texture.scale, 1.05); break;
            }
        }
    }
    void directManipulationIsBoundedAndPoseOverridesAreExplicit() {
        using Kind = BrushVisualWidget::Kind;
        BrushVisualWidget spacing(Kind::Spacing); spacing.resize(200, spacing.height()); spacing.show();
        QTest::mouseClick(&spacing, Qt::LeftButton, Qt::NoModifier, {199, 30}); QCOMPARE(spacing.brush().spacing, 4);
        QTest::mouseClick(&spacing, Qt::LeftButton, Qt::NoModifier, {1, 30}); QCOMPARE(spacing.brush().spacing, .01);
        BrushVisualWidget center(Kind::Symmetry); center.resize(200, center.height()); center.show();
        QTest::mouseClick(&center, Qt::LeftButton, Qt::NoModifier, {199, 139}); QCOMPARE(center.brush().symmetry.center, QPointF(1, 1));
        BrushVisualWidget pose(Kind::Pose); pose.resize(200, pose.height()); pose.show();
        QTest::mouseClick(&pose, Qt::LeftButton, Qt::NoModifier, {120, 63});
        QVERIFY(pose.brush().pose.overrideTiltX); QVERIFY(pose.brush().pose.overrideTiltY); QVERIFY(!pose.brush().pose.overrideRotation);
        QVERIFY(pose.brush().pose.tiltX > 0);
        QTest::mouseClick(&pose, Qt::LeftButton, Qt::NoModifier, {100, 13});
        QVERIFY(pose.brush().pose.overrideRotation); QVERIFY(pose.brush().pose.rotation < -80);
    }
    void diagonalPoseTipRemainsTiltHandleOutsideRotationRing() {
        for (const qreal tiltY : {90., 45.}) {
            BrushVisualWidget pose(BrushVisualWidget::Kind::Pose);
            BrushSettings b; b.pose.tiltX = 90; b.pose.tiltY = tiltY;
            b.pose.overrideTiltX = b.pose.overrideTiltY = true;
            b.pose.rotation = 37; b.pose.overrideRotation = false;
            pose.setBrush(b); pose.resize(200, pose.height()); pose.show();
            const QRectF field = QRectF(pose.rect()).adjusted(16, 12, -16, -25);
            const qreal radius = qMin(field.width(), field.height()) / 2;
            const QPoint tip = (field.center() + QPointF(90, tiltY) / 90 * radius * .72).toPoint();
            QTest::mousePress(&pose, Qt::LeftButton, Qt::NoModifier, tip);
            QTest::mouseMove(&pose, tip - QPoint(9, 7));
            QTest::mouseRelease(&pose, Qt::LeftButton, Qt::NoModifier, tip - QPoint(9, 7));
            QVERIFY(pose.brush().pose.tiltX < b.pose.tiltX);
            QVERIFY(pose.brush().pose.tiltY < b.pose.tiltY);
            QCOMPARE(pose.brush().pose.rotation, b.pose.rotation);
            QVERIFY(!pose.brush().pose.overrideRotation);
        }
    }
    void interruptedDragNeverResumesOnLaterMoves() {
        BrushVisualWidget spacing(BrushVisualWidget::Kind::Spacing);
        spacing.resize(200, spacing.height()); spacing.show();
        auto move = [&](Qt::MouseButtons buttons) {
            const QPointF point(190, 30);
            QMouseEvent event(QEvent::MouseMove, point, spacing.mapToGlobal(point), Qt::NoButton, buttons, Qt::NoModifier);
            QApplication::sendEvent(&spacing, &event);
        };
        for (const auto type : {QEvent::FocusOut, QEvent::Hide, QEvent::UngrabMouse, QEvent::WindowDeactivate}) {
            QTest::mousePress(&spacing, Qt::LeftButton, Qt::NoModifier, {70, 30});
            const auto before = spacing.brush();
            if (type == QEvent::FocusOut) { QFocusEvent event(type); QApplication::sendEvent(&spacing, &event); }
            else if (type == QEvent::Hide) { QHideEvent event; QApplication::sendEvent(&spacing, &event); }
            else { QEvent event(type); QApplication::sendEvent(&spacing, &event); }
            move(Qt::LeftButton); QVERIFY(spacing.brush() == before);
            QTest::mouseRelease(&spacing, Qt::LeftButton, Qt::NoModifier, {190, 30});
            QVERIFY(spacing.brush() == before);
        }
        QTest::mousePress(&spacing, Qt::LeftButton, Qt::NoModifier, {70, 30});
        const auto before = spacing.brush(); move(Qt::NoButton); move(Qt::LeftButton);
        QVERIFY(spacing.brush() == before);
        QTest::mouseRelease(&spacing, Qt::LeftButton, Qt::NoModifier, {190, 30});
        QVERIFY(spacing.brush() == before);
    }
    void widgetsAndPreciseControlsShareActualBrushAndNoDocumentHistory() {
        Canvas canvas; canvas.setProject(Project::create()); BrushSettingsPanel panel(&canvas); panel.resize(270, 540); panel.show(); panel.openAdvancedSettings();
        auto *tabs = panel.findChild<QTabBar *>("brushSettingsCategory"); QVERIFY(tabs); QCOMPARE(tabs->count(), 12);
        tabs->setCurrentIndex(1);
        auto *tip = panel.findChild<BrushVisualWidget *>("brushTipVisual"); QVERIFY(tip);
        QTest::keyClick(tip, Qt::Key_Right); QCOMPARE(canvas.brush.angle, 1);
        auto *angle = panel.findChild<ValueSlider *>("brushAngle"); QCOMPARE(angle->value(), 1);
        angle->setValue(52); QCOMPARE(tip->brush().angle, 52);
        tabs->setCurrentIndex(3);
        auto *scatter = panel.findChild<BrushVisualWidget *>("brushScatterVisual"); QTest::keyClick(scatter, Qt::Key_Up);
        QCOMPARE(canvas.brush.scatter, .1); QCOMPARE(panel.findChild<ValueSlider *>("brushScatter")->value(), 10);
        tabs->setCurrentIndex(9); panel.findChild<QToolButton *>("brushSymmetryChoice6")->click(); QCOMPARE(canvas.brush.symmetry.mode, SymmetryMode::Circle);
        auto *center = panel.findChild<BrushVisualWidget *>("brushSymmetryVisual"); QTest::keyClick(center, Qt::Key_Right);
        QCOMPARE(canvas.brush.symmetry.center.x(), .51);
        for (int i = 0; i < tabs->count(); ++i) { QVERIFY(!tabs->tabIcon(i).isNull()); QVERIFY(!tabs->tabToolTip(i).isEmpty()); QVERIFY(!tabs->accessibleTabName(i).isEmpty()); }
        tabs->setFocus(); tabs->setCurrentIndex(0); QTest::keyClick(tabs, Qt::Key_Down); QCOMPARE(tabs->currentIndex(), 1);
        QCOMPARE(canvas.document()->history()->count(), 0); QVERIFY(canvas.document()->history()->isClean());
        for (auto *widget : panel.findChildren<BrushVisualWidget *>()) { QVERIFY(!widget->toolTip().isEmpty()); QVERIFY(!widget->accessibleName().isEmpty()); }
    }
};
QTEST_MAIN(BrushWidgetsTest)
#include "test_brush_widgets.moc"
