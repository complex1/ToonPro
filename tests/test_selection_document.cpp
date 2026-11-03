#include "document.h"
#include "canvas.h"
#include "main_window.h"
#include "drawing_tool_controller.h"
#include "object_tools.h"
#include <QClipboard>
#include <QFileInfo>
#include <QAction>
#include <QMessageBox>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <limits>

using namespace toon;
namespace {
QPainterPath rectangle(QRectF rect) { QPainterPath path; path.addRect(rect); return path; }
Project artwork() {
    auto project = Project::create(); project.transparent = true; project.width = 256; project.height = 256;
    QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::transparent);
    QPainter painter(&tile); painter.fillRect(20, 20, 20, 20, Qt::red); painter.fillRect(90, 90, 20, 20, Qt::blue); painter.end();
    project.shots[0].layers[0].tiles[{0, 0}] = tile; return project;
}
void tap(Document &document, QPointF point, bool erase = false) {
    BrushSettings brush; brush.size = 60; brush.color = Qt::green;
    document.beginStroke(point, 1, brush, erase); document.endStroke();
}
}
class SelectionDocumentTest : public QObject {
    Q_OBJECT
private slots:
    void fragmentedSelectionsRemainEditableAndRecoverable() {
        auto project = Project::create(); project.width = project.height = 512;
        project.shots[0].sheet.overscan = 0; Document document; document.load(project);
        QPainterPath islands;
        for (int y = 4; y < 512; y += 8) for (int x = 4; x < 512; x += 8) islands.addRect(x, y, 3, 3);
        QVERIFY(islands.elementCount() > 4096);
        QSignalSpy warnings(&document, &Document::warning);
        document.setRasterSelection(islands);
        document.invertRasterSelection();
        QVERIFY(!document.rasterSelection().path.contains({5, 5}));
        QVERIFY(document.rasterSelection().path.contains({8, 8}));
        document.invertRasterSelection();
        QVERIFY(document.rasterSelection().path.contains({5, 5}));
        QVERIFY(!document.rasterSelection().path.contains({8, 8}));
        document.combineRasterSelection(rectangle({0, 0, 128, 512}), SelectionCombine::Add);
        QVERIFY(document.rasterSelection().path.contains({8, 8}));
        QVERIFY(!document.rasterSelection().path.contains({136, 8}));
        document.combineRasterSelection(rectangle({0, 0, 128, 512}), SelectionCombine::Subtract);
        QVERIFY(!document.rasterSelection().path.contains({5, 5}));
        QVERIFY(document.rasterSelection().path.contains({133, 5}));
        document.combineRasterSelection(rectangle({128, 0, 128, 512}), SelectionCombine::Intersect);
        QVERIFY(document.rasterSelection().path.contains({133, 5}));
        QVERIFY(!document.rasterSelection().path.contains({261, 5}));
        document.setRasterSelection(islands); document.modifyRasterSelection(1);
        QVERIFY(document.rasterSelection().path.contains({3.5, 5.5}));
        document.setRasterSelection(islands); document.modifyRasterSelection(-1);
        QVERIFY(document.rasterSelection().path.contains({5.5, 5.5}));
        QVERIFY(!document.rasterSelection().path.contains({4.5, 5.5}));
        // Clipping a dense selection is also bounded, including negative overscan.
        document.setRasterSelection(islands.translated(-10, -10));
        QVERIFY(document.drawingBounds().contains(document.rasterSelection().path.boundingRect()));
        QCOMPARE(warnings.count(), 0); QCOMPARE(document.history()->count(), 0);
        document.clearRasterSelection(); tap(document, {100, 100});
        QCOMPARE(document.history()->count(), 1);
        QVERIFY(!document.rasterSelection().active); QVERIFY(!document.editingGestureActive());
    }
    void oversizedDetailedOperationPreservesSelectionAndCanDeselect() {
        auto project = Project::create(); project.width = project.height = 8192;
        project.shots[0].sheet.overscan = 0; Document document; document.load(project);
        QPainterPath islands;
        for (int y = 4; y < 512; y += 8) for (int x = 4; x < 512; x += 8) islands.addRect(x, y, 3, 3);
        document.setRasterSelection(islands); QSignalSpy warnings(&document, &Document::warning);
        document.invertRasterSelection();
        QCOMPARE(warnings.count(), 1); QCOMPARE(document.rasterSelection().path, islands);
        document.clearRasterSelection(); QVERIFY(!document.rasterSelection().active);
    }
    void combineSelectionsPreservesHolesAndDoesNotEditPixels() {
        Document document; document.load(artwork());
        const auto original = renderMovieFrame(document.project());
        document.combineRasterSelection(rectangle({20, 20, 40, 40}), SelectionCombine::Add);
        document.combineRasterSelection(rectangle({50, 20, 40, 40}), SelectionCombine::Add);
        QCOMPARE(document.rasterSelection().path.boundingRect(), QRectF(20, 20, 70, 40));
        document.combineRasterSelection(rectangle({30, 30, 10, 10}), SelectionCombine::Subtract);
        QVERIFY(!document.rasterSelection().path.contains({35, 35}));
        QVERIFY(document.rasterSelection().path.contains({25, 35}));
        document.combineRasterSelection(rectangle({15, 32, 90, 10}), SelectionCombine::Intersect);
        QVERIFY(document.rasterSelection().path.contains({25, 35}));
        QVERIFY(!document.rasterSelection().path.contains({35, 35}));
        QVERIFY(!document.rasterSelection().path.contains({25, 25}));
        document.combineRasterSelection(rectangle({140, 140, 10, 10}), SelectionCombine::Intersect);
        QVERIFY(document.rasterSelection().active); QVERIFY(document.rasterSelection().path.isEmpty());
        document.clearRasterSelection();
        // A fresh gesture always starts a selection after Deselect, even if a mode remains chosen.
        document.combineRasterSelection(rectangle({10, 10, 20, 20}), SelectionCombine::Subtract);
        QCOMPARE(document.rasterSelection().path.boundingRect(), QRectF(10, 10, 20, 20));
        QCOMPARE(renderMovieFrame(document.project()), original);
        QCOMPARE(document.history()->count(), 0); QVERIFY(document.history()->isClean());
    }
    void growShrinkAreBoundedTransientAndRespectBusyState() {
        Document document; document.load(artwork());
        document.setRasterSelection(rectangle({20, 20, 40, 40}));
        document.modifyRasterSelection(5);
        QCOMPARE(document.rasterSelection().path.boundingRect(), QRectF(15, 15, 50, 50));
        QVERIFY(document.rasterSelection().path.contains({16, 40}));
        document.modifyRasterSelection(-10);
        QVERIFY(document.rasterSelection().path.contains({30, 40}));
        QVERIFY(!document.rasterSelection().path.contains({22, 40}));
        const auto selection = document.rasterSelection().path;
        QSignalSpy warnings(&document, &Document::warning);
        document.modifyRasterSelection(101); document.modifyRasterSelection(0);
        document.modifyRasterSelection(std::numeric_limits<qreal>::quiet_NaN());
        QCOMPARE(document.rasterSelection().path, selection); QCOMPARE(warnings.count(), 3);
        QVERIFY(document.beginTransform());
        document.modifyRasterSelection(10);
        document.combineRasterSelection(rectangle({90, 90, 20, 20}), SelectionCombine::Replace);
        QCOMPARE(document.rasterSelection().path, selection);
        document.cancelTransform();
        document.modifyRasterSelection(-100);
        QVERIFY(document.rasterSelection().active); QVERIFY(document.rasterSelection().path.isEmpty());
        document.selectAllPixels(); document.modifyRasterSelection(100);
        QCOMPARE(document.rasterSelection().path.boundingRect(), document.drawingBounds());
        QCOMPARE(document.history()->count(), 0);
    }
    void selectionIsTransientAndEmptyMeansNoCoverage() {
        Document document; document.load(Project::create());
        const auto before = document.project().toJson();
        document.setRasterSelection(rectangle({10, 10, 20, 20}));
        QVERIFY(document.rasterSelection().active); QVERIFY(document.history()->isClean());
        QCOMPARE(document.project().toJson(), before);
        document.selectAllPixels(); QCOMPARE(document.rasterSelection().path.boundingRect(), document.drawingBounds());
        document.invertRasterSelection(); QVERIFY(document.rasterSelection().active); QVERIFY(document.rasterSelection().path.isEmpty());
        tap(document, {100, 100}); QCOMPARE(document.history()->count(), 0);
        QVERIFY(document.project().shots[0].layers[0].tiles.isEmpty());
        document.clearRasterSelection(); QVERIFY(!document.rasterSelection().active);
        tap(document, {100, 100}); QCOMPARE(document.history()->count(), 1);
        document.load(Project::create()); QVERIFY(!document.rasterSelection().active);
    }
    void paintingAndErasingRespectSelectionAndUndo() {
        auto project = Project::create(); project.transparent = true;
        Document document; document.load(project);
        document.setRasterSelection(rectangle({10, 10, 20, 20})); tap(document, {20, 20});
        auto image = renderMovieFrame(document.project());
        QCOMPARE(image.pixelColor(10, 10), QColor(Qt::green)); QCOMPARE(image.pixelColor(29, 29), QColor(Qt::green));
        QCOMPARE(image.pixelColor(9, 20).alpha(), 0); QCOMPARE(image.pixelColor(30, 20).alpha(), 0);
        document.setRasterSelection(rectangle({15, 15, 10, 10})); tap(document, {20, 20}, true);
        image = renderMovieFrame(document.project()); QCOMPARE(image.pixelColor(20, 20).alpha(), 0);
        QCOMPARE(image.pixelColor(10, 10), QColor(Qt::green));
        document.history()->undo(); QCOMPARE(renderMovieFrame(document.project()).pixelColor(20, 20), QColor(Qt::green));
        document.history()->redo(); QCOMPARE(renderMovieFrame(document.project()).pixelColor(20, 20).alpha(), 0);
    }
    void previewApplyCancelUndoAndPortablePixels() {
        Document document; document.load(artwork());
        document.setRasterSelection(rectangle({18, 18, 24, 24})); const auto originalSelection = document.rasterSelection();
        const auto original = renderMovieFrame(document.project());
        QVERIFY(document.beginTransform()); QCOMPARE(document.transformBounds(), QRectF(20, 20, 20, 20));
        TransformParameters parameters; parameters.translation = {40, 0};
        QVERIFY(document.previewTransform(parameters));
        QCOMPARE(renderMovieFrame(document.project()), original);
        const auto preview = renderMovieFrame(document.displayProject());
        QCOMPARE(preview.pixelColor(25, 25).alpha(), 0); QCOMPARE(preview.pixelColor(65, 25), QColor(Qt::red));
        QCOMPARE(preview.pixelColor(95, 95), QColor(Qt::blue)); QVERIFY(document.history()->isClean());
        document.cancelTransform(); QCOMPARE(renderMovieFrame(document.displayProject()), original);
        QCOMPARE(document.rasterSelection().path, originalSelection.path);
        QVERIFY(document.beginTransform()); QVERIFY(document.previewTransform(parameters)); document.applyTransform();
        QVERIFY(!document.transformActive()); QCOMPARE(document.history()->count(), 1);
        QCOMPARE(renderMovieFrame(document.project()), preview);
        QCOMPARE(document.rasterSelection().path.boundingRect(), QRectF(58, 18, 24, 24));
        document.history()->undo(); QCOMPARE(renderMovieFrame(document.project()), original);
        QCOMPARE(document.rasterSelection().path, originalSelection.path);
        document.history()->redo(); QCOMPARE(renderMovieFrame(document.project()), preview);
        QTemporaryDir temporary; QString error;
        const auto path = temporary.filePath("transformed.toonproj");
        QVERIFY2(saveProject(document.project(), path, &error), qPrintable(error));
        const auto reopened = loadProject(path, &error); QVERIFY2(reopened.has_value(), qPrintable(error));
        document.load(*reopened); QCOMPARE(renderMovieFrame(document.project()), preview);
        QVERIFY(!document.rasterSelection().active);
    }
    void invalidPreviewPreservesLastValidAndIdentityIsClean() {
        Document document; document.load(artwork());
        QVERIFY(document.beginTransform()); QVERIFY(document.previewTransform({})); document.applyTransform();
        QCOMPARE(document.history()->count(), 0);
        QVERIFY(document.beginTransform()); TransformParameters valid; valid.translation = {30, 40};
        QVERIFY(document.previewTransform(valid)); const auto preview = renderMovieFrame(document.displayProject());
        auto invalid = valid; invalid.translation = {50000, 0}; QSignalSpy warnings(&document, &Document::warning);
        QVERIFY(!document.previewTransform(invalid)); QCOMPARE(warnings.count(), 1);
        QCOMPARE(renderMovieFrame(document.displayProject()), preview);
        invalid = valid; invalid.scaleX = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!document.previewTransform(invalid)); QCOMPARE(renderMovieFrame(document.displayProject()), preview);
        document.applyTransform(); QCOMPARE(renderMovieFrame(document.project()), preview);
    }
    void outsidePreviewCanReturnAndOnlyApplyCropsSelectedPixels() {
        auto project = artwork(); project.shots[0].sheet.overscan = 0;
        Document document; document.load(project);
        const auto selected = rectangle({20, 20, 20, 20}); document.setRasterSelection(selected);
        const auto original = renderMovieFrame(project);
        QVERIFY(document.beginTransform()); const auto prepared = document.transformPreview()->moving.tiles;
        TransformParameters movement; movement.translation = {1000, -1000};
        QVERIFY(document.previewTransform(movement));
        QCOMPARE(document.transformPreview()->moving.tiles, prepared);
        QCOMPARE(renderMovieFrame(document.project()), original); QCOMPARE(document.history()->count(), 0);
        QVERIFY(document.previewTransform({}));
        QCOMPARE(renderMovieFrame(document.displayProject()), original);
        movement.translation = {-30, 0}; QVERIFY(document.previewTransform(movement));
        document.cancelTransform(); QCOMPARE(renderMovieFrame(document.project()), original);
        QVERIFY(document.beginTransform()); QVERIFY(document.previewTransform(movement)); QVERIFY(document.applyTransform());
        const auto clipped = renderMovieFrame(document.project());
        QCOMPARE(clipped.pixelColor(0, 25), QColor(Qt::red)); QCOMPARE(clipped.pixelColor(9, 25), QColor(Qt::red));
        QCOMPARE(clipped.pixelColor(10, 25).alpha(), 0); QCOMPARE(clipped.pixelColor(25, 25).alpha(), 0);
        QCOMPARE(clipped.pixelColor(95, 95), QColor(Qt::blue));
        QCOMPARE(document.rasterSelection().path.boundingRect(), QRectF(0, 20, 10, 20));
        QCOMPARE(document.history()->count(), 1);
        document.history()->undo(); QCOMPARE(renderMovieFrame(document.project()), original);
        QCOMPARE(document.rasterSelection().path, selected);
        document.history()->redo(); QCOMPARE(renderMovieFrame(document.project()), clipped);
        document.history()->undo();
        QVERIFY(document.beginTransform()); movement.translation = {1000, 1000};
        QVERIFY(document.previewTransform(movement)); QVERIFY(document.applyTransform());
        QVERIFY(!document.rasterSelection().active);
        QCOMPARE(renderMovieFrame(document.project()).pixelColor(25, 25).alpha(), 0);
        QCOMPARE(renderMovieFrame(document.project()).pixelColor(95, 95), QColor(Qt::blue));
        document.history()->undo(); QCOMPARE(renderMovieFrame(document.project()), original);
        QCOMPARE(document.rasterSelection().path, selected);
    }
    void transformGuardsAndMutationIsolation() {
        Document document; document.load(artwork());
        document.setLayerAlphaLocked(true); QVERIFY(!document.beginTransform()); document.setLayerAlphaLocked(false);
        document.addGroup(); QVERIFY(!document.beginTransform());
        document.changeLayer("Character", true, true, false, 1); document.selectLayer(0); QVERIFY(!document.beginTransform());
        document.selectLayer(1); document.changeLayer("Character", true, false, false, 1); document.selectLayer(0);
        QVERIFY(document.beginTransform());
        const auto before = document.project().toJson(); const int edits = document.history()->index();
        document.addLayer(); document.addGroup(); document.selectLayer(1); document.deleteLayer(); document.moveLayer(1);
        document.changeLayer("Changed", true, false, false, .5); document.setLayerAlphaLocked(true);
        document.clearRasterSelection(); document.selectAllPixels();
        BrushSettings brush; QVERIFY(!document.beginStroke({25, 25}, 1, brush, false));
        QCOMPARE(document.activeLayer(), 0); QCOMPARE(document.history()->index(), edits); QCOMPARE(document.project().toJson(), before);
        QVERIFY(!document.rasterSelection().active);
        document.cancelTransform(); document.setRasterSelection({}); QVERIFY(!document.beginTransform());
    }
    void draggingReusesPreparedPixelsAndDoesNotSignalDocumentEdits() {
        auto project = artwork(); project.width = 3840; project.height = 2160;
        auto &tiles = project.shots.first().layers.first().tiles;
        QImage filled(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); filled.fill(Qt::red);
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 15; ++x) tiles[{x, y}] = filled;
        Document document; document.load(project); document.selectAllPixels();
        const auto before = tiles;
        QVERIFY(document.beginTransform());
        QVERIFY(document.transformPreview());
        const auto prepared = document.transformPreview()->moving.tiles;
        QVERIFY(document.transformPreview()->stationary.tiles.isEmpty());
        QSignalSpy edits(&document, &Document::changed);
        for (int i = 1; i <= 60; ++i) {
            TransformParameters parameters; parameters.translation = {qreal(i), 0};
            parameters.scaleX = parameters.scaleY = .9; parameters.rotation = i / 60.0;
            QVERIFY(document.previewTransform(parameters));
            QCOMPARE(document.transformPreview()->moving.tiles.size(), prepared.size());
            for (auto tile = prepared.cbegin(); tile != prepared.cend(); ++tile)
                QCOMPARE(document.transformPreview()->moving.tiles.value(tile.key()).cacheKey(), tile->cacheKey());
        }
        QCOMPARE(edits.count(), 0); QCOMPARE(document.history()->count(), 0);
        QCOMPARE(document.project().shots.first().layers.first().tiles, before);
        document.cancelTransform(); QVERIFY(!document.transformPreview());
        QCOMPARE(document.project().shots.first().layers.first().tiles, before);
    }
    void failedApplyKeepsPreviewAndCommittedPixels() {
        auto project = artwork();
        auto &tiles = project.shots.first().layers.first().tiles;
        tiles.first().setPixel(255, 20, qRgb(255, 0, 0));
        auto filler = RasterLayer::create("Other drawing");
        QImage empty(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); empty.fill(Qt::transparent);
        for (int i = 0; i < maxProjectTiles - 1; ++i) filler.tiles[{i % 32, i / 32}] = empty;
        project.shots.first().layers.append(filler);
        Document document; document.load(project); QVERIFY(document.beginTransform());
        TransformParameters parameters; parameters.translation = {1, 0};
        QVERIFY(document.previewTransform(parameters));
        QSignalSpy warnings(&document, &Document::warning);
        QVERIFY(!document.applyTransform()); QCOMPARE(warnings.count(), 1);
        QVERIFY(document.transformActive()); QCOMPARE(document.transformParameters().translation, QPointF(1, 0));
        QCOMPARE(document.history()->count(), 0);
        QCOMPARE(document.project().shots.first().layers.first().tiles, project.shots.first().layers.first().tiles);
        document.cancelTransform(); QVERIFY(!document.transformActive());
    }
    void menuUndoCancelsPreviewAndRecoveryUsesCommittedPixels() {
        QTemporaryDir temporary; QString error; const auto path = temporary.filePath("working.toonproj");
        QVERIFY(saveProject(artwork(), path, &error));
        MainWindow window(temporary.filePath("workspace.ini")); QVERIFY(window.openProject(path));
        window.show(); window.activateWindow(); QCoreApplication::processEvents();
        window.canvas()->setFocus(); QTRY_COMPARE(QApplication::focusWidget(), window.canvas());
        auto *document = window.canvas()->document();
        auto *save = window.findChild<QAction *>("saveProjectAction");
        auto *undo = window.findChild<QAction *>("undoAction");
        auto *exportFrame = window.findChild<QAction *>("frameExportAction");
        auto *exportVideo = window.findChild<QAction *>("videoExportAction"); QVERIFY(exportVideo);
        QVERIFY(save); QVERIFY(undo); QVERIFY(exportFrame);
        tap(*document, {160, 160}); const auto committed = renderMovieFrame(document->project());
        QVERIFY(document->beginTransform()); TransformParameters p; p.translation = {30, 0}; QVERIFY(document->previewTransform(p));
        QVERIFY(!save->isEnabled()); QVERIFY(!exportFrame->isEnabled()); QVERIFY(undo->isEnabled());
        QVERIFY(!exportVideo->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(window.findChild<QTimer *>("recoveryTimer"), "timeout", Qt::DirectConnection));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path + ".recovery"), 10000);
        auto recovered = loadProject(path + ".recovery", &error); QVERIFY2(recovered.has_value(), qPrintable(error));
        QCOMPARE(renderMovieFrame(*recovered), committed);
        QVERIFY(undo->isEnabled());
        undo->trigger(); QVERIFY(!document->transformActive()); QCOMPARE(renderMovieFrame(document->project()), committed);
        QCOMPARE(document->history()->index(), 1); QVERIFY(save->isEnabled()); QVERIFY(exportFrame->isEnabled());
        QVERIFY(exportVideo->isEnabled());
        document->history()->setClean();
    }
    void advancedPreviewRecoveryAndMenuUndoUseCommittedPixels() {
        QTemporaryDir temporary; QString error; const auto path = temporary.filePath("advanced.toonproj");
        QVERIFY(saveProject(artwork(), path, &error));
        MainWindow window(temporary.filePath("workspace.ini")); QVERIFY(window.openProject(path));
        window.show(); window.activateWindow(); QCoreApplication::processEvents();
        window.canvas()->setFocus(); QTRY_COMPARE(QApplication::focusWidget(), window.canvas());
        auto *document = window.canvas()->document(); tap(*document, {160,160});
        const auto committed = renderMovieFrame(document->project());
        auto *action = window.findChild<QAction *>("advancedTransformAction"); QVERIFY(action); QVERIFY(action->isEnabled());
        action->trigger(); auto *transform = window.canvas()->drawingTools()->transform(); QVERIFY(transform->active());
        QVERIFY(transform->setMatrix(QTransform::fromTranslate(30,0)));
        auto *undo = window.findChild<QAction *>("undoAction"); auto *save = window.findChild<QAction *>("saveProjectAction");
        QVERIFY(undo->isEnabled()); QVERIFY(!save->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(window.findChild<QTimer *>("recoveryTimer"), "timeout", Qt::DirectConnection));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path + ".recovery"), 10000);
        auto recovered = loadProject(path + ".recovery", &error); QVERIFY2(recovered.has_value(), qPrintable(error));
        QCOMPARE(renderMovieFrame(*recovered), committed);
        QVERIFY(undo->isEnabled());
        undo->trigger(); QVERIFY(!transform->active()); QCOMPARE(renderMovieFrame(document->project()), committed);
        QCOMPARE(document->history()->index(),1); QVERIFY(save->isEnabled()); document->history()->setClean();
    }
    void objectPreviewDoesNotLeakIntoRecovery() {
        QTemporaryDir temporary; QString error; const auto path = temporary.filePath("objects.toonproj");
        QVERIFY(saveProject(artwork(),path,&error)); MainWindow window(temporary.filePath("workspace.ini")); QVERIFY(window.openProject(path));
        auto *document=window.canvas()->document(); QVERIFY(ObjectTools::addLayer(document,LayerKind::Vector));
        const auto id=ObjectTools::addPath(document,{BezierNode::corner({30,30}),BezierNode::corner({80,30})},false,Qt::red,5);
        QVERIFY(!id.isEmpty()); const auto committed=renderMovieFrame(document->project());
        ObjectTools objects(document); QVERIFY(objects.beginEdit(id));
        auto object=*ObjectTools::object(document->project().shots.first().layers[document->activeLayer()],id);
        object.transform=QTransform::fromTranslate(0,50); QVERIFY(objects.previewEdit(object));
        QVERIFY(renderMovieFrame(document->project())!=committed);
        QVERIFY(QMetaObject::invokeMethod(window.findChild<QTimer *>("recoveryTimer"),"timeout",Qt::DirectConnection));
        QVERIFY(!QFileInfo::exists(path+".recovery")); objects.cancelEdit();
        QVERIFY(QMetaObject::invokeMethod(window.findChild<QTimer *>("recoveryTimer"),"timeout",Qt::DirectConnection));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path + ".recovery"), 10000);
        const auto recovered=loadProject(path+".recovery",&error); QVERIFY2(recovered.has_value(),qPrintable(error));
        QCOMPARE(renderMovieFrame(*recovered),committed); document->history()->setClean();
    }
    void pasteMenuCreatesLayerThenAllowsPositioningAndUndo() {
        QTemporaryDir temporary; QString error; const auto path=temporary.filePath("paste.toonproj");
        QVERIFY(saveProject(artwork(),path,&error)); MainWindow window(temporary.filePath("workspace.ini")); QVERIFY(window.openProject(path));
        auto *document=window.canvas()->document(); document->setRasterSelection(rectangle({0,0,5,5}));
        QImage image(10,10,QImage::Format_ARGB32_Premultiplied); image.fill(Qt::green); QGuiApplication::clipboard()->setImage(image);
        auto *paste=window.findChild<QAction *>("pastePixelsAction"); QVERIFY(paste); QVERIFY(paste->isEnabled()); paste->trigger();
        QCOMPARE(document->project().shots.first().layers.size(),2); QVERIFY(document->transformActive()); QVERIFY(!document->rasterSelection().active);
        const int createdIndex=document->history()->index(); QVERIFY(createdIndex>0);
        auto *undo=window.findChild<QAction *>("undoAction"); undo->trigger(); QVERIFY(!document->transformActive());
        QCOMPARE(document->project().shots.first().layers.size(),2); QCOMPARE(document->history()->index(),createdIndex);
        undo->trigger(); QCOMPARE(document->project().shots.first().layers.size(),1); document->history()->setClean();
    }

    void pathAndObjectToolsKeepBackspaceForEditing() {
        QTemporaryDir temporary; QString error; const auto path=temporary.filePath("shortcuts.toonproj");
        QVERIFY(saveProject(artwork(),path,&error)); MainWindow window(temporary.filePath("workspace.ini")); QVERIFY(window.openProject(path));
        auto *canvas=window.canvas(); canvas->document()->setRasterSelection(rectangle({0,0,100,100}));
        auto *clear=window.findChild<QAction *>("clearSelectedPixelsAction"); QVERIFY(clear); QVERIFY(clear->isEnabled());
        for(const auto tool:{Canvas::Tool::Polygon,Canvas::Tool::PolygonSelect,Canvas::Tool::Curve,Canvas::Tool::VectorPen,Canvas::Tool::Text,Canvas::Tool::VectorNode,Canvas::Tool::Guides}) {
            canvas->setTool(tool); QVERIFY(!clear->isEnabled());
        }
        canvas->setTool(Canvas::Tool::Brush); QVERIFY(clear->isEnabled());
    }

};
QTEST_MAIN(SelectionDocumentTest)
#include "test_selection_document.moc"
