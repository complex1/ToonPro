#include "canvas.h"
#include "mask_tool.h"
#include "camera_tool.h"
#include "keyframe_transform_tool.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include "animation_test_fixture.h"
#include "raster_tools.h"
#include <QMouseEvent>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QtTest>
#include <cmath>
using namespace toon;
namespace {
Project fixture(){auto p=Project::create();p.width=256;p.height=192;p.transparent=true;p.shots[0].layers.resize(1);auto &layer=p.shots[0].layers[0];layer.mask=LayerMask{};QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::red);layer.tiles.insert({0,0},tile);return p;}
void show(Canvas &c){c.resize(900,720);c.show();c.fitSheet();QCoreApplication::processEvents();}
void mouse(Canvas &c,QEvent::Type type,QPointF p,Qt::MouseButtons buttons=Qt::LeftButton){QMouseEvent e(type,p,p,c.mapToGlobal(p),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:buttons,Qt::NoModifier);QApplication::sendEvent(&c,&e);}
QPointF point(Canvas &c,QPointF p){
    const int frame=c.document()->currentFrame();
    auto artwork=(c.document()->keyframeSourceEditing()?QTransform():worldLayerTransform(c.shot(),c.shot().layers[c.document()->activeLayer()].id,frame)).map(p);
    if(c.cameraView())artwork=shotCameraTransform(c.shot(),frame,QSizeF(c.project().width,c.project().height)).map(artwork);
    return c.viewTransform().map(artwork+sheetGeometry(c.project(),c.shot()).output.topLeft());
}
int value(Canvas &c,int x,int y){const auto &m=*c.shot().layers[c.document()->activeLayer()].mask;const auto tile=m.tiles.value({x/tileSize,y/tileSize});return tile.isNull()?m.defaultValue:tile.constScanLine(y%tileSize)[x%tileSize];}
void dab(Canvas &c,QPointF p){mouse(c,QEvent::MouseButtonPress,point(c,p));mouse(c,QEvent::MouseButtonRelease,point(c,p));}
}
class EffectsCanvasTests final:public QObject{
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void maskStrokeAndEscapeNeverModifyArtwork(){
        Canvas c;c.setProject(fixture());show(c);auto *tool=c.maskTool();tool->setSize(30);tool->setHardness(1);tool->setEnabled(true);QVERIFY(tool->enabled());const auto source=c.shot().layers[0].tiles;
        mouse(c,QEvent::MouseButtonPress,point(c,{40,40}));mouse(c,QEvent::MouseMove,point(c,{90,40}));QVERIFY(tool->dragging());QCOMPARE(c.document()->history()->count(),0);
        mouse(c,QEvent::MouseButtonRelease,point(c,{90,40}));QVERIFY(!tool->dragging());QCOMPARE(c.document()->history()->count(),1);QCOMPARE(value(c,60,40),0);QCOMPARE(value(c,60,90),255);QCOMPARE(c.shot().layers[0].tiles,source);
        c.document()->history()->undo();QCOMPARE(value(c,60,40),255);c.document()->history()->redo();QCOMPARE(value(c,60,40),0);
        const auto mask=c.shot().layers[0].mask;mouse(c,QEvent::MouseButtonPress,point(c,{140,80}));QTest::keyClick(&c,Qt::Key_Escape);QCOMPARE(c.shot().layers[0].mask,mask);QVERIFY(!tool->dragging());
        tool->setRevealing(true);dab(c,{60,40});QCOMPARE(value(c,60,40),255);QCOMPARE(c.shot().layers[0].tiles,source);
    }
    void selectionCameraAndAnimatedTransformMapIntoMaskSpace(){
        auto p=fixture();auto &shot=p.shots[0];const auto id=shot.layers[0].id;shot.camera2D={{25,15},1.3,20};
        shot.layerAnimations={{id,{},{{KeyframeProperty::PositionX,{{0,20}}},{KeyframeProperty::ScaleX,{{0,1.5}}},{KeyframeProperty::ScaleY,{{0,1.5}}},{KeyframeProperty::Rotation,{{0,15}}}}, {}}};
        Canvas c;c.setProject(p);show(c);QPainterPath path;path.addRect(40,20,40,60);c.document()->setRasterSelection(worldLayerTransform(shot,id,0).map(path));
        auto *tool=c.maskTool();tool->setSize(50);tool->setHardness(1);tool->setEnabled(true);dab(c,{40,40});
        QVERIFY(value(c,45,40)<20);QCOMPARE(value(c,30,40),255);QCOMPARE(value(c,100,100),255);
        mouse(c,QEvent::MouseMove,point(c,{70,40}),Qt::NoButton);QVERIFY(!tool->cursorPath().isEmpty());
        const auto rect=tool->cursorPath().boundingRect();QVERIFY(std::abs(rect.width()-50*1.5*1.3*c.zoom())<.2);
    }
    void toolFrameSourceAndPlaybackResolveMaskGestures(){
        Canvas c;c.setProject(fixture());TimelineController timeline(&c);show(c);auto *tool=c.maskTool();tool->setEnabled(true);
        mouse(c,QEvent::MouseButtonPress,point(c,{40,40}));QVERIFY(timeline.setFrame(6));QVERIFY(!tool->dragging());QCOMPARE(c.document()->history()->count(),1);
        c.cameraTool()->setEnabled(true);QVERIFY(!tool->enabled());tool->setEnabled(true);QVERIFY(!c.cameraTool()->enabled());
        c.keyframeTransformTool()->setEnabled(true);QVERIFY(!tool->enabled());tool->setEnabled(true);QVERIFY(!c.keyframeTransformTool()->enabled());
        timeline.setSourceEditing(true);QVERIFY(!tool->enabled());tool->setEnabled(true);c.setTool(Canvas::Tool::Eraser);QVERIFY(!tool->enabled());
        tool->setEnabled(true);c.setProject(fixture());QVERIFY(!tool->enabled());QVERIFY(!tool->dragging());
    }
    void tabletPressureAndMissingRelease(){
        Canvas c;c.setProject(fixture());show(c);auto *tool=c.maskTool();tool->setSize(20);tool->setHardness(1);tool->setEnabled(true);
        QPointingDevice pen("Mask test pen",92,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
        const auto send=[&](QEvent::Type type,QPointF local,qreal pressure,Qt::MouseButtons buttons){const auto pos=point(c,local);QTabletEvent e(type,&pen,pos,c.mapToGlobal(pos),pressure,0,0,0,0,0,Qt::NoModifier,type==QEvent::TabletMove?Qt::NoButton:Qt::LeftButton,buttons);QApplication::sendEvent(&c,&e);};
        send(QEvent::TabletPress,{40,40},.5,Qt::LeftButton);QVERIFY(tool->dragging());QVERIFY(value(c,40,40)>100&&value(c,40,40)<150);
        send(QEvent::TabletMove,{120,120},0,Qt::NoButton);QVERIFY(!tool->dragging());QCOMPARE(c.document()->history()->count(),1);QCOMPARE(value(c,120,120),255);
        const auto mask=c.shot().layers[0].mask;send(QEvent::TabletRelease,{120,120},0,Qt::NoButton);QCOMPARE(c.shot().layers[0].mask,mask);
    }
    void maskBarrelReleaseKeepsTheTipStrokeAndOneUndo(){
        Canvas c;c.setProject(fixture());show(c);auto *tool=c.maskTool();tool->setSize(16);tool->setHardness(1);tool->setEnabled(true);
        const auto artwork=c.shot().layers[0].tiles;
        QPointingDevice pen("Mask barrel pen",93,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,3);
        const auto send=[&](QEvent::Type type,QPointF local,qreal pressure,Qt::MouseButton button,Qt::MouseButtons buttons){
            const auto pos=point(c,local);QTabletEvent e(type,&pen,pos,c.mapToGlobal(pos),pressure,0,0,0,0,0,Qt::NoModifier,button,buttons);QApplication::sendEvent(&c,&e);
        };
        send(QEvent::TabletPress,{40,40},1,Qt::LeftButton,Qt::LeftButton);
        send(QEvent::TabletMove,{80,40},1,Qt::NoButton,Qt::LeftButton);
        send(QEvent::TabletPress,{80,40},1,Qt::RightButton,Qt::LeftButton|Qt::RightButton);
        send(QEvent::TabletRelease,{80,40},1,Qt::RightButton,Qt::LeftButton);
        QVERIFY(tool->dragging());QVERIFY(c.document()->effectsEditActive());QCOMPARE(c.document()->history()->count(),0);
        send(QEvent::TabletMove,{160,40},1,Qt::NoButton,Qt::LeftButton);
        send(QEvent::TabletRelease,{160,40},0,Qt::LeftButton,Qt::NoButton);
        QVERIFY(!tool->dragging());QCOMPARE(c.document()->history()->count(),1);QCOMPARE(value(c,120,40),0);
        QCOMPARE(c.shot().layers[0].tiles,artwork);
        c.document()->history()->undo();QCOMPARE(c.shot().layers[0].mask,fixture().shots[0].layers[0].mask);
    }
    void interruptedMaskContactEndsWithoutAConnectingLine_data(){
        QTest::addColumn<int>("interruption");
        QTest::newRow("leave-proximity")<<0;
        QTest::newRow("release-over-panel")<<1;
        QTest::newRow("hover-over-panel")<<2;
        QTest::newRow("next-tip-press")<<3;
        QTest::newRow("new-tip-press-over-panel")<<4;
        QTest::newRow("new-tip-press-over-floating-panel")<<5;
    }
    void interruptedMaskContactEndsWithoutAConnectingLine(){
        QFETCH(int,interruption);
        QWidget workspace;Canvas c(&workspace);c.setProject(fixture());show(c);auto *tool=c.maskTool();tool->setSize(16);tool->setHardness(1);tool->setEnabled(true);
        QPointingDevice pen("Interrupted mask pen",94,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,3);
        const auto send=[&](QEvent::Type type,QPointF local,qreal pressure){
            const auto pos=point(c,local);QTabletEvent e(type,&pen,pos,c.mapToGlobal(pos),pressure,0,0,0,0,0,Qt::NoModifier,
                type==QEvent::TabletMove?Qt::NoButton:Qt::LeftButton,type==QEvent::TabletRelease?Qt::NoButton:Qt::LeftButton);QApplication::sendEvent(&c,&e);
        };
        send(QEvent::TabletPress,{40,40},1);send(QEvent::TabletMove,{80,40},1);
        const auto first=c.shot().layers[0].mask;QVERIFY(tool->dragging());
        if(interruption==3)send(QEvent::TabletPress,{180,140},1);
        else {
            QWidget panel(&workspace,interruption==5?Qt::Tool:Qt::Widget);
            if(interruption==5){QVERIFY(panel.isWindow());QCOMPARE(panel.parentWidget(),&workspace);QVERIFY(panel.window()!=c.window());}
            const auto type=interruption==0?QEvent::TabletLeaveProximity:interruption==1?QEvent::TabletRelease:
                interruption>=4?QEvent::TabletPress:QEvent::TabletMove;
            QTabletEvent e(type,&pen,{10,10},panel.mapToGlobal(QPoint(10,10)),interruption>=4?1:0,0,0,0,0,0,Qt::NoModifier,
                interruption==1||interruption>=4?Qt::LeftButton:Qt::NoButton,interruption>=4?Qt::LeftButton:Qt::NoButton);
            QApplication::sendEvent(interruption==0?static_cast<QObject*>(qApp):&panel,&e);
        }
        QCOMPARE(c.document()->history()->count(),1);QCOMPARE(tool->dragging(),interruption==3);
        if(interruption!=3){QCOMPARE(c.shot().layers[0].mask,first);send(QEvent::TabletPress,{180,140},1);}
        send(QEvent::TabletMove,{220,140},1);send(QEvent::TabletRelease,{220,140},0);
        QCOMPARE(c.document()->history()->count(),2);QCOMPARE(value(c,200,140),0);QCOMPARE(value(c,130,90),255);
        c.document()->history()->undo();QCOMPARE(c.shot().layers[0].mask,first);
    }
    void disabledMaskAndLockedLayersBlockPainting(){
        Canvas c;c.setProject(fixture());show(c);auto *tool=c.maskTool();tool->setEnabled(true);QVERIFY(c.document()->setLayerMaskEnabled(false));QVERIFY(!tool->unavailableReason().isEmpty());
        const auto before=c.project().toJson();dab(c,{50,50});QCOMPARE(c.project().toJson(),before);
        QVERIFY(c.document()->setLayerMaskEnabled(true));tool->setEnabled(true);const auto id=c.shot().layers[0].id;c.document()->setLayersLocked({id},true);QVERIFY(!tool->unavailableReason().isEmpty());
        const auto locked=c.project().toJson();dab(c,{50,50});QCOMPARE(c.project().toJson(),locked);
    }
    void spaceDuringMaskStrokeCannotSwallowReleaseOrPan(){
        Canvas c;c.setProject(fixture());show(c);auto *tool=c.maskTool();tool->setSize(20);tool->setHardness(1);tool->setEnabled(true);
        const auto view=c.viewTransform();
        mouse(c,QEvent::MouseButtonPress,point(c,{40,40}));QVERIFY(tool->dragging());
        QTest::keyPress(&c,Qt::Key_Space);mouse(c,QEvent::MouseMove,point(c,{90,40}));
        mouse(c,QEvent::MouseButtonRelease,point(c,{90,40}));
        QVERIFY(!tool->dragging());QVERIFY(!c.document()->effectsEditActive());
        QTest::keyRelease(&c,Qt::Key_Space);QCOMPARE(c.viewTransform(),view);
        QCOMPARE(c.document()->history()->count(),1);QCOMPARE(value(c,70,40),0);
        QVERIFY(c.document()->setCurrentFrame(1));
    }
    void lowStrengthAccumulatesToFullyHiddenAndRevealedWithinOneStroke(){
        Canvas c;c.setProject(fixture());show(c);auto *tool=c.maskTool();tool->setSize(20);tool->setHardness(1);tool->setStrength(.01);tool->setEnabled(true);
        const auto source=c.shot().layers[0].tiles;
        for(bool reveal:{false,true}) {
            tool->setRevealing(reveal);mouse(c,QEvent::MouseButtonPress,point(c,{100,100}));
            for(int i=0;i<800;++i)mouse(c,QEvent::MouseMove,point(c,{i%2?100.:102.,100}));
            mouse(c,QEvent::MouseButtonRelease,point(c,{100,100}));
            QCOMPARE(value(c,100,100),reveal?255:0);QVERIFY(!tool->dragging());QVERIFY(!c.document()->effectsEditActive());
        }
        QCOMPARE(c.document()->history()->count(),2);QCOMPARE(c.shot().layers[0].tiles,source);
        c.document()->history()->undo();QCOMPARE(value(c,100,100),0);
        c.document()->history()->undo();QCOMPARE(value(c,100,100),255);
        tool->setRevealing(false);mouse(c,QEvent::MouseButtonPress,point(c,{100,100}));QTest::keyClick(&c,Qt::Key_Escape);
        QCOMPARE(value(c,100,100),255);dab(c,{100,100});QVERIFY(value(c,100,100)>=251&&value(c,100,100)<=253);
    }
    void exhaustedProjectMaskBudgetCancelsWithoutTouchingPixels(){
        auto project=fixture();auto other=RasterLayer::create("Other masks");other.mask=LayerMask{};
        QImage image(tileSize,tileSize,QImage::Format_Grayscale8);image.fill(255);
        for(int i=0;i<maxProjectMaskTiles;++i)other.mask->tiles.insert({i/32,i%32},image);
        project.shots[0].layers.append(other);QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        Canvas c;c.setProject(project);show(c);auto *tool=c.maskTool();tool->setEnabled(true);QSignalSpy warning(c.document(),&Document::warning);
        const auto before=c.project().toJson();dab(c,{40,40});
        QVERIFY(!tool->dragging());QVERIFY(!c.document()->effectsEditActive());QCOMPARE(c.document()->history()->count(),0);
        QCOMPARE(c.project().toJson(),before);QCOMPARE(c.shot().layers[0].tiles,project.shots[0].layers[0].tiles);
        QVERIFY(!warning.isEmpty());QVERIFY(warning.first()[0].toString().contains("limit"));
    }
    void paintingRefreshesShadowAcrossTileBoundaryBeforeRelease(){
        auto project=fixture();project.width=512;project.transparent=false;
        auto &layer=project.shots[0].layers[0];layer.tiles.clear();layer.mask.reset();
        layer.effects.shadowEnabled=true;layer.effects.shadowRadius=0;layer.effects.shadowOffset={280,0};
        layer.effects.shadowColor=Qt::blue;layer.effects.shadowOpacity=1;
        Canvas c;c.setProject(project);show(c);c.brush.size=18;c.brush.hardness=1;c.brush.pressureSize=false;
        c.brush.opacity=1;c.brush.strokeOpacity=1;c.brush.color=Qt::red;c.setTool(Canvas::Tool::Brush);
        const auto colorAt=[&](const QImage &image,QPointF artwork){return image.pixelColor((point(c,artwork)*image.devicePixelRatio()).toPoint());};
        const auto settled=[&]{c.repaint();return QTest::qWaitFor([&]{return !c.property("effectsPreviewPending").toBool();},30000);};
        QVERIFY(settled());const auto before=c.grab().toImage();QCOMPARE(colorAt(before,{320,80}),QColor(Qt::white));
        mouse(c,QEvent::MouseButtonPress,point(c,{40,80}));QVERIFY(c.document()->strokeActive());
        QVERIFY(settled());const auto during=c.grab().toImage();const auto shadow=colorAt(during,{320,80});
        QVERIFY2(shadow.blue()>180&&shadow.red()<70,qPrintable(shadow.name()));
        mouse(c,QEvent::MouseMove,point(c,{80,80}));QVERIFY(c.document()->strokeActive());
        QVERIFY(settled());const auto moved=c.grab().toImage();const auto newShadow=colorAt(moved,{360,80});
        QVERIFY2(newShadow.blue()>180&&newShadow.red()<70,qPrintable(newShadow.name()));
        QCOMPARE(c.document()->history()->count(),0);mouse(c,QEvent::MouseButtonRelease,point(c,{80,80}));
        QCOMPARE(c.document()->history()->count(),1);
    }
    void transformedFeatherUsesWorldCoverage_data(){
        QTest::addColumn<qreal>("scaleX");QTest::addColumn<qreal>("scaleY");QTest::addColumn<QRectF>("selection");
        QTest::addColumn<QPoint>("sourcePixel");QTest::addColumn<int>("expected");
        // A 21-pixel box feather of the world rectangle yields 61 and 73
        // at x=34 and 35. Source x=17.5 maps halfway between them: 67.
        QTest::newRow("anisotropic")<<qreal(2)<<qreal(1)<<QRectF(40,40,40,40)<<QPoint(17,60)<<67;
        // Under 5% scale, the ten-world-pixel feather reaches 200 source
        // pixels. The two-axis box coverage near world (12.525,12.525) is 5.
        QTest::newRow("small-scale")<<qreal(.05)<<qreal(.05)<<QRectF(20,20,20,20)<<QPoint(250,250)<<5;
    }
    void transformedFeatherUsesWorldCoverage(){
        QFETCH(qreal,scaleX);QFETCH(qreal,scaleY);QFETCH(QRectF,selection);QFETCH(QPoint,sourcePixel);QFETCH(int,expected);
        auto project=fixture();auto &shot=project.shots[0];shot.layers[0].mask.reset();const auto id=shot.layers[0].id;
        shot.layerAnimations={{id,{},{{KeyframeProperty::ScaleX,{{0,scaleX}}},{KeyframeProperty::ScaleY,{{0,scaleY}}}}, {}}};
        Canvas c;c.setProject(project);show(c);QPainterPath path;path.addRect(selection);c.document()->setRasterSelection(path);
        QVERIFY(RasterTools::featherSelection(c.document(),10));QVERIFY(c.document()->addLayerMask(true));
        QCOMPARE(value(c,sourcePixel.x(),sourcePixel.y()),expected);QCOMPARE(c.document()->history()->count(),1);
        c.document()->history()->undo();QVERIFY(!c.shot().layers[0].mask);QVERIFY(c.document()->addLayerMask());
        auto *tool=c.maskTool();tool->setSize(20);tool->setHardness(1);tool->setEnabled(true);
        dab(c,QPointF(sourcePixel)+QPointF(.5,.5));
        QCOMPARE(value(c,sourcePixel.x(),sourcePixel.y()),255-expected);
        QCOMPARE(c.shot().layers[0].tiles,project.shots[0].layers[0].tiles);
    }
    void adjustmentCannotReplaceLastRasterCelOrTemplateRole(){
        auto project=animation_fixture::project();Document document;document.load(project);
        document.selectLayer(3);const auto drawingLayer=project.shots[0].layers[3].id;
        QVERIFY(document.addAdjustmentLayer());const auto after=document.project().toJson();
        document.deleteLayers({drawingLayer});QCOMPARE(document.project().toJson(),after);
        auto malformed=document.project();auto &drawing=malformed.shots[0].animationTracks[0].drawings[0];
        drawing.templateLayers["ink"]=malformed.shots[0].layers[document.activeLayer()].id;
        QVERIFY(!malformed.validate().isEmpty());QString error;QVERIFY(!Project::fromJson(malformed.toJson(),&error));
    }
};
QTEST_MAIN(EffectsCanvasTests)
#include "effects_canvas_tests.moc"
