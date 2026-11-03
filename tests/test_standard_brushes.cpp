#include "standard_brushes.h"
#include "catalog_brushes.h"
#include "brush_engine.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

using namespace toon;

namespace {
BrushPreset presetNamed(const QString &id) {
    for (const auto &preset : standardBrushPresets())
        if (preset.id == "builtin/" + id) return preset;
    return {};
}
QByteArray imageDigest(const QImage &image) {
    return QCryptographicHash::hash(
        QByteArray::fromRawData(reinterpret_cast<const char *>(image.constBits()), int(image.sizeInBytes())),
        QCryptographicHash::Sha256);
}
int paintedPixels(const QImage &image) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixel(x, y) != qRgb(255, 255, 255)) ++count;
    return count;
}
} // namespace

class StandardBrushesTests : public QObject {
    Q_OBJECT
private slots:
    void legacyBrushesStayUnchanged() {
        QTemporaryDir temporary;
        BrushLibrary library(temporary.filePath("brushes.json"));
        QVERIFY(library.load());
        QCOMPARE(library.presets().size(), 19 + catalogBrushPresets().size());
        const QStringList ids{"builtin/ink", "builtin/sketch", "builtin/blocking"};
        const QStringList names{"Round Ink", "Round Sketch", "Round Blocking"};
        const QList<qreal> sizes{12, 6, 96}, flows{1, .25, 1}, smoothing{20, 10, 0};
        for (int i = 0; i < 3; ++i) {
            const auto &preset = library.presets()[i];
            BrushSettings expected;
            expected.size = sizes[i]; expected.opacity = flows[i]; expected.pressureSize = i != 2;
            QCOMPARE(preset.id, ids[i]); QCOMPARE(preset.name, names[i]);
            QVERIFY(preset.brush == expected);
            QCOMPARE(preset.smoothing, smoothing[i]);
            QVERIFY(preset.builtIn);
            QVERIFY(preset.source.isEmpty());
            QCOMPARE(standardBrushCategory(preset.id), QString("Drawing"));
            QVERIFY(standardBrushTags(preset.id).isEmpty());
        }
    }

    void presetsAreValidAndResourcesAreSmall() {
        const auto presets = standardBrushPresets();
        QCOMPARE(presets.size(), 16);
        QSet<QString> ids, names, categories;
        qint64 resourceBytes = 0;
        for (const auto &preset : presets) {
            QString error;
            QVERIFY2(validateBrushSettings(preset.brush, &error), qPrintable(preset.name + ": " + error));
            QVERIFY(preset.builtIn);
            QVERIFY(preset.id.startsWith("builtin/"));
            QVERIFY(!ids.contains(preset.id)); ids.insert(preset.id);
            QVERIFY(!names.contains(preset.name)); names.insert(preset.name);
            QVERIFY(preset.source.startsWith("Toon Pro native"));
            QVERIFY(!standardBrushCategory(preset.id).isEmpty());
            categories.insert(standardBrushCategory(preset.id));
            QVERIFY(standardBrushTags(preset.id).contains("Standard"));
            QVERIFY(preset.brush.colorTip.isNull());
            // Each native resource is much smaller than the imported-tip limit,
            // and particle presets do not multiply a stroke into huge dab counts.
            QVERIFY(preset.brush.count <= 2);
            for (const auto &resource : {preset.brush.tipMask, preset.brush.texture.pattern}) {
                if (resource.isNull()) continue;
                QCOMPARE(resource.format(), QImage::Format_Grayscale8);
                QVERIFY(resource.width() <= 128); QVERIFY(resource.height() <= 128);
                resourceBytes += resource.sizeInBytes();
            }
        }
        QVERIFY(categories.size() >= 5);
        QVERIFY(resourceBytes < 96 * 1024);
        QVERIFY(standardBrushCategory("imported/custom").isEmpty());
        QVERIFY(standardBrushTags("imported/custom").isEmpty());
    }

    void eachPresetMakesADistinctRepeatableStroke() {
        QSet<QByteArray> rendered;
        for (const auto &preset : standardBrushPresets()) {
            const auto image = brushStrokePreview(preset.brush, {240, 80}, Qt::white);
            QVERIFY2(paintedPixels(image) > 10, qPrintable(preset.name + " must make a visible stroke"));
            QCOMPARE(brushStrokePreview(preset.brush, {240, 80}, Qt::white), image);
            auto detached = preset.brush;
            detached.tipMask = detached.tipMask.copy();
            detached.texture.pattern = detached.texture.pattern.copy();
            QCOMPARE(brushStrokePreview(detached, {240, 80}, Qt::white), image);
            const auto digest = imageDigest(image);
            QVERIFY2(!rendered.contains(digest), qPrintable(preset.name + " must differ from the other starter strokes"));
            rendered.insert(digest);
        }
    }

