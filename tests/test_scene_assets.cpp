#include "scene3d.h"
#include "scene_model_io.h"
#include "model_library.h"
#include "document.h"
#include "advanced_transform.h"
#include "clipboard_tools.h"
#include "object_tools.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QBuffer>
#include <QtEndian>
#include <limits>
#include <cstring>
using namespace toon;
namespace {
void put(QByteArray &b,float value){quint32 bits;std::memcpy(&bits,&value,4);char bytes[4];qToLittleEndian(bits,bytes);b.append(bytes,4);}
void putU32(QByteArray &b,quint32 value){char bytes[4];qToLittleEndian(value,bytes);b.append(bytes,4);}
QJsonObject triangle(QByteArray &buffer){buffer.clear();for(float f:{0.f,0.f,0.f,1.f,0.f,0.f,0.f,1.f,0.f})put(buffer,f);
    return {{"asset",QJsonObject{{"version","2.0"}}},{"buffers",QJsonArray{QJsonObject{{"byteLength",buffer.size()},{"uri","data:application/octet-stream;base64,"+QString::fromLatin1(buffer.toBase64())}}}},
        {"bufferViews",QJsonArray{QJsonObject{{"buffer",0},{"byteLength",buffer.size()}}}},
        {"accessors",QJsonArray{QJsonObject{{"bufferView",0},{"componentType",5126},{"count",3},{"type","VEC3"}}}},
        {"meshes",QJsonArray{QJsonObject{{"primitives",QJsonArray{QJsonObject{{"attributes",QJsonObject{{"POSITION",0}}}}}}}}},
        {"nodes",QJsonArray{QJsonObject{{"mesh",0},{"translation",QJsonArray{2,3,4}}}}},{"scenes",QJsonArray{QJsonObject{{"nodes",QJsonArray{0}}}}},{"scene",0}};
}
QString save(const QTemporaryDir &dir,const QJsonObject &json,const QString &name="triangle.gltf"){auto path=dir.filePath(name);QFile f(path);if(!f.open(QIODevice::WriteOnly))return {};f.write(QJsonDocument(json).toJson(QJsonDocument::Compact));return path;}
QByteArray glb(QJsonObject json,const QByteArray &binary){auto buffers=json.value("buffers").toArray();auto b=buffers[0].toObject();b.remove("uri");buffers[0]=b;json["buffers"]=buffers;QByteArray j=QJsonDocument(json).toJson(QJsonDocument::Compact);while(j.size()%4)j+=' ';QByteArray bin=binary;while(bin.size()%4)bin+='\0';QByteArray out;putU32(out,0x46546c67);putU32(out,2);putU32(out,quint32(12+8+j.size()+8+bin.size()));putU32(out,quint32(j.size()));putU32(out,0x4e4f534a);out+=j;putU32(out,quint32(bin.size()));putU32(out,0x004e4942);out+=bin;return out;}
}
class SceneAssetTests:public QObject {
    Q_OBJECT
private slots:
    void builtInsAndRoundTrip(){const auto assets=builtInModels3D();QCOMPARE(assets.size(),5);QSet<QString> ids;for(const auto &a:assets){QVERIFY(a);QVERIFY(a->bounds.valid);QVERIFY(a->bounds.minimum.y()>=-.001f);QVERIFY2(validateModel3D(*a).isEmpty(),qPrintable(validateModel3D(*a)));QVERIFY(!ids.contains(a->id));ids<<a->id;QString e;auto bytes=serializeModel3D(*a,&e);QVERIFY2(!bytes.isEmpty(),qPrintable(e));auto decoded=deserializeModel3D(bytes,&e);QVERIFY2(decoded,qPrintable(e));QCOMPARE(decoded->id,a->id);QCOMPARE(decoded->bounds,a->bounds);QCOMPARE(serializeModel3D(*decoded),bytes);}}
    void importTriangleAndGlb(){QTemporaryDir dir;QByteArray buffer;auto j=triangle(buffer);QString e;auto a=importModel3D(save(dir,j),&e);QVERIFY2(a,qPrintable(e));QCOMPARE(a->meshes.size(),1);QCOMPARE(a->meshes[0].positions.size(),3);QCOMPARE(a->bounds.minimum,QVector3D(2,3,4));QCOMPARE(a->bounds.maximum,QVector3D(3,4,4));QVERIFY(a->meshes[0].normals[0].z()>.9f);QFile f(dir.filePath("triangle.glb"));QVERIFY(f.open(QIODevice::WriteOnly));f.write(glb(j,buffer));f.close();auto binary=importModel3D(f.fileName(),&e);QVERIFY2(binary,qPrintable(e));QCOMPARE(binary->id,a->id);}
    void matrixAndNestedTransforms(){QTemporaryDir dir;QByteArray buffer;auto j=triangle(buffer);j["nodes"]=QJsonArray{QJsonObject{{"children",QJsonArray{1}},{"translation",QJsonArray{1,2,3}}},QJsonObject{{"mesh",0},{"matrix",QJsonArray{2,0,0,0,0,2,0,0,0,0,2,0,4,5,6,1}}}};QString e;auto a=importModel3D(save(dir,j),&e);QVERIFY2(a,qPrintable(e));QCOMPARE(a->bounds.minimum,QVector3D(5,7,9));QCOMPARE(a->bounds.maximum,QVector3D(7,9,9));}
    void invalidImports_data(){QTest::addColumn<QString>("issue");for(const auto *name:{"skin","animation","extension","line","sparse","badindex","cycle","accessoroverrun","nan","blend","outside","singular","badmode","badstride","badtype"})QTest::newRow(name)<<QString::fromLatin1(name);}
    void invalidImports(){QFETCH(QString,issue);QTemporaryDir dir;QByteArray buffer;auto j=triangle(buffer);auto meshes=j["meshes"].toArray();auto mesh=meshes[0].toObject();auto primitives=mesh["primitives"].toArray();auto p=primitives[0].toObject();auto accessors=j["accessors"].toArray();auto a=accessors[0].toObject();
        if(issue=="skin")j["skins"]=QJsonArray{QJsonObject{}};
        if(issue=="animation")j["animations"]=QJsonArray{QJsonObject{}};
        if(issue=="extension")j["extensionsRequired"]=QJsonArray{"KHR_draco_mesh_compression"};
        if(issue=="line")p["mode"]=1;
        if(issue=="sparse")a["sparse"]=QJsonObject{};
        if(issue=="badindex")p["indices"]=99;
        if(issue=="cycle")j["nodes"]=QJsonArray{QJsonObject{{"mesh",0},{"children",QJsonArray{0}}}};
        if(issue=="accessoroverrun")a["count"]=4;
        if(issue=="nan"){buffer.clear();put(buffer,std::numeric_limits<float>::quiet_NaN());for(int i=0;i<8;++i)put(buffer,0);j["buffers"]=QJsonArray{QJsonObject{{"byteLength",buffer.size()},{"uri","data:application/octet-stream;base64,"+QString::fromLatin1(buffer.toBase64())}}};}
        if(issue=="blend"){p["material"]=0;j["materials"]=QJsonArray{QJsonObject{{"alphaMode","BLEND"}}};}
        if(issue=="outside")j["buffers"]=QJsonArray{QJsonObject{{"byteLength",36},{"uri","../outside.bin"}}};
        if(issue=="singular")j["nodes"]=QJsonArray{QJsonObject{{"mesh",0},{"scale",QJsonArray{0,1,1}}}};
        if(issue=="badmode")p["mode"]=4.5;
        if(issue=="badstride")j["bufferViews"]=QJsonArray{QJsonObject{{"buffer",0},{"byteLength",36},{"byteStride",4}}};
        if(issue=="badtype")a["componentType"]=5123;
        accessors[0]=a;j["accessors"]=accessors;primitives[0]=p;mesh["primitives"]=primitives;meshes[0]=mesh;j["meshes"]=meshes;QString error;auto result=importModel3D(save(dir,j),&error);QVERIFY2(!result,qPrintable(issue));QVERIFY(!error.isEmpty());}
    void externalResourcesAndTexture(){QTemporaryDir dir;QByteArray buffer;auto j=triangle(buffer);for(float f:{0.f,0.f,1.f,0.f,0.f,1.f})put(buffer,f);QFile bin(dir.filePath("mesh.bin"));QVERIFY(bin.open(QIODevice::WriteOnly));bin.write(buffer);bin.close();j["buffers"]=QJsonArray{QJsonObject{{"byteLength",buffer.size()},{"uri","mesh.bin"}}};j["bufferViews"]=QJsonArray{QJsonObject{{"buffer",0},{"byteLength",36}},QJsonObject{{"buffer",0},{"byteOffset",36},{"byteLength",24}}};auto accessors=j["accessors"].toArray();accessors<<QJsonObject{{"bufferView",1},{"componentType",5126},{"count",3},{"type","VEC2"}};j["accessors"]=accessors;
        j["meshes"]=QJsonArray{QJsonObject{{"primitives",QJsonArray{QJsonObject{{"attributes",QJsonObject{{"POSITION",0},{"TEXCOORD_0",1}}},{"material",0}}}}}};QImage image(2,2,QImage::Format_RGBA8888);image.fill(QColor(255,0,0,128));QVERIFY(image.save(dir.filePath("texture.png")));j["images"]=QJsonArray{QJsonObject{{"uri","texture.png"}}};j["textures"]=QJsonArray{QJsonObject{{"source",0}}};j["materials"]=QJsonArray{QJsonObject{{"alphaMode","MASK"},{"doubleSided",true},{"pbrMetallicRoughness",QJsonObject{{"baseColorTexture",QJsonObject{{"index",0}}},{"baseColorFactor",QJsonArray{.5,1,1,1}}}}}};QString e;auto a=importModel3D(save(dir,j),&e);QVERIFY2(a,qPrintable(e));QCOMPARE(a->meshes[0].alphaCutoff,.5f);QVERIFY(a->meshes[0].doubleSided);QCOMPARE(a->meshes[0].baseColorTexture.size(),QSize(2,2));auto bytes=serializeModel3D(*a);auto decoded=deserializeModel3D(bytes,&e);QVERIFY2(decoded,qPrintable(e));QCOMPARE(serializeModel3D(*decoded),bytes);QCOMPARE(decoded->meshes[0].baseColorTexture.pixelColor(0,0),QColor(255,0,0,128));}
    void corruptModelBytes(){auto a=builtInModels3D()[0];auto bytes=serializeModel3D(*a);QString e;auto damaged=bytes;damaged[damaged.size()-1]^=1;QVERIFY(!deserializeModel3D(damaged,&e));QVERIFY(e.contains("checksum"));QVERIFY(!deserializeModel3D(bytes.left(40),&e));QVERIFY(!deserializeModel3D(bytes+"x",&e));
        // A correctly checksummed payload still cannot request an unbounded name allocation.
        auto payload=bytes.mid(40);qToLittleEndian(quint32(0xffffffff),payload.data());auto forged=QByteArray("TOON3D01",8)+QCryptographicHash::hash(payload,QCryptographicHash::Sha256)+payload;QVERIFY(!deserializeModel3D(forged,&e));}
    void sceneRoundTripAndCamera(){Scene3D s;auto model=builtInModels3D()[0];s.models.insert(model->id,model);s.objects<<ModelInstance3D{"object","Cube",model->id};s.objects[0].position={3,0,0};s.camera=fitCamera3D(s.camera,sceneBounds(s));QVERIFY(validateScene3D(s).isEmpty());QCOMPARE(s.camera.target,QVector3D(3,.5,0));auto copy=s;QVERIFY(copy.models[model->id].get()==model.get());QString e;auto decoded=scene3DFromJson(scene3DToJson(s),s.models,&e);QVERIFY2(decoded,qPrintable(e));QCOMPARE(*decoded,s);QVERIFY(!scene3DFromJson(scene3DToJson(s),{},&e));s.objects[0].scale={0,1,1};QVERIFY(!validateScene3D(s).isEmpty());s.objects[0].scale={1,1,1};s.camera.pitch=90;QVERIFY(!validateScene3D(s).isEmpty());}
    void libraryPersistsAndProjectOwnsAssets(){QTemporaryDir dir;ModelLibrary library(dir.filePath("library"));QString e;QVERIFY(library.load(&e));QCOMPARE(library.entries().size(),5);QString builtIn=library.entries()[0].id;QVERIFY(library.setFavorite(builtIn,true));QVERIFY(library.setTags(builtIn,{" Favorite ","favorite"}));QVERIFY(!library.remove(builtIn));QByteArray b;auto j=triangle(b);QString id=library.importFile(save(dir,j),&e);QVERIFY2(!id.isEmpty(),qPrintable(e));QCOMPARE(library.entries().size(),6);auto projectOwned=library.entry(id)->model;QVERIFY(library.rename(id,"Character prop"));QVERIFY(library.setTags(id,{" Props ","props","City"}));QVERIFY(library.setFavorite(id,true));QCOMPARE(library.entry(id)->tags,QStringList({"Props","City"}));QCOMPARE(library.importFile(dir.filePath("triangle.gltf")),id);QCOMPARE(library.entries().size(),6);
        ModelLibrary reloaded(dir.filePath("library"));QVERIFY2(reloaded.load(&e),qPrintable(e));QCOMPARE(reloaded.entry(id)->name,QString("Character prop"));QVERIFY(reloaded.entry(id)->favorite);QVERIFY(reloaded.entry(builtIn)->favorite);QCOMPARE(reloaded.entry(builtIn)->tags,QStringList{"Favorite"});const auto &entryId=reloaded.entry(id)->id;QVERIFY(reloaded.remove(entryId));QVERIFY(!QFile::exists(dir.filePath("library/models/"+id+".toon3d")));QVERIFY(!reloaded.entry(id));QVERIFY(projectOwned);QCOMPARE(projectOwned->id,id);QVERIFY(!serializeModel3D(*projectOwned).isEmpty());}
    void drawingActionsPreserveThe3DReference() {
        auto project = Project::create();
        auto reference = RasterLayer::create("3D reference");
        reference.kind = LayerKind::Scene3D; reference.scene3d = Scene3D{};
        auto model = builtInModels3D().first();
        reference.scene3d->models.insert(model->id, model);
        reference.scene3d->objects << ModelInstance3D{"object", "Cube", model->id};
        project.shots[0].layers.prepend(reference);
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        Document document; document.load(project); document.selectLayer(0);
        QSignalSpy warning(&document, &Document::warning);
        const auto original = document.project().toJson();
        QString error;
        QVERIFY(!document.beginTransform());
        QVERIFY(!ObjectTools::rasterize(&document));
        QVERIFY(!ObjectTools::addLayer(&document, LayerKind::Scene3D));
        QVERIFY(!ClipboardTools::copy(&document, false, &error));
        QVERIFY(!ClipboardTools::copy(&document, true, &error));
        QVERIFY(!ClipboardTools::duplicate(&document, &error));
        document.duplicateLayers({reference.id});
        document.setLayerClipped(true);
        QVERIFY(!document.flattenImage());
        document.selectLayer(1);
        QVERIFY(!document.mergeDown());
        document.setLayerClipped(true);
        QCOMPARE(document.project().toJson(), original);
        QCOMPARE(document.history()->count(), 0);
        QVERIFY(warning.size() >= 10);

        document.groupLayers({project.shots[0].layers[0].id, project.shots[0].layers[1].id});
        const auto group = document.project().shots[0].layers[document.activeLayer()].id;
        const auto grouped = document.project().toJson();
        const int history = document.history()->count();
        document.duplicateLayers({group});
        AdvancedTransform transform;
        QVERIFY(!transform.begin(&document, {group}, false));
        QVERIFY(!document.externalEditActive());
        QCOMPARE(document.project().toJson(), grouped);
        QCOMPARE(document.history()->count(), history);
        QVERIFY(!document.flattenImage());

        // Reference selection does not prevent creating an independent raster layer.
        document.load(project); document.selectLayer(0);
        QImage pixels(4, 4, QImage::Format_ARGB32_Premultiplied); pixels.fill(Qt::red);
        QGuiApplication::clipboard()->setImage(pixels);
        QVERIFY(!ClipboardTools::paste(&document, false, QPointF(0, 0), &error));
        QVERIFY2(ClipboardTools::paste(&document, true, QPointF(0, 0), &error), qPrintable(error));
        QCOMPARE(document.project().shots[0].layers.size(), 3);
        QCOMPARE(document.project().shots[0].layers[0].scene3d, reference.scene3d);
        QVERIFY(document.project().validate().isEmpty());
    }
    void libraryBoundsDecodedTextureMemory() {
        QTemporaryDir directory; ModelLibrary library(directory.path()); QVERIFY(library.load());
        QImage image(4096, 4096, QImage::Format_RGBA8888); image.fill(Qt::white);
        Model3DPtr rejected;
        QString error;
        for (int i = 0; i < 4; ++i) {
            Model3D value = *builtInModels3D().first(); value.name = QString("Large texture %1").arg(i);
            value.meshes[0].baseColorTexture = image;
            auto model = finalizeModel3D(std::move(value), &error); QVERIFY2(model, qPrintable(error));
            if (i < 3) QVERIFY2(!library.addModel(model, {}, &error).isEmpty(), qPrintable(error));
            else { rejected = model; QVERIFY(library.addModel(model, {}, &error).isEmpty()); }
        }
        QVERIFY(error.contains("decoded-memory"));
        QCOMPARE(library.entries().size(), 8);
        // A hand-edited index must not bypass the same budget during reload.
        QFile index(directory.filePath("library.json")); QVERIFY(index.open(QIODevice::ReadOnly));
        auto json = QJsonDocument::fromJson(index.readAll()).object(); index.close();
        auto entries = json["entries"].toArray();
        entries.append(QJsonObject{{"id", rejected->id}, {"name", rejected->name}, {"tags", QJsonArray{}}, {"favorite", false}, {"builtIn", false}});
        json["entries"] = entries;
        QFile asset(directory.filePath("models/" + rejected->id + ".toon3d"));
        QVERIFY(asset.open(QIODevice::WriteOnly)); asset.write(serializeModel3D(*rejected)); asset.close();
        QVERIFY(index.open(QIODevice::WriteOnly)); index.write(QJsonDocument(json).toJson()); index.close();
        QVERIFY(!library.load(&error)); QVERIFY(error.contains("decoded-memory"));
        QVERIFY(!library.writable()); QCOMPARE(library.entries().size(), 8);
    }
    void brokenLibraryPreservesFile(){QTemporaryDir dir;ModelLibrary library(dir.path());QVERIFY(library.load());QVERIFY(library.setFavorite(library.entries()[0].id,true));QFile f(dir.filePath("library.json"));QVERIFY(f.open(QIODevice::WriteOnly));f.write("{\"version\":999}");f.close();QString e;QVERIFY(!library.load(&e));QVERIFY(!library.writable());QVERIFY(!library.setFavorite(library.entries()[0].id,false));QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),QByteArray("{\"version\":999}"));}
};
QTEST_MAIN(SceneAssetTests)
#include "test_scene_assets.moc"
