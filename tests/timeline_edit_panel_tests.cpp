#include "timeline_test_geometry.h"
#include "canvas.h"
#include "document.h"
#include "timeline_controller.h"
#include "timeline_panel.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QAbstractScrollArea>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QKeySequence>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QtTest>
using namespace toon;
namespace {
Project fixture(){auto project=Project::create();project.width=160;project.height=120;project.shots[0].durationFrames=40;return project;}
void settle(){QCoreApplication::processEvents();for(int i=0;i<4;++i)QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);}
QAbstractScrollArea *grid(QWidget &panel){return panel.findChild<QAbstractScrollArea *>("timelineGrid");}
int label(QAbstractScrollArea *view){return timeline_test::labelWidth(view);}
QPoint cell(QAbstractScrollArea *view,int frame,int row){return {label(view)+frame*20+10-view->horizontalScrollBar()->value(),timeline_test::rowCenter(view,row)};}
QPoint edge(QAbstractScrollArea *view,int frame,int row,int inset=0){return {label(view)+frame*20+inset-view->horizontalScrollBar()->value(),timeline_test::rowCenter(view,row)};}
void move(QAbstractScrollArea *view,QPoint point,Qt::KeyboardModifiers modifiers=Qt::NoModifier){QMouseEvent event(QEvent::MouseMove,point,view->viewport()->mapToGlobal(point),Qt::NoButton,Qt::LeftButton,modifiers);QApplication::sendEvent(view->viewport(),&event);}
void click(QAbstractScrollArea *view,int frame,int row,Qt::KeyboardModifiers modifiers=Qt::NoModifier){QTest::mouseClick(view->viewport(),Qt::LeftButton,modifiers,cell(view,frame,row));}
void drag(QAbstractScrollArea *view,QPoint from,QPoint to,Qt::KeyboardModifiers modifiers=Qt::NoModifier){QTest::mousePress(view->viewport(),Qt::LeftButton,modifiers,from);move(view,to,modifiers);QTest::mouseRelease(view->viewport(),Qt::LeftButton,modifiers,to);}
void setupKeys(TimelineController &controller){const auto id=controller.selectedPropertyLayerId();controller.setPropertyEnabled(KeyframeProperty::PositionX,true);controller.setPropertyEnabled(KeyframeProperty::PositionY,true);for(int frame:{2,7}){controller.setFrame(frame);controller.document()->setKeyframeValue(id,KeyframeProperty::PositionX,frame*10);}for(int frame:{4,9}){controller.setFrame(frame);controller.document()->setKeyframeValue(id,KeyframeProperty::PositionY,frame*10);}controller.setFrame(0);}
QVector<int> frames(Canvas &canvas,KeyframeProperty property){QVector<int> result;const auto id=canvas.shot().layers[canvas.document()->activeLayer()].id;if(const auto *channel=propertyChannel(canvas.shot(),id,property))for(const auto &key:channel->keys)result.append(key.frame);return result;}
CelExposure exposure(Canvas &canvas,const QString &id){for(const auto &track:canvas.shot().animationTracks)for(const auto &value:track.exposures)if(value.id==id)return value;return {};}
void show(TimelinePanel &panel){panel.resize(1000,360);panel.show();settle();grid(panel)->horizontalScrollBar()->setValue(0);grid(panel)->verticalScrollBar()->setValue(0);}
}
class TimelineEditPanelTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void additiveRangeSelectionAndBatchDragCommitOnce(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);setupKeys(controller);TimelinePanel panel(&controller);show(panel);auto *view=grid(panel);
        click(view,2,1);QCOMPARE(controller.selectedKeys().size(),1);click(view,7,1,Qt::ControlModifier);QCOMPARE(controller.selectedKeys().size(),2);
        click(view,9,2,Qt::ShiftModifier);QCOMPARE(controller.selectedKeys().size(),3);const int before=canvas.document()->history()->count();
        const auto from=cell(view,2,1),to=cell(view,5,1);QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,from);move(view,to);
        QCOMPARE(frames(canvas,KeyframeProperty::PositionX),(QVector<int>{0,2,7}));QCOMPARE(canvas.document()->history()->count(),before);QVERIFY(controller.hasPendingEdits());
        QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,to);QCOMPARE(frames(canvas,KeyframeProperty::PositionX),(QVector<int>{0,5,10}));QCOMPARE(frames(canvas,KeyframeProperty::PositionY),(QVector<int>{0,4,12}));
        QCOMPARE(canvas.document()->history()->count(),before+1);QCOMPARE(controller.selectedKeys().size(),3);QVERIFY(!controller.hasPendingEdits());
        canvas.document()->history()->undo();QCOMPARE(frames(canvas,KeyframeProperty::PositionX),(QVector<int>{0,2,7}));QCOMPARE(frames(canvas,KeyframeProperty::PositionY),(QVector<int>{0,4,9}));
    }
    void pairedPositionKeysToggleTogetherAndPreviewBothLanes(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);setupKeys(controller);
        const auto id=controller.selectedPropertyLayerId();QVERIFY(canvas.document()->setMotionPathCurved(id,true,0));
        TimelinePanel panel(&controller);show(panel);auto *view=grid(panel);const int before=canvas.document()->history()->count();
        click(view,2,1);QCOMPARE(controller.selectedKeys().size(),2);
        click(view,7,2,Qt::ControlModifier);QCOMPARE(controller.selectedKeys().size(),4);
        click(view,2,1,Qt::ControlModifier);QCOMPARE(controller.selectedKeys().size(),2);
        for(const auto &key:controller.selectedKeys())QCOMPARE(key.frame,7);
        click(view,7,2,Qt::MetaModifier);QVERIFY(controller.selectedKeys().isEmpty());QCOMPARE(canvas.document()->history()->count(),before);
        const auto from=cell(view,2,1),to=cell(view,5,1);
        QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,from);move(view,to);settle();
        QCOMPARE(controller.selectedKeys().size(),2);QCOMPARE(frames(canvas,KeyframeProperty::PositionX),(QVector<int>{0,2,4,7,9}));
        const auto image=view->viewport()->grab().toImage();const auto ratio=image.devicePixelRatio();
        const auto marker=[&](int frame,int row){
            // Stay outside the playhead's center line. Antialiasing can blend
            // the selected diamond's white outline into its blue interior.
            const auto point=cell(view,frame,row)+QPoint(3,0);
            const auto color=image.pixelColor(qRound(point.x()*ratio),qRound(point.y()*ratio));
            const QColor accent(ui::accent);
            return qAbs(color.red()-accent.red())+qAbs(color.green()-accent.green())+qAbs(color.blue()-accent.blue())<90;
        };
        for(int row:{1,2}){QVERIFY2(marker(5,row),qPrintable(QString("Missing moved marker in lane %1").arg(row)));QVERIFY2(!marker(2,row),qPrintable(QString("Marker remained at its old frame in lane %1").arg(row)));}
        QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,to);
        QCOMPARE(frames(canvas,KeyframeProperty::PositionX),(QVector<int>{0,4,5,7,9}));QCOMPARE(frames(canvas,KeyframeProperty::PositionY),(QVector<int>{0,4,5,7,9}));
        QCOMPARE(canvas.document()->history()->count(),before+1);QVERIFY(!controller.hasPendingEdits());
    }
    void marqueeClipboardAndDeletePreserveRelativeTiming(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);setupKeys(controller);TimelinePanel panel(&controller);show(panel);auto *view=grid(panel);
        drag(view,cell(view,1,1),cell(view,8,2));QCOMPARE(controller.selectedKeys().size(),3);QTest::keySequence(view,QKeySequence::Copy);QVERIFY(controller.canPasteKeys());
        const int before=canvas.document()->history()->count();QVERIFY(controller.setFrame(15));QTest::keySequence(view,QKeySequence::Paste);
        QCOMPARE(frames(canvas,KeyframeProperty::PositionX),(QVector<int>{0,2,7,15,20}));QCOMPARE(frames(canvas,KeyframeProperty::PositionY),(QVector<int>{0,4,9,17}));QCOMPARE(controller.selectedKeys().size(),3);QCOMPARE(canvas.document()->history()->count(),before+1);
        QTest::keyClick(view,Qt::Key_Delete);QCOMPARE(frames(canvas,KeyframeProperty::PositionX),(QVector<int>{0,2,7}));QCOMPARE(frames(canvas,KeyframeProperty::PositionY),(QVector<int>{0,4,9}));QCOMPARE(canvas.document()->history()->count(),before+2);
        QTest::keySequence(view,QKeySequence::SelectAll);QCOMPARE(controller.selectedKeys().size(),6);
        QTest::keyClick(view,Qt::Key_Escape);QVERIFY(controller.selectedKeys().isEmpty());
    }
    void collisionCancelAndSaveResolutionAreAtomic(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);setupKeys(controller);TimelinePanel panel(&controller);show(panel);auto *view=grid(panel);
        click(view,2,1);click(view,4,2,Qt::ControlModifier);const int before=canvas.document()->history()->count();drag(view,cell(view,2,1),cell(view,0,1));
        QCOMPARE(frames(canvas,KeyframeProperty::PositionX),(QVector<int>{0,2,7}));QCOMPARE(frames(canvas,KeyframeProperty::PositionY),(QVector<int>{0,4,9}));QCOMPARE(canvas.document()->history()->count(),before);
        QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,2,1));move(view,cell(view,3,1));QVERIFY(controller.hasPendingEdits());controller.cancelEditing();QVERIFY(!controller.hasPendingEdits());QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,3,1));QCOMPARE(canvas.document()->history()->count(),before);
        QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,2,1));move(view,cell(view,3,1));controller.finishEditing();QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,3,1));
        QCOMPARE(frames(canvas,KeyframeProperty::PositionX),(QVector<int>{0,3,7}));QCOMPARE(frames(canvas,KeyframeProperty::PositionY),(QVector<int>{0,5,9}));QCOMPARE(canvas.document()->history()->count(),before+1);QVERIFY(!controller.hasPendingEdits());
    }
    void exposureBodyAndEdgesHaveSingleUndoAndRejectOverlap(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);QVERIFY(controller.addTrack(true,DrawingTemplate::Simple,"Character"));QVERIFY(controller.setExposureTiming(0,4));const QString first=canvas.shot().animationTracks.first().exposures.first().id;
        QVERIFY(controller.setFrame(8));QVERIFY(controller.newDrawing(false,2));QVERIFY(controller.setFrame(0));TimelinePanel panel(&controller);show(panel);auto *view=grid(panel);const int before=canvas.document()->history()->count();
        drag(view,cell(view,1,0),cell(view,4,0));QCOMPARE(exposure(canvas,first).startFrame,3);QCOMPARE(exposure(canvas,first).durationFrames,4);QCOMPARE(canvas.document()->history()->count(),before+1);
        drag(view,edge(view,7,0,-2),edge(view,8,0));QCOMPARE(exposure(canvas,first).startFrame,3);QCOMPARE(exposure(canvas,first).durationFrames,5);QCOMPARE(canvas.document()->history()->count(),before+2);
        drag(view,edge(view,3,0,2),edge(view,4,0,2));QCOMPARE(exposure(canvas,first).startFrame,4);QCOMPARE(exposure(canvas,first).durationFrames,4);QCOMPARE(canvas.document()->history()->count(),before+3);
        drag(view,cell(view,5,0),cell(view,7,0));QCOMPARE(exposure(canvas,first).startFrame,4);QCOMPARE(exposure(canvas,first).durationFrames,4);QCOMPARE(canvas.document()->history()->count(),before+3);
        canvas.document()->history()->undo();QCOMPARE(exposure(canvas,first).startFrame,3);QCOMPARE(exposure(canvas,first).durationFrames,5);
    }
    void singleFrameEndHandleAndPendingPanelDeletion(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);QVERIFY(controller.addTrack(true,DrawingTemplate::Simple,"Character"));const QString first=canvas.shot().animationTracks.first().exposures.first().id;
        auto *panel=new TimelinePanel(&controller);show(*panel);auto *view=grid(*panel);const int before=canvas.document()->history()->count();
        drag(view,edge(view,1,0),edge(view,4,0));QCOMPARE(exposure(canvas,first).durationFrames,4);QCOMPARE(canvas.document()->history()->count(),before+1);
        QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,1,0));move(view,cell(view,4,0));QVERIFY(controller.hasPendingEdits());delete panel;
        QVERIFY(!controller.hasPendingEdits());QCOMPARE(exposure(canvas,first).startFrame,0);QCOMPARE(canvas.document()->history()->count(),before+1);
    }
    void playbackRangeRulerHandlesAndEscapeDoNotChangeArtwork(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);auto *view=grid(panel);const int before=canvas.document()->history()->count();
        QPoint from=cell(view,3,0),to=cell(view,9,0);from.setY(10);to.setY(10);drag(view,from,to,Qt::ShiftModifier);
        QVERIFY(controller.playbackRangeEnabled());QCOMPARE(controller.playbackFirstFrame(),3);QCOMPARE(controller.playbackLastFrame(),9);QCOMPARE(canvas.document()->history()->count(),before);
        QPoint start=edge(view,3,0),startTo=edge(view,5,0);start.setY(10);startTo.setY(10);drag(view,start,startTo);QCOMPARE(controller.playbackFirstFrame(),5);
        QPoint end=edge(view,10,0),endTo=edge(view,13,0);end.setY(10);endTo.setY(10);drag(view,end,endTo);QCOMPARE(controller.playbackLastFrame(),12);
        QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::ShiftModifier,from);move(view,to,Qt::ShiftModifier);QTest::keyClick(view,Qt::Key_Escape);QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::ShiftModifier,to);
        QCOMPARE(controller.playbackFirstFrame(),5);QCOMPARE(controller.playbackLastFrame(),12);QVERIFY(!controller.hasPendingEdits());QCOMPARE(canvas.document()->history()->count(),before);
        panel.findChild<QToolButton *>("timelineRange")->click();QVERIFY(!controller.playbackRangeEnabled());panel.findChild<QToolButton *>("timelineRange")->click();QCOMPARE(controller.playbackFirstFrame(),5);QCOMPARE(controller.playbackLastFrame(),12);
    }
    void rangeSettingsRemainCompactAccessibleAndPrecise(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);TimelinePanel panel(&controller);panel.resize(240,360);panel.show();settle();QCOMPARE(panel.width(),240);
        for(auto *button:panel.findChildren<QToolButton *>())if(button->isVisibleTo(&panel)){QVERIFY(!button->toolTip().isEmpty());QVERIFY(!button->accessibleName().isEmpty());QCOMPARE(button->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,button),600);QVERIFY(QRect(button->mapTo(&panel,QPoint()),button->size()).right()<panel.width());}
        QTimer::singleShot(0,&panel,[&]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());QVERIFY(dialog);dialog->resize(240,200);settle();
            for(auto *spin:dialog->findChildren<QDoubleSpinBox *>()){QCOMPARE(spin->width(),48);QVERIFY(spin->suffix().isEmpty());}
            auto *first=dialog->findChild<ValueSlider *>("timelineRangeFirst");auto *last=dialog->findChild<ValueSlider *>("timelineRangeLast");QVERIFY(first);QVERIFY(last);
            first->findChild<QDoubleSpinBox *>()->setValue(4);last->findChild<QDoubleSpinBox *>()->setValue(12);dialog->accept();});
        panel.findChild<QToolButton *>("timelineRangeSettings")->click();QCOMPARE(controller.playbackFirstFrame(),3);QCOMPARE(controller.playbackLastFrame(),11);
    }
    void staleGestureDoesNotRecreateDeletedKeys(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);setupKeys(controller);TimelinePanel panel(&controller);show(panel);auto *view=grid(panel);const auto id=controller.selectedPropertyLayerId();
        QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,2,1));move(view,cell(view,5,1));QVERIFY(controller.hasPendingEdits());
        QVERIFY(canvas.document()->removeKeyframe(id,KeyframeProperty::PositionX,2));QVERIFY(!controller.hasPendingEdits());QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,5,1));
        QCOMPARE(frames(canvas,KeyframeProperty::PositionX),(QVector<int>{0,7}));
    }
    void pendingFieldResolutionReacquiresExposureBeforeDrag(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);QVERIFY(controller.addTrack(true,DrawingTemplate::Simple,"Character"));const auto first=canvas.shot().animationTracks.first().exposures.first().id;
        TimelinePanel panel(&controller);show(panel);auto *view=grid(panel);QObject draft;bool pending=true;const int before=canvas.document()->history()->count();
        connect(&controller,&TimelineController::editResolutionRequested,&draft,[&]{if(!pending)return;pending=false;QVERIFY(canvas.document()->setAnimationExposureTiming(0,4));controller.setPendingEdits(&draft,false);});controller.setPendingEdits(&draft,true);
        drag(view,cell(view,0,0),cell(view,2,0));QCOMPARE(exposure(canvas,first).startFrame,2);QCOMPARE(exposure(canvas,first).durationFrames,4);QCOMPARE(canvas.document()->history()->count(),before+2);QVERIFY(!controller.hasPendingEdits());
    }
    void drawingKeysOutsideTheirHoldStaySelectable(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);QVERIFY(controller.addTrack(true,DrawingTemplate::Simple,"Character"));QVERIFY(controller.setExposureTiming(0,4));const auto id=controller.selectedPropertyLayerId();
        QVERIFY(controller.setPropertyEnabled(KeyframeProperty::PositionX,true));QVERIFY(controller.setFrame(2));QVERIFY(canvas.document()->setKeyframeValue(id,KeyframeProperty::PositionX,20));QVERIFY(canvas.document()->moveKeyframe(id,KeyframeProperty::PositionX,2,10));QVERIFY(controller.setFrame(0));
        TimelinePanel panel(&controller);show(panel);auto *view=grid(panel);click(view,10,1);QCOMPARE(controller.currentFrame(),0);QCOMPARE(controller.selectedPropertyLayerId(),id);QCOMPARE(controller.selectedKeys().size(),1);
        drag(view,cell(view,10,1),cell(view,12,1));QCOMPARE(controller.currentFrame(),0);QCOMPARE(controller.selectedPropertyLayerId(),id);QCOMPARE(controller.selectedKeys().size(),1);QCOMPARE(frames(canvas,KeyframeProperty::PositionX),(QVector<int>{0,12}));
        QTest::keyClick(view,Qt::Key_Delete);QCOMPARE(frames(canvas,KeyframeProperty::PositionX),(QVector<int>{0}));
    }
    void lostContactAndWindowDeactivationCancelPreviews(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);setupKeys(controller);TimelinePanel panel(&controller);show(panel);auto *view=grid(panel);const int before=canvas.document()->history()->count();
        QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,2,1));move(view,cell(view,3,1));QVERIFY(controller.hasPendingEdits());
        const auto point=cell(view,3,1);QMouseEvent hover(QEvent::MouseMove,point,view->viewport()->mapToGlobal(point),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view->viewport(),&hover);QVERIFY(!controller.hasPendingEdits());controller.finishEditing();QCOMPARE(canvas.document()->history()->count(),before);
        QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,2,1));move(view,cell(view,3,1));QVERIFY(controller.hasPendingEdits());QEvent deactivate(QEvent::WindowDeactivate);QApplication::sendEvent(view,&deactivate);QVERIFY(!controller.hasPendingEdits());controller.finishEditing();QCOMPARE(canvas.document()->history()->count(),before);QCOMPARE(frames(canvas,KeyframeProperty::PositionX),(QVector<int>{0,2,7}));
    }
    void renderPreview(){
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");if(output.isEmpty())QSKIP("Opt-in timeline editing previews.");
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);setupKeys(controller);controller.setPlaybackRange(3,12);TimelinePanel panel(&controller);show(panel);auto *view=grid(panel);
        click(view,2,1);click(view,7,1,Qt::ControlModifier);click(view,9,2,Qt::ControlModifier);QDir().mkpath(output);QVERIFY(panel.grab().save(output+"/timeline-edit-wide.png"));
        panel.resize(240,360);settle();QVERIFY(panel.grab().save(output+"/timeline-edit-240.png"));
    }
};
QTEST_MAIN(TimelineEditPanelTests)
#include "timeline_edit_panel_tests.moc"
