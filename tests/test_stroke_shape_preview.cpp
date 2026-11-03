#include "document.h"
#include "vector_brush.h"

#include <QPainter>
#include <QSignalSpy>
#include <QtTest>
#include <limits>

using namespace toon;
namespace {
Project fixture(bool vector = false) {
    auto p = Project::create(); p.width = 512; p.height = 384; p.transparent = true;
    p.shots[0].sheet.overscan = 0;
    p.shots[0].layers[0].kind = vector ? LayerKind::Vector : LayerKind::Raster;
    return p;
}
const RasterLayer &layer(const Document &document) { return document.activeShot().layers[document.activeLayer()]; }
BrushInput input(qreal pressure, qreal rotation = 0) {
    BrushInput result; result.pressure = pressure; result.tiltX = 15; result.tiltY = -9;
    result.rotation = rotation; result.wheel = .7; return result;
}
BrushSettings brush() {
    BrushSettings b; b.size = 18; b.spacing = .3; b.color = QColor("#52337e");
    b.opacity = .55; b.strokeOpacity = .7;
    b.sizeDynamics.jitter = .15; b.angleDynamics.jitter = .1;
    b.colorDynamics.enabled = true; b.colorDynamics.perTip = true; b.colorDynamics.hue = .08;
    b.opacityDynamics.control = BrushControl::Pressure;
    b.lockRandomSeed = false;
    return b;
}
QVector<StrokeInputSample> profile() {
    return {{{40, 80}, input(.25, 350)}, {{140, 80}, input(.85, 5)}, {{240, 80}, input(.4, 25)}};
}
void paint(Document &document, const BrushSettings &settings, const QVector<StrokeInputSample> &samples, bool eraser = false) {
    QVERIFY(!samples.isEmpty());
    QVERIFY(document.beginStroke(samples.first().point, samples.first().input, settings, eraser));
    for (int i = 1; i < samples.size(); ++i) document.continueStroke(samples[i].point, samples[i].input);
}
void compareCapturedStroke(const Document &actual, const Document &expected) {
    QCOMPARE(layer(actual).objects.size(), layer(expected).objects.size());
    if (layer(actual).objects.isEmpty()) return;
    const auto &a = *layer(actual).objects.last().brushStroke;
    const auto &b = *layer(expected).objects.last().brushStroke;
    QCOMPARE(a.seed, b.seed); QCOMPARE(a.settings, b.settings); QCOMPARE(a.samples, b.samples);
    QCOMPARE(a.selection, b.selection); QCOMPARE(a.selectionActive, b.selectionActive);
}
}

class StrokeShapePreviewTests : public QObject {
    Q_OBJECT
private slots:
    void lineReplacementMatchesPressureSelectionAndSymmetry_data() {
        QTest::addColumn<bool>("vector"); QTest::newRow("raster") << false; QTest::newRow("vector") << true;
    }
    void lineReplacementMatchesPressureSelectionAndSymmetry() {
        QFETCH(bool, vector);
        const auto project = fixture(vector); Document actual, expected;
        auto b = brush(); b.symmetry.mode = SymmetryMode::Vertical;
        QPainterPath selection; selection.addRect(QRectF(20, 40, 300, 260));
        for (auto *document : {&actual, &expected}) {
            document->load(project); document->setBrushRandomSeed(711); document->setRasterSelection(selection);
        }
        QSignalSpy changes(&actual, &Document::changed), warnings(&actual, &Document::warning);
        const auto samples = profile(); paint(actual, b, samples); actual.preserveStrokeShapeOriginal();
        const auto seed = actual.brushStrokeSeed();
        QPainterPath line({40, 180}); line.lineTo({240, 180});
        QVERIFY(actual.previewStrokePath(line, samples));
        QVector<StrokeInputSample> straight = samples;
        for (auto &sample : straight) sample.point.setY(180);
        paint(expected, b, straight);
        QCOMPARE(layer(actual).tiles, layer(expected).tiles);
        QCOMPARE(actual.brushStrokeSeed(), seed); QCOMPARE(actual.history()->count(), 0);
        QCOMPARE(changes.count(), 0); QVERIFY(warnings.isEmpty());
        actual.endStroke(); expected.endStroke();
        QCOMPARE(actual.history()->count(), 1); QCOMPARE(layer(actual).tiles, layer(expected).tiles);
        if (vector) compareCapturedStroke(actual, expected);
        const auto pixels = layer(actual).tiles;
        actual.history()->undo(); QVERIFY(layer(actual).tiles.isEmpty()); QVERIFY(layer(actual).objects.isEmpty());
        actual.history()->redo(); QCOMPARE(layer(actual).tiles, pixels);
    }

