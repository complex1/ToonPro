#include "timeline_test_geometry.h"
#include "canvas.h"
#include "document.h"
#include "symbol_clips.h"
#include "symbol_frame_picker_dialog.h"
#include "symbol_poses.h"
#include "symbols.h"
#include "timeline_controller.h"
#include "timeline_panel.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QAbstractScrollArea>
#include <QApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QMenu>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QtTest>

using namespace toon;
namespace {
QString id(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
Project fixture(){
    auto project=Project::create();project.width=192;project.height=128;project.shots[0].durationFrames=48;
    auto content=Project::create().shots[0];content.durationFrames=12;content.layers[0].name="Mouth shapes";
    QImage pixels(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);pixels.fill(Qt::transparent);
    QPainter painter(&pixels);painter.setBrush(QColor("#d69b85"));painter.setPen(QPen(Qt::black,3));painter.drawEllipse(QRectF(35,35,70,30));painter.end();
    content.layers[0].tiles[{0,0}]=pixels;
    content.layerAnimations.append({content.layers[0].id,{},{{KeyframeProperty::ScaleY,{{0,.25},{5,1},{11,.4}}}}});
    SymbolDefinition definition{id(),"Mika · Mouth",content};project.symbols.append(definition);
    auto mouth=RasterLayer::create("Mouth");mouth.kind=LayerKind::Symbol;
    SymbolInstance instance{definition.id};instance.playback=SymbolPlayback::Poses;instance.startFrame=4;instance.durationFrames=24;instance.playbackOffset=2;
    instance.poseKeys={{2,0},{6,2},{12,6}};mouth.symbol=instance;project.shots[0].layers={mouth};return project;
}
QAbstractScrollArea *grid(QWidget &panel){return panel.findChild<QAbstractScrollArea *>("timelineGrid");}
int label(QAbstractScrollArea *view){return timeline_test::labelWidth(view);}
QPoint cell(QAbstractScrollArea *view,int frame,int row=1){return {label(view)+frame*20+10-view->horizontalScrollBar()->value(),timeline_test::rowCenter(view,row)};}
void settle(){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);}
void show(QWidget &panel,QSize size={1000,420}){
    panel.resize(size);panel.show();QVERIFY(QTest::qWaitForWindowExposed(&panel));panel.raise();panel.activateWindow();QVERIFY(QTest::qWaitForWindowActive(&panel));settle();
}
void move(QAbstractScrollArea *view,QPoint point){QMouseEvent event(QEvent::MouseMove,point,view->viewport()->mapToGlobal(point),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view->viewport(),&event);}
void click(QAbstractScrollArea *view,int frame,int row=1,Qt::KeyboardModifiers modifiers=Qt::NoModifier){QTest::mouseClick(view->viewport(),Qt::LeftButton,modifiers,cell(view,frame,row));}
void drag(QAbstractScrollArea *view,int from,int to){QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,from));move(view,cell(view,to));QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,to));}
const SymbolInstance &instance(Canvas &canvas){return *canvas.shot().layers[0].symbol;}
}
class SymbolPoseTimelineTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();QVERIFY2(fixture().validate().isEmpty(),qPrintable(fixture().validate()));}
    void poseLaneSelectsParentFrameWithoutSelectingClipOrCamera(){
        const auto project=fixture();Canvas canvas;canvas.setProject(project);TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);
        click(grid(panel),8);QCOMPARE(controller.selectedSymbolPoseLayerId(),project.shots[0].layers[0].id);QVERIFY(controller.selectedSymbolClipId().isEmpty());QVERIFY(!controller.cameraSelected());
        QCOMPARE(controller.currentFrame(),8);QCOMPARE(controller.selectedKeys().size(),1);QVERIFY(controller.selectedKeys()[0].isSymbolPose());QCOMPARE(controller.selectedKeys()[0].frame,8);
        QCOMPARE(canvas.project().toJson(),project.toJson());QCOMPARE(canvas.document()->history()->count(),0);
        click(grid(panel),10,0);QCOMPARE(controller.selectedSymbolClipId(),project.shots[0].layers[0].id);QVERIFY(controller.selectedSymbolPoseLayerId().isEmpty());QVERIFY(controller.selectedKeys().isEmpty());
        click(grid(panel),0,2);QVERIFY(controller.cameraSelected());QVERIFY(controller.selectedSymbolPoseLayerId().isEmpty());
    }
    void keyDragIsPreviewUntilReleaseAndOneUndo(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);auto *view=grid(panel);
        const auto before=canvas.project().toJson();QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,8));move(view,cell(view,10));
        QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),0);
        QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,10));
        QVERIFY(!symbolPoseKeyAtParent(instance(canvas),8));QVERIFY(symbolPoseKeyAtParent(instance(canvas),10));QCOMPARE(symbolPoseKeyAtParent(instance(canvas),10)->poseFrame,2);
        QCOMPARE(canvas.document()->history()->count(),1);canvas.document()->history()->undo();QCOMPARE(canvas.project().toJson(),before);
    }
    void shiftSelectionMovesSeveralPosesTogether(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);auto *view=grid(panel);
        click(view,4);click(view,14,1,Qt::ShiftModifier);QCOMPARE(controller.selectedKeys().size(),3);drag(view,8,10);
        for(int frame:{6,10,16})QVERIFY(symbolPoseKeyAtParent(instance(canvas),frame));QCOMPARE(canvas.document()->history()->count(),1);
        canvas.document()->history()->undo();for(int frame:{4,8,14})QVERIFY(symbolPoseKeyAtParent(instance(canvas),frame));
    }
    void escapeWindowDeactivationAndCollisionNeverCommit(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);auto *view=grid(panel);const auto before=canvas.project().toJson();
        QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,8));move(view,cell(view,10));QTest::keyClick(view,Qt::Key_Escape);QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,10));QCOMPARE(canvas.project().toJson(),before);
        QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,8));move(view,cell(view,10));QEvent deactivate(QEvent::WindowDeactivate);QApplication::sendEvent(view,&deactivate);QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,10));QCOMPARE(canvas.project().toJson(),before);
        drag(view,8,14);QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),0);
    }
    void undoDuringDragDoesNotRestoreAStalePose(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);auto *view=grid(panel);const auto layer=canvas.shot().layers[0].id;
        QVERIFY(setSymbolPose(canvas.document(),layer,8,9));QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,8));move(view,cell(view,10));canvas.document()->history()->undo();
        QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,10));QVERIFY(!symbolPoseKeyAtParent(instance(canvas),10));QCOMPARE(symbolPoseKeyAtParent(instance(canvas),8)->poseFrame,2);QCOMPARE(canvas.document()->history()->index(),0);
    }
    void copyPasteDeleteAndSelectAllStayInPoseDomain(){
        auto project=fixture();const auto layer=project.shots[0].layers[0].id;project.shots[0].layerAnimations={{layer,{},{{KeyframeProperty::PositionX,{{0,0},{12,20}}}}}};
        Canvas canvas;canvas.setProject(project);TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);auto *view=grid(panel);
        click(view,8);click(view,14,1,Qt::ControlModifier);QCOMPARE(controller.selectedKeys().size(),2);QTest::keySequence(view,QKeySequence(QKeySequence::Copy));
        QVERIFY(controller.setFrame(18));QTest::keySequence(view,QKeySequence(QKeySequence::Paste));QVERIFY(symbolPoseKeyAtParent(instance(canvas),18));QVERIFY(symbolPoseKeyAtParent(instance(canvas),24));QCOMPARE(canvas.document()->history()->count(),1);
        QTest::keySequence(view,QKeySequence(QKeySequence::SelectAll));QCOMPARE(controller.selectedKeys().size(),5);for(const auto &key:controller.selectedKeys())QVERIFY(key.isSymbolPose());
        auto *remove=panel.findChild<QToolButton *>("timelineRemoveExposure");QVERIFY(remove->isEnabled());remove->click();QVERIFY(instance(canvas).poseKeys.isEmpty());QCOMPARE(instance(canvas).durationFrames,24);
        QCOMPARE(canvas.shot().layerAnimations[0].channels[0].keys.size(),2);QCOMPARE(canvas.document()->history()->count(),2);canvas.document()->history()->undo();QCOMPARE(instance(canvas).poseKeys.size(),5);
    }
    void lockedPosesRemainSelectableButDoNotChange(){
        auto project=fixture();project.shots[0].layers[0].locked=true;Canvas canvas;canvas.setProject(project);TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);
        auto *view=grid(panel);click(view,8);QVERIFY(!controller.selectedKeys().isEmpty());QVERIFY(!panel.findChild<QToolButton *>("timelineRemoveExposure")->isEnabled());drag(view,8,10);QTest::keyClick(view,Qt::Key_Delete);
        QCOMPARE(canvas.project().toJson(),project.toJson());QCOMPARE(canvas.document()->history()->count(),0);
    }
    void trimmedKeysStayInDataAndSelectAllOnlySelectsVisibleClipKeys(){
        auto project=fixture();auto &clip=*project.shots[0].layers[0].symbol;clip.startFrame=10;clip.durationFrames=8;clip.playbackOffset=8;
        Canvas canvas;canvas.setProject(project);TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);auto *view=grid(panel);
        click(view,14);QTest::keySequence(view,QKeySequence(QKeySequence::SelectAll));QCOMPARE(controller.selectedKeys().size(),1);QCOMPARE(controller.selectedKeys()[0].frame,14);
        QTest::keyClick(view,Qt::Key_Delete);QCOMPARE(instance(canvas).poseKeys.size(),2);QVERIFY(symbolPoseKeyAtParent(instance(canvas),4));QVERIFY(symbolPoseKeyAtParent(instance(canvas),8));
    }
    void fittingPoseSelectionDoesNotFitWholeClip(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);auto *view=grid(panel);click(view,8);click(view,14,1,Qt::ShiftModifier);
        panel.findChild<QToolButton *>("timelineFitSelection")->click();QVERIFY(panel.findChild<ValueSlider *>("timelineZoom")->value()>100);QCOMPARE(controller.selectedKeys().size(),2);QCOMPARE(canvas.document()->history()->count(),0);
    }
    void doubleClickOpensTimedPickerAtClickedFrameAndCancelDoesNotEdit(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);auto *view=grid(panel);bool visited=false;
        QTimer poll;poll.setInterval(5);QTimer deadline;deadline.setSingleShot(true);
        connect(&poll,&QTimer::timeout,&panel,[&]{if(auto *dialog=qobject_cast<SymbolFramePickerDialog *>(QApplication::activeModalWidget())){if(!dialog->isActiveWindow())return;poll.stop();visited=true;QVERIFY(dialog->windowTitle().contains("pose",Qt::CaseInsensitive));QCOMPARE(controller.currentFrame(),11);dialog->reject();}});
        connect(&deadline,&QTimer::timeout,&panel,[]{if(auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()))dialog->reject();});
        poll.start();deadline.start(15000);QTest::mouseDClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,11));poll.stop();deadline.stop();QVERIFY(visited);QCOMPARE(canvas.document()->history()->count(),0);QCOMPARE(instance(canvas).poseKeys.size(),3);
    }
    void renderPreview(){
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");if(output.isEmpty())QSKIP("Opt-in timed Symbol pose captures.");
        auto project=fixture();TimelineCue dialogue;dialogue.id=id();dialogue.kind=TimelineCueKind::Dialogue;dialogue.name="Greeting";dialogue.speaker="Mika";dialogue.text="I have been waiting for you.";dialogue.startFrame=4;dialogue.durationFrames=24;project.shots[0].timelineCues.append(dialogue);
        Canvas canvas;canvas.setProject(project);TimelineController controller(&canvas);QWidget workspace;auto *layout=new QHBoxLayout(&workspace);
        TimelinePanel timeline(&controller,&workspace);FramePropertiesPanel properties(&controller,&workspace);properties.setFixedWidth(300);layout->addWidget(&timeline,1);layout->addWidget(&properties);show(workspace,{1200,760});
        QVERIFY(controller.selectSymbolPoseLayer(project.shots[0].layers[0].id));QVERIFY(controller.setFrame(8));controller.setSelectedKeys({{project.shots[0].layers[0].id,KeyframeProperty::PositionX,{},8,TimelineKeyKind::SymbolPose}});emit controller.symbolPropertiesRequested();settle();
        QDir().mkpath(output);QVERIFY(workspace.grab().save(output+"/symbol-poses-workspace.png"));properties.setFixedWidth(240);auto font=properties.font();font.setPointSizeF(font.pointSizeF()+3);properties.setFont(font);settle();QVERIFY(properties.grab().save(output+"/symbol-poses-properties-240.png"));
    }
};
QTEST_MAIN(SymbolPoseTimelineTests)
#include "test_symbol_pose_timeline.moc"
