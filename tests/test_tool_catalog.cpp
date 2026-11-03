#include "tool_catalog.h"
#include "standard_brushes.h"
#include "catalog_brushes.h"
#include "tool_session.h"
#include "icons.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <utility>

using namespace toon;

class ToolCatalogTests : public QObject {
    Q_OBJECT
    static QByteArray read(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }
    static bool write(const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
    static QJsonObject legacyBrushResources(QJsonObject root) {
        // Historical schema fixtures must not carry newer pressure curves,
        // per-stroke jitter or RGBA tips merely because today's defaults do.
        auto tools = root["tools"].toArray();
        QSet<QString> references;
        for (int i = 0; i < tools.size(); ++i) {
            auto object = tools[i].toObject();
            BrushSettings brush;
            if (brushSettingsFromJson(object["brush"].toObject(), brush)) {
                brush.lockRandomSeed = false; brush.randomSeed = 1;
                brush.connected = {}; brush.surface = {}; brush.clusterSpray = {}; brush.speedDynamics = {};
                brush.colorDynamics.applyToColorTip = false; brush.colorDynamics.tintAmount = 0;
                brush.tipFrames.clear(); brush.size = qMin(brush.size,256.);
                for (auto *dynamic : {&brush.sizeDynamics, &brush.angleDynamics, &brush.roundnessDynamics,
                                     &brush.texture.dynamics, &brush.opacityDynamics, &brush.flowDynamics}) {
                    dynamic->response = BrushDynamic{}.response;
                    dynamic->jitterPerStroke = false;
                    if (dynamic->control == BrushControl::Rotation) dynamic->control = BrushControl::Off;
                }
                for (auto *control : {&brush.scatterControl, &brush.countControl, &brush.colorDynamics.control})
                    if (*control == BrushControl::Rotation) *control = BrushControl::Off;
                object["brush"] = brushSettingsToJson(brush);
            }
            object.remove("colorTip"); object.remove("surfaceImage");
            for (const auto *key : {"tip", "texture", "dualTip"})
                if (object.contains(key)) references.insert(object[key].toString());
            tools[i] = object;
        }
        for (const auto &value : root["entries"].toArray()) {
            const auto entry = value.toObject();
            if (entry.contains("customIcon")) references.insert(entry["customIcon"].toString());
        }
        QJsonObject resources;
        const auto originalResources = root["resources"].toObject();
        for (const auto &id : references) resources[id] = originalResources[id];
        root["tools"] = tools; root["resources"] = resources;
        return root;
    }
    static QJsonObject legacyCatalog(QJsonObject root, int version) {
        root["version"] = version;
        root.remove("deletedBrushPresets");
        for (const auto &field : {"tools", "entries"}) {
            auto values = root[field].toArray();
            for (int i = 0; i < values.size(); ++i) {
                auto object = values[i].toObject();
                object.remove("tags"); object.remove("color"); object.remove("customIcon");
                object.remove("brushType"); object.remove("category"); object.remove("colorTip"); values[i] = object;
            }
            root[field] = values;
        }
        return legacyBrushResources(root);
    }
    static QJsonObject schemaFour(QJsonObject root) {
        root["version"] = 4;
        root.remove("deletedBrushPresets");
        QJsonArray tools;
        for (const auto &value : root["tools"].toArray()) {
            auto object = value.toObject();
            object.remove("brushType"); object.remove("category"); object.remove("colorTip");
            if (object["id"].toString() != "scene-rig") tools.append(object);
        }
        root["tools"] = tools;
        auto entries = root["entries"].toArray();
        for (int i = 0; i < entries.size(); ++i) {
            auto entry = entries[i].toObject(); QJsonArray members;
            for (const auto &value : entry["tools"].toArray())
                if (value.toString() != "scene-rig") members.append(value);
            entry["tools"] = members; entries[i] = entry;
        }
        root["entries"] = entries;
        return legacyBrushResources(root);
    }
    static QJsonObject beforeCatalogPack(QJsonObject root) {
        QJsonArray tools, entries;
        for (const auto &value : root["tools"].toArray()) {
            if (!value.toObject()["brushPresetId"].toString().startsWith("builtin/catalog/"))
                tools.append(value);
        }
        for (const auto &value : root["entries"].toArray()) {
            auto entry = value.toObject();
            if (entry["id"].toString().startsWith("group/catalog/")) continue;
            QJsonArray ids;
            for (const auto &id : entry["tools"].toArray())
                if (!id.toString().startsWith("brush/builtin/catalog/")) ids.append(id);
            entry["tools"] = ids;
            entries.append(entry);
        }
        root["tools"] = tools;
        root["entries"] = entries;
        return root;
    }
    static QString addResource(QJsonObject &resources, const QImage &image) {
        QByteArray png;
        QBuffer buffer(&png);
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return {};
        const auto id = QString::fromLatin1(QCryptographicHash::hash(png, QCryptographicHash::Sha256).toHex());
        resources[id] = QString::fromLatin1(png.toBase64());
        return id;
    }
    static QJsonObject pruneResources(QJsonObject root) {
        const auto source = root["resources"].toObject();
        QJsonObject used;
        for (const auto &value : root["tools"].toArray()) {
            const auto object = value.toObject();
            for (const auto *field : {"tip", "colorTip", "texture", "dualTip", "surfaceImage"})
                if (object.contains(field)) used[object[field].toString()] = source[object[field].toString()];
        }
        for (const auto &value : root["entries"].toArray()) {
            const auto object = value.toObject();
            if (object.contains("customIcon")) used[object["customIcon"].toString()] = source[object["customIcon"].toString()];
        }
        root["resources"] = used;
        return root;
    }
    static QJsonObject shippedCatalogRecipes(QJsonObject root, int revision = 1) {
        const auto originals = catalogBrushPresets(revision);
        auto resources = root["resources"].toObject();
        auto tools = root["tools"].toArray();
        for (int i = 0; i < tools.size(); ++i) {
            auto object = tools[i].toObject();
            const auto found = std::find_if(originals.cbegin(), originals.cend(), [&](const BrushPreset &preset) {
                return object["id"].toString() == "brush/" + preset.id && object["brushPresetId"].toString() == preset.id;
            });
            if (found != originals.cend()) {
                if(revision==1)object.remove("builtinRecipeRevision");else object["builtinRecipeRevision"]=revision;
                object["brush"] = brushSettingsToJson(found->brush);
                object["smoothing"] = found->smoothing;
                const std::pair<const char *, const QImage *> images[]{
                    {"tip", &found->brush.tipMask}, {"colorTip", &found->brush.colorTip},
                    {"texture", &found->brush.texture.pattern}, {"dualTip", &found->brush.dual.tipMask}, {"surfaceImage", &found->brush.surface.image}};
                for (const auto &[field, image] : images) {
                    object.remove(field);
                    if (!image->isNull()) object[field] = addResource(resources, *image);
                }
            }
            tools[i] = object;
        }
        root["tools"] = tools;
        root["resources"] = resources;
        return pruneResources(root);
    }
    static BrushPreset recipe(const QString &suffix, int revision = catalogBrushRecipeRevision) {
        for (const auto &preset : catalogBrushPresets(revision))
            if (preset.id == "builtin/catalog/" + suffix) return preset;
        return {};
    }
private slots:
    void semanticDefaultsUpgradeWithoutChangingCustomArtworkOrToolIdentity() {
        QTemporaryDir dir; ToolCatalog catalog(nullptr, dir.filePath("tools.json")); QVERIFY(catalog.load());
        QCOMPARE(catalog.tool("arc")->icon, featureIconName(FeatureIcon::Arc));
        QCOMPARE(catalog.tool("curve")->icon, featureIconName(FeatureIcon::Bezier));
        QVERIFY(catalog.tool("arc")->icon != catalog.tool("curve")->icon);
        const auto copied = catalog.duplicateTool("arc", "group/shapes", "My custom arch"); QVERIFY(!copied.isEmpty());
        QVERIFY(catalog.renameTool("arc", "Storyboard arch"));
        QVERIFY(catalog.setToolTags({"arc"}, {"Favorites"}));
        QVERIFY(catalog.setToolColor({"arc"}, QColor("#d5b68c")));
        QImage custom(24,24,QImage::Format_ARGB32_Premultiplied); custom.fill(Qt::cyan);
        QVERIFY(catalog.setEntryIcon("group/guides", "grid-2x2", custom));
        QVERIFY(catalog.activateTool("arc")); QVERIFY(catalog.save());
        auto source = QJsonDocument::fromJson(read(catalog.path())).object();
        auto tools = source["tools"].toArray(); const int count = tools.size();
        for (int i=0; i<tools.size(); ++i) {
            auto tool = tools[i].toObject(); const auto id = tool["id"].toString();
            if (id=="arc" || id=="curve" || id==copied) tool["icon"]="spline";
            if (id=="warp") tool["icon"]="grid-2x2";
            if (id=="blur") tool["icon"]="sun"; // Explicit customization must survive.
            tools[i]=tool;
        }
        source["tools"]=tools; auto entries=source["entries"].toArray();
        for (int i=0; i<entries.size(); ++i) {
            auto entry=entries[i].toObject(); if(entry["id"]=="entry/warp") entry["icon"]="grid-2x2";
            entries[i]=entry;
        }
        source["entries"]=entries; QVERIFY(write(catalog.path(), QJsonDocument(source).toJson()));
        QVERIFY(catalog.load()); QCOMPARE(catalog.tools().size(), count); QCOMPARE(catalog.activeToolId(), QString("arc"));
        QCOMPARE(catalog.tool("arc")->icon, featureIconName(FeatureIcon::Arc));
        QCOMPARE(catalog.tool("curve")->icon, featureIconName(FeatureIcon::Bezier));
        QCOMPARE(catalog.tool("warp")->icon, featureIconName(FeatureIcon::Warp));
        QCOMPARE(catalog.entry("entry/warp")->icon, featureIconName(FeatureIcon::Warp));
        QCOMPARE(catalog.tool(copied)->icon, QString("spline")); QCOMPARE(catalog.tool("blur")->icon, QString("sun"));
        QCOMPARE(catalog.entry("group/guides")->icon, QString("grid-2x2"));
        QCOMPARE(catalog.entry("group/guides")->customIcon, custom);
        QCOMPARE(catalog.tool("arc")->name, QString("Storyboard arch"));
        QCOMPARE(catalog.tool("arc")->tags, QStringList{"Favorites"});
        QCOMPARE(catalog.tool("arc")->color, QColor("#d5b68c")); QCOMPARE(catalog.history()->count(),0);
        QVERIFY(catalog.save()); QVERIFY(catalog.load());
        QCOMPARE(catalog.tool(copied)->icon, QString("spline")); QCOMPARE(catalog.entry("group/guides")->customIcon, custom);
    }

    void seedsIndividualRasterToolsAndRestoresLastUsed() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        QVERIFY(catalog.writable());
        QCOMPARE(catalog.activeToolId(), QString("brush/builtin/ink"));
        for (const auto &preset : library.presets()) {
            const auto *tool = catalog.tool("brush/" + preset.id); QVERIFY(tool);
            QCOMPARE(tool->brushPresetId, preset.id); QCOMPARE(tool->engine, Canvas::Tool::Brush);
        }
        for (const auto &tool : catalog.tools()) {
            QVERIFY(tool.engine != Canvas::Tool::VectorPen);

        }
        QVERIFY(catalog.tool("path-edit"));
        QVERIFY(catalog.tool("simplify-brush"));
        QVERIFY(catalog.tool("width-brush"));
        QVERIFY(catalog.entry("group/vector-editing"));
        QVERIFY(catalog.tool("scene-rig"));
        QCOMPARE(catalog.tool("scene-rig")->engine, Canvas::Tool::SceneRig);
        QCOMPARE(catalog.entry("group/scene3d")->tools, (QStringList{"scene-object", "scene-camera", "scene-rig"}));
        QVERIFY(catalog.selectEntry("group/shapes"));
        QCOMPARE(catalog.activeToolId(), QString("line"));
        QVERIFY(catalog.activateTool("ellipse"));
        QVERIFY(catalog.selectEntry("group/color"));
        QVERIFY(catalog.selectEntry("group/shapes"));
        QCOMPARE(catalog.activeToolId(), QString("ellipse"));
        QCOMPARE(catalog.history()->count(), 0);
        QVERIFY(catalog.activateTool("warp"));
        QCOMPARE(catalog.tool(catalog.activeToolId())->variant, 1);
        QCOMPARE(catalog.visibleToolIds(), QStringList{"warp"});
    }