    void checkpointRestoresPixelsSensorsSeedAndContinuation_data() {
        QTest::addColumn<bool>("vector"); QTest::newRow("raster") << false; QTest::newRow("vector") << true;
    }
    void checkpointRestoresPixelsSensorsSeedAndContinuation() {
        QFETCH(bool, vector);
        const auto project = fixture(vector); Document actual, expected;
        const auto b = brush();
        const QVector<StrokeInputSample> samples{{{45, 70}, input(.2, 345)}, {{95, 120}, input(.9, 8)}, {{170, 65}, input(.5, 25)}};
        for (auto *document : {&actual, &expected}) { document->load(project); document->setBrushRandomSeed(912); paint(*document, b, samples); }
        const auto original = layer(actual).tiles;
        const auto seed = actual.brushStrokeSeed(); const auto nextDab = actual.brushDabIndex();
        const auto direction = actual.brushDirection(), initialDirection = actual.brushInitialDirection();
        actual.preserveStrokeShapeOriginal();
        QPainterPath line({50, 220}); line.lineTo({330, 220});
        QVERIFY(actual.previewStrokePath(line, samples));
        QVERIFY(layer(actual).tiles != original);
        QPainterPath arc({60, 180}); arc.quadTo({180, 20}, {340, 230});
        QVERIFY(actual.previewStrokePath(arc, samples));
        const auto arcPixels = layer(actual).tiles;
        QVERIFY(actual.previewStrokePath(arc, samples)); QCOMPARE(layer(actual).tiles, arcPixels);
        QCOMPARE(actual.brushStrokeSeed(), seed); QCOMPARE(actual.history()->count(), 0);
        QVERIFY(actual.restoreStrokeShapeOriginal()); QCOMPARE(layer(actual).tiles, original);
        QCOMPARE(actual.brushDabIndex(), nextDab); QCOMPARE(actual.brushDirection(), direction);
        QCOMPARE(actual.brushInitialDirection(), initialDirection);
        QVERIFY(!actual.restoreStrokeShapeOriginal());
        const auto nextInput = input(.75, 45);
        actual.continueStroke({270, 145}, nextInput); expected.continueStroke({270, 145}, nextInput);
        QCOMPARE(layer(actual).tiles, layer(expected).tiles);
        // A second hold in the same continuous stroke must take a fresh snapshot.
        actual.preserveStrokeShapeOriginal(); QVERIFY(actual.previewStrokePath(line, samples));
        QVERIFY(actual.restoreStrokeShapeOriginal()); QCOMPARE(layer(actual).tiles, layer(expected).tiles);
        actual.endStroke(); expected.endStroke();
        QCOMPARE(actual.history()->count(), 1); QCOMPARE(layer(actual).tiles, layer(expected).tiles);
        if (vector) compareCapturedStroke(actual, expected);
        // Preview attempts must not consume additional stroke random seeds.
        for (auto *document : {&actual, &expected}) QVERIFY(document->beginStroke({60, 280}, input(.8), b, false));
        QCOMPARE(actual.brushStrokeSeed(), expected.brushStrokeSeed());
        actual.cancelStroke(); expected.cancelStroke();
    }

