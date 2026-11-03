#include "animation_test_fixture.h"
#include "audio.h"
#include "canvas.h"
#include "document.h"
#include "timeline_controller.h"
#include "timeline_panel.h"
#include "timeline_test_geometry.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QAbstractScrollArea>
#include <QAction>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QMenu>
#include <QScrollBar>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QtTest>
#include <cmath>
using namespace toon;
namespace {
QToolButton *button(QWidget &panel,const char *name){return panel.findChild<QToolButton *>(name);}
ValueSlider *field(QWidget &panel,const char *name){return panel.findChild<ValueSlider *>(name);}
void settle(){QCoreApplication::processEvents();for(int i=0;i<4;++i)QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);}
void show(TimelinePanel &panel,int width=1000){panel.resize(width,420);panel.show();settle();}
Project keyedFixture(){
    auto project=animation_fixture::project();auto &shot=project.shots[0];
    auto &ink=shot.layers[3];ink.mask=LayerMask{};
    shot.layerAnimations={{ink.id,{},{{KeyframeProperty::BlurRadius,{{0,4},{2,20}}},{KeyframeProperty::MaskPositionX,{{0,0},{2,12}}}}, {}}};
    return project;
}
}
class TimelineUxTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void toolbarDeletesSelectedCompositingKeysAndKeepsExposures(){
        Canvas canvas;canvas.setProject(keyedFixture());TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);
        const auto track=canvas.shot().animationTracks[0];const auto id=canvas.shot().layers[3].id;
        QVERIFY(controller.selectTrack(track.id));QVERIFY(controller.selectPropertyLayer(id));QVERIFY(controller.setFrame(2));
        controller.setSelectedKeys({{id,KeyframeProperty::BlurRadius,{},2},{id,KeyframeProperty::MaskPositionX,{},2}});
        QCOMPARE(controller.selectedKeys().size(),2);QVERIFY(button(panel,"timelineRemoveExposure")->isEnabled());
        QVERIFY(button(panel,"timelineRemoveExposure")->toolTip().contains("keyframes"));
        QVERIFY(!button(panel,"timelineNewDrawing")->isEnabled());QVERIFY(!button(panel,"timelineDuplicateDrawing")->isEnabled());
        QVERIFY(panel.property("timelineSelectionContext").toString().contains("2 selected keyframes"));
        const auto source=canvas.shot().layers[3].tiles;const int history=canvas.document()->history()->count();
        button(panel,"timelineRemoveExposure")->click();
        QCOMPARE(canvas.shot().animationTracks[0],track);QCOMPARE(canvas.shot().layers[3].tiles,source);
        QCOMPARE(propertyChannel(canvas.shot(),id,KeyframeProperty::BlurRadius)->keys.size(),1);
        QCOMPARE(propertyChannel(canvas.shot(),id,KeyframeProperty::MaskPositionX)->keys.size(),1);
        QCOMPARE(canvas.document()->history()->count(),history+1);canvas.document()->history()->undo();
        QCOMPARE(propertyChannel(canvas.shot(),id,KeyframeProperty::BlurRadius)->keys.size(),2);QCOMPARE(canvas.shot().animationTracks[0],track);
    }
    void selectedKeyLocksDisableWholeTrashSelection(){
        Canvas canvas;canvas.setProject(keyedFixture());TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);
        const auto id=canvas.shot().layers[3].id;QVERIFY(controller.selectPropertyLayer(id));QVERIFY(controller.setFrame(2));
        canvas.document()->setLayerPositionLocked(true);
        controller.setSelectedKeys({{id,KeyframeProperty::BlurRadius,{},2},{id,KeyframeProperty::MaskPositionX,{},2}});
        QVERIFY(!button(panel,"timelineRemoveExposure")->isEnabled());const auto unchanged=canvas.project().toJson();button(panel,"timelineRemoveExposure")->click();QCOMPARE(canvas.project().toJson(),unchanged);
        controller.setSelectedKeys({{id,KeyframeProperty::BlurRadius,{},2}});QVERIFY(button(panel,"timelineRemoveExposure")->isEnabled());
        canvas.document()->setLayersLocked({id},true);QVERIFY(!button(panel,"timelineRemoveExposure")->isEnabled());
    }
    void currentExposureTrashRespectsDrawingAndTrackLocks_data(){
        QTest::addColumn<int>("lock");
        QTest::newRow("unlocked")<<0;QTest::newRow("track-locked")<<1;QTest::newRow("track-position-locked")<<2;
        QTest::newRow("drawing-locked")<<3;QTest::newRow("drawing-position-locked")<<4;
    }
    void currentExposureTrashRespectsDrawingAndTrackLocks(){
        QFETCH(int,lock);auto project=animation_fixture::project();const auto track=project.shots[0].animationTracks[0];
        if(lock){const auto id=lock<=2?track.rootGroupLayerId:track.drawings[0].groupLayerId;
            for(auto &layer:project.shots[0].layers)if(layer.id==id){layer.locked=lock==1||lock==3;layer.positionLocked=lock==2||lock==4;}}
        Canvas canvas;canvas.setProject(project);TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);
        QVERIFY(controller.selectTrack(track.id));QVERIFY(controller.setFrame(1));controller.clearCelSelection();QVERIFY(controller.selectedCels().isEmpty());
        auto *trash=button(panel,"timelineRemoveExposure");QCOMPARE(trash->isEnabled(),lock==0);
        const auto before=canvas.project().toJson();const int history=canvas.document()->history()->count();trash->click();
        if(lock){QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),history);}
        else{const auto &edited=canvas.shot().animationTracks[0];QCOMPARE(edited.exposures.size(),track.exposures.size()-1);
            QCOMPARE(edited.exposures.first(),track.exposures.last());QCOMPARE(edited.drawings,track.drawings);
            QCOMPARE(canvas.document()->history()->count(),history+1);canvas.document()->history()->undo();QCOMPARE(canvas.project().toJson(),before);}
    }
    void toolbarDeletesCameraAndAudioTargetsWithoutDeletingDrawings(){
        auto project=animation_fixture::project();auto &shot=project.shots[0];shot.camera2DKeys={{0,shot.camera2D},{2,shot.camera2D}};
        AudioClip clip;clip.id=animation_fixture::id();clip.name="Dialogue";clip.asset=makeAudioAsset("Dialogue",QByteArray(audioSampleRate*audioBytesPerSampleFrame,0));clip.sampleCount=clip.asset->sampleFrameCount();shot.audioClips.append(clip);
        Canvas canvas;canvas.setProject(project);TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel);
        const auto tracks=canvas.shot().animationTracks;QVERIFY(controller.selectCamera());
        TimelineKeyRef key;key.kind=TimelineKeyKind::Camera2D;key.frame=2;controller.setSelectedKeys({key});QVERIFY(button(panel,"timelineRemoveExposure")->isEnabled());
        button(panel,"timelineRemoveExposure")->click();QCOMPARE(canvas.shot().camera2DKeys.size(),1);QCOMPARE(canvas.shot().animationTracks,tracks);canvas.document()->history()->undo();QCOMPARE(canvas.shot().camera2DKeys.size(),2);
        QVERIFY(controller.selectAudioClip(clip.id));QVERIFY(button(panel,"timelineRemoveExposure")->toolTip().contains("audio"));QVERIFY(panel.property("timelineSelectionContext").toString().contains("Dialogue"));
        button(panel,"timelineRemoveExposure")->click();QVERIFY(canvas.shot().audioClips.isEmpty());QCOMPARE(canvas.shot().animationTracks,tracks);
        canvas.document()->history()->undo();QCOMPARE(canvas.shot().audioClips,QVector<AudioClip>{clip});
    }
    void centerPlayheadKeepsZoomFrameSelectionAndArtwork(){
        auto project=animation_fixture::project();project.shots[0].durationFrames=2400;Canvas canvas;canvas.setProject(project);TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel,800);
        QVERIFY(controller.setFrame(1200));auto *grid=panel.findChild<QAbstractScrollArea *>("timelineGrid");QVERIFY(grid);grid->horizontalScrollBar()->setValue(0);
        const auto before=canvas.project().toJson();const auto selected=controller.selectedTrackId();const auto zoom=field(panel,"timelineZoom")->value();
        button(panel,"timelineCenterPlayhead")->click();const auto content=grid->viewport()->width()-timeline_test::labelWidth(grid);
        const auto x=(controller.currentFrame()+.5)*zoom/5-grid->horizontalScrollBar()->value();QVERIFY(std::abs(x-content/2.)<=1);
        QCOMPARE(controller.currentFrame(),1200);QCOMPARE(controller.selectedTrackId(),selected);QCOMPARE(field(panel,"timelineZoom")->value(),zoom);QCOMPARE(canvas.project().toJson(),before);
    }
    void responsiveRowsAndOptionsKeepEveryActionReachable(){
        Canvas canvas;canvas.setProject(animation_fixture::project());TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel,1200);
        const QString captures=qEnvironmentVariable("TOON_UI_PREVIEW");if(!captures.isEmpty())QVERIFY(QDir().mkpath(captures));
        for(int width:{1200,700,360,240}){
            panel.resize(width,420);settle();QCOMPARE(panel.width(),width);
            const bool compact=width<700;QCOMPARE(button(panel,"timelineMore")->isVisibleTo(&panel),compact);
            for(auto *control:panel.findChildren<QToolButton *>())if(control->isVisibleTo(&panel)){
                const QRect bounds(control->mapTo(&panel,QPoint()),control->size());QVERIFY2(bounds.left()>=0&&bounds.right()<panel.width(),qPrintable(control->objectName()));
                QVERIFY(!control->toolTip().isEmpty());QVERIFY(!control->accessibleName().isEmpty());QCOMPARE(control->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,control),600);
            }
            for(auto *value:panel.findChildren<ValueSlider *>())if(value->isVisibleTo(&panel)){auto *number=value->findChild<QDoubleSpinBox *>();QCOMPARE(number->width(),48);QVERIFY(number->suffix().isEmpty());const QRect bounds(value->mapTo(&panel,QPoint()),value->size());QVERIFY(bounds.left()>=0&&bounds.right()<panel.width());}
            if(compact){auto *menu=button(panel,"timelineMore")->menu();for(const auto *id:{"timelineAddFolderOption","timelineReuseDrawingOption","timelineAddCueOption","timelineRangeSettingsOption","timelineOnionSettingsOption","timelineRippleOption","timelineImportAudioOption","timelineAudioScrubOption","timelineCenterPlayheadOption"}){auto *action=menu->findChild<QAction *>(id);QVERIFY(action);QVERIFY(!action->text().isEmpty());QVERIFY(!action->toolTip().isEmpty());}}
            if(!captures.isEmpty())QVERIFY(panel.grab().save(captures+QString("/timeline-ux-%1.png").arg(width)));
        }
        auto *audible=button(panel,"timelineMore")->menu()->findChild<QAction *>("timelineAudioScrubOption");QVERIFY(audible);const bool initial=controller.audioScrubbing();audible->trigger();QCOMPARE(controller.audioScrubbing(),!initial);QCOMPARE(audible->isChecked(),!initial);
        QFont larger=panel.font();larger.setPixelSize(panel.fontMetrics().height()+5);panel.setFont(larger);panel.resize(240,480);settle();QCOMPARE(panel.width(),240);
        if(!captures.isEmpty())QVERIFY(panel.grab().save(captures+"/timeline-ux-large-font.png"));
    }
    void resizeKeepsFrameDraftAndDoesNotEditArtwork(){
        Canvas canvas;canvas.setProject(animation_fixture::project());TimelineController controller(&canvas);TimelinePanel panel(&controller);show(panel,1000);
        QVERIFY(QTest::qWaitForWindowExposed(&panel));panel.raise();panel.activateWindow();QVERIFY(QTest::qWaitForWindowActive(&panel));
        auto *value=field(panel,"timelineCurrentFrame");auto *number=value->findChild<QDoubleSpinBox *>();auto *editor=number->findChild<QLineEdit *>();
        number->setFocus();QTRY_VERIFY(number->hasFocus()||editor->hasFocus());settle();
        QSignalSpy finished(value,&ValueSlider::editingFinished),changed(value,&ValueSlider::valueChanged);
        number->selectAll();QTest::keyClicks(number,"13");QCOMPARE(editor->text(),QString("13"));QVERIFY(value->editingActive());QCOMPARE(controller.currentFrame(),0);
        const auto source=canvas.project().toJson();const int history=canvas.document()->history()->count();
        for(int width:{240,360,1200,240}){panel.resize(width,420);settle();QVERIFY2(editor->text()=="13",qPrintable(QString("Frame draft after resize to %1: %2; focus: %3; editing: %4").arg(width).arg(editor->text()).arg(QApplication::focusWidget()?QApplication::focusWidget()->objectName():QString("none")).arg(value->editingActive())));QVERIFY(number->hasFocus()||editor->hasFocus());QCOMPARE(controller.currentFrame(),0);QCOMPARE(canvas.project().toJson(),source);}
        QCOMPARE(finished.count(),0);QCOMPARE(changed.count(),0);
        QTest::keyClick(number,Qt::Key_Return);QCOMPARE(controller.currentFrame(),12);QCOMPARE(canvas.document()->history()->count(),history);QCOMPARE(finished.count(),1);QCOMPARE(changed.count(),1);
    }
};
QTEST_MAIN(TimelineUxTests)
#include "test_timeline_ux.moc"
