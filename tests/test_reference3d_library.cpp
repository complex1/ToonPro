#include "reference3d_library.h"
#include "reference3d_builtin.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace toon;
namespace {
ReferenceScene smallScene() {
    ReferenceScene scene; scene.name = "Hand study";
    ReferenceInstance object; object.id = "hand"; object.name = "Hand";
    object.asset = createReferenceStarter(ReferenceStarterKind::Hand); scene.instances.append(object); return scene;
}
}
class ReferenceLibraryTests final : public QObject {
    Q_OBJECT
private slots:
    void materialsSurviveRestartAndSourceRemoval() {
        QTemporaryDir directory; const auto path = directory.filePath("materials"); QString error;
        ReferenceMaterialLibrary library(path); QVERIFY2(library.load(&error),qPrintable(error));
        auto source = smallScene(); const int finger = source.instances[0].asset->humanoidBones.value("leftIndexProximal");
        source.instances[0].poseRotations[finger] = QQuaternion::fromEulerAngles(45,0,0);
        const auto id = library.addScene(source,"Pointing hand",ReferenceLibraryKind::HandPose,&error);
        QVERIFY2(!id.isEmpty(),qPrintable(error));
        QVERIFY(library.setTags(id,{"Hands","Study"},&error)); QVERIFY(library.setFavorite(id,true,&error));
        const auto expected = serializeReferenceScene(source); source = {};
        ReferenceMaterialLibrary reopened(path); QVERIFY2(reopened.load(&error),qPrintable(error));
        QCOMPARE(reopened.entries().size(),1); QCOMPARE(reopened.entry(id)->tags,(QStringList{"Hands","Study"}));
        QVERIFY(reopened.entry(id)->favorite);
        const auto loaded = reopened.materialScene(id,&error); QVERIFY2(loaded,qPrintable(error));
        QCOMPARE(serializeReferenceScene(*loaded),expected);
        auto target = smallScene().instances.first(); target.translation={4,2,1};
        const auto assetId = target.asset->id;
        QVERIFY2(reopened.applyToInstance(id,target,&error),qPrintable(error));
        QCOMPARE(target.asset->id,assetId); QCOMPARE(target.translation,QVector3D(4,2,1));
        QVERIFY((target.poseRotations[finger].rotatedVector({0,1,0}) - QVector3D(0,1,0)).length() > .5f);
        QVERIFY(reopened.remove(id,&error)); QVERIFY(reopened.entries().isEmpty());
        QVERIFY(validateReferenceScene(*loaded).isEmpty());
    }
    void crossProportionPoseTransferKeepsShapeAndBindOrientation() {
        auto source = smallScene().instances.first(), target = source; QString error;
        const int sourceFinger=source.asset->humanoidBones.value("leftIndexProximal");
        source.poseRotations[sourceFinger]=QQuaternion::fromEulerAngles(35,0,10);
        QVERIFY(setReferenceBodyShape(target,"fingerLength",1.3f,&error));
        const auto shape=target.asset->id;
        QVERIFY2(applyReferencePose(source,target,true,&error),qPrintable(error));
        QCOMPARE(target.asset->id,shape);
        const int targetFinger=target.asset->humanoidBones.value("leftIndexProximal");
        QVERIFY(QQuaternion::dotProduct(source.poseRotations[sourceFinger],target.poseRotations[targetFinger]) > .999f);
    }
    void staleWritersAndCorruptPayloadsCannotReplaceValidMaterials() {
        QTemporaryDir directory; QString error;
        ReferenceMaterialLibrary first(directory.path()), stale(directory.path());
        QVERIFY(first.load(&error)); QVERIFY(stale.load(&error));
        const auto id=first.addScene(smallScene(),"Hand",ReferenceLibraryKind::Character,&error); QVERIFY2(!id.isEmpty(),qPrintable(error));
        auto staleScene=smallScene(); staleScene.name="A distinct stale payload";
        QVERIFY(stale.addScene(staleScene,"Stale",ReferenceLibraryKind::Character,&error).isEmpty());
        QVERIFY(error.contains("another window"));
        QCOMPARE(QDir(directory.path()).entryList({"*.t3dscene"},QDir::Files).size(),1);
        QVERIFY(first.rename(id,"Renamed hand",&error));
        QFile payload(QDir(directory.path()).filePath(first.entry(id)->contentHash+".t3dscene"));
        QVERIFY(payload.open(QIODevice::ReadWrite)); QVERIFY(payload.seek(payload.size()-1));
        const auto byte=payload.read(1); QVERIFY(payload.seek(payload.size()-1));
        QVERIFY(payload.write(QByteArray(1,char(byte[0]^1)))==1); payload.close();
        QVERIFY(!first.materialScene(id,&error)); QVERIFY(error.contains("checksum"));
        QCOMPARE(first.entry(id)->name,QString("Renamed hand"));
    }
    void shapeMaterialPreservesTargetPoseAndRejectsDifferentModelFamily() {
        QTemporaryDir directory; QString error; ReferenceMaterialLibrary library(directory.path()); QVERIFY(library.load());
        auto scene=smallScene(); QVERIFY(setReferenceBodyShape(scene.instances[0],"palmWidth",1.4f,&error));
        const auto id=library.addScene(scene,"Broad palm",ReferenceLibraryKind::HandShape,&error); QVERIFY2(!id.isEmpty(),qPrintable(error));
        auto target=smallScene().instances.first(); const int finger=target.asset->humanoidBones.value("leftIndexProximal");
        target.poseRotations[finger]=QQuaternion::fromEulerAngles(20,0,0);
        QVERIFY(library.applyToInstance(id,target,&error)); QCOMPARE(target.asset->id,scene.instances[0].asset->id);
        QVERIFY(target.poseRotations.contains(finger));
        target.asset=createReferenceStarter(ReferenceStarterKind::Head); target.poseRotations.clear();
        QVERIFY(!library.applyToInstance(id,target,&error));
    }
    void typedShapeKindsAndMalformedPoseInputsRejectBeforeMutation() {
        QTemporaryDir directory; QString error; ReferenceMaterialLibrary library(directory.path()); QVERIFY(library.load());
        auto scene=smallScene();
        QVERIFY(library.addScene(scene,"Wrong family",ReferenceLibraryKind::HeadShape,&error).isEmpty());
        QVERIFY(library.entries().isEmpty()); QVERIFY(QDir(directory.path()).entryList({"*.t3dscene"},QDir::Files).isEmpty());
        auto source=scene.instances.first(),target=source;
        target.poseRotations.insert(-1,QQuaternion()); const auto invalidPose=target.poseRotations;
        QVERIFY(!applyReferencePose(source,target,true,&error)); QCOMPARE(target.poseRotations,invalidPose);
        target=source; auto malformed=*source.asset; malformed.humanoidBones["leftHand"]=-1;
        source.asset=std::make_shared<const ReferenceAsset>(malformed);
        QVERIFY(!applyReferencePose(source,target,true,&error)); QVERIFY(target.poseRotations.isEmpty());
    }
    void removingMaterialsReclaimsOnlyUnreferencedLibraryOwnedPayloads() {
        QTemporaryDir directory; QString error; ReferenceMaterialLibrary library(directory.path()); QVERIFY(library.load());
        const auto scene=smallScene();
        const auto first=library.addScene(scene,"First",ReferenceLibraryKind::Character,&error); QVERIFY(!first.isEmpty());
        const auto second=library.addScene(scene,"Second",ReferenceLibraryKind::HandPose,&error); QVERIFY(!second.isEmpty());
        QCOMPARE(library.entry(first)->contentHash,library.entry(second)->contentHash);
        QCOMPARE(QDir(directory.path()).entryList({"*.t3dscene"},QDir::Files).size(),1);
        const auto placed=library.materialScene(first,&error); QVERIFY(placed);
        QVERIFY(library.remove(first,&error)); QVERIFY(library.materialScene(second,&error));
        QCOMPARE(QDir(directory.path()).entryList({"*.t3dscene"},QDir::Files).size(),1);
        QVERIFY(library.remove(second,&error));
        QVERIFY(QDir(directory.path()).entryList({"*.t3dscene"},QDir::Files).isEmpty());
        QVERIFY(validateReferenceScene(*placed).isEmpty());
    }
};
QTEST_MAIN(ReferenceLibraryTests)
#include "test_reference3d_library.moc"
