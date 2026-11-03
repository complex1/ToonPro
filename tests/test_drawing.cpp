#include "canvas.h"
#include "main_window.h"
#include <QApplication>
#include <QDockWidget>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPointingDevice>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace toon;
static Project transparentProject() { auto p = Project::create(); p.transparent = true; return p; }
static void line(Document &doc, QPointF from, QPointF to, QColor color = Qt::black, bool eraser = false, qreal size = 20) {
    BrushSettings brush; brush.color = color; brush.size = size;
    doc.beginStroke(from, 1, brush, eraser); doc.continueStroke(to, 1); doc.endStroke();
}
class DrawingTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void pixelsUndoRedoAndEraser() {
        Document doc; doc.load(transparentProject());
        line(doc, {240, 100}, {290, 100}, Qt::red);
        QCOMPARE(doc.history()->count(), 1);
        QCOMPARE(renderMovieFrame(doc.project()).pixelColor(256, 100), QColor(Qt::red));
        doc.history()->setClean();
        line(doc, {250, 100}, {275, 100}, Qt::black, true);
        QCOMPARE(renderMovieFrame(doc.project()).pixelColor(256, 100).alpha(), 0);
        doc.history()->undo();
        QVERIFY(doc.history()->isClean());
        QCOMPARE(renderMovieFrame(doc.project()).pixelColor(256, 100), QColor(Qt::red));
        doc.history()->undo();
        QCOMPARE(renderMovieFrame(doc.project()).pixelColor(256, 100).alpha(), 0);
        doc.history()->redo(); doc.history()->redo();
        QCOMPARE(renderMovieFrame(doc.project()).pixelColor(256, 100).alpha(), 0);
        // Same pixel allocation spans two tiles; no seam at their boundary.
        doc.history()->undo();
        for (int x = 250; x < 265; ++x) QCOMPARE(renderMovieFrame(doc.project()).pixelColor(x, 100), QColor(Qt::red));
    }
    void layersAndAnnotations() {
        Document doc; doc.load(transparentProject());
        line(doc, {100, 100}, {200, 100}, Qt::red);
        doc.addLayer(); line(doc, {100, 100}, {200, 100}, Qt::blue);
        QCOMPARE(renderMovieFrame(doc.project()).pixelColor(150, 100), QColor(Qt::blue));
        doc.changeLayer("Blue", true, false, false, .5);
        const auto blend = renderMovieFrame(doc.project()).pixelColor(150, 100);
        QVERIFY(blend.red() > 120 && blend.blue() > 120);
        doc.changeLayer("Blue", false, false, false, 1);
        QCOMPARE(renderMovieFrame(doc.project()).pixelColor(150, 100), QColor(Qt::red));
        BrushSettings brush;
        QVERIFY(!doc.beginStroke({150, 100}, 1, brush, false));
        doc.changeLayer("Blue", true, true, false, 1);
        QVERIFY(!doc.beginStroke({150, 100}, 1, brush, false));
        doc.changeLayer("Blue", true, false, false, 1);
        doc.moveLayer(-1);
        QCOMPARE(renderMovieFrame(doc.project()).pixelColor(150, 100), QColor(Qt::red));
        doc.addLayer(true); line(doc, {100, 100}, {200, 100}, Qt::green);
        QCOMPARE(renderMovieFrame(doc.project()).pixelColor(150, 100), QColor(Qt::red));
        // Move notes above the red drawing to make the sheet check unambiguous.
        doc.moveLayer(1);
        const auto g = sheetGeometry(doc.project(), doc.project().shots.first());
        QCOMPARE(renderSheet(doc.project(), doc.project().shots.first()).pixelColor((g.output.topLeft() + QPointF(150, 100)).toPoint()), QColor(Qt::green));
        doc.deleteLayer(); doc.history()->undo();
        QCOMPARE(doc.project().shots.first().layers.size(), 3);
    }
    void overscanMarginsAndCancel() {
        Document doc; doc.load(transparentProject());
        line(doc, {-70, 80}, {-20, 80}, Qt::red);
        const auto before = doc.project();
        QVERIFY(!before.shots.first().layers.first().tiles.isEmpty());
        QCOMPARE(renderMovieFrame(before).pixelColor(0, 80).alpha(), 0);
        const auto g = sheetGeometry(before, before.shots.first());
        QCOMPARE(renderSheet(before, before.shots.first()).pixelColor((g.output.topLeft() + QPointF(-50, 80)).toPoint()), QColor(Qt::red));
        BrushSettings brush; doc.beginStroke({100, 100}, 1, brush, false); doc.cancelStroke();
        QCOMPARE(renderMovieFrame(doc.project()), renderMovieFrame(before));
        QCOMPARE(doc.history()->count(), 1);
        auto modified = before; modified.shots.first().sheet.left += 120;
        doc.replaceProject(modified, "Margins");
        QCOMPARE(renderMovieFrame(doc.project()), renderMovieFrame(before));
        doc.history()->undo();
        QCOMPARE(doc.project().shots.first().sheet.left, before.shots.first().sheet.left);
        line(doc, {-10000, -10000}, {-9990, -9990});
        QCOMPARE(doc.history()->index(), 1);
    }
    void artworkRoundTripAndCorruption() {
        Document doc; doc.load(transparentProject());
        line(doc, {-30, 100}, {600, 140}, Qt::red);
        doc.addLayer(true); line(doc, {30, 60}, {500, 60}, Qt::blue);
        QTemporaryDir temp; QString error; const auto path = temp.filePath("drawing.toonproj");
        QVERIFY2(saveProject(doc.project(), path, &error), qPrintable(error));
        const auto restored = loadProject(path, &error);
        QVERIFY2(restored.has_value(), qPrintable(error));
        QCOMPARE(restored->toJson(), doc.project().toJson());
        QCOMPARE(renderMovieFrame(*restored), renderMovieFrame(doc.project()));
        QCOMPARE(renderSheet(*restored, restored->shots.first()), renderSheet(doc.project(), doc.project().shots.first()));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadWrite));
        const auto length = file.size(); file.seek(length - 10); auto byte = file.read(1); byte[0] ^= 1;
        file.seek(length - 10); file.write(byte); file.close();
        QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("checksum"));
        QVERIFY(saveProject(doc.project(), path, &error));
        QVERIFY(file.open(QIODevice::ReadWrite)); file.resize(file.size() - 4); file.close();
        QVERIFY(!loadProject(path, &error));
    }
    void legacyMigration() {
        auto original = Project::create().toJson(); original["version"] = 1;
        auto shots = original["shots"].toArray();
        auto shot = shots[0].toObject(); shot.remove("layers"); shots[0] = shot; original["shots"] = shots;
        QTemporaryDir temp; const auto path = temp.filePath("legacy.toonproj");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(original).toJson()); file.close();
        QString error; const auto migrated = loadProject(path, &error);
        QVERIFY2(migrated.has_value(), qPrintable(error));
        QCOMPARE(migrated->shots.first().layers.size(), 1);
        QCOMPARE(migrated->width, 1920);
        QVERIFY(saveProject(*migrated, path, &error));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.read(10), QByteArray("TOONPRO19\n")); file.close();
        QVERIFY(loadProject(path, &error));
    }
    void rotatedMouseAndPressureTablet() {
        Canvas canvas; canvas.resize(1000, 800); canvas.setProject(transparentProject()); canvas.show();
        canvas.setZoom(.75); canvas.setRotation(32);
        auto screenPoint = [&canvas](QPointF p) { return canvas.viewTransform().map(sheetGeometry(canvas.project(), canvas.shot()).output.topLeft() + p); };
        const QPointF target(900, 480);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, screenPoint(target).toPoint());
        QCOMPARE(renderMovieFrame(canvas.project()).pixelColor(target.toPoint()), canvas.brush.color);
        canvas.document()->history()->undo();
        canvas.brush.size = 40;
        QPointingDevice pen("Test pen", 10, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
        auto send = [&](QEvent::Type type, QPointF art, qreal pressure) {
            auto position = screenPoint(art);
            QTabletEvent e(type, &pen, position, canvas.mapToGlobal(position), pressure, 0, 0, 0, 0, 0,
                Qt::NoModifier, type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton,
                type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton);
            QApplication::sendEvent(&canvas, &e); QVERIFY(e.isAccepted());
        };
        send(QEvent::TabletPress, {800, 480}, .25); send(QEvent::TabletRelease, {800, 480}, 0);
        send(QEvent::TabletPress, {950, 480}, 1); send(QEvent::TabletRelease, {950, 480}, 0);
        const auto image = renderMovieFrame(canvas.project());
        QCOMPARE(image.pixelColor(800, 492).alpha(), 0);
        QVERIFY(image.pixelColor(950, 492).alpha() > 240);
        QCOMPARE(canvas.document()->history()->index(), 2);
        canvas.setTool(Canvas::Tool::Hand);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, screenPoint({900, 480}).toPoint());
        QCOMPARE(canvas.document()->history()->index(), 2);
    }
    void memoryLimitCancelsWholeStroke() {
        auto p = transparentProject();
        QImage blank(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); blank.fill(Qt::transparent);
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) p.shots.first().layers[0].tiles[{x, y}] = blank;
        Document doc; doc.load(p); QSignalSpy warnings(&doc, &Document::warning);
        BrushSettings brush;
        QVERIFY(!doc.beginStroke({-50, -50}, 1, brush, false));
        QVERIFY(!doc.strokeActive()); QCOMPARE(warnings.count(), 1);
        QCOMPARE(doc.project().shots.first().layers.first().tiles.size(), 1024);
        QVERIFY(doc.history()->isClean());
    }
    void fractionalZoomHasNoTileSeams() {
        auto layer = RasterLayer::create();
        QImage solid(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); solid.fill(Qt::blue);
        layer.tiles[{0, 0}] = solid; layer.tiles[{1, 0}] = solid;
        QImage image(200, 100, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::white);
        QPainter p(&image); p.setRenderHint(QPainter::Antialiasing); p.translate(5.3, 5.7); p.scale(.27, .27);
        paintLayers(p, {layer}, false); p.end();
        for (int x = 65; x < 85; ++x) QCOMPARE(image.pixelColor(x, 30), QColor(Qt::blue));
    }
    void recoveryAndDrawingPreview() {
        QTemporaryDir temp; QString error; const auto path = temp.filePath("drawing.toonproj");
        QVERIFY(saveProject(transparentProject(), path, &error));
        MainWindow window(temp.filePath("workspace.ini")); window.show(); QVERIFY(window.openProject(path));
        auto *doc = window.canvas()->document();
        line(*doc, {500, 240}, {750, 600}, QColor("#497fc0"), false, 32);
        line(*doc, {750, 600}, {1100, 280}, QColor("#497fc0"), false, 32);
        doc->addLayer(true); line(*doc, {1200, 200}, {1400, 200}, QColor("#b54e55"));
        QVERIFY(window.isWindowModified());
        QVERIFY(QMetaObject::invokeMethod(window.findChild<QTimer *>("recoveryTimer"), "timeout", Qt::DirectConnection));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path + ".recovery"), 10000);
        auto recovery = loadProject(path + ".recovery", &error); QVERIFY2(recovery.has_value(), qPrintable(error));
        QCOMPARE(renderMovieFrame(*recovery), renderMovieFrame(doc->project()));
        auto saved = loadProject(path, &error); QVERIFY(saved);
        QCOMPARE(renderMovieFrame(*saved).pixelColor(750, 600).alpha(), 0);
        while (doc->history()->canUndo()) doc->history()->undo();
        QVERIFY(!QFile::exists(path + ".recovery"));
        while (doc->history()->canRedo()) doc->history()->redo();
        for (const auto &id : {"toolsPanel", "propertiesPanel", "layersPanel", "colorsPanel", "controlPanel"})
            window.findChild<QDockWidget *>(id)->show();
        window.findChild<QDockWidget *>("layersPanel")->raise();
        window.findChild<QDockWidget *>("propertiesPanel")->raise();
        QTest::qWait(50); window.canvas()->fitSheet();
        QDir().mkpath("test-artifacts"); QVERIFY(window.grab().save("test-artifacts/drawing.png"));
        doc->history()->setClean(); window.close();
        // Recreate a persisted snapshot to simulate a prior process ending before save.
        QVERIFY(saveProject(*recovery, path + ".recovery", &error));
        MainWindow recoveredWindow(temp.filePath("second.ini"));
        QTimer::singleShot(0, [] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->button(QMessageBox::Yes)->click();
        });
        QVERIFY(recoveredWindow.openProject(path));
        QVERIFY(recoveredWindow.isWindowModified());
        QCOMPARE(renderMovieFrame(recoveredWindow.canvas()->project()), renderMovieFrame(*recovery));
        recoveredWindow.canvas()->document()->history()->setClean(); recoveredWindow.close();
    }
};
QTEST_MAIN(DrawingTest)
#include "test_drawing.moc"