    void standaloneGroupsAndRemovalPreserveDefinitions() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        const int tools = int(catalog.tools().size());
        const auto standalone = catalog.addStandalone("rectangle"); QVERIFY(!standalone.isEmpty());
        QCOMPARE(catalog.visibleToolIds(), QStringList{"rectangle"});
        QVERIFY(!catalog.entry(standalone)->group);
        QVERIFY(!catalog.addTools(standalone, {"ellipse"}));
        const auto group = catalog.addGroup("Background", "image", {"rectangle", "brush/builtin/ink"});
        QVERIFY(!group.isEmpty());
        QVERIFY(catalog.removeTool(group, "rectangle"));
        QVERIFY(catalog.tool("rectangle"));
        QCOMPARE(catalog.entry(standalone)->tools, QStringList{"rectangle"});
        catalog.history()->undo();
        QCOMPARE(catalog.entry(group)->tools.size(), 2);
        QVERIFY(catalog.removeEntry(group));
        QCOMPARE(catalog.tools().size(), tools);
        catalog.history()->undo();
        QVERIFY(catalog.entry(group));
        QVERIFY(catalog.renameEntry(standalone, "Background Rectangle"));
        QCOMPARE(catalog.tool("rectangle")->name, QString("Background Rectangle"));
        QCOMPARE(catalog.entry(standalone)->name, QString("Background Rectangle"));
        catalog.history()->undo();
        QCOMPARE(catalog.tool("rectangle")->name, QString("Rectangle"));
    }

    void duplicateHasIndependentSettingsAndOneUndo() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        auto original = catalog.tool("brush/builtin/ink")->state;
        original.brush.size = 33;
        QVERIFY(catalog.updateToolState("brush/builtin/ink", original));
        const auto copy = catalog.duplicateTool("brush/builtin/ink", "group/drawing", "My Ink");
        QVERIFY(!copy.isEmpty()); QCOMPARE(catalog.history()->count(), 1);
        QCOMPARE(catalog.tool(copy)->name, QString("My Ink"));
        QCOMPARE(catalog.tool(copy)->state.brush.size, qreal(33));
        QVERIFY(catalog.tool(copy)->brushPresetId.isEmpty());
        auto edited = catalog.tool(copy)->state; edited.brush.size = 72;
        QVERIFY(catalog.updateToolState(copy, edited));
        QCOMPARE(catalog.tool("brush/builtin/ink")->state.brush.size, qreal(33));
        catalog.history()->undo();
        QVERIFY(!catalog.tool(copy));
        catalog.history()->redo();
        QCOMPARE(catalog.tool(copy)->state.brush.size, qreal(72));
        const auto standaloneCopy = catalog.duplicateTool(copy, {}, "Independent Ink");
        QVERIFY(!standaloneCopy.isEmpty());
        QCOMPARE(catalog.visibleToolIds(), QStringList{standaloneCopy});
        QCOMPARE(catalog.history()->count(), 2);
        QVERIFY(!catalog.entry(catalog.selectedEntryId())->group);
    }

    void capturePrecedesActivationAndOrganizationUndoKeepsSettings() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        QString captured;
        connect(&catalog, &ToolCatalog::aboutToActivate, &catalog, [&] {
            captured = catalog.activeToolId();
            if (captured == "brush/builtin/ink") {
                auto state = catalog.tool(captured)->state; state.brush.size = 81;
                catalog.updateToolState(captured, state);
            }
        });
        QSignalSpy activation(&catalog, &ToolCatalog::activationRequested);
        QVERIFY(catalog.selectEntry("group/shapes"));
        QCOMPARE(captured, QString("brush/builtin/ink"));
        QCOMPARE(activation.last().first().toString(), QString("line"));
        QCOMPARE(catalog.tool("brush/builtin/ink")->state.brush.size, qreal(81));
        const auto group = catalog.addGroup("Paint", "brush", {"brush/builtin/ink"});
        QVERIFY(!group.isEmpty());
        auto state = catalog.tool("line")->state; state.options["strokeWidth"] = 24;
        QVERIFY(catalog.updateToolState("line", state));
        catalog.history()->undo();
        QCOMPARE(catalog.tool("line")->state.options.value("strokeWidth").toInt(), 24);
        QCOMPARE(catalog.tool("brush/builtin/ink")->state.brush.size, qreal(81));
    }

    void sharedToolsAndEmptyGroupNavigation() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        const auto a = catalog.addGroup("A", "brush", {"brush/builtin/ink"});
        const auto b = catalog.addGroup("B", "brush", {"brush/builtin/ink"});
        auto settings = catalog.tool("brush/builtin/ink")->state; settings.brush.size = 29;
        settings.brush.color = Qt::red;
        QSignalSpy structure(&catalog, &ToolCatalog::changed);
        QSignalSpy stateChanges(&catalog, &ToolCatalog::settingsChanged);
        QVERIFY(catalog.updateToolState("brush/builtin/ink", settings));
        QCOMPARE(structure.count(), 0); QCOMPARE(stateChanges.count(), 1);
        QCOMPARE(catalog.tool("brush/builtin/ink")->state.brush.color, BrushSettings{}.color);
        QVERIFY(catalog.selectEntry(a));
        QCOMPARE(catalog.tool(catalog.activeToolId())->state.brush.size, qreal(29));
        QVERIFY(catalog.selectEntry(b));
        QCOMPARE(catalog.tool(catalog.activeToolId())->state.brush.size, qreal(29));
        QSignalSpy activation(&catalog, &ToolCatalog::activationRequested);
        const auto empty = catalog.addGroup("Empty", "folder-open");
        QVERIFY(!empty.isEmpty()); QCOMPARE(catalog.activeToolId(), QString("brush/builtin/ink")); QVERIFY(catalog.visibleToolIds().isEmpty());
        QCOMPARE(activation.count(), 0);
        QVERIFY(catalog.save());
        ToolCatalog restored(&library, catalog.path()); QVERIFY(restored.load());
        QCOMPARE(restored.selectedEntryId(), empty); QCOMPARE(restored.activeToolId(), QString("brush/builtin/ink"));
        QVERIFY(catalog.addTools(empty, {"rectangle"}));
        QCOMPARE(catalog.activeToolId(), QString("rectangle"));
        QVERIFY(catalog.removeTool(empty, "rectangle"));
        QVERIFY(!catalog.activeToolId().isEmpty()); QVERIFY(catalog.entry(empty));
        QVERIFY(catalog.selectedEntryId() != empty);
        while (!catalog.entries().isEmpty()) QVERIFY(catalog.removeEntry(catalog.entries().first().id));
        QVERIFY(catalog.activeToolId().isEmpty());
        QCOMPARE(activation.last().first().toString(), QString{});
        catalog.history()->undo(); // Restores the final empty group only.
        QVERIFY(catalog.activeToolId().isEmpty()); QVERIFY(catalog.entry(empty));
        catalog.history()->undo(); // Restores the previous populated group.
        QVERIFY(!catalog.activeToolId().isEmpty());
    }

    void roundTripIncludesResourcesOptionsAndSingletons() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        const auto path = dir.filePath("custom/tools.json");
        ToolCatalog catalog(&library, path); QVERIFY(catalog.load());
        QVERIFY(catalog.save());const auto initialResources=QJsonDocument::fromJson(read(path)).object()["resources"].toObject().size();
        auto settings = catalog.tool("brush/builtin/ink")->state;
        QImage stamp(4, 7, QImage::Format_Grayscale8); stamp.fill(142); stamp.scanLine(1)[2] = 255;
        settings.brush.tipMask = stamp; settings.brush.texture.pattern = stamp; settings.brush.dual.tipMask = stamp;
        settings.brush.texture.enabled = true; settings.brush.texture.scale = 1.72;
        settings.smoothing = 42; settings.options = {{"tolerance", 32}, {"fillColor", "#ff443355"}};
        QVERIFY(catalog.updateToolState("brush/builtin/ink", settings));
        const auto copy = catalog.duplicateTool("brush/builtin/ink", {}, "Sampled Ink");
        QVERIFY(!copy.isEmpty());
        const auto entry = catalog.selectedEntryId();
        QString error; QVERIFY2(catalog.save(&error), qPrintable(error));
        const auto root = QJsonDocument::fromJson(read(path)).object();
        QCOMPARE(root.value("resources").toObject().size(), initialResources+1);
        ToolCatalog loaded(&library, path); QVERIFY2(loaded.load(&error), qPrintable(error));
        QCOMPARE(loaded.selectedEntryId(), entry); QCOMPARE(loaded.activeToolId(), copy);
        QCOMPARE(loaded.visibleToolIds(), QStringList{copy});
        QCOMPARE(loaded.tool(copy)->state, settings);
        QCOMPARE(loaded.tool("brush/builtin/ink")->state, settings);
        QCOMPARE(loaded.history()->count(), 0);
        auto changed = settings; changed.brush.size = 117;
        QVERIFY(loaded.updateToolState(copy, changed));
        QSignalSpy reapplied(&loaded, &ToolCatalog::activationRequested);
        QVERIFY(loaded.load());
        QCOMPARE(loaded.tool(copy)->state.brush.size, settings.brush.size);
        QCOMPARE(reapplied.count(), 1);
        QCOMPARE(reapplied.first().first().toString(), copy);
    }

    void upgradesRasterCatalogOnceWithoutResettingOrganization() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        const auto path=dir.filePath("tools.json"); ToolCatalog original(&library,path); QVERIFY(original.load());
        QVERIFY(original.renameEntry("group/shapes","My shapes")); QVERIFY(original.activateTool("ellipse")); QVERIFY(original.save());
        auto root=legacyCatalog(QJsonDocument::fromJson(read(path)).object(), 1);
        QJsonArray tools,entries;
        for(const auto &v:root["tools"].toArray()) if(!QStringList{"path-edit","simplify-brush","width-brush","vector-eraser"}.contains(v.toObject()["id"].toString()))tools.append(v);
        for(const auto &v:root["entries"].toArray()) if(v.toObject()["id"].toString()!="group/vector-editing")entries.append(v);
        root["tools"]=tools;root["entries"]=entries;QVERIFY(write(path,QJsonDocument(root).toJson()));
        ToolCatalog upgraded(&library,path);QVERIFY(upgraded.load());QCOMPARE(upgraded.activeToolId(),QString("ellipse"));
        QCOMPARE(upgraded.entry("group/shapes")->name,QString("My shapes"));QVERIFY(upgraded.entry("group/vector-editing"));
        QVERIFY(upgraded.removeEntry("group/vector-editing"));QVERIFY(upgraded.save());
        ToolCatalog reopened(&library,path);QVERIFY(reopened.load());QVERIFY(!reopened.entry("group/vector-editing"));
        QVERIFY(reopened.tool("width-brush"));QCOMPARE(reopened.activeToolId(),QString("ellipse"));
    }

    void corruptFileProtectionAndRecovery() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        const auto path = dir.filePath("tools.json");
        ToolCatalog catalog(&library, path); QVERIFY(catalog.load()); QVERIFY(catalog.save());
        const auto good = read(path);
        const QByteArray broken("{\"tools\": false, bad json"); QVERIFY(write(path, broken));
        QString error; QVERIFY(!catalog.load(&error)); QVERIFY(!error.isEmpty()); QVERIFY(!catalog.writable());
        QVERIFY(catalog.addStandalone("rectangle").isEmpty());
        QVERIFY(!catalog.renameTool("rectangle", "Cannot persist"));
        QVERIFY(!catalog.removeEntry("group/shapes"));
        QVERIFY(!catalog.save(&error)); QCOMPARE(read(path), broken);
        QVERIFY(write(path, good)); QVERIFY(catalog.load()); QVERIFY(catalog.writable());
        QCOMPARE(catalog.activeToolId(), QString("brush/builtin/ink"));
        QVERIFY(catalog.save());
    }

    void rejectsDanglingReferencesAndUnknownFields() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        const auto path = dir.filePath("tools.json");
        ToolCatalog catalog(&library, path); QVERIFY(catalog.load()); QVERIFY(catalog.save());
        const auto original = QJsonDocument::fromJson(read(path)).object();
        for (int variant = 0; variant < 5; ++variant) {
            auto object = original;
            if (variant == 0) object["futureSetting"] = true;
            if (variant == 1) object["version"] = 999;
            if (variant == 2) { auto entries = object["entries"].toArray(); auto e = entries[0].toObject(); e["tools"] = QJsonArray{"missing"}; entries[0] = e; object["entries"] = entries; }
            if (variant == 3) { auto tools = object["tools"].toArray(); tools.push_back(tools[0]); object["tools"] = tools; }
            if (variant == 4) { auto tools = object["tools"].toArray(); auto t = tools[0].toObject(); t["tip"] = "missing"; tools[0] = t; object["tools"] = tools; }
            const auto corrupt = QJsonDocument(object).toJson(); QVERIFY(write(path, corrupt));
            QString error; QVERIFY(!catalog.load(&error)); QVERIFY(!error.isEmpty());
            QVERIFY(!catalog.save()); QCOMPARE(read(path), corrupt);
        }
    }

    void importAddsToolsWithoutResettingExistingSettings() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        auto state = catalog.tool("brush/builtin/ink")->state; state.brush.size = 57;
        QVERIFY(catalog.updateToolState("brush/builtin/ink", state));
        BrushPreset preset; preset.id = "imported"; preset.name = "Imported"; preset.brush.size = 26;
        QVERIFY(library.addPresets({preset}));
        QVERIFY(catalog.tool("brush/imported"));
        QVERIFY(catalog.entry("group/drawing")->tools.contains("brush/imported"));
        QCOMPARE(catalog.tool("brush/builtin/ink")->state.brush.size, qreal(57));
        QVERIFY(library.removePreset("imported"));
        QVERIFY(catalog.tool("brush/imported"));
        QVERIFY(catalog.save());
    }

    void shortcutLocatesPlacementsAndRestoresUnplacedTools() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        QVERIFY(catalog.activateTool("rectangle"));
        QCOMPARE(catalog.selectedEntryId(), QString("group/shapes"));
        QVERIFY(catalog.removeTool("group/shapes", "rectangle"));
        const auto before = catalog.history()->count();
        QVERIFY(catalog.activateTool("rectangle"));
        QCOMPARE(catalog.history()->count(), before + 1);
        QCOMPARE(catalog.visibleToolIds(), QStringList{"rectangle"});
        QVERIFY(!catalog.entry(catalog.selectedEntryId())->group);
        catalog.history()->undo();
        QVERIFY(catalog.tool("rectangle"));
        QVERIFY(catalog.activeToolId() != "rectangle");
    }

    void metadataBatchEditsNormalizeAndUndoWithoutActivatingTools() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        const QStringList ids{"brush/builtin/ink", "rectangle"};
        const auto initial = catalog.activeToolId();
        const auto initialState = catalog.tool(initial)->state;
        QSignalSpy activation(&catalog, &ToolCatalog::activationRequested);
        QSignalSpy settings(&catalog, &ToolCatalog::settingsChanged);
        int guards = 0;
        catalog.setActivationGuard([&] { ++guards; return false; });
        QVERIFY(catalog.setToolTags(ids, {"  Ink  ", "ink", "Backgrounds", ""}));
        QCOMPARE(catalog.tool(ids[0])->tags, (QStringList{"Ink", "Backgrounds"}));
        QCOMPARE(catalog.tool(ids[1])->tags, catalog.tool(ids[0])->tags);
        QCOMPARE(catalog.history()->count(), 1);
        QVERIFY(catalog.addToolTag(ids, " INK ")); QCOMPARE(catalog.history()->count(), 1);
        QVERIFY(catalog.addToolTag(ids, "Portraits")); QCOMPARE(catalog.history()->count(), 2);
        QVERIFY(catalog.removeToolTag(ids, " BACKGROUNDS "));
        QCOMPARE(catalog.tool(ids[0])->tags, (QStringList{"Ink", "Portraits"}));
        QVERIFY(catalog.setToolColor(ids, QColor(18, 91, 203, 27)));
        QCOMPARE(catalog.tool(ids[0])->color, QColor(18, 91, 203));
        QVERIFY(catalog.setEntryTags("group/drawing", {"Favorites", "favorites", " Anime "}));
        QVERIFY(catalog.setEntryColor("group/drawing", QColor(Qt::red)));
        const auto originalToolName = catalog.tool(ids[0])->name;
        QVERIFY(catalog.renameTool(ids[0], "My Ink"));
        QVERIFY(catalog.renameEntry("group/drawing", "My Drawing", "paintbrush"));
        QCOMPARE(catalog.entry("group/drawing")->tags, (QStringList{"Favorites", "Anime"}));
        QCOMPARE(guards, 0); QCOMPARE(activation.count(), 0); QCOMPARE(settings.count(), 0);
        QCOMPARE(catalog.activeToolId(), initial); QCOMPARE(catalog.tool(initial)->state, initialState);

        catalog.setActivationGuard({}); QVERIFY(catalog.activateTool("ellipse"));
        activation.clear();
        catalog.setActivationGuard([&] { ++guards; return false; });
        const auto selected = catalog.selectedEntryId();
        catalog.history()->setIndex(0);
        QVERIFY(catalog.tool(ids[0])->tags.isEmpty()); QVERIFY(!catalog.tool(ids[0])->color.isValid());
        QVERIFY(catalog.entry("group/drawing")->tags.isEmpty()); QVERIFY(!catalog.entry("group/drawing")->color.isValid());
        QCOMPARE(catalog.tool(ids[0])->name, originalToolName);
        QCOMPARE(catalog.entry("group/drawing")->name, QString("Drawing"));
        QCOMPARE(catalog.entry("group/drawing")->icon, QString("brush"));
        QCOMPARE(catalog.activeToolId(), QString("ellipse")); QCOMPARE(catalog.selectedEntryId(), selected);
        catalog.history()->setIndex(catalog.history()->count());
        QCOMPARE(catalog.tool(ids[0])->tags, (QStringList{"Ink", "Portraits"}));
        QCOMPARE(catalog.entry("group/drawing")->color, QColor(Qt::red));
        QCOMPARE(catalog.tool(ids[0])->name, QString("My Ink"));
        QCOMPARE(catalog.entry("group/drawing")->name, QString("My Drawing"));
        QCOMPARE(catalog.entry("group/drawing")->icon, QString("paintbrush"));
        QCOMPARE(guards, 0); QCOMPARE(activation.count(), 0); QCOMPARE(settings.count(), 0);
        QCOMPARE(catalog.activeToolId(), QString("ellipse")); QCOMPARE(catalog.selectedEntryId(), selected);
        QVERIFY(catalog.setToolColor(ids, {})); QVERIFY(!catalog.tool(ids[0])->color.isValid());
        QVERIFY(catalog.setEntryColor("group/drawing", {})); QVERIFY(!catalog.entry("group/drawing")->color.isValid());
    }

    void metadataValidationAndMembershipReplacementAreAtomic() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        QVERIFY(catalog.setToolTags({"rectangle"}, {"Keep"}));
        QStringList full;
        for (int i = 0; i < ToolCatalog::maxTags; ++i) full.append(QString::number(i));
        QVERIFY(catalog.setToolTags({"line"}, full));
        const auto before = catalog.history()->count();
        QVERIFY(!catalog.setToolTags({"rectangle", "missing"}, {"Lost"}));
        QVERIFY(!catalog.setToolTags({"rectangle", "rectangle"}, {"Lost"}));
        QVERIFY(!catalog.setToolTags({"rectangle"}, {QString(ToolCatalog::maxTagLength + 1, 'x')}));
        QVERIFY(!catalog.setToolTags({"rectangle"}, {QString(QChar(1)) + "sentinel"}));
        QVERIFY(!catalog.setToolTags({"rectangle"}, {"Ink, Paint"}));
        QVERIFY(!catalog.addToolTag({"rectangle", "line"}, "Two, tags"));
        QVERIFY(!catalog.setEntryTags("group/shapes", {"Anime, backgrounds"}));
        QVERIFY(!catalog.setToolTags({}, {"Lost"}));
        QVERIFY(!catalog.addToolTag({"rectangle", "line"}, "Over limit"));
        QVERIFY(!catalog.setEntryTags("group/shapes", full + QStringList{"Over limit"}));
        QVERIFY(!catalog.setEntryTags("entry/hand", {"Standalone"}));
        QVERIFY(!catalog.setEntryColor("entry/hand", Qt::blue));
        QVERIFY(!catalog.setEntryIcon("entry/hand", "brush"));
        QVERIFY(!catalog.setEntryTools("group/shapes", {"rectangle", "missing"}));
        QVERIFY(!catalog.setEntryTools("group/shapes", {"rectangle", "rectangle"}));
        QCOMPARE(catalog.history()->count(), before);
        QCOMPARE(catalog.tool("rectangle")->tags, QStringList{"Keep"}); QCOMPARE(catalog.tool("line")->tags, full);
        QVERIFY(catalog.activateTool("rectangle"));
        const auto original = catalog.entry("group/shapes")->tools;
        catalog.setActivationGuard([] { return false; });
        QVERIFY(!catalog.setEntryTools("group/shapes", {"line", "ellipse"}));
        QCOMPARE(catalog.entry("group/shapes")->tools, original); QCOMPARE(catalog.activeToolId(), QString("rectangle"));
        QVERIFY(catalog.setEntryTools("group/shapes", {"rectangle", "ellipse"}));
        QCOMPARE(catalog.activeToolId(), QString("rectangle"));
        QCOMPARE(catalog.history()->count(), before + 1);
        catalog.history()->undo(); QCOMPARE(catalog.entry("group/shapes")->tools, original);
        catalog.history()->redo(); QCOMPARE(catalog.entry("group/shapes")->tools, (QStringList{"rectangle", "ellipse"}));
        catalog.setActivationGuard({});
        QVERIFY(catalog.setEntryTools("group/shapes", {"line", "ellipse"}));
        QCOMPARE(catalog.activeToolId(), QString("line"));
        QVERIFY(catalog.setEntryTools("group/shapes", {})); QVERIFY(catalog.entry("group/shapes")->tools.isEmpty());
        QVERIFY(!catalog.activeToolId().isEmpty()); QVERIFY(catalog.selectedEntryId() != "group/shapes");
    }

    void metadataAndSharedColorIconsPersistAndDuplicate() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        const auto path = dir.filePath("tools.json");
        ToolCatalog catalog(&library, path); QVERIFY(catalog.load());
        QVERIFY(catalog.save());const auto initialResources=QJsonDocument::fromJson(read(path)).object()["resources"].toObject().size();
        QVERIFY(catalog.setToolTags({"brush/builtin/ink"}, {"Line art", "Favorite"}));
        QVERIFY(catalog.setToolColor({"brush/builtin/ink"}, QColor("#ce5939")));
        QVERIFY(catalog.setEntryTags("group/drawing", {"Production"}));
        QVERIFY(catalog.setEntryColor("group/drawing", QColor("#6633aa")));
        QImage image(512, 256, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
        for (int y = 40; y < 160; ++y) for (int x = 50; x < 220; ++x) image.setPixelColor(x, y, QColor(120, 30, 210, 160));
        image.setText("Do not persist", QString(3000, 'x'));
        QVERIFY(catalog.setEntryIcon("group/drawing", "brush", image));
        QCOMPARE(catalog.entry("group/drawing")->customIcon.size(), QSize(256, 128));
        QVERIFY(catalog.entry("group/drawing")->customIcon.textKeys().isEmpty());
        QVERIFY(catalog.setEntryIcon("group/shapes", "circle", catalog.entry("group/drawing")->customIcon));
        const auto copy = catalog.duplicateTool("brush/builtin/ink", "group/drawing"); QVERIFY(!copy.isEmpty());
        QCOMPARE(catalog.tool(copy)->tags, catalog.tool("brush/builtin/ink")->tags);
        QCOMPARE(catalog.tool(copy)->color, catalog.tool("brush/builtin/ink")->color);
        QVERIFY(catalog.setToolTags({copy}, {"Independent"}));
        QCOMPARE(catalog.tool("brush/builtin/ink")->tags, (QStringList{"Line art", "Favorite"}));
        QString error; QVERIFY2(catalog.save(&error), qPrintable(error));
        auto root = QJsonDocument::fromJson(read(path)).object();
        QCOMPARE(root.value("version").toInt(), 12); QCOMPARE(root.value("resources").toObject().size(), initialResources+1);
        ToolCatalog restored(&library, path); QVERIFY2(restored.load(&error), qPrintable(error));
        QCOMPARE(restored.tool(copy)->tags, QStringList{"Independent"});
        QCOMPARE(restored.tool(copy)->color, QColor("#ce5939"));
        QCOMPARE(restored.entry("group/drawing")->tags, QStringList{"Production"});
        QCOMPARE(restored.entry("group/drawing")->color, QColor("#6633aa"));
        QCOMPARE(restored.entry("group/drawing")->customIcon, catalog.entry("group/drawing")->customIcon);
        QCOMPARE(restored.entry("group/drawing")->customIcon.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(restored.entry("group/shapes")->customIcon, restored.entry("group/drawing")->customIcon);
        QVERIFY(restored.setEntryIcon("group/drawing", "palette")); QVERIFY(restored.entry("group/drawing")->customIcon.isNull());
        restored.history()->undo(); QVERIFY(!restored.entry("group/drawing")->customIcon.isNull());
        restored.history()->redo(); QVERIFY(restored.entry("group/drawing")->customIcon.isNull());
    }

    void schemaTwoMigratesWithoutResetAndMalformedMetadataDoesNotLoad() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        const auto path = dir.filePath("tools.json");
        ToolCatalog catalog(&library, path); QVERIFY(catalog.load());
        QVERIFY(catalog.renameEntry("group/shapes", "My shapes"));
        QVERIFY(catalog.removeEntry("group/vector-editing"));
        QVERIFY(catalog.activateTool("ellipse")); QVERIFY(catalog.save());
        auto root = legacyCatalog(QJsonDocument::fromJson(read(path)).object(), 2);
        QVERIFY(write(path, QJsonDocument(root).toJson()));
        QVERIFY(catalog.load()); QCOMPARE(catalog.activeToolId(), QString("ellipse"));
        QCOMPARE(catalog.entry("group/shapes")->name, QString("My shapes"));
        QVERIFY(!catalog.entry("group/vector-editing"));
        QVERIFY(catalog.tool("ellipse")->tags.isEmpty()); QVERIFY(!catalog.tool("ellipse")->color.isValid());
        QVERIFY(catalog.setToolTags({"ellipse"}, {"Keep"})); QVERIFY(catalog.save());
        root = QJsonDocument::fromJson(read(path)).object();
        QCOMPARE(root.value("version").toInt(), 10);
        for (int variant = 0; variant < 8; ++variant) {
            auto broken = root;
            auto tools = broken["tools"].toArray(); auto tool = tools[0].toObject();
            auto entries = broken["entries"].toArray(); auto group = entries[0].toObject();
            if (variant == 0) tool["tags"] = "not a list";
            if (variant == 1) tool["tags"] = QJsonArray{123};
            if (variant == 2) tool["tags"] = QJsonArray{QString(41, 'x')};
            if (variant == 3) tool["color"] = "red";
            if (variant == 4) tool["color"] = "#ff112233";
            if (variant == 5) group["customIcon"] = "missing";
            if (variant == 6) group["tags"] = QJsonArray{QString("bad\ntag")};
            if (variant == 7) broken["version"] = 99;
            tools[0] = tool; entries[0] = group; broken["tools"] = tools; broken["entries"] = entries;
            const auto bytes = QJsonDocument(broken).toJson(); QVERIFY(write(path, bytes));
            QString error; QVERIFY(!catalog.load(&error)); QVERIFY(!error.isEmpty());
            QCOMPARE(catalog.activeToolId(), QString("ellipse")); QCOMPARE(catalog.tool("ellipse")->tags, QStringList{"Keep"});
            QVERIFY(!catalog.writable()); QVERIFY(!catalog.setToolTags({"ellipse"}, {"Lost"}));
            QVERIFY(!catalog.save()); QCOMPARE(read(path), bytes);
        }
    }

    void schemaFourAddsRigToCustomizedSceneGroupOnce() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        const auto path = dir.filePath("tools.json");
        ToolCatalog catalog(&library, path); QVERIFY(catalog.load());
        QVERIFY(catalog.renameEntry("group/scene3d", "My reference room", "video"));
        QVERIFY(catalog.setEntryTags("group/scene3d", {"Backgrounds", "Favorites"}));
        QVERIFY(catalog.setEntryColor("group/scene3d", QColor("#a36acc")));
        QImage icon(24, 24, QImage::Format_ARGB32_Premultiplied); icon.fill(QColor("#f96c30"));
        QVERIFY(catalog.setEntryIcon("group/scene3d", "video", icon));
        QVERIFY(catalog.setToolTags({"scene-camera"}, {"My camera"}));
        QVERIFY(catalog.setToolColor({"scene-camera"}, QColor("#3d91f0")));
        QVERIFY(catalog.activateTool("scene-camera")); QVERIFY(catalog.save());
        auto root = schemaFour(QJsonDocument::fromJson(read(path)).object());
        auto entries = root["entries"].toArray();
        for (int i = 0; i < entries.size(); ++i) {
            auto entry = entries[i].toObject();
            if (entry["id"].toString() == "group/scene3d") { entry["id"] = "group/my-room"; entries[i] = entry; }
        }
        root["entries"] = entries; root["selectedEntryId"] = "group/my-room";
        QVERIFY(write(path, QJsonDocument(root).toJson()));
        QString error; QVERIFY2(catalog.load(&error), qPrintable(error));
        QCOMPARE(catalog.entries().size(), entries.size());
        const auto *group = catalog.entry("group/my-room"); QVERIFY(group);
        QCOMPARE(group->name, QString("My reference room")); QCOMPARE(group->icon, QString("video"));
        QCOMPARE(group->tags, (QStringList{"Backgrounds", "Favorites"})); QCOMPARE(group->color, QColor("#a36acc"));
        QCOMPARE(group->customIcon, icon);
        QCOMPARE(group->tools, (QStringList{"scene-object", "scene-camera", "scene-rig"}));
        QCOMPARE(group->lastToolId, QString("scene-camera"));
        QCOMPARE(catalog.selectedEntryId(), QString("group/my-room")); QCOMPARE(catalog.activeToolId(), QString("scene-camera"));
        QCOMPARE(catalog.tool("scene-camera")->tags, QStringList{"My camera"});
        QCOMPARE(catalog.tool("scene-camera")->color, QColor("#3d91f0"));
        QCOMPARE(catalog.history()->count(), 0);
        const auto count = catalog.tools().size(); QVERIFY(catalog.save());
        QCOMPARE(QJsonDocument::fromJson(read(path)).object()["version"].toInt(), 10);
        QVERIFY2(catalog.load(&error), qPrintable(error)); QCOMPARE(catalog.tools().size(), count);
        QCOMPARE(catalog.entry("group/my-room")->tools.count("scene-rig"), 1);
        // Removing the new placement remains a deliberate customization on the
        // next launch; the one-time migration must not silently restore it.
        QVERIFY(catalog.removeTool("group/my-room", "scene-rig")); QVERIFY(catalog.save()); QVERIFY(catalog.load());
        QCOMPARE(catalog.tools().size(), count); QVERIFY(catalog.tool("scene-rig"));
        QVERIFY(!catalog.entry("group/my-room")->tools.contains("scene-rig"));
        QCOMPARE(catalog.activeToolId(), QString("scene-camera"));
    }

    void schemaFourRigMigrationRespectsCapacityAndExistingNames_data() {
        QTest::addColumn<bool>("sceneGroup"); QTest::addColumn<bool>("fullEntries");
        QTest::addColumn<bool>("fullTools"); QTest::addColumn<bool>("expectRig");
        QTest::newRow("existing-group-at-entry-limit") << true << true << false << true;
        QTest::newRow("new-group-with-name-collision") << false << false << false << true;
        QTest::newRow("no-placement-capacity") << false << true << false << false;
        QTest::newRow("no-tool-capacity") << true << false << true << false;
    }

    void schemaFourRigMigrationRespectsCapacityAndExistingNames() {
        QFETCH(bool, sceneGroup); QFETCH(bool, fullEntries); QFETCH(bool, fullTools); QFETCH(bool, expectRig);
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        const auto path = dir.filePath("tools.json");
        ToolCatalog catalog(&library, path); QVERIFY(catalog.load()); QVERIFY(catalog.save());
        auto root = schemaFour(QJsonDocument::fromJson(read(path)).object());
        auto entries = root["entries"].toArray();
        if (!sceneGroup) for (int i = 0; i < entries.size(); ++i) {
            auto entry = entries[i].toObject();
            if (entry["id"].toString() != "group/scene3d") continue;
            entry["name"] = "An unrelated custom group";
            entry["tools"] = QJsonArray{"hand"}; entry["lastToolId"] = "hand"; entries[i] = entry;
        }
        if (fullEntries) while (entries.size() < ToolCatalog::maxEntries) {
            auto entry = entries.first().toObject(); entry["id"] = QString("group/padding-%1").arg(entries.size()); entries.append(entry);
        }
        root["entries"] = entries;
        auto tools = root["tools"].toArray();
        if (fullTools) while (tools.size() < ToolCatalog::maxTools) {
            auto tool = tools.first().toObject(); tool["id"] = QString("tool/padding-%1").arg(tools.size());
            tool["brushPresetId"] = ""; tools.append(tool);
        }
        root["tools"] = tools;
        QVERIFY(write(path, QJsonDocument(root).toJson()));
        QString error; QVERIFY2(catalog.load(&error), qPrintable(error));
        QCOMPARE(catalog.activeToolId(), root["activeToolId"].toString());
        QCOMPARE(catalog.selectedEntryId(), root["selectedEntryId"].toString());
        QCOMPARE(bool(catalog.tool("scene-rig")), expectRig);
        QCOMPARE(catalog.tools().size(), tools.size() + (expectRig ? 1 : 0));
        QCOMPARE(catalog.entries().size(), entries.size() + (expectRig && !sceneGroup ? 1 : 0));
        if (expectRig && sceneGroup) QVERIFY(catalog.entry("group/scene3d")->tools.contains("scene-rig"));
        if (!sceneGroup) {
            QCOMPARE(catalog.entry("group/scene3d")->name, QString("An unrelated custom group"));
            QCOMPARE(catalog.entry("group/scene3d")->tools, QStringList{"hand"});
        }
        QVERIFY(catalog.save()); QVERIFY2(catalog.load(&error), qPrintable(error));
        QCOMPARE(catalog.tools().size(), tools.size() + (expectRig ? 1 : 0));
    }

    void rejectsOversizedGroupIconsAndColorBrushResources() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        const auto path = dir.filePath("tools.json");
        ToolCatalog catalog(&library, path); QVERIFY(catalog.load()); QVERIFY(catalog.save());
        const auto original = QJsonDocument::fromJson(read(path)).object();
        for (int variant = 0; variant < 4; ++variant) {
            auto root = original;
            // Oversized grayscale icons must be rejected at their reference too,
            // since a brush tip of the same dimensions is a valid shared resource.
            QImage image(variant == 0 ? 300 : variant == 1 ? 257 : 32, 16,
                         variant == 0 ? QImage::Format_Grayscale8 : QImage::Format_ARGB32_Premultiplied);
            if (variant == 0) image.fill(128); else image.fill(QColor(200, 20, 180, 100));
            QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "PNG"));
            const auto id = QString::fromLatin1(QCryptographicHash::hash(png, QCryptographicHash::Sha256).toHex());
            root["resources"] = QJsonObject{{id, QString::fromLatin1(png.toBase64())}};
            if (variant == 2) {
                auto tools = root["tools"].toArray(); auto tool = tools[0].toObject(); tool["tip"] = id; tools[0] = tool; root["tools"] = tools;
            } else {
                auto entries = root["entries"].toArray(); auto group = entries[0].toObject(); group["customIcon"] = id;
                entries[0] = group; root["entries"] = entries;
            }
            if (variant == 3) {
                root = legacyCatalog(root, 2);
                // The legacy helper strips unsupported current defaults; this
                // deliberately unsupported resource must remain in the fixture.
                root["resources"] = QJsonObject{{id, QString::fromLatin1(png.toBase64())}};
            }
            const auto bytes = QJsonDocument(root).toJson(); QVERIFY(write(path, bytes));
            QString error; QVERIFY(!catalog.load(&error)); QVERIFY(!error.isEmpty());
            QVERIFY(catalog.entry("group/drawing")->customIcon.isNull());
            QVERIFY(catalog.tool("brush/builtin/ink")->state.brush.tipMask.isNull());
            QVERIFY(!catalog.save()); QCOMPARE(read(path), bytes);
        }
    }
    void standardPackUpgradePreservesCustomizationsAndDeletedBrushes() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library,dir.filePath("tools.json")); QVERIFY(catalog.load());
        const auto standard = standardBrushPresets(); QVERIFY(!standard.isEmpty());
        const QString ink = "brush/builtin/ink", deleted = "brush/" + standard.first().id;
        auto state=catalog.tool(ink)->state;state.brush.size=23;state.smoothing=51;
        QVERIFY(catalog.updateToolState(ink,state));QVERIFY(catalog.renameTool(ink,"My ink"));
        QVERIFY(catalog.setToolTags({ink},{"Mine"}));QVERIFY(catalog.setBrushCategory({ink},"My category"));
        QVERIFY(catalog.deleteBrushes({deleted}));QVERIFY(catalog.save());
        auto root=QJsonDocument::fromJson(read(catalog.path())).object();QSet<QString> newIds;
        for(const auto &preset:standard)newIds.insert("brush/"+preset.id);
        QJsonArray tools;
        for(const auto &value:root["tools"].toArray())if(!newIds.contains(value.toObject()["id"].toString()))tools.append(value);
        root["tools"]=tools;QJsonArray entries;
        for(const auto &value:root["entries"].toArray()){
            auto entry=value.toObject();QJsonArray ids;
            for(const auto &id:entry["tools"].toArray())if(!newIds.contains(id.toString()))ids.append(id);
            entry["tools"]=ids;entries.append(entry);
        }
        root["entries"]=entries;QVERIFY(write(catalog.path(),QJsonDocument(root).toJson()));
        QVERIFY(catalog.load());QCOMPARE(catalog.tool(ink)->name,QString("My ink"));
        QCOMPARE(catalog.tool(ink)->state,state);QCOMPARE(catalog.tool(ink)->tags,QStringList{"Mine"});
        QCOMPARE(catalog.tool(ink)->category,QString("My category"));QVERIFY(!catalog.tool(deleted));
        for(const auto &preset:standard)if("brush/"+preset.id!=deleted){
            const auto *tool=catalog.tool("brush/"+preset.id);QVERIFY(tool);
            QCOMPARE(tool->category,standardBrushCategory(preset.id));QVERIFY(tool->tags.contains("Standard"));
            QVERIFY(catalog.entry("group/drawing")->tools.contains(tool->id));
        }
        const auto count=catalog.tools().size();QVERIFY(catalog.save());QVERIFY(catalog.load());
        QCOMPARE(catalog.tools().size(),count);QVERIFY(!catalog.tool(deleted));
    }
    void catalogDefaultsHaveSeparateFamiliesAndDoNotFloodDrawing() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        QCOMPARE(catalog.entry("group/drawing")->tools.size(), 20);
        QCOMPARE(catalog.activeToolId(), QString("brush/builtin/ink"));
        QSet<QString> grouped, familyIcons;
        QCOMPARE(catalog.tool("brush/builtin/catalog/dry-manga-nib")->brushType, BrushType::Ink);
        QCOMPARE(catalog.tool("brush/builtin/catalog/soft-graphite-side")->brushType, BrushType::Pen);
        QCOMPARE(catalog.tool("brush/builtin/catalog/short-grass-tuft")->brushType, BrushType::Stamp);
        QCOMPARE(catalog.tool("brush/builtin/catalog/velvet-shade")->brushType, BrushType::Soft);
        QCOMPARE(catalog.tool("brush/builtin/catalog/granular-spray")->brushType, BrushType::Spray);
        int families = 0;
        for (const auto &entry : catalog.entries()) {
            if (!entry.id.startsWith("group/catalog/")) continue;
            ++families;
            QVERIFY2(!appIcon(entry.icon).isNull(), qPrintable(entry.icon));
            QVERIFY2(!familyIcons.contains(entry.icon), qPrintable(entry.name));
            familyIcons.insert(entry.icon);
            QVERIFY(entry.group); QVERIFY(!entry.tools.isEmpty());
            QCOMPARE(entry.lastToolId, entry.tools.first());
            for (const auto &id : entry.tools) {
                QVERIFY2(!grouped.contains(id), qPrintable(id));
                grouped.insert(id);
                const auto *tool = catalog.tool(id); QVERIFY(tool);
                QCOMPARE(tool->category, entry.name);
                QCOMPARE(tool->tags, catalogBrushTags(tool->brushPresetId));
                QVERIFY(!catalog.entry("group/drawing")->tools.contains(id));
            }
        }
        QCOMPARE(families, 15);
        QCOMPARE(grouped.size(), catalogBrushPresets().size());
        for (const auto &preset : catalogBrushPresets())
            QVERIFY(grouped.contains("brush/" + preset.id));
        QString error;
        QVERIFY2(catalog.save(&error), qPrintable(error));
        ToolCatalog restored(&library, catalog.path());
        QVERIFY2(restored.load(&error), qPrintable(error));
        QCOMPARE(restored.entries().size(), catalog.entries().size());
        QCOMPARE(restored.tools().size(), catalog.tools().size());
        QCOMPARE(restored.activeToolId(), catalog.activeToolId());
    }
    void catalogPackUpgradePreservesExistingOrderSettingsAndNavigation() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        const QString ink = "brush/builtin/ink";
        auto state = catalog.tool(ink)->state;
        state.brush.size = 37; state.smoothing = 11;
        QVERIFY(catalog.updateToolState(ink, state));
        QVERIFY(catalog.renameTool(ink, "My everyday ink"));
        QVERIFY(catalog.setToolTags({ink}, {"My favorite"}));
        QVERIFY(catalog.renameEntry("group/drawing", "My drawing"));
        QVERIFY(catalog.moveEntry("entry/hand", 0));
        QVERIFY(catalog.selectEntry("group/shapes"));
        QVERIFY(catalog.activateTool("ellipse"));
        QVERIFY(catalog.save());
        const auto legacy = beforeCatalogPack(QJsonDocument::fromJson(read(catalog.path())).object());
        QVERIFY(write(catalog.path(), QJsonDocument(legacy).toJson()));
        QStringList previousOrder;
        for (const auto &value : legacy["entries"].toArray()) previousOrder.append(value.toObject()["id"].toString());
        QString error;
        QVERIFY2(catalog.load(&error), qPrintable(error));
        QCOMPARE(catalog.activeToolId(), QString("ellipse"));
        QCOMPARE(catalog.selectedEntryId(), QString("group/shapes"));
        QCOMPARE(catalog.tool(ink)->state, state);
        QCOMPARE(catalog.tool(ink)->name, QString("My everyday ink"));
        QCOMPARE(catalog.tool(ink)->tags, QStringList{"My favorite"});
        QCOMPARE(catalog.entry("group/drawing")->name, QString("My drawing"));
        QCOMPARE(catalog.entry("group/drawing")->tools.size(), 20);
        for (int i = 0; i < previousOrder.size(); ++i) QCOMPARE(catalog.entries()[i].id, previousOrder[i]);
        QCOMPARE(catalog.entries().size(), previousOrder.size() + 15);
        for (const auto &preset : catalogBrushPresets()) QVERIFY(catalog.tool("brush/" + preset.id));
        const auto count = catalog.tools().size();
        QVERIFY(catalog.save()); QVERIFY(catalog.load());
        QCOMPARE(catalog.tools().size(), count);
        QCOMPARE(catalog.entries().size(), previousOrder.size() + 15);
    }
    void catalogGroupsAndBrushEditsStayRemovedOrCustomized() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        const QString familyId = "group/catalog/grass-and-ground-cover";
        const QString otherId = "group/catalog/trees-and-foliage";
        QVERIFY(catalog.entry(familyId)); QVERIFY(catalog.entry(otherId));
        const auto family = *catalog.entry(familyId);
        const auto other = *catalog.entry(otherId);
        const auto editedId = family.tools.first();
        const auto removedPlacement = family.tools.last();
        auto state = catalog.tool(editedId)->state; state.brush.size = 67;
        QVERIFY(catalog.updateToolState(editedId, state));
        QVERIFY(catalog.renameTool(editedId, "My grass"));
        QVERIFY(catalog.setToolTags({editedId}, {"Meadow"}));
        QVERIFY(catalog.setBrushCategory({editedId}, "My backgrounds"));
        QVERIFY(catalog.removeTool(familyId, removedPlacement));
        QVERIFY(catalog.renameEntry(familyId, "My meadow", "paintbrush"));
        QVERIFY(catalog.removeEntry(otherId));
        // Deleting every member must not resurrect its group on reconciliation.
        QVERIFY(catalog.deleteBrushes(other.tools));
        QVERIFY(catalog.save());
        QString error;
        QVERIFY2(catalog.load(&error), qPrintable(error));
        QVERIFY(!catalog.entry(otherId));
        for (const auto &id : other.tools) QVERIFY(!catalog.tool(id));
        QCOMPARE(catalog.entry(familyId)->name, QString("My meadow"));
        QCOMPARE(catalog.entry(familyId)->icon, QString("paintbrush"));
        QVERIFY(!catalog.entry(familyId)->tools.contains(removedPlacement));
        QVERIFY(catalog.tool(removedPlacement));
        QCOMPARE(catalog.tool(editedId)->state, state);
        QCOMPARE(catalog.tool(editedId)->name, QString("My grass"));
        QCOMPARE(catalog.tool(editedId)->tags, QStringList{"Meadow"});
        QCOMPARE(catalog.tool(editedId)->category, QString("My backgrounds"));
        QVERIFY(catalog.removeEntry(familyId));
        QVERIFY(catalog.save()); QVERIFY(catalog.load());
        QVERIFY(!catalog.entry(familyId));
        QVERIFY(catalog.tool(editedId));
        QVERIFY(library.load());
        QVERIFY(!catalog.entry(familyId)); QVERIFY(!catalog.entry(otherId));
    }
    void catalogPackAtGroupCapacityKeepsBrushesDiscoverable() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        QVERIFY(catalog.save());
        auto legacy = beforeCatalogPack(QJsonDocument::fromJson(read(catalog.path())).object());
        auto entries = legacy["entries"].toArray();
        auto empty = entries.first().toObject();
        empty["tools"] = QJsonArray{}; empty["lastToolId"] = "";
        while (entries.size() < ToolCatalog::maxEntries) {
            empty["id"] = QString("group/my-%1").arg(entries.size());
            empty["name"] = QString("My group %1").arg(entries.size());
            entries.append(empty);
        }
        legacy["entries"] = entries;
        QVERIFY(write(catalog.path(), QJsonDocument(legacy).toJson()));
        QString error;
        QVERIFY2(catalog.load(&error), qPrintable(error));
        QCOMPARE(catalog.entries().size(), ToolCatalog::maxEntries);
        QCOMPARE(catalog.entry("group/drawing")->tools.size(), 20);
        for (const auto &preset : catalogBrushPresets()) QVERIFY(catalog.tool("brush/" + preset.id));
        QVERIFY2(catalog.save(&error), qPrintable(error));
        QVERIFY2(catalog.load(&error), qPrintable(error));
        QCOMPARE(catalog.entries().size(), ToolCatalog::maxEntries);
    }
    void correctedCatalogDefaultsUpgradeShippedRecipesExactlyOnce() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        Canvas canvas; canvas.setProject(Project::create());
        ToolSession session(&canvas, &catalog);
        const QString active = "brush/builtin/catalog/brick-surface";
        QVERIFY(catalog.activateTool(active)); QVERIFY(catalog.save());
        const auto shipped = shippedCatalogRecipes(QJsonDocument::fromJson(read(catalog.path())).object());
        QVERIFY(write(catalog.path(), QJsonDocument(shipped).toJson()));
        QSignalSpy activations(&catalog, &ToolCatalog::activationRequested);
        QString error;
        QVERIFY2(catalog.load(&error), qPrintable(error));
        QCOMPARE(catalog.activeToolId(), active);
        QVERIFY(!activations.isEmpty());
        QCOMPARE(canvas.brush.texture.pattern, recipe("brick-surface").brush.texture.pattern);
        QCOMPARE(canvas.brush.spacing, recipe("brick-surface").brush.spacing);
        for (const auto &preset : catalogBrushPresets()) {
            const auto *tool = catalog.tool("brush/" + preset.id); QVERIFY(tool);
            QCOMPARE(tool->builtinRecipeRevision, catalogBrushRecipeRevision);
            QCOMPARE(brushSettingsToJson(tool->state.brush), brushSettingsToJson(preset.brush));
            QCOMPARE(tool->state.brush.tipMask, preset.brush.tipMask);
            QCOMPARE(tool->state.brush.colorTip, preset.brush.colorTip);
            QCOMPARE(tool->state.brush.texture.pattern, preset.brush.texture.pattern);
            QCOMPARE(tool->state.brush.dual.tipMask, preset.brush.dual.tipMask);
            QCOMPARE(tool->state.smoothing, preset.smoothing);
        }
        // An artist can subsequently select an old-looking setting deliberately;
        // later loads and source-library refreshes must not apply the fix again.
        auto adjusted = catalog.tool(active)->state;
        adjusted.brush.texture.pattern = recipe("brick-surface", 1).brush.texture.pattern;
        adjusted.brush.spacing = recipe("brick-surface", 1).brush.spacing;
        QVERIFY(catalog.updateToolState(active, adjusted));
        QVERIFY(catalog.save()); const auto firstSave = read(catalog.path());
        QVERIFY(catalog.load()); QCOMPARE(catalog.tool(active)->state, adjusted);
        QVERIFY(library.load()); QCOMPARE(catalog.tool(active)->state, adjusted);
        QVERIFY(catalog.save()); QCOMPARE(read(catalog.path()), firstSave);
    }
    void correctedRecipesPreserveCustomFieldsCopiesOrganizationAndDeletions() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        const QString brick = "brush/builtin/catalog/brick-surface";
        const QString leaves = "brush/builtin/catalog/broadleaf-cluster";
        const QString deleted = "brush/builtin/catalog/roof-tile-surface";
        const QString custom = "brush/builtin/catalog/wood-grain";
        const auto duplicate = catalog.duplicateTool(brick, "group/drawing", "Artist brick copy");
        QVERIFY(!duplicate.isEmpty());
        auto copyState = catalog.tool(duplicate)->state;
        copyState.brush = recipe("brick-surface", 1).brush;
        QVERIFY(catalog.updateToolState(duplicate, copyState));
        QVERIFY(catalog.renameTool(brick, "My wall lines"));
        QVERIFY(catalog.setToolTags({brick}, {"My walls"}));
        QVERIFY(catalog.setToolColor({brick}, QColor("#a46632")));
        QVERIFY(catalog.setBrushCategory({brick}, "My backgrounds"));
        QVERIFY(catalog.removeEntry("group/catalog/walls-and-construction-surfaces"));
        QVERIFY(catalog.deleteBrushes({deleted}));
        QVERIFY(catalog.selectEntry("group/shapes")); QVERIFY(catalog.activateTool("ellipse"));
        QVERIFY(catalog.save());
        auto shipped = shippedCatalogRecipes(QJsonDocument::fromJson(read(catalog.path())).object());
        auto definitions = shipped["tools"].toArray();
        auto resources = shipped["resources"].toObject();
        QImage artistTip(31, 27, QImage::Format_Grayscale8); artistTip.fill(87);
        const auto artistImage = addResource(resources, artistTip);
        ToolState editedBrick, editedLeaves;
        for (int i = 0; i < definitions.size(); ++i) {
            auto item = definitions[i].toObject(); const auto id = item["id"].toString();
            if (id == brick || id == leaves) {
                BrushSettings value;
                QVERIFY(brushSettingsFromJson(item["brush"].toObject(), value));
                value.size = 211; value.opacity = .23; value.strokeOpacity = .62;
                value.sizeDynamics.response = {0, .2, .37, .61, 1};
                value.angleDynamics.jitter = .37;
                value.colorDynamics.brightness = .39;
                value.flowDynamics.jitterPerStroke = true;
                item["brush"] = brushSettingsToJson(value); item["smoothing"] = 19;
                item["options"] = QJsonObject{{"artistOption", true}};
                (id == brick ? editedBrick : editedLeaves) = {value, 19, {{"artistOption", true}}};
            }
            if (id == custom) item["texture"] = artistImage;
            definitions[i] = item;
        }
        shipped["tools"] = definitions; shipped["resources"] = resources;
        QVERIFY(write(catalog.path(), QJsonDocument(pruneResources(shipped)).toJson()));
        QString error; QVERIFY2(catalog.load(&error), qPrintable(error));
        QCOMPARE(catalog.selectedEntryId(), QString("group/shapes"));
        QCOMPARE(catalog.activeToolId(), QString("ellipse"));
        QVERIFY(!catalog.tool(deleted)); QVERIFY(!catalog.entry("group/catalog/walls-and-construction-surfaces"));
        QCOMPARE(catalog.tool(brick)->name, QString("My wall lines"));
        QCOMPARE(catalog.tool(brick)->tags, QStringList{"My walls"});
        QCOMPARE(catalog.tool(brick)->color, QColor("#a46632"));
        QCOMPARE(catalog.tool(brick)->category, QString("My backgrounds"));
        QCOMPARE(catalog.tool(duplicate)->state, copyState);
        QCOMPARE(catalog.tool(duplicate)->builtinRecipeRevision, 0);
        for (const auto &id : {brick, leaves}) {
            const auto &state = catalog.tool(id)->state;
            const auto &edited = id == brick ? editedBrick : editedLeaves;
            QCOMPARE(state.brush.size, edited.brush.size); QCOMPARE(state.brush.opacity, edited.brush.opacity);
            QCOMPARE(state.brush.strokeOpacity, edited.brush.strokeOpacity);
            QCOMPARE(state.brush.sizeDynamics.response, edited.brush.sizeDynamics.response);
            QCOMPARE(state.brush.angleDynamics.jitter, edited.brush.angleDynamics.jitter);
            QCOMPARE(state.brush.colorDynamics.brightness, edited.brush.colorDynamics.brightness);
            QCOMPARE(state.brush.flowDynamics.jitterPerStroke, true);
            QCOMPARE(state.smoothing, edited.smoothing); QCOMPARE(state.options, edited.options);
            QCOMPARE(catalog.tool(id)->builtinRecipeRevision, catalogBrushRecipeRevision);
        }
        QCOMPARE(catalog.tool(brick)->state.brush.texture.pattern, recipe("brick-surface").brush.texture.pattern);
        QVERIFY(catalog.tool(brick)->state.brush.texture.pattern != recipe("brick-surface", 1).brush.texture.pattern);
        QCOMPARE(catalog.tool(leaves)->state.brush.colorDynamics.perTip, true);
        QCOMPARE(catalog.tool(leaves)->state.brush.colorDynamics.hue, recipe("broadleaf-cluster").brush.colorDynamics.hue);
        QCOMPARE(catalog.tool(custom)->state.brush.texture.pattern, artistTip);
        QVERIFY(catalog.save()); const auto stable = read(catalog.path());
        QVERIFY(catalog.load()); QVERIFY(catalog.save()); QCOMPARE(read(catalog.path()), stable);
    }
    void futureRecipeRevisionsStayUntouchedAndInvalidRevisionsAreRejected() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load()); QVERIFY(catalog.save());
        auto root = shippedCatalogRecipes(QJsonDocument::fromJson(read(catalog.path())).object());
        auto definitions = root["tools"].toArray(); int index = -1;
        for (int i = 0; i < definitions.size(); ++i)
            if (definitions[i].toObject()["id"].toString() == "brush/builtin/catalog/brick-surface") index = i;
        QVERIFY(index >= 0);
        auto future = definitions[index].toObject(); future["builtinRecipeRevision"] = 999;
        definitions[index] = future; root["tools"] = definitions;
        QVERIFY(write(catalog.path(), QJsonDocument(root).toJson())); QVERIFY(catalog.load());
        const auto *brush = catalog.tool(future["id"].toString()); QVERIFY(brush);
        QCOMPARE(brush->builtinRecipeRevision, 999);
        QCOMPARE(brush->state.brush.texture.pattern, recipe("brick-surface", 1).brush.texture.pattern);
        QVERIFY(catalog.save());
        const auto valid = QJsonDocument::fromJson(read(catalog.path())).object();
        for (const QJsonValue bad : {QJsonValue(0), QJsonValue(-1), QJsonValue(1.5), QJsonValue("two")}) {
            auto broken = valid; auto values = broken["tools"].toArray();
            auto object = values[index].toObject(); object["builtinRecipeRevision"] = bad; values[index] = object;
            broken["tools"] = values; const auto bytes = QJsonDocument(broken).toJson();
            QVERIFY(write(catalog.path(), bytes)); QString error;
            QVERIFY(!catalog.load(&error)); QVERIFY(error.contains("revision"));
            QVERIFY(!catalog.save()); QCOMPARE(read(catalog.path()), bytes);
        }
    }
    void recipeCorrectionInvalidatesOnlyPreviewsWhoseBrushStateChanged() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load()); QVERIFY(catalog.save());
        const QString customized = "brush/builtin/catalog/brick-surface";
        auto preservedState = catalog.tool(customized)->state;
        preservedState.brush.size = 171;
        const auto original = QJsonDocument::fromJson(read(catalog.path())).object();
        auto root = shippedCatalogRecipes(original);
        auto definitions = root["tools"].toArray(); auto resources = root["resources"].toObject();
        QJsonObject edited;
        for (const auto &value : original["tools"].toArray())
            if (value.toObject()["id"].toString() == customized) edited = value.toObject();
        QVERIFY(!edited.isEmpty());
        // Every corrected channel/image has already been customized to its new
        // value. Advancing its revision must not masquerade as a brush edit.
        edited["builtinRecipeRevision"] = 1;
        edited["brush"] = brushSettingsToJson(preservedState.brush);
        for (const auto *field : {"tip", "colorTip", "texture", "dualTip", "surfaceImage"})
            if (edited.contains(field)) resources[edited[field].toString()] = original["resources"].toObject()[edited[field].toString()];
        for (int i = 0; i < definitions.size(); ++i)
            if (definitions[i].toObject()["id"].toString() == customized) definitions[i] = edited;
        root["tools"] = definitions; root["resources"] = resources;
        QVERIFY(write(catalog.path(), QJsonDocument(pruneResources(root)).toJson()));
        QSignalSpy changes(&catalog, &ToolCatalog::settingsChanged);
        QString error; QVERIFY2(catalog.load(&error), qPrintable(error));
        QSet<QString> expected;
        for (const auto &current : catalogBrushPresets()) {
            const auto before = recipe(current.id.sliced(QString("builtin/catalog/").size()), 1);
            const auto id = "brush/" + current.id;
            if (before.id.isEmpty()) continue;
            if (id != customized && (before.brush != current.brush || before.smoothing != current.smoothing))
                expected.insert(id);
        }
        QVERIFY(!expected.isEmpty());
        QSet<QString> notified;
        for (const auto &signal : changes) notified.insert(signal.first().toString());
        QCOMPARE(notified, expected); QCOMPARE(changes.size(), expected.size());
        QVERIFY(!notified.contains(customized));
        QCOMPARE(catalog.tool(customized)->state, preservedState);
        QCOMPARE(catalog.tool(customized)->builtinRecipeRevision, catalogBrushRecipeRevision);
        changes.clear(); QVERIFY(catalog.save()); QVERIFY(catalog.load()); QVERIFY(library.load());
        QCOMPARE(changes.size(), 0);
    }
    void recipeCorrectionDefersAtomicallyAtImageBudgetAndRetriesAfterSpaceIsFreed() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        const QString brick = "brush/builtin/catalog/brick-surface";
        QStringList removed;
        for (const auto &tool : catalog.tools())
            if (tool.engine == Canvas::Tool::Brush && tool.id != brick) removed.append(tool.id);
        QVERIFY(catalog.deleteBrushes(removed)); QVERIFY(catalog.activateTool(brick)); QVERIFY(catalog.save());
        auto root = shippedCatalogRecipes(QJsonDocument::fromJson(read(catalog.path())).object());
        auto definitions = root["tools"].toArray(); auto resources = root["resources"].toObject();
        QJsonObject base;
        for (const auto &value : definitions)
            if (value.toObject()["id"].toString() == brick) base = value.toObject();
        QVERIFY(!base.isEmpty());
        // A user copy retains the old texture, so its memory cannot be reclaimed
        // when the canonical brush receives the corrected pattern.
        auto copy = base; copy["id"] = "tool/artist-wall"; copy["brushPresetId"] = "";
        copy["name"] = "Artist wall copy"; definitions.append(copy);
        qint64 used = 0;
        for (const auto &value : resources) {
            const auto image = QImage::fromData(QByteArray::fromBase64(value.toString().toLatin1()), "PNG");
            QVERIFY(!image.isNull()); QCOMPARE(image.format(), QImage::Format_Grayscale8);
            used += qint64(image.width()) * image.height();
        }
        constexpr qint64 budget = 64 * 1024 * 1024;
        int count = 0; QStringList fillerIds;
        while (used < budget) {
            const qint64 remaining = budget - used;
            const bool color = remaining >= 4 * 2048 * 2048;
            const int width = color ? 2048 : int(qMin<qint64>(2048, remaining));
            const int height = color ? 2048 : int(qMin<qint64>(2048, remaining / width));
            QImage image(width, height, color ? QImage::Format_ARGB32_Premultiplied : QImage::Format_Grayscale8);
            if (color) image.fill(QColor(70 + count * 10, 120, 180, 255)); else image.fill(130 + count);
            auto item = base;
            const auto id = QString("tool/budget-%1").arg(count++); fillerIds.append(id);
            item["id"] = id; item["name"] = id; item["brushPresetId"] = "";
            item["brush"] = brushSettingsToJson(BrushSettings{});
            for (const auto *field : {"tip", "colorTip", "texture", "dualTip", "surfaceImage"}) item.remove(field);
            item[color ? "colorTip" : "tip"] = addResource(resources, image);
            item["brushType"] = int(color ? BrushType::ColoredStamp : BrushType::Stamp);
            definitions.append(item);
            used += qint64(width) * height * (color ? 4 : 1);
        }
        QCOMPARE(used, budget);
        root["tools"] = definitions; root["resources"] = resources;
        QVERIFY(write(catalog.path(), QJsonDocument(root).toJson()));
        QSignalSpy warnings(&catalog, &ToolCatalog::warning);
        QSignalSpy changes(&catalog, &ToolCatalog::settingsChanged);
        QString error; QVERIFY2(catalog.load(&error), qPrintable(error));
        QVERIFY(catalog.writable()); QCOMPARE(warnings.size(), 1);
        QCOMPARE(changes.size(), 0);
        QVERIFY(warnings.first().first().toString().contains("corrections were deferred"));
        QVERIFY(warnings.first().first().toString().contains("image limit"));
        QCOMPARE(catalog.tool(brick)->builtinRecipeRevision, 1);
        QCOMPARE(catalog.tool(brick)->state.brush.texture.pattern, recipe("brick-surface", 1).brush.texture.pattern);
        QCOMPARE(catalog.activeToolId(), brick);
        QVERIFY(catalog.renameTool(brick, "Still editable")); QVERIFY(catalog.save());
        QCOMPARE(catalog.history()->count(), 1);
        QVERIFY(library.load()); QCOMPARE(catalog.history()->count(), 1);
        QCOMPARE(changes.size(), 0);
        QVERIFY(catalog.deleteBrushes({fillerIds.first()})); warnings.clear();
        QVERIFY(library.load()); QCOMPARE(warnings.size(), 0);
        QCOMPARE(changes.size(), 1); QCOMPARE(changes.first().first().toString(), brick);
        QCOMPARE(catalog.tool(brick)->builtinRecipeRevision, catalogBrushRecipeRevision);
        QCOMPARE(catalog.tool(brick)->name, QString("Still editable"));
        QCOMPARE(catalog.tool(brick)->state.brush.texture.pattern, recipe("brick-surface").brush.texture.pattern);
        QCOMPARE(catalog.tool("tool/artist-wall")->state.brush.texture.pattern, recipe("brick-surface", 1).brush.texture.pattern);
        QVERIFY(catalog.save()); QVERIFY(catalog.load());
        QCOMPARE(catalog.tool(brick)->builtinRecipeRevision, catalogBrushRecipeRevision);
        for (const auto &id : removed) QVERIFY(!catalog.tool(id));
    }
    void correctedRecipesDoNotNeedAdditionalToolOrGroupCapacity() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load()); QVERIFY(catalog.save());
        auto root = shippedCatalogRecipes(QJsonDocument::fromJson(read(catalog.path())).object());
        auto definitions = root["tools"].toArray(); auto entries = root["entries"].toArray();
        QJsonObject line;
        for (const auto &value : definitions)
            if (value.toObject()["id"].toString() == "line") line = value.toObject();
        QVERIFY(!line.isEmpty());
        while (definitions.size() < ToolCatalog::maxTools) {
            line["id"] = QString("tool/artist-line-%1").arg(definitions.size());
            definitions.append(line);
        }
        auto empty = entries.first().toObject(); empty["tools"] = QJsonArray{}; empty["lastToolId"] = "";
        while (entries.size() < ToolCatalog::maxEntries) {
            empty["id"] = QString("group/artist-empty-%1").arg(entries.size()); entries.append(empty);
        }
        root["tools"] = definitions; root["entries"] = entries;
        QVERIFY(write(catalog.path(), QJsonDocument(root).toJson()));
        QSignalSpy warnings(&catalog, &ToolCatalog::warning);
        QString error; QVERIFY2(catalog.load(&error), qPrintable(error));
        QCOMPARE(warnings.size(), 0); QCOMPARE(catalog.tools().size(), ToolCatalog::maxTools);
        QCOMPARE(catalog.entries().size(), ToolCatalog::maxEntries);
        for (const auto &preset : catalogBrushPresets())
            QCOMPARE(catalog.tool("brush/" + preset.id)->builtinRecipeRevision, catalogBrushRecipeRevision);
        QCOMPARE(catalog.tool("brush/builtin/catalog/brick-surface")->state.brush.texture.pattern,
                 recipe("brick-surface").brush.texture.pattern);
        QVERIFY(catalog.save()); QVERIFY(catalog.load());
        QCOMPARE(catalog.tools().size(), ToolCatalog::maxTools); QCOMPARE(catalog.entries().size(), ToolCatalog::maxEntries);
    }
    void standardPackUpgradeDefersWhenImageBudgetIsFull() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        QStringList additions;
        for (const auto &preset : standardBrushPresets()) additions.append("brush/" + preset.id);
        for (const auto &preset : catalogBrushPresets()) additions.append("brush/" + preset.id);
        QVERIFY(catalog.deleteBrushes(additions));

        // Four distinct RGBA stamps fill the valid 64 MiB decoded-image budget.
        // Remove the new pack first to construct a pre-upgrade catalog at its limit.
        QStringList custom;
        for (int i = 0; i < 4; ++i) {
            ToolState state;
            state.brush.colorTip = QImage(2048, 2048, QImage::Format_ARGB32_Premultiplied);
            state.brush.colorTip.fill(QColor(40 + i * 40, 30 + i * 20, 160 - i * 30, 200));
            const auto id = catalog.createBrush(QString("Large stamp %1").arg(i + 1),
                                                BrushType::ColoredStamp, state, "My stamps");
            QVERIFY(!id.isEmpty()); custom.append(id);
        }
        const QString ink = "brush/builtin/ink";
        QVERIFY(catalog.renameTool(ink, "Artist ink"));
        QVERIFY(catalog.save());
        auto root = QJsonDocument::fromJson(read(catalog.path())).object();
        // Missing additions model an older install, not an artist deletion.
        root["deletedBrushPresets"] = QJsonArray{};
        const auto before = QJsonDocument(root).toJson();
        QVERIFY(write(catalog.path(), before));
        const auto count = catalog.tools().size();
        QSignalSpy warnings(&catalog, &ToolCatalog::warning);
        QString error;
        QVERIFY2(catalog.load(&error), qPrintable(error));
        QVERIFY(catalog.writable()); QVERIFY(error.isEmpty());
        QCOMPARE(catalog.tools().size(), count);
        QCOMPARE(catalog.tool(ink)->name, QString("Artist ink"));
        QCOMPARE(warnings.count(), 1);
        QVERIFY(warnings.first().first().toString().contains("deferred"));
        QVERIFY(warnings.first().first().toString().contains("image limit"));
        for (const auto &id : additions) QVERIFY(!catalog.tool(id));
        for (const auto &id : custom) {
            QVERIFY(catalog.tool(id));
            QCOMPARE(catalog.tool(id)->state.brush.colorTip.size(), QSize(2048, 2048));
        }
        // Deferring source additions must not block subsequent ordinary edits,
        // saving, or an existing organization Undo history.
        QVERIFY(catalog.renameTool(ink, "Still editable"));
        QCOMPARE(catalog.history()->count(), 1);
        QVERIFY(library.load());
        QCOMPARE(catalog.history()->count(), 1);
        QVERIFY2(catalog.save(&error), qPrintable(error));
        QVERIFY2(catalog.load(&error), qPrintable(error));
        QCOMPARE(catalog.tool(ink)->name, QString("Still editable"));
        QCOMPARE(catalog.tools().size(), count);

        // They were deferred, not tombstoned: another reconciliation can install
        // the complete pack once sufficient resource capacity is available.
        QVERIFY(catalog.deleteBrushes({custom.last(),custom[custom.size()-2]}));
        warnings.clear();
        QVERIFY(library.load());
        QCOMPARE(warnings.count(), 0);
        for (const auto &id : additions) QVERIFY(catalog.tool(id));
        QCOMPARE(catalog.tools().size(), count - 2 + additions.size());
        QVERIFY2(catalog.save(&error), qPrintable(error));
        QVERIFY2(catalog.load(&error), qPrintable(error));
        QCOMPARE(catalog.tool(ink)->name, QString("Still editable"));
        QVERIFY(!catalog.tool(custom.last()));
    }

    void revisionTwoQualityUpgradePreservesCustomTipRoots() {
        QTemporaryDir dir;BrushLibrary library(dir.filePath("brushes.json"));QVERIFY(library.load());
        ToolCatalog catalog(&library,dir.filePath("tools.json"));QVERIFY(catalog.load());QVERIFY(catalog.save());
        auto root=shippedCatalogRecipes(QJsonDocument::fromJson(read(catalog.path())).object(),2);
        auto tools=root["tools"].toArray();const QString id="brush/builtin/catalog/short-grass-tuft";
        const auto original=recipe("short-grass-tuft",2).brush;
        for(int i=0;i<tools.size();++i){auto object=tools[i].toObject();if(object["id"]!=id)continue;
            auto b=original;b.tipFrames={{original.tipMask.rect(),{.5,1},2}};b.tipOrder=BrushTipOrder::Random;
            object["brush"]=brushSettingsToJson(b);tools[i]=object;
        }
        root["tools"]=tools;QVERIFY(write(catalog.path(),QJsonDocument(root).toJson()));QString error;QVERIFY2(catalog.load(&error),qPrintable(error));
        const auto *tool=catalog.tool(id);QVERIFY(tool);QCOMPARE(tool->builtinRecipeRevision,catalogBrushRecipeRevision);
        QCOMPARE(tool->state.brush.tipMask,original.tipMask);QCOMPARE(tool->state.brush.tipFrames.size(),1);QCOMPARE(tool->state.brush.tipFrames[0].anchor,QPointF(.5,1));QCOMPARE(tool->state.brush.tipFrames[0].weight,2.);QCOMPARE(tool->state.brush.tipOrder,BrushTipOrder::Random);
        const auto *untouched=catalog.tool("brush/builtin/catalog/wind-grass");QVERIFY(untouched);QCOMPARE(untouched->state.brush.tipFrames.size(),6);
        QVERIFY(catalog.save());QVERIFY(catalog.load());QCOMPARE(catalog.tool(id)->state.brush.tipMask,original.tipMask);
    }
    void revisionFourJoinFixesPreserveArtistEdits() {
        QTemporaryDir dir;BrushLibrary library(dir.filePath("brushes.json"));QVERIFY(library.load());
        ToolCatalog catalog(&library,dir.filePath("tools.json"));QVERIFY(catalog.load());QVERIFY(catalog.save());
        auto root=shippedCatalogRecipes(QJsonDocument::fromJson(read(catalog.path())).object(),4);
        auto tools=root["tools"].toArray();auto resources=root["resources"].toObject();
        const QString fence="brush/builtin/catalog/fence-posts",rail="brush/builtin/catalog/railings-and-bars";
        auto custom=recipe("railings-and-bars",4).brush.tipMask;custom.fill(150);
        for(int i=0;i<tools.size();++i){auto object=tools[i].toObject();
            if(object["id"]==fence){auto b=recipe("fence-posts",4).brush;b.size=173;b.opacity=.42;object["brush"]=brushSettingsToJson(b);object["name"]="My fence";}
            if(object["id"]==rail)object["tip"]=addResource(resources,custom);
            tools[i]=object;
        }
        root["tools"]=tools;root["resources"]=resources;QVERIFY(write(catalog.path(),QJsonDocument(root).toJson()));
        QString error;QVERIFY2(catalog.load(&error),qPrintable(error));
        const auto *updated=catalog.tool(fence);QVERIFY(updated);QCOMPARE(updated->name,QString("My fence"));
        QCOMPARE(updated->state.brush.size,173.);QCOMPARE(updated->state.brush.opacity,.42);
        QCOMPARE(updated->state.brush.tipMask,recipe("fence-posts").brush.tipMask);
        QVERIFY(updated->state.brush.tipMask!=recipe("fence-posts",4).brush.tipMask);
        QCOMPARE(catalog.tool(rail)->state.brush.tipMask,custom);
        QCOMPARE(updated->builtinRecipeRevision,catalogBrushRecipeRevision);
        QVERIFY(catalog.save());QVERIFY(catalog.load());QCOMPARE(catalog.tool(rail)->state.brush.tipMask,custom);
        QCOMPARE(catalog.tool(fence)->state.brush.tipMask,recipe("fence-posts").brush.tipMask);
    }
    void extendedDynamicsPersistAndRequireVersionSeven() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json"));QVERIFY(library.load());
        ToolCatalog catalog(nullptr,dir.filePath("tools.json"));QVERIFY(catalog.load());QVERIFY(catalog.save());
        QCOMPARE(QJsonDocument::fromJson(read(catalog.path())).object()["version"].toInt(),10);
        const auto id=catalog.activeToolId();auto state=catalog.tool(id)->state;
        state.brush.sizeDynamics.control=BrushControl::Pressure;state.brush.sizeDynamics.response={0,.1,.2,.5,1};
        state.brush.angleDynamics.control=BrushControl::Rotation;state.brush.angleDynamics.jitterPerStroke=true;
        state.brush.lockRandomSeed=true;state.brush.randomSeed=0xffffffffu;
        QVERIFY(catalog.updateToolState(id,state));QVERIFY(catalog.save());
        auto root=QJsonDocument::fromJson(read(catalog.path())).object();QCOMPARE(root["version"].toInt(),10);
        ToolCatalog restored(nullptr,catalog.path());QString error;QVERIFY2(restored.load(&error),qPrintable(error));
        QCOMPARE(restored.tool(id)->state,state);
        root["version"]=6;const auto invalid=QJsonDocument(root).toJson();QVERIFY(write(catalog.path(),invalid));
        QVERIFY(!restored.load(&error));QVERIFY(error.contains("version 7"));
        QCOMPARE(restored.tool(id)->state,state);QVERIFY(!restored.save());QCOMPARE(read(catalog.path()),invalid);
    }
};
QTEST_MAIN(ToolCatalogTests)
#include "test_tool_catalog.moc"
