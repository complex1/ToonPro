#include "pose3d.h"
#include "scene3d.h"
#include "scene_model_io.h"
#include <QJsonArray>
#include <QTest>
#include <algorithm>
#include <cmath>
#include <limits>
using namespace toon;
namespace {
Rig3D chain(float first=1,float second=1){
    return {{{"a","A",{},{},{0,0,0}}, {"b","B","a",{},{first,0,0}}, {"c","C","b",{},{first+second,0,0}}},0};
}
bool near(QVector3D a,QVector3D b,float epsilon=.0001f){return (a-b).length()<epsilon;}
bool lengthIs(const PoseEvaluation3D &pose,int a,int b,float length){return std::abs((pose.positions[a]-pose.positions[b]).length()-length)<.0001f;}
Skin3D skinFor(const Model3D &model,const Rig3D &rig){
    Skin3D skin;for(const auto &joint:rig.joints)skin.jointIds.append(joint.id);
    for(const auto &mesh:model.meshes){SkinVertex3D v;v.weights[0]=.25f;v.joints[1]=1;v.weights[1]=.75f;
        skin.meshes.append(QVector<SkinVertex3D>(mesh.positions.size(),v));}
    return skin;
}
Model3DPtr triangle(QMatrix4x4 transform={}){
    Model3D model;model.name="Pose triangle";ModelMesh3D mesh;
    mesh.positions={{0,0,0},{1,0,0},{0,1,0}};mesh.normals=QVector<QVector3D>(3,{0,0,1});mesh.indices={0,1,2};mesh.transform=transform;
    model.meshes.append(mesh);return finalizeModel3D(std::move(model));
}
}
class Pose3DTest:public QObject{
    Q_OBJECT
private slots:
    void forwardKinematicsUsesLocalRotationsAndPreservesLengths(){
        const auto rig=chain();Pose3D pose;pose.rootTranslation={3,4,5};QString error;
        QVERIFY(setPoseJointRotation3D(pose,rig,"a",{0,0,90},false,&error));
        QVERIFY(setPoseJointRotation3D(pose,rig,"b",{0,0,-90},false,&error));
        const auto result=evaluatePose3D(rig,pose,&error);QVERIFY2(result,qPrintable(error));
        QVERIFY(near(result->positions[0],{3,4,5}));QVERIFY(near(result->positions[1],{3,5,5}));QVERIFY(near(result->positions[2],{4,5,5}));
        QVERIFY(lengthIs(*result,0,1,1));QVERIFY(lengthIs(*result,1,2,1));
        QVERIFY(near(result->skinMatrices[1].map({2,0,0}),{4,5,5}));
        auto unordered=rig;std::reverse(unordered.joints.begin(),unordered.joints.end());
        const auto reordered=evaluatePose3D(unordered,pose);QVERIFY(reordered);
        QCOMPARE(reordered->jointIds,QStringList({"c","b","a"}));QVERIFY(near(reordered->positions[0],result->positions[2]));
    }
    void fkAndIkRespectLimitsAndResetRetainsLimits(){
        const auto rig=chain();Pose3D pose;pose.limits.append({"a",{-15,-30,-45},{15,30,45},true});
        QVERIFY(setPoseJointRotation3D(pose,rig,"a",{90,-80,100}));QCOMPARE(poseJointRotation3D(pose,"a"),QVector3D(15,-30,45));
        pose.limits[0].enabled=false;QVERIFY(setPoseJointRotation3D(pose,rig,"a",{90,-80,100}));QCOMPARE(poseJointRotation3D(pose,"a"),QVector3D(90,-80,100));
        resetPose3D(pose);QVERIFY(pose3DIsIdentity(pose));QCOMPARE(pose.limits.size(),1);
        pose.limits={{"a",{0,0,0},{0,0,0},true},{"b",{0,0,0},{0,0,0},true}};
        QVERIFY(solvePoseIK3D(pose,rig,{"c",{0,2,0},{1,1,0},true}));const auto result=evaluatePose3D(rig,pose);QVERIFY(result);
        QVERIFY(near(result->positions[2],{2,0,0}));QVERIFY(lengthIs(*result,0,1,1));QVERIFY(lengthIs(*result,1,2,1));
    }
    void twoBoneIkReachesTargetsAndPoleChoosesBend(){
        const auto rig=chain();QString error;Pose3D pose;
        QVERIFY2(solvePoseIK3D(pose,rig,{"c",{1,1,0},{0,3,0},true},&error),qPrintable(error));
        const auto result=evaluatePose3D(rig,pose,&error);QVERIFY2(result,qPrintable(error));
        QVERIFY(near(result->positions[2],{1,1,0}));QVERIFY(near(result->positions[1],{0,1,0}));
        QVERIFY(lengthIs(*result,0,1,1));QVERIFY(lengthIs(*result,1,2,1));
        QVERIFY(solvePoseIK3D(pose,rig,{"c",{1,1,0},{3,0,0},true}));
        QVERIFY(near(evaluatePose3D(rig,pose)->positions[1],{1,0,0}));
        // A pin stays model-local when the root translates or FK changes.
        pose.rootTranslation={.25f,0,0};QVERIFY(setPoseJointRotation3D(pose,rig,"a",{25,20,60}));
        const auto pinned=evaluatePose3D(rig,pose);QVERIFY(pinned);QVERIFY(near(pinned->positions[2],{1,1,0}));
        QVERIFY(lengthIs(*pinned,0,1,1));QVERIFY(lengthIs(*pinned,1,2,1));
        pose.targets[0].pinned=false;QVERIFY(!near(evaluatePose3D(rig,pose)->positions[2],{1,1,0}));
    }
    void unreachableAndDegenerateIkAreBoundedAndAtomic(){
        Pose3D pose;const auto rig=chain();QString error;
        QVERIFY(solvePoseIK3D(pose,rig,{"c",{20,0,0},{0,1,0},true}));auto result=evaluatePose3D(rig,pose);QVERIFY(result);
        QVERIFY(near(result->positions[2],{2,0,0}));QVERIFY(lengthIs(*result,0,1,1));QVERIFY(lengthIs(*result,1,2,1));
        QVERIFY(solvePoseIK3D(pose,rig,{"c",{0,0,0},{0,0,0},true}));result=evaluatePose3D(rig,pose);QVERIFY(result);
        QVERIFY(near(result->positions[2],{}));QVERIFY(lengthIs(*result,0,1,1));QVERIFY(lengthIs(*result,1,2,1));
        Pose3D unequal;const auto longRig=chain(2,1);QVERIFY(solvePoseIK3D(unequal,longRig,{"c",{0,0,0},{0,1,0},true}));
        result=evaluatePose3D(longRig,unequal);QVERIFY(result);QVERIFY(std::abs(result->positions[2].length()-1)<.0001f);
        const auto before=pose;QVERIFY(!solvePoseIK3D(pose,rig,{"a",{},{}},&error));QCOMPARE(pose,before);
        auto zero=rig;zero.joints[1].position=zero.joints[0].position;
        QVERIFY(!solvePoseIK3D(pose,zero,{"c",{1,1,0},{}},&error));QVERIFY(error.contains("zero-length"));QCOMPARE(pose,before);
    }
    void releasingNestedPinKeepsTheEvaluatedParentFrame(){
        auto rig=chain();rig.joints.append({"d","D","c",{},{3,0,0}});
        rig.joints.append({"e","E","d",{},{4,0,0}});rig.joints.append({"f","F","e",{},{5,0,0}});
        Pose3D pose;QString error;
        QVERIFY(solvePoseIK3D(pose,rig,{"c",{1.4f,.7f,0},{0,2,0},true},&error));
        auto evaluated=evaluatePose3D(rig,pose);QVERIFY(evaluated);
        const auto target=evaluated->positions[3]+QVector3D(1,.8f,0);
        QVERIFY(solvePoseIK3D(pose,rig,{"f",target,target+QVector3D(0,2,0),true},&error));
        pose.rootTranslation={.15f,.1f,0};evaluated=evaluatePose3D(rig,pose);QVERIFY(evaluated);
        const auto before=*evaluated;
        auto released=pose.targets.last();released.pinned=false;QVERIFY2(solvePoseIK3D(pose,rig,released,&error),qPrintable(error));
        const auto after=evaluatePose3D(rig,pose);QVERIFY(after);
        for(int i=0;i<rig.joints.size();++i)QVERIFY2(near(after->positions[i],before.positions[i],.0003f),qPrintable(rig.joints[i].id));
    }
    void mirrorExchangesPairedRotationsTargetsAndTranslation(){
        Rig3D rig;rig.mirrorPlaneX=2;
        rig.joints={{"la","LA",{},"ra",{1,0,0}},{"lb","LB","la","rb",{0,0,0}},{"lc","LC","lb","rc",{-1,0,0}},
                    {"ra","RA",{},"la",{3,0,0}},{"rb","RB","ra","lb",{4,0,0}},{"rc","RC","rb","lc",{5,0,0}}};
        Pose3D pose;pose.rootTranslation={.25f,.5f,1};QString error;
        QVERIFY(setPoseJointRotation3D(pose,rig,"la",{15,20,30},true));QCOMPARE(poseJointRotation3D(pose,"ra"),QVector3D(15,-20,-30));
        pose.targets.append({"lc",{0,1,0},{0,1,1},false});const auto original=pose;
        QVERIFY2(mirrorPose3D(pose,rig,&error),qPrintable(error));
        QCOMPARE(pose.rootTranslation,QVector3D(-.25f,.5f,1));QCOMPARE(pose.targets[0].jointId,QString("rc"));
        QCOMPARE(pose.targets[0].target,QVector3D(4,1,0));
        QVERIFY(mirrorPose3D(pose,rig));QCOMPARE(pose,original);
    }
    void strictValidationRejectsUnknownNonfiniteCyclesAndConflictingPins(){
        const auto rig=chain();Pose3D pose;pose.joints={{"missing",{}}};QVERIFY(!validatePose3D(pose,rig).isEmpty());
        pose.joints={{"a",{}},{"a",{}}};QVERIFY(validatePose3D(pose,rig).contains("duplicate"));
        pose.joints={{"a",{0,181,0}}};QVERIFY(!validatePose3D(pose,rig).isEmpty());
        pose={};pose.rootTranslation.setX(std::numeric_limits<float>::infinity());QVERIFY(!evaluatePose3D(rig,pose));
        pose={};pose.limits={{"a",{1,-20,-20},{20,20,20},true}};QVERIFY(validatePose3D(pose,rig).contains("zero-angle"));
        auto cycle=rig;cycle.joints[0].parentId="c";QVERIFY(!evaluatePose3D(cycle,{}));
        auto branched=rig;branched.joints.append({"d","D","b",{},{1,1,0}});
        pose={};pose.targets={{"c",{1,1,0},{0,1,0},true},{"d",{1,1,0},{0,1,0},true}};
        QVERIFY(validatePose3D(pose,branched).contains("same joint"));
        pose.targets[1].pinned=false;QVERIFY(validatePose3D(pose,branched).isEmpty());
    }
    void deformationPreservesSourcesTransformsAndConservativeBounds(){
        QMatrix4x4 transform;transform.translate(4,2,0);transform.scale(2,3,1);
        const auto model=triangle(transform);QVERIFY(model);const auto original=serializeModel3D(*model);
        const auto rig=chain();const auto skin=skinFor(*model,rig);QString error;
        const auto identity=deformPosedModel3D(*model,rig,skin,{},&error);QVERIFY2(identity,qPrintable(error));QCOMPARE(serializeModel3D(*identity),original);
        Pose3D pose;pose.rootTranslation={1,2,0};QVERIFY(setPoseJointRotation3D(pose,rig,"a",{0,0,90}));
        const auto deformed=deformPosedModel3D(*model,rig,skin,pose,&error);QVERIFY2(deformed,qPrintable(error));
        QCOMPARE(deformed->meshes[0].transform,transform);QCOMPARE(serializeModel3D(*model),original);
        const auto expected=QVector3D(-1,6,0);QVERIFY(near(transform.map(deformed->meshes[0].positions[0]),expected));
        const auto bounds=posedModelBounds3D(*model,rig,pose,&error);QVERIFY2(bounds.valid,qPrintable(error));
        for(const auto &local:deformed->meshes[0].positions){const auto point=transform.map(local);
            for(int axis=0;axis<3;++axis){QVERIFY(point[axis]>=bounds.minimum[axis]-.0001f);QVERIFY(point[axis]<=bounds.maximum[axis]+.0001f);}}
        Scene3D scene;scene.models.insert(model->id,model);ModelInstance3D object;object.id="o";object.name="O";object.modelId=model->id;
        object.rig=rig;object.skin=skin;object.pose=pose;object.position={10,20,30};object.scale={2,3,4};scene.objects.append(object);
        const auto world=sceneBounds(scene);QVERIFY(world.valid);
        for(const auto &local:deformed->meshes[0].positions){const auto point=instanceMatrix(object).map(transform.map(local));
            for(int axis=0;axis<3;++axis){QVERIFY(point[axis]>=world.minimum[axis]-.001f);QVERIFY(point[axis]<=world.maximum[axis]+.001f);}}
    }
    void poseJsonRoundTripAndMalformedPayloads(){
        const auto rig=chain();Pose3D pose;pose.rootTranslation={1,2,3};pose.joints={{"a",{10,20,30}}};
        pose.limits={{"b",{-30,-40,-50},{30,40,50},true}};pose.targets={{"c",{2,3,4},{4,3,2},false}};
        QString error;const auto json=pose3DToJson(pose);const auto roundTrip=pose3DFromJson(json,rig,&error);QVERIFY2(roundTrip,qPrintable(error));QCOMPARE(*roundTrip,pose);
        auto bad=json;bad["unknown"]=true;QVERIFY(!pose3DFromJson(bad,rig));
        bad=json;bad["rootTranslation"]=QJsonArray{1,2};QVERIFY(!pose3DFromJson(bad,rig));
        bad=json;auto targets=bad["targets"].toArray();auto target=targets[0].toObject();target["pinned"]=1;targets[0]=target;bad["targets"]=targets;QVERIFY(!pose3DFromJson(bad,rig));
        QVERIFY(pose3DByteSize(pose)>pose3DByteSize({}));QVERIFY(pose3DCacheKey(pose)!=pose3DCacheKey({}));
    }
};
QTEST_GUILESS_MAIN(Pose3DTest)
#include "test_pose3d.moc"
