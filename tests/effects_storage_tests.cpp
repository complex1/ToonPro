#include "animation_test_fixture.h"
#include "layer_effects.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>
using namespace toon;
namespace {
QImage maskTile(int seed = 0) {
    QImage image(tileSize, tileSize, QImage::Format_Grayscale8);
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
        image.scanLine(y)[x] = uchar((x * 3 + y * 5 + seed) % 256);
    return image;
}
LayerEffects allEffects() {
    LayerEffects effects;
    effects.blurEnabled = effects.shadowEnabled = effects.glowEnabled = effects.colorEnabled = true;
    effects.blurRadius = 2.25; effects.shadowRadius = 10.75; effects.shadowOffset = {-32.5, 17.25};
    effects.shadowColor = QColor("#123456"); effects.shadowOpacity = .27;
    effects.glowRadius = 14.5; effects.glowColor = QColor("#aabbcc"); effects.glowOpacity = .76;
    effects.exposure = 1.25; effects.contrast = -22; effects.saturation = 45; effects.hue = -135;
    return effects;
}
Project fixture() {
    auto project = animation_fixture::project(); auto &shot = project.shots[0];
    shot.layers[0].effects = allEffects(); shot.layers[0].mask = LayerMask{};
    shot.layers[0].mask->defaultValue = 0;
    shot.layers[0].mask->tiles = {{{-1, 0}, maskTile(3)}, {{0, 0}, maskTile(19)}};
    shot.layers[1].effects.blurEnabled = true; shot.layers[1].effects.blurRadius = 3.5;
    shot.layers[1].mask = LayerMask{}; shot.layers[1].mask->enabled = false;
    shot.layers[1].mask->tiles.insert({0, -1}, maskTile(41));
    auto adjustment = RasterLayer::create("Grade"); adjustment.adjustment = true;
    adjustment.effects.colorEnabled = true; adjustment.effects.exposure = -.5;
    adjustment.mask = LayerMask{}; shot.layers.append(adjustment);
    shot.camera2DKeys = {{0, {}}, {10, {{4, 2}, 1.5, 20}, KeyframeInterpolation::EaseInOut}};
    shot.layerAnimations = {{shot.layers[0].id, {}, {{KeyframeProperty::Opacity, {{0, 1}, {10, .4}}}}, {}}};
    const auto asset = makeAudioAsset("Line", QByteArray(400, '\x14'));
    shot.audioClips = {{animation_fixture::id(), "Dialogue", asset, 1, 10, 80, .8, false}};
    auto second = Project::create().shots[0]; second.layers[0].mask = LayerMask{};
    second.layers[0].mask->tiles.insert({1, 0}, maskTile(97)); project.shots.append(second);
    return project;
}
QByteArray bytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
bool write(const QString &path, const QByteArray &data) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size(); }
QByteArray png(const QImage &image) { QByteArray result; QBuffer buffer(&result); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG"); return result; }
QByteArray container(const QJsonObject &manifest, const QList<QByteArray> &payloads = {}, int version = -1) {
    if (version < 0) version = manifest["version"].toInt();
    QByteArray data = QByteArray("TOONPRO") + QByteArray::number(version) + '\n';
    QDataStream stream(&data, QIODevice::Append); stream.setByteOrder(QDataStream::BigEndian);
    const auto append = [&](const QByteArray &blob) {
        stream << quint32(blob.size()); const auto hash = QCryptographicHash::hash(blob, QCryptographicHash::Sha256);
        stream.writeRawData(hash.constData(), hash.size()); stream.writeRawData(blob.constData(), blob.size());
    };
    append(QJsonDocument(manifest).toJson(QJsonDocument::Compact)); for (const auto &payload : payloads) append(payload);
    return data;
}
QJsonObject layerMetadata(const QJsonObject &json, int layer = 0) {
    return json["shots"].toArray()[0].toObject()["layers"].toArray()[layer].toObject();
}
QJsonObject withLayer(QJsonObject json, const QJsonObject &layer, int index = 0) {
    auto shots = json["shots"].toArray(); auto shot = shots[0].toObject(); auto layers = shot["layers"].toArray();
    layers[index] = layer; shot["layers"] = layers; shots[0] = shot; json["shots"] = shots; return json;
}
Project oneMask() {
    auto project = Project::create(); project.shots[0].layers[0].mask = LayerMask{};
    project.shots[0].layers[0].mask->tiles.insert({0, 0}, maskTile()); return project;
}
}
class EffectsStorageTests final : public QObject {
    Q_OBJECT
private slots:
    void effectsGroupsMasksAdjustmentsAndExistingMediaRoundTrip() {
        const auto project = fixture(); QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("effects.toon"); QString error;
        QVERIFY2(saveProject(project, path, &error), qPrintable(error)); QVERIFY(bytes(path).startsWith("TOONPRO22\n"));
        const auto loaded = loadProject(path, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->toJson(), project.toJson());
        for (int si = 0; si < project.shots.size(); ++si) for (int i = 0; i < project.shots[si].layers.size(); ++i) {
            const auto &before = project.shots[si].layers[i], &after = loaded->shots[si].layers[i];
            QCOMPARE(after.effects, before.effects); QCOMPARE(after.mask, before.mask); QCOMPARE(after.adjustment, before.adjustment);
            QCOMPARE(after.tiles, before.tiles);
        }
        QCOMPARE(loaded->shots[0].camera2DKeys, project.shots[0].camera2DKeys);
        QCOMPARE(loaded->shots[0].layerAnimations, project.shots[0].layerAnimations);
        QCOMPARE(loaded->shots[0].animationTracks, project.shots[0].animationTracks);
        QCOMPARE(loaded->shots[0].audioClips[0].asset->pcm, project.shots[0].audioClips[0].asset->pcm);
        const auto mask = layerMetadata(project.toJson())["mask"].toObject();
        QCOMPARE(mask.keys(), QStringList({"defaultValue", "enabled", "tiles"}));
        QCOMPARE(mask["tiles"].toArray(), QJsonArray({QJsonArray{-1, 0}, QJsonArray{0, 0}}));
        auto metadata = Project::fromJson(oneMask().toJson(), &error); QVERIFY(metadata);
        QVERIFY(metadata->validate().contains("mask resources"));
        QVERIFY(metadata->shots[0].layers[0].mask->tiles.value({0, 0}).isNull());
        QVERIFY(!saveProject(*metadata, directory.filePath("metadata-only.toon"), &error));
    }
    void disabledSettingsRemainStoredAndDefaultFieldsStayAbsent() {
        auto project = Project::create(); const auto defaults = layerMetadata(project.toJson());
        QVERIFY(!defaults.contains("effects")); QVERIFY(!defaults.contains("mask")); QVERIFY(!defaults.contains("adjustment"));
        auto &layer = project.shots[0].layers[0]; layer.effects = allEffects(); layer.effects.enabled = false;
        QVERIFY(!hasLayerEffects(layer.effects)); QString error;
        const auto parsed = Project::fromJson(project.toJson(), &error); QVERIFY2(parsed, qPrintable(error));
        QCOMPARE(parsed->shots[0].layers[0].effects, layer.effects);
        layer.effects.enabled = true; QVERIFY(hasLayerEffects(layer.effects));
        layer.effects = {}; QVERIFY(!hasLayerEffects(layer.effects));
        layer.mask = LayerMask{}; layer.mask->defaultValue = 255;
        const auto emptyMask = Project::fromJson(project.toJson(), &error); QVERIFY(emptyMask); QVERIFY(emptyMask->validate().isEmpty());
        QVERIFY(emptyMask->shots[0].layers[0].mask); QCOMPARE(emptyMask->shots[0].layers[0].mask->defaultValue, 255);
    }
    void earlierVersionsLoadAndCannotDisguiseNewMetadata() {
        QTemporaryDir directory; const auto path = directory.filePath("legacy.toon"); QString error;
        for (int version = 1; version <= 16; ++version) {
            auto manifest = Project::create().toJson(); manifest["version"] = version;
            const auto raw = version == 1 ? QJsonDocument(manifest).toJson(QJsonDocument::Compact) : container(manifest, {}, version);
            QVERIFY(write(path, raw)); const auto loaded = loadProject(path, &error);
            QVERIFY2(loaded, qPrintable(QString("Version %1: %2").arg(version).arg(error)));
            QCOMPARE(loaded->shots[0].layers[0].effects, LayerEffects{}); QVERIFY(!loaded->shots[0].layers[0].mask);
            QVERIFY(!loaded->shots[0].layers[0].adjustment); QCOMPARE(loaded->toJson()["version"].toInt(), 19);
            for (const auto &field : QStringList{"effects", "mask", "adjustment"}) {
                auto layer = layerMetadata(manifest);
                layer[field] = field == "effects" ? QJsonValue(layerEffectsToJson({}))
                    : field == "mask" ? QJsonValue(layerMaskToJson({})) : QJsonValue(false);
                QVERIFY(!Project::fromJson(withLayer(manifest, layer), &error)); QVERIFY(error.contains("format 17"));
            }
        }
        auto future = Project::create().toJson(); future["version"] = latestProjectFormatVersion + 1;
        QVERIFY(write(path, container(future, {}, latestProjectFormatVersion + 1))); QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("container version"));
    }
    void effectParametersValidateEvenWhenDisabledAndParsersAreAtomic() {
        const auto valid = allEffects(); LayerEffects result = valid; QString error;
        QVERIFY(layerEffectsFromJson(layerEffectsToJson(valid), &result, &error)); QCOMPARE(result, valid);
        for (int failure = 0; failure < 13; ++failure) {
            auto invalid = valid; invalid.enabled = false;
            if (failure == 0) invalid.blurRadius = -1;
            if (failure == 1) invalid.blurRadius = 128.01;
            if (failure == 2) invalid.shadowRadius = std::numeric_limits<qreal>::infinity();
            if (failure == 3) invalid.glowRadius = std::numeric_limits<qreal>::quiet_NaN();
            if (failure == 4) invalid.shadowOffset.setX(512.1);
            if (failure == 5) invalid.shadowOffset.setY(-512.1);
            if (failure == 6) invalid.shadowColor.setAlpha(254);
            if (failure == 7) invalid.glowColor = QColor();
            if (failure == 8) invalid.shadowOpacity = -.01;
            if (failure == 9) invalid.glowOpacity = 1.01;
            if (failure == 10) invalid.exposure = -4.1;
            if (failure == 11) invalid.contrast = 100.1;
            if (failure == 12) invalid.hue = 180.1;
            QVERIFY2(!validateLayerEffects(invalid).isEmpty(), qPrintable(QString::number(failure)));
        }
        for (int failure = 0; failure < 8; ++failure) {
            auto json = layerEffectsToJson(valid);
            if (failure == 0) json["extra"] = true;
            if (failure == 1) json.remove("glowColor");
            if (failure == 2) json["enabled"] = 1;
            if (failure == 3) json["saturation"] = "50";
            if (failure == 4) json["shadowOffset"] = QJsonArray{2};
            if (failure == 5) json["shadowOffset"] = QJsonArray{false, 2};
            if (failure == 6) json["glowColor"] = "#80ffffff";
            if (failure == 7) json["saturation"] = -101;
            QVERIFY(!layerEffectsFromJson(json, &result, &error)); QCOMPARE(result, valid);
        }
    }
    void maskMetadataRejectsMalformedCoordinatesOrderAndBudgets() {
        const auto original = oneMask(); QString error; LayerMask target = *original.shots[0].layers[0].mask;
        const auto before = target; const auto valid = layerMaskToJson(target);
        for (int failure = 0; failure < 11; ++failure) {
            auto json = valid;
            if (failure == 0) json["defaultValue"] = 256;
            if (failure == 1) json["defaultValue"] = .5;
            if (failure == 2) json["enabled"] = "yes";
            if (failure == 3) json["pixels"] = "base64";
            if (failure == 4) json["tiles"] = QJsonArray{QJsonArray{0, 0}, QJsonArray{0, 0}};
            if (failure == 5) json["tiles"] = QJsonArray{QJsonArray{1, 0}, QJsonArray{0, 0}};
            if (failure == 6) json["tiles"] = QJsonArray{QJsonArray{0, 129}};
            if (failure == 7) json["tiles"] = QJsonArray{QJsonArray{0, -.5}};
            if (failure == 8) json["tiles"] = QJsonArray{QJsonArray{0, false}};
            if (failure == 9) json["tiles"] = QJsonArray{QJsonArray{0}};
            if (failure == 10) json["tiles"] = QJsonArray{"bad"};
            QVERIFY(!layerMaskFromJson(json, &target, &error)); QCOMPARE(target, before);
        }
        auto project = Project::create(); auto &shot = project.shots[0]; shot.layers.append(RasterLayer::create("Second"));
        auto manifest = project.toJson(); QJsonArray coordinates;
        for (int i = 0; i < 1024; ++i) coordinates.append(QJsonArray{i / 32, i % 32});
        auto first = layerMetadata(manifest); first["mask"] = QJsonObject{{"enabled", true}, {"defaultValue", 255}, {"tiles", coordinates}};
        manifest = withLayer(manifest, first);
        QVERIFY(Project::fromJson(manifest, &error));
        auto second = layerMetadata(manifest, 1); second["mask"] = valid; manifest = withLayer(manifest, second, 1);
        QVERIFY(!Project::fromJson(manifest, &error)); QVERIFY(error.contains("1024"));
        coordinates.append(QJsonArray{32, 0}); auto oversized = valid; oversized["tiles"] = coordinates;
        QVERIFY(!layerMaskFromJson(oversized, &target, &error)); QCOMPARE(target, before);
    }
    void maskPayloadDimensionsFormatChecksumAndTrailingDataAreChecked() {
        const auto project = oneMask(); const auto image = project.shots[0].layers[0].mask->tiles.first();
        const auto manifest = project.toJson(); const auto goodPng = png(image);
        QTemporaryDir directory; const auto path = directory.filePath("mask.toon"); QString error;
        const auto good = container(manifest, {goodPng}); QVERIFY(write(path, good));
        const auto loaded = loadProject(path, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->shots[0].layers[0].mask->tiles.first(), image);
        auto corrupt = good; corrupt[corrupt.size() - 1] ^= 1;
        QVERIFY(write(path, corrupt)); QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("checksum"));
        QVERIFY(write(path, container(manifest))); QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("mask"));
        QVERIFY(write(path, good.left(good.size() - 5))); QVERIFY(!loadProject(path, &error));
        QVERIFY(write(path, good + "extra")); QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("trailing"));
        const QList<QImage> invalidImages{image.copy(0, 0, 128, 128), image.convertToFormat(QImage::Format_RGB32),
            image.convertToFormat(QImage::Format_ARGB32), image.convertToFormat(QImage::Format_Grayscale16)};
        for (const auto &invalid : invalidImages) {
            QVERIFY(write(path, container(manifest, {png(invalid)}))); QVERIFY2(!loadProject(path, &error), qPrintable(QString::number(invalid.format())));
        }
        QVERIFY(write(path, container(manifest, {QByteArray(1024 * 1024 + 1, 'x')}))); QVERIFY(!loadProject(path, &error));
        QVERIFY(write(path, container(manifest, {goodPng, goodPng}))); QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("trailing"));
    }
    void unsupportedTargetsAndAdjustmentContentAreRejected() {
        const auto original = oneMask();
        auto project = original; project.shots[0].layers[0].annotation = true; QVERIFY(project.validate().contains("Notes"));
        project = original; auto notes = RasterLayer::create("Notes group"); notes.group = notes.annotation = true;
        project.shots[0].layers[0].parentId = notes.id; project.shots[0].layers.append(notes); QVERIFY(project.validate().contains("Notes"));
        project = original; auto &scene = project.shots[0].layers[0]; scene.kind = LayerKind::Scene3D; scene.scene3d = Scene3D{};
        QVERIFY(project.validate().contains("3D reference"));
        project = Project::create(); auto adjustment = RasterLayer::create("Grade"); adjustment.adjustment = true;
        adjustment.effects.colorEnabled = true; adjustment.effects.exposure = .5; project.shots[0].layers.append(adjustment);
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        for (int failure = 0; failure < 8; ++failure) {
            auto invalid = project; auto &layer = invalid.shots[0].layers[1];
            if (failure == 0) layer.effects.blurEnabled = true;
            if (failure == 1) layer.effects.shadowEnabled = true;
            if (failure == 2) layer.effects.glowEnabled = true;
            if (failure == 3) layer.group = true;
            if (failure == 4) layer.kind = LayerKind::Vector;
            if (failure == 5) layer.tiles.insert({0, 0}, maskTile().convertToFormat(QImage::Format_ARGB32_Premultiplied));
            if (failure == 6) layer.alphaLocked = true;
            if (failure == 7) layer.clipped = true;
            QVERIFY(!invalid.validate().isEmpty()); QString error;
            QVERIFY(!Project::fromJson(invalid.toJson(), &error));
        }
        auto manifest = project.toJson(); auto layer = layerMetadata(manifest, 1); layer["adjustment"] = "true"; QString error;
        QVERIFY(!Project::fromJson(withLayer(manifest, layer, 1), &error));
    }
    void invalidEffectsAndMasksCannotOverwriteExistingProject() {
        const auto project = fixture(); QTemporaryDir directory; const auto path = directory.filePath("protected.toon"); QString error;
        QVERIFY2(saveProject(project, path, &error), qPrintable(error)); const auto before = bytes(path);
        for (int failure = 0; failure < 5; ++failure) {
            auto invalid = project; auto &layer = invalid.shots[0].layers[0];
            if (failure == 0) layer.effects.exposure = std::numeric_limits<qreal>::quiet_NaN();
            if (failure == 1) layer.mask->tiles.first() = QImage{};
            if (failure == 2) layer.mask->tiles.first() = maskTile().convertToFormat(QImage::Format_ARGB32_Premultiplied);
            if (failure == 3) layer.mask->tiles.insert({129, 0}, maskTile());
            if (failure == 4) layer.mask->defaultValue = -1;
            QVERIFY(!saveProject(invalid, path, &error)); QCOMPARE(bytes(path), before);
        }
    }
};
QTEST_MAIN(EffectsStorageTests)
#include "effects_storage_tests.moc"
