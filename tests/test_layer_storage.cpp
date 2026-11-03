#include "project.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

using namespace toon;

namespace {
RasterLayer group(const QString &name = "Character") {
    auto result = RasterLayer::create(name);
    result.group = true;
    return result;
}
QImage pixels(QSize size = {tileSize, tileSize}, QColor color = QColor(64, 128, 224, 127)) {
    QImage result(size, QImage::Format_ARGB32_Premultiplied);
    result.fill(color);
    return result;
}
QJsonObject replaceLayers(QJsonObject manifest, const QJsonArray &layers) {
    auto shots = manifest["shots"].toArray();
    auto shot = shots[0].toObject();
    shot["layers"] = layers; shots[0] = shot; manifest["shots"] = shots;
    return manifest;
}
QJsonArray jsonLayers(const QJsonObject &manifest) {
    return manifest["shots"].toArray()[0].toObject()["layers"].toArray();
}
void appendBlob(QDataStream &stream, const QByteArray &data) {
    stream << quint32(data.size());
    const auto hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    stream.writeRawData(hash.constData(), hash.size());
    stream.writeRawData(data.constData(), data.size());
}
QByteArray imageBytes(const QImage &image) {
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}
Project nestedProject(int depth) {
    auto project = Project::create();
    QString parent;
    for (int i = 0; i < depth; ++i) {
        auto entry = group(QString("Group %1").arg(i + 1));
        entry.parentId = parent;
        parent = entry.id;
        project.shots[0].layers.append(entry);
    }
    project.shots[0].layers[0].parentId = parent;
    return project;
}
}

