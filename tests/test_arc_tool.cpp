#include "canvas.h"
#include "drawing_tool_controller.h"
#include "drawing_tool_panel.h"
#include "object_tools.h"
#include "tool_catalog.h"
#include "tool_session.h"
#include "ui_theme.h"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QSignalSpy>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>
using namespace toon;
namespace {
void setup(Canvas &canvas) {
    auto project=Project::create();project.width=640;project.height=480;project.transparent=true;
    project.shots[0].sheet.overscan=0;
    canvas.resize(1000,800);canvas.setProject(project);canvas.show();canvas.setZoom(1);
    canvas.setTool(Canvas::Tool::Arc);QApplication::processEvents();
}
QPointF view(const Canvas &canvas,QPointF point){return canvas.viewTransform().map(point+sheetGeometry(canvas.project(),canvas.shot()).output.topLeft());}
void mouse(Canvas &canvas,QEvent::Type type,QPointF point,Qt::MouseButtons buttons=Qt::LeftButton){
    const auto v=view(canvas,point);QMouseEvent event(type,v,v,canvas.mapToGlobal(v),
        type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
        type==QEvent::MouseButtonRelease?Qt::NoButton:buttons,Qt::NoModifier);
    QApplication::sendEvent(&canvas,&event);
}
void endpoints(Canvas &canvas){mouse(canvas,QEvent::MouseButtonPress,{100,220});mouse(canvas,QEvent::MouseMove,{300,220});mouse(canvas,QEvent::MouseButtonRelease,{300,220});}
void click(Canvas &canvas,QPointF point){mouse(canvas,QEvent::MouseButtonPress,point);mouse(canvas,QEvent::MouseButtonRelease,point);}
const RasterLayer &active(const Canvas &canvas){return canvas.shot().layers[canvas.document()->activeLayer()];}
QJsonObject read(const QString &path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return QJsonDocument::fromJson(file.readAll()).object();}
bool write(const QString &path,const QJsonObject &root){QFile file(path);if(!file.open(QIODevice::WriteOnly))return false;const auto data=QJsonDocument(root).toJson();return file.write(data)==data.size();}
QJsonObject withoutArc(QJsonObject root){
    root["version"]=9;QJsonArray tools,entries;
    for(const auto &value:root["tools"].toArray())if(value.toObject()["engine"].toInt()!=int(Canvas::Tool::Arc))tools.append(value);
    for(const auto &value:root["entries"].toArray()){
        auto entry=value.toObject();QJsonArray ids;
        for(const auto &id:entry["tools"].toArray())if(id.toString()!="arc")ids.append(id);
        entry["tools"]=ids;entries.append(entry);
    }
    root["tools"]=tools;root["entries"]=entries;return root;
}
}
class ArcToolTests:public QObject{
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void rasterPreviewIsTransientAndPassesThroughThirdPoint(){
        Canvas canvas;setup(canvas);auto *tools=canvas.drawingTools();tools->strokeWidth=5;
        const auto before=canvas.project().toJson();endpoints(canvas);
        QVERIFY(tools->waitingForArcBend());QVERIFY(!tools->dragging());QVERIFY(tools->active());
        QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),0);
        QSignalSpy changed(tools,&DrawingToolController::changed);
        mouse(canvas,QEvent::MouseMove,{200,100},Qt::NoButton);const auto above=canvas.grab().toImage();
        mouse(canvas,QEvent::MouseMove,{200,320},Qt::NoButton);const auto below=canvas.grab().toImage();
        QVERIFY(above!=below);QCOMPARE(changed.count(),0);QCOMPARE(canvas.project().toJson(),before);
        click(canvas,{200,100});QVERIFY(!tools->active());QCOMPARE(canvas.document()->history()->count(),1);
        const auto frame=renderMovieFrame(canvas.project());QVERIFY(frame.pixelColor(200,100).alpha()>0);
        QCOMPARE(frame.pixelColor(200,220).alpha(),0);QVERIFY(frame.pixelColor(100,220).alpha()>0);
        canvas.document()->history()->undo();QVERIFY(active(canvas).tiles.isEmpty());
        canvas.document()->history()->redo();QCOMPARE(renderMovieFrame(canvas.project()),frame);
    }
    void vectorArcIsEditableAndSingleUndo(){
        Canvas canvas;setup(canvas);ObjectTools::addLayer(canvas.document(),LayerKind::Vector);
        const int count=canvas.document()->history()->count();endpoints(canvas);
        mouse(canvas,QEvent::MouseMove,{180,100},Qt::NoButton);QTest::keyClick(&canvas,Qt::Key_Return);
        QCOMPARE(active(canvas).objects.size(),1);const auto object=active(canvas).objects.first();
        QCOMPARE(object.nodes.size(),2);QVERIFY(!object.closed);
        QCOMPARE(object.nodes.first().anchor,QPointF(100,220));QCOMPARE(object.nodes.last().anchor,QPointF(300,220));
        QVERIFY(QLineF(editableObjectPath(object).pointAtPercent(.5),QPointF(180,100)).length()<.001);
        QCOMPARE(canvas.document()->history()->count(),count+1);canvas.document()->history()->undo();
        QVERIFY(active(canvas).objects.isEmpty());canvas.document()->history()->redo();QCOMPARE(active(canvas).objects.first(),object);
    }
    void cancellationAndContextChangesLeaveNoArtwork(){
        Canvas canvas;setup(canvas);endpoints(canvas);QTest::keyClick(&canvas,Qt::Key_Escape);
        QVERIFY(!canvas.drawingTools()->active());QVERIFY(active(canvas).tiles.isEmpty());
        endpoints(canvas);QTest::keyClick(&canvas,Qt::Key_Backspace);QVERIFY(!canvas.drawingTools()->active());
        endpoints(canvas);canvas.setTool(Canvas::Tool::Brush);QVERIFY(!canvas.drawingTools()->active());
        canvas.setTool(Canvas::Tool::Arc);endpoints(canvas);canvas.document()->addLayer();
        QVERIFY(!canvas.drawingTools()->active());QTest::keyClick(&canvas,Qt::Key_Return);
        for(const auto &layer:canvas.shot().layers)QVERIFY(layer.tiles.isEmpty());
    }
    void missedReleaseAndTinyChordCancel(){
        Canvas canvas;setup(canvas);click(canvas,{200,200});QVERIFY(!canvas.drawingTools()->active());
        mouse(canvas,QEvent::MouseButtonPress,{100,220});mouse(canvas,QEvent::MouseMove,{300,220});
        mouse(canvas,QEvent::MouseMove,{200,100},Qt::NoButton);QVERIFY(!canvas.drawingTools()->active());
        QVERIFY(active(canvas).tiles.isEmpty());QCOMPARE(canvas.document()->history()->count(),0);
    }
    void selectedAreaClipsRasterArc(){
        Canvas canvas;setup(canvas);QPainterPath selection;selection.addRect(0,0,200,480);
        canvas.document()->setRasterSelection(selection);endpoints(canvas);click(canvas,{200,100});
        const auto frame=renderMovieFrame(canvas.project());QVERIFY(frame.pixelColor(100,220).alpha()>0);
        QCOMPARE(frame.pixelColor(300,220).alpha(),0);QCOMPARE(canvas.document()->history()->count(),1);
    }
    void tabletHoverMovesBendAndTapCommits(){
        Canvas canvas;setup(canvas);ObjectTools::addLayer(canvas.document(),LayerKind::Vector);
        QPointingDevice pen("Test pen",420,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
        const auto tablet=[&](QEvent::Type type,QPointF point){
            const auto v=view(canvas,point);const bool contact=type==QEvent::TabletPress;
            QTabletEvent event(type,&pen,v,canvas.mapToGlobal(v),contact?.7:0,0,0,0,0,0,Qt::NoModifier,
                type==QEvent::TabletMove?Qt::NoButton:Qt::LeftButton,contact?Qt::LeftButton:Qt::NoButton);
            QApplication::sendEvent(&canvas,&event);
        };
        tablet(QEvent::TabletPress,{100,220});tablet(QEvent::TabletRelease,{300,220});
        QVERIFY(canvas.drawingTools()->waitingForArcBend());QVERIFY(active(canvas).objects.isEmpty());
        tablet(QEvent::TabletMove,{180,100});const auto preview=canvas.grab().toImage();
        tablet(QEvent::TabletMove,{180,320});QVERIFY(canvas.grab().toImage()!=preview);
        tablet(QEvent::TabletMove,{180,100});QTest::keyClick(&canvas,Qt::Key_Return);
        QCOMPARE(active(canvas).objects.size(),1);
        QVERIFY(QLineF(editableObjectPath(active(canvas).objects.first()).pointAtPercent(.5),QPointF(180,100)).length()<.001);
        tablet(QEvent::TabletPress,{100,220});tablet(QEvent::TabletRelease,{300,220});
        tablet(QEvent::TabletPress,{200,100});tablet(QEvent::TabletRelease,{200,100});
        QCOMPARE(active(canvas).objects.size(),2);QVERIFY(!canvas.drawingTools()->active());
    }
    void cubicCurveKeepsFourClickWorkflow(){
        Canvas canvas;setup(canvas);canvas.setTool(Canvas::Tool::Curve);ObjectTools::addLayer(canvas.document(),LayerKind::Vector);
        for(const auto &point:{QPointF(100,200),QPointF(120,50),QPointF(300,50)}){click(canvas,point);QVERIFY(active(canvas).objects.isEmpty());}
        click(canvas,{320,200});QCOMPARE(active(canvas).objects.size(),1);
        QCOMPARE(active(canvas).objects.first().nodes.first().outControl,QPointF(120,50));
        QCOMPARE(active(canvas).objects.first().nodes.last().inControl,QPointF(300,50));
    }
    void arcPropertiesHideUnusedCloseAction(){
        Canvas canvas;setup(canvas);DrawingToolPanel panel(&canvas);panel.show();QApplication::processEvents();
        auto *closed=panel.findChild<QToolButton*>("pathClosed");auto *finish=panel.findChild<QToolButton*>("finishDrawingPath");
        QVERIFY(closed);QVERIFY(finish);QVERIFY(closed->isHidden());QVERIFY(finish->toolTip().contains("arc"));
        canvas.setTool(Canvas::Tool::Curve);QVERIFY(!closed->isHidden());
    }
    void catalogUpgradePreservesMetadataAndHonorsLaterRemoval(){
        QTemporaryDir directory;const auto path=directory.filePath("tools.json");ToolCatalog catalog(nullptr,path);QVERIFY(catalog.load());
        QVERIFY(catalog.renameEntry("group/shapes","My ink geometry"));QVERIFY(catalog.setEntryTags("group/shapes",{"clean"}));
        QVERIFY(catalog.setEntryColor("group/shapes",QColor("#ff9944")));QVERIFY(catalog.selectEntry("group/shapes"));QVERIFY(catalog.activateTool("ellipse"));
        QVERIFY(catalog.save());auto root=withoutArc(read(path));QVERIFY(write(path,root));
        QString error;QVERIFY2(catalog.load(&error),qPrintable(error));QVERIFY(catalog.tool("arc"));
        const auto *group=catalog.entry("group/shapes");QCOMPARE(group->name,QString("My ink geometry"));
        QCOMPARE(group->tags,QStringList{"clean"});QCOMPARE(group->color,QColor("#ff9944"));
        QCOMPARE(group->lastToolId,QString("ellipse"));QCOMPARE(catalog.activeToolId(),QString("ellipse"));
        QVERIFY(group->tools.contains("arc"));QVERIFY(catalog.removeTool("group/shapes","arc"));QVERIFY(catalog.save());
        QCOMPARE(read(path)["version"].toInt(),10);QVERIFY2(catalog.load(&error),qPrintable(error));
        QVERIFY(!catalog.entry("group/shapes")->tools.contains("arc"));
        Canvas canvas;setup(canvas);ToolSession session(&canvas,&catalog);QVERIFY(catalog.addStandalone("arc").size()>0);
        QVERIFY(catalog.activateTool("arc"));QCOMPARE(canvas.tool(),Canvas::Tool::Arc);
    }
    void catalogUpgradeDoesNotRecreateRemovedShapes(){
        QTemporaryDir directory;const auto path=directory.filePath("tools.json");ToolCatalog catalog(nullptr,path);QVERIFY(catalog.load());
        QVERIFY(catalog.removeEntry("group/shapes"));QVERIFY(catalog.save());QVERIFY(write(path,withoutArc(read(path))));
        QVERIFY(catalog.load());QVERIFY(!catalog.entry("group/shapes"));QVERIFY(catalog.entry("entry/arc"));
        QVERIFY(!catalog.entry("entry/arc")->group);QCOMPARE(catalog.entry("entry/arc")->tools,QStringList{"arc"});
        QVERIFY(catalog.removeEntry("entry/arc"));QVERIFY(catalog.save());QVERIFY(catalog.load());QVERIFY(!catalog.entry("entry/arc"));
    }
};
QTEST_MAIN(ArcToolTests)
#include "test_arc_tool.moc"
