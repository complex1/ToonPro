#include "reference3d_builtin.h"
#include "reference3d_catalog.h"
#include "reference3d_import.h"
#include "reference3d_pets.h"
#include "reference3d_pose_library.h"
#include "reference3d_review.h"
#include "ui_theme.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <cmath>

using namespace toon;
namespace {
struct Tile { QString title; QImage image; };
QString safeName(QString value) { return value.replace(QRegularExpression("[^a-zA-Z0-9_-]"), "-"); }
QJsonArray vector(QVector3D value) { return {value.x(), value.y(), value.z()}; }
QJsonObject counts(const ReferenceInstance &instance) {
    qint64 vertices=0,triangles=0,blended=0,drawnTriangles=0;
    for (const auto &mesh:instance.asset->meshes) for (const auto &primitive:mesh.primitives) {
        vertices+=primitive.vertices.size();triangles+=primitive.indices.size()/3;
        for (const auto &vertex:primitive.vertices) {
            int influences=0; for (float weight:vertex.weights) if (weight>0) ++influences;
            if(influences>1)++blended;
        }
    }
    for(const auto &node:instance.asset->nodes)if(node.meshIndex>=0)
        for(const auto &primitive:instance.asset->meshes[node.meshIndex].primitives)drawnTriangles+=primitive.indices.size()/3;
    return {{"vertices",vertices},{"triangles",triangles},{"blendedVertices",blended},
        {"drawnTriangles",drawnTriangles},
        {"nodes",instance.asset->nodes.size()},{"mappedBones",instance.asset->humanoidBones.size()},
        {"shapeParameters",referenceBodyShapeParameters(instance).size()},
        {"expressions",instance.asset->expressions.size()},{"assetId",instance.asset->id},
        {"provenance",instance.asset->metadata.value("provenance")}};
}
bool waitForPreview(Reference3DReviewWindow &window) {
    QElapsedTimer timer;timer.start();
    while(!window.viewportReady()&&timer.elapsed()<20000){QCoreApplication::processEvents(QEventLoop::AllEvents,5);QThread::msleep(1);}
    return window.viewportReady()&&window.viewportError().isEmpty()&&!window.viewportImage().isNull();
}
bool sheet(const QVector<Tile> &tiles,const QString &path) {
    if(tiles.isEmpty())return false;
    constexpr int columns=4,width=280,height=348;
    QImage image(columns*width,((tiles.size()+columns-1)/columns)*height,QImage::Format_RGB32);
    image.fill(QColor(ui::panel));QPainter painter(&image);painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QFont font=painter.font();font.setPixelSize(15);painter.setFont(font);
    for(int i=0;i<tiles.size();++i){
        const QRect tile((i%columns)*width,(i/columns)*height,width,height);
        painter.setPen(QColor(ui::border));painter.drawRect(tile.adjusted(0,0,-1,-1));
        auto scaled=tiles[i].image.scaled(width-16,height-48,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        painter.drawImage(QPoint(tile.center().x()-scaled.width()/2,tile.y()+8),scaled);
        painter.setPen(QColor(ui::text));painter.drawText(tile.adjusted(8,height-38,-8,-4),Qt::AlignCenter|Qt::TextWordWrap,tiles[i].title);
    }
    painter.end();return image.save(path);
}
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);QApplication::setOrganizationName("ToonProTests");QApplication::setApplicationName("Reference3DAssetReview");applyTheme();
    const QString output=qEnvironmentVariable("TOON_REFERENCE3D_ASSET_REVIEW_DIR");
    if(output.isEmpty()||!QDir().mkpath(output)){std::fprintf(stderr,"Set TOON_REFERENCE3D_ASSET_REVIEW_DIR to an output directory.\n");return 2;}
    QTemporaryDir materials;Reference3DReviewWindow window(nullptr,materials.path());window.resize(1180,760);window.show();
    QCoreApplication::processEvents();QJsonArray records,controlChecks;QHash<QString,QVector<Tile>> sheets;QString error;
    auto capture=[&](const QString &id,const QString &label,const QString &group,const ReferenceInstance &instance,float yaw=25.f)->bool{
        const auto evaluation=evaluateReferenceInstance(instance,&error);
        if(!evaluation||!evaluation->bounds.valid){std::fprintf(stderr,"Evaluation %s: %s\n",qPrintable(id),qPrintable(error));return false;}
        ReferenceScene scene;scene.name=label;scene.instances={instance};scene.camera.target=evaluation->bounds.center();
        const auto span=evaluation->bounds.maximum-evaluation->bounds.minimum;
        const float aspect=float(window.viewportWidget()->width())/qMax(1,window.viewportWidget()->height());
        scene.camera.orthographic=true;scene.camera.orthoScale=qMax(span.y(),qMax(span.x(),span.z())/qMin(1.f,aspect))*1.20f;
        scene.camera.distance=qMax(1.f,span.length()*2.5f);scene.camera.yaw=yaw;scene.camera.pitch=8;
        if(!window.setScene(scene,&error)||!waitForPreview(window)){
            std::fprintf(stderr,"Preview %s: %s\n",qPrintable(id),qPrintable(error.isEmpty()?window.viewportError():error));return false;
        }
        const auto image=window.viewportImage();const auto filename=safeName(id)+".png";
        if(!image.save(QDir(output).filePath(filename)))return false;
        sheets[group].append({label,image});auto record=counts(instance);record["id"]=id;record["name"]=label;record["group"]=group;record["image"]=filename;
        record["boundsMinimum"]=vector(evaluation->bounds.minimum);record["boundsMaximum"]=vector(evaluation->bounds.maximum);records.append(record);
        return true;
    };
    const QVector<ReferenceStarterKind> kinds{ReferenceStarterKind::Figure,ReferenceStarterKind::Head,ReferenceStarterKind::Hand};
    const QStringList names{"Body","Head","Hand"};
    const bool catalogOnly=qEnvironmentVariableIsSet("TOON_REFERENCE3D_ASSET_REVIEW_CATALOG_ONLY");
    if(!catalogOnly)for(int k=0;k<kinds.size();++k){
        ReferenceInstance instance;instance.id="review";instance.name=names[k];instance.asset=createReferenceStarter(kinds[k],&error);
        if(!instance.asset){std::fprintf(stderr,"Starter: %s\n",qPrintable(error));return 1;}
        const auto original=evaluateReferenceInstance(instance,&error);if(!original)return 1;
        for(const auto &parameter:referenceBodyShapeParameters(instance))for(float value:{parameter.minimum,parameter.maximum}){
            if(std::abs(value-parameter.value)<1e-7f)continue;
            auto changed=instance;QElapsedTimer elapsed;elapsed.start();
            if(!setReferenceBodyShape(changed,parameter.id,value,&error)){std::fprintf(stderr,"Control %s: %s\n",qPrintable(parameter.id),qPrintable(error));return 1;}
            const double generationMs=elapsed.nsecsElapsed()/1000000.;
            const auto evaluated=evaluateReferenceInstance(changed,&error);if(!evaluated)return 1;
            double maximumDelta=0;
            if(evaluated->primitives.size()!=original->primitives.size())maximumDelta=1;
            else for(int p=0;p<evaluated->primitives.size();++p){
                const auto &a=original->primitives[p].vertices,&b=evaluated->primitives[p].vertices;
                if(a.size()!=b.size()){maximumDelta=1;break;}
                for(int v=0;v<a.size();++v)maximumDelta=qMax(maximumDelta,double((a[v].position-b[v].position).length()));
            }
            controlChecks.append(QJsonObject{{"model",names[k]},{"parameter",parameter.id},{"value",value},{"maximumVertexDeltaMeters",maximumDelta},{"generationMs",generationMs}});
            if(maximumDelta<1e-7){std::fprintf(stderr,"Control %s / %s has no geometry effect at %g\n",qPrintable(names[k]),qPrintable(parameter.id),double(value));return 1;}
        }
        for(int v=0;v<3;++v)if(!capture(names[k].toLower()+QString::number(v),names[k]+QStringList{" / front"," / three quarter"," / side"}[v],"base-models",instance,v==0?0.f:v==1?35.f:90.f))return 1;
        ReferenceScene saved;saved.name=names[k];saved.instances={instance};
        if(!saveReferenceScene(saved,QDir(output).filePath(names[k].toLower()+".toon3d"),&error))return 1;
    }
    if(!catalogOnly)for(const auto &preset:referenceStarterPresets()){
        ReferenceInstance instance;instance.id="shape";instance.name=preset.name;instance.asset=createReferenceStarter(preset.id,&error);
        if(!instance.asset||!capture("shape-"+preset.id,preset.name,preset.kind==ReferenceStarterKind::Head?"head-shapes":preset.kind==ReferenceStarterKind::Hand?"hand-shapes":"body-shapes",instance))return 1;
    }
    if(!catalogOnly)for(const auto &preset:referencePosePresets()){
        ReferenceInstance instance;instance.id="pose";instance.name=preset.name;instance.asset=createReferenceStarter(preset.handsOnly?ReferenceStarterKind::Hand:ReferenceStarterKind::Figure,&error);
        if(!instance.asset||!applyReferencePosePreset(preset.id,instance,&error)||!capture("pose-"+preset.id,preset.name,preset.handsOnly?"hand-poses":"body-poses",instance)){
            std::fprintf(stderr,"Pose %s: %s\n",qPrintable(preset.id),qPrintable(error));return 1;
        }
    }
    const auto importPath=qEnvironmentVariable("TOON_REFERENCE3D_IMPORT_REVIEW_FILE");
    if(!importPath.isEmpty()){
        const auto imported=importReferenceAsset(importPath);
        if(!imported.asset){std::fprintf(stderr,"Import review: %s\n",qPrintable(imported.error));return 1;}
        ReferenceInstance instance;instance.id="import-review";instance.name=imported.asset->name;instance.asset=imported.asset;
        for(int v=0;v<3;++v)if(!capture("import-"+QString::number(v),instance.name+QStringList{" / front"," / three quarter"," / side"}[v],"imported-model",instance,v==0?0.f:v==1?35.f:90.f))return 1;
    }
    ReferenceScene catalogScene;catalogScene.name="Anime characters and pets";
    int character=0,pet=0;
    for(const auto &entry:referenceModelCatalog()){
        if(entry.kind!=ReferenceCatalogKind::Anime&&entry.kind!=ReferenceCatalogKind::Pet)continue;
        ReferenceInstance instance;instance.id=entry.id;instance.name=entry.name;instance.asset=createReferenceCatalogAsset(entry.id,&error);
        if(!instance.asset){std::fprintf(stderr,"Catalog %s: %s\n",qPrintable(entry.id),qPrintable(error));return 1;}
        const QString group=entry.kind==ReferenceCatalogKind::Anime?"anime-models":"pet-models";
        for(int v=0;v<3;++v)if(!capture(entry.id+QString::number(v),entry.name+QStringList{" / front"," / three quarter"," / side"}[v],group,instance,v==0?0.f:v==1?35.f:90.f))return 1;
        if(entry.kind==ReferenceCatalogKind::Anime){
            for(const auto &expression:instance.asset->expressions){
                if(!QStringList{"happy","joy","angry","blink","fun"}.contains(expression.name,Qt::CaseInsensitive))continue;
                auto expressed=instance;if(!setReferenceExpression(expressed,expression.name,1,&error)||!capture(entry.id+"-"+expression.name,entry.name+" / "+expression.name,"anime-expressions",expressed,0))return 1;
            }
            for(const auto &pose:referencePosePresets()){
                auto posed=instance;if(!applyReferencePosePreset(pose.id,posed,&error)){
                    std::fprintf(stderr,"Anime pose %s / %s: %s\n",qPrintable(entry.id),qPrintable(pose.id),qPrintable(error));return 1;
                }
                if(!pose.handsOnly&&!capture(entry.id+"-"+pose.id,entry.name+" / "+pose.name,"anime-poses",posed))return 1;
            }
            instance.translation={character++? .6f:-.6f,0,0};
            for(const auto &pose:referencePosePresets())if(pose.name.contains("relaxed",Qt::CaseInsensitive)&&!pose.handsOnly){if(!applyReferencePosePreset(pose.id,instance,&error))return 1;break;}
        }else{
            for(const auto &pose:referencePetPosePresets(instance)){
                auto posed=instance;if(!applyReferencePetPosePreset(pose.id,posed,&error)||!capture(entry.id+"-"+pose.id,entry.name+" / "+pose.name,"pet-poses",posed))return 1;
            }
            instance.translation={float(pet++-1)*.7f,0,.65f};
        }
        catalogScene.instances.append(instance);
    }
    ReferenceBounds catalogBounds;for(const auto &instance:catalogScene.instances){const auto evaluated=evaluateReferenceInstance(instance,&error);if(!evaluated)return 1;catalogBounds.include(evaluated->bounds.minimum);catalogBounds.include(evaluated->bounds.maximum);}
    const auto catalogSpan=catalogBounds.maximum-catalogBounds.minimum;const float catalogAspect=float(window.viewportWidget()->width())/qMax(1,window.viewportWidget()->height());
    catalogScene.camera.orthographic=true;catalogScene.camera.target=catalogBounds.center();catalogScene.camera.orthoScale=qMax(catalogSpan.y(),qMax(catalogSpan.x(),catalogSpan.z())/qMin(1.f,catalogAspect))*1.25f;catalogScene.camera.yaw=8;catalogScene.camera.pitch=5;
    if(!saveReferenceScene(catalogScene,QDir(output).filePath("anime-pets-review.toon3d"),&error))return 1;
    if(!window.setScene(catalogScene,&error)||!waitForPreview(window)||!window.viewportImage().save(QDir(output).filePath("anime-pets-review.png")))return 1;
    ReferenceScene showroom;showroom.name="Body, head and hand review";
    if(!catalogOnly)for(int k=0;k<kinds.size();++k){
        ReferenceInstance instance;instance.id=QString("review-%1").arg(k);instance.name=names[k];instance.asset=createReferenceStarter(kinds[k],&error);
        if(!instance.asset)return 1;
        if(k==0){for(const auto &pose:referencePosePresets())if(!pose.handsOnly&&pose.name.contains("relaxed",Qt::CaseInsensitive)){if(!applyReferencePosePreset(pose.id,instance,&error))return 1;break;}}
        if(k==1){instance.translation={.95f,1.10f,0};instance.scale={1.5f,1.5f,1.5f};}
        if(k==2){instance.translation={.95f,.28f,0};instance.scale={2,2,2};}
        showroom.instances.append(instance);
    }
    showroom.camera.orthographic=true;showroom.camera.target={.20f,.9f,0};showroom.camera.orthoScale=2.5f;showroom.camera.yaw=18;showroom.camera.pitch=8;
    if(!catalogOnly&&!saveReferenceScene(showroom,QDir(output).filePath("models-review.toon3d"),&error))return 1;
    for(auto it=sheets.constBegin();it!=sheets.constEnd();++it)if(!sheet(it.value(),QDir(output).filePath(it.key()+"-sheet.png")))return 1;
    QJsonObject report{{"schemaVersion",1},{"renderer","Actual experimental review viewport; final-quality frames"},
        {"qtVersion",QString(qVersion())},{"records",records},{"controlChecks",controlChecks}};
    QFile file(QDir(output).filePath("manifest.json"));if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(report).toJson())<0)return 1;
    std::printf("Captured %lld model, shape and pose views in %s\n",static_cast<long long>(records.size()),qPrintable(output));return 0;
}
