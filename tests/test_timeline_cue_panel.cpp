#include "timeline_test_geometry.h"
#include "animation_test_fixture.h"
#include "canvas.h"
#include "document.h"
#include "timeline_controller.h"
#include "timeline_cue_panel.h"
#include "timeline_cues.h"
#include "timeline_panel.h"
#include "ui_theme.h"
#include "value_slider.h"

#include <QAbstractScrollArea>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QTextEdit>
#include <QToolButton>
#include <QtTest>

using namespace toon;
namespace {
TimelineCue cue(TimelineCueKind kind, int start, int hold, QString name) {
    TimelineCue result; result.id=animation_fixture::id(); result.kind=kind;
    result.startFrame=start; result.durationFrames=hold; result.name=name;
    if(kind==TimelineCueKind::Dialogue){result.speaker="Mika";result.text="We will meet at the station.";result.color=QColor("#86ad9c");}
    return result;
}
Project fixture(bool withCues=true) {
    auto project=animation_fixture::project();project.width=160;project.height=120;project.shots[0].durationFrames=60;
    if(withCues)project.shots[0].timelineCues={cue(TimelineCueKind::KeyPose,5,12,"Turn toward camera")};
    return project;
}
QAbstractScrollArea *grid(QWidget &panel){return panel.findChild<QAbstractScrollArea *>("timelineGrid");}
int label(QAbstractScrollArea *view){return timeline_test::labelWidth(view);}
QPoint cell(QAbstractScrollArea *view,int frame,int row=0){return {label(view)+frame*20+10-view->horizontalScrollBar()->value(),timeline_test::rowCenter(view,row)};}
QPoint edge(QAbstractScrollArea *view,int frame,int row=0){return cell(view,frame,row)-QPoint(10,0);}
void settle(){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);}
void show(QWidget &panel,QSize size={900,420}){
    settle();panel.resize(size);panel.show();QVERIFY(QTest::qWaitForWindowExposed(&panel));panel.raise();panel.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&panel));settle();
    if(auto *view=grid(panel)){view->horizontalScrollBar()->setValue(0);view->verticalScrollBar()->setValue(0);}
}
void move(QAbstractScrollArea *view,QPoint point){
    QMouseEvent event(QEvent::MouseMove,point,view->viewport()->mapToGlobal(point),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(view->viewport(),&event);
}
void drag(QAbstractScrollArea *view,QPoint from,QPoint to){
    QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,from);move(view,to);
    QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,to);
}
void click(QAbstractScrollArea *view,int frame,int row=0){QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,frame,row));}
bool contains(QWidget *parent,QWidget *child){return child&&parent->rect().contains(QRect(child->mapTo(parent,QPoint()),child->size()));}
}
class TimelineCuePanelTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void bodyMovePreviewsOnlyAndCommitsOnce(){
        const auto project=fixture();Canvas canvas;canvas.setProject(project);TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);
        auto *view=grid(panel);const auto original=canvas.shot().timelineCues[0];const auto from=cell(view,8),to=cell(view,12);
        QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,from);move(view,to);
        QCOMPARE(controller.selectedCueId(),original.id);QVERIFY(controller.hasPendingEdits());
        QCOMPARE(canvas.shot().timelineCues[0],original);QCOMPARE(canvas.document()->history()->count(),0);
        QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,to);
        QCOMPARE(canvas.shot().timelineCues[0].startFrame,9);QCOMPARE(canvas.shot().timelineCues[0].durationFrames,12);
        QCOMPARE(canvas.document()->history()->count(),1);QVERIFY(!controller.hasPendingEdits());
        QCOMPARE(canvas.shot().animationTracks,project.shots[0].animationTracks);
        canvas.document()->history()->undo();QCOMPARE(canvas.project().toJson(),project.toJson());
    }
    void rangeEdgesTrimAndUndo(){
        const auto project=fixture();Canvas canvas;canvas.setProject(project);TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);
        auto *view=grid(panel);drag(view,edge(view,5),edge(view,7));
        QCOMPARE(canvas.shot().timelineCues[0].startFrame,7);QCOMPARE(canvas.shot().timelineCues[0].durationFrames,10);QCOMPARE(canvas.document()->history()->count(),1);
        canvas.document()->history()->undo();drag(view,edge(view,17),edge(view,14));
        QCOMPARE(canvas.shot().timelineCues[0].startFrame,5);QCOMPARE(canvas.shot().timelineCues[0].durationFrames,9);QCOMPARE(canvas.document()->history()->count(),1);
        canvas.document()->history()->undo();QCOMPARE(canvas.project().toJson(),project.toJson());
    }
    void escapeInvalidAndWindowDeactivationDiscardPreview(){
        const auto project=fixture();Canvas canvas;canvas.setProject(project);TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);
        auto *view=grid(panel);const auto from=cell(view,8),to=cell(view,12);
        QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,from);move(view,to);QTest::keyClick(view,Qt::Key_Escape);
        QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,to);QCOMPARE(canvas.project().toJson(),project.toJson());QVERIFY(!controller.hasPendingEdits());
        QSignalSpy warning(canvas.document(),&Document::warning);drag(view,cell(view,8),cell(view,1));
        QVERIFY(!warning.isEmpty());QCOMPARE(canvas.project().toJson(),project.toJson());QCOMPARE(canvas.document()->history()->count(),0);
        QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,from);move(view,to);QEvent deactivate(QEvent::WindowDeactivate);QApplication::sendEvent(view,&deactivate);
        QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,to);QCOMPARE(canvas.project().toJson(),project.toJson());QVERIFY(!controller.hasPendingEdits());
    }
    void undoDuringDragNeverRestoresStaleCue(){
        auto project=fixture(false);Canvas canvas;canvas.setProject(project);TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);
        QVERIFY(controller.setFrame(5));const auto id=controller.addCue(TimelineCueKind::Marker);QVERIFY(!id.isEmpty());auto *view=grid(panel);
        QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,5));move(view,cell(view,9));
        canvas.document()->history()->undo();QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,9));
        QVERIFY(canvas.shot().timelineCues.isEmpty());QVERIFY(!controller.hasPendingEdits());QCOMPARE(canvas.project().toJson(),project.toJson());
    }
    void overlappingRangesHaveSeparateSelectableLanes(){
        auto project=fixture();auto &cues=project.shots[0].timelineCues;
        cues.append(cue(TimelineCueKind::Breakdown,8,6,"Hand settles"));cues.append(cue(TimelineCueKind::Marker,25,1,"Cut"));
        cues.append(cue(TimelineCueKind::Dialogue,8,18,"First line"));cues.append(cue(TimelineCueKind::Dialogue,10,2,"Overlapping reply"));
        Canvas canvas;canvas.setProject(project);TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);auto *view=grid(panel);
        click(view,9,0);QCOMPARE(controller.selectedCueId(),cues[0].id);
        click(view,9,1);QCOMPARE(controller.selectedCueId(),cues[1].id);
        click(view,25,1);QCOMPARE(controller.selectedCueId(),cues[2].id);QCOMPARE(controller.currentFrame(),25);
        click(view,11,2);QCOMPARE(controller.selectedCueId(),cues[3].id);
        click(view,11,3);QCOMPARE(controller.selectedCueId(),cues[4].id);
        QCOMPARE(canvas.document()->history()->count(),0);
    }
    void clipboardSelectAllAndDeleteStayInCueDomain(){
        const auto project=fixture();Canvas canvas;canvas.setProject(project);TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);
        const auto &track=project.shots[0].animationTracks[0];QVERIFY(controller.selectTrack(track.id));
        controller.setSelectedCels({{track.id,track.exposures[0].id}});controller.copySelectedCels();QVERIFY(controller.canPasteCels());
        auto *view=grid(panel);click(view,8);QVERIFY(controller.selectedCels().isEmpty());QVERIFY(controller.selectedKeys().isEmpty());
        QTest::keySequence(view,QKeySequence::SelectAll);QTest::keySequence(view,QKeySequence::Copy);QTest::keySequence(view,QKeySequence::Paste);
        QVERIFY(controller.selectedCels().isEmpty());QVERIFY(controller.selectedKeys().isEmpty());QVERIFY(controller.canPasteCels());QCOMPARE(canvas.project().toJson(),project.toJson());
        QTest::keyClick(view,Qt::Key_Right,Qt::AltModifier);QCOMPARE(canvas.shot().timelineCues[0].startFrame,6);
        QTest::keyClick(view,Qt::Key_Delete);QVERIFY(canvas.shot().timelineCues.isEmpty());
        QCOMPARE(canvas.shot().animationTracks,project.shots[0].animationTracks);QCOMPARE(canvas.shot().layers.size(),project.shots[0].layers.size());
        canvas.document()->history()->undo();canvas.document()->history()->undo();QCOMPARE(canvas.project().toJson(),project.toJson());
    }
    void markerShortcutAndAddMenuUsePlayhead(){
        auto project=fixture(false);Canvas canvas;canvas.setProject(project);TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);
        auto *view=grid(panel);QVERIFY(controller.setFrame(9));view->setFocus();QTest::keyClick(view,Qt::Key_M);
        QCOMPARE(canvas.shot().timelineCues.size(),1);QCOMPARE(canvas.shot().timelineCues[0].kind,TimelineCueKind::Marker);
        QCOMPARE(canvas.shot().timelineCues[0].startFrame,9);QCOMPARE(canvas.shot().timelineCues[0].durationFrames,1);
        auto *button=panel.findChild<QToolButton *>("timelineAddCue");QVERIFY(button);QVERIFY(button->menu());QCOMPARE(button->menu()->actions().size(),5);
        for(auto *action:button->menu()->actions()){QVERIFY(!action->icon().isNull());QVERIFY(!action->text().isEmpty());}
        auto *dialogue=button->menu()->findChild<QAction *>("timelineAddCue_dialogue");QVERIFY(dialogue);dialogue->trigger();
        QCOMPARE(canvas.shot().timelineCues.size(),2);QCOMPARE(canvas.shot().timelineCues[1].kind,TimelineCueKind::Dialogue);
        QCOMPARE(canvas.shot().timelineCues[1].startFrame,9);QCOMPARE(canvas.shot().timelineCues[1].durationFrames,qRound(project.fps));
        FramePropertiesPanel properties(&controller);show(properties,{240,700});auto *name=properties.findChild<QLineEdit *>("cueName");QVERIFY(name);
        name->setFocus();QTest::keyClicks(name,"m");QCOMPARE(canvas.shot().timelineCues.size(),2);
        QWidget *text=properties.findChild<QPlainTextEdit *>("cueText");if(!text)text=properties.findChild<QTextEdit *>("cueText");QVERIFY(text);
        text->setFocus();QTest::keyClicks(text,"m");QCOMPARE(canvas.shot().timelineCues.size(),2);
    }
    void fitCueRangeAndSelectionNeverCreateHistory(){
        const auto project=fixture();Canvas canvas;canvas.setProject(project);TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);
        auto *view=grid(panel);click(view,8);const int frame=controller.currentFrame();const auto id=controller.selectedCueId();
        panel.findChild<QToolButton *>("timelineFitSelection")->click();const qreal cellWidth=panel.findChild<ValueSlider *>("timelineZoom")->value()/5;
        const int left=label(view)+qRound(5*cellWidth)-view->horizontalScrollBar()->value(),right=label(view)+qRound(17*cellWidth)-view->horizontalScrollBar()->value();
        QVERIFY(left>=label(view));QVERIFY(right<=view->viewport()->width());QVERIFY(cellWidth>20);
        QCOMPARE(controller.currentFrame(),frame);QCOMPARE(controller.selectedCueId(),id);QCOMPARE(canvas.document()->history()->count(),0);QCOMPARE(canvas.project().toJson(),project.toJson());
    }
    void noCueRowsPreserveExistingDrawingCoordinates(){
        const auto project=fixture(false);Canvas canvas;canvas.setProject(project);TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);
        click(grid(panel),1,0);QCOMPARE(controller.selectedTrackId(),project.shots[0].animationTracks[0].id);
        QCOMPARE(controller.selectedCels().size(),1);QCOMPARE(controller.selectedCels()[0].exposureId,project.shots[0].animationTracks[0].exposures[0].id);
        QVERIFY(controller.selectedCueId().isEmpty());
    }
    void sixthTabOpensOnlyForExplicitRequestAndFitsNarrowWidth(){
        const auto project=fixture();Canvas canvas;canvas.setProject(project);TimelineController controller(&canvas);
        FramePropertiesPanel properties(&controller);show(properties,{240,760});auto *tabs=properties.findChild<QTabBar *>("framePropertiesTabs");QVERIFY(tabs);
        QCOMPARE(tabs->count(),6);QCOMPARE(tabs->accessibleTabName(4),QString("Symbol clip properties"));QCOMPARE(tabs->accessibleTabName(5),QString("Cue properties"));
        tabs->setCurrentIndex(0);QVERIFY(controller.selectCue(project.shots[0].timelineCues[0].id));QCOMPARE(tabs->currentIndex(),0);
        emit controller.cuePropertiesRequested();QCOMPARE(tabs->currentIndex(),5);QVERIFY(properties.findChild<TimelineCuePanel *>()->isVisibleTo(&properties));
        QVERIFY(controller.setFrame(12));QCOMPARE(tabs->currentIndex(),5);
        auto font=properties.font();font.setPointSizeF(font.pointSizeF()+3);properties.setFont(font);settle();
        QCOMPARE(properties.width(),240);QVERIFY(contains(&properties,tabs));
        for(const char *id:{"cueStart","cueHold"}){auto *field=properties.findChild<ValueSlider *>(id);QVERIFY(field);QVERIFY(contains(&properties,field));
            const auto *number=field->findChild<QDoubleSpinBox *>();QVERIFY(number);QCOMPARE(number->width(),48);QVERIFY(number->suffix().isEmpty());}
        for(auto *button:properties.findChild<TimelineCuePanel *>()->findChildren<QToolButton *>())if(button->isVisibleTo(&properties)){
            QVERIFY(!button->accessibleName().isEmpty());QVERIFY(!button->toolTip().isEmpty());QCOMPARE(button->iconSize(),QSize(16,16));
            QCOMPARE(button->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,button),600);}
    }
    void doubleClickRequestsCuePropertiesWithoutDialog(){
        const auto project=fixture();Canvas canvas;canvas.setProject(project);TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);
        QSignalSpy request(&controller,&TimelineController::cuePropertiesRequested);QTest::mouseDClick(grid(panel)->viewport(),Qt::LeftButton,Qt::NoModifier,cell(grid(panel),8));
        QCOMPARE(request.size(),1);QVERIFY(!QApplication::activeModalWidget());QCOMPARE(canvas.project().toJson(),project.toJson());
    }
    void symbolEditorCannotCreateShotNotes(){
        const auto project=fixture(false);Canvas canvas;canvas.setProject(project);TimelineController controller(&canvas);controller.setProperty("symbolEditor",true);
        TimelinePanel panel(&controller);show(panel);QVERIFY(!panel.findChild<QToolButton *>("timelineAddCue")->isVisible());QTest::keyClick(grid(panel),Qt::Key_M);
        QVERIFY(canvas.shot().timelineCues.isEmpty());FramePropertiesPanel properties(&controller);QVERIFY(!properties.findChild<QTabBar *>("framePropertiesTabs")->isTabVisible(5));
    }
    void renderPreview(){
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");if(output.isEmpty())QSKIP("Opt-in cue timeline previews.");
        auto project=fixture();auto &cues=project.shots[0].timelineCues;
        cues.append(cue(TimelineCueKind::Breakdown,9,6,"Eyes lead the turn"));cues.append(cue(TimelineCueKind::Marker,25,1,"Cut"));
        cues.append(cue(TimelineCueKind::Dialogue,8,18,"At the station"));cues.append(cue(TimelineCueKind::Dialogue,13,8,"Quiet reply"));
        Canvas canvas;canvas.setProject(project);TimelineController controller(&canvas);QWidget workspace;auto *layout=new QHBoxLayout(&workspace);
        TimelinePanel timeline(&controller,&workspace);FramePropertiesPanel properties(&controller,&workspace);properties.setFixedWidth(300);layout->addWidget(&timeline,1);layout->addWidget(&properties);
        show(workspace,{1200,760});QVERIFY(controller.selectCue(cues[3].id));emit controller.cuePropertiesRequested();settle();QDir().mkpath(output);
        QVERIFY(workspace.grab().save(output+"/timeline-cues-workspace.png"));
        auto *button=timeline.findChild<QToolButton *>("timelineAddCue");button->menu()->popup(button->mapToGlobal(QPoint(0,button->height())));
        QVERIFY(QTest::qWaitForWindowExposed(button->menu()));QVERIFY(button->menu()->grab().save(output+"/timeline-cues-add-menu.png"));button->menu()->close();settle();
        properties.setFixedWidth(240);auto font=properties.font();font.setPointSizeF(font.pointSizeF()+3);properties.setFont(font);settle();
        QVERIFY(properties.grab().save(output+"/timeline-cues-properties-240-large.png"));
    }
};
QTEST_MAIN(TimelineCuePanelTests)
#include "test_timeline_cue_panel.moc"
