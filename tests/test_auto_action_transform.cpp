#include "auto_actions.h"
#include "raster_tools.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

using namespace toon;

namespace {
Project paintedProject(QRect rectangle = {20, 30, 30, 20}) {
    auto project = Project::create();
    project.width = 320; project.height = 240; project.transparent = true;
    project.shots[0].sheet.overscan = 0;
    QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::transparent);
    { QPainter painter(&tile); painter.fillRect(rectangle, QColor("#cc6040")); }
    project.shots[0].layers[0].tiles[{0, 0}] = tile;
    return project;
}
const RasterLayer &layer(const Document &document) {
    return document.activeShot().layers[document.activeLayer()];
}
QPainterPath rectangle(QRectF bounds) { QPainterPath path; path.addRect(bounds); return path; }
QImage pixels(const Document &document) {
    QImage image(320, 240, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    QPainter painter(&image); paintRasterLayerPixels(painter, layer(document)); return image;
}
}

class AutoActionTransformTests : public QObject {
    Q_OBJECT
private slots:
    void finalAppliedGestureRecordsAndReplays() {
        QTemporaryDir directory; Document document; const auto source = paintedProject(); document.load(source);
        AutoActions actions(&document, directory.filePath("actions.json")); const auto id = actions.create("Place artwork");
        QVERIFY(actions.startRecording(id)); QVERIFY(document.beginTransform());
        TransformParameters parameters; parameters.translation = {9, 4};
        QVERIFY(document.previewTransform(parameters)); QVERIFY(actions.action(id)->steps.isEmpty());
        parameters.translation = {42, 31}; parameters.scaleX = 1.5; parameters.scaleY = .75; parameters.rotation = 390;
        QVERIFY(document.previewTransform(parameters)); QVERIFY(actions.action(id)->steps.isEmpty());
        QVERIFY(document.applyTransform()); QVERIFY(actions.recording());
        QCOMPARE(actions.action(id)->steps.size(), 1);
        const auto step = actions.action(id)->steps.first();
        QCOMPARE(step.operation, QString("transform"));
        QCOMPARE(step.parameters, (QJsonObject{{"translationX", 42.}, {"translationY", 31.},
            {"scaleX", 1.5}, {"scaleY", .75}, {"rotation", 30.}}));
        const auto expected = pixels(document); actions.stopRecording();
        document.load(source); const auto before = pixels(document); QVERIFY(expected != before);
        QVERIFY(actions.play(id)); QCOMPARE(pixels(document), expected); QCOMPARE(document.history()->count(), 1);
        document.history()->undo(); QCOMPARE(pixels(document), before);
        document.history()->redo(); QCOMPARE(pixels(document), expected);
        AutoActions reopened(&document, actions.path()); QVERIFY(reopened.load());
        QCOMPARE(reopened.action(id)->steps.first().parameters, step.parameters);
    }

    void replayCentersScaleAndRotationOnCurrentArtwork() {
        QTemporaryDir directory; Document document; document.load(paintedProject({20, 30, 20, 10}));
        AutoActions actions(&document, directory.filePath("actions.json")); const auto id = actions.create("Turn and enlarge");
        QVERIFY(actions.startRecording(id)); QVERIFY(document.beginTransform());
        TransformParameters parameters; parameters.translation = {-10, 10}; parameters.scaleX = 2; parameters.rotation = 90;
        QVERIFY(document.previewTransform(parameters)); QVERIFY(document.applyTransform()); actions.stopRecording();
        const auto target = paintedProject({120, 100, 40, 20});
        Document manual; manual.load(target); QVERIFY(manual.beginTransform());
        QVERIFY(manual.previewTransform(parameters)); QVERIFY(manual.applyTransform());
        document.load(target); QVERIFY(actions.play(id));
        QCOMPARE(pixels(document), pixels(manual));
        // Geometry is centered on this larger target. The rasterizer may add a
        // one-pixel antialias fringe around the expected transformed rectangle.
        const QRectF expectedBounds(120, 80, 20, 80);
        const auto paintedBounds = selectedPixelBounds(layer(document));
        QVERIFY(paintedBounds.contains(expectedBounds));
        QVERIFY(expectedBounds.adjusted(-1, -1, 1, 1).contains(paintedBounds));
        QCOMPARE(pixels(document).pixelColor(130, 120), QColor("#cc6040"));
        QCOMPARE(pixels(document).pixelColor(30, 30).alpha(), 0);
    }

