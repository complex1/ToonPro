#include "document.h"
#include "vector_brush.h"

#include <QPainter>
#include <QSignalSpy>
#include <QtTest>
#include <limits>

using namespace toon;
namespace {
Project blank(bool vector = false) {
    auto project = Project::create(); project.width = 512; project.height = 384; project.transparent = true;
    project.shots[0].sheet.overscan = 0;
    project.shots[0].layers[0].kind = vector ? LayerKind::Vector : LayerKind::Raster;
    return project;
}
const RasterLayer &active(const Document &document) { return document.activeShot().layers[document.activeLayer()]; }
Project background(bool vector = false) {
    Document document; document.load(blank(vector));
    BrushSettings base; base.size = 140; base.pressureSize = false; base.color = QColor("#849cac");
    if (!document.beginStroke({90, 160}, 1, base, false)) qFatal("Cannot create stamp fixture");
    document.continueStroke({360, 160}, 1); document.endStroke(); return document.project();
}
BrushInput contact() { BrushInput input; input.pressure = .8; input.tiltX = 12; input.tiltY = -7; input.rotation = 35; return input; }
BrushSettings stamp(bool color = false) {
    BrushSettings brush; brush.size = 72; brush.opacity = .7; brush.strokeOpacity = .8;
    brush.pressureSize = true; brush.color = QColor("#9d365e"); brush.lockRandomSeed = false;
    brush.sizeDynamics.jitter = .15; brush.angleDynamics.jitter = .2;
    brush.colorDynamics.enabled = true; brush.colorDynamics.perTip = true; brush.colorDynamics.hue = .1;
    if (color) {
        QImage a(31, 23, QImage::Format_ARGB32_Premultiplied); a.fill(Qt::transparent);
        QImage b = a;
        { QPainter painter(&a); painter.fillRect(2, 3, 10, 16, QColor(220, 45, 70, 180)); painter.fillRect(18, 3, 9, 16, QColor(35, 90, 220, 220)); }
        { QPainter painter(&b); painter.fillRect(3, 2, 24, 6, QColor(40, 180, 75, 190)); painter.fillRect(3, 15, 24, 6, QColor(210, 170, 25, 200)); }
        if (!setBrushTipImages(brush, {a, b}, {{.3, .7}, {.6, .2}})) qFatal("Cannot create colored stamp fixture");
    }
    return brush;
}
EditableObject capturedStamp(const Project &project, const BrushSettings &brush, QPointF point, const RasterSelection &selection = {}) {
    auto vectorProject = project;
    auto &captureLayer = vectorProject.shots[0].layers[0];
    captureLayer.kind = LayerKind::Vector; captureLayer.alphaLocked = false;
    captureLayer.tiles.clear(); captureLayer.objects.clear();
    Document document; document.load(vectorProject); document.setBrushRandomSeed(711);
    if (selection.active) document.setRasterSelection(selection.path);
    if (!document.beginStroke(point, contact(), brush, false)) qFatal("Cannot capture reference stamp");
    document.endStroke();
    if (active(document).objects.isEmpty()) qFatal("Reference stamp has no vector capture");
    auto object = active(document).objects.last();
    object.brushStroke->alphaLocked = project.shots[0].layers[0].alphaLocked;
    return object;
}
}

