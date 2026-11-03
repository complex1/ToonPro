#include "audio_mix.h"
#include "audio_playback.h"
#include "movie_playback_sequence.h"
#include "project.h"
#include "video_export_audio.h"
#include <QSignalSpy>
#include <QUuid>
#include <QtEndian>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

using namespace toon;
namespace {
Project movie() {
    auto project = Project::create();
    project.fps = 120;
    project.shots[0].scene = "A"; project.shots[0].durationFrames = 2;
    for (int i = 1; i < 4; ++i) {
        auto shot = Project::create().shots.first();
        shot.number = QString("C%1").arg(i + 1, 3, 10, QLatin1Char('0'));
        shot.scene = i == 2 ? "A" : i == 1 ? "B" : "C";
        shot.durationFrames = i + 2;
        project.shots.append(shot);
    }
    return project;
}
AudioAssetPtr tone(qint16 value, int samples = 20000) {
    QByteArray pcm(samples * audioBytesPerSampleFrame, Qt::Uninitialized);
    for (int i = 0; i < samples; ++i) {
        qToLittleEndian<qint16>(value, pcm.data() + i * 4);
        qToLittleEndian<qint16>(-value, pcm.data() + i * 4 + 2);
    }
    return makeAudioAsset("Movie fixture", pcm);
}
AudioClip clip(AudioAssetPtr source, QString id = "audio") {
    AudioClip value;
    value.id = QUuid::createUuid().toString(QUuid::WithoutBraces); value.name = id; value.asset = source;
    value.sampleCount = source->sampleFrameCount();
    return value;
}
qint16 sample(const QByteArray &bytes, qint64 index, int channel = 0) {
    qint16 value = 0;
    std::memcpy(&value, bytes.constData() + index * audioBytesPerSampleFrame + channel * 2, sizeof(value));
    return value;
}
}

