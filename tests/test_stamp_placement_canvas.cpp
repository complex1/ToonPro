#include "canvas.h"
#include "selection_context_bar.h"
#include "ui_theme.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPointingDevice>
#include <QPushButton>
#include <QTabletEvent>
#include <QTimer>
#include <QToolButton>
#include <QtTest>
#include <cmath>

using namespace toon;

namespace {
Project fixture() {
    auto project = Project::create();
    project.width = 320; project.height = 260; project.transparent = true;
    project.shots[0].sheet.overscan = 0;
    QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::transparent);
    QPainter painter(&tile); painter.fillRect(QRect(20, 24, 24, 28), QColor("#b42a38")); painter.end();
    project.shots[0].layers[0].tiles[{0, 0}] = tile;
    return project;
}

BrushSettings stamp(bool colored = false) {
    BrushSettings brush;
    brush.size = 64; brush.spacing = .1; brush.color = QColor("#245080");
    brush.lockRandomSeed = true; brush.randomSeed = 411;
    brush.smoothingEnabled = false;
    QImage mask(32, 24, QImage::Format_Grayscale8); mask.fill(0);
    for (int y = 2; y < 22; ++y) for (int x = 3; x < 29; ++x)
        if (x < 12 || y > 11) mask.scanLine(y)[x] = 255;
    brush.tipMask = mask;
    if (colored) {
        brush.colorTip = QImage(mask.size(), QImage::Format_ARGB32_Premultiplied);
        brush.colorTip.fill(Qt::transparent);
        for (int y = 0; y < mask.height(); ++y) for (int x = 0; x < mask.width(); ++x)
            if (mask.constScanLine(y)[x]) brush.colorTip.setPixelColor(x, y, x < 12 ? QColor("#2f9373") : QColor("#e5b446"));
    }
    return brush;
}

void prepare(Canvas &canvas, bool colored = false) {
    canvas.resize(900, 760); canvas.setProject(fixture()); canvas.setZoom(1);
    canvas.setBrushSettings(stamp(colored)); canvas.setStabilization(0);
    canvas.setStampPlacementEnabled(true); canvas.setSelectionBarVisible(false);
    canvas.show(); QApplication::processEvents();
}

QPointF view(const Canvas &canvas, QPointF point) {
    return canvas.viewTransform().map(sheetGeometry(canvas.project(), canvas.shot()).output.topLeft() + point);
}

QPointingDevice &penDevice() {
    static QPointingDevice pen("Stamp pen", 504, QInputDevice::DeviceType::Stylus,
        QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
    return pen;
}

void pointerView(Canvas &canvas, bool pen, QEvent::Type type, QPointF local, qreal pressure = 1,
                 bool hover = false) {
    const bool press = type == QEvent::MouseButtonPress, release = type == QEvent::MouseButtonRelease;
    const auto button = press || release ? Qt::LeftButton : Qt::NoButton;
    const auto buttons = release || hover ? Qt::NoButton : Qt::LeftButton;
    if (pen) {
        QTabletEvent event(press ? QEvent::TabletPress : release ? QEvent::TabletRelease : QEvent::TabletMove,
            &penDevice(), local, canvas.mapToGlobal(local), release || hover ? 0 : pressure,
            0, 0, 0, 0, 0, Qt::NoModifier, button, buttons);
        QApplication::sendEvent(&canvas, &event);
    } else {
        QMouseEvent event(type, local, canvas.mapToGlobal(local), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &event);
    }
}

void pointer(Canvas &canvas, bool pen, QEvent::Type type, QPointF point, qreal pressure = 1, bool hover = false) {
    pointerView(canvas, pen, type, view(canvas, point), pressure, hover);
}

void contact(Canvas &canvas, QPointF point = {130, 110}, bool pen = false, qreal pressure = 1) {
    pointer(canvas, pen, QEvent::MouseButtonPress, point, pressure);
    pointer(canvas, pen, QEvent::MouseButtonRelease, point, pressure);
}

void drag(Canvas &canvas, QPointF from, QPointF to, bool pen = false) {
    pointerView(canvas, pen, QEvent::MouseButtonPress, from);
    pointerView(canvas, pen, QEvent::MouseMove, (from + to) / 2);
    pointerView(canvas, pen, QEvent::MouseMove, to);
    pointerView(canvas, pen, QEvent::MouseButtonRelease, to);
}

SelectionContextBar *bar(Canvas &canvas) {
    return canvas.findChild<SelectionContextBar *>("selectionContextBar");
}

QToolButton *button(Canvas &canvas, const char *name) {
    return bar(canvas) ? bar(canvas)->findChild<QToolButton *>(name) : nullptr;
}

QPolygonF transformBox(const Canvas &canvas) {
    const auto bounds = canvas.document()->transformBounds();
    QPolygonF result;
    for (const auto &point : {bounds.topLeft(), bounds.topRight(), bounds.bottomRight(), bounds.bottomLeft()})
        result.append(view(canvas, canvas.document()->transformMatrix().map(point)));
    return result;
}

void answerDialog(const char *name) {
    QTimer::singleShot(0, [name] {
        auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        QVERIFY2(dialog, "Expected a pending stamp/transform dialog");
        QCOMPARE(dialog->objectName(), QString("pendingTransformDialog"));
        auto *action = dialog->findChild<QPushButton *>(name); QVERIFY(action);
        action->click();
    });
}
}

