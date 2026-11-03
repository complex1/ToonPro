#include "audio.h"
#include "audio_envelope_widget.h"
#include "audio_panel.h"
#include "canvas.h"
#include "document.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QAction>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QScrollArea>
#include <QSlider>
#include <QStyle>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QUuid>
#include <QtEndian>
#include <QtTest>
#include <cmath>
using namespace toon;
namespace {
Project movie(double fps=24){
    auto p=Project::create();p.width=160;p.height=120;p.fps=fps;p.shots[0].durationFrames=100;
    QByteArray pcm(audioSampleRate*4*audioBytesPerSampleFrame,0);for(int i=0;i<audioSampleRate*4;++i){const auto sample=qint16(std::sin(i*.07)*20000);qToLittleEndian(sample,pcm.data()+i*4);qToLittleEndian(sample,pcm.data()+i*4+2);}
    AudioClip clip;clip.id=QUuid::createUuid().toString(QUuid::WithoutBraces);clip.name="Dialogue";clip.asset=makeAudioAsset("Dialogue source",pcm);clip.startFrame=2;clip.sourceOffsetSamples=250;clip.sampleCount=audioSampleRate;clip.gain=.8;p.shots[0].audioClips.append(clip);return p;
}
AudioClip current(const Canvas &canvas){return canvas.shot().audioClips.first();}
QDoubleSpinBox *number(AudioPanel &panel,const char *name){return panel.findChild<ValueSlider *>(name)->findChild<QDoubleSpinBox *>();}
QToolButton *button(AudioPanel &panel,const char *name){return panel.findChild<QToolButton *>(name);}
void show(AudioPanel &panel){panel.resize(240,630);panel.show();panel.findChild<QTabWidget *>("audioFinishingTabs")->setCurrentIndex(1);QCoreApplication::processEvents();}
void type(AudioPanel &panel,const char *name,const QString &text){auto *spin=number(panel,name);spin->setFocus();spin->selectAll();QTest::keyClicks(spin,text);}
void move(QWidget *widget,QPoint point){QMouseEvent event(QEvent::MouseMove,point,widget->mapToGlobal(point),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(widget,&event);}
}
class AudioFinishingPanelTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void fadeFieldsSaveCancelAndFractionalFrames(){
        Canvas canvas;canvas.setProject(movie(23.976));TimelineController controller(&canvas);QVERIFY(controller.selectAudioClip(current(canvas).id));AudioPanel panel(&controller);show(panel);const auto original=current(canvas);const auto history=canvas.document()->history()->index();
        type(panel,"audioClipFadeIn","3.5");QVERIFY(controller.hasPendingEdits());emit controller.changed();QCOMPARE(number(panel,"audioClipFadeIn")->findChild<QLineEdit *>()->text(),QString("3.5"));QCOMPARE(current(canvas),original);controller.finishEditing();
        QCOMPARE(current(canvas).fadeInSamples,audioSampleFrameAtTimelineFrame(3.5,23.976));QCOMPARE(canvas.document()->history()->index(),history+1);QCOMPARE(current(canvas).sourceOffsetSamples,original.sourceOffsetSamples);QCOMPARE(current(canvas).sampleCount,original.sampleCount);
        type(panel,"audioClipFadeOut","4");controller.cancelEditing();QCOMPARE(current(canvas).fadeOutSamples,qint64(0));QVERIFY(!controller.hasPendingEdits());
        QTemporaryDir directory;QString error;QVERIFY2(saveProject(canvas.project(),directory.filePath("mix.toonproj"),&error),qPrintable(error));const auto loaded=loadProject(directory.filePath("mix.toonproj"),&error);QVERIFY2(loaded.has_value(),qPrintable(error));QCOMPARE(loaded->shots[0].audioClips[0],current(canvas));
        const auto in=current(canvas).fadeInSamples;panel.findChild<QTabWidget *>("audioFinishingTabs")->setCurrentIndex(0);number(panel,"audioClipSourceIn")->setValue(2);QCOMPARE(current(canvas).fadeInSamples,in);QCOMPARE(current(canvas).fadeInStartSample,original.sourceOffsetSamples);
    }
    void volumeKeysUsePlayheadAndPreserveDisabledAutomation(){
        Canvas canvas;auto project=movie();project.shots[0].audioClips[0].fadeInSamples=12000;canvas.setProject(project);TimelineController controller(&canvas);QVERIFY(controller.selectAudioClip(current(canvas).id));QVERIFY(controller.setFrame(4));AudioPanel panel(&controller);show(panel);
        button(panel,"audioVolumeAnimated")->click();QVERIFY(current(canvas).gainAnimated);QCOMPARE(current(canvas).gainKeys.size(),1);QCOMPARE(current(canvas).gainKeys[0].sourceSample,qint64(4250));QCOMPARE(current(canvas).gainKeys[0].gain,.8);
        number(panel,"audioClipVolume")->setValue(50);QCOMPARE(current(canvas).gainKeys[0].gain,.5);QCOMPARE(current(canvas).gain,.8);
        QVERIFY(controller.setFrame(10));button(panel,"audioVolumeAddKey")->click();QCOMPARE(current(canvas).gainKeys.size(),2);number(panel,"audioClipVolume")->setValue(150);QCOMPARE(current(canvas).gainKeys[1].gain,1.5);
        QVERIFY(controller.setFrame(7));QCOMPARE(number(panel,"audioClipVolume")->value(),100.0);
        button(panel,"audioVolumeAnimated")->click();QVERIFY(!current(canvas).gainAnimated);QCOMPARE(current(canvas).gainKeys.size(),2);QCOMPARE(number(panel,"audioClipVolume")->value(),80.0);
        button(panel,"audioVolumeAnimated")->click();QCOMPARE(current(canvas).gainKeys.size(),2);QCOMPARE(number(panel,"audioClipVolume")->value(),100.0);
        QVERIFY(controller.setFrame(80));QVERIFY(!button(panel,"audioVolumeAddKey")->isEnabled());QVERIFY(!panel.findChild<ValueSlider *>("audioClipVolume")->isEnabled());
        QVERIFY(controller.setFrame(10));button(panel,"audioVolumeDeleteKey")->click();QCOMPARE(current(canvas).gainKeys.size(),1);
        QVERIFY(controller.setFrame(4));const auto history=canvas.document()->history()->index();button(panel,"audioVolumeDeleteKey")->click();QVERIFY(current(canvas).gainKeys.isEmpty());QVERIFY(!current(canvas).gainAnimated);QCOMPARE(current(canvas).gain,.5);QCOMPARE(number(panel,"audioClipVolume")->value(),50.0);QCOMPARE(canvas.document()->history()->index(),history+1);canvas.document()->history()->undo();QVERIFY(current(canvas).gainAnimated);QCOMPARE(current(canvas).gainKeys.size(),1);
    }
    void fadeHandleIsOneDraftAndNavigationCommitsIt(){
        Canvas canvas;canvas.setProject(movie());TimelineController controller(&canvas);QVERIFY(controller.selectAudioClip(current(canvas).id));AudioPanel panel(&controller);show(panel);auto *graph=panel.findChild<AudioEnvelopeWidget *>();const auto before=current(canvas);const int history=canvas.document()->history()->index();
        QTest::mousePress(graph,Qt::LeftButton,Qt::NoModifier,{12,13});move(graph,{graph->width()/3,13});move(graph,{graph->width()/2,13});QVERIFY(controller.hasPendingEdits());QCOMPARE(current(canvas),before);QCOMPARE(canvas.document()->history()->index(),history);
        QTest::mouseRelease(graph,Qt::LeftButton,Qt::NoModifier,{graph->width()/2,13});QVERIFY(current(canvas).fadeInSamples>20000);QVERIFY(current(canvas).fadeInSamples<28000);QCOMPARE(canvas.document()->history()->index(),history+1);QVERIFY(!controller.hasPendingEdits());
        canvas.document()->history()->undo();QTest::mousePress(graph,Qt::LeftButton,Qt::NoModifier,{12,13});move(graph,{graph->width()/3,13});QTest::keyClick(graph,Qt::Key_Escape);QTest::mouseRelease(graph,Qt::LeftButton,Qt::NoModifier,{graph->width()/3,13});QCOMPARE(current(canvas),before);QVERIFY(!controller.hasPendingEdits());
        QTest::mousePress(graph,Qt::LeftButton,Qt::NoModifier,{12,13});move(graph,{graph->width()/3,13});QVERIFY(controller.setFrame(12));QVERIFY(current(canvas).fadeInSamples>0);QVERIFY(!controller.hasPendingEdits());QTest::mouseRelease(graph,Qt::LeftButton,Qt::NoModifier,{graph->width()/3,13});QCOMPARE(canvas.document()->history()->index(),history+1);
    }
    void envelopeKeysSupportDirectAndKeyboardEdits(){
        Canvas canvas;canvas.setProject(movie());TimelineController controller(&canvas);QVERIFY(controller.selectAudioClip(current(canvas).id));AudioPanel panel(&controller);show(panel);auto *graph=panel.findChild<AudioEnvelopeWidget *>();const int history=canvas.document()->history()->index();
        const QPoint at(graph->width()/2,86);QTest::mouseDClick(graph,Qt::LeftButton,Qt::NoModifier,at);QCOMPARE(current(canvas).gainKeys.size(),1);QVERIFY(current(canvas).gainAnimated);QCOMPARE(canvas.document()->history()->index(),history+1);const auto key=current(canvas).gainKeys[0];
        QTest::keyClick(graph,Qt::Key_Up);QCOMPARE(current(canvas).gainKeys[0].gain,key.gain+.05);QTest::keyClick(graph,Qt::Key_Right);QCOMPARE(current(canvas).gainKeys[0].sourceSample,key.sourceSample+2000);
        QTest::keyClick(graph,Qt::Key_Delete);QVERIFY(current(canvas).gainKeys.isEmpty());QVERIFY(!current(canvas).gainAnimated);QCOMPARE(current(canvas).gain,key.gain+.05);canvas.document()->history()->undo();QCOMPARE(current(canvas).gainKeys.size(),1);QVERIFY(current(canvas).gainAnimated);
        QTest::keyClick(graph,Qt::Key_I);QTest::keyClick(graph,Qt::Key_Right);QCOMPARE(current(canvas).fadeInSamples,qint64(2000));QTest::keyClick(graph,Qt::Key_O);QTest::keyClick(graph,Qt::Key_Right);QCOMPARE(current(canvas).fadeOutSamples,qint64(2000));
    }
    void crossfadeChoosesChronologicalOrderAndUndoesOnce(){
        Canvas canvas;auto project=movie();auto second=project.shots[0].audioClips[0];second.id=QUuid::createUuid().toString(QUuid::WithoutBraces);second.name="Next line";second.startFrame=20;project.shots[0].audioClips.append(second);canvas.setProject(project);TimelineController controller(&canvas);QVERIFY(controller.selectAudioClip(second.id));AudioPanel panel(&controller);show(panel);auto *crossfade=button(panel,"audioCrossfade");QVERIFY(crossfade->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(crossfade->menu(),"aboutToShow",Qt::DirectConnection));const auto actions=crossfade->menu()->findChildren<QAction *>("audioCrossfadeEqualPower");QCOMPARE(actions.size(),1);const int history=canvas.document()->history()->index();actions.first()->trigger();
        QCOMPARE(canvas.shot().audioClips[0].fadeOutSamples,qint64(12000));QCOMPARE(canvas.shot().audioClips[1].fadeInSamples,qint64(12000));QCOMPARE(canvas.shot().audioClips[1].fadeInCurve,AudioFadeCurve::EqualPower);QCOMPARE(canvas.document()->history()->index(),history+1);QCOMPARE(canvas.shot().audioClips[0].startFrame,2);QCOMPARE(canvas.shot().audioClips[1].startFrame,20);
        canvas.document()->history()->undo();QCOMPARE(canvas.shot().audioClips,project.shots[0].audioClips);
    }
    void externalDeletionCancelsEnvelopeAndNarrowControlsStayAccessible(){
        Canvas canvas;canvas.setProject(movie());TimelineController controller(&canvas);QVERIFY(controller.selectAudioClip(current(canvas).id));AudioPanel panel(&controller);show(panel);QCOMPARE(panel.width(),240);auto *graph=panel.findChild<AudioEnvelopeWidget *>();
        for(auto *spin:panel.findChildren<QDoubleSpinBox *>()){QCOMPARE(spin->width(),48);QVERIFY(spin->suffix().isEmpty());}
        for(const auto *name:{"audioVolumeAnimated","audioVolumeAddKey","audioVolumeDeleteKey","audioFadeLinear","audioFadeEqualPower","audioCrossfade"}){auto *control=button(panel,name);QVERIFY(!control->icon().isNull());QVERIFY(!control->toolTip().isEmpty());QVERIFY(!control->accessibleName().isEmpty());QCOMPARE(control->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,control),600);}
        QTest::mousePress(graph,Qt::LeftButton,Qt::NoModifier,{12,13});move(graph,{graph->width()/3,13});QVERIFY(controller.hasPendingEdits());QVERIFY(canvas.document()->removeAudioClip(current(canvas).id));QVERIFY(!controller.hasPendingEdits());QTest::mouseRelease(graph,Qt::LeftButton,Qt::NoModifier,{graph->width()/3,13});QVERIFY(canvas.shot().audioClips.isEmpty());QVERIFY(!graph->isEnabled());
    }
    void preview(){
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");if(output.isEmpty())QSKIP("Opt-in audio-finishing preview.");Canvas canvas;auto project=movie();auto &clip=project.shots[0].audioClips[0];clip.fadeInSamples=8000;clip.fadeOutSamples=10000;clip.gainAnimated=true;clip.gainKeys={{250,.65},{16000,1.45},{38000,.8}};canvas.setProject(project);TimelineController controller(&canvas);QVERIFY(controller.selectAudioClip(current(canvas).id));QVERIFY(controller.setFrame(10));AudioPanel panel(&controller);show(panel);QVERIFY(QDir().mkpath(output));QVERIFY(panel.grab().save(output+"/audio-mix-240.png"));panel.resize(320,640);QCoreApplication::processEvents();QVERIFY(panel.grab().save(output+"/audio-mix-320.png"));
    }
};
QTEST_MAIN(AudioFinishingPanelTests)
#include "audio_finishing_panel_tests.moc"
