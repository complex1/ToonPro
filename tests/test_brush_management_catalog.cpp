#include "tool_catalog.h"
#include "tool_catalog_legacy_fixture.h"

#include <QBuffer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <limits>

using namespace toon;

class BrushManagementCatalogTests : public QObject {
    Q_OBJECT
    static QJsonObject read(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject{};
    }
    static bool write(const QString &path, const QJsonObject &object) {
        QFile file(path);
        const auto bytes = QJsonDocument(object).toJson();
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
    static ToolState stateFor(BrushType type) {
        ToolState state{brushSettingsForType(type), brushTypeSmoothing(type), {}};
        if (type == BrushType::Stamp) {
            state.brush.tipMask = QImage(12, 8, QImage::Format_Grayscale8);
            state.brush.tipMask.fill(200);
        } else if (type == BrushType::ColoredStamp) {
            state.brush.colorTip = QImage(12, 8, QImage::Format_ARGB32_Premultiplied);
            state.brush.colorTip.fill(QColor(230, 50, 80, 128));
        }
        return state;
    }
private slots:
    void createsIndependentTypedBrushesWithoutChangingTheDrawingTool() {
        QTemporaryDir directory;
        BrushLibrary library(directory.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, directory.filePath("tools.json")); QVERIFY(catalog.load());
        const auto original = catalog.activeToolId();
        const auto originalEntry = catalog.selectedEntryId();
        QSignalSpy activation(&catalog, &ToolCatalog::activationRequested);
        for (int value = int(BrushType::Pen); value <= int(BrushType::Custom); ++value) {
            const auto type = BrushType(value);
            const auto state = stateFor(type);
            QString error; QVERIFY2(validateBrushSettings(state.brush, &error), qPrintable(error));
            const auto id = catalog.createBrush(" " + brushTypeName(type) + " ", type, state,
                                                " Effects ", {" Favorite ", "favorite", "Anime"}, "group/drawing");
            QVERIFY(!id.isEmpty());
            const auto *brush = catalog.tool(id); QVERIFY(brush);
            QCOMPARE(brush->engine, Canvas::Tool::Brush);
            QCOMPARE(brush->brushType, type);
            QCOMPARE(brush->state, state);
            QCOMPARE(brush->name, brushTypeName(type));
            QCOMPARE(brush->category, QString("Effects"));
            QCOMPARE(brush->tags, (QStringList{"Favorite", "Anime"}));
            QVERIFY(brush->brushPresetId.isEmpty());
            QVERIFY(catalog.entry("group/drawing")->tools.contains(id));
            QCOMPARE(catalog.activeToolId(), original);
            QCOMPARE(catalog.selectedEntryId(), originalEntry);
            catalog.history()->undo(); QVERIFY(!catalog.tool(id));
            catalog.history()->redo(); QVERIFY(catalog.tool(id));
        }
        QCOMPARE(activation.count(), 0);
        const auto id = catalog.createBrush("Unplaced pen", BrushType::Pen, stateFor(BrushType::Pen));
        QVERIFY(!id.isEmpty());
        for (const auto &entry : catalog.entries()) QVERIFY(!entry.tools.contains(id));
        const auto duplicate = catalog.duplicateTool(id, "group/drawing");
        QVERIFY(!duplicate.isEmpty());
        QCOMPARE(catalog.tool(duplicate)->brushType, BrushType::Pen);
        QVERIFY(catalog.setBrushCategory({duplicate}, "Inking"));
        QVERIFY(catalog.tool(id)->category.isEmpty());
        QVERIFY(brushSettingsForType(BrushType::Soft).hardness < .2);
        QVERIFY(brushSettingsForType(BrushType::Spray).scatter > 0);
        QVERIFY(brushSettingsForType(BrushType::Spray).count > 1);
    }

    void deletingBrushesRemovesEveryPlacementAndRespectsPendingArtwork() {
        QTemporaryDir directory;
        BrushLibrary library(directory.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, directory.filePath("tools.json")); QVERIFY(catalog.load());
        const QString ink = "brush/builtin/ink";
        const auto group = catalog.addGroup("Favorites", "brush", {ink, "brush/builtin/sketch"});
        const auto standalone = catalog.addStandalone(ink); QVERIFY(!standalone.isEmpty());
        auto state = catalog.tool(ink)->state; state.brush.size = 71;
        QVERIFY(catalog.updateToolState(ink, state));
        const auto history = catalog.history()->index();
        catalog.setActivationGuard([] { return false; });
        QVERIFY(!catalog.deleteBrushes({ink}));
        QVERIFY(catalog.tool(ink)); QVERIFY(catalog.entry(standalone));
        QCOMPARE(catalog.history()->index(), history);
        catalog.setActivationGuard({});
        QVERIFY(catalog.deleteBrushes({ink, "brush/builtin/sketch"}));
        QVERIFY(!catalog.tool(ink)); QVERIFY(!catalog.tool("brush/builtin/sketch"));
        QVERIFY(!catalog.entry(standalone));
        QVERIFY(catalog.entry(group)); QVERIFY(catalog.entry(group)->tools.isEmpty());
        for (const auto &entry : catalog.entries()) QVERIFY(!entry.tools.contains(ink));
        QVERIFY(catalog.activeToolId() != ink);
        QCOMPARE(catalog.history()->index(), history + 1);
        catalog.history()->undo();
        QVERIFY(catalog.tool(ink)); QVERIFY(catalog.entry(standalone));
        QCOMPARE(catalog.activeToolId(), ink);
        QCOMPARE(catalog.tool(ink)->state.brush.size, qreal(71));
        catalog.history()->redo(); QVERIFY(!catalog.tool(ink));
        // Built-in/import source copies stay intact; only their editor definitions
        // and placements are deleted, and tombstones prevent automatic resurrection.
        QVERIFY(std::any_of(library.presets().cbegin(), library.presets().cend(),
            [](const BrushPreset &preset) { return preset.id == "builtin/ink"; }));
        QVERIFY(catalog.save());
        ToolCatalog restored(&library, catalog.path()); QVERIFY(restored.load());
        QVERIFY(!restored.tool(ink)); QVERIFY(!restored.tool("brush/builtin/sketch"));
        BrushPreset imported; imported.id = "new-import"; imported.name = "Imported soft pen";
        imported.brush.hardness = .1;
        QVERIFY(library.addPresets({imported}));
        QVERIFY(restored.tool("brush/new-import"));
        QCOMPARE(restored.tool("brush/new-import")->brushType, BrushType::Soft);
        QCOMPARE(restored.tool("brush/new-import")->category, QString("Imported"));
        QVERIFY(!restored.tool(ink)); QVERIFY(!catalog.tool(ink));
        QVERIFY(restored.save()); QVERIFY(restored.load()); QVERIFY(!restored.tool(ink));
    }

    void batchCategoryChangesPreserveMixedTypesSettingsAndNavigation() {
        QTemporaryDir directory;
        BrushLibrary library(directory.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, directory.filePath("tools.json")); QVERIFY(catalog.load());
        const auto pen = catalog.createBrush("Pen", BrushType::Pen, stateFor(BrushType::Pen), "Characters");
        const auto spray = catalog.createBrush("Dust", BrushType::Spray, stateFor(BrushType::Spray), "Effects");
        const auto penState = catalog.tool(pen)->state;
        const auto sprayState = catalog.tool(spray)->state;
        const auto active = catalog.activeToolId();
        catalog.setActivationGuard([] { return false; });
        const auto history = catalog.history()->index();
        QVERIFY(catalog.setBrushCategory({pen, spray}, " Production "));
        QCOMPARE(catalog.history()->index(), history + 1);
        QCOMPARE(catalog.tool(pen)->category, QString("Production"));
        QCOMPARE(catalog.tool(spray)->category, QString("Production"));
        QCOMPARE(catalog.tool(pen)->brushType, BrushType::Pen);
        QCOMPARE(catalog.tool(spray)->brushType, BrushType::Spray);
        catalog.history()->undo();
        QCOMPARE(catalog.tool(pen)->category, QString("Characters"));
        QCOMPARE(catalog.tool(spray)->category, QString("Effects"));
        catalog.history()->redo();
        QVERIFY(catalog.setBrushMetadata({pen, spray}, BrushType::Custom, "Custom"));
        QCOMPARE(catalog.tool(pen)->state, penState);
        QCOMPARE(catalog.tool(spray)->state, sprayState);
        QCOMPARE(catalog.activeToolId(), active);
        QVERIFY(catalog.setToolTags({pen, spray}, {"Atmosphere"}));
        const auto unchanged = catalog.history()->index();
        QVERIFY(catalog.setBrushCategory({pen, spray}, "Custom"));
        QCOMPARE(catalog.history()->index(), unchanged);
        QVERIFY(catalog.save());
        ToolCatalog restored(&library, catalog.path()); QVERIFY(restored.load());
        QCOMPARE(restored.tool(pen)->brushType, BrushType::Custom);
        QCOMPARE(restored.tool(spray)->category, QString("Custom"));
        QCOMPARE(restored.tool(spray)->tags, QStringList{"Atmosphere"});
    }

    void coloredStampsPersistAlphaAndShareResources() {
        QTemporaryDir directory;
        BrushLibrary library(directory.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, directory.filePath("tools.json")); QVERIFY(catalog.load());
        QVERIFY(catalog.save());const auto initialResources=read(catalog.path())["resources"].toObject().size();
        auto state = stateFor(BrushType::ColoredStamp);
        state.brush.colorTip = QImage(512, 20, QImage::Format_ARGB32_Premultiplied);
        state.brush.colorTip.fill(Qt::transparent);
        state.brush.colorTip.setPixelColor(7, 9, QColor(220, 70, 30, 160));
        state.brush.colorTip.setPixelColor(111, 4, QColor(40, 180, 200, 255));
        const auto id = catalog.createBrush("Petals", BrushType::ColoredStamp, state, "Nature", {"Flowers"});
        QVERIFY(!id.isEmpty());
        const auto copy = catalog.duplicateTool(id, "group/drawing"); QVERIFY(!copy.isEmpty());
        QVERIFY(catalog.save());
        QCOMPARE(read(catalog.path())["resources"].toObject().size(), initialResources+1);
        ToolCatalog restored(&library, catalog.path());
        QString error; QVERIFY2(restored.load(&error), qPrintable(error));
        QCOMPARE(restored.tool(id)->state.brush.colorTip, state.brush.colorTip);
        QCOMPARE(restored.tool(copy)->state.brush.colorTip, state.brush.colorTip);
        QCOMPARE(restored.tool(id)->state.brush.colorTip.format(), QImage::Format_ARGB32_Premultiplied);
        QCOMPARE(restored.tool(id)->brushType, BrushType::ColoredStamp);
        QCOMPARE(restored.tool(id)->category, QString("Nature"));
        QCOMPARE(restored.tool(id)->state.brush.colorTip.pixelColor(0, 0).alpha(), 0);
    }

    void largeColoredStampEncodesBeyondTheGrayscaleLimit() {
        QTemporaryDir directory;
        BrushLibrary library(directory.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, directory.filePath("tools.json")); QVERIFY(catalog.load());
        auto state = stateFor(BrushType::ColoredStamp);
        state.brush.colorTip = QImage(2048, 1536, QImage::Format_ARGB32_Premultiplied);
        quint32 random = 0x12345678;
        const auto sample = [&random] {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            return random;
        };
        for (int y = 0; y < state.brush.colorTip.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(state.brush.colorTip.scanLine(y));
            for (int x = 0; x < state.brush.colorTip.width(); ++x) {
                const int alpha = 128 + int(sample() & 127);
                row[x] = qRgba(int(sample() & 255) * alpha / 255, int(sample() & 255) * alpha / 255,
                              int(sample() & 255) * alpha / 255, alpha);
            }
        }
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(state.brush.colorTip.save(&buffer, "PNG"));
        QVERIFY(png.size() > 6 * 1024 * 1024); QVERIFY(png.size() <= 17 * 1024 * 1024);
        const auto id = catalog.createBrush("Large color tip", BrushType::ColoredStamp, state);
        QVERIFY(!id.isEmpty());
        QString error; QVERIFY2(catalog.save(&error), qPrintable(error));
        ToolCatalog restored(&library, catalog.path()); QVERIFY2(restored.load(&error), qPrintable(error));
        QCOMPARE(restored.tool(id)->state.brush.colorTip, state.brush.colorTip);
    }

    void resourceBudgetRejectsCreationBeforeChangingTheCatalog() {
        QTemporaryDir directory;
        BrushLibrary library(directory.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, directory.filePath("tools.json")); QVERIFY(catalog.load());
        // Reserve the entire image budget for this boundary fixture, independent
        // of additional textured brushes shipped in the starter pack.
        QStringList sampled;
        for(const auto &tool:catalog.tools())if(!tool.state.brush.tipMask.isNull() || !tool.state.brush.texture.pattern.isNull()
            || !tool.state.brush.dual.tipMask.isNull() || !tool.state.brush.colorTip.isNull()
            || !tool.state.brush.surface.image.isNull())sampled.append(tool.id);
        if(!sampled.isEmpty())QVERIFY(catalog.deleteBrushes(sampled));
        auto state = stateFor(BrushType::ColoredStamp);
        state.brush.colorTip = QImage(2048, 2048, QImage::Format_ARGB32_Premultiplied);
        for (int i = 0; i < 4; ++i) {
            state.brush.colorTip.fill(QColor(30 + i * 40, 70, 130));
            QVERIFY(!catalog.createBrush(QString("Color %1").arg(i), BrushType::ColoredStamp, state).isEmpty());
        }
        const auto count = catalog.tools().size(); const auto history = catalog.history()->index();
        QSignalSpy warnings(&catalog, &ToolCatalog::warning);
        state.brush.colorTip.fill(Qt::cyan);
        QVERIFY(catalog.createBrush("Over budget", BrushType::ColoredStamp, state).isEmpty());
        QCOMPARE(catalog.tools().size(), count); QCOMPARE(catalog.history()->index(), history);
        QCOMPARE(warnings.count(), 1); QVERIFY(warnings.first().first().toString().contains("image limit"));
        QString error; QVERIFY2(catalog.save(&error), qPrintable(error));
        ToolCatalog restored(&library, catalog.path()); QVERIFY2(restored.load(&error), qPrintable(error));
        QCOMPARE(restored.tools().size(), count);
    }

    void oldCatalogMigrationInfersTypesWithoutResettingCustomSettings() {
        QTemporaryDir directory;
        BrushLibrary library(directory.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, directory.filePath("tools.json")); QVERIFY(catalog.load());
        auto state = catalog.tool("brush/builtin/sketch")->state;
        state.brush.scatter = 2.7; state.brush.count = 6; state.brush.size = 37;
        state.smoothing = 51;
        QVERIFY(catalog.updateToolState("brush/builtin/sketch", state));
        QVERIFY(catalog.setToolTags({"brush/builtin/sketch"}, {"My favorites"}));
        QVERIFY(catalog.renameTool("brush/builtin/sketch", "Atmosphere"));
        QVERIFY(catalog.activateTool("brush/builtin/sketch"));
        QVERIFY(catalog.save());
        auto root = test_fixture::toolCatalogBefore14(read(catalog.path()), 5); root.remove("deletedBrushPresets");
        QJsonArray tools, entries;
        QSet<QString> referenced;
        // The pre-management catalog predates the expanded native pack and its
        // pressure curves/RGBA tips. Keep this fixture in its actual old schema.
        for (const auto &value : root["tools"].toArray()) {
            auto tool = value.toObject();
            if (tool["brushPresetId"].toString().startsWith("builtin/catalog/")) continue;
            tool.remove("brushType"); tool.remove("category");
            for (const auto *key : {"tip", "texture", "dualTip"})
                if (tool.contains(key)) referenced.insert(tool[key].toString());
            tools.append(tool);
        }
        for (const auto &value : root["entries"].toArray()) {
            const auto entry = value.toObject();
            if (!entry["id"].toString().startsWith("group/catalog/")) entries.append(entry);
        }
        QJsonObject resources;
        const auto originalResources = root["resources"].toObject();
        for (const auto &id : referenced) resources[id] = originalResources[id];
        root["tools"] = tools; root["entries"] = entries; root["resources"] = resources;
        QVERIFY(write(catalog.path(), root));
        ToolCatalog restored(&library, catalog.path()); QString error; QVERIFY2(restored.load(&error), qPrintable(error));
        const auto *brush = restored.tool("brush/builtin/sketch"); QVERIFY(brush);
        QCOMPARE(brush->state, state); QCOMPARE(brush->name, QString("Atmosphere"));
        QCOMPARE(brush->tags, QStringList{"My favorites"}); QCOMPARE(brush->brushType, BrushType::Spray);
        QCOMPARE(restored.tool("brush/builtin/ink")->brushType, BrushType::Ink);
        QCOMPARE(restored.activeToolId(), QString("brush/builtin/sketch"));
        QCOMPARE(restored.history()->count(), 0);
        QVERIFY(restored.save()); QCOMPARE(read(catalog.path())["version"].toInt(), 14);
    }

    void rejectedEditsAreAtomicAndReportCreationFailures() {
        QTemporaryDir directory;
        BrushLibrary library(directory.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, directory.filePath("tools.json")); QVERIFY(catalog.load());
        const auto count = catalog.tools().size();
        QSignalSpy warnings(&catalog, &ToolCatalog::warning);
        QVERIFY(catalog.createBrush(" ", BrushType::Pen, stateFor(BrushType::Pen)).isEmpty());
        QVERIFY(catalog.createBrush("Invalid", BrushType(99), stateFor(BrushType::Pen)).isEmpty());
        QVERIFY(catalog.createBrush("Missing stamp", BrushType::Stamp, {}).isEmpty());
        QVERIFY(catalog.createBrush("Missing color", BrushType::ColoredStamp, {}).isEmpty());
        QVERIFY(catalog.createBrush("Bad category", BrushType::Pen, {}, "two\nlines").isEmpty());
        QVERIFY(catalog.createBrush("Too long", BrushType::Pen, {}, QString(81, 'x')).isEmpty());
        QVERIFY(catalog.createBrush("Bad tags", BrushType::Pen, {}, {}, {"a,b"}).isEmpty());
        QVERIFY(catalog.createBrush("Missing group", BrushType::Pen, {}, {}, {}, "missing").isEmpty());
        QVERIFY(catalog.createBrush("Standalone", BrushType::Pen, {}, {}, {}, "entry/hand").isEmpty());
        auto state = stateFor(BrushType::Pen); state.smoothing = std::numeric_limits<qreal>::quiet_NaN();
        QVERIFY(catalog.createBrush("Invalid settings", BrushType::Pen, state).isEmpty());
        state = stateFor(BrushType::ColoredStamp); state.brush.colorTip.fill(Qt::transparent);
        QVERIFY(catalog.createBrush("Empty color", BrushType::ColoredStamp, state).isEmpty());
        state = stateFor(BrushType::Stamp); state.brush.tipMask.fill(0);
        QVERIFY(catalog.createBrush("Empty stamp", BrushType::Stamp, state).isEmpty());
        QCOMPARE(warnings.count(), 12);
        for (const auto &warning : warnings) QVERIFY(!warning.first().toString().isEmpty());
        QVERIFY(!catalog.deleteBrushes({"brush/builtin/ink", "line"}));
        QVERIFY(!catalog.deleteBrushes({"brush/builtin/ink", "brush/builtin/ink"}));
        QVERIFY(!catalog.deleteBrushes({"missing"}));
        QVERIFY(!catalog.setBrushMetadata({"brush/builtin/ink"}, BrushType(-1), ""));
        QVERIFY(!catalog.setBrushCategory({"brush/builtin/ink", "line"}, "Invalid"));
        QVERIFY(!catalog.setBrushCategory({"brush/builtin/ink"}, "two\nlines"));
        QCOMPARE(catalog.tools().size(), count);
        QCOMPARE(catalog.history()->count(), 0);
    }

    void malformedManagementDataPreservesExistingCatalog() {
        QTemporaryDir directory;
        BrushLibrary library(directory.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, directory.filePath("tools.json")); QVERIFY(catalog.load());
        QVERIFY(catalog.save()); const auto original = read(catalog.path());
        for (int variant = 0; variant < 7; ++variant) {
            auto root = original; auto tools = root["tools"].toArray(); auto tool = tools[0].toObject();
            if (variant == 0) tool["brushType"] = 99;
            if (variant == 1) tool["brushType"] = .5;
            if (variant == 2) tool["category"] = QString(81, 'x');
            if (variant == 3) root["deletedBrushPresets"] = "not an array";
            if (variant == 4) root["deletedBrushPresets"] = QJsonArray{"removed", "removed"};
            if (variant == 5) root["deletedBrushPresets"] = QJsonArray{"builtin/ink"};
            if (variant == 6) tool["colorTip"] = "missing image";
            tools[0] = tool; root["tools"] = tools; QVERIFY(write(catalog.path(), root));
            QString error; QVERIFY(!catalog.load(&error)); QVERIFY(!error.isEmpty());
            QVERIFY(catalog.tool("brush/builtin/ink")); QVERIFY(!catalog.writable());
            QVERIFY(!catalog.save()); QCOMPARE(read(catalog.path()), root);
        }
    }
};

QTEST_MAIN(BrushManagementCatalogTests)
#include "test_brush_management_catalog.moc"
