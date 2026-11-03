#include "audio.h"
#include "audio_mix.h"
#include "document.h"
#include "timeline_ripple.h"
#include "video_export_audio.h"
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QUuid>
#include <QtEndian>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>

using namespace toon;
namespace {
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
AudioAssetPtr asset(qint64 count = 9000) {
    QByteArray pcm(count * audioBytesPerSampleFrame, '\0');
    for (qint64 i = 0; i < count; ++i) {
        qToLittleEndian<qint16>(10000, pcm.data() + i * 4);
        qToLittleEndian<qint16>(-20000, pcm.data() + i * 4 + 2);
    }
    return makeAudioAsset("Finishing fixture", pcm);
}
AudioClip clip(AudioAssetPtr source, qint64 offset = 0, qint64 count = 1000, int frame = 0) {
    return {id(), "Audio", source, frame, offset, count, 1, false};
}
qint16 sample(const QByteArray &data, qint64 frame, int channel = 0) {
    qint16 value = 0; std::memcpy(&value, data.constData() + frame * 4 + channel * 2, sizeof(value)); return value;
}
QJsonObject editedFinishing(QJsonObject manifest, const std::function<void(QJsonObject &)> &edit) {
    auto shots = manifest["shots"].toArray(); auto shot = shots[0].toObject();
    auto clips = shot["audioClips"].toArray(); auto audio = clips[0].toObject(); auto finish = audio["finishing"].toObject();
    edit(finish); audio["finishing"] = finish; clips[0] = audio; shot["audioClips"] = clips; shots[0] = shot; manifest["shots"] = shots;
    return manifest;
}
}

