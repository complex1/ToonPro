#include "brush_engine.h"
#include "document.h"
#include <QtTest>
#include <QSignalSpy>
#include <cmath>
using namespace toon;
namespace {
Project blank(){auto p=Project::create();p.width=640;p.height=480;p.transparent=true;return p;}
QColor pixel(const Document &d,int x,int y){const auto &tiles=d.project().shots.first().layers.first().tiles;const int tx=int(std::floor(x/256.0)),ty=int(std::floor(y/256.0));const auto it=tiles.constFind({tx,ty});return it==tiles.end()?QColor(Qt::transparent):it->pixelColor(x-tx*256,y-ty*256);}
qint64 mass(const QImage &mask){qint64 total=0;for(int y=0;y<mask.height();++y)for(int x=0;x<mask.width();++x)total+=mask.constScanLine(y)[x];return total;}
QImage mask(int width,int height,int value=255){QImage image(width,height,QImage::Format_Grayscale8);image.fill(value);return image;}
QRect coverage(const QImage &image){QRect result;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)if(image.constScanLine(y)[x]>20)result|=QRect(x,y,1,1);return result;}
void tap(Document &d,const BrushSettings &b,QPointF point={100,100},BrushInput input={}){QVERIFY(d.beginStroke(point,input,b,false));d.endStroke();}
}
class AdvancedBrushTest:public QObject{
    Q_OBJECT
private slots:
    void hardnessShapeFlipAndProjection(){
        BrushSettings b;b.size=60;b.pressureSize=false;
        const auto hard=brushTipMask(b,{});b.hardness=0;QVERIFY(mass(brushTipMask(b,{}))<mass(hard)*.6);
        b.hardness=1;b.roundness=.25;const auto flat=coverage(brushTipMask(b,{}));QVERIFY(flat.width()>flat.height()*3);
        b.angle=90;const auto rotated=coverage(brushTipMask(b,{}));QVERIFY(rotated.height()>rotated.width()*3);
        b.angle=0;b.roundness=1;b.tipMask=mask(20,10,0);for(int y=0;y<10;++y)for(int x=0;x<7;++x)b.tipMask.scanLine(y)[x]=255;
        const auto original=brushTipMask(b,{});b.flipX=true;const auto flipped=brushTipMask(b,{});QCOMPARE(flipped,original.flipped(Qt::Horizontal));
        b.tipMask={};b.flipX=false;b.projection=true;BrushInput input;input.tiltX=75;QVERIFY(mass(brushTipMask(b,input))<mass(hard)*.35);
    }
    void controlsJitterFadeAndPose(){
        BrushSettings b;b.size=80;b.pressureSize=false;b.sizeDynamics.control=BrushControl::Pressure;b.sizeDynamics.minimum=.1;
        BrushInput in;in.pressure=.25;const auto small=brushTipMask(b,in);in.pressure=1;QVERIFY(mass(small)<mass(brushTipMask(b,in))*.1);
        b.sizeDynamics.control=BrushControl::Wheel;in.wheel=.2;QVERIFY(mass(brushTipMask(b,in))<mass(brushTipMask(b,{}))*.1);
        b.sizeDynamics.control=BrushControl::Fade;b.sizeDynamics.fade=5;QVERIFY(mass(brushTipMask(b,in,0,0,5))<mass(brushTipMask(b,in,0,0,0))*.1);
        b.sizeDynamics.control=BrushControl::Tilt;in.tiltX=45;b.tiltScale=.5;const auto tilt=mass(brushTipMask(b,in));b.tiltScale=1;QVERIFY(mass(brushTipMask(b,in))>tilt*3);
        b.sizeDynamics.control=BrushControl::Off;b.sizeDynamics.jitter=.8;QVERIFY(brushTipMask(b,in,0,0,3,7)!=brushTipMask(b,in,0,0,4,7));QCOMPARE(brushTipMask(b,in,0,0,3,7),brushTipMask(b,in,0,0,3,7));
        b.sizeDynamics.jitter=0;b.pressureSize=true;b.pose.overridePressure=true;b.pose.pressure=.5;in.pressure=.01;QCOMPARE(brushTipMask(b,in),brushTipMask(b,{}));
        b.roundness=.2;b.pose.overrideRotation=true;b.pose.rotation=90;const auto pose=coverage(brushTipMask(b,in));QVERIFY(pose.height()>pose.width()*3);
        b.pose.overrideRotation=false;b.angleDynamics.control=BrushControl::Direction;const auto direction=coverage(brushTipMask(b,in,90));QVERIFY(direction.height()>direction.width()*3);
        b.angleDynamics.control=BrushControl::InitialDirection;QCOMPARE(brushTipMask(b,in,0,90),brushTipMask(b,in,45,90));
        b.angleDynamics.control=BrushControl::Off;b.roundness=1;b.roundnessDynamics.control=BrushControl::Pressure;b.roundnessDynamics.minimum=.2;b.pose.overridePressure=false;in.pressure=.1;
        const auto roundness=coverage(brushTipMask(b,in));QVERIFY(roundness.width()>roundness.height()*3);
    }
    void strokeOpacityCapsFlowAndAirbrush(){
        BrushSettings b;b.size=40;b.pressureSize=false;b.opacity=.2;b.strokeOpacity=.4;b.airbrush=true;
        Document d;d.load(blank());QVERIFY(d.beginStroke({100,100},1,b,false));const int first=pixel(d,100,100).alpha();QVERIFY(first>=19&&first<=22);
        for(int i=0;i<60;++i)d.airbrushTick();const int built=pixel(d,100,100).alpha();QVERIFY(built>first*4);QVERIFY(built<=103);
        d.endStroke();QCOMPARE(d.history()->count(),1);tap(d,b);QVERIFY(pixel(d,100,100).alpha()>built);d.history()->undo();QCOMPARE(pixel(d,100,100).alpha(),built);
        d.history()->undo();QCOMPARE(pixel(d,100,100).alpha(),0);
        b.airbrush=false;QVERIFY(d.beginStroke({100,100},1,b,false));const auto before=pixel(d,100,100);d.airbrushTick();QCOMPARE(pixel(d,100,100),before);d.cancelStroke();QCOMPARE(pixel(d,100,100).alpha(),0);
    }
    void lowFlowPrecisionAndDescendingOpacityColor(){
        BrushSettings b;b.size=4;b.pressureSize=false;b.opacity=.01;b.airbrush=true;
        Document d;d.load(blank());QVERIFY(d.beginStroke({100,100},1,b,false));
        for(int i=0;i<1000;++i)d.airbrushTick();QVERIFY(pixel(d,100,100).alpha()>=254);d.endStroke();
        RasterLayer output,acc,base;BrushDab red;red.mask=mask(4,4);red.color=Qt::red;
        QVERIFY(applyBrushDab(output,acc,base,red,{10,10},{},BrushBlend::Normal,false,{0,0,30,30},4,1));
        auto blue=red;blue.color=Qt::blue;blue.opacity=.1;
        QVERIFY(applyBrushDab(output,acc,base,blue,{10,10},{},BrushBlend::Normal,false,{0,0,30,30},4,1));
        const auto color=output.tiles[{0,0}].pixelColor(10,10);QCOMPARE(color.alpha(),255);QVERIFY(color.red()>225);QVERIFY(color.blue()<30);
    }
    void masksBlendModesAndStrokeSeed(){
        BrushSettings b;b.pressureSize=false;b.size=20;b.tipMask=mask(20,20,100);b.texture.enabled=true;b.texture.pattern=mask(4,4,170);
        QVector<int> centers;
        for(int mode=0;mode<=int(MaskBlend::Difference);++mode){b.texture.mode=MaskBlend(mode);const auto tip=brushTipMask(b,{});centers.append(tip.constScanLine(tip.height()/2)[tip.width()/2]);}
        QVERIFY(centers[int(MaskBlend::Lighten)]>100);QVERIFY(centers[int(MaskBlend::Add)]>170);QVERIFY(centers[int(MaskBlend::Multiply)]<100);QCOMPARE(centers[int(MaskBlend::Subtract)],0);
        b.texture.enabled=false;b.dual.enabled=true;b.dual.tipMask=mask(20,20,170);b.dual.size=20;b.dual.mode=MaskBlend::Lighten;
        const auto tip=brushTipMask(b,{});QVERIFY(tip.constScanLine(tip.height()/2)[tip.width()/2]>100);
        b.dual.enabled=false;b.colorDynamics.enabled=true;b.colorDynamics.hue=1;b.colorDynamics.perTip=false;b.color=Qt::red;b.tipMask=mask(20,20);
        Document d;d.load(blank());tap(d,b,{100,100});tap(d,b,{200,100});QVERIFY(pixel(d,100,100)!=pixel(d,200,100));
    }
    void transferPressureMinimumAndJitter(){
        BrushSettings b;b.size=40;b.pressureSize=false;b.opacityDynamics.control=BrushControl::Pressure;b.opacityDynamics.minimum=.2;
        BrushInput input;input.pressure=.1;auto dab=evaluateBrushDab(b,input,{},0,0,0,1);QCOMPARE(dab.opacity,.2);
        b.flowDynamics.control=BrushControl::Wheel;b.flowDynamics.minimum=.3;input.wheel=.1;dab=evaluateBrushDab(b,input,{},0,0,0,1);QCOMPARE(dab.flow,.3);
        b.flowDynamics.jitter=1;b.flowDynamics.control=BrushControl::Off;b.flowDynamics.minimum=0;QVERIFY(evaluateBrushDab(b,input,{},0,0,1,1).flow!=evaluateBrushDab(b,input,{},0,0,2,1).flow);
        Document d;d.load(blank());tap(d,b,{100,100},input);QVERIFY(pixel(d,100,100).alpha()<52);
    }
    void textureOptionsAndWorldAnchoring(){
        BrushSettings b;b.size=40;b.pressureSize=false;b.tipMask=mask(40,40);b.texture.enabled=true;b.texture.pattern=mask(8,8,0);
        for(int y=0;y<8;++y)for(int x=0;x<4;++x)b.texture.pattern.scanLine(y)[x]=255;
        auto base=evaluateBrushDab(b,{},QPointF(100,100),0,0,0,1).mask;
        QVERIFY(mass(base)<mass(brushTipMask(BrushSettings{.size=40,.pressureSize=false,.tipMask=mask(40,40)},{}))*.6);
        b.texture.invert=true;QVERIFY(evaluateBrushDab(b,{},QPointF(100,100),0,0,0,1).mask!=base);b.texture.invert=false;
        b.texture.scale=2;QVERIFY(evaluateBrushDab(b,{},QPointF(100,100),0,0,0,1).mask!=base);b.texture.scale=1;
        b.texture.depth=0;const auto none=mass(brushTipMask(b,{}));QVERIFY(none>mass(base)*1.8);b.texture.depth=1;
        b.texture.brightness=1;QCOMPARE(mass(brushTipMask(b,{})),none);b.texture.brightness=0;
        b.texture.pattern=mask(8,8,100);auto gray=mass(brushTipMask(b,{}));b.texture.contrast=1;QVERIFY(mass(brushTipMask(b,{}))<gray);b.texture.contrast=0;
        b.texture.dynamics.control=BrushControl::Pressure;BrushInput input;input.pressure=0;QCOMPARE(mass(brushTipMask(b,input)),none);b.texture.dynamics.minimum=.5;QVERIFY(mass(brushTipMask(b,input))<none);
        b.texture.dynamics={};b.texture.pattern=mask(8,8,0);for(int y=0;y<8;++y)for(int x=0;x<4;++x)b.texture.pattern.scanLine(y)[x]=255;
        QCOMPARE(evaluateBrushDab(b,{},QPointF(100,100),0,0,0,1).mask,evaluateBrushDab(b,{},QPointF(108,100),0,0,9,1).mask);
        b.texture.eachTip=true;QVERIFY(evaluateBrushDab(b,{},QPointF(100,100),0,0,0,1).mask!=evaluateBrushDab(b,{},QPointF(100,100),0,0,1,1).mask);
    }
    void dualTipSpacingScatterAndModes(){
        BrushSettings b;b.size=60;b.pressureSize=false;b.dual.enabled=true;b.dual.tipMask=mask(10,10);b.dual.size=12;
        auto small=brushTipMask(b,{});b.dual.size=30;QVERIFY(mass(brushTipMask(b,{}))>mass(small)*3);
        b.dual.size=12;b.dual.spacing=2;QVERIFY(brushTipMask(b,{},0,0,0)!=brushTipMask(b,{},0,0,5));
        b.dual.scatter=.8;b.dual.count=1;const auto single=brushTipMask(b,{});b.dual.count=8;QVERIFY(mass(brushTipMask(b,{}))>mass(single));
        b.dual.mode=MaskBlend::Subtract;QVERIFY(mass(brushTipMask(b,{}))>mass(single));
    }
    void scatterCountAndColorDynamics(){
        BrushSettings b;b.pressureSize=false;b.scatter=2;b.scatterBothAxes=false;
        auto dab=evaluateBrushDab(b,{}, {},0,0,0,11);QCOMPARE(dab.offset.x(),0.0);QVERIFY(dab.offset.y()!=0);
        b.scatterBothAxes=true;QVERIFY(evaluateBrushDab(b,{}, {},0,0,0,11).offset.x()!=0);
        b.scatterControl=BrushControl::Pressure;BrushInput input;input.pressure=0;QCOMPARE(evaluateBrushDab(b,input, {},0,0,0,11).offset,QPointF());
        b.count=12;QCOMPARE(brushDabCount(b,{},0,11),12);b.countControl=BrushControl::Pressure;QCOMPARE(brushDabCount(b,input,0,11),1);
        b.countControl=BrushControl::Off;b.countJitter=1;QVERIFY(brushDabCount(b,{},0,11)<12);
        b.color=Qt::red;b.backgroundColor=Qt::blue;b.colorDynamics.enabled=true;b.colorDynamics.foregroundBackground=1;b.colorDynamics.control=BrushControl::Pressure;
        input.pressure=1;QCOMPARE(evaluateBrushDab(b,input,{},0,0,0,1).color,QColor(Qt::blue));
        input.pressure=0;QCOMPARE(evaluateBrushDab(b,input,{},0,0,0,1).color,QColor(Qt::red));
        b.colorDynamics.control=BrushControl::Off;b.colorDynamics.foregroundBackground=0;b.colorDynamics.hue=1;
        QVERIFY(evaluateBrushDab(b,input,{},0,0,0,1).color!=evaluateBrushDab(b,input,{},0,0,1,1).color);
        b.colorDynamics.hue=0;b.colorDynamics.purity=-1;QCOMPARE(evaluateBrushDab(b,input,{},0,0,0,1).color.saturation(),0);
        b.colorDynamics.purity=0;b.colorDynamics.brightness=1;b.color=QColor("#805020");QVERIFY(evaluateBrushDab(b,input,{},0,0,0,1).color!=b.color);
    }
    void noiseAndWetEdgesChangeCoverage(){
        BrushSettings b;b.size=60;b.pressureSize=false;const auto plain=brushTipMask(b,{});b.noise=true;const auto noisy=brushTipMask(b,{});QVERIFY(mass(noisy)<mass(plain));QCOMPARE(noisy,brushTipMask(b,{}));
        b.noise=false;b.wetEdges=true;const auto wet=brushTipMask(b,{});QVERIFY(mass(wet)<mass(plain)*.6);QVERIFY(wet.constScanLine(wet.height()/2)[wet.width()/2]<110);
        QVERIFY(wet.constScanLine(wet.height()/2)[3]>wet.constScanLine(wet.height()/2)[wet.width()/2]);
    }
    void blendModesCompositeAgainstOriginal(){
        BrushSettings base;base.size=40;base.pressureSize=false;base.color=QColor(160,100,50);
        BrushSettings brush=base;brush.color=QColor(80,140,220);
        for(int mode=int(BrushBlend::Normal);mode<=int(BrushBlend::Dissolve);++mode){
            Document d;d.load(blank());tap(d,base);brush.blendMode=BrushBlend(mode);tap(d,brush);QVERIFY(pixel(d,100,100).isValid());QCOMPARE(pixel(d,100,100).alpha(),255);
            if(brush.blendMode==BrushBlend::Multiply){const auto c=pixel(d,100,100);QVERIFY(qAbs(c.red()-50)<=1);QVERIFY(qAbs(c.green()-55)<=1);QVERIFY(qAbs(c.blue()-43)<=1);}
            if(brush.blendMode==BrushBlend::Screen)QVERIFY(pixel(d,100,100).red()>160);
            d.history()->undo();QCOMPARE(pixel(d,100,100),base.color);
        }
        brush.blendMode=BrushBlend::Dissolve;brush.strokeOpacity=.4;Document d;d.load(blank());tap(d,brush);int visible=0;for(int y=90;y<110;++y)for(int x=90;x<110;++x){const int a=pixel(d,x,y).alpha();QVERIFY(a==0||a==255);visible+=a>0;}QVERIFY(visible>100&&visible<230);
    }
    void symmetryGeometryAndGuides(){
        BrushSymmetry s;s.center={.5,.5};const QSizeF art(400,300);const QPointF p(240,180);
        s.mode=SymmetryMode::Vertical;auto copies=brushSymmetryInstances(s,p,art);QCOMPARE(copies.size(),2);QCOMPARE(copies[1].point,QPointF(160,180));
        s.mode=SymmetryMode::DualAxis;QCOMPARE(brushSymmetryInstances(s,p,art).size(),4);
        s.mode=SymmetryMode::Diagonal;copies=brushSymmetryInstances(s,p,art);QVERIFY(QLineF(copies[1].point,QPointF(230,190)).length()<.0001);QVERIFY(QLineF(copies[1].transform.map(QPointF(1,0)),QPointF(0,1)).length()<.0001);
        s.mode=SymmetryMode::Circle;s.radius=50;copies=brushSymmetryInstances(s,QPointF(260,150),art);QVERIFY(QLineF(copies[1].point,QPointF(240,150)).length()<.0001);
        s.mode=SymmetryMode::Wave;s.amplitude=0;copies=brushSymmetryInstances(s,p,art);QVERIFY(QLineF(copies[1].point,QPointF(240,120)).length()<.0001);
        s.mode=SymmetryMode::ParallelLines;s.count=5;s.distance=40;QCOMPARE(brushSymmetryInstances(s,p,art).size(),5);
        s.mode=SymmetryMode::Radial;s.count=8;copies=brushSymmetryInstances(s,p,art);QCOMPARE(copies.size(),8);for(const auto &c:copies)QVERIFY(std::abs(QLineF(c.point,QPointF(200,150)).length()-50)<.0001);
        for(int mode=1;mode<=int(SymmetryMode::Radial);++mode){s.mode=SymmetryMode(mode);QVERIFY(!brushSymmetryGuide(s,art).isEmpty());copies=brushSymmetryInstances(s,p,art);QVERIFY(copies.size()>1);for(const auto &c:copies){QVERIFY(std::isfinite(c.point.x()));QVERIFY(std::isfinite(c.point.y()));}}
        BrushSettings b;b.size=20;b.pressureSize=false;b.symmetry.mode=SymmetryMode::Vertical;Document d;d.load(blank());tap(d,b,{100,100});QCOMPARE(pixel(d,100,100),b.color);QCOMPARE(pixel(d,540,100),b.color);QCOMPARE(d.history()->count(),1);
    }
    void textureRemainsAnchoredAcrossSymmetryCopies(){
        BrushSettings b;b.size=24;b.tipMask=mask(24,24);b.pressureSize=false;
        b.texture.enabled=true;b.texture.pattern=mask(7,7,0);
        for(int y=0;y<7;++y)for(int x=0;x<3;++x)b.texture.pattern.scanLine(y)[x]=255;
        b.symmetry.mode=SymmetryMode::Vertical;
        Document d;d.load(blank());tap(d,b,{101.5,100.5});
        // Reflect x101.5 about artwork center x320 to x538.5. Compare this
        // second mark with independently painting at the same world position.
        Document plain;plain.load(blank());b.symmetry.mode=SymmetryMode::Off;tap(plain,b,{538.5,100.5});
        for(int y=94;y<107;++y)for(int x=533;x<544;++x)QCOMPARE(pixel(d,x,y),pixel(plain,x,y));
    }
    void randomizedStrokeIndependentOfEventDensity(){
        BrushSettings b;b.size=30;b.spacing=.3;b.pressureSize=false;b.scatter=.6;b.scatterBothAxes=true;b.count=3;b.countJitter=.5;b.sizeDynamics.jitter=.5;b.angleDynamics.jitter=.4;
        b.colorDynamics.enabled=true;b.colorDynamics.hue=.4;b.opacity=.3;b.strokeOpacity=.7;b.texture.enabled=true;b.texture.pattern=mask(3,3,170);b.texture.eachTip=true;b.texture.dynamics.jitter=.3;
        Document whole,split;whole.load(blank());split.load(blank());whole.setBrushRandomSeed(938);split.setBrushRandomSeed(938);
        QVERIFY(whole.beginStroke({50,100},1,b,false));whole.continueStroke({350,100},1);whole.endStroke();
        QVERIFY(split.beginStroke({50,100},1,b,false));for(int x:{71,106,181,247,300,350})split.continueStroke({qreal(x),100},1);split.endStroke();
        QCOMPARE(whole.project().shots.first().layers.first().tiles,split.project().shots.first().layers.first().tiles);
    }
    void advancedMaskTileSeamsAndCancellation(){
        BrushSettings b;b.size=48;b.color=QColor("#3469a4");b.opacity=.6;b.tipMask=mask(96,64);b.angle=23;b.roundness=.6;b.noise=true;b.wetEdges=true;
        Document negative,positive,unsplit;negative.load(blank());positive.load(blank());unsplit.load(blank());
        tap(negative,b,{.25,.75});tap(positive,b,{256.25,256.75});tap(unsplit,b,{200.25,200.75});
        for(int y=-30;y<=30;++y)for(int x=-30;x<=30;++x){QCOMPARE(pixel(negative,x,y),pixel(unsplit,200+x,200+y));QCOMPARE(pixel(positive,256+x,256+y),pixel(unsplit,200+x,200+y));}
        const auto before=negative.project().shots.first().layers.first().tiles;QVERIFY(negative.beginStroke({100,100},1,b,false));negative.continueStroke({300,200},.5);negative.cancelStroke();QCOMPARE(negative.project().shots.first().layers.first().tiles,before);
    }
};
QTEST_GUILESS_MAIN(AdvancedBrushTest)
#include "test_brush_advanced.moc"
