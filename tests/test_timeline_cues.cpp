#include "timeline_cues.h"
#include "document.h"
#include "sheet.h"
#include "symbols.h"
#include "animation_render.h"
#include "animation_test_fixture.h"
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

using namespace toon;
namespace {
TimelineCue cue(int start = 2, int hold = 3, TimelineCueKind kind = TimelineCueKind::Marker) {
    TimelineCue result;
    result.id = animation_fixture::id(); result.kind = kind; result.startFrame = start; result.durationFrames = hold;
    result.name = "Turn toward window"; result.speaker = "Akari"; result.text = "Wait…\n\tLook outside.";
    return result;
}
Project fixture() {
    auto project = Project::create(); project.width = project.height = 128; project.transparent = true;
    project.shots[0].durationFrames = 40;
    QImage pixels(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); pixels.fill(Qt::transparent);
    QPainter painter(&pixels); painter.fillRect(QRect(10,10,20,20), Qt::red); painter.end();
    project.shots[0].layers[0].tiles[{0,0}] = pixels;
    return project;
}
QJsonObject withCues(QJsonObject manifest, const QJsonValue &data) {
    auto shots = manifest["shots"].toArray(); auto shot = shots[0].toObject();
    shot["timelineCues"] = data; shots[0] = shot; manifest["shots"] = shots; return manifest;
}
QJsonObject withCue(QJsonObject manifest, const QJsonObject &data) { return withCues(manifest, QJsonArray{data}); }
QJsonObject cueData(const QJsonObject &manifest) {
    return manifest["shots"].toArray()[0].toObject()["timelineCues"].toArray()[0].toObject();
}
}

class TimelineCuesTests final : public QObject {
    Q_OBJECT
private slots:
    void allKindsRangesOverlapsAndBounds() {
        auto project = fixture(); auto &shot = project.shots[0];
        for (auto kind : {TimelineCueKind::Marker, TimelineCueKind::KeyPose, TimelineCueKind::Breakdown,
                          TimelineCueKind::Inbetween, TimelineCueKind::Dialogue}) {
            QVERIFY(!timelineCueKindName(kind).isEmpty());
            QCOMPARE(timelineCueKindFromKey(timelineCueKindKey(kind)), std::optional<TimelineCueKind>(kind));
            shot.timelineCues.append(cue(0, 1, kind)); shot.timelineCues.append(cue(0, 40, kind));
        }
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        QVERIFY(!timelineCueKindFromKey("unknown")); QVERIFY(timelineCueKindKey(TimelineCueKind(99)).isEmpty());
        QCOMPARE(timelineCue(shot, shot.timelineCues[0].id), &shot.timelineCues[0]);
        QVERIFY(!timelineCue(shot, "missing"));
        TimelineCue preview;
        QVERIFY(previewTimelineCueTiming(shot.timelineCues[0], 40, 39, 1, &preview));
        QCOMPARE(preview.startFrame, 39); QCOMPARE(preview.durationFrames, 1);
        const auto original = preview; QString error;
        for (auto timing : {QPair<int,int>{-1,1}, {0,0}, {39,2}, {std::numeric_limits<int>::max(),1}}) {
            QVERIFY(!previewTimelineCueTiming(shot.timelineCues[0], 40, timing.first, timing.second, &preview, &error));
            QCOMPARE(preview, original); QVERIFY(!error.isEmpty());
        }
    }

    void namesTextColorsAndIdsAreStrict() {
        auto project = fixture(); project.shots[0].timelineCues = {cue()};
        const auto good = project.shots[0].timelineCues[0];
        const auto rejected = [&](TimelineCue bad) {
            auto changed = project; changed.shots[0].timelineCues[0] = std::move(bad);
            return !changed.validate().isEmpty();
        };
        for (const QString &name : {QString(), QString(" padded"), QString("two\nlines"), QString(101,'x')}) {
            auto bad = good; bad.name = name; QVERIFY(rejected(bad));
        }
        for (const QString &speaker : {QString("two\twords"), QString("padded "), QString(101,'x')}) {
            auto bad = good; bad.speaker = speaker; QVERIFY(rejected(bad));
        }
        for (const QString &text : {QString(4097,'x'), QString("bad") + QChar(0), QString("bad\rreturn")}) {
            auto bad = good; bad.text = text; QVERIFY(rejected(bad));
        }
        auto bad = good; bad.color.setAlpha(254); QVERIFY(rejected(bad));
        bad = good; bad.color = QColor(); QVERIFY(rejected(bad));
        bad = good; bad.id = "bad"; QVERIFY(rejected(bad));
        bad = good; bad.id = project.shots[0].layers[0].id; QVERIFY(rejected(bad));
        project.shots[0].timelineCues.append(good); QVERIFY(!project.validate().isEmpty());
    }