class AudioFinishingTests final : public QObject {
    Q_OBJECT
private slots:
    void fadeEndpointsCurvesOverlapsAndSingleSamples() {
        auto audio = clip(asset(), 200, 1000); audio.gain = 1.5;
        audio.fadeInSamples = 101; audio.fadeOutSamples = 101;
        QCOMPARE(audioClipGainAtSourceSample(audio, 199), 0.);
        QCOMPARE(audioClipGainAtSourceSample(audio, 200), 0.);
        QCOMPARE(audioClipGainAtSourceSample(audio, 250), .75);
        QCOMPARE(audioClipGainAtSourceSample(audio, 300), 1.5);
        QCOMPARE(audioClipGainAtSourceSample(audio, 1099), 1.5);
        QCOMPARE(audioClipGainAtSourceSample(audio, 1149), .75);
        QCOMPARE(audioClipGainAtSourceSample(audio, 1199), 0.);
        QCOMPARE(audioClipGainAtSourceSample(audio, 1200), 0.);
        audio.fadeInCurve = audio.fadeOutCurve = AudioFadeCurve::EqualPower;
        QVERIFY(std::abs(audioClipGainAtSourceSample(audio, 250) - 1.5 / std::sqrt(2.)) < 1e-12);
        QVERIFY(std::abs(audioClipGainAtSourceSample(audio, 1149) - 1.5 / std::sqrt(2.)) < 1e-12);
        audio.fadeInSamples = audio.fadeOutSamples = 1000;
        const double middle = audioClipGainAtSourceSample(audio, 700);
        QVERIFY(middle > .749 && middle < .751); // Both envelopes multiply.
        audio.fadeInSamples = audio.fadeOutSamples = 1;
        QCOMPARE(audioClipGainAtSourceSample(audio, 200), 0.);
        QCOMPARE(audioClipGainAtSourceSample(audio, 201), 1.5);
        QCOMPARE(audioClipGainAtSourceSample(audio, 1198), 1.5);
        QCOMPARE(audioClipGainAtSourceSample(audio, 1199), 0.);
        audio.muted = true; QCOMPARE(audioClipGainAtSourceSample(audio, 500), 0.);
    }
    void sourceKeysReplaceStaticGainInterpolateHoldAndRetainWhenDisabled() {
        auto audio = clip(asset(), 200, 1000); audio.gain = 0;
        audio.gainAnimated = true; audio.gainKeys = {{300, .2}, {500, 1.8}, {1000, .8}};
        QCOMPARE(audioClipGainAtSourceSample(audio, 200), .2);
        QCOMPARE(audioClipGainAtSourceSample(audio, 300), .2);
        QCOMPARE(audioClipGainAtSourceSample(audio, 400), 1.);
        QCOMPARE(audioClipGainAtSourceSample(audio, 750), 1.3);
        QCOMPARE(audioClipGainAtSourceSample(audio, 1199), .8);
        AudioMixStream stream({audio}, 120, 0, 0, 3, false);
        QVERIFY(stream.hasAudio()); QCOMPARE(sample(stream.read(4), 0), qint16(2000));
        audio.gainAnimated = false; audio.gain = .6;
        QCOMPARE(audioClipGainAtSourceSample(audio, 400), .6);
        QCOMPARE(audio.gainKeys.size(), 3);
        audio.gainAnimated = true; audio.gainKeys = {{400, 1.25}};
        QCOMPARE(audioClipGainAtSourceSample(audio, 200), 1.25);
        QCOMPARE(audioClipGainAtSourceSample(audio, 1199), 1.25);
    }
    void playbackOfflineExportAndChunkedMixShareExactEnvelope() {
        auto project = Project::create(); project.fps = 29.97; project.shots[0].durationFrames = 7;
        auto a = clip(asset(), 13, 6001, 1); a.fadeInSamples = 701; a.fadeOutSamples = 903;
        a.fadeOutCurve = AudioFadeCurve::EqualPower; a.gain = 0; a.gainAnimated = true;
        a.gainKeys = {{13, .2}, {1700, 1.8}, {6000, .4}};
        auto b = clip(a.asset, 37, 4000, 3); b.gain = .3; b.fadeInSamples = 1200;
        project.shots[0].audioClips = {a, b};
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        auto settings = videoExportDefaults(project); settings.firstFrame = 2; settings.lastFrame = 6;
        VideoExportAudioStream offline(project, settings); QVERIFY(offline.valid());
        AudioMixStream playback(project.shots[0].audioClips, project.fps, 2, 2, 6, false);
        const auto expected = offline.readAll(); QVERIFY(!expected.isEmpty());
        QCOMPARE(playback.read(expected.size()), expected);
        VideoExportAudioStream fragmented(project, settings); QByteArray actual;
        while (!fragmented.atEnd()) actual += fragmented.read(103);
        QCOMPARE(actual, expected);
        const auto timelineFirst = audioSampleFrameAtTimelineFrame(2, project.fps);
        for (qint64 i = 0; i < expected.size() / 4; ++i) {
            double gain = 0;
            for (const auto &entry : project.shots[0].audioClips)
                gain += audioClipGainAtSourceSample(entry, entry.sourceOffsetSamples + timelineFirst + i
                    - audioSampleFrameAtTimelineFrame(entry.startFrame, project.fps));
            QCOMPARE(sample(expected, i), qint16(std::lround(std::clamp(gain * 10000, -32768., 32767.))));
        }
        AudioMixStream loop({a}, project.fps, 2, 2, 3, true);
        const auto period = audioSampleFrameAtTimelineFrame(4, project.fps) - audioSampleFrameAtTimelineFrame(2, project.fps);
        const auto loopBytes = loop.read(period * 4 * 3);
        QCOMPARE(loopBytes.left(period * 4), loopBytes.mid(period * 4, period * 4));
        QCOMPARE(loopBytes.left(period * 4), loopBytes.mid(period * 8, period * 4));
    }
    void crossfadeIsAtomicUndoableAndDoesNotMoveClips() {
        auto project = Project::create(); project.fps = 120;
        auto a = clip(asset(), 19, 1200, 0), b = clip(a.asset, 37, 1600, 2);
        a.fadeInSamples = 30; b.fadeOutSamples = 20;
        project.shots[0].audioClips = {a, b};
        Document document; document.load(project);
        QVERIFY(document.crossfadeAudioClips(a.id, b.id, AudioFadeCurve::Linear));
        QCOMPARE(document.history()->count(), 1);
        const auto result = document.activeShot().audioClips;
        QCOMPARE(result[0].startFrame, a.startFrame); QCOMPARE(result[1].startFrame, b.startFrame);
        QCOMPARE(result[0].sampleCount, a.sampleCount); QCOMPARE(result[1].sourceOffsetSamples, b.sourceOffsetSamples);
        QCOMPARE(result[0].fadeOutSamples, qint64(400)); QCOMPARE(result[1].fadeInSamples, qint64(400));
        QCOMPARE(result[0].fadeInSamples, a.fadeInSamples); QCOMPARE(result[1].fadeOutSamples, b.fadeOutSamples);
        for (int i = 0; i < 400; ++i) {
            const double sum = audioClipGainAtSourceSample(result[0], a.sourceOffsetSamples + 800 + i)
                + audioClipGainAtSourceSample(result[1], b.sourceOffsetSamples + i);
            QVERIFY(std::abs(sum - 1) < 1e-12);
        }
        document.history()->undo(); QCOMPARE(document.activeShot().audioClips, project.shots[0].audioClips);
        document.history()->redo(); QCOMPARE(document.activeShot().audioClips, result);
        QVERIFY(document.crossfadeAudioClips(a.id, b.id, AudioFadeCurve::EqualPower));
        const auto powered = document.activeShot().audioClips;
        for (int i = 0; i < 400; ++i) {
            const double out = audioClipGainAtSourceSample(powered[0], a.sourceOffsetSamples + 800 + i);
            const double in = audioClipGainAtSourceSample(powered[1], b.sourceOffsetSamples + i);
            QVERIFY(std::abs(out * out + in * in - 1) < 1e-12);
        }
        const auto unchanged = document.project().toJson(); const int commands = document.history()->count();
        QVERIFY(!document.crossfadeAudioClips(b.id, a.id)); QVERIFY(!document.crossfadeAudioClips(a.id, a.id));
        QVERIFY(!document.crossfadeAudioClips(a.id, id()));
        QVERIFY(!document.crossfadeAudioClips(a.id, b.id, AudioFadeCurve(99)));
        QVERIFY(document.beginLayerPropertyEdit()); QVERIFY(!document.crossfadeAudioClips(a.id, b.id)); document.cancelLayerPropertyEdit();
        QCOMPARE(document.project().toJson(), unchanged); QCOMPARE(document.history()->count(), commands);
    }
    void trimsAndRippleCutsPreserveSourceEnvelope() {
        auto project = Project::create(); project.fps = 120; project.shots[0].durationFrames = 10;
        auto original = clip(asset(), 100, 3900);
        original.fadeInSamples = original.fadeOutSamples = 1200;
        original.gainAnimated = true; original.gainKeys = {{100, .2}, {1800, 1.8}, {3999, .7}};
        project.shots[0].audioClips = {original};
        Document document; document.load(project);
        auto trimmed = original; trimmed.sourceOffsetSamples = 500; trimmed.sampleCount = 2900;
        QVERIFY(document.updateAudioClip(trimmed));
        const auto result = document.activeShot().audioClips[0];
        QCOMPARE(result.fadeInStartSample, qint64(100)); QCOMPARE(result.fadeOutEndSample, qint64(4000));
        for (qint64 i = 500; i < 3400; i += 31)
            QCOMPARE(audioClipGainAtSourceSample(result, i), audioClipGainAtSourceSample(original, i));
        document.history()->undo(); QCOMPARE(document.activeShot().audioClips[0], original);
        document.history()->redo(); QCOMPARE(document.activeShot().audioClips[0], result);
        auto freshFade = result; freshFade.sourceOffsetSamples += 100; freshFade.sampleCount -= 100;
        freshFade.fadeInSamples = 200; freshFade.fadeInStartSample = -1;
        QVERIFY(document.updateAudioClip(freshFade));
        QCOMPARE(document.activeShot().audioClips[0].fadeInStartSample, qint64(-1));
        QCOMPARE(audioClipGainAtSourceSample(document.activeShot().audioClips[0], 600), 0.);
        for (const auto edit : {TimelineRippleEdit{TimelineRippleKind::Insert, 2, 1},
                               TimelineRippleEdit{TimelineRippleKind::Delete, 2, 1},
                               TimelineRippleEdit{TimelineRippleKind::Move, 2, 2, 8}}) {
            auto shot = project.shots[0]; QVERIFY2(applyTimelineRipple(shot, project.fps, edit).isEmpty(), "Ripple must preserve envelope bounds");
            QVERIFY(shot.audioClips.size() >= 2);
            for (const auto &piece : shot.audioClips) {
                QCOMPARE(piece.gainKeys, original.gainKeys);
                QCOMPARE(piece.fadeInStartSample, qint64(100)); QCOMPARE(piece.fadeOutEndSample, qint64(4000));
                for (qint64 i = piece.sourceOffsetSamples; i < piece.sourceOffsetSamples + piece.sampleCount; i += 31)
                    QCOMPARE(audioClipGainAtSourceSample(piece, i), audioClipGainAtSourceSample(original, i));
            }
        }
    }
    void strictValidationRejectsInvalidFinishingWithoutMutatingHistory() {
        Document document; auto project = Project::create(); auto original = clip(asset());
        project.shots[0].audioClips = {original}; document.load(project);
        const auto before = document.project().toJson();
        const QVector<std::function<void(AudioClip &)>> changes{
            [](auto &c) { c.fadeInSamples = -1; }, [](auto &c) { c.fadeOutSamples = 1001; },
            [](auto &c) { c.fadeInCurve = AudioFadeCurve(9); }, [](auto &c) { c.fadeOutEndSample = 0; },
            [](auto &c) { c.fadeInStartSample = 9000; }, [](auto &c) { c.fadeInStartSample = -2; },
            [](auto &c) { c.gainAnimated = true; }, [](auto &c) { c.gainKeys = {{500, .5}, {500, 1}}; },
            [](auto &c) { c.gainKeys = {{501, .5}, {500, 1}}; }, [](auto &c) { c.gainKeys = {{9000, 1}}; },
            [](auto &c) { c.gainKeys = {{1, std::numeric_limits<double>::infinity()}}; },
            [](auto &c) { c.gainKeys = {{1, -.1}}; }, [](auto &c) { c.gainKeys = {{1, 2.1}}; },
            [](auto &c) { for (int i = 0; i <= maxAudioGainKeys; ++i) c.gainKeys.append({i, 1}); },
            [](auto &c) { c.fadeOutEndSample = 20; c.fadeOutSamples = 21; },
            [](auto &c) { c.fadeInStartSample = 8900; c.fadeInSamples = 101; }
        };
        for (const auto &change : changes) { auto invalid = original; change(invalid); QVERIFY(!document.updateAudioClip(invalid)); }
        QCOMPARE(document.project().toJson(), before); QCOMPARE(document.history()->count(), 0);
        auto valid = original; valid.gainKeys = {{10, .4}}; valid.gainAnimated = false;
        QVERIFY(document.updateAudioClip(valid)); QVERIFY(audioHistoryCost(project, document.project()) >= 512 + qint64(sizeof(AudioGainKey)));
    }
    void finishingRoundTripsVersion23AndLegacyClipsStayUnchanged() {
        auto project = Project::create(); auto audio = clip(asset(), 100, 1200);
        project.shots[0].audioClips = {audio};
        QCOMPARE(project.toJson()["version"].toInt(), 19);
        QCOMPARE(audioClipsToJson({audio})[0].toObject().size(), 8);
        audio.fadeInSamples = 101; audio.fadeOutSamples = 99; audio.fadeInCurve = AudioFadeCurve::EqualPower;
        audio.gainKeys = {{20, .3}, {400, 1.7}}; // Disabled animation is retained.
        materializeAudioFadeAnchors(audio); project.shots[0].audioClips = {audio};
        QCOMPARE(project.toJson()["version"].toInt(), 23);
        QTemporaryDir directory; const auto path = directory.filePath("finishing.toon"); QString error;
        QVERIFY2(saveProject(project, path, &error), qPrintable(error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.read(10), QByteArray("TOONPRO23\n")); file.close();
        const auto loaded = loadProject(path, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->shots[0].audioClips, project.shots[0].audioClips);
        QCOMPARE(loaded->shots[0].audioClips[0].asset->pcm, audio.asset->pcm);
        auto disguised = project.toJson(); disguised["version"] = 22;
        QVERIFY(!Project::fromJson(disguised, &error)); QVERIFY(error.contains("23"));
        const QVector<std::function<void(QJsonObject &)>> mutations{
            [](auto &f) { f["extra"] = 1; }, [](auto &f) { f.remove("gainKeys"); },
            [](auto &f) { f["fadeInSamples"] = 2.5; }, [](auto &f) { f["fadeInCurve"] = "unknown"; },
            [](auto &f) { f["gainAnimated"] = 1; }, [](auto &f) { f["gainKeys"] = "keys"; },
            [](auto &f) { f["fadeOutEndSample"] = 0; }, [](auto &f) { f["fadeOutSamples"] = 99999999; },
            [](auto &f) { f["gainKeys"] = QJsonArray{QJsonObject{{"sourceSample", .5}, {"gain", 1}}}; },
            [](auto &f) { f["gainKeys"] = QJsonArray{QJsonObject{{"sourceSample", 500}, {"gain", -1}}}; },
            [](auto &f) { f["gainKeys"] = QJsonArray{QJsonObject{{"sourceSample", 500}, {"gain", 1}, {"extra", true}}}; }
        };
        for (const auto &mutation : mutations) QVERIFY(!Project::fromJson(editedFinishing(project.toJson(), mutation), &error));
        auto bad = project; bad.shots[0].audioClips[0].gainKeys[0].gain = -1;
        QVERIFY(!saveProject(bad, path, &error));
        const auto protectedFile = loadProject(path, &error); QVERIFY(protectedFile);
        QCOMPARE(protectedFile->toJson(), project.toJson());
    }
};
QTEST_MAIN(AudioFinishingTests)
#include "audio_finishing_tests.moc"
