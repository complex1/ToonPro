#include "tool_session.h"
#include "tool_navigation_panel.h"
#include "drawing_tool_controller.h"
#include <QAction>
#include <QApplication>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
using namespace toon;

namespace {
Project artwork() {
    auto project = Project::create(); project.width = 640; project.height = 480; project.transparent = true;
    project.shots[0].sheet.overscan = 0;
    QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::transparent);
    QPainter painter(&tile); painter.fillRect(QRect(60, 60, 60, 60), Qt::red); painter.end();
    project.shots[0].layers[0].tiles[{0,0}] = tile;
    return project;
}
void moveRaster(Canvas &canvas) {
    canvas.setTool(Canvas::Tool::Transform);
    QVERIFY(canvas.document()->transformActive());
    TransformParameters transform; transform.translation = {80, 20};
    QVERIFY(canvas.document()->previewTransform(transform));
}
void answer(const char *name, int *count = nullptr) {
    QTimer::singleShot(0, [name, count] {
        auto *dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        QVERIFY2(dialog, "Expected a pending-transform dialog");
        QCOMPARE(dialog->objectName(), QString("pendingTransformDialog"));
        auto *button = dialog->findChild<QPushButton*>(name); QVERIFY(button);
        if (count) ++*count;
        button->click();
    });
}
}
class TransformToolSwitchTests final : public QObject {
    Q_OBJECT
private slots:
    void directSwitchCancelPreservesPreviewThenApplyCommitsOnce() {
        Canvas canvas; canvas.setProject(artwork()); moveRaster(canvas);
        const auto original = renderMovieFrame(canvas.project());
        const auto matrix = canvas.document()->transformMatrix();
        answer("cancelPendingTransform"); canvas.setTool(Canvas::Tool::Brush);
        QCOMPARE(canvas.tool(), Canvas::Tool::Transform); QVERIFY(canvas.document()->transformActive());
        QCOMPARE(canvas.document()->transformMatrix(), matrix); QCOMPARE(renderMovieFrame(canvas.project()), original);
        QCOMPARE(canvas.document()->history()->count(), 0);
        answer("applyPendingTransform"); canvas.setTool(Canvas::Tool::Brush);
        QCOMPARE(canvas.tool(), Canvas::Tool::Brush); QVERIFY(!canvas.document()->transformActive());
        QCOMPARE(renderMovieFrame(canvas.project()).pixelColor(150,100), QColor(Qt::red));
        QCOMPARE(canvas.document()->history()->count(), 1);
        canvas.document()->history()->undo(); QCOMPARE(renderMovieFrame(canvas.project()), original);
    }
    void discardKeepsOriginalAndIdentityNeedsNoDialog() {
        Canvas canvas; canvas.setProject(artwork()); const auto original = renderMovieFrame(canvas.project());
        canvas.setTool(Canvas::Tool::Transform); canvas.setTool(Canvas::Tool::Brush);
        QCOMPARE(canvas.tool(), Canvas::Tool::Brush); QVERIFY(!canvas.document()->transformActive());
        moveRaster(canvas); answer("discardPendingTransform"); canvas.setTool(Canvas::Tool::Eraser);
        QCOMPARE(canvas.tool(), Canvas::Tool::Eraser); QCOMPARE(renderMovieFrame(canvas.project()), original);
        QCOMPARE(canvas.document()->history()->count(), 0);
    }
    void escapeDiscardsAndSpacePanKeepsPendingTransform() {
        Canvas canvas; canvas.resize(800,600); canvas.setProject(artwork()); canvas.show(); canvas.setFocus();
        moveRaster(canvas); const auto matrix = canvas.document()->transformMatrix();
        QTest::keyPress(&canvas, Qt::Key_Space); QTest::keyRelease(&canvas, Qt::Key_Space);
        QCOMPARE(canvas.document()->transformMatrix(), matrix); QVERIFY(canvas.document()->transformActive());
        canvas.setTool(Canvas::Tool::Transform); QCOMPARE(canvas.document()->transformMatrix(), matrix);
        QTest::keyClick(&canvas, Qt::Key_Escape); QVERIFY(!canvas.document()->transformActive());
        canvas.setTool(Canvas::Tool::Brush); QCOMPARE(canvas.document()->history()->count(), 0);
    }
    void catalogCancelKeepsEntrySettingsAndShortcutRoute() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        Canvas canvas; canvas.setProject(artwork()); ToolSession session(&canvas, &catalog);
        moveRaster(canvas); canvas.setBrushSize(37);
        const auto tool = catalog.activeToolId(), entry = catalog.selectedEntryId();
        const auto state = catalog.tool(tool)->state;
        answer("cancelPendingTransform"); QVERIFY(!catalog.selectEntry("group/shapes"));
        QCOMPARE(catalog.activeToolId(), tool); QCOMPARE(catalog.selectedEntryId(), entry);
        QVERIFY(catalog.tool(tool)->state == state); QCOMPARE(canvas.brush.size, 37.);
        answer("cancelPendingTransform"); canvas.setTool(Canvas::Tool::Brush);
        QCOMPARE(catalog.activeToolId(), tool); QCOMPARE(canvas.tool(), Canvas::Tool::Transform);
        answer("applyPendingTransform"); QVERIFY(catalog.selectEntry("group/shapes"));
        QCOMPARE(catalog.activeToolId(), QString("line")); QCOMPARE(canvas.tool(), Canvas::Tool::Line);
        QCOMPARE(canvas.document()->history()->count(), 1);
    }
    void perspectiveToWarpGuardRunsBeforeVariantRestoration() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        Canvas canvas; canvas.setProject(artwork()); ToolSession session(&canvas, &catalog);
        QVERIFY(catalog.activateTool("advanced-transform"));
        auto *controller = canvas.drawingTools(); QVERIFY(controller->transform()->active());
        QVERIFY(controller->transform()->setMatrix(QTransform::fromTranslate(80,20)));
        const auto entry = catalog.selectedEntryId();
        answer("cancelPendingTransform"); QVERIFY(!catalog.activateTool("warp"));
        QCOMPARE(catalog.selectedEntryId(), entry); QVERIFY(!controller->warpMode);
        QCOMPARE(controller->transform()->matrix(), QTransform::fromTranslate(80,20));
        answer("discardPendingTransform"); QVERIFY(catalog.activateTool("warp"));
        QVERIFY(controller->warpMode); QVERIFY(controller->transform()->active());
        QCOMPARE(canvas.document()->history()->count(), 0);
        auto nodes = controller->transform()->controlPoints(); nodes[4] += QPointF(12,8);
        QVERIFY(controller->transform()->setWarp(nodes));
        answer("applyPendingTransform"); QVERIFY(catalog.activateTool("brush/builtin/ink"));
        QCOMPARE(canvas.document()->history()->count(), 1); QVERIFY(!controller->transform()->active());
    }
    void boundaryTransformAlsoResolvesBeforeSwitch() {
        Canvas canvas; canvas.setProject(artwork()); QPainterPath selection; selection.addRect(40,40,100,100);
        canvas.document()->setRasterSelection(selection);
        QVERIFY(canvas.drawingTools()->beginAdvanced({},true));
        QVERIFY(canvas.drawingTools()->transform()->setMatrix(QTransform::fromTranslate(30,20)));
        answer("cancelPendingTransform"); canvas.setTool(Canvas::Tool::Brush);
        QVERIFY(canvas.drawingTools()->transform()->active()); QCOMPARE(canvas.document()->rasterSelection().path,selection);
        answer("applyPendingTransform"); canvas.setTool(Canvas::Tool::Brush);
        QCOMPARE(canvas.document()->rasterSelection().path.boundingRect(), QRectF(70,60,100,100));
        QCOMPARE(canvas.document()->history()->count(), 1);
        canvas.document()->history()->undo(); QCOMPARE(canvas.document()->rasterSelection().path,selection);
    }
    void organizationCancelPreservesMutationAndUndoIndex() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        Canvas canvas; canvas.setProject(artwork()); ToolSession session(&canvas, &catalog);
        const auto group = catalog.addGroup("Transform", "move", {"transform"}); QVERIFY(!group.isEmpty());
        TransformParameters transform; transform.translation={80,20}; QVERIFY(canvas.document()->previewTransform(transform));
        const int index = catalog.history()->index();
        answer("cancelPendingTransform"); QVERIFY(!catalog.removeEntry(group));
        QVERIFY(catalog.entry(group)); QCOMPARE(catalog.history()->index(),index);
        answer("cancelPendingTransform"); QVERIFY(catalog.duplicateTool("transform",group).isEmpty());
        QCOMPARE(catalog.history()->index(),index);
        answer("cancelPendingTransform"); catalog.history()->undo();
        QCOMPARE(catalog.history()->index(),index); QVERIFY(catalog.entry(group));
        answer("discardPendingTransform"); catalog.history()->undo();
        QCOMPARE(catalog.history()->index(),index-1); QVERIFY(!catalog.entry(group));
        QVERIFY(!canvas.document()->transformActive()); QCOMPARE(canvas.document()->history()->count(),0);
    }
    void failedApplyKeepsToolCatalogAndPendingArtwork() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        auto project = artwork(); project.width = project.height = 8192;
        QImage opaque(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied); opaque.fill(Qt::red);
        auto &layers = project.shots[0].layers; layers[0].tiles.clear(); layers[0].tiles[{0,0}]=opaque;
        auto other = RasterLayer::create("Other");
        // Shared images keep the fixture small while occupying the tile budget.
        for (int i=0;i<maxProjectTiles-1;++i) other.tiles[{i%32,i/32}]=opaque;
        layers.append(other);
        Canvas canvas; canvas.setProject(project); ToolSession session(&canvas,&catalog);
        canvas.setTool(Canvas::Tool::Transform); QVERIFY(canvas.document()->transformActive());
        TransformParameters transform; transform.scaleX=transform.scaleY=2; transform.translation={128,128};
        QVERIFY(canvas.document()->previewTransform(transform));
        const auto tool=catalog.activeToolId(),entry=catalog.selectedEntryId();
        QSignalSpy warnings(canvas.document(),&Document::warning);
        answer("applyPendingTransform"); QVERIFY(!catalog.activateTool("brush/builtin/ink"));
        QVERIFY(!warnings.isEmpty()); QCOMPARE(catalog.activeToolId(),tool); QCOMPARE(catalog.selectedEntryId(),entry);
        QCOMPARE(canvas.tool(),Canvas::Tool::Transform); QVERIFY(canvas.document()->transformActive());
        QCOMPARE(canvas.document()->transformParameters().translation,QPointF(128,128));
        QCOMPARE(canvas.document()->history()->count(),0);
        canvas.document()->cancelTransform();
    }
    void rejectedMouseClickRestoresRowAndPromptsOnce() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        QWidget window; Canvas canvas(&window); canvas.setProject(artwork()); ToolSession session(&canvas, &catalog);
        ToolNavigationPanel panel(&catalog,true,&window); panel.resize(58,700); window.resize(800,800); window.show();
        moveRaster(canvas); auto *list = panel.findChild<QListWidget*>(); QVERIFY(list);
        const auto previous = list->currentItem(); QVERIFY(previous);
        QListWidgetItem *target=nullptr; for(int i=0;i<list->count();++i)if(list->item(i)->text()=="Shapes")target=list->item(i);
        QVERIFY(target); int count=0; answer("cancelPendingTransform",&count);
        QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,list->visualItemRect(target).center());
        QCOMPARE(count,1); QCOMPARE(list->currentItem(),previous); QVERIFY(canvas.document()->transformActive());
    }
};
QTEST_MAIN(TransformToolSwitchTests)
#include "test_transform_tool_switch.moc"