class MoviePlaybackAudioTests final : public QObject {
    Q_OBJECT
private slots:
    void movieAndSceneMappingPreserveProjectOrder() {
        auto project = movie();
        // The low-level mapper also handles unassigned scene metadata; normal
        // project/export fixtures below retain valid, nonempty scene names.
        project.shots.last().scene.clear();
        const auto all = moviePlaybackSegments(project, MoviePlaybackScope::Movie);
        QCOMPARE(all, QVector<MoviePlaybackSegment>({{0, 0, 2}, {1, 2, 3}, {2, 5, 4}, {3, 9, 5}}));
        QCOMPARE(moviePlaybackDuration(all), 14);
        QCOMPARE(moviePlaybackLocation(all, -1), MoviePlaybackLocation());
        QCOMPARE(moviePlaybackLocation(all, 0), (MoviePlaybackLocation{0, 0}));
        QCOMPARE(moviePlaybackLocation(all, 1), (MoviePlaybackLocation{0, 1}));
        QCOMPARE(moviePlaybackLocation(all, 2), (MoviePlaybackLocation{1, 0}));
        QCOMPARE(moviePlaybackLocation(all, 8), (MoviePlaybackLocation{2, 3}));
        QCOMPARE(moviePlaybackLocation(all, 13), (MoviePlaybackLocation{3, 4}));
        QCOMPARE(moviePlaybackLocation(all, 14), MoviePlaybackLocation());
        const auto scene = moviePlaybackSegments(project, MoviePlaybackScope::Scene, "A");
        QCOMPARE(scene, QVector<MoviePlaybackSegment>({{0, 0, 2}, {2, 2, 4}}));
        QCOMPARE(moviePlaybackLocation(scene, 2), (MoviePlaybackLocation{2, 0}));
        QCOMPARE(moviePlaybackSegments(project, MoviePlaybackScope::Scene), QVector<MoviePlaybackSegment>({{3, 0, 5}}));
        QVERIFY(moviePlaybackSegments(project, MoviePlaybackScope::Scene, "missing").isEmpty());
    }
    void invalidAndOverflowingSequencesAreRejected() {
        auto project = movie();
        QVERIFY(moviePlaybackSegments(project, MoviePlaybackScope(99)).isEmpty());
        project.shots[0].durationFrames = 0;
        QVERIFY(moviePlaybackSegments(project, MoviePlaybackScope::Movie).isEmpty());
        project.shots[0].durationFrames = std::numeric_limits<int>::max();
        QVERIFY(moviePlaybackSegments(project, MoviePlaybackScope::Movie).isEmpty());
        QCOMPARE(moviePlaybackDuration({{0, 0, 10}, {1, 9, 2}}), 0);
        QCOMPARE(moviePlaybackDuration({{0, 0, std::numeric_limits<int>::max()}, {1, std::numeric_limits<int>::max(), 2}}), 0);
        QCOMPARE(moviePlaybackDuration({{-1, 0, 2}}), 0);
        QCOMPARE(moviePlaybackDuration({}), 0);
        project = movie();
        AudioMixStream badShot(project, {{99, 0, 3}}, 0, 0, 2, false);
        AudioMixStream wrongDuration(project, {{0, 0, 3}}, 0, 0, 2, false);
        AudioMixStream badRange(project, moviePlaybackSegments(project, MoviePlaybackScope::Movie), 0, 0, 14, false);
        QVERIFY(!badShot.valid()); QVERIFY(!wrongDuration.valid()); QVERIFY(!badRange.valid());
    }
    void cutStopsOutgoingAudioAndKeepsSilentShotsSilent() {
        auto project = movie();
        project.shots[0].audioClips = {clip(tone(1000))};
        project.shots[2].audioClips = {clip(tone(3000))};
        auto muted = clip(tone(2000)); muted.muted = true;
        project.shots[1].audioClips = {muted};
        const auto segments = moviePlaybackSegments(project, MoviePlaybackScope::Movie);
        AudioMixStream mix(project, segments, 0, 0, 13, false);
        QVERIFY(mix.valid()); QVERIFY(mix.hasAudio());
        const auto bytes = mix.read(6000 * audioBytesPerSampleFrame);
        QCOMPARE(bytes.size(), 6000 * audioBytesPerSampleFrame);
        for (int i = 0; i < 6000; ++i) {
            const int expected = i < 800 ? 1000 : i >= 2000 && i < 3600 ? 3000 : 0;
            QCOMPARE(sample(bytes, i), expected);
            QCOMPARE(sample(bytes, i, 1), -expected);
        }
        QVERIFY(!mix.atEnd()); QVERIFY(mix.bytesAvailable() <= 8192);
    }
    void sceneAudioOmitsOtherScenesAndStartsAtSelectedPlayhead() {
        auto project = movie();
        for (int i = 0; i < project.shots.size(); ++i) project.shots[i].audioClips = {clip(tone(1000 * (i + 1)))};
        const auto segments = moviePlaybackSegments(project, MoviePlaybackScope::Scene, "A");
        AudioMixStream mix(project, segments, 3, 1, 4, true);
        const auto bytes = mix.read(4000 * audioBytesPerSampleFrame);
        for (int i = 0; i < 4000; ++i) {
            const int global = i < 800 ? 1200 + i : 400 + (i - 800) % 1600;
            QCOMPARE(sample(bytes, i), global < 800 ? 1000 : 3000);
        }
    }
    void fractionalCutsMatchExportIncludingSourceEnvelopes_data() {
        QTest::addColumn<double>("fps");
        QTest::newRow("23.976") << 23.976;
        QTest::newRow("29.97") << 29.97;
        QTest::newRow("59.94") << 59.94;
    }
    void fractionalCutsMatchExportIncludingSourceEnvelopes() {
        QFETCH(double, fps);
        auto project = movie(); project.fps = fps;
        const auto source = tone(7000);
        for (int i = 0; i < project.shots.size(); ++i) {
            auto audio = clip(source, QString("clip%1").arg(i));
            audio.startFrame = i % 2; audio.sourceOffsetSamples = 37; audio.sampleCount = 15000;
            audio.fadeInSamples = 600; audio.fadeOutSamples = 10000;
            audio.fadeOutCurve = AudioFadeCurve::EqualPower;
            audio.gainAnimated = true; audio.gainKeys = {{37, .1}, {5000, 1.5}, {17000, .2}};
            auto overlap = clip(source, QString("overlap%1").arg(i)); overlap.gain = .3;
            overlap.startFrame = 1; overlap.sourceOffsetSamples = 123; overlap.sampleCount = 10000;
            project.shots[i].audioClips = {audio, overlap};
        }
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        auto settings = videoExportMovieDefaults(project);
        settings.firstFrame = 1; settings.lastFrame = 11;
        VideoExportAudioStream exported(project, settings); QVERIFY(exported.valid());
        const auto expected = exported.readAll(); QVERIFY(!expected.isEmpty());
        AudioMixStream playback(project, moviePlaybackSegments(project, MoviePlaybackScope::Movie), 1, 1, 11, false);
        QVERIFY(playback.valid()); QCOMPARE(playback.read(expected.size()), expected);
        QCOMPARE(playback.read(99), QByteArray(99, '\0'));
    }
    void hardCutsDoNotMoveFadeOutEnvelope() {
        auto project = movie();
        auto audio = clip(tone(10000)); audio.sampleCount = 4000; audio.fadeOutSamples = 1000;
        project.shots[0].audioClips = {audio};
        AudioMixStream mix(project, moviePlaybackSegments(project, MoviePlaybackScope::Movie), 0, 0, 13, false);
        const auto bytes = mix.read(805 * audioBytesPerSampleFrame);
        QCOMPARE(sample(bytes, 0), 10000); QCOMPARE(sample(bytes, 799), 10000); QCOMPARE(sample(bytes, 800), 0);
    }
    void fractionalLoopsWrapAtAbsoluteSampleBoundaries() {
        auto project = movie(); project.fps = 29.97;
        for (int i = 0; i < project.shots.size(); ++i) project.shots[i].audioClips = {clip(tone(1000 * (i + 1)))};
        const auto segments = moviePlaybackSegments(project, MoviePlaybackScope::Movie);
        const auto first = audioSampleFrameAtTimelineFrame(1, project.fps);
        const auto start = audioSampleFrameAtTimelineFrame(6, project.fps);
        const auto end = audioSampleFrameAtTimelineFrame(11, project.fps);
        AudioMixStream mix(project, segments, 6, 1, 10, true);
        const auto count = (end - first) * 8 + 31;
        const auto bytes = mix.read(count * audioBytesPerSampleFrame);
        for (qint64 i = 0; i < count; ++i) {
            const auto absolute = first + (start - first + i) % (end - first);
            int expected = 0;
            for (const auto &segment : segments)
                if (absolute >= audioSampleFrameAtTimelineFrame(segment.firstFrame, project.fps)
                    && absolute < audioSampleFrameAtTimelineFrame(segment.firstFrame + segment.frameCount, project.fps))
                    expected = 1000 * (segment.shotIndex + 1);
            QCOMPARE(sample(bytes, i), expected);
        }
    }
    void fragmentedFloatingReadsAndSnapshotRemainStable() {
        auto project = movie();
        project.shots[0].audioClips = {clip(tone(1000))};
        project.shots[1].audioClips = {clip(tone(2000))};
        const auto segments = moviePlaybackSegments(project, MoviePlaybackScope::Movie);
        AudioMixStream reference(project, segments, 0, 0, 13, true, AudioMixFormat::Float32);
        AudioMixStream fragmented(project, segments, 0, 0, 13, true, AudioMixFormat::Float32);
        project.shots.clear();
        const auto expected = reference.read(13337);
        QByteArray actual;
        while (actual.size() < expected.size()) actual += fragmented.read(qMin<qsizetype>(71, expected.size() - actual.size()));
        QCOMPARE(actual, expected);
        float first = 0, cut = 0;
        std::memcpy(&first, actual.constData(), sizeof(float));
        std::memcpy(&cut, actual.constData() + 800 * 8, sizeof(float));
        QCOMPARE(first, 1000.f / 32768); QCOMPARE(cut, 2000.f / 32768);
    }
    void longMovieMixRetainsBoundedReadBuffer() {
        auto project = movie(); project.shots.clear();
        const auto source = tone(500, 400);
        for (int i = 0; i < maxProjectShots; ++i) {
            Shot shot; shot.durationFrames = 1; shot.audioClips = {clip(source)};
            project.shots.append(shot);
        }
        const auto segments = moviePlaybackSegments(project, MoviePlaybackScope::Movie);
        AudioMixStream mix(project, segments, 9998, 0, 9999, true);
        QVERIFY(mix.valid()); QVERIFY(mix.bytesAvailable() <= 8192);
        const auto bytes = mix.read(5000 * audioBytesPerSampleFrame);
        for (int i = 0; i < 5000; ++i) QCOMPARE(sample(bytes, i), 500);
    }
    void playbackSharesOneClockAcrossCutsAndRestartsForSeek() {
        auto project = movie();
        project.shots[0].audioClips = {clip(tone(1000))};
        const auto segments = moviePlaybackSegments(project, MoviePlaybackScope::Movie);
        AudioPlayback playback(nullptr, AudioPlayback::OutputMode::Silent);
        QSignalSpy warnings(&playback, &AudioPlayback::warning);
        playback.start(project, segments, 0, 0, 13, false);
        QVERIFY(playback.active()); QVERIFY(!playback.usingAudioClock());
        QTRY_VERIFY_WITH_TIMEOUT(playback.elapsedUSecs() > 60000, 500);
        const auto elapsed = playback.elapsedUSecs();
        QVERIFY(elapsed > 2 * 1000000 / project.fps); // Already crossed the first cut without a restart.
        playback.start(project, segments, 5, 3, 13, true);
        QVERIFY(playback.active()); QVERIFY(playback.elapsedUSecs() < elapsed);
        QCOMPARE(warnings.size(), 0);
        playback.stop(); QVERIFY(!playback.active()); QCOMPARE(playback.elapsedUSecs(), 0);
        playback.start(project, {}, 0, 0, 0, false); QVERIFY(!playback.active());
    }
};
QTEST_GUILESS_MAIN(MoviePlaybackAudioTests)
#include "movie_playback_audio_tests.moc"
