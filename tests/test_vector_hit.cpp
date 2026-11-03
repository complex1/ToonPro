#include "document.h"
#include "object_tools.h"
#include "vector_brush.h"
#include <QElapsedTimer>
#include <QtTest>
#include <cmath>
using namespace toon;
namespace {
QImage ringMask() {
    QImage mask(16,16,QImage::Format_Grayscale8);mask.fill(0);
    for(int y=1;y<15;++y)for(int x=1;x<15;++x)
        if(x<5||x>=11||y<5||y>=11)mask.scanLine(y)[x]=255;
    return mask;
}
EditableObject dabObject(QPointF center={100,100},qreal size=64) {
    auto object=EditableObject::create();object.stroke=Qt::black;object.strokeWidth=size;
    object.nodes={BezierNode::corner(center),BezierNode::corner(center+QPointF(40,0))};
    VectorBrushStroke stroke;stroke.settings.size=size;stroke.settings.pressureSize=false;
    stroke.settings.tipMask=ringMask();stroke.settings.hardness=1;
    stroke.canvasSize={300,300};stroke.samples={{0,center,{},0,0,0}};
    stroke.sourceGeometry=editableGeometrySignature(object);object.brushStroke=stroke;
    return object;
}
bool hit(const EditableObject &object,QPointF point,qreal radius=.2) {
    QPainterPath region;region.addRect({point-QPointF(radius,radius),QSizeF(radius*2,radius*2)});
    return vectorBrushIntersects(object,region);
}
int alphaAt(const RasterLayer &layer,int x,int y) {
    const int tx=int(std::floor(qreal(x)/tileSize)),ty=int(std::floor(qreal(y)/tileSize));
    const auto found=layer.tiles.constFind({tx,ty});
    return found==layer.tiles.cend()?0:qAlpha(found->pixel(x-tx*tileSize,y-ty*tileSize));
}
void comparePixels(const EditableObject &object,const QRect &area,int step=1) {
    RasterLayer layer;QString error;
    QVERIFY2(replayVectorBrushStroke(layer,object,QRectF(-1024,-1024,2048,2048),64,&error),qPrintable(error));
    int painted=0,empty=0;
    for(int y=area.top();y<=area.bottom();y+=step)for(int x=area.left();x<=area.right();x+=step){
        const bool expected=alphaAt(layer,x,y)>0;
        painted+=expected;empty+=!expected;
        const bool selected=hit(object,QPointF(x+.5,y+.5));
        QVERIFY2(selected==expected,qPrintable(QString("Pixel (%1,%2), alpha %3: selected %4")
            .arg(x).arg(y).arg(alphaAt(layer,x,y)).arg(selected)));
    }
    QVERIFY(painted>0);QVERIFY(empty>0);
}
}
class VectorHitTest:public QObject {
    Q_OBJECT
private slots:
    void transparentTipHolesExposeLowerPaths() {
        RasterLayer layer;layer.kind=LayerKind::Vector;
        auto lower=EditableObject::create();lower.nodes={BezierNode::corner({90,100}),BezierNode::corner({110,100})};
        lower.strokeWidth=4;const auto upper=dabObject();layer.objects={lower,upper};
        QCOMPARE(ObjectTools::hitTest(layer,{100,100},1),lower.id);
        QCOMPARE(ObjectTools::hitTest(layer,{125,100},1),upper.id);
        QVERIFY(ObjectTools::hitTest(layer,{132,132},.5).isEmpty());
    }
    void erasingThroughHoleKeepsPathAndHistory() {
        auto project=Project::create();project.width=300;project.height=300;project.transparent=true;
        auto &layer=project.shots[0].layers[0];layer.kind=LayerKind::Vector;layer.objects={dabObject()};
        QString error;QVERIFY2(rebuildObjectLayer(layer,{0,0,300,300},16,&error),qPrintable(error));
        Document doc;doc.load(project);const auto before=doc.project().shots[0].layers[0];
        QVERIFY(!ObjectTools::eraseAt(&doc,{100,100},2,false));
        QVERIFY(!ObjectTools::eraseAt(&doc,{100,100},2,true));
        QCOMPARE(doc.project().shots[0].layers[0].objects,before.objects);
        QCOMPARE(doc.project().shots[0].layers[0].tiles,before.tiles);QCOMPARE(doc.history()->count(),0);
        QVERIFY(ObjectTools::eraseAt(&doc,{124,100},2,false));
        QVERIFY(doc.project().shots[0].layers[0].objects.isEmpty());
        doc.history()->undo();QCOMPARE(doc.project().shots[0].layers[0].objects,before.objects);
    }
    void sampledEdgesMatchRasterPixels() {
        auto object=dabObject({-.25,-.75},20);
        object.transform.rotate(27);object.transform.shear(.17,-.1);object.transform.scale(1.4,.6);
        comparePixels(object,{-18,-13,36,26});
    }
    void largeRotatedSampleMatchesRasterPixels() {
        auto object=dabObject({-.25,-.75},256);
        object.transform.rotate(27);object.transform.shear(.17,-.1);object.transform.scale(1.4,.6);
        comparePixels(object,{-220,-150,440,300},9);
    }
    void mirroredAndTransformedTipsKeepHoles() {
        auto object=dabObject({80,80});object.brushStroke->settings.symmetry.mode=SymmetryMode::Vertical;
        object.transform.translate(-150,-100);object.transform.rotate(17);object.transform.scale(1.2,.7);
        for(const QPointF center:{QPointF(80,80),QPointF(220,80)}){
            QVERIFY(!hit(object,object.transform.map(center),1));
            QVERIFY(hit(object,object.transform.map(center+QPointF(24,0)),1));
        }
        object.brushStroke->settings.symmetry.mode=SymmetryMode::Radial;
        object.brushStroke->settings.symmetry.count=4;
        QVERIFY(hit(object,object.transform.map(QPointF(220,244)),1));
    }
    void continuousTextureMatchesTransformedNegativePixels() {
        auto object=dabObject({-2,-3},22);auto &texture=object.brushStroke->settings.texture;
        texture.enabled=true;texture.eachTip=false;texture.depth=1;
        texture.pattern=QImage(7,9,QImage::Format_Grayscale8);texture.pattern.fill(255);
        for(int y=0;y<9;++y)for(int x=0;x<7;++x)if(x<3||y<3)texture.pattern.scanLine(y)[x]=0;
        object.transform.rotate(33);object.transform.scale(1.3,.8);
        comparePixels(object,{-18,-18,36,34});
    }
    void capturedSelectionAndFeatherMatchPaintedPixels() {
        auto object=dabObject({0,0},26);auto &stroke=*object.brushStroke;
        stroke.selectionActive=true;stroke.selection.addRect({-20,-20,20,40});
        object.transform.translate(-3.5,2);object.transform.rotate(10);
        comparePixels(object,{-22,-17,36,38});
        stroke.selectionFeather=4;
        comparePixels(object,{-22,-17,36,38});
        QVERIFY(hit(object,object.transform.map(QPointF(1,-10)),.2));
        stroke.selection={};QVERIFY(!hit(object,object.transform.map(QPointF(-10,-10))));
    }
    void zeroCoverageAndClippedRegionsDoNotSelect() {
        auto object=dabObject();object.brushStroke->settings.tipMask.fill(0);
        QVERIFY(!hit(object,{100,100},40));
        object=dabObject();object.opacity=0;QVERIFY(!hit(object,{124,100}));
        object=dabObject();object.brushStroke->selectionActive=true;
        object.brushStroke->selection.addRect({0,0,90,200});
        QVERIFY(!hit(object,{124,100}));QVERIFY(hit(object,{76,100}));
    }
    void faintDabsAccumulateBeforeTheyBecomeSelectable() {
        auto object=dabObject({0,0},20);object.brushStroke->settings.opacity=.001;
        QVERIFY(!hit(object,{7.5,.5}));
        object.brushStroke->samples.append({1,{0,0},{},0,0,1});
        QVERIFY(hit(object,{7.5,.5}));
        comparePixels(object,{-12,-12,24,24});
        object.brushStroke->settings.opacity=1;object.opacity=.001;
        QVERIFY(!hit(object,{7.5,.5}));
    }
    void dissolveCoverageMatchesPaintedPixels() {
        auto object=dabObject({0,0},20);object.brushStroke->settings.blendMode=BrushBlend::Dissolve;
        object.brushStroke->settings.strokeOpacity=.3;
        comparePixels(object,{-12,-12,24,24});
    }
    void segmentIntersectionReturnsOnlyPaintedSamples() {
        auto object=dabObject({50,50},30);auto &stroke=*object.brushStroke;
        stroke.samples.append({1,{150,50},{},0,0,1});
        QPainterPath hole;hole.addEllipse({50,50},2,2);
        QVERIFY(vectorBrushIntersectionPositions(object,hole).isEmpty());
        QPainterPath second;second.addEllipse({162,50},1,1);
        QCOMPARE(vectorBrushIntersectionPositions(object,second),QVector<qreal>{1});
    }
    void enormousTransformUsesSmallQueryBuffers() {
        auto object=dabObject({0,0});object.transform.scale(10000,10000);
        QElapsedTimer timer;timer.start();
        QVERIFY(!hit(object,{0,0},2));
        QVERIFY(hit(object,{240000,0},2));
        // This would allocate a 660k-square stamp if the picker reused the
        // full-footprint rasterizer instead of clipping to query tiles.
        QVERIFY2(timer.elapsed()<2000,"Picking a small area must not render the full enlarged stamp.");
    }
};
QTEST_MAIN(VectorHitTest)
#include "test_vector_hit.moc"
