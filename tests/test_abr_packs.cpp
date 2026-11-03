#include "brush_library.h"
#include "brush_panel.h"
#include "document.h"
#include "tool_catalog.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

using namespace toon;

namespace {
Project drawingProject(LayerKind kind) {
    auto project = Project::create();
    project.width = 512;
    project.height = 384;
    project.transparent = true;
    project.shots[0].sheet.overscan = 0;
    project.shots[0].layers[0].kind = kind;
    return project;
}

const RasterLayer &paintedLayer(const Document &document) {
    return document.project().shots[0].layers[document.activeLayer()];
}

QByteArray fileDigest(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return hash.result();
}
}

// Optional compatibility coverage for locally supplied packs. No downloaded
// artwork is copied into the repository, and every writable file is temporary.
// TOON_ABR_PACKS_DIR selects a folder of .abr files. The two optional expected
// count variables make known-pack checks strict without hardcoding user paths.
class AbrPackTests : public QObject {
    Q_OBJECT
private slots:
    void localPacksImportPaintAndPersist() {
        const auto directory = qEnvironmentVariable("TOON_ABR_PACKS_DIR");
        if (directory.isEmpty()) QSKIP("Set TOON_ABR_PACKS_DIR to test local Photoshop brush packs.");
        const QDir packs(directory);
        QVERIFY2(packs.exists(), qPrintable("Brush pack directory does not exist: " + directory));
        const auto files = packs.entryInfoList({"*.abr", "*.ABR"}, QDir::Files, QDir::Name);
        QVERIFY2(!files.isEmpty(), "No .abr files found in TOON_ABR_PACKS_DIR.");
        const int expectedPacks = qEnvironmentVariableIntValue("TOON_ABR_EXPECTED_PACKS");
        const int expectedPerPack = qEnvironmentVariableIntValue("TOON_ABR_EXPECTED_BRUSHES_PER_PACK");
        if (expectedPacks > 0) QCOMPARE(files.size(), expectedPacks);
        QMap<QString, QByteArray> sourceDigests;
        for (const auto &file : files) {
            const auto digest = fileDigest(file.absoluteFilePath());
            QVERIFY2(!digest.isEmpty(), qPrintable("Cannot read brush pack: " + file.fileName()));
            sourceDigests.insert(file.absoluteFilePath(), digest);
        }

        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto libraryPath = temporary.filePath("brushes.json");
        const auto catalogPath = temporary.filePath("tools.json");
        BrushLibrary library(libraryPath);
        QString error;
        QVERIFY2(library.load(&error), qPrintable(error));
        ToolCatalog catalog(&library, catalogPath);
        QVERIFY2(catalog.load(&error), qPrintable(error));
        const auto initialPresetCount = library.presets().size();
        const auto initialToolCount = catalog.tools().size();
        QSignalSpy catalogWarnings(&catalog, &ToolCatalog::warning);
        QStringList importedIds;
        for (const auto &file : files) {
            QString report;
            QStringList ids;
            QVERIFY2(importAbrIntoLibrary(&library, file.absoluteFilePath(), &report, &error, &ids),
                     qPrintable(file.fileName() + ": " + error));
            QVERIFY(!ids.isEmpty());
            QVERIFY(!report.isEmpty());
            if (expectedPerPack > 0) QCOMPARE(ids.size(), expectedPerPack);
            qInfo().noquote() << QString("Imported %1 sampled tips from %2").arg(ids.size()).arg(file.fileName());
            importedIds += ids;
        }
        QCOMPARE(library.presets().size(), initialPresetCount + importedIds.size());
        QCOMPARE(catalog.tools().size(), initialToolCount + importedIds.size());
        QVERIFY2(catalogWarnings.isEmpty(), catalogWarnings.isEmpty() ? "" :
                 qPrintable(catalogWarnings.first().first().toString()));
        QVERIFY2(catalog.save(&error), qPrintable(error));

        BrushLibrary restoredLibrary(libraryPath);
        QVERIFY2(restoredLibrary.load(&error), qPrintable(error));
        ToolCatalog restoredCatalog(&restoredLibrary, catalogPath);
        QVERIFY2(restoredCatalog.load(&error), qPrintable(error));
        QCOMPARE(restoredLibrary.presets().size(), library.presets().size());
        QCOMPARE(restoredCatalog.tools().size(), catalog.tools().size());
        const auto *drawingGroup = restoredCatalog.entry("group/drawing");
        QVERIFY(drawingGroup);

        for (const auto &id : importedIds) {
            const auto original = std::find_if(library.presets().cbegin(), library.presets().cend(),
                [&](const auto &preset) { return preset.id == id; });
            const auto restored = std::find_if(restoredLibrary.presets().cbegin(), restoredLibrary.presets().cend(),
                [&](const auto &preset) { return preset.id == id; });
            QVERIFY(original != library.presets().cend());
            QVERIFY(restored != restoredLibrary.presets().cend());
            QCOMPARE(restored->brush.tipMask, original->brush.tipMask);
            QCOMPARE(restored->brush.tipMask.format(), QImage::Format_Grayscale8);
            QVERIFY(!restored->brush.tipMask.isNull());
            QVERIFY(restored->brush.tipMask.width() <= 1024);
            QVERIFY(restored->brush.tipMask.height() <= 1024);
            const auto *tool = restoredCatalog.tool("brush/" + id);
            QVERIFY(tool);
            QCOMPARE(tool->brushPresetId, id);
            QCOMPARE(tool->engine, Canvas::Tool::Brush);
            QCOMPARE(tool->state.brush.tipMask, original->brush.tipMask);
            QVERIFY(drawingGroup->tools.contains(tool->id));

            auto brush = tool->state.brush;
            brush.size = 96;
            brush.color = QColor("#a14768");
            Document raster, vector;
            raster.load(drawingProject(LayerKind::Raster));
            vector.load(drawingProject(LayerKind::Vector));
            QSignalSpy rasterWarnings(&raster, &Document::warning);
            QSignalSpy vectorWarnings(&vector, &Document::warning);
            for (auto *document : {&raster, &vector}) {
                QVERIFY2(document->beginStroke({160, 180}, .7, brush, false), qPrintable(original->name));
                document->continueStroke({240, 200}, 1);
                document->endStroke();
            }
            QVERIFY(rasterWarnings.isEmpty());
            QVERIFY(vectorWarnings.isEmpty());
            QVERIFY2(!paintedLayer(raster).tiles.isEmpty(), qPrintable(original->name));
            QCOMPARE(paintedLayer(vector).tiles, paintedLayer(raster).tiles);
            QCOMPARE(paintedLayer(vector).objects.size(), 1);
            const auto projectPath = temporary.filePath("painted-stroke.toonproj");
            QVERIFY2(saveProject(vector.project(), projectPath, &error), qPrintable(error));
            auto reopened = loadProject(projectPath, &error);
            QVERIFY2(reopened.has_value(), qPrintable(error));
            const auto &layer = reopened->shots[0].layers[0];
            QCOMPARE(layer.tiles, paintedLayer(vector).tiles);
            QVERIFY(layer.objects.first().brushStroke.has_value());
            QCOMPARE(layer.objects.first().brushStroke->settings.tipMask, original->brush.tipMask);
        }
        for (auto it = sourceDigests.cbegin(); it != sourceDigests.cend(); ++it)
            QCOMPARE(fileDigest(it.key()), it.value());
        qInfo() << importedIds.size() << "imported tools survived library/catalog reload, raster/vector painting and project reload.";
    }
};

QTEST_MAIN(AbrPackTests)
#include "test_abr_packs.moc"