    void editsAreSingleUndoNoOpSafeAndIgnoreArtworkLocks() {
        auto project = fixture(); project.shots[0].layers[0].locked = true;
        Document document; document.load(project); QVERIFY(document.setCurrentFrame(7));
        const auto selectedLayer = document.activeShot().layers[document.activeLayer()].id;
        auto note = cue(); note.id.clear(); note.name = "  Look up  "; note.speaker = " Akari "; QString error;
        const auto id = addTimelineCue(&document, note, &error); QVERIFY2(!id.isEmpty(), qPrintable(error));
        QCOMPARE(document.history()->count(), 1); QCOMPARE(document.currentFrame(), 7);
        QCOMPARE(document.activeShot().layers[document.activeLayer()].id, selectedLayer);
        QCOMPARE(timelineCue(document.activeShot(), id)->name, QString("Look up"));
        QCOMPARE(timelineCue(document.activeShot(), id)->speaker, QString("Akari"));
        note = *timelineCue(document.activeShot(), id);
        QVERIFY(updateTimelineCue(&document, note, &error)); QCOMPARE(document.history()->count(), 1);
        note.text = "Open the door."; QVERIFY(updateTimelineCue(&document, note, &error)); QCOMPARE(document.history()->count(), 2);
        document.history()->undo(); QVERIFY(timelineCue(document.activeShot(), id)->text != note.text);
        document.history()->redo(); QCOMPARE(timelineCue(document.activeShot(), id)->text, note.text);
        QVERIFY(removeTimelineCue(&document, id, &error)); QCOMPARE(document.history()->count(), 3);
        QVERIFY(document.activeShot().timelineCues.isEmpty());
        document.history()->undo(); QCOMPARE(*timelineCue(document.activeShot(), id), note);
        document.history()->undo(); document.history()->undo(); QCOMPARE(document.project().toJson(), project.toJson());
    }

    void failuresAndActiveGesturesLeaveDocumentUntouched() {
        auto project = fixture(); project.shots[0].timelineCues = {cue()}; Document document; document.load(project);
        const auto manifest = document.project().toJson(); QString error;
        auto invalid = project.shots[0].timelineCues[0]; invalid.durationFrames = 100;
        QVERIFY(!updateTimelineCue(&document, invalid, &error));
        QVERIFY(addTimelineCue(&document, project.shots[0].timelineCues[0], &error).isEmpty());
        QVERIFY(!removeTimelineCue(&document, "missing", &error));
        auto shorter = document.project(); shorter.shots[0].durationFrames = 3;
        document.replaceProject(shorter, "Shorten shot");
        QCOMPARE(document.project().toJson(), manifest); QCOMPARE(document.history()->count(), 0);
        QVERIFY(document.beginLayerPropertyEdit());
        QVERIFY(addTimelineCue(&document, cue(), &error).isEmpty());
        QVERIFY(!removeTimelineCue(&document, project.shots[0].timelineCues[0].id, &error));
        document.cancelLayerPropertyEdit();
        QVERIFY(document.beginStroke(QPointF(25,25), 1., BrushSettings{}, false));
        QVERIFY(addTimelineCue(&document, cue(), &error).isEmpty());
        QVERIFY(!updateTimelineCue(&document, invalid, &error)); document.cancelStroke();
        QCOMPARE(document.project().toJson(), manifest); QCOMPARE(document.history()->count(), 0);
    }