class StampPlacementTests : public QObject {
    Q_OBJECT
private slots:
    void identityMatchesOrdinaryOneDab_data() {
        QTest::addColumn<bool>("vector"); QTest::addColumn<int>("variant");
        for (bool vector : {false, true}) for (int variant = 0; variant < 5; ++variant)
            QTest::newRow(qPrintable(QString("%1-%2").arg(vector ? "vector" : "raster").arg(variant))) << vector << variant;
    }
    void identityMatchesOrdinaryOneDab() {
        QFETCH(bool, vector); QFETCH(int, variant);
        const auto project = background(vector); auto brush = stamp(variant != 0);
        if (variant == 1) brush.blendMode = BrushBlend::Multiply;
        if (variant == 2) { brush.symmetry.mode = SymmetryMode::Vertical; brush.count = 3; brush.scatter = .15; }
        if (variant == 3) brush.colorDynamics.perTip = false;
        auto input = contact();
        if (variant == 4) {
            input.rotation = 270; brush.angleDynamics.control = BrushControl::Rotation;
            brush.angleDynamics.response = {0, .1, .7, .9, 1};
        }
        Document actual, expected;
        for (auto *document : {&actual, &expected}) { document->load(project); document->setBrushRandomSeed(711); }
        const auto before = active(actual).tiles; const auto objects = active(actual).objects;
        QSignalSpy changes(&actual, &Document::changed);
        QVERIFY(actual.beginStampPlacement({180, 155}, input, brush));
        QVERIFY(actual.stampPlacementActive()); QVERIFY(actual.transformActive()); QVERIFY(!actual.strokeActive());
        QCOMPARE(active(actual).tiles, before); QCOMPARE(active(actual).objects, objects);
        QVERIFY(expected.beginStroke({180, 155}, input, brush, false)); expected.endStroke();
        QCOMPARE(actual.transformPreview()->stationary.tiles, active(expected).tiles);
        QCOMPARE(actual.history()->count(), 0); QCOMPARE(changes.count(), 0);
        QVERIFY(actual.applyTransform()); QVERIFY(!actual.stampPlacementActive()); QVERIFY(!actual.transformActive());
        QCOMPARE(active(actual).tiles, active(expected).tiles); QCOMPARE(actual.history()->count(), 1);
        QCOMPARE(actual.history()->undoText(), QString("Place stamp"));
        if (vector) {
            QCOMPARE(active(actual).objects.size(), objects.size() + 1);
            const auto &source = *active(actual).objects.last().brushStroke;
            QCOMPARE(source.samples.size(), 1); QCOMPARE(source.seed, quint32(711));
            QCOMPARE(source.samples, active(expected).objects.last().brushStroke->samples);
            auto rebuilt = active(actual); QString error;
            QVERIFY2(rebuildObjectLayer(rebuilt, actual.drawingBounds(), 1024, &error), qPrintable(error));
            QCOMPARE(rebuilt.tiles, active(actual).tiles);
        }
        const auto after = active(actual).tiles;
        actual.history()->undo(); QCOMPARE(active(actual).tiles, before); QCOMPARE(active(actual).objects, objects);
        actual.history()->redo(); QCOMPARE(active(actual).tiles, after);
    }

    void transformsOnlyNewStampAndDoesNotReroll_data() {
        QTest::addColumn<bool>("vector"); QTest::newRow("raster") << false; QTest::newRow("vector") << true;
    }
    void transformsOnlyNewStampAndDoesNotReroll() {
        QFETCH(bool, vector);
        const auto project = background(vector); const auto brush = stamp(true);
        Document document; document.load(project); document.setBrushRandomSeed(711);
        const auto before = active(document).tiles; const auto objects = active(document).objects;
        QVERIFY(document.beginStampPlacement({150, 155}, contact(), brush));
        const auto initialPreview = document.transformPreview()->stationary.tiles;
        const auto seed = document.brushStrokeSeed();
        TransformParameters transform; transform.translation = {120, 35}; transform.scaleX = 1.6; transform.scaleY = .7; transform.rotation = 47;
        QVERIFY(document.previewTransform(transform));
        const auto moved = document.transformPreview()->stationary.tiles;
        QVERIFY(moved != initialPreview); QCOMPARE(active(document).tiles, before);
        auto source = capturedStamp(project, brush, {150, 155}); source.transform = document.transformMatrix();
        auto expected = project.shots[0].layers[0]; QString error;
        QVERIFY2(replayVectorBrushStroke(expected, source, document.drawingBounds(), 1024, &error), qPrintable(error));
        QCOMPARE(moved, expected.tiles);
        QVERIFY(document.previewTransform({})); QCOMPARE(document.transformPreview()->stationary.tiles, initialPreview);
        QVERIFY(document.previewTransform(transform)); QCOMPARE(document.transformPreview()->stationary.tiles, moved);
        QCOMPARE(document.brushStrokeSeed(), seed);
        QVERIFY(document.applyTransform()); QCOMPARE(active(document).tiles, moved);
        QCOMPARE(active(document).objects.size(), objects.size() + (vector ? 1 : 0));
        if (vector) {
            QCOMPARE(active(document).objects.first(), objects.first());
            QCOMPARE(active(document).objects.last().transform, source.transform);
            auto rebuilt = active(document);
            QVERIFY2(rebuildObjectLayer(rebuilt, document.drawingBounds(), 1024, &error), qPrintable(error));
            QCOMPARE(rebuilt.tiles, active(document).tiles);
        }
    }

