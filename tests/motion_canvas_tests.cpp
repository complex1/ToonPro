#include "canvas.h"
#include "keyframe_transform_tool.h"
#include "motion_controls.h"
#include "sheet.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include "animation_test_fixture.h"
#include <QDir>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointingDevice>
#include <QSignalSpy>
#include <QTabletEvent>
#include <QtTest>
#include <cmath>
using namespace toon;
namespace {
using Mode=KeyframeTransformTool::Mode;
using P=KeyframeProperty;
Project fixture(bool grouped=false){
    auto project=Project::create();project.width=256;project.height=192;project.transparent=true;
    auto &shot=project.shots[0];shot.durationFrames=31;shot.sheet.overscan=0;shot.layers.resize(1);
    QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);
    {QPainter painter(&tile);painter.fillRect(QRect(32,24,64,48),Qt::red);}shot.layers[0].tiles[{0,0}]=tile;
    if(grouped){auto parent=RasterLayer::create("Parent");parent.group=true;shot.layers[0].parentId=parent.id;shot.layers.prepend(parent);}
    return project;
}
void show(Canvas &canvas){canvas.resize(900,720);canvas.show();canvas.fitSheet();canvas.setZoom(1.8);canvas.setRotation(23);QCoreApplication::processEvents();}
QTransform artworkView(const Canvas &canvas){auto transform=canvas.viewTransform();const auto origin=sheetGeometry(canvas.project(),canvas.shot()).output.topLeft();transform.translate(origin.x(),origin.y());return transform;}
void pointer(Canvas &canvas,QEvent::Type type,QPointF point,Qt::MouseButtons buttons=Qt::LeftButton,Qt::MouseEventSource source=Qt::MouseEventNotSynthesized){
    QMouseEvent event(type,point,point,canvas.mapToGlobal(point),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
        type==QEvent::MouseButtonRelease?Qt::NoButton:buttons,Qt::NoModifier,source);
    QApplication::sendEvent(&canvas,&event);
}
void drag(Canvas &canvas,QPointF from,QPointF to){pointer(canvas,QEvent::MouseButtonPress,from);pointer(canvas,QEvent::MouseMove,to);pointer(canvas,QEvent::MouseButtonRelease,to);}
bool near(QPointF a,QPointF b,qreal epsilon=.0001){return QLineF(a,b).length()<epsilon;}
void selectChild(Canvas &canvas){canvas.document()->selectLayer(canvas.shot().layers.size()-1);}
void motion(Canvas &canvas,bool parent=false){
    auto *document=canvas.document();const auto id=canvas.shot().layers.last().id;
    QVERIFY(document->setCurrentFrame(20));
    if(parent){const auto parentId=canvas.shot().layers.first().id;
        QVERIFY(document->setKeyframeValue(parentId,P::PositionX,35));QVERIFY(document->setKeyframeValue(parentId,P::PositionY,-8));
        QVERIFY(document->setKeyframeValue(parentId,P::ScaleX,1.7));QVERIFY(document->setKeyframeValue(parentId,P::ScaleY,.6));
        QVERIFY(document->setKeyframeValue(parentId,P::Rotation,42));}
    QVERIFY(document->setKeyframeValue(id,P::PositionX,45));QVERIFY(document->setKeyframeValue(id,P::PositionY,22));
    QVERIFY(document->setKeyframeInterpolation(id,P::PositionX,20,KeyframeInterpolation::EaseOut));
    QVERIFY(document->setKeyframeInterpolation(id,P::PositionY,20,KeyframeInterpolation::Hold));
    QVERIFY(document->setCurrentFrame(10));selectChild(canvas);
}
void enable(Canvas &canvas,Mode mode){auto *tool=canvas.keyframeTransformTool();tool->setMode(mode);tool->setEnabled(true);QVERIFY(tool->enabled());}
QImage overlay(Canvas &canvas){QImage result(canvas.size(),QImage::Format_ARGB32_Premultiplied);result.fill(Qt::transparent);QPainter painter(&result);canvas.keyframeTransformTool()->paint(painter);return result;}
bool empty(const QImage &image){for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)if(qAlpha(image.pixel(x,y)))return false;return true;}
}
class MotionCanvasTests final:public QObject{
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void dragEarlierMarkerUsesItsParentFrame_data(){QTest::addColumn<bool>("grouped");QTest::newRow("layer")<<false;QTest::newRow("animated-parent")<<true;}
    void dragEarlierMarkerUsesItsParentFrame(){
        QFETCH(bool,grouped);Canvas canvas;canvas.setProject(fixture(grouped));motion(canvas,grouped);show(canvas);enable(canvas,Mode::MotionPath);
        auto *document=canvas.document();const auto id=canvas.shot().layers.last().id;const auto before=canvas.project().toJson();const auto source=canvas.shot().layers.last().tiles;
        const auto oldX=*propertyChannel(canvas.shot(),id,P::PositionX),oldY=*propertyChannel(canvas.shot(),id,P::PositionY);
        const auto parent=grouped?worldLayerTransform(canvas.shot(),canvas.shot().layers.first().id,20):QTransform();
        const auto position=animationPivotWorld(canvas.shot(),id,20);const QPointF delta(8,-7);
        const auto start=artworkView(canvas).map(position),end=artworkView(canvas).map(parent.map(parent.inverted().map(position)+delta));
        const int count=document->history()->count();pointer(canvas,QEvent::MouseButtonPress,start);QVERIFY(canvas.keyframeTransformTool()->dragging());
        pointer(canvas,QEvent::MouseMove,end);QCOMPARE(document->currentFrame(),10);QCOMPARE(document->history()->count(),count);
        QVERIFY(std::abs(evaluateProperty(canvas.shot(),id,P::PositionX,20)-53)<.0001);QVERIFY(std::abs(evaluateProperty(canvas.shot(),id,P::PositionY,20)-15)<.0001);
        pointer(canvas,QEvent::MouseButtonRelease,end);QCOMPARE(document->history()->count(),count+1);QCOMPARE(canvas.shot().layers.last().tiles,source);
        for(const auto property:{P::PositionX,P::PositionY}){const auto *channel=propertyChannel(canvas.shot(),id,property);const auto &old=property==P::PositionX?oldX:oldY;
            QCOMPARE(channel->keys.size(),old.keys.size());for(int i=0;i<old.keys.size();++i){QCOMPARE(channel->keys[i].frame,old.keys[i].frame);QCOMPARE(channel->keys[i].interpolation,old.keys[i].interpolation);}}
        document->history()->undo();QCOMPARE(canvas.project().toJson(),before);
    }
    void currentMarkerCreatesPairedPositionKeys(){
        Canvas canvas;canvas.setProject(fixture());auto *document=canvas.document();QVERIFY(document->setCurrentFrame(10));show(canvas);enable(canvas,Mode::MotionPath);
        const auto id=canvas.shot().layers.first().id;auto *tool=canvas.keyframeTransformTool();const auto before=canvas.project().toJson();const int count=document->history()->count();
        const auto start=tool->pivotHandle(),end=artworkView(canvas).map(canvas.artworkPoint(start)+QPointF(16,9));drag(canvas,start,end);
        for(const auto property:{P::PositionX,P::PositionY}){const auto *channel=propertyChannel(canvas.shot(),id,property);QVERIFY(channel);QCOMPARE(channel->keys.size(),2);QCOMPARE(channel->keys.last().frame,10);}
        QVERIFY(std::abs(evaluateProperty(canvas.shot(),id,P::PositionX,10)-16)<.0001);QVERIFY(std::abs(evaluateProperty(canvas.shot(),id,P::PositionY,10)-9)<.0001);
        QCOMPARE(document->history()->count(),count+1);document->history()->undo();QCOMPARE(canvas.project().toJson(),before);
    }
    void pivotPreservesCurrentPoseUnderParentAndViewTransforms(){
        Canvas canvas;canvas.setProject(fixture(true));motion(canvas,true);auto *document=canvas.document();const auto id=canvas.shot().layers.last().id;
        QVERIFY(document->setCurrentFrame(20));QVERIFY(document->setKeyframeValue(id,P::Rotation,60));QVERIFY(document->setKeyframeValue(id,P::ScaleX,1.5));QVERIFY(document->setKeyframeValue(id,P::ScaleY,.5));QVERIFY(document->setCurrentFrame(10));selectChild(canvas);
        show(canvas);enable(canvas,Mode::Pivot);auto *tool=canvas.keyframeTransformTool();const auto before=canvas.project().toJson();const auto world=worldLayerTransform(canvas.shot(),id,10);
        const auto originalPivot=effectiveAnimationPivot(canvas.shot(),id),target=originalPivot+QPointF(18,-11);const int count=document->history()->count();
        const auto start=tool->pivotHandle(),end=artworkView(canvas).map(world.map(target));pointer(canvas,QEvent::MouseButtonPress,start);pointer(canvas,QEvent::MouseMove,end);
        QVERIFY(near(effectiveAnimationPivot(canvas.shot(),id),target));QVERIFY(near(tool->pivotHandle(),end));
        for(const auto point:{QPointF(0,0),QPointF(32,24),QPointF(96,72),QPointF(200,100)})QVERIFY(near(worldLayerTransform(canvas.shot(),id,10).map(point),world.map(point)));
        QCOMPARE(document->history()->count(),count);pointer(canvas,QEvent::MouseButtonRelease,end);QCOMPARE(document->history()->count(),count+1);
        const auto after=canvas.project().toJson();document->history()->undo();QCOMPARE(canvas.project().toJson(),before);document->history()->redo();QCOMPARE(canvas.project().toJson(),after);
    }
    void escapeAndReturnToStart_data(){QTest::addColumn<int>("mode");QTest::newRow("path")<<int(Mode::MotionPath);QTest::newRow("pivot")<<int(Mode::Pivot);}
    void escapeAndReturnToStart(){
        QFETCH(int,mode);Canvas canvas;canvas.setProject(fixture());QVERIFY(canvas.document()->setCurrentFrame(10));show(canvas);enable(canvas,Mode(mode));
        auto *tool=canvas.keyframeTransformTool();const auto before=canvas.project().toJson();const int count=canvas.document()->history()->count();const auto start=tool->pivotHandle();
        pointer(canvas,QEvent::MouseButtonPress,start);pointer(canvas,QEvent::MouseMove,start+QPointF(35,25));QVERIFY(canvas.document()->keyframeEditActive());QVERIFY(canvas.project().toJson()!=before);
        QTest::keyClick(&canvas,Qt::Key_Escape);QVERIFY(!tool->dragging());QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),count);
        pointer(canvas,QEvent::MouseButtonPress,start);pointer(canvas,QEvent::MouseMove,start+QPointF(35,25));pointer(canvas,QEvent::MouseMove,start);pointer(canvas,QEvent::MouseButtonRelease,start);
        QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),count);QVERIFY(!canvas.document()->keyframeEditActive());
    }
    void frameModeAndFocusTransitionsResolveGestures(){
        Canvas canvas;canvas.setProject(fixture());TimelineController timeline(&canvas);QVERIFY(timeline.setFrame(10));show(canvas);enable(canvas,Mode::MotionPath);auto *tool=canvas.keyframeTransformTool();auto *document=canvas.document();
        const int count=document->history()->count();auto start=tool->pivotHandle();pointer(canvas,QEvent::MouseButtonPress,start);pointer(canvas,QEvent::MouseMove,start+QPointF(35,25));
        QVERIFY(timeline.setFrame(12));QVERIFY(!tool->dragging());QVERIFY(!document->keyframeEditActive());QCOMPARE(document->history()->count(),count+1);
        start=tool->pivotHandle();pointer(canvas,QEvent::MouseButtonPress,start);pointer(canvas,QEvent::MouseMove,start+QPointF(20,15));tool->setMode(Mode::Pivot);
        QVERIFY(!tool->dragging());QCOMPARE(document->history()->count(),count+2);QVERIFY(tool->enabled());
        start=tool->pivotHandle();pointer(canvas,QEvent::MouseButtonPress,start);pointer(canvas,QEvent::MouseMove,start+QPointF(-20,15));QFocusEvent focus(QEvent::FocusOut);QApplication::sendEvent(&canvas,&focus);
        QVERIFY(!tool->dragging());QCOMPARE(document->history()->count(),count+3);
        tool->setMode(Mode::MotionPath);start=tool->pivotHandle();pointer(canvas,QEvent::MouseButtonPress,start);pointer(canvas,QEvent::MouseMove,start+QPointF(25,15));canvas.setTool(Canvas::Tool::Eraser);
        QVERIFY(!tool->enabled());QVERIFY(!document->keyframeEditActive());QCOMPARE(document->history()->count(),count+4);
    }
    void lostContactAndSyntheticMouseRespectTabletGesture(){
        Canvas canvas;canvas.setProject(fixture());QVERIFY(canvas.document()->setCurrentFrame(10));show(canvas);enable(canvas,Mode::MotionPath);
        auto *tool=canvas.keyframeTransformTool();auto *document=canvas.document();int count=document->history()->count();auto start=tool->pivotHandle();
        pointer(canvas,QEvent::MouseButtonPress,start);pointer(canvas,QEvent::MouseMove,start+QPointF(25,15));pointer(canvas,QEvent::MouseMove,start+QPointF(100,100),Qt::NoButton);
        QVERIFY(!tool->dragging());QCOMPARE(document->history()->count(),count+1);const auto mouseResult=canvas.project().toJson();pointer(canvas,QEvent::MouseButtonRelease,start+QPointF(100,100));QCOMPARE(canvas.project().toJson(),mouseResult);
        QPointingDevice pen("Motion test pen",79,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
        const auto tablet=[&](QEvent::Type type,QPointF point,Qt::MouseButtons buttons){QTabletEvent event(type,&pen,point,canvas.mapToGlobal(point),buttons?.7:0,0,0,0,0,0,Qt::NoModifier,type==QEvent::TabletMove?Qt::NoButton:Qt::LeftButton,buttons);QApplication::sendEvent(&canvas,&event);QVERIFY(event.isAccepted());};
        start=tool->pivotHandle();count=document->history()->count();tablet(QEvent::TabletPress,start,Qt::LeftButton);QVERIFY(tool->dragging());
        pointer(canvas,QEvent::MouseButtonRelease,start+QPointF(100,100),Qt::NoButton,Qt::MouseEventSynthesizedByQt);QVERIFY(tool->dragging());
        tablet(QEvent::TabletMove,start+QPointF(30,15),Qt::LeftButton);const auto tabletResult=canvas.project().toJson();tablet(QEvent::TabletMove,start+QPointF(100,100),Qt::NoButton);
        QVERIFY(!tool->dragging());QCOMPARE(document->history()->count(),count+1);tablet(QEvent::TabletRelease,start+QPointF(100,100),Qt::NoButton);QCOMPARE(canvas.project().toJson(),tabletResult);
    }
    void overlaysStayOutOfSourcePlaybackAndExports(){
        Canvas canvas;canvas.setProject(fixture());motion(canvas);show(canvas);auto *document=canvas.document();const auto before=canvas.project().toJson();const auto exported=renderMovieFrame(canvas.project(),10);
        for(const auto mode:{Mode::MotionPath,Mode::Pivot}){
            enable(canvas,mode);QVERIFY(!empty(overlay(canvas)));QCOMPARE(canvas.project().toJson(),before);QCOMPARE(renderMovieFrame(canvas.project(),10),exported);
            document->setKeyframeSourceEditing(true);QVERIFY(empty(overlay(canvas)));QSignalSpy blocked(&canvas,&Canvas::actionBlocked);const auto point=canvas.keyframeTransformTool()->pivotHandle();
            pointer(canvas,QEvent::MouseButtonPress,point);pointer(canvas,QEvent::MouseButtonRelease,point);QCOMPARE(blocked.size(),1);QVERIFY(!document->keyframeEditActive());
            document->setKeyframeSourceEditing(false);canvas.setAnimationPlayback(true);QVERIFY(empty(overlay(canvas)));pointer(canvas,QEvent::MouseButtonPress,point);pointer(canvas,QEvent::MouseButtonRelease,point);QVERIFY(!document->keyframeEditActive());canvas.setAnimationPlayback(false);
            QCOMPARE(canvas.project().toJson(),before);QCOMPARE(renderMovieFrame(canvas.project(),10),exported);
        }
    }
    void restrictedLayersAndZeroScalesAreBlocked(){
        for(int state=0;state<6;++state){auto project=fixture(true);auto &parent=project.shots[0].layers.first();auto &child=project.shots[0].layers.last();
            if(state==0)child.locked=true;if(state==1)parent.visible=false;if(state==2)child.positionLocked=true;if(state==3)child.annotation=true;
            Canvas canvas;canvas.setProject(project);auto *document=canvas.document();const auto id=child.id;
            if(state==4)QVERIFY(document->setKeyframeValue(parent.id,P::ScaleX,0));if(state==5)QVERIFY(document->setKeyframeValue(id,P::ScaleY,0));selectChild(canvas);show(canvas);
            auto *tool=canvas.keyframeTransformTool();tool->setMode(Mode::Pivot);QVERIFY(!tool->unavailableReason().isEmpty());const auto before=canvas.project().toJson();tool->setEnabled(true);QVERIFY(!tool->enabled());QCOMPARE(canvas.project().toJson(),before);
        }
    }
    void markerWithZeroScaleParentAtItsFrameIsBlocked(){
        Canvas canvas;canvas.setProject(fixture(true));motion(canvas,true);auto *document=canvas.document();const auto parent=canvas.shot().layers.first().id,id=canvas.shot().layers.last().id;
        QVERIFY(document->setCurrentFrame(20));QVERIFY(document->setKeyframeValue(parent,P::ScaleX,0));QVERIFY(document->setCurrentFrame(10));selectChild(canvas);show(canvas);enable(canvas,Mode::MotionPath);
        const auto point=artworkView(canvas).map(animationPivotWorld(canvas.shot(),id,20));const auto before=canvas.project().toJson();QSignalSpy blocked(&canvas,&Canvas::actionBlocked);
        drag(canvas,point,point+QPointF(40,20));QCOMPARE(blocked.size(),1);QVERIFY(!canvas.keyframeTransformTool()->dragging());QVERIFY(!document->keyframeEditActive());QCOMPARE(canvas.project().toJson(),before);
    }
    void offTimeMarkerRemainsEditableWhenCurrentParentHasZeroScale(){
        Canvas canvas;canvas.setProject(fixture(true));motion(canvas,true);auto *document=canvas.document();const auto parentId=canvas.shot().layers.first().id,id=canvas.shot().layers.last().id;
        QVERIFY(document->setKeyframeValue(parentId,P::ScaleX,0));selectChild(canvas);show(canvas);enable(canvas,Mode::MotionPath);QVERIFY(canvas.keyframeTransformTool()->unavailableReason().isEmpty());
        const auto parent=worldLayerTransform(canvas.shot(),parentId,20),view=artworkView(canvas);const auto world=animationPivotWorld(canvas.shot(),id,20);
        const auto start=view.map(world),end=view.map(parent.map(parent.inverted().map(world)+QPointF(4,5)));const int count=document->history()->count();
        drag(canvas,start,end);QCOMPARE(document->currentFrame(),10);QCOMPARE(document->history()->count(),count+1);
        QVERIFY(std::abs(evaluateProperty(canvas.shot(),id,P::PositionX,20)-49)<.0001);QVERIFY(std::abs(evaluateProperty(canvas.shot(),id,P::PositionY,20)-27)<.0001);
    }
    void draggingUnexposedTimeMarkerKeepsCurrentDrawingAndPlayhead(){
        auto project=animation_fixture::project();auto &shot=project.shots.first();const auto id=shot.animationTracks.first().drawings.first().groupLayerId;
        shot.layerAnimations={{id,{64,64},{{P::PositionX,{{0,0},{5,50}}},{P::PositionY,{{0,0},{5,20}}}}, {}}};
        Canvas canvas;canvas.setProject(project);auto *document=canvas.document();QVERIFY(document->setCurrentFrame(1));
        const int selected=animation_fixture::layerIndex(canvas.shot(),id);document->selectLayer(selected);QCOMPARE(document->activeLayer(),selected);
        QVERIFY(layerPresentAtFrame(canvas.shot(),id,1));QVERIFY(!layerPresentAtFrame(canvas.shot(),id,5));show(canvas);enable(canvas,Mode::MotionPath);
        const auto before=canvas.project().toJson();const auto tracks=canvas.shot().animationTracks;const auto layers=canvas.shot().layers;const auto otherDrawing=renderMovieFrame(canvas.project(),5);
        const auto start=artworkView(canvas).map(animationPivotWorld(canvas.shot(),id,5));const auto end=artworkView(canvas).map(canvas.artworkPoint(start)+QPointF(12,-6));
        const int count=document->history()->count();pointer(canvas,QEvent::MouseButtonPress,start);QVERIFY(canvas.keyframeTransformTool()->dragging());pointer(canvas,QEvent::MouseMove,end);
        QCOMPARE(document->currentFrame(),1);QCOMPARE(document->activeLayer(),selected);QCOMPARE(canvas.shot().animationTracks,tracks);
        pointer(canvas,QEvent::MouseButtonRelease,end);QCOMPARE(document->currentFrame(),1);QCOMPARE(document->activeLayer(),selected);QCOMPARE(document->history()->count(),count+1);
        QVERIFY(std::abs(evaluateProperty(canvas.shot(),id,P::PositionX,5)-62)<.0001);QVERIFY(std::abs(evaluateProperty(canvas.shot(),id,P::PositionY,5)-14)<.0001);
        for(int i=0;i<layers.size();++i)QCOMPARE(canvas.shot().layers[i].tiles,layers[i].tiles);
        QCOMPARE(renderMovieFrame(canvas.project(),5),otherDrawing);document->history()->undo();QCOMPARE(canvas.project().toJson(),before);QCOMPARE(document->currentFrame(),1);QCOMPARE(document->activeLayer(),selected);
    }
    void renderPreview(){
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");if(output.isEmpty())QSKIP("Opt-in motion canvas previews.");
        auto project=fixture();auto &shot=project.shots.first();const auto id=shot.layers.first().id;
        shot.layerAnimations={{id,{64,48},{
            {P::PositionX,{{0,0},{8,56,KeyframeInterpolation::EaseInOut},{16,118},{25,34},{30,9}}},
            {P::PositionY,{{0,0},{8,-12},{16,56,KeyframeInterpolation::EaseOut},{25,74},{30,42}}},
            {P::Rotation,{{0,0},{30,45}}}
        },{}}};
        Canvas canvas;canvas.setProject(project);QVERIFY(canvas.document()->setCurrentFrame(12));show(canvas);canvas.setRotation(0);canvas.setZoom(2.1);
        canvas.centerOn(sheetGeometry(canvas.project(),canvas.shot()).output.center());enable(canvas,Mode::MotionPath);
        const auto marker=artworkView(canvas).map(animationPivotWorld(canvas.shot(),id,16));drag(canvas,marker,marker);QCoreApplication::processEvents();
        QDir().mkpath(output);QVERIFY(canvas.grab().save(output+"/motion-path-canvas.png"));
        canvas.keyframeTransformTool()->setMode(Mode::Pivot);QCoreApplication::processEvents();QVERIFY(canvas.grab().save(output+"/motion-pivot-canvas.png"));
    }
};
QTEST_MAIN(MotionCanvasTests)
#include "motion_canvas_tests.moc"