    void duplicateShotsGetIndependentIdsAndSymbolConversionKeepsNotesInShot() {
        auto project = fixture(); project.shots[0].timelineCues = {cue()}; Document document; document.load(project);
        const auto original = project.shots[0].timelineCues[0]; const auto copyId = document.duplicateShot(project.shots[0].id);
        QVERIFY(!copyId.isEmpty()); QCOMPARE(document.project().shots.size(), 2);
        auto duplicated = document.activeShot().timelineCues[0]; QVERIFY(duplicated.id != original.id);
        duplicated.id = original.id; QCOMPARE(duplicated, original); QVERIFY(document.project().validate().isEmpty());
        document.history()->undo(); QCOMPARE(document.project().toJson(), project.toJson());
        QString error;
        const auto symbolId = convertLayersToSymbol(&document, {document.activeShot().layers[0].id}, "Face", &error);
        QVERIFY2(!symbolId.isEmpty(), qPrintable(error));
        QCOMPARE(document.activeShot().timelineCues, project.shots[0].timelineCues);
        QVERIFY(document.project().symbols[0].content.timelineCues.isEmpty());
        auto invalid = document.project(); invalid.symbols[0].content.timelineCues = {cue(0,1)};
        QVERIFY(!invalid.validate().isEmpty());
        QVERIFY(!Project::fromJson(invalid.toJson(), &error));
        auto edit = editProjectForSymbol(document.project(), symbolId, &error);
        const auto expected = document.project().symbols[0].content;
        edit.shots[0].timelineCues = {cue(0,1)};
        edit.shots[0].layers[0].name = "Renamed inside Symbol";
        QVERIFY2(updateSymbolContent(&document,symbolId,edit.shots[0],expected,&error),qPrintable(error));
        QVERIFY(document.project().symbols[0].content.timelineCues.isEmpty());
        QCOMPARE(document.activeShot().timelineCues,project.shots[0].timelineCues);
    }

