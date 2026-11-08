// Opt-in asset preparation utility; does not modify a drawing project.
#include "reference3d_import.h"
#include "reference3d_pose_library.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <cstdio>

using namespace toon;
namespace {
bool write(const QString &path,const QByteArray &bytes,QString *error) {
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()) {
        *error=file.errorString(); return false;
    }
    return true;
}
}
int main(int argc,char **argv) {
    QCoreApplication application(argc,argv); application.setApplicationName("toon_reference3d_prepare_asset");
    QCommandLineParser parser; parser.setApplicationDescription("Import, validate and package a reference GLB/VRM without flattening its rig or morphs.");
    parser.addHelpOption(); parser.addPositionalArgument("input","Prepared GLB/glTF/VRM path"); parser.addPositionalArgument("output","Canonical .toon3dasset output");
    parser.addOption({"name","Display name","name"}); parser.addOption({"catalog-id","Reference catalog ID","id"});
    parser.addOption({"source-url","Pinned source URL","url"}); parser.addOption({"report","Write technical JSON report","path"});
    parser.addOption({"yaw-180","Append a parent root rotating the complete source 180 degrees about Y, preserving all original node indices and bind data"});
    parser.addOption({"exclude-mesh","Detach nodes using this exact optional mesh name; retain original source geometry/rig data","name"});
    parser.addOption({"compress","Write a qCompress wrapper instead of raw canonical bytes"}); parser.process(application);
    const auto arguments=parser.positionalArguments(); if(arguments.size()!=2)parser.showHelp(1);
    const auto imported=importReferenceAsset(arguments[0]);
    if(!imported){std::fprintf(stderr,"Import: %s\n",qPrintable(imported.error));return 2;}
    auto source=*imported.asset;
    if(parser.isSet("name"))source.name=parser.value("name");
    if(parser.isSet("catalog-id")){source.metadata["referenceCatalogId"]=parser.value("catalog-id");source.metadata["referenceCatalogKind"]="anime";}
    if(parser.isSet("source-url"))source.metadata["referenceSourceUrl"]=parser.value("source-url");
    if(parser.isSet("yaw-180")) {
        const int root=source.nodes.size();for(auto &node:source.nodes)if(node.parentIndex<0)node.parentIndex=root;
        ReferenceNode orientation;orientation.name="Reference front orientation";orientation.rotation=QQuaternion(0,0,1,0);source.nodes.append(orientation);
        source.metadata["referenceFrontNormalization"]=QJsonObject{{"yawDegrees",180},{"method","Appended parent root; original node indices and inverse binds unchanged"}};
    }
    QJsonArray detached;
    for(const auto &name:parser.values("exclude-mesh")) {
        int changed=0;
        for(int index=0;index<source.nodes.size();++index) {
            auto &node=source.nodes[index];if(node.meshIndex<0||source.meshes[node.meshIndex].name!=name)continue;
            detached.append(QJsonObject{{"nodeIndex",index},{"nodeName",node.name},{"meshIndex",node.meshIndex},{"meshName",name}});
            node.meshIndex=-1;node.skinIndex=-1;++changed;
        }
        if(!changed){std::fprintf(stderr,"Optional mesh not found: %s\n",qPrintable(name));return 3;}
    }
    if(!detached.isEmpty())source.metadata["referenceDetachedOptionalMeshNodes"]=detached;
    QString error;auto asset=finalizeReferenceAsset(std::move(source),&error);
    if(!asset){std::fprintf(stderr,"Publish: %s\n",qPrintable(error));return 3;}
    ReferenceInstance instance;instance.id="prepared-reference";instance.name=asset->name;instance.asset=asset;
    const auto evaluated=evaluateReferenceInstance(instance,&error);
    if(!evaluated){std::fprintf(stderr,"Evaluate: %s\n",qPrintable(error));return 4;}
    const auto canonical=serializeReferenceAsset(*asset,&error);const auto roundTrip=deserializeReferenceAsset(canonical,&error);
    if(canonical.isEmpty()||!roundTrip||roundTrip->id!=asset->id||serializeReferenceAsset(*roundTrip,&error)!=canonical){std::fprintf(stderr,"Canonical roundtrip: %s\n",qPrintable(error));return 5;}
    const auto compressed=qCompress(canonical,9);const auto bytes=parser.isSet("compress")?compressed:canonical;
    if(!write(arguments[1],bytes,&error)){std::fprintf(stderr,"Write: %s\n",qPrintable(error));return 6;}
    QJsonArray expressions;for(const auto &expression:asset->expressions)expressions.append(expression.name);
    int vertices=0,triangles=0,morphs=0;for(const auto &mesh:asset->meshes)for(const auto &primitive:mesh.primitives){vertices+=primitive.vertices.size();triangles+=primitive.indices.size()/3;morphs+=primitive.morphTargets.size();}
    int activeVertices=0,activeTriangles=0;for(const auto &primitive:evaluated->primitives){activeVertices+=primitive.vertices.size();activeTriangles+=primitive.indices.size()/3;}
    auto posed=instance;const bool body=applyReferencePosePreset("body/relaxed-standing",posed,&error);const auto bodyError=error;
    posed=instance;const bool hand=applyReferencePosePreset("hands/fist",posed,&error);const auto handError=error;
    QJsonObject report{{"input",arguments[0]},{"output",arguments[1]},{"name",asset->name},{"assetId",asset->id},
        {"catalogId",parser.value("catalog-id")},{"sourceUrl",parser.value("source-url")},
        {"canonicalBytes",canonical.size()},{"compressedBytes",compressed.size()},{"fileBytes",bytes.size()},
        {"outputSha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())},
        {"encoding",parser.isSet("compress")?"qCompress(canonical,9)":"canonical"},
        {"frontYawDegrees",parser.isSet("yaw-180")?180:0},
        {"detachedOptionalMeshNodes",detached},
        {"nodes",asset->nodes.size()},{"skins",asset->skins.size()},{"bones",asset->humanoidBones.size()},
        {"vertices",vertices},{"triangles",triangles},{"morphTargets",morphs},{"images",asset->sourceImages.size()},
        {"activeVertices",activeVertices},{"activeTriangles",activeTriangles},
        {"expressions",expressions},{"height",evaluated->bounds.maximum.y()-evaluated->bounds.minimum.y()},
        {"bodyPoseApplies",body},{"bodyPoseError",bodyError},{"handPoseApplies",hand},{"handPoseError",handError},
        {"warnings",QJsonArray::fromStringList(imported.warnings)}};
    const auto json=QJsonDocument(report).toJson();
    if(parser.isSet("report")&&!write(parser.value("report"),json,&error)){std::fprintf(stderr,"Report: %s\n",qPrintable(error));return 7;}
    std::fwrite(json.constData(),1,size_t(json.size()),stdout);return 0;
}
