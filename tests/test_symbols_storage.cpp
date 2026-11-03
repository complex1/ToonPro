#include "animation_test_fixture.h"
#include "document.h"
#include "vector_brush.h"
#include "vector_brush_storage.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

using namespace toon;
namespace {
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QImage pixels(QColor color, bool mask = false) {
    QImage result(tileSize, tileSize, mask ? QImage::Format_Grayscale8 : QImage::Format_ARGB32_Premultiplied);
    result.fill(color); return result;
}
SymbolDefinition definition(const QString &name = "Blink") {
    return {id(), name, Project::create().shots[0]};
}
RasterLayer instance(const SymbolDefinition &symbol) {
    auto layer = RasterLayer::create(symbol.name); layer.kind = LayerKind::Symbol;
    layer.symbol = SymbolInstance{symbol.id}; return layer;
}
Project fixture() {
    auto project = Project::create(); project.width = project.height = 128;
    auto symbol = definition(); symbol.content.durationFrames = 12;
    symbol.content.layers[0].tiles.insert({0, 0}, pixels(QColor(27, 87, 126, 177)));
    symbol.content.layers[0].effects.colorEnabled = true; symbol.content.layers[0].effects.saturation = -30;
    symbol.content.layers[0].mask = LayerMask{};
    symbol.content.layers[0].mask->tiles.insert({0, 0}, pixels(QColor(175, 175, 175), true));
    project.symbols.append(symbol); project.shots[0].layers.append(instance(symbol)); return project;
}
EditableObject sampledStroke() {
    auto object = EditableObject::create(); object.nodes = {BezierNode::corner({20, 25}), BezierNode::corner({100, 90})};
    object.strokeWidth = 15; object.stroke = QColor("#476582");
    VectorBrushStroke capture; capture.settings.size = 15; capture.settings.color = object.stroke;
    capture.settings.tipMask = QImage(19, 17, QImage::Format_Grayscale8); capture.settings.tipMask.fill(0);
    for (int y = 2; y < 15; ++y) for (int x = 3; x < 16; ++x) capture.settings.tipMask.scanLine(y)[x] = uchar(50 + x * y % 190);
    capture.settings.texture.enabled = true; capture.settings.texture.pattern = capture.settings.tipMask;
    capture.seed = 179; capture.canvasSize = {128, 128}; capture.sourceGeometry = editableGeometrySignature(object);
    for (int i = 0; i < 12; ++i) {
        VectorBrushSample sample; sample.dabIndex = i; sample.point = {20 + i * 80. / 11, 25 + i * 65. / 11};
        sample.position = i / 11.; sample.input.pressure = .3 + i * .05;
        sample.direction = 39; sample.initialDirection = 39; capture.samples.append(sample);
    }
    object.brushStroke = capture; return object;
}
QJsonObject withInstance(QJsonObject manifest, QJsonValue data) {
    auto shots = manifest["shots"].toArray(); auto shot = shots[0].toObject(); auto layers = shot["layers"].toArray();
    auto layer = layers.last().toObject(); layer["symbol"] = data; layers[layers.size() - 1] = layer;
    shot["layers"] = layers; shots[0] = shot; manifest["shots"] = shots; return manifest;
}
}