    void format33RoundTripAndNoCueLegacyMetadataStayExact() {
        auto project = fixture(); const auto legacy = project.toJson(); QCOMPARE(legacy["version"].toInt(), 19);
        project.shots[0].timelineCues = {cue(), cue(10,7,TimelineCueKind::Dialogue)};
        const auto manifest = project.toJson(); QCOMPARE(manifest["version"].toInt(), 33);
        QVERIFY(manifest["symbols"].toArray().isEmpty()); QVERIFY(manifest.contains("characters"));
        QTemporaryDir directory; const auto path = directory.filePath("cues.toonproj"); QString error;
        QVERIFY2(saveProject(project, path, &error), qPrintable(error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.read(10), QByteArray("TOONPRO33\n")); file.close();
        const auto loaded = loadProject(path, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->toJson(), manifest); QCOMPARE(loaded->shots[0].timelineCues, project.shots[0].timelineCues);
        QCOMPARE(renderMovieFrame(*loaded, 2), renderMovieFrame(project, 2));
        QVERIFY(file.open(QIODevice::ReadWrite)); QCOMPARE(file.write("TOONPRO32\n",10), qint64(10)); file.close();
        QVERIFY(!loadProject(path, &error)); QVERIFY(error.contains("container"));
        project.shots[0].timelineCues.clear(); QCOMPARE(project.toJson(), legacy);
        QVERIFY(saveProject(project, path, &error));
        const auto old = loadProject(path, &error); QVERIFY2(old, qPrintable(error)); QCOMPARE(old->toJson(), legacy);
    }

    void cueOnlyFormat33SupportsCharacterLibrariesAndSavedAids() {
        auto project = fixture(); project.shots[0].timelineCues = {cue()};
        project.characters.append({animation_fixture::id(), "Akari", {}});
        auto &character = project.characters[0];
        character.colors = {{animation_fixture::id(), "Hair", CharacterColorCategory::Hair, QColor("#351d37")}};
        project.drawingAids.activeShotId = project.shots[0].id; project.drawingAids.activeFrame = 3;
        QTemporaryDir directory; QString error; const auto path = directory.filePath("character.toonproj");
        QVERIFY2(saveProject(project,path,&error),qPrintable(error)); const auto loaded = loadProject(path,&error);
        QVERIFY2(loaded,qPrintable(error)); QCOMPARE(loaded->toJson(),project.toJson());
        Document document; document.load(project); auto note = project.shots[0].timelineCues[0]; note.text = "New line";
        const auto aids = drawingAidsToJson(document.project().drawingAids);
        QVERIFY(updateTimelineCue(&document,note,&error)); QCOMPARE(drawingAidsToJson(document.project().drawingAids),aids);
        document.history()->undo(); QCOMPARE(drawingAidsToJson(document.project().drawingAids),aids);
    }

    void malformedCueMetadataAndDowngradesAreRejected() {
        auto project = fixture(); project.shots[0].timelineCues = {cue()}; const auto manifest = project.toJson();
        const auto data = cueData(manifest); QString error;
        for (const auto field : {QString("startFrame"), QString("durationFrames")})
            for (const auto value : {QJsonValue("1"), QJsonValue(true), QJsonValue(QJsonValue::Null), QJsonValue(1.5), QJsonValue(-1), QJsonValue(1000001)}) {
                auto bad = data; bad[field] = value; QVERIFY(!Project::fromJson(withCue(manifest,bad),&error));
            }
        for (const auto field : data.keys()) {
            auto bad = data; bad.remove(field); QVERIFY(!Project::fromJson(withCue(manifest,bad),&error));
        }
        for (const auto color : {"red", "#ff001122", "#12345g", "#123", "transparent"}) {
            auto bad = data; bad["color"] = color; QVERIFY(!Project::fromJson(withCue(manifest,bad),&error));
        }
        auto bad = data; bad["kind"] = "mouth-pose"; QVERIFY(!Project::fromJson(withCue(manifest,bad),&error));
        bad = data; bad["extra"] = true; QVERIFY(!Project::fromJson(withCue(manifest,bad),&error));
        QVERIFY(!Project::fromJson(withCues(manifest,QJsonObject{}),&error));
        QVERIFY(!Project::fromJson(withCues(manifest,QJsonArray{data,data}),&error));
        auto old = manifest; old["version"] = 32; QVERIFY(!Project::fromJson(old,&error)); QVERIFY(error.contains("33"));
        auto future = manifest; future["version"] = latestProjectFormatVersion+1; QVERIFY(!Project::fromJson(future,&error));
        QVector<TimelineCue> parsed{cue()}; const auto before = parsed;
        QVERIFY(!timelineCuesFromJson({data,QJsonValue(true)},&parsed,&error)); QCOMPARE(parsed,before);
    }

    void cueLimitsAreAtomicAndGlobal() {
        auto project = fixture(); auto &shot = project.shots[0];
        for (int i=0;i<maxShotTimelineCues;++i) shot.timelineCues.append(cue());
        QVERIFY(project.validate().isEmpty()); Document document; document.load(project); QString error;
        QVERIFY(addTimelineCue(&document,cue(),&error).isEmpty()); QCOMPARE(document.history()->count(),0);
        QCOMPARE(document.project().toJson(),project.toJson());
        shot.timelineCues.append(cue()); QVERIFY(!project.validate().isEmpty());
        project = fixture(); project.shots.clear();
        for(int s=0;s<16;++s) {
            auto next = Project::create().shots[0];
            for(int i=0;i<maxShotTimelineCues;++i) next.timelineCues.append(cue());
            project.shots.append(next);
        }
        QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        auto extra = Project::create().shots[0]; extra.timelineCues = {cue()}; project.shots.append(extra);
        QVERIFY(project.validate().contains("16384"));
    }

    void insertionInsideExtendsButBoundaryInsertionMovesOrLeavesCue() {
        const auto original = cue(10,10,TimelineCueKind::Dialogue);
        for (int at : {0,10,15,20,40}) {
            auto project = fixture(); auto &shot = project.shots[0]; shot.timelineCues = {original};
            QVERIFY(applyTimelineRipple(shot,24.,{TimelineRippleKind::Insert,at,5,0}).isEmpty());
            QCOMPARE(shot.durationFrames,45); QCOMPARE(shot.timelineCues.size(),1); const auto &result=shot.timelineCues[0];
            QCOMPARE(result.startFrame,at<=10?15:10); QCOMPARE(result.durationFrames,at>10&&at<20?15:10);
            QCOMPARE(result.id,original.id); QCOMPARE(result.text,original.text);
        }
    }

    void deletionShrinksJoinsAndRemovesNotes() {
        auto project = fixture(); auto &shot=project.shots[0];
        const auto kept=cue(5,20,TimelineCueKind::Dialogue); const auto removed=cue(10,3);
        shot.timelineCues={kept,removed};
        QVERIFY(applyTimelineRipple(shot,24.,{TimelineRippleKind::Delete,10,5,0}).isEmpty());
        QCOMPARE(shot.durationFrames,35); QCOMPARE(shot.timelineCues.size(),1);
        QCOMPARE(shot.timelineCues[0].id,kept.id); QCOMPARE(shot.timelineCues[0].startFrame,5);
        QCOMPARE(shot.timelineCues[0].durationFrames,15);
    }

    void movingPartOfCueSplitsAndWholeRangeCoalesces() {
        auto project=fixture(); auto &shot=project.shots[0]; const auto original=cue(5,15,TimelineCueKind::Dialogue);
        shot.timelineCues={original}; QVERIFY(applyTimelineRipple(shot,24.,{TimelineRippleKind::Move,10,5,30}).isEmpty());
        QCOMPARE(shot.timelineCues.size(),2); QCOMPARE(shot.timelineCues[0].startFrame,5);
        QCOMPARE(shot.timelineCues[0].durationFrames,10); QCOMPARE(shot.timelineCues[0].id,original.id);
        QCOMPARE(shot.timelineCues[1].startFrame,25); QCOMPARE(shot.timelineCues[1].durationFrames,5);
        QVERIFY(shot.timelineCues[1].id!=original.id); QCOMPARE(shot.timelineCues[1].text,original.text);
        project=fixture(); project.shots[0].timelineCues={cue(0,40)};
        QVERIFY(applyTimelineRipple(project.shots[0],24.,{TimelineRippleKind::Move,10,5,30}).isEmpty());
        QCOMPARE(project.shots[0].timelineCues.size(),1); QCOMPARE(project.shots[0].timelineCues[0].durationFrames,40);
    }

    void rippleSplittingLimitRejectsAtomicallyAndDocumentUndoRestoresCues() {
        auto project=fixture(); for(int i=0;i<maxShotTimelineCues;++i) project.shots[0].timelineCues.append(cue(5,15));
        const auto before=project.toJson();
        QVERIFY(!applyTimelineRipple(project.shots[0],24.,{TimelineRippleKind::Move,10,5,30}).isEmpty());
        QCOMPARE(project.toJson(),before);
        project=fixture(); project.shots[0].timelineCues={cue(5,15)}; Document document; document.load(project);
        QVERIFY(document.rippleTimeline({TimelineRippleKind::Delete,10,5,0})); QCOMPARE(document.history()->count(),1);
        QCOMPARE(document.activeShot().timelineCues[0].durationFrames,10);
        document.history()->undo(); QCOMPARE(document.project().toJson(),project.toJson());
        document.history()->redo(); QCOMPARE(document.activeShot().timelineCues[0].durationFrames,10);
    }

    void cuesNeverChangeArtworkExportsOrDisplayCacheIdentity() {
        auto project=animation_fixture::project(); const auto original=project;
        for(int frame=0;frame<project.shots[0].durationFrames;++frame)
            project.shots[0].timelineCues.append(cue(frame,1,TimelineCueKind::Dialogue));
        for(int frame : {0,2,4,6,10,19}) {
            QCOMPARE(renderMovieFrame(project,frame),renderMovieFrame(original,frame));
            QCOMPARE(animationDisplayKey(project.shots[0],frame),animationDisplayKey(original.shots[0],frame));
        }
        QCOMPARE(renderSheet(project,project.shots[0]),renderSheet(original,original.shots[0]));
        QTemporaryDir before,after; QString error;
        QVERIFY(exportPngSequence(original,0,0,1,before.path(),{},&error));
        QVERIFY(exportPngSequence(project,0,0,1,after.path(),{},&error));
        const auto names=QDir(before.path()).entryList({"*.png"},QDir::Files);
        QVERIFY(!names.isEmpty());
        for(const auto &name:names) QCOMPARE(QImage(before.filePath(name)),QImage(after.filePath(name)));
        QVERIFY(timelineCueHistoryCost(original.shots[0],project.shots[0])>4096);
        QCOMPARE(timelineCueHistoryCost(project.shots[0],project.shots[0]),qint64(0));
    }
};
QTEST_MAIN(TimelineCuesTests)
#include "test_timeline_cues.moc"
