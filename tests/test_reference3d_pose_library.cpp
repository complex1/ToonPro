#include "reference3d_pose_library.h"
#include "reference3d_builtin.h"
#include <QSet>
#include <QtTest>
#include <algorithm>
#include <cmath>

using namespace toon;
namespace {
ReferenceInstance starter(ReferenceStarterKind kind=ReferenceStarterKind::Figure) {
    ReferenceInstance result; result.id="pose-study"; result.name="Original pose study"; result.asset=createReferenceStarter(kind); return result;
}
QByteArray snapshot(const ReferenceInstance &instance) {
    ReferenceScene scene; scene.instances.append(instance); return serializeReferenceScene(scene);
}
QVector3D joint(const ReferenceInstance &instance,const QString &semantic) {
    const auto globals=evaluateReferenceNodeGlobals(instance);
    return globals->at(instance.asset->humanoidBones.value(semantic)).map(QVector3D());
}
QVector3D direction(const ReferenceInstance &instance,const QString &first,const QString &second) {
    return (joint(instance,second)-joint(instance,first)).normalized();
}
ReferenceAssetPtr reorientedBind(const ReferenceAssetPtr &source,QString *error) {
    // The geometry and every joint position stay unchanged, but every local
    // bone frame differs. A local-Euler preset would fail this fixture.
    ReferenceInstance rest; rest.id="rebound-fixture"; rest.name="Rebound fixture"; rest.asset=source;
    const auto globals=evaluateReferenceNodeGlobals(rest,error); if (!globals) return {};
    auto asset=*source; QVector<QQuaternion> desired(asset.nodes.size()); QVector<QMatrix4x4> rebound(asset.nodes.size());
    for (int i=0;i<asset.nodes.size();++i) {
        auto &node=asset.nodes[i]; const int parent=node.parentIndex;
        if (node.meshIndex<0) desired[i]=QQuaternion::fromEulerAngles(float(13+(i%5)*7),float(-27+(i%7)*9),float(19+(i%3)*11));
        const auto parentRotation=parent<0 ? QQuaternion() : desired[parent];
        const auto position=globals->at(i).map(QVector3D());
        const auto parentPosition=parent<0 ? QVector3D() : globals->at(parent).map(QVector3D());
        node.translation=parentRotation.conjugated().rotatedVector(position-parentPosition);
        node.rotation=(parentRotation.conjugated()*desired[i]).normalized();
        rebound[i].translate(position); rebound[i].rotate(desired[i]);
    }
    for (auto &skin:asset.skins) for (int i=0;i<skin.joints.size();++i) skin.inverseBindMatrices[i]=rebound[skin.joints[i]].inverted();
    return finalizeReferenceAsset(std::move(asset),error);
}
}
class ReferencePoseLibraryTests final : public QObject {
    Q_OBJECT
private slots:
    void catalogIsOriginalBoundedAndEveryRecipeDeformsTheMesh() {
        const auto presets=referencePosePresets(); QVERIFY(presets.size()>=25 && presets.size()<=48);
        QSet<QString> ids,categories; QSet<QByteArray> bodyPoses,handPoses; QString error;
        auto figure=starter(),hand=starter(ReferenceStarterKind::Hand); QVERIFY(figure.asset); QVERIFY(hand.asset);
        const auto originalHand=evaluateReferenceInstance(hand); QVERIFY(originalHand);
        for (const auto &preset:presets) {
            QVERIFY(!preset.id.isEmpty()); QVERIFY(!preset.name.isEmpty()); QVERIFY(!preset.category.isEmpty());
            QVERIFY2(!ids.contains(preset.id),qPrintable(preset.id)); ids.insert(preset.id);
            auto instance=preset.handsOnly ? hand : figure;
            QVERIFY2(applyReferencePosePreset(preset.id,instance,&error),qPrintable(preset.id+": "+error));
            QVERIFY2(validateReferenceInstance(instance).isEmpty(),qPrintable(preset.id));
            const auto evaluated=evaluateReferenceInstance(instance,&error); QVERIFY2(evaluated,qPrintable(error)); QVERIFY(evaluated->bounds.valid);
            QByteArray fingerprint;
            for (const auto &primitive:evaluated->primitives) for (const auto &vertex:primitive.vertices) {
                const auto p=vertex.position; QVERIFY(std::isfinite(p.x()) && std::isfinite(p.y()) && std::isfinite(p.z()));
                fingerprint+=QByteArray::number(qRound(p.x()*10000))+','+QByteArray::number(qRound(p.y()*10000))+','+QByteArray::number(qRound(p.z()*10000))+';';
            }
            auto &poses=preset.handsOnly ? handPoses : bodyPoses;
            QVERIFY2(!poses.contains(fingerprint),qPrintable("Indistinguishable mesh for "+preset.id)); poses.insert(fingerprint);
            if (!preset.handsOnly) categories.insert(preset.category);
        }
        QCOMPARE(bodyPoses.size(),20); QCOMPARE(handPoses.size(),11);
        for (const auto &category:{"Standing","Seated","Ground","Movement","Balance","Reach","Action"}) QVERIFY(categories.contains(category));
    }
    void landmarksDescribeActualAnatomicalDirectionsAndGroundedBounds() {
        QString error; auto standing=starter();
        QVERIFY2(applyReferencePosePreset("body/relaxed-standing",standing,&error),qPrintable(error));
        QVERIFY(direction(standing,"leftUpperArm","leftLowerArm").y()<-.9f);
        QVERIFY(direction(standing,"rightUpperArm","rightLowerArm").y()<-.9f);
        auto hips=starter(); QVERIFY(applyReferencePosePreset("body/hands-on-hips",hips,&error));
        QVERIFY(std::abs(joint(hips,"leftHand").y()-joint(hips,"hips").y())<.2f);
        QVERIFY(std::abs(joint(hips,"rightHand").y()-joint(hips,"hips").y())<.2f);
        auto seated=starter(); QVERIFY2(applyReferencePosePreset("body/seated-chair",seated,&error),qPrintable(error));
        for (const auto &side:{QString("left"),QString("right")}) {
            const auto thigh=direction(seated,side+"UpperLeg",side+"LowerLeg");
            QVERIFY(std::abs(thigh.y())<.06f); QVERIFY(thigh.z()>.98f);
            QVERIFY(direction(seated,side+"LowerLeg",side+"Foot").y()<-.99f);
        }
        auto reach=starter(); QVERIFY(applyReferencePosePreset("body/reach-up",reach,&error));
        QVERIFY(joint(reach,"leftHand").y()>joint(reach,"head").y());
        QVERIFY(joint(reach,"rightHand").y()>joint(reach,"head").y());
        auto crouch=starter(); QVERIFY(applyReferencePosePreset("body/crouch",crouch,&error));
        QVERIFY(joint(crouch,"hips").y()<joint(standing,"hips").y()*.6f);
        auto kneel=starter(); QVERIFY(applyReferencePosePreset("body/kneel",kneel,&error));
        QVERIFY(direction(kneel,"leftLowerLeg","leftFoot").z()<-.99f);
        QVERIFY(direction(kneel,"leftFoot","leftToes").z()<-.9f);
        auto lying=starter(); QVERIFY(applyReferencePosePreset("body/lying-back",lying,&error));
        const auto bounds=evaluateReferenceInstance(lying)->bounds;
        QVERIFY(bounds.maximum.z()-bounds.minimum.z()>bounds.maximum.y()-bounds.minimum.y());
        const auto restBounds=evaluateReferenceInstance(starter())->bounds;
        QVERIFY(std::abs(bounds.minimum.y()-restBounds.minimum.y())<.0001f);
        auto open=starter(ReferenceStarterKind::Hand),fist=open;
        QVERIFY(applyReferencePosePreset("hands/open",open,&error)); QVERIFY(applyReferencePosePreset("hands/fist",fist,&error));
        QVERIFY((joint(open,"leftMiddleDistal")-joint(fist,"leftMiddleDistal")).length()>.035f);
    }
    void authoredThumbGesturesHaveUsefulTipDirectionsAndApproximateContact() {
        QString error; auto hand=starter(ReferenceStarterKind::Hand),rest=hand;
        QVERIFY(applyReferencePosePreset("hands/thumbs-up",hand,&error));
        QVERIFY(QVector3D::dotProduct(direction(hand,"leftThumbProximal","leftThumbDistal"),direction(rest,"leftThumbProximal","leftThumbDistal"))>.999f);
        for (const auto &id:{"hands/pinch","hands/ok"}) {
            hand=rest; hand.poseScales[hand.asset->humanoidBones.value("leftHand")]={1.5f,1.5f,1.5f};
            QVERIFY2(applyReferencePosePreset(id,hand,&error),qPrintable(error));
            const auto globals=evaluateReferenceNodeGlobals(hand),restGlobals=evaluateReferenceNodeGlobals(rest);
            const auto tip=[&](const QString &finger) {
                const auto middle=hand.asset->humanoidBones.value("left"+finger+(finger=="Thumb" ? "Proximal" : "Intermediate"));
                const auto distal=hand.asset->humanoidBones.value("left"+finger+"Distal");
                const auto local=restGlobals->at(distal).inverted().mapVector(restGlobals->at(distal).map(QVector3D())-restGlobals->at(middle).map(QVector3D()));
                return globals->at(distal).map(QVector3D())+globals->at(distal).mapVector(local)*.70f;
            };
            QVERIFY2((tip("Thumb")-tip("Index")).length()<.002f,id);
        }
    }
    void importedRestFramesDoNotChangeGestureAnatomy() {
        QString error; auto canonical=starter(),foreign=canonical; foreign.asset=reorientedBind(canonical.asset,&error); QVERIFY2(foreign.asset,qPrintable(error));
        const auto a=evaluateReferenceInstance(canonical),b=evaluateReferenceInstance(foreign); QVERIFY(a && b);
        QVERIFY((a->bounds.minimum-b->bounds.minimum).length()<.00001f);
        for (const auto &id:{"body/relaxed-standing","body/seated-chair","body/reach-up","body/run-stride"}) {
            auto posed=canonical,retargeted=foreign;
            QVERIFY2(applyReferencePosePreset(id,posed,&error),qPrintable(error));
            QVERIFY2(applyReferencePosePreset(id,retargeted,&error),qPrintable(error));
            for (const auto &semantic:{"leftLowerArm","leftHand","rightHand","leftLowerLeg","leftFoot","rightFoot","leftMiddleDistal"})
                QVERIFY2((joint(posed,semantic)-joint(retargeted,semantic)).length()<.001f,semantic);
        }
        canonical=starter(ReferenceStarterKind::Hand); foreign=canonical; foreign.asset=reorientedBind(canonical.asset,&error); QVERIFY(foreign.asset);
        QVERIFY(applyReferencePosePreset("hands/fist",canonical,&error)); QVERIFY2(applyReferencePosePreset("hands/fist",foreign,&error),qPrintable(error));
        for (const auto &finger:{"Thumb","Index","Middle","Ring","Little"})
            QVERIFY((joint(canonical,QString("left")+finger+"Distal")-joint(foreign,QString("left")+finger+"Distal")).length()<.0001f);
    }
    void pinchAndOKPreservePalmWebWithoutExtremeMetacarpalRotation() {
        QString error;
        for (bool right:{false,true}) {
            auto rest=starter(ReferenceStarterKind::Hand);
            if (right) QVERIFY(setReferenceBodyShape(rest,"handedness",1,&error));
            const QString side=right ? "right" : "left";
            const int base=rest.asset->humanoidBones.value(side+"ThumbMetacarpal"),proximal=rest.asset->humanoidBones.value(side+"ThumbProximal");
            const auto original=evaluateReferenceInstance(rest); QVERIFY(original);
            const auto &source=original->primitives.first(); const auto &skin=rest.asset->skins.first();
            const auto wrist=joint(rest,side+"Hand");
            const auto inWeb=[&](const ReferenceVertex &vertex) {
                float amount=0;
                for(int i=0;i<4;++i) if(vertex.weights[size_t(i)]>0) {
                    const int node=skin.joints[vertex.joints[size_t(i)]];
                    if(node==base||node==proximal)amount+=vertex.weights[size_t(i)];
                }
                return amount>.05f&&(vertex.position-wrist).length()<.105f;
            };
            for(const auto &id:{"hands/pinch","hands/ok"}) {
                auto posed=rest; QVERIFY2(applyReferencePosePreset(id,posed,&error),qPrintable(error));
                const auto evaluated=evaluateReferenceInstance(posed); QVERIFY(evaluated); const auto &surface=evaluated->primitives.first();
                float maximumStretch=0,restExtent=0,posedExtent=0; int webEdges=0;
                for(int i=0;i<source.vertices.size();++i)if(inWeb(source.vertices[i])) {
                    restExtent=std::max(restExtent,(source.vertices[i].position-wrist).length());
                    posedExtent=std::max(posedExtent,(surface.vertices[i].position-wrist).length());
                }
                for(int i=0;i<source.indices.size();i+=3)for(int edge=0;edge<3;++edge) {
                    const int a=int(source.indices[i+edge]),b=int(source.indices[i+(edge+1)%3]);
                    if(!inWeb(source.vertices[a])&&!inWeb(source.vertices[b]))continue;
                    const float length=(source.vertices[a].position-source.vertices[b].position).length();
                    if(length<.0005f)continue;
                    maximumStretch=std::max(maximumStretch,(surface.vertices[a].position-surface.vertices[b].position).length()/length); ++webEdges;
                }
                QVERIFY(webEdges>100); QVERIFY(restExtent>.01f);
                QVERIFY2(maximumStretch<2.f,qPrintable(QString(id)+" web edge stretch "+QString::number(maximumStretch)));
                QVERIFY(posedExtent<restExtent*1.1f);
                const auto relative=(rest.asset->nodes[base].rotation.conjugated()*posed.poseRotations.value(base)).normalized();
                QVERIFY2(std::abs(relative.scalar())>std::cos(10.f*3.14159265358979323846f/180.f),"Thumb metacarpal must stay within 20 degrees of rest");
            }
        }
    }
    void handTargetsPreserveWristOtherSideAndAllNonPoseState() {
        auto object=starter(); QString error;
        QVERIFY(setReferenceBodyShape(object,"height",1.9f,&error));
        object.translation={2,3,4}; object.rotation=QQuaternion::fromEulerAngles(5,20,15); object.scale={1.1f,.9f,1.2f};
        QVERIFY(setReferenceExpression(object,"Smile",.35f,&error));
        const int wrist=object.asset->humanoidBones.value("leftHand"),rightFinger=object.asset->humanoidBones.value("rightIndexProximal");
        const auto wristRotation=QQuaternion::fromEulerAngles(10,20,30),rightRotation=QQuaternion::fromEulerAngles(7,8,9);
        object.poseRotations[wrist]=wristRotation; object.poseRotations[rightFinger]=rightRotation;
        const auto saved=object; QVERIFY2(applyReferencePosePreset("hands/fist",object,&error,ReferenceHandTarget::Left),qPrintable(error));
        QCOMPARE(object.asset,saved.asset); QCOMPARE(object.translation,saved.translation); QCOMPARE(object.rotation,saved.rotation); QCOMPARE(object.scale,saved.scale);
        QCOMPARE(object.expressionWeights,saved.expressionWeights); QCOMPARE(object.morphOverrides,saved.morphOverrides);
        QCOMPARE(object.poseRotations.value(wrist),wristRotation); QCOMPARE(object.poseRotations.value(rightFinger),rightRotation);
        const auto leftRotations=object.poseRotations;
        QVERIFY(applyReferencePosePreset("hands/point",object,&error,ReferenceHandTarget::Left,true));
        const int leftFinger=object.asset->humanoidBones.value("leftIndexProximal");
        QCOMPARE(object.poseRotations.value(leftFinger),leftRotations.value(leftFinger));
        QVERIFY(object.poseRotations.value(rightFinger)!=rightRotation);
        auto standalone=starter(ReferenceStarterKind::Hand); QVERIFY(applyReferencePosePreset("hands/fist",standalone,&error));
        const auto before=snapshot(standalone); QVERIFY(!applyReferencePosePreset("hands/fist",standalone,&error,ReferenceHandTarget::Right));
        QVERIFY(error.contains("right")); QCOMPARE(snapshot(standalone),before);
    }
    void bodyMirrorSwapsAsymmetricInstructionsAndPreservesPlacement() {
        auto normal=starter(),mirrored=normal; QString error;
        const auto up=(joint(normal,"head")-joint(normal,"hips")).normalized();
        auto left=joint(normal,"leftUpperArm")-joint(normal,"rightUpperArm"); left-=up*QVector3D::dotProduct(left,up); left.normalize();
        QVERIFY(applyReferencePosePreset("body/run-stride",normal,&error));
        QVERIFY(applyReferencePosePreset("body/run-stride",mirrored,&error,ReferenceHandTarget::Both,true));
        // Source rigs may have unequal lengths/origins and a tilted anatomical
        // frame. Mirroring reflects authored directions in that frame; it does
        // not replace the artist's rest positions with a symmetric skeleton.
        for (const auto &segment:QVector<QPair<QString,QString>>{{"UpperArm","LowerArm"},{"LowerArm","Hand"},{"UpperLeg","LowerLeg"},{"LowerLeg","Foot"}}) {
            const auto a=direction(normal,"left"+segment.first,"left"+segment.second);
            const auto b=direction(mirrored,"right"+segment.first,"right"+segment.second);
            const auto reflected=a-left*(2*QVector3D::dotProduct(a,left));
            QVERIFY2((reflected-b).length()<.00001f,qPrintable(segment.first+" -> "+segment.second));
        }
        auto placed=starter(); placed.translation={5,2,-3}; placed.scale={1.2f,.8f,1.1f}; placed.rotation=QQuaternion::fromEulerAngles(8,31,2);
        const auto saved=placed; QVERIFY(applyReferencePosePreset("body/lying-side",placed,&error));
        QCOMPARE(placed.translation,saved.translation); QCOMPARE(placed.rotation,saved.rotation); QCOMPARE(placed.scale,saved.scale);
    }
    void missingOrUnsupportedRigsFailAtomicallyAndPosesPersist() {
        QString error; auto object=starter();
        for (const auto &bad:{"body/no-such-pose",""}) { const auto before=snapshot(object); QVERIFY(!applyReferencePosePreset(bad,object,&error)); QVERIFY(!error.isEmpty()); QCOMPARE(snapshot(object),before); }
        auto incomplete=*object.asset; incomplete.humanoidBones.remove("leftLowerLeg"); object.asset=finalizeReferenceAsset(std::move(incomplete),&error); QVERIFY(object.asset);
        const auto before=snapshot(object); QVERIFY(!applyReferencePosePreset("body/crouch",object,&error)); QVERIFY(error.contains("leftLowerLeg")); QCOMPARE(snapshot(object),before);
        object=starter(); object.locked=true; const auto locked=snapshot(object); QVERIFY(!applyReferencePosePreset("body/crouch",object,&error)); QCOMPARE(snapshot(object),locked);
        object=starter(); auto scaled=*object.asset; scaled.nodes[0].scale={1,2,1}; object.asset=finalizeReferenceAsset(std::move(scaled),&error); QVERIFY(object.asset);
        const auto scaledBefore=snapshot(object); QVERIFY(!applyReferencePosePreset("body/crouch",object,&error)); QVERIFY(error.contains("uniform")); QCOMPARE(snapshot(object),scaledBefore);
        object=starter(); QVERIFY(applyReferencePosePreset("body/seated-chair",object,&error)); QVERIFY(applyReferencePosePreset("hands/victory",object,&error,ReferenceHandTarget::Left));
        const auto bytes=snapshot(object); const auto loaded=deserializeReferenceScene(bytes,&error); QVERIFY2(loaded,qPrintable(error));
        QCOMPARE(serializeReferenceScene(*loaded),bytes); QCOMPARE(loaded->instances.first().poseRotations.size(),object.poseRotations.size());
        for (auto it=object.poseRotations.cbegin();it!=object.poseRotations.cend();++it)
            QVERIFY(std::abs(QQuaternion::dotProduct(loaded->instances.first().poseRotations.value(it.key()),it.value()))>.999999f);
        QCOMPARE(loaded->instances.first().poseTranslations,object.poseTranslations);
    }
    void fingerControlsRoundTripRealQuaternionsAndPreserveOtherFingers() {
        auto object=starter(ReferenceStarterKind::Hand); QString error;
        QHash<QString,QVector2D> values;
        for (const auto &id:{"thumb","index","middle","ring","little"}) values.insert(id,{.37f,.18f});
        const auto before=joint(object,"leftMiddleDistal");
        QVERIFY2(setReferenceHandControls(object,values,ReferenceHandTarget::Both,&error),qPrintable(error));
        QVERIFY((joint(object,"leftMiddleDistal")-before).length()>.02f);
        const auto controls=referenceHandControls(object,ReferenceHandTarget::Both,&error); QCOMPARE(controls.size(),5);
        for (const auto &control:controls) {
            QVERIFY2(std::abs(control.curl-.37f)<.0002f,qPrintable(control.id+" curl "+QString::number(control.curl)));
            QVERIFY2(std::abs(control.splay-.18f)<.0002f,qPrintable(control.id+" splay "+QString::number(control.splay)));
            QVERIFY(!control.approximate);
        }
        const int middle=object.asset->humanoidBones.value("leftMiddleProximal"),index=object.asset->humanoidBones.value("leftIndexProximal");
        const auto saved=object; QVERIFY(setReferenceHandControls(object,{{"index",{.8f,-.2f}}},ReferenceHandTarget::Left,&error));
        QCOMPARE(object.poseRotations.value(middle),saved.poseRotations.value(middle)); QVERIFY(object.poseRotations.value(index)!=saved.poseRotations.value(index));
        const auto bytes=snapshot(object); auto loaded=deserializeReferenceScene(bytes,&error); QVERIFY(loaded);
        const auto restored=referenceHandControls(loaded->instances.first(),ReferenceHandTarget::Both,&error); QCOMPARE(restored.size(),5);
        for (const auto &control:restored) if (control.id=="index") { QVERIFY(std::abs(control.curl-.8f)<.0002f); QVERIFY(std::abs(control.splay+.2f)<.0002f); }
    }
    void fingerControlsRetargetBindAxesAndRejectInvalidBatchAtomically() {
        auto ordinary=starter(),foreign=ordinary; QString error;
        foreign.asset=reorientedBind(ordinary.asset,&error); QVERIFY2(foreign.asset,qPrintable(error));
        const QHash<QString,QVector2D> controls{{"thumb",{.3f,.2f}},{"index",{.65f,-.1f}},{"middle",{.7f,0}},{"ring",{.75f,.1f}},{"little",{.8f,.2f}}};
        QVERIFY(setReferenceHandControls(ordinary,controls,ReferenceHandTarget::Both,&error));
        QVERIFY2(setReferenceHandControls(foreign,controls,ReferenceHandTarget::Both,&error),qPrintable(error));
        for (const auto &semantic:{"leftIndexDistal","leftThumbDistal","rightMiddleDistal"}) QVERIFY((joint(ordinary,semantic)-joint(foreign,semantic)).length()<.0001f);
        QVERIFY(setReferenceHandControls(ordinary,{{"index",{.1f,.7f}}},ReferenceHandTarget::Left,&error));
        const auto mixed=referenceHandControls(ordinary,ReferenceHandTarget::Both,&error); QVERIFY(mixed[1].approximate);
        auto before=snapshot(ordinary); QVERIFY(!setReferenceHandControls(ordinary,{{"index",{2,0}},{"middle",{.2f,0}}},ReferenceHandTarget::Both,&error)); QCOMPARE(snapshot(ordinary),before);
        QVERIFY(!setReferenceHandControls(ordinary,{{"not-a-finger",{.2f,0}}},ReferenceHandTarget::Both,&error)); QCOMPARE(snapshot(ordinary),before);
        auto hand=starter(ReferenceStarterKind::Hand); before=snapshot(hand);
        QVERIFY(!setReferenceHandControls(hand,{{"index",{.2f,0}}},ReferenceHandTarget::Right,&error)); QCOMPARE(snapshot(hand),before);
        QVERIFY(applyReferencePosePreset("hands/fist",hand,&error));
        const auto approximated=referenceHandControls(hand,ReferenceHandTarget::Both,&error); QVERIFY(approximated[0].approximate || approximated[1].approximate);
    }
};
QTEST_MAIN(ReferencePoseLibraryTests)
#include "test_reference3d_pose_library.moc"
