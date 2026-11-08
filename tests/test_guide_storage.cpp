#include "drawing_guide.h"
#include "project.h"
#include "sheet.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <limits>

using namespace toon;
namespace {
DrawingGuide fixture(GuideType type = GuideType::Perspective) {
    DrawingGuide guide;
    guide.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    guide.name = "City guide";
    guide.type = type;
    switch (type) {
    case GuideType::Perspective: guide.points = {{-40, -20}, {250, 10}, {190, 240}, {-5, 190}}; break;
    case GuideType::VanishingPoint: guide.points = {{-800, 360}}; break;
    case GuideType::TwoPoint: guide.points = {{-1200, 100}, {2600, 100}, {400, 800}}; break;
    case GuideType::ParallelRuler: guide.points = {{-400, -200}, {800, 600}}; break;
    case GuideType::ThreePoint: guide.points = {{-1200, 100}, {2600, 100}, {400, -2400}, {400, 800}}; break;
    case GuideType::Fisheye: guide.points = {{-200, 200}, {600, 200}, {200, 500}}; break;
    case GuideType::Ellipse: guide.points = {{50, 100}, {450, 300}, {210, 280}}; break;
    }
    return guide;
}
QJsonObject editGuide(QJsonObject object, const QString &key, const QJsonValue &value) {
    auto guides = object["guides"].toArray();
    auto guide = guides[0].toObject(); guide[key] = value;
    guides[0] = guide; object["guides"] = guides;
    return object;
}
bool writeFile(const QString &path, const QByteArray &data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
QByteArray readFile(const QString &path) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
QByteArray imageBytes(const QImage &image) {
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG"); return bytes;
}
QByteArray container(int version, const QJsonObject &manifest, const QList<QByteArray> &payloads = {}) {
    QByteArray bytes = "TOONPRO" + QByteArray::number(version) + "\n";
    QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly | QIODevice::Append);
    QDataStream stream(&buffer); stream.setByteOrder(QDataStream::BigEndian);
    auto append = [&stream](const QByteArray &data) {
        stream << quint32(data.size());
        const auto hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
        stream.writeRawData(hash.constData(), hash.size());
        stream.writeRawData(data.constData(), data.size());
    };
    append(QJsonDocument(manifest).toJson(QJsonDocument::Compact));
    for (const auto &payload : payloads) append(payload);
    return bytes;
}
}

