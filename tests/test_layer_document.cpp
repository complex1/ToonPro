#include "document.h"
#include "sheet.h"
#include <QSignalSpy>
#include <QSet>
#include <QtTest>

using namespace toon;
class LayerDocumentTest : public QObject {
    Q_OBJECT
    static int index(const Document &doc, const QString &id) {
        const auto &layers = doc.project().shots.first().layers;
        for (int i = 0; i < layers.size(); ++i) if (layers[i].id == id) return i;
        return -1;
    }
    static RasterLayer active(const Document &doc) { return doc.project().shots.first().layers[doc.activeLayer()]; }
private slots:
    void groupingReparentingAndSelectionHistory() {
        Document doc; doc.load(Project::create());
        const auto original = active(doc).id;
        doc.addGroup(); const auto outer = active(doc).id;
        QVERIFY(active(doc).group);
        QCOMPARE(doc.project().shots.first().layers[index(doc, original)].parentId, outer);
        doc.history()->undo(); QCOMPARE(active(doc).id, original);
        QCOMPARE(doc.project().shots.first().layers.size(), 1);
        doc.history()->redo(); QCOMPARE(active(doc).id, outer);
        doc.addLayer(); const auto upper = active(doc).id;
        QCOMPARE(active(doc).parentId, outer);
        doc.addGroup(); const auto inner = active(doc).id;
        QCOMPARE(active(doc).parentId, outer);
        QCOMPARE(doc.project().shots.first().layers[index(doc, upper)].parentId, inner);
        doc.setLayerParent({}); QCOMPARE(active(doc).id, inner); QVERIFY(active(doc).parentId.isEmpty());
        QVERIFY(layerIsDescendant(doc.project().shots.first().layers, index(doc, upper), inner));
        doc.history()->undo(); QCOMPARE(active(doc).id, inner); QCOMPARE(active(doc).parentId, outer);
        const int edits = doc.history()->index(); QSignalSpy warnings(&doc, &Document::warning);
        doc.selectLayer(index(doc, outer)); doc.setLayerParent(inner);
        QCOMPARE(doc.history()->index(), edits); QCOMPARE(warnings.count(), 1);
        QVERIFY(doc.project().validate().isEmpty());
    }
    void reorderAndDeleteWholeGroup() {
        Document doc; doc.load(Project::create());
        const auto first = active(doc).id;
        doc.addGroup(); const auto group = active(doc).id;
        doc.addLayer(); const auto second = active(doc).id;
        doc.moveLayer(-1); QVERIFY(index(doc, second) < index(doc, first));
        QCOMPARE(active(doc).id, second);
        doc.history()->undo(); QVERIFY(index(doc, second) > index(doc, first));
        QCOMPARE(active(doc).id, second);
        doc.setLayerParent({}); // A root sibling above the group.
        doc.selectLayer(index(doc, group)); doc.moveLayer(1);
        QVERIFY(index(doc, group) > index(doc, second));
        QVERIFY(layerIsDescendant(doc.project().shots.first().layers, index(doc, first), group));
        doc.deleteLayer(); QCOMPARE(doc.project().shots.first().layers.size(), 1);
        QCOMPARE(active(doc).id, second);
        doc.history()->undo(); QCOMPARE(active(doc).id, group);
        QCOMPARE(doc.project().shots.first().layers.size(), 3);
        doc.selectLayer(index(doc, second)); doc.setLayerParent(group);
        doc.selectLayer(index(doc, group)); const int edits = doc.history()->index();
        doc.deleteLayer(); QCOMPARE(doc.history()->index(), edits); // Retain a raster layer.
        QVERIFY(doc.project().validate().isEmpty());
    }
    void inheritedVisibilityLockAndNotes() {
        auto project = Project::create(); project.transparent = true;
        Document doc; doc.load(project); const auto leaf = active(doc).id;
        doc.addGroup(); const auto group = active(doc).id;
        BrushSettings brush; brush.size = 12; brush.color = Qt::red;
        QVERIFY(!doc.beginStroke({100, 100}, 1, brush, false)); // Groups have no paint pixels.
        doc.changeLayer("Character", true, true, false, 1);
        doc.selectLayer(index(doc, leaf)); QVERIFY(!doc.beginStroke({100, 100}, 1, brush, false));
        doc.selectLayer(index(doc, group)); doc.changeLayer("Character", false, false, false, 1);
        doc.selectLayer(index(doc, leaf)); QVERIFY(!doc.beginStroke({100, 100}, 1, brush, false));
        doc.selectLayer(index(doc, group)); doc.changeLayer("Annotations", true, false, true, 1);
        doc.selectLayer(index(doc, leaf));
        QVERIFY(layerIsAnnotation(doc.project().shots.first().layers, doc.activeLayer()));
        QVERIFY(doc.beginStroke({-160, 100}, 1, brush, false)); doc.endStroke();
        const auto geometry = sheetGeometry(doc.project(), doc.project().shots.first());
        const auto sheet = renderSheet(doc.project(), doc.project().shots.first());
        QCOMPARE(sheet.pixelColor((geometry.output.topLeft() + QPointF(-160, 100)).toPoint()), QColor(Qt::red));
        QVERIFY(doc.beginStroke({100, 100}, 1, brush, false)); doc.endStroke();
        QCOMPARE(renderMovieFrame(doc.project()).pixelColor(100, 100).alpha(), 0);
        doc.selectLayer(index(doc, group)); doc.changeLayer("Character", true, false, false, 1);
        QCOMPARE(renderMovieFrame(doc.project()).pixelColor(100, 100), QColor(Qt::red));
    }
    void flagsUndoAndStrokeSafety() {
        Document doc; doc.load(Project::create());
        doc.setLayerAlphaLocked(true); QVERIFY(active(doc).alphaLocked);
        doc.history()->undo(); QVERIFY(!active(doc).alphaLocked);
        doc.history()->redo(); QVERIFY(active(doc).alphaLocked);
        doc.setLayerClipped(true); QVERIFY(active(doc).clipped);
        BrushSettings brush;
        QVERIFY(!doc.beginStroke({100, 100}, 1, brush, true));
        QVERIFY(doc.beginStroke({100, 100}, 1, brush, false));
        const int edits = doc.history()->index();
        doc.addGroup(); doc.setLayerParent("invalid"); doc.setLayerAlphaLocked(false); doc.setLayerClipped(false);
        QCOMPARE(doc.project().shots.first().layers.size(), 1);
        QVERIFY(active(doc).alphaLocked); QVERIFY(active(doc).clipped);
        doc.endStroke(); QCOMPARE(doc.history()->index(), edits); // Empty alpha-locked layer stays empty.
        doc.addGroup(); QVERIFY(active(doc).group);
        doc.setLayerAlphaLocked(true); doc.setLayerClipped(true);
        QVERIFY(!active(doc).alphaLocked); QVERIFY(!active(doc).clipped);
    }
    void depthLimitAndTopmostNewChild() {
        Document doc; doc.load(Project::create());
        for (int i = 0; i < 8; ++i) doc.addGroup();
        const int count = doc.project().shots.first().layers.size();
        doc.addGroup(); QCOMPARE(doc.project().shots.first().layers.size(), count);
        const auto rootGroup = active(doc).id;
        doc.addLayer(); const auto first = active(doc).id;
        doc.selectLayer(index(doc, rootGroup)); doc.addLayer(); const auto second = active(doc).id;
        QVERIFY(index(doc, second) > index(doc, first));
        QCOMPARE(active(doc).parentId, rootGroup);
        QVERIFY(doc.project().validate().isEmpty());
    }
    void deleteRedoRestoresItsSurvivingSelection() {
        Document doc; doc.load(Project::create()); const auto first = active(doc).id;
        doc.addLayer(); const auto second = active(doc).id;
        doc.addLayer(); const auto third = active(doc).id;
        doc.selectLayer(index(doc, second)); doc.deleteLayer(); QCOMPARE(active(doc).id, third);
        doc.history()->undo(); QCOMPARE(active(doc).id, second);
        doc.selectLayer(index(doc, first)); doc.history()->redo(); QCOMPARE(active(doc).id, third);
    }
    void batchVisibilityAndOpacityAreOneUndoablePreview() {
        Document doc; doc.load(Project::create()); const auto first = active(doc).id;
        doc.addLayer(); const auto second = active(doc).id;
        const int baseline = doc.history()->index();
        QSignalSpy selection(&doc, &Document::selectionChanged);
        QVERIFY(doc.beginLayerPropertyEdit("Show or hide layers"));
        doc.previewLayersVisible({second}, false);
        doc.previewLayersVisible({second, first}, false);
        QVERIFY(!active(doc).visible); QCOMPARE(doc.history()->index(), baseline);
        QCOMPARE(selection.count(), 0); QCOMPARE(active(doc).id, second);
        doc.selectLayer(index(doc, first)); QCOMPARE(active(doc).id, second);
        doc.addLayer(); QCOMPARE(doc.project().shots.first().layers.size(), 2);
        BrushSettings brush; QVERIFY(!doc.beginStroke({100, 100}, 1, brush, false));
        doc.commitLayerPropertyEdit(); QCOMPARE(doc.history()->index(), baseline + 1);
        doc.history()->undo(); QVERIFY(active(doc).visible);
        QVERIFY(doc.project().shots.first().layers[index(doc, first)].visible);
        doc.history()->redo(); QVERIFY(!active(doc).visible);
        QVERIFY(doc.beginLayerPropertyEdit());
        for (int value = 99; value >= 25; --value) doc.previewLayerOpacity(value / 100.0);
        QCOMPARE(active(doc).opacity, .25); QCOMPARE(doc.history()->index(), baseline + 1);
        doc.commitLayerPropertyEdit(); QCOMPARE(doc.history()->index(), baseline + 2);
        doc.history()->undo(); QCOMPARE(active(doc).opacity, 1.0);
        QVERIFY(doc.beginLayerPropertyEdit()); doc.previewLayerOpacity(.5); doc.cancelLayerPropertyEdit();
        QCOMPARE(active(doc).opacity, 1.0); QVERIFY(!doc.layerPropertyEditActive());
        const int edits = doc.history()->index();
        QVERIFY(doc.beginLayerPropertyEdit()); doc.previewLayerOpacity(.5); doc.previewLayerOpacity(1); doc.commitLayerPropertyEdit();
        QCOMPARE(doc.history()->index(), edits);
    }
    void propertyPreviewHistoryAndLoadDoNotLeaveStaleTransactions() {
        Document doc; doc.load(Project::create()); doc.addLayer();
        QVERIFY(doc.beginLayerPropertyEdit()); doc.previewLayerOpacity(.3);
        doc.history()->undo(); QVERIFY(!doc.layerPropertyEditActive());
        QCOMPARE(doc.project().shots.first().layers.size(), 1); QCOMPARE(active(doc).opacity, 1.0);
        QVERIFY(doc.beginLayerPropertyEdit()); doc.previewLayerOpacity(.2);
        doc.load(Project::create()); QVERIFY(!doc.layerPropertyEditActive()); QCOMPARE(active(doc).opacity, 1.0);
    }
    void fullAndPositionLocksAreInheritedWithoutBlockingPainting() {
        Document doc; doc.load(Project::create()); const auto first = active(doc).id;
        doc.addGroup(); const auto group = active(doc).id;
        doc.setLayerPositionLocked(true);
        doc.selectLayer(index(doc, first));
        BrushSettings brush; brush.size = 10;
        QVERIFY(doc.beginStroke({100, 100}, 1, brush, false)); doc.endStroke();
        QVERIFY(!doc.beginTransform());
        const int baseline = doc.history()->index();
        doc.moveLayers({first}, {}); doc.groupLayers({first});
        QCOMPARE(doc.history()->index(), baseline); QCOMPARE(active(doc).parentId, group);
        doc.selectLayer(index(doc, group)); doc.setLayerPositionLocked(false);
        doc.selectLayer(index(doc, first)); QVERIFY(doc.beginTransform()); doc.cancelTransform();
        doc.setLayersLocked({group}, true);
        const auto frozen = active(doc); const int lockedBaseline = doc.history()->index();
        doc.changeLayer("Changed", true, false, true, .25);
        doc.setLayerBlendMode(LayerBlendMode::Multiply); doc.setLayerAlphaLocked(true);
        doc.setLayersRowColor({first}, Qt::red); doc.deleteLayers({first});
        doc.duplicateLayers({first});
        QCOMPARE(doc.history()->index(), lockedBaseline); QCOMPARE(active(doc).name, frozen.name);
        QCOMPARE(active(doc).opacity, frozen.opacity); QVERIFY(!active(doc).annotation);
        QVERIFY(!doc.beginStroke({100, 100}, 1, brush, false));
        doc.setLayersVisible({first}, false); QVERIFY(!active(doc).visible);
        doc.setLayersLocked({group}, false); doc.setLayersVisible({first}, true);
        QVERIFY(doc.beginStroke({100, 100}, 1, brush, false)); doc.cancelStroke();
        doc.setLayersLocked({first}, true); doc.deleteLayers({first}); QVERIFY(index(doc, first) >= 0);
        doc.setLayersLocked({first}, false); QVERIFY(!active(doc).locked);
    }
    void batchGroupingMovingAndDeletingKeepSelectionAndOneUndo() {
        Document doc; doc.load(Project::create()); const auto a = active(doc).id;
        doc.addLayer(); const auto b = active(doc).id;
        doc.addLayer(); const auto c = active(doc).id;
        doc.addLayer(); const auto d = active(doc).id;
        int baseline = doc.history()->index(); doc.groupLayers({b, c}); const auto group = active(doc).id;
        QCOMPARE(doc.history()->index(), baseline + 1);
        QVERIFY(active(doc).group); QCOMPARE(doc.project().shots.first().layers[index(doc, b)].parentId, group);
        QCOMPARE(doc.project().shots.first().layers[index(doc, c)].parentId, group);
        doc.history()->undo(); QCOMPARE(doc.project().shots.first().layers.size(), 4); QCOMPARE(active(doc).id, d);
        doc.history()->redo(); QCOMPARE(active(doc).id, group);
        baseline = doc.history()->index(); doc.moveLayers({group}, {}, a);
        QCOMPARE(doc.history()->index(), baseline + 1); QVERIFY(index(doc, group) < index(doc, a));
        QCOMPARE(active(doc).id, group); QVERIFY(layerIsDescendant(doc.project().shots.first().layers, index(doc, b), group));
        doc.history()->undo(); QVERIFY(index(doc, group) > index(doc, a));
        baseline = doc.history()->index(); doc.deleteLayers({group, b});
        QCOMPARE(doc.history()->index(), baseline + 1); QCOMPARE(doc.project().shots.first().layers.size(), 2);
        QVERIFY(index(doc, b) < 0); QVERIFY(index(doc, c) < 0); QVERIFY(index(doc, group) < 0);
        doc.history()->undo(); QCOMPARE(active(doc).id, group); QVERIFY(doc.project().validate().isEmpty());
        doc.deleteLayers({a, group, d}); QCOMPARE(doc.project().shots.first().layers.size(), 5);
    }
    void duplicatesCloneSubtreesAboveOriginalAndUseNewIdentifiers() {
        auto project = Project::create();
        QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::red);
        project.shots.first().layers.first().tiles.insert({0, 0}, tile);
        Document doc; doc.load(project); const auto original = active(doc).id;
        doc.addGroup(); const auto group = active(doc).id;
        doc.addLayer(); const auto child = active(doc).id;
        doc.selectLayer(index(doc, group)); const int baseline = doc.history()->index();
        doc.duplicateLayers({group, child}); const auto copy = active(doc).id;
        QCOMPARE(doc.history()->index(), baseline + 1); QVERIFY(copy != group); QVERIFY(active(doc).group);
        QVERIFY(index(doc, copy) > index(doc, group)); QCOMPARE(doc.project().shots.first().layers.size(), 6);
        QSet<QString> ids; int children = 0; QString paintedCopy;
        for (const auto &layer : doc.project().shots.first().layers) {
            QVERIFY(!ids.contains(layer.id)); ids.insert(layer.id);
            if (layer.parentId == copy) { ++children; if (!layer.tiles.isEmpty()) paintedCopy = layer.id; }
        }
        QCOMPARE(children, 2); QVERIFY(!paintedCopy.isEmpty());
        doc.selectLayer(index(doc, paintedCopy)); BrushSettings brush; brush.color = Qt::blue; brush.size = 20;
        QVERIFY(doc.beginStroke({100, 100}, 1, brush, false)); doc.endStroke();
        QCOMPARE(doc.project().shots.first().layers[index(doc, original)].tiles.value({0, 0}).pixelColor(100, 100), QColor(Qt::red));
        QVERIFY(doc.project().validate().isEmpty());
        doc.history()->undo(); doc.history()->undo(); QCOMPARE(doc.project().shots.first().layers.size(), 3);
    }
    void clippingFamiliesCannotBeAccidentallyReboundByStructureChanges() {
        Document doc; doc.load(Project::create()); const auto base = active(doc).id;
        doc.addLayer(); const auto clipped = active(doc).id; doc.setLayerClipped(true);
        doc.addLayer(); const auto top = active(doc).id;
        const int baseline = doc.history()->index();
        doc.deleteLayers({base}); doc.moveLayers({base}, {}); doc.duplicateLayers({base}); doc.groupLayers({base});
        QCOMPARE(doc.history()->index(), baseline); QCOMPARE(doc.project().shots.first().layers.size(), 3);
        doc.moveLayers({base, clipped}, {}); QVERIFY(index(doc, base) > index(doc, top));
        QVERIFY(index(doc, clipped) > index(doc, base));
        doc.history()->undo(); doc.duplicateLayers({base, clipped});
        QCOMPARE(doc.project().shots.first().layers.size(), 5); QVERIFY(doc.project().validate().isEmpty());
        doc.history()->undo(); doc.deleteLayers({base, clipped}); QCOMPARE(doc.project().shots.first().layers.size(), 1);
    }
    void rowColorsAndBlendModesUndoWithoutChangingArtwork() {
        Document doc; doc.load(Project::create()); const auto a = active(doc).id;
        doc.addLayer(); const auto b = active(doc).id;
        int baseline = doc.history()->index(); doc.setLayersRowColor({a, b}, QColor("#ff4444"));
        QCOMPARE(doc.history()->index(), baseline + 1);
        for (const auto &layer : doc.project().shots.first().layers) QCOMPARE(layer.rowColor, QColor("#ff4444"));
        doc.history()->undo(); QVERIFY(!active(doc).rowColor.isValid()); doc.history()->redo();
        doc.setLayerBlendMode(LayerBlendMode::Multiply); QCOMPARE(active(doc).blendMode, LayerBlendMode::Multiply);
        doc.history()->undo(); QCOMPARE(active(doc).blendMode, LayerBlendMode::Normal);
        baseline = doc.history()->index(); doc.setLayersRowColor({b}, QColor(255, 0, 0, 30));
        QCOMPARE(doc.history()->index(), baseline); QVERIFY(doc.project().validate().isEmpty());
    }
    void sparseMergePreservesPixelsOpacityOverscanAndUndo() {
        auto project = Project::create(); project.transparent = true;
        auto &layers = project.shots.first().layers;
        layers.first().opacity = .7;
        QVERIFY(paintDab(layers.first(), {-35, 40}, 20, Qt::red, .8, false, {-120, -120, 2160, 1320}, 1024));
        QVERIFY(paintDab(layers.first(), {100, 100}, 25, Qt::red, .8, false, {-120, -120, 2160, 1320}, 1024));
        auto upper = RasterLayer::create("Ink"); upper.opacity = .5;
        QVERIFY(paintDab(upper, {105, 100}, 25, Qt::blue, .9, false, {-120, -120, 2160, 1320}, 1024));
        layers.append(upper);
        const auto frame = renderMovieFrame(project); const auto sheet = renderSheet(project, project.shots.first());
        Document doc; doc.load(project); doc.selectLayer(1);
        QVERIFY(doc.mergeDown()); QCOMPARE(doc.project().shots.first().layers.size(), 1);
        QCOMPARE(active(doc).opacity, 1.0); QCOMPARE(active(doc).tiles.size(), 2);
        QCOMPARE(renderMovieFrame(doc.project()), frame);
        QCOMPARE(renderSheet(doc.project(), doc.project().shots.first()), sheet);
        QCOMPARE(doc.history()->index(), 1); doc.history()->undo(); QCOMPARE(active(doc).id, upper.id);
        QCOMPARE(doc.project().shots.first().layers.size(), 2); doc.history()->redo();
        QCOMPARE(renderMovieFrame(doc.project()), frame);
    }
    void unsafeMergesAreAtomic() {
        Document doc; doc.load(Project::create()); doc.addLayer();
        doc.setLayerBlendMode(LayerBlendMode::Multiply); int baseline = doc.history()->index();
        QVERIFY(!doc.mergeDown()); QCOMPARE(doc.history()->index(), baseline);
        doc.setLayerBlendMode(LayerBlendMode::Normal); doc.setLayerClipped(true); baseline = doc.history()->index();
        QVERIFY(!doc.mergeDown()); QCOMPARE(doc.history()->index(), baseline);
        doc.setLayerClipped(false); const auto upper = active(doc); doc.changeLayer(upper.name, true, false, true, 1);
        baseline = doc.history()->index(); QVERIFY(!doc.mergeDown()); QCOMPARE(doc.history()->index(), baseline);
    }
    void flattenPreservesSeparateNotesAndMovieAndGroupBlending() {
        auto project = Project::create(); project.transparent = true;
        auto &layers = project.shots.first().layers;
        auto group = RasterLayer::create("Paint"); group.group = true; group.opacity = .75;
        layers.first().parentId = group.id;
        QVERIFY(paintDab(layers.first(), {100, 100}, 30, Qt::red, 1, false, {-120, -120, 2160, 1320}, 1024));
        layers.append(group);
        auto upper = RasterLayer::create("Shading"); upper.blendMode = LayerBlendMode::Multiply;
        QVERIFY(paintDab(upper, {105, 100}, 30, Qt::blue, .5, false, {-120, -120, 2160, 1320}, 1024)); layers.append(upper);
        auto notes = RasterLayer::create("Notes"); notes.annotation = true;
        QVERIFY(paintDab(notes, {102, 100}, 15, Qt::green, .8, false, {-220, -180, 2360, 1480}, 1024));
        QVERIFY(paintDab(notes, {-160, 50}, 15, Qt::green, 1, false, {-220, -180, 2360, 1480}, 1024)); layers.append(notes);
        const auto frame = renderMovieFrame(project), sheet = renderSheet(project, project.shots.first());
        Document doc; doc.load(project); QVERIFY(doc.flattenImage());
        QCOMPARE(doc.project().shots.first().layers.size(), 2);
        QVERIFY(!doc.project().shots.first().layers[0].annotation); QVERIFY(doc.project().shots.first().layers[1].annotation);
        QCOMPARE(renderMovieFrame(doc.project()), frame); QCOMPARE(renderSheet(doc.project(), doc.project().shots.first()), sheet);
        QCOMPARE(doc.history()->index(), 1); doc.history()->undo(); QCOMPARE(doc.project().shots.first().layers.size(), 4);
        doc.history()->redo(); QCOMPARE(renderMovieFrame(doc.project()), frame);
    }
    void flattenRefusesInterleavedNotesAndLockedContent() {
        Document doc; doc.load(Project::create()); const auto original = active(doc).id;
        doc.addLayer(true); doc.addLayer(); int baseline = doc.history()->index();
        QVERIFY(!doc.flattenImage()); QCOMPARE(doc.history()->index(), baseline);
        doc.deleteLayer(); doc.setLayersLocked({original}, true); baseline = doc.history()->index();
        QVERIFY(!doc.flattenImage()); QCOMPARE(doc.history()->index(), baseline);
        QVERIFY(doc.project().validate().isEmpty());
    }
};
QTEST_MAIN(LayerDocumentTest)
#include "test_layer_document.moc"
