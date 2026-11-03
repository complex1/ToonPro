#include "project.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
using namespace toon;
namespace {
Project fixture() {
    auto p = Project::create(); auto &shot = p.shots[0];
    shot.layerAnimations = {{shot.layers[0].id, {20, 30}, {
        {KeyframeProperty::PositionX, {{0, 0}, {20, 80}}},
        {KeyframeProperty::PositionY, {{0, 0}, {20, 0}}}}, {}, true}};
    for (auto &channel : shot.layerAnimations[0].channels) channel.keys[0].easing = CubicEasing{{.2, .1}, {.8, .9}};
    shot.layerAnimations[0].channels[0].keys[0].spatial = SpatialTangents{{}, {0, 50}, true};
    shot.layerAnimations[0].channels[0].keys[1].spatial = SpatialTangents{{0, 50}, {}, false};
    return p;
}
QByteArray bytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
bool writeContainer(const QString &path, QJsonObject metadata, int version) {
    metadata["version"] = version; QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
    if (version == 1) return file.write(QJsonDocument(metadata).toJson(QJsonDocument::Compact)) > 0;
    file.write(QByteArray("TOONPRO") + QByteArray::number(version) + '\n');
    QDataStream stream(&file); stream.setByteOrder(QDataStream::BigEndian);
    const auto data = QJsonDocument(metadata).toJson(QJsonDocument::Compact), hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    stream << quint32(data.size()); stream.writeRawData(hash.constData(), hash.size()); stream.writeRawData(data.constData(), data.size());
    return stream.status() == QDataStream::Ok;
}
}
class AdvancedMotionStorageTests : public QObject {
    Q_OBJECT
private slots:
    void roundTripAndFailedSavePreserveCurves() {
        QTemporaryDir dir; const auto path = dir.filePath("motion.toon"); auto p = fixture(); QString error;
        QVERIFY2(saveProject(p, path, &error), qPrintable(error)); QVERIFY(bytes(path).startsWith("TOONPRO19\n"));
        auto loaded = loadProject(path, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->shots[0].layerAnimations, p.shots[0].layerAnimations); QCOMPARE(loaded->toJson(), p.toJson());
        for (int frame : {0, 1, 8, 19, 20, 119}) QCOMPARE(evaluateLayerTransform(loaded->shots[0], p.shots[0].layers[0].id, frame), evaluateLayerTransform(p.shots[0], p.shots[0].layers[0].id, frame));
        const auto original = bytes(path); p.shots[0].layerAnimations[0].channels[0].keys[0].easing->first.setX(.9);
        QVERIFY(!saveProject(p, path, &error)); QCOMPARE(bytes(path), original);
    }
    void allLegacyContainersMigrateAndNewFieldsCannotBeDisguised() {
        QTemporaryDir dir; const auto path = dir.filePath("legacy.toon"); QString error;
        for (int version = 1; version <= 17; ++version) {
            auto metadata = Project::create().toJson(); if (version < 15) metadata.remove("audioResources");
            QVERIFY(writeContainer(path, metadata, version)); const auto loaded = loadProject(path, &error);
            QVERIFY2(loaded, qPrintable(QString("Legacy %1: %2").arg(version).arg(error)));
            QCOMPARE(loaded->toJson()["version"].toInt(), 19);
        }
        for (int version = 14; version <= 17; ++version) {
            auto metadata = fixture().toJson(); metadata["version"] = version;
            QVERIFY(!Project::fromJson(metadata, &error)); QVERIFY(error.contains("format 18"));
        }
        auto future = Project::create().toJson(); future["version"] = latestProjectFormatVersion + 1;
        QVERIFY(!Project::fromJson(future, &error)); QVERIFY(writeContainer(path, future, latestProjectFormatVersion + 1)); QVERIFY(!loadProject(path, &error));
    }
    void strictNestedMetadataAndPairing() {
        auto p = fixture(); const auto original = layerAnimationsToJson(p.shots[0].layerAnimations); QString error;
        QVector<LayerAnimation> parsed;
        QVERIFY(layerAnimationsFromJson(original, &parsed, &error)); QCOMPARE(parsed, p.shots[0].layerAnimations);
        auto mutate = [&](const QString &field, const QJsonValue &value) {
            auto json = original; auto animation = json[0].toObject(); auto channels = animation["channels"].toArray();
            auto channel = channels[0].toObject(); auto keys = channel["keys"].toArray(); auto key = keys[0].toObject();
            key[field] = value; keys[0] = key; channel["keys"] = keys; channels[0] = channel; animation["channels"] = channels; json[0] = animation;
            return json;
        };
        for (const auto value : {QJsonValue(), QJsonValue("curve"), QJsonValue(QJsonObject{{"first", QJsonArray{.8, 0}}, {"second", QJsonArray{.2, 1}}}),
            QJsonValue(QJsonObject{{"first", QJsonArray{.1, 0}}, {"second", QJsonArray{.8, 1}}, {"extra", 1}})})
            QVERIFY(!layerAnimationsFromJson(mutate("easing", value), &parsed, &error));
        QVERIFY(!layerAnimationsFromJson(mutate("spatial", QJsonObject{{"incoming", QJsonArray{0, 0}}, {"outgoing", QJsonArray{0, 50}}, {"curved", "yes"}}), &parsed, &error));
        auto json = original; auto animation = json[0].toObject(); animation["orientAlongPath"] = 1; json[0] = animation;
        QVERIFY(!layerAnimationsFromJson(json, &parsed, &error));
        p.shots[0].layerAnimations[0].channels[1].keys[0].easing.reset();
        QVERIFY(!layerAnimationsFromJson(layerAnimationsToJson(p.shots[0].layerAnimations), &parsed, &error));
    }
};
QTEST_MAIN(AdvancedMotionStorageTests)
#include "advanced_motion_storage_tests.moc"