    void rejectedGeometryOrSensorsLeaveLiveStrokeUntouched_data() {
        QTest::addColumn<int>("variant");
        const QStringList names{"empty-path", "multiple-subpaths", "excessive-elements", "extreme-cubic", "too-long-path",
            "empty-profile", "single-profile-point", "profile-nan", "profile-infinity", "profile-distance-overflow",
            "pressure-nan", "pressure-infinity", "pressure-extreme", "tilt-nan", "rotation-infinity", "wheel-nan",
            "stationary-profile", "excessive-profile", "nonfinite-path", "extreme-tilt", "extreme-wheel", "extreme-rotation"};
        for (int i = 0; i < names.size(); ++i) QTest::newRow(qPrintable(names[i])) << i;
    }
    void rejectedGeometryOrSensorsLeaveLiveStrokeUntouched() {
        QFETCH(int, variant);
        Document document; document.load(fixture(true)); document.setBrushRandomSeed(447);
        const auto b = brush(); paint(document, b, profile()); document.preserveStrokeShapeOriginal();
        const auto pixels = layer(document).tiles;
        const auto seed = document.brushStrokeSeed(); const auto dab = document.brushDabIndex();
        const auto nan = std::numeric_limits<qreal>::quiet_NaN(), infinity = std::numeric_limits<qreal>::infinity();
        QPainterPath path({40, 180}); path.lineTo({240, 180});
        auto samples = profile();
        switch (variant) {
        case 0: path = {}; break;
        case 1: path.moveTo({10, 10}); path.lineTo({100, 40}); break;
        case 2: for (int i = 0; i < 1030; ++i) path.lineTo({40. + i % 200, 20. + i % 100}); break;
        case 3:
            path.cubicTo({1e8, 1e8}, {-1e8, -1e8}, {300, 220});
            QVERIFY(path.controlPointRect().width() > 1e7);
            break;
        case 4: path.lineTo({70000, 220}); break;
        case 5: samples.clear(); break;
        case 6: samples.resize(1); break;
        case 7: samples[1].point.setX(nan); break;
        case 8: samples[1].point.setY(infinity); break;
        case 9: samples[0].point.setX(-1e308); samples[1].point.setX(1e308); break;
        case 10: samples[1].input.pressure = nan; break;
        case 11: samples[1].input.pressure = infinity; break;
        case 12: samples[1].input.pressure = 1e308; break;
        case 13: samples[1].input.tiltX = nan; break;
        case 14: samples[1].input.rotation = infinity; break;
        case 15: samples[1].input.wheel = nan; break;
        case 16: for (auto &sample : samples) sample.point = samples.first().point; break;
        case 17: samples.fill(samples.first(), 8193); break;
        case 18: path.setElementPositionAt(1, nan, 180); break;
        case 19: samples[1].input.tiltY = 1e308; break;
        case 20: samples[1].input.wheel = 1e308; break;
        case 21: samples[1].input.rotation = -1e308; break;
        }
        QVERIFY(!document.previewStrokePath(path, samples));
        QVERIFY(document.strokeActive()); QCOMPARE(layer(document).tiles, pixels);
        QCOMPARE(document.brushStrokeSeed(), seed); QCOMPARE(document.brushDabIndex(), dab);
        QCOMPARE(document.history()->count(), 0);
        QVERIFY(document.restoreStrokeShapeOriginal()); QCOMPARE(layer(document).tiles, pixels);
        document.endStroke(); QCOMPARE(document.history()->count(), 1); QCOMPARE(layer(document).objects.size(), 1);
    }

    void stationarySensorChangesMatchDirectDrawing_data() {
        QTest::addColumn<bool>("vector"); QTest::newRow("raster") << false; QTest::newRow("vector") << true;
    }
    void stationarySensorChangesMatchDirectDrawing() {
        QFETCH(bool, vector);
        Document actual, expected; const auto project = fixture(vector); const auto b = brush();
        const QVector<StrokeInputSample> samples{{{40, 80}, input(.2)}, {{40, 80}, input(.6)},
            {{140, 80}, input(.6)}, {{140, 80}, input(.9)}, {{240, 80}, input(.9)}, {{240, 80}, input(.3)}};
        for (auto *document : {&actual, &expected}) { document->load(project); document->setBrushRandomSeed(917); }
        paint(actual, b, samples); actual.preserveStrokeShapeOriginal();
        QPainterPath line({40, 180}); line.lineTo({240, 180});
        QVERIFY(actual.previewStrokePath(line, samples));
        auto straight = samples; for (auto &sample : straight) sample.point.setY(180);
        paint(expected, b, straight);
        QCOMPARE(layer(actual).tiles, layer(expected).tiles);
        // The final stationary pressure packet must also govern the next span.
        for (auto *document : {&actual, &expected}) document->continueStroke({300, 180}, input(.3));
        QCOMPARE(layer(actual).tiles, layer(expected).tiles);
        actual.endStroke(); expected.endStroke();
        if (vector) compareCapturedStroke(actual, expected);
    }

    void vectorSampleLimitPreservesPreviousPreviewAndOriginal() {
        Document document, expected; const auto project = fixture(true);
        BrushSettings b; b.size = 1; b.spacing = .01; b.pressureSize = false;
        const auto samples = profile();
        for (auto *doc : {&document, &expected}) { doc->load(project); doc->setBrushRandomSeed(71); paint(*doc, b, samples); }
        const auto original = layer(document).tiles;
        document.preserveStrokeShapeOriginal();
        QPainterPath small({40, 180}); small.lineTo({240, 180});
        QVERIFY(document.previewStrokePath(small, samples));
        const auto previous = layer(document).tiles;
        const auto dab = document.brushDabIndex(); const auto seed = document.brushStrokeSeed();
        QSignalSpy warnings(&document, &Document::warning);
        QPainterPath tooManySamples({40, 180}); tooManySamples.lineTo({20040, 180});
        QVERIFY(!document.previewStrokePath(tooManySamples, samples));
        QVERIFY(document.strokeActive()); QCOMPARE(layer(document).tiles, previous);
        QCOMPARE(document.brushDabIndex(), dab); QCOMPARE(document.brushStrokeSeed(), seed);
        QVERIFY(!warnings.isEmpty()); QCOMPARE(document.history()->count(), 0);
        QVERIFY(document.restoreStrokeShapeOriginal()); QCOMPARE(layer(document).tiles, original);
        for (auto *doc : {&document, &expected}) { doc->continueStroke({280, 120}, input(.7)); doc->endStroke(); }
        QCOMPARE(layer(document).tiles, layer(expected).tiles); compareCapturedStroke(document, expected);
    }

