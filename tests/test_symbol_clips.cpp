#include "symbol_clips.h"
#include "symbols.h"
#include "document.h"
#include "sheet.h"
#include "animation_render.h"
#include "timeline_ripple.h"
#include "animation_test_fixture.h"
#include <QFile>
#include <QJsonArray>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

using namespace toon;
namespace {
Project fixture() {
    auto project = Project::create(); project.width = project.height = 128; project.transparent = true;
    SymbolDefinition symbol{animation_fixture::id(), "Blink", Project::create().shots[0]};
    symbol.content.durationFrames = 4;
    QImage pixels(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); pixels.fill(Qt::transparent);
    QPainter painter(&pixels); painter.fillRect(QRect(8,8,12,12), Qt::red); painter.end();
    symbol.content.layers[0].tiles[{0,0}] = pixels;
    project.symbols.append(symbol);
    auto layer = RasterLayer::create("Blink instance"); layer.kind = LayerKind::Symbol;
    layer.symbol = SymbolInstance{symbol.id, SymbolPlayback::Loop, 10, 1, .5, 20, -3};
    project.shots[0].layers = {layer}; project.shots[0].durationFrames = 60;
    return project;
}
QJsonObject instanceData(const QJsonObject &manifest) {
    return manifest["shots"].toArray()[0].toObject()["layers"].toArray()[0].toObject()["symbol"].toObject();
}
QJsonObject withInstance(QJsonObject manifest, const QJsonObject &data) {
    auto shots = manifest["shots"].toArray(); auto shot = shots[0].toObject();
    auto layers = shot["layers"].toArray(); auto layer = layers[0].toObject();
    layer["symbol"] = data; layers[0] = layer; shot["layers"] = layers; shots[0] = shot; manifest["shots"] = shots;
    return manifest;
}
bool deleted(int frame, const TimelineRippleEdit &edit) {
    return edit.kind == TimelineRippleKind::Delete && frame >= edit.startFrame && frame < edit.startFrame + edit.durationFrames;
}
}

class SymbolClipsTests final : public QObject {
    Q_OBJECT
private slots:
    void finiteWindowsAndSignedPhaseAcrossPlaybackModes() {
        SymbolInstance clip{"unused", SymbolPlayback::Loop, 3, 1, .5, 6, -3};
        QCOMPARE(symbolLocalFrame(clip, 2, 4), -1);
        QCOMPARE(symbolLocalFrame(clip, 3, 4), 3); // floor(-1.5) + 1, positively wrapped
        QCOMPARE(symbolLocalFrame(clip, 4, 4), 0);
        QCOMPARE(symbolLocalFrame(clip, 8, 4), 2);
        QCOMPARE(symbolLocalFrame(clip, 9, 4), -1);
        clip.playback = SymbolPlayback::Once;
        QCOMPARE(symbolLocalFrame(clip, 3, 4), 0);
        clip.playbackOffset = 100; QCOMPARE(symbolLocalFrame(clip, 8, 4), 3);
        clip.playback = SymbolPlayback::PingPong; clip.speed = 1; clip.firstFrame = 0; clip.playbackOffset = -3;
        const QVector<int> frames{3, 2, 1, 0, 1, 2};
        for (int i = 0; i < frames.size(); ++i) QCOMPARE(symbolLocalFrame(clip, 3 + i, 4), frames[i]);
        clip.playback = SymbolPlayback::SingleFrame; clip.firstFrame = 2;
        QCOMPARE(symbolLocalFrame(clip, 3, 4), 2); QCOMPARE(symbolLocalFrame(clip, 9, 4), -1);
        clip.durationFrames = 0; QCOMPARE(symbolLocalFrame(clip, 3, 4), -1);
        clip.durationFrames = -1; QCOMPARE(symbolLocalFrame(clip, 59, 4), 2);
        clip.playbackOffset = -1000001; QCOMPARE(symbolLocalFrame(clip, 3, 4), -1);
        clip.playbackOffset = 0; clip.durationFrames = -2; QCOMPARE(symbolLocalFrame(clip, 3, 4), -1);
    }