class GuideStorageTest : public QObject {
    Q_OBJECT
private slots:
    void allGuideTypesRoundTripWithArtwork() {
        auto project = Project::create();
        auto &shot = project.shots[0];
        for (auto type : {GuideType::Perspective, GuideType::VanishingPoint, GuideType::TwoPoint, GuideType::ParallelRuler,
                          GuideType::ThreePoint, GuideType::Fisheye, GuideType::Ellipse})
            shot.guides.append(fixture(type));
        auto &guide = shot.guides[0];
        guide.name = QString::fromUtf8("街 / roof");
        guide.visible = false; guide.locked = true; guide.snapping = false; guide.customColor = true;
        guide.color = QColor(17, 48, 199, 213); guide.opacity = .35;
        guide.subdivisions = 64; guide.vertical = false; guide.limited = true;
        guide.area = {-15.5, 22.25, 333.75, 444.5};
        shot.guides[1].subdivisions = 1;
        shot.guideSettings = {false, true, false, QColor("#aa7733"), .8};
        QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(QColor(33, 55, 88, 128));
        shot.layers[0].tiles[{-1, 0}] = tile;
        shot.references.append({QUuid::createUuid().toString(QUuid::WithoutBraces), "City reference", tile});
        auto second = Project::create().shots[0]; second.guides.append(fixture(GuideType::VanishingPoint));
        project.shots.append(second);
        QTemporaryDir temporary; QString error;
        const auto path = temporary.filePath("guides.toonproj");
        QVERIFY2(saveProject(project, path, &error), qPrintable(error));
        QCOMPARE(readFile(path).left(10), QByteArray("TOONPRO22\n"));
        const auto loaded = loadProject(path, &error);
        QVERIFY2(loaded.has_value(), qPrintable(error));
        QCOMPARE(loaded->toJson(), project.toJson());
        QVERIFY(loaded->shots[0].guides == project.shots[0].guides);
        QVERIFY(loaded->shots[0].guideSettings == project.shots[0].guideSettings);
        QCOMPARE(loaded->shots[0].layers[0].tiles, project.shots[0].layers[0].tiles);
        QCOMPARE(loaded->shots[0].references[0].image, tile);
        QVERIFY(loaded->shots[1].guides == second.guides);
    }
    void standaloneGuideSetRoundTripAndAtomicFailure() {
        QTemporaryDir temporary; QString error;
        const auto path = temporary.filePath("city.toonguides");
        QVector<DrawingGuide> guides{fixture(), fixture(GuideType::TwoPoint), fixture(GuideType::ThreePoint), fixture(GuideType::Fisheye), fixture(GuideType::Ellipse)};
        GuideSettings settings{true, true, false, QColor("#6995fe"), .7};
        QVERIFY2(saveGuideSet(guides, settings, path, &error), qPrintable(error));
        const auto bytes = readFile(path);
        QVERIFY(bytes.size() < 1024 * 1024);
        const auto json = QJsonDocument::fromJson(bytes).object();
        QCOMPARE(json.value("format").toString(), QString("toon-pro-guides"));
        QCOMPARE(json.value("version").toInt(), 1);
        QVector<DrawingGuide> loaded; GuideSettings loadedSettings;
        QVERIFY2(loadGuideSet(path, &loaded, &loadedSettings, &error), qPrintable(error));
        QVERIFY(loaded == guides); QVERIFY(loadedSettings == settings);
        guides[0].points[0].setX(std::numeric_limits<qreal>::quiet_NaN());
        QVERIFY(!saveGuideSet(guides, settings, path, &error));
        QCOMPARE(readFile(path), bytes);
        QVERIFY(!saveGuideSet(loaded, settings, temporary.filePath("missing/file.toonguides"), &error));
        QVERIFY(saveGuideSet({}, {}, path, &error));
        QVERIFY(loadGuideSet(path, &loaded, &loadedSettings, &error));
        QVERIFY(loaded.isEmpty()); QVERIFY(loadedSettings == GuideSettings{});
    }
    void invalidJsonNeverPartiallyReplacesDestination_data() {
        QTest::addColumn<QString>("kind");
        for (const char *kind : {"missing setting", "numeric toggle", "null opacity", "negative opacity", "oversized opacity",
            "named color", "bad color", "unknown type", "null type", "bad id", "empty name", "long name", "missing point",
            "wrong coordinate type", "large coordinate", "bow-tie quad", "coincident quad", "fractional subdivision",
            "zero subdivision", "large subdivision", "negative area", "out-of-bounds area", "duplicate id", "nonobject guide",
            "collinear three-point", "three-point anchor on horizon", "three-point anchor on side",
            "coincident fisheye axis", "flat fisheye", "missing fisheye point", "three-point wrong count"})
            QTest::newRow(kind) << QString(kind);
    }
    void invalidJsonNeverPartiallyReplacesDestination() {
        QFETCH(QString, kind);
        auto object = guidesToJson({fixture()}, {});
        if (kind == "missing setting") { auto settings = object["settings"].toObject(); settings.remove("handles"); object["settings"] = settings; }
        else if (kind == "numeric toggle") object = editGuide(object, "visible", 1);
        else if (kind == "null opacity") object = editGuide(object, "opacity", QJsonValue(QJsonValue::Null));
        else if (kind == "negative opacity") object = editGuide(object, "opacity", -.1);
        else if (kind == "oversized opacity") object = editGuide(object, "opacity", 1.1);
        else if (kind == "named color") object = editGuide(object, "color", "blue");
        else if (kind == "bad color") object = editGuide(object, "color", "#zz0000");
        else if (kind == "unknown type") object = editGuide(object, "type", "unsupported-guide");
        else if (kind == "null type") object = editGuide(object, "type", QJsonValue(QJsonValue::Null));
        else if (kind == "bad id") object = editGuide(object, "id", "guide-123");
        else if (kind == "empty name") object = editGuide(object, "name", "   ");
        else if (kind == "long name") object = editGuide(object, "name", QString(101, 'x'));
        else if (kind == "missing point") object = editGuide(object, "points", QJsonArray{QJsonArray{0, 0}});
        else if (kind == "wrong coordinate type" || kind == "large coordinate") {
            auto points = object["guides"].toArray()[0].toObject()["points"].toArray();
            points[0] = QJsonArray{kind == "wrong coordinate type" ? QJsonValue("40") : QJsonValue(1000001), 0};
            object = editGuide(object, "points", points);
        } else if (kind == "bow-tie quad") object = editGuide(object, "points", QJsonArray{
            QJsonArray{0, 0}, QJsonArray{100, 100}, QJsonArray{0, 100}, QJsonArray{100, 0}});
        else if (kind == "coincident quad") object = editGuide(object, "points", QJsonArray{
            QJsonArray{0, 0}, QJsonArray{0, 0}, QJsonArray{0, 100}, QJsonArray{100, 0}});
        else if (kind == "fractional subdivision") object = editGuide(object, "subdivisions", 2.5);
        else if (kind == "zero subdivision") object = editGuide(object, "subdivisions", 0);
        else if (kind == "large subdivision") object = editGuide(object, "subdivisions", 65);
        else if (kind == "negative area") object = editGuide(object, "area", QJsonArray{0, 0, -1, 40});
        else if (kind == "out-of-bounds area") object = editGuide(object, "area", QJsonArray{999999, 0, 2, 40});
        else if (kind == "duplicate id") { auto entries = object["guides"].toArray(); entries.append(entries[0]); object["guides"] = entries; }
        else if (kind == "nonobject guide") object["guides"] = QJsonArray{1};
        else if (kind.startsWith("three-point") || kind == "collinear three-point") {
            auto guide = fixture(GuideType::ThreePoint);
            if (kind == "collinear three-point") guide.points[2] = {400, 100};
            else if (kind == "three-point anchor on horizon") guide.points[3] = {400, 100};
            else if (kind == "three-point anchor on side") guide.points[3] = (guide.points[0] + guide.points[2]) / 2;
            else guide.points.removeLast();
            object = guidesToJson({guide}, {});
        } else if (kind.contains("fisheye")) {
            auto guide = fixture(GuideType::Fisheye);
            if (kind == "coincident fisheye axis") guide.points[1] = guide.points[0];
            else if (kind == "flat fisheye") guide.points[2] = {400, 200};
            else guide.points.removeLast();
            object = guidesToJson({guide}, {});
        }
        QVector<DrawingGuide> destination{fixture(GuideType::ParallelRuler)};
        const auto before = destination;
        GuideSettings settings{false, true, false, QColor("#123456"), .3};
        const auto beforeSettings = settings;
        QString error;
        QVERIFY(!guidesFromJson(object, &destination, &settings, &error));
        QVERIFY(!error.isEmpty()); QVERIFY(destination == before); QVERIFY(settings == beforeSettings);
    }
    void newGuideTypeKeysAndPointCountsAreStable() {
        const auto json = guidesToJson({fixture(GuideType::ThreePoint), fixture(GuideType::Fisheye), fixture(GuideType::Ellipse)}, {});
        const auto entries = json.value("guides").toArray();
        QCOMPARE(entries[0].toObject().value("type").toString(), QString("three-point"));
        QCOMPARE(entries[1].toObject().value("type").toString(), QString("fisheye"));
        QCOMPARE(entries[2].toObject().value("type").toString(), QString("ellipse"));
        QCOMPARE(guidePointCount(GuideType::ThreePoint), 4);
        QCOMPARE(guidePointCount(GuideType::Fisheye), 3);
        QCOMPARE(guidePointCount(GuideType::Ellipse), 3);
        QCOMPARE(guideTypeName(GuideType::ThreePoint), QString("Three-point perspective"));
        QCOMPARE(guideTypeName(GuideType::Fisheye), QString("Fisheye perspective"));
        QCOMPARE(guideTypeName(GuideType::Ellipse), QString("Ellipse ruler"));
        QCOMPARE(guidePointCount(static_cast<GuideType>(99)), 0);
    }
    void unknownGuideTypesRejectProjectAndStandaloneSet() {
        auto project = Project::create();
        project.shots[0].guides.append(fixture(GuideType::Fisheye));
        auto manifest = project.toJson();
        auto shots = manifest.value("shots").toArray();
        auto shot = shots[0].toObject();
        shot["drawingGuides"] = editGuide(shot["drawingGuides"].toObject(), "type", "future-guide");
        shots[0] = shot; manifest["shots"] = shots;
        QTemporaryDir temporary; QString error;
        const auto projectPath = temporary.filePath("future.toonproj");
        QVERIFY(writeFile(projectPath, container(manifest.value("version").toInt(), manifest)));
        QVERIFY(!loadProject(projectPath, &error));
        QVERIFY(error.contains("Unsupported drawing guide type"));
        auto object = shot["drawingGuides"].toObject();
        object["format"] = "toon-pro-guides"; object["version"] = 1;
        const auto setPath = temporary.filePath("future.toonguides");
        QVERIFY(writeFile(setPath, QJsonDocument(object).toJson()));
        QVector<DrawingGuide> guides{fixture()}; const auto before = guides;
        GuideSettings settings; const auto beforeSettings = settings;
        QVERIFY(!loadGuideSet(setPath, &guides, &settings, &error));
        QVERIFY(error.contains("Unsupported drawing guide type"));
        QVERIFY(guides == before); QVERIFY(settings == beforeSettings);
    }
    void ellipseGuideRetainsFlagsAndRejectsDegenerateSaveAtomically() {
        auto ellipse = fixture(GuideType::Ellipse); ellipse.visible = false; ellipse.locked = true;
        ellipse.snapping = false; ellipse.customColor = true; ellipse.color = QColor(80, 190, 140, 170);
        ellipse.opacity = .4; ellipse.limited = true; ellipse.area = {10, 20, 600, 500};
        auto project = Project::create(); project.shots[0].guides = {ellipse};
        const auto manifest = project.toJson(); QCOMPARE(manifest["version"].toInt(), 19);
        QTemporaryDir directory; QString error; const auto file = directory.filePath("ellipse.toonproj");
        QVERIFY2(saveProject(project, file, &error), qPrintable(error)); const auto original = readFile(file);
        const auto loaded = loadProject(file, &error); QVERIFY2(loaded.has_value(), qPrintable(error));
        QCOMPARE(loaded->shots[0].guides, project.shots[0].guides);
        const auto set = directory.filePath("ellipse.toonguides");
        QVERIFY2(saveGuideSet({ellipse}, {}, set, &error), qPrintable(error));
        const auto savedSet = readFile(set); QVector<DrawingGuide> restored; GuideSettings settings;
        QVERIFY2(loadGuideSet(set, &restored, &settings, &error), qPrintable(error)); QCOMPARE(restored, QVector<DrawingGuide>{ellipse});
        project.shots[0].guides[0].points[1] = project.shots[0].guides[0].points[0];
        QVERIFY(!saveProject(project, file, &error)); QCOMPARE(readFile(file), original);
        auto invalid = ellipse; invalid.points[2] = (invalid.points[0] + invalid.points[1]) / 2;
        QVERIFY(!saveGuideSet({invalid}, {}, set, &error)); QCOMPARE(readFile(set), savedSet);
    }
    void budgetsAndIdentifiersSpanWholeProject() {
        auto project = Project::create();
        for (int i = 0; i < maxDrawingGuides; ++i) project.shots[0].guides.append(fixture());
        QVERIFY(project.validate().isEmpty());
        project.shots[0].guides.append(fixture());
        QVERIFY(project.validate().contains("64"));
        project.shots[0].guides.removeLast();
        for (int s = 1; s < 4; ++s) {
            auto shot = Project::create().shots[0];
            for (int i = 0; i < maxDrawingGuides; ++i) shot.guides.append(fixture(GuideType::VanishingPoint));
            project.shots.append(shot);
        }
        QVERIFY(project.validate().isEmpty());
        auto last = Project::create().shots[0]; last.guides.append(fixture()); project.shots.append(last);
        QVERIFY(project.validate().contains("256"));
        QString error; QVERIFY(!Project::fromJson(project.toJson(), &error)); QVERIFY(error.contains("256"));
        project = Project::create(); project.shots[0].guides.append(fixture());
        project.shots[0].guides[0].id = QUuid(project.shots[0].layers[0].id).toString().toUpper();
        QVERIFY(project.validate().contains("identifier"));
        project.shots[0].guides[0] = fixture();
        auto second = Project::create().shots[0]; second.guides = project.shots[0].guides;
        project.shots.append(second);
        QVERIFY(project.validate().contains("identifier"));
        QVERIFY(!Project::fromJson(project.toJson(), &error));
    }
    void corruptAndOversizedGuideFilesLeaveSettingsUntouched() {
        QTemporaryDir temporary; QString error;
        const auto path = temporary.filePath("bad.toonguides");
        QVector<DrawingGuide> guides{fixture()}; GuideSettings settings;
        const auto expected = guides;
        for (const QByteArray &bytes : {QByteArray("{broken"), QByteArray("[]"), QByteArray("{}"), QByteArray(1024 * 1024 + 1, ' ')}) {
            QVERIFY(writeFile(path, bytes)); QVERIFY(!loadGuideSet(path, &guides, &settings, &error));
            QVERIFY(guides == expected); QVERIFY(settings == GuideSettings{});
        }
        QVERIFY(!loadGuideSet(temporary.filePath("missing.toonguides"), &guides, &settings, &error));
        auto object = guidesToJson(guides, settings); object["format"] = "toon-pro-guides"; object["version"] = 2;
        QVERIFY(writeFile(path, QJsonDocument(object).toJson()));
        QVERIFY(!loadGuideSet(path, &guides, &settings, &error)); QVERIFY(error.contains("version"));
    }
    void olderVersionsMigrateWithoutChangingArtwork_data() {
        QTest::addColumn<int>("version");
        for (int version = 1; version <= 4; ++version) QTest::newRow(qPrintable(QString::number(version))) << version;
    }
    void olderVersionsMigrateWithoutChangingArtwork() {
        QFETCH(int, version);
        auto project = Project::create();
        QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(QColor(15, 34, 95, 120));
        if (version >= 2) project.shots[0].layers[0].tiles[{-1, 2}] = tile;
        if (version >= 3) project.shots[0].references.append({QUuid::createUuid().toString(QUuid::WithoutBraces), "Legacy", tile});
        auto manifest = project.toJson(); manifest["version"] = version;
        auto shots = manifest["shots"].toArray(); auto shot = shots[0].toObject(); shot.remove("drawingGuides");
        if (version == 1) shot.remove("layers");
        else if (version < 4) {
            auto layers = shot["layers"].toArray(); auto layer = layers[0].toObject();
            for (const char *key : {"parentId", "group", "clipped", "alphaLocked"}) layer.remove(key);
            layers[0] = layer; shot["layers"] = layers;
        }
        if (version < 3) shot.remove("references");
        shots[0] = shot; manifest["shots"] = shots;
        QList<QByteArray> payloads;
        if (version >= 2) payloads.append(imageBytes(tile));
        if (version >= 3) payloads.append(imageBytes(tile));
        const auto bytes = version == 1 ? QJsonDocument(manifest).toJson() : container(version, manifest, payloads);
        QTemporaryDir temporary; QString error;
        const auto path = temporary.filePath("legacy.toonproj");
        QVERIFY(writeFile(path, bytes));
        const auto loaded = loadProject(path, &error);
        QVERIFY2(loaded.has_value(), qPrintable(error));
        QVERIFY(loaded->shots[0].guides.isEmpty()); QVERIFY(loaded->shots[0].guideSettings == GuideSettings{});
        QCOMPARE(renderMovieFrame(*loaded), renderMovieFrame(project));
        if (version >= 2) QCOMPARE(loaded->shots[0].layers[0].tiles, project.shots[0].layers[0].tiles);
        if (version >= 3) QCOMPARE(loaded->shots[0].references[0].image, tile);
        QCOMPARE(readFile(path), bytes); // Loading never rewrites the source.
        QVERIFY(saveProject(*loaded, path, &error));
        QCOMPARE(readFile(path).left(10), QByteArray("TOONPRO19\n"));
        const auto reopened = loadProject(path, &error); QVERIFY2(reopened.has_value(), qPrintable(error));
        QCOMPARE(reopened->toJson(), loaded->toJson());
    }
    void currentVersionRequiresGuideFieldsAndMatchingContainer() {
        auto manifest = Project::create().toJson();
        auto shots = manifest["shots"].toArray(); auto shot = shots[0].toObject(); shot.remove("drawingGuides");
        shots[0] = shot; manifest["shots"] = shots;
        QString error; QVERIFY(!Project::fromJson(manifest, &error)); QVERIFY(error.contains("guide"));
        QTemporaryDir temporary; const auto path = temporary.filePath("mismatch.toonproj");
        QVERIFY(writeFile(path, container(4, Project::create().toJson())));
        QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("container"));
    }
    void editorGuidesNeverAppearInArtworkExports() {
        auto project = Project::create(); project.width = 640; project.height = 360;
        QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(QColor(30, 50, 90, 100));
        project.shots[0].layers[0].tiles[{0, 0}] = tile;
        const auto sheet = renderSheet(project, project.shots[0]);
        const auto frame = renderMovieFrame(project);
        for (auto type : {GuideType::Perspective, GuideType::VanishingPoint, GuideType::TwoPoint, GuideType::ParallelRuler,
                          GuideType::ThreePoint, GuideType::Fisheye, GuideType::Ellipse}) {
            auto guide = fixture(type); guide.color = Qt::red; guide.customColor = true; guide.opacity = 1;
            project.shots[0].guides.append(guide);
        }
        project.shots[0].guideSettings.snapping = true;
        project.shots[0].guideSettings.color = Qt::green;
        project.shots[0].guideSettings.opacity = 1;
        QCOMPARE(renderSheet(project, project.shots[0]), sheet);
        QCOMPARE(renderMovieFrame(project), frame);
        project.transparent = true;
        const auto transparentWithGuides = renderMovieFrame(project);
        project.shots[0].guides.clear();
        QCOMPARE(renderMovieFrame(project), transparentWithGuides);
    }
};
QTEST_MAIN(GuideStorageTest)
#include "test_guide_storage.moc"
