#include "light_table.h"
#include "canvas.h"
#include "sheet.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFocusEvent>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPointingDevice>
#include <QSignalSpy>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QToolButton>
#include <QUuid>
#include <QtTest>
#include <limits>

using namespace toon;
namespace {
QImage drawing(QColor color = Qt::red) {
    QImage image(256, 256, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    QPainter painter(&image); painter.fillRect(QRect(20, 30, 80, 90), color); return image;
}
Project fixture() {
    auto project = Project::create(); project.width = 320; project.height = 240;
    project.shots[0].sheet.overscan = 0;
    project.shots[0].layers[0].tiles[{0, 0}] = drawing();
    auto second = Project::create().shots[0]; second.number = "C002"; second.sheet.overscan = 0;
    second.layers[0].name = "Reverse angle"; second.layers[0].tiles[{0, 0}] = drawing(Qt::blue);
    project.shots.append(second); return project;
}
QJsonObject artworkMetadata(Project project) { project.drawingAids = {}; return project.toJson(); }
void setup(Canvas &canvas) {
    canvas.resize(800, 650); canvas.setProject(fixture()); canvas.show(); canvas.fitSheet(); QApplication::processEvents();
}
void mouse(Canvas &canvas, QEvent::Type type, QPointF position) {
    const auto button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent event(type, position, canvas.mapToGlobal(position), button,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &event);
}
void pen(Canvas &canvas, QEvent::Type type, QPointF position) {
    static QPointingDevice device("Light table pen", 611, QInputDevice::DeviceType::Stylus,
        QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
    const bool release = type == QEvent::TabletRelease;
    QTabletEvent event(type, &device, position, canvas.mapToGlobal(position), release ? 0 : .6,
                      0, 0, 0, 0, 0, Qt::NoModifier, type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton,
                      release ? Qt::NoButton : Qt::LeftButton);
    QApplication::sendEvent(&canvas, &event);
}
}
class LightTableTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void pinsAcrossShotsWithoutChangingSourceOrSelection() {
        Canvas canvas; setup(canvas); LightTablePanel panel(&canvas); panel.show();
        const auto before = artworkMetadata(canvas.project()); const auto source = canvas.shot().layers[0].tiles;
        const auto exportBefore = renderMovieFrame(canvas.project());
        const auto selectedShot = canvas.document()->activeShotId();
        QString error;
        QVERIFY2(panel.pinCurrentLayer(&error), qPrintable(error));
        QTRY_COMPARE(panel.pins().size(), 1); QVERIFY(!panel.busy());
        QCOMPARE(panel.pins()[0].bounds, QRectF(20, 30, 80, 90));
        QVERIFY2(panel.pinLayer(canvas.project().shots[1].id, canvas.project().shots[1].layers[0].id, &error), qPrintable(error));
        QTRY_COMPARE(panel.pins().size(), 2);
        QCOMPARE(canvas.document()->activeShotId(), selectedShot); QCOMPARE(canvas.document()->activeLayer(), 0);
        QVERIFY(panel.setPinTransform(0, {45, -22}, 1.8, 37));
        QVERIFY(panel.setPinOpacity(0, .23)); panel.setPinVisible(1, false);
        panel.flip(-1); QVERIFY(panel.solo()); QCOMPARE(panel.selectedPin(), 0);
        panel.setOverlaysVisible(false); panel.setOverlaysVisible(true);
        QCOMPARE(artworkMetadata(canvas.project()), before); QCOMPARE(canvas.shot().layers[0].tiles, source);
        QCOMPARE(renderMovieFrame(canvas.project()), exportBefore); QCOMPARE(canvas.document()->history()->count(), 0);
        QVERIFY(canvas.document()->selectShot(canvas.project().shots[1].id));
        QCOMPARE(panel.pins().size(), 2); QCOMPARE(panel.pins()[0].offset, QPointF(45, -22));
    }
    void snapshotRefreshAndSourceRemovalAreExplicit() {
        Canvas canvas; setup(canvas); LightTablePanel panel(&canvas); panel.show(); QString error;
        QVERIFY(panel.pinCurrentLayer(&error)); QTRY_COMPARE(panel.pins().size(), 1);
        const auto initial = panel.pins()[0].image;
        auto changed = canvas.project(); changed.shots[0].layers[0].tiles[{0, 0}] = drawing(Qt::green);
        canvas.document()->replaceProject(changed, "Change drawing");
        QCOMPARE(panel.pins()[0].image, initial); QVERIFY(panel.setPinTransform(0, {20, 10}, 2, 45));
        QVERIFY(panel.refreshSelected(&error)); QTRY_VERIFY(!panel.busy());
        QVERIFY(panel.pins()[0].image != initial); QCOMPARE(panel.pins()[0].image.pixelColor(10, 10), QColor(Qt::green));
        QCOMPARE(panel.pins()[0].offset, QPointF(20, 10)); QCOMPARE(panel.pins()[0].scale, 2.);
        changed = canvas.project(); changed.shots[0].layers[0].id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        canvas.document()->replaceProject(changed, "Replace source");
        const auto stable = panel.pins()[0].image;
        QVERIFY(!panel.refreshSelected(&error)); QVERIFY(error.contains("no longer"));
        QCOMPARE(panel.pins()[0].image, stable);
    }
    void canvasMoveScaleRotateAndEscapeKeepArtworkIntact() {
        Canvas canvas; setup(canvas); LightTablePanel panel(&canvas); panel.show();
        QVERIFY(panel.pinImage(drawing(), "Pose"));
        const auto before = artworkMetadata(canvas.project()); const auto pixels = canvas.shot().layers[0].tiles;
        QSignalSpy editing(&panel, &LightTablePanel::editingChanged);
        panel.setEditing(true); QVERIFY(panel.editing());
        const auto start = panel.artworkToViewport().map(panel.pins()[0].bounds.center());
        const QPointF delta(30, 20);
        mouse(canvas, QEvent::MouseButtonPress, start); mouse(canvas, QEvent::MouseMove, start + delta);
        mouse(canvas, QEvent::MouseButtonRelease, start + delta);
        const auto expected = canvas.artworkPoint(start + delta) - canvas.artworkPoint(start);
        QVERIFY(QLineF(panel.pins()[0].offset, expected).length() < .001);
        const auto corners = panel.selectedViewportPolygon();
        const auto center = panel.artworkToViewport().map(panel.pins()[0].bounds.center() + panel.pins()[0].offset);
        mouse(canvas, QEvent::MouseButtonPress, corners[2]);
        mouse(canvas, QEvent::MouseMove, center + (corners[2] - center) * 1.5);
        mouse(canvas, QEvent::MouseButtonRelease, center + (corners[2] - center) * 1.5);
        QVERIFY(qAbs(panel.pins()[0].scale - 1.5) < .001);
        const auto handle = panel.rotationHandle(), vector = handle - center;
        const auto quarterTurn = center + QPointF(-vector.y(), vector.x());
        mouse(canvas, QEvent::MouseButtonPress, handle); mouse(canvas, QEvent::MouseMove, quarterTurn);
        mouse(canvas, QEvent::MouseButtonRelease, quarterTurn);
        QVERIFY(qAbs(panel.pins()[0].rotation - 90) < .001);
        const auto stable = panel.pins()[0].offset;
        const auto midpoint = panel.artworkToViewport().map(panel.pins()[0].bounds.center() + stable);
        mouse(canvas, QEvent::MouseButtonPress, midpoint); mouse(canvas, QEvent::MouseMove, midpoint + delta);
        QTest::keyClick(&canvas, Qt::Key_Escape); QVERIFY(!panel.editing());
        QCOMPARE(panel.pins()[0].offset, stable); QCOMPARE(editing.count(), 2);
        QCOMPARE(artworkMetadata(canvas.project()), before); QCOMPARE(canvas.shot().layers[0].tiles, pixels);
        QCOMPARE(canvas.document()->history()->count(), 0);
    }
    void penEditingAndDrawingThroughUseSeparateGestures() {
        Canvas canvas; setup(canvas); LightTablePanel panel(&canvas); panel.show();
        QVERIFY(panel.pinImage(drawing(), "Pose")); panel.setEditing(true);
        const auto start = panel.artworkToViewport().map(panel.pins()[0].bounds.center());
        pen(canvas, QEvent::TabletPress, start); pen(canvas, QEvent::TabletMove, start + QPointF(25, 15));
        pen(canvas, QEvent::TabletRelease, start + QPointF(25, 15));
        QVERIFY(!panel.pins()[0].offset.isNull()); QCOMPARE(canvas.document()->history()->count(), 0);
        QVERIFY(!canvas.document()->strokeActive());
        panel.setEditing(false); canvas.setTool(Canvas::Tool::Brush); canvas.setHoldShapeEnabled(false);
        canvas.setStabilization(0); canvas.setBrushColor(Qt::black);
        const auto a = panel.artworkToViewport().map(QPointF(160, 140));
        const auto b = panel.artworkToViewport().map(QPointF(205, 150));
        pen(canvas, QEvent::TabletPress, a); pen(canvas, QEvent::TabletMove, b); pen(canvas, QEvent::TabletRelease, b);
        QCOMPARE(canvas.document()->history()->count(), 1); QVERIFY(!canvas.document()->strokeActive());
        QCOMPARE(panel.pins().size(), 1);
    }
    void mirrorWaitsForPinContactAndRecoversOnEveryExit_data() {
        QTest::addColumn<int>("exit");QTest::addColumn<bool>("tablet");
        QTest::newRow("mouse-release")<<0<<false;QTest::newRow("pen-release")<<0<<true;
        QTest::newRow("escape")<<1<<false;QTest::newRow("lost-mouse-release")<<2<<false;
        QTest::newRow("lost-pen-release")<<2<<true;QTest::newRow("hide")<<3<<false;
        QTest::newRow("destroy-owner")<<4<<false;QTest::newRow("save-finishes")<<5<<false;
        QTest::newRow("focus-loss")<<6<<false;
    }
    void mirrorWaitsForPinContactAndRecoversOnEveryExit() {
        QFETCH(int,exit);QFETCH(bool,tablet);
        Canvas canvas;setup(canvas);auto *panel=new LightTablePanel(&canvas);panel->show();
        QVERIFY(panel->pinImage(drawing(),"Pose"));panel->setEditing(true);QVERIFY(panel->editing());
        const auto start=panel->artworkToViewport().map(panel->pins()[0].bounds.center());
        if(tablet)pen(canvas,QEvent::TabletPress,start);else mouse(canvas,QEvent::MouseButtonPress,start);
        canvas.setCanvasMirrored(true);QVERIFY(!canvas.canvasMirrored());
        if(tablet)pen(canvas,QEvent::TabletMove,start+QPointF(18,10));else mouse(canvas,QEvent::MouseMove,start+QPointF(18,10));
        const auto moved=panel->pins()[0].offset;QVERIFY(!moved.isNull());
        canvas.setCanvasMirrored(true);QVERIFY(!canvas.canvasMirrored());QCOMPARE(panel->pins()[0].offset,moved);
        if(exit==0){if(tablet)pen(canvas,QEvent::TabletRelease,start+QPointF(18,10));else mouse(canvas,QEvent::MouseButtonRelease,start+QPointF(18,10));}
        else if(exit==1)QTest::keyClick(&canvas,Qt::Key_Escape);
        else if(exit==2){
            if(tablet){
                static QPointingDevice device("Lost contact guard pen",613,QInputDevice::DeviceType::Stylus,
                    QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
                QTabletEvent event(QEvent::TabletMove,&device,start,canvas.mapToGlobal(start),0,0,0,0,0,0,Qt::NoModifier,Qt::NoButton,Qt::NoButton);
                QApplication::sendEvent(&canvas,&event);
            }else{QMouseEvent event(QEvent::MouseMove,start,canvas.mapToGlobal(start),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&canvas,&event);}
        }
        else if(exit==3)panel->hide();
        else if(exit==4){delete panel;panel=nullptr;}
        else if(exit==5)QVERIFY(panel->finishPersistence());
        else{QFocusEvent event(QEvent::FocusOut);QApplication::sendEvent(&canvas,&event);}
        canvas.setCanvasMirrored(true);QVERIFY(canvas.canvasMirrored());
        QCOMPARE(canvas.document()->history()->count(),0);delete panel;
    }
    void rotatedCameraMappingAndFocusedNumberDraft() {
        Canvas canvas; setup(canvas); LightTablePanel panel(&canvas); panel.show(); panel.resize(220, 410); panel.show();
        Camera2D camera; camera.position = {18, -10}; camera.zoom = 1.4; camera.rotation = 22;
        QVERIFY(canvas.document()->setShotCamera(camera)); canvas.setCameraView(true);
        canvas.setRotation(31); canvas.setZoom(.85);
        QVERIFY(panel.pinImage(drawing(), "Pose"));
        for (const QPointF point : {QPointF(20, 30), QPointF(200, 180), QPointF(-40, 22)})
            QVERIFY(QLineF(canvas.artworkPoint(panel.artworkToViewport().map(point)), point).length() < .0001);
        auto *value = panel.findChild<ValueSlider *>("lightTableX"); QVERIFY(value);
        auto *number = value->findChild<QDoubleSpinBox *>(); auto *text = number->findChild<QLineEdit *>();
        text->setFocus(); text->selectAll(); QTest::keyClicks(text, "123");
        const auto draft = text->text();
        canvas.document()->addCharacter("Test"); QCOMPARE(text->text(), draft);
        QTest::keyClick(text, Qt::Key_Return); QCOMPARE(panel.pins()[0].offset.x(), 123.);
        const auto directory = qEnvironmentVariable("TOON_LIGHT_TABLE_CAPTURE_DIR");
        if (!directory.isEmpty()) {
            QDir().mkpath(directory); QVERIFY(panel.grab().save(directory + "/light-table-220.png"));
            QVERIFY(canvas.grab().save(directory + "/light-table-canvas.png"));
        }
    }
    void hiddenPanelAndLostReleasesDoNotInterceptOrMovePins() {
        Canvas canvas; setup(canvas); LightTablePanel panel(&canvas); panel.show();
        QVERIFY(panel.pinImage(drawing(), "Pose")); panel.setEditing(true); QVERIFY(panel.editing());
        const auto start = panel.artworkToViewport().map(panel.pins()[0].bounds.center());
        pen(canvas, QEvent::TabletPress, start); pen(canvas, QEvent::TabletMove, start + QPointF(15, 10));
        const auto lastPen = panel.pins()[0].offset;
        static QPointingDevice device("Hover pen", 612, QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
        QTabletEvent hover(QEvent::TabletMove, &device, start + QPointF(250, 100),
            canvas.mapToGlobal(start + QPointF(250, 100)), 0, 0, 0, 0, 0, 0,
            Qt::NoModifier, Qt::NoButton, Qt::NoButton);
        QApplication::sendEvent(&canvas, &hover); QCOMPARE(panel.pins()[0].offset, lastPen);
        const auto movedCenter = panel.artworkToViewport().map(panel.pins()[0].bounds.center() + lastPen);
        mouse(canvas, QEvent::MouseButtonPress, movedCenter);
        mouse(canvas, QEvent::MouseMove, movedCenter + QPointF(12, 6));
        const auto lastMouse = panel.pins()[0].offset;
        QMouseEvent mouseHover(QEvent::MouseMove, movedCenter + QPointF(250, 100),
            canvas.mapToGlobal(movedCenter + QPointF(250, 100)), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &mouseHover); QCOMPARE(panel.pins()[0].offset, lastMouse);
        panel.hide(); QVERIFY(!panel.editing()); panel.setEditing(true); QVERIFY(!panel.editing());
        canvas.setHoldShapeEnabled(false); canvas.setStabilization(0);
        const auto a = panel.artworkToViewport().map(QPointF(150, 150));
        mouse(canvas, QEvent::MouseButtonPress, a); mouse(canvas, QEvent::MouseButtonRelease, a + QPointF(20, 0));
        QCOMPARE(canvas.document()->history()->count(), 1);
    }
    void middleAndSpacePanWithoutTransformingPin() {
        Canvas canvas; setup(canvas); LightTablePanel panel(&canvas); panel.show();
        QVERIFY(panel.pinImage(drawing(), "Pose")); panel.setEditing(true);
        const auto start = panel.artworkToViewport().map(panel.pins()[0].bounds.center());
        const auto dispatch = [&](QEvent::Type type, QPointF position, Qt::MouseButton button, Qt::MouseButtons buttons) {
            QMouseEvent event(type, position, canvas.mapToGlobal(position), button, buttons, Qt::NoModifier);
            QApplication::sendEvent(&canvas, &event);
        };
        const auto center = canvas.center();
        dispatch(QEvent::MouseButtonPress, start, Qt::MiddleButton, Qt::MiddleButton);
        dispatch(QEvent::MouseMove, start + QPointF(30, 10), Qt::NoButton, Qt::MiddleButton);
        dispatch(QEvent::MouseButtonRelease, start + QPointF(30, 10), Qt::MiddleButton, Qt::NoButton);
        QVERIFY(canvas.center() != center); QCOMPARE(panel.pins()[0].offset, QPointF());
        const auto middleCenter = canvas.center(); QTest::keyPress(&canvas, Qt::Key_Space);
        mouse(canvas, QEvent::MouseButtonPress, start); mouse(canvas, QEvent::MouseMove, start + QPointF(-20, 15));
        // Releasing Space before the pen/mouse must still deliver release to Canvas.
        QTest::keyRelease(&canvas, Qt::Key_Space); mouse(canvas, QEvent::MouseButtonRelease, start + QPointF(-20, 15));
        QVERIFY(canvas.center() != middleCenter); QCOMPARE(panel.pins()[0].offset, QPointF());
        dispatch(QEvent::MouseButtonPress, start, Qt::RightButton, Qt::RightButton);
        dispatch(QEvent::MouseMove, start + QPointF(40, 25), Qt::NoButton, Qt::RightButton);
        dispatch(QEvent::MouseButtonRelease, start + QPointF(40, 25), Qt::RightButton, Qt::NoButton);
        QCOMPARE(panel.pins()[0].offset, QPointF()); QCOMPARE(canvas.document()->history()->count(), 0);
    }
    void penRedrawsReuseViewportComposite() {
        Canvas canvas; setup(canvas); LightTablePanel panel(&canvas); panel.show();
        QVERIFY(panel.pinImage(drawing(), "Pose"));
        canvas.grab(); const auto initial = panel.overlayBuildCount(); QVERIFY(initial > 0);
        for (int pass = 0; pass < 4; ++pass) { canvas.update(); canvas.grab(); }
        QCOMPARE(panel.overlayBuildCount(), initial);
        QVERIFY(panel.setPinTransform(0, {20, 30}, 1.3, 25)); canvas.grab();
        QCOMPARE(panel.overlayBuildCount(), initial + 1);
        canvas.setZoom(canvas.zoom() * 1.2); canvas.grab();
        QCOMPARE(panel.overlayBuildCount(), initial + 2);
    }
    void budgetsAndSwitchingProjectsRejectStaleSnapshots() {
        Canvas canvas; setup(canvas); LightTablePanel panel(&canvas); panel.show(); QString error;
        for (int index = 0; index < 8; ++index) QVERIFY(panel.pinImage(drawing(), QString::number(index), &error));
        QVERIFY(!panel.pinImage(drawing(), "Ninth", &error)); QVERIFY(error.contains("eight"));
        QVERIFY(!panel.setPinTransform(0, {}, 0, 0));
        QVERIFY(!panel.setPinTransform(0, {std::numeric_limits<double>::infinity(), 0}, 1, 0));
        QVERIFY(!panel.setPinOpacity(0, std::numeric_limits<double>::quiet_NaN()));
        panel.setEditing(true); panel.setPinVisible(panel.selectedPin(), false); QVERIFY(!panel.editing());
        panel.removeSelected(); QCOMPARE(panel.pins().size(), 7); panel.resetSelected();
        QCOMPARE(panel.pins()[panel.selectedPin()].transform(), QTransform());
        canvas.setProject(fixture()); QVERIFY(panel.pins().isEmpty()); QVERIFY(!panel.editing());
        QVERIFY(panel.pinCurrentLayer(&error)); canvas.setProject(fixture());
        QTest::qWait(150); QVERIFY(panel.pins().isEmpty()); QVERIFY(!panel.busy());
        QCOMPARE(canvas.document()->history()->count(), 0);
    }
    void saveReloadEmbedsPinsAndRestoresEveryPlacementSetting() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto imagePath = directory.filePath("reference.png"); QVERIFY(drawing(Qt::yellow).save(imagePath));
        Canvas canvas; setup(canvas); LightTablePanel panel(&canvas); panel.show();
        QString error;
        QVERIFY2(panel.pinLayer(canvas.project().shots[1].id, canvas.project().shots[1].layers[0].id, &error), qPrintable(error));
        QTRY_COMPARE(panel.pins().size(), 1);
        QVERIFY2(panel.importImage(imagePath, &error), qPrintable(error)); QCOMPARE(panel.pins().size(), 2);
        QVERIFY(panel.setPinTransform(0, {-123.5, 72.25}, 1.75, -47.5));
        QVERIFY(panel.setPinOpacity(0, .31)); panel.setPinVisible(1, false);
        panel.selectPin(0); panel.setSolo(true); panel.setOverlaysVisible(false);
        QVERIFY(panel.finishPersistence());
        const auto original = panel.pins(); const auto artwork = renderMovieFrame(canvas.project());
        const auto file = directory.filePath("light-table.toonproj");
        QVERIFY2(saveProject(canvas.project(), file, &error), qPrintable(error));
        QVERIFY(QFile::remove(imagePath));
        auto loaded = loadProject(file, &error); QVERIFY2(loaded.has_value(), qPrintable(error));
        Canvas reopened; reopened.setProject(*loaded); LightTablePanel restored(&reopened); restored.show();
        QCOMPARE(restored.pins().size(), 2); QCOMPARE(restored.selectedPin(), 0);
        QVERIFY(restored.solo()); QVERIFY(!restored.overlaysVisible()); QVERIFY(!restored.editing());
        for (int index = 0; index < 2; ++index) {
            const auto &a = original[index], &b = restored.pins()[index];
            QCOMPARE(b.id, a.id); QCOMPARE(b.name, a.name); QCOMPARE(b.image, a.image); QCOMPARE(b.bounds, a.bounds);
            QCOMPARE(b.offset, a.offset); QCOMPARE(b.scale, a.scale); QCOMPARE(b.rotation, a.rotation);
            QCOMPARE(b.opacity, a.opacity); QCOMPARE(b.visible, a.visible);
            QCOMPARE(b.shotId, a.shotId); QCOMPARE(b.layerId, a.layerId);
        }
        QCOMPARE(reopened.document()->history()->count(), 0); QVERIFY(!reopened.document()->drawingAidsDirty());
        QCOMPARE(renderMovieFrame(reopened.project()), artwork);
        // Loading into an existing panel replaces rather than duplicates its snapshots.
        reopened.setProject(*loaded); QCOMPARE(restored.pins().size(), 2);
        QCOMPARE(restored.pins()[0].id, original[0].id); QVERIFY(!reopened.document()->drawingAidsDirty());
    }
    void completedGesturesPublishOnceAndFocusedTextFlushesForSave() {
        Canvas canvas; setup(canvas); LightTablePanel panel(&canvas); panel.show();
        QVERIFY(panel.pinImage(drawing(), "Pose")); canvas.document()->markDrawingAidsSaved();
        QVERIFY(!canvas.document()->drawingAidsDirty()); panel.setEditing(true);
        QSignalSpy published(canvas.document(), &Document::drawingAidsChanged);
        const auto start = panel.artworkToViewport().map(panel.pins()[0].bounds.center());
        mouse(canvas, QEvent::MouseButtonPress, start);
        for (int index = 1; index <= 5; ++index) mouse(canvas, QEvent::MouseMove, start + QPointF(index * 5, index * 3));
        QCOMPARE(published.count(), 0); QCOMPARE(canvas.project().drawingAids.lightTable.pins[0].offset, QPointF());
        mouse(canvas, QEvent::MouseButtonRelease, start + QPointF(25, 15));
        QCOMPARE(published.count(), 1); QVERIFY(canvas.document()->drawingAidsDirty());
        QCOMPARE(canvas.project().drawingAids.lightTable.pins[0].offset, panel.pins()[0].offset);
        QCOMPARE(canvas.document()->history()->count(), 0);
        canvas.document()->markDrawingAidsSaved(); published.clear(); panel.setEditing(false);
        auto *value = panel.findChild<ValueSlider *>("lightTableX");
        auto *text = value->findChild<QDoubleSpinBox *>()->findChild<QLineEdit *>();
        text->setFocus(); text->selectAll(); QTest::keyClicks(text, "145.5");
        QCOMPARE(published.count(), 0); QVERIFY(!canvas.document()->drawingAidsDirty());
        QVERIFY(panel.finishPersistence()); QCOMPARE(panel.pins()[0].offset.x(), 145.5);
        QCOMPARE(canvas.project().drawingAids.lightTable.pins[0].offset.x(), 145.5);
        QCOMPARE(published.count(), 1); QVERIFY(canvas.document()->drawingAidsDirty());
        // Another drawing-aid update must not overwrite this pin's unfinished number.
        text->selectAll(); QTest::keyClicks(text, "177.5");
        auto aids = canvas.project().drawingAids; aids.activeShotId = canvas.project().shots[1].id;
        QVERIFY(canvas.document()->setDrawingAids(aids)); QCOMPARE(text->text(), QString("177.5"));
        QTest::keyClick(text, Qt::Key_Escape); QCOMPARE(panel.pins()[0].offset.x(), 145.5);
        QCOMPARE(canvas.project().drawingAids.lightTable.pins[0].offset.x(), 145.5);
    }
    void artworkUndoKeepsCurrentPinsAndSaveWaitsForSnapshot() {
        Canvas canvas; setup(canvas); LightTablePanel panel(&canvas); panel.show();
        canvas.document()->addLayer();
        QString error; QVERIFY(panel.pinLayer(canvas.project().shots[0].id, canvas.project().shots[0].layers[0].id, &error));
        QVERIFY(panel.busy()); QVERIFY(!panel.finishPersistence());
        QTRY_COMPARE(panel.pins().size(), 1); QVERIFY(panel.finishPersistence());
        const auto id = panel.pins()[0].id; const auto image = panel.pins()[0].image;
        QVERIFY(panel.setPinTransform(0, {33, 21}, 1.5, 25));
        canvas.document()->history()->undo(); QCOMPARE(panel.pins().size(), 1);
        QCOMPARE(panel.pins()[0].id, id); QCOMPARE(panel.pins()[0].image, image); QCOMPARE(panel.pins()[0].offset, QPointF(33, 21));
        QCOMPARE(canvas.project().drawingAids.lightTable.pins.size(), 1);
        canvas.document()->history()->redo(); QCOMPARE(panel.pins().size(), 1);
        QCOMPARE(canvas.project().drawingAids.lightTable.pins[0].id, id);
        // Explicit removal is persisted without touching artwork history.
        const auto history = canvas.document()->history()->count(); panel.removeSelected();
        QVERIFY(canvas.project().drawingAids.lightTable.pins.isEmpty()); QCOMPARE(canvas.document()->history()->count(), history);
    }
    void snapshotFinishingDuringStrokeCannotLeakIntoAnotherProject() {
        // Keep this transaction-level regression independent of window focus:
        // showing a second top-level panel legitimately finishes Canvas strokes.
        Canvas canvas; canvas.setProject(fixture()); LightTablePanel panel(&canvas);
        QString error; QVERIFY(panel.pinCurrentLayer(&error));
        QVERIFY(canvas.document()->beginStroke({160, 140}, .6, BrushSettings{}, false));
        QTRY_COMPARE(panel.pins().size(), 1);
        QVERIFY(canvas.document()->strokeActive());
        // Pin completion can precede pen-up. Its metadata waits for the artwork
        // transaction and must never publish into a subsequently loaded project.
        QVERIFY(canvas.project().drawingAids.lightTable.pins.isEmpty());
        canvas.setProject(fixture());
        QVERIFY(panel.pins().isEmpty()); QVERIFY(canvas.project().drawingAids.lightTable.pins.isEmpty());
        QVERIFY(!canvas.document()->drawingAidsDirty()); QCOMPARE(canvas.document()->history()->count(), 0);
    }
    void pinFolderRespectsChildVisibilityAndWidgetLifetime() {
        Canvas canvas; auto project = fixture(); auto &layers = project.shots[0].layers;
        auto group = RasterLayer::create("Completed cel"); group.group = true;
        layers[0].parentId = group.id; layers.prepend(group);
        auto hidden = RasterLayer::create("Hidden rough"); hidden.parentId = group.id; hidden.visible = false;
        hidden.tiles[{0, 0}] = drawing(Qt::yellow); layers.append(hidden);
        canvas.setProject(project); canvas.document()->selectLayer(0);
        auto *panel = new LightTablePanel(&canvas); panel->show(); QString error;
        QVERIFY(panel->pinCurrentLayer(&error)); QTRY_COMPARE(panel->pins().size(), 1);
        QCOMPARE(panel->pins()[0].image.pixelColor(20, 20), QColor(Qt::red));
        panel->setEditing(true); delete panel; // Canvas outlives its dock safely.
        auto *canvasFirst = new Canvas; canvasFirst->setProject(fixture());
        auto *panelLast = new LightTablePanel(canvasFirst);
        delete canvasFirst; delete panelLast; // Dock may outlive the central widget.
    }
};
QTEST_MAIN(LightTableTest)
#include "test_light_table.moc"
