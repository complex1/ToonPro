#include "reference3d_builtin.h"
#include <QtTest>
#include <cmath>
#include <limits>

using namespace toon;
namespace {
ReferenceInstance instance(ReferenceStarterKind kind) {
    ReferenceInstance result; result.id = "study"; result.name = "Study"; result.asset = createReferenceStarter(kind); return result;
}
}
class ReferenceBuiltinTests final : public QObject {
    Q_OBJECT
private slots:
    void startersHaveUsableGeometryAndMappedJoints() {
        const auto starters = referenceStarters(); QCOMPARE(starters.size(), 3);
        for (const auto &starter : starters) {
            QVERIFY2(starter.asset, qPrintable(starter.name));
            QVERIFY2(validateReferenceAsset(*starter.asset).isEmpty(), qPrintable(validateReferenceAsset(*starter.asset)));
            auto model = instance(starter.kind); QString error;
            const auto evaluation = evaluateReferenceInstance(model, &error); QVERIFY2(evaluation, qPrintable(error));
            QVERIFY(evaluation->bounds.valid); QVERIFY(!evaluation->primitives.isEmpty());
            QVERIFY(!model.asset->skins.isEmpty()); QVERIFY(!model.asset->humanoidBones.isEmpty());
            QVERIFY(!referenceBodyShapeParameters(model).isEmpty());
        }
        const auto figure = instance(ReferenceStarterKind::Figure);
        QVERIFY(figure.asset->humanoidBones.contains("hips")); QVERIFY(figure.asset->humanoidBones.contains("leftIndexDistal"));
        QCOMPARE(figure.asset->expressions.size(), 3);
        const auto posed = evaluateReferenceInstance(figure); QVERIFY(posed);
        QVERIFY(std::abs(posed->bounds.maximum.y() - posed->bounds.minimum.y() - 1.75f) < .001f);
    }
    void proportionChangesUpdateBindGeometryAndPreservePose() {
        auto figure = instance(ReferenceStarterKind::Figure); QVERIFY(figure.asset);
        const int elbow = figure.asset->humanoidBones.value("leftLowerArm");
        figure.poseRotations.insert(elbow, QQuaternion::fromEulerAngles(0, 0, 35));
        figure.translation = {3, 0, 2};
        const auto rotation = figure.poseRotations[elbow];
        const auto oldId = figure.asset->id;
        QString error; QVERIFY2(setReferenceBodyShape(figure, "height", 2, &error), qPrintable(error));
        QCOMPARE(figure.translation, QVector3D(3,0,2)); QCOMPARE(figure.poseRotations[elbow], rotation);
        QVERIFY(figure.asset->id != oldId);
        const auto evaluation = evaluateReferenceInstance(figure, &error); QVERIFY2(evaluation, qPrintable(error));
        QVERIFY(std::abs(evaluation->bounds.maximum.y() - evaluation->bounds.minimum.y() - 2.f) < .001f);
        const auto head = figure.asset->humanoidBones.value("head");
        QVERIFY(evaluation->nodeGlobals[head].column(3).y() > 1.7f);
        QVERIFY(setReferenceBodyShape(figure, "chinWidth", 1.4f, &error));
        QCOMPARE(figure.asset->metadata.value("shape").toObject().value("chinWidth").toDouble(), double(1.4f));
        const auto before = serializeReferenceAsset(*figure.asset);
        QVERIFY(!setReferenceBodyShape(figure,"height",std::numeric_limits<float>::quiet_NaN(),&error));
        QVERIFY(!setReferenceBodyShape(figure,"height",100,&error));
        QVERIFY(!setReferenceBodyShape(figure,"unknown",1,&error));
        QCOMPARE(serializeReferenceAsset(*figure.asset), before);
    }
    void headCountChangesProportionalSizeAndKeepsStature() {
        auto large=instance(ReferenceStarterKind::Figure),small=large;QString error;
        QVERIFY(setReferenceBodyShape(large,"headRatio",5,&error));
        QVERIFY(setReferenceBodyShape(small,"headRatio",9,&error));
        const auto width=[](const ReferenceInstance &i){
            const int head=i.asset->humanoidBones.value("head");float lo=1e6f,hi=-1e6f;
            for(const auto &v:i.asset->meshes[0].primitives[0].vertices){float w=0;for(int j=0;j<4;++j)if(v.joints[j]==head)w+=v.weights[j];if(w>.5f){lo=std::min(lo,v.position.x());hi=std::max(hi,v.position.x());}}
            return hi-lo;
        };
        const float headScale=width(large)/width(small);QVERIFY(headScale>1.4f);
        const auto eyeHeight=[](const ReferenceInstance&i){const auto &p=i.asset->meshes[0].primitives[1];float lo=1e6f,hi=-1e6f;for(int v=0;v<117;++v){lo=std::min(lo,p.vertices[v].position.y());hi=std::max(hi,p.vertices[v].position.y());}return hi-lo;};
        QVERIFY(std::abs(eyeHeight(large)/eyeHeight(small)-headScale)<.05f);
        for(const auto *i:{&large,&small}){const auto e=evaluateReferenceInstance(*i,&error);QVERIFY2(e,qPrintable(error));QVERIFY(std::abs(e->bounds.maximum.y()-e->bounds.minimum.y()-1.75f)<.0001f);}
        auto wide=instance(ReferenceStarterKind::Head),narrow=wide;
        QVERIFY(setReferenceBodyShape(wide,"headWidth",1.3f,&error));QVERIFY(setReferenceBodyShape(narrow,"headWidth",.7f,&error));
        const auto eyeWidth=[](const ReferenceInstance&i){const auto&p=i.asset->meshes[0].primitives[1];float lo=1e6f,hi=-1e6f;for(int v=0;v<117;++v){lo=std::min(lo,p.vertices[v].position.x());hi=std::max(hi,p.vertices[v].position.x());}return hi-lo;};
        QVERIFY(eyeWidth(wide)>eyeWidth(narrow)*1.1f);
    }
    void articulatedFingersAndExpressionsChangeTheActualMesh() {
        auto hand = instance(ReferenceStarterKind::Hand); QVERIFY(hand.asset);
        const auto before = evaluateReferenceInstance(hand); QVERIFY(before);
        const int proximal = hand.asset->humanoidBones.value("leftIndexProximal", -1);
        const int distal = hand.asset->humanoidBones.value("leftIndexDistal", -1); QVERIFY(proximal >= 0 && distal >= 0);
        hand.poseRotations[proximal] = QQuaternion::fromEulerAngles(60,0,0);
        const auto after = evaluateReferenceInstance(hand); QVERIFY(after);
        QVERIFY((after->nodeGlobals[distal].column(3) - before->nodeGlobals[distal].column(3)).length() > .01f);
        auto head = instance(ReferenceStarterKind::Head);
        const auto rest = evaluateReferenceInstance(head); QVERIFY(rest);
        QString error; QVERIFY2(setReferenceExpression(head,"Blink",1,&error),qPrintable(error));
        const auto blink = evaluateReferenceInstance(head); QVERIFY(blink);
        int changed = 0;
        for (int p=0; p<rest->primitives.size(); ++p) for (int v=0; v<rest->primitives[p].vertices.size(); ++v)
            if ((rest->primitives[p].vertices[v].position-blink->primitives[p].vertices[v].position).length() > .00001f) ++changed;
        QVERIFY(changed > 20);
    }
    void anatomicalSurfacesAreContinuousAndTopologyStable() {
        for (const auto kind : {ReferenceStarterKind::Figure, ReferenceStarterKind::Head, ReferenceStarterKind::Hand}) {
            auto model=instance(kind); QVERIFY(model.asset);
            const auto &surface=model.asset->meshes[0].primitives[0];
            QVERIFY(surface.vertices.size()>1500);
            QVERIFY(surface.indices.size()/3<28000);
            QHash<quint64,int> edges;QVector<QVector<int>> neighbors(surface.vertices.size());
            int blended=0;
            for(const auto &vertex:surface.vertices) {
                QVERIFY(std::isfinite(vertex.normal.x()));QVERIFY(std::abs(vertex.normal.lengthSquared()-1)<.0001f);
                int active=0;float sum=0;for(int i=0;i<4;++i){active+=vertex.weights[i]>.001f;sum+=vertex.weights[i];}
                blended+=active>1;QVERIFY(std::abs(sum-1)<.00001f);
            }
            QVERIFY(blended>100);
            for(int i=0;i<surface.indices.size();i+=3)for(int e=0;e<3;++e){
                const int a=int(surface.indices[i+e]),b=int(surface.indices[i+(e+1)%3]);
                ++edges[(quint64(std::min(a,b))<<32)|quint32(std::max(a,b))];neighbors[a].append(b);neighbors[b].append(a);
            }
            for(auto count:edges)QCOMPARE(count,2); // Closed body, capped neck and wrist.
            QSet<int> visited;QVector<int> pending{0};while(!pending.isEmpty()){const int v=pending.takeLast();if(visited.contains(v))continue;visited.insert(v);for(int n:neighbors[v])pending.append(n);}
            QCOMPARE(visited.size(),surface.vertices.size());
            const auto indices=surface.indices;const auto count=surface.vertices.size();QString error;
            QVERIFY2(setReferenceBodyShape(model,kind==ReferenceStarterKind::Hand?"palmWidth":"chinWidth",1.25f,&error),qPrintable(error));
            QCOMPARE(model.asset->meshes[0].primitives[0].vertices.size(),count);
            QCOMPARE(model.asset->meshes[0].primitives[0].indices,indices);
            QVERIFY(!model.asset->humanoidBones.contains(kind==ReferenceStarterKind::Hand?"head":kind==ReferenceStarterKind::Head?"hips":"unused"));
            if(kind==ReferenceStarterKind::Head){
                float lowest=1e6f;for(const auto &v:model.asset->meshes[0].primitives[0].vertices)lowest=std::min(lowest,v.position.y());
                QVERIFY(lowest>.01f); // The capped study stays clear of the viewport grid.
            }
            QCOMPARE(model.asset->metadata.value("geometryRevision").toInt(),2);
            QCOMPARE(model.asset->metadata.value("provenance").toObject().value("license").toString(),QString("CC0-1.0"));
        }
    }
    void lazyCatalogAndRealFamilyMixesRoundTrip() {
        const auto catalog=referenceStarterPresets();QCOMPARE(catalog.size(),20);
        QSet<QString> ids;for(const auto &p:catalog)ids.insert(p.id);QCOMPARE(ids.size(),20);
        QString factoryError;const auto preset=createReferenceStarter("head/oval",&factoryError);QVERIFY2(preset,qPrintable(factoryError));
        auto head=instance(ReferenceStarterKind::Head);auto hand=instance(ReferenceStarterKind::Hand);
        QCOMPARE(referenceShapeFamilyMix(head,ReferenceStarterKind::Head).size(),9);
        QCOMPARE(referenceShapeFamilyMix(hand,ReferenceStarterKind::Hand).size(),7);
        QVERIFY(referenceShapeFamilyMix(head,ReferenceStarterKind::Hand).isEmpty());
        QString error;const auto original=head.asset->meshes[0].primitives[0].vertices;
        QVERIFY2(setReferenceShapeFamilyMix(head,ReferenceStarterKind::Head,{{"square",1},{"round",1}},true,&error),qPrintable(error));
        const auto normalized=head.asset->meshes[0].primitives[0].vertices;int changed=0;
        for(int v=0;v<original.size();++v)changed+=(original[v].position-normalized[v].position).length()>.00001f;
        QVERIFY(changed>500);
        QVERIFY(setReferenceShapeFamilyMix(head,ReferenceStarterKind::Head,{{"square",.5f},{"round",.5f}},false,&error));
        const auto equivalent=head.asset->meshes[0].primitives[0].vertices;
        for(int v=0;v<normalized.size();++v)QVERIFY((normalized[v].position-equivalent[v].position).length()<.000001f);
        const auto encoded=serializeReferenceAsset(*head.asset,&error);QVERIFY2(!encoded.isEmpty(),qPrintable(error));
        auto restored=deserializeReferenceAsset(encoded,&error);QVERIFY2(restored,qPrintable(error));head.asset=restored;
        const auto mix=referenceShapeFamilyMix(head,ReferenceStarterKind::Head);QCOMPARE(mix[2].weight,.5f);QVERIFY(!referenceShapeFamilyMixLimited(head,ReferenceStarterKind::Head));
        const auto before=head.asset;QVERIFY(!setReferenceShapeFamilyMix(head,ReferenceStarterKind::Head,{{"unknown",1}},true,&error));QCOMPARE(head.asset,before);
        QVERIFY(!createReferenceStarter("head/missing",&error));
    }
    void batchEditsAndHandMirrorPreservePoseAndStoredShape() {
        auto hand=instance(ReferenceStarterKind::Hand);QString error;
        const int finger=hand.asset->humanoidBones.value("leftIndexProximal");
        hand.poseRotations[finger]=QQuaternion::fromEulerAngles(45,12,8);hand.translation={2,0,-1};
        QVERIFY2(setReferenceBodyShapeValues(hand,{{"indexLength",1.2f},{"palmWidth",1.1f},{"nailShape",.8f}},&error),qPrintable(error));
        const auto originalPose=hand.poseRotations[finger];const auto left=evaluateReferenceInstance(hand);QVERIFY(left);
        QVERIFY2(setReferenceBodyShape(hand,"handedness",1,&error),qPrintable(error));
        QVERIFY(!hand.asset->humanoidBones.contains("leftHand"));QVERIFY(hand.asset->humanoidBones.contains("rightHand"));
        QCOMPARE(hand.asset->humanoidBones.value("rightIndexProximal"),finger);
        const auto right=evaluateReferenceInstance(hand);QVERIFY(right);
        for(int p=0;p<left->primitives.size();++p)for(int v=0;v<left->primitives[p].vertices.size();++v){auto expected=left->primitives[p].vertices[v].position;expected.setX(4-expected.x());QVERIFY((right->primitives[p].vertices[v].position-expected).length()<.00001f);}
        QVERIFY(setReferenceBodyShape(hand,"handedness",0,&error));QCOMPARE(hand.poseRotations[finger],originalPose);
        const auto before=hand.asset;QVERIFY(!setReferenceBodyShapeValues(hand,{{"palmWidth",1.3f},{"indexLength",100}},&error));QCOMPARE(hand.asset,before);
        auto figure=instance(ReferenceStarterKind::Figure);const auto a=figure.asset;
        QVERIFY(setReferenceBodyShape(figure,"chestFullness",0,&error));QVERIFY(figure.asset->id!=a->id);
    }
    void handStudyFrameMatchesPalmAndGuideAnatomy() {
        QString error;
        for(bool right:{false,true}) {
            auto hand=instance(ReferenceStarterKind::Hand);QVERIFY(hand.asset);
            if(right)QVERIFY(setReferenceBodyShape(hand,"handedness",1,&error));
            const QString side=right?"right":"left";
            const auto globals=evaluateReferenceNodeGlobals(hand,&error);QVERIFY2(globals,qPrintable(error));
            const auto point=[&](const QString&part){return (*globals)[hand.asset->humanoidBones.value(side+part)].column(3).toVector3D();};
            const auto forward=(point("MiddleProximal")-point("Hand")).normalized();
            auto across=point("LittleProximal")-point("IndexProximal");across=(across-forward*QVector3D::dotProduct(across,forward)).normalized();
            QVERIFY(QVector3D::dotProduct(forward,{0,1,0})>.999f);
            QVERIFY(QVector3D::dotProduct(across,right?QVector3D(-1,0,0):QVector3D(1,0,0))>.999f);
            const auto values=hand.asset->metadata.value("handFrames").toObject().value(side).toObject().value("volar").toArray();
            const QVector3D volar(float(values[0].toDouble()),float(values[1].toDouble()),float(values[2].toDouble()));
            QVERIFY(QVector3D::dotProduct(volar,{0,0,1})>.999f);
            QVector3D nailNormal;for(const auto&v:hand.asset->meshes[0].primitives[1].vertices)nailNormal+=v.normal;
            QVERIFY(QVector3D::dotProduct(nailNormal.normalized(),{0,0,-1})>.8f);
            const auto old=point("IndexDistal");const int finger=hand.asset->humanoidBones.value(side+"IndexProximal");
            hand.poseRotations[finger]=QQuaternion::fromAxisAndAngle(1,0,0,55);
            const auto curl=evaluateReferenceNodeGlobals(hand,&error);QVERIFY(curl);
            const auto now=(*curl)[hand.asset->humanoidBones.value(side+"IndexDistal")].column(3).toVector3D();
            QVERIFY(QVector3D::dotProduct(now-old,volar)>.01f);
        }
        auto figure=instance(ReferenceStarterKind::Figure);const auto g=evaluateReferenceNodeGlobals(figure,&error);QVERIFY(g);
        for(const QString side:{QString("left"),QString("right")}){
            const auto p=[&](const QString&part){return (*g)[figure.asset->humanoidBones.value(side+part)].column(3).toVector3D();};
            const auto forward=(p("MiddleProximal")-p("Hand")).normalized();auto across=p("LittleProximal")-p("IndexProximal");across=(across-forward*QVector3D::dotProduct(across,forward)).normalized();
            const auto expected=QVector3D::crossProduct(across,forward).normalized()*(side=="left"?1.f:-1.f);
            const auto a=figure.asset->metadata.value("handFrames").toObject().value(side).toObject().value("volar").toArray();
            const QVector3D authored(float(a[0].toDouble()),float(a[1].toDouble()),float(a[2].toDouble()));
            QVERIFY(QVector3D::dotProduct(expected,authored)>.9999f);
        }
    }
    void nailGuidesStayNearTheirSkinnedFingerSurface() {
        auto hand=instance(ReferenceStarterKind::Hand);QVERIFY(hand.asset);QString error;
        int blended=0;for(const auto&v:hand.asset->meshes[0].primitives[1].vertices){int count=0;for(float w:v.weights)count+=w>.001f;blended+=count>1;}
        QVERIFY(blended>0); // Guides inherit skin weights rather than a rigid distal assignment.
        const auto segmentDistance=[](const QVector3D&p,const QVector3D&a,const QVector3D&b){const auto d=b-a;const float t=d.lengthSquared()>1e-16f?std::clamp(QVector3D::dotProduct(p-a,d)/d.lengthSquared(),0.f,1.f):0.f;return (p-a-d*t).lengthSquared();};
        const auto distance=[&](const QVector3D&p,const QVector3D&a,const QVector3D&b,const QVector3D&c){
            const auto ab=b-a,ac=c-a,ap=p-a;const float aa=QVector3D::dotProduct(ab,ab),bb=QVector3D::dotProduct(ab,ac),cc=QVector3D::dotProduct(ac,ac),d=QVector3D::dotProduct(ap,ab),e=QVector3D::dotProduct(ap,ac),det=aa*cc-bb*bb;
            if(det>1e-20f){const float u=(cc*d-bb*e)/det,v=(aa*e-bb*d)/det;if(u>=0&&v>=0&&u+v<=1)return (ap-ab*u-ac*v).lengthSquared();}
            return std::min({segmentDistance(p,a,b),segmentDistance(p,b,c),segmentDistance(p,c,a)});
        };
        for(bool curled:{false,true}){
            if(curled)for(const QString&finger:QStringList{"Thumb","Index","Middle","Ring","Little"})for(const QString&part:(finger=="Thumb"?QStringList{"Metacarpal","Proximal","Distal"}:QStringList{"Proximal","Intermediate","Distal"}))hand.poseRotations[hand.asset->humanoidBones.value("left"+finger+part)]=QQuaternion::fromAxisAndAngle(1,0,0,65);
            const auto evaluation=evaluateReferenceInstance(hand,&error);QVERIFY2(evaluation,qPrintable(error));const auto&skin=evaluation->primitives[0];
            float maximum=0;for(const auto&guide:evaluation->primitives[1].vertices){float closest=1e6f;for(int t=0;t<skin.indices.size();t+=3)closest=std::min(closest,distance(guide.position,skin.vertices[int(skin.indices[t])].position,skin.vertices[int(skin.indices[t+1])].position,skin.vertices[int(skin.indices[t+2])].position));maximum=std::max(maximum,std::sqrt(closest));}
            QVERIFY2(maximum<(curled?.0025f:.0003f),qPrintable(QString("Nail guide separation %1m (curled %2)").arg(maximum).arg(curled)));
        }
    }
    void shapeMaterialsRejectIncompatibleFamiliesAtomically() {
        auto source = instance(ReferenceStarterKind::Head), target = instance(ReferenceStarterKind::Head);
        QString error; QVERIFY(setReferenceBodyShape(source,"chinLength",1.3f,&error));
        const int head = target.asset->humanoidBones.value("head");
        target.poseRotations[head] = QQuaternion::fromEulerAngles(10,20,0);
        const auto rotation = target.poseRotations[head];
        QVERIFY2(applyReferenceBuiltinShape(source,target,&error),qPrintable(error));
        QCOMPARE(target.asset->id,source.asset->id); QCOMPARE(target.poseRotations[head],rotation);
        auto figure = instance(ReferenceStarterKind::Figure); const auto id = figure.asset->id;
        QVERIFY(!applyReferenceBuiltinShape(source,figure,&error)); QCOMPARE(figure.asset->id,id);
        target.poseRotations.insert(-1,QQuaternion()); const auto invalid=target.poseRotations;
        QVERIFY(!applyReferenceBuiltinShape(source,target,&error)); QCOMPARE(target.poseRotations,invalid);
    }
};
QTEST_MAIN(ReferenceBuiltinTests)
#include "test_reference3d_builtin.moc"