class StampPlacementCanvasTests final : public QObject {
    Q_OBJECT
private slots:
    void oneContactCreatesOneFrozenStamp_data() {
        QTest::addColumn<bool>("pen"); QTest::addColumn<bool>("colored");
        for (bool pen : {false, true}) for (bool colored : {false, true})
            QTest::newRow(qPrintable(QString("%1-%2").arg(pen ? "pen" : "mouse", colored ? "colored" : "mask")))
                << pen << colored;
    }

    void oneContactCreatesOneFrozenStamp() {
        QFETCH(bool, pen); QFETCH(bool, colored);
        Canvas canvas; prepare(canvas, colored);
        QVERIFY(canvas.stampPlacementEnabled()); QVERIFY(canvas.stampPlacementAvailable());
        const auto before = renderMovieFrame(canvas.project());
        const QPointF origin(130, 110);
        const qreal pressure = pen ? .4 : 1;
        pointer(canvas, pen, QEvent::MouseButtonPress, origin, pressure);
        QVERIFY(canvas.document()->stampPlacementActive());
        QVERIFY(canvas.document()->transformActive());
        QVERIFY(!canvas.document()->strokeActive());
        QVERIFY(bar(canvas)); QVERIFY(bar(canvas)->isHidden());
        const auto bounds = canvas.document()->transformBounds();
        pointer(canvas, pen, QEvent::MouseMove, {185, 145}, .7);
        pointer(canvas, pen, QEvent::MouseMove, {225, 195}, .95);
        pointer(canvas, pen, QEvent::MouseButtonRelease, {225, 195});
        QCOMPARE(canvas.document()->transformBounds(), bounds);
        QVERIFY(canvas.document()->transformMatrix().isIdentity());
        QCOMPARE(renderMovieFrame(canvas.project()), before);
        QCOMPARE(canvas.document()->history()->count(), 0);
        QVERIFY(bar(canvas)); QVERIFY(bar(canvas)->isVisible());
        QVERIFY(button(canvas, "contextApply")); QVERIFY(button(canvas, "contextApply")->isVisible());
        QCOMPARE(button(canvas, "contextApply")->toolTip(), QString("Place stamp (Enter)"));
        QCOMPARE(button(canvas, "contextCancel")->toolTip(), QString("Discard stamp (Esc)"));
        QVERIFY(button(canvas, "contextMore")->isHidden());
        QVERIFY(button(canvas, "contextTransform")->isHidden());
        QVERIFY(button(canvas, "contextSelectionReplace")->isHidden());
        QVERIFY(canvas.rect().contains(bar(canvas)->geometry()));

        Document expected; expected.load(fixture());
        QVERIFY(expected.beginStroke(origin, pressure, stamp(colored), false)); expected.endStroke();
        if (pen) QTest::keyClick(&canvas, Qt::Key_Return);
        else button(canvas, "contextApply")->click();
        QVERIFY(!canvas.document()->stampPlacementActive());
        QVERIFY(!canvas.document()->transformActive());
        QCOMPARE(canvas.document()->history()->count(), 1);
        const auto placed = renderMovieFrame(canvas.project());
        QCOMPARE(placed, renderMovieFrame(expected.project()));
        QCOMPARE(placed.pixelColor(25, 30), before.pixelColor(25, 30));
        QVERIFY(bar(canvas)->isHidden());
        canvas.document()->history()->undo(); QCOMPARE(renderMovieFrame(canvas.project()), before);
        canvas.document()->history()->redo(); QCOMPARE(renderMovieFrame(canvas.project()), placed);
    }

