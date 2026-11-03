#include "project.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
using namespace toon;
namespace {
Project fixture(bool pixels = false) {
    auto project = Project::create(); project.width = project.height = 128;
    auto &shot = project.shots[0]; auto &layer = shot.layers[0];
    layer.effects.blurRadius = 17.5; layer.effects.shadowColor = QColor("#123456");
    layer.mask = LayerMask{}; layer.mask->defaultValue = 0;
    layer.mask->position = {-12.5, 8}; layer.mask->pivot = {64, 32};
    layer.mask->scale = {-1.25, .75}; layer.mask->rotation = 35;
    layer.mask->opacity = .7; layer.mask->feather = 6.5; layer.mask->inverted = true;
    if (pixels) {
        QImage mask(tileSize, tileSize, QImage::Format_Grayscale8); mask.fill(97);
        layer.mask->tiles.insert({-1, 0}, mask);
        QImage image(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); image.fill(QColor(40, 70, 150));
        layer.tiles.insert({0, 0}, image);
    }
    LayerAnimation animation{layer.id, {32, 64}, {}, {}};
    for (int i = 0; i < keyframePropertyCount; ++i) {
        const auto property = KeyframeProperty(i); const auto base = keyframeSourceValue(layer, property);
        const auto mode = keyframePropertyIsDiscrete(property) ? KeyframeInterpolation::Hold : KeyframeInterpolation::EaseInOut;
        animation.channels.append({property, {{0, base, mode}, {10, base, mode}}});
    }
    shot.layerAnimations = {animation}; return project;
}
QJsonObject firstLayer(const QJsonObject &json) {
    return json["shots"].toArray()[0].toObject()["layers"].toArray()[0].toObject();
}
QJsonObject replaceFirstLayer(QJsonObject json, const QJsonObject &layer) {
    auto shots = json["shots"].toArray(); auto shot = shots[0].toObject();
    auto layers = shot["layers"].toArray(); layers[0] = layer; shot["layers"] = layers;
    shots[0] = shot; json["shots"] = shots; return json;
}
QJsonObject replaceFirstAnimation(QJsonObject json, const QJsonObject &animation) {
    auto shots = json["shots"].toArray(); auto shot = shots[0].toObject();
    auto animations = shot["layerAnimations"].toArray(); animations[0] = animation;
    shot["layerAnimations"] = animations; shots[0] = shot; json["shots"] = shots; return json;
}
QJsonObject firstAnimation(const QJsonObject &json) {
    return json["shots"].toArray()[0].toObject()["layerAnimations"].toArray()[0].toObject();
}
QByteArray container(const QJsonObject &json, int header = 36) {
    QByteArray bytes = QByteArray("TOONPRO") + QByteArray::number(header) + '\n';
    QDataStream stream(&bytes, QIODevice::Append); stream.setByteOrder(QDataStream::BigEndian);
    const auto manifest = QJsonDocument(json).toJson(QJsonDocument::Compact);
    stream << quint32(manifest.size()); const auto hash = QCryptographicHash::hash(manifest, QCryptographicHash::Sha256);
    stream.writeRawData(hash.constData(), hash.size()); stream.writeRawData(manifest.constData(), manifest.size()); return bytes;
}
bool write(const QString &path, const QByteArray &bytes) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
}
class AnimatedCompositingStorageTests : public QObject {
    Q_OBJECT
private slots:
    void allChannelsAndStaticMaskSettingsRoundTripInVersion36() {
        auto project = fixture(); QString error;
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        const auto json = project.toJson(); QCOMPARE(json["version"].toInt(), 36);
        auto parsed = Project::fromJson(json, &error); QVERIFY2(parsed, qPrintable(error));
        QCOMPARE(parsed->toJson(), json); QCOMPARE(parsed->shots[0].layerAnimations, project.shots[0].layerAnimations);
        QCOMPARE(parsed->shots[0].layers[0].mask, project.shots[0].layers[0].mask);
        project = fixture(true); QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("animated-effects.toon");
        QVERIFY2(saveProject(project, path, &error), qPrintable(error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.read(10), QByteArray("TOONPRO36\n"));
        auto loaded = loadProject(path, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->toJson(), project.toJson());
        QCOMPARE(loaded->shots[0].layers[0].mask, project.shots[0].layers[0].mask);
        QCOMPARE(loaded->shots[0].layers[0].tiles, project.shots[0].layers[0].tiles);
    }
    void oldMasksRemainLegacyAndStaticNewMaskFieldsPromoteFormat() {
        auto project = Project::create(); auto &layer = project.shots[0].layers[0]; layer.mask = LayerMask{};
        auto json = project.toJson(); QCOMPARE(json["version"].toInt(), 19);
        QCOMPARE(firstLayer(json)["mask"].toObject().keys(), QStringList({"defaultValue", "enabled", "tiles"}));
        QString error; QVERIFY2(Project::fromJson(json, &error), qPrintable(error));
        layer.mask->pivot = {32, 32}; json = project.toJson(); QCOMPARE(json["version"].toInt(), 36);
        QVERIFY(firstLayer(json)["mask"].toObject().contains("properties"));
        auto parsed = Project::fromJson(json, &error); QVERIFY2(parsed, qPrintable(error));
        QCOMPARE(parsed->shots[0].layers[0].mask->pivot, QPointF(32, 32));
        json["version"] = 35; QVERIFY(!Project::fromJson(json, &error)); QVERIFY(error.contains("36"));
    }
    void symbolOnlyAnimationPromotesVersionAndPersistsDefinitionAndInstance() {
        auto project = Project::create(); auto definitionProject = fixture(true);
        SymbolDefinition definition{QUuid::createUuid().toString(QUuid::WithoutBraces), "Glow pose", definitionProject.shots[0]};
        project.symbols.append(definition);
        auto &instance = project.shots[0].layers[0]; instance.kind = LayerKind::Symbol;
        instance.symbol = SymbolInstance{definition.id, SymbolPlayback::Loop, 0, 0, 1};
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        QCOMPARE(project.toJson()["version"].toInt(), 36);
        QTemporaryDir directory; QString error; const auto path = directory.filePath("symbol-effects.toon");
        QVERIFY2(saveProject(project, path, &error), qPrintable(error));
        auto loaded = loadProject(path, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->toJson(), project.toJson());
        QCOMPARE(loaded->symbols[0].content.layerAnimations, definition.content.layerAnimations);
        QCOMPARE(loaded->symbols[0].content.layers[0].mask, definition.content.layers[0].mask);
        auto downgrade = project.toJson(); downgrade["version"] = 35;
        QVERIFY(!Project::fromJson(downgrade, &error)); QVERIFY(error.contains("36"));
    }
    void malformedChannelsAndMaskMetadataAreRejectedBeforeLoading() {
        const auto json = fixture().toJson(); QString error;
        auto mutated = json; mutated["version"] = latestProjectFormatVersion + 1;
        QVERIFY(!Project::fromJson(mutated, &error));
        auto layer = firstLayer(json); layer.remove("mask");
        QVERIFY(!Project::fromJson(replaceFirstLayer(json, layer), &error));
        for (const auto field : {QString("feather"), QString("opacity"), QString("rotation")}) {
            layer = firstLayer(json); auto mask = layer["mask"].toObject(); auto properties = mask["properties"].toObject();
            properties[field] = 1000000; mask["properties"] = properties; layer["mask"] = mask;
            QVERIFY(!Project::fromJson(replaceFirstLayer(json, layer), &error));
        }
        auto animation = firstAnimation(json); auto channels = animation["channels"].toArray();
        auto channel = channels[6].toObject(); auto keys = channel["keys"].toArray(); auto key = keys[0].toObject();
        key["interpolation"] = "linear"; keys[0] = key; channel["keys"] = keys; channels[6] = channel; animation["channels"] = channels;
        QVERIFY(!Project::fromJson(replaceFirstAnimation(json, animation), &error));
        animation = firstAnimation(json); channels = animation["channels"].toArray(); channel = channels[6].toObject();
        channel["property"] = "unsupportedEffect"; channels[6] = channel; animation["channels"] = channels;
        QVERIFY(!Project::fromJson(replaceFirstAnimation(json, animation), &error));
    }
    void downgradedMetadataAndMismatchedContainerHeadersAreRejected() {
        const auto json = fixture().toJson(); QTemporaryDir directory; QString error;
        const auto path = directory.filePath("malformed.toon");
        QVERIFY(write(path, container(json, 35))); QVERIFY(!loadProject(path, &error));
        auto downgraded = json; downgraded["version"] = 35;
        QVERIFY(write(path, container(downgraded, 35))); QVERIFY(!loadProject(path, &error));
        QVERIFY(error.contains("36"));
        // The metadata guard also rejects an explicitly present, all-default properties object.
        auto staticProject = Project::create(); staticProject.shots[0].layers[0].mask = LayerMask{};
        auto legacy = staticProject.toJson(); auto layer = firstLayer(legacy); auto mask = layer["mask"].toObject();
        const auto extendedMask = firstLayer(json)["mask"].toObject()["properties"].toObject();
        auto defaults = extendedMask; defaults["position"] = QJsonArray{0, 0}; defaults["pivot"] = QJsonArray{0, 0};
        defaults["scale"] = QJsonArray{1, 1}; defaults["rotation"] = 0; defaults["opacity"] = 1;
        defaults["feather"] = 0; defaults["inverted"] = false;
        mask["properties"] = defaults; layer["mask"] = mask; legacy = replaceFirstLayer(legacy, layer);
        QVERIFY(!Project::fromJson(legacy, &error)); QVERIFY(error.contains("36"));
    }
};
QTEST_MAIN(AnimatedCompositingStorageTests)
#include "animated_compositing_storage_tests.moc"