    void pureMoveTrimAndSlipPreservePhaseAtFractionalSpeeds() {
        for (const auto mode : {SymbolPlayback::Loop, SymbolPlayback::Once, SymbolPlayback::SingleFrame, SymbolPlayback::PingPong})
            for (qreal speed : {.1, .5, 1.5, 2.75}) {
                SymbolInstance original{"unused", mode, 10, 2, speed, 20, -2}, result;
                QString error;
                QVERIFY2(previewSymbolClipEdit(original, 60, SymbolClipOperation::TrimStart, 13, &result, &error), qPrintable(error));
                QCOMPARE(result.durationFrames, 17); QCOMPARE(result.playbackOffset, 1);
                for (int frame = 13; frame < 30; ++frame)
                    QCOMPARE(symbolLocalFrame(result, frame, 7), symbolLocalFrame(original, frame, 7));
                QVERIFY(previewSymbolClipEdit(original, 60, SymbolClipOperation::TrimStart, 5, &result));
                QCOMPARE(result.playbackOffset, -7); QCOMPARE(result.durationFrames, 25);
                for (int frame = 10; frame < 30; ++frame)
                    QCOMPARE(symbolLocalFrame(result, frame, 7), symbolLocalFrame(original, frame, 7));
                QVERIFY(previewSymbolClipEdit(original, 60, SymbolClipOperation::Move, -8, &result));
                QCOMPARE(result.startFrame, 2); QCOMPARE(result.durationFrames, 20); QCOMPARE(result.playbackOffset, -2);
                for (int frame = 10; frame < 30; ++frame)
                    QCOMPARE(symbolLocalFrame(result, frame - 8, 7), symbolLocalFrame(original, frame, 7));
                QVERIFY(previewSymbolClipEdit(original, 60, SymbolClipOperation::Slip, 3, &result));
                QCOMPARE(result.startFrame, 10); QCOMPARE(result.durationFrames, 20); QCOMPARE(result.playbackOffset, 1);
                for (int frame = 10; frame < 27; ++frame)
                    QCOMPARE(symbolLocalFrame(result, frame, 7), symbolLocalFrame(original, frame + 3, 7));
                QVERIFY(previewSymbolClipEdit(original, 60, SymbolClipOperation::TrimEnd, 22, &result));
                QCOMPARE(result.durationFrames, 12); QCOMPARE(result.playbackOffset, original.playbackOffset);
            }
    }

    void pureBoundsAndEmptyClipRestoration() {
        SymbolInstance original{"unused", SymbolPlayback::Loop, 10, 0, 1}, result;
        QVERIFY(previewSymbolClipEdit(original, 60, SymbolClipOperation::Move, 0, &result));
        QCOMPARE(result, original);
        QVERIFY(previewSymbolClipEdit(original, 60, SymbolClipOperation::TrimStart, 10, &result));
        QCOMPARE(result, original);
        QVERIFY(previewSymbolClipEdit(original, 60, SymbolClipOperation::TrimEnd, 60, &result));
        QCOMPARE(result, original);
        QVERIFY(previewSymbolClipEdit(original, 60, SymbolClipOperation::Move, -5, &result));
        QCOMPARE(result.startFrame, 5); QCOMPARE(result.durationFrames, 50); QCOMPARE(symbolClipEnd(result, 60), 55);
        const auto sentinel = result; QString error;
        for (const auto edit : {QPair{SymbolClipOperation::Move, 1}, QPair{SymbolClipOperation::Move, -11},
                                QPair{SymbolClipOperation::TrimEnd, 61}, QPair{SymbolClipOperation::TrimStart, -1},
                                QPair{SymbolClipOperation::TrimStart, 61}, QPair{SymbolClipOperation::Slip, 1000001},
                                QPair{SymbolClipOperation::Move, std::numeric_limits<int>::max()}}) {
            QVERIFY(!previewSymbolClipEdit(original, 60, edit.first, edit.second, &result, &error));
            QVERIFY(!error.isEmpty()); QCOMPARE(result, sentinel);
        }
        QVERIFY(previewSymbolClipEdit(original, 60, SymbolClipOperation::TrimEnd, 10, &result));
        QCOMPARE(result.durationFrames, 0); QCOMPARE(symbolClipEnd(result, 60), 10);
        const auto empty = result;
        QVERIFY(previewSymbolClipEdit(empty, 60, SymbolClipOperation::TrimEnd, 16, &result));
        QCOMPARE(result.durationFrames, 6); QCOMPARE(symbolLocalFrame(result, 10, 4), 0);
        original.startFrame = 100; QCOMPARE(symbolClipEnd(original, 60), 60);
        QVERIFY(!previewSymbolClipEdit(original, 60, SymbolClipOperation::Move, -1, &result));
    }

