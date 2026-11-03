#include "model_library.h"
#include "scene_model_io.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace toon {
namespace {
constexpr int maxLibraryModels = 128;
constexpr qint64 maxLibraryBytes = 256LL*1024*1024;
// Each model package decodes its own texture pool. Charge that persisted form,
// even if two newly added models currently share the same QImage allocation.
qint64 decodedBytes(const Model3D &model) {
    qint64 bytes = 0; QSet<qint64> textures;
    for (const auto &mesh : model.meshes) {
        bytes += qint64(mesh.positions.size()) * sizeof(QVector3D)
            + qint64(mesh.normals.size()) * sizeof(QVector3D)
            + qint64(mesh.texCoords.size()) * sizeof(QVector2D)
            + qint64(mesh.indices.size()) * sizeof(quint32);
        if (!mesh.baseColorTexture.isNull() && !textures.contains(mesh.baseColorTexture.cacheKey())) {
            textures.insert(mesh.baseColorTexture.cacheKey());
            bytes += qint64(mesh.baseColorTexture.width()) * mesh.baseColorTexture.height() * 4;
        }
    }
    return bytes;
}
QVector<ModelLibraryEntry> defaults(){QVector<ModelLibraryEntry> entries;for(const auto &model:builtInModels3D())if(model)entries<<ModelLibraryEntry{model->id,model->name,{model->name.contains("Mannequin")?"Characters":model->name.contains("building")?"Environment":"Primitives"},false,true,model,{}};return entries;}
bool validId(const QString &id){static const QRegularExpression expression("^[0-9a-f]{64}$");return expression.match(id).hasMatch();}
QStringList normalizeTags(const QStringList &tags,QString &error){QStringList result;QSet<QString> seen;for(const auto &raw:tags){QString tag=raw.trimmed();if(tag.isEmpty())continue;if(tag.size()>40||tag.contains(',')||std::any_of(tag.cbegin(),tag.cend(),[](QChar c){return c.category()==QChar::Other_Control;})){error="Tags must contain 1–40 characters without commas or control characters.";return {};}
    if(!seen.contains(tag.toCaseFolded())){seen.insert(tag.toCaseFolded());result<<tag;}}
    if(result.size()>32){error="Use at most 32 tags per model.";return {};}return result;
}
}
ModelLibrary::ModelLibrary(QString directory,QObject *parent):QObject(parent),m_directory(std::move(directory)),m_entries(defaults()){}
const ModelLibraryEntry *ModelLibrary::entry(const QString &id) const {for(const auto &e:m_entries)if(e.id==id)return &e;return nullptr;}
bool ModelLibrary::fail(const QString &message,QString *error){if(error)*error=message;emit warning(message);return false;}
bool ModelLibrary::load(QString *error){
    auto entries=defaults();QFile index(QDir(m_directory).filePath("library.json"));if(!index.exists()){m_entries=std::move(entries);m_writable=true;if(error)error->clear();emit changed();return true;}
    auto reject=[&](const QString &message){m_writable=false;return fail(message,error);};
    if(!index.open(QIODevice::ReadOnly)||index.size()>2*1024*1024)return reject("Cannot read the 3D library index or it exceeds 2 MiB.");
    QJsonParseError parse;auto document=QJsonDocument::fromJson(index.readAll(),&parse);auto root=document.object();
    if(parse.error!=QJsonParseError::NoError||!document.isObject()||root.value("version").toInt(-1)!=1||!root.value("entries").isArray())return reject("Unsupported or damaged 3D library. Its original files were preserved.");
    auto list=root.value("entries").toArray();if(list.size()>maxLibraryModels+entries.size())return reject("3D library contains too many models.");QSet<QString> ids;qint64 totalBytes=0, totalDecoded=0;
    for(const auto &entry:entries) totalDecoded+=decodedBytes(*entry.model);
    for(const auto &v:list){if(!v.isObject())return reject("Malformed 3D library entry.");auto j=v.toObject();QString id=j.value("id").toString(),name=j.value("name").toString();if(!validId(id)||ids.contains(id)||name.trimmed().isEmpty()||name.size()>256||!j.value("tags").isArray()||!j.value("favorite").isBool()||!j.value("builtIn").isBool())return reject("Invalid 3D library metadata.");ids.insert(id);QStringList tags;for(const auto &t:j.value("tags").toArray()){if(!t.isString())return reject("Invalid 3D library tag.");tags<<t.toString();}QString tagError;tags=normalizeTags(tags,tagError);if(!tagError.isEmpty())return reject(tagError);
        if(j.value("builtIn").toBool()){auto it=std::find_if(entries.begin(),entries.end(),[&](const auto &e){return e.id==id;});if(it==entries.end())return reject("Unknown built-in 3D model.");it->tags=tags;it->favorite=j.value("favorite").toBool();continue;}
        if(std::any_of(entries.cbegin(),entries.cend(),[&](const auto &e){return e.id==id;}))return reject("Duplicate 3D model ID.");
        QFile file(QDir(m_directory).filePath("models/"+id+".toon3d"));if(!file.open(QIODevice::ReadOnly)||file.size()>maxModel3DBytes||file.size()<=40)return reject("Missing or oversized 3D library model: "+name);totalBytes+=file.size();if(totalBytes>maxLibraryBytes)return reject("3D library exceeds its 256 MiB model budget.");QString modelError;auto model=deserializeModel3D(file.readAll(),&modelError);if(!model||model->id!=id)return reject("Cannot load "+name+": "+(modelError.isEmpty()?"model ID mismatch":modelError));totalDecoded+=decodedBytes(*model);if(totalDecoded>maxLibraryBytes)return reject("3D library exceeds its 256 MiB decoded-memory budget.");entries<<ModelLibraryEntry{id,name,tags,j.value("favorite").toBool(),false,model,{}};
    }
    m_entries=std::move(entries);m_writable=true;if(error)error->clear();emit changed();return true;
}
bool ModelLibrary::commit(const QVector<ModelLibraryEntry> &entries,QString *error){if(!m_writable)return fail("The 3D library is read-only because it could not be loaded safely.",error);
    if(entries.size()>maxLibraryModels+builtInModels3D().size())return fail("The 3D library supports up to 128 imported models.",error);
    if(!QDir().mkpath(m_directory))return fail("Cannot create the 3D library folder.",error);
    QJsonArray array;for(const auto &entry:entries){QJsonArray tags;for(const auto &tag:entry.tags)tags.append(tag);array.append(QJsonObject{{"id",entry.id},{"name",entry.name},{"tags",tags},{"favorite",entry.favorite},{"builtIn",entry.builtIn}});}
    QSaveFile file(QDir(m_directory).filePath("library.json"));auto bytes=QJsonDocument(QJsonObject{{"version",1},{"entries",array}}).toJson();if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())return fail("Cannot save the 3D library index.",error);
    m_entries=entries;if(error)error->clear();emit changed();return true;
}
QString ModelLibrary::importFile(const QString &path,QString *error){QString message;auto model=importModel3D(path,&message);if(!model){fail(message,error);return {};}return addModel(model,model->name,error);}
QString ModelLibrary::addModel(Model3DPtr model,const QString &requestedName,QString *error){if(!m_writable){fail("The 3D library is read-only because it could not be loaded safely.",error);return {};}
    if(!model||!validId(model->id)){fail("Invalid 3D model.",error);return {};}
    if(entry(model->id)){if(error)error->clear();return model->id;}
    QString name=requestedName.isEmpty()?model->name:requestedName.trimmed();if(name.isEmpty()||name.size()>256){fail("Model names must contain 1–256 characters.",error);return {};}
    if(m_entries.size()>=maxLibraryModels+builtInModels3D().size()){fail("The 3D library supports up to 128 imported models.",error);return {};}
    qint64 decoded=decodedBytes(*model);for(const auto &e:m_entries)if(e.model)decoded+=decodedBytes(*e.model);
    if(decoded>maxLibraryBytes){fail("The 3D library exceeds its 256 MiB decoded-memory budget.",error);return {};}
    QString message;auto bytes=serializeModel3D(*model,&message);if(bytes.isEmpty()||QString::fromLatin1(bytes.mid(8,32).toHex())!=model->id){fail(message.isEmpty()?"Model contents do not match their ID.":message,error);return {};}
    qint64 total=bytes.size();for(const auto &e:m_entries)if(!e.builtIn)total+=QFileInfo(QDir(m_directory).filePath("models/"+e.id+".toon3d")).size();if(total>maxLibraryBytes){fail("The 3D library exceeds its 256 MiB model budget.",error);return {};}
    if(!QDir().mkpath(QDir(m_directory).filePath("models"))){fail("Cannot create the model storage folder.",error);return {};}
    QSaveFile file(QDir(m_directory).filePath("models/"+model->id+".toon3d"));if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()){fail("Cannot save the imported 3D model.",error);return {};}
    auto entries=m_entries;entries<<ModelLibraryEntry{model->id,name,{},false,false,model,{}};return commit(entries,error)?model->id:QString();
}
bool ModelLibrary::rename(const QString &id,const QString &rawName,QString *error){auto entries=m_entries;auto it=std::find_if(entries.begin(),entries.end(),[&](const auto &e){return e.id==id;});if(it==entries.end()||it->builtIn)return fail("Only imported models can be renamed.",error);QString name=rawName.trimmed();if(name.isEmpty()||name.size()>256)return fail("Model names must contain 1–256 characters.",error);it->name=name;return commit(entries,error);}
bool ModelLibrary::setTags(const QString &id,const QStringList &raw,QString *error){QString message;auto tags=normalizeTags(raw,message);if(!message.isEmpty())return fail(message,error);auto entries=m_entries;auto it=std::find_if(entries.begin(),entries.end(),[&](const auto &e){return e.id==id;});if(it==entries.end())return fail("Model is no longer in the library.",error);it->tags=tags;return commit(entries,error);}
bool ModelLibrary::setFavorite(const QString &id,bool favorite,QString *error){auto entries=m_entries;auto it=std::find_if(entries.begin(),entries.end(),[&](const auto &e){return e.id==id;});if(it==entries.end())return fail("Model is no longer in the library.",error);it->favorite=favorite;return commit(entries,error);}
bool ModelLibrary::remove(const QString &id,QString *error){auto entries=m_entries;auto it=std::find_if(entries.begin(),entries.end(),[&](const auto &e){return e.id==id;});if(it==entries.end()||it->builtIn)return fail("Only imported models can be removed.",error);const auto path=QDir(m_directory).filePath("models/"+id+".toon3d");entries.erase(it);if(!commit(entries,error))return false;QFile::remove(path);return true;}
} // namespace toon
