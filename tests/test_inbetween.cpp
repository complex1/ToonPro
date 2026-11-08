#include "animation_test_fixture.h"
#include "document.h"
#include "inbetween.h"
#include "sheet.h"
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

using namespace toon;
namespace {
QVector<TimelineCelRef> pair(const Project &project) {
    const auto &track=project.shots[0].animationTracks[0];
    return {{track.id,track.exposures[0].id},{track.id,track.exposures[1].id}};
}
InbetweenPlan plan(const Project &project,int step=1,CubicEasing curve={}) {
    QString error; auto result=planInbetweens(project,project.shots[0].id,pair(project),step,curve,&error);
    if (!error.isEmpty()) qFatal("%s",qPrintable(error));
    return result;
}
QVector<QImage> generated(const InbetweenPlan &plan) {
    QVector<QImage> images;
    for (int i=0;i<plan.samples.size();++i) {
        QImage image(plan.size,QImage::Format_RGB32); image.fill(QColor(20+i*30,150,40)); images.append(image);
    }
    return images;
}
}
class InbetweenTests : public QObject {
    Q_OBJECT
private slots:
    void easingUsesOriginalTimelinePositions() {
        auto project=animation_fixture::project();
        const auto linear=plan(project);
        QCOMPARE(linear.samples.size(),3); QCOMPARE(linear.samples[0].frame,1); QCOMPARE(linear.samples[2].frame,3);
        QCOMPARE(linear.samples[0].time,.25); QCOMPARE(linear.samples[1].time,.5); QCOMPARE(linear.samples[2].time,.75);
        const auto eased=plan(project,1,easingForInterpolation(KeyframeInterpolation::EaseIn));
        QCOMPARE(eased.samples[0].time,.0625); QCOMPARE(eased.samples[1].time,.25); QCOMPARE(eased.samples[2].time,.5625);
        auto reverse=pair(project); std::reverse(reverse.begin(),reverse.end()); QString error;
        QCOMPARE(planInbetweens(project,project.shots[0].id,reverse,1,{},&error),linear); QVERIFY(error.isEmpty());
        const auto twos=plan(project,2); QCOMPARE(twos.samples.size(),1); QCOMPARE(twos.samples[0].frame,2); QCOMPARE(twos.samples[0].hold,2);
    }
    void rendersDrawingWithoutOtherTracksOrPaper() {
        auto project=animation_fixture::project();
        project.shots[0].layers[0].tiles.insert({0,0},project.shots[0].layers[3].tiles.first());
        QString error; const auto prepared=plan(project);
        const auto first=renderInbetweenSource(project,prepared,false,&error);
        QVERIFY2(error.isEmpty(),qPrintable(error)); QCOMPARE(first.pixelColor(10,10),QColor(Qt::red));
        const auto last=renderInbetweenSource(project,prepared,true,&error);
        QVERIFY2(error.isEmpty(),qPrintable(error)); QCOMPARE(last.pixelColor(10,10),QColor(Qt::blue));
        const auto &track=project.shots[0].animationTracks[0];
        auto &ink=project.shots[0].layers[animation_fixture::layerIndex(project.shots[0],track.drawings[0].templateLayers.value("ink"))];
        ink.tiles.first().setPixelColor(10,10,Qt::transparent);
        const auto transparent=renderInbetweenSource(project,prepared,false,&error);
        QVERIFY(error.isEmpty()); QCOMPARE(transparent.pixelColor(10,10).alpha(),0);
        QCOMPARE(inbetweenWhiteMatte(transparent).pixelColor(10,10),QColor(Qt::white));
    }
    void rejectsConflictsLocksAndBudgets() {
        auto project=animation_fixture::project(); auto refs=pair(project); QString error;
        auto &track=project.shots[0].animationTracks[0];
        track.exposures.insert(1,{animation_fixture::id(),track.drawings[0].id,3,1});
        planInbetweens(project,project.shots[0].id,refs,1,{},&error); QVERIFY(error.contains("another drawing"));
        project=animation_fixture::project(); refs=pair(project);
        project.shots[0].layers[1].locked=true;
        planInbetweens(project,project.shots[0].id,refs,1,{},&error); QVERIFY(!error.isEmpty());
        project=animation_fixture::project(); project.width=4096; project.height=4096; refs=pair(project);
        planInbetweens(project,project.shots[0].id,refs,1,{},&error); QVERIFY(!error.isEmpty());
        project=animation_fixture::project(); refs=pair(project);
        auto &root=project.shots[0].layers[1]; root.opacity=.5;
        planInbetweens(project,project.shots[0].id,refs,1,{},&error); QVERIFY(!error.isEmpty());
    }
    void batchInsertionIsOneUndoAndPreservesEndpoints() {
        auto project=animation_fixture::project(); auto &track=project.shots[0].animationTracks[0];
        track.exposures[0].durationFrames=4;
        Document document; document.load(project); const auto before=document.project().toJson();
        const auto prepared=plan(project); const auto images=generated(prepared);
        const auto revision=document.contentRevision();
        QVERIFY(document.insertInbetweens(prepared,images,revision));
        QCOMPARE(document.history()->count(),1); QVERIFY(document.project().validate().isEmpty());
        const auto &after=document.project().shots[0].animationTracks[0];
        QCOMPARE(after.drawings.size(),5); QCOMPARE(after.exposures.size(),5);
        QCOMPARE(after.exposures[0].drawingId,track.exposures[0].drawingId); QCOMPARE(after.exposures[0].durationFrames,1);
        QCOMPARE(after.exposures.last(),track.exposures.last());
        QString error; const auto evaluated=evaluateLayers(document.project(),document.activeShot(),2,false,{},&error);
        QImage canvas(128,128,QImage::Format_ARGB32_Premultiplied); canvas.fill(Qt::transparent);
        { QPainter painter(&canvas); paintLayers(painter,evaluated,false); }
        QCOMPARE(canvas.pixelColor(20,20),images[1].pixelColor(20,20));
        const auto afterJson=document.project().toJson();
        QTemporaryDir temporary; QString saveError;
        const auto path=temporary.filePath("inbetweens.toon");
        QVERIFY2(saveProject(document.project(),path,&saveError),qPrintable(saveError));
        const auto reopened=loadProject(path,&saveError); QVERIFY2(reopened.has_value(),qPrintable(saveError));
        QCOMPARE(reopened->toJson(),afterJson);
        document.history()->undo(); QCOMPARE(document.project().toJson(),before);
        document.history()->redo(); QCOMPARE(document.project().toJson(),afterJson);
        QVERIFY(!document.insertInbetweens(prepared,images,revision)); QCOMPARE(document.history()->count(),1);
    }
    void invalidBatchNeverPartiallyInserts() {
        Document document; document.load(animation_fixture::project());
        const auto prepared=plan(document.project()); auto images=generated(prepared);
        const auto before=document.project().toJson(); const auto revision=document.contentRevision();
        images.last()=QImage(2,2,QImage::Format_RGB32);
        QVERIFY(!document.insertInbetweens(prepared,images,revision));
        QCOMPARE(document.history()->count(),0); QCOMPARE(document.project().toJson(),before);
        images=generated(prepared); images[1]=images[1].convertToFormat(QImage::Format_ARGB32); images[1].setPixelColor(1,1,Qt::transparent);
        QVERIFY(!document.insertInbetweens(prepared,images,revision)); QCOMPARE(document.project().toJson(),before);
    }
    void onTwosFillsTimingAndKeepsShortLastHold() {
        auto project=animation_fixture::project(); auto &track=project.shots[0].animationTracks[0];
        track.exposures[0].durationFrames=1; track.exposures[1].startFrame=5;
        Document document; document.load(project); auto prepared=plan(project,2);
        QCOMPARE(prepared.samples.size(),2); QCOMPARE(prepared.samples.last().hold,1);
        QVERIFY(document.insertInbetweens(prepared,generated(prepared),document.contentRevision()));
        const auto &result=document.activeShot().animationTracks[0];
        QCOMPARE(result.exposures[0].durationFrames,2);
        for (int frame=0;frame<=5;++frame) QVERIFY(exposureAtFrame(result,frame));
        document.history()->undo(); QCOMPARE(document.project().toJson(),project.toJson());
    }
    void lineArtMatteRecoversColoredInkAndSoftEdges() {
        const QColor ink(31,37,48);
        QImage source(256,1,QImage::Format_ARGB32_Premultiplied);
        for (int x=0;x<256;++x) source.setPixelColor(x,0,QColor(ink.red(),ink.green(),ink.blue(),x));
        QCOMPARE(inbetweenInkColor(source,source),ink);
        const auto matte=inbetweenWhiteMatte(source);
        const auto recovered=inbetweenTransparentInk(matte,ink);
        const auto recomposited=inbetweenWhiteMatte(recovered);
        for (int x=0;x<256;++x) {
            QVERIFY(qAbs(recovered.pixelColor(x,0).alpha()-x)<=1);
            for (int channel=0;channel<3;++channel) {
                const auto a=matte.pixelColor(x,0), b=recomposited.pixelColor(x,0);
                QVERIFY(qAbs((channel==0?a.red():channel==1?a.green():a.blue())-(channel==0?b.red():channel==1?b.green():b.blue()))<=1);
            }
        }
        QCOMPARE(recovered.pixelColor(255,0),ink); QCOMPARE(recovered.pixelColor(0,0).alpha(),0);
    }
    void transparentBatchPersistsAlphaAndOneUndo() {
        auto project=animation_fixture::project(); QString error;
        const auto prepared=planInbetweens(project,project.shots[0].id,pair(project),1,{},&error,InbetweenOutput::TransparentInk);
        QVERIFY(error.isEmpty()); auto images=generated(prepared);
        for (auto &image : images) { image=image.convertToFormat(QImage::Format_ARGB32_Premultiplied); image.setPixelColor(0,0,Qt::transparent); image.setPixelColor(1,0,QColor(31,37,48,128)); }
        Document document; document.load(project);
        QVERIFY(document.insertInbetweens(prepared,images,document.contentRevision())); QCOMPARE(document.history()->count(),1);
        const auto &track=document.activeShot().animationTracks[0];
        const auto &drawing=track.drawings[2];
        QImage pixel;
        for (const auto &layer:document.activeShot().layers) if (layer.parentId==drawing.groupLayerId && !layer.group) pixel=layer.tiles.first();
        QCOMPARE(pixel.pixelColor(0,0).alpha(),0); QCOMPARE(pixel.pixelColor(1,0).alpha(),128);
        QTemporaryDir temporary; const auto path=temporary.filePath("transparent.toon");
        QVERIFY(saveProject(document.project(),path,&error)); const auto loaded=loadProject(path,&error); QVERIFY(loaded.has_value());
        for (const auto &layer:loaded->shots[0].layers) if (layer.parentId==drawing.groupLayerId && !layer.group)
            QCOMPARE(layer.tiles.first().pixelColor(1,0).alpha(),128);
        document.history()->undo(); QCOMPARE(document.project().toJson(),project.toJson()); document.history()->redo();
        QVERIFY(document.project().validate().isEmpty());
    }
};
QTEST_MAIN(InbetweenTests)
#include "test_inbetween.moc"