    void familiesRespondToPressureAndRandomness() {
        BrushInput light, heavy;
        light.pressure = .15; heavy.pressure = 1;
        const auto pen = presetNamed("g-pen").brush;
        QVERIFY(brushTipMask(pen, light).width() < brushTipMask(pen, heavy).width());
        const auto liner = presetNamed("fine-liner").brush;
        QCOMPARE(brushTipMask(liner, light), brushTipMask(liner, heavy));
        const auto airbrush = presetNamed("soft-airbrush").brush;
        const auto softLight = evaluateBrushDab(airbrush, light, {40, 40}, 0, 0, 0, 7);
        const auto softHeavy = evaluateBrushDab(airbrush, heavy, {40, 40}, 0, 0, 0, 7);
        QCOMPARE(softLight.mask, softHeavy.mask);
        QVERIFY(softLight.flow < softHeavy.flow);
        const auto stipple = presetNamed("stipple").brush;
        const auto first = evaluateBrushDab(stipple, heavy, {40, 40}, 0, 0, 5, 7);
        const auto repeat = evaluateBrushDab(stipple, heavy, {40, 40}, 0, 0, 5, 7);
        const auto reseeded = evaluateBrushDab(stipple, heavy, {40, 40}, 0, 0, 5, 19);
        QCOMPARE(first.offset, repeat.offset); QCOMPARE(first.mask, repeat.mask);
        QVERIFY(first.offset != reseeded.offset);
        const auto flat = presetNamed("flat-paint").brush;
        QVERIFY(flat.tipMask.width() > flat.tipMask.height() * 2);
        const auto wash = presetNamed("pigment-wash");
        QVERIFY(wash.brush.strokeOpacity < .5);
        QVERIFY(wash.compatibility.join(' ').contains("no fluid"));
    }

    void copiedStarterBrushesRoundTripWithoutChangingDefaults() {
        QTemporaryDir temporary;
        const auto path = temporary.filePath("brushes.json");
        BrushLibrary library(path);
        QVERIFY(library.load());
        auto copies = standardBrushPresets();
        for (auto &copy : copies) {
            copy.id.replace("builtin/", "user/");
            copy.name += " copy";
            copy.builtIn = false;
        }
        QString error;
        QVERIFY2(library.addPresets(copies, &error), qPrintable(error));
        BrushLibrary restored(path);
        QVERIFY2(restored.load(&error), qPrintable(error));
        const auto builtInCount = 19 + catalogBrushPresets().size();
        QCOMPARE(restored.presets().size(), builtInCount + copies.size());
        for (int i = 0; i < copies.size(); ++i) {
            const auto &actual = restored.presets()[builtInCount + i];
            QCOMPARE(actual.id, copies[i].id);
            QCOMPARE(actual.name, copies[i].name);
            QVERIFY(actual.brush == copies[i].brush);
            QCOMPARE(actual.smoothing, copies[i].smoothing);
        }
        // Editing an imported/copy tip cannot mutate the process-wide defaults.
        auto changed = presetNamed("charcoal");
        const auto expectedTip = changed.brush.tipMask.copy();
        changed.brush.tipMask.fill(0);
        QCOMPARE(presetNamed("charcoal").brush.tipMask, expectedTip);
    }

    void swatchPreview() {
        const auto destination = qEnvironmentVariable("TOON_BRUSH_SWATCH_DIR");
        if (destination.isEmpty()) QSKIP("Set TOON_BRUSH_SWATCH_DIR to export the native brush swatches.");
        QVERIFY(QDir().mkpath(destination));
        const auto presets = standardBrushPresets();
        constexpr int width = 360, height = 100;
        QImage atlas(width * 2, height * 8, QImage::Format_ARGB32_Premultiplied);
        atlas.fill(Qt::white);
        QPainter painter(&atlas);
        QByteArray manifest("row\tcolumn\tbrush\n");
        for (int i = 0; i < presets.size(); ++i) {
            const auto &preset = presets[i];
            const auto swatch = brushStrokePreview(preset.brush, {width - 8, height - 8}, Qt::white);
            const int column = i % 2, row = i / 2;
            painter.drawImage(QPoint(column * width + 4, row * height + 4), swatch);
            painter.setPen(QColor("#d8d8d8"));
            painter.drawRect(QRect(column * width, row * height, width - 1, height - 1));
            manifest += QString("%1\t%2\t%3\n").arg(row + 1).arg(column + 1).arg(preset.name).toUtf8();
            const QString name = preset.id.section('/', -1) + ".png";
            QVERIFY(swatch.save(QDir(destination).filePath(name)));
        }
        painter.end();
        QVERIFY(atlas.save(QDir(destination).filePath("standard-brushes.png")));
        QFile mapping(QDir(destination).filePath("standard-brushes.tsv"));
        QVERIFY(mapping.open(QIODevice::WriteOnly));
        QCOMPARE(mapping.write(manifest), manifest.size());
    }
};

QTEST_GUILESS_MAIN(StandardBrushesTests)
#include "test_standard_brushes.moc"