    void handlesMoveScaleRotateOnlyTheStamp() {
        Canvas canvas; prepare(canvas, true);
        QPainterPath selection; selection.addRect(75, 35, 220, 205);
        canvas.document()->setRasterSelection(selection);
        const auto before = renderMovieFrame(canvas.project());
        contact(canvas);
        QVERIFY(canvas.document()->stampPlacementActive());
        QCOMPARE(canvas.document()->rasterSelection().path, selection);
        QVERIFY(QLineF(canvas.selectionViewportBounds().center(), transformBox(canvas).boundingRect().center()).length() < 1e-8);
        QVERIFY(canvas.selectionViewportBounds().width() < 100);

        auto box = transformBox(canvas);
        const QPointF offset(35, 22);
        drag(canvas, (box[0] + box[2]) / 2, (box[0] + box[2]) / 2 + offset);
        QVERIFY(QLineF(canvas.document()->transformParameters().translation, offset).length() < 1e-8);
        QVERIFY(bar(canvas)->isVisible());
        box = transformBox(canvas);
        const auto center = (box[0] + box[2]) / 2;
        drag(canvas, box[1], center + (box[1] - center) * 1.5);
        QVERIFY(qAbs(canvas.document()->transformParameters().scaleX - 1.5) < 1e-8);
        QVERIFY(qAbs(canvas.document()->transformParameters().scaleY - 1.5) < 1e-8);
        QVERIFY(bar(canvas)->isVisible());
        box = transformBox(canvas);
        const auto top = (box[0] + box[1]) / 2;
        auto outward = top - center; outward /= QLineF(center, top).length();
        const auto rotation = top + outward * 28;
        drag(canvas, rotation, center + QPointF(QLineF(center, rotation).length(), 0), true);
        QVERIFY(qAbs(canvas.document()->transformParameters().rotation - 90) < 1e-7);
        QVERIFY(bar(canvas)->isVisible());
        QCOMPARE(canvas.document()->history()->count(), 0);
        QCOMPARE(renderMovieFrame(canvas.project()), before);
        QCOMPARE(canvas.document()->rasterSelection().path, selection);
        const auto preview = renderMovieFrame(canvas.document()->displayProject());
        QTest::keyClick(&canvas, Qt::Key_Return);
        QCOMPARE(canvas.document()->history()->count(), 1);
        QCOMPARE(renderMovieFrame(canvas.project()), preview);
        QCOMPARE(renderMovieFrame(canvas.project()).pixelColor(25, 30), before.pixelColor(25, 30));
        QCOMPARE(canvas.document()->rasterSelection().path, selection);
        canvas.document()->history()->undo(); QCOMPARE(renderMovieFrame(canvas.project()), before);
    }

    void discardLeavesNoPaintOrHistory_data() {
        QTest::addColumn<bool>("keyboard");
        QTest::newRow("escape") << true; QTest::newRow("cancel-button") << false;
    }

    void discardLeavesNoPaintOrHistory() {
        QFETCH(bool, keyboard);
        Canvas canvas; prepare(canvas);
        const auto before = renderMovieFrame(canvas.project()); contact(canvas);
        QVERIFY(canvas.document()->stampPlacementActive());
        QVERIFY(renderMovieFrame(canvas.document()->displayProject()) != before);
        if (keyboard) QTest::keyClick(&canvas, Qt::Key_Escape);
        else { QVERIFY(button(canvas, "contextCancel")); button(canvas, "contextCancel")->click(); }
        QVERIFY(!canvas.document()->stampPlacementActive());
        QVERIFY(!canvas.document()->transformActive());
        QCOMPARE(renderMovieFrame(canvas.project()), before);
        QCOMPARE(canvas.document()->history()->count(), 0);
        QVERIFY(bar(canvas)->isHidden());
    }

    void ordinarySelectionActionsReturnAfterDiscard() {
        Canvas canvas; prepare(canvas); canvas.setSelectionBarVisible(true);
        QPainterPath selection; selection.addRect(75, 35, 220, 205);
        canvas.document()->setRasterSelection(selection);
        contact(canvas);
        QVERIFY(canvas.document()->stampPlacementActive());
        QVERIFY(button(canvas, "contextMore")->isHidden());
        button(canvas, "contextCancel")->click();
        QVERIFY(!canvas.document()->stampPlacementActive());
        QCOMPARE(canvas.document()->rasterSelection().path, selection);
        QVERIFY(bar(canvas)->isVisible());
        QVERIFY(button(canvas, "contextMore")->isVisible());
        QVERIFY(button(canvas, "contextSelectionReplace")->isVisible());
        QVERIFY(button(canvas, "contextApply")->isHidden());
        QCOMPARE(bar(canvas)->accessibleName(), QString("Selection actions"));
        QCOMPARE(button(canvas, "contextApply")->toolTip(), QString("Apply transform (Enter)"));
        QCOMPARE(canvas.document()->history()->count(), 0);
    }

