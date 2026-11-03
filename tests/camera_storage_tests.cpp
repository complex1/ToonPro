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
    auto project = Project::create(); project.width = 128; project.height = 96;
    auto &shot = project.shots[0]; shot.durationFrames = 60; shot.camera2D = {{20, 10}, 1.4, 23};
    shot.camera2DKeys = {{0, {}}, {30, {{40, -5}, 2, 720}, KeyframeInterpolation::EaseInOut}};
    auto scene = RasterLayer::create("City"); scene.kind = LayerKind::Scene3D; scene.scene3d = Scene3D();
    scene.scene3d->camera.orthographic = true; shot.layers.append(scene);
    shot.sceneCameraAnimations = {{scene.id, {{0, {}}, {40, {{3, 1, -2}, 120, -20, 14, 60, 7}, KeyframeInterpolation::EaseOut}}}};
    return project;
}
QByteArray bytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
bool writeContainer(const QString &path, QJsonObject json, int version) {
    QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
    const auto magic = QString("TOONPRO%1\n").arg(version).toLatin1(); if (file.write(magic) != magic.size()) return false;
    const auto data = QJsonDocument(json).toJson(QJsonDocument::Compact), hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    QDataStream stream(&file); stream.setByteOrder(QDataStream::BigEndian); stream << quint32(data.size());
    return stream.writeRawData(hash.constData(), hash.size()) == hash.size() && stream.writeRawData(data.constData(), data.size()) == data.size();
}
}
class CameraStorageTests final : public QObject {
    Q_OBJECT
private slots:
    void currentFormatRoundTripAndFailedSavePreservesFile() {
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("camera.toon");
        const auto project = fixture(); QString error;
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        QVERIFY2(saveProject(project, path, &error), qPrintable(error)); const auto saved = bytes(path);
        QVERIFY(saved.startsWith("TOONPRO19\n"));
        const auto loaded = loadProject(path, &error); QVERIFY2(loaded.has_value(), qPrintable(error));
        QCOMPARE(loaded->toJson(), project.toJson()); QCOMPARE(loaded->shots[0].camera2DKeys, project.shots[0].camera2DKeys);
        QCOMPARE(loaded->shots[0].sceneCameraAnimations, project.shots[0].sceneCameraAnimations);
        QCOMPARE(evaluateShotCamera(loaded->shots[0], 15), evaluateShotCamera(project.shots[0], 15));
        auto invalid = project; invalid.shots[0].durationFrames = 20;
        QVERIFY(!saveProject(invalid, path, &error)); QCOMPARE(bytes(path), saved);
        invalid = project; invalid.shots[0].camera2D.zoom = 0;
        QVERIFY(!saveProject(invalid, path, &error)); QCOMPARE(bytes(path), saved);
    }
    void legacy15IsIdentityAndCameraMetadataCannotHideInOlderFormat() {
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("legacy.toon");
        auto json = Project::create().toJson(); json["version"] = 15; QString error;
        QVERIFY(writeContainer(path, json, 15)); const auto loaded = loadProject(path, &error);
        QVERIFY2(loaded.has_value(), qPrintable(error)); QCOMPARE(loaded->shots[0].camera2D, Camera2D());
        QVERIFY(loaded->shots[0].camera2DKeys.isEmpty()); QVERIFY(loaded->shots[0].sceneCameraAnimations.isEmpty());
        QCOMPARE(loaded->toJson()["version"].toInt(), 19);
        json = fixture().toJson(); json["version"] = 15;
        QVERIFY(!Project::fromJson(json, &error)); QVERIFY(error.contains("format 16"));
        json = fixture().toJson(); json["version"] = latestProjectFormatVersion + 1; QVERIFY(!Project::fromJson(json, &error));
        QVERIFY(writeContainer(path, Project::create().toJson(), 15)); QVERIFY(!loadProject(path, &error));
        QVERIFY(error.contains("does not match"));
    }
    void malformedAndDanglingCameraKeysAreRejected() {
        const auto project = fixture(); QString error;
        auto json = project.toJson(); auto shots = json["shots"].toArray(); auto shot = shots[0].toObject();
        auto scenes = shot["sceneCameraAnimations"].toArray(); auto scene = scenes[0].toObject(); scene["layerId"] = project.shots[0].layers[0].id;
        scenes[0] = scene; shot["sceneCameraAnimations"] = scenes; shots[0] = shot; json["shots"] = shots;
        QVERIFY(!Project::fromJson(json, &error));
        json = project.toJson(); shots = json["shots"].toArray(); shot = shots[0].toObject();
        auto camera = shot["camera2D"].toObject(); auto keys = camera["keys"].toArray(); auto key = keys[1].toObject(); key["frame"] = 60;
        keys[1] = key; camera["keys"] = keys; shot["camera2D"] = camera; shots[0] = shot; json["shots"] = shots;
        QVERIFY(!Project::fromJson(json, &error));
    }
};
QTEST_MAIN(CameraStorageTests)
#include "camera_storage_tests.moc"