    void clipWindowControlsCanvasExportAndNestedPlayback() {
        auto project = fixture(); auto &clip = *project.shots[0].layers[0].symbol;
        clip.startFrame = 2; clip.durationFrames = 3; clip.playbackOffset = 0;
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        QCOMPARE(renderMovieFrame(project, 1).pixelColor(10,10).alpha(), 0);
        QCOMPARE(renderMovieFrame(project, 2).pixelColor(10,10), QColor(Qt::red));
        QCOMPARE(renderMovieFrame(project, 4).pixelColor(10,10), QColor(Qt::red));
        QCOMPARE(renderMovieFrame(project, 5).pixelColor(10,10).alpha(), 0);
        QTemporaryDir directory; QString error;
        QVERIFY2(exportPngSequence(project, 0, 1, 5, directory.path(), {}, &error), qPrintable(error));
        for (int frame = 1; frame <= 5; ++frame) {
            const QImage exported(directory.filePath(QString("frame-%1.png").arg(frame + 1, 4, 10, QChar('0'))));
            QVERIFY(!exported.isNull());
            const auto rendered = renderMovieFrame(project, frame);
            QCOMPARE(exported.convertToFormat(rendered.format()), rendered);
        }
        SymbolDefinition outer{animation_fixture::id(), "Face", Project::create().shots[0]};
        outer.content.durationFrames = 12; outer.content.layers = project.shots[0].layers;
        outer.content.layers[0].id = animation_fixture::id();
        project.symbols.append(outer);
        project.shots[0].layers[0].symbol = SymbolInstance{outer.id, SymbolPlayback::Loop, 4, 0, 1, 6, 0};
        QCOMPARE(renderMovieFrame(project, 4).pixelColor(10,10).alpha(), 0);
        QCOMPARE(renderMovieFrame(project, 6).pixelColor(10,10), QColor(Qt::red));
        QCOMPARE(renderMovieFrame(project, 9).pixelColor(10,10).alpha(), 0);
        QCOMPARE(renderMovieFrame(project, 10).pixelColor(10,10).alpha(), 0);
        Document document; document.load(project); QVERIFY(document.setCurrentFrame(6));
        QVERIFY2(breakApartSymbolInstance(&document, document.activeShot().layers[0].id, &error), qPrintable(error));
        bool found = false;
        for (const auto &layer : document.activeShot().layers) if (layer.symbol) {
            found = true; QCOMPARE(layer.symbol->durationFrames, -1); QCOMPARE(layer.symbol->playbackOffset, 0);
        }
        QVERIFY(found); QCOMPARE(renderMovieFrame(document.project(), 59).pixelColor(10,10), QColor(Qt::red));
    }

    void intervalsRespectRepeatedDrawingExposureAndGroups() {
        auto project = fixture(); auto symbol = project.shots[0].layers[0];
        auto shot = animation_fixture::project().shots[0];
        auto &track = shot.animationTracks[0];
        track.exposures.append({animation_fixture::id(), track.drawings[0].id, 12, 3});
        symbol.parentId = track.drawings[0].groupLayerId;
        symbol.symbol->startFrame = 1; symbol.symbol->durationFrames = 13;
        shot.layers.insert(4, symbol);
        QCOMPARE(symbolClipIntervals(shot, symbol.id), (QVector<QPair<int,int>>{{1,3},{12,14}}));
        shot.layers[4].symbol->durationFrames = 0; QVERIFY(symbolClipIntervals(shot, symbol.id).isEmpty());
        auto folder = RasterLayer::create("Folder"); folder.group = true;
        symbol.parentId = folder.id; shot.layers = {folder, symbol}; shot.animationTracks.clear();
        QCOMPARE(symbolClipIntervals(shot, symbol.id), (QVector<QPair<int,int>>{{1,14}}));
        QVERIFY(symbolClipIntervals(shot, "missing").isEmpty());
    }

