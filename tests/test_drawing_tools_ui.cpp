#include "canvas.h"
#include "drawing_tool_controller.h"
#include "drawing_tool_panel.h"
#include "object_tools.h"
#include "layer_panel.h"
#include "ui_theme.h"
#include "vector_edit_geometry.h"
#include <QApplication>
#include <QDir>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QPainter>
#include <QTabletEvent>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>
using namespace toon;
namespace {
Project project() { auto p=Project::create();p.width=640;p.height=480;p.transparent=true;p.shots[0].sheet.overscan=0;return p; }
void setup(Canvas &canvas) { canvas.resize(1000,800);canvas.setProject(project());canvas.show();canvas.setZoom(1);QApplication::processEvents(); }
QPointF view(Canvas &canvas,QPointF p){return canvas.viewTransform().map(p+sheetGeometry(canvas.project(),canvas.shot()).output.topLeft());}
void mouse(Canvas &canvas,QEvent::Type type,QPointF p,Qt::KeyboardModifiers mods=Qt::NoModifier,Qt::MouseButtons buttons=Qt::LeftButton){
    const auto v=view(canvas,p);QMouseEvent event(type,v,v,canvas.mapToGlobal(v),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:buttons,mods);QApplication::sendEvent(&canvas,&event);
}
void click(Canvas &canvas,QPointF p,Qt::KeyboardModifiers mods=Qt::NoModifier){mouse(canvas,QEvent::MouseButtonPress,p,mods);mouse(canvas,QEvent::MouseButtonRelease,p,mods);}
void drag(Canvas &canvas,QPointF a,QPointF b,Qt::KeyboardModifiers mods=Qt::NoModifier){mouse(canvas,QEvent::MouseButtonPress,a,mods);mouse(canvas,QEvent::MouseMove,b,mods);mouse(canvas,QEvent::MouseButtonRelease,b,mods);}
const RasterLayer &active(Canvas &canvas){return canvas.shot().layers[canvas.document()->activeLayer()];}
bool near(QPointF a,QPointF b){return QLineF(a,b).length()<.001;}
}
class DrawingToolsUiTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void encloseFillCommitsOnReleaseAndCancelsInterruptedLoops_data(){
        QTest::addColumn<bool>("pen");QTest::addColumn<int>("end");QTest::addColumn<bool>("mirrored");
        for(bool pen:{false,true})for(int end:{0,1,2,3})QTest::newRow(qPrintable(QString("%1-%2").arg(pen?"pen":"mouse").arg(end)))<<pen<<end<<(!pen&&end==0);
    }
    void encloseFillCommitsOnReleaseAndCancelsInterruptedLoops(){
        QFETCH(bool,pen);QFETCH(int,end);QFETCH(bool,mirrored);
        auto p=project();p.width=320;p.height=240;p.shots[0].layers[0].fillReference=true;
        QImage lines(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);lines.fill(Qt::transparent);
        {QPainter painter(&lines);painter.setPen(QPen(Qt::black,3));painter.drawRect(QRectF(70,70,40,40));painter.drawRect(QRectF(150,70,40,40));}
        p.shots[0].layers[0].tiles.insert({0,0},lines);p.shots[0].layers.append(RasterLayer::create("Flats"));
        Canvas canvas;canvas.resize(800,600);canvas.setProject(p);canvas.document()->selectLayer(1);canvas.show();canvas.setZoom(1);canvas.setRotation(13);canvas.setCanvasMirrored(mirrored);QApplication::processEvents();
        auto*c=canvas.drawingTools();canvas.setTool(Canvas::Tool::Bucket);c->encloseFillMode=true;c->fillSettings.source=RasterSampleSource::MarkedReferences;canvas.setBrushColor(Qt::red);
        const auto before=renderMovieFrame(canvas.project());const auto reference=canvas.shot().layers[0].tiles;
        QPointingDevice device("Enclose pen",232,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,3);
        const auto send=[&](QEvent::Type type,QPointF point){if(!pen){mouse(canvas,type,point);return;}const auto pos=view(canvas,point);const bool release=type==QEvent::MouseButtonRelease;const bool press=type==QEvent::MouseButtonPress;QTabletEvent event(release?QEvent::TabletRelease:press?QEvent::TabletPress:QEvent::TabletMove,&device,pos,canvas.mapToGlobal(pos),release?0:.8,0,0,0,0,0,Qt::NoModifier,press||release?Qt::LeftButton:Qt::NoButton,release?Qt::NoButton:Qt::LeftButton);QApplication::sendEvent(&canvas,&event);};
        send(QEvent::MouseButtonPress,{50,50});for(const QPointF point:{QPointF(210,50),QPointF(210,130),QPointF(50,130),QPointF(50,50)})send(QEvent::MouseMove,point);
        QVERIFY(c->dragging());QCOMPARE(renderMovieFrame(canvas.project()),before);QCOMPARE(canvas.document()->history()->count(),0);
        if(end==1)QTest::keyClick(&canvas,Qt::Key_Escape);
        else if(end==2){QFocusEvent event(QEvent::FocusOut,Qt::OtherFocusReason);QApplication::sendEvent(&canvas,&event);}
        else if(end==3){
            if(pen){const auto pos=view(canvas,{50,50});QTabletEvent event(QEvent::TabletMove,&device,pos,canvas.mapToGlobal(pos),0,0,0,0,0,0,Qt::NoModifier,Qt::NoButton,Qt::NoButton);QApplication::sendEvent(&canvas,&event);}
            else mouse(canvas,QEvent::MouseMove,{50,50},Qt::NoModifier,Qt::NoButton);
        }
        send(QEvent::MouseButtonRelease,{50,50});QVERIFY(!c->active());QCOMPARE(canvas.shot().layers[0].tiles,reference);
        if(end){QCOMPARE(renderMovieFrame(canvas.project()),before);QCOMPARE(canvas.document()->history()->count(),0);return;}
        QCOMPARE(canvas.document()->history()->count(),1);const auto after=renderMovieFrame(canvas.project());QCOMPARE(after.pixelColor(90,90),QColor(Qt::red));QCOMPARE(after.pixelColor(170,90),QColor(Qt::red));QCOMPARE(after.pixelColor(30,90).alpha(),0);
        canvas.document()->history()->undo();QCOMPARE(renderMovieFrame(canvas.project()),before);canvas.document()->history()->redo();QCOMPARE(renderMovieFrame(canvas.project()),after);
    }
    void vectorIntersectionModeErasesOnlyTheTouchedInterval(){
        Canvas canvas;setup(canvas);auto*doc=canvas.document();QVERIFY(ObjectTools::addLayer(doc,LayerKind::Vector));
        const auto line=ObjectTools::addPath(doc,{BezierNode::corner({50,100}),BezierNode::corner({250,100})},false,Qt::black,3);QVERIFY(!line.isEmpty());
        const auto first=ObjectTools::addPath(doc,{BezierNode::corner({100,50}),BezierNode::corner({100,150})},false,Qt::black,3);QVERIFY(!first.isEmpty());
        const auto second=ObjectTools::addPath(doc,{BezierNode::corner({200,50}),BezierNode::corner({200,150})},false,Qt::black,3);QVERIFY(!second.isEmpty());
        const auto before=active(canvas).objects;const auto count=doc->history()->count();DrawingToolPanel panel(&canvas);canvas.setTool(Canvas::Tool::VectorEraser);
        panel.findChild<QTabBar*>("vectorEraseMode")->setCurrentIndex(2);canvas.drawingTools()->retouchSettings.size=6;
        drag(canvas,{145,100},{155,100});QCOMPARE(active(canvas).objects.size(),4);QVERIFY(ObjectTools::object(active(canvas),first));QVERIFY(ObjectTools::object(active(canvas),second));QCOMPARE(doc->history()->count(),count+1);
        doc->history()->undo();QCOMPARE(active(canvas).objects,before);doc->history()->redo();QCOMPARE(active(canvas).objects.size(),4);
    }
    void retouchPixelsPersistAfterContact_data(){
        QTest::addColumn<int>("mode");QTest::addColumn<int>("input");
        const QStringList inputs={"mouse","pen-release","pen-hover-before-release","mouse-missing-release","pen-barrel-release"};
        for(int mode=0;mode<=int(RetouchMode::Burn);++mode)
            for(int input=0;input<inputs.size();++input)
                QTest::newRow(qPrintable(QString("mode-%1-%2").arg(mode).arg(inputs[input])))<<mode<<input;
    }
    void retouchPixelsPersistAfterContact(){
        QFETCH(int,mode);QFETCH(int,input);
        auto p=project();p.width=p.height=128;
        QImage base(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);base.fill(QColor("#345678"));
        p.shots[0].layers[0].tiles.insert({0,0},base);
        auto layer=RasterLayer::create("Retouch target");
        QImage image(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
        {QPainter painter(&image);painter.fillRect(0,0,64,128,QColor(120,100,80));painter.fillRect(64,0,64,128,QColor(180,150,120));}
        layer.tiles.insert({0,0},image);p.shots[0].layers.append(layer);
        Canvas canvas;canvas.resize(640,480);canvas.setProject(p);canvas.document()->selectLayer(1);canvas.setZoom(1);
        QPainterPath selection;selection.addRect(0,0,80,128);canvas.document()->setRasterSelection(selection);
        canvas.setBrushColor(Qt::red);canvas.setTool(Canvas::Tool::Retouch);
        auto *controller=canvas.drawingTools();controller->retouchSettings.mode=RetouchMode(mode);
        controller->retouchSettings.size=40;controller->retouchSettings.strength=.8;controller->notifySettings();
        const auto before=renderMovieFrame(canvas.project());const auto source=active(canvas).tiles;
        QPointingDevice pen("Retouch persistence pen",181,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,3);
        const auto send=[&](QEvent::Type type,QPointF point,qreal pressure,Qt::MouseButton button,Qt::MouseButtons buttons){
            const auto position=view(canvas,point);QTabletEvent event(type,&pen,position,canvas.mapToGlobal(position),pressure,0,0,0,0,0,Qt::NoModifier,button,buttons);
            QApplication::sendEvent(&canvas,&event);
        };
        const bool mouseInput=input==0||input==3;
        if(mouseInput){mouse(canvas,QEvent::MouseButtonPress,{55,64});mouse(canvas,QEvent::MouseMove,{75,64});}
        else {send(QEvent::TabletPress,{55,64},.8,Qt::LeftButton,Qt::LeftButton);send(QEvent::TabletMove,{75,64},.8,Qt::NoButton,Qt::LeftButton);}
        QVERIFY(controller->dragging());QVERIFY(canvas.document()->strokeActive());QCOMPARE(canvas.document()->history()->count(),0);
        QVERIFY(active(canvas).tiles!=source);
        if(mouseInput){
            if(input==3)mouse(canvas,QEvent::MouseMove,{110,110},Qt::NoModifier,Qt::NoButton);
            mouse(canvas,QEvent::MouseButtonRelease,{75,64});
        }
        else {
            if(input==4){
                send(QEvent::TabletPress,{75,64},.8,Qt::RightButton,Qt::LeftButton|Qt::RightButton);
                send(QEvent::TabletRelease,{75,64},.8,Qt::RightButton,Qt::LeftButton);
                QVERIFY(canvas.document()->strokeActive());QCOMPARE(canvas.document()->history()->count(),0);
            }
            if(input==2)send(QEvent::TabletMove,{110,110},0,Qt::NoButton,Qt::NoButton);
            send(QEvent::TabletRelease,input==2?QPointF(110,110):QPointF(75,64),0,Qt::LeftButton,Qt::NoButton);
        }
        QVERIFY(!controller->dragging());QVERIFY(!canvas.document()->strokeActive());
        QCOMPARE(canvas.document()->history()->count(),1);const auto after=renderMovieFrame(canvas.project());QVERIFY(after!=before);
        QCOMPARE(canvas.shot().layers[0].tiles,p.shots[0].layers[0].tiles);
        QCOMPARE(active(canvas).tiles.value({0,0}).pixelColor(90,64),image.pixelColor(90,64));
        canvas.document()->history()->undo();QCOMPARE(renderMovieFrame(canvas.project()),before);
        canvas.document()->history()->redo();QCOMPARE(renderMovieFrame(canvas.project()),after);
        QTemporaryDir directory;QString error;const auto path=directory.filePath("retouched.toon");
        QVERIFY2(saveProject(canvas.project(),path,&error),qPrintable(error));const auto loaded=loadProject(path,&error);
        QVERIFY2(loaded.has_value(),qPrintable(error));QCOMPARE(renderMovieFrame(*loaded),after);
        // Changing the next gesture's values must not change cancellation or
        // merge that gesture into the completed stroke's history entry.
        controller->retouchSettings.size=24;controller->retouchSettings.strength=.6;controller->notifySettings();
        if(mouseInput){mouse(canvas,QEvent::MouseButtonPress,{55,64});mouse(canvas,QEvent::MouseMove,{75,64});}
        else {send(QEvent::TabletPress,{55,64},.7,Qt::LeftButton,Qt::LeftButton);send(QEvent::TabletMove,{75,64},.7,Qt::NoButton,Qt::LeftButton);}
        QTest::keyClick(&canvas,Qt::Key_Escape);
        QCOMPARE(renderMovieFrame(canvas.project()),after);QCOMPARE(canvas.document()->history()->count(),1);
        if(!mouseInput)send(QEvent::TabletRelease,{75,64},0,Qt::LeftButton,Qt::NoButton);
        QCOMPARE(renderMovieFrame(canvas.project()),after);
    }
    void rasterShapeAndGradientUseSingleUndoPerGesture(){
        Canvas canvas;setup(canvas);auto *tools=canvas.drawingTools();tools->shapeFill=true;tools->fillColor=Qt::red;tools->shapeStroke=false;
        canvas.setTool(Canvas::Tool::Rectangle);drag(canvas,{60,70},{160,170});
        QCOMPARE(renderMovieFrame(canvas.project()).pixelColor(100,100),QColor(Qt::red));QCOMPARE(canvas.document()->history()->count(),1);
        QVERIFY(!canvas.document()->strokeActive());canvas.document()->history()->undo();QVERIFY(active(canvas).tiles.isEmpty());canvas.document()->history()->redo();
        canvas.setTool(Canvas::Tool::EllipseSelect);drag(canvas,{60,70},{160,170});QVERIFY(canvas.document()->rasterSelection().path.contains({110,120}));QVERIFY(!canvas.document()->rasterSelection().path.contains({62,72}));
        tools->gradientStops={{0,Qt::black},{1,Qt::white}};canvas.setTool(Canvas::Tool::Gradient);drag(canvas,{60,120},{160,120});
        const auto image=renderMovieFrame(canvas.project());QVERIFY(image.pixelColor(80,120).red()<image.pixelColor(140,120).red());QCOMPARE(image.pixelColor(62,72),QColor(Qt::red));
        QCOMPARE(canvas.document()->history()->count(),2);
    }
    void polygonSelectionUsesClickedPointsAndFirstModifier(){
        Canvas canvas;setup(canvas);QPainterPath base;base.addRect(10,10,20,20);canvas.document()->setRasterSelection(base);
        canvas.setTool(Canvas::Tool::PolygonSelect);click(canvas,{100,100},Qt::ShiftModifier);click(canvas,{200,100});click(canvas,{150,200});
        mouse(canvas,QEvent::MouseMove,{450,350},Qt::NoModifier,Qt::NoButton);QTest::keyClick(&canvas,Qt::Key_Return);
        const auto selection=canvas.document()->rasterSelection().path;QVERIFY(selection.contains({15,15}));QVERIFY(selection.contains({150,130}));QVERIFY(!selection.contains({300,250}));
        QVERIFY(active(canvas).tiles.isEmpty());QVERIFY(canvas.document()->history()->isClean());
    }
    void penCreatesEditableBezierAndNodeControlsDragOnce(){
        Canvas canvas;setup(canvas);canvas.setTool(Canvas::Tool::VectorPen);
        drag(canvas,{80,180},{120,80});drag(canvas,{280,180},{320,230});QTest::keyClick(&canvas,Qt::Key_Return);
        QCOMPARE(active(canvas).kind,LayerKind::Vector);QCOMPARE(active(canvas).objects.size(),1);QCOMPARE(active(canvas).objects.first().nodes.size(),2);
        const auto id=active(canvas).objects.first().id;QVERIFY(near(active(canvas).objects.first().nodes.first().outControl,{120,80}));
        canvas.setTool(Canvas::Tool::VectorNode);canvas.drawingTools()->setPathEditMode(DrawingToolController::PathEditMode::Nodes);const int count=canvas.document()->history()->count();
        drag(canvas,{120,80},{150,60});QCOMPARE(canvas.document()->history()->count(),count+1);
        QVERIFY(near(active(canvas).objects.first().nodes.first().outControl,{150,60}));
        canvas.document()->history()->undo();QVERIFY(near(active(canvas).objects.first().nodes.first().outControl,{120,80}));
        QCOMPARE(active(canvas).objects.first().id,id);
    }
    void vectorNodeMovesTransformedObjectInArtworkCoordinates(){
        Canvas canvas;setup(canvas);ObjectTools::addLayer(canvas.document(),LayerKind::Vector);
        const auto id=ObjectTools::addPath(canvas.document(),{BezierNode::corner({50,50}),BezierNode::corner({150,50})},false,Qt::black,8);
        auto object=*ObjectTools::object(active(canvas),id);object.transform.translate(80,90);object.transform.rotate(30);ObjectTools::updateObject(canvas.document(),object);
        canvas.setTool(Canvas::Tool::VectorNode);const auto center=object.transform.map(QPointF(100,50));
        drag(canvas,center,center+QPointF(40,30));
        const auto after=active(canvas).objects.first();QVERIFY(near(after.transform.map(QPointF(100,50)),center+QPointF(40,30)));
        QVERIFY(near(after.nodes.first().anchor,{50,50}));
    }
    void cornerCanBecomeCurveWithAltDrag(){
        Canvas canvas;setup(canvas);canvas.setTool(Canvas::Tool::VectorPen);click(canvas,{70,70});click(canvas,{200,70});QTest::keyClick(&canvas,Qt::Key_Return);
        canvas.setTool(Canvas::Tool::VectorNode);canvas.drawingTools()->setPathEditMode(DrawingToolController::PathEditMode::Nodes);drag(canvas,{70,70},{100,110},Qt::AltModifier|Qt::ShiftModifier);
        const auto node=active(canvas).objects.first().nodes.first();QVERIFY(near(node.anchor,{70,70}));QVERIFY(near(node.outControl,{100,110}));QVERIFY(near(node.inControl,{40,30}));
    }
    void vectorRectangleHasFourNodesAndStrokeStyle(){
        Canvas canvas;setup(canvas);ObjectTools::addLayer(canvas.document(),LayerKind::Vector);auto *tools=canvas.drawingTools();tools->shapeFill=true;tools->fillColor=Qt::green;tools->strokeWidth=5;
        canvas.setTool(Canvas::Tool::Rectangle);drag(canvas,{50,50},{150,150});const auto object=active(canvas).objects.first();
        QCOMPARE(object.nodes.size(),4);QVERIFY(object.closed);QCOMPARE(object.strokeWidth,5.0);QCOMPARE(renderMovieFrame(canvas.project()).pixelColor(100,100),QColor(Qt::green));
        canvas.setTool(Canvas::Tool::VectorNode);tools->fillColor=Qt::blue;tools->applyObjectStyle();QCOMPARE(renderMovieFrame(canvas.project()).pixelColor(100,100),QColor(Qt::blue));
    }
    void vectorEllipseRetainsClosedCubicTangents(){
        Canvas canvas;setup(canvas);ObjectTools::addLayer(canvas.document(),LayerKind::Vector);canvas.setTool(Canvas::Tool::Ellipse);drag(canvas,{40,40},{240,140});
        const auto object=active(canvas).objects.first();QCOMPARE(object.nodes.size(),4);QVERIFY(object.closed);
        QVERIFY(object.nodes.first().inControl!=object.nodes.first().anchor);QVERIFY(object.nodes.first().outControl!=object.nodes.first().anchor);
        const auto bounds=editableObjectPath(object).boundingRect();QVERIFY(qAbs(bounds.width()-200)<.01);QVERIFY(qAbs(bounds.height()-100)<.01);
    }
    void vectorBrushAndSingleClickEraserNeverTouchRasterSource(){
        Canvas canvas;setup(canvas);ObjectTools::addLayer(canvas.document(),LayerKind::Vector);canvas.brush.size=10;canvas.setTool(Canvas::Tool::Brush);
        drag(canvas,{70,100},{250,100});QCOMPARE(active(canvas).objects.size(),1);QCOMPARE(active(canvas).objects.first().strokeWidth,10.0);
        QVERIFY(canvas.shot().layers.first().tiles.isEmpty());QVERIFY(!canvas.document()->strokeActive());
        canvas.setTool(Canvas::Tool::VectorEraser);click(canvas,{150,100});QVERIFY(active(canvas).objects.isEmpty());
        canvas.document()->history()->undo();QCOMPARE(active(canvas).objects.size(),1);
    }
    void textCreationEditingAndMovementKeepsText(){
        Canvas canvas;setup(canvas);canvas.drawingTools()->textStyle.text="Title";canvas.drawingTools()->textStyle.fontSize=40;
        canvas.setTool(Canvas::Tool::Text);click(canvas,{80,100});QCOMPARE(active(canvas).kind,LayerKind::Text);QCOMPARE(active(canvas).objects.first().text,QString("Title"));
        auto *tools=canvas.drawingTools();tools->textStyle.text="Updated title";tools->textStyle.bold=true;tools->applyObjectStyle();QCOMPARE(active(canvas).objects.first().text,QString("Updated title"));
        const auto before=editableObjectBounds(active(canvas).objects.first());
        // Hit a glyph contour rather than blank space between characters.
        const auto path=editableObjectPath(active(canvas).objects.first());QPointF point=path.pointAtPercent(.01);
        drag(canvas,point,point+QPointF(30,20));const auto after=editableObjectBounds(active(canvas).objects.first());
        QVERIFY(near(after.topLeft(),before.topLeft()+QPointF(30,20)));QCOMPARE(active(canvas).objects.first().text,QString("Updated title"));
    }
    void nodeEscapeAndLostReleaseRestoreOriginalArtwork(){
        Canvas canvas;setup(canvas);canvas.setTool(Canvas::Tool::VectorPen);click(canvas,{80,100});click(canvas,{250,100});QTest::keyClick(&canvas,Qt::Key_Return);canvas.setTool(Canvas::Tool::VectorNode);canvas.drawingTools()->setPathEditMode(DrawingToolController::PathEditMode::Nodes);
        const auto before=canvas.project().toJson();const int count=canvas.document()->history()->count();
        mouse(canvas,QEvent::MouseButtonPress,{80,100});mouse(canvas,QEvent::MouseMove,{100,180});QVERIFY(canvas.document()->externalEditActive());QTest::keyClick(&canvas,Qt::Key_Escape);
        QCOMPARE(canvas.project().toJson(),before);QVERIFY(!canvas.document()->externalEditActive());
        mouse(canvas,QEvent::MouseButtonPress,{80,100});mouse(canvas,QEvent::MouseMove,{150,200});mouse(canvas,QEvent::MouseMove,{170,200},Qt::NoModifier,Qt::NoButton);
        QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),count);QVERIFY(!canvas.document()->externalEditActive());
    }
    void advancedTransformHandlesPreviewAndUndo(){
        Canvas canvas;setup(canvas);canvas.drawingTools()->shapeFill=true;canvas.drawingTools()->fillColor=Qt::red;canvas.setTool(Canvas::Tool::Rectangle);drag(canvas,{50,60},{150,160});
        const auto before=renderMovieFrame(canvas.project());auto *tools=canvas.drawingTools();QVERIFY(tools->beginAdvanced());const auto quad=tools->transform()->quad();
        drag(canvas,quad[2],quad[2]+QPointF(40,30));QVERIFY(tools->transform()->active());QVERIFY(tools->transform()->matrix()!=QTransform());
        QTest::keyClick(&canvas,Qt::Key_Return);QVERIFY(!tools->transform()->active());QVERIFY(renderMovieFrame(canvas.project())!=before);
        canvas.document()->history()->undo();QCOMPARE(renderMovieFrame(canvas.project()),before);
    }
    void canceledShapesAndIncompleteCurvesProduceNoPixels(){
        Canvas canvas;setup(canvas);for(auto tool:{Canvas::Tool::Line,Canvas::Tool::Rectangle,Canvas::Tool::Ellipse,Canvas::Tool::Gradient,Canvas::Tool::EllipseSelect}){
            canvas.setTool(tool);mouse(canvas,QEvent::MouseButtonPress,{40,40});mouse(canvas,QEvent::MouseMove,{140,140});QTest::keyClick(&canvas,Qt::Key_Escape);mouse(canvas,QEvent::MouseButtonRelease,{140,140});
            QVERIFY(active(canvas).tiles.isEmpty());QVERIFY(!canvas.document()->rasterSelection().active);
        }
        canvas.setTool(Canvas::Tool::Curve);click(canvas,{40,40});click(canvas,{100,100});QTest::keyClick(&canvas,Qt::Key_Return);QVERIFY(active(canvas).tiles.isEmpty());QVERIFY(canvas.document()->history()->isClean());
    }
    void changingLayersCancelsPendingPenOrPolygon(){
        Canvas canvas;setup(canvas);canvas.document()->addLayer();canvas.setTool(Canvas::Tool::VectorPen);click(canvas,{60,60});click(canvas,{150,150});QVERIFY(canvas.drawingTools()->active());
        canvas.document()->selectLayer(0);QVERIFY(!canvas.drawingTools()->active());QTest::keyClick(&canvas,Qt::Key_Return);QCOMPARE(canvas.shot().layers.size(),2);QVERIFY(active(canvas).tiles.isEmpty());
        canvas.setTool(Canvas::Tool::PolygonSelect);click(canvas,{80,80});click(canvas,{200,80});click(canvas,{150,200});canvas.document()->selectLayer(1);
        QVERIFY(!canvas.drawingTools()->active());QTest::keyClick(&canvas,Qt::Key_Return);QVERIFY(!canvas.document()->rasterSelection().active);
    }
    void vectorBoundsScaleRotateAndCancelWithoutChangingNodes(){
        Canvas canvas;setup(canvas);ObjectTools::addLayer(canvas.document(),LayerKind::Vector);
        const auto id=ObjectTools::addPath(canvas.document(),{BezierNode::corner({80,100}),BezierNode::corner({240,100}),BezierNode::corner({240,200})},false,Qt::black,10);
        canvas.setTool(Canvas::Tool::VectorNode);click(canvas,{160,100});auto object=active(canvas).objects.first();
        const auto originalNodes=object.nodes;const auto handles=vectorTransformHandles(object);const int count=canvas.document()->history()->count();
        drag(canvas,handles[4],handles[4]+QPointF(100,70));QCOMPARE(canvas.document()->history()->count(),count+1);
        QVERIFY(!active(canvas).objects.first().transform.isIdentity());QCOMPARE(active(canvas).objects.first().nodes.first().anchor,originalNodes.first().anchor);
        canvas.document()->history()->undo();object=active(canvas).objects.first();
        const auto before=canvas.project().toJson();mouse(canvas,QEvent::MouseButtonPress,handles[4]);mouse(canvas,QEvent::MouseMove,handles[4]+QPointF(50,50));QTest::keyClick(&canvas,Qt::Key_Escape);QCOMPARE(canvas.project().toJson(),before);
        const auto top=handles[1],bottom=handles[5];const auto rotation=top+(top-bottom)*(26/QLineF(top,bottom).length());
        drag(canvas,rotation,rotation+QPointF(60,40));QVERIFY(qAbs(active(canvas).objects.first().transform.m12())>.01);
        QCOMPARE(active(canvas).objects.first().id,id);
    }
    void vectorWidthHandlesAndBrushKeepEditableObjectsAndUndo(){
        Canvas canvas;setup(canvas);ObjectTools::addLayer(canvas.document(),LayerKind::Vector);
        ObjectTools::addPath(canvas.document(),{BezierNode::corner({60,180}),BezierNode::corner({300,180})},false,Qt::black,20);
        canvas.setTool(Canvas::Tool::VectorNode);click(canvas,{170,180});auto *tools=canvas.drawingTools();tools->setPathEditMode(DrawingToolController::PathEditMode::Width);
        const int count=canvas.document()->history()->count();drag(canvas,{180,180},{180,150});
        QCOMPARE(active(canvas).objects.first().nodes.size(),3);QVERIFY(active(canvas).objects.first().widthFactors[1]>2.9);QCOMPARE(canvas.document()->history()->count(),count+1);
        canvas.document()->history()->undo();QCOMPARE(active(canvas).objects.first().nodes.size(),2);
        canvas.setTool(Canvas::Tool::VectorWidth);tools->widthBrushRadius=25;tools->widthBrushStrength=.4;tools->widthThicken=true;
        click(canvas,{180,180});QVERIFY(active(canvas).objects.first().nodes.size()>2);qreal maximum=1;for(auto width:active(canvas).objects.first().widthFactors)maximum=qMax(maximum,width);QVERIFY(maximum>1.2);
        QCOMPARE(active(canvas).objects.first().widthFactors.first(),1.);QCOMPARE(active(canvas).objects.first().widthFactors.last(),1.);
        const auto thick=active(canvas).objects.first().widthFactors;tools->widthThicken=false;click(canvas,{180,180});
        qreal thinMaximum=1;for(auto width:active(canvas).objects.first().widthFactors)thinMaximum=qMax(thinMaximum,width);QVERIFY(thinMaximum<maximum);
        QCOMPARE(active(canvas).kind,LayerKind::Vector);canvas.document()->history()->undo();QCOMPARE(active(canvas).objects.first().widthFactors,thick);
    }
    void vectorSimplifyBrushOnlyTouchesLocalRegionAndHonorsLocks(){
        Canvas canvas;setup(canvas);ObjectTools::addLayer(canvas.document(),LayerKind::Vector);QVector<BezierNode> nodes;
        for(int i=0;i<31;++i)nodes.append(BezierNode::corner({qreal(50+i*10),180+qreal(i%2)*.4}));
        ObjectTools::addPath(canvas.document(),nodes,false,Qt::black,8);canvas.setTool(Canvas::Tool::VectorSimplify);auto *tools=canvas.drawingTools();tools->widthBrushRadius=45;tools->simplifyTolerance=2;
        const int count=canvas.document()->history()->count();click(canvas,{200,180});QVERIFY(active(canvas).objects.first().nodes.size()<nodes.size());QVERIFY(active(canvas).objects.first().nodes.size()>10);
        QCOMPARE(active(canvas).objects.first().nodes.first().anchor,nodes.first().anchor);QCOMPARE(active(canvas).objects.first().nodes.last().anchor,nodes.last().anchor);QCOMPARE(canvas.document()->history()->count(),count+1);
        canvas.document()->history()->undo();QCOMPARE(active(canvas).objects.first().nodes.size(),nodes.size());
        canvas.document()->setLayerPositionLocked(true);const auto locked=canvas.project().toJson();click(canvas,{200,180});QCOMPARE(canvas.project().toJson(),locked);
        canvas.document()->setLayerPositionLocked(false);mouse(canvas,QEvent::MouseButtonPress,{200,180});mouse(canvas,QEvent::MouseMove,{220,180});QTest::keyClick(&canvas,Qt::Key_Escape);QCOMPARE(active(canvas).objects.first().nodes.size(),nodes.size());
    }
    void vectorStylePreviewCommitsOnceAndWidthBrushCancels(){
        Canvas canvas;setup(canvas);ObjectTools::addLayer(canvas.document(),LayerKind::Vector);
        ObjectTools::addPath(canvas.document(),{BezierNode::corner({60,180}),BezierNode::corner({300,180})},false,Qt::black,12);
        canvas.setTool(Canvas::Tool::VectorNode);click(canvas,{170,180});auto *tools=canvas.drawingTools();const int count=canvas.document()->history()->count();
        QVERIFY(tools->beginStyleEdit());for(int i=13;i<=24;++i){tools->strokeWidth=i;tools->objectOpacity=.5;tools->previewObjectStyle();}
        QCOMPARE(canvas.document()->history()->count(),count);tools->finishStyleEdit();QCOMPARE(canvas.document()->history()->count(),count+1);QCOMPARE(active(canvas).objects.first().opacity,.5);
        canvas.document()->history()->undo();QCOMPARE(tools->strokeWidth,12.);QCOMPARE(tools->objectOpacity,1.);
        canvas.setTool(Canvas::Tool::VectorWidth);const auto before=canvas.project().toJson();mouse(canvas,QEvent::MouseButtonPress,{160,180});mouse(canvas,QEvent::MouseMove,{190,180});QTest::keyClick(&canvas,Qt::Key_Escape);QCOMPARE(canvas.project().toJson(),before);QVERIFY(!canvas.document()->externalEditActive());
    }
    void scalarPathStyleDoesNotApplyAnUnrelatedForegroundColor(){
        Canvas canvas;setup(canvas);ObjectTools::addLayer(canvas.document(),LayerKind::Vector);
        ObjectTools::addPath(canvas.document(),{BezierNode::corner({60,180}),BezierNode::corner({300,180})},false,Qt::blue,12);
        canvas.setTool(Canvas::Tool::VectorNode);click(canvas,{170,180});auto *tools=canvas.drawingTools();
        canvas.setBrushColor(Qt::red);tools->applyObjectStyle();QCOMPARE(active(canvas).objects.first().stroke,QColor(Qt::red));canvas.document()->history()->undo();
        QCOMPARE(active(canvas).objects.first().stroke,QColor(Qt::blue));canvas.setBrushColor(Qt::green);
        QVERIFY(tools->beginStyleEdit());tools->strokeWidth=20;tools->objectOpacity=.6;tools->previewObjectStyle();tools->finishStyleEdit();
        QCOMPARE(active(canvas).objects.first().stroke,QColor(Qt::blue));QCOMPARE(active(canvas).objects.first().strokeWidth,20.);QCOMPARE(active(canvas).objects.first().opacity,.6);
    }
    void returningToPathEditPreservesTheSelectedObjectsOpacity(){
        Canvas canvas;setup(canvas);ObjectTools::addLayer(canvas.document(),LayerKind::Vector);
        const auto id=ObjectTools::addPath(canvas.document(),{BezierNode::corner({60,180}),BezierNode::corner({300,180})},false,Qt::blue,12);
        auto object=active(canvas).objects.first();object.opacity=.4;QVERIFY(ObjectTools::updateObject(canvas.document(),object));
        canvas.setTool(Canvas::Tool::VectorNode);click(canvas,{170,180});auto *tools=canvas.drawingTools();canvas.setTool(Canvas::Tool::Brush);canvas.setTool(Canvas::Tool::VectorNode);
        // ToolSession can restore this tool's saved defaults while retaining its selection.
        tools->strokeWidth=3;tools->objectOpacity=1;QCOMPARE(tools->selectedObjectId(),id);QVERIFY(tools->beginStyleEdit());tools->strokeWidth=24;tools->previewObjectStyle();tools->finishStyleEdit();
        QCOMPARE(active(canvas).objects.first().opacity,.4);QCOMPARE(active(canvas).objects.first().strokeWidth,24.);
    }
    void simplifyingAfterSelectingALateNodeKeepsDeleteSafe(){
        Canvas canvas;setup(canvas);ObjectTools::addLayer(canvas.document(),LayerKind::Vector);QVector<BezierNode> nodes;
        for(int i=0;i<25;++i)nodes.append(BezierNode::corner({qreal(50+i*10),180+qreal(i%2)*.2}));
        ObjectTools::addPath(canvas.document(),nodes,false,Qt::black,8);canvas.setTool(Canvas::Tool::VectorNode);click(canvas,{160,180});auto *tools=canvas.drawingTools();tools->setPathEditMode(DrawingToolController::PathEditMode::Nodes);
        click(canvas,nodes.last().anchor);QVERIFY(tools->simplifySelected());QCOMPARE(active(canvas).objects.first().nodes.size(),2);
        QTest::keyClick(&canvas,Qt::Key_Delete);QVERIFY(active(canvas).objects.isEmpty());canvas.document()->history()->undo();QCOMPARE(active(canvas).objects.first().nodes.size(),2);
    }
    void symmetryBrushCopiesCanBeSelectedAndSegmentErased(){
        Canvas canvas;setup(canvas);ObjectTools::addLayer(canvas.document(),LayerKind::Vector);auto *document=canvas.document();
        BrushSettings brush;brush.size=12;brush.pressureSize=false;brush.symmetry.mode=SymmetryMode::Vertical;brush.symmetry.center={.5,.5};
        QVERIFY(document->beginStroke({80,100},1,brush,false));for(int y=110;y<=280;y+=10)document->continueStroke({80,qreal(y)},1);document->endStroke();
        QCOMPARE(active(canvas).objects.size(),1);const auto original=active(canvas).objects.first();QVERIFY(original.brushStroke.has_value());
        QCOMPARE(ObjectTools::hitTest(active(canvas),{560,190},2),original.id);
        QVERIFY(ObjectTools::eraseAt(document,{560,190},8,true));QVERIFY(active(canvas).objects.size()>=2);
        for(const auto &fragment:active(canvas).objects){QVERIFY(fragment.brushStroke.has_value());QVERIFY(fragment.brushStroke->samples.size()<original.brushStroke->samples.size());QVERIFY(validateEditableObject(fragment).isEmpty());}
        document->history()->undo();QCOMPARE(active(canvas).objects.size(),1);QCOMPARE(active(canvas).objects.first().id,original.id);
        QVERIFY(ObjectTools::eraseAt(document,{560,190},8,false));QVERIFY(active(canvas).objects.isEmpty());
    }
    void clippedBrushCannotBeSelectedOutsideItsPaintedRegion(){
        Canvas canvas;setup(canvas);ObjectTools::addLayer(canvas.document(),LayerKind::Vector);auto *document=canvas.document();QPainterPath selection;selection.addRect(120,80,80,80);document->setRasterSelection(selection);
        BrushSettings brush;brush.size=16;brush.pressureSize=false;QVERIFY(document->beginStroke({60,120},1,brush,false));document->continueStroke({280,120},1);document->endStroke();
        const auto id=active(canvas).objects.first().id;QCOMPARE(ObjectTools::hitTest(active(canvas),{160,120},2),id);QVERIFY(ObjectTools::hitTest(active(canvas),{250,120},2).isEmpty());
        QVERIFY(!ObjectTools::eraseAt(document,{250,120},5));QCOMPARE(active(canvas).objects.first().id,id);
    }
    void vectorLayerButtonCreatesRealLayer(){
        Document doc;doc.load(project());LayerPanel panel(&doc);panel.resize(270,400);panel.show();QApplication::processEvents();
        auto *button=panel.findChild<QToolButton*>("addVectorLayer");QVERIFY(button);QVERIFY(button->isEnabled());QTest::mouseClick(button,Qt::LeftButton);
        QCOMPARE(doc.project().shots[0].layers[doc.activeLayer()].kind,LayerKind::Vector);QVERIFY(!panel.findChild<QToolButton*>("layerAlphaLocked")->isEnabled());
        QDir().mkpath("test-artifacts");QVERIFY(panel.grab().save("test-artifacts/layers-vector-text.png"));
    }
};
QTEST_MAIN(DrawingToolsUiTest)
#include "test_drawing_tools_ui.moc"
