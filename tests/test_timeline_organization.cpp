#include "timeline_organization.h"
#include "animation_test_fixture.h"
#include "animation_render.h"
#include "document.h"
#include "sheet.h"
#include "symbol_poses.h"
#include <QFile>
#include <QDataStream>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
using namespace toon;
namespace {
int index(const Shot &shot,const QString &id){return animation_fixture::layerIndex(shot,id);}
RasterLayer &layer(Project &p,const QString &id){return p.shots[0].layers[index(p.shots[0],id)];}
QString addFolder(Project &p,const QString &name,const QString &parent={}) {
    auto folder=RasterLayer::create(name);folder.group=true;folder.parentId=parent;p.shots[0].layers.append(folder);return folder.id;
}
Project fixture() {
    auto p=animation_fixture::project();p.transparent=true;
    SymbolDefinition mouth{animation_fixture::id(),"Mouth",Project::create().shots[0]};mouth.content.durationFrames=4;p.symbols.append(mouth);
    auto instance=RasterLayer::create("Mouth");instance.kind=LayerKind::Symbol;instance.parentId=p.shots[0].animationTracks[0].drawings[0].groupLayerId;
    instance.symbol=SymbolInstance{mouth.id,SymbolPlayback::Poses,0,0,1,20,0,{{0,1},{2,3}}};p.shots[0].layers.append(instance);
    const auto ink=p.shots[0].animationTracks[0].drawings[0].templateLayers.value("ink");
    p.shots[0].layerAnimations={{ink,{},{{KeyframeProperty::PositionX,{{0,0},{19,10}}}}}};
    return p;
}
bool writeManifestContainer(const QString &path,const QJsonObject &manifest,int headerVersion) {
    QFile file(path);if(!file.open(QIODevice::WriteOnly))return false;
    const auto header=QString("TOONPRO%1\n").arg(headerVersion).toUtf8();
    if(file.write(header)!=header.size())return false;
    QDataStream stream(&file);stream.setByteOrder(QDataStream::BigEndian);
    const auto data=QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    const auto hash=QCryptographicHash::hash(data,QCryptographicHash::Sha256);
    stream<<quint32(data.size());stream.writeRawData(hash.constData(),hash.size());stream.writeRawData(data.constData(),data.size());
    return stream.status()==QDataStream::Ok;
}
QImage composite(const QVector<RasterLayer> &layers) {
    QImage image(128,128,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter painter(&image);paintLayers(painter,layers,false);return image;
}
}
class TimelineOrganizationTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){QVERIFY2(fixture().validate().isEmpty(),qPrintable(fixture().validate()));}
    void folderAndTrackMovesPreserveDrawingClocksAndUndo() {
        Document d;d.load(fixture());const auto original=d.project();const auto root=d.activeShot().animationTracks[0].rootGroupLayerId;
        QString outer,inner,error;QVERIFY2(addTimelineFolder(&d,{},"Characters",&outer,&error),qPrintable(error));
        QCOMPARE(d.history()->count(),1);QVERIFY(addTimelineFolder(&d,outer,"Hero",&inner,&error));
        const auto before=d.project();QVERIFY2(moveTimelineLayer(&d,root,inner,{},&error),qPrintable(error));QCOMPARE(d.history()->count(),3);
        const auto after=d.project();QCOMPARE(after.shots[0].layers[index(after.shots[0],root)].parentId,inner);
        QCOMPARE(after.shots[0].animationTracks,original.shots[0].animationTracks);QCOMPARE(after.shots[0].layerAnimations,original.shots[0].layerAnimations);
        QCOMPARE(after.shots[0].audioClips,original.shots[0].audioClips);
        for(const auto &old:original.shots[0].layers)if(old.symbol)QCOMPARE(after.shots[0].layers[index(after.shots[0],old.id)].symbol,old.symbol);
        for(int frame:{0,2,3,4,6,19})QCOMPARE(renderMovieFrame(after,frame),renderMovieFrame(original,frame));
        d.history()->undo();QCOMPARE(d.project().toJson(),before.toJson());d.history()->redo();QCOMPARE(d.project().toJson(),after.toJson());
        QVERIFY(moveTimelineLayer(&d,outer,{},outer,&error));QCOMPARE(d.history()->count(),3);
        QVERIFY(moveTimelineLayer(&d,outer,{}, {},&error));QCOMPARE(d.history()->count(),3);
        d.history()->undo();d.history()->undo();d.history()->undo();QCOMPARE(d.project().toJson(),original.toJson());
    }
    void siblingOrderChangesCompositionButNeverTiming() {
        auto p=animation_fixture::project();p.transparent=true;const auto root=p.shots[0].animationTracks[0].rootGroupLayerId;
        auto cover=RasterLayer::create("Background");QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::green);cover.tiles[{0,0}]=tile;p.shots[0].layers.append(cover);
        const auto tracks=p.shots[0].animationTracks;Document d;d.load(p);QCOMPARE(renderMovieFrame(d.project(),0).pixelColor(64,64),QColor(Qt::green));
        QVERIFY(moveTimelineLayer(&d,cover.id,{},root));QCOMPARE(renderMovieFrame(d.project(),0).pixelColor(64,64),QColor(Qt::red));QCOMPARE(d.activeShot().animationTracks,tracks);
        d.history()->undo();QCOMPARE(renderMovieFrame(d.project(),0).pixelColor(64,64),QColor(Qt::green));
    }
    void invalidParentsCyclesDrawingMovesAndClippingRejectAtomically() {
        auto p=fixture();const auto root=p.shots[0].animationTracks[0].rootGroupLayerId, drawing=p.shots[0].animationTracks[0].drawings[0].groupLayerId;
        const auto folder=addFolder(p,"Folder"),child=addFolder(p,"Child",folder);Document d;d.load(p);const auto before=d.project().toJson();QString error;
        for(const auto &target:QStringList{root,drawing,"missing"}){QVERIFY(!moveTimelineLayer(&d,root,target,{},&error));QVERIFY(!error.isEmpty());}
        QVERIFY(!moveTimelineLayer(&d,folder,child));QVERIFY(!moveTimelineLayer(&d,drawing,folder));
        const auto ink=d.activeShot().animationTracks[0].drawings[0].templateLayers.value("ink");QVERIFY(!moveTimelineLayer(&d,ink,{}));
        QVERIFY(!addTimelineFolder(&d,root,"Wrong"));QVERIFY(!addTimelineFolder(&d,drawing,"Wrong"));
        QCOMPARE(d.project().toJson(),before);QCOMPARE(d.history()->count(),0);
        auto clipped=Project::create();auto base=clipped.shots[0].layers[0];auto top=RasterLayer::create("Clipped");top.clipped=true;clipped.shots[0].layers.append(top);
        d.load(clipped);QVERIFY(!moveTimelineLayer(&d,base.id,{}));QCOMPARE(d.project().toJson(),clipped.toJson());QCOMPARE(d.history()->count(),0);
    }
    void lockedAncestorsContentsAndPositionsProtectOrganization() {
        auto p=fixture();const auto root=p.shots[0].animationTracks[0].rootGroupLayerId;const auto folder=addFolder(p,"Characters"),other=addFolder(p,"Other");layer(p,root).parentId=folder;
        for(int mode=0;mode<4;++mode){auto locked=p;
            if(mode==0)layer(locked,folder).locked=true;
            if(mode==1)layer(locked,folder).positionLocked=true;
            if(mode==2)layer(locked,other).positionLocked=true;
            if(mode==3)layer(locked,locked.shots[0].animationTracks[0].drawings[0].templateLayers.value("ink")).locked=true;
            Document d;d.load(locked);QVERIFY(!moveTimelineLayer(&d,root,other));QCOMPARE(d.project().toJson(),locked.toJson());QCOMPARE(d.history()->count(),0);
        }
        layer(p,folder).locked=true;Document d;d.load(p);QVERIFY(d.selectAnimationTrack(d.activeShot().animationTracks[0].id));
        QVERIFY(!d.createAnimationDrawing(false,1));QVERIFY(!d.renameAnimationDrawing("Changed"));QVERIFY(!d.removeAnimationTrack());QVERIFY(!addTimelineFolder(&d,folder,"New"));QCOMPARE(d.history()->count(),0);
    }
    void layersPanelMutationAndOwnershipRemainConsistent() {
        auto p=fixture();const auto root=p.shots[0].animationTracks[0].rootGroupLayerId,folder=addFolder(p,"Characters");Document d;d.load(p);
        d.moveLayers({root},folder);QCOMPARE(d.activeShot().layers[index(d.activeShot(),root)].parentId,folder);QCOMPARE(d.history()->count(),1);
        for(const auto &old:p.shots[0].layers)QCOMPARE(animationOwner(d.activeShot(),old.id),animationOwner(p.shots[0],old.id));
        d.history()->undo();QCOMPARE(d.project().toJson(),p.toJson());
    }
    void format35OnlyForNestedAnimationAndRoundTripRestoresArtwork() {
        Document d;d.load(fixture());const int previous=d.project().toJson()["version"].toInt();QCOMPARE(previous,34);
        QString folder,error;QVERIFY(addTimelineFolder(&d,{},"Characters",&folder));QCOMPARE(d.project().toJson()["version"].toInt(),previous);
        const auto root=d.activeShot().animationTracks[0].rootGroupLayerId;QVERIFY(moveTimelineLayer(&d,root,folder));const auto p=d.project();QCOMPARE(p.toJson()["version"].toInt(),35);
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto file=dir.filePath("nested.toon");QVERIFY2(saveProject(p,file,&error),qPrintable(error));QFile raw(file);QVERIFY(raw.open(QIODevice::ReadOnly));QCOMPARE(raw.read(10),QByteArray("TOONPRO35\n"));
        const auto restored=loadProject(file,&error);QVERIFY2(restored.has_value(),qPrintable(error));QCOMPARE(restored->toJson(),p.toJson());
        for(int frame:{0,2,4,6,19})QCOMPARE(renderMovieFrame(*restored,frame),renderMovieFrame(p,frame));
        auto downgraded=p.toJson();downgraded["version"]=34;QVERIFY(!Project::fromJson(downgraded,&error));QVERIFY(error.contains("35"));
        QVERIFY(moveTimelineLayer(&d,root,{}));QCOMPARE(d.project().toJson()["version"].toInt(),previous);
        auto ordinary=Project::create();const auto a=addFolder(ordinary,"A"),b=addFolder(ordinary,"B",a);QVERIFY(!b.isEmpty());QCOMPARE(ordinary.toJson()["version"].toInt(),19);
    }
    void nestedSymbolDefinitionsUseFormat35AndRejectDisguisedContainers() {
        auto p=Project::create();p.width=p.height=128;p.transparent=true;
        auto content=animation_fixture::project().shots[0];const auto root=content.animationTracks[0].rootGroupLayerId;
        const auto definition=animation_fixture::id();p.symbols.append({definition,"Character",content});
        auto instance=RasterLayer::create("Character instance");instance.kind=LayerKind::Symbol;instance.symbol=SymbolInstance{definition};p.shots[0].layers={instance};
        QVERIFY2(p.validate().isEmpty(),qPrintable(p.validate()));QCOMPARE(p.toJson()["version"].toInt(),31);
        const auto before=renderMovieFrame(p,0);auto folder=RasterLayer::create("Nested source folder");folder.group=true;
        auto &source=p.symbols[0].content;source.layers.append(folder);source.layers[index(source,root)].parentId=folder.id;
        QVERIFY2(p.validate().isEmpty(),qPrintable(p.validate()));QCOMPARE(p.toJson()["version"].toInt(),35);QCOMPARE(renderMovieFrame(p,0),before);
        QTemporaryDir dir;QVERIFY(dir.isValid());QString error;const auto file=dir.filePath("nested-symbol.toon");
        QVERIFY2(saveProject(p,file,&error),qPrintable(error));const auto restored=loadProject(file,&error);QVERIFY2(restored.has_value(),qPrintable(error));
        QCOMPARE(restored->toJson(),p.toJson());for(int frame:{0,2,4,6,19})QCOMPARE(renderMovieFrame(*restored,frame),renderMovieFrame(p,frame));
        const auto disguised=dir.filePath("disguised.toon");auto old=p.toJson();old["version"]=34;
        QVERIFY(!Project::fromJson(old,&error));QVERIFY2(error.contains("format 35"),qPrintable(error));
        QVERIFY(writeManifestContainer(disguised,old,34));QVERIFY(!loadProject(disguised,&error));QVERIFY2(error.contains("format 35"),qPrintable(error));
        QVERIFY(writeManifestContainer(disguised,p.toJson(),34));QVERIFY(!loadProject(disguised,&error));QVERIFY2(error.contains("container format"),qPrintable(error));
    }
    void legacyFolderDeletionCannotRemoveAnimationContainers() {
        auto p=fixture();const auto root=p.shots[0].animationTracks[0].rootGroupLayerId,folder=addFolder(p,"Characters");layer(p,root).parentId=folder;
        Document d;d.load(p);QSignalSpy warnings(&d,&Document::warning);const auto before=d.project().toJson();
        d.deleteLayers({folder});QCOMPARE(d.project().toJson(),before);QCOMPARE(d.history()->count(),0);QVERIFY(!warnings.isEmpty());
        d.duplicateLayers({folder});QCOMPARE(d.project().toJson(),before);QCOMPARE(d.history()->count(),0);
        d.deleteLayers({root});QCOMPARE(d.project().toJson(),before);QCOMPARE(d.history()->count(),0);
    }
    void invalidNestedTrackAncestryAndExcessDepthFailValidation() {
        auto p=animation_fixture::project();const auto root=p.shots[0].animationTracks[0].rootGroupLayerId;
        auto second=animation_fixture::project();const auto track=second.shots[0].animationTracks[0];p.shots[0].animationTracks.append(track);
        for(const auto &l:second.shots[0].layers)if(!animationOwner(second.shots[0],l.id).trackId.isEmpty())p.shots[0].layers.append(l);
        layer(p,root).parentId=track.rootGroupLayerId;QVERIFY(!p.validate().isEmpty());
        p=animation_fixture::project();const auto parent=addFolder(p,"Notes");layer(p,parent).annotation=true;layer(p,p.shots[0].animationTracks[0].rootGroupLayerId).parentId=parent;QVERIFY(!p.validate().isEmpty());
        p=animation_fixture::project();QString deep;
        for(int i=0;i<7;++i)deep=addFolder(p,QString::number(i),deep);
        Document d;d.load(p);QVERIFY(!moveTimelineLayer(&d,p.shots[0].animationTracks[0].rootGroupLayerId,deep));QCOMPARE(d.history()->count(),0);
    }
    void onionSamplesKeepNestedAncestorTransformsAndVisibility() {
        auto p=animation_fixture::project();p.transparent=true;auto &shot=p.shots[0];const auto track=shot.animationTracks[0];
        const auto folder=addFolder(p,"Characters");layer(p,track.rootGroupLayerId).parentId=folder;
        shot.layerAnimations={{folder,{},{{KeyframeProperty::PositionX,{{0,16},{19,16}}}}}};
        const auto samples=onionSkinSamples(shot,4,track.id,-1,1);QCOMPARE(samples.size(),1);
        const auto layers=onionSkinLayersForSample(p,shot,4,track.id,samples[0],{0,0,128,128});
        QVERIFY(std::any_of(layers.cbegin(),layers.cend(),[&](const auto &l){return l.id==folder;}));
        const auto image=composite(layers);QCOMPARE(image.pixelColor(1,64).alpha(),0);QCOMPARE(image.pixelColor(64,64),QColor(Qt::red));
        layer(p,folder).visible=false;QVERIFY(onionSkinSamples(shot,4,track.id,-1,1).isEmpty());
    }
    void activeEditsAndInvalidFolderNamesDoNotWrite() {
        Document d;d.load(fixture());QString created="old",error;QVERIFY(!addTimelineFolder(&d,{},"  ",&created,&error));QVERIFY(created.isEmpty());
        const auto root=d.activeShot().animationTracks[0].rootGroupLayerId;QVERIFY(d.beginLayerPropertyEdit("Draft"));
        QVERIFY(!moveTimelineLayer(&d,root,{}));QVERIFY(!addTimelineFolder(&d,{},"Folder"));d.cancelLayerPropertyEdit();QCOMPARE(d.history()->count(),0);
    }
};
QTEST_MAIN(TimelineOrganizationTests)
#include "test_timeline_organization.moc"
