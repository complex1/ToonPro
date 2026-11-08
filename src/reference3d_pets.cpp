#include "reference3d_pets.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QResource>
#include <cmath>

static void initializeReferencePetResource() { Q_INIT_RESOURCE(reference3d_catalog); }
namespace toon {
namespace {
struct NodePose { QString name;int parent=-1;QVector3D translation,scale{1,1,1};QQuaternion rotation; };
struct PetPose { ReferencePetPosePreset preset;QVector<NodePose> nodes; };
struct PetRig {QString kind;QVector<NodePose> nodes;QVector<PetPose> poses;};
struct PetBank {QVector<PetRig> rigs;QString error;};
QVector3D vector(const QJsonArray&a){return {float(a[0].toDouble()),float(a[1].toDouble()),float(a[2].toDouble())};}
NodePose readNode(const QJsonObject&o){NodePose n;n.name=o.value("name").toString();n.parent=o.value("parent").toInt(-1);n.translation=vector(o.value("translation").toArray());n.scale=vector(o.value("scale").toArray());const auto q=o.value("rotation").toArray();n.rotation=QQuaternion(float(q[3].toDouble()),float(q[0].toDouble()),float(q[1].toDouble()),float(q[2].toDouble()));return n;}
const PetBank&bank(){
    static const PetBank value=[] {PetBank b;initializeReferencePetResource();QFile file(":/reference3d/pet-poses-v1.json");
        if(!file.open(QIODevice::ReadOnly)||file.size()>1024*1024){b.error="The bundled pet pose data is unavailable.";return b;}
        const auto document=QJsonDocument::fromJson(file.readAll());const auto root=document.object();if(root.value("version").toInt()!=1){b.error="The pet pose data version is unsupported.";return b;}
        for(const auto&entry:root.value("rigs").toArray()){const auto o=entry.toObject();PetRig rig;rig.kind=o.value("kind").toString();
            for(const auto&n:o.value("nodes").toArray())rig.nodes.append(readNode(n.toObject()));
            for(const auto&p:o.value("poses").toArray()){const auto pose=p.toObject();PetPose item;item.preset={pose.value("id").toString(),pose.value("name").toString(),rig.kind};for(const auto&n:pose.value("nodes").toArray())item.nodes.append(readNode(n.toObject()));rig.poses.append(std::move(item));}
            if(rig.nodes.isEmpty()||rig.nodes.size()>256||rig.poses.isEmpty()||rig.poses.size()>32){b.error="The pet pose data has an invalid rig layout.";b.rigs.clear();return b;}b.rigs.append(std::move(rig));}
        if(b.rigs.size()!=3)b.error="The pet pose collection is incomplete.";return b;}();return value;
}
bool close(QVector3D a,QVector3D b){const float d=(a-b).lengthSquared();return std::isfinite(d)&&d<1e-9f;}
bool compatible(const ReferenceAsset&a,const PetRig&rig){
    if(a.metadata.value("referencePetKind").toString()!=rig.kind||a.nodes.size()!=rig.nodes.size())return false;
    for(int i=0;i<rig.nodes.size();++i){const auto&n=a.nodes[i];const auto&r=rig.nodes[i];const float dot=std::abs(QQuaternion::dotProduct(n.rotation.normalized(),r.rotation.normalized()));
        if(n.name!=r.name||n.parentIndex!=r.parent||!close(n.translation,r.translation)||!close(n.scale,r.scale)||!std::isfinite(dot)||dot<.999999f)return false;}
    return true;
}
const PetRig*rigFor(const ReferenceInstance&i){if(!i.asset)return nullptr;const auto&b=bank();if(!b.error.isEmpty())return nullptr;for(const auto&rig:b.rigs)if(compatible(*i.asset,rig))return &rig;return nullptr;}
bool fail(QString*error,const QString&message){if(error)*error=message;return false;}
}
QVector<ReferencePetPosePreset> referencePetPosePresets(const ReferenceInstance&i){QVector<ReferencePetPosePreset> result;if(const auto*r=rigFor(i))for(const auto&p:r->poses)result.append(p.preset);return result;}
bool applyReferencePetPosePreset(const QString&id,ReferenceInstance&i,QString*error){
    if(i.locked)return fail(error,"Unlock the pet reference before applying a pose.");
    const auto invalid=validateReferenceInstance(i);if(!invalid.isEmpty())return fail(error,invalid);
    const auto*rig=rigFor(i);if(!rig)return fail(error,bank().error.isEmpty()?QString("This pet pose requires its matching bundled animal rig."):bank().error);
    const PetPose*pose=nullptr;for(const auto&p:rig->poses)if(p.preset.id==id){pose=&p;break;}
    if(!pose)return fail(error,"That pose belongs to another animal or is unavailable.");
    if(pose->nodes.size()!=i.asset->nodes.size())return fail(error,"The pet pose has an incompatible node layout.");
    auto next=i;next.poseRotations.clear();next.poseTranslations.clear();next.poseScales.clear();
    for(int index=0;index<pose->nodes.size();++index){const auto&n=pose->nodes[index];if(n.name!=i.asset->nodes[index].name)return fail(error,"The pet pose node mapping is invalid.");next.poseTranslations.insert(index,n.translation);next.poseRotations.insert(index,n.rotation);next.poseScales.insert(index,n.scale);}
    const auto problem=validateReferenceInstance(next);if(!problem.isEmpty())return fail(error,problem);
    i=std::move(next);if(error)error->clear();return true;
}
} // namespace toon
