#include "document.h"
#include "object_tools.h"
#include "raster_tools.h"
#include "sheet.h"
#include "vector_brush.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace toon;
namespace {
Project project(bool vector) {
    auto p=Project::create();p.width=512;p.height=384;p.transparent=true;
    p.shots[0].sheet.overscan=0;
    p.shots[0].layers[0].kind=vector?LayerKind::Vector:LayerKind::Raster;
    return p;
}
const RasterLayer &active(const Document &d){return d.project().shots[0].layers[d.activeLayer()];}
QImage mask(int w,int h){
    QImage image(w,h,QImage::Format_Grayscale8);image.fill(0);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)image.scanLine(y)[x]=uchar((x*19+y*31)%256);
    return image;
}
void draw(Document &doc,const BrushSettings &brush){
    BrushInput in;in.pressure=.35;in.tiltX=12;in.tiltY=-8;in.rotation=350;in.wheel=.6;
    QVERIFY(doc.beginStroke({35.25,90.75},in,brush,false));
    for(int i=1;i<=12;++i){in.pressure=.2+.06*i;in.tiltX=10+i;in.rotation=350+3*i;in.wheel=.4+.04*i;
        doc.continueStroke({35.25+25*i,90.75+std::sin(i*.42)*40},in);if(brush.airbrush&&i%4==0)doc.airbrushTick();}
    doc.endStroke();
}
BrushSettings richBrush(){
    BrushSettings b;b.size=24;b.spacing=.3;b.opacity=.43;b.strokeOpacity=.68;b.color=QColor("#c34d82");
    b.tipMask=mask(23,13);b.angle=23;b.roundness=.73;b.sizeDynamics.jitter=.2;b.angleDynamics.control=BrushControl::Direction;
    b.angleDynamics.jitter=.11;b.roundnessDynamics.jitter=.15;b.scatter=.25;b.scatterBothAxes=true;b.count=2;b.countJitter=.2;
    b.texture.enabled=true;b.texture.pattern=mask(12,10);b.texture.depth=.5;b.texture.scale=1.3;
    b.dual.enabled=true;b.dual.tipMask=mask(11,17);b.dual.size=14;b.dual.spacing=.35;b.dual.scatter=.2;b.dual.count=2;
    b.colorDynamics.enabled=true;b.colorDynamics.hue=.12;b.colorDynamics.brightness=.15;b.colorDynamics.saturation=.1;
    b.flowDynamics.jitter=.12;b.opacityDynamics.control=BrushControl::Pressure;b.noise=true;b.wetEdges=true;b.airbrush=true;
    b.symmetry.mode=SymmetryMode::Vertical;b.symmetry.center={.5,.5};return b;
}
bool rebuild(Document &doc){
    auto p=doc.project();QString error;
    if(!rebuildObjectLayer(p.shots[0].layers[0],doc.drawingBounds(),1024,&error)){qWarning()<<error;return false;}
    doc.replaceProject(p,"Rebuild source");return true;
}
}
class VectorBrushTest:public QObject{
    Q_OBJECT
private slots:
    void rasterParity_data(){QTest::addColumn<int>("variant");for(int v=0;v<6;++v)QTest::newRow(qPrintable(QString::number(v)))<<v;}
    void rasterParity(){
        QFETCH(int,variant);auto brush=variant==0?BrushSettings():richBrush();
        if(variant==2)brush.texture.eachTip=true;
        if(variant==3){brush.colorDynamics.perTip=false;brush.projection=true;}
        if(variant==4){brush.symmetry.mode=SymmetryMode::Radial;brush.symmetry.count=3;}
        if(variant==5)brush.blendMode=BrushBlend::Dissolve;
        Document raster,vector;raster.load(project(false));vector.load(project(true));
        QSignalSpy warnings(&vector,&Document::warning);draw(raster,brush);draw(vector,brush);
        QVERIFY2(warnings.isEmpty(),warnings.isEmpty()?"":qPrintable(warnings.first().first().toString()));
        QCOMPARE(active(vector).objects.size(),1);QCOMPARE(active(vector).tiles,active(raster).tiles);
        const auto pixels=active(vector).tiles;QVERIFY(rebuild(vector));QCOMPARE(active(vector).tiles,pixels);
    }
    void blendAndSelectionReplayExactly(){
        Document raster,vector;raster.load(project(false));vector.load(project(true));
        BrushSettings base;base.size=70;base.pressureSize=false;base.color=QColor("#5078ab");draw(raster,base);draw(vector,base);
        QPainterPath selection;selection.addEllipse({60,40,180,160});raster.setRasterSelection(selection);vector.setRasterSelection(selection);
        QVERIFY(RasterTools::featherSelection(&raster,6));QVERIFY(RasterTools::featherSelection(&vector,6));
        auto b=richBrush();b.blendMode=BrushBlend::Multiply;draw(raster,b);draw(vector,b);
        QCOMPARE(active(vector).objects.size(),2);QCOMPARE(active(vector).tiles,active(raster).tiles);
        const auto pixels=active(vector).tiles;QVERIFY(rebuild(vector));QCOMPARE(active(vector).tiles,pixels);
        vector.history()->undo();QCOMPARE(active(vector).objects.size(),2);
    }
    void capturedAlphaLockReplayUsesEarlierObjects(){
        Document raster,vector;raster.load(project(false));vector.load(project(true));
        BrushSettings base;base.size=70;base.pressureSize=false;draw(raster,base);draw(vector,base);
        raster.setLayerAlphaLocked(true);
        auto brush=richBrush();brush.blendMode=BrushBlend::Multiply;draw(raster,brush);draw(vector,brush);
        // The current layer UI intentionally exposes alpha lock only on raster
        // layers. Exercise the replay flag independently of that UI restriction.
        auto source=active(vector);source.objects.last().brushStroke->alphaLocked=true;
        QString error;QVERIFY2(rebuildObjectLayer(source,vector.drawingBounds(),1024,&error),qPrintable(error));
        QCOMPARE(source.tiles,active(raster).tiles);
    }
    void singleDabUndoAndCancel(){
        Document doc;doc.load(project(true));BrushSettings b;b.size=14;b.pressureSize=false;
        QVERIFY(doc.beginStroke({80,90},1,b,false));doc.endStroke();
        QCOMPARE(active(doc).objects.size(),1);QCOMPARE(active(doc).objects.first().nodes.size(),2);
        const auto pixels=active(doc).tiles;QVERIFY(rebuild(doc));QCOMPARE(active(doc).tiles,pixels);
        QVERIFY(doc.beginStroke({120,90},1,b,false));doc.continueStroke({160,90},1);doc.cancelStroke();QCOMPARE(active(doc).tiles,pixels);
        doc.history()->undo();doc.history()->undo();QVERIFY(active(doc).objects.isEmpty());
        doc.history()->redo();QCOMPARE(active(doc).tiles,pixels);
    }
    void styleWidthAndGeometryRemainEditable(){
        Document doc;doc.load(project(true));BrushSettings b;b.size=20;b.pressureSize=false;b.spacing=.08;draw(doc,b);
        auto object=active(doc).objects.first();const auto source=object.brushStroke->samples;
        object.stroke=Qt::red;object.strokeWidth=35;object.opacity=.4;object.widthFactors.fill(1,object.nodes.size());
        object.widthFactors.first()=.1;object.widthFactors.last()=2;
        QVERIFY(ObjectTools::updateObject(&doc,object));const auto changed=active(doc).tiles;QVERIFY(rebuild(doc));QCOMPARE(active(doc).tiles,changed);
        object=active(doc).objects.first();object.nodes[object.nodes.size()/2].anchor+=QPointF(0,50);
        object.nodes[object.nodes.size()/2].inControl=object.nodes[object.nodes.size()/2].anchor;
        object.nodes[object.nodes.size()/2].outControl=object.nodes[object.nodes.size()/2].anchor;
        QVERIFY(ObjectTools::updateObject(&doc,object));QVERIFY(active(doc).tiles!=changed);QCOMPARE(active(doc).objects.first().brushStroke->samples,source);
        object=active(doc).objects.first();object.transform.translate(20,30);object.transform.rotate(15);object.transform.scale(1.2,.8);
        QVERIFY(ObjectTools::updateObject(&doc,object));const auto transformed=active(doc).tiles;QVERIFY(rebuild(doc));QCOMPARE(active(doc).tiles,transformed);
    }
    void sampledResourcesSurviveSaveAndLoad(){
        Document doc;doc.load(project(true));draw(doc,richBrush());const auto pixels=active(doc).tiles;
        QTemporaryDir dir;QString error;const auto file=dir.filePath("stroke.toonproj");
        QVERIFY2(saveProject(doc.project(),file,&error),qPrintable(error));auto loaded=loadProject(file,&error);QVERIFY2(loaded.has_value(),qPrintable(error));
        QCOMPARE(loaded->shots[0].layers[0].tiles,pixels);
        const auto &settings=loaded->shots[0].layers[0].objects.first().brushStroke->settings;
        QCOMPARE(settings.tipMask,richBrush().tipMask);QCOMPARE(settings.texture.pattern,richBrush().texture.pattern);QCOMPARE(settings.dual.tipMask,richBrush().dual.tipMask);
    }
    void prefixCacheInvalidatesEarlierChanges(){
        Document doc;doc.load(project(true));BrushSettings b;b.pressureSize=false;draw(doc,b);b.color=Qt::red;b.size=30;draw(doc,b);
        QVERIFY(rebuild(doc));auto first=active(doc).objects.first();first.stroke=Qt::green;QVERIFY(ObjectTools::updateObject(&doc,first));
        const auto expected=active(doc).tiles;auto independent=active(doc);independent.id="fresh-cache-key";QString error;
        QVERIFY(rebuildObjectLayer(independent,doc.drawingBounds(),1024,&error));QCOMPARE(independent.tiles,expected);
    }
    void excessiveStampTransformFailsAtomically(){
        Document doc;doc.load(project(true));BrushSettings b;b.size=200;b.pressureSize=false;
        QVERIFY(doc.beginStroke({0,0},1,b,false));doc.endStroke();
        const auto before=active(doc).tiles;auto object=active(doc).objects.first();object.transform.scale(80,80);
        QVERIFY(!ObjectTools::updateObject(&doc,object));QCOMPARE(active(doc).tiles,before);
    }
};
QTEST_MAIN(VectorBrushTest)
#include "test_vector_brush.moc"