    void penHoverAndBarClicksCannotPaintOrMoveStamp() {
        Canvas canvas; prepare(canvas);
        const auto before = renderMovieFrame(canvas.project());
        contact(canvas, {130, 110}, true, .55);
        QVERIFY(canvas.document()->stampPlacementActive());
        const auto bounds = canvas.document()->transformBounds();
        const auto preview = renderMovieFrame(canvas.document()->displayProject());
        pointer(canvas, true, QEvent::MouseMove, {270, 210}, 0, true);
        QCOMPARE(canvas.document()->transformBounds(), bounds);
        QVERIFY(canvas.document()->transformMatrix().isIdentity());
        QCOMPARE(renderMovieFrame(canvas.document()->displayProject()), preview);
        auto *popup = bar(canvas); QVERIFY(popup);
        auto *apply = button(canvas, "contextApply"); QVERIFY(apply);
        QVERIFY(popup->testAttribute(Qt::WA_NoMousePropagation));
        for (QWidget *target : {static_cast<QWidget *>(popup), static_cast<QWidget *>(apply)}) {
            const QPoint position = target == popup ? QPoint(2, 2) : target->rect().center();
            for (auto type : {QEvent::TabletPress, QEvent::TabletMove, QEvent::TabletRelease}) {
                const bool release = type == QEvent::TabletRelease;
                QTabletEvent event(type, &penDevice(), position, target->mapToGlobal(position), release ? 0 : .5,
                    0, 0, 0, 0, 0, Qt::NoModifier, type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton,
                    release ? Qt::NoButton : Qt::LeftButton);
                event.setAccepted(false); QApplication::sendEvent(target, &event);
                QVERIFY(!event.isAccepted());
                QVERIFY(canvas.document()->stampPlacementActive());
                QCOMPARE(renderMovieFrame(canvas.project()), before);
                QCOMPARE(canvas.document()->history()->count(), 0);
            }
        }
        QTest::mouseClick(apply, Qt::LeftButton);
        QCOMPARE(renderMovieFrame(canvas.project()), preview);
        QCOMPARE(canvas.document()->history()->count(), 1);
    }

    void outsideContactCommitsAndStartsTheNextStamp() {
        Canvas canvas; prepare(canvas);
        const auto before = renderMovieFrame(canvas.project());
        contact(canvas, {105, 90});
        QVERIFY(canvas.document()->stampPlacementActive());
        const auto first = renderMovieFrame(canvas.document()->displayProject());
        contact(canvas, {250, 200});
        QVERIFY(canvas.document()->stampPlacementActive());
        QCOMPARE(canvas.document()->history()->count(), 1);
        QCOMPARE(renderMovieFrame(canvas.project()), first);
        QVERIFY(canvas.document()->transformBounds().contains(QPointF(250, 200)));
        QTest::keyClick(&canvas, Qt::Key_Return);
        QCOMPARE(canvas.document()->history()->count(), 2);
        Document expected; expected.load(fixture());
        for (const auto &point : {QPointF(105, 90), QPointF(250, 200)}) {
            QVERIFY(expected.beginStroke(point, 1, stamp(), false)); expected.endStroke();
        }
        QCOMPARE(renderMovieFrame(canvas.project()), renderMovieFrame(expected.project()));
        canvas.document()->history()->undo(); QCOMPARE(renderMovieFrame(canvas.project()), first);
        canvas.document()->history()->undo(); QCOMPARE(renderMovieFrame(canvas.project()), before);
    }

    void missingPenReleaseEndsContactWithoutAStampTrail() {
        Canvas canvas; prepare(canvas);
        pointer(canvas, true, QEvent::MouseButtonPress, {130, 110}, .5);
        QVERIFY(canvas.document()->stampPlacementActive());
        const auto preview = renderMovieFrame(canvas.document()->displayProject());
        pointer(canvas, true, QEvent::MouseMove, {280, 210}, 0, true);
        QVERIFY(canvas.document()->stampPlacementActive());
        QVERIFY(!canvas.document()->strokeActive());
        QCOMPARE(renderMovieFrame(canvas.document()->displayProject()), preview);
        QCOMPARE(canvas.document()->history()->count(), 0);
        QVERIFY(bar(canvas)); QVERIFY(bar(canvas)->isVisible());
        button(canvas, "contextApply")->click();
        QCOMPARE(renderMovieFrame(canvas.project()), preview);
        QCOMPARE(canvas.document()->history()->count(), 1);
    }

