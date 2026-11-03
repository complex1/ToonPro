// Opt-in production workloads. Writes only temporary files; no user libraries,
// settings, windows or third-party artwork. Run profiles serially for clean RSS.
#include "audio_mix.h"
#include "document.h"
#include "project_save.h"
#include "scene3d_renderer.h"
#include "sheet.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#ifdef Q_OS_UNIX
#include <sys/resource.h>
#endif

using namespace toon;
namespace {
void check(bool value, const QString &message) { if (!value) throw std::runtime_error(message.toStdString()); }
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
qint64 peakRssBytes() {
#ifdef Q_OS_UNIX
    rusage usage{}; if (getrusage(RUSAGE_SELF, &usage)) return -1;
#ifdef Q_OS_MACOS
    return usage.ru_maxrss;
#else
    return usage.ru_maxrss * 1024LL;
#endif
#else
    return -1;
#endif
}
void print(QJsonObject result) {
    result.insert("qt", QString::fromLatin1(qVersion()));
    result.insert("os", QSysInfo::prettyProductName());
    result.insert("architecture", QSysInfo::currentCpuArchitecture());
    result.insert("process_peak_rss_bytes", double(peakRssBytes()));
    std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << std::endl;
}
QJsonObject statistics(QVector<double> values) {
    check(!values.isEmpty(), "No timing samples were collected"); std::sort(values.begin(), values.end());
    return {{"samples", values.size()}, {"min_ms", values.first()}, {"median_ms", values[values.size() / 2]},
        {"p95_ms", values[qMin(values.size() - 1, qsizetype(std::ceil(values.size() * .95)) - 1)]}, {"max_ms", values.last()}};
}
void measure(const QString &profile, const QString &operation, int count, const std::function<void(int)> &work, QJsonObject detail = {}) {
    QVector<double> times;
    for (int i = 0; i < count; ++i) { QElapsedTimer timer; timer.start(); work(i); times.append(timer.nsecsElapsed() / 1000000.); }
    detail.insert("profile", profile); detail.insert("operation", operation); detail.insert("timing", statistics(times)); print(detail);
}
Project productionMovie() {
    auto project = Project::create(); project.width = 3840; project.height = 2160; project.shots.clear();
    for (int s = 0; s < 4; ++s) {
        auto shot = Project::create().shots[0]; shot.number = QString("C%1").arg(s + 1, 3, 10, QChar('0'));
        shot.name = "Production fixture"; shot.scene = s < 2 ? "City" : "Interior"; shot.durationFrames = 240; shot.layers.clear();
        for (int l = 0; l < 24; ++l) {
            auto layer = RasterLayer::create(QString("Drawing %1").arg(l + 1));
            for (int t = 0; t < 8; ++t) {
                QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
                for (int y = 0; y < tileSize; ++y) {
                    auto *row = reinterpret_cast<QRgb *>(tile.scanLine(y));
                    for (int x = 0; x < tileSize; ++x) {
                        const quint32 seed = quint32(x / 4) * 73856093U ^ quint32(y / 4) * 19349663U
                            ^ quint32((s * 192 + l * 8 + t) * 83492791U);
                        row[x] = qRgb((seed >> 8) & 255, (seed >> 16) & 255, seed & 255);
                    }
                }
                layer.tiles.insert({(l + t * 3) % 15, (l / 2 + t) % 8}, tile);
            }
            shot.layers.append(std::move(layer));
        }
        project.shots.append(std::move(shot));
    }
    const auto error = project.validate(); check(error.isEmpty(), error); return project;
}
void storageWorkload() {
    QTemporaryDir directory; check(directory.isValid(), "Cannot create benchmark directory");
    const auto project = productionMovie();
    const QJsonObject dimensions{{"resolution", "3840x2160"}, {"shots", 4}, {"layers_per_shot", 24},
        {"tiles", 768}, {"decoded_tile_bytes", 768. * tileSize * tileSize * 4}, {"project_tile_limit", maxProjectTiles}};
    measure("layered_movie", "copy_on_write_snapshot", 101, [&](int i) {
        auto copy = project; copy.shots[0].layers[0].opacity = .5 + i * .001;
        check(copy.shots[0].layers[0].tiles.first().cacheKey() == project.shots[0].layers[0].tiles.first().cacheKey(), "Snapshot duplicated pixels");
        check(project.shots[0].layers[0].opacity == 1, "Snapshot changed the original movie");
    }, dimensions);
    Document document; document.load(project);
    measure("layered_movie", "switch_shot", 25, [&](int i) {
        const auto target = project.shots[(i + 1) % project.shots.size()].id;
        check(document.selectShot(target), "Cannot switch benchmark shot"); check(document.activeShotId() == target, "Wrong active shot");
    }, dimensions);
    measure("layered_movie", "composite_4k_frame", 7, [&](int i) {
        const auto image = renderMovieFrame(project, 0, i % project.shots.size());
        check(image.size() == QSize(3840, 2160) && image.pixelColor(10, 10).alpha() > 0, "Composite is incomplete");
    }, dimensions);
    const auto path = directory.filePath("movie.toon"); QString error;
    measure("layered_movie", "synchronous_save_reference", 1, [&](int) { check(saveProject(project, path, &error), error); }, dimensions);
    QVector<double> jobTimes, submitTimes, heartbeatDelays;
    for (int iteration = 0; iteration < 3; ++iteration) {
        ProjectSave save; QEventLoop loop; QTimer heartbeat, deadline;
        QElapsedTimer total, interval; heartbeat.setInterval(10); deadline.setSingleShot(true); bool completed = false;
        QObject::connect(&heartbeat, &QTimer::timeout, &loop, [&] {
            heartbeatDelays.append(std::max(0., interval.nsecsElapsed() / 1000000. - 10.)); interval.restart();
        });
        QObject::connect(&save, &ProjectSave::finished, &loop, [&](bool saved, bool cancelled, const QString &, const QString &failure) {
            completed = saved && !cancelled; error = failure; loop.quit();
        });
        QObject::connect(&deadline, &QTimer::timeout, &loop, [&] { error = "Project save timed out"; save.cancel(); loop.quit(); });
        total.start(); check(save.start(project, path), "Cannot start snapshot save"); submitTimes.append(total.nsecsElapsed() / 1000000.);
        interval.start(); heartbeat.start(); deadline.start(120000); loop.exec(); heartbeat.stop(); deadline.stop();
        check(completed, error); jobTimes.append(total.nsecsElapsed() / 1000000.);
    }
    auto saveResult = dimensions; saveResult.insert("profile", "layered_movie"); saveResult.insert("operation", "asynchronous_save");
    saveResult.insert("timing", statistics(jobTimes)); saveResult.insert("gui_submission", statistics(submitTimes));
    if (!heartbeatDelays.isEmpty()) saveResult.insert("gui_10ms_heartbeat_lateness", statistics(heartbeatDelays));
    saveResult.insert("saved_file_bytes", double(QFileInfo(path).size())); print(saveResult);
    std::optional<Project> loaded;
    measure("layered_movie", "load_saved_movie", 3, [&](int) { loaded = loadProject(path, &error); check(bool(loaded), error); }, dimensions);
    check(loaded->toJson() == project.toJson(), "Saved movie metadata changed");
    for (int s = 0; s < project.shots.size(); ++s) for (int l = 0; l < project.shots[s].layers.size(); ++l)
        check(loaded->shots[s].layers[l].tiles == project.shots[s].layers[l].tiles, "Saved movie pixels changed");
}
void audioWorkload() {
    constexpr int sourceFrames = 96000, blockFrames = 1024, blocks = 96;
    QByteArray pcm(sourceFrames * 4, '\0');
    for (int i = 0; i < sourceFrames; ++i) { qToLittleEndian<qint16>(1000, pcm.data() + i * 4); qToLittleEndian<qint16>(-2000, pcm.data() + i * 4 + 2); }
    const auto source = makeAudioAsset("Generated benchmark PCM", pcm); check(bool(source), "Cannot create audio");
    QVector<AudioClip> clips;
    for (int i = 0; i < maxAudioClipsPerShot; ++i) {
        AudioClip clip; clip.id = id(); clip.name = "Automated clip"; clip.asset = source; clip.startFrame = i % 8;
        clip.sampleCount = sourceFrames; clip.gain = 0; clip.gainAnimated = true;
        clip.fadeInSamples = clip.fadeOutSamples = sourceFrames / 2;
        clip.fadeInCurve = clip.fadeOutCurve = AudioFadeCurve::EqualPower;
        for (int k = 0; k < maxAudioGainKeys; ++k) clip.gainKeys.append({qint64(k) * (sourceFrames - 1) / (maxAudioGainKeys - 1), .002 + .018 * ((k + i) % 9) / 8.});
        clips.append(std::move(clip));
    }
    AudioMixStream mix(clips, 24, 24, 24, 47, true); check(mix.valid() && mix.hasAudio(), "Audio stream unavailable");
    QVector<double> times; qint64 framesRead = 0;
    for (int block = 0; block < blocks; ++block) {
        QElapsedTimer timer; timer.start(); const auto bytes = mix.read(blockFrames * 4); times.append(timer.nsecsElapsed() / 1000000.);
        check(bytes.size() == blockFrames * 4, "Audio dropped samples");
        for (int frame : {0, blockFrames - 1}) {
            const auto timelineSample = audioSampleRate + (framesRead + frame) % audioSampleRate;
            double gain = 0;
            for (const auto &clip : clips) gain += audioClipGainAtSourceSample(clip, timelineSample - audioSampleFrameAtTimelineFrame(clip.startFrame, 24));
            qint16 left = 0, right = 0; std::memcpy(&left, bytes.constData() + frame * 4, 2); std::memcpy(&right, bytes.constData() + frame * 4 + 2, 2);
            check(left == std::lround(1000 * gain) && right == std::lround(-2000 * gain), "Audio automation mismatch");
        }
        framesRead += blockFrames;
    }
    print({{"profile", "dense_audio"}, {"operation", "mix_1024_frames"}, {"clips", clips.size()}, {"gain_keys_per_clip", maxAudioGainKeys},
        {"shared_source_bytes", pcm.size()}, {"buffer_frames", blockFrames}, {"buffer_duration_ms", 1000. * blockFrames / audioSampleRate},
        {"timing", statistics(times)}, {"sample_exact_checks_passed", true}});
}
void sceneWorkload() {
    const auto models = builtInModels3D(); check(models.size() >= 5, "Missing native scene fixtures");
    Scene3D scene;
    for (const int index : {2, 3, 4}) scene.models.insert(models[index]->id, models[index]);
    for (int i = 0; i < 24; ++i) {
        ModelInstance3D object; object.id = id(); object.name = "Building"; object.modelId = models[4]->id;
        object.position = {float((i % 6) * 3 - 8), 0, float((i / 6) * 4 - 6)}; object.scale.setY(1 + (i % 3) * .7f); scene.objects.append(object);
    }
    ModelInstance3D ground; ground.id = id(); ground.name = "Ground"; ground.modelId = models[2]->id; ground.scale = {6, 1, 5}; scene.objects.append(ground);
    ModelInstance3D character; character.id = id(); character.name = "Character"; character.modelId = models[3]->id; character.position = {0, 0, 10}; scene.objects.append(character);
    scene.camera = fitCamera3D(scene.camera, sceneBounds(scene), 1280.f / 720); scene.camera.pitch = 25;
    auto shot = Project::create().shots[0]; auto layer = RasterLayer::create("City reference"); layer.kind = LayerKind::Scene3D; layer.scene3d = scene;
    shot.layers = {layer}; const auto firstPose = sceneCameraPose(scene.camera); auto lastPose = firstPose; lastPose.yaw += 35;
    shot.sceneCameraAnimations = {{layer.id, {{0, firstPose}, {24, lastPose}}}};
    const auto validation = validateScene3D(scene); check(validation.isEmpty(), validation);
    Scene3DRenderService renderer; QVector<double> requests, complete; QImage firstImage, lastImage;
    for (int frame = 0; frame <= 24; ++frame) {
        QEventLoop loop; QTimer deadline; deadline.setSingleShot(true); QString failure; QImage image;
        const auto ready = QObject::connect(&renderer, &Scene3DRenderService::frameReady, &loop, [&](quint64, const QImage &result) { image = result; loop.quit(); });
        const auto failed = QObject::connect(&renderer, &Scene3DRenderService::failed, &loop, [&](quint64, const QString &message) { failure = message; loop.quit(); });
        QObject::connect(&deadline, &QTimer::timeout, &loop, [&] { failure = "Scene render timed out"; loop.quit(); });
        scene.camera = evaluateSceneCamera(shot, layer.id, frame);
        QElapsedTimer timer; timer.start(); renderer.request(scene, {1280, 720}); requests.append(timer.nsecsElapsed() / 1000000.);
        deadline.start(30000); loop.exec(); deadline.stop(); QObject::disconnect(ready); QObject::disconnect(failed);
        if (failure.contains("unavailable in this build")) { print({{"profile", "animated_city"}, {"status", "skipped"}, {"reason", failure}}); return; }
        check(failure.isEmpty(), failure); check(image.size() == QSize(1280, 720), "Scene returned incorrect dimensions");
        complete.append(timer.nsecsElapsed() / 1000000.);
        if (frame == 0) firstImage = image; lastImage = image;
    }
    check(firstImage != lastImage, "Animated camera did not change the scene image");
    bool visible = false;
    for (int y = 0; y < lastImage.height() && !visible; y += 32) for (int x = 0; x < lastImage.width(); x += 32)
        if (qAlpha(lastImage.pixel(x, y))) { visible = true; break; }
    check(visible, "Scene rendered no visible geometry");
    const double first = complete.takeFirst(); requests.removeFirst();
    print({{"profile", "animated_city"}, {"operation", "camera_frame_readback"}, {"resolution", "1280x720"}, {"objects", scene.objects.size()},
        {"unique_models", scene.models.size()}, {"cold_frame_ms", first}, {"gui_request", statistics(requests)}, {"timing", statistics(complete)},
        {"changed_image_verified", true}});
}
}
int main(int argc, char **argv) {
    QApplication application(argc, argv);
    const auto arguments = application.arguments(); const QString profile = arguments.size() > 1 ? arguments[1] : "all";
    if (!QStringList{"all", "storage", "audio", "scene"}.contains(profile)) return 2;
    try {
        if (profile == "all" || profile == "storage") storageWorkload();
        if (profile == "all" || profile == "audio") audioWorkload();
        if (profile == "all" || profile == "scene") sceneWorkload();
    } catch (const std::exception &failure) { std::cerr << failure.what() << std::endl; return 1; }
    return 0;
}
