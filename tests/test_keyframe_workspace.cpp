#include "canvas.h"
#include "keyframe_render.h"
#include "keyframe_transform_tool.h"
#include "timeline_controller.h"
#include "animation_render.h"
#include "animation_test_fixture.h"
#include "raster_tools.h"
#include "clipboard_tools.h"
#include "main_window.h"
#include "value_slider.h"
#include <QAction>
#include <QColorSpace>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QTabBar>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
using namespace toon;
namespace {
Project fixture(){auto p=Project::create();p.width=p.height=64;p.transparent=true;p.shots[0].durationFrames=21;p.shots[0].sheet.overscan=0;
    QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);QPainter painter(&tile);painter.fillRect(QRect(8,8,16,16),Qt::red);painter.end();p.shots[0].layers[0].tiles.insert({0,0},tile);return p;}
void motion(Document &d,const QString &id){QVERIFY(d.setCurrentFrame(20));QVERIFY(d.setKeyframeValue(id,KeyframeProperty::PositionX,32));QVERIFY(d.setKeyframeValue(id,KeyframeProperty::Opacity,.5));QVERIFY(d.setCurrentFrame(0));}
}
class KeyframeWorkspaceTests:public QObject{
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void renderInterpolationAndExportWithoutChangingSource(){Document d;d.load(fixture());const auto source=d.project().shots[0].layers[0];motion(d,source.id);
        QCOMPARE(renderMovieFrame(d.project(),0).pixelColor(10,10),QColor(Qt::red));
        auto middle=renderMovieFrame(d.project(),10);QCOMPARE(middle.pixelColor(10,10).alpha(),0);QCOMPARE(middle.pixelColor(26,10).red(),255);QVERIFY(qAbs(middle.pixelColor(26,10).alpha()-191)<=1);
        const auto end=renderMovieFrame(d.project(),20);QVERIFY(qAbs(end.pixelColor(42,10).alpha()-128)<=1);
        QCOMPARE(d.project().shots[0].layers[0].tiles,source.tiles);QVERIFY(d.project().shots[0].layers[0].renderTransform.isIdentity());
        QTemporaryDir dir;QString error;QVERIFY2(exportPngSequence(d.project(),0,0,20,dir.filePath("frames"),{},&error),qPrintable(error));
        QCOMPARE(QImage(dir.filePath("frames/frame-0011.png")).convertToFormat(QImage::Format_ARGB32_Premultiplied),middle);
    }
    void transformedGroupsKeepIsolationAndClipCoverage(){auto p=fixture();auto &s=p.shots[0];auto group=RasterLayer::create("Group");group.group=true;group.opacity=.5;auto base=s.layers[0];base.parentId=group.id;
        auto overlay=base;overlay.id=RasterLayer::create().id;overlay.clipped=true;QImage green(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);green.fill(Qt::green);overlay.tiles[TileKey(0,0)]=green;s.layers={group,base,overlay};
        Document d;d.load(p);QVERIFY(d.setCurrentFrame(20));QVERIFY(d.setKeyframeValue(group.id,KeyframeProperty::PositionX,16));QVERIFY(d.setKeyframeValue(base.id,KeyframeProperty::PositionY,8));QVERIFY(d.setKeyframeValue(overlay.id,KeyframeProperty::PositionY,8));
        auto image=renderMovieFrame(d.project(),20);QCOMPARE(image.pixelColor(26,18).green(),255);QVERIFY(qAbs(image.pixelColor(26,18).alpha()-128)<=1);QCOMPARE(image.pixelColor(10,10).alpha(),0);QCOMPARE(image.pixelColor(45,18).alpha(),0);
    }
    void animationExportsRetainFullResolutionAbovePreviewBudget(){auto p=fixture();p.width=2200;p.height=2000;Document d;d.load(p);auto id=p.shots[0].layers[0].id;QVERIFY(d.setKeyframeValue(id,KeyframeProperty::PositionX,1));auto result=renderMovieFrame(d.project(),0);
        QImage expected(p.width,p.height,QImage::Format_ARGB32_Premultiplied);expected.fill(Qt::transparent);QPainter painter(&expected);painter.fillRect(QRect(9,8,16,16),Qt::red);painter.end();
        expected.setColorSpace(QColorSpace::SRgb);
        QCOMPARE(result,expected);
    }
    void fractionalMotionExportsFilteredEdges(){
        Document document;document.load(fixture());const auto id=document.project().shots[0].layers[0].id;
        QVERIFY(document.setKeyframeValue(id,KeyframeProperty::PositionX,.5));
        const auto image=renderMovieFrame(document.project(),0);const int alpha=image.pixelColor(8,10).alpha();
        QVERIFY(alpha>100&&alpha<180);QCOMPARE(image.pixelColor(10,10),QColor(Qt::red));
        QCOMPARE(RasterTools::sample(&document,{8,10},RasterSampleSource::AllLayers),image.pixelColor(8,10));
    }
    void fractionalMotionFiltersAcrossTileBoundaries(){
        auto project=fixture();project.width=project.height=512;
        QImage source(512,512,QImage::Format_ARGB32_Premultiplied);source.fill(Qt::transparent);
        {QPainter painter(&source);painter.fillRect(QRect(247,247,18,18),QColor(240,50,20,180));
         painter.fillRect(QRect(256,247,9,18),QColor(20,100,230,150));}
        auto &layer=project.shots[0].layers[0];layer.tiles.clear();
        for(int y=0;y<2;++y)for(int x=0;x<2;++x)layer.tiles.insert({x,y},source.copy(x*tileSize,y*tileSize,tileSize,tileSize));
        const auto tiles=layer.tiles;Document document;document.load(project);const auto id=layer.id;
        const auto sample=[&](int x,int y){return source.rect().contains(x,y)?source.pixel(x,y):QRgb(0);};
        for(const auto offset:{QPointF(.25,.75),QPointF(-.75,-.5),QPointF(.5,0),QPointF(.25,.75)}){
            QVERIFY(document.setKeyframeValue(id,KeyframeProperty::PositionX,offset.x()));
            QVERIFY(document.setKeyframeValue(id,KeyframeProperty::PositionY,offset.y()));
            const auto result=renderMovieFrame(document.project(),0);
            QImage expected(source.size(),source.format());expected.setColorSpace(QColorSpace::SRgb);
            for(int y=0;y<expected.height();++y)for(int x=0;x<expected.width();++x){
                const qreal sx=x-offset.x(),sy=y-offset.y();const int left=std::floor(sx),top=std::floor(sy);
                const qreal fx=sx-left,fy=sy-top;
                const QRgb pixels[]{sample(left,top),sample(left+1,top),sample(left,top+1),sample(left+1,top+1)};
                const qreal weights[]{(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};
                qreal red=0,green=0,blue=0,alpha=0;
                for(int n=0;n<4;++n){red+=qRed(pixels[n])*weights[n];green+=qGreen(pixels[n])*weights[n];blue+=qBlue(pixels[n])*weights[n];alpha+=qAlpha(pixels[n])*weights[n];}
                expected.setPixel(x,y,qRgba(qRound(red),qRound(green),qRound(blue),qRound(alpha)));
            }
            QCOMPARE(result,expected);QCOMPARE(document.project().shots[0].layers[0].tiles,tiles);
        }
    }
    void onionGhostsUseNeighborFrameTransforms(){
        auto p=animation_fixture::project();
        for (int index : {3,5}) {
            QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);
            QPainter painter(&tile);painter.fillRect(QRect(8,8,4,4), index==3?Qt::red:Qt::blue);painter.end();
            p.shots[0].layers[index].tiles[TileKey(0,0)]=tile;
        }
        const auto track=p.shots[0].animationTracks[0];Document document;document.load(p);
        QVERIFY(document.setCurrentFrame(19));QVERIFY(document.setKeyframeValue(track.rootGroupLayerId,KeyframeProperty::PositionX,19));
        const auto saved=document.project().toJson();
        for (int direction : {-1,1}) {
            const auto ghost=onionSkinLayers(document.project().shots[0],3,track.id,direction);
            QImage image(64,64,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
            QPainter painter(&image);paintLayers(painter,ghost,false);painter.end();
            QCOMPARE(image.pixelColor(direction<0?10:12,9),QColor(direction<0?Qt::red:Qt::blue));
            QCOMPARE(image.pixelColor(8,9).alpha(),0);
        }
        QCOMPARE(document.project().toJson(),saved);
    }
    void sourceModeSamplingAndEditingAreConsistent(){Canvas canvas;TimelineController timeline(&canvas);canvas.resize(700,550);canvas.setProject(fixture());canvas.show();const auto id=canvas.shot().layers[0].id;motion(*canvas.document(),id);QVERIFY(timeline.setFrame(20));canvas.grab();
        QVERIFY(!canvas.toolBlockReason().isEmpty());QVERIFY(!canvas.document()->beginStroke({42,10},1,BrushSettings{},false));
        QCOMPARE(RasterTools::sample(canvas.document(),QPointF(42,10),RasterSampleSource::AllLayers).red(),255);
        const auto animated=renderMovieFrame(canvas.project(),20);const auto count=canvas.artworkRenderCount();timeline.setSourceEditing(true);canvas.grab();QVERIFY(canvas.artworkRenderCount()>count);QVERIFY(canvas.toolBlockReason().isEmpty());
        QCOMPARE(RasterTools::sample(canvas.document(),QPointF(10,10),RasterSampleSource::AllLayers).red(),255);QCOMPARE(renderMovieFrame(canvas.project(),20),animated);
        timeline.setPlaying(true);QVERIFY(!timeline.sourceEditing());timeline.setPlaying(false);
    }
    void animatedCanvasMoveCommitsOnceAndUndoCancelsPreview(){Canvas canvas;TimelineController timeline(&canvas);canvas.resize(700,550);canvas.setProject(fixture());canvas.show();canvas.fitSheet();QVERIFY(timeline.setFrame(10));timeline.editAnimatedTransform();auto *tool=canvas.keyframeTransformTool();QVERIFY(tool->enabled());
        const auto source=canvas.shot().layers[0].tiles;const auto box=tool->box();const QPoint start=((box[0]+box[2])/2).toPoint();const QPoint end=start+QPoint(40,20);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,start);QMouseEvent move(QEvent::MouseMove,end,canvas.mapToGlobal(end),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&canvas,&move);QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,end);
        QCOMPARE(canvas.document()->history()->count(),1);QCOMPARE(canvas.shot().layers[0].tiles,source);QVERIFY(evaluateProperty(canvas.shot(),canvas.shot().layers[0].id,KeyframeProperty::PositionX,10)>0);
        const auto after=canvas.project().toJson();const auto movedBox=tool->box();const auto next=((movedBox[0]+movedBox[2])/2).toPoint();QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,next);
        QMouseEvent again(QEvent::MouseMove,next+QPoint(20,0),canvas.mapToGlobal(next+QPoint(20,0)),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&canvas,&again);QTest::keyClick(&canvas,Qt::Key_Escape);QCOMPARE(canvas.project().toJson(),after);QCOMPARE(canvas.document()->history()->count(),1);
    }
    void nativeWindowSaveResolvesTypedKeyframe(){
        QTemporaryDir dir;QString error;const auto p=fixture();const auto id=p.shots[0].layers[0].id,path=dir.filePath("motion.toonproj");QVERIFY2(saveProject(p,path,&error),qPrintable(error));
        MainWindow window(dir.filePath("workspace.ini"));QVERIFY(window.openProject(path));window.resize(1320,900);window.show();
        auto *timeline=window.findChild<TimelineController*>();QVERIFY(timeline);QVERIFY(timeline->setFrame(10));QVERIFY(timeline->setPropertyEnabled(KeyframeProperty::PositionX,true));
        auto *dock=window.findChild<QDockWidget*>("framePropertiesPanel");QVERIFY(dock);dock->show();dock->raise();emit timeline->animationPropertiesRequested();QTest::qWait(40);
        auto *field=dock->findChild<ValueSlider*>("keyframeValue_positionX");QVERIFY(field);auto *spin=field->findChild<QDoubleSpinBox*>();QVERIFY(spin);QVERIFY(spin->isVisible());
        auto *undo=window.findChild<QAction*>("undoAction"),*save=window.findChild<QAction*>("saveProjectAction");QVERIFY(undo&&save);auto *history=window.canvas()->document()->history();
        save->trigger();QVERIFY(history->isClean());const int initial=history->count();const auto baseline=window.canvas()->project().toJson();
        const auto enter=[&](const QString &text){spin->setFocus();spin->selectAll();QTest::keyClicks(spin,text);};
        enter("18");QVERIFY(timeline->hasPendingEdits());QVERIFY(undo->isEnabled());QCOMPARE(spin->findChild<QLineEdit*>()->text(),QString("18"));QCOMPARE(window.canvas()->project().toJson(),baseline);
        undo->trigger();QVERIFY(!timeline->hasPendingEdits());QVERIFY(!window.canvas()->document()->keyframeEditActive());spin->clearFocus();QCoreApplication::processEvents();QCOMPARE(window.canvas()->project().toJson(),baseline);QCOMPARE(history->count(),initial);QVERIFY(history->isClean());
        enter("35");QVERIFY(timeline->hasPendingEdits());QTest::keyClick(spin,Qt::Key_Escape);QVERIFY(!timeline->hasPendingEdits());spin->clearFocus();QCoreApplication::processEvents();QCOMPARE(window.canvas()->project().toJson(),baseline);QCOMPARE(history->count(),initial);
        enter("24");QVERIFY(timeline->hasPendingEdits());QCOMPARE(evaluateProperty(window.canvas()->shot(),id,KeyframeProperty::PositionX,10),0.);
        QVERIFY(QMetaObject::invokeMethod(timeline,"changed"));QCOMPARE(spin->findChild<QLineEdit*>()->text(),QString("24"));save->trigger();
        QVERIFY(!timeline->hasPendingEdits());QVERIFY(!window.canvas()->document()->keyframeEditActive());QCOMPARE(history->count(),initial+1);QVERIFY(history->isClean());
        const auto saved=loadProject(path,&error);QVERIFY2(saved.has_value(),qPrintable(error));QCOMPARE(evaluateProperty(saved->shots[0],id,KeyframeProperty::PositionX,10),24.);
        spin->clearFocus();QCoreApplication::processEvents();QCOMPARE(history->count(),initial+1);
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");if(!output.isEmpty()){QDir().mkpath(output);auto *dockTimeline=window.findChild<QDockWidget*>("animationPanel");dockTimeline->show();window.resizeDocks({dockTimeline},{290},Qt::Vertical);QTest::qWait(40);QVERIFY(window.grab().save(output+"/keyframe-workspace.png"));}
        enter("36");QVERIFY(timeline->hasPendingEdits());
        QTimer::singleShot(0,&window,[]{auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());QVERIFY(box);box->button(QMessageBox::Cancel)->click();});
        QVERIFY(!window.close());QVERIFY(window.isVisible());QVERIFY(!timeline->hasPendingEdits());QCOMPARE(evaluateProperty(window.canvas()->shot(),id,KeyframeProperty::PositionX,10),36.);QVERIFY(!history->isClean());
        undo->trigger();QCOMPARE(evaluateProperty(window.canvas()->shot(),id,KeyframeProperty::PositionX,10),24.);QVERIFY(history->isClean());
        enter("48");QVERIFY(timeline->hasPendingEdits());
        QTimer::singleShot(0,&window,[]{auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());QVERIFY(box);box->button(QMessageBox::Save)->click();});
        QVERIFY(window.close());const auto closed=loadProject(path,&error);QVERIFY2(closed.has_value(),qPrintable(error));QCOMPARE(evaluateProperty(closed->shots[0],id,KeyframeProperty::PositionX,10),48.);
    }
};
QTEST_MAIN(KeyframeWorkspaceTests)
#include "test_keyframe_workspace.moc"