    void selectedPixelsAndFeatheredBoundaryReplayAndUndoTogether() {
        QTemporaryDir directory; auto source = paintedProject({20, 20, 30, 30});
        { QPainter painter(&source.shots[0].layers[0].tiles[{0, 0}]); painter.fillRect(120, 120, 20, 20, Qt::blue); }
        Document document; document.load(source); const auto selection = rectangle({18, 18, 34, 34});
        document.setRasterSelection(selection); QVERIFY(RasterTools::featherSelection(&document, 4));
        AutoActions actions(&document, directory.filePath("actions.json")); const auto id = actions.create("Move selection");
        QVERIFY(actions.startRecording(id)); QVERIFY(document.beginTransform());
        TransformParameters parameters; parameters.translation = {45, 25};
        QVERIFY(document.previewTransform(parameters)); QVERIFY(document.applyTransform());
        QVERIFY(actions.recording()); QCOMPARE(actions.action(id)->steps.size(), 1);
        const auto expected = pixels(document); const auto moved = document.rasterSelection();
        QCOMPARE(moved.path, QTransform::fromTranslate(45, 25).map(selection)); QCOMPARE(moved.featherRadius, 4.);
        // Committing its own boundary change must not stop this recording.
        document.changeLayer(layer(document).name, true, false, false, .7);
        QVERIFY(actions.recording()); QCOMPARE(actions.action(id)->steps.size(), 2); actions.stopRecording();
        document.load(source); document.setRasterSelection(selection); QVERIFY(RasterTools::featherSelection(&document, 4));
        const auto before = pixels(document); const int history = document.history()->count();
        QVERIFY(actions.play(id)); QCOMPARE(pixels(document), expected); QCOMPARE(document.rasterSelection().path, moved.path);
        QCOMPARE(document.rasterSelection().featherRadius, 4.); QCOMPARE(layer(document).opacity, .7);
        QCOMPARE(pixels(document).pixelColor(125, 125), QColor(Qt::blue)); QCOMPARE(document.history()->count(), history + 1);
        document.history()->undo(); QCOMPARE(pixels(document), before); QCOMPARE(document.rasterSelection().path, selection);
        QCOMPARE(document.rasterSelection().featherRadius, 4.); QCOMPARE(layer(document).opacity, 1.);
        document.history()->redo(); QCOMPARE(pixels(document), expected); QCOMPARE(document.rasterSelection().path, moved.path);
    }

    void selectionOnlyChangeIsRecordedAndUndoable() {
        QTemporaryDir directory; const auto source = paintedProject({40, 40, 20, 20});
        Document document; document.load(source); const auto selection = rectangle({30, 30, 70, 60});
        document.setRasterSelection(selection); const auto before = pixels(document);
        AutoActions actions(&document, directory.filePath("actions.json")); const auto id = actions.create("Rotate boundary");
        QVERIFY(actions.startRecording(id)); QVERIFY(document.beginTransform());
        TransformParameters parameters; parameters.rotation = 180;
        QVERIFY(document.previewTransform(parameters)); QVERIFY(document.applyTransform());
        QCOMPARE(pixels(document), before); QVERIFY(document.rasterSelection().path != selection);
        const auto moved = document.rasterSelection().path;
        QVERIFY(actions.recording()); QCOMPARE(actions.action(id)->steps.size(), 1); QCOMPARE(document.history()->count(), 1);
        actions.stopRecording(); document.history()->undo(); QCOMPARE(document.rasterSelection().path, selection);
        document.load(source); document.setRasterSelection(selection); QVERIFY(actions.play(id));
        QCOMPARE(pixels(document), before); QCOMPARE(document.rasterSelection().path, moved); QCOMPARE(document.history()->count(), 1);
        document.history()->undo(); QCOMPARE(document.rasterSelection().path, selection);
    }