class SymbolsStorageTest final : public QObject {
    Q_OBJECT
private slots:
    void definitionsAreStoredOnceAndReferencesKeepIndependentPlayback() {
        auto project = fixture(); auto second = instance(project.symbols[0]);
        second.symbol->playback = SymbolPlayback::PingPong; second.symbol->startFrame = 7;
        second.symbol->firstFrame = 3; second.symbol->speed = .5; second.opacity = .6;
        project.shots[0].layers.append(second);
        auto shot = Project::create().shots[0]; shot.number = "C002"; shot.layers.append(instance(project.symbols[0]));
        project.shots.append(shot);
        project.shots[0].layerAnimations.append({second.id, {64, 64}, {{KeyframeProperty::PositionX, {{0, 5}, {10, 30}}}}, {}});
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        const auto manifest = project.toJson(); QCOMPARE(manifest["version"].toInt(), 31);
        QCOMPARE(manifest["shots"].toArray().size(), 2); QCOMPARE(manifest["symbols"].toArray().size(), 1);
        QCOMPARE(projectArtworkShots(project).size(), 3);
        const auto &constant = project; QCOMPARE(projectArtworkShots(constant).size(), 3);
        QTemporaryDir directory; QString error; const auto path = directory.filePath("symbols.toon");
        QVERIFY2(saveProject(project, path, &error), qPrintable(error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.read(10), QByteArray("TOONPRO31\n")); file.close();
        const auto loaded = loadProject(path, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->toJson(), manifest); QCOMPARE(loaded->symbols.size(), 1); QCOMPARE(loaded->shots.size(), 2);
        QCOMPARE(loaded->symbols[0].content.layers[0].tiles, project.symbols[0].content.layers[0].tiles);
        QCOMPARE(loaded->symbols[0].content.layers[0].mask, project.symbols[0].content.layers[0].mask);
        QCOMPARE(loaded->symbols[0].content.layers[0].effects, project.symbols[0].content.layers[0].effects);
        QCOMPARE(loaded->shots[0].layers.last().symbol, second.symbol);
        QCOMPARE(loaded->shots[0].layerAnimations, project.shots[0].layerAnimations);
        auto sourceOnly = project; sourceOnly.shots[0].layers.removeLast(); sourceOnly.shots[0].layerAnimations.clear(); sourceOnly.shots.removeLast();
        const auto smaller = directory.filePath("one-instance.toon"); QVERIFY(saveProject(sourceOnly, smaller, &error));
        // More instances add metadata only; they never duplicate the embedded PNGs.
        QVERIFY(QFileInfo(path).size() - QFileInfo(smaller).size() < 8192);
    }
    void animatedNestedDefinitionsAndSharedBrushImagesRoundTrip() {
        auto project = fixture(); auto animated = definition("Walk"); animated.content = animation_fixture::project().shots[0];
        auto vector = RasterLayer::create("Inks"); vector.kind = LayerKind::Vector; vector.objects.append(sampledStroke());
        QString error; QVERIFY2(rebuildObjectLayer(vector, {-120, -120, 368, 368}, maxProjectTiles, &error), qPrintable(error));
        animated.content.layers.append(vector);
        project.symbols[0].content.layers.append(instance(animated)); project.symbols.append(animated);
        auto copy = vector; copy.id = id(); copy.objects[0].id = id(); project.shots[0].layers.append(copy);
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        QCOMPARE(project.toJson()["brushResources"].toArray().size(), 1);
        QTemporaryDir directory; const auto path = directory.filePath("nested.toon");
        QVERIFY2(saveProject(project, path, &error), qPrintable(error)); const auto loaded = loadProject(path, &error);
        QVERIFY2(loaded, qPrintable(error)); QCOMPARE(loaded->toJson(), project.toJson());
        QCOMPARE(loaded->symbols[1].content.animationTracks, animated.content.animationTracks);
        QCOMPARE(loaded->symbols[1].content.layers.last().tiles, vector.tiles);
        QCOMPARE(loaded->symbols[1].content.layers.last().objects[0].brushStroke->settings.tipMask,
                 vector.objects[0].brushStroke->settings.tipMask);
        QCOMPARE(loaded->shots[0].layers.last().objects[0].brushStroke->settings.tipMask,
                 vector.objects[0].brushStroke->settings.tipMask);
        auto changed = project; changed.symbols[1].content.layers.last().objects[0].nodes[0].anchor += QPointF(5, 0);
        QVERIFY(editableObjectHistoryCost(project, changed) > 0);
    }
    void mixedCharacterLibrariesKeepRequiredModernMetadata() {
        auto project = fixture(); project.characters.append({id(), "Hana", {}});
        QTemporaryDir directory; QString error; const auto path = directory.filePath("characters.toon");
        QVERIFY2(saveProject(project, path, &error), qPrintable(error)); auto loaded = loadProject(path, &error);
        QVERIFY2(loaded, qPrintable(error)); QCOMPARE(loaded->toJson(), project.toJson());
        QVERIFY(loaded->characters[0].colors.isEmpty()); QVERIFY(loaded->characters[0].colorVariants.isEmpty());
    }
    void oldFormatsRemainUnchangedAndDisguisedSymbolsAreRejected() {
        const auto project = Project::create(); QString error;
        QCOMPARE(project.toJson()["version"].toInt(), 19); QVERIFY(!project.toJson().contains("symbols"));
        const auto loaded = Project::fromJson(project.toJson(), &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->toJson(), project.toJson()); QVERIFY(loaded->symbols.isEmpty());
        auto metadata = fixture().toJson(); metadata["version"] = 30;
        QVERIFY(!Project::fromJson(metadata, &error)); QVERIFY(error.contains("31"));
        metadata.remove("symbols"); QVERIFY(!Project::fromJson(metadata, &error)); QVERIFY(error.contains("31"));
        metadata = fixture().toJson(); metadata.remove("symbols"); QVERIFY(!Project::fromJson(metadata, &error));
    }
    void missingReferencesCyclesAndExcessiveNestingAreRejected() {
        QString error; auto project = fixture(); project.shots[0].layers.last().symbol->symbolId = id();
        QVERIFY(project.validate().contains("missing")); QVERIFY(!Project::fromJson(project.toJson(), &error));
        project = fixture(); project.symbols[0].content.layers.append(instance(project.symbols[0]));
        QVERIFY(project.validate().contains("cycles")); QVERIFY(!Project::fromJson(project.toJson(), &error));
        project = fixture();
        for (int i = 1; i < maxSymbolNestingDepth; ++i) {
            auto next = definition(QString("Nested %1").arg(i));
            project.symbols.last().content.layers.append(instance(next)); project.symbols.append(next);
        }
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        auto fifth = definition("Too deep"); project.symbols.last().content.layers.append(instance(fifth)); project.symbols.append(fifth);
        QVERIFY(project.validate().contains("four")); QVERIFY(!Project::fromJson(project.toJson(), &error));
    }
    void malformedPlaybackAndUnsupportedDefinitionDataAreRejected() {
        auto project = fixture(); const auto manifest = project.toJson(); QString error;
        const auto original = manifest["shots"].toArray()[0].toObject()["layers"].toArray().last().toObject()["symbol"].toObject();
        for (const auto key : {"symbolId", "playback", "startFrame", "firstFrame", "speed"}) {
            auto data = original; data.remove(key); QVERIFY(!Project::fromJson(withInstance(manifest, data), &error));
        }
        for (const auto value : QList<QJsonValue>{-1, 1.5, 1000001, "zero"}) {
            auto data = original; data["startFrame"] = value; QVERIFY(!Project::fromJson(withInstance(manifest, data), &error));
        }
        for (const auto value : QList<QJsonValue>{0, -.1, 100.1, "fast"}) {
            auto data = original; data["speed"] = value; QVERIFY(!Project::fromJson(withInstance(manifest, data), &error));
        }
        auto data = original; data["playback"] = "reverse"; QVERIFY(!Project::fromJson(withInstance(manifest, data), &error));
        data = original; data["firstFrame"] = 12; QVERIFY(!Project::fromJson(withInstance(manifest, data), &error));
        project.symbols[0].content.references.append({id(), "Forbidden reference", pixels(Qt::red)});
        QVERIFY(project.validate().contains("references")); QVERIFY(!Project::fromJson(project.toJson(), &error));
        project = fixture(); project.symbols[0].content.camera2D.zoom = 2;
        QVERIFY(!project.validate().isEmpty()); QVERIFY(!Project::fromJson(project.toJson(), &error));
        project = fixture(); project.shots[0].layers.last().tiles.insert({0, 0}, pixels(Qt::red));
        QVERIFY(project.validate().contains("independent artwork"));
    }
    void limitsIncludeDefinitionArtworkAndFailedSavesPreserveTheFile() {
        const auto original = fixture(); auto project = original; QString error; QTemporaryDir directory;
        const auto path = directory.filePath("safe.toon"); QVERIFY(saveProject(project, path, &error));
        while (project.symbols.size() <= maxProjectSymbols) project.symbols.append(definition());
        QVERIFY(project.validate().contains("128")); QVERIFY(!Project::fromJson(project.toJson(), &error));
        QVERIFY(!saveProject(project, path, &error)); auto loaded = loadProject(path, &error);
        QVERIFY2(loaded, qPrintable(error)); QCOMPARE(loaded->toJson(), original.toJson());
        project = original; const auto tile = pixels(Qt::red);
        for (int i = 0; i < maxProjectTiles; ++i) project.symbols[0].content.layers[0].tiles.insert({i % 100, i / 100}, tile);
        project.shots[0].layers[0].tiles.insert({0, 0}, tile);
        QVERIFY(project.validate().contains("1024-tile")); QVERIFY(!saveProject(project, path, &error));
        project = original; auto &mask = *project.symbols[0].content.layers[0].mask; const auto maskPixel = pixels(Qt::white, true);
        for (int i = 0; i < maxProjectMaskTiles; ++i) mask.tiles.insert({i % 100, i / 100}, maskPixel);
        project.shots[0].layers[0].mask = LayerMask{}; project.shots[0].layers[0].mask->tiles.insert({0, 0}, maskPixel);
        QVERIFY(project.validate().contains("1024-mask")); QVERIFY(!Project::fromJson(project.toJson(), &error));
        project = original; project.symbols[0].id = project.shots[0].id;
        QVERIFY(!project.validate().isEmpty());
    }
    void drawingAndMasksRespectTheSharedLibraryBudget() {
        auto project = fixture(); auto &content = project.symbols[0].content.layers[0];
        const auto tile = pixels(Qt::red), mask = pixels(Qt::white, true);
        for (int i = 0; i < maxProjectTiles; ++i) content.tiles.insert({i % 100, i / 100}, tile);
        for (int i = 0; i < maxProjectMaskTiles; ++i) content.mask->tiles.insert({i % 100, i / 100}, mask);
        QCOMPARE(projectTileCount(project), maxProjectTiles); QCOMPARE(projectMaskTileCount(project), maxProjectMaskTiles);
        QCOMPARE(projectTileCount(project, content.id), 0); QCOMPARE(projectMaskTileCount(project, content.id), 0);
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        Document document; document.load(project); document.selectLayer(0);
        BrushSettings brush; brush.size = 10; brush.pressureSize = false;
        document.beginStroke({40, 40}, 1, brush, false); document.continueStroke({70, 40}, 1); document.endStroke();
        QCOMPARE(projectTileCount(document.project()), maxProjectTiles);
        QVERIFY(document.activeShot().layers[0].tiles.isEmpty());
        QPainterPath selection; selection.addRect(10, 10, 30, 30); document.setRasterSelection(selection);
        QVERIFY(!document.addLayerMask(true));
        QCOMPARE(projectMaskTileCount(document.project()), maxProjectMaskTiles);
        QVERIFY(!document.activeShot().layers[0].mask);
    }
};
QTEST_MAIN(SymbolsStorageTest)
#include "test_symbols_storage.moc"
