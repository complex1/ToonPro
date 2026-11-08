#include "reference3d_pets.h"
#include "reference3d_catalog.h"
#include <QtTest>
#include <cmath>
using namespace toon;
namespace {
ReferenceInstance pet(const QString&kind){ReferenceInstance i;i.id="pet-study";i.name="Pet study";i.asset=createReferenceCatalogAsset("pet/"+kind);return i;}
float displacement(const ReferenceEvaluation&a,const ReferenceEvaluation&b){float d=0;for(int p=0;p<a.primitives.size();++p)for(int v=0;v<a.primitives[p].vertices.size();++v)d=std::max(d,(a.primitives[p].vertices[v].position-b.primitives[p].vertices[v].position).length());return d;}
}
class ReferencePetTests final:public QObject {
 Q_OBJECT
private slots:
 void recognizableAnimalAssetsHaveNativeRigsAndBoundedGeometry(){
  for(const QString kind:{QString("dog"),QString("cat"),QString("bird")}){auto i=pet(kind);QVERIFY2(i.asset,qPrintable(kind));QCOMPARE(i.asset->metadata.value("referencePetKind").toString(),kind);QCOMPARE(i.asset->metadata.value("referenceCatalogId").toString(),"pet/"+kind);QVERIFY(i.asset->humanoidBones.isEmpty());QVERIFY(!i.asset->skins.isEmpty());QVERIFY(i.asset->skins[0].joints.size()>=20);
   int vertices=0,triangles=0;for(const auto&m:i.asset->meshes)for(const auto&p:m.primitives){vertices+=p.vertices.size();triangles+=p.indices.size()/3;}QVERIFY(vertices>250&&vertices<5000);QVERIFY(triangles>500&&triangles<2000);
   bool head=false;for(const auto&n:i.asset->nodes)head|=n.name=="Head";QVERIFY(head);QString error;const auto e=evaluateReferenceInstance(i,&error);QVERIFY2(e,qPrintable(error));QVERIFY(e->bounds.valid);QVERIFY((e->bounds.maximum-e->bounds.minimum).length()>.2f);
   const auto poses=referencePetPosePresets(i);QCOMPARE(poses.size(),kind=="bird"?5:4);for(const auto&p:poses){QCOMPARE(p.kind,kind);QVERIFY(p.id.startsWith("pet/"+kind+"/"));}
  }
 }
 void authoredSnapshotsDeformTheMatchingAnimalAndPreservePlacement(){
  for(const QString kind:{QString("dog"),QString("cat"),QString("bird")}){auto i=pet(kind);QVERIFY(i.asset);i.translation={3,2,-1};i.rotation=QQuaternion::fromEulerAngles(4,30,2);i.scale={1.1f,.9f,1.2f};const auto original=i;const auto before=evaluateReferenceInstance(i);QVERIFY(before);int changed=0;
   for(const auto&p:referencePetPosePresets(i)){QString error;QVERIFY2(applyReferencePetPosePreset(p.id,i,&error),qPrintable(error));QCOMPARE(i.asset,original.asset);QCOMPARE(i.translation,original.translation);QCOMPARE(i.rotation,original.rotation);QCOMPARE(i.scale,original.scale);const auto e=evaluateReferenceInstance(i,&error);QVERIFY2(e,qPrintable(error));changed+=displacement(*before,*e)>.0001f;}
   QVERIFY(changed>=2);
  }
 }
 void wrongSpeciesAndEditedRigsRejectAtomically(){
  auto dog=pet("dog");QVERIFY(dog.asset);const auto before=dog;QString error;QVERIFY(!applyReferencePetPosePreset("pet/cat/standing",dog,&error));QVERIFY(!error.isEmpty());QCOMPARE(dog.poseRotations,before.poseRotations);QCOMPARE(dog.poseTranslations,before.poseTranslations);dog.locked=true;QVERIFY(!applyReferencePetPosePreset("pet/dog/walking-left",dog,&error));QVERIFY(dog.poseRotations.isEmpty());dog.locked=false;
  auto edited=*dog.asset;edited.nodes[0].translation+=QVector3D(.01f,0,0);dog.asset=finalizeReferenceAsset(std::move(edited),&error);QVERIFY(dog.asset);QVERIFY(referencePetPosePresets(dog).isEmpty());QVERIFY(!applyReferencePetPosePreset("pet/dog/standing",dog,&error));QVERIFY(dog.poseRotations.isEmpty());
 }
 void posesPersistAndNativeFkChangesTheMesh(){
  auto i=pet("cat");QVERIFY(i.asset);QString error;QVERIFY(applyReferencePetPosePreset("pet/cat/walking-left",i,&error));ReferenceScene scene;scene.name="Animal study";scene.instances={i};const auto bytes=serializeReferenceScene(scene,&error);QVERIFY2(!bytes.isEmpty(),qPrintable(error));const auto loaded=deserializeReferenceScene(bytes,&error);QVERIFY2(loaded,qPrintable(error));QCOMPARE(serializeReferenceScene(*loaded,&error),bytes);QCOMPARE(referencePetPosePresets(loaded->instances[0]).size(),4);
  i=pet("bird");const auto before=evaluateReferenceInstance(i);QVERIFY(before);int head=-1;for(int n=0;n<i.asset->nodes.size();++n)if(i.asset->nodes[n].name=="Head")head=n;QVERIFY(head>=0);i.poseRotations[head]=(i.asset->nodes[head].rotation*QQuaternion::fromAxisAndAngle(1,0,0,30)).normalized();const auto after=evaluateReferenceInstance(i,&error);QVERIFY2(after,qPrintable(error));QVERIFY(displacement(*before,*after)>.005f);
 }
 void eagleUsesARaisedWingFlightDefaultAndHonestSourcePoseNames(){
  auto bird=pet("bird");QVERIFY(bird.asset);QString error;const auto original=evaluateReferenceInstance(bird,&error);QVERIFY2(original,qPrintable(error));
  const auto poses=referencePetPosePresets(bird);QCOMPARE(poses.first().id,QString("pet/bird/flying-up"));
  bool lowered=false,study=false;
  for(const auto&p:poses){QVERIFY(!p.id.contains("perched"));QVERIFY(!p.name.contains("perched",Qt::CaseInsensitive));lowered|=p.id=="pet/bird/wings-lowered";study|=p.id=="pet/bird/wings-lowered-study";}
  QVERIFY(lowered&&study);QVERIFY(original->bounds.maximum.y()-original->bounds.minimum.y()>.25f);
  auto raised=bird;QVERIFY2(applyReferencePetPosePreset("pet/bird/flying-up",raised,&error),qPrintable(error));const auto raisedMesh=evaluateReferenceInstance(raised,&error);QVERIFY2(raisedMesh,qPrintable(error));QVERIFY(displacement(*original,*raisedMesh)<.00001f);
  auto gliding=bird;QVERIFY2(applyReferencePetPosePreset("pet/bird/flying-spread",gliding,&error),qPrintable(error));const auto spread=evaluateReferenceInstance(gliding,&error);QVERIFY2(spread,qPrintable(error));QVERIFY(std::abs(spread->bounds.maximum.x()-spread->bounds.minimum.x()-1.2f)<.001f);
 }
};
QTEST_MAIN(ReferencePetTests)
#include "test_reference3d_pets.moc"
