#include "symbol_poses.h"
#include "symbol_clips.h"
#include "symbols.h"
#include "document.h"
#include "sheet.h"
#include "timeline_ripple.h"
#include "animation_test_fixture.h"
#include <QFile>
#include <QJsonArray>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

using namespace toon;
namespace {
Project fixture() {
    auto project = Project::create(); project.width = project.height = 128; project.transparent = true;
    SymbolDefinition source{animation_fixture::id(),"Mouth",Project::create().shots[0]}; source.content.durationFrames = 4;
    QImage image(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    QPainter painter(&image); painter.fillRect(QRect(8,8,12,12),Qt::red); painter.end();
    source.content.layers[0].tiles[{0,0}] = image;
    source.content.layerAnimations = {{source.content.layers[0].id,{},{{KeyframeProperty::PositionX,
        {{0,0,KeyframeInterpolation::Hold},{1,20,KeyframeInterpolation::Hold},{2,40,KeyframeInterpolation::Hold},{3,60,KeyframeInterpolation::Hold}}}}}};
    project.symbols.append(source);
    auto layer = RasterLayer::create("Mouth instance"); layer.kind = LayerKind::Symbol;
    layer.symbol = SymbolInstance{source.id,SymbolPlayback::Poses,3,0,1.5,30,-2,{{1,1},{7,2},{15,3}}};
    project.shots[0].layers = {layer}; project.shots[0].durationFrames = 40;
    return project;
}
const SymbolInstance &instance(const Project &project) { return *project.shots[0].layers[0].symbol; }
TimelineKeyRef ref(const Project &project,int frame) { return {project.shots[0].layers[0].id,KeyframeProperty::PositionX,{},frame,TimelineKeyKind::SymbolPose}; }
QJsonObject data(const QJsonObject &manifest) { return manifest["shots"].toArray()[0].toObject()["layers"].toArray()[0].toObject()["symbol"].toObject(); }
QJsonObject withData(QJsonObject manifest,const QJsonObject &value) {
    auto shots=manifest["shots"].toArray(); auto shot=shots[0].toObject(); auto layers=shot["layers"].toArray(); auto layer=layers[0].toObject();
    layer["symbol"]=value; layers[0]=layer; shot["layers"]=layers; shots[0]=shot; manifest["shots"]=shots; return manifest;
}
bool deleted(int frame,const TimelineRippleEdit &edit) { return edit.kind==TimelineRippleKind::Delete&&frame>=edit.startFrame&&frame<edit.startFrame+edit.durationFrames; }
}
class SymbolPosesTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QVERIFY2(fixture().validate().isEmpty(),qPrintable(fixture().validate())); }
    void heldPosesUseUnscaledClockAndRespectVisibility() {
        auto clip=instance(fixture()); QCOMPARE(symbolPoseClockFrame(clip,6),1); QCOMPARE(symbolPoseParentFrame(clip,1),6);
        QCOMPARE(symbolLocalFrame(clip,2,4),-1); QCOMPARE(symbolLocalFrame(clip,3,4),0);
        QCOMPARE(symbolLocalFrame(clip,6,4),1); QCOMPARE(symbolLocalFrame(clip,11,4),1); QCOMPARE(symbolLocalFrame(clip,12,4),2);
        QCOMPARE(symbolLocalFrame(clip,20,4),3); QCOMPARE(symbolLocalFrame(clip,32,4),3); QCOMPARE(symbolLocalFrame(clip,33,4),-1);
        QVERIFY(symbolPoseKeyAtParent(clip,6)); QVERIFY(!symbolPoseKeyAtParent(clip,7));
        for (double speed : {.01,.5,1.,100.}) { clip.speed=speed; QCOMPARE(symbolLocalFrame(clip,12,4),2); }
        clip.poseKeys.clear(); QCOMPARE(symbolLocalFrame(clip,20,4),0);
        clip.durationFrames=0; QCOMPARE(symbolLocalFrame(clip,3,4),-1);
    }
    void moveTrimSlipKeepHiddenKeysAndRecoverTheirPoses() {
        auto original=instance(fixture()); SymbolInstance trimmed,moved,extended,slipped;
        QVERIFY(previewSymbolClipEdit(original,40,SymbolClipOperation::TrimStart,12,&trimmed));
        QCOMPARE(trimmed.poseKeys,original.poseKeys);
        QVERIFY(previewSymbolClipEdit(trimmed,40,SymbolClipOperation::Move,-9,&moved));
        QCOMPARE(moved.poseKeys,original.poseKeys);
        QVERIFY(previewSymbolClipEdit(moved,40,SymbolClipOperation::TrimStart,0,&extended));
        for(int frame=12;frame<33;++frame) QCOMPARE(symbolLocalFrame(moved,frame-9,4),symbolLocalFrame(original,frame,4));
        QCOMPARE(symbolLocalFrame(extended,0,4),symbolLocalFrame(original,9,4));
        QVERIFY(previewSymbolClipEdit(original,40,SymbolClipOperation::Slip,4,&slipped));
        for(int frame=3;frame<29;++frame) QCOMPARE(symbolLocalFrame(slipped,frame,4),symbolLocalFrame(original,frame+4,4));
    }
    void editsAreOneUndoAndRejectLocksBoundsAndInvalidSources() {
        const auto project=fixture(); Document document; document.load(project); const auto id=project.shots[0].layers[0].id;
        QVERIFY(setSymbolPose(&document,id,9,3)); QCOMPARE(document.history()->count(),1);
        QCOMPARE(symbolPoseKeyAtParent(instance(document.project()),9)->poseFrame,3);
        QVERIFY(setSymbolPose(&document,id,9,3)); QCOMPARE(document.history()->count(),1);
        const auto changed=document.project().toJson();
        QVERIFY(!setSymbolPose(&document,id,-1,1)); QVERIFY(!setSymbolPose(&document,id,40,1)); QVERIFY(!setSymbolPose(&document,id,9,4));
        QCOMPARE(document.project().toJson(),changed);
        document.history()->undo(); QCOMPARE(document.project().toJson(),project.toJson()); document.history()->redo();
        QVERIFY(removeSymbolPose(&document,id,9)); QVERIFY(!symbolPoseKeyAtParent(instance(document.project()),9));
        QCOMPARE(instance(document.project()).playback,SymbolPlayback::Poses);
        auto locked=project; locked.shots[0].layers[0].locked=true; document.load(locked); QVERIFY(!setSymbolPose(&document,id,9,3));
        locked.shots[0].layers[0].locked=false; locked.shots[0].layers[0].positionLocked=true; document.load(locked); QVERIFY(!setSymbolPose(&document,id,9,3));
        locked.shots[0].layers[0].positionLocked=false; locked.shots[0].layers[0].symbol->playback=SymbolPlayback::Loop; document.load(locked);
        QVERIFY(setSymbolPose(&document,id,9,3)); QCOMPARE(instance(document.project()).playback,SymbolPlayback::Poses);
    }
    void genericMoveCopyPasteDeleteStayInPoseDomainAndAreAtomic() {
        auto project=fixture(); const auto id=project.shots[0].layers[0].id;
        // A pose key uses the default PositionX enum but must not touch that channel.
        project.shots[0].layerAnimations={{id,{},{{KeyframeProperty::PositionX,{{6,25}}}}}};
        Document document; document.load(project); TimelineKeyClipboard clipboard;
        QVERIFY(document.copyTimelineKeys({ref(project,6),ref(project,12)},&clipboard));
        QCOMPARE(clipboard.keys[0].symbolDefinitionId,instance(project).symbolId); QCOMPARE(clipboard.keys[0].symbolPose->frame,6);
        QVERIFY(document.moveTimelineKeys({ref(project,6)},1)); QCOMPARE(document.history()->count(),1);
        QCOMPARE(symbolPoseKeyAtParent(instance(document.project()),7)->poseFrame,1);
        QCOMPARE(document.project().shots[0].layerAnimations,project.shots[0].layerAnimations);
        const auto moved=document.project().toJson(); QVERIFY(!document.moveTimelineKeys({ref(project,7)},5)); QCOMPARE(document.project().toJson(),moved);
        QVERIFY(document.pasteTimelineKeys(clipboard,22)); QVERIFY(symbolPoseKeyAtParent(instance(document.project()),22)); QVERIFY(symbolPoseKeyAtParent(instance(document.project()),28));
        QVERIFY(document.removeTimelineKeys({ref(project,22),ref(project,28)})); QVERIFY(!symbolPoseKeyAtParent(instance(document.project()),22));
        auto bad=clipboard; bad.keys[1].symbolDefinitionId=animation_fixture::id(); const auto before=document.project().toJson();
        QVERIFY(!document.pasteTimelineKeys(bad,22)); QCOMPARE(document.project().toJson(),before);
        bad=clipboard; bad.keys[1].symbolPose->poseFrame=99; QVERIFY(!document.pasteTimelineKeys(bad,22)); QCOMPARE(document.project().toJson(),before);
        bad=clipboard; bad.keys[1].source.kind=TimelineKeyKind::Layer; QVERIFY(!document.pasteTimelineKeys(bad,22)); QCOMPARE(document.project().toJson(),before);
    }
    void sourceDefinitionChangesInvalidateClipboardAndDurationShrink() {
        auto project=fixture(); Document document; document.load(project); TimelineKeyClipboard clipboard;
        QVERIFY(document.copyTimelineKeys({ref(project,6)},&clipboard));
        const auto old=project.symbols[0].content; auto shorter=old; shorter.durationFrames=3;
        shorter.layerAnimations[0].channels[0].keys.removeLast();
        QString error; QVERIFY(!updateSymbolContent(&document,project.symbols[0].id,shorter,old,&error)); QVERIFY(!error.isEmpty());
        QCOMPARE(document.project().toJson(),project.toJson());
        const auto unique=makeSymbolInstanceUnique(&document,project.shots[0].layers[0].id); QVERIFY(!unique.isEmpty());
        const auto before=document.project().toJson(); QVERIFY(!document.pasteTimelineKeys(clipboard,25)); QCOMPARE(document.project().toJson(),before);
    }
    void strictValidationAndLimitsIncludeDormantKeysAndNestedSources() {
        const auto project=fixture(); const auto bad=[&](QVector<SymbolPoseKey> keys) { auto copy=project;copy.shots[0].layers[0].symbol->poseKeys=keys;return !copy.validate().isEmpty(); };
        QVERIFY(bad({{1,0},{1,1}})); QVERIFY(bad({{2,0},{1,1}})); QVERIFY(bad({{minSymbolPoseClock-1,0}})); QVERIFY(bad({{maxSymbolPoseClock+1,0}}));
        QVERIFY(bad({{0,-1}})); QVERIFY(bad({{0,4}}));
        auto copy=project; copy.shots[0].layers[0].symbol->poseKeys={{minSymbolPoseClock,0},{maxSymbolPoseClock,3}}; QVERIFY(copy.validate().isEmpty());
        QVector<SymbolPoseKey> keys; for(int i=0;i<=maxSymbolPoseKeys;++i) keys.append({i,i%4}); QVERIFY(bad(keys));
        copy=project; auto &shot=copy.shots[0]; auto layer=shot.layers[0]; shot.layers.clear(); keys.removeLast();
        for(int i=0;i<17;++i){layer.id=animation_fixture::id();layer.symbol->poseKeys=keys;shot.layers.append(layer);}
        QVERIFY(!copy.validate().isEmpty());
        QCOMPARE(symbolPoseHistoryCost(project.shots[0],project.shots[0]),0);
        copy=project;copy.shots[0].layers[0].symbol->poseKeys.append({30,2}); QVERIFY(symbolPoseHistoryCost(project.shots[0],copy.shots[0])>0);
    }
    void format34RoundTripAndLegacyFormatsRemainUnchanged() {
        auto project=fixture(); const auto manifest=project.toJson(); QCOMPARE(manifest["version"].toInt(),34);
        QVERIFY(data(manifest).contains("poseKeys")); QTemporaryDir directory; QString error; const auto path=directory.filePath("poses.toonproj");
        QVERIFY2(saveProject(project,path,&error),qPrintable(error)); QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); auto bytes=file.readAll();file.close();QVERIFY(bytes.startsWith("TOONPRO34\n"));
        auto reopened=loadProject(path,&error); QVERIFY2(reopened,qPrintable(error)); QCOMPARE(reopened->toJson(),manifest); QCOMPARE(renderMovieFrame(*reopened,12),renderMovieFrame(project,12));
        project.shots[0].layers[0].symbol->poseKeys.clear(); project.shots[0].layers[0].symbol->playback=SymbolPlayback::Loop;
        QCOMPARE(project.toJson()["version"].toInt(),32); QVERIFY(!data(project.toJson()).contains("poseKeys"));
        project.shots[0].layers[0].symbol->durationFrames=-1;project.shots[0].layers[0].symbol->playbackOffset=0;
        QCOMPARE(project.toJson()["version"].toInt(),31); QCOMPARE(data(project.toJson()).size(),5);
        TimelineCue cue;cue.id=animation_fixture::id();cue.name="Dialogue";project.shots[0].timelineCues={cue};QCOMPARE(project.toJson()["version"].toInt(),33);
        bytes.replace(0,10,"TOONPRO33\n");QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(bytes),bytes.size());file.close();QVERIFY(!loadProject(path,&error));
    }
    void malformedPoseMetadataIsRejectedAtomically() {
        const auto manifest=fixture().toJson(); const auto original=data(manifest); QString error;
        const auto reject=[&](QJsonObject instanceData){return !Project::fromJson(withData(manifest,instanceData),&error);};
        for (const auto &keys : {QJsonValue(3),QJsonValue(QJsonArray{QJsonObject{{"frame",1.5},{"poseFrame",1}}}),
             QJsonValue(QJsonArray{QJsonObject{{"frame",0},{"poseFrame",4}}}),QJsonValue(QJsonArray{QJsonObject{{"frame",0},{"poseFrame",1},{"extra",0}}}),
             QJsonValue(QJsonArray{QJsonObject{{"frame",0},{"poseFrame",1}},QJsonObject{{"frame",0},{"poseFrame",2}}})}) {
            auto bad=original;bad["poseKeys"]=keys;QVERIFY(reject(bad));
        }
        auto missing=original;missing.remove("poseKeys");QVERIFY(reject(missing));
        auto old=manifest;old["version"]=33;QVERIFY(!Project::fromJson(old,&error));
        auto invalid=original;invalid["playback"]="unknown";QVERIFY(reject(invalid));
    }
    void ripplePreservesEverySurvivingPoseAtCutsAndMovedBoundaries() {
        for(int hold : {-1,30}) for(int phase : {-4,0,4}) for(const auto &edit : {
            TimelineRippleEdit{TimelineRippleKind::Insert,10,4,0}, {TimelineRippleKind::Delete,9,6,0},
            {TimelineRippleKind::Move,12,5,25}, {TimelineRippleKind::Move,18,5,8}, {TimelineRippleKind::Delete,0,5,0}}) {
            auto project=fixture();project.shots[0].layers[0].symbol->durationFrames=hold;project.shots[0].layers[0].symbol->playbackOffset=phase;
            const auto original=instance(project);auto &shot=project.shots[0]; const auto error=applyTimelineRipple(shot,24,edit);QVERIFY2(error.isEmpty(),qPrintable(error));
            for(int frame=0;frame<40;++frame) if(!deleted(frame,edit)) {
                const int mapped=timelineRippleFrame(frame,40,edit);
                QCOMPARE(symbolLocalFrame(*shot.layers[0].symbol,mapped,4),symbolLocalFrame(original,frame,4));
            }
            QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        }
    }
    void rippleLimitAndVisibilitySplitsRejectWithoutPartialEdits() {
        auto project=fixture();auto &shot=project.shots[0];shot.durationFrames=10000;auto &clip=*shot.layers[0].symbol;
        clip.startFrame=0;clip.durationFrames=10000;clip.playbackOffset=0;clip.poseKeys.clear();
        for(int i=0;i<maxSymbolPoseKeys;++i)clip.poseKeys.append({i*2,i%4});
        const auto before=project.toJson();QVERIFY(!applyTimelineRipple(shot,24,{TimelineRippleKind::Move,3,1,9}).isEmpty());QCOMPARE(project.toJson(),before);
        project=fixture();project.shots[0].layers[0].symbol->startFrame=10;project.shots[0].layers[0].symbol->durationFrames=10;
        const auto bounded=project.toJson();QVERIFY(!applyTimelineRipple(project.shots[0],24,{TimelineRippleKind::Move,12,3,30}).isEmpty());QCOMPARE(project.toJson(),bounded);
    }
    void independentInstancesAndNestedPosesRenderAndBreakApartExactly() {
        auto project=fixture();auto second=project.shots[0].layers[0];second.id=animation_fixture::id();second.name="Second mouth";second.symbol->poseKeys={{0,3}};project.shots[0].layers.append(second);
        Document document;document.load(project);QVERIFY(setSymbolPose(&document,project.shots[0].layers[0].id,10,2));
        QCOMPARE(document.project().shots[0].layers[1].symbol->poseKeys,second.symbol->poseKeys);
        auto nested=fixture();auto content=Project::create().shots[0];content.durationFrames=40;content.layers={nested.shots[0].layers[0]};content.layers[0].id=animation_fixture::id();
        const QString wrapperId=animation_fixture::id();nested.symbols.append({wrapperId,"Face",content});
        auto &outer=*nested.shots[0].layers[0].symbol;outer.symbolId=wrapperId;outer.startFrame=0;outer.durationFrames=40;outer.playbackOffset=0;outer.poseKeys={{0,6},{10,12},{20,20}};
        QVERIFY2(nested.validate().isEmpty(),qPrintable(nested.validate()));
        auto direct=fixture(); QCOMPARE(renderMovieFrame(nested,10),renderMovieFrame(direct,12));
        document.load(nested);QVERIFY(document.setCurrentFrame(10));const auto frame=renderMovieFrame(document.project(),10);
        QVERIFY(breakApartSymbolInstance(&document,nested.shots[0].layers[0].id));QCOMPARE(renderMovieFrame(document.project(),10),frame);
        document.history()->undo();QCOMPARE(document.project().toJson(),nested.toJson());
    }
};
QTEST_MAIN(SymbolPosesTests)
#include "test_symbol_poses.moc"
