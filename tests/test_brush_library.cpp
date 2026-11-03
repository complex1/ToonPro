#include "brush_library.h"
#include "standard_brushes.h"
#include "catalog_brushes.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>
#include <functional>

using namespace toon;
namespace { qsizetype builtinCount() { return 3 + standardBrushPresets().size() + catalogBrushPresets().size(); } }

class BrushLibraryTests : public QObject {
    Q_OBJECT
private:
    static QByteArray readFile(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
    }
    static bool writeFile(const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
    static BrushPreset sampledPreset() {
        BrushPreset preset;
        preset.id = "test-sample";
        preset.name = "Dry edge";
        preset.brush.color = QColor("#e51525");
        preset.brush.size = 47.5;
        preset.brush.opacity = .42;
        preset.brush.pressureSize = false;
        preset.brush.spacing = .31;
        preset.smoothing = 62;
        preset.source = "stamps.abr / sample 1 / sha256:example";
        preset.compatibility = {"Sampled tip only", "Photoshop texture settings omitted"};
        preset.brush.tipMask = QImage(7, 3, QImage::Format_Grayscale8);
        preset.brush.tipMask.fill(0);
        preset.brush.tipMask.scanLine(1)[2] = 192;
        preset.brush.tipMask.scanLine(2)[6] = 255;
        return preset;
    }

private slots:
    void firstRunAndBuiltIns() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto path = temporary.filePath("brushes/library.json");
        BrushLibrary library(path);
        QString error;
        QVERIFY(!library.writable());
        QVERIFY(!library.saveCurrent("Before load", {}, 0, &error));
        QVERIFY(library.load(&error));
        QVERIFY(error.isEmpty());
        QVERIFY(library.writable());
        QCOMPARE(library.presets().size(), builtinCount());
        QVERIFY(!QFile::exists(path));
        const auto id = library.presets().first().id;
        QVERIFY(library.presets().first().builtIn);
        QVERIFY(!library.renamePreset(id, "Edited builtin", &error));
        QVERIFY(!library.removePreset(id, &error));
        QVERIFY(!library.addPresets({library.presets().first()}, &error));
        QCOMPARE(library.presets().first().name, QString("Round Ink"));
        QCOMPARE(library.presets().size(), builtinCount());
        QVERIFY(!QFile::exists(path));
    }

    void sampledRoundTripAndEditing() {
        QTemporaryDir temporary;
        const auto path = temporary.filePath("brushes/library.json");
        BrushLibrary library(path);
        QVERIFY(library.load());
        QSignalSpy changed(&library, &BrushLibrary::changed);
        const auto sample = sampledPreset();
        QString error;
        QVERIFY2(library.addPresets({sample}, &error), qPrintable(error));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(library.presets().size(), builtinCount() + 1);
        QVERIFY(QFile::exists(path));
        const auto serialized = readFile(path);
        const auto storedSettings = QJsonDocument::fromJson(serialized).object()["presets"]
                                        .toArray().first().toObject()["settings"].toObject();
        QVERIFY(!storedSettings.contains("color"));
        QVERIFY(!storedSettings.contains("backgroundColor"));
        BrushLibrary restored(path);
        QVERIFY2(restored.load(&error), qPrintable(error));
        const auto actual = restored.presets().last();
        QCOMPARE(actual.id, sample.id);
        QCOMPARE(actual.name, sample.name);
        QCOMPARE(actual.source, sample.source);
        QCOMPARE(actual.compatibility, sample.compatibility);
        QCOMPARE(actual.brush.size, sample.brush.size);
        QCOMPARE(actual.brush.opacity, sample.brush.opacity);
        QCOMPARE(actual.brush.pressureSize, sample.brush.pressureSize);
        QCOMPARE(actual.brush.spacing, sample.brush.spacing);
        QCOMPARE(actual.smoothing, sample.smoothing);
        QCOMPARE(actual.brush.tipMask, sample.brush.tipMask);
        QCOMPARE(actual.brush.color, BrushSettings{}.color);
        QVERIFY(!actual.builtIn);
        QVERIFY2(restored.renamePreset(sample.id, "  Renamed stamp  ", &error), qPrintable(error));
        QCOMPARE(restored.presets().last().name, QString("Renamed stamp"));
        QVERIFY(restored.saveCurrent("My round brush", BrushSettings{}, 13, &error));
        const auto generated = restored.presets().last();
        QVERIFY(!generated.id.isEmpty());
        QVERIFY(generated.id != sample.id);
        QCOMPARE(generated.smoothing, qreal(13));
        QVERIFY(generated.brush.tipMask.isNull());
        QVERIFY(restored.removePreset(sample.id, &error));
        BrushLibrary edited(path);
        QVERIFY(edited.load());
        QCOMPARE(edited.presets().size(), builtinCount() + 1);
        QCOMPARE(edited.presets().last().id, generated.id);
    }

