#include "project.h"
#include "vector_brush_storage.h"
#include "scene_model_io.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <cmath>

namespace toon {
namespace {
const QByteArray magicV2 = "TOONPRO2\n";
const QByteArray magicV3 = "TOONPRO3\n";
const QByteArray magicV4 = "TOONPRO4\n";
const QByteArray magicV5 = "TOONPRO5\n";
const QByteArray magicV6 = "TOONPRO6\n";
const QByteArray magicV7 = "TOONPRO7\n";
const QByteArray magicV8 = "TOONPRO8\n";
const QByteArray magicV9 = "TOONPRO9\n";
const QByteArray magicV10 = "TOONPRO10\n";
const QByteArray magicV11 = "TOONPRO11\n";
const QByteArray magicV12 = "TOONPRO12\n";
const QByteArray magicV13 = "TOONPRO13\n";
const QByteArray magicV14 = "TOONPRO14\n";
const QByteArray magicV15 = "TOONPRO15\n";
const QByteArray magicV16 = "TOONPRO16\n";
const QByteArray magicV17 = "TOONPRO17\n";
const QByteArray magicV18 = "TOONPRO18\n";
const QByteArray magicV19 = "TOONPRO19\n";
const QByteArray magicV20 = "TOONPRO20\n";
const QByteArray magicV21 = "TOONPRO21\n";
const QByteArray magicV22 = "TOONPRO22\n";
const QByteArray magicV23 = "TOONPRO23\n";
const QByteArray magicV24 = "TOONPRO24\n";
const QByteArray magicV25 = "TOONPRO25\n";
const QByteArray magicV26 = "TOONPRO26\n";
const QByteArray magicV27 = "TOONPRO27\n";
const QByteArray magicV28 = "TOONPRO28\n";
const QByteArray magicV29 = "TOONPRO29\n";
const QByteArray magicV30 = "TOONPRO30\n";
const QByteArray magicV31 = "TOONPRO31\n";
const QByteArray magicV32 = "TOONPRO32\n";
const QByteArray magicV33 = "TOONPRO33\n";
const QByteArray magicV34 = "TOONPRO34\n";
const QByteArray magicV35 = "TOONPRO35\n";
const QByteArray magicV36 = "TOONPRO36\n";
const QByteArray magicV37 = "TOONPRO37\n";
constexpr qint64 maxManifest = 4 * 1024 * 1024;
constexpr qint64 maxBrushManifest = 32 * 1024 * 1024;
constexpr qint64 maxSkinManifest = 64 * 1024 * 1024;
constexpr qint64 maxContainer = 300LL * 1024 * 1024;
constexpr qint64 maxReferenceBlob = 64LL * 1024 * 1024;
constexpr qint64 maxCharacterSheetBlob = 65LL * 1024 * 1024;
constexpr qint64 maxBrushResourceBlob = 8LL * 1024 * 1024;
bool writeBlob(QDataStream &s, const QByteArray &data) {
    s << quint32(data.size());
    const auto hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    return s.writeRawData(hash.constData(), hash.size()) == hash.size()
        && s.writeRawData(data.constData(), data.size()) == data.size();
}
std::optional<QByteArray> readBlob(QDataStream &s, qint64 limit) {
    quint32 size = 0; s >> size;
    if (s.status() != QDataStream::Ok || size == 0 || size > limit || s.device()->bytesAvailable() < qint64(size) + 32) return {};
    const auto hash = s.device()->read(32);
    const auto data = s.device()->read(size);
    if (data.size() != size || QCryptographicHash::hash(data, QCryptographicHash::Sha256) != hash) return {};
    return data;
}
}
bool saveProject(const Project &p, const QString &path, QString *error, const std::function<bool()> &cancelled) {
    auto fail = [error](QString message) { if (error) *error = message; return false; };
    if (cancelled && cancelled()) return fail("Save cancelled.");
    const auto validation = p.validate();
    if (!validation.isEmpty()) return fail(validation);
    if (cancelled && cancelled()) return fail("Save cancelled.");
    const auto metadata = p.toJson();
    const auto manifest = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
    const auto &magic = metadata.value("version").toInt() >= 37 ? magicV37 : metadata.value("version").toInt() >= 36 ? magicV36 : metadata.value("version").toInt() >= 35 ? magicV35 : metadata.value("version").toInt() >= 34 ? magicV34 : metadata.value("version").toInt() >= 33 ? magicV33 : metadata.value("version").toInt() >= 32 ? magicV32 : metadata.value("version").toInt() >= 31 ? magicV31 : metadata.value("version").toInt() >= 30 ? magicV30 : metadata.value("version").toInt() >= 29 ? magicV29 : metadata.value("version").toInt() >= 28 ? magicV28 : metadata.value("version").toInt() >= 27 ? magicV27 : metadata.value("version").toInt() >= 26 ? magicV26 : metadata.value("version").toInt() >= 25 ? magicV25 : metadata.value("version").toInt() >= 24 ? magicV24 : metadata.value("version").toInt() >= 23 ? magicV23 : metadata.value("version").toInt() >= 22 ? magicV22 : metadata.value("version").toInt() >= 21 ? magicV21 : metadata.value("version").toInt() >= 20 ? magicV20 : magicV19;
    if (manifest.size() > maxSkinManifest) return fail("Project manifest exceeds the 64 MiB limit.");
    if (cancelled && cancelled()) return fail("Save cancelled.");
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::BigEndian);
    if (file.write(magic) != magic.size() || !writeBlob(stream, manifest)) return fail(file.errorString());
    for (const auto *shot : projectArtworkShots(p)) for (const auto &layer : shot->layers) if (layer.kind == LayerKind::Raster) for (const auto &tile : layer.tiles) {
        if (cancelled && cancelled()) return fail("Save cancelled.");
        QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
        if (!tile.save(&buffer, "PNG") || !writeBlob(stream, png)) return fail("Could not write artwork tile.");
        if (file.pos() > maxContainer) return fail("Project exceeds the 300 MB container limit.");
    }
    for (const auto *shot : projectArtworkShots(p)) for (const auto &reference : shot->references) {
        if (cancelled && cancelled()) return fail("Save cancelled.");
        QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
        if (!reference.image.save(&buffer, "PNG") || png.size() > maxReferenceBlob || !writeBlob(stream, png))
            return fail("Could not write reference image.");
        if (file.pos() > maxContainer) return fail("Project exceeds the 300 MB container limit.");
    }
    QMap<QString,QImage> resources;
    for (const auto *shot : projectArtworkShots(p)) for(const auto &layer:shot->layers) for(const auto &object:layer.objects) if(object.brushStroke) {
        const auto &brush=object.brushStroke->settings;
        for(const auto *image:{&brush.tipMask,&brush.texture.pattern,&brush.dual.tipMask,&brush.colorTip,&brush.surface.image})
            if(!image->isNull()) resources.insert(vectorBrushResourceId(*image),*image);
    }
    for(const auto &image:resources) {
        if (cancelled && cancelled()) return fail("Save cancelled.");
        QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
        if(!image.save(&buffer,"PNG") || png.size()>(image.format()==QImage::Format_ARGB32_Premultiplied ? 17LL*1024*1024 : maxBrushResourceBlob) || !writeBlob(stream,png))
            return fail("Could not write vector brush image resource.");
        if(file.pos()>maxContainer) return fail("Project exceeds the 300 MB container limit.");
    }
    QMap<QString, Model3DPtr> models;
    for (const auto *shot : projectArtworkShots(p)) for (const auto &layer : shot->layers)
        if (layer.scene3d) for (auto it = layer.scene3d->models.cbegin(); it != layer.scene3d->models.cend(); ++it)
            models.insert(it.key(), it.value());
    for (auto it = models.cbegin(); it != models.cend(); ++it) {
        if (cancelled && cancelled()) return fail("Save cancelled.");
        QString modelError;
        const QByteArray data = serializeModel3D(*it.value(), &modelError);
        if (data.isEmpty()) return fail(modelError);
        if (QString::fromLatin1(data.mid(8, 32).toHex()) != it.key())
            return fail("3D model content does not match its identifier.");
        if (file.pos() + data.size() + 36 > maxContainer) return fail("Project exceeds the 300 MB container limit.");
        if (!writeBlob(stream, data)) return fail("Could not write 3D model resource.");
    }
    for (const auto &asset : audioResources(p)) {
        if (cancelled && cancelled()) return fail("Save cancelled.");
        if (file.pos() + asset->pcm.size() + 36 > maxContainer) return fail("Project exceeds the 300 MB container limit.");
        if (!writeBlob(stream, asset->pcm)) return fail("Could not write audio resource.");
    }
    // Format 17 appends masks after all older resource sections. QMap ordering
    // is shared with the manifest, so no resource can silently change owners.
    for (const auto *shot : projectArtworkShots(p)) for (const auto &layer : shot->layers) if (layer.mask)
        for (const auto &tile : layer.mask->tiles) {
            if (cancelled && cancelled()) return fail("Save cancelled.");
        QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
            if (!tile.save(&buffer, "PNG") || png.size() > 1024 * 1024)
                return fail("Could not encode layer mask tile.");
            if (file.pos() + png.size() + 36 > maxContainer) return fail("Project exceeds the 300 MB container limit.");
            if (!writeBlob(stream, png)) return fail("Could not write layer mask tile.");
        }
    // Format 27 appends the shared character library once, never per shot.
    for (const auto &character : p.characters) for (const auto &sheet : character.sheets) {
        if (cancelled && cancelled()) return fail("Save cancelled.");
        QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
        if (!sheet.image.save(&buffer, "PNG") || png.size() > maxCharacterSheetBlob)
            return fail("Could not encode character model sheet.");
        if (file.pos() + png.size() + 36 > maxContainer) return fail("Project exceeds the 300 MB container limit.");
        if (!writeBlob(stream, png)) return fail("Could not write character model sheet.");
    }
    // Format 29 appends portable light-table snapshots after the older resources.
    for (const auto &pin : p.drawingAids.lightTable.pins) {
        if (cancelled && cancelled()) return fail("Save cancelled.");
        QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
        if (!pin.image.save(&buffer, "PNG") || png.size() > 17LL * 1024 * 1024)
            return fail("Could not encode light-table snapshot.");
        if (file.pos() + png.size() + 36 > maxContainer) return fail("Project exceeds the 300 MB container limit.");
        if (!writeBlob(stream, png)) return fail("Could not write light-table snapshot.");
    }
    if (cancelled && cancelled()) return fail("Save cancelled.");
    if (stream.status() != QDataStream::Ok || !file.commit()) return fail(file.errorString());
    return true;
}
std::optional<Project> loadProject(const QString &path, QString *error) {
    auto fail = [error](QString message) -> std::optional<Project> { if (error) *error = message; return {}; };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(file.errorString());
    const auto prefix = file.peek(magicV19.size());
    int version = 1;
    qsizetype prefixSize = 0;
    if (prefix.startsWith("TOONPRO")) {
        const QList<QByteArray> headers{magicV2, magicV3, magicV4, magicV5, magicV6, magicV7, magicV8, magicV9, magicV10, magicV11, magicV12, magicV13, magicV14, magicV15, magicV16, magicV17, magicV18, magicV19, magicV20, magicV21, magicV22, magicV23, magicV24, magicV25, magicV26, magicV27, magicV28, magicV29, magicV30, magicV31, magicV32, magicV33, magicV34, magicV35, magicV36, magicV37};
        bool recognized = false;
        for (int index = 0; index < headers.size(); ++index) if (prefix.startsWith(headers[index])) {
            version = index + 2;
            prefixSize = headers[index].size();
            recognized = true;
            break;
        }
        if (!recognized) return fail("Unsupported Toon Pro project container version.");
    }
    const bool binary = version != 1;
    if (file.size() > (binary ? maxContainer : maxManifest))
        return fail(binary ? "Project exceeds the 300 MB limit." : "Project manifest exceeds the 4 MB limit.");
    QDataStream stream(&file); stream.setByteOrder(QDataStream::BigEndian);
    QByteArray bytes;
    if (binary) {
        file.read(prefixSize);
        const auto data = readBlob(stream, version>=11 ? maxSkinManifest : version>=8 ? maxBrushManifest : maxManifest);
        if (!data) return fail("Damaged project manifest or checksum.");
        bytes = *data;
    } else bytes = file.readAll();
    QJsonParseError parse;
    const auto json = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !json.isObject()) return fail("Invalid project JSON: " + parse.errorString());
    if (json.object().value("version") != version)
        return fail("Project version does not match its container format.");
    auto project = Project::fromJson(json.object(), error);
    if (!project || !binary) return project;
    int count = 0;
    auto shots = json.object().value("shots").toArray();
    for (const auto &symbol : json.object().value("symbols").toArray()) shots.append(symbol.toObject().value("content"));
    const auto artwork = projectArtworkShots(*project);
    for (int si = 0; si < shots.size(); ++si) {
        const auto layers = shots[si].toObject().value("layers").toArray();
        for (int li = 0; li < layers.size(); ++li) {
            auto &layer = artwork[si]->layers[li];
            for (const auto &value : layers[li].toObject().value("tiles").toArray()) {
                if (++count > maxProjectTiles) return fail("Project exceeds the 1024-tile memory limit.");
                const auto key = value.toArray();
                if (key.size() != 2) return fail("Invalid tile position.");
                for (const auto &coordinate : key) {
                    const double n = coordinate.toDouble(1000);
                    if (!coordinate.isDouble() || !std::isfinite(n) || std::abs(n) > 128 || n != std::floor(n))
                        return fail("Invalid tile coordinate.");
                }
                const TileKey position{key[0].toInt(), key[1].toInt()};
                if (layer.tiles.contains(position)) return fail("Duplicate artwork tile.");
                auto data = readBlob(stream, 1024 * 1024);
                if (!data) return fail("Damaged or missing artwork tile/checksum.");
                QBuffer buffer(&*data); buffer.open(QIODevice::ReadOnly);
                QImageReader reader(&buffer, "PNG"); reader.setAutoDetectImageFormat(false);
                if (reader.size() != QSize(tileSize, tileSize)) return fail("Invalid tile dimensions.");
                const auto image = reader.read();
                if (image.isNull()) return fail("Could not decode artwork tile.");
                layer.tiles.insert(position, image.convertToFormat(QImage::Format_ARGB32_Premultiplied));
            }
        }
    }
    if (version >= 3) for (int si = 0; si < shots.size(); ++si) {
        const auto references = shots[si].toObject().value("references").toArray();
        for (int ri = 0; ri < references.size(); ++ri) {
            const auto metadata = references[ri].toObject();
            const QSize expectedSize(metadata.value("width").toInt(), metadata.value("height").toInt());
            auto data = readBlob(stream, maxReferenceBlob);
            if (!data) return fail("Damaged or missing reference image/checksum.");
            QBuffer buffer(&*data); buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer, "PNG"); reader.setAutoDetectImageFormat(false);
            // Manifest sizes and the total decoded-pixel budget were checked before reading any image.
            if (reader.size() != expectedSize) return fail("Reference image dimensions do not match the project manifest.");
            const auto image = reader.read();
            if (image.isNull() || image.size() != expectedSize) return fail("Could not decode reference image.");
            artwork[si]->references[ri].image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        }
    }
    if(version>=8) {
        QMap<QString,QImage> resources;
        for(const auto &value:json.object().value("brushResources").toArray()) {
            const auto descriptor=value.toObject();
            const QSize expected(descriptor.value("width").toInt(),descriptor.value("height").toInt());
            const bool colored = descriptor.value("format") == "rgba8";
            auto data=readBlob(stream,colored ? 17LL*1024*1024 : maxBrushResourceBlob);
            if(!data) return fail("Damaged or missing vector brush image/checksum.");
            QBuffer buffer(&*data); buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer,"PNG"); reader.setAutoDetectImageFormat(false);
            // Every dimension and the aggregate decoded-pixel budget were
            // checked in Project::fromJson before any resource allocation.
            if(reader.size()!=expected) return fail("Vector brush image dimensions do not match the project manifest.");
            auto image=reader.read();
            if(image.isNull() || image.size()!=expected || (!colored && (image.hasAlphaChannel() || !image.isGrayscale())))
                return fail("Vector brush resource is not a valid PNG in its declared color format.");
            image=image.convertToFormat(colored ? QImage::Format_ARGB32_Premultiplied : QImage::Format_Grayscale8);
            const auto id=descriptor.value("id").toString();
            if(vectorBrushResourceId(image)!=id) return fail("Vector brush resource content does not match its identifier.");
            resources.insert(id,image);
        }
        for(int si=0;si<shots.size();++si) {
            const auto layers=shots[si].toObject().value("layers").toArray();
            for(int li=0;li<layers.size();++li) {
                const auto objects=layers[li].toObject().value("objects").toArray();
                for(int oi=0;oi<objects.size();++oi) {
                    auto &object=artwork[si]->layers[li].objects[oi];
                    if(!object.brushStroke) continue;
                    const auto metadata=objects[oi].toObject().value("brushStroke").toObject();
                    auto &brush=object.brushStroke->settings;
                    brush.tipMask=resources.value(metadata.value("tipResource").toString());
                    brush.colorTip=resources.value(metadata.value("colorTipResource").toString());
                    brush.surface.image=resources.value(metadata.value("surfaceResource").toString());
                    brush.texture.pattern=resources.value(metadata.value("textureResource").toString());
                    brush.dual.tipMask=resources.value(metadata.value("dualTipResource").toString());
                }
            }
        }
        project->m_brushResourcesLoaded=true;
    }
    if (version >= 9) {
        QHash<QString, Model3DPtr> models;
        qint64 decodedBytes = 0;
        for (const auto &value : json.object().value("modelResources").toArray()) {
            const auto id = value.toObject().value("id").toString();
            auto data = readBlob(stream, maxModel3DBytes);
            if (!data) return fail("Damaged or missing 3D model resource/checksum.");
            if (QString::fromLatin1(data->mid(8, 32).toHex()) != id)
                return fail("3D model content does not match its identifier.");
            QString modelError;
            auto model = deserializeModel3D(*data, &modelError);
            if (!model || model->id != id) return fail(modelError.isEmpty() ? "Invalid 3D model resource." : modelError);
            QSet<qint64> textureKeys;
            for (const auto &mesh : model->meshes) {
                decodedBytes += mesh.positions.size() * qint64(sizeof(QVector3D))
                    + mesh.normals.size() * qint64(sizeof(QVector3D))
                    + mesh.texCoords.size() * qint64(sizeof(QVector2D))
                    + mesh.indices.size() * qint64(sizeof(quint32));
                if (!mesh.baseColorTexture.isNull() && !textureKeys.contains(mesh.baseColorTexture.cacheKey())) {
                    textureKeys.insert(mesh.baseColorTexture.cacheKey());
                    decodedBytes += mesh.baseColorTexture.sizeInBytes();
                }
            }
            if (decodedBytes > maxProjectModelDecodedBytes) return fail("Project 3D models exceed the 256 MB decoded-memory limit.");
            models.insert(id, model);
        }
        for (int si = 0; si < shots.size(); ++si) {
            const auto layers = shots[si].toObject().value("layers").toArray();
            for (int li = 0; li < layers.size(); ++li) {
                auto &layer = artwork[si]->layers[li];
                if (layer.kind != LayerKind::Scene3D) continue;
                QString sceneError;
                layer.scene3d = scene3DFromJson(layers[li].toObject().value("scene3d").toObject(), models, &sceneError);
                if (!layer.scene3d) return fail(sceneError);
            }
        }
        project->m_sceneResourcesLoaded = true;
    }
    if (version >= 15) {
        QMap<QString, AudioAssetPtr> assets;
        for (const auto &descriptor : json.object().value("audioResources").toArray()) {
            const auto metadata = descriptor.toObject();
            const qint64 expectedBytes = qint64(metadata.value("sampleFrames").toDouble()) * audioBytesPerSampleFrame;
            auto pcm = readBlob(stream, expectedBytes);
            if (!pcm || pcm->size() != expectedBytes) return fail("Damaged or missing audio samples/checksum.");
            QString audioError;
            auto asset = makeAudioAsset(metadata.value("name").toString(), std::move(*pcm), &audioError);
            if (!asset || asset->id != metadata.value("id").toString()) return fail("Audio resource content does not match its identifier.");
            assets.insert(asset->id, asset);
        }
        for (auto *shot : artwork) for (auto &clip : shot->audioClips) clip.asset = assets.value(clip.asset->id);
        project->m_audioResourcesLoaded = true;
    }
    if (version >= 17) {
        qint64 decodedMaskBytes = 0;
        for (auto *shot : artwork) for (auto &layer : shot->layers) if (layer.mask)
            for (auto tile = layer.mask->tiles.begin(); tile != layer.mask->tiles.end(); ++tile) {
                decodedMaskBytes += qint64(tileSize) * tileSize;
                if (decodedMaskBytes > qint64(maxProjectMaskTiles) * tileSize * tileSize)
                    return fail("Project masks exceed the 64 MiB decoded-memory limit.");
                auto data = readBlob(stream, 1024 * 1024);
                if (!data) return fail("Damaged or missing layer mask tile/checksum.");
                QBuffer buffer(&*data); buffer.open(QIODevice::ReadOnly);
                QImageReader reader(&buffer, "PNG"); reader.setAutoDetectImageFormat(false);
                if (reader.size() != QSize(tileSize, tileSize)) return fail("Invalid layer mask tile dimensions.");
                // Reject RGB, indexed, alpha and 16-bit images before decoding:
                // masks have one byte of coverage per pixel, never color data.
                if (data->size() < 33 || data->mid(0, 8) != QByteArray::fromHex("89504e470d0a1a0a")
                    || data->mid(12, 4) != "IHDR" || quint8((*data)[24]) != 8 || quint8((*data)[25]) != 0)
                    return fail("Layer mask resources must be 8-bit grayscale PNGs.");
                const auto image = reader.read();
                if (image.isNull() || image.size() != QSize(tileSize, tileSize) || image.format() != QImage::Format_Grayscale8
                    || image.sizeInBytes() != qint64(tileSize) * tileSize)
                    return fail("Could not decode Grayscale8 layer mask tile.");
                tile.value() = image;
            }
        project->m_maskResourcesLoaded = true;
    }
    if (version >= 27) {
        const auto characters = json.object().value("characters").toArray();
        for (int ci = 0; ci < characters.size(); ++ci) {
            const auto sheets = characters[ci].toObject().value("sheets").toArray();
            for (int si = 0; si < sheets.size(); ++si) {
                const auto metadata = sheets[si].toObject();
                const QSize expected(metadata.value("width").toInt(), metadata.value("height").toInt());
                auto data = readBlob(stream, maxCharacterSheetBlob);
                if (!data) return fail("Damaged or missing character model sheet/checksum.");
                QBuffer buffer(&*data); buffer.open(QIODevice::ReadOnly);
                QImageReader reader(&buffer, "PNG"); reader.setAutoDetectImageFormat(false);
                // Count, dimensions and aggregate pixels were validated before
                // reading any blobs. Reject high-depth files before allocation.
                if (reader.size() != expected) return fail("Character model sheet dimensions do not match the project manifest.");
                if (data->size() < 33 || data->mid(0, 8) != QByteArray::fromHex("89504e470d0a1a0a")
                    || data->mid(12, 4) != "IHDR" || quint8((*data)[24]) > 8)
                    return fail("Character model sheets must use 8-bit PNG images.");
                const auto image = reader.read();
                if (image.isNull() || image.size() != expected) return fail("Could not decode character model sheet.");
                project->characters[ci].sheets[si].image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                if (project->characters[ci].sheets[si].image.isNull()) return fail("Could not load character model sheet pixels.");
            }
        }
    }
    if (version >= 29) {
        const auto pins = json.object().value("drawingAids").toObject().value("lightTable").toObject().value("pins").toArray();
        for (int index = 0; index < pins.size(); ++index) {
            const auto metadata = pins[index].toObject();
            const QSize expected(metadata.value("width").toInt(), metadata.value("height").toInt());
            auto data = readBlob(stream, 17LL * 1024 * 1024);
            if (!data) return fail("Damaged or missing light-table snapshot/checksum.");
            QBuffer buffer(&*data); buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer, "PNG"); reader.setAutoDetectImageFormat(false);
            if (reader.size() != expected) return fail("Light-table snapshot dimensions do not match the manifest.");
            if (data->size() < 33 || data->mid(0, 8) != QByteArray::fromHex("89504e470d0a1a0a")
                || data->mid(12, 4) != "IHDR" || quint8((*data)[24]) > 8)
                return fail("Light-table snapshots must use 8-bit PNG images.");
            const auto image = reader.read();
            if (image.isNull() || image.size() != expected) return fail("Could not decode light-table snapshot.");
            auto &pin = project->drawingAids.lightTable.pins[index];
            pin.image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            if (pin.image.isNull()) return fail("Could not load light-table snapshot pixels.");
        }
    }
    if (!file.atEnd()) return fail("Unexpected trailing project data.");
    if (version >= 7) {
        for (auto *shot : artwork) for (auto &layer : shot->layers) if (layerHasEditableObjects(layer)) {
            const int overscan = shot->sheet.overscan;
            QRectF bounds(-overscan,-overscan,project->width+2*overscan,project->height+2*overscan);
            const int index = int(&layer - shot->layers.data());
            if (layerIsAnnotation(shot->layers,index)) bounds.adjust(-shot->sheet.left,-shot->sheet.top,shot->sheet.right,shot->sheet.bottom);
            if (!rebuildObjectLayer(layer,bounds,maxProjectTiles-count,error)) return {};
            count += layer.tiles.size();
        }
    }
    const auto validation = project->validate();
    if (!validation.isEmpty()) return fail(validation);
    return project;
}
} // namespace toon
