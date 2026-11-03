#include "timeline_test_geometry.h"
#include "animation.h"
#include "drawing_template_dialog.h"
#include <QComboBox>
#include "canvas.h"
#include "timeline_controller.h"
#include "timeline_panel.h"
#include "value_slider.h"
#include "ui_theme.h"
#include <QAbstractScrollArea>
#include <QAction>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QSlider>
#include <QStyle>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtTest>

using namespace toon;
namespace {
Project smallProject(){auto project=Project::create();project.width=480;project.height=320;project.shots[0].durationFrames=48;return project;}
QToolButton *button(QWidget &panel,const char *name){return panel.findChild<QToolButton *>(name);}
ValueSlider *value(QWidget &panel,const char *name){return panel.findChild<ValueSlider *>(name);}
QDoubleSpinBox *number(QWidget &panel,const char *name){return value(panel,name)->findChild<QDoubleSpinBox *>();}
const AnimationTrack *track(const Canvas &canvas,const QString &id){for(const auto &entry:canvas.shot().animationTracks)if(entry.id==id)return &entry;return nullptr;}
const RasterLayer *layer(const Canvas &canvas,const QString &id){for(const auto &entry:canvas.shot().layers)if(entry.id==id)return &entry;return nullptr;}
void settle(){for(int i=0;i<4;++i)QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);}
}
class TimelinePanelTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void addTrackDialogUsesRealTemplates(){
        Canvas canvas;canvas.setProject(smallProject());TimelineController controller(&canvas);TimelinePanel panel(&controller);
        QTimer::singleShot(0,&panel,[]{
            auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());QVERIFY(dialog);QCOMPARE(dialog->objectName(),QString("addAnimationTrackDialog"));
            dialog->findChild<QLineEdit *>("animationTrackName")->setText("Character");
            auto *templates=dialog->findChild<QComboBox *>("animationDrawingTemplate");QVERIFY(templates->count()>=3);templates->setCurrentIndex(2);
            QVERIFY(dialog->findChild<DrawingTemplateWidget *>()->templateSlots().size()==6);
            dialog->findChild<QPushButton *>("confirmAnimationTrack")->click();
        });button(panel,"timelineAddTrack")->click();QCOMPARE(canvas.shot().animationTracks.size(),1);
        const auto &created=canvas.shot().animationTracks.first();QCOMPARE(created.drawingTemplate.size(),6);QCOMPARE(created.drawings.size(),1);QCOMPARE(created.exposures.size(),1);
        QCOMPARE(layer(canvas,created.rootGroupLayerId)->name,QString("Character"));QCOMPARE(created.drawings.first().templateLayers.size(),6);
        QTimer::singleShot(0,&panel,[]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());QVERIFY(dialog);
            dialog->findChild<QTabBar *>("animationTrackType")->setCurrentIndex(0);dialog->findChild<QLineEdit *>("animationTrackName")->setText("Background");
            QVERIFY(!dialog->findChild<QComboBox *>("animationDrawingTemplate")->isVisible());dialog->findChild<QPushButton *>("confirmAnimationTrack")->click();});
        button(panel,"timelineAddTrack")->click();QCOMPARE(canvas.shot().animationTracks.size(),1);QVERIFY(layer(canvas,controller.selectedTrackId()));
        QCOMPARE(layer(canvas,controller.selectedTrackId())->name,QString("Background"));QVERIFY(!button(panel,"timelineNewDrawing")->isEnabled());
    }
    void scrubbingAndNavigationRemainTransient(){
        Canvas canvas;canvas.setProject(smallProject());TimelineController controller(&canvas);QVERIFY(controller.addTrack(true,DrawingTemplate::Simple,"Character"));
        const auto id=controller.selectedTrackId();QVERIFY(controller.setExposureTiming(0,3));QVERIFY(controller.setFrame(8));QVERIFY(controller.newDrawing(false,2));QVERIFY(controller.setFrame(0));
        TimelinePanel panel(&controller);panel.resize(600,240);panel.show();settle();auto *history=canvas.document()->history();history->setClean();const auto before=canvas.project().toJson();const int count=history->count();
        auto *grid=panel.findChild<QAbstractScrollArea *>("timelineGrid");QVERIFY(grid);QCOMPARE(grid->horizontalScrollBar()->value(),0);
        const int labels=timeline_test::labelWidth(grid);
        QTest::mouseClick(grid->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(labels+4*20+10,12));QCOMPARE(controller.currentFrame(),4);
        button(panel,"timelinePreviousDrawing")->click();QCOMPARE(controller.currentFrame(),0);button(panel,"timelineNextDrawing")->click();QCOMPARE(controller.currentFrame(),8);
        button(panel,"timelinePreviousFrame")->click();QCOMPARE(controller.currentFrame(),7);button(panel,"timelineNextFrame")->click();QCOMPARE(controller.currentFrame(),8);
        number(panel,"timelineCurrentFrame")->setValue(16);QCOMPARE(controller.currentFrame(),15);
        auto *slider=value(panel,"timelineCurrentFrame")->findChild<QSlider *>();slider->setSliderDown(true);slider->setValue(20);QVERIFY(slider->isSliderDown());slider->setValue(21);slider->setSliderDown(false);QCOMPARE(controller.currentFrame(),20);
        QTest::keyClick(grid,Qt::Key_Left);QCOMPARE(controller.currentFrame(),19);button(panel,"timelineLoop")->click();button(panel,"timelineOnion")->click();
        QCOMPARE(controller.selectedTrackId(),id);QCOMPARE(canvas.project().toJson(),before);QCOMPARE(history->count(),count);QVERIFY(history->isClean());
    }
    void playbackQualityMenuIsAccessibleAndDoesNotEditTheProject(){
        Canvas canvas;canvas.setProject(smallProject());TimelineController controller(&canvas);TimelinePanel panel(&controller);
        panel.resize(240,300);panel.show();settle();auto *quality=button(panel,"timelinePreviewQuality");QVERIFY(quality);QVERIFY(quality->menu());
        QCOMPARE(controller.playbackPreviewDivisor(),1);QCOMPARE(canvas.playbackPreviewDivisor(),1);
        QVERIFY(!quality->toolTip().isEmpty());QVERIFY(!quality->accessibleName().isEmpty());QCOMPARE(quality->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,quality),600);
        QVERIFY(QRect(quality->mapTo(&panel,QPoint()),quality->size()).right()<panel.width());
        const auto original=canvas.project().toJson();const int history=canvas.document()->history()->count();
        const QStringList names{"timelinePreviewFull","timelinePreviewHalf","timelinePreviewQuarter","timelinePreviewEighth"};
        for(int i=0;i<names.size();++i){
            const int divisor=1<<i;auto *action=quality->menu()->findChild<QAction *>(names[i]);QVERIFY(action);QVERIFY(action->isCheckable());QCOMPARE(action->data().toInt(),divisor);
            action->trigger();QCOMPARE(controller.playbackPreviewDivisor(),divisor);QCOMPARE(canvas.playbackPreviewDivisor(),divisor);QVERIFY(action->isChecked());
            int checked=0;for(auto *candidate:quality->menu()->actions())checked+=candidate->isCheckable()&&candidate->isChecked();QCOMPARE(checked,1);
        }
        canvas.setPlaybackPreviewDivisor(2);QVERIFY(quality->menu()->findChild<QAction *>("timelinePreviewHalf")->isChecked());
        controller.setPlaying(true);QVERIFY(controller.playing());quality->menu()->findChild<QAction *>("timelinePreviewQuarter")->trigger();
        QVERIFY(controller.playing());QCOMPARE(controller.playbackPreviewDivisor(),4);controller.setPlaying(false);
        QCOMPARE(canvas.project().toJson(),original);QCOMPARE(canvas.document()->history()->count(),history);
    }
    void playbackQualitySelectionPreservesUncommittedTimingDraft(){
        Canvas canvas;canvas.setProject(smallProject());TimelineController controller(&canvas);QVERIFY(controller.addTrack(true,DrawingTemplate::Simple,"Character"));
        const auto trackId=controller.selectedTrackId();QWidget workspace;auto *layout=new QHBoxLayout(&workspace);
        TimelinePanel panel(&controller,&workspace);FramePropertiesPanel properties(&controller,&workspace);layout->addWidget(&panel,2);layout->addWidget(&properties,1);
        // Earlier cases rapidly open and destroy native top-level panels.
        // Let their activation events drain, then activate this exposed host
        // before typing; direct key delivery alone does not establish focus.
        QCoreApplication::processEvents();workspace.resize(860,340);workspace.show();
        QVERIFY(QTest::qWaitForWindowExposed(&workspace));workspace.raise();workspace.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&workspace));settle();
        auto *hold=number(properties,"frameExposureHold");hold->setFocus();QTRY_VERIFY(hold->hasFocus());hold->selectAll();QTest::keyClicks(hold,"8");
        QVERIFY(controller.hasPendingEdits());const int history=canvas.document()->history()->count();
        auto *quality=button(panel,"timelinePreviewQuality");QVERIFY(quality);QVERIFY(quality->menu());
        QTimer::singleShot(0,&panel,[quality]{auto *menu=quality->menu();QVERIFY(menu->isVisible());auto *action=menu->findChild<QAction *>("timelinePreviewQuarter");QVERIFY(action);action->trigger();menu->close();});
        QTest::mouseClick(quality,Qt::LeftButton);
        QCOMPARE(controller.playbackPreviewDivisor(),4);QCOMPARE(hold->findChild<QLineEdit *>()->text(),QString("8"));QVERIFY(controller.hasPendingEdits());
        QTRY_VERIFY(hold->hasFocus());
        QCOMPARE(track(canvas,trackId)->exposures.first().durationFrames,1);QCOMPARE(canvas.document()->history()->count(),history);
        controller.cancelEditing();QCOMPARE(hold->value(),1.);QVERIFY(!controller.hasPendingEdits());QCOMPARE(canvas.document()->history()->count(),history);
    }
    void zoomPreservesPlayheadAndWheelAnchorWithoutDocumentChanges(){
        auto project=smallProject();project.shots[0].durationFrames=2400;
        Canvas canvas;canvas.setProject(project);TimelineController controller(&canvas);TimelinePanel panel(&controller);
        panel.resize(800,300);panel.show();settle();QVERIFY(controller.setFrame(1200));
        auto *grid=panel.findChild<QAbstractScrollArea *>("timelineGrid");auto *scroll=grid->horizontalScrollBar();
        const int labels=timeline_test::labelWidth(grid);
        const auto before=canvas.project().toJson();const int history=canvas.document()->history()->count();
        const qreal playhead=labels+1200.5*20-scroll->value();
        value(panel,"timelineZoom")->setValue(200);
        QVERIFY(std::abs(labels+1200.5*40-scroll->value()-playhead)<=1);
        QCOMPARE(controller.currentFrame(),1200);
        const QPointF at(labels+80,12);const qreal anchor=(at.x()-labels+scroll->value())/40;
        QWheelEvent wheel(at,grid->viewport()->mapToGlobal(at.toPoint()),{},QPoint(0,120),Qt::NoButton,Qt::ControlModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(grid->viewport(),&wheel);
        QCOMPARE(value(panel,"timelineZoom")->value(),250.0);
        QVERIFY(std::abs((at.x()-labels+scroll->value())/50-anchor)<=.02);
        const int expected=int(std::floor((at.x()-labels+scroll->value())/50));
        QTest::mouseClick(grid->viewport(),Qt::LeftButton,Qt::NoModifier,at.toPoint());QCOMPARE(controller.currentFrame(),expected);
        QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),history);
    }
    void fitLongShotAndSelectedRangeRemainUsableAtNarrowWidths(){
        auto project=smallProject();project.shots[0].durationFrames=1000000;
        Canvas canvas;canvas.setProject(project);TimelineController controller(&canvas);TimelinePanel panel(&controller);
        panel.resize(800,300);panel.show();settle();QVERIFY(controller.setFrame(500000));
        auto *grid=panel.findChild<QAbstractScrollArea *>("timelineGrid");
        button(panel,"timelineFitShot")->click();settle();QCOMPARE(grid->horizontalScrollBar()->maximum(),0);
        QVERIFY(value(panel,"timelineZoom")->value()<1);QCOMPARE(controller.currentFrame(),500000);
        panel.resize(240,300);settle();QCOMPARE(grid->horizontalScrollBar()->maximum(),0);
        // Fractional frame widths must render without one grid line per frame.
        QVERIFY(!grid->grab().isNull());
        controller.setPlaybackRange(499990,500010);controller.setPlaybackRangeEnabled(true);
        button(panel,"timelineFitSelection")->click();settle();
        const qreal width=value(panel,"timelineZoom")->value()/5;
        const int labels=timeline_test::labelWidth(grid);
        QVERIFY(499990*width-grid->horizontalScrollBar()->value()>=-2);
        QVERIFY(500011*width-grid->horizontalScrollBar()->value()<=grid->viewport()->width()-labels+2);
        QCOMPARE(controller.currentFrame(),500000);QVERIFY(canvas.document()->history()->isClean());
    }
    void fitSelectedKeysAndDrawingDoesNotChangeSelection(){
        Canvas canvas;canvas.setProject(smallProject());TimelineController controller(&canvas);
        QVERIFY(controller.addTrack(true,DrawingTemplate::Simple,"Character"));QVERIFY(controller.setExposureTiming(0,12));
        TimelinePanel panel(&controller);panel.resize(600,300);panel.show();settle();
        button(panel,"timelineFitSelection")->click();QVERIFY(value(panel,"timelineZoom")->value()>100);
        const auto id=controller.selectedPropertyLayerId();QVERIFY(!id.isEmpty());
        controller.setPropertyEnabled(KeyframeProperty::PositionX,true);QVERIFY(controller.setFrame(10));controller.setPropertyKey(KeyframeProperty::PositionX);
        controller.setSelectedKeys({{id,KeyframeProperty::PositionX,{},0},{id,KeyframeProperty::PositionX,{},10}});
        const auto selected=controller.selectedKeys();QCOMPARE(selected.size(),2);const int history=canvas.document()->history()->count();
        button(panel,"timelineFitSelection")->click();QCOMPARE(controller.selectedKeys(),selected);QCOMPARE(controller.currentFrame(),10);
        const qreal expected=std::min(qreal(400),qreal(panel.findChild<QAbstractScrollArea *>("timelineGrid")->viewport()->width()-timeline_test::labelWidth(panel.findChild<QAbstractScrollArea *>("timelineGrid"))-33)/11*5);
        QVERIFY(std::abs(value(panel,"timelineZoom")->value()-expected)<.01);QCOMPARE(canvas.document()->history()->count(),history);
    }
    void fractionalZoomSelectsPaintedKeysAndShortCelsStillMove(){
        Canvas canvas;canvas.setProject(smallProject());TimelineController controller(&canvas);
        QVERIFY(controller.addTrack(true,DrawingTemplate::Simple,"Character"));QVERIFY(controller.setExposureTiming(0,12));
        const auto id=controller.selectedPropertyLayerId();QVERIFY(controller.setPropertyEnabled(KeyframeProperty::PositionX,true));
        QVERIFY(controller.setFrame(2));QVERIFY(controller.setPropertyKey(KeyframeProperty::PositionX));controller.clearKeySelection();
        TimelinePanel panel(&controller);panel.resize(600,300);panel.show();settle();
        auto *grid=panel.findChild<QAbstractScrollArea *>("timelineGrid");
        const int labels=timeline_test::labelWidth(grid);
        value(panel,"timelineZoom")->setValue(5.5);grid->verticalScrollBar()->setValue(0);
        QTest::mouseClick(grid->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(labels+2,timeline_test::rowCenter(grid,1)));
        QVERIFY(controller.selectedKeys().contains({id,KeyframeProperty::PositionX,{},2}));QCOMPARE(controller.currentFrame(),2);
        // Zoomed-out one-frame cels reserve their center for moving, not trim.
        QVERIFY(controller.setFrame(0));QVERIFY(controller.setExposureTiming(0,1));controller.clearKeySelection();
        value(panel,"timelineZoom")->setValue(50);grid->horizontalScrollBar()->setValue(0);grid->verticalScrollBar()->setValue(0);
        const QPoint from(labels+5,timeline_test::rowCenter(grid,0)),to=from+QPoint(30,0);
        QTest::mousePress(grid->viewport(),Qt::LeftButton,Qt::NoModifier,from);
        value(panel,"timelineZoom")->setValue(200);QCOMPARE(value(panel,"timelineZoom")->value(),50.0);
        QMouseEvent move(QEvent::MouseMove,to,grid->viewport()->mapToGlobal(to),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(grid->viewport(),&move);QTest::mouseRelease(grid->viewport(),Qt::LeftButton,Qt::NoModifier,to);
        const auto *changed=track(canvas,controller.selectedTrackId());QVERIFY(changed);QCOMPARE(changed->exposures.first().startFrame,3);QCOMPARE(changed->exposures.first().durationFrames,1);
    }
    void blankDuplicateRemoveAndUndoUseIndependentDrawings(){
        Canvas canvas;canvas.setProject(smallProject());TimelineController controller(&canvas);QVERIFY(controller.addTrack(true,DrawingTemplate::Rough,"Character"));
        const auto id=controller.selectedTrackId();const auto original=track(canvas,id)->drawings.first();TimelinePanel panel(&controller);
        QVERIFY(controller.setFrame(3));QVERIFY(button(panel,"timelineDuplicateDrawing")->isEnabled());button(panel,"timelineDuplicateDrawing")->click();
        QCOMPARE(track(canvas,id)->drawings.size(),2);const auto duplicate=*drawingAtFrame(*track(canvas,id),3);QVERIFY(duplicate.id!=original.id);QVERIFY(duplicate.groupLayerId!=original.groupLayerId);
        for(auto it=original.templateLayers.cbegin();it!=original.templateLayers.cend();++it)QVERIFY(duplicate.templateLayers.value(it.key())!=it.value());
        QVERIFY(controller.setFrame(6));button(panel,"timelineNewDrawing")->click();QCOMPARE(track(canvas,id)->drawings.size(),3);QVERIFY(exposureAtFrame(*track(canvas,id),6));
        button(panel,"timelineRemoveExposure")->click();QVERIFY(!exposureAtFrame(*track(canvas,id),6));QVERIFY(!button(panel,"timelineRemoveExposure")->isEnabled());
        canvas.document()->history()->undo();QVERIFY(exposureAtFrame(*track(canvas,id),6));
    }
    void timingDraftsSurviveRefreshAndCommitOnce(){
        Canvas canvas;canvas.setProject(smallProject());TimelineController controller(&canvas);QVERIFY(controller.addTrack(true,DrawingTemplate::Simple,"Character"));
        const auto id=controller.selectedTrackId();FramePropertiesPanel panel(&controller);panel.resize(240,260);panel.show();settle();auto *history=canvas.document()->history();
        auto *field=value(panel,"frameExposureHold");auto *spin=field->findChild<QDoubleSpinBox *>();spin->setFocus();spin->selectAll();QTest::keyClicks(spin,"5");
        const int initial=history->count();for(int i=0;i<3;++i){QVERIFY(QMetaObject::invokeMethod(&controller,"changed"));QCOMPARE(spin->findChild<QLineEdit *>()->text(),QString("5"));}
        QCOMPARE(track(canvas,id)->exposures.first().durationFrames,1);QCOMPARE(history->count(),initial);
        QTest::keyClick(spin,Qt::Key_Return);QCOMPARE(track(canvas,id)->exposures.first().durationFrames,5);QCOMPARE(history->count(),initial+1);
        auto *slider=field->findChild<QSlider *>();slider->setSliderDown(true);slider->setValue(6);slider->setValue(7);QCOMPARE(track(canvas,id)->exposures.first().durationFrames,5);
        slider->setSliderDown(false);QCOMPARE(track(canvas,id)->exposures.first().durationFrames,7);QCOMPARE(history->count(),initial+2);
        slider->setSliderDown(true);slider->setValue(9);field->cancelEditing();QCOMPARE(field->value(),7.0);QCOMPARE(track(canvas,id)->exposures.first().durationFrames,7);QCOMPARE(history->count(),initial+2);
        spin->setFocus();spin->selectAll();QTest::keyClicks(spin,"11");controller.finishEditing();QCOMPARE(track(canvas,id)->exposures.first().durationFrames,11);QCOMPARE(history->count(),initial+3);
        QTemporaryDir directory;QString error;const auto path=directory.filePath("timeline.toonproj");QVERIFY2(saveProject(canvas.project(),path,&error),qPrintable(error));
        const auto loaded=loadProject(path,&error);QVERIFY2(loaded.has_value(),qPrintable(error));QCOMPARE(loaded->shots.first().animationTracks.first().exposures.first().durationFrames,11);
        spin->clearFocus();QCoreApplication::processEvents();QCOMPARE(history->count(),initial+3);
    }
    void timingCollisionRestoresFieldsAndSelectionRefreshHasNoStaleDraft(){
        Canvas canvas;canvas.setProject(smallProject());TimelineController controller(&canvas);QVERIFY(controller.addTrack(true,DrawingTemplate::Simple,"Character"));const auto id=controller.selectedTrackId();
        QVERIFY(controller.setExposureTiming(0,3));QVERIFY(controller.setFrame(5));QVERIFY(controller.newDrawing(false,2));QVERIFY(controller.setFrame(0));
        FramePropertiesPanel panel(&controller);panel.show();auto *history=canvas.document()->history();const int initial=history->count();
        auto *hold=number(panel,"frameExposureHold");hold->setFocus();hold->selectAll();QTest::keyClicks(hold,"8");QTest::keyClick(hold,Qt::Key_Return);
        QCOMPARE(hold->value(),3.0);QCOMPARE(track(canvas,id)->exposures.first().durationFrames,3);QCOMPARE(history->count(),initial);
        hold->selectAll();QTest::keyClicks(hold,"4");QVERIFY(controller.setFrame(5));QCOMPARE(track(canvas,id)->exposures.first().durationFrames,4);
        QCOMPARE(number(panel,"frameExposureStart")->value(),6.0);QCOMPARE(hold->value(),2.0);hold->clearFocus();QCoreApplication::processEvents();QCOMPARE(track(canvas,id)->exposures.last().durationFrames,2);
        auto *name=panel.findChild<QLineEdit *>("frameDrawingName");name->setFocus();name->selectAll();QTest::keyClicks(name,"A002 clean");
        QVERIFY(QMetaObject::invokeMethod(&controller,"changed"));QCOMPARE(name->text(),QString("A002 clean"));controller.finishEditing();
        const auto *drawing=drawingAtFrame(*track(canvas,id),5);QVERIFY(drawing);QCOMPARE(layer(canvas,drawing->groupLayerId)->name,QString("A002 clean"));
        QVERIFY(controller.setFrame(20));QVERIFY(!name->isEnabled());QVERIFY(!value(panel,"frameExposureStart")->isEnabled());QVERIFY(name->text().isEmpty());
        QVERIFY(controller.setFrame(5));QCOMPARE(name->text(),QString("A002 clean"));
    }
    void cleanProjectDraftCanCancelWithoutHistory_data(){
        QTest::addColumn<QString>("editor");QTest::newRow("hold")<<QString("hold");QTest::newRow("start")<<QString("start");QTest::newRow("name")<<QString("name");
    }
    void cleanProjectDraftCanCancelWithoutHistory(){
        QFETCH(QString,editor);QTemporaryDir directory;const auto path=directory.filePath("draft.toonproj");QString error;
        Canvas canvas;canvas.setProject(smallProject());TimelineController controller(&canvas);QVERIFY(controller.addTrack(true,DrawingTemplate::Simple,"Character"));const auto id=controller.selectedTrackId();
        QVERIFY2(saveProject(canvas.project(),path,&error),qPrintable(error));const auto loaded=loadProject(path,&error);QVERIFY2(loaded.has_value(),qPrintable(error));canvas.setProject(*loaded);QVERIFY(controller.selectTrack(id));
        FramePropertiesPanel panel(&controller);panel.resize(240,260);panel.show();settle();auto *history=canvas.document()->history();QCOMPARE(history->count(),0);QVERIFY(history->isClean());
        const auto before=canvas.project().toJson();QWidget *target=nullptr;
        if(editor=="name")target=panel.findChild<QLineEdit *>("frameDrawingName");else target=number(panel,editor=="hold"?"frameExposureHold":"frameExposureStart");
        const auto enter=[&]{target->setFocus();if(auto *spin=qobject_cast<QDoubleSpinBox *>(target))spin->selectAll();else qobject_cast<QLineEdit *>(target)->selectAll();QTest::keyClicks(target,editor=="name"?"A001 line art":"4");};
        enter();QVERIFY(controller.hasPendingEdits());QCOMPARE(history->count(),0);QCOMPARE(canvas.project().toJson(),before);
        controller.cancelEditing();QVERIFY(!controller.hasPendingEdits());QCOMPARE(history->count(),0);target->clearFocus();QCoreApplication::processEvents();QCOMPARE(canvas.project().toJson(),before);
        enter();QVERIFY(controller.hasPendingEdits());QTest::keyClick(target,Qt::Key_Escape);QVERIFY(!controller.hasPendingEdits());target->clearFocus();QCoreApplication::processEvents();QCOMPARE(canvas.project().toJson(),before);QCOMPARE(history->count(),0);
        enter();controller.finishEditing();QVERIFY(!controller.hasPendingEdits());QCOMPARE(history->count(),1);QVERIFY(canvas.project().toJson()!=before);
        QVERIFY2(saveProject(canvas.project(),path,&error),qPrintable(error));history->setClean();target->clearFocus();QCoreApplication::processEvents();QCOMPARE(history->count(),1);QVERIFY(history->isClean());
        const auto saved=loadProject(path,&error);QVERIFY2(saved.has_value(),qPrintable(error));QCOMPARE(saved->toJson(),canvas.project().toJson());
    }
    void contextMenuConvertsAndDeletesTracksWithUndo(){
        Canvas canvas;canvas.setProject(smallProject());TimelineController controller(&canvas);QVERIFY(controller.addTrack(false,DrawingTemplate::Simple,"Static folder"));
        const auto childId=canvas.shot().layers[canvas.document()->activeLayer()].id;TimelinePanel panel(&controller);panel.resize(600,260);panel.show();settle();
        auto *grid=panel.findChild<QAbstractScrollArea *>("timelineGrid");
        auto chooseMenu=[&](const QString &name){QTimer::singleShot(0,&panel,[name]{auto *menu=qobject_cast<QMenu *>(QApplication::activePopupWidget());QVERIFY(menu);
                auto *action=menu->findChild<QAction *>(name);QVERIFY(action);QVERIFY(action->isEnabled());action->trigger();menu->close();});
            const QPoint point(100,timeline_test::rowCenter(grid,0));QContextMenuEvent event(QContextMenuEvent::Mouse,point,grid->viewport()->mapToGlobal(point));QApplication::sendEvent(grid->viewport(),&event);};
        chooseMenu("timelineConvertTrack");QCOMPARE(canvas.shot().animationTracks.size(),1);const auto id=controller.selectedTrackId();QVERIFY(track(canvas,id));QVERIFY(layer(canvas,childId));
        chooseMenu("timelineDeleteTrack");QVERIFY(!track(canvas,id));canvas.document()->history()->undo();QVERIFY(track(canvas,id));
    }
    void locksPlaybackAndAccessibleNarrowControls(){
        Canvas canvas;canvas.setProject(smallProject());TimelineController controller(&canvas);QVERIFY(controller.addTrack(true,DrawingTemplate::Simple,"Character with a deliberately long track name"));
        const auto id=controller.selectedTrackId();TimelinePanel panel(&controller);FramePropertiesPanel properties(&controller);panel.resize(240,260);properties.resize(240,260);panel.show();properties.show();settle();
        QCOMPARE(panel.width(),240);QCOMPARE(properties.width(),240);
        for(auto *button:panel.findChildren<QToolButton *>())if(button->isVisibleTo(&panel)){
            if(button->objectName()=="timelinePreviewQuality")QVERIFY(!button->text().isEmpty());else QVERIFY(!button->icon().isNull());
            QVERIFY(!button->toolTip().isEmpty());QVERIFY(!button->accessibleName().isEmpty());QCOMPARE(button->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,button),600);
            QVERIFY(QRect(button->mapTo(&panel,QPoint()),button->size()).right()<panel.width());}
        for(auto *parent:{static_cast<QWidget *>(&panel),static_cast<QWidget *>(&properties)})for(auto *spin:parent->findChildren<QDoubleSpinBox *>()){QCOMPARE(spin->width(),48);QVERIFY(spin->suffix().isEmpty());}
        const auto rootId=track(canvas,id)->rootGroupLayerId;canvas.document()->setLayersLocked({rootId},true);
        QVERIFY(!button(panel,"timelineNewDrawing")->isEnabled());QVERIFY(!button(panel,"timelineRemoveExposure")->isEnabled());QVERIFY(!value(properties,"frameExposureHold")->isEnabled());
        canvas.document()->setLayersLocked({rootId},false);QVERIFY(button(panel,"timelineNewDrawing")->isEnabled());
        auto *history=canvas.document()->history();history->setClean();const int count=history->count();button(panel,"timelinePlay")->click();QVERIFY(controller.playing());
        QVERIFY(!button(panel,"timelineNewDrawing")->isEnabled());QVERIFY(!value(properties,"frameExposureHold")->isEnabled());
        auto *frame=number(panel,"timelineCurrentFrame");frame->setFocus();frame->selectAll();QTest::keyClicks(frame,"23");QVERIFY(!controller.playing());
        QCOMPARE(frame->findChild<QLineEdit *>()->text(),QString("23"));QTest::keyClick(frame,Qt::Key_Return);QCOMPARE(controller.currentFrame(),22);
        QCOMPARE(history->count(),count);QVERIFY(history->isClean());
    }
    void renderPreview(){
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");if(output.isEmpty())QSKIP("Opt-in timeline previews.");
        Canvas canvas;canvas.setProject(smallProject());TimelineController controller(&canvas);
        QVERIFY(controller.addTrack(true,DrawingTemplate::Anime,"Character"));const auto character=controller.selectedTrackId();QVERIFY(controller.setExposureTiming(0,6));
        QVERIFY(controller.setFrame(6));QVERIFY(controller.newDrawing(false,4));QVERIFY(controller.setFrame(12));QVERIFY(controller.newDrawing(true,6));
        QVERIFY(controller.addTrack(true,DrawingTemplate::Rough,"Effects"));QVERIFY(controller.setExposureTiming(12,8));QVERIFY(controller.addTrack(false,DrawingTemplate::Simple,"Background"));
        QVERIFY(controller.selectTrack(character));QVERIFY(controller.setFrame(6));controller.setPlaybackPreviewDivisor(4);
        TimelinePanel timeline(&controller);timeline.resize(800,280);timeline.show();FramePropertiesPanel properties(&controller);properties.resize(280,260);properties.show();settle();QDir().mkpath(output);
        const auto captureQualityMenu=[&](const QString &name){
            auto *quality=button(timeline,"timelinePreviewQuality");QVERIFY(quality);QCOMPARE(quality->text(),QString("1/4"));
            // Capture only after Cocoa has finished creating the popup window.
            // A zero-delay timer inside mousePress can close it during show_sys.
            timeline.raise();timeline.activateWindow();QVERIFY(QTest::qWaitForWindowActive(&timeline));
            auto *menu=quality->menu();menu->popup(quality->mapToGlobal(QPoint(0,quality->height())));
            QVERIFY(QTest::qWaitForWindowExposed(menu));QVERIFY(menu->grab().save(output+"/"+name));menu->close();settle();
        };
        QVERIFY(timeline.grab().save(output+"/timeline-wide.png"));QVERIFY(properties.grab().save(output+"/frame-properties.png"));
        captureQualityMenu("timeline-quality-wide.png");
        timeline.resize(280,280);settle();QVERIFY(timeline.grab().save(output+"/timeline-280.png"));timeline.resize(240,280);properties.resize(240,260);settle();
        QVERIFY(timeline.grab().save(output+"/timeline-240.png"));QVERIFY(properties.grab().save(output+"/frame-properties-240.png"));
        captureQualityMenu("timeline-quality-240.png");controller.setPlaying(true);settle();
        QVERIFY(timeline.grab().save(output+"/timeline-playing-240.png"));controller.setPlaying(false);
    }
};
QTEST_MAIN(TimelinePanelTests)
#include "timeline_panel_tests.moc"
