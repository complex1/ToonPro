#include "brush_surface_pattern.h"
#include "brush_surface_pattern_widget.h"
#include "brush_engine.h"
#include "document.h"
#include "object_tools.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QCheckBox>
#include <QDir>
#include <QImageReader>
#include <QJsonArray>
#include <QPainter>
#include <QSignalSpy>
#include <QTabBar>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <limits>
using namespace toon;
namespace {
QImage tile(){QImage image(16,16,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
    QPainter painter(&image);painter.fillRect(0,0,8,8,QColor(220,60,30,128));painter.fillRect(8,0,8,8,QColor(25,160,220,255));painter.fillRect(0,8,8,8,QColor(210,170,60,190));return image;}
QImage render(const BrushSurfacePattern &pattern,QSize size={200,200},QPoint origin={}){QImage image(size,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);applyBrushSurface(image,origin,BrushSurfaceSampler(pattern));return image;}
qreal meanAlpha(const QImage &image){qreal sum=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)sum+=qAlpha(image.pixel(x,y))/255.;return sum/(image.width()*image.height());}
int transitions(const QImage &image,int y){int count=0;bool previous=qAlpha(image.pixel(0,y))>128;for(int x=1;x<image.width();++x){const bool next=qAlpha(image.pixel(x,y))>128;count+=next!=previous;previous=next;}return count;}
}
class BrushSurfacePatternTests:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void stablePhaseAcrossOverlappingDabs_data(){QTest::addColumn<int>("kind");for(int i=1;i<=7;++i)QTest::newRow(qPrintable(QString::number(i)))<<i;}
    void stablePhaseAcrossOverlappingDabs(){QFETCH(int,kind);BrushSurfacePattern p;p.kind=BrushSurfaceKind(kind);p.image=tile();p.angle=23;p.phase={-21.25,31};p.density=.36;p.seed=141;
        const auto left=render(p,{100,80},{-20,10}),right=render(p,{100,80},{20,10});
        QCOMPARE(left.copy(40,0,60,80),right.copy(0,0,60,80));QCOMPARE(left,render(p,{100,80},{-20,10}));
    }
    void densityFrequencyAndAngleAreIndependent(){
        for(auto kind:{BrushSurfaceKind::Dots,BrushSurfaceKind::Lines,BrushSurfaceKind::Crosshatch,BrushSurfaceKind::Diamond}){
            BrushSurfacePattern p;p.kind=kind;p.density=.35;p.frequency=10;const auto first=render(p);const auto a=meanAlpha(first);QVERIFY2(std::abs(a-.35)<.025,qPrintable(QString("%1 mean %2").arg(int(kind)).arg(a)));
            p.frequency=20;const auto b=meanAlpha(render(p));QVERIFY(std::abs(a-b)<.035);
            p.density=.7;QVERIFY(meanAlpha(render(p))>.65);
        }
        BrushSurfacePattern p;p.kind=BrushSurfaceKind::Lines;p.density=.25;p.frequency=10;p.angle=90;
        const auto ten=render(p);p.frequency=20;const auto twenty=render(p);QVERIFY(transitions(twenty,100)>transitions(ten,100)*1.8);
        p.angle=0;const auto horizontal=render(p);QCOMPARE(transitions(horizontal,100),0);QVERIFY(horizontal!=twenty);
    }
    void exactExtremesAndNoiseSeed(){
        for(auto kind:{BrushSurfaceKind::Dots,BrushSurfaceKind::Lines,BrushSurfaceKind::Crosshatch,BrushSurfaceKind::Diamond,BrushSurfaceKind::Noise}){
            BrushSurfacePattern p;p.kind=kind;p.density=0;QCOMPARE(meanAlpha(render(p)),0.);p.density=1;QCOMPARE(meanAlpha(render(p)),1.);
        }
        BrushSurfacePattern p;p.kind=BrushSurfaceKind::Noise;p.frequency=20;p.seed=19;const auto a=render(p);p.seed=20;QVERIFY(a!=render(p));
        p.kind=BrushSurfaceKind::FadeDots;p.density=.9;p.densityEnd=.1;p.fadeLength=200;const auto fade=render(p);QVERIFY(meanAlpha(fade.copy(0,0,200,50))>meanAlpha(fade.copy(0,150,200,50))+.5);
    }
    void rgbaTilePreservesColorsTransparencyAndWraps(){
        BrushSurfacePattern p;p.kind=BrushSurfaceKind::Image;p.image=tile();p.tileSize={16,16};BrushSurfaceSampler sampler(p,Qt::magenta);
        for(int y=0;y<16;++y)for(int x=0;x<16;++x){QCOMPARE(sampler.sample({x+.5,y+.5}),p.image.pixel(x,y));QCOMPARE(sampler.sample({x+16.5,y-15.5}),p.image.pixel(x,y));}
        auto source=p.image;QImage dab(32,32,QImage::Format_ARGB32_Premultiplied);dab.fill(qPremultiply(qRgba(40,80,120,128)));QImage mask;applyBrushSurface(dab,{},sampler,true,&mask);
        QCOMPARE(mask.constScanLine(3)[3],128);QCOMPARE(qAlpha(dab.pixel(3,3)),64);QCOMPARE(qAlpha(dab.pixel(12,12)),0);
        for(int y=0;y<32;++y)for(int x=0;x<32;++x){const auto c=dab.pixel(x,y);QVERIFY(qRed(c)<=qAlpha(c)&&qGreen(c)<=qAlpha(c)&&qBlue(c)<=qAlpha(c));}
        QCOMPARE(p.image,source);
    }
    void imageMinificationAveragesInsteadOfAliasing(){
        BrushSurfacePattern p;p.kind=BrushSurfaceKind::Image;p.image=QImage(64,64,QImage::Format_ARGB32_Premultiplied);
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)p.image.setPixel(x,y,(x+y)%2?qRgb(255,255,255):qRgb(0,0,0));
        p.tileSize={1,1};BrushSurfaceSampler sampler(p);
        for(QPointF point:{QPointF(.5,.5),QPointF(.1,.7),QPointF(14.3,-21.4)}){const auto color=sampler.sample(point);QVERIFY(qRed(color)>=126&&qRed(color)<=129);QCOMPARE(qAlpha(color),255);}
    }
    void identityAndPerspectiveMapping(){
        BrushSurfacePattern p;p.kind=BrushSurfaceKind::Image;p.image=tile();p.tileSize={16,16};const BrushSurfaceSampler flat(p);p.perspective=true;const BrushSurfaceSampler identity(p);
        for(int y=10;y<220;y+=17)for(int x=10;x<220;x+=13)QCOMPARE(flat.sample({x+.5,y+.5}),identity.sample({x+.5,y+.5}));
        p.planeSize={128,128};p.quad={{{20,20},{240,50},{205,230},{45,210}}};QVERIFY(validateBrushSurface(p));BrushSurfaceSampler perspective(p);
        QTransform forward;QVERIFY(QTransform::quadToQuad(QPolygonF{{0,0},{128,0},{128,128},{0,128}},QPolygonF{p.quad[0],p.quad[1],p.quad[2],p.quad[3]},forward));
        for(QPointF source:{QPointF(32.5,32.5),QPointF(72.5,64.5),QPointF(44.5,28.5)}){
            const auto expected=flat.sample(source),actual=perspective.sample(forward.map(source));QVERIFY(std::abs(qRed(actual)-qRed(expected))<=1);QVERIFY(std::abs(qAlpha(actual)-qAlpha(expected))<=1);
        }
        QCOMPARE(perspective.sample({0,0}),QRgb(0));QCOMPARE(perspective.sample({500,500}),QRgb(0));
    }
    void malformedPlaneAndMetadataAreRejectedAtomically(){
        BrushSurfacePattern p;p.kind=BrushSurfaceKind::Dots;QString error;QVERIFY(validateBrushSurface(p,&error));
        auto bad=p;std::swap(bad.quad[1],bad.quad[2]);QVERIFY(!validateBrushSurface(bad,&error));bad=p;bad.quad[1]={1,0};QVERIFY(!validateBrushSurface(bad));bad=p;bad.quad[0].setX(std::numeric_limits<qreal>::quiet_NaN());QVERIFY(!validateBrushSurface(bad));
        bad=p;bad.image=QImage(2,2,QImage::Format_ARGB32_Premultiplied);bad.image.fill(qRgba(255,0,0,10));QVERIFY(!validateBrushSurface(bad));bad.image.fill(qPremultiply(qRgba(255,0,0,10)));QVERIFY(validateBrushSurface(bad));
        auto json=brushSurfaceToJson(p);auto restored=p;restored.image=tile();QVERIFY(brushSurfaceFromJson(json,restored,&error));QCOMPARE(restored.image,tile());
        const auto before=restored;json["perspective"]="yes";QVERIFY(!brushSurfaceFromJson(json,restored,&error));QCOMPARE(restored,before);
        json=brushSurfaceToJson(p);json["unexpected"]=3;QVERIFY(!brushSurfaceFromJson(json,restored,&error));QCOMPARE(restored,before);
        json=brushSurfaceToJson(p);json["seed"]=-1;QVERIFY(!brushSurfaceFromJson(json,restored,&error));
    }
    void repeatedDabsCapToneAntialiasCoverage(){
        BrushDab dab;dab.mask=QImage(80,80,QImage::Format_Grayscale8);dab.mask.fill(255);dab.color=Qt::black;dab.surface.kind=BrushSurfaceKind::Dots;dab.surface.frequency=13;dab.surface.density=.33;
        RasterLayer target,accumulation,original;const QRectF bounds(0,0,256,256);
        QVERIFY(applyBrushDab(target,accumulation,original,dab,{80,80},{},BrushBlend::Normal,false,bounds,64,1));const auto once=target.tiles;
        for(int i=0;i<30;++i)QVERIFY(applyBrushDab(target,accumulation,original,dab,{80,80},{},BrushBlend::Normal,false,bounds,64,1));QCOMPARE(target.tiles,once);
        const auto image=target.tiles.value({0,0});BrushSurfaceSampler sampler(dab.surface);bool antialiased=false;
        for(int y=42;y<118;++y)for(int x=42;x<118;++x){const int expected=qAlpha(sampler.sample({x+.5,y+.5}));QCOMPARE(qAlpha(image.pixel(x,y)),expected);antialiased|=expected>0&&expected<255;}QVERIFY(antialiased);
    }
    void rasterAndVectorStrokeReplayMatch(){
        auto project=Project::create();project.width=320;project.height=240;project.transparent=true;Document raster,vector;raster.load(project);project.shots[0].layers[0].kind=LayerKind::Vector;vector.load(project);
        BrushSettings b;b.size=64;b.pressureSize=false;b.spacing=.2;b.surface.kind=BrushSurfaceKind::Image;b.surface.image=tile();b.surface.tileSize={16,16};b.surface.perspective=true;b.surface.planeSize={256,256};b.surface.quad={{{0,0},{300,20},{280,240},{15,230}}};
        for(auto *d:{&raster,&vector}){QVERIFY(d->beginStroke({40,120},1,b,false));for(int i=1;i<=16;++i)d->continueStroke({40+i*14.,120+std::sin(i*.3)*35},1);d->endStroke();}
        const auto expected=raster.activeShot().layers[0].tiles;QVERIFY(!expected.isEmpty());QCOMPARE(vector.activeShot().layers[0].tiles,expected);
        auto layer=vector.activeShot().layers[0];QString error;QVERIFY2(rebuildObjectLayer(layer,vector.drawingBounds(),maxProjectTiles,&error),qPrintable(error));QCOMPARE(layer.tiles,expected);
        QTemporaryDir dir;const auto path=dir.filePath("surface.toonproj");QVERIFY2(saveProject(vector.project(),path,&error),qPrintable(error));auto loaded=loadProject(path,&error);QVERIFY2(loaded.has_value(),qPrintable(error));auto replay=loaded->shots[0].layers[0];QVERIFY(rebuildObjectLayer(replay,vector.drawingBounds(),maxProjectTiles,&error));QCOMPARE(replay.tiles,expected);
    }
    void widgetImportsAtomicallyAndEditsPlane(){
        QTemporaryDir dir;const auto path=dir.filePath("tile.png");QVERIFY(tile().save(path));BrushSurfacePatternWidget widget;widget.resize(340,750);widget.show();QTest::qWait(20);QSignalSpy edited(&widget,&BrushSurfacePatternWidget::patternEdited);QString error;
        QVERIFY(widget.importImage(path,&error));QCOMPARE(edited.size(),1);auto p=edited.last()[0].value<BrushSurfacePattern>();QCOMPARE(p.image,tile());QCOMPARE(p.kind,BrushSurfaceKind::Image);
        QVERIFY(!widget.importImage(dir.filePath("missing.png"),&error));QCOMPARE(edited.size(),1);
        widget.findChild<ValueSlider*>("surfaceTileWidth")->setValue(180);p=edited.last()[0].value<BrushSurfacePattern>();QCOMPARE(p.tileSize.width(),180.);
        widget.findChild<QCheckBox*>("surfacePerspective")->setChecked(true);p=edited.last()[0].value<BrushSurfacePattern>();QVERIFY(p.perspective);
        auto *preview=widget.findChild<QWidget*>("surfacePlanePreview");QVERIFY(preview);QTest::keyClick(preview,Qt::Key_Right);p=edited.last()[0].value<BrushSurfacePattern>();QCOMPARE(p.quad[0].x(),1.);QVERIFY(validateBrushSurface(p));
        const auto directory=qEnvironmentVariable("TOON_SURFACE_PREVIEW");if(!directory.isEmpty()){QDir().mkpath(directory);QVERIFY(widget.grab().save(directory+"/surface-editor.png"));}
    }
    void planeDragKeepsItsViewportWhileTheModelRefreshes(){
        BrushSurfacePattern p;p.kind=BrushSurfaceKind::Dots;p.perspective=true;
        BrushSurfacePatternWidget widget;widget.setPattern(p);widget.resize(340,720);widget.show();QTest::qWait(20);
        connect(&widget,&BrushSurfacePatternWidget::patternEdited,&widget,&BrushSurfacePatternWidget::setPattern);
        QSignalSpy edited(&widget,&BrushSurfacePatternWidget::patternEdited);
        auto *preview=widget.findChild<QWidget*>("surfacePlanePreview");QVERIFY(preview);
        const QPoint start(qRound(9+(preview->width()-18)*(.12/1.24)),qRound(9+(preview->height()-18)*(.12/1.24)));
        QTest::mousePress(preview,Qt::LeftButton,Qt::NoModifier,start);
        auto move=[&](int pixels){const QPoint at=start+QPoint(pixels,0);QMouseEvent event(QEvent::MouseMove,QPointF(at),QPointF(preview->mapToGlobal(at)),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(preview,&event);};
        move(20);QVERIFY(!edited.isEmpty());const qreal first=edited.last()[0].value<BrushSurfacePattern>().quad[0].x();
        move(40);const qreal second=edited.last()[0].value<BrushSurfacePattern>().quad[0].x();
        QVERIFY(first>30);QVERIFY(std::abs(second-2*first)<2);
        QTest::mouseRelease(preview,Qt::LeftButton,Qt::NoModifier,start+QPoint(40,0));
    }
    void realRenderPreview(){
        const auto directory=qEnvironmentVariable("TOON_SURFACE_PREVIEW");if(directory.isEmpty())QSKIP("Set TOON_SURFACE_PREVIEW to save the actual brush render sheet.");QDir().mkpath(directory);
        QImage sheet(960,620,QImage::Format_ARGB32_Premultiplied);sheet.fill(QColor("#ecebe7"));QPainter painter(&sheet);painter.setPen(QColor("#222222"));
        const QStringList names{"Dots · 10 / 100 px","Lines · 35°","Crosshatch · 30%","Diamond · 45%","Seeded noise","Dot fade","RGBA tile","Perspective tile"};
        const BrushSurfaceKind kinds[]{BrushSurfaceKind::Dots,BrushSurfaceKind::Lines,BrushSurfaceKind::Crosshatch,BrushSurfaceKind::Diamond,BrushSurfaceKind::Noise,BrushSurfaceKind::FadeDots,BrushSurfaceKind::Image,BrushSurfaceKind::Image};
        for(int i=0;i<8;++i){BrushDab dab;dab.mask=QImage(206,244,QImage::Format_Grayscale8);dab.mask.fill(255);dab.color=QColor("#263138");dab.surface.kind=kinds[i];dab.surface.image=tile();dab.surface.tileSize={32,32};dab.surface.density=.3;
            if(i==1)dab.surface.angle=35;if(i==3)dab.surface.density=.45;if(i==4)dab.surface.frequency=35;if(i==5){dab.surface.density=.8;dab.surface.densityEnd=.05;dab.surface.fadeLength=220;}
            if(i==7){dab.surface.perspective=true;dab.surface.planeSize={220,260};dab.surface.quad={{{12,15},{204,40},{175,245},{33,217}}};}
            RasterLayer target,accumulation,original;QVERIFY(applyBrushDab(target,accumulation,original,dab,{112,130},{},BrushBlend::Normal,false,QRectF(0,0,256,280),64,1));
            const QPoint at(12+(i%4)*240,26+(i/4)*310);painter.drawText(QRect(at-QPoint(0,20),QSize(220,20)),names[i]);painter.fillRect(QRect(at,QSize(220,270)),Qt::white);
            painter.save();painter.setClipRect(QRect(at,QSize(220,270)));for(auto it=target.tiles.begin();it!=target.tiles.end();++it)painter.drawImage(at+QPoint(it.key().first*tileSize,it.key().second*tileSize),it.value());painter.restore();
        }painter.end();QVERIFY(sheet.save(directory+"/surface-render-sheet.png"));
    }
};
QTEST_MAIN(BrushSurfacePatternTests)
#include "test_brush_surface_pattern.moc"