    void tileLimitPreservesOriginalWithoutLargeAllocations() {
        auto project = fixture();
        auto occupied = RasterLayer::create("Occupied tiles"); occupied.visible = false;
        QImage shared(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); shared.fill(Qt::black);
        // All 1023 entries share one 256 KiB image; only the active stroke detaches.
        for (int i = 0; i < maxProjectTiles - 1; ++i) occupied.tiles.insert({i % 32, i / 32}, shared);
        project.shots[0].layers.append(occupied);
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        Document document; document.load(project);
        BrushSettings b; b.size = 8; b.pressureSize = false;
        const auto samples = profile(); paint(document, b, samples);
        const auto original = layer(document).tiles;
        QCOMPARE(original.size(), 1); document.preserveStrokeShapeOriginal();
        QSignalSpy warnings(&document, &Document::warning);
        QPainterPath newTile({40, 180}); newTile.lineTo({350, 180});
        QVERIFY(!document.previewStrokePath(newTile, samples));
        QVERIFY(document.strokeActive()); QCOMPARE(layer(document).tiles, original);
        QVERIFY(!warnings.isEmpty()); QVERIFY(document.restoreStrokeShapeOriginal());
        document.endStroke(); QCOMPARE(document.history()->count(), 1);
        QCOMPARE(layer(document).tiles, original);
    }

    void fittedVectorCaptureReplaysExactlyAndCancelIsClean() {
        Document document; document.load(fixture(true)); document.setBrushRandomSeed(451);
        auto b = brush(); b.symmetry.mode = SymmetryMode::Vertical;
        QImage stamp(13, 9, QImage::Format_Grayscale8); stamp.fill(0);
        { QPainter painter(&stamp); painter.fillRect(2, 2, 7, 4, Qt::white); }
        b.tipMask = stamp; b.angleDynamics.control = BrushControl::Direction;
        QPainterPath selection; selection.addRect(QRectF(30, 30, 330, 270)); document.setRasterSelection(selection);
        const auto samples = profile(); paint(document, b, samples); document.preserveStrokeShapeOriginal();
        QPainterPath ellipse; ellipse.addEllipse(QRectF(90, 75, 230, 190));
        QVERIFY(document.previewStrokePath(ellipse, samples)); document.endStroke();
        QCOMPARE(layer(document).objects.size(), 1);
        auto rebuilt = layer(document); QString error;
        QVERIFY2(rebuildObjectLayer(rebuilt, document.drawingBounds(), 1024, &error), qPrintable(error));
        QCOMPARE(rebuilt.tiles, layer(document).tiles);
        const auto pixels = layer(document).tiles;
        paint(document, b, samples); document.preserveStrokeShapeOriginal();
        QVERIFY(document.previewStrokePath(ellipse, samples)); document.cancelStroke();
        QCOMPARE(layer(document).tiles, pixels); QCOMPARE(layer(document).objects.size(), 1);
        QCOMPARE(document.history()->count(), 1); QVERIFY(!document.strokeActive());
    }

    void eraserPreviewAndOriginalRemainOneUndo() {
        Document document; document.load(fixture());
        BrushSettings background; background.size = 350; background.pressureSize = false;
        QVERIFY(document.beginStroke({230, 190}, 1, background, false)); document.endStroke();
        const auto before = layer(document).tiles;
        const auto samples = profile(); const auto b = brush(); paint(document, b, samples, true);
        const auto original = layer(document).tiles; QVERIFY(original != before);
        document.preserveStrokeShapeOriginal(); QPainterPath line({60, 190}); line.lineTo({320, 190});
        QVERIFY(document.previewStrokePath(line, samples)); QVERIFY(layer(document).tiles != original);
        QVERIFY(document.restoreStrokeShapeOriginal()); QCOMPARE(layer(document).tiles, original);
        document.endStroke(); QCOMPARE(document.history()->count(), 2);
        document.history()->undo(); QCOMPARE(layer(document).tiles, before);
        document.history()->redo(); QCOMPARE(layer(document).tiles, original);
    }
};

QTEST_MAIN(StrokeShapePreviewTests)
#include "test_stroke_shape_preview.moc"
