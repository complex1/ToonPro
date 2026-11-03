#include "audio.h"
#include "audio_mix.h"
#include "audio_playback.h"
#include "project.h"
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtEndian>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

using namespace toon;
namespace {
using Stereo = QPair<qint16, qint16>;
AudioAssetPtr asset(const QVector<Stereo> &samples) {
    QByteArray pcm(samples.size() * audioBytesPerSampleFrame, Qt::Uninitialized);
    for (qsizetype i = 0; i < samples.size(); ++i) {
        qToLittleEndian<qint16>(samples[i].first, pcm.data() + i * 4);
        qToLittleEndian<qint16>(samples[i].second, pcm.data() + i * 4 + 2);
    }
    return makeAudioAsset(QStringLiteral("Fixture"), pcm);
}
AudioAssetPtr ramp(int count) {
    QVector<Stereo> samples;
    samples.reserve(count);
    for (int i = 0; i < count; ++i) samples.append({qint16(i % 30001), qint16(-(i % 30001))});
    return asset(samples);
}
AudioClip clip(const AudioAssetPtr &source, int start = 0, qint64 offset = 0, qint64 count = -1,
               double gain = 1, bool muted = false) {
    AudioClip result;
    result.id = QStringLiteral("clip"); result.name = QStringLiteral("Clip"); result.asset = source;
    result.startFrame = start; result.sourceOffsetSamples = offset;
    result.sampleCount = count < 0 && source ? source->sampleFrameCount() - offset : count;
    result.gain = gain; result.muted = muted;
    return result;
}
QVector<Stereo> signedSamples(const QByteArray &bytes) {
    QVector<Stereo> result;
    result.reserve(bytes.size() / audioBytesPerSampleFrame);
    for (qsizetype i = 0; i + 3 < bytes.size(); i += 4) {
        qint16 left = 0, right = 0;
        std::memcpy(&left, bytes.constData() + i, 2);
        std::memcpy(&right, bytes.constData() + i + 2, 2);
        result.append({left, right});
    }
    return result;
}
QVector<Stereo> read(AudioMixStream &stream, int frames) {
    return signedSamples(stream.read(qint64(frames) * audioBytesPerSampleFrame));
}
bool silent(const QVector<Stereo> &samples) {
    return std::all_of(samples.begin(), samples.end(), [](Stereo value) { return value.first == 0 && value.second == 0; });
}
} // namespace

