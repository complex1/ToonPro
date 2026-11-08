#include "document.h"
#include "managed_project_store.h"
#include "reference3d_builtin.h"
#include "reference3d_layer_serialization.h"
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>

using namespace toon;
namespace {
ReferenceScene study(){
    ReferenceScene scene;scene.name="Head study";ReferenceInstance model;model.id="head-study";model.name="Head";
    model.asset=createReferenceStarter(ReferenceStarterKind::Head);scene.instances={model};scene.camera.orthographic=true;scene.camera.target={0,.2f,0};scene.camera.orthoScale=.65f;
    scene.light.color=QColor::fromRgbF(.1234f,.5678f,.2468f,1.f);
    QString error;setReferenceExpression(scene.instances[0],"Smile",.65f,&error);return scene;
}
Project drawing(){auto p=newManagedDocument("Reference drawing",false,320,240,Qt::white,false);return p;}
const RasterLayer *find(const Project&p,const QString&id){for(const auto &l:p.shots[0].layers)if(l.id==id)return &l;return nullptr;}
}
class ReferenceLayerStorageTests final:public QObject{
    Q_OBJECT
private slots:
    void insertionGestureAndUndoPreservePaintAndSource(){
        auto p=drawing();QImage pixels(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);pixels.fill(QColor(30,40,50));p.shots[0].layers[0].tiles.insert({0,0},pixels);
        const auto paintId=p.shots[0].layers[0].id;Document doc;doc.load(p);QString error;const auto scene=study();QVERIFY(scene.instances[0].asset);
        const auto id=doc.addReference3DLayer(scene,"Pose reference",&error);QVERIFY2(!id.isEmpty(),qPrintable(error));QCOMPARE(doc.history()->count(),1);
        const auto *layer=find(doc.project(),id);QVERIFY(layer);QCOMPARE(layer->kind,LayerKind::Reference3D);QVERIFY(layer->tiles.isEmpty());QCOMPARE(layer->reference3d->instances[0].asset.get(),scene.instances[0].asset.get());
        QVERIFY(doc.beginReference3DEdit(id,&error));QVERIFY(doc.reference3DEditActive());QVERIFY(doc.editingGestureActive());
        auto posed=scene;for(int i=1;i<=20;++i){posed.camera.yaw=float(i*3);posed.instances[0].rotation=QQuaternion::fromEulerAngles(0,float(i),0);QVERIFY2(doc.previewReference3DScene(posed,&error),qPrintable(error));}
        QCOMPARE(doc.history()->count(),1);doc.commitReference3DEdit();QVERIFY(!doc.reference3DEditActive());QCOMPARE(doc.history()->count(),2);
        QVERIFY(referenceLayerScenesEqual(*find(doc.project(),id)->reference3d,posed));QCOMPARE(find(doc.project(),paintId)->tiles.value({0,0}),pixels);
        doc.history()->undo();QVERIFY(referenceLayerScenesEqual(*find(doc.project(),id)->reference3d,scene));doc.history()->redo();QVERIFY(referenceLayerScenesEqual(*find(doc.project(),id)->reference3d,posed));
        QVERIFY(doc.beginReference3DEdit(id));posed.camera.yaw=130;QVERIFY(doc.previewReference3DScene(posed));doc.cancelReference3DEdit();QCOMPARE(doc.history()->count(),2);QVERIFY(!referenceLayerScenesEqual(*find(doc.project(),id)->reference3d,posed));
        doc.history()->undo();doc.history()->undo();QVERIFY(!find(doc.project(),id));QCOMPARE(find(doc.project(),paintId)->tiles.value({0,0}),pixels);doc.history()->redo();QVERIFY(find(doc.project(),id));
    }
    void projectsSerializePortableSharedModelsOnce(){
        Document doc;doc.load(drawing());QString error;const auto first=doc.addReference3DLayer(study(),{},&error);QVERIFY2(!first.isEmpty(),qPrintable(error));
        auto other=study();other.instances[0].translation={.2f,0,.1f};const auto second=doc.addReference3DLayer(other,{},&error);QVERIFY2(!second.isEmpty(),qPrintable(error));
        const auto json=doc.project().toJson();QCOMPARE(json.value("version").toInt(),41);QCOMPARE(json.value("reference3DAssets").toArray().size(),1);
        const auto parsed=Project::fromJson(json,&error);QVERIFY2(parsed,qPrintable(error));QCOMPARE(parsed->validate(),QString());
        QCOMPARE(find(*parsed,first)->reference3d->instances[0].asset.get(),find(*parsed,second)->reference3d->instances[0].asset.get());
        QCOMPARE(serializeReferenceScene(*find(*parsed,first)->reference3d),serializeReferenceScene(*find(doc.project(),first)->reference3d));
        QTemporaryDir directory;const auto path=directory.filePath("reference.toon");QVERIFY2(saveProject(doc.project(),path,&error),qPrintable(error));const auto loaded=loadProject(path,&error);QVERIFY2(loaded,qPrintable(error));
        QCOMPARE(loaded->toJson(),doc.project().toJson());
        auto store=ManagedProjectStore::create(directory.filePath("Study.toonstudio"),"Study","drawing",&error);QVERIFY2(store,qPrintable(error));
        const auto item=store->createDocument("Study","drawing",doc.project(),{},{},&error);QVERIFY2(!item.isEmpty(),qPrintable(error));const auto managed=store->loadDocument(item,false,&error);QVERIFY2(managed,qPrintable(error));
        QCOMPARE(serializeReferenceScene(*find(*managed,second)->reference3d),serializeReferenceScene(other));
        auto changed=*managed;for(auto &layer:changed.shots[0].layers)if(layer.id==second)layer.reference3d->camera.yaw=123;
        const auto revision=store->items(&error).first().revision;const auto savedRevision=store->saveDocument(item,revision,changed,false,&error);QVERIFY2(!savedRevision.isEmpty(),qPrintable(error));
        auto recovery=changed;for(auto &layer:recovery.shots[0].layers)if(layer.id==second)layer.reference3d->camera.yaw=154;
        QVERIFY2(!store->saveDocument(item,savedRevision,recovery,true,&error).isEmpty(),qPrintable(error));
        store.reset();store=ManagedProjectStore::open(directory.filePath("Study.toonstudio"),&error);QVERIFY2(store,qPrintable(error));
        const auto reopened=store->loadDocument(item,false,&error),recovered=store->loadDocument(item,true,&error);QVERIFY2(reopened,qPrintable(error));QVERIFY2(recovered,qPrintable(error));
        QCOMPARE(find(*reopened,second)->reference3d->camera.yaw,123.f);QCOMPARE(find(*recovered,second)->reference3d->camera.yaw,154.f);
        QCOMPARE(find(*recovered,second)->reference3d->instances[0].asset->id,other.instances[0].asset->id);
    }
    void lockedAndInvalidChangesAreAtomicAndDuplicateKeepsSource(){
        Document doc;doc.load(drawing());QString error;const auto id=doc.addReference3DLayer(study(),{},&error);QVERIFY2(!id.isEmpty(),qPrintable(error));const auto source=doc.project().toJson();
        auto invalid=study();invalid.camera.distance=-3;QVERIFY(!doc.setReference3DScene(id,invalid,&error));QVERIFY(!error.isEmpty());QCOMPARE(doc.project().toJson(),source);
        doc.duplicateLayers({id});QCOMPARE(doc.project().shots[0].layers.size(),3);const auto copy=doc.project().shots[0].layers[doc.activeLayer()];QVERIFY(copy.id!=id);QVERIFY(copy.reference3d);QCOMPARE(copy.reference3d->instances[0].asset.get(),find(doc.project(),id)->reference3d->instances[0].asset.get());
        doc.setLayersLocked({copy.id},true);QVERIFY(!doc.beginReference3DEdit(copy.id,&error));QVERIFY(!doc.setReference3DScene(copy.id,study(),&error));QVERIFY(!doc.reference3DEditActive());
        doc.setLayersLocked({copy.id},false);QVERIFY(doc.beginReference3DEdit(copy.id));doc.load(drawing());QVERIFY(!doc.reference3DEditActive());
    }
    void corruptResourcesAndMismatchedKindsReject(){
        Document doc;doc.load(drawing());QVERIFY(!doc.addReference3DLayer(study()).isEmpty());const auto original=doc.project().toJson();QString error;
        auto damaged=original;auto assets=damaged.value("reference3DAssets").toArray();auto asset=assets[0].toObject();asset["data"]="AAAA";assets[0]=asset;damaged["reference3DAssets"]=assets;QVERIFY(!Project::fromJson(damaged,&error));QVERIFY(!error.isEmpty());
        damaged=original;damaged["reference3DAssets"]=QJsonArray{};QVERIFY(!Project::fromJson(damaged,&error));
        damaged=original;damaged["version"]=40;QVERIFY(!Project::fromJson(damaged,&error));
        auto p=doc.project();p.shots[0].layers.last().kind=LayerKind::Raster;QVERIFY(!p.validate().isEmpty());
    }
};
QTEST_MAIN(ReferenceLayerStorageTests)
#include "test_reference3d_layer_storage.moc"
