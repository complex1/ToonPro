#include "brush_engine.h"
#include "brush_library.h"
#include "document.h"
#include "vector_brush.h"
#include "vector_brush_storage.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QSet>
#include <QtTest>
#include <cmath>
#include <limits>
using namespace toon;
namespace {
Project project(bool vector = false) {
    auto p = Project::create(); p.width = 320; p.height = 256; p.transparent = true;
    p.shots[0].sheet.overscan = 0;
    p.shots[0].layers[0].kind = vector ? LayerKind::Vector : LayerKind::Raster;
    return p;
}
const RasterLayer &layer(const Document &d) { return d.project().shots[0].layers[0]; }
BrushSettings dynamicBrush() {
    BrushSettings b; b.size = 27; b.spacing = .35; b.opacity = .7; b.strokeOpacity = .8;
    b.tipMask = QImage(24, 12, QImage::Format_Grayscale8); b.tipMask.fill(0);
    for (int y = 0; y < 12; ++y) for (int x = 0; x < 24; ++x)
        b.tipMask.scanLine(y)[x] = uchar((x * 17 + y * 31) % 256);
    b.sizeDynamics.response = {0, .5, .7, .9, 1}; b.sizeDynamics.jitter = .25;
    b.angleDynamics.control = BrushControl::Rotation; b.angleDynamics.jitter = .2;
    b.angleDynamics.jitterPerStroke = true;
    b.opacityDynamics.control = BrushControl::Pressure; b.opacityDynamics.response = {0, .1, .25, .5, 1};
    b.flowDynamics.control = BrushControl::Pressure; b.flowDynamics.response = {0, .4, .7, .9, 1};
    b.scatter = .6; b.scatterBothAxes = true; b.lockRandomSeed = true; b.randomSeed = 713;
    return b;
}
void draw(Document &d, const BrushSettings &b) {
    BrushInput in; in.pressure = .35; in.rotation = -30;
    QVERIFY(d.beginStroke({40, 100}, in, b, false));
    for (int i = 1; i <= 12; ++i) {
        in.pressure = .2 + .06 * i; in.rotation = -30 + 13 * i;
        d.continueStroke({40. + 17 * i, 100. + 30 * std::sin(i * .4)}, in);
    }
    d.endStroke();
}
QRect footprint(const QImage &mask) {
    QRect result;
    for (int y = 0; y < mask.height(); ++y) for (int x = 0; x < mask.width(); ++x)
        if (mask.constScanLine(y)[x] > 16) result |= QRect(x, y, 1, 1);
    return result;
}
bool writeJson(const QString &path, const QJsonObject &object) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(object).toJson()) > 0;
}
QByteArray maskDigest(const QImage &image) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(image.width()) + ':' + QByteArray::number(image.height()));
    for (int y = 0; y < image.height(); ++y)
        hash.addData(QByteArrayView(reinterpret_cast<const char *>(image.constScanLine(y)), image.width()));
    return hash.result();
}
QRgb pixel(const RasterLayer &layer, QPoint point) {
    const auto image = layer.tiles.value({point.x() / tileSize, point.y() / tileSize});
    return image.isNull() ? QRgb(0) : image.pixel(point.x() % tileSize, point.y() % tileSize);
}
}
class BrushDynamicsTest : public QObject {
    Q_OBJECT
private slots:
    void responseCurvesAffectSizeOpacityAndFlow() {
        BrushDynamic d; d.control = BrushControl::Pressure; d.response = {0, .1, .25, .5, 1};
        BrushInput in; in.pressure = .375;
        QCOMPARE(brushDynamicValue(d, in, 0, 1, 1), .175);
        in.pressure = .5;
        QCOMPARE(brushDynamicValue(d, in, 0, 1, 1), .25);
        d.minimum = .4;
        QCOMPARE(brushDynamicValue(d, in, 0, 1, 1), .4);
        BrushSettings b; b.size = 80; b.sizeDynamics.minimum = 0;
        const auto normal = footprint(brushTipMask(b, in));
        b.sizeDynamics.response = {0, .1, .25, .5, 1};
        const auto curved = footprint(brushTipMask(b, in));
        QVERIFY(curved.width() < normal.width() * .6);
        b.opacity = .8; b.strokeOpacity = .6;
        b.flowDynamics = d; b.flowDynamics.minimum = 0;
        b.opacityDynamics = d; b.opacityDynamics.minimum = 0;
        const auto dab = evaluateBrushDab(b, in, {}, 0, 0, 0, 1);
        QCOMPARE(dab.flow, .2); QCOMPARE(dab.opacity, .15);
        b.sizeDynamics.response = {1, 1, 1, 1, 1};
        QVERIFY(footprint(brushTipMask(b, in)).width() > normal.width() * 1.8);
    }
    void legacyDefaultsKeepExactDynamicFormulaAndJson() {
        BrushDynamic d; d.jitter = .6; d.minimum = .13;
        for (auto control : {BrushControl::Off, BrushControl::Pressure, BrushControl::Tilt, BrushControl::Wheel,
                             BrushControl::Fade, BrushControl::InitialDirection, BrushControl::Direction}) {
            d.control = control;
            for (int i = 0; i < 24; ++i) {
                BrushInput in; in.pressure = i / 23.; in.tiltX = i; in.wheel = .7;
                const qreal before = qBound(d.minimum, brushControlValue(control, in, i, d.fade, 27, 81) *
                    (1 - d.jitter * brushRandom(19, i, 4)), 1.0);
                QCOMPARE(brushDynamicValue(d, in, i, 19, 4, 27, 81), before);
            }
        }
        const auto json = brushSettingsToJson(BrushSettings{});
        QVERIFY(!json.contains("randomSeed")); QVERIFY(!json.contains("lockRandomSeed"));
        QVERIFY(!json["sizeDynamics"].toObject().contains("response"));
        QVERIFY(!json["angleDynamics"].toObject().contains("jitterPerStroke"));
        auto decoded = dynamicBrush(); QString error;
        QVERIFY2(brushSettingsFromJson(json, decoded, &error), qPrintable(error));
        QVERIFY(!usesExtendedBrushDynamics(decoded));
        QCOMPARE(decoded.sizeDynamics.response, BrushDynamic{}.response);
        QCOMPARE(decoded.randomSeed, quint32(1)); QVERIFY(!decoded.lockRandomSeed);
        QCOMPARE(project().toJson()["version"].toInt(), 19);
    }
    void legacyAngleMinimumDoesNotChangeSavedStrokeOrientation() {
        BrushSettings legacy; legacy.size = 60; legacy.pressureSize = false;
        legacy.tipMask = QImage(24, 8, QImage::Format_Grayscale8); legacy.tipMask.fill(255);
        legacy.angleDynamics.control = BrushControl::Pressure;
        legacy.angleDynamics.minimum = .5;
        BrushInput in; in.pressure = .25;
        auto expected = legacy; expected.angleDynamics = {}; expected.angle = 90;
        const auto expectedPixels = brushTipMask(expected, in);
        QCOMPARE(brushTipMask(legacy, in), expectedPixels);
        QVERIFY(!usesExtendedBrushDynamics(legacy));
        auto restored = legacy; QString error;
        QVERIFY2(brushSettingsFromJson(brushSettingsToJson(legacy), restored, &error), qPrintable(error));
        QCOMPARE(brushTipMask(restored, in), expectedPixels);
        // A newly configured response may use the minimum; it is explicitly
        // versioned and cannot silently alter older vector captures.
        restored.angleDynamics.response = {0, .1, .4, .7, 1};
        expected.angle = 180;
        QCOMPARE(brushTipMask(restored, in), brushTipMask(expected, in));
        QVERIFY(usesExtendedBrushDynamics(restored));
    }
    void rotationControlAndResponseAvoidDoubleRotation() {
        BrushSettings legacy; legacy.size = 60; legacy.pressureSize = false;
        legacy.tipMask = QImage(24, 8, QImage::Format_Grayscale8); legacy.tipMask.fill(255);
        BrushInput in; in.rotation = 90;
        auto explicitRotation = legacy; explicitRotation.angleDynamics.control = BrushControl::Rotation;
        QCOMPARE(brushTipMask(explicitRotation, in), brushTipMask(legacy, in));
        explicitRotation.angleDynamics.response = {0, .125, .25, .5, 1};
        auto half = in; half.rotation = 45;
        QCOMPARE(brushTipMask(explicitRotation, in), brushTipMask(legacy, half));
        QCOMPARE(brushControlValue(BrushControl::Rotation, in, 0, 25), .25);
        in.rotation = -90;
        QCOMPARE(brushControlValue(BrushControl::Rotation, in, 0, 25), .75);
        auto width = legacy; width.sizeDynamics.control = BrushControl::Rotation;
        QVERIFY(footprint(brushTipMask(width, in)).width() < footprint(brushTipMask(legacy, in)).width());
    }
    void jitterCanStayConstantPerStrokeOrVaryPerDab() {
        BrushDynamic d; d.jitter = .9; d.jitterPerStroke = true;
        const auto first = brushDynamicValue(d, {}, 0, 319, 2);
        for (int i = 1; i < 30; ++i) QCOMPARE(brushDynamicValue(d, {}, i, 319, 2), first);
        QVERIFY(brushDynamicValue(d, {}, 0, 320, 2) != first);
        d.jitterPerStroke = false;
        QVERIFY(brushDynamicValue(d, {}, 3, 319, 2) != first);
        BrushSettings b; b.size = 60; b.pressureSize = false; b.roundness = .25;
        b.angleDynamics.jitter = .9; b.angleDynamics.jitterPerStroke = true;
        QCOMPARE(brushTipMask(b, {}, 0, 0, 0, 319), brushTipMask(b, {}, 0, 0, 20, 319));
        b.angleDynamics.jitterPerStroke = false;
        QVERIFY(brushTipMask(b, {}, 0, 0, 0, 319) != brushTipMask(b, {}, 0, 0, 20, 319));
    }
    void eachDynamicChannelChangesWithinOneHeldStroke() {
        // Use painting-size dabs and constant tablet input. Preview pressure ramps
        // can otherwise hide a setting that only changes when the pen is lifted.
        BrushSettings base; base.size = 192; base.pressureSize = false;
        base.tipMask = QImage(48, 18, QImage::Format_Grayscale8); base.tipMask.fill(255);
        base.color = QColor("#358c4c"); base.backgroundColor = QColor("#c7ae42");
        BrushInput input; input.pressure = .75;
        for (int channel = 0; channel < 5; ++channel) {
            auto brush = base;
            if (channel == 0) brush.sizeDynamics.jitter = .4;
            if (channel == 1) brush.angleDynamics.jitter = .8;
            if (channel == 2) brush.flowDynamics.jitter = .6;
            if (channel == 3) brush.opacityDynamics.jitter = .6;
            if (channel == 4) {
                brush.colorDynamics.enabled = true; brush.colorDynamics.perTip = true;
                brush.colorDynamics.foregroundBackground = .4;
                brush.colorDynamics.hue = .04; brush.colorDynamics.saturation = .1;
                brush.colorDynamics.brightness = .15;
            }
            QSet<QByteArray> masks; QSet<qreal> values; QSet<QRgb> colors;
            for (int dab = 0; dab < 32; ++dab) {
                const auto stamp = evaluateBrushDab(brush, input, {300, 240}, 0, 0, dab, 713);
                const auto repeated = evaluateBrushDab(brush, input, {300, 240}, 0, 0, dab, 713);
                QCOMPARE(stamp.mask, repeated.mask); QCOMPARE(stamp.flow, repeated.flow);
                QCOMPARE(stamp.opacity, repeated.opacity); QCOMPARE(stamp.color, repeated.color);
                masks.insert(maskDigest(stamp.mask)); colors.insert(stamp.color.rgba());
                values.insert(channel == 2 ? stamp.flow : stamp.opacity);
            }
            const int distinct = channel < 2 ? masks.size() : channel < 4 ? values.size() : colors.size();
            QVERIFY2(distinct >= 24, qPrintable(QString("Channel %1 varied in only %2 of 32 dabs").arg(channel).arg(distinct)));
        }
    }
    void actualHeldStrokeChangesBetweenDabsAndIgnoresEventBatching() {
        auto p = project(); p.width = 2304; p.height = 512;
        auto b = dynamicBrush(); b.size = 192; b.spacing = 1.25; b.pressureSize = false;
        b.tipMask = QImage(48, 18, QImage::Format_Grayscale8); b.tipMask.fill(255);
        b.sizeDynamics = {}; b.sizeDynamics.jitter = .25;
        b.angleDynamics = {}; b.angleDynamics.jitter = .5;
        b.opacityDynamics = {}; b.opacityDynamics.jitter = .25;
        b.flowDynamics = {}; b.flowDynamics.jitter = .25;
        b.scatter = 0; b.color = QColor("#358c4c"); b.backgroundColor = QColor("#c7ae42");
        b.colorDynamics.enabled = true; b.colorDynamics.perTip = true;
        b.colorDynamics.foregroundBackground = .3; b.colorDynamics.hue = .04;
        b.colorDynamics.saturation = .1; b.colorDynamics.brightness = .15;
        BrushInput input; input.pressure = .75;
        Document grouped, separate, vector; grouped.load(p); separate.load(p);
        p.shots[0].layers[0].kind = LayerKind::Vector; vector.load(p);
        const QPointF start(192, 256), end(2112, 256);
        QVERIFY(grouped.beginStroke(start, input, b, false));
        QVERIFY(separate.beginStroke(start, input, b, false));
        QVERIFY(vector.beginStroke(start, input, b, false));
        grouped.continueStroke(end, input);
        vector.continueStroke(end, input);
        for (int x = start.x() + 8; x <= end.x(); x += 8) separate.continueStroke({qreal(x), 256}, input);
        QVERIFY(grouped.strokeActive()); QVERIFY(separate.strokeActive()); QVERIFY(vector.strokeActive());
        QCOMPARE(layer(grouped).tiles, layer(separate).tiles);
        QCOMPARE(layer(grouped).tiles, layer(vector).tiles);
        QSet<QRgb> renderedColors;
        for (int x = start.x(); x <= end.x(); x += 240) {
            const auto rendered = pixel(layer(grouped), {x, 256});
            QVERIFY(qAlpha(rendered) > 20);
            renderedColors.insert(qUnpremultiply(rendered) | 0xff000000u);
        }
        QVERIFY2(renderedColors.size() >= 8, "A single held stroke must visibly vary before pen-up");
        grouped.endStroke(); separate.endStroke(); vector.endStroke();
        const auto &capture = *layer(vector).objects[0].brushStroke;
        QCOMPARE(capture.samples.size(), 9);
        for (int i = 0; i < capture.samples.size(); ++i) QCOMPARE(capture.samples[i].dabIndex, quint64(i));
        auto replayed = layer(vector); QString error;
        QVERIFY2(rebuildObjectLayer(replayed, vector.drawingBounds(), maxProjectTiles, &error), qPrintable(error));
        QCOMPARE(replayed.tiles, layer(grouped).tiles);
    }
    void continuousLineTextureLeavesItsBackgroundTransparent() {
        Document d; d.load(project());
        BrushSettings b; b.size = 192; b.pressureSize = false; b.hardness = 0;
        b.opacity = .6; b.spacing = .05; b.texture.enabled = true;
        b.texture.eachTip = false; b.texture.depth = 1; b.texture.mode = MaskBlend::Multiply;
        b.texture.pattern = QImage(32, 32, QImage::Format_Grayscale8); b.texture.pattern.fill(0);
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x)
            if (x < 2 || y < 2) b.texture.pattern.scanLine(y)[x] = 255;
        QVERIFY(d.beginStroke({40, 128}, 1, b, false));
        d.continueStroke({280, 128}, 1);
        int paintedLines = 0;
        for (int y = 105; y <= 151; ++y) for (int x = 50; x <= 270; ++x) {
            const int alpha = qAlpha(pixel(layer(d), {x, y}));
            if (x % 32 >= 2 && y % 32 >= 2) QCOMPARE(alpha, 0);
            else if (alpha) ++paintedLines;
        }
        QVERIFY(paintedLines > 500);
        d.endStroke();
    }
    void seedLockRepeatsPixelsAndUnlockKeepsSequence() {
        Document d; d.load(project()); d.setBrushRandomSeed(900);
        auto b = dynamicBrush(); draw(d, b); QCOMPARE(d.brushStrokeSeed(), b.randomSeed);
        const auto original = layer(d).tiles; d.history()->undo();
        draw(d, b); QCOMPARE(layer(d).tiles, original); d.history()->undo();
        ++b.randomSeed; draw(d, b); QVERIFY(layer(d).tiles != original); d.history()->undo();
        b.lockRandomSeed = false; draw(d, b); QCOMPARE(d.brushStrokeSeed(), quint32(900));
        const auto unlocked = layer(d).tiles; d.history()->undo();
        draw(d, b); QCOMPARE(d.brushStrokeSeed(), quint32(900) + 0x9e3779b9U);
        QVERIFY(layer(d).tiles != unlocked);
        b.lockRandomSeed = true;
        const auto preview = brushStrokePreview(b);
        QCOMPARE(brushStrokePreview(b), preview); ++b.randomSeed;
        QVERIFY(brushStrokePreview(b) != preview);
    }
    void rasterVectorReplayAndProjectRoundTrip() {
        auto b = dynamicBrush(); Document raster, vector;
        raster.load(project()); vector.load(project(true)); draw(raster, b); draw(vector, b);
        QCOMPARE(layer(vector).objects.size(), 1); QCOMPARE(layer(vector).tiles, layer(raster).tiles);
        QCOMPARE(layer(vector).objects[0].brushStroke->seed, b.randomSeed);
        auto rebuilt = layer(vector); QString error;
        QVERIFY2(rebuildObjectLayer(rebuilt, vector.drawingBounds(), maxProjectTiles, &error), qPrintable(error));
        QCOMPARE(rebuilt.tiles, layer(vector).tiles);
        QTemporaryDir dir; const auto path = dir.filePath("dynamics.toonproj");
        QVERIFY2(saveProject(vector.project(), path, &error), qPrintable(error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.read(10), QByteArray("TOONPRO21\n"));
        const auto loaded = loadProject(path, &error); QVERIFY2(loaded.has_value(), qPrintable(error));
        QCOMPARE(loaded->shots[0].layers[0].tiles, layer(vector).tiles);
        QCOMPARE(loaded->shots[0].layers[0].objects[0].brushStroke->settings, b);
        auto json = vector.project().toJson(); json["version"] = 20;
        QVERIFY(!Project::fromJson(json, &error)); QVERIFY(error.contains("format 21"));
        auto capture = vectorBrushStrokeToJson(*layer(vector).objects[0].brushStroke);
        QCOMPARE(capture["version"].toInt(), 3); capture["version"] = 2;
        VectorBrushStroke source; QVERIFY(!vectorBrushStrokeFromJson(capture, &source, &error));
        QVERIFY(error.contains("capture version 3"));
        json["version"] = latestProjectFormatVersion + 1; QVERIFY(!Project::fromJson(json, &error));
    }
    void libraryVersionAndLegacyMigration() {
        QTemporaryDir dir; const auto path = dir.filePath("brushes.json"); QString error;
        BrushLibrary library(path); QVERIFY(library.load(&error)); auto b = dynamicBrush();
        QVERIFY2(library.saveCurrent("Dynamic pen", b, 12, &error), qPrintable(error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        auto json = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        QCOMPARE(json["version"].toInt(), 4);
        BrushLibrary restored(path); QVERIFY2(restored.load(&error), qPrintable(error));
        QCOMPARE(restored.presets().last().brush, b);
        json["version"] = 3; QVERIFY(writeJson(path, json));
        BrushLibrary disguised(path); QVERIFY(!disguised.load(&error)); QVERIFY(error.contains("format 4"));
        json["version"] = 99; QVERIFY(writeJson(path, json));
        BrushLibrary future(path); QVERIFY(!future.load(&error));
        const auto legacyPath = dir.filePath("legacy.json"); BrushLibrary legacy(legacyPath);
        QVERIFY(legacy.load(&error)); QVERIFY(legacy.saveCurrent("Classic pen", BrushSettings{}, 0, &error));
        QFile legacyFile(legacyPath); QVERIFY(legacyFile.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(legacyFile.readAll()).object()["version"].toInt(), 2);
    }
    void invalidDynamicsFailAtomically() {
        auto b = dynamicBrush(); const auto original = b; QString error;
        const auto valid = brushSettingsToJson(b);
        for (const auto &invalid : {QJsonValue(QJsonArray{0, 1}), QJsonValue(QJsonArray{0, .25, "oops", .75, 1}),
                                    QJsonValue(QJsonArray{0, .25, .5, .75, 1.1}), QJsonValue("linear")}) {
            auto json = valid; auto dynamic = json["sizeDynamics"].toObject();
            dynamic["response"] = invalid; json["sizeDynamics"] = dynamic;
            QVERIFY(!brushSettingsFromJson(json, b, &error)); QCOMPARE(b, original);
        }
        for (const auto &invalid : {QJsonValue(-1), QJsonValue(1.5), QJsonValue(4294967296.0), QJsonValue("7")}) {
            auto json = valid; json["randomSeed"] = invalid;
            QVERIFY(!brushSettingsFromJson(json, b, &error)); QCOMPARE(b, original);
        }
        auto json = valid; json["lockRandomSeed"] = "yes";
        QVERIFY(!brushSettingsFromJson(json, b, &error)); QCOMPARE(b, original);
        json = valid; auto dynamic = json["angleDynamics"].toObject(); dynamic["control"] = 8;
        json["angleDynamics"] = dynamic; QVERIFY(!brushSettingsFromJson(json, b, &error)); QCOMPARE(b, original);
        json = valid; dynamic = json["flowDynamics"].toObject(); dynamic["curveTypo"] = 1;
        json["flowDynamics"] = dynamic; QVERIFY(!brushSettingsFromJson(json, b, &error)); QCOMPARE(b, original);
        b.sizeDynamics.response[2] = std::numeric_limits<qreal>::quiet_NaN();
        QVERIFY(!validateBrushSettings(b, &error));
    }
};
QTEST_GUILESS_MAIN(BrushDynamicsTest)
#include "test_brush_dynamics.moc"
