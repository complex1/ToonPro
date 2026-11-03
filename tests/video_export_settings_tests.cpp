#include "animation_test_fixture.h"
#include "audio.h"
#include "video_export_settings.h"
#include <QColorSpace>
#include <QPainter>
#include <QtTest>
#include <cmath>
#include <iterator>
#include <limits>

using namespace toon;
namespace {
Project fixture() {
    auto project = Project::create();
    project.width = 128; project.height = 64;
    project.shots[0].durationFrames = 60;
    return project;
}
AudioClip clip(bool muted = false, double gain = 1) {
    const auto asset = makeAudioAsset("Audio", QByteArray(audioSampleRate * audioBytesPerSampleFrame, '\0'));
    return {animation_fixture::id(), "Audio", asset, 0, 0, audioSampleRate, gain, muted};
}
}
class VideoExportSettingsTests final : public QObject {
    Q_OBJECT
private slots:
    void defaultsUseWholeRequestedShotAndAudibleClips() {
        auto project = fixture();
        auto second = Project::create().shots.first(); second.durationFrames = 31;
        second.audioClips = {clip()}; project.shots.append(second);
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        auto settings = videoExportDefaults(project);
        QCOMPARE(settings.shotIndex, 0); QCOMPARE(settings.firstFrame, 0); QCOMPARE(settings.lastFrame, 59);
        QCOMPARE(settings.size, QSize(128, 64)); QCOMPARE(settings.quality, VideoExportQuality::High);
        QVERIFY(!settings.includeAudio);
        settings = videoExportDefaults(project, 1);
        QCOMPARE(settings.shotIndex, 1); QCOMPARE(settings.lastFrame, 30); QVERIFY(settings.includeAudio);
        for (const auto &silent : {clip(true), clip(false, 0)}) {
            project.shots[1].audioClips = {silent};
            QVERIFY(!videoExportDefaults(project, 1).includeAudio);
        }
        // A defensive default must not enable audio for clips wholly outside the range.
        auto outside = clip(); outside.startFrame = 90; project.shots[1].audioClips = {outside};
        QVERIFY(!videoExportDefaults(project, 1).includeAudio);
        QVERIFY(!validateVideoExportSettings(project, videoExportDefaults(project, 1)).isEmpty());
    }
    void defaultsRoundToEvenAndFitEncoderLimit() {
        auto project = fixture(); project.width = 8191; project.height = 4095;
        auto settings = videoExportDefaults(project);
        QCOMPARE(settings.size, QSize(4096, 2048));
        QVERIFY(validateVideoExportSettings(project, settings).isEmpty());
        project.width = 101; project.height = 65;
        settings = videoExportDefaults(project);
        QCOMPARE(settings.size, QSize(102, 66));
        QVERIFY(validateVideoExportSettings(project, settings).isEmpty());
    }
    void invalidShotDurationsHaveSafeDefaultsAndFailValidation() {
        for (const int duration : {0, -1, std::numeric_limits<int>::min()}) {
            auto project = fixture(); project.shots[0].durationFrames = duration;
            const auto settings = videoExportDefaults(project);
            QCOMPARE(settings.firstFrame, 0); QCOMPARE(settings.lastFrame, 0);
            QVERIFY(!settings.includeAudio);
            QVERIFY(!validateVideoExportSettings(project, settings).isEmpty());
        }
    }
    void invalidSettings_data() {
        QTest::addColumn<int>("invalid");
        const char *names[] = {"negative-shot", "missing-shot", "negative-start", "reversed-range", "past-shot",
            "zero-width", "too-small", "odd-width", "odd-height", "too-large", "unknown-quality",
            "zero-fps", "high-fps", "nan-fps", "infinite-fps", "invalid-source-size", "invalid-project"};
        for (int i = 0; i < int(std::size(names)); ++i) QTest::newRow(names[i]) << i;
    }
    void invalidSettings() {
        QFETCH(int, invalid);
        auto project = fixture(); auto settings = videoExportDefaults(project);
        switch (invalid) {
        case 0: settings.shotIndex = -1; break;
        case 1: settings.shotIndex = 1; break;
        case 2: settings.firstFrame = -1; break;
        case 3: settings.firstFrame = 20; settings.lastFrame = 19; break;
        case 4: settings.lastFrame = 60; break;
        case 5: settings.size.setWidth(0); break;
        case 6: settings.size.setHeight(14); break;
        case 7: settings.size.setWidth(127); break;
        case 8: settings.size.setHeight(63); break;
        case 9: settings.size.setWidth(4098); break;
        case 10: settings.quality = static_cast<VideoExportQuality>(99); break;
        case 11: project.fps = 0; break;
        case 12: project.fps = 121; break;
        case 13: project.fps = std::numeric_limits<double>::quiet_NaN(); break;
        case 14: project.fps = std::numeric_limits<double>::infinity(); break;
        case 15: project.width = 63; break;
        case 16: project.shots[0].layers[0].id = "invalid-id"; break;
        }
        QVERIFY(!validateVideoExportSettings(project, settings).isEmpty());
    }
    void aSingleFrameAndBoundarySizesAreValid() {
        auto project = fixture(); auto settings = videoExportDefaults(project);
        settings.firstFrame = settings.lastFrame = 59;
        for (const auto size : {QSize(16, 16), QSize(4096, 4096)}) {
            settings.size = size;
            for (const auto quality : {VideoExportQuality::Draft, VideoExportQuality::Standard, VideoExportQuality::High}) {
                settings.quality = quality;
                QVERIFY2(validateVideoExportSettings(project, settings).isEmpty(), qPrintable(validateVideoExportSettings(project, settings)));
            }
        }
    }
    void fractionalRangeUsesRoundedAbsoluteSampleEndpoints() {
        auto project = fixture(); project.fps = 29.97;
        auto settings = videoExportDefaults(project); settings.firstFrame = 6; settings.lastFrame = 8;
        // round(9 * 48000 / 29.97) - round(6 * 48000 / 29.97), not round(3 * 48000 / 29.97).
        QCOMPARE(videoExportSampleCount(project, settings), qint64(4804));
        QCOMPARE(qRound64(3 * 48000.0 / project.fps), qint64(4805));
        for (double fps : {23.976, 29.97, 59.94, 120.}) {
            project.fps = fps;
            for (const auto range : {QPair(0, 0), QPair(6, 8), QPair(17, 59)}) {
                settings.firstFrame = range.first; settings.lastFrame = range.second;
                const auto endpoint = [fps](int frame) { return qint64(std::floor(static_cast<long double>(frame) * 48000 / fps + .5L)); };
                QCOMPARE(videoExportSampleCount(project, settings), endpoint(range.second + 1) - endpoint(range.first));
            }
        }
        settings.lastFrame = 60; QCOMPARE(videoExportSampleCount(project, settings), qint64(0));
    }
    void timestampsDeriveFromFrameIndexWithoutAccumulatedDrift() {
        QCOMPARE(videoExportFrameTimeUSecs(0, 29.97), qint64(0));
        QCOMPARE(videoExportFrameTimeUSecs(1, 29.97), qint64(33367));
        QCOMPARE(videoExportFrameTimeUSecs(30, 29.97), qint64(1001001));
        QCOMPARE(videoExportFrameTimeUSecs(999999, 29.97), qint64(std::floor(999999.L * 1000000 / 29.97 + .5L)));
        QCOMPARE(videoExportFrameTimeUSecs(-1, 24), qint64(0));
        QCOMPARE(videoExportFrameTimeUSecs(1, 0), qint64(0));
        QCOMPARE(videoExportFrameTimeUSecs(1, std::numeric_limits<double>::quiet_NaN()), qint64(0));
    }
    void rendersAbsoluteExposureFramesAndBlankGaps() {
        const auto project = animation_fixture::project();
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        auto settings = videoExportDefaults(project); settings.firstFrame = 3; settings.lastFrame = 6;
        QString error = "stale";
        const auto gap = renderVideoExportFrame(project, settings, 3, &error);
        QVERIFY2(!gap.isNull(), qPrintable(error)); QVERIFY(error.isEmpty());
        QCOMPARE(gap.pixelColor(64, 64), QColor(Qt::white));
        const auto drawing = renderVideoExportFrame(project, settings, 4, &error);
        QCOMPARE(drawing.pixelColor(64, 64), QColor(Qt::blue));
        QVERIFY(renderVideoExportFrame(project, settings, 0, &error).isNull()); QVERIFY(!error.isEmpty());
        QVERIFY(renderVideoExportFrame(project, settings, 7, &error).isNull()); QVERIFY(!error.isEmpty());
    }
    void fitsArtworkWithOpaqueWhiteMatteAndDoesNotMutateSource() {
        auto project = fixture(); project.transparent = true;
        auto &layer = project.shots[0].layers[0];
        QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(QColor(255, 0, 0, 128));
        layer.tiles.insert({0, 0}, tile);
        LayerAnimation animation; animation.layerId = layer.id;
        animation.channels = {{KeyframeProperty::PositionX, {{0, 0}, {10, 32}}}};
        project.shots[0].layerAnimations = {animation};
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        const auto metadata = project.toJson(); const auto tiles = layer.tiles;
        auto settings = videoExportDefaults(project); settings.size = QSize(128, 128); settings.firstFrame = 10; settings.lastFrame = 12;
        QString error;
        const auto rendered = renderVideoExportFrame(project, settings, 10, &error);
        QVERIFY2(!rendered.isNull(), qPrintable(error)); QCOMPARE(rendered.size(), settings.size);
        QCOMPARE(rendered.colorSpace(), QColorSpace(QColorSpace::SRgb));
        QCOMPARE(rendered.pixelColor(64, 0), QColor(Qt::white));
        QCOMPARE(rendered.pixelColor(64, 127), QColor(Qt::white));
        QCOMPARE(rendered.pixelColor(16, 64), QColor(Qt::white)); // Absolute frame 10 translates the artwork by 32px.
        const auto pink = rendered.pixelColor(64, 64);
        QCOMPARE(pink.red(), 255); QVERIFY(qAbs(pink.green() - 127) <= 1); QVERIFY(qAbs(pink.blue() - 127) <= 1);
        for (int y = 0; y < rendered.height(); ++y)
            for (int x = 0; x < rendered.width(); ++x) QCOMPARE(rendered.pixelColor(x, y).alpha(), 255);
        QCOMPARE(project.toJson(), metadata); QCOMPARE(project.shots[0].layers[0].tiles, tiles);
    }
    void requestedShotIsRenderedWithoutSheetMarginsOrNotes() {
        auto project = fixture();
        auto second = Project::create().shots.first(); second.durationFrames = 20;
        QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::green);
        second.layers[0].tiles.insert({0, 0}, tile);
        auto note = RasterLayer::create("Production notes"); note.annotation = true;
        tile.fill(Qt::red); note.tiles.insert({0, 0}, tile); second.layers.append(note);
        project.shots.append(second);
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        auto settings = videoExportDefaults(project, 1); QString error;
        const auto rendered = renderVideoExportFrame(project, settings, 0, &error);
        QVERIFY2(!rendered.isNull(), qPrintable(error)); QCOMPARE(rendered.size(), QSize(128, 64));
        QCOMPARE(rendered.pixelColor(0, 0), QColor(Qt::green));
        QCOMPARE(rendered.pixelColor(127, 63), QColor(Qt::green));
    }
};
QTEST_MAIN(VideoExportSettingsTests)
#include "video_export_settings_tests.moc"
