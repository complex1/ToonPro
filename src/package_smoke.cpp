#include "package_smoke.h"
#include "managed_project_store.h"
#include "audio_export.h"
#include "brush_library.h"
#include "canvas.h"
#include "icons.h"
#include "main_window.h"
#include "movie_review_controller.h"
#include "project.h"
#include "scene3d_renderer.h"
#include "timeline_controller.h"
#include "video_export.h"
#include <QApplication>
#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QDeadlineTimer>
#include <QDir>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QMediaPlayer>
#include <QPainter>
#include <QSaveFile>
#include <QScreen>
#include <QSysInfo>
#include <QTimer>
#include <QUuid>
#include <QVideoFrame>
#include <QVideoSink>
#include <QtEndian>
#include <cmath>
#include <functional>

namespace toon {
namespace {
bool waitUntil(QEventLoop &loop, const std::function<bool()> &done, const QDeadlineTimer &deadline) {
    if (done()) return true;
    if (deadline.hasExpired()) return false;
    QTimer timer; timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(int(qMin<qint64>(30000, deadline.remainingTime()))); loop.exec(); return done();
}
Project fixture() {
    auto project = Project::create(); project.movie = "Package verification";
    project.width = 160; project.height = 90; project.fps = 12;
    auto &shot = project.shots[0]; shot.durationFrames = 6;
    QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(QColor(230, 30, 30));
    shot.layers[0].tiles.insert({0, 0}, tile);
    QByteArray pcm(24000 * audioBytesPerSampleFrame, Qt::Uninitialized);
    for (int i = 0; i < 24000; ++i) {
        qToLittleEndian<qint16>(qint16(std::lround(9000 * std::sin(i * 6.283185307179586 * 440 / audioSampleRate))), pcm.data() + i * 4);
        qToLittleEndian<qint16>(qint16(std::lround(6000 * std::sin(i * 6.283185307179586 * 660 / audioSampleRate))), pcm.data() + i * 4 + 2);
    }
    AudioClip clip; clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces); clip.name = "Stereo test";
    clip.asset = makeAudioAsset("Stereo test", pcm); clip.sampleCount = 24000; clip.gain = .75;
    clip.fadeInSamples = clip.fadeOutSamples = 2400; shot.audioClips = {clip};
    shot.scene = "Exterior";
    for (int i = 1; i < 3; ++i) {
        auto next = Project::create().shots[0];
        next.number = QString("C00%1").arg(i + 1); next.name = i == 1 ? "Interior cut" : "Symbol cut";
        next.scene = i == 1 ? "Interior" : "Exterior"; next.durationFrames = 6;
        QImage pixels(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
        pixels.fill(i == 1 ? QColor(30, 220, 30) : QColor(30, 30, 230));
        next.layers[0].tiles.insert({0, 0}, pixels);
        auto sound = clip; sound.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        sound.name = next.name; sound.gain = i == 1 ? .5 : .25; next.audioClips = {sound};
        project.shots.append(next);
    }
    // Exercise embedded Symbol artwork and character palettes when reopening
    // the packaged runtime, in addition to ordinary raster/audio payloads.
    SymbolDefinition symbol{QUuid::createUuid().toString(QUuid::WithoutBraces), "Blue prop", Project::create().shots[0]};
    symbol.content.durationFrames = 6; symbol.content.layers = project.shots[2].layers;
    project.symbols.append(symbol);
    auto instance = RasterLayer::create("Reusable prop"); instance.kind = LayerKind::Symbol;
    instance.symbol = SymbolInstance{symbol.id}; project.shots[2].layers = {instance};
    CharacterModel character; character.id = QUuid::createUuid().toString(QUuid::WithoutBraces); character.name = "Package character";
    character.colors.append({QUuid::createUuid().toString(QUuid::WithoutBraces), "Skin", CharacterColorCategory::Skin, QColor("#edb08a")});
    character.colors.append({QUuid::createUuid().toString(QUuid::WithoutBraces), "Hair", CharacterColorCategory::Hair, QColor("#303049")});
    character.colorVariants = standardCharacterColorVariants(character.colors); project.characters.append(character);
    return project;
}
Scene3D referenceFixture() {
    auto model = std::make_shared<Model3D>(); model->id = QString(64, 'a'); model->name = "Package square";
    ModelMesh3D mesh; mesh.name = "Surface";
    mesh.positions = {{-.7f,-.7f,0},{.7f,-.7f,0},{.7f,.7f,0},{-.7f,.7f,0}};
    mesh.normals = QVector<QVector3D>(4, {0,0,1}); mesh.texCoords = {{0,1},{1,1},{1,0},{0,0}};
    mesh.indices = {0,1,2,0,2,3}; mesh.baseColor = Qt::red; model->meshes.append(mesh);
    for (const auto &point : mesh.positions) model->bounds.include(point);
    Scene3D scene; scene.camera.target = {}; scene.camera.yaw = scene.camera.pitch = 0;
    scene.camera.distance = 3; scene.camera.orthographic = true; scene.camera.orthoScale = 2;
    scene.models.insert(model->id, model); ModelInstance3D object;
    object.id = "square"; object.name = "Square"; object.modelId = model->id; scene.objects.append(object); return scene;
}
}
int runPackageSmoke(const QString &outputDirectory, bool require3D) {
    const QFileInfo outputInfo(outputDirectory); QDir output(outputInfo.absoluteFilePath());
    if (outputDirectory.trimmed().isEmpty() || outputInfo.isSymLink()
        || (output.exists() && !output.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty()) || !output.mkpath(".")) {
        qCritical("Package smoke requires a new or empty writable output folder."); return 2;
    }
    QApplication::setQuitOnLastWindowClosed(false);
    QDeadlineTimer deadline(45000); QElapsedTimer elapsed; elapsed.start(); QJsonArray checks; bool success = true;
    auto check = [&](const QString &name, bool passed, const QString &detail = QString{}) {
        checks.append(QJsonObject{{"name", name}, {"passed", passed}, {"detail", detail}}); success &= passed; return passed;
    };
    QJsonObject report{{"schemaVersion", 2}, {"applicationVersion", QApplication::applicationVersion()}, {"qtVersion", QString::fromLatin1(qVersion())},
        {"platform", QGuiApplication::platformName()}, {"architecture", QSysInfo::currentCpuArchitecture()},
        {"libraryPaths", QJsonArray::fromStringList(QCoreApplication::libraryPaths())},
        {"qtPrefix", QLibraryInfo::path(QLibraryInfo::PrefixPath)}, {"require3D", require3D},
        {"osProduct", QSysInfo::prettyProductName()}, {"kernelVersion", QSysInfo::kernelVersion()},
        {"profileIsolation", "explicit output-directory profile"},
        {"capabilities", QJsonObject{{"rasterDrawing", true}, {"vectorDrawing", true},
            {"shotSceneMovieReview", true}, {"embeddedAudio", true}, {"symbolLibrary", true},
            {"requiresPhysicalTabletAcceptance", true}}}};
    QString managedError;
    auto managed=ManagedProjectStore::create(output.filePath("managed.toonstudio"),"Package storage","drawing",&managedError);
    bool managedOk=bool(managed);
    if(managed){auto doc=newManagedDocument("Drawing",false,160,90,Qt::white,false);auto id=managed->createDocument("Drawing","drawing",doc,{}, {},&managedError);managedOk=!id.isEmpty()&&managed->loadDocument(id,false,&managedError).has_value();}
    check("managed-sqlite-storage",managedOk,managedError);
    managed.reset();
    QString error; const auto project = fixture();
    const auto projectPath = output.filePath("smoke.toonproj");
    if (check("project-save", saveProject(project, projectPath, &error), error)) {
        const auto reopened = loadProject(projectPath, &error);
        bool pixelsMatch = reopened && reopened->shots.size() == project.shots.size() && reopened->toJson() == project.toJson();
        if (pixelsMatch) for (int i = 0; i < project.shots.size(); ++i)
            pixelsMatch &= renderMovieFrame(*reopened, 0, i) == renderMovieFrame(project, 0, i);
        check("project-reopen-artwork-symbols-colors", pixelsMatch, error);
        MainWindow window(output.filePath("profile/workspace.ini"));
        window.setAttribute(Qt::WA_DontShowOnScreen); window.resize(1100, 760);
        check("workspace-open", window.openProject(projectPath)); window.show();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 30);
        const auto screenshot = window.grab();
        report["devicePixelRatio"] = screenshot.devicePixelRatio();
        check("workspace-screenshot", !screenshot.isNull() && screenshot.save(output.filePath("workspace.png")));
        check("embedded-icons", !appIcon("brush").isNull() && !appIcon("music").isNull());
        auto *brushes = window.findChild<BrushLibrary *>();
        report["builtInBrushCount"] = brushes ? brushes->presets().size() : 0;
        check("default-brush-library", brushes && brushes->presets().size() > 3
            && brushes->path() == output.filePath("profile/brushes.json"));
        if (window.canvas()->hasProject()) {
            auto *document = window.canvas()->document();
            const auto original = document->activeShot().layers[document->activeLayer()].tiles;
            const int historyBefore = document->history()->count();
            BrushSettings brush; brush.size = 12; brush.color = Qt::white; brush.pressureSize = false;
            bool painted = document->beginStroke({24, 30}, 1, brush, false);
            if (painted) { document->continueStroke({120, 55}, 1); document->endStroke(); }
            painted &= document->activeShot().layers[document->activeLayer()].tiles != original
                && document->history()->count() == historyBefore + 1;
            check("raster-stroke-commit", painted);
            if (document->history()->canUndo()) document->history()->undo();
            check("raster-stroke-undo", document->activeShot().layers[document->activeLayer()].tiles == original
                && document->history()->isClean());

            auto *timeline = window.findChild<TimelineController *>("timelineController");
            if (check("movie-review-controller", timeline != nullptr)) {
                auto *review = timeline->movieReview(); timeline->setFrame(2);
                const auto editingShot = document->activeShotId(); const int editingLayer = document->activeLayer();
                const int historyCount = document->history()->count(), historyIndex = document->history()->index();
                const auto source = document->project().toJson();
                const auto preserved = [&] {
                    return document->activeShotId() == editingShot && document->currentFrame() == 2
                        && document->activeLayer() == editingLayer && document->history()->count() == historyCount
                        && document->history()->index() == historyIndex && document->project().toJson() == source;
                };
                check("scene-review-order", review->setMode(ReviewMode::Scene) && review->frameCount() == 12
                    && review->segments().size() == 2 && review->segments()[1].shotIndex == 2 && preserved());
                check("movie-review-global-seek", review->setMode(ReviewMode::Movie) && review->frameCount() == 18
                    && review->setFrame(8) && review->location() == MoviePlaybackLocation{1, 2} && preserved());
                QEventLoop previewLoop; QImage preview;
                const auto previewConnection = QObject::connect(review, &MovieReviewController::previewChanged, &previewLoop, [&](const QImage &image) {
                    if (image.isNull()) return;
                    const auto center = image.pixelColor(image.width() / 2, image.height() / 2);
                    if (center.green() > 180 && center.red() < 80 && center.blue() < 80) { preview = image; previewLoop.quit(); }
                });
                // Seeking queued work publishes only after the event loop runs.
                check("movie-review-render", waitUntil(previewLoop, [&] { return !preview.isNull(); }, deadline));
                QObject::disconnect(previewConnection);
                if (!preview.isNull()) preview.save(output.filePath("movie-review.png"));
                QEventLoop thumbnailLoop; bool blueThumbnail = false;
                const auto thumbConnection = QObject::connect(review, &MovieReviewController::thumbnailReady, &thumbnailLoop, [&](int index, const QImage &image) {
                    if (index != 2 || image.isNull()) return;
                    const auto center = image.pixelColor(image.width() / 2, image.height() / 2);
                    blueThumbnail = center.blue() > 180 && center.red() < 80 && center.green() < 80;
                    thumbnailLoop.quit();
                });
                review->requestThumbnail(2);
                check("movie-symbol-thumbnail", waitUntil(thumbnailLoop, [&] { return blueThumbnail; }, deadline));
                QObject::disconnect(thumbConnection);
                QEventLoop playbackLoop; bool crossedCut = false;
                const auto playbackConnection = QObject::connect(review, &MovieReviewController::changed, &playbackLoop, [&] {
                    crossedCut |= review->location().shotIndex > 0;
                    if (!review->playing() && review->currentFrame() == 17) playbackLoop.quit();
                });
                review->setLooping(false); review->setFrame(5); crossedCut = false; review->setPlaying(true);
                const bool finished = waitUntil(playbackLoop, [&] { return !review->playing() && review->currentFrame() == 17; }, deadline);
                check("movie-continuous-playback", finished && crossedCut && preserved());
                QObject::disconnect(playbackConnection); review->setPlaying(false);
                check("movie-return-to-drawing", review->setMode(ReviewMode::Shot) && preserved());
            }
        }
        window.close();
    }
    {
        AudioExport exporter; QEventLoop loop; bool done = false; AudioExportResult result;
        QObject::connect(&exporter, &AudioExport::finished, &loop, [&](const AudioExportResult &value) { result = value; done = true; loop.quit(); });
        exporter.start(project, audioExportDefaults(project), output.filePath("smoke.wav"));
        if (!waitUntil(loop, [&] { return done; }, deadline)) { exporter.cancel(); check("wav-export", false, "Timed out."); }
        else {
            QFile wav(output.filePath("smoke.wav")); const bool opened = wav.open(QIODevice::ReadOnly); const auto header = opened ? wav.read(44) : QByteArray{};
            check("wav-export", result.outcome == AudioExportOutcome::Completed && result.sampleFrames == 24000 && header.startsWith("RIFF") && wav.size() == 96044, result.error);
        }
    }
    const auto videoPath = output.filePath("smoke.mp4"); bool videoReady = false;
    {
        VideoExport exporter; QEventLoop loop; bool done = false; VideoExportResult result;
        QObject::connect(&exporter, &VideoExport::finished, &loop, [&](const VideoExportResult &value) { result = value; done = true; loop.quit(); });
        auto settings = videoExportDefaults(project); settings.includeAudio = true; settings.overwriteApproved = false;
        exporter.start(project, settings, videoPath);
        if (!waitUntil(loop, [&] { return done; }, deadline)) { exporter.cancel(); check("h264-aac-export", false, "Timed out."); }
        else videoReady = check("h264-aac-export", result.outcome == VideoExportOutcome::Completed && result.framesEncoded == 6, result.error);
    }
    if (videoReady) {
        QMediaPlayer player; QVideoSink sink; QEventLoop loop; QImage decoded; QString decodeError;
        player.setVideoSink(&sink);
        QObject::connect(&sink, &QVideoSink::videoFrameChanged, &loop, [&](const QVideoFrame &frame) {
            if (frame.isValid() && decoded.isNull()) { decoded = frame.toImage(); if (!decoded.isNull()) loop.quit(); }
        });
        QObject::connect(&player, &QMediaPlayer::errorOccurred, &loop, [&](QMediaPlayer::Error, const QString &message) { decodeError = message; loop.quit(); });
        player.setSource(QUrl::fromLocalFile(videoPath)); player.play();
        const bool ready = waitUntil(loop, [&] { return !decoded.isNull() || !decodeError.isEmpty(); }, deadline); player.stop();
        const auto pixel = decoded.isNull() ? QColor{} : decoded.pixelColor(decoded.width() / 2, decoded.height() / 2);
        check("h264-decode-pixels", ready && decodeError.isEmpty() && decoded.size() == QSize(160, 90)
            && pixel.red() > 200 && pixel.green() < 65 && pixel.blue() < 65, decodeError);
        if (!decoded.isNull()) decoded.save(output.filePath("decoded-video.png"));
    }
    if (videoReady) {
        QAudioDecoder decoder; QAudioFormat format; format.setSampleRate(audioSampleRate);
        format.setChannelConfig(QAudioFormat::ChannelConfigStereo); format.setSampleFormat(QAudioFormat::Int16); decoder.setAudioFormat(format);
        QEventLoop loop; bool done = false; QString decodeError; qint64 frames = 0; long double energy = 0;
        QObject::connect(&decoder, &QAudioDecoder::bufferReady, &loop, [&] {
            const auto buffer = decoder.read();
            if (buffer.format() != format || frames + buffer.frameCount() > audioSampleRate * 2) { decodeError = "Unexpected decoded audio format or duration."; loop.quit(); return; }
            frames += buffer.frameCount(); const auto *samples = buffer.constData<qint16>();
            for (int i = 0; i < buffer.sampleCount(); ++i) energy += double(samples[i]) * samples[i];
        });
        QObject::connect(&decoder, &QAudioDecoder::finished, &loop, [&] { done = true; loop.quit(); });
        QObject::connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), &loop,
            [&](QAudioDecoder::Error) { decodeError = decoder.errorString(); loop.quit(); });
        decoder.setSource(QUrl::fromLocalFile(videoPath)); decoder.start();
        const bool ready = waitUntil(loop, [&] { return done || !decodeError.isEmpty(); }, deadline); decoder.stop();
        const auto rms = frames ? std::sqrt(double(energy / (frames * audioChannels))) : 0;
        report["decodedAudioFrames"] = double(frames); report["decodedAudioRms"] = rms;
        check("aac-decode-samples", ready && done && decodeError.isEmpty() && qAbs(frames - 24000) < 4000 && rms > 1000, decodeError);
    }
    {
        Scene3DRenderService renderer; QEventLoop loop; QImage image; QString renderError;
        QObject::connect(&renderer, &Scene3DRenderService::frameReady, &loop, [&](quint64, const QImage &value) { image = value; loop.quit(); });
        QObject::connect(&renderer, &Scene3DRenderService::failed, &loop, [&](quint64, const QString &message) { renderError = message; loop.quit(); });
        renderer.request(referenceFixture(), {160, 120});
        const bool ready = waitUntil(loop, [&] { return !image.isNull() || !renderError.isEmpty(); }, deadline);
        const bool unavailable = renderError.contains("unavailable in this build");
        const auto pixel = image.isNull() ? QColor{} : image.pixelColor(80, 60);
        const bool rendered = ready && !image.isNull() && pixel.alpha() > 240 && pixel.red() > pixel.blue() * 2 && image.pixelColor(0, 0).alpha() == 0;
        report["renderer3D"] = rendered ? "rendered" : unavailable ? "unavailable" : "failed";
        auto capabilities = report["capabilities"].toObject();
        capabilities["reference3DRendered"] = rendered; capabilities["reference3DUnavailable"] = unavailable;
        report["capabilities"] = capabilities;
        check("3d-reference-render", rendered || (!require3D && unavailable), renderError);
        if (!image.isNull()) image.save(output.filePath("reference3d.png"));
    }
    report["checks"] = checks; report["success"] = success; report["elapsedMs"] = double(elapsed.elapsed());
    QSaveFile file(output.filePath("smoke.json")); const auto data = QJsonDocument(report).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) return 2;
    return success ? 0 : 1;
}
} // namespace toon