    void toolSwitchOffersApplyDiscardAndCancelEvenWithoutMoving() {
        Canvas canvas; prepare(canvas);
        const auto before = renderMovieFrame(canvas.project()); contact(canvas);
        const auto preview = renderMovieFrame(canvas.document()->displayProject());
        answerDialog("cancelPendingTransform"); canvas.setTool(Canvas::Tool::Hand);
        QCOMPARE(canvas.tool(), Canvas::Tool::Brush);
        QVERIFY(canvas.document()->stampPlacementActive());
        QCOMPARE(canvas.document()->history()->count(), 0);
        QCOMPARE(renderMovieFrame(canvas.document()->displayProject()), preview);
        answerDialog("discardPendingTransform"); canvas.setTool(Canvas::Tool::Hand);
        QCOMPARE(canvas.tool(), Canvas::Tool::Hand);
        QVERIFY(!canvas.document()->stampPlacementActive());
        QCOMPARE(renderMovieFrame(canvas.project()), before);
        QCOMPARE(canvas.document()->history()->count(), 0);
        canvas.setTool(Canvas::Tool::Brush); contact(canvas);
        answerDialog("applyPendingTransform"); canvas.setTool(Canvas::Tool::Eraser);
        QCOMPARE(canvas.tool(), Canvas::Tool::Eraser);
        QVERIFY(!canvas.document()->stampPlacementActive());
        QCOMPARE(renderMovieFrame(canvas.project()), preview);
        QCOMPARE(canvas.document()->history()->count(), 1);
    }

    void disabledModeKeepsTheNormalSampledBrushStroke() {
        Canvas canvas; prepare(canvas); canvas.setStampPlacementEnabled(false);
        QVERIFY(!canvas.stampPlacementEnabled());
        pointer(canvas, false, QEvent::MouseButtonPress, {80, 90});
        pointer(canvas, false, QEvent::MouseMove, {200, 90});
        pointer(canvas, false, QEvent::MouseButtonRelease, {200, 90});
        QVERIFY(!canvas.document()->stampPlacementActive());
        QVERIFY(!canvas.document()->transformActive());
        Document expected; expected.load(fixture());
        QVERIFY(expected.beginStroke({80, 90}, 1, stamp(), false));
        expected.continueStroke({200, 90}, 1); expected.endStroke();
        QCOMPARE(renderMovieFrame(canvas.project()), renderMovieFrame(expected.project()));
        QCOMPARE(canvas.document()->history()->count(), 1);
    }

    void pendingPlacementScreenshot() {
        const auto destination = qEnvironmentVariable("TOON_STAMP_SCREENSHOT");
        if (destination.isEmpty()) return;
        applyTheme();
        auto project = fixture();
        project.width = 640; project.height = 480;
        project.movie = "Stamp placement";
        auto &shot = project.shots[0]; shot.name = "Adjust before placing";
        shot.sheet.left = shot.sheet.right = shot.sheet.top = shot.sheet.bottom = 24;
        shot.sheet.header = 120;
        Document painted; painted.load(project);
        BrushSettings paint; paint.size = 45; paint.pressureSize = false; paint.color = QColor("#4d7396");
        QVERIFY(painted.beginStroke({90, 300}, 1, paint, false));
        painted.continueStroke({175, 265}, 1); painted.continueStroke({250, 320}, 1); painted.endStroke();
        Canvas canvas; canvas.resize(1024, 760); canvas.setProject(painted.project());
        canvas.setBrushSettings(stamp(true)); canvas.setBrushSize(140);
        canvas.setStampPlacementEnabled(true); canvas.setSelectionBarVisible(false);
        canvas.show(); canvas.fitSheet(); QApplication::processEvents();
        contact(canvas, {400, 240});
        QVERIFY(canvas.document()->stampPlacementActive());
        TransformParameters parameters; parameters.scaleX = parameters.scaleY = 1.5; parameters.rotation = 32;
        QVERIFY(canvas.document()->previewTransform(parameters));
        QVERIFY(bar(canvas)); QVERIFY(bar(canvas)->isVisible());
        QVERIFY(canvas.rect().contains(bar(canvas)->geometry()));
        QVERIFY(QDir().mkpath(QFileInfo(destination).absolutePath()));
        QVERIFY(canvas.grab().save(destination));
    }
};

QTEST_MAIN(StampPlacementCanvasTests)
#include "test_stamp_placement_canvas.moc"
