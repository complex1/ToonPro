#include "audio_export.h"
#include "audio.h"
#include "video_export_audio.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUuid>
#include <QtEndian>
#include <QtTest>
#include <cstring>
#include <cmath>

using namespace toon;
namespace {
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QByteArray bytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
bool write(const QString &path, const QByteArray &data) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size(); }
Project fixture(double fps = 24) {
    auto project = Project::create(); project.width = project.height = 128; project.fps = fps; project.shots[0].durationFrames = 72;
    QByteArray pcm(audioSampleRate * 4 * 4, Qt::Uninitialized);
    for (int i = 0; i < pcm.size() / 4; ++i) {
        qToLittleEndian<qint16>(qint16(4000 + i % 1000), pcm.data() + i * 4);
        qToLittleEndian<qint16>(qint16(-6000 - i % 500), pcm.data() + i * 4 + 2);
    }
    auto asset = makeAudioAsset("Stereo", pcm);
    project.shots[0].audioClips = {{uuid(), "Trimmed", asset, 12, 1234, 72000, .5, false},
                                 {uuid(), "Overlap", asset, 24, 4000, 48000, .25, false},
                                 {uuid(), "Muted", asset, 0, 0, 144000, 2, true}};
    return project;
}
AudioExportResult resultOf(QSignalSpy &finished) { return qvariant_cast<AudioExportResult>(finished.last().first()); }
quint32 u32(const QByteArray &data, int offset) { return qFromLittleEndian<quint32>(data.constData() + offset); }
quint16 u16(const QByteArray &data, int offset) { return qFromLittleEndian<quint16>(data.constData() + offset); }
QByteArray littleEndianStream(VideoExportAudioStream &stream) {
    QByteArray data = stream.readAll();
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    for (int i = 0; i < data.size(); i += 2) { qint16 value; std::memcpy(&value, data.constData() + i, 2); qToLittleEndian(value, data.data() + i); }
#endif
    return data;
}
}
class AudioExportTest final : public QObject {
    Q_OBJECT
private slots:
    void mixedStereoRangeHasExactHeaderAndPlaybackMix() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto project = fixture(29.97); const auto original = project.toJson();
        auto settings = audioExportDefaults(project); settings.firstFrame = 6; settings.lastFrame = 65;
        const auto samples = audioExportSampleCount(project, settings); QCOMPARE(samples, qint64(96096));
        AudioExport exporter; QSignalSpy finished(&exporter, &AudioExport::finished), progress(&exporter, &AudioExport::progress);
        const auto path = dir.filePath("stereo.wav"); QVERIFY(exporter.start(project, settings, path)); QVERIFY(exporter.busy());
        QVERIFY(!exporter.start(project, settings, dir.filePath("second.wav")));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        const auto result = resultOf(finished); QCOMPARE(result.outcome, AudioExportOutcome::Completed);
        QCOMPARE(result.path, path); QCOMPARE(result.sampleFrames, samples); QVERIFY(!exporter.busy());
        const auto data = bytes(path); QCOMPARE(data.size(), samples * 4 + 44);
        QCOMPARE(data.first(4), QByteArray("RIFF")); QCOMPARE(data.mid(8, 8), QByteArray("WAVEfmt "));
        QCOMPARE(u32(data, 4), quint32(data.size() - 8)); QCOMPARE(u32(data, 16), 16U);
        QCOMPARE(u16(data, 20), 1); QCOMPARE(u16(data, 22), 2); QCOMPARE(u32(data, 24), 48000U);
        QCOMPARE(u32(data, 28), 192000U); QCOMPARE(u16(data, 32), 4); QCOMPARE(u16(data, 34), 16);
        QCOMPARE(data.mid(36, 4), QByteArray("data")); QCOMPARE(u32(data, 40), quint32(samples * 4));
        auto video = videoExportDefaults(project); video.firstFrame = settings.firstFrame; video.lastFrame = settings.lastFrame;
        VideoExportAudioStream stream(project, video); QCOMPARE(data.mid(44), littleEndianStream(stream));
        const auto audio = data.mid(44); QCOMPARE(audio.first(100), QByteArray(100, '\0'));
        const auto absolute = audioSampleFrameAtTimelineFrame(18, project.fps);
        const auto relative = absolute - audioSampleFrameAtTimelineFrame(6, project.fps);
        const auto source = 1234 + absolute - audioSampleFrameAtTimelineFrame(12, project.fps);
        QCOMPARE(qFromLittleEndian<qint16>(audio.constData() + relative * 4), qint16(qRound((4000 + source % 1000) * .5)));
        QCOMPARE(qFromLittleEndian<qint16>(audio.constData() + relative * 4 + 2), qint16(-qRound((6000 + source % 500) * .5)));
        qint64 previous = 0;
        for (const auto &event : progress) { const auto current = event[0].toLongLong(); QVERIFY(current >= previous); QVERIFY(current <= samples); QCOMPARE(event[1].toLongLong(), samples); previous = current; }
        QCOMPARE(previous, samples); QCOMPARE(project.toJson(), original);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size(), 1);
    }
    void movieCutsUseGlobalFractionalSampleBoundaries() {
        auto project = fixture(23.976); project.shots[0].durationFrames = 31;
        auto second = Project::create().shots[0]; second.number = "C002"; second.durationFrames = 43;
        QByteArray pcm(96000 * 4, Qt::Uninitialized);
        for (int i = 0; i < 96000; ++i) { qToLittleEndian<qint16>(14000, pcm.data() + i * 4); qToLittleEndian<qint16>(-14000, pcm.data() + i * 4 + 2); }
        auto asset = makeAudioAsset("Second", pcm); second.audioClips = {{uuid(), "Second clip", asset, 0, 0, 96000, .5, false}};
        project.shots.append(second); QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        QTemporaryDir dir; auto settings = audioExportMovieDefaults(project); settings.firstFrame = 29; settings.lastFrame = 35;
        AudioExport exporter; QSignalSpy finished(&exporter, &AudioExport::finished);
        QVERIFY(exporter.start(project, settings, dir.filePath("movie.wav"))); QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QCOMPARE(resultOf(finished).outcome, AudioExportOutcome::Completed);
        auto video = videoExportMovieDefaults(project); video.firstFrame = 29; video.lastFrame = 35;
        VideoExportAudioStream stream(project, video);
        const auto data = bytes(dir.filePath("movie.wav")).mid(44); QCOMPARE(data, littleEndianStream(stream));
        const auto cut = audioSampleFrameAtTimelineFrame(31, project.fps) - audioSampleFrameAtTimelineFrame(29, project.fps);
        QCOMPARE(qFromLittleEndian<qint16>(data.constData() + cut * 4), 7000);
        QCOMPARE(qFromLittleEndian<qint16>(data.constData() + cut * 4 + 2), -7000);
        const auto count = audioSampleFrameAtTimelineFrame(36, project.fps) - audioSampleFrameAtTimelineFrame(29, project.fps);
        QCOMPARE(data.size(), count * 4);
    }
    void fadesAutomationAndOverlapAreWrittenToTheWav() {
        auto project = Project::create(); project.fps = 24; project.shots[0].durationFrames = 48;
        QByteArray pcm(48001 * 4, Qt::Uninitialized);
        for (int i = 0; i < 48001; ++i) { qToLittleEndian<qint16>(10000, pcm.data() + i * 4); qToLittleEndian<qint16>(-10000, pcm.data() + i * 4 + 2); }
        auto asset = makeAudioAsset("Constant stereo", pcm);
        AudioClip a{uuid(), "Automated fade", asset, 0, 0, 48000, 0, false};
        a.fadeInSamples = a.fadeOutSamples = 12001; a.fadeOutCurve = AudioFadeCurve::EqualPower;
        a.gainAnimated = true; a.gainKeys = {{0, .5}, {48000, 1.5}};
        AudioClip b{uuid(), "Crossfade overlap", asset, 18, 0, 24000, .25, false};
        b.fadeInSamples = 12001; b.fadeInCurve = AudioFadeCurve::EqualPower;
        project.shots[0].audioClips = {a, b};
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        QTemporaryDir dir; AudioExport exporter; QSignalSpy finished(&exporter, &AudioExport::finished);
        QVERIFY(exporter.start(project, audioExportDefaults(project), dir.filePath("fades.wav")));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000); QCOMPARE(resultOf(finished).outcome, AudioExportOutcome::Completed);
        const auto pcmOut = bytes(dir.filePath("fades.wav")).mid(44);
        auto sample = [&](int frame) { return qFromLittleEndian<qint16>(pcmOut.constData() + frame * 4); };
        QCOMPARE(sample(0), 0); QCOMPARE(sample(6000), 3125); QCOMPARE(sample(12000), 7500);
        const auto expected = qint16(std::lround(10000 * (1.375 * std::sin(5999. / 12000 * 1.5707963267948966)
            + .25 * std::sin(.5 * 1.5707963267948966))));
        QCOMPARE(sample(42000), expected);
        QCOMPARE(qFromLittleEndian<qint16>(pcmOut.constData() + 42000 * 4 + 2), -expected);
        QCOMPARE(sample(48000), 2500); QCOMPARE(sample(60000), 0); QCOMPARE(sample(95999), 0);
    }
    void invalidRangesAndRiffLimitFailBeforeFileCreation() {
        QTemporaryDir dir; auto project = fixture(); auto settings = audioExportDefaults(project);
        settings.firstFrame = 30; settings.lastFrame = 12; QVERIFY(!validateAudioExportSettings(project, settings).isEmpty());
        settings = audioExportDefaults(project); settings.shotIndex = 9; QVERIFY(!validateAudioExportSettings(project, settings).isEmpty());
        settings = audioExportDefaults(project); settings.scope = AudioExportScope(42); QVERIFY(!validateAudioExportSettings(project, settings).isEmpty());
        project.shots[0].durationFrames = 1000000; project.fps = 1; settings = audioExportDefaults(project);
        QVERIFY(validateAudioExportSettings(project, settings).contains("WAV file size limit"));
        AudioExport exporter; QSignalSpy finished(&exporter, &AudioExport::finished);
        QVERIFY(exporter.start(project, settings, dir.filePath("oversize.wav"))); QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(resultOf(finished).outcome, AudioExportOutcome::Failed); QVERIFY(!QFileInfo::exists(dir.filePath("oversize.wav")));
        QVERIFY(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
    }
    void existingOutputNeedsExplicitApprovalAndCanRetry() {
        QTemporaryDir dir; const auto path = dir.filePath("existing.wav"); QVERIFY(write(path, "original"));
        auto project = fixture(); auto settings = audioExportDefaults(project); settings.lastFrame = 1;
        AudioExport exporter; QSignalSpy finished(&exporter, &AudioExport::finished);
        QVERIFY(exporter.start(project, settings, path)); QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(resultOf(finished).outcome, AudioExportOutcome::Failed); QCOMPARE(bytes(path), QByteArray("original"));
        settings.overwriteApproved = true; QVERIFY(exporter.start(project, settings, path)); QTRY_COMPARE(finished.count(), 2);
        QCOMPARE(resultOf(finished).outcome, AudioExportOutcome::Completed); QCOMPARE(bytes(path).first(4), QByteArray("RIFF"));
        QVERIFY(exporter.start(project, settings, dir.filePath("missing/audio.wav"))); QTRY_COMPARE(finished.count(), 3);
        QCOMPARE(resultOf(finished).outcome, AudioExportOutcome::Failed);
    }
    void cancelPreservesExistingDestinationAndRemovesTemporaryFiles() {
        QTemporaryDir dir; const auto path = dir.filePath("cancel.wav"); QVERIFY(write(path, "preserve"));
        auto project = fixture(); project.shots[0].durationFrames = 100000; auto settings = audioExportDefaults(project); settings.overwriteApproved = true;
        AudioExport exporter; QSignalSpy finished(&exporter, &AudioExport::finished);
        QVERIFY(exporter.start(project, settings, path)); exporter.cancel(); QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QCOMPARE(resultOf(finished).outcome, AudioExportOutcome::Cancelled); QCOMPARE(bytes(path), QByteArray("preserve"));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden), QStringList{"cancel.wav"});
        settings.overwriteApproved = false; finished.clear(); QVERIFY(exporter.start(project, settings, dir.filePath("new.wav"))); exporter.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000); QCOMPARE(resultOf(finished).outcome, AudioExportOutcome::Cancelled);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden), QStringList{"cancel.wav"});
    }
    void outputAppearingDuringExportIsNotOverwritten() {
        QTemporaryDir dir; const auto path = dir.filePath("raced.wav"); auto project = fixture();
        AudioExport exporter; QSignalSpy finished(&exporter, &AudioExport::finished);
        // This slot runs on the worker's first progress event, after staging was
        // opened but before publication, reproducing the other-writer race.
        bool created = false;
        connect(&exporter, &AudioExport::progress, &exporter, [&](qint64 completed, qint64) {
            if (!completed && !created) created = write(path, "other program");
        }, Qt::DirectConnection);
        QVERIFY(exporter.start(project, audioExportDefaults(project), path)); QTRY_COMPARE(finished.count(), 1);
        QVERIFY(created); QCOMPARE(resultOf(finished).outcome, AudioExportOutcome::Failed);
        QCOMPARE(bytes(path), QByteArray("other program")); QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden), QStringList{"raced.wav"});
    }
};
QTEST_MAIN(AudioExportTest)
#include "audio_export_tests.moc"
