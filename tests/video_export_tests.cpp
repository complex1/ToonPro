#include "audio.h"
#include "video_export.h"
#include <QAudioDecoder>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUuid>
#include <QVideoFrame>
#include <QVideoSink>
#include <QtEndian>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

using namespace toon;
namespace {
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
Project blankProject(double fps = 30) {
    auto project = Project::create(); project.width = 160; project.height = 90; project.fps = fps;
    project.shots[0].durationFrames = 210;
    return project;
}
QByteArray readFile(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
bool writeFile(const QString &path, const QByteArray &bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
bool hasMp4Container(const QByteArray &bytes) {
    return bytes.size() > 128 && bytes.mid(4, 4) == "ftyp" && bytes.contains("moov") && bytes.contains("mdat") && bytes.contains("vide");
}
AudioAssetPtr sineAsset(const QString &name, int seconds, double leftHz, double rightHz, int leftLevel, int rightLevel) {
    QByteArray pcm(seconds * audioSampleRate * audioBytesPerSampleFrame, Qt::Uninitialized);
    constexpr double tau = 6.2831853071795864769;
    for (int i = 0; i < seconds * audioSampleRate; ++i) {
        qToLittleEndian<qint16>(qint16(std::lround(leftLevel * std::sin(tau * leftHz * i / audioSampleRate))), pcm.data() + i * 4);
        qToLittleEndian<qint16>(qint16(std::lround(rightLevel * std::sin(tau * rightHz * i / audioSampleRate))), pcm.data() + i * 4 + 2);
    }
    return makeAudioAsset(name, pcm);
}
Project smokeProject() {
    auto project = blankProject(); project.transparent = true;
    auto &shot = project.shots[0]; auto &layer = shot.layers[0];
    QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::transparent);
    QPainter painter(&tile); painter.fillRect(QRect(8, 24, 20, 30), QColor(230, 30, 30)); painter.end();
    layer.tiles.insert({0, 0}, tile);
    LayerAnimation animation; animation.layerId = layer.id;
    animation.channels = {{KeyframeProperty::PositionX, {{0, 0}, {209, 104.5}}}};
    shot.layerAnimations = {animation};
    const auto a = sineAsset("A", 3, 440, 660, 10000, 6000);
    const auto b = sineAsset("B", 3, 880, 330, 8000, 10000);
    const auto muted = sineAsset("Muted", 5, 110, 220, 30000, 30000);
    shot.audioClips = {
        {uuid(), "A trimmed", a, 45, 4800, 96000, .5, false},
        {uuid(), "B trimmed", b, 75, 9600, 96000, .5, false},
        {uuid(), "Muted", muted, 30, 0, 240000, 1, true}
    };
    return project;
}
double rms(const QByteArray &pcm, int channel, double startSeconds, double endSeconds) {
    const int first = int(startSeconds * audioSampleRate), end = int(endSeconds * audioSampleRate);
    if (pcm.size() < end * audioBytesPerSampleFrame) return -1;
    long double energy = 0;
    for (int frame = first; frame < end; ++frame) {
        qint16 sample = 0;
        std::memcpy(&sample, pcm.constData() + frame * audioBytesPerSampleFrame + channel * 2, 2);
        energy += double(sample) * sample;
    }
    return std::sqrt(double(energy / (end - first)));
}
}

class VideoExportTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<VideoExportResult>();
        QVERIFY2(VideoExport::availabilityError(true).isEmpty(), qPrintable(VideoExport::availabilityError(true)));
        QVERIFY2(VideoExport::availabilityError(false).isEmpty(), qPrintable(VideoExport::availabilityError(false)));
    }
    void fiveSecondMovieIncludesTrimmedOverlappingStereoAudio() {
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const QString retained = qEnvironmentVariable("TOON_VIDEO_TEST_OUTPUT");
        const QString path = retained.isEmpty() ? temporary.filePath("smoke.mp4") : QFileInfo(retained).absoluteFilePath();
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        auto project = smokeProject();
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        auto settings = videoExportDefaults(project); settings.firstFrame = 30; settings.lastFrame = 179; settings.size = QSize(320, 180);
        QVERIFY(settings.includeAudio); QCOMPARE(videoExportSampleCount(project, settings), qint64(240000));
        const auto metadata = project.toJson(); const auto tiles = project.shots[0].layers[0].tiles;
        const auto first = renderVideoExportFrame(project, settings, 30);
        const auto last = renderVideoExportFrame(project, settings, 179);
        QVERIFY(!first.isNull()); QVERIFY(first != last);
        if (!retained.isEmpty()) {
            QVERIFY(first.save(path + ".first.png")); QVERIFY(last.save(path + ".last.png"));
        }
        VideoExport exporter; QSignalSpy finished(&exporter, &VideoExport::finished), progress(&exporter, &VideoExport::progress);
        QVERIFY(exporter.start(project, settings, path)); QVERIFY(exporter.busy()); QCOMPARE(finished.size(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 45000);
        const auto result = qvariant_cast<VideoExportResult>(finished[0][0]);
        QVERIFY2(result.outcome == VideoExportOutcome::Completed, qPrintable(result.error));
        QVERIFY(result.error.isEmpty()); QCOMPARE(result.path, path); QCOMPARE(result.framesEncoded, 150); QVERIFY(!exporter.busy());
        QCOMPARE(project.toJson(), metadata); QCOMPARE(project.shots[0].layers[0].tiles, tiles);
        int previous = 0;
        for (const auto &event : progress) {
            const int count = event[0].toInt(); QVERIFY(count >= previous); QVERIFY(count <= 150);
            QCOMPARE(event[1].toInt(), 150); previous = count;
        }
        QCOMPARE(previous, 150);
        const auto bytes = readFile(path); QVERIFY(hasMp4Container(bytes)); QVERIFY(bytes.contains("soun"));

        // QAudioDecoder reads the exported AAC stream without creating an audio output device.
        QAudioDecoder decoder; QAudioFormat format;
        format.setSampleRate(audioSampleRate); format.setChannelConfig(QAudioFormat::ChannelConfigStereo); format.setSampleFormat(QAudioFormat::Int16);
        decoder.setAudioFormat(format); QByteArray decoded; QString decodeError; bool done = false;
        connect(&decoder, &QAudioDecoder::bufferReady, this, [&] {
            const auto buffer = decoder.read();
            if (buffer.format() != format) { decodeError = "Unexpected decoded audio format"; return; }
            decoded.append(buffer.constData<char>(), buffer.byteCount());
        });
        connect(&decoder, &QAudioDecoder::finished, this, [&] { done = true; });
        connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this, [&](QAudioDecoder::Error) { decodeError = decoder.errorString(); });
        decoder.setSource(QUrl::fromLocalFile(path)); decoder.start();
        QTRY_VERIFY_WITH_TIMEOUT(done || !decodeError.isEmpty(), 15000);
        QVERIFY2(decodeError.isEmpty(), qPrintable(decodeError)); QVERIFY(done);
        const double seconds = double(decoded.size()) / audioBytesPerSampleFrame / audioSampleRate;
        QVERIFY2(qAbs(seconds - 5) < .06, qPrintable(QString("Decoded duration: %1").arg(seconds)));
        for (int channel : {0, 1}) { QVERIFY(rms(decoded, channel, .1, .3) < 40); QVERIFY(rms(decoded, channel, 4, 4.7) < 40); }
        // A: .5–2.5s, B: 1.5–3.5s; the loud muted clip must never enter the mix.
        const double expected[3][2] = {{5000 / std::sqrt(2.), 3000 / std::sqrt(2.)},
            {std::sqrt((5000. * 5000 + 4000. * 4000) / 2), std::sqrt((3000. * 3000 + 5000. * 5000) / 2)},
            {4000 / std::sqrt(2.), 5000 / std::sqrt(2.)}};
        for (int segment = 0; segment < 3; ++segment)
            for (int channel = 0; channel < 2; ++channel) {
                const double actual = rms(decoded, channel, .7 + segment, 1.0 + segment);
                QVERIFY2(qAbs(actual - expected[segment][channel]) < expected[segment][channel] * .12,
                    qPrintable(QString("Segment %1 channel %2 RMS %3 expected %4").arg(segment).arg(channel).arg(actual).arg(expected[segment][channel])));
            }
    }
    void blankFractionalRangeActuallyEncodes() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto project = blankProject(29.97); auto settings = videoExportDefaults(project);
        settings.firstFrame = 6; settings.lastFrame = 8; settings.includeAudio = true;
        QCOMPARE(videoExportSampleCount(project, settings), qint64(4804));
        const auto image = renderVideoExportFrame(project, settings, 6);
        QCOMPARE(image.pixelColor(80, 45), QColor(Qt::white));
        VideoExport exporter; QSignalSpy finished(&exporter, &VideoExport::finished);
        const auto retained = qEnvironmentVariable("TOON_VIDEO_TEST_OUTPUT");
        const auto path = retained.isEmpty() ? directory.filePath("blank-fractional.mp4")
                                             : QFileInfo(retained).absoluteFilePath() + ".fractional.mp4";
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QVERIFY(exporter.start(project, settings, path));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        const auto result = qvariant_cast<VideoExportResult>(finished[0][0]);
        QVERIFY2(result.outcome == VideoExportOutcome::Completed, qPrintable(result.error));
        QCOMPARE(result.framesEncoded, 3); QVERIFY(hasMp4Container(readFile(path)));
    }
    void newOutputPublishesWithoutReplacementAndCleansStaging() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("new.mp4");
        const auto project = blankProject(); auto settings = videoExportDefaults(project);
        settings.firstFrame = 0; settings.lastFrame = 2; settings.includeAudio = false;
        settings.overwriteApproved = false;
        VideoExport exporter; QSignalSpy finished(&exporter, &VideoExport::finished);
        QVERIFY(exporter.start(project, settings, path));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        const auto result = qvariant_cast<VideoExportResult>(finished[0][0]);
        QVERIFY2(result.outcome == VideoExportOutcome::Completed, qPrintable(result.error));
        QCOMPARE(result.framesEncoded, 3);
        const auto original = readFile(path); QVERIFY(hasMp4Container(original));
        QCOMPARE(QDir(directory.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), QStringList{"new.mp4"});
        QVERIFY(exporter.start(project, settings, path));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 10000);
        QCOMPARE(qvariant_cast<VideoExportResult>(finished[1][0]).outcome, VideoExportOutcome::Failed);
        QCOMPARE(readFile(path), original);
    }
    void longFractionalRangeEncodesEveryFrameWithoutChangingProject() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        auto project = blankProject(29.97); project.shots[0].durationFrames = 600;
        auto settings = videoExportDefaults(project);
        settings.firstFrame = 6; settings.lastFrame = 305; settings.includeAudio = true;
        QCOMPARE(videoExportSampleCount(project, settings), qint64(480480));
        const auto metadata = project.toJson(); const auto tiles = project.shots[0].layers[0].tiles;
        const auto retained = qEnvironmentVariable("TOON_VIDEO_TEST_OUTPUT");
        const auto path = retained.isEmpty() ? directory.filePath("fractional-long.mp4")
                                             : QFileInfo(retained).absoluteFilePath() + ".fractional-long.mp4";
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        VideoExport exporter; QSignalSpy finished(&exporter, &VideoExport::finished), progress(&exporter, &VideoExport::progress);
        QVERIFY(exporter.start(project, settings, path));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 45000);
        const auto result = qvariant_cast<VideoExportResult>(finished[0][0]);
        QVERIFY2(result.outcome == VideoExportOutcome::Completed, qPrintable(result.error));
        QCOMPARE(result.framesEncoded, 300); QCOMPARE(result.path, path); QVERIFY(!exporter.busy());
        QVERIFY(!progress.isEmpty()); QCOMPARE(progress.last()[0].toInt(), 300); QCOMPARE(progress.last()[1].toInt(), 300);
        const auto bytes = readFile(path); QVERIFY(hasMp4Container(bytes)); QVERIFY(bytes.contains("soun"));
        QCOMPARE(project.toJson(), metadata); QCOMPARE(project.shots[0].layers[0].tiles, tiles);
    }
    void singleFrameFractionalExport_data() {
        QTest::addColumn<double>("fps"); QTest::addColumn<QString>("suffix"); QTest::addColumn<bool>("withAudio");
        for (const double fps : {1.5, 23.976, 29.97, 59.94})
            for (const bool withAudio : {false, true}) {
                const auto label = QString::number(fps, 'g', 8) + (withAudio ? "-audio" : "");
                QTest::newRow(qPrintable(label)) << fps << ".single-" + label + ".mp4" << withAudio;
            }
    }
    void singleFrameFractionalExport() {
        QFETCH(double, fps); QFETCH(QString, suffix); QFETCH(bool, withAudio);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto project = blankProject(fps); const auto metadata = project.toJson();
        auto settings = videoExportDefaults(project);
        settings.firstFrame = settings.lastFrame = 6; settings.includeAudio = withAudio;
        const auto retained = qEnvironmentVariable("TOON_VIDEO_TEST_OUTPUT");
        const auto path = retained.isEmpty() ? directory.filePath("single-frame.mp4")
                                             : QFileInfo(retained).absoluteFilePath() + suffix;
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        VideoExport exporter; QSignalSpy finished(&exporter, &VideoExport::finished);
        QVERIFY(exporter.start(project, settings, path));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        const auto result = qvariant_cast<VideoExportResult>(finished[0][0]);
        QVERIFY2(result.outcome == VideoExportOutcome::Completed, qPrintable(result.error));
        QCOMPARE(result.framesEncoded, 1); QCOMPARE(result.path, path); QVERIFY(!exporter.busy());
        const auto bytes = readFile(path); QVERIFY(hasMp4Container(bytes));
        if (withAudio) QVERIFY(bytes.contains("soun"));
        QCOMPARE(project.toJson(), metadata);

        // Read duration back from the completed container. This independently
        // catches Qt's integer-FPS fallback for a clip containing only one frame.
        QMediaPlayer reader; QString loadError;
        connect(&reader, &QMediaPlayer::errorOccurred, this,
            [&](QMediaPlayer::Error, const QString &message) { loadError = message; });
        reader.setSource(QUrl::fromLocalFile(path)); // Metadata only; no playback or audio output.
        QTRY_VERIFY_WITH_TIMEOUT((reader.mediaStatus() == QMediaPlayer::LoadedMedia && reader.duration() > 0)
            || !loadError.isEmpty(), 10000);
        QVERIFY2(loadError.isEmpty(), qPrintable(loadError));
        const qint64 expectedMS = qRound64(1000.0 / fps), durationMS = reader.duration();
        // Container time bases may round by 1ms. AAC may add one 1024-sample
        // trailing packet; do not mistake that padding for video timing drift.
        const qint64 latestMS = expectedMS + (withAudio ? 24 : 1);
        QVERIFY2(durationMS >= expectedMS - 1 && durationMS <= latestMS,
            qPrintable(QString("%1 FPS, audio %2: container duration %3ms, expected %4ms%5")
                .arg(fps, 0, 'g', 8).arg(withAudio).arg(durationMS).arg(expectedMS)
                .arg(withAudio ? " plus at most 24ms AAC padding" : " within 1ms")));
    }
    void shortExportsKeepAudibleStereo_data() {
        QTest::addColumn<double>("fps"); QTest::addColumn<int>("frames");
        QTest::newRow("59.94-single") << 59.94 << 1;
        QTest::newRow("60-single") << 60.0 << 1;
        QTest::newRow("120-single") << 120.0 << 1;
        QTest::newRow("120-two-frames") << 120.0 << 2;
    }
    void shortExportsKeepAudibleStereo() {
        QFETCH(double, fps); QFETCH(int, frames);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        auto project = blankProject(fps);
        const auto sound = sineAsset("Short stereo", 1, 440, 660, 18000, 12000);
        project.shots[0].audioClips = {{uuid(), "Short stereo", sound, 6, 0, sound->sampleFrameCount(), 1, false}};
        const auto metadata = project.toJson();
        auto settings = videoExportDefaults(project); settings.firstFrame = 6;
        settings.lastFrame = settings.firstFrame + frames - 1; settings.includeAudio = true;
        const auto sourceSamples = videoExportSampleCount(project, settings);
        QVERIFY(sourceSamples > 0 && sourceSamples < 1024);
        const auto path = directory.filePath("short-stereo.mp4");
        VideoExport exporter; QSignalSpy finished(&exporter, &VideoExport::finished);
        QVERIFY(exporter.start(project, settings, path));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        const auto result = qvariant_cast<VideoExportResult>(finished[0][0]);
        QVERIFY2(result.outcome == VideoExportOutcome::Completed, qPrintable(result.error));
        QCOMPARE(result.framesEncoded, frames); QVERIFY(readFile(path).contains("soun"));
        QCOMPARE(project.toJson(), metadata);

        QAudioDecoder decoder; QAudioFormat format;
        format.setSampleRate(audioSampleRate); format.setChannelConfig(QAudioFormat::ChannelConfigStereo);
        format.setSampleFormat(QAudioFormat::Int16); decoder.setAudioFormat(format);
        QByteArray decoded; QString decodeError; QStringList packets; bool done = false;
        connect(&decoder, &QAudioDecoder::bufferReady, this, [&] {
            const auto buffer = decoder.read();
            packets.append(QString("%1us/%2frames").arg(buffer.startTime()).arg(buffer.frameCount()));
            if (buffer.format() != format) { decodeError = "Unexpected decoded audio format"; return; }
            decoded.append(buffer.constData<char>(), buffer.byteCount());
        });
        connect(&decoder, &QAudioDecoder::finished, this, [&] { done = true; });
        connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this,
            [&](QAudioDecoder::Error) { decodeError = decoder.errorString(); });
        decoder.setSource(QUrl::fromLocalFile(path)); decoder.start();
        QTRY_VERIFY_WITH_TIMEOUT(done || !decodeError.isEmpty(), 15000);
        QVERIFY2(decodeError.isEmpty(), qPrintable(decodeError)); QVERIFY(done);
        const auto decodedSamples = decoded.size() / audioBytesPerSampleFrame;
        const auto audioDetail = QString("%1 decoded / %2 source samples; packets [%3]")
            .arg(decodedSamples).arg(sourceSamples).arg(packets.join(", "));
        QVERIFY2(decodedSamples >= sourceSamples, qPrintable(audioDetail));
        for (int channel : {0, 1})
            QVERIFY2(rms(decoded, channel, 0, double(sourceSamples) / audioSampleRate) > 500,
                qPrintable(QString("Short export lost audible channel %1; %2").arg(channel).arg(audioDetail)));

        // Decoders may expose full AAC packets beyond the MP4 edit duration.
        // Check the presented duration independently, with the same bounded
        // AAC tail allowance as the existing fractional-frame regression.
        QMediaPlayer reader; QString loadError;
        connect(&reader, &QMediaPlayer::errorOccurred, this,
            [&](QMediaPlayer::Error, const QString &message) { loadError = message; });
        reader.setSource(QUrl::fromLocalFile(path));
        QTRY_VERIFY_WITH_TIMEOUT((reader.mediaStatus() == QMediaPlayer::LoadedMedia && reader.duration() > 0)
            || !loadError.isEmpty(), 10000);
        QVERIFY2(loadError.isEmpty(), qPrintable(loadError));
        const auto expectedMS = qRound64(1000.0 * frames / fps);
        QVERIFY(reader.duration() >= expectedMS - 1);
        QVERIFY2(reader.duration() <= expectedMS + 24,
            qPrintable(QString("Short export duration %1ms exceeds %2ms plus AAC padding")
                .arg(reader.duration()).arg(expectedMS)));
    }
    void invalidSettingsFailAsynchronouslyAndPreserveDestination_data() {
        QTest::addColumn<bool>("fractionalSingle");
        QTest::newRow("movie") << false; QTest::newRow("single-fractional") << true;
    }
    void invalidSettingsFailAsynchronouslyAndPreserveDestination() {
        QFETCH(bool, fractionalSingle);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("existing.mp4"); const QByteArray original("Existing movie must survive\0exactly", 34);
        QVERIFY(writeFile(path, original));
        const auto project = blankProject(fractionalSingle ? 1.5 : 30); auto settings = videoExportDefaults(project);
        if (fractionalSingle) settings.firstFrame = settings.lastFrame = 6;
        settings.size.setWidth(159);
        VideoExport exporter; QSignalSpy finished(&exporter, &VideoExport::finished);
        QVERIFY(exporter.start(project, settings, path)); QVERIFY(exporter.busy()); QCOMPARE(finished.size(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        const auto result = qvariant_cast<VideoExportResult>(finished[0][0]);
        QCOMPARE(result.outcome, VideoExportOutcome::Failed); QVERIFY(!result.error.isEmpty());
        QCOMPARE(result.framesEncoded, 0); QVERIFY(!exporter.busy()); QCOMPARE(readFile(path), original);
        QCOMPARE(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot), QStringList{"existing.mp4"});
    }
    void outputPathErrorsPreserveExistingDirectoryContents() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto originalPath = directory.filePath("keep.mp4"); const QByteArray original("Keep existing content");
        QVERIFY(writeFile(originalPath, original));
        const auto project = blankProject(); const auto settings = videoExportDefaults(project);
        for (const auto &path : {directory.path(), directory.filePath("missing-parent/output.mp4")}) {
            VideoExport exporter; QSignalSpy finished(&exporter, &VideoExport::finished);
            QVERIFY(exporter.start(project, settings, path)); QCOMPARE(finished.size(), 0);
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
            const auto result = qvariant_cast<VideoExportResult>(finished[0][0]);
            QCOMPARE(result.outcome, VideoExportOutcome::Failed); QVERIFY(!result.error.isEmpty());
            QCOMPARE(readFile(originalPath), original); QVERIFY(!exporter.busy());
        }
        QCOMPARE(QDir(directory.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), QStringList{"keep.mp4"});
    }
    void canRestartAfterFailureAndOwnsIndependentArtworkSnapshot() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        auto project = blankProject(); auto settings = videoExportDefaults(project);
        settings.firstFrame = 6; settings.lastFrame = 8; settings.includeAudio = false;
        QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::red);
        project.shots[0].layers[0].tiles.insert({0, 0}, tile);
        const auto path = directory.filePath("snapshot.mp4");
        VideoExport exporter; QSignalSpy finished(&exporter, &VideoExport::finished);
        auto invalid = settings; invalid.lastFrame = project.shots[0].durationFrames;
        QVERIFY(exporter.start(project, invalid, path));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QCOMPARE(qvariant_cast<VideoExportResult>(finished.takeFirst()[0]).outcome, VideoExportOutcome::Failed);
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(exporter.start(project, settings, path));
        // All nested containers and images must detach from the worker's value snapshot.
        project.shots[0].layers[0].tiles[{0, 0}].fill(Qt::green);
        project.shots[0].layers[0].name = "Edited while exporting";
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        const auto result = qvariant_cast<VideoExportResult>(finished[0][0]);
        QVERIFY2(result.outcome == VideoExportOutcome::Completed, qPrintable(result.error));
        QCOMPARE(result.framesEncoded, 3); QVERIFY(!exporter.busy());
        QCOMPARE(project.shots[0].layers[0].tiles.value({0, 0}).pixelColor(80, 45), QColor(Qt::green));

        QVideoSink sink; QMediaPlayer player; QImage decoded; QString error;
        player.setVideoSink(&sink); // No QAudioOutput is created: this is an offline image assertion.
        connect(&sink, &QVideoSink::videoFrameChanged, this, [&](const QVideoFrame &frame) {
            if (decoded.isNull() && frame.isValid()) decoded = frame.toImage();
        });
        connect(&player, &QMediaPlayer::errorOccurred, this, [&](QMediaPlayer::Error, const QString &message) { error = message; });
        player.setSource(QUrl::fromLocalFile(path)); player.play();
        QTRY_VERIFY_WITH_TIMEOUT(!decoded.isNull() || !error.isEmpty(), 15000);
        player.stop(); QVERIFY2(error.isEmpty(), qPrintable(error)); QVERIFY(!decoded.isNull());
        QCOMPARE(decoded.size(), settings.size);
        const auto pixel = decoded.pixelColor(80, 45);
        QVERIFY2(pixel.red() > 230 && pixel.green() < 25 && pixel.blue() < 25, qPrintable(pixel.name()));
    }
    void animatedShotCameraSurvivesEncodingAndDecoding() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        auto project = blankProject(5); auto &shot = project.shots[0];
        QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::transparent);
        QPainter painter(&tile); painter.fillRect(QRect(98, 39, 12, 12), QColor(230, 30, 30)); painter.end();
        shot.layers[0].tiles.insert({0, 0}, tile);
        shot.camera2DKeys = {{10, {{0, 0}, 1, 0}}, {12, {{-8, 0}, 2, 0}}};
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        const auto metadata = project.toJson(); const auto originalPixels = shot.layers[0].tiles;
        auto settings = videoExportDefaults(project); settings.firstFrame = 10; settings.lastFrame = 12; settings.includeAudio = false;
        const auto retained = qEnvironmentVariable("TOON_CAMERA_VIDEO_OUTPUT");
        const auto path = retained.isEmpty() ? directory.filePath("animated-camera.mp4") : QFileInfo(retained).absoluteFilePath();
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        VideoExport exporter; QSignalSpy finished(&exporter, &VideoExport::finished);
        QVERIFY(exporter.start(project, settings, path));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        const auto result = qvariant_cast<VideoExportResult>(finished[0][0]);
        QVERIFY2(result.outcome == VideoExportOutcome::Completed, qPrintable(result.error)); QCOMPARE(result.framesEncoded, 3);

        QVideoSink sink; QMediaPlayer player; QMap<qint64, QImage> decoded; QString error;
        player.setVideoSink(&sink); // Decode images only; no audio output device.
        connect(&sink, &QVideoSink::videoFrameChanged, this, [&](const QVideoFrame &frame) {
            if (frame.isValid()) {
                const auto image = frame.toImage();
                if (!image.isNull()) decoded.insert(frame.startTime(), image);
            }
        });
        connect(&player, &QMediaPlayer::errorOccurred, this, [&](QMediaPlayer::Error, const QString &message) { error = message; });
        player.setSource(QUrl::fromLocalFile(path)); player.play();
        QTRY_VERIFY_WITH_TIMEOUT(decoded.size() >= 3 || !error.isEmpty(), 15000);
        player.stop(); QVERIFY2(error.isEmpty(), qPrintable(error)); QCOMPARE(decoded.size(), 3);
        // Independent geometry expectations, not another call to the renderer:
        // frame centre (80,45), marker centre (104,45), camera X 0/-4/-8,
        // and zoom 1/1.5/2 produce output centres 104/122/144. Pixel-index
        // centroids sit half a pixel below those geometric coordinates.
        const qreal expectedX[] = {103.5, 121.5, 143.5};
        QVector<int> areas; int frameIndex = 0; qreal previousX = -1;
        for (const auto &image : decoded) {
            QCOMPARE(image.size(), QSize(160, 90)); qreal sumX = 0, sumY = 0; int count = 0;
            for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
                const auto pixel = image.pixelColor(x, y);
                if (pixel.red() > 150 && pixel.red() > pixel.green() + 80 && pixel.red() > pixel.blue() + 80) {
                    sumX += x; sumY += y; ++count;
                }
            }
            QVERIFY2(count > 80, "The exported frame lost its red artwork marker.");
            const qreal centerX = sumX / count, centerY = sumY / count;
            QVERIFY2(qAbs(centerX - expectedX[frameIndex]) < 1.5,
                qPrintable(QString("Decoded camera frame %1: marker X %2, expected %3").arg(frameIndex).arg(centerX).arg(expectedX[frameIndex])));
            QVERIFY(qAbs(centerY - 44.5) < 1.5); QVERIFY(centerX > previousX);
            previousX = centerX; areas.append(count); ++frameIndex;
        }
        QVERIFY(areas[1] > areas[0] * 1.6); QVERIFY(areas[2] > areas[0] * 3);
        QCOMPARE(project.toJson(), metadata); QCOMPARE(project.shots[0].layers[0].tiles, originalPixels);
    }
    void curvedMotionEasingAndOrientationSurviveEncoding() {
        QTemporaryDir directory;auto project=blankProject(5);auto &shot=project.shots[0];shot.durationFrames=3;
        auto &layer=shot.layers[0];QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);
        {QPainter painter(&tile);painter.fillRect(QRect(8,16,24,8),QColor(230,20,20));}layer.tiles.insert({0,0},tile);
        ScalarKeyframe start{0,0},end{2,80};start.easing=CubicEasing{{1./3,0},{2./3,0}};
        start.spatial=SpatialTangents{{},{0,60},true};end.spatial=SpatialTangents{{0,60},{},false};
        ScalarKeyframe y{0,0};y.easing=start.easing;
        shot.layerAnimations={{layer.id,{20,20},{{KeyframeProperty::PositionX,{start,end}},{KeyframeProperty::PositionY,{y,{2,0}}}},{},true}};
        QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));const auto original=project.toJson();const auto pixels=layer.tiles;
        auto settings=videoExportDefaults(project);settings.includeAudio=false;
        const auto path=directory.filePath("advanced-motion.mp4");VideoExport exporter;QSignalSpy finished(&exporter,&VideoExport::finished);
        QVERIFY(exporter.start(project,settings,path));QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,30000);
        const auto result=qvariant_cast<VideoExportResult>(finished[0][0]);QVERIFY2(result.outcome==VideoExportOutcome::Completed,qPrintable(result.error));QCOMPARE(result.framesEncoded,3);
        QVideoSink sink;QMediaPlayer player;QMap<qint64,QImage> decoded;QString error;player.setVideoSink(&sink);
        connect(&sink,&QVideoSink::videoFrameChanged,this,[&](const QVideoFrame &frame){if(frame.isValid()){const auto image=frame.toImage();if(!image.isNull())decoded.insert(frame.startTime(),image);}});
        connect(&player,&QMediaPlayer::errorOccurred,this,[&](QMediaPlayer::Error,const QString &message){error=message;});
        player.setSource(QUrl::fromLocalFile(path));player.play();QTRY_VERIFY_WITH_TIMEOUT(decoded.size()>=3||!error.isEmpty(),15000);player.stop();
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(decoded.size(),3);
        // Time midpoint uses progress t^3=1/8, then spatial cubic control points
        // (0,0),(0,60),(80,60),(80,0) place its pivot at (23.4375,39.6875).
        const QPointF centers[]={{19.5,19.5},{22.9375,39.1875},{99.5,19.5}};int index=0;
        for(const auto &image:decoded){qreal sx=0,sy=0;int count=0;QRect occupied;
            for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto c=image.pixelColor(x,y);if(c.red()>150 && c.red()>c.green()+80 && c.red()>c.blue()+80){sx+=x;sy+=y;++count;occupied|=QRect(x,y,1,1);}}
            QVERIFY(count>120);QVERIFY2(QLineF(QPointF(sx/count,sy/count),centers[index]).length()<1.5,qPrintable(QString("Frame %1 centroid %2,%3").arg(index).arg(sx/count).arg(sy/count)));
            if(index!=1)QVERIFY(occupied.height()>occupied.width()*2);++index;
        }
        QCOMPARE(project.toJson(),original);QCOMPARE(layer.tiles,pixels);
    }
    void encodedMovieIncludesMasksLightingAndColorCorrection() {
        QTemporaryDir directory;QVERIFY(directory.isValid());auto project=blankProject(5);
        auto &shot=project.shots[0];auto &layer=shot.layers[0];
        QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);
        {QPainter p(&tile);p.fillRect(QRect(20,20,40,30),Qt::red);}layer.tiles.insert({0,0},tile);
        LayerMask mask;QImage maskTile(tileSize,tileSize,QImage::Format_Grayscale8);maskTile.fill(255);
        for(int y=0;y<tileSize;++y)memset(maskTile.scanLine(y),0,40);mask.tiles.insert({0,0},maskTile);layer.mask=mask;
        layer.effects.colorEnabled=true;layer.effects.hue=120;
        layer.effects.blurEnabled=true;layer.effects.blurRadius=3;
        layer.effects.shadowEnabled=true;layer.effects.shadowRadius=4;layer.effects.shadowOffset={40,0};layer.effects.shadowOpacity=1;
        layer.effects.glowEnabled=true;layer.effects.glowRadius=6;layer.effects.glowColor=Qt::blue;layer.effects.glowOpacity=.8;
        auto adjustment=RasterLayer::create("Color correction");adjustment.adjustment=true;
        adjustment.effects.colorEnabled=true;adjustment.effects.exposure=-1;shot.layers.append(adjustment);
        shot.camera2DKeys={{0,{},KeyframeInterpolation::Linear},{2,{{4,0},1,0},KeyframeInterpolation::Linear}};
        QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        const auto original=project.toJson();const auto originalTiles=shot.layers[0].tiles;const auto originalMask=shot.layers[0].mask;
        auto settings=videoExportDefaults(project);settings.firstFrame=0;settings.lastFrame=2;settings.includeAudio=false;
        const auto expected=renderVideoExportFrame(project,settings,0);QVERIFY(!expected.isNull());
        QCOMPARE(expected.pixelColor(25,35),QColor(Qt::white));
        QVERIFY(expected.pixelColor(50,35).green()>150);QVERIFY(expected.pixelColor(50,35).red()<20);
        QVERIFY(expected.pixelColor(90,35).red()<20);QVERIFY(expected.pixelColor(90,35).green()<20);
        const auto retained=qEnvironmentVariable("TOON_EFFECTS_VIDEO_OUTPUT");
        const auto path=retained.isEmpty()?directory.filePath("effects.mp4"):QFileInfo(retained).absoluteFilePath();
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        VideoExport exporter;QSignalSpy finished(&exporter,&VideoExport::finished);QVERIFY(exporter.start(project,settings,path));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,30000);const auto result=qvariant_cast<VideoExportResult>(finished[0][0]);
        QVERIFY2(result.outcome==VideoExportOutcome::Completed,qPrintable(result.error));QCOMPARE(result.framesEncoded,3);
        QVideoSink sink;QMediaPlayer player;QMap<qint64,QImage> decoded;QString error;player.setVideoSink(&sink);
        connect(&sink,&QVideoSink::videoFrameChanged,this,[&](const QVideoFrame &frame){
            if(frame.isValid()){const auto image=frame.toImage();if(!image.isNull())decoded.insert(frame.startTime(),image);}});
        connect(&player,&QMediaPlayer::errorOccurred,this,[&](QMediaPlayer::Error,const QString &message){error=message;});
        player.setSource(QUrl::fromLocalFile(path));player.play();QTRY_VERIFY_WITH_TIMEOUT(decoded.size()>=3||!error.isEmpty(),15000);
        player.stop();QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(decoded.size(),3);
        int frame=0;for(const auto &image:decoded) {
            const auto reference=renderVideoExportFrame(project,settings,frame);QCOMPARE(image.size(),reference.size());
            for(const auto &point:{QPoint(25,35),QPoint(50-2*frame,35),QPoint(62-2*frame,35),QPoint(90-2*frame,35)}) {
                const auto actual=image.pixelColor(point),wanted=reference.pixelColor(point);
                QVERIFY2(std::max({qAbs(actual.red()-wanted.red()),qAbs(actual.green()-wanted.green()),qAbs(actual.blue()-wanted.blue())})<=20,
                    qPrintable(QString("Effects frame %1 sample %2,%3: %4 versus %5").arg(frame).arg(point.x()).arg(point.y()).arg(actual.name()).arg(wanted.name())));
            }
            ++frame;
        }
        QCOMPARE(project.toJson(),original);QCOMPARE(shot.layers[0].tiles,originalTiles);QCOMPARE(shot.layers[0].mask,originalMask);
    }
    void cancel_data() {
        QTest::addColumn<bool>("afterProgress"); QTest::addColumn<bool>("existing"); QTest::addColumn<bool>("fractionalSingle");
        QTest::newRow("immediate-existing") << false << true << false;
        QTest::newRow("in-progress-existing") << true << true << false;
        QTest::newRow("in-progress-new") << true << false << false;
        QTest::newRow("immediate-single-fractional-existing") << false << true << true;
    }
    void cancel() {
        QFETCH(bool, afterProgress); QFETCH(bool, existing); QFETCH(bool, fractionalSingle);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("cancel.mp4"); const QByteArray original("Keep this movie");
        if (existing) QVERIFY(writeFile(path, original));
        const auto project = blankProject(fractionalSingle ? 1.5 : 30); auto settings = videoExportDefaults(project);
        settings.overwriteApproved = existing;
        if (fractionalSingle) settings.firstFrame = settings.lastFrame = 6; else settings.lastFrame = 179;
        VideoExport exporter; QSignalSpy finished(&exporter, &VideoExport::finished);
        bool receivedProgress = false;
        if (afterProgress) connect(&exporter, &VideoExport::progress, this, [&](int completed, int, const QString &) {
            if (completed > 0) { receivedProgress = true; exporter.cancel(); }
        });
        QVERIFY(exporter.start(project, settings, path)); QVERIFY(exporter.busy());
        QVERIFY(!exporter.start(project, settings, directory.filePath("must-not-start.mp4")));
        if (!afterProgress) exporter.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        const auto result = qvariant_cast<VideoExportResult>(finished[0][0]);
        QVERIFY2(result.outcome == VideoExportOutcome::Cancelled, qPrintable(result.error)); QVERIFY(!exporter.busy());
        if (afterProgress) { QVERIFY(receivedProgress); QVERIFY(result.framesEncoded > 0); }
        if (existing) QCOMPARE(readFile(path), original); else QVERIFY(!QFileInfo::exists(path));
        QCOMPARE(QDir(directory.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), existing ? QStringList{"cancel.mp4"} : QStringList{});
    }
    void destroyingActiveExporterCancelsAndPreservesDestination_data() {
        QTest::addColumn<bool>("fractionalSingle");
        QTest::newRow("in-progress-movie") << false;
        QTest::newRow("single-fractional") << true;
    }
    void destroyingActiveExporterCancelsAndPreservesDestination() {
        QFETCH(bool, fractionalSingle);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("existing.mp4"); const QByteArray original("Do not replace on shutdown");
        QVERIFY(writeFile(path, original));
        auto project = blankProject(fractionalSingle ? 1.5 : 30); project.shots[0].durationFrames = 3000;
        auto settings = videoExportDefaults(project); settings.size = QSize(640, 360);
        if (fractionalSingle) settings.firstFrame = settings.lastFrame = 6;
        auto exporter = std::make_unique<VideoExport>(); bool destroyQueued = false, destroyed = false, wasBusy = false;
        qint64 shutdownMSecs = -1;
        connect(exporter.get(), &VideoExport::progress, this, [&](int completed, int, const QString &) {
            if (!destroyQueued && completed > 0) {
                destroyQueued = true;
                // Leave the signal callback before deleting its sender. No explicit
                // cancel: the destructor must stop this in-progress atomic write.
                QMetaObject::invokeMethod(this, [&] {
                    wasBusy = exporter->busy();
                    QElapsedTimer timer; timer.start(); exporter.reset();
                    shutdownMSecs = timer.elapsed(); destroyed = true;
                }, Qt::QueuedConnection);
            }
        });
        QVERIFY(exporter->start(project, settings, path));
        if (fractionalSingle) {
            wasBusy = exporter->busy();
            QElapsedTimer timer; timer.start(); exporter.reset();
            shutdownMSecs = timer.elapsed(); destroyed = true;
        } else QTRY_VERIFY_WITH_TIMEOUT(destroyed, 15000);
        QVERIFY(wasBusy);
        QVERIFY2(shutdownMSecs < 7000, "Destroying the exporter exceeded its bounded shutdown wait.");
        QTRY_COMPARE_WITH_TIMEOUT(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot), QStringList{"existing.mp4"}, 10000);
        QCOMPARE(readFile(path), original);
    }
};
QTEST_MAIN(VideoExportTests)
#include "video_export_tests.moc"
