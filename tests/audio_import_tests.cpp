#include "audio_import.h"
#include "canvas.h"
#include "timeline_controller.h"
#include <QDataStream>
#include <QDir>
#include <QDialog>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
using namespace toon;
namespace {
bool wave(const QString &path) {
    QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
    constexpr int samples=11025, bytes=samples*2;
    QDataStream stream(&file);stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF",4);stream<<quint32(36+bytes);stream.writeRawData("WAVEfmt ",8);
    stream<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(44100)<<quint32(88200)<<quint16(2)<<quint16(16);
    stream.writeRawData("data",4);stream<<quint32(bytes);
    for(int i=0;i<samples;++i)stream<<qint16(std::sin(i*2*3.141592653589793*440/44100)*8000);
    return stream.status()==QDataStream::Ok;
}
}
class AudioImportTests final:public QObject {
    Q_OBJECT
private slots:
    void waveConvertsRateAndChannels(){
        QTemporaryDir dir;const auto path=dir.filePath("Voice.wav");QVERIFY(wave(path));
        AudioImport importer;AudioAssetPtr result;QString error;
        connect(&importer,&AudioImport::completed,this,[&](AudioAssetPtr asset){result=asset;});
        connect(&importer,&AudioImport::failed,this,[&](const QString &message){error=message;});
        QVERIFY(importer.start(path));QTRY_VERIFY_WITH_TIMEOUT(!importer.busy(),15000);QVERIFY2(result,qPrintable(error));
        // The decoder's rate converter can retain a sub-millisecond filter tail,
        // and mono-to-stereo conversion may apply center-channel attenuation.
        QCOMPARE(result->name,QString("Voice"));QVERIFY2(qAbs(result->sampleFrameCount()-12000)<=48,qPrintable(QString::number(result->sampleFrameCount())));
        const auto peak=audioPeak(*result,0,result->sampleFrameCount());QVERIFY(peak.maximum>.1f);QVERIFY(peak.minimum<-.1f);
        for(qint64 sample=0;sample<result->sampleFrameCount();++sample)
            QCOMPARE(result->pcm.mid(sample*4,2),result->pcm.mid(sample*4+2,2));
    }
    void cancellationAndMalformedInput(){
        QTemporaryDir dir;const auto path=dir.filePath("Sound.wav");QVERIFY(wave(path));
        AudioImport importer;int completed=0,failed=0;
        connect(&importer,&AudioImport::completed,this,[&]{++completed;});connect(&importer,&AudioImport::failed,this,[&]{++failed;});
        QVERIFY(importer.start(path));importer.cancel();QTest::qWait(100);QVERIFY(!importer.busy());QCOMPARE(completed,0);QCOMPARE(failed,0);
        QVERIFY(!importer.start(dir.filePath("missing.wav")));QCOMPARE(failed,1);
        QFile invalid(dir.filePath("bad.mp3"));QVERIFY(invalid.open(QIODevice::WriteOnly));invalid.write("not audio");invalid.close();
        importer.start(invalid.fileName());QTRY_VERIFY_WITH_TIMEOUT(!importer.busy(),15000);QCOMPARE(completed,0);QCOMPARE(failed,2);
    }
    void controllerImportEmbedsAtRequestedTimeAndLoadCancels(){
        QTemporaryDir dir;const auto path=dir.filePath("Dialogue.wav");QVERIFY(wave(path));
        Canvas canvas;auto project=Project::create();project.width=project.height=128;canvas.setProject(project);
        TimelineController controller(&canvas);QVERIFY(controller.setFrame(8));const auto layer=controller.selectedPropertyLayerId();
        QVERIFY(controller.importAudioFile(path));QVERIFY(!controller.importAudioFile(path));QVERIFY(controller.setFrame(10));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.audioImportBusy(),15000);
        QCOMPARE(canvas.shot().audioClips.size(),1);const auto clip=canvas.shot().audioClips.first();
        QCOMPARE(clip.startFrame,8);QCOMPARE(controller.selectedAudioClipId(),clip.id);QCOMPARE(controller.selectedPropertyLayerId(),layer);
        QCOMPARE(canvas.document()->history()->count(),1);canvas.document()->history()->undo();QVERIFY(canvas.shot().audioClips.isEmpty());
        canvas.document()->history()->redo();QCOMPARE(canvas.shot().audioClips.first().asset,clip.asset);
        QVERIFY(controller.importAudioFile(path));canvas.setProject(project);QTest::qWait(150);
        QVERIFY(!controller.audioImportBusy());QVERIFY(canvas.shot().audioClips.isEmpty());
    }
    void cancellationAtPeakPreparationKeepsNextImportIndependent(){
        QTemporaryDir dir;const auto path=dir.filePath("Sound.wav");QVERIFY(wave(path));
        AudioImport importer;bool cancelled=false;int completed=0,failed=0;
        connect(&importer,&AudioImport::progress,this,[&](int progress){if(progress==96&&!cancelled){cancelled=true;importer.cancel();}});
        connect(&importer,&AudioImport::completed,this,[&]{++completed;});connect(&importer,&AudioImport::failed,this,[&]{++failed;});
        QVERIFY(importer.start(path));QTRY_VERIFY_WITH_TIMEOUT(cancelled,15000);QVERIFY(!importer.busy());
        QCOMPARE(completed,0);QCOMPARE(failed,0);QVERIFY(importer.start(path));
        QTRY_COMPARE_WITH_TIMEOUT(completed,1,15000);QVERIFY(!importer.busy());QCOMPARE(failed,0);
    }
    void decodedImportWaitsForStrokeAndPreservesIndependentUndo(){
        QTemporaryDir dir;const auto path=dir.filePath("Dialogue.wav");QVERIFY(wave(path));
        Canvas canvas;auto project=Project::create();project.width=project.height=128;canvas.setProject(project);
        TimelineController controller(&canvas);auto *importer=controller.findChild<AudioImport *>();QVERIFY(importer);
        bool decoded=false;connect(importer,&AudioImport::completed,this,[&]{decoded=true;});
        QVERIFY(controller.importAudioFile(path));BrushSettings brush;brush.size=8;
        QVERIFY(canvas.document()->beginStroke({20,20},1,brush,false));
        QTRY_VERIFY_WITH_TIMEOUT(decoded,15000);QTest::qWait(120);
        QVERIFY(canvas.document()->strokeActive());QVERIFY(controller.audioImportBusy());QVERIFY(controller.audioImportStatus().contains("waiting"));
        QVERIFY(canvas.shot().audioClips.isEmpty());QCOMPARE(canvas.document()->history()->count(),0);
        QVERIFY(!controller.importAudioFile(path));canvas.document()->continueStroke({50,20},1);canvas.document()->endStroke();
        const auto drawing=canvas.project().toJson();QCOMPARE(canvas.document()->history()->count(),1);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.audioImportBusy(),15000);QCOMPARE(canvas.shot().audioClips.size(),1);
        QCOMPARE(canvas.document()->history()->count(),2);canvas.document()->history()->undo();QCOMPARE(canvas.project().toJson(),drawing);
        canvas.document()->history()->redo();QCOMPARE(canvas.shot().audioClips.size(),1);
    }
    void modalSnapshotDefersImportAndWaitingResultCanBeCancelledOrReloaded(){
        QTemporaryDir dir;const auto path=dir.filePath("Dialogue.wav");QVERIFY(wave(path));
        Canvas canvas;auto project=Project::create();project.width=project.height=128;canvas.setProject(project);
        TimelineController controller(&canvas);auto *importer=controller.findChild<AudioImport *>();QVERIFY(importer);
        int decoded=0;connect(importer,&AudioImport::completed,this,[&]{++decoded;});
        QDialog modal;modal.setModal(true);
        QVERIFY(controller.importAudioFile(path));modal.show();QTRY_COMPARE(QApplication::activeModalWidget(),&modal);
        QTRY_COMPARE_WITH_TIMEOUT(decoded,1,15000);QTest::qWait(120);QVERIFY(controller.audioImportBusy());QVERIFY(canvas.shot().audioClips.isEmpty());
        project.movie="Settings accepted";canvas.document()->replaceProject(project,"Project settings");modal.accept();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.audioImportBusy(),15000);QCOMPARE(canvas.shot().audioClips.size(),1);QCOMPARE(canvas.project().movie,project.movie);
        const auto saved=canvas.project();const int history=canvas.document()->history()->count();
        QVERIFY(controller.importAudioFile(path));modal.show();QTRY_COMPARE_WITH_TIMEOUT(decoded,2,15000);QTest::qWait(120);
        controller.cancelAudioImport();QVERIFY(!controller.audioImportBusy());QVERIFY(controller.audioImportStatus().isEmpty());modal.accept();QTest::qWait(120);
        QCOMPARE(canvas.project().toJson(),saved.toJson());QCOMPARE(canvas.document()->history()->count(),history);
        QVERIFY(controller.importAudioFile(path));modal.show();QTRY_COMPARE_WITH_TIMEOUT(decoded,3,15000);QVERIFY(controller.audioImportBusy());
        canvas.setProject(saved);QVERIFY(!controller.audioImportBusy());modal.accept();QTest::qWait(120);QCOMPARE(canvas.project().toJson(),saved.toJson());
    }
    void pendingEditorsAndPlaybackDeferImportUntilIdle(){
        QTemporaryDir dir;const auto path=dir.filePath("Dialogue.wav");QVERIFY(wave(path));
        Canvas canvas;auto project=Project::create();project.width=project.height=128;canvas.setProject(project);
        TimelineController controller(&canvas);auto *importer=controller.findChild<AudioImport *>();QVERIFY(importer);
        bool decoded=false;connect(importer,&AudioImport::completed,this,[&]{decoded=true;});
        QVERIFY(controller.importAudioFile(path));controller.setPendingEdits(true);
        QTRY_VERIFY_WITH_TIMEOUT(decoded,15000);QTest::qWait(120);QVERIFY(controller.hasPendingEdits());QVERIFY(canvas.shot().audioClips.isEmpty());
        controller.setPendingEdits(false);controller.setPlaying(true);QVERIFY(controller.playing());QTest::qWait(120);
        QVERIFY(controller.audioImportBusy());QVERIFY(canvas.shot().audioClips.isEmpty());controller.setPlaying(false);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.audioImportBusy(),15000);QCOMPARE(canvas.shot().audioClips.size(),1);
    }
    void compressedFixtures(){
        const auto path=qEnvironmentVariable("TOON_AUDIO_FIXTURES");if(path.isEmpty())QSKIP("Opt-in codec fixtures generated in build/test-artifacts.");
        const auto files=QDir(path).entryList({"*.mp3","*.m4a","*.flac","*.ogg"},QDir::Files);QCOMPARE(files.size(),4);
        for(const auto &file:files){AudioImport importer;AudioAssetPtr result;QString error;
            connect(&importer,&AudioImport::completed,this,[&](AudioAssetPtr asset){result=asset;});
            connect(&importer,&AudioImport::failed,this,[&](const QString &message){error=message;});
            QVERIFY(importer.start(QDir(path).filePath(file)));QTRY_VERIFY_WITH_TIMEOUT(!importer.busy(),15000);
            QVERIFY2(result,qPrintable(file+": "+error));QVERIFY(result->sampleFrameCount()>10000);
        }
    }
};
QTEST_MAIN(AudioImportTests)
#include "audio_import_tests.moc"