    void selectionStaysFixedAndAlphaLockMatchesBrush_data() {
        QTest::addColumn<bool>("vector"); QTest::addColumn<bool>("alphaLock");
        // Alpha lock is a raster-layer setting; vector layers reject it.
        for (bool vector : {false, true}) for (bool locked : {false, true}) {
            if (vector && locked) continue;
            QTest::newRow(qPrintable(QString("%1-%2").arg(vector).arg(locked))) << vector << locked;
        }
    }
    void selectionStaysFixedAndAlphaLockMatchesBrush() {
        QFETCH(bool, vector); QFETCH(bool, alphaLock);
        auto project = background(vector); project.shots[0].layers[0].alphaLocked = alphaLock;
        auto brush = stamp(true); brush.size = 150; brush.blendMode = BrushBlend::Multiply;
        QPainterPath selection; selection.addRect(QRectF(125, 110, 90, 110));
        Document document; document.load(project); document.setBrushRandomSeed(711); document.setRasterSelection(selection);
        QVERIFY(document.beginStampPlacement({175, 165}, contact(), brush));
        TransformParameters transform; transform.translation = {22, -15}; transform.scaleX = .8; transform.scaleY = 1.3; transform.rotation = 35;
        QVERIFY(document.previewTransform(transform));
        auto expected = project.shots[0].layers[0];
        auto source = capturedStamp(project, brush, {175, 165}, {true, selection, 0});
        source.transform = document.transformMatrix();
        source.brushStroke->selection = source.transform.inverted().map(selection);
        QString error;
        QVERIFY2(replayVectorBrushStroke(expected, source, document.drawingBounds(), 1024, &error), qPrintable(error));
        QCOMPARE(document.transformPreview()->stationary.tiles, expected.tiles);
        QVERIFY(document.applyTransform()); QCOMPARE(active(document).tiles, expected.tiles);
        QVERIFY(document.rasterSelection().active); QCOMPARE(document.rasterSelection().path, selection);
        if (vector) {
            auto rebuilt = active(document); QVERIFY2(rebuildObjectLayer(rebuilt, document.drawingBounds(), 1024, &error), qPrintable(error));
            QCOMPARE(rebuilt.tiles, expected.tiles);
        }
        document.history()->undo(); QCOMPARE(document.rasterSelection().path, selection);
    }

    void outsideSheetReturnsIntactAndApplyClips_data() {
        QTest::addColumn<bool>("vector"); QTest::newRow("raster") << false; QTest::newRow("vector") << true;
    }
    void outsideSheetReturnsIntactAndApplyClips() {
        QFETCH(bool, vector);
        const auto project = background(vector); const auto brush = stamp(true);
        Document document; document.load(project); document.setBrushRandomSeed(711);
        QVERIFY(document.beginStampPlacement({10, 10}, contact(), brush));
        const auto first = document.transformPreview()->stationary.tiles;
        // Source starts across the top/left edge, yet its omitted pixels remain available.
        TransformParameters inside; inside.translation = {180, 170};
        QVERIFY(document.previewTransform(inside)); const auto insidePixels = document.transformPreview()->stationary.tiles;
        TransformParameters outside; outside.translation = {-1000, -1000};
        QVERIFY(document.previewTransform(outside));
        QVERIFY(document.transformPreview()->stationary.tiles != project.shots[0].layers[0].tiles);
        QCOMPARE(active(document).tiles, project.shots[0].layers[0].tiles);
        QVERIFY(document.previewTransform({})); QCOMPARE(document.transformPreview()->stationary.tiles, first);
        QVERIFY(document.previewTransform(inside)); QCOMPARE(document.transformPreview()->stationary.tiles, insidePixels);
        QVERIFY(document.previewTransform(outside)); QVERIFY(document.applyTransform());
        QCOMPARE(active(document).tiles, project.shots[0].layers[0].tiles);
        QCOMPARE(active(document).objects, project.shots[0].layers[0].objects);
        QCOMPARE(document.history()->count(), 0); QVERIFY(!document.transformActive());
    }

    void partialEdgeVectorPlacementPreservesUnclippedSource() {
        const auto project = background(true); const auto brush = stamp(true);
        Document document; document.load(project); document.setBrushRandomSeed(711);
        QVERIFY(document.beginStampPlacement({10, 10}, contact(), brush));
        TransformParameters transform; transform.rotation = 22; transform.scaleX = 1.6; transform.scaleY = 1.2;
        QVERIFY(document.previewTransform(transform));
        const auto expandedPixels = document.transformPreview()->stationary.tiles;
        auto source = capturedStamp(project, brush, {10, 10}); source.transform = document.transformMatrix();
        auto clipped = project.shots[0].layers[0]; QString error;
        QVERIFY2(replayVectorBrushStroke(clipped, source, document.drawingBounds(), 1024, &error), qPrintable(error));
        QVERIFY(clipped.tiles != expandedPixels);
        QVERIFY(document.applyTransform()); QCOMPARE(active(document).tiles, clipped.tiles);
        QCOMPARE(active(document).objects.size(), project.shots[0].layers[0].objects.size() + 1);
        const auto &stored = active(document).objects.last();
        QCOMPARE(stored.transform, source.transform);
        QCOMPARE(stored.brushStroke->samples, source.brushStroke->samples);
        QCOMPARE(stored.brushStroke->settings, source.brushStroke->settings);
        // The vector source survives the pixel clipping and can reveal its full
        // original tip when the drawing area later expands.
        auto rebuilt = active(document);
        QVERIFY2(rebuildObjectLayer(rebuilt, document.drawingBounds().adjusted(-256, -256, 256, 256), 1024, &error), qPrintable(error));
        QCOMPARE(rebuilt.tiles, expandedPixels);
    }

