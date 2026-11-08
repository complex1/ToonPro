// Opt-in offline pet packer. Runtime models require no Blender or network.
#include "reference3d_import.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>
#include <QSaveFile>
#include <cstdio>
using namespace toon;
QVector3D vec(const QJsonValue&v){const auto a=v.toArray();return {float(a[0].toDouble()),float(a[1].toDouble()),float(a[2].toDouble())};}
QJsonArray array(QVector3D v){return {v.x(),v.y(),v.z()};}
QJsonObject node(const ReferenceNode&n){return {{"name",n.name},{"parent",n.parentIndex},{"translation",array(n.translation)},{"rotation",QJsonArray{n.rotation.x(),n.rotation.y(),n.rotation.z(),n.rotation.scalar()}},{"scale",array(n.scale)}};}
int main(int argc,char**argv){QCoreApplication app(argc,argv);if(argc!=3){std::fprintf(stderr,"Usage: toon_reference3d_prepare_pets <preparation-directory> <resource-output-directory>\n");return 1;}const QDir root(QString::fromLocal8Bit(argv[1])),resources(QString::fromLocal8Bit(argv[2]));if(!QDir().mkpath(resources.absolutePath()))return 1;QFile source(root.filePath("pet-poses-source.json"));if(!source.open(QIODevice::ReadOnly))return 2;auto bank=QJsonDocument::fromJson(source.readAll()).object();QJsonArray rigs,report;QString error;
for(const auto&value:bank.value("rigs").toArray()){auto rig=value.toObject();const auto kind=rig.value("kind").toString();const auto path=root.filePath("prepared/"+kind+".glb");auto imported=importReferenceAsset(path);if(!imported){std::fprintf(stderr,"%s: %s\n",qPrintable(kind),qPrintable(imported.error));return 3;}auto asset=*imported.asset;asset.id.clear();asset.name=kind=="bird"?"Stylized eagle":kind=="cat"?"Stylized cat":"Stylized dog";
const auto defaults=rig.value("nodes").toArray();if(defaults.size()!=asset.nodes.size())return 4;
for(int i=0;i<defaults.size();++i){const auto n=defaults[i].toObject();if(asset.nodes[i].name!=n.value("name").toString())return 5;asset.nodes[i].translation=vec(n.value("translation"));asset.nodes[i].scale=vec(n.value("scale"));const auto q=n.value("rotation").toArray();asset.nodes[i].rotation={float(q[3].toDouble()),float(q[0].toDouble()),float(q[1].toDouble()),float(q[2].toDouble())};}
asset.metadata["referencePetKind"]=kind;asset.metadata["referenceCatalogId"]="pet/"+kind;asset.metadata["referencePetPoseCount"]=rig.value("poses").toArray().size();asset.metadata["provenance"]=QJsonObject{{"creator","Quaternius"},{"source","https://opengameart.org/content/animated-animales-low-poly"},{"archiveSha256","6d8f6789d65dbdb352ebad98a4209f26b41b384e87d81bc05e9ca76139db1248"},{"preparation",kind=="bird"?"Blender5.2: normalized +Zforward/1.2m glide wingspan, descriptive native bone names, Flying .25 raised-wing default, truthful lowered-wing source Idle snapshots; no runtime animation playback.":"Blender5.2: normalized scale/+Zforward, descriptive native bone names, native IK clips baked, idle default, selected static clip snapshots; no runtime animation playback."}};
auto finalized=finalizeReferenceAsset(std::move(asset),&error);if(!finalized){std::fprintf(stderr,"%s\n",qPrintable(error));return 6;}ReferenceInstance i;i.id="pet";i.name=finalized->name;i.asset=finalized;auto e=evaluateReferenceInstance(i,&error);if(!e){std::fprintf(stderr,"%s\n",qPrintable(error));return 7;}
// Use a legible raised-wing flight default for the eagle. Its source Idle
// action has hanging wings, not a perched/folded pose. Normalize the eagle by
// the true spread-wing span, independently of the chosen default silhouette.
float factor=0;
if(kind=="bird"){
 auto gliding=i;bool found=false;
 for(const auto&value:rig.value("poses").toArray()){
  const auto pose=value.toObject();if(pose.value("id").toString()!="pet/bird/flying-spread")continue;
  const auto nodes=pose.value("nodes").toArray();if(nodes.size()!=finalized->nodes.size())return 16;
  for(int n=0;n<nodes.size();++n){const auto state=nodes[n].toObject();gliding.poseTranslations[n]=vec(state.value("translation"));gliding.poseScales[n]=vec(state.value("scale"));const auto q=state.value("rotation").toArray();gliding.poseRotations[n]={float(q[3].toDouble()),float(q[0].toDouble()),float(q[1].toDouble()),float(q[2].toDouble())};}
  found=true;break;
 }
 if(!found)return 17;const auto spread=evaluateReferenceInstance(gliding,&error);if(!spread)return 18;
 factor=1.2f/(spread->bounds.maximum.x()-spread->bounds.minimum.x());
}else{
 const float targetHeight=kind=="dog"?.65f:.38f;
 factor=targetHeight/(e->bounds.maximum.y()-e->bounds.minimum.y());
}
int placement=-1;for(int n=0;n<finalized->nodes.size();++n)if(finalized->nodes[n].name=="ToonPetPlacement")placement=n;if(placement<0)return 11;
auto normalized=*finalized;normalized.id.clear();auto&placementNode=normalized.nodes[placement];placementNode.scale*=factor;placementNode.translation*=factor;placementNode.translation.setY(placementNode.translation.y()-e->bounds.minimum.y()*factor+.002f);
finalized=finalizeReferenceAsset(std::move(normalized),&error);if(!finalized)return 12;i.asset=finalized;e=evaluateReferenceInstance(i,&error);if(!e)return 13;
QJsonArray poses;
for(const auto&value:rig.value("poses").toArray()){
 auto pose=value.toObject();auto nodes=pose.value("nodes").toArray();auto placementPose=nodes[placement].toObject();placementPose["translation"]=array(finalized->nodes[placement].translation);placementPose["scale"]=array(finalized->nodes[placement].scale);nodes[placement]=placementPose;
 auto posed=i;for(int n=0;n<nodes.size();++n){const auto state=nodes[n].toObject();posed.poseTranslations[n]=vec(state.value("translation"));posed.poseScales[n]=vec(state.value("scale"));const auto q=state.value("rotation").toArray();posed.poseRotations[n]={float(q[3].toDouble()),float(q[0].toDouble()),float(q[1].toDouble()),float(q[2].toDouble())};}
 const auto bounds=evaluateReferenceInstance(posed,&error);if(!bounds)return 14;auto translation=vec(placementPose.value("translation"));translation.setY(translation.y()+.002f-bounds->bounds.minimum.y());placementPose["translation"]=array(translation);nodes[placement]=placementPose;pose["nodes"]=nodes;poses.append(pose);
}
rig["poses"]=poses;
QSaveFile output(resources.filePath(kind+"-v1.toon3dasset"));if(!output.open(QIODevice::WriteOnly))return 8;const auto bytes=serializeReferenceAsset(*finalized,&error);if(bytes.isEmpty()||output.write(bytes)!=bytes.size()||!output.commit())return 9;
QJsonArray nodes;for(const auto&n:finalized->nodes)nodes.append(node(n));rig["nodes"]=nodes;rigs.append(rig);int vertices=0,triangles=0;for(const auto&m:finalized->meshes)for(const auto&p:m.primitives){vertices+=p.vertices.size();triangles+=p.indices.size()/3;}report.append(QJsonObject{{"kind",kind},{"assetId",finalized->id},{"assetBytes",bytes.size()},{"nodes",finalized->nodes.size()},{"joints",finalized->skins.first().joints.size()},{"vertices",vertices},{"triangles",triangles},{"minimum",array(e->bounds.minimum)},{"maximum",array(e->bounds.maximum)}});
}
bank["rigs"]=rigs;QSaveFile output(resources.filePath("pet-poses-v1.json"));if(!output.open(QIODevice::WriteOnly))return 10;if(output.write(QJsonDocument(bank).toJson(QJsonDocument::Compact))<0||!output.commit())return 10;QSaveFile summary(root.filePath("canonical-inventory.json"));if(!summary.open(QIODevice::WriteOnly))return 15;if(summary.write(QJsonDocument(report).toJson())<0||!summary.commit())return 15;std::puts(QJsonDocument(report).toJson().constData());}
