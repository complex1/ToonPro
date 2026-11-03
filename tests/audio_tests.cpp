#include "audio.h"
#include "document.h"
#include <QtEndian>
#include <QtTest>
#include <QUuid>
#include <limits>
using namespace toon;
namespace {
QByteArray pcm(qint64 frames, qint16 left = 1000, qint16 right = -2000) {
    QByteArray bytes(frames * audioBytesPerSampleFrame, '\0');
    for (qint64 i = 0; i < frames; ++i) {
        qToLittleEndian(left, reinterpret_cast<uchar *>(bytes.data()) + i * 4);
        qToLittleEndian(right, reinterpret_cast<uchar *>(bytes.data()) + i * 4 + 2);
    }
    return bytes;
}
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
}
class AudioTests : public QObject {
    Q_OBJECT
private slots:
    void immutableContentIdsStereoPeaksAndExactEdgeQueries() {
        auto bytes = pcm(800, 0, 0);
        qToLittleEndian(qint16(16384), reinterpret_cast<uchar *>(bytes.data()) + 512 * 4);
        qToLittleEndian(qint16(-32768), reinterpret_cast<uchar *>(bytes.data()) + 513 * 4 + 2);
        QString error; const auto asset = makeAudioAsset("  Dialogue  ", bytes, &error);
        QVERIFY2(asset, qPrintable(error)); QCOMPARE(asset->name, QString("Dialogue")); QCOMPARE(asset->sampleFrameCount(), 800);
        const auto duplicate = makeAudioAsset("Other name", bytes); QCOMPARE(duplicate->id, asset->id);
        bytes[0] = 1; QVERIFY(makeAudioAsset("Changed", bytes)->id != asset->id);
        QCOMPARE(asset->pcm[0], char(0));
        char borrowed[4]{};
        const auto owned = makeAudioAsset("Borrowed", QByteArray::fromRawData(borrowed, 4));
        borrowed[0] = 127; QCOMPARE(owned->pcm[0], char(0));
        const auto all = audioPeak(*asset, 0, 800); QCOMPARE(all.minimum, -1.f); QCOMPARE(all.maximum, .5f);
        QCOMPARE(audioPeak(*asset, 100, 412).maximum, 0.f);
        QCOMPARE(audioPeak(*asset, 100, 413).maximum, .5f);
        QCOMPARE(audioPeak(*asset, 512, 1).minimum, 0.f);
        QCOMPARE(audioPeak(*asset, 513, 1).minimum, -1.f);
        QCOMPARE(audioPeak(*asset, -1, 800).maximum, 0.f);
        QCOMPARE(audioPeak(*asset, 800, 1).maximum, 0.f);
        QCOMPARE(audioPeak(*asset, 799, std::numeric_limits<qint64>::max()).maximum, 0.f);
        QVERIFY(!makeAudioAsset("Empty", {}, &error)); QVERIFY(!makeAudioAsset("Bad alignment", QByteArray(3, 0), &error));
        QVERIFY(!makeAudioAsset(" ", pcm(1), &error));
    }
    void sampleTimingDoesNotStretchWithFpsAndPreservesPartialFrames() {
        AudioClip clip; clip.sampleCount = 48000;
        QCOMPARE(audioClipDurationFrames(clip, 24), 24); QCOMPARE(audioClipDurationFrames(clip, 30), 30);
        QCOMPARE(clip.sampleCount, 48000); clip.sampleCount = 48001;
        QCOMPARE(audioClipDurationFrames(clip, 24), 25); clip.sampleCount = 1;
        QCOMPARE(audioClipDurationFrames(clip, 24), 1);
        QCOMPARE(audioSampleFrameAtTimelineFrame(24, 24), 48000);
        QCOMPARE(audioSampleFrameAtTimelineFrame(23.976, 23.976), 48000);
        QCOMPARE(audioSampleFrameAtTimelineFrame(1, 29.97), 1602);
        QCOMPARE(audioSampleFrameAtTimelineFrame(qQNaN(), 24), 0);
    }
    void importEditDeleteAreAtomicUndoableAndShareSamples() {
        Document document; auto project = Project::create(); project.shots[0].durationFrames = 20;
        project.shots[0].layers[0].locked = true; document.load(project);
        const auto source = makeAudioAsset("Music", pcm(48000)); QVERIFY(source);
        const auto first = document.addAudioClip(source, 19); QVERIFY(!first.isEmpty());
        QCOMPARE(document.history()->count(), 1); QCOMPARE(document.project().shots[0].audioClips.size(), 1);
        auto clip = document.project().shots[0].audioClips[0]; QCOMPARE(clip.asset, source);
        QCOMPARE(audioClipDurationFrames(clip, project.fps), 24); // Extending past shot end is allowed.
        clip.name = "Trimmed"; clip.startFrame = 2; clip.sourceOffsetSamples = 100; clip.sampleCount = 900; clip.gain = 1.5; clip.muted = true;
        QVERIFY(document.updateAudioClip(clip)); QCOMPARE(document.history()->count(), 2);
        QVERIFY(document.updateAudioClip(clip)); QCOMPARE(document.history()->count(), 2);
        QCOMPARE(document.project().shots[0].audioClips[0].asset, source);
        const auto second = document.addAudioClip(makeAudioAsset("Reimport", source->pcm), 2);
        QVERIFY(!second.isEmpty()); QCOMPARE(document.project().shots[0].audioClips[1].asset, source);
        QCOMPARE(audioResources(document.project()).size(), 1);
        QVERIFY(document.removeAudioClip(first)); QCOMPARE(document.project().shots[0].audioClips.size(), 1);
        document.history()->undo(); QCOMPARE(document.project().shots[0].audioClips[0], clip);
        document.history()->undo(); QCOMPARE(document.project().shots[0].audioClips.size(), 1);
        document.history()->undo(); QCOMPARE(document.project().shots[0].audioClips[0].sampleCount, 48000);
        document.history()->undo(); QVERIFY(document.project().shots[0].audioClips.isEmpty());
        document.history()->redo(); QCOMPARE(document.project().shots[0].audioClips[0].asset, source);
    }
    void invalidChangesAndPendingGesturesPreserveProject() {
        Document document; document.load(Project::create());
        const auto clipId = document.addAudioClip(makeAudioAsset("Dialogue", pcm(4800)), 0); QVERIFY(!clipId.isEmpty());
        const auto original = document.project().shots[0].audioClips[0]; const auto before = document.project().toJson(); const int count = document.history()->count();
        for (int failure = 0; failure < 7; ++failure) {
            auto invalid = original;
            if (failure == 0) invalid.startFrame = -1;
            if (failure == 1) invalid.sourceOffsetSamples = 4800;
            if (failure == 2) invalid.sampleCount = 0;
            if (failure == 3) invalid.sampleCount = std::numeric_limits<qint64>::max();
            if (failure == 4) invalid.gain = qQNaN();
            if (failure == 5) invalid.name = "";
            if (failure == 6) invalid.asset.reset();
            QVERIFY(!document.updateAudioClip(invalid));
        }
        QVERIFY(document.addAudioClip(original.asset, 120).isEmpty());
        QVERIFY(!document.removeAudioClip("missing"));
        QVERIFY(document.beginLayerPropertyEdit());
        auto changed = original; changed.gain = .5;
        QVERIFY(!document.updateAudioClip(changed)); QVERIFY(!document.removeAudioClip(original.id));
        QVERIFY(document.addAudioClip(original.asset, 0).isEmpty()); document.cancelLayerPropertyEdit();
        QCOMPARE(document.project().toJson(), before); QCOMPARE(document.history()->count(), count);
    }
    void clipLimitsProjectIdentityAndHistoryAccounting() {
        auto project = Project::create(); const auto asset = makeAudioAsset("Shared", pcm(100));
        for (int i = 0; i < maxAudioClipsPerShot; ++i) project.shots[0].audioClips.append({id(), "Clip", asset, 0, 0, 100, 1, false});
        QVERIFY(project.validate().isEmpty()); QCOMPARE(audioHistoryCost(project, project), 0);
        auto changed = project; changed.shots[0].audioClips[0].gain = .25;
        QVERIFY(audioHistoryCost(project, changed) < asset->pcm.size() + maxAudioClipsPerShot * 512 + 1);
        Document document; document.load(project);
        QVERIFY(document.addAudioClip(asset, 0).isEmpty()); QCOMPARE(document.history()->count(), 0);
        auto invalid = project; invalid.shots[0].audioClips[0].id = invalid.shots[0].layers[0].id;
        QVERIFY(!invalid.validate().isEmpty());
        auto empty = Project::create();
        QVERIFY(audioHistoryCost(empty, project) >= asset->pcm.size());
    }
};
QTEST_MAIN(AudioTests)
#include "audio_tests.moc"