    void cancellationInvalidValuesAndApplyFlush() {
        const auto project = background(); const auto brush = stamp(true);
        Document document; document.load(project); document.setBrushRandomSeed(711);
        QVERIFY(document.beginStampPlacement({180, 160}, contact(), brush));
        const auto pixels = document.transformPreview()->stationary.tiles;
        for (const auto value : {std::numeric_limits<qreal>::quiet_NaN(), .001, 17.}) {
            TransformParameters invalid; invalid.scaleX = value;
            QVERIFY(!document.previewTransform(invalid)); QVERIFY(document.stampPlacementActive());
            QCOMPARE(document.transformPreview()->stationary.tiles, pixels);
        }
        document.cancelTransform(); QVERIFY(!document.stampPlacementActive());
        QCOMPARE(active(document).tiles, project.shots[0].layers[0].tiles); QCOMPARE(document.history()->count(), 0);
        QVERIFY(document.beginStampPlacement({180, 160}, contact(), brush));
        TransformParameters latest; latest.translation = {75, 50};
        connect(&document, &Document::transformApplyRequested, &document, [&] { QVERIFY(document.previewTransform(latest)); });
        QVERIFY(document.applyTransform()); QCOMPARE(document.history()->count(), 1);
        QVERIFY(active(document).tiles != pixels);
        QVERIFY(document.beginStampPlacement({180, 160}, contact(), brush));
        document.load(project); QVERIFY(!document.stampPlacementActive()); QVERIFY(!document.transformActive());
    }

    void memoryLimitKeepsPreviousPlacement() {
        auto project = blank();
        auto occupied = RasterLayer::create("Other artwork"); occupied.visible = false;
        QImage shared(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); shared.fill(Qt::black);
        for (int i = 0; i < maxProjectTiles - 1; ++i) occupied.tiles.insert({i % 32, i / 32}, shared);
        project.shots[0].layers.append(occupied);
        Document document; document.load(project);
        BrushSettings brush; brush.size = 24; brush.pressureSize = false;
        QVERIFY(document.beginStampPlacement({100, 100}, contact(), brush));
        const auto pixels = document.transformPreview()->stationary.tiles;
        TransformParameters wide; wide.translation = {151, 0}; wide.scaleX = 3;
        QVERIFY(!document.previewTransform(wide)); QVERIFY(document.stampPlacementActive());
        QCOMPARE(document.transformPreview()->stationary.tiles, pixels); QCOMPARE(document.transformMatrix(), QTransform());
        QVERIFY(active(document).tiles.isEmpty()); QCOMPARE(document.history()->count(), 0);
        QVERIFY(document.applyTransform()); QCOMPARE(active(document).tiles, pixels); QCOMPARE(document.history()->count(), 1);
    }

    void inactiveLayersAndInvalidInputAreRejected() {
        auto project = blank(); Document document; auto brush = stamp();
        project.shots[0].layers[0].locked = true; document.load(project);
        QVERIFY(!document.beginStampPlacement({100, 100}, contact(), brush));
        project.shots[0].layers[0].locked = false; project.shots[0].layers[0].visible = false; document.load(project);
        QVERIFY(!document.beginStampPlacement({100, 100}, contact(), brush));
        document.load(blank()); auto invalid = contact(); invalid.pressure = std::numeric_limits<qreal>::quiet_NaN();
        QVERIFY(!document.beginStampPlacement({100, 100}, invalid, brush));
        QVERIFY(!document.beginStampPlacement({1e100, 100}, contact(), brush));
        brush.strokeOpacity = 0; QVERIFY(!document.beginStampPlacement({100, 100}, contact(), brush));
        QVERIFY(!document.stampPlacementActive()); QCOMPARE(document.history()->count(), 0); QVERIFY(active(document).tiles.isEmpty());
    }
};

QTEST_MAIN(StampPlacementTests)
#include "test_stamp_placement.moc"
