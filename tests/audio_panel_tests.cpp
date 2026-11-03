#include "timeline_test_geometry.h"
#include "audio.h"
#include "audio_panel.h"
#include "canvas.h"
#include "document.h"
#include "property_row.h"
#include "timeline_controller.h"
#include "timeline_panel.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QAbstractScrollArea>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QScrollBar>
#include <QSlider>
#include <QStyle>
#include <QStackedWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QUuid>
#include <QtEndian>
#include <QtTest>
#include <cmath>
using namespace toon;
namespace {
Project fixture(double fps=24){auto p=Project::create();p.width=160;p.height=120;p.fps=fps;p.shots[0].durationFrames=40;
    QByteArray pcm(audioSampleRate*audioBytesPerSampleFrame,0);for(int i=0;i<audioSampleRate;++i){const qint16 sample=qint16(std::sin(i*.07)*std::sin(i*.0003)*24000);qToLittleEndian(sample,pcm.data()+i*4);qToLittleEndian(sample,pcm.data()+i*4+2);}
    auto asset=makeAudioAsset("Dialogue source",pcm);AudioClip clip;clip.id=QUuid::createUuid().toString(QUuid::WithoutBraces);clip.name="Dialogue";clip.asset=asset;clip.startFrame=2;clip.sourceOffsetSamples=231;clip.sampleCount=12333;clip.gain=.8;p.shots[0].audioClips.append(clip);return p;
}
void settle(){QCoreApplication::processEvents();for(int i=0;i<4;++i)QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);}
ValueSlider *field(QWidget &panel,const QString &id){return panel.findChild<ValueSlider *>(id);}
QDoubleSpinBox *number(QWidget &panel,const QString &id){return field(panel,id)->findChild<QDoubleSpinBox *>();}
void type(QWidget &panel,const QString &id,const QString &text){auto *spin=number(panel,id);spin->setFocus();spin->selectAll();QTest::keyClicks(spin,text);}
QAbstractScrollArea *grid(QWidget &panel){return panel.findChild<QAbstractScrollArea *>("timelineGrid");}
QPoint point(QAbstractScrollArea *view,qreal frame,int row=1){const int label=timeline_test::labelWidth(view);return {qRound(label+frame*20-view->horizontalScrollBar()->value()),timeline_test::rowCenter(view,row)};}
void move(QAbstractScrollArea *view,QPoint to){QMouseEvent e(QEvent::MouseMove,to,view->viewport()->mapToGlobal(to),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view->viewport(),&e);}
void drag(QAbstractScrollArea *view,QPoint from,QPoint to){QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,from);move(view,to);QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,to);}
void show(TimelinePanel &panel){panel.resize(1000,330);panel.show();settle();grid(panel)->horizontalScrollBar()->setValue(0);grid(panel)->verticalScrollBar()->setValue(0);}
AudioClip clip(Canvas &canvas){return canvas.shot().audioClips.first();}
}
class AudioPanelTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void selectingAudioOpensPropertiesAndKeepsArtworkLayer(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);const auto layer=controller.selectedPropertyLayerId();FramePropertiesPanel properties(&controller);properties.resize(240,600);properties.show();TimelinePanel timeline(&controller);show(timeline);auto *view=grid(timeline);
        QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,point(view,3.5));QCOMPARE(controller.selectedAudioClipId(),clip(canvas).id);QCOMPARE(controller.selectedPropertyLayerId(),layer);QCOMPARE(properties.findChild<QTabBar *>("framePropertiesTabs")->currentIndex(),2);QVERIFY(!controller.hasPendingEdits());
        QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,point(view,.5,0));QVERIFY(controller.selectedAudioClipId().isEmpty());QCOMPARE(controller.selectedPropertyLayerId(),layer);QCOMPARE(properties.findChild<QTabBar *>("framePropertiesTabs")->currentIndex(),1);
        QVERIFY(controller.addTrack(true,DrawingTemplate::Simple,"Character"));const auto animation=controller.selectedTrackId();QVERIFY(controller.selectAudioClip(clip(canvas).id));QCOMPARE(properties.findChild<QTabBar *>("framePropertiesTabs")->currentIndex(),2);QVERIFY(controller.selectTrack(animation));QCOMPARE(properties.findChild<QTabBar *>("framePropertiesTabs")->currentIndex(),0);
    }
    void initialAudioSelectionOpensAudioProperties(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);QVERIFY(controller.selectAudioClip(clip(canvas).id));FramePropertiesPanel properties(&controller);properties.resize(240,600);properties.show();settle();QCOMPARE(properties.findChild<QTabBar *>("framePropertiesTabs")->currentIndex(),2);QCOMPARE(properties.findChild<QStackedWidget *>("framePropertiesPages")->currentIndex(),2);QVERIFY(properties.findChild<QLineEdit *>("audioClipName")->isVisible());
    }
    void audioBodyMoveAndBothTrimEdgesKeepSourceImmutable(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);TimelinePanel timeline(&controller);show(timeline);auto *view=grid(timeline);const auto original=clip(canvas);const int before=canvas.document()->history()->count();
        const auto from=point(view,3.5),to=point(view,6.5);QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,from);move(view,to);QCOMPARE(clip(canvas),original);QCOMPARE(canvas.document()->history()->count(),before);QVERIFY(controller.hasPendingEdits());QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,to);
        QCOMPARE(clip(canvas).startFrame,5);QCOMPARE(clip(canvas).sampleCount,original.sampleCount);QCOMPARE(canvas.document()->history()->count(),before+1);
        drag(view,point(view,5.1),point(view,7.1));QCOMPARE(clip(canvas).startFrame,7);QCOMPARE(clip(canvas).sourceOffsetSamples,original.sourceOffsetSamples+4000);QCOMPARE(clip(canvas).sampleCount,original.sampleCount-4000);QCOMPARE(canvas.document()->history()->count(),before+2);
        const qreal end=7+qreal(clip(canvas).sampleCount)*24/audioSampleRate;drag(view,point(view,end-.05),point(view,10));QCOMPARE(clip(canvas).sampleCount,qint64(6000));QCOMPARE(canvas.document()->history()->count(),before+3);QCOMPARE(clip(canvas).asset,original.asset);QCOMPARE(clip(canvas).asset->pcm,original.asset->pcm);
        canvas.document()->history()->undo();QCOMPARE(clip(canvas).sampleCount,original.sampleCount-4000);canvas.document()->history()->undo();QCOMPARE(clip(canvas).sourceOffsetSamples,original.sourceOffsetSamples);
    }
    void leftTrimCanRestorePreviouslyTrimmedSource(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);TimelinePanel timeline(&controller);show(timeline);auto *view=grid(timeline);const auto original=clip(canvas);const int before=canvas.document()->history()->count();
        drag(view,point(view,2.1),point(view,4.1));QCOMPARE(clip(canvas).sourceOffsetSamples,original.sourceOffsetSamples+4000);QCOMPARE(clip(canvas).sampleCount,original.sampleCount-4000);
        drag(view,point(view,4.1),point(view,2.1));QCOMPARE(clip(canvas),original);QCOMPARE(canvas.document()->history()->count(),before+2);
    }
    void propertiesPreservePartialSamplesUntilTrimIsChanged(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);QVERIFY(controller.selectAudioClip(clip(canvas).id));AudioPanel panel(&controller);panel.resize(240,500);panel.show();settle();const auto original=clip(canvas);
        QCOMPARE(number(panel,"audioClipDuration")->value(),7.0);number(panel,"audioClipVolume")->setValue(55.5);QCOMPARE(clip(canvas).gain,.555);QCOMPARE(clip(canvas).sampleCount,original.sampleCount);QCOMPARE(clip(canvas).sourceOffsetSamples,original.sourceOffsetSamples);
        number(panel,"audioClipStart")->setValue(11);QCOMPARE(clip(canvas).startFrame,10);QCOMPARE(clip(canvas).sampleCount,original.sampleCount);
        auto *name=panel.findChild<QLineEdit *>("audioClipName");name->setFocus();name->selectAll();QTest::keyClicks(name,"Dialogue edit");controller.finishEditing();QCOMPARE(clip(canvas).name,QString("Dialogue edit"));QCOMPARE(clip(canvas).sampleCount,original.sampleCount);QCOMPARE(clip(canvas).sourceOffsetSamples,original.sourceOffsetSamples);
        number(panel,"audioClipSourceIn")->setValue(1);QCOMPARE(clip(canvas).sourceOffsetSamples,qint64(2000));number(panel,"audioClipDuration")->setValue(3);QCOMPARE(clip(canvas).sampleCount,qint64(6000));QCOMPARE(clip(canvas).asset,original.asset);
    }
    void typedTimingSaveCancelAndVolumeDragUseOneUndo(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);QVERIFY(controller.selectAudioClip(clip(canvas).id));AudioPanel panel(&controller);panel.resize(240,500);panel.show();settle();const int before=canvas.document()->history()->count();
        type(panel,"audioClipStart","14");QVERIFY(controller.hasPendingEdits());for(int i=0;i<3;++i){emit controller.changed();QCOMPARE(number(panel,"audioClipStart")->findChild<QLineEdit *>()->text(),QString("14"));}QCOMPARE(clip(canvas).startFrame,2);controller.finishEditing();QCOMPARE(clip(canvas).startFrame,13);QCOMPARE(canvas.document()->history()->count(),before+1);
        QTemporaryDir directory;QString error;QVERIFY2(saveProject(canvas.project(),directory.filePath("audio.toonproj"),&error),qPrintable(error));const auto restored=loadProject(directory.filePath("audio.toonproj"),&error);QVERIFY2(restored.has_value(),qPrintable(error));QCOMPARE(restored->shots.first().audioClips.first().startFrame,13);
        canvas.document()->history()->undo();QCOMPARE(clip(canvas).startFrame,2);type(panel,"audioClipStart","9");controller.cancelEditing();QCOMPARE(clip(canvas).startFrame,2);QVERIFY(!controller.hasPendingEdits());
        auto *volume=field(panel,"audioClipVolume");auto *slider=volume->findChild<QSlider *>();const int history=canvas.document()->history()->index();slider->setSliderDown(true);slider->setValue(500);slider->setValue(600);QCOMPARE(clip(canvas).gain,.8);QCOMPARE(canvas.document()->history()->index(),history);slider->setSliderDown(false);QCOMPARE(clip(canvas).gain,.6);QCOMPARE(canvas.document()->history()->index(),history+1);
    }
    void dragCancelAndExternalDeletionDoNotCommitStaleAudio(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);TimelinePanel timeline(&controller);show(timeline);auto *view=grid(timeline);const int before=canvas.document()->history()->count();
        QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,point(view,3.5));move(view,point(view,6.5));QTest::keyClick(view,Qt::Key_Escape);QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,point(view,6.5));QCOMPARE(clip(canvas).startFrame,2);QCOMPARE(canvas.document()->history()->count(),before);
        QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,point(view,3.5));move(view,point(view,6.5));QVERIFY(controller.hasPendingEdits());QVERIFY(canvas.document()->removeAudioClip(clip(canvas).id));QVERIFY(!controller.hasPendingEdits());QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,point(view,6.5));QVERIFY(canvas.shot().audioClips.isEmpty());
    }
    void fractionalFrameRateTrimUsesSampleConversion(){
        Canvas canvas;canvas.setProject(fixture(23.976));TimelineController controller(&canvas);QVERIFY(controller.selectAudioClip(clip(canvas).id));AudioPanel panel(&controller);panel.resize(240,500);panel.show();settle();number(panel,"audioClipSourceIn")->setValue(2);QCOMPARE(clip(canvas).sourceOffsetSamples,audioSampleFrameAtTimelineFrame(2,23.976));number(panel,"audioClipDuration")->setValue(4);QCOMPARE(clip(canvas).sampleCount,audioSampleFrameAtTimelineFrame(4,23.976));
    }
    void mutePlaybackAndNarrowAccessibility(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);QVERIFY(controller.selectAudioClip(clip(canvas).id));FramePropertiesPanel panel(&controller);panel.resize(240,600);panel.show();TimelinePanel timeline(&controller);timeline.resize(240,330);timeline.show();settle();
        auto *mute=panel.findChild<QToolButton *>("audioClipMute");QVERIFY(mute);mute->click();QVERIFY(clip(canvas).muted);QCOMPARE(mute->accessibleName(),QString("Unmute this audio clip"));mute->click();QVERIFY(!clip(canvas).muted);
        QCOMPARE(panel.width(),240);QCOMPARE(timeline.width(),240);for(auto *spin:panel.findChildren<QDoubleSpinBox *>()){QCOMPARE(spin->width(),48);QVERIFY(spin->suffix().isEmpty());}
        for(auto *control:{mute,timeline.findChild<QToolButton *>("timelineImportAudio")}){QVERIFY(!control->icon().isNull());QVERIFY(!control->toolTip().isEmpty());QVERIFY(!control->accessibleName().isEmpty());QCOMPARE(control->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,control),600);}
        mute->click();QVERIFY(clip(canvas).muted);controller.setPlaying(true);QVERIFY(!field(panel,"audioClipStart")->isEnabled());QVERIFY(!mute->isEnabled());controller.setPlaying(false);QVERIFY(field(panel,"audioClipStart")->isEnabled());
    }
    void compactAudioTimingRowsKeepDraftDuringResize(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);QVERIFY(controller.selectAudioClip(clip(canvas).id));
        AudioPanel panel(&controller);panel.resize(240,550);panel.show();settle();
        for(const auto &id:QStringList{"audioClipStart","audioClipSourceIn","audioClipDuration"}){
            auto *row=panel.findChild<PropertyRow *>(id+"Row");QVERIFY(row);auto *control=field(panel,id);
            QVERIFY(row->height()<=control->height()+2);QVERIFY(control->findChild<QSlider *>()->width()>=76);
        }
        auto *mute=panel.findChild<QToolButton *>("audioClipMute");QCOMPARE(mute->iconSize(),QSize(16,16));QCOMPARE(mute->size(),QSize(28,28));
        const auto original=clip(canvas);const int count=canvas.document()->history()->count();type(panel,"audioClipStart","8");auto *spin=number(panel,"audioClipStart");
        QFont larger=panel.font();larger.setPixelSize(panel.fontMetrics().height()+6);panel.setFont(larger);
        for(int width:{320,190,240}){
            panel.resize(width,550);settle();QCOMPARE(panel.width(),width);QVERIFY(spin->hasFocus());
            QCOMPARE(spin->findChild<QLineEdit *>()->text(),QString("8"));QVERIFY(controller.hasPendingEdits());QCOMPARE(canvas.document()->history()->count(),count);
            auto *control=field(panel,"audioClipStart");QVERIFY(control->findChild<QSlider *>()->width()>=76);
            const QRect bounds(control->mapTo(&panel,QPoint()),control->size());QVERIFY(bounds.left()>=0&&bounds.right()<panel.width());
        }
        controller.cancelEditing();QCOMPARE(clip(canvas),original);QCOMPARE(canvas.document()->history()->count(),count);
    }
    void renderPreview(){
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");if(output.isEmpty())QSKIP("Opt-in audio timeline previews.");Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);QVERIFY(controller.selectAudioClip(clip(canvas).id));TimelinePanel timeline(&controller);show(timeline);FramePropertiesPanel properties(&controller);properties.resize(280,600);properties.show();settle();QCOMPARE(properties.findChild<QTabBar *>("framePropertiesTabs")->currentIndex(),2);QCOMPARE(properties.findChild<QStackedWidget *>("framePropertiesPages")->currentIndex(),2);QDir().mkpath(output);
        QVERIFY(timeline.grab().save(output+"/audio-timeline-wide.png"));QVERIFY(properties.grab().save(output+"/audio-properties-280.png"));timeline.resize(240,330);properties.resize(240,600);settle();QVERIFY(timeline.grab().save(output+"/audio-timeline-240.png"));QVERIFY(properties.grab().save(output+"/audio-properties-240.png"));
    }
};
QTEST_MAIN(AudioPanelTests)
#include "audio_panel_tests.moc"