    void documentTimingEditsAreAtomicUndoableAndGuarded() {
        auto project = fixture(); Document document; document.load(project); QString error;
        const auto id = document.activeShot().layers[0].id;
        QVERIFY2(editSymbolClip(&document, id, SymbolClipOperation::TrimStart, 13, &error), qPrintable(error));
        QCOMPARE(document.history()->count(), 1); QCOMPARE(document.activeShot().layers[0].symbol->playbackOffset, 0);
        document.history()->undo(); QCOMPARE(document.project().toJson(), project.toJson());
        document.history()->redo(); QCOMPARE(document.activeShot().layers[0].symbol->startFrame, 13);
        const auto before = document.project().toJson(); const int count = document.history()->count();
        QVERIFY(!editSymbolClip(&document, id, SymbolClipOperation::Move, 60, &error));
        QCOMPARE(document.project().toJson(), before); QCOMPARE(document.history()->count(), count);
        QVERIFY(document.beginLayerPropertyEdit());
        QVERIFY(!setSymbolClipTiming(&document, id, 10, 5, 0, &error)); QVERIFY(error.contains("current edit"));
        document.cancelLayerPropertyEdit();
        QVERIFY(document.beginKeyframeEdit(id));
        QVERIFY(!editSymbolClip(&document, id, SymbolClipOperation::Slip, 1, &error));
        document.cancelKeyframeEdit();
        QVERIFY(setSymbolClipTiming(&document, id, 10, 0, -3, &error));
        QCOMPARE(document.activeShot().layers[0].symbol->durationFrames, 0);
        QVERIFY(setSymbolClipTiming(&document, id, 10, 8, -3, &error));
        QCOMPARE(document.activeShot().layers[0].symbol->durationFrames, 8);
        for (const bool ancestor : {false, true}) for (const bool position : {false, true}) {
            auto locked = project;
            if (ancestor) {
                auto folder = RasterLayer::create("Folder"); folder.group = true;
                locked.shots[0].layers[0].parentId = folder.id; locked.shots[0].layers.prepend(folder);
            }
            auto &target = locked.shots[0].layers[0];
            if (position) target.positionLocked = true; else target.locked = true;
            document.load(locked);
            QVERIFY(!symbolClipEditError(document.activeShot(), id).isEmpty());
            QVERIFY(!editSymbolClip(&document, id, SymbolClipOperation::Move, 1, &error));
            QCOMPARE(document.project().toJson(), locked.toJson()); QCOMPARE(document.history()->count(), 0);
        }
    }

    void format32RoundTripAndLegacy31StayExact() {
        auto project = fixture(); QTemporaryDir directory; QString error;
        auto manifest = project.toJson(); QCOMPARE(manifest["version"].toInt(), 32); QCOMPARE(instanceData(manifest).size(), 7);
        const auto path = directory.filePath("clips.toon");
        QVERIFY2(saveProject(project, path, &error), qPrintable(error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.read(10), QByteArray("TOONPRO32\n")); file.close();
        auto reopened = loadProject(path, &error); QVERIFY2(reopened, qPrintable(error));
        QCOMPARE(reopened->toJson(), manifest); QCOMPARE(reopened->shots[0].layers[0].symbol, project.shots[0].layers[0].symbol);
        QCOMPARE(renderMovieFrame(*reopened, 12), renderMovieFrame(project, 12));
        // A forged old container cannot disguise new clip metadata.
        QVERIFY(file.open(QIODevice::ReadWrite)); QCOMPARE(file.write("TOONPRO31\n", 10), qint64(10)); file.close();
        QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("container"));
        project.shots[0].layers[0].symbol->durationFrames = -1; project.shots[0].layers[0].symbol->playbackOffset = 0;
        const auto legacy = project.toJson(); QCOMPARE(legacy["version"].toInt(), 31); QCOMPARE(instanceData(legacy).size(), 5);
        QVERIFY(!instanceData(legacy).contains("durationFrames")); QVERIFY(!instanceData(legacy).contains("playbackOffset"));
        // The manifest parser restores playback metadata; binary artwork is
        // hydrated separately by loadProject, which is the full round trip.
        const auto loaded = Project::fromJson(legacy, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(instanceData(loaded->toJson()), instanceData(legacy));
        QCOMPARE(loaded->shots[0].layers[0].symbol, project.shots[0].layers[0].symbol);
        QVERIFY(saveProject(project, path, &error)); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.read(10), QByteArray("TOONPRO31\n")); file.close();
        reopened = loadProject(path, &error); QVERIFY2(reopened, qPrintable(error));
        QCOMPARE(reopened->toJson(), legacy);
        QCOMPARE(renderMovieFrame(*reopened, 12), renderMovieFrame(project, 12));
        // Nested clip metadata alone is enough to select format 32.
        auto child = project.symbols[0]; child.id = animation_fixture::id(); child.content = Project::create().shots[0];
        project.symbols.append(child);
        auto nested = project.shots[0].layers[0]; nested.id = animation_fixture::id(); nested.symbol->symbolId = child.id;
        nested.symbol->durationFrames = 0; nested.symbol->playbackOffset = -7;
        project.symbols[0].content.layers = {nested};
        QCOMPARE(project.toJson()["version"].toInt(), 32);
        QVERIFY2(saveProject(project, path, &error), qPrintable(error)); reopened = loadProject(path, &error);
        QVERIFY2(reopened, qPrintable(error)); QCOMPARE(reopened->toJson(), project.toJson());
    }

