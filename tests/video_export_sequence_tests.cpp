#include "video_export.h"
#include "video_export_audio.h"
#include "video_export_queue.h"
#include "video_export_dialog.h"
#include "value_slider.h"
#include "ui_theme.h"
#include <QAudioDecoder>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QMediaPlayer>
#include <QSignalSpy>
#include <QTabBar>
#include <QTemporaryDir>
#include <QUuid>
#include <QVideoSink>
#include <QVideoFrame>
#include <QtEndian>
#include <QtTest>
#include <cmath>

using namespace toon;
namespace {
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QByteArray read(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
bool write(const QString &path, QByteArray bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
AudioAssetPtr tone(int amplitude, int frequency = 0) {
    QByteArray pcm(audioSampleRate * 2 * audioBytesPerSampleFrame, Qt::Uninitialized);
    for (int i = 0; i < audioSampleRate * 2; ++i) {
        const qint16 value = frequency ? qint16(std::lround(amplitude * std::sin(6.283185307179586 * frequency * i / audioSampleRate))) : qint16(amplitude);
        qToLittleEndian<qint16>(value, pcm.data() + i * 4);
        qToLittleEndian<qint16>(qint16(-value / 2), pcm.data() + i * 4 + 2);
    }
    return makeAudioAsset("Export audio", pcm);
}
Project movie(bool audio = false) {
    auto project = Project::create(); project.width = 96; project.height = 64; project.fps = 29.97;
    project.shots.clear();
    const QColor colors[]{Qt::red, Qt::green, Qt::blue};
    for (int i = 0; i < 3; ++i) {
        auto shot = Project::create().shots.first(); shot.number = QString("C%1").arg(i + 1); shot.name = QString("Cut %1").arg(i + 1);
        shot.durationFrames = 15 + i * 5;
        QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(colors[i]);
        shot.layers[0].tiles.insert({0, 0}, tile);
        if (audio) { const auto asset = tone((i + 1) * 3000, 220 + i * 110); shot.audioClips.append({id(), "Tone", asset, 0, 37, asset->sampleFrameCount() - 37, 1, false}); }
        project.shots.append(shot);
    }
    return project;
}
double rms(const QByteArray &pcm, double from, double until) {
    const int first = int(from * audioSampleRate), last = int(until * audioSampleRate);
    if (first < 0 || last * 4 > pcm.size() || last <= first) return -1;
    long double energy = 0;
    for (int i = first; i < last; ++i) { const auto value = qFromLittleEndian<qint16>(pcm.constData() + i * 4); energy += double(value) * value; }
    return std::sqrt(double(energy / (last - first)));
}
}
class VideoExportSequenceTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void dialogPreviews() {
        const auto folder = qEnvironmentVariable("TOON_UI_PREVIEW");
        if (folder.isEmpty()) QSKIP("Set TOON_UI_PREVIEW to capture movie and shot-queue dialogs.");
        QVERIFY(QDir().mkpath(folder)); const auto project = movie(true);
        VideoExportDialog dialog(project, 1, 2, 7, true); dialog.show();
        auto *scope = dialog.findChild<QTabBar *>("videoExportScope");
        for (int mode : {1, 2}) {
            scope->setCurrentIndex(mode);
            dialog.setDestination(mode == 1 ? QDir(folder).filePath("movie.mp4") : folder);
            if (mode == 2) dialog.setQueuedShotIds({project.shots[0].id, project.shots[2].id});
            for (int width : {520, 360}) {
                dialog.resize(width, mode == 2 ? 820 : 640); QTest::qWait(30);
                QCOMPARE(dialog.width(), width);
                QVERIFY(dialog.grab().save(QDir(folder).filePath(QString("video-export-%1-%2.png").arg(mode == 1 ? "movie" : "queue").arg(width))));
            }
        }
        dialog.reject();
    }
    void movieFramesFollowCutsAndGlobalRange() {
        const auto project = movie(); const auto metadata = project.toJson();
        auto settings = videoExportMovieDefaults(project);
        QCOMPARE(settings.scope, VideoExportScope::Movie); QCOMPARE(settings.lastFrame, 59);
        QVERIFY(validateVideoExportSettings(project, settings).isEmpty());
        for (const auto check : {QPair(0, QColor(Qt::red)), QPair(14, QColor(Qt::red)), QPair(15, QColor(Qt::green)),
                                 QPair(34, QColor(Qt::green)), QPair(35, QColor(Qt::blue)), QPair(59, QColor(Qt::blue))}) {
            const auto image = renderVideoExportFrame(project, settings, check.first);
            QVERIFY(!image.isNull()); QCOMPARE(image.pixelColor(48, 32), check.second);
        }
        settings.firstFrame = 14; settings.lastFrame = 35;
        QCOMPARE(videoExportSampleCount(project, settings), audioSampleFrameAtTimelineFrame(36, project.fps) - audioSampleFrameAtTimelineFrame(14, project.fps));
        QVERIFY(renderVideoExportFrame(project, settings, 13).isNull());
        QCOMPARE(videoExportFrameLocation(project, settings, 15).shotIndex, 1);
        QCOMPARE(videoExportFrameLocation(project, settings, 15).frame, 0);
        QCOMPARE(project.toJson(), metadata);
    }
    void manyFractionalCutsHaveExactBoundariesAndFiniteAudio() {
        auto project = movie(); project.shots.clear();
        const auto asset = tone(10000);
        for (int i = 0; i < 50; ++i) {
            auto shot = Project::create().shots.first(); shot.durationFrames = 1;
            shot.audioClips.append({id(), "Cut audio", asset, 0, 0, asset->sampleFrameCount(), (i + 1) / 100., false});
            project.shots.append(shot);
        }
        auto settings = videoExportMovieDefaults(project); settings.firstFrame = 1; settings.lastFrame = 48;
        VideoExportAudioStream stream(project, settings); QVERIFY(stream.valid());
        const auto expectedSamples = audioSampleFrameAtTimelineFrame(49, project.fps) - audioSampleFrameAtTimelineFrame(1, project.fps);
        QCOMPARE(stream.bytesAvailable(), expectedSamples * audioBytesPerSampleFrame);
        QByteArray pcm;
        while (!stream.atEnd()) { const auto chunk = stream.read(777); QVERIFY(!chunk.isEmpty()); pcm += chunk; }
        QCOMPARE(pcm.size(), expectedSamples * audioBytesPerSampleFrame); QCOMPARE(stream.read(100), QByteArray{});
        const auto base = audioSampleFrameAtTimelineFrame(1, project.fps);
        for (int shot = 1; shot <= 48; ++shot) {
            const auto first = audioSampleFrameAtTimelineFrame(shot, project.fps) - base;
            const auto end = audioSampleFrameAtTimelineFrame(shot + 1, project.fps) - base;
            for (const auto sample : {first, end - 1}) {
                QCOMPARE(qFromLittleEndian<qint16>(pcm.constData() + sample * 4), qint16((shot + 1) * 100));
                QCOMPARE(qFromLittleEndian<qint16>(pcm.constData() + sample * 4 + 2), qint16(-(shot + 1) * 50));
            }
        }
    }
    void movieMp4DecodesAllCutColorsAndStereoTiming() {
        if (!VideoExport::availabilityError(true).isEmpty()) QSKIP("H.264/AAC encoder unavailable");
        QTemporaryDir dir; auto project = movie(true); const auto metadata = project.toJson();
        auto settings = videoExportMovieDefaults(project); const auto path = dir.filePath("sequence.mp4");
        settings.overwriteApproved = false;
        VideoExport exporter; QSignalSpy finished(&exporter, &VideoExport::finished);
        QVERIFY(exporter.start(project, settings, path));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        const auto result = qvariant_cast<VideoExportResult>(finished.first().first());
        QVERIFY2(result.outcome == VideoExportOutcome::Completed, qPrintable(result.error)); QCOMPARE(result.framesEncoded, 60);
        QCOMPARE(project.toJson(), metadata);
        QMediaPlayer player; QVideoSink sink; player.setVideoSink(&sink);
        QString videoError; QSet<int> colors; int received = 0;
        connect(&player, &QMediaPlayer::errorOccurred, this, [&](QMediaPlayer::Error, const QString &message) { videoError = message; });
        connect(&sink, &QVideoSink::videoFrameChanged, this, [&](const QVideoFrame &frame) {
            if (!frame.isValid()) return;
            const auto image = frame.toImage(); if (image.isNull()) return;
            ++received; const auto c = image.pixelColor(image.width() / 2, image.height() / 2);
            const int channel = c.red() > c.green() && c.red() > c.blue() ? 0 : c.green() > c.blue() ? 1 : 2;
            colors.insert(channel);
            const int expected = frame.startTime() < videoExportFrameTimeUSecs(15, project.fps) - 1000 ? 0
                : frame.startTime() < videoExportFrameTimeUSecs(35, project.fps) - 1000 ? 1 : 2;
            QCOMPARE(channel, expected);
        });
        player.setSource(QUrl::fromLocalFile(path)); player.play();
        QTRY_VERIFY_WITH_TIMEOUT(player.mediaStatus() == QMediaPlayer::EndOfMedia || !videoError.isEmpty(), 15000);
        QVERIFY2(videoError.isEmpty(), qPrintable(videoError)); QCOMPARE(colors.size(), 3); QVERIFY(received >= 3);
        QVERIFY(qAbs(player.duration() - qRound64(60000 / project.fps)) <= 24);
        QAudioDecoder decoder; QAudioFormat format; format.setSampleRate(audioSampleRate);
        format.setChannelConfig(QAudioFormat::ChannelConfigStereo); format.setSampleFormat(QAudioFormat::Int16);
        decoder.setAudioFormat(format); QByteArray decoded; bool done = false; QString audioError;
        connect(&decoder, &QAudioDecoder::bufferReady, this, [&] { const auto buffer = decoder.read(); decoded.append(buffer.constData<char>(), buffer.byteCount()); });
        connect(&decoder, &QAudioDecoder::finished, this, [&] { done = true; });
        connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this, [&](QAudioDecoder::Error) { audioError = decoder.errorString(); });
        decoder.setSource(QUrl::fromLocalFile(path)); decoder.start();
        QTRY_VERIFY_WITH_TIMEOUT(done || !audioError.isEmpty(), 15000); QVERIFY2(audioError.isEmpty(), qPrintable(audioError));
        QVERIFY(qAbs(double(decoded.size()) / 4 / audioSampleRate - 60 / project.fps) < .06);
        const double starts[]{.15, .7, 1.4};
        for (int i = 0; i < 3; ++i) {
            const double expected = (i + 1) * 3000 / std::sqrt(2.);
            QVERIFY(qAbs(rms(decoded, starts[i], starts[i] + .15) - expected) < expected * .15);
        }
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot), QStringList{"sequence.mp4"});
    }
    void queueSkipsUnapprovedOutputsAndKeepsSnapshot() {
        if (!VideoExport::availabilityError(false).isEmpty()) QSKIP("H.264 encoder unavailable");
        QTemporaryDir dir; auto project = movie();
        QVector<VideoExportQueueItem> items;
        for (int i = 0; i < 3; ++i) {
            auto settings = videoExportDefaults(project, i); settings.lastFrame = 1;
            items.append({settings, dir.filePath(videoExportShotFileName(project, i)), false, {}});
        }
        QVERIFY(write(items[0].destination, "original"));
        VideoExportQueue queue; QSignalSpy finished(&queue, &VideoExportQueue::finished);
        QString error; QVERIFY2(queue.start(project, items, &error), qPrintable(error));
        project.shots.clear(); // The running queue owns its snapshot.
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        const auto report = qvariant_cast<VideoExportQueueReport>(finished.first().first());
        QCOMPARE(report.outcome, VideoExportOutcome::Completed); QCOMPARE(report.completedJobs, 2); QCOMPARE(report.skippedJobs, 1);
        QCOMPARE(read(items[0].destination), QByteArray("original"));
        QVERIFY(read(items[1].destination).contains("ftyp")); QVERIFY(read(items[2].destination).contains("ftyp"));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).size(), 3);
    }
    void queueCancellationStopsRemainingJobsAndPreservesExistingOutput() {
        if (!VideoExport::availabilityError(false).isEmpty()) QSKIP("H.264 encoder unavailable");
        QTemporaryDir dir; auto project = movie(); project.shots[0].durationFrames = 100000;
        auto first = videoExportDefaults(project, 0), second = videoExportDefaults(project, 1);
        const QString path = dir.filePath("first.mp4"); QVERIFY(write(path, "keep"));
        VideoExportQueue queue; QSignalSpy finished(&queue, &VideoExportQueue::finished);
        connect(&queue, &VideoExportQueue::progress, this, [&](int, int, int completed, int, const QString &) { if (completed > 0) queue.cancel(); });
        QVERIFY(queue.start(project, {{first, path, true, {}}, {second, dir.filePath("second.mp4"), false, {}}}));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        const auto report = qvariant_cast<VideoExportQueueReport>(finished.first().first());
        QCOMPARE(report.outcome, VideoExportOutcome::Cancelled); QCOMPARE(report.completedJobs, 0);
        QCOMPARE(read(path), QByteArray("keep")); QVERIFY(!QFileInfo::exists(dir.filePath("second.mp4")));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot), QStringList{"first.mp4"});
    }
    void queueFailureKeepsCompletedFilesAndDoesNotStartLaterJobs() {
        if (!VideoExport::availabilityError(false).isEmpty()) QSKIP("H.264 encoder unavailable");
        QTemporaryDir dir; auto project = movie(); QVERIFY(QDir(dir.path()).mkdir("later"));
        QVector<VideoExportQueueItem> items;
        for (int i = 0; i < 3; ++i) {
            auto settings = videoExportDefaults(project, i); settings.firstFrame = settings.lastFrame = 0;
            items.append({settings, i == 1 ? dir.filePath("later/second.mp4") : dir.filePath(QString("%1.mp4").arg(i)), false, {}});
        }
        VideoExportQueue queue; QSignalSpy finished(&queue, &VideoExportQueue::finished);
        connect(&queue, &VideoExportQueue::itemFinished, this, [&](int index, const VideoExportResult &) {
            if (index == 0) QVERIFY(QDir(dir.path()).rmdir("later"));
        });
        QVERIFY(queue.start(project, items)); QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        const auto report = qvariant_cast<VideoExportQueueReport>(finished.first().first());
        QCOMPARE(report.outcome, VideoExportOutcome::Failed); QCOMPARE(report.completedJobs, 1);
        QVERIFY(read(items[0].destination).contains("ftyp")); QVERIFY(!QFileInfo::exists(items[2].destination));
    }
    void newlyOccupiedMovieDestinationIsNeverOverwritten() {
        if (!VideoExport::availabilityError(false).isEmpty()) QSKIP("H.264 encoder unavailable");
        QTemporaryDir dir; const auto project = movie(); const auto path = dir.filePath("racing.mp4");
        auto settings = videoExportMovieDefaults(project); settings.overwriteApproved = false;
        VideoExport exporter; QSignalSpy finished(&exporter, &VideoExport::finished); bool occupied = false;
        connect(&exporter, &VideoExport::progress, this, [&](int completed, int, const QString &) {
            if (completed > 0 && !occupied) { occupied = true; QVERIFY(write(path, "other writer")); }
        });
        QVERIFY(exporter.start(project, settings, path)); QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        QVERIFY(occupied); QCOMPARE(qvariant_cast<VideoExportResult>(finished.first().first()).outcome, VideoExportOutcome::Failed);
        QCOMPARE(read(path), QByteArray("other writer"));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot), QStringList{"racing.mp4"});
    }
    void movieCancellationPreservesApprovedExistingFile() {
        if (!VideoExport::availabilityError(false).isEmpty()) QSKIP("H.264 encoder unavailable");
        QTemporaryDir dir; auto project = movie(); project.shots[1].durationFrames = 100000;
        const auto path = dir.filePath("movie.mp4"); QVERIFY(write(path, "original movie"));
        auto settings = videoExportMovieDefaults(project); settings.overwriteApproved = true;
        VideoExport exporter; QSignalSpy finished(&exporter, &VideoExport::finished);
        connect(&exporter, &VideoExport::progress, this, [&](int completed, int, const QString &) { if (completed > 0) exporter.cancel(); });
        QVERIFY(exporter.start(project, settings, path)); QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        QCOMPARE(qvariant_cast<VideoExportResult>(finished.first().first()).outcome, VideoExportOutcome::Cancelled);
        QCOMPARE(read(path), QByteArray("original movie"));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot), QStringList{"movie.mp4"});
    }
    void outputNamesAreSafeAndQueueRejectsAliases() {
        auto project = movie(); project.shots[0].number = "../CON"; project.shots[0].name = "../same\\name";
        project.shots[1].number = project.shots[0].number; project.shots[1].name = project.shots[0].name;
        const auto first = videoExportShotFileName(project, 0), second = videoExportShotFileName(project, 1);
        QVERIFY(!first.contains('/')); QVERIFY(!first.contains('\\')); QVERIFY(!first.contains("..")); QVERIFY(first != second);
        QTemporaryDir dir; VideoExportQueue queue; QString error;
        const auto settings = videoExportDefaults(project);
        QVERIFY(!queue.start(project, {{settings, dir.filePath("same.mp4")}, {settings, dir.filePath("SAME.mp4")}}, &error));
        QVERIFY(error.contains("different output")); QVERIFY(!queue.busy());
    }
    void dialogRemembersPerShotSettingsAndSelectedIds() {
        auto project = movie(true); QTemporaryDir dir;
        VideoExportDialog dialog(project, 1, 2, 7, true);
        QCOMPARE(dialog.settings().shotIndex, 1); QCOMPARE(dialog.settings().lastFrame, 19);
        dialog.findChild<QTabBar *>("videoExportScope")->setCurrentIndex(1);
        QCOMPARE(dialog.settings().scope, VideoExportScope::Movie); QCOMPARE(dialog.settings().lastFrame, 59);
        dialog.setQueuedShotIds({project.shots[0].id, project.shots[2].id}); dialog.setDestination(dir.path());
        auto *shots = dialog.findChild<QListWidget *>("videoExportQueueShots"); QCOMPARE(shots->currentRow(), 0);
        dialog.findChild<QTabBar *>("videoExportRange")->setCurrentIndex(1);
        dialog.findChild<ValueSlider *>("videoExportFirst")->setValue(3); dialog.findChild<ValueSlider *>("videoExportLast")->setValue(8);
        dialog.findChild<ValueSlider *>("videoExportWidth")->setValue(192);
        dialog.findChild<QTabBar *>("videoExportQuality")->setCurrentIndex(0);
        dialog.findChild<QCheckBox *>("videoExportAudio")->setChecked(false);
        shots->setCurrentRow(2);
        QCOMPARE(dialog.settings().shotIndex, 2); QCOMPARE(dialog.settings().lastFrame, 24);
        dialog.findChild<QTabBar *>("videoExportQuality")->setCurrentIndex(1);
        shots->setCurrentRow(0);
        QCOMPARE(dialog.settings().firstFrame, 2); QCOMPARE(dialog.settings().lastFrame, 7);
        QCOMPARE(dialog.settings().size, QSize(192, 128)); QVERIFY(!dialog.settings().includeAudio);
        const auto items = dialog.queueItems(); QCOMPARE(items.size(), 2);
        QCOMPARE(items[0].settings.quality, VideoExportQuality::Draft); QCOMPARE(items[1].settings.quality, VideoExportQuality::Standard);
        QCOMPARE(items[0].destination, dir.filePath(videoExportShotFileName(project, 0)));
        QCOMPARE(items[1].settings.shotIndex, 2); QVERIFY(!items[0].overwriteApproved);
        dialog.findChild<QTabBar *>("videoExportScope")->setCurrentIndex(0);
        QCOMPARE(dialog.settings().shotIndex, 1); QCOMPARE(dialog.settings().lastFrame, 19);
    }
};
QTEST_MAIN(VideoExportSequenceTests)
#include "video_export_sequence_tests.moc"