class LayerStorageTest : public QObject {
    Q_OBJECT
private slots:
    void currentVersionPreservesNestedLayersPixelsAndReferences() {
        QTemporaryDir temporary; QString error;
        auto project = Project::create();
        auto outer = group(); outer.opacity = 0.6; outer.locked = true; outer.blendMode = LayerBlendMode::Screen;
        outer.positionLocked = true; outer.rowColor = QColor("#b05090");
        auto inner = group("Colors"); inner.parentId = outer.id; inner.visible = false;
        auto &base = project.shots[0].layers[0];
        base.name = "Base color"; base.parentId = inner.id; base.alphaLocked = true;
        base.tiles[{-1, 0}] = pixels();
        auto shadows = RasterLayer::create("Shadows");
        shadows.parentId = inner.id; shadows.clipped = true; shadows.opacity = 0.75;
        shadows.blendMode = LayerBlendMode::Multiply; shadows.rowColor = QColor("#308090");
        shadows.tiles[{1, 2}] = pixels({tileSize, tileSize}, QColor(24, 48, 72, 192));
        // Parent links are independent of flat-array positions; sibling order remains unchanged.
        project.shots[0].layers << shadows << inner << outer;
        project.shots[0].references.append({QUuid::createUuid().toString(QUuid::WithoutBraces),
            "Character reference", pixels({32, 48})});
        const auto path = temporary.filePath("character.toonproj");
        QVERIFY2(saveProject(project, path, &error), qPrintable(error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.read(10), QByteArray("TOONPRO19\n")); file.close();
        const auto loaded = loadProject(path, &error);
        QVERIFY2(loaded.has_value(), qPrintable(error));
        QCOMPARE(loaded->toJson(), project.toJson());
        QCOMPARE(loaded->shots[0].layers[0].tiles, project.shots[0].layers[0].tiles);
        QCOMPARE(loaded->shots[0].layers[1].tiles, project.shots[0].layers[1].tiles);
        QCOMPARE(loaded->shots[0].references[0].image, project.shots[0].references[0].image);
        QVERIFY(loaded->shots[0].layers[2].tiles.isEmpty());
        QVERIFY(loaded->shots[0].layers[3].tiles.isEmpty());
    }
    void versionThreeMigratesArtworkAndReferencesToFlatLayers() {
        QTemporaryDir temporary; QString error;
        auto original = Project::create();
        original.shots[0].layers[0].tiles[{0, -1}] = pixels();
        original.shots[0].references.append({QUuid::createUuid().toString(QUuid::WithoutBraces),
            "Legacy reference", pixels({16, 24})});
        auto manifest = original.toJson(); manifest["version"] = 3;
        auto layers = jsonLayers(manifest);
        auto legacyLayer = layers[0].toObject();
        for (const char *key : {"parentId", "group", "clipped", "alphaLocked"}) legacyLayer.remove(key);
        layers[0] = legacyLayer; manifest = replaceLayers(manifest, layers);
        QByteArray bytes("TOONPRO3\n");
        QBuffer buffer(&bytes); QVERIFY(buffer.open(QIODevice::WriteOnly | QIODevice::Append));
        QDataStream stream(&buffer); stream.setByteOrder(QDataStream::BigEndian);
        appendBlob(stream, QJsonDocument(manifest).toJson(QJsonDocument::Compact));
        appendBlob(stream, imageBytes(original.shots[0].layers[0].tiles.first()));
        appendBlob(stream, imageBytes(original.shots[0].references[0].image));
        buffer.close();
        const auto path = temporary.filePath("legacy.toonproj");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size()); file.close();
        const auto loaded = loadProject(path, &error);
        QVERIFY2(loaded.has_value(), qPrintable(error));
        const auto &layer = loaded->shots[0].layers[0];
        QVERIFY(layer.parentId.isEmpty()); QVERIFY(!layer.group); QVERIFY(!layer.clipped); QVERIFY(!layer.alphaLocked);
        QCOMPARE(layer.tiles, original.shots[0].layers[0].tiles);
        QCOMPARE(loaded->shots[0].references[0].image, original.shots[0].references[0].image);
        QVERIFY2(saveProject(*loaded, path, &error), qPrintable(error));
        const auto reloaded = loadProject(path, &error);
        QVERIFY2(reloaded.has_value(), qPrintable(error));
        QCOMPARE(reloaded->toJson(), original.toJson());
        QCOMPARE(reloaded->shots[0].layers[0].tiles, layer.tiles);
    }
    void hierarchyRejectsInvalidParentsAndCycles_data() {
        QTest::addColumn<QString>("kind");
        for (const auto &kind : {"missing parent", "cross-shot parent", "raster parent", "self cycle", "group cycle", "no raster"})
            QTest::newRow(kind) << QString(kind);
    }
    void legacyVersionsDefaultNewMetadata_data() {
        QTest::addColumn<int>("version");
        for (int version = 2; version <= 5; ++version)
            QTest::newRow(qPrintable(QString::number(version))) << version;
    }
    void legacyVersionsDefaultNewMetadata() {
        QFETCH(int, version);
        auto manifest = Project::create().toJson(); manifest["version"] = version;
        auto layers = jsonLayers(manifest); auto layer = layers[0].toObject();
        layer.remove("blendMode"); layer.remove("positionLocked"); layer.remove("rowColor");
        layers[0] = layer; manifest = replaceLayers(manifest, layers);
        QString error;
        const auto loaded = Project::fromJson(manifest, &error);
        QVERIFY2(loaded.has_value(), qPrintable(error));
        const auto &result = loaded->shots[0].layers[0];
        QCOMPARE(result.blendMode, LayerBlendMode::Normal); QVERIFY(!result.positionLocked); QVERIFY(!result.rowColor.isValid());
    }
    void versionSixRequiresStrictLayerMetadata_data() {
        QTest::addColumn<QString>("field"); QTest::addColumn<QJsonValue>("value");
        for (const auto &field : {"blendMode", "positionLocked", "rowColor"}) {
            QTest::newRow(qPrintable(QString(field) + " missing")) << QString(field) << QJsonValue(QJsonValue::Undefined);
            QTest::newRow(qPrintable(QString(field) + " wrong type")) << QString(field) << QJsonValue(1);
        }
        QTest::newRow("unknown blend") << QString("blendMode") << QJsonValue("passthrough");
        QTest::newRow("row color name") << QString("rowColor") << QJsonValue("red");
        QTest::newRow("row color alpha") << QString("rowColor") << QJsonValue("#80402010");
        QTest::newRow("row color malformed") << QString("rowColor") << QJsonValue("#gg6622");
    }
    void versionSixRequiresStrictLayerMetadata() {
        QFETCH(QString, field); QFETCH(QJsonValue, value);
        auto manifest = Project::create().toJson();
        auto layers = jsonLayers(manifest); auto layer = layers[0].toObject(); layer[field] = value; layers[0] = layer;
        QString error; QVERIFY(!Project::fromJson(replaceLayers(manifest, layers), &error)); QVERIFY(!error.isEmpty());
    }
    void invalidInMemoryMetadataCannotBeSaved() {
        auto project = Project::create(); auto &layer = project.shots[0].layers[0];
        layer.blendMode = LayerBlendMode(-1); QVERIFY(!project.validate().isEmpty());
        layer.blendMode = LayerBlendMode::Exclusion; layer.rowColor = QColor(32, 64, 96, 128);
        QVERIFY(!project.validate().isEmpty());
        layer.rowColor = QColor("#abcdef"); QVERIFY(project.validate().isEmpty());
    }
    void everyBlendModeRoundTrips() {
        auto project = Project::create(); QString error;
        for (int mode = int(LayerBlendMode::Normal); mode <= int(LayerBlendMode::Exclusion); ++mode) {
            auto &layer = project.shots[0].layers[0]; layer.blendMode = LayerBlendMode(mode);
            const auto loaded = Project::fromJson(project.toJson(), &error);
            QVERIFY2(loaded.has_value(), qPrintable(error));
            QCOMPARE(loaded->shots[0].layers[0].blendMode, layer.blendMode);
        }
    }
    void hierarchyRejectsInvalidParentsAndCycles() {
        QFETCH(QString, kind);
        auto project = nestedProject(2);
        auto &layers = project.shots[0].layers;
        if (kind == "missing parent") layers[0].parentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        else if (kind == "cross-shot parent") {
            auto second = Project::create().shots[0]; second.layers.append(group());
            layers[0].parentId = second.layers.last().id; project.shots.append(second);
        } else if (kind == "raster parent") layers[1].parentId = layers[0].id;
        else if (kind == "self cycle") layers[1].parentId = layers[1].id;
        else if (kind == "group cycle") layers[1].parentId = layers[2].id;
        else if (kind == "no raster") layers[0].group = true;
        QVERIFY(!project.validate().isEmpty());
        QString error; QVERIFY(!Project::fromJson(project.toJson(), &error)); QVERIFY(!error.isEmpty());
    }
    void nestingAndNodeLimitsAreBounded() {
        auto project = nestedProject(8);
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        QString error;
        QVERIFY2(Project::fromJson(project.toJson(), &error), qPrintable(error));
        project = nestedProject(9);
        QVERIFY(project.validate().contains("eight"));
        QVERIFY(!Project::fromJson(project.toJson(), &error)); QVERIFY(error.contains("eight"));
        project = Project::create();
        for (int i = 1; i < 64; ++i) project.shots[0].layers.append(group());
        QVERIFY(project.validate().isEmpty());
        project.shots[0].layers.append(group());
        QVERIFY(project.validate().contains("64"));
    }
    void groupsRejectRasterOnlyState_data() {
        QTest::addColumn<QString>("property");
        for (const auto &property : {"tiles", "clipped", "alphaLocked"}) QTest::newRow(property) << QString(property);
    }
    void groupsRejectRasterOnlyState() {
        QFETCH(QString, property);
        auto project = nestedProject(1);
        auto &entry = project.shots[0].layers[1];
        if (property == "tiles") entry.tiles[{0, 0}] = pixels();
        else if (property == "clipped") entry.clipped = true;
        else entry.alphaLocked = true;
        QVERIFY(project.validate().contains("Groups cannot"));
        QString error; QVERIFY(!Project::fromJson(project.toJson(), &error)); QVERIFY(error.contains("Groups cannot"));
    }
    void versionFourRequiresTypedHierarchyFields_data() {
        QTest::addColumn<QString>("field"); QTest::addColumn<bool>("missing");
        for (const auto &field : {"parentId", "group", "clipped", "alphaLocked"}) {
            QTest::newRow(qPrintable(QString(field) + " missing")) << QString(field) << true;
            QTest::newRow(qPrintable(QString(field) + " wrong type")) << QString(field) << false;
        }
    }
    void versionFourRequiresTypedHierarchyFields() {
        QFETCH(QString, field); QFETCH(bool, missing);
        auto manifest = Project::create().toJson();
        auto layers = jsonLayers(manifest); auto layer = layers[0].toObject();
        if (missing) layer.remove(field); else layer[field] = 1;
        layers[0] = layer;
        QString error;
        QVERIFY(!Project::fromJson(replaceLayers(manifest, layers), &error));
        QVERIFY(error.contains("settings"));
    }
    void invalidHierarchyCannotOverwriteSavedProject() {
        QTemporaryDir temporary; QString error;
        const auto path = temporary.filePath("recovery.toonproj");
        auto project = nestedProject(2); project.shots[0].layers[0].tiles[{0, 0}] = pixels();
        const auto expected = project.toJson();
        QVERIFY(saveProject(project, path, &error));
        project.shots[0].layers[1].parentId = project.shots[0].layers[2].id;
        QVERIFY(!saveProject(project, path, &error));
        const auto loaded = loadProject(path, &error);
        QVERIFY2(loaded.has_value(), qPrintable(error));
        QCOMPARE(loaded->toJson(), expected);
        QCOMPARE(loaded->shots[0].layers[0].tiles, project.shots[0].layers[0].tiles);
    }
};

QTEST_GUILESS_MAIN(LayerStorageTest)
#include "test_layer_storage.moc"