    void corruptClipMetadataIsRejected() {
        const auto manifest = fixture().toJson(); const auto data = instanceData(manifest); QString error;
        for (const auto field : {QString("durationFrames"), QString("playbackOffset")}) {
            for (const auto value : {QJsonValue("3"), QJsonValue(true), QJsonValue(QJsonValue::Null),
                                     QJsonValue(1.25), QJsonValue(1000001), QJsonValue(-1000001)}) {
                auto changed = data; changed[field] = value;
                QVERIFY(!Project::fromJson(withInstance(manifest, changed), &error)); QVERIFY(!error.isEmpty());
            }
            auto changed = data; changed.remove(field);
            QVERIFY(!Project::fromJson(withInstance(manifest, changed), &error));
        }
        auto changed = data; changed["durationFrames"] = -2;
        QVERIFY(!Project::fromJson(withInstance(manifest, changed), &error));
        changed = data; changed["unknown"] = 0; QVERIFY(!Project::fromJson(withInstance(manifest, changed), &error));
        auto forged = manifest; forged["version"] = 31;
        QVERIFY(!Project::fromJson(forged, &error));
        changed = data; changed["durationFrames"] = -1; changed["playbackOffset"] = -1000000;
        QVERIFY2(Project::fromJson(withInstance(manifest, changed), &error), qPrintable(error));
    }

    void finiteRipplePreservesEverySurvivingFrame() {
        const auto original = fixture();
        for (const auto mode : {SymbolPlayback::Loop, SymbolPlayback::Once, SymbolPlayback::PingPong, SymbolPlayback::SingleFrame})
            for (const auto edit : {TimelineRippleEdit{TimelineRippleKind::Insert, 5, 4},
                                   TimelineRippleEdit{TimelineRippleKind::Insert, 30, 4},
                                   TimelineRippleEdit{TimelineRippleKind::Delete, 4, 4},
                                   TimelineRippleEdit{TimelineRippleKind::Delete, 5, 10},
                                   TimelineRippleEdit{TimelineRippleKind::Delete, 25, 10},
                                   TimelineRippleEdit{TimelineRippleKind::Move, 10, 20, 50},
                                   TimelineRippleEdit{TimelineRippleKind::Delete, 10, 20}}) {
                auto before = original; before.shots[0].layers[0].symbol->playback = mode;
                auto after = before; auto &shot = after.shots[0];
                const auto error = applyTimelineRipple(shot, after.fps, edit);
                QVERIFY2(error.isEmpty(), qPrintable(error)); QVERIFY2(after.validate().isEmpty(), qPrintable(after.validate()));
                for (int frame = 0; frame < 60; ++frame) if (!deleted(frame, edit))
                    QCOMPARE(symbolLocalFrame(*shot.layers[0].symbol, timelineRippleFrame(frame, 60, edit), 4),
                             symbolLocalFrame(*before.shots[0].layers[0].symbol, frame, 4));
                QCOMPARE(after.toJson()["symbols"], before.toJson()["symbols"]);
                if (edit.kind == TimelineRippleKind::Delete && edit.startFrame == 10) {
                    QCOMPARE(shot.layers.size(), 1); QCOMPARE(shot.layers[0].symbol->durationFrames, 0);
                    QCOMPARE(shot.layers[0].symbol->playbackOffset, -3);
                }
            }
    }