    void cancelledIdentityAndInvalidGesturesDoNotRecord() {
        QTemporaryDir directory; Document document; document.load(paintedProject());
        const auto before = pixels(document); AutoActions actions(&document, directory.filePath("actions.json"));
        const auto id = actions.create("Cancelled"); QVERIFY(actions.startRecording(id)); QVERIFY(document.beginTransform());
        TransformParameters parameters; parameters.translation = {80, 15};
        QVERIFY(document.previewTransform(parameters)); document.cancelTransform();
        QVERIFY(actions.recording()); QVERIFY(actions.action(id)->steps.isEmpty()); QCOMPARE(pixels(document), before);
        QVERIFY(document.beginTransform()); QVERIFY(document.applyTransform()); QVERIFY(actions.action(id)->steps.isEmpty());
        QVERIFY(document.beginTransform());
        parameters = {}; parameters.scaleX = -1; QVERIFY(!document.previewTransform(parameters));
        parameters = {}; parameters.scaleY = 0; QVERIFY(!document.previewTransform(parameters));
        parameters = {}; parameters.rotation = std::numeric_limits<qreal>::infinity(); QVERIFY(!document.previewTransform(parameters));
        parameters = {}; parameters.translation.setX(std::numeric_limits<qreal>::quiet_NaN()); QVERIFY(!document.previewTransform(parameters));
        QVERIFY(document.applyTransform()); QVERIFY(actions.recording()); QVERIFY(actions.action(id)->steps.isEmpty());
        QCOMPARE(document.history()->count(), 0); QCOMPARE(pixels(document), before);
    }

    void failedTargetsLeavePixelsSelectionAndHistoryUntouched_data() {
        QTest::addColumn<int>("target");
        QTest::newRow("locked") << 0; QTest::newRow("position locked") << 1;
        QTest::newRow("alpha locked") << 2; QTest::newRow("empty") << 3;
    }
    void failedTargetsLeavePixelsSelectionAndHistoryUntouched() {
        QFETCH(int, target); QTemporaryDir directory; Document document; const auto source = paintedProject(); document.load(source);
        AutoActions actions(&document, directory.filePath("actions.json")); const auto id = actions.create("Move");
        QVERIFY(actions.startRecording(id)); QVERIFY(document.beginTransform());
        TransformParameters parameters; parameters.translation = {40, 20};
        QVERIFY(document.previewTransform(parameters)); QVERIFY(document.applyTransform()); actions.stopRecording();
        auto destination = source;
        if (target == 0) destination.shots[0].layers[0].locked = true;
        if (target == 1) destination.shots[0].layers[0].positionLocked = true;
        if (target == 2) destination.shots[0].layers[0].alphaLocked = true;
        if (target == 3) destination.shots[0].layers[0].tiles.clear();
        document.load(destination); document.setRasterSelection(rectangle({10, 10, 70, 70}));
        const auto before = pixels(document); const auto metadata = document.project().toJson(); const auto selection = document.rasterSelection().path;
        QVERIFY(!actions.play(id)); QCOMPARE(pixels(document), before); QCOMPARE(document.project().toJson(), metadata);
        QCOMPARE(document.rasterSelection().path, selection); QCOMPARE(document.history()->count(), 0); QVERIFY(!document.transformActive());
    }

    void savedTransformRejectsNegativeScaleWithoutReplacingLibrary() {
        QTemporaryDir directory; Document document; document.load(paintedProject());
        AutoActions actions(&document, directory.filePath("actions.json")); const auto id = actions.create("Move");
        QVERIFY(actions.startRecording(id)); QVERIFY(document.beginTransform());
        TransformParameters parameters; parameters.translation = {10, 20};
        QVERIFY(document.previewTransform(parameters)); QVERIFY(document.applyTransform()); actions.stopRecording();
        QFile file(actions.path()); QVERIFY(file.open(QIODevice::ReadOnly)); auto root = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        QCOMPARE(root["version"].toInt(), 2);
        auto entries = root["actions"].toArray(); auto entry = entries[0].toObject(); auto steps = entry["steps"].toArray();
        auto step = steps[0].toObject(); auto values = step["parameters"].toObject(); values["scaleX"] = -1;
        step["parameters"] = values; steps[0] = step; entry["steps"] = steps; entries[0] = entry; root["actions"] = entries;
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate)); QVERIFY(file.write(QJsonDocument(root).toJson()) > 0); file.close();
        QVERIFY(!actions.load()); QCOMPARE(actions.action(id)->steps.first().parameters["scaleX"].toDouble(), 1.);
    }
};

QTEST_MAIN(AutoActionTransformTests)
#include "test_auto_action_transform.moc"
