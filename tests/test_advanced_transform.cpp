#include "advanced_transform.h"
#include "editable_object.h"
#include <QtTest>
#include <cmath>

using namespace toon;
namespace {
int coordinate(int n) { return int(std::floor(n / qreal(tileSize))); }
void pixel(RasterLayer &layer,int x,int y,QRgb color) {
    const TileKey key(coordinate(x),coordinate(y));
    if(!layer.tiles.contains(key)){QImage image(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);layer.tiles.insert(key,image);}
    layer.tiles[key].setPixel(x-key.first*tileSize,y-key.second*tileSize,color);
}
QRgb pixel(const RasterLayer &layer,int x,int y) {
    const TileKey key(coordinate(x),coordinate(y));const auto tile=layer.tiles.constFind(key);
    return tile==layer.tiles.cend()?0:tile->pixel(x-key.first*tileSize,y-key.second*tileSize);
}
Project project() { auto p=Project::create();p.width=320;p.height=240;p.shots.first().layers={RasterLayer::create("Ink")};return p; }
QPainterPath rectangle(QRectF r){QPainterPath path;path.addRect(r);return path;}
void block(RasterLayer &layer,QRect rect,QRgb color){for(int y=rect.top();y<=rect.bottom();++y)for(int x=rect.left();x<=rect.right();++x)pixel(layer,x,y,color);}
QImage render(const QVector<RasterLayer> &layers,const RasterLayerPreview *preview=nullptr){QImage image(320,240,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter p(&image);paintLayers(p,layers,true,preview);return image;}
}
class AdvancedTransformTest:public QObject {
    Q_OBJECT
private slots:
    void flipNegativeOverscanAndUndo() {
        auto p=project();auto &layer=p.shots.first().layers[0];
        pixel(layer,-12,20,qRgb(220,20,20));pixel(layer,-9,20,qRgb(20,50,220));
        Document doc;doc.load(p);AdvancedTransform transform;QVERIFY(transform.begin(&doc));
        QVERIFY(doc.editingGestureActive());const auto before=doc.project().shots.first().layers[0].tiles;
        QVERIFY(transform.flip(true));QCOMPARE(doc.project().shots.first().layers[0].tiles,before);
        QVERIFY(transform.apply());QVERIFY(!doc.editingGestureActive());QCOMPARE(doc.history()->count(),1);
        QCOMPARE(pixel(doc.project().shots.first().layers[0],-12,20),qRgb(20,50,220));
        QCOMPARE(pixel(doc.project().shots.first().layers[0],-9,20),qRgb(220,20,20));
        doc.history()->undo();QCOMPARE(doc.project().shots.first().layers[0].tiles,before);
    }
    void quadPreviewAndCommitAgreeInsideProjectiveShape() {
        auto p=project();block(p.shots.first().layers[0],{40,40,80,80},qRgb(240,60,30));
        Document doc;doc.load(p);AdvancedTransform transform;QVERIFY(transform.begin(&doc));
        QVERIFY(transform.setQuad({{65,30},{140,45},{130,150},{40,120}}));
        const auto preview=render(doc.project().shots.first().layers,transform.preview());
        QCOMPARE(preview.pixelColor(90,80),QColor(240,60,30));QVERIFY(preview.pixelColor(42,42).alpha()==0);
        QVERIFY(transform.apply());const auto committed=render(doc.project().shots.first().layers);
        QCOMPARE(committed.pixelColor(90,80),preview.pixelColor(90,80));QVERIFY(committed.pixelColor(42,42).alpha()==0);
    }
    void folderAndMultipleLayersKeepTheirStructureAndOneUndo() {
        auto p=project();auto group=RasterLayer::create("Folder");group.group=true;
        auto first=RasterLayer::create("Base"),second=RasterLayer::create("Shade");first.parentId=second.parentId=group.id;
        first.opacity=.75;second.blendMode=LayerBlendMode::Multiply;second.clipped=true;
        block(first,{30,30,20,20},qRgb(210,140,60));block(second,{35,35,10,10},qRgb(20,50,200));
        p.shots.first().layers={group,first,second};Document doc;doc.load(p);AdvancedTransform transform;
        QVERIFY(transform.begin(&doc,{group.id}));QCOMPARE(transform.preview()->additional.size(),1);
        QVERIFY(transform.setMatrix(QTransform::fromTranslate(40,20)));const auto preview=render(doc.project().shots.first().layers,transform.preview());
        QVERIFY(preview.pixelColor(75,55).alpha()>0);QCOMPARE(preview.pixelColor(35,35).alpha(),0);
        QVERIFY(transform.apply());QCOMPARE(doc.history()->count(),1);
        const auto &layers=doc.project().shots.first().layers;QCOMPARE(layers.size(),3);QCOMPARE(layers[1].parentId,group.id);
        QCOMPARE(layers[1].opacity,.75);QVERIFY(layers[2].clipped);QCOMPARE(layers[2].blendMode,LayerBlendMode::Multiply);
        QCOMPARE(pixel(layers[1],70,50),qRgb(210,140,60));QCOMPARE(pixel(layers[2],75,55),qRgb(20,50,200));
        doc.history()->undo();QCOMPARE(doc.project().shots.first().layers[1].tiles,first.tiles);
    }
    void selectionBoundaryOnlyPreservesPixelsAndUndoes() {
        auto p=project();block(p.shots.first().layers[0],{20,20,80,80},qRgb(80,120,190));Document doc;doc.load(p);
        const auto selected=rectangle({20,20,30,40});doc.setRasterSelection(selected);AdvancedTransform transform;
        QVERIFY(transform.begin(&doc,{},true));QVERIFY(!transform.preview());
        QVERIFY(transform.setMatrix(QTransform::fromTranslate(50,20)));
        QCOMPARE(transform.selectionPath().boundingRect(),QRectF(70,40,30,40));
        const auto count=doc.project().shots.first().layers.size();doc.addLayer();QCOMPARE(doc.project().shots.first().layers.size(),count);
        QVERIFY(transform.apply());QCOMPARE(doc.project().shots.first().layers[0].tiles,p.shots.first().layers[0].tiles);
        QCOMPARE(doc.rasterSelection().path.boundingRect(),QRectF(70,40,30,40));doc.history()->undo();QCOMPARE(doc.rasterSelection().path,selected);
    }
    void warpMovesCenterAndLeavesUnselectedPixels() {
        auto p=project();block(p.shots.first().layers[0],{40,40,100,100},qRgb(20,80,170));
        block(p.shots.first().layers[0],{86,86,8,8},qRgb(240,80,20));pixel(p.shots.first().layers[0],200,50,qRgb(10,210,40));
        Document doc;doc.load(p);doc.setRasterSelection(rectangle({40,40,100,100}));AdvancedTransform transform;QVERIFY(transform.begin(&doc));
        auto nodes=transform.controlPoints();nodes[4]+=QPointF(20,10);QVERIFY(transform.setWarp(nodes));
        const auto preview=render(doc.project().shots.first().layers,transform.preview());QVERIFY(preview.pixelColor(110,100).red()>200);
        QVERIFY(transform.apply());const auto &layer=doc.project().shots.first().layers[0];QVERIFY(qRed(pixel(layer,110,100))>200);
        QCOMPARE(pixel(layer,200,50),qRgb(10,210,40));QCOMPARE(doc.history()->count(),1);
        doc.history()->undo();QCOMPARE(doc.project().shots.first().layers[0].tiles,p.shots.first().layers[0].tiles);
    }
    void invalidQuadWarpAndLockedDescendantNeverMutate() {
        auto p=project();block(p.shots.first().layers[0],{30,30,40,40},qRgb(20,50,80));Document doc;doc.load(p);AdvancedTransform transform;
        QVERIFY(transform.begin(&doc));QVERIFY(!transform.setQuad({{20,20},{70,70},{70,20},{20,70}}));
        auto nodes=transform.controlPoints();nodes[4]=QPointF(1000,1000);QVERIFY(!transform.setWarp(nodes));
        QVERIFY(!transform.setMatrix(QTransform::fromTranslate(50000,0)));transform.cancel();QCOMPARE(doc.history()->count(),0);
        auto folder=RasterLayer::create("Locked group");folder.group=true;folder.locked=true;
        p.shots.first().layers[0].parentId=folder.id;p.shots.first().layers.prepend(folder);doc.load(p);
        QVERIFY(!transform.begin(&doc,{folder.id}));QVERIFY(!doc.editingGestureActive());
    }
    void unchangedMeshIsANoopAndEmptySelectionExtentSurvivesWarp() {
        auto p=project();block(p.shots.first().layers[0],{60,60,20,20},qRgb(40,90,180));Document doc;doc.load(p);
        doc.setRasterSelection(rectangle({20,20,120,120}));AdvancedTransform transform;QVERIFY(transform.begin(&doc));
        QCOMPARE(transform.bounds(),QRectF(20,20,120,120));QVERIFY(transform.setWarp(transform.controlPoints()));
        QVERIFY(transform.apply());QCOMPARE(doc.history()->count(),0);QCOMPARE(doc.project().shots.first().layers[0].tiles,p.shots.first().layers[0].tiles);
        QVERIFY(transform.begin(&doc));auto points=transform.controlPoints();points[4]+=QPointF(5,5);QVERIFY(transform.setWarp(points));
        QCOMPARE(transform.selectionPath().boundingRect(),QRectF(20,20,120,120));
    }
    void detailedSelectionWarpRejectsWithoutTrappingTheGesture_data() {
        QTest::addColumn<bool>("boundaryOnly");
        QTest::newRow("artwork-and-selection") << false;
        QTest::newRow("selection-boundary") << true;
    }
    void detailedSelectionWarpRejectsWithoutTrappingTheGesture() {
        QFETCH(bool, boundaryOnly); auto p=project();
        block(p.shots.first().layers[0],{20,20,200,160},qRgb(30,80,180));
        QPainterPath selected;
        for(int y=20;y<180;y+=4)for(int x=20;x<220;x+=4)selected.addRect(x,y,2,2);
        QVERIFY(selected.elementCount()>4096);
        Document doc;doc.load(p);doc.setRasterSelection(selected);AdvancedTransform transform;
        QVERIFY(transform.begin(&doc,{},boundaryOnly));
        const auto translated=QTransform::fromTranslate(10,5);QVERIFY(transform.setMatrix(translated));
        const auto previous=transform.selectionPath();auto points=transform.controlPoints();points[4]+=QPointF(5,3);
        QSignalSpy warnings(&doc,&Document::warning);QElapsedTimer timer;timer.start();
        QVERIFY(!transform.setWarp(points));QVERIFY(timer.elapsed()<500);
        QCOMPARE(warnings.count(),1);QVERIFY(warnings.first()[0].toString().contains("mesh warp"));
        QVERIFY(transform.active());QVERIFY(!transform.warped());QCOMPARE(transform.matrix(),translated);
        QCOMPARE(transform.selectionPath(),previous);QCOMPARE(doc.rasterSelection().path,selected);
        QCOMPARE(doc.project().shots.first().layers[0].tiles,p.shots.first().layers[0].tiles);
        QVERIFY(transform.apply());QVERIFY(!doc.editingGestureActive());QCOMPARE(doc.history()->count(),1);
        QCOMPARE(doc.rasterSelection().path,previous);
        doc.history()->undo();QCOMPARE(doc.rasterSelection().path,selected);
        QVERIFY(transform.begin(&doc,{},boundaryOnly));QVERIFY(!transform.setWarp(transform.controlPoints()));
        transform.cancel();doc.clearRasterSelection();QVERIFY(!doc.rasterSelection().active);QVERIFY(!doc.editingGestureActive());
        BrushSettings brush;brush.color=Qt::red;brush.size=8;brush.pressureSize=false;
        QVERIFY(doc.beginStroke({30,30},1,brush,false));doc.endStroke();
    }
    void outsidePreviewKeepsSourceUntilApply_data() {
        QTest::addColumn<bool>("mesh");QTest::newRow("matrix")<<false;QTest::newRow("mesh")<<true;
    }
    void outsidePreviewKeepsSourceUntilApply() {
        QFETCH(bool,mesh);auto p=project();p.shots.first().sheet.overscan=0;
        block(p.shots.first().layers[0],{20,30,40,40},qRgb(220,50,40));
        Document doc;doc.load(p);const auto original=p.shots.first().layers[0].tiles;
        const auto selected=rectangle({20,30,40,40});doc.setRasterSelection(selected);
        AdvancedTransform transform;QVERIFY(transform.begin(&doc));
        const auto move=[&](QPointF offset) {
            if(!mesh)return transform.setMatrix(QTransform::fromTranslate(offset.x(),offset.y()));
            transform.setMatrix({});auto nodes=transform.controlPoints();for(auto &node:nodes)node+=offset;
            return transform.setWarp(nodes);
        };
        QVERIFY(move({280,0}));QCOMPARE(doc.project().shots.first().layers[0].tiles,original);
        QImage preview(400,240,QImage::Format_ARGB32_Premultiplied);preview.fill(Qt::transparent);
        {QPainter painter(&preview);paintLayers(painter,doc.project().shots.first().layers,true,transform.preview());}
        QCOMPARE(preview.pixelColor(330,40),QColor(220,50,40));
        QVERIFY(move({0,0}));QCOMPARE(transform.selectionPath().boundingRect(),selected.boundingRect());
        QVERIFY(move({280,0}));transform.cancel();QCOMPARE(doc.project().shots.first().layers[0].tiles,original);
        QCOMPARE(doc.rasterSelection().path,selected);QCOMPARE(doc.history()->count(),0);
        QVERIFY(transform.begin(&doc));QVERIFY(move({280,0}));QVERIFY(transform.apply());
        const auto &after=doc.project().shots.first().layers[0];QCOMPARE(selectedPixelBounds(after),QRectF(300,30,20,40));
        QCOMPARE(pixel(after,310,40),qRgb(220,50,40));QCOMPARE(pixel(after,330,40),QRgb(0));
        QCOMPARE(doc.rasterSelection().path.boundingRect(),QRectF(300,30,20,40));
        doc.history()->undo();QCOMPARE(doc.project().shots.first().layers[0].tiles,original);QCOMPARE(doc.rasterSelection().path,selected);
        QVERIFY(transform.begin(&doc));QVERIFY(move({600,0}));QVERIFY(transform.apply());
        QVERIFY(doc.project().shots.first().layers[0].tiles.isEmpty());QVERIFY(!doc.rasterSelection().active);QVERIFY(doc.rasterSelection().path.isEmpty());
        doc.history()->undo();QCOMPARE(doc.project().shots.first().layers[0].tiles,original);QCOMPARE(doc.rasterSelection().path,selected);
    }
    void groupOffCanvasCommitClipsChildrenAndUndoesTogether() {
        auto p=project();p.shots.first().sheet.overscan=0;
        auto group=RasterLayer::create("Folder");group.group=true;
        auto first=RasterLayer::create("First"),second=RasterLayer::create("Second");first.parentId=second.parentId=group.id;
        block(first,{10,20,30,30},qRgb(210,60,30));block(second,{280,60,20,20},qRgb(30,80,200));
        p.shots.first().layers={group,first,second};Document doc;doc.load(p);AdvancedTransform transform;
        QVERIFY(transform.begin(&doc,{group.id}));QVERIFY(transform.setMatrix(QTransform::fromTranslate(-20,0)));QVERIFY(transform.apply());
        QCOMPARE(selectedPixelBounds(doc.project().shots.first().layers[1]),QRectF(0,20,20,30));
        QCOMPARE(selectedPixelBounds(doc.project().shots.first().layers[2]),QRectF(260,60,20,20));QCOMPARE(doc.history()->count(),1);
        doc.history()->undo();QCOMPARE(doc.project().shots.first().layers[1].tiles,first.tiles);QCOMPARE(doc.project().shots.first().layers[2].tiles,second.tiles);
    }
    void selectionBoundaryMayLeaveExtentThenClipsOnlyOnApply() {
        auto p=project();p.shots.first().sheet.overscan=0;block(p.shots.first().layers[0],{10,10,30,30},qRgb(10,50,160));
        Document doc;doc.load(p);const auto selection=rectangle({10,10,30,30});doc.setRasterSelection(selection);AdvancedTransform transform;
        QVERIFY(transform.begin(&doc,{},true));QVERIFY(transform.setMatrix(QTransform::fromTranslate(-25,0)));
        QCOMPARE(transform.selectionPath().boundingRect(),QRectF(-15,10,30,30));QCOMPARE(doc.rasterSelection().path,selection);
        QVERIFY(transform.apply());QCOMPARE(doc.rasterSelection().path.boundingRect(),QRectF(0,10,15,30));
        QCOMPARE(doc.project().shots.first().layers[0].tiles,p.shots.first().layers[0].tiles);
        doc.history()->undo();QCOMPARE(doc.rasterSelection().path,selection);
        QVERIFY(transform.begin(&doc,{},true));QVERIFY(transform.setMatrix(QTransform::fromTranslate(500,0)));QVERIFY(transform.apply());
        QVERIFY(!doc.rasterSelection().active);QVERIFY(doc.rasterSelection().path.isEmpty());
        doc.history()->undo();QCOMPARE(doc.rasterSelection().path,selection);
    }
    void editableOutsideCommitClipsDerivedPixelsAndRetainsSource() {
        auto p=project();p.shots.first().sheet.overscan=0;auto &layer=p.shots.first().layers[0];layer.kind=LayerKind::Vector;
        auto object=EditableObject::create();object.nodes={BezierNode::corner({40,40}),BezierNode::corner({90,40})};object.strokeWidth=8;object.stroke=Qt::red;
        layer.objects={object};QString error;QVERIFY2(rebuildObjectLayer(layer,{0,0,320,240},32,&error),qPrintable(error));
        Document doc;doc.load(p);AdvancedTransform transform;QVERIFY(transform.begin(&doc));
        QVERIFY(transform.setMatrix(QTransform::fromTranslate(260,0)));QVERIFY(transform.apply());
        const auto &after=doc.project().shots.first().layers[0];QCOMPARE(after.kind,LayerKind::Vector);QCOMPARE(after.objects.size(),1);
        QCOMPARE(after.objects[0].nodes,object.nodes);QCOMPARE(after.objects[0].transform.map(QPointF(90,40)),QPointF(350,40));
        QVERIFY(doc.drawingBounds().contains(selectedPixelBounds(after)));QCOMPARE(pixel(after,340,40),QRgb(0));
        doc.history()->undo();QCOMPARE(doc.project().shots.first().layers[0].objects[0],object);
    }
    void editableMatrixKeepsObjectsAndWarpRequiresRasterize() {
        auto p=project();auto &layer=p.shots.first().layers[0];layer.kind=LayerKind::Vector;
        auto object=EditableObject::create();object.nodes={BezierNode::corner({40,40}),BezierNode::corner({90,40})};object.strokeWidth=8;object.stroke=Qt::red;
        layer.objects={object};QString error;QVERIFY2(rebuildObjectLayer(layer,{-120,-120,560,480},32,&error),qPrintable(error));
        Document doc;doc.load(p);AdvancedTransform transform;QVERIFY(transform.begin(&doc));
        QVERIFY(!transform.setWarp(transform.controlPoints()));QVERIFY(transform.setMatrix(QTransform::fromTranslate(20,30)));QVERIFY(transform.apply());
        const auto &after=doc.project().shots.first().layers[0];QCOMPARE(after.kind,LayerKind::Vector);QCOMPARE(after.objects.size(),1);
        QCOMPARE(after.objects[0].nodes[0].anchor,QPointF(40,40));QCOMPARE(after.objects[0].transform.map(QPointF(40,40)),QPointF(60,70));
    }
};
QTEST_MAIN(AdvancedTransformTest)
#include "test_advanced_transform.moc"
