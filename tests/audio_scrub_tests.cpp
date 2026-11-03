#include "audio_scrub.h"
#include "audio_mix.h"
#include "canvas.h"
#include "timeline_controller.h"
#include "timeline_panel.h"
#include "main_window.h"
#include <QAbstractScrollArea>
#include <QAction>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtEndian>
#include <QtTest>
#include <cstring>
using namespace toon;
namespace {
Project fixture() {
    auto p=Project::create();p.width=p.height=64;p.fps=24;p.shots[0].durationFrames=24;
    QByteArray pcm(audioSampleRate*audioBytesPerSampleFrame,Qt::Uninitialized);
    for(int i=0;i<audioSampleRate;++i){qToLittleEndian<qint16>(qint16(2000+i%10000),pcm.data()+i*4);qToLittleEndian<qint16>(-2000,pcm.data()+i*4+2);}
    AudioClip c;c.id="85c83cf1-d4e6-4016-88e2-8eac31dbe116";c.name="Dialogue";c.asset=makeAudioAsset("Dialogue",pcm);c.sampleCount=audioSampleRate;
    c.fadeInSamples=4000;c.gainAnimated=true;c.gainKeys={{0,.25},{audioSampleRate-1,1}};
    p.shots[0].audioClips={c};return p;
}
qint16 sample(const QByteArray &bytes,int i){qint16 value;std::memcpy(&value,bytes.constData()+i*2,2);return value;}
}
class AudioScrubTests final:public QObject{
 Q_OBJECT
private slots:
 void previewUsesTheMixAndStopsImmediately(){
    auto p=fixture();AudioScrub scrub(nullptr,AudioScrub::OutputMode::Silent);QSignalSpy grains(&scrub,&AudioScrub::grainReady);
    scrub.begin(p.shots[0],p.fps);QVERIFY(scrub.active());scrub.seek(5);QCOMPARE(grains.size(),1);
    const auto bytes=grains[0][1].toByteArray();QCOMPARE(bytes.size(),qsizetype(3840*4));QCOMPARE(sample(bytes,0),0);QCOMPARE(sample(bytes,7679),0);
    AudioMixStream mix(p.shots[0].audioClips,p.fps,5,0,23,false);const auto expected=mix.read(bytes.size());
    QCOMPARE(bytes.mid(96*4,(3840-192)*4),expected.mid(96*4,(3840-192)*4));
    scrub.seek(6);scrub.seek(7);scrub.end();QTest::qWait(45);QCOMPARE(grains.size(),1);QVERIFY(!scrub.active());
    scrub.seek(8);QCOMPARE(grains.size(),1);
 }
 void seeksCoalesceAndNewGestureCannotReplayOldRequests(){
    auto p=fixture();AudioScrub scrub(nullptr,AudioScrub::OutputMode::Silent);QSignalSpy grains(&scrub,&AudioScrub::grainReady);
    scrub.begin(p.shots[0],p.fps);scrub.seek(0);for(int i=1;i<12;++i)scrub.seek(i);
    QTRY_COMPARE_WITH_TIMEOUT(grains.size(),2,500);QCOMPARE(grains.last()[0].toInt(),11);
    scrub.seek(12);scrub.end();scrub.begin(p.shots[0],p.fps);scrub.seek(3);QTest::qWait(40);QCOMPARE(grains.size(),3);QCOMPARE(grains.last()[0].toInt(),3);
    scrub.end();p.shots[0].audioClips[0].muted=true;scrub.begin(p.shots[0],p.fps);scrub.seek(4);QCOMPARE(grains.size(),3);
 }
 void frameNavigationIsSilentAndScrubDoesNotEdit(){
    Canvas canvas;auto p=fixture();p.shots[0].audioClips.clear();canvas.setProject(p);
    TimelineController controller(&canvas);auto *scrub=controller.findChild<AudioScrub *>();QVERIFY(scrub);
    QVERIFY(!controller.audioScrubbing());QVERIFY(controller.setFrame(3));QVERIFY(!scrub->active());
    controller.setAudioScrubbing(true);QVERIFY(controller.scrubToFrame(7));QVERIFY(scrub->active());QCOMPARE(controller.currentFrame(),7);
    QVERIFY(controller.scrubToFrame(9));QVERIFY(scrub->active());QCOMPARE(canvas.document()->history()->count(),0);
    controller.endScrub();QVERIFY(!scrub->active());QVERIFY(controller.setFrame(12));QVERIFY(!scrub->active());
    QVERIFY(controller.scrubToFrame(4));controller.setAudioScrubbing(false);QVERIFY(!scrub->active());
 }
 void rulerReleaseAndEscapeStopScrubbing(){
    Canvas canvas;auto p=fixture();p.shots[0].audioClips.clear();canvas.setProject(p);TimelineController controller(&canvas);controller.setAudioScrubbing(true);
    TimelinePanel panel(&controller);panel.resize(700,300);panel.show();QCoreApplication::processEvents();
    auto *grid=panel.findChild<QAbstractScrollArea *>("timelineGrid");auto *scrub=controller.findChild<AudioScrub *>();QVERIFY(grid&&scrub);
    QTest::mousePress(grid->viewport(),Qt::LeftButton,Qt::NoModifier,{320,10});QVERIFY(scrub->active());
    QTest::mouseRelease(grid->viewport(),Qt::LeftButton,Qt::NoModifier,{340,10});QVERIFY(!scrub->active());
    QTest::mousePress(grid->viewport(),Qt::LeftButton,Qt::NoModifier,{360,10});QVERIFY(scrub->active());
    QTest::keyClick(grid,Qt::Key_Escape);QVERIFY(!scrub->active());
 }
 void preferencePersistsAndExportIsAvailable(){
    QTemporaryDir dir;const auto path=dir.filePath("workspace.ini");
    {MainWindow window(path);auto *controller=window.findChild<TimelineController *>();QVERIFY(controller);QVERIFY(!controller->audioScrubbing());
     controller->setAudioScrubbing(true);auto *action=window.findChild<QAction *>("audioExportAction");QVERIFY(action);QVERIFY(!action->isEnabled());}
    {MainWindow window(path);QVERIFY(window.findChild<TimelineController *>()->audioScrubbing());}
 }
};
QTEST_MAIN(AudioScrubTests)
#include "audio_scrub_tests.moc"