    void failedSaveKeepsMemoryAndSignals() {
        QTemporaryDir temporary;
        const auto path = temporary.filePath("library.json");
        BrushLibrary library(path);
        QVERIFY(library.load());
        QVERIFY(library.addPresets({sampledPreset()}));
        const auto previous = readFile(path);
        // A directory at the destination makes replacement fail on every OS,
        // even when tests run under an account that can ignore mode bits.
        QVERIFY(QFile::rename(path, path + ".previous"));
        QVERIFY(QDir().mkdir(path));
        QSignalSpy changed(&library, &BrushLibrary::changed);
        QString error;
        QVERIFY(!library.renamePreset("test-sample", "Unsaved name", &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(library.presets().last().name, QString("Dry edge"));
        QCOMPARE(changed.count(), 0);
        QCOMPARE(readFile(path + ".previous"), previous);
        QVERIFY(QDir().rmdir(path));
        QVERIFY(QFile::rename(path + ".previous", path));
        QVERIFY(library.renamePreset("test-sample", "Saved name", &error));
        QCOMPARE(changed.count(), 1);
    }

    void malformedLoadProtectsExistingFile() {
        QTemporaryDir temporary;
        const auto path = temporary.filePath("library.json");
        BrushLibrary library(path);
        QVERIFY(library.load());
        QVERIFY(library.addPresets({sampledPreset()}));
        const auto validBytes = readFile(path);
        const QByteArray broken("{\"presets\": this is broken");
        QVERIFY(writeFile(path, broken));
        QSignalSpy changed(&library, &BrushLibrary::changed);
        QString error;
        QVERIFY(!library.load(&error));
        QVERIFY(!library.writable());
        QCOMPARE(library.presets().last().name, QString("Dry edge"));
        QVERIFY(!library.saveCurrent("Accidental overwrite", {}, 0, &error));
        QVERIFY(!library.removePreset("test-sample", &error));
        QCOMPARE(readFile(path), broken);
        QCOMPARE(changed.count(), 0);
        QVERIFY(writeFile(path, validBytes));
        QVERIFY(library.load(&error));
        QVERIFY(library.writable());
        QCOMPARE(changed.count(), 1);
    }

    void duplicateAndCountLimitsAreAtomic() {
        QTemporaryDir temporary;
        const auto path = temporary.filePath("library.json");
        BrushLibrary library(path);
        QVERIFY(library.load());
        auto sample = sampledPreset();
        QVERIFY(library.addPresets({sample}));
        const auto previous = readFile(path);
        QSignalSpy changed(&library, &BrushLibrary::changed);
        QString error;
        QVERIFY(!library.addPresets({sample}, &error));
        QCOMPARE(library.presets().size(), builtinCount() + 1);
        QCOMPARE(readFile(path), previous);
        QVector<BrushPreset> tooMany;
        for (int i = 0; i < BrushLibrary::maxCustomPresets; ++i) {
            BrushPreset preset;
            preset.name = QString("Preset %1").arg(i);
            tooMany.push_back(preset);
        }
        QVERIFY(!library.addPresets(tooMany, &error));
        QCOMPARE(library.presets().size(), builtinCount() + 1);
        QCOMPARE(readFile(path), previous);
        QCOMPARE(changed.count(), 0);
        sample.id = "builtin/ink";
        QVERIFY(!library.addPresets({sample}, &error));
    }

    void settingsAndMaskLimits() {
        QTemporaryDir temporary;
        BrushLibrary library(temporary.filePath("library.json"));
        QVERIFY(library.load());
        QString error;
        auto preset = sampledPreset();
        preset.brush.spacing = 0;
        QVERIFY(!library.addPresets({preset}, &error));
        preset = sampledPreset();
        preset.brush.size = std::numeric_limits<qreal>::quiet_NaN();
        QVERIFY(!library.addPresets({preset}, &error));
        preset = sampledPreset();
        preset.brush.tipMask = QImage(3, 3, QImage::Format_ARGB32);
        preset.brush.tipMask.fill(Qt::white);
        QVERIFY(!library.addPresets({preset}, &error));
        preset.brush.tipMask = QImage(BrushLibrary::maxTipSide + 1, 1, QImage::Format_Grayscale8);
        preset.brush.tipMask.fill(0);
        QVERIFY(!library.addPresets({preset}, &error));
        preset.brush.tipMask = QImage(3, 3, QImage::Format_Grayscale8);
        preset.brush.tipMask.fill(0);
        QVERIFY(!library.addPresets({preset}, &error));
        preset = sampledPreset();
        preset.brush.tipMask = QImage(BrushLibrary::maxTipSide, BrushLibrary::maxTipSide, QImage::Format_Grayscale8);
        preset.brush.tipMask.fill(128);
        QVector<BrushPreset> oversized;
        const qint64 tipPixels = qint64(preset.brush.tipMask.width()) * preset.brush.tipMask.height();
        for (int i = 0; i <= BrushLibrary::maxTipPixels / tipPixels; ++i) {
            preset.id = QString("large-%1").arg(i);
            oversized.push_back(preset);
        }
        QVERIFY(!library.addPresets(oversized, &error));
        QCOMPARE(library.presets().size(), builtinCount());
        QVERIFY(!QFile::exists(library.path()));
    }

    void everyAdvancedSettingAndResourceRoundTrips() {
        QTemporaryDir temporary;
        const auto path = temporary.filePath("library.json");
        BrushLibrary library(path);
        QVERIFY(library.load());
        auto preset = sampledPreset();
        auto &b = preset.brush;
        b.backgroundColor = Qt::green;
        b.strokeOpacity = .61;
        b.hardness = .23;
        b.blendMode = BrushBlend::ColorBurn;
        b.angle = -138;
        b.roundness = .39;
        b.flipX = b.flipY = true;
        b.sizeDynamics = {.21, .13, BrushControl::Pressure, 42};
        b.angleDynamics = {.32, .24, BrushControl::InitialDirection, 53};
        b.roundnessDynamics = {.43, .35, BrushControl::Tilt, 64};
        b.tiltScale = 3.14;
        b.projection = true;
        b.scatter = 2.1;
        b.scatterBothAxes = true;
        b.scatterControl = BrushControl::Fade;
        b.scatterFade = 120;
        b.count = 4;
        b.countJitter = .34;
        b.countControl = BrushControl::Wheel;
        b.countFade = 101;
        b.texture.enabled = true;
        b.texture.name = "Paper grain";
        b.texture.pattern = QImage(5, 8, QImage::Format_Grayscale8);
        b.texture.pattern.fill(96);
        b.texture.pattern.scanLine(5)[3] = 217;
        b.texture.invert = true;
        b.texture.scale = 2.35;
        b.texture.brightness = -.16;
        b.texture.contrast = .37;
        b.texture.eachTip = true;
        b.texture.mode = MaskBlend::LinearHeight;
        b.texture.depth = .48;
        b.texture.dynamics = {.54, .46, BrushControl::Fade, 75};
        b.dual.enabled = true;
        b.dual.name = "Dry tip";
        b.dual.tipMask = QImage(3, 9, QImage::Format_Grayscale8);
        b.dual.tipMask.fill(171);
        b.dual.mode = MaskBlend::Difference;
        b.dual.size = 57.1;
        b.dual.spacing = 1.31;
        b.dual.scatter = 4.42;
        b.dual.count = 7;
        b.colorDynamics.enabled = true;
        b.colorDynamics.perTip = false;
        b.colorDynamics.foregroundBackground = .27;
        b.colorDynamics.control = BrushControl::Direction;
        b.colorDynamics.fade = 113;
        b.colorDynamics.hue = .18;
        b.colorDynamics.saturation = .29;
        b.colorDynamics.brightness = .31;
        b.colorDynamics.purity = -.52;
        b.opacityDynamics = {.65, .57, BrushControl::Wheel, 86};
        b.flowDynamics = {.76, .68, BrushControl::Pressure, 97};
        b.pose.overrideTiltX = b.pose.overrideTiltY = b.pose.overrideRotation = b.pose.overridePressure = true;
        b.pose.tiltX = -33;
        b.pose.tiltY = 54;
        b.pose.rotation = 76;
        b.pose.pressure = .59;
        b.noise = b.wetEdges = b.airbrush = b.protectTexture = true;
        b.airbrushRate = 43;
        b.smoothingEnabled = false;
        b.symmetry.mode = SymmetryMode::Spiral;
        b.symmetry.center = {.23, .67};
        b.symmetry.angle = -37;
        b.symmetry.count = 9;
        b.symmetry.radius = 153;
        b.symmetry.distance = 246;
        b.symmetry.amplitude = 79;
        b.symmetry.wavelength = 387;
        QString error;
        QVERIFY2(library.addPresets({preset}, &error), qPrintable(error));
        QCOMPARE(QJsonDocument::fromJson(readFile(path)).object()["version"].toInt(), 2);
        BrushLibrary reloaded(path);
        QVERIFY2(reloaded.load(&error), qPrintable(error));
        b.color = BrushSettings{}.color;
        b.backgroundColor = BrushSettings{}.backgroundColor;
        QVERIFY(reloaded.presets().last().brush == b);
        QCOMPARE(reloaded.presets().last().smoothing, preset.smoothing);
        // Embedded images do not depend on any external source path.
        QCOMPARE(reloaded.presets().last().brush.texture.pattern, b.texture.pattern);
        QCOMPARE(reloaded.presets().last().brush.dual.tipMask, b.dual.tipMask);
    }

    void legacyVersionOneMigratesWithoutChangingFlowOrWritingOnLoad() {
        QTemporaryDir temporary;
        const auto path = temporary.filePath("legacy.json");
        auto expected = sampledPreset();
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(expected.brush.tipMask.save(&buffer, "PNG"));
        QJsonObject entry{{"id", expected.id}, {"name", expected.name},
            {"size", expected.brush.size}, {"opacity", expected.brush.opacity},
            {"spacing", expected.brush.spacing}, {"pressureSize", expected.brush.pressureSize},
            {"smoothing", expected.smoothing}, {"source", expected.source},
            {"compatibility", QJsonArray::fromStringList(expected.compatibility)},
            {"tipMask", QString::fromLatin1(png.toBase64())}};
        const auto legacy = QJsonDocument(QJsonObject{{"format", "toonpro.brush-library"},
            {"version", 1}, {"presets", QJsonArray{entry}}}).toJson();
        QVERIFY(writeFile(path, legacy));
        BrushLibrary library(path);
        QString error;
        QVERIFY2(library.load(&error), qPrintable(error));
        expected.brush.color = BrushSettings{}.color;
        QVERIFY(library.presets().last().brush == expected.brush);
        QCOMPARE(library.presets().last().brush.opacity, qreal(.42));
        QCOMPARE(library.presets().last().brush.strokeOpacity, qreal(1));
        QCOMPARE(readFile(path), legacy);
        QVERIFY(library.renamePreset(expected.id, "Migrated", &error));
        QCOMPARE(QJsonDocument::fromJson(readFile(path)).object()["version"].toInt(), 2);
        BrushLibrary reloaded(path);
        QVERIFY(reloaded.load(&error));
        QVERIFY(reloaded.presets().last().brush == expected.brush);
    }

    void allEnumChoicesPersist() {
        QString error;
        auto roundTrip = [&](const BrushSettings &expected) {
            BrushSettings result;
            return brushSettingsFromJson(brushSettingsToJson(expected), result, &error) && result == expected;
        };
        for (int i = 0; i <= int(BrushBlend::Dissolve); ++i) {
            BrushSettings brush;
            brush.blendMode = BrushBlend(i);
            QVERIFY2(roundTrip(brush), qPrintable(error));
        }
        for (int i = 0; i <= int(MaskBlend::Difference); ++i) {
            BrushSettings brush;
            brush.texture.mode = brush.dual.mode = MaskBlend(i);
            QVERIFY2(roundTrip(brush), qPrintable(error));
        }
        for (int i = 0; i <= int(BrushControl::Rotation); ++i) {
            BrushSettings brush;
            brush.sizeDynamics.control = brush.angleDynamics.control = brush.roundnessDynamics.control =
                brush.scatterControl = brush.countControl = brush.texture.dynamics.control =
                brush.colorDynamics.control = brush.opacityDynamics.control = brush.flowDynamics.control = BrushControl(i);
            QVERIFY2(roundTrip(brush), qPrintable(error));
        }
        for (int i = 0; i <= int(SymmetryMode::Radial); ++i) {
            BrushSettings brush;
            brush.symmetry.mode = SymmetryMode(i);
            QVERIFY2(roundTrip(brush), qPrintable(error));
        }
    }

    void strictSettingsSchemaAndFiniteRangesAreAtomic() {
        const auto valid = brushSettingsToJson(BrushSettings{});
        const QVector<std::function<void(QJsonObject &)>> corruptions{
            [](auto &o) { o.remove("airbrushRate"); },
            [](auto &o) { o["newFeatureFromFutureVersion"] = true; },
            [](auto &o) { o["pressureSize"] = 1; },
            [](auto &o) { o["count"] = 2.5; },
            [](auto &o) { o["blendMode"] = -1; },
            [](auto &o) { o["blendMode"] = 999; },
            [](auto &o) { o["flow"] = -.1; },
            [](auto &o) { o["roundness"] = 0; },
            [](auto &o) { o["airbrushRate"] = 61; },
            [](auto &o) { auto c = o["texture"].toObject(); c["scale"] = 0; o["texture"] = c; },
            [](auto &o) { auto c = o["dual"].toObject(); c["mode"] = 999; o["dual"] = c; },
            [](auto &o) { auto c = o["pose"].toObject(); c["pressure"] = 1.1; o["pose"] = c; },
            [](auto &o) { auto c = o["symmetry"].toObject(); c["centerX"] = 2; o["symmetry"] = c; },
            [](auto &o) { auto c = o["sizeDynamics"].toObject(); c["fade"] = 0; o["sizeDynamics"] = c; },
            [](auto &o) { auto c = o["flowDynamics"].toObject(); c["control"] = "pressure"; o["flowDynamics"] = c; }
        };
        auto existing = sampledPreset().brush;
        existing.backgroundColor = Qt::cyan;
        const auto previous = existing;
        QString error;
        for (const auto &corrupt : corruptions) {
            auto object = valid;
            corrupt(object);
            QVERIFY(!brushSettingsFromJson(object, existing, &error));
            QVERIFY(!error.isEmpty());
            QVERIFY(existing == previous);
        }
        QVERIFY(brushSettingsFromJson(valid, existing, &error));
        QVERIFY(error.isEmpty());
        QCOMPARE(existing.color, previous.color);
        QCOMPARE(existing.backgroundColor, previous.backgroundColor);
        QCOMPARE(existing.tipMask, previous.tipMask);
        const QVector<std::function<void(BrushSettings &)>> badNumbers{
            [](auto &b) { b.opacity = std::numeric_limits<qreal>::quiet_NaN(); },
            [](auto &b) { b.texture.depth = std::numeric_limits<qreal>::infinity(); },
            [](auto &b) { b.symmetry.angle = -std::numeric_limits<qreal>::infinity(); },
            [](auto &b) { b.sizeDynamics.jitter = 1.01; },
            [](auto &b) { b.roundnessDynamics.minimum = -.01; },
            [](auto &b) { b.colorDynamics.purity = -1.01; },
            [](auto &b) { b.dual.count = 17; },
            [](auto &b) { b.countControl = BrushControl(100); },
            [](auto &b) { b.pose.tiltY = 91; }
        };
        for (const auto &corrupt : badNumbers) {
            BrushSettings brush;
            corrupt(brush);
            QVERIFY(!validateBrushSettings(brush, &error));
        }
        BrushSettings boundaries;
        boundaries.opacity = boundaries.strokeOpacity = 0;
        boundaries.spacing = boundaries.dual.spacing = 4;
        boundaries.texture.scale = .1;
        boundaries.symmetry.count = 12;
        QVERIFY2(validateBrushSettings(boundaries, &error), qPrintable(error));
        QVERIFY(brushSettingsFromJson(brushSettingsToJson(boundaries), existing, &error));
    }

    void resourcesShareBudgetAndFailuresPreserveLibrary() {
        QTemporaryDir temporary;
        const auto path = temporary.filePath("library.json");
        BrushLibrary library(path);
        QVERIFY(library.load());
        auto preset = sampledPreset();
        QImage large(BrushLibrary::maxTipSide, BrushLibrary::maxTipSide, QImage::Format_Grayscale8);
        large.fill(127);
        preset.brush.tipMask = preset.brush.texture.pattern = preset.brush.dual.tipMask = large;
        const qint64 pixelsPerPreset = qint64(large.width()) * large.height() * 3;
        const int withinBudgetCount = int(BrushLibrary::maxTipPixels / pixelsPerPreset);
        QVector<BrushPreset> withinBudget;
        for (int i = 0; i < withinBudgetCount; ++i) {
            auto copy = preset;
            copy.id = QString("shared-resource-%1").arg(i);
            withinBudget.push_back(copy);
        }
        QString error;
        QVERIFY2(library.addPresets(withinBudget, &error), qPrintable(error));
        const auto previous = readFile(path);
        const auto valid = QJsonDocument::fromJson(previous).object();
        BrushLibrary reloaded(path);
        QVERIFY2(reloaded.load(&error), qPrintable(error));
        QCOMPARE(reloaded.presets().size(), builtinCount() + withinBudgetCount);
        QSignalSpy changed(&library, &BrushLibrary::changed);
        auto second = preset;
        second.id = "over-budget";
        QVERIFY(!library.addPresets({second}, &error));
        QCOMPARE(readFile(path), previous);
        QCOMPARE(changed.count(), 0);
        for (const auto *key : {"texturePattern", "dualTipMask"}) {
            auto invalid = valid;
            auto entries = invalid["presets"].toArray();
            auto entry = entries.first().toObject();
            entry[QLatin1String(key)] = "not-an-image";
            entries[0] = entry;
            invalid["presets"] = entries;
            const auto corrupt = QJsonDocument(invalid).toJson();
            QVERIFY(writeFile(path, corrupt));
            QVERIFY(!library.load(&error));
            QVERIFY(!library.writable());
            QVERIFY(!library.renamePreset(withinBudget.first().id, "Do not overwrite", &error));
            QCOMPARE(readFile(path), corrupt);
            QCOMPARE(library.presets().size(), builtinCount() + withinBudgetCount);
        }
        auto overBudget = valid;
        auto entries = overBudget["presets"].toArray();
        auto additional = entries.first().toObject();
        additional["id"] = "duplicate-resources";
        entries.push_back(additional);
        overBudget["presets"] = entries;
        QVERIFY(writeFile(path, QJsonDocument(overBudget).toJson()));
        QVERIFY(!library.load(&error));
        QVERIFY(error.contains("pixel"));
        QCOMPARE(changed.count(), 0);
        QVERIFY(writeFile(path, previous));
        QVERIFY(library.load());
        auto blackTexture = sampledPreset();
        blackTexture.id = "black-texture";
        blackTexture.brush.texture.pattern = QImage(2, 2, QImage::Format_Grayscale8);
        blackTexture.brush.texture.pattern.fill(0);
        QVERIFY(library.addPresets({blackTexture}));
    }

    void rejectsUnknownVersionMalformedMaskAndSchema() {
        QTemporaryDir temporary;
        const auto path = temporary.filePath("library.json");
        BrushLibrary library(path);
        QVERIFY(library.load());
        QVERIFY(library.addPresets({sampledPreset()}));
        const auto valid = QJsonDocument::fromJson(readFile(path)).object();
        auto invalid = valid;
        invalid["version"] = 99;
        QVERIFY(writeFile(path, QJsonDocument(invalid).toJson()));
        QString error;
        QVERIFY(!library.load(&error));
        invalid = valid;
        auto entries = invalid["presets"].toArray();
        auto entry = entries.first().toObject();
        entry["tipMask"] = "@@bad-base64@@";
        entries[0] = entry;
        invalid["presets"] = entries;
        QVERIFY(writeFile(path, QJsonDocument(invalid).toJson()));
        QVERIFY(!library.load(&error));
        invalid = valid;
        entries = invalid["presets"].toArray();
        entry = entries.first().toObject();
        auto settings = entry["settings"].toObject();
        settings["pressureSize"] = "yes";
        entry["settings"] = settings;
        entries[0] = entry;
        invalid["presets"] = entries;
        QVERIFY(writeFile(path, QJsonDocument(invalid).toJson()));
        QVERIFY(!library.load(&error));
        invalid = valid;
        entries = invalid["presets"].toArray();
        entries.push_back(entries.first());
        invalid["presets"] = entries;
        QVERIFY(writeFile(path, QJsonDocument(invalid).toJson()));
        QVERIFY(!library.load(&error));
        QCOMPARE(library.presets().size(), builtinCount() + 1);
    }
};

QTEST_GUILESS_MAIN(BrushLibraryTests)
#include "test_brush_library.moc"
