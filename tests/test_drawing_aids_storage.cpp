#include "drawing_aids.h"
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
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QImage image(QSize size = {32, 24}) {
    QImage image(size, QImage::Format_ARGB32_Premultiplied); image.fill(QColor(35, 90, 170, 128)); image.setPixelColor(0, 0, Qt::transparent); return image;
}
Project fixture() {
    auto project = Project::create(); project.width = 320; project.height = 240;
    const auto &shot = project.shots[0];
    LightTablePin pin; pin.id = id(); pin.name = "C001 · Drawing / Inks"; pin.shotId = shot.id; pin.layerId = shot.layers[0].id;
    pin.image = image(); pin.bounds = QRectF(-20, 15, 200, 150); pin.offset = QPointF(12.5, -34.25); pin.scale = 1.35; pin.rotation = -28; pin.opacity = .31; pin.visible = false;
    project.drawingAids.lightTable.pins.append(pin); project.drawingAids.lightTable.selectedPinId = pin.id;
    project.drawingAids.lightTable.solo = true; project.drawingAids.lightTable.visible = false;
    CelShadingDraft draft; draft.shotId = shot.id; draft.outlineLayerId = shot.layers[0].id; draft.frame = 7; draft.width = 6;
    draft.kind = 1; draft.mode = 1; draft.finish = CelBoundaryFinish::RegionColor; draft.highlight = QColor("#f0e6b2");
    QPainterPath path(QPointF(10, 12)); path.lineTo(30, 40); path.cubicTo(50, 20, 80, 65, 90, 20); path.setFillRule(Qt::WindingFill);
    draft.boundaries.append({path, CelShadeKind::Highlight, QColor("#aabbcc"), 6});
    draft.fills.append({QPoint(30, 30), CelShadeKind::Highlight, QColor("#cc9988")}); draft.operations = {false, true};
    project.drawingAids.shadingDrafts.append(draft); project.drawingAids.activeShotId = shot.id; project.drawingAids.activeFrame = 7;
    return project;
}
QJsonObject withAids(QJsonObject manifest, QJsonObject aids) { manifest["drawingAids"] = aids; return manifest; }
QJsonObject withPin(QJsonObject manifest, QJsonObject pin) {
    auto aids = manifest["drawingAids"].toObject(); auto light = aids["lightTable"].toObject(); auto pins = light["pins"].toArray();
    pins[0] = pin; light["pins"] = pins; aids["lightTable"] = light; return withAids(manifest, aids);
}
QJsonObject withDraft(QJsonObject manifest, QJsonObject draft) {
    auto aids = manifest["drawingAids"].toObject(); auto drafts = aids["shadingDrafts"].toArray(); drafts[0] = draft; aids["shadingDrafts"] = drafts; return withAids(manifest, aids);
}
QByteArray container(const QJsonObject &manifest, const QVector<QByteArray> &blobs = {}) {
    QByteArray bytes = "TOONPRO" + QByteArray::number(manifest["version"].toInt()) + "\n";
    QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly | QIODevice::Append); QDataStream stream(&buffer); stream.setByteOrder(QDataStream::BigEndian);
    auto write = [&](const QByteArray &data) { stream << quint32(data.size()); const auto hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256); stream.writeRawData(hash.constData(), hash.size()); stream.writeRawData(data.constData(), data.size()); };
    write(QJsonDocument(manifest).toJson(QJsonDocument::Compact)); for (const auto &blob : blobs) write(blob); return bytes;
}
QByteArray png(const QImage &image) { QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG"); return bytes; }
bool write(const QString &path, const QByteArray &bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
}

class DrawingAidsStorageTest : public QObject {
    Q_OBJECT
private slots:
    void roundTripKeepsEmbeddedSnapshotGeometryAndDraftControls() {
        const auto project = fixture(); QString error; QTemporaryDir temp; const auto path = temp.filePath("drawing.toonproj");
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        QCOMPARE(project.toJson()["version"].toInt(), 29);
        QVERIFY(project.toJson()["characters"].isArray()); QVERIFY(project.toJson()["characters"].toArray().isEmpty());
        QVERIFY2(saveProject(project, path, &error), qPrintable(error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.read(10), QByteArray("TOONPRO29\n")); file.close();
        auto loaded = loadProject(path, &error); QVERIFY2(loaded, qPrintable(error));
        QVERIFY(drawingAidsEqual(loaded->drawingAids, project.drawingAids)); QCOMPARE(loaded->toJson(), project.toJson());
        QCOMPARE(loaded->drawingAids.lightTable.pins[0].image, project.drawingAids.lightTable.pins[0].image);
        QCOMPARE(loaded->drawingAids.shadingDrafts[0].boundaries[0].path, project.drawingAids.shadingDrafts[0].boundaries[0].path);
        auto clean = *loaded; clean.drawingAids = {}; QCOMPARE(renderMovieFrame(*loaded), renderMovieFrame(clean));
        QCOMPARE(renderSheet(*loaded, loaded->shots[0]), renderSheet(clean, clean.shots[0]));
    }
    void snapshotsCoexistWithArtworkReferencesAndCharacterLibrary() {
        auto project = fixture(); QString error; QTemporaryDir temp;
        project.shots[0].layers[0].tiles[{0, 0}] = image({tileSize, tileSize});
        project.shots[0].references.append({id(), "Reference", image({12, 15})});
        project.characters.append({id(), "Hana", {{id(), "Front", ModelSheetCategory::Turnaround, image({20, 30})}}, {}});
        auto second = Project::create().shots[0]; second.number = "C002"; project.shots.append(second);
        auto another = project.drawingAids.shadingDrafts[0]; another.shotId = second.id; another.outlineLayerId = second.layers[0].id; project.drawingAids.shadingDrafts.append(another);
        LightTablePin imported = project.drawingAids.lightTable.pins[0]; imported.id = id(); imported.shotId.clear(); imported.layerId.clear(); imported.name = "Imported pose"; project.drawingAids.lightTable.pins.append(imported);
        const auto path = temp.filePath("mixed.toonproj"); QVERIFY2(saveProject(project, path, &error), qPrintable(error)); const auto loaded = loadProject(path, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->toJson(), project.toJson()); QVERIFY(drawingAidsEqual(loaded->drawingAids, project.drawingAids));
        QCOMPARE(loaded->shots[0].layers[0].tiles, project.shots[0].layers[0].tiles); QCOMPARE(loaded->shots[0].references[0].image, project.shots[0].references[0].image);
        QCOMPARE(loaded->characters[0].sheets[0].image, project.characters[0].sheets[0].image);
    }
    void legacyFilesKeepTheirFormatAndCannotSilentlyDropAidFields() {
        QString error; QTemporaryDir temp; auto project = Project::create(); const auto path = temp.filePath("legacy.toonproj");
        QCOMPARE(project.toJson()["version"].toInt(), 19); QVERIFY(saveProject(project, path, &error)); auto loaded = loadProject(path, &error); QVERIFY(loaded); QVERIFY(!hasDrawingAids(loaded->drawingAids));
        project.characters.append({id(), "Hana", {}}); QCOMPARE(project.toJson()["version"].toInt(), 27); QVERIFY(saveProject(project, path, &error)); loaded = loadProject(path, &error); QVERIFY(loaded);
        project.characters[0].colors.append({id(), "Skin", CharacterColorCategory::Skin, Qt::red}); QCOMPARE(project.toJson()["version"].toInt(), 28); QVERIFY(saveProject(project, path, &error)); loaded = loadProject(path, &error); QVERIFY(loaded);
        auto downgraded = fixture().toJson(); downgraded["version"] = 28; QVERIFY(!Project::fromJson(downgraded, &error)); QVERIFY(error.contains("29"));
        auto missing = fixture().toJson(); missing.remove("drawingAids"); QVERIFY(!Project::fromJson(missing, &error));
        auto future = fixture().toJson(); future["version"] = latestProjectFormatVersion + 1; QVERIFY(!Project::fromJson(future, &error));
        project.drawingAids.activeShotId = project.shots[0].id; project.drawingAids.activeFrame = 20; QCOMPARE(project.toJson()["version"].toInt(), 28);
    }
    void missingDeletedSourcesStillPreservePortableSnapshotsAndDrafts() {
        auto project = fixture(); project.drawingAids.lightTable.pins[0].shotId = id(); project.drawingAids.lightTable.pins[0].layerId = id();
        project.drawingAids.shadingDrafts[0].shotId = id(); project.drawingAids.shadingDrafts[0].outlineLayerId = id();
        QString error; QTemporaryDir temp; const auto path = temp.filePath("missing-source.toonproj"); QVERIFY2(saveProject(project, path, &error), qPrintable(error));
        const auto loaded = loadProject(path, &error); QVERIFY2(loaded, qPrintable(error)); QVERIFY(drawingAidsEqual(loaded->drawingAids, project.drawingAids));
    }
    void malformedPinMetadataIsRejectedBeforeAllocatingImagePixels() {
        const auto original = fixture().toJson(); const auto pin = original["drawingAids"].toObject()["lightTable"].toObject()["pins"].toArray()[0].toObject(); QString error;
        for (const auto &key : {"scale", "opacity", "rotation", "width", "height"}) { auto bad = pin; bad[key] = "invalid"; QVERIFY(!Project::fromJson(withPin(original, bad), &error)); }
        for (const auto &[key, value] : QVector<QPair<QString, QJsonValue>>{{"width", 2049}, {"height", 0}, {"scale", 0}, {"opacity", 1.1}, {"rotation", 361}, {"visible", 1}, {"id", "not-id"}, {"name", "A\nB"}, {"unexpected", true}}) {
            auto bad = pin; bad[key] = value; QVERIFY2(!Project::fromJson(withPin(original, bad), &error), qPrintable(key));
        }
        auto bad = pin; bad["offset"] = QJsonArray{1}; QVERIFY(!Project::fromJson(withPin(original, bad), &error));
        bad = pin; bad["bounds"] = QJsonArray{0, 0, -10, 20}; QVERIFY(!Project::fromJson(withPin(original, bad), &error));
        bad = pin; bad["shotId"] = ""; QVERIFY(!Project::fromJson(withPin(original, bad), &error));
        auto aid = original["drawingAids"].toObject(); auto light = aid["lightTable"].toObject(); light["selectedPinId"] = id(); aid["lightTable"] = light; QVERIFY(!Project::fromJson(withAids(original, aid), &error));
        light = original["drawingAids"].toObject()["lightTable"].toObject(); auto pins = light["pins"].toArray(); pins.append(pin); light["pins"] = pins; aid["lightTable"] = light; QVERIFY(!Project::fromJson(withAids(original, aid), &error));
        auto project = fixture(); project.drawingAids.lightTable.pins[0].id = project.shots[0].id; QVERIFY(!project.validate().isEmpty());
    }
    void malformedDraftOperationsEnumsColorsAndCurvesAreRejected() {
        const auto original = fixture().toJson(); const auto draft = original["drawingAids"].toObject()["shadingDrafts"].toArray()[0].toObject(); QString error;
        for (const auto &[key, value] : QVector<QPair<QString, QJsonValue>>{{"frame", -1}, {"mode", 2}, {"kind", -1}, {"finish", 2}, {"width", 13}, {"shadow", "red"}, {"highlight", "#aabbcc"}, {"unexpected", true}}) {
            auto bad = draft; bad[key] = value; QVERIFY2(!Project::fromJson(withDraft(original, bad), &error), qPrintable(key));
        }
        auto bad = draft; bad["operations"] = QJsonArray{true, true}; QVERIFY(!Project::fromJson(withDraft(original, bad), &error));
        bad = draft; bad["operations"] = QJsonArray{false}; QVERIFY(!Project::fromJson(withDraft(original, bad), &error));
        auto boundaries = draft["boundaries"].toArray(); auto boundary = boundaries[0].toObject();
        for (const auto &points : QVector<QJsonArray>{QJsonArray{QJsonArray{0, 1, 1}, QJsonArray{2, 2, 2}}, QJsonArray{QJsonArray{1, 1, 1}, QJsonArray{1, 2, 2}}, QJsonArray{QJsonArray{0, 1, 1}, QJsonArray{3, 2, 2}}}) {
            boundary["path"] = points; boundaries[0] = boundary; bad = draft; bad["boundaries"] = boundaries; QVERIFY(!Project::fromJson(withDraft(original, bad), &error));
        }
        auto fills = draft["fills"].toArray(); auto fill = fills[0].toObject(); fill["seed"] = QJsonArray{12.5, 20}; fills[0] = fill; bad = draft; bad["fills"] = fills; QVERIFY(!Project::fromJson(withDraft(original, bad), &error));
        auto project = fixture(); project.drawingAids.shadingDrafts.append(project.drawingAids.shadingDrafts[0]); QVERIFY(!project.validate().isEmpty());
        project = fixture(); project.drawingAids.shadingDrafts[0].shadow = QColor(); QVERIFY(!project.validate().isEmpty());
    }
    void imageChecksumsDimensionsMissingBlobsAndTrailingDataAreRejected() {
        const auto project = fixture(); const auto manifest = project.toJson(); const auto blob = png(project.drawingAids.lightTable.pins[0].image);
        QString error; QTemporaryDir temp; const auto path = temp.filePath("damaged.toonproj");
        QVERIFY(write(path, container(manifest))); QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("snapshot"));
        auto bytes = container(manifest, {blob}); bytes[bytes.size() - 1] ^= char(1); QVERIFY(write(path, bytes)); QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("checksum"));
        bytes = container(manifest, {blob}); bytes.chop(8); QVERIFY(write(path, bytes)); QVERIFY(!loadProject(path, &error));
        QVERIFY(write(path, container(manifest, {png(image({10, 10}))}))); QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("dimensions"));
        QVERIFY(write(path, container(manifest, {blob}) + "extra")); QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("trailing"));
        QImage deep(32, 24, QImage::Format_RGBA64); deep.fill(Qt::red); QVERIFY(write(path, container(manifest, {png(deep)}))); QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("8-bit"));
    }
    void pathBudgetsAreEnforcedAcrossDrafts() {
        auto project = fixture(); auto &draft = project.drawingAids.shadingDrafts[0];
        QPainterPath path(QPointF(0, 0));
        for (int i = 1; i < 32768; ++i) path.lineTo(i, i % 2);
        draft.boundaries.clear(); draft.fills.clear(); draft.operations.clear();
        // Each boundary is legal on its own, but their aggregate must stay bounded.
        for (int i = 0; i < 31; ++i) { draft.boundaries.append({path, CelShadeKind::Shadow, Qt::blue, 2}); draft.operations.append(false); }
        QVERIFY(project.validate().contains("path-element limit"));
        QString error; QVERIFY(!Project::fromJson(project.toJson(), &error));
        draft.boundaries.resize(1); draft.operations.resize(1);
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        path.lineTo(32768, 0); draft.boundaries[0].path = path;
        QVERIFY(!project.validate().isEmpty()); QVERIFY(!Project::fromJson(project.toJson(), &error));
    }
    void budgetsAndCancelledSaveDoNotReplaceTheExistingFile() {
        auto project = fixture(); const auto original = project; QString error; QTemporaryDir temp; const auto path = temp.filePath("safe.toonproj");
        QVERIFY(saveProject(project, path, &error));
        for (int i = 0; i < 8; ++i) { auto pin = original.drawingAids.lightTable.pins[0]; pin.id = id(); project.drawingAids.lightTable.pins.append(pin); }
        QVERIFY(!project.validate().isEmpty()); QVERIFY(!Project::fromJson(project.toJson(), &error)); QVERIFY(!saveProject(project, path, &error));
        project = original; project.drawingAids.lightTable.pins.clear();
        for (int i = 0; i < 5; ++i) { auto pin = original.drawingAids.lightTable.pins[0]; pin.id = id(); pin.image = image({2048, 2048}); project.drawingAids.lightTable.pins.append(pin); }
        project.drawingAids.lightTable.selectedPinId.clear(); QVERIFY(project.validate().contains("64 MiB")); QVERIFY(!Project::fromJson(project.toJson(), &error));
        project = original; project.drawingAids.lightTable.pins[0].scale = std::numeric_limits<double>::infinity(); QVERIFY(!project.validate().isEmpty());
        project = original; for (int i = 0; i < 64; ++i) { auto draft = original.drawingAids.shadingDrafts[0]; draft.frame = 20 + i; project.drawingAids.shadingDrafts.append(draft); }
        QVERIFY(project.validate().contains("64")); QVERIFY(!Project::fromJson(project.toJson(), &error));
        int checks = 0; QVERIFY(!saveProject(original, path, &error, [&] { return ++checks >= 4; }));
        const auto loaded = loadProject(path, &error); QVERIFY2(loaded, qPrintable(error)); QVERIFY(drawingAidsEqual(loaded->drawingAids, original.drawingAids));
    }
};
QTEST_MAIN(DrawingAidsStorageTest)
#include "test_drawing_aids_storage.moc"