    void unsafeRippleSplitsAndLocksRejectWithoutPartialEdits() {
        const auto original = fixture();
        for (const auto edit : {TimelineRippleEdit{TimelineRippleKind::Insert, 18, 3},
                               TimelineRippleEdit{TimelineRippleKind::Delete, 18, 3},
                               TimelineRippleEdit{TimelineRippleKind::Move, 14, 3, 44}}) {
            auto after = original;
            const auto error = applyTimelineRipple(after.shots[0], after.fps, edit);
            QVERIFY2(error.contains("Symbol"), qPrintable(error)); QCOMPARE(after.toJson(), original.toJson());
        }
        auto locked = original; locked.shots[0].layers[0].positionLocked = true;
        auto after = locked;
        QVERIFY(!applyTimelineRipple(after.shots[0], after.fps, {TimelineRippleKind::Delete, 25, 10}).isEmpty());
        QCOMPARE(after.toJson(), locked.toJson());
        auto still = original; still.shots[0].layers[0].symbol->playback = SymbolPlayback::SingleFrame;
        after = still;
        QVERIFY(applyTimelineRipple(after.shots[0], after.fps, {TimelineRippleKind::Delete, 18, 3}).isEmpty());
        QCOMPARE(after.shots[0].layers[0].symbol->durationFrames, 17);
    }

    void rippleRetainsUnusedDrawingSourceTiming() {
        auto project = fixture(); auto symbol = project.shots[0].layers[0];
        project.shots[0] = animation_fixture::project().shots[0]; auto &shot = project.shots[0];
        symbol.parentId = shot.animationTracks[0].drawings[0].groupLayerId;
        symbol.symbol->startFrame = 0; symbol.symbol->durationFrames = 3;
        shot.layers.insert(4, symbol);
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        QVERIFY(applyTimelineRipple(shot, project.fps, {TimelineRippleKind::Delete, 0, 3}).isEmpty());
        const int index = animation_fixture::layerIndex(shot, symbol.id); QVERIFY(index >= 0);
        QCOMPARE(shot.layers[index].symbol, symbol.symbol);
        QVERIFY(symbolClipIntervals(shot, symbol.id).isEmpty()); QCOMPARE(shot.animationTracks[0].drawings.size(), 2);
    }

    void ripplePreservesDormantFutureWindowsAndBounds() {
        auto project = fixture(); auto &shot = project.shots[0];
        shot.layers[0].symbol->startFrame = 40; shot.layers[0].symbol->durationFrames = 40;
        const auto original = *shot.layers[0].symbol;
        QVERIFY(applyTimelineRipple(shot, project.fps, {TimelineRippleKind::Insert, 10, 5}).isEmpty());
        QCOMPARE(shot.layers[0].symbol->startFrame, 45); QCOMPARE(shot.layers[0].symbol->durationFrames, 40);
        QCOMPARE(shot.layers[0].symbol->playbackOffset, original.playbackOffset);
        for (int frame = 40; frame < 60; ++frame)
            QCOMPARE(symbolLocalFrame(*shot.layers[0].symbol, frame + 5, 4), symbolLocalFrame(original, frame, 4));
        shot.layers[0].symbol->startFrame = 80; shot.layers[0].symbol->durationFrames = 12;
        QVERIFY(applyTimelineRipple(shot, project.fps, {TimelineRippleKind::Insert, 1, 5}).isEmpty());
        QCOMPARE(shot.layers[0].symbol->startFrame, 85); QCOMPARE(shot.layers[0].symbol->durationFrames, 12);
        shot.layers[0].symbol->startFrame = 1000000;
        const auto before = project.toJson();
        QVERIFY(!applyTimelineRipple(shot, project.fps, {TimelineRippleKind::Insert, 1, 1}).isEmpty());
        QCOMPARE(project.toJson(), before);
    }
};
QTEST_MAIN(SymbolClipsTests)
#include "test_symbol_clips.moc"