class AudioPlaybackTests final : public QObject {
    Q_OBJECT
private slots:
    void leadingSilenceAndSourceTrimAreSampleExact() {
        const auto source = ramp(32); QVERIFY(source);
        AudioMixStream stream({clip(source, 2, 3, 5)}, 120, 0, 0, 4, false);
        QVERIFY(stream.valid()); QVERIFY(stream.hasAudio()); QVERIFY(stream.isSequential());
        const auto samples = read(stream, 1200); QCOMPARE(samples.size(), 1200);
        QVERIFY(silent(samples.mid(0, 800)));
        for (int i = 0; i < 5; ++i) QCOMPARE(samples[800 + i], Stereo(i + 3, -i - 3));
        QVERIFY(silent(samples.mid(805)));
    }
    void startingInsideClipAccountsForTrim() {
        const auto source = ramp(2000); QVERIFY(source);
        AudioMixStream stream({clip(source, 2, 50, 1000)}, 120, 3, 0, 5, false);
        const auto samples = read(stream, 650); QCOMPARE(samples.size(), 650);
        for (int i = 0; i < 600; ++i) QCOMPARE(samples[i], Stereo(450 + i, -450 - i));
        QVERIFY(silent(samples.mid(600)));
    }
    void overlappingClipsGainMuteAndSaturation() {
        const auto a = asset({{20000, -20000}, {10000, 2000}, {-20000, 20000}});
        const auto b = asset({{20000, -20000}, {-20000, 8000}, {-20000, 20000}});
        const auto muted = asset({{30000, 30000}, {30000, 30000}, {30000, 30000}});
        QVERIFY(a && b && muted);
        AudioMixStream stream({clip(a, 0, 0, 3, 1.5), clip(b, 0, 0, 3, .5), clip(muted, 0, 0, 3, 2, true)}, 24, 0, 0, 1, false);
        const auto samples = read(stream, 4);
        QCOMPARE(samples, QVector<Stereo>({{32767, -32768}, {5000, 7000}, {-32768, 32767}, {0, 0}}));
    }
    void sumIsClippedOnlyAfterAllContributors() {
        const auto positive = asset({{30000, 30000}}), negative = asset({{-30000, -30000}});
        AudioMixStream first({clip(positive), clip(positive), clip(negative)}, 24, 0, 0, 0, false);
        AudioMixStream second({clip(negative), clip(positive), clip(positive)}, 24, 0, 0, 0, false);
        QCOMPARE(read(first, 1), QVector<Stereo>({{30000, 30000}}));
        QCOMPARE(read(second, 1), QVector<Stereo>({{30000, 30000}}));
    }
    void floatingOutputRetainsFractionalGainAndClips() {
        const auto source = asset({{1, -1}, {32767, -32768}}); QVERIFY(source);
        AudioMixStream stream({clip(source, 0, 0, 2, .5), clip(source, 0, 0, 2, .25)}, 24, 0, 0, 1, false, AudioMixFormat::Float32);
        const auto bytes = stream.read(4 * sizeof(float)); QCOMPARE(bytes.size(), qsizetype(4 * sizeof(float)));
        std::array<float, 4> values{}; std::memcpy(values.data(), bytes.constData(), bytes.size());
        QCOMPARE(values[0], .75f / 32768); QCOMPARE(values[1], -.75f / 32768);
        QCOMPARE(values[2], .75f * 32767 / 32768); QCOMPARE(values[3], -.75f);
        AudioMixStream clipped({clip(source, 0, 1, 1, 2)}, 24, 0, 0, 1, false, AudioMixFormat::Float32);
        const auto clippedBytes = clipped.read(2 * sizeof(float));
        std::memcpy(values.data(), clippedBytes.constData(), clippedBytes.size());
        QCOMPARE(values[0], 1.f); QCOMPARE(values[1], -1.f);
    }
    void fractionalFpsBoundaries_data() {
        QTest::addColumn<double>("fps");
        QTest::newRow("23.976") << 23.976;
        QTest::newRow("29.97") << 29.97;
        QTest::newRow("59.94") << 59.94;
    }
    void fractionalFpsBoundaries() {
        QFETCH(double, fps);
        const auto source = ramp(6000); QVERIFY(source);
        const auto first = audioSampleFrameAtTimelineFrame(6, fps), start = audioSampleFrameAtTimelineFrame(7, fps);
        const auto end = audioSampleFrameAtTimelineFrame(9, fps);
        AudioMixStream stream({clip(source, 7, 19, 5000)}, fps, 6, 6, 8, false);
        const auto samples = read(stream, int(end - first) + 40);
        for (int i = 0; i < samples.size(); ++i) {
            const qint64 absolute = first + i;
            const qint16 expected = absolute >= start && absolute < std::min(end, start + 5000) ? qint16(19 + absolute - start) : 0;
            QCOMPARE(samples[i], Stereo(expected, -expected));
        }
    }
    void rangeLoopStartsAtPlayheadThenWraps() {
        const auto source = ramp(3000); QVERIFY(source);
        AudioMixStream stream({clip(source)}, 120, 3, 2, 4, true);
        const auto samples = read(stream, 7000); QCOMPARE(samples.size(), 7000);
        for (int i = 0; i < samples.size(); ++i) {
            const int expected = i < 800 ? 1200 + i : 800 + (i - 800) % 1200;
            QCOMPARE(samples[i], Stereo(expected, -expected));
        }
    }
    void fractionalLoopHasNoCumulativeBoundaryDrift() {
        constexpr double fps = 29.97;
        const auto source = ramp(12000); QVERIFY(source);
        const auto first = audioSampleFrameAtTimelineFrame(3, fps), end = audioSampleFrameAtTimelineFrame(6, fps);
        AudioMixStream stream({clip(source)}, fps, 3, 3, 5, true);
        const auto samples = read(stream, int((end - first) * 7 + 31));
        for (int i = 0; i < samples.size(); ++i) {
            const qint16 expected = qint16(first + i % (end - first));
            QCOMPARE(samples[i], Stereo(expected, -expected));
        }
    }
    void endpointSilenceKeepsClockSourceReadable() {
        const auto source = asset(QVector<Stereo>(800, {700, -300})); QVERIFY(source);
        AudioMixStream stream({clip(source)}, 120, 0, 0, 0, false);
        const auto samples = read(stream, 12000);
        QCOMPARE(samples.size(), 12000);
        for (int i = 0; i < 400; ++i) QCOMPARE(samples[i], Stereo(700, -300));
        QVERIFY(silent(samples.mid(400))); QVERIFY(!stream.atEnd()); QVERIFY(stream.bytesAvailable() > 0);
        QVERIFY(silent(read(stream, 2000)));
    }
    void partialReadsPreserveEveryByte_data() {
        QTest::addColumn<bool>("floating");
        QTest::newRow("signed16") << false; QTest::newRow("float32") << true;
    }
    void partialReadsPreserveEveryByte() {
        QFETCH(bool, floating);
        const auto format = floating ? AudioMixFormat::Float32 : AudioMixFormat::Signed16;
        const auto source = ramp(2000); QVERIFY(source);
        AudioMixStream whole({clip(source)}, 120, 1, 0, 3, true, format);
        AudioMixStream fragments({clip(source)}, 120, 1, 0, 3, true, format);
        const auto expected = whole.read(23017);
        QByteArray actual;
        for (const int size : {1, 3, 7, 11, 17, 1027, 4099, 8191, 9661}) actual += fragments.read(size);
        QCOMPARE(actual.size(), expected.size()); QCOMPARE(actual, expected);
    }
    void silentOrOutOfRangeClipsDoNotRequireOutput() {
        const auto source = ramp(1000); QVERIFY(source);
        AudioMixStream muted({clip(source, 0, 0, 1000, 1, true)}, 120, 0, 0, 5, false);
        AudioMixStream zeroGain({clip(source, 0, 0, 1000, 0)}, 120, 0, 0, 5, false);
        AudioMixStream before({clip(source)}, 120, 5, 0, 9, false);
        AudioMixStream after({clip(source, 20)}, 120, 0, 0, 9, true);
        AudioMixStream loopBack({clip(source)}, 120, 5, 0, 9, true);
        QVERIFY(!muted.hasAudio()); QVERIFY(!zeroGain.hasAudio()); QVERIFY(!before.hasAudio()); QVERIFY(!after.hasAudio());
        QVERIFY(loopBack.hasAudio()); QVERIFY(silent(read(muted, 10)));
    }
    void noClipAndLongShotStreamsStayBounded() {
        AudioMixStream empty({}, 1, 0, 0, 999999, true);
        QVERIFY(empty.valid()); QVERIFY(!empty.hasAudio()); QVERIFY(silent(read(empty, 1024)));
        const auto source = ramp(10); QVERIFY(source);
        AudioMixStream late({clip(source, 999999)}, 1, 999999, 0, 999999, false);
        const auto samples = read(late, 10);
        for (int i = 0; i < samples.size(); ++i) QCOMPARE(samples[i], Stereo(i, -i));
        QVERIFY(late.bytesAvailable() <= 8192);
    }
    void invalidMetadataDoesNotReadPastAsset() {
        const auto source = ramp(5); QVERIFY(source);
        AudioMixStream oversized({clip(source, 0, 3, std::numeric_limits<qint64>::max())}, 24, 0, 0, 1, false);
        QCOMPARE(read(oversized, 4), QVector<Stereo>({{3, -3}, {4, -4}, {0, 0}, {0, 0}}));
        AudioMixStream invalid({clip(source, 0, -1, 5), clip(source, 0, 5, 1), clip(source, 0, 0, 5, std::numeric_limits<double>::quiet_NaN())}, 24, 0, 0, 1, false);
        QVERIFY(!invalid.hasAudio()); QVERIFY(silent(read(invalid, 10)));
        AudioMixStream invalidFps({}, std::numeric_limits<double>::quiet_NaN(), 0, 0, 2, false);
        AudioMixStream invalidRange({}, 24, 0, 2, 1, false);
        QVERIFY(!invalidFps.valid()); QVERIFY(!invalidRange.valid());
    }
    void metadataOnlyAssetIsNeverReadAsPcm() {
        const auto source = ramp(10); QVERIFY(source);
        auto project = Project::create(); project.shots[0].audioClips = {clip(source)};
        QMap<QString, AudioAssetPtr> placeholders; QString error;
        QVERIFY2(audioResourcesFromJson(audioResourcesToJson(project), &placeholders, &error), qPrintable(error));
        const auto unloaded = placeholders.value(source->id); QVERIFY(unloaded);
        QCOMPARE(unloaded->sampleFrameCount(), source->sampleFrameCount()); QVERIFY(unloaded->pcm.isEmpty());
        AudioMixStream stream({clip(unloaded)}, 24, 0, 0, 2, false);
        QVERIFY(stream.valid()); QVERIFY(!stream.hasAudio()); QVERIFY(silent(read(stream, 100)));
    }
    void sourceSnapshotSurvivesDocumentEdits() {
        auto source = ramp(900); QVERIFY(source);
        QVector<AudioClip> clips{clip(source)};
        AudioMixStream stream(clips, 120, 0, 0, 2, false);
        clips[0].gain = 0; clips[0].muted = true; clips.clear(); source.reset();
        const auto samples = read(stream, 900);
        for (int i = 0; i < samples.size(); ++i) QCOMPARE(samples[i], Stereo(i, -i));
    }
    void silentPlaybackClockStopsAndRestarts() {
        Shot shot; shot.durationFrames = 100; shot.audioClips = {clip(ramp(1000))};
        AudioPlayback playback(nullptr, AudioPlayback::OutputMode::Silent); QSignalSpy warnings(&playback, &AudioPlayback::warning);
        QVERIFY(!playback.active()); QCOMPARE(playback.elapsedUSecs(), 0);
        playback.start(shot, 24, 7, 3, 20, true);
        QVERIFY(playback.active()); QVERIFY(!playback.usingAudioClock());
        QTRY_VERIFY_WITH_TIMEOUT(playback.elapsedUSecs() >= 15000, 500);
        const auto before = playback.elapsedUSecs(); QTest::qWait(5); QVERIFY(playback.elapsedUSecs() >= before);
        playback.stop(); QVERIFY(!playback.active()); QCOMPARE(playback.elapsedUSecs(), 0);
        playback.start(shot, 24, 3, 3, 20, false); QVERIFY(playback.active());
        QVERIFY(playback.elapsedUSecs() < before); QCOMPARE(warnings.size(), 0);
        playback.stop(); playback.stop(); QVERIFY(!playback.active());
    }
    void emptyShotUsesWallClockWithoutOpeningOutput() {
        Shot shot; shot.durationFrames = 100;
        AudioPlayback playback; QSignalSpy warnings(&playback, &AudioPlayback::warning);
        playback.start(shot, 24, 0, 0, 99, false);
        QVERIFY(playback.active()); QVERIFY(!playback.usingAudioClock());
        QTRY_VERIFY_WITH_TIMEOUT(playback.elapsedUSecs() > 5000, 500);
        QCOMPARE(warnings.size(), 0); playback.stop();
        shot.durationFrames = 0; playback.start(shot, 24, 0, 0, 0, false); QVERIFY(!playback.active());
    }
    void nativeSilentDeviceSmoke() {
        if (!qEnvironmentVariableIsSet("TOON_AUDIO_DEVICE_TEST")) QSKIP("Set TOON_AUDIO_DEVICE_TEST=1 for a native output test; only zero PCM is played.");
        Shot shot; shot.durationFrames = 120;
        const auto silence = makeAudioAsset(QStringLiteral("Silent device test"), QByteArray(audioSampleRate * audioBytesPerSampleFrame, '\0'));
        QVERIFY(silence); shot.audioClips = {clip(silence)};
        AudioPlayback playback; QSignalSpy warnings(&playback, &AudioPlayback::warning);
        playback.start(shot, 24, 0, 0, 119, true);
        QVERIFY2(playback.usingAudioClock(), warnings.isEmpty() ? "Output did not start" : qPrintable(warnings.first().first().toString()));
        QTRY_VERIFY_WITH_TIMEOUT(playback.elapsedUSecs() >= 40000, 2000);
        QVERIFY(playback.active()); QVERIFY(playback.usingAudioClock()); QCOMPARE(warnings.size(), 0);
        const auto before = playback.elapsedUSecs(); QTest::qWait(50); QVERIFY(playback.elapsedUSecs() >= before);
        QElapsedTimer timer; timer.start(); playback.stop();
        QVERIFY2(timer.elapsed() < 500, "Stopping audio should discard queued buffers immediately");
        QVERIFY(!playback.active()); QCOMPARE(playback.elapsedUSecs(), 0);
        // Restart also exercises queued signals from the previous sink.
        playback.start(shot, 24, 0, 0, 119, true);
        QTRY_VERIFY_WITH_TIMEOUT(playback.elapsedUSecs() >= 40000, 2000);
        QVERIFY(playback.usingAudioClock()); QCOMPARE(warnings.size(), 0); playback.stop();
    }
};
QTEST_MAIN(AudioPlaybackTests)
#include "audio_playback_tests.moc"
