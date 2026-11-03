#include "document.h"
#include "raster_tools.h"
#include "vector_brush.h"
#include <QUuid>
#include <QtTest>

using namespace toon;
namespace {
Project project(bool vector=true) {
    auto p=Project::create();p.width=320;p.height=320;p.transparent=true;
    p.shots[0].sheet.overscan=0;
    p.shots[0].layers[0].kind=vector?LayerKind::Vector:LayerKind::Raster;
    return p;
}
const RasterLayer &layer(const Document &doc){return doc.project().shots[0].layers[0];}
QImage mask(int side) {
    QImage image(side,side,QImage::Format_Grayscale8);
    for(int y=0;y<side;++y)for(int x=0;x<side;++x)image.scanLine(y)[x]=uchar((x*31+y*17+x*y*3)%256);
    return image;
}
BrushSettings brush() {
    BrushSettings b;b.size=30;b.pressureSize=false;b.spacing=.3;b.opacity=.31;b.strokeOpacity=.6;
    b.color=QColor("#b52976");b.tipMask=mask(19);b.angleDynamics.jitter=.2;
    b.colorDynamics.enabled=true;b.colorDynamics.hue=.15;b.colorDynamics.brightness=.12;
    b.texture.enabled=true;b.texture.pattern=mask(9);b.texture.depth=.45;
    b.dual.enabled=true;b.dual.tipMask=mask(11);b.dual.size=16;
    b.symmetry.mode=SymmetryMode::Vertical;return b;
}
void draw(Document &doc,const BrushSettings &b,QPointF offset={}) {
    BrushInput input;input.pressure=.7;
    QVERIFY(doc.beginStroke(QPointF(35.25,70.75)+offset,input,b,false));
    for(int i=1;i<=7;++i){input.pressure=.15+.1*i;
        doc.continueStroke(QPointF(35.25+i*24,70.75+std::sin(i*.5)*25)+offset,input);}
    doc.endStroke();
}
RasterLayer sourceLayer() {
    Document doc;doc.load(project());auto b=brush();
    draw(doc,b);b.color=Qt::cyan;draw(doc,b,{0,40});
    b.blendMode=BrushBlend::Multiply;draw(doc,b,{0,16});
    b.blendMode=BrushBlend::Dissolve;draw(doc,b,{0,80});
    return layer(doc);
}
const QRectF bounds(0,0,320,320);
}
class VectorRebuildTest:public QObject {
    Q_OBJECT
private slots:
    void init(){clearObjectLayerCaches();}
    void cleanup(){clearObjectLayerCaches();}
    void allBlendModesMatchRaster_data(){
        QTest::addColumn<int>("mode");QTest::addColumn<bool>("alphaLocked");
        for(int mode=0;mode<=int(BrushBlend::Dissolve);++mode)
            for(bool alpha:{false,true})QTest::newRow(qPrintable(QString::number(mode)+(alpha?"-alpha":"")))<<mode<<alpha;
    }
    void allBlendModesMatchRaster(){
        QFETCH(int,mode);QFETCH(bool,alphaLocked);
        Document raster,vector;raster.load(project(false));vector.load(project());
        BrushSettings base;base.size=45;base.pressureSize=false;base.spacing=.3;base.color=QColor("#5389b9");
        draw(raster,base);draw(vector,base);
        QPainterPath selected;selected.addEllipse(QRectF(25,45,220,130));
        raster.setRasterSelection(selected);vector.setRasterSelection(selected);
        QVERIFY(RasterTools::featherSelection(&raster,5));QVERIFY(RasterTools::featherSelection(&vector,5));
        if(alphaLocked)raster.setLayerAlphaLocked(true);
        auto b=brush();b.blendMode=BrushBlend(mode);draw(raster,b);draw(vector,b);
        auto rebuilt=layer(vector);rebuilt.objects.last().brushStroke->alphaLocked=alphaLocked;
        QString error;QVERIFY2(rebuildObjectLayer(rebuilt,bounds,1024,&error),qPrintable(error));
        QCOMPARE(rebuilt.tiles,layer(raster).tiles);
        clearObjectLayerCaches();
        QVERIFY2(rebuildObjectLayer(rebuilt,bounds,1024,&error),qPrintable(error));
        QCOMPARE(rebuilt.tiles,layer(raster).tiles);
        // Force replay of every stroke while retaining the per-stroke cache.
        rebuilt.id=QUuid::createUuid().toString();rebuilt.tiles.clear();
        QVERIFY2(rebuildObjectLayer(rebuilt,bounds,1024,&error),qPrintable(error));
        QCOMPARE(rebuilt.tiles,layer(raster).tiles);
    }
    void earlierChangesInvalidateOnlyTheirOwnPaint_data(){
        QTest::addColumn<int>("change");
        for(int i=0;i<9;++i)QTest::newRow(qPrintable(QString::number(i)))<<i;
    }
    void earlierChangesInvalidateOnlyTheirOwnPaint(){
        QFETCH(int,change);auto original=sourceLayer();QString error;
        original.objects[2].brushStroke->alphaLocked=true;
        QVERIFY2(rebuildObjectLayer(original,bounds,1024,&error),qPrintable(error));
        auto edited=original;
        switch(change){
        case 0:edited.objects[0].stroke=Qt::yellow;edited.objects[0].opacity=.35;break;
        case 1:edited.objects[0].transform.translate(15,50);break;
        case 2:edited.objects[0].nodes[1].anchor+=QPointF(20,55);break;
        case 3:edited.objects[0].strokeWidth=50;edited.objects[0].widthFactors.fill(.4,edited.objects[0].nodes.size());break;
        case 4:edited.objects.swapItemsAt(0,2);break;
        case 5:edited.objects.removeFirst();break;
        case 6:edited.objects[0].brushStroke->settings.tipMask=mask(13);break;
        case 7:edited.objects[0].brushStroke->selectionActive=true;
            edited.objects[0].brushStroke->selection.addRect({40,60,90,80});edited.objects[0].brushStroke->selectionFeather=7;break;
        case 8:edited.objects[0].brushStroke->settings.texture.pattern=mask(7);break;
        }
        QVERIFY2(rebuildObjectLayer(edited,bounds,1024,&error),qPrintable(error));
        const auto warm=edited.tiles;
        clearObjectLayerCaches();edited.tiles.clear();
        QVERIFY2(rebuildObjectLayer(edited,bounds,1024,&error),qPrintable(error));
        QCOMPARE(edited.tiles,warm);
    }
    void boundsAndAlphaBackdropInvalidateSafely(){
        auto original=sourceLayer();QString error;
        // An alpha-locked first stroke paints nothing, including on a cache hit.
        original.objects.resize(1);original.objects[0].brushStroke->alphaLocked=true;
        QVERIFY(rebuildObjectLayer(original,bounds,1024,&error));QVERIFY(original.tiles.isEmpty());
        original.id=QUuid::createUuid().toString();
        QVERIFY(rebuildObjectLayer(original,bounds,1024,&error));QVERIFY(original.tiles.isEmpty());
        original.objects[0].brushStroke->alphaLocked=false;
        QVERIFY(rebuildObjectLayer(original,bounds,1024,&error));QVERIFY(!original.tiles.isEmpty());
        const QRectF clipped(0,0,100,100);
        QVERIFY(rebuildObjectLayer(original,clipped,1024,&error));const auto warm=original.tiles;
        clearObjectLayerCaches();QVERIFY(rebuildObjectLayer(original,clipped,1024,&error));QCOMPARE(original.tiles,warm);
    }
    void tinyFlowKeepsExactPixelsAndTilePresence(){
        Document raster,vector;raster.load(project(false));vector.load(project());
        BrushSettings b;b.size=16;b.pressureSize=false;b.opacity=.00001;b.strokeOpacity=.00001;
        draw(raster,b);draw(vector,b);auto rebuilt=layer(vector);QString error;
        QVERIFY(rebuildObjectLayer(rebuilt,bounds,1024,&error));QCOMPARE(rebuilt.tiles,layer(raster).tiles);
        rebuilt.id=QUuid::createUuid().toString();QVERIFY(rebuildObjectLayer(rebuilt,bounds,1024,&error));
        QCOMPARE(rebuilt.tiles,layer(raster).tiles);
    }
    void failureLeavesArtworkUntouched(){
        auto original=sourceLayer();QString error;
        QVERIFY(rebuildObjectLayer(original,bounds,1024,&error));const auto pixels=original.tiles;
        auto limited=original;limited.id=QUuid::createUuid().toString();
        QVERIFY(!rebuildObjectLayer(limited,bounds,0,&error));QCOMPARE(limited.tiles,pixels);
        original.objects[0].transform.scale(200,200);
        QVERIFY(!rebuildObjectLayer(original,bounds,1024,&error));QCOMPARE(original.tiles,pixels);
    }
    void largeStrokeUsesStreamingReplay(){
        auto p=project();p.width=8192;p.height=512;
        Document doc;doc.load(p);BrushSettings b;b.size=8;b.pressureSize=false;b.spacing=1;
        QVERIFY(doc.beginStroke({8,256},1,b,false));doc.continueStroke({8180,256},1);doc.endStroke();
        auto source=layer(doc);QVERIFY(source.tiles.size()>32);const auto pixels=source.tiles;
        clearObjectLayerCaches();QString error;
        QVERIFY2(rebuildObjectLayer(source,{0,0,8192,512},1024,&error),qPrintable(error));
        QCOMPARE(source.tiles,pixels);
        source.id=QUuid::createUuid().toString();
        QVERIFY(rebuildObjectLayer(source,{0,0,8192,512},1024,&error));QCOMPARE(source.tiles,pixels);
    }
};
QTEST_MAIN(VectorRebuildTest)
#include "test_vector_rebuild.moc"
