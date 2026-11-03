#include "project.h"
#include "symbol_poses.h"
#include "vector_brush_storage.h"
#include "scene_model_io.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <cmath>
#include <algorithm>

namespace toon {
QVector<const Shot *> projectArtworkShots(const Project &project) {
    QVector<const Shot *> result;
    result.reserve(project.shots.size() + project.symbols.size());
    for (const auto &shot : project.shots) result.append(&shot);
    for (const auto &symbol : project.symbols) result.append(&symbol.content);
    return result;
}
QVector<Shot *> projectArtworkShots(Project &project) {
    QVector<Shot *> result;
    result.reserve(project.shots.size() + project.symbols.size());
    for (auto &shot : project.shots) result.append(&shot);
    for (auto &symbol : project.symbols) result.append(&symbol.content);
    return result;
}
int projectTileCount(const Project &project, const QString &excludedLayerId) {
    int count = 0;
    for (const auto *shot : projectArtworkShots(project)) for (const auto &layer : shot->layers)
        if (layer.id != excludedLayerId) count += layer.tiles.size();
    return count;
}
int projectMaskTileCount(const Project &project, const QString &excludedLayerId) {
    int count = 0;
    for (const auto *shot : projectArtworkShots(project)) for (const auto &layer : shot->layers)
        if (layer.mask && layer.id != excludedLayerId) count += layer.mask->tiles.size();
    return count;
}
namespace {

QJsonArray artworkShotMetadata(const QJsonObject &manifest) {
    auto shots = manifest.value("shots").toArray();
    for (const auto &symbol : manifest.value("symbols").toArray())
        shots.append(symbol.toObject().value("content"));
    return shots;
}
QString symbolPlaybackKey(SymbolPlayback mode) {
    switch (mode) {
    case SymbolPlayback::Loop: return "loop";
    case SymbolPlayback::Once: return "once";
    case SymbolPlayback::SingleFrame: return "singleFrame";
    case SymbolPlayback::PingPong: return "pingPong";
    case SymbolPlayback::Poses: return "poses";
    }
    return {};
}
std::optional<SymbolPlayback> symbolPlaybackFromKey(const QString &key) {
    for (const auto mode : {SymbolPlayback::Loop, SymbolPlayback::Once, SymbolPlayback::SingleFrame, SymbolPlayback::PingPong, SymbolPlayback::Poses})
        if (symbolPlaybackKey(mode) == key) return mode;
    return {};
}

bool integer(const QJsonObject &o, const char *key, int &value) {
    const auto v = o.value(QLatin1String(key));
    const double n = v.toDouble(-1);
    if (!v.isDouble() || !std::isfinite(n) || n < 0 || n > 1000000 || std::floor(n) != n)
        return false;
    value = static_cast<int>(n);
    return true;
}
bool boundedInteger(const QJsonObject &o, const char *key, int minimum, int maximum, int &value) {
    const auto v = o.value(QLatin1String(key));
    const double n = v.toDouble();
    if (!v.isDouble() || !std::isfinite(n) || n < minimum || n > maximum || std::floor(n) != n)
        return false;
    value = static_cast<int>(n);
    return true;
}
bool validModelSheetName(const QString &name) {
    return !name.isEmpty() && name.size() <= 100 && name == name.trimmed()
        && std::none_of(name.cbegin(), name.cend(), [](QChar ch) {
            return ch.category() == QChar::Other_Control || ch == QChar::LineSeparator || ch == QChar::ParagraphSeparator;
        });
}
QString modelSheetCategoryKey(ModelSheetCategory category) {
    switch (category) {
    case ModelSheetCategory::Turnaround: return "turnaround";
    case ModelSheetCategory::Expression: return "expression";
    case ModelSheetCategory::Costume: return "costume";
    }
    return {};
}
QString characterColorCategoryKey(CharacterColorCategory category) {
    switch (category) {
    case CharacterColorCategory::Skin: return "skin";
    case CharacterColorCategory::Hair: return "hair";
    case CharacterColorCategory::Costume: return "costume";
    case CharacterColorCategory::Shadow: return "shadow";
    }
    return {};
}
bool validCharacterColor(const QColor &color) {
    if (!color.isValid()) return false;
    for (const qreal channel : {color.redF(), color.greenF(), color.blueF(), color.alphaF()})
        if (!std::isfinite(channel) || channel < 0 || channel > 1) return false;
    return true;
}
std::optional<QColor> characterColorFromJson(const QJsonValue &value) {
    if (!value.isString()) return {};
    const auto text = value.toString();
    if (text.size() != 9 || text[0] != '#') return {};
    for (const auto ch : QStringView(text).mid(1))
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))) return {};
    bool parsed = false;
    const auto rgba = text.mid(1).toUInt(&parsed, 16);
    return parsed ? std::optional<QColor>(QColor::fromRgba(rgba)) : std::nullopt;
}
QMap<QString,QImage> brushResources(const Project &project) {
    QMap<QString,QImage> resources;
    for (const auto *shot : projectArtworkShots(project)) for (const auto &layer:shot->layers) for (const auto &object:layer.objects) {
        if (!object.brushStroke) continue;
        const auto &settings=object.brushStroke->settings;
        for (const auto *image:{&settings.tipMask,&settings.texture.pattern,&settings.dual.tipMask,&settings.colorTip,&settings.surface.image})
            if (!image->isNull()) resources.insert(vectorBrushResourceId(*image),*image);
    }
    return resources;
}
QMap<QString, Model3DPtr> modelResources(const Project &project) {
    QMap<QString, Model3DPtr> resources;
    for (const auto *shot : projectArtworkShots(project)) for (const auto &layer : shot->layers)
        if (layer.scene3d) for (auto it = layer.scene3d->models.cbegin(); it != layer.scene3d->models.cend(); ++it)
            resources.insert(it.key(), it.value());
    return resources;
}
bool validateModelManifest(const QJsonObject &manifest, QString *error) {
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (!manifest.value("modelResources").isArray()) return fail("Missing 3D model resource list.");
    const auto resources = manifest.value("modelResources").toArray();
    if (resources.size() > maxProjectModelResources) return fail("Project has too many 3D model resources.");
    QSet<QString> ids, used;
    for (const auto &value : resources) {
        const auto descriptor = value.toObject();
        const auto id = descriptor.value("id").toString();
        if (!value.isObject() || descriptor.size() != 1 || !validVectorBrushResourceId(id) || ids.contains(id))
            return fail("Invalid or duplicate 3D model resource descriptor.");
        ids.insert(id);
    }
    for (const auto &shot : artworkShotMetadata(manifest)) for (const auto &value : shot.toObject().value("layers").toArray()) {
        const auto layer = value.toObject();
        if (layer.value("kind") != "scene3d") continue;
        if (!layer.value("scene3d").isObject()) return fail("Missing 3D scene metadata.");
        for (const auto &resource : layer.value("scene3d").toObject().value("models").toArray()) {
            const auto id = resource.toString();
            if (!ids.contains(id)) return fail("A 3D scene refers to a missing model resource.");
            used.insert(id);
        }
    }
    if (used != ids) return fail("Project contains unused 3D model resources.");
    return true;
}
bool validateResourceManifest(const QJsonObject &manifest, QString *error) {
    auto fail=[error](const QString &message) { if(error) *error=message; return false; };
    if (!manifest.value("brushResources").isArray()) return fail("Missing vector brush resource list.");
    const auto resources=manifest.value("brushResources").toArray();
    if (resources.size()>maxProjectVectorBrushResources) return fail("Project has too many vector brush resources.");
    QSet<QString> ids, colored; qint64 pixels=0;
    for (const auto &value:resources) {
        if (!value.isObject()) return fail("Invalid vector brush resource descriptor.");
        const auto resource=value.toObject(); int width,height;
        const auto id=resource.value("id").toString();
        const bool color = resource.value("format") == "rgba8";
        if ((resource.contains("format") && (!color || manifest.value("version").toInt() < 20)) ||
            resource.size()!=(color ? 4 : 3) || !validVectorBrushResourceId(id) || ids.contains(id)
            || !integer(resource,"width",width) || !integer(resource,"height",height)
            || width<1 || height<1 || width>maxVectorBrushResourceDimension || height>maxVectorBrushResourceDimension)
            return fail("Invalid or duplicate vector brush resource descriptor.");
        pixels+=qint64(width)*height;
        if(pixels>maxProjectVectorBrushResourcePixels) return fail("Vector brush resources exceed the 16 megapixel limit.");
        ids.insert(id);
        if (color) colored.insert(id);
    }
    QSet<QString> used;
    for (const auto &shot:artworkShotMetadata(manifest)) for (const auto &layer:shot.toObject().value("layers").toArray())
        for (const auto &object:layer.toObject().value("objects").toArray()) {
            const auto brush=object.toObject().value("brushStroke").toObject();
            for(const auto key:{"tipResource","textureResource","dualTipResource","colorTipResource","surfaceResource"}) if(brush.contains(key)) {
                const auto id=brush.value(key).toString();
                if(!ids.contains(id)) return fail("A vector brush refers to a missing image resource.");
                if ((QString::fromLatin1(key) == "colorTipResource" || QString::fromLatin1(key) == "surfaceResource") != colored.contains(id))
                    return fail("A vector brush refers to an image with the wrong color format.");
                used.insert(id);
            }
        }
    if(used!=ids) return fail("Project contains unused vector brush image resources.");
    return true;
}
}

QVector<CharacterColorSwatch> characterVariantColors(const CharacterModel &character, const QString &variantId) {
    if (variantId.isEmpty()) return character.colors;
    for (const auto &variant : character.colorVariants) if (variant.id == variantId) {
        auto colors = character.colors;
        for (auto &swatch : colors) {
            const auto found = variant.colors.constFind(swatch.id);
            if (found == variant.colors.cend()) return {};
            swatch.color = found.value();
        }
        return colors;
    }
    return {};
}

QVector<CharacterColorVariant> standardCharacterColorVariants(const QVector<CharacterColorSwatch> &colors) {
    QMap<QString, QColor> values;
    for (const auto &swatch : colors) values.insert(swatch.id, swatch.color);
    QVector<CharacterColorVariant> variants;
    for (const auto *name : {"Night", "Sunset", "Interior"})
        variants.append({QUuid::createUuid().toString(QUuid::WithoutBraces), QString::fromLatin1(name), values});
    return variants;
}

Project Project::create() {
    Project p;
    Shot shot;
    shot.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    shot.layers.append(RasterLayer::create());
    p.shots.append(shot);
    return p;
}

QString Project::validateStructure() const {
    if (movie.trimmed().isEmpty() || movie.size() > 200)
        return "Enter a movie title between 1 and 200 characters.";
    if (width < 64 || width > 8192 || height < 64 || height > 8192)
        return "Output dimensions must be between 64 and 8192 pixels in this prototype.";
    if (!std::isfinite(fps) || fps < 1 || fps > 120)
        return "Frame rate must be between 1 and 120 FPS.";
    if (shots.isEmpty() || shots.size() > maxProjectShots)
        return "A movie must contain between 1 and 10,000 shots.";
    if (symbols.size() > maxProjectSymbols) return "A project supports up to 128 symbols.";
    QSet<QUuid> ids;
    QHash<QString, const SymbolDefinition *> definitions;
    for (const auto &symbol : symbols) {
        if (QUuid(symbol.id).isNull() || ids.contains(QUuid(symbol.id)))
            return "Each symbol needs a unique, valid identifier.";
        ids.insert(QUuid(symbol.id));
        if (!validModelSheetName(symbol.name)) return "Symbol names must contain 1 to 100 characters on one line.";
        definitions.insert(symbol.id, &symbol);
        const auto &content = symbol.content;
        if (!content.references.isEmpty() || !content.guides.isEmpty() || !content.savedSelections.isEmpty()
            || !content.audioClips.isEmpty() || content.camera2D != Camera2D{}
            || !content.camera2DKeys.isEmpty() || !content.sceneCameraAnimations.isEmpty() || !content.timelineCues.isEmpty())
            return "Symbols support 2D artwork and animation; references, guides, saved selections, timeline cues, audio and cameras must remain in the shot.";
        for (const auto &layer : content.layers) if (layer.kind == LayerKind::Scene3D || layer.scene3d)
            return "3D reference scenes cannot be stored inside symbols.";
    }
    // Validate the complete library, including unused definitions. This prevents
    // a cycle from becoming active only when an instance is inserted later.
    QHash<QString, int> depths;
    QSet<QString> visiting;
    std::function<int(const QString &)> depth = [&](const QString &id) -> int {
        if (!definitions.contains(id) || visiting.contains(id)) return -1;
        if (depths.contains(id)) return depths.value(id);
        visiting.insert(id);
        int result = 1;
        for (const auto &layer : definitions.value(id)->content.layers) if (layer.symbol) {
            const int nested = depth(layer.symbol->symbolId);
            if (nested < 0 || nested >= maxSymbolNestingDepth) return -1;
            result = std::max(result, nested + 1);
        }
        visiting.remove(id);
        depths.insert(id, result);
        return result;
    };
    for (const auto &symbol : symbols) if (depth(symbol.id) < 0)
        return "Symbols must refer to existing definitions, cannot contain cycles, and support at most four nested levels.";
    int tileCount = 0;
    int maskTileCount = 0;
    int referenceCount = 0;
    int guideCount = 0;
    int cueCount = 0, symbolPoseCount = 0;
    int objectCount = 0;
    qint64 brushSamples = 0;
    qint64 skinBytes = 0;
    for (const auto *shot : projectArtworkShots(*this)) {
        if (QUuid(shot->id).isNull() || ids.contains(QUuid(shot->id)))
            return "Each shot needs a unique, valid identifier.";
        ids.insert(QUuid(shot->id));
        if (shot->number.trimmed().isEmpty() || shot->number.size() > 40 || shot->name.size() > 200)
            return "Shot number is required (up to 40 characters); shot names allow up to 200.";
        if (shot->scene.isEmpty() || shot->scene.size() > 100 || shot->scene != shot->scene.trimmed() ||
            std::any_of(shot->scene.cbegin(), shot->scene.cend(), [](QChar ch) { return ch.category() == QChar::Other_Control || ch == QChar::LineSeparator || ch == QChar::ParagraphSeparator; }))
            return "Scene names must contain 1 to 100 characters on one line.";
        if (shot->durationFrames < 1 || shot->durationFrames > 1000000)
            return "Shot duration must be between 1 and 1,000,000 frames.";
        const auto cueValidation = validateTimelineCues(*shot);
        if (!cueValidation.isEmpty()) return cueValidation;
        cueCount += shot->timelineCues.size();
        if (cueCount > maxProjectTimelineCues) return "A project supports up to 16384 timeline cues.";
        for (const auto &cue : shot->timelineCues) {
            if (ids.contains(QUuid(cue.id))) return "Timeline cue identifiers must be unique throughout the project.";
            ids.insert(QUuid(cue.id));
        }
        for (const auto &clip : shot->audioClips) {
            if (QUuid(clip.id).isNull() || ids.contains(QUuid(clip.id))) return "Invalid or duplicate audio clip identifier.";
            ids.insert(QUuid(clip.id));
        }
        if (shot->layers.isEmpty() || shot->layers.size() > (shot->animationTracks.isEmpty() ? maxDrawingLayers : maxAnimationLayers))
            return shot->animationTracks.isEmpty()
                ? "A static shot must contain between 1 and 64 layers and groups."
                : "An animated shot must contain between 1 and 4096 layers and groups.";
        QHash<QString, int> layerIndices;
        QHash<QString, bool> adjustmentClippingBase;
        int rasterCount = 0;
        int sceneCount = 0;
        for (const auto &layer : shot->layers) {
            if (layer.clipped && adjustmentClippingBase.value(layer.parentId))
                return "Adjustment layers cannot be clipping bases. Choose an artwork layer as the base.";
            if (!layer.clipped) adjustmentClippingBase.insert(layer.parentId, layer.adjustment);
            if (QUuid(layer.id).isNull() || ids.contains(QUuid(layer.id))) return "Invalid or duplicate layer identifier.";
            ids.insert(QUuid(layer.id));
            if (layer.name.trimmed().isEmpty() || layer.name.size() > 100 || !std::isfinite(layer.opacity)
                || layer.opacity < 0 || layer.opacity > 1 || layerBlendModeKey(layer.blendMode).isEmpty()
                || (layer.rowColor.isValid() && layer.rowColor.alpha() != 255)) return "Invalid layer properties.";
            if (layerKindKey(layer.kind).isEmpty() || layer.objects.size() > 1024
                || ((layer.group || layer.kind == LayerKind::Raster || layer.kind == LayerKind::Scene3D || layer.kind == LayerKind::Symbol) && !layer.objects.isEmpty())
                || (layer.group && layer.kind != LayerKind::Raster)
                || (layer.kind != LayerKind::Raster && layer.alphaLocked)) return "Invalid editable layer properties.";
            if ((layer.kind == LayerKind::Scene3D) != layer.scene3d.has_value())
                return "3D scene data does not match its layer type.";
            if ((layer.kind == LayerKind::Symbol) != layer.symbol.has_value())
                return "Symbol instance data does not match its layer type.";
            if (layer.symbol) {
                const auto &instance = *layer.symbol;
                const auto definition = definitions.value(instance.symbolId, nullptr);
                if (!definition) return "A symbol instance refers to a missing definition.";
                if (!layer.tiles.isEmpty() || layer.group || layer.adjustment || layer.alphaLocked || layer.scene3d)
                    return "Symbol instances cannot contain independent artwork, groups or adjustments.";
                if (symbolPlaybackKey(instance.playback).isEmpty() || instance.startFrame < 0 || instance.startFrame > 1000000
                    || instance.durationFrames < -1 || instance.durationFrames > 1000000
                    || instance.playbackOffset < -1000000 || instance.playbackOffset > 1000000
                    || instance.firstFrame < 0 || instance.firstFrame >= definition->content.durationFrames
                    || !std::isfinite(instance.speed) || instance.speed < .01 || instance.speed > 100)
                    return "Invalid symbol playback. Choose a frame inside the symbol and a speed between 0.01 and 100.";
                const auto poseError = validateSymbolPoses(instance,definition->content.durationFrames);
                if (!poseError.isEmpty()) return poseError;
                symbolPoseCount += instance.poseKeys.size();
                if (symbolPoseCount > maxProjectSymbolPoseKeys) return "A project supports up to 65536 Symbol pose keys.";
            }
            const auto effectError = validateLayerEffects(layer.effects);
            if (!effectError.isEmpty()) return effectError;
            if (layer.mask) {
                const auto maskError = validateLayerMask(*layer.mask, m_maskResourcesLoaded);
                if (!maskError.isEmpty()) return maskError;
                maskTileCount += layer.mask->tiles.size();
                if (maskTileCount > maxProjectMaskTiles) return "Project exceeds the 1024-mask-tile / 64 MiB decoded-memory limit.";
            }
            if (layer.adjustment && (layer.kind != LayerKind::Raster || layer.group || layer.alphaLocked || layer.clipped || !layer.tiles.isEmpty()
                || !layer.objects.isEmpty() || layer.scene3d || layer.effects.blurEnabled
                || layer.effects.shadowEnabled || layer.effects.glowEnabled))
                return "Adjustment layers support color corrections only and cannot contain artwork.";
            if (layer.kind == LayerKind::Scene3D && (layer.effects != LayerEffects{} || layer.mask || layer.adjustment))
                return "3D reference layers cannot contain effects, masks or adjustments.";
            if (layer.scene3d) {
                if (++sceneCount > 1) return "A shot supports one shared 3D reference scene.";
                if (!layer.tiles.isEmpty() || layer.clipped) return "3D reference layers cannot contain painted tiles or clipping masks.";
                const auto sceneError = validateScene3D(*layer.scene3d);
                if (!sceneError.isEmpty()) return sceneError;
                for (const auto &object : layer.scene3d->objects) if (object.skin) {
                    skinBytes += skin3DByteSize(*object.skin);
                    if (skinBytes > maxProjectSkin3DBytes) return "Project skin weights exceed the 48 MiB decoded limit.";
                }
            }
            for (const auto &object : layer.objects) {
                if (++objectCount > 4096) return "A project supports up to 4096 editable objects.";
                if ((layer.kind == LayerKind::Vector && object.kind != ObjectKind::Path)
                    || (layer.kind == LayerKind::Text && object.kind != ObjectKind::Text)) return "Object type does not match its layer.";
                const auto validation = validateEditableObject(object);
                if (!validation.isEmpty()) return validation;
                if (object.brushStroke) {
                    brushSamples += object.brushStroke->samples.size();
                    if (brushSamples > maxProjectVectorBrushSamples) return "Project exceeds the 262144 vector brush sample limit.";
                }
                if (ids.contains(QUuid(object.id))) return "Invalid or duplicate object identifier.";
                ids.insert(QUuid(object.id));
            }
            layerIndices.insert(layer.id, layerIndices.size());
            if (layer.group) {
                if (!layer.tiles.isEmpty() || layer.clipped || layer.alphaLocked)
                    return "Groups cannot contain artwork tiles, clipping flags or alpha locks.";
            } else if (!layer.adjustment) ++rasterCount;
            for (auto tile = layer.tiles.cbegin(); tile != layer.tiles.cend(); ++tile) {
                if (++tileCount > maxProjectTiles) return "Project exceeds the 1024-tile memory limit.";
                if (tile->size() != QSize(tileSize, tileSize) || tile->format() != QImage::Format_ARGB32_Premultiplied
                    || qAbs(tile.key().first) > 128 || qAbs(tile.key().second) > 128)
                    return "Invalid raster tile.";
            }
        }
        if (rasterCount == 0 && shot->animationTracks.isEmpty()) return "A static shot must contain at least one artwork layer.";
        for (const auto &layer : shot->layers) {
            QSet<QString> ancestors{layer.id};
            QString parentId = layer.parentId;
            int depth = layer.group ? 1 : 0;
            bool annotation = layer.annotation;
            while (!parentId.isEmpty()) {
                if (ancestors.contains(parentId)) return "Layer groups cannot contain cycles.";
                const auto parent = layerIndices.constFind(parentId);
                if (parent == layerIndices.cend()) return "A layer's parent group must exist in the same shot.";
                const auto &group = shot->layers[*parent];
                if (!group.group) return "A layer's parent must be a group.";
                annotation = annotation || group.annotation;
                if (++depth > 8) return "Layer groups support at most eight nested levels.";
                ancestors.insert(parentId);
                parentId = group.parentId;
            }
            if (annotation && (layer.effects != LayerEffects{} || layer.mask || layer.adjustment))
                return "Notes layers and their descendants cannot contain effects, masks or adjustments.";
        }
        const auto propertyValidation = validatePropertyAnimation(*shot);
        if (!propertyValidation.isEmpty()) return propertyValidation;
        const auto cameraValidation = validateCameraAnimation(*shot);
        if (!cameraValidation.isEmpty()) return cameraValidation;
        const auto animationValidation = validateAnimation(*shot);
        if (!animationValidation.isEmpty()) return animationValidation;
        for (const auto &track : shot->animationTracks) {
            QStringList animationIds{track.id};
            for (const auto &drawing : track.drawings) animationIds.append(drawing.id);
            for (const auto &exposure : track.exposures) animationIds.append(exposure.id);
            for (const auto &id : animationIds) {
                if (ids.contains(QUuid(id))) return "Animation identifiers must be unique throughout the project.";
                ids.insert(QUuid(id));
            }
        }
        for (const auto &reference : shot->references) {
            if (++referenceCount > maxProjectReferences)
                return "A project supports up to 16 reference images.";
            if (QUuid(reference.id).isNull() || ids.contains(QUuid(reference.id)))
                return "Invalid or duplicate reference identifier.";
            ids.insert(QUuid(reference.id));
            if (reference.name.trimmed().isEmpty() || reference.name.size() > 100)
                return "Reference names must contain between 1 and 100 characters.";
        }
        const auto guideValidation = validateGuides(shot->guides, shot->guideSettings);
        if (!guideValidation.isEmpty()) return guideValidation;
        for (const auto &guide : shot->guides) {
            if (++guideCount > maxProjectDrawingGuides)
                return "A project supports up to 256 drawing guides.";
            if (ids.contains(QUuid(guide.id))) return "Invalid or duplicate drawing guide identifier.";
            ids.insert(QUuid(guide.id));
        }
        if (shot->savedSelections.size() > 64) return "A shot supports up to 64 saved selections.";
        QSet<QString> selectionNames;
        for (const auto &selection : shot->savedSelections) {
            if (QUuid(selection.id).isNull() || ids.contains(QUuid(selection.id))) return "Invalid or duplicate saved selection identifier.";
            ids.insert(QUuid(selection.id));
            if (selection.name.trimmed().isEmpty() || selection.name.size() > 100 || selectionNames.contains(selection.name)) return "Saved selection names must be unique and contain 1 to 100 characters.";
            selectionNames.insert(selection.name);
            if (selection.path.isEmpty() || selection.path.elementCount() > 262144 || !std::isfinite(selection.featherRadius)
                || selection.featherRadius < 0 || selection.featherRadius > 100) return "Invalid saved selection.";
            for (int i = 0; i < selection.path.elementCount(); ++i) {
                const auto point = selection.path.elementAt(i);
                if (!std::isfinite(point.x) || !std::isfinite(point.y) || qAbs(point.x) > 32768 || qAbs(point.y) > 32768) return "Saved selection exceeds the supported artwork range.";
            }
        }
        const auto &s = shot->sheet;
        for (int margin : {s.left, s.right, s.top, s.bottom, s.header, s.overscan})
            if (margin < 0 || margin > 4096)
                return "Margins, header and drawing extension must be between 0 and 4096 pixels.";
        const qint64 sheetWidth = width + 2LL * s.overscan + s.left + s.right;
        const qint64 sheetHeight = height + 2LL * s.overscan + s.top + s.bottom + (s.showHeader ? qMax(s.header, 600) : 0);
        if (sheetWidth * sheetHeight > 64000000)
            return "The production sheet exceeds the prototype's 64 megapixel limit. Reduce its dimensions or margins.";
    }
    if (characters.size() > maxProjectCharacters) return "A project supports up to 128 characters.";
    int sheetCount = 0, colorCount = 0, colorSlots = 0;
    QSet<QString> characterNames;
    for (const auto &character : characters) {
        if (QUuid(character.id).isNull() || ids.contains(QUuid(character.id)))
            return "Invalid or duplicate character identifier.";
        ids.insert(QUuid(character.id));
        if (!validModelSheetName(character.name)) return "Character names must contain 1 to 100 characters on one line.";
        if (characterNames.contains(character.name.toCaseFolded())) return "Each character needs a unique name.";
        characterNames.insert(character.name.toCaseFolded());
        for (const auto &sheet : character.sheets) {
            if (++sheetCount > maxProjectCharacterSheets) return "A project supports up to 256 character model sheets.";
            if (QUuid(sheet.id).isNull() || ids.contains(QUuid(sheet.id))) return "Invalid or duplicate character model sheet identifier.";
            ids.insert(QUuid(sheet.id));
            if (!validModelSheetName(sheet.name)) return "Character model sheet names must contain 1 to 100 characters on one line.";
            if (modelSheetCategoryKey(sheet.category).isEmpty()) return "Invalid character model sheet category.";
        }
        if (character.colors.size() > maxCharacterColors) return "A character supports up to 256 color swatches.";
        QSet<QString> colorIds;
        for (const auto &swatch : character.colors) {
            if (++colorCount > maxProjectCharacterColors) return "A project supports up to 8192 character color swatches.";
            if (QUuid(swatch.id).isNull() || ids.contains(QUuid(swatch.id))) return "Invalid or duplicate character color identifier.";
            ids.insert(QUuid(swatch.id));
            colorIds.insert(swatch.id);
            if (!validModelSheetName(swatch.name)) return "Character color names must contain 1 to 100 characters on one line.";
            if (characterColorCategoryKey(swatch.category).isEmpty()) return "Invalid character color category.";
            if (!validCharacterColor(swatch.color)) return "Character colors must contain valid RGBA values.";
        }
        colorSlots += character.colors.size();
        if (character.colorVariants.size() > maxCharacterColorVariants) return "A character supports up to 16 lighting variants in addition to Day.";
        QSet<QString> variantNames{QStringLiteral("day")};
        for (const auto &variant : character.colorVariants) {
            if (QUuid(variant.id).isNull() || ids.contains(QUuid(variant.id))) return "Invalid or duplicate character color variant identifier.";
            ids.insert(QUuid(variant.id));
            if (!validModelSheetName(variant.name)) return "Color variant names must contain 1 to 100 characters on one line.";
            if (variantNames.contains(variant.name.toCaseFolded())) return "Each color variant needs a unique name; Day is reserved for the base palette.";
            variantNames.insert(variant.name.toCaseFolded());
            if (variant.colors.size() != character.colors.size()) return "Each color variant must contain every shared character color exactly once.";
            for (auto color = variant.colors.cbegin(); color != variant.colors.cend(); ++color)
                if (!colorIds.contains(color.key()) || !validCharacterColor(color.value())) return "Color variants need matching character color identifiers and valid RGBA values.";
            colorSlots += variant.colors.size();
        }
        if (colorSlots > maxProjectCharacterColorSlots) return "Character palettes exceed the 65536 project color-slot limit.";
    }
    for (const auto &pin : drawingAids.lightTable.pins)
        if (ids.contains(QUuid(pin.id))) return "Light-table pin identifiers must be distinct from project artwork and library identifiers.";
    const auto aidError = validateDrawingAids(drawingAids, false);
    if (!aidError.isEmpty()) return aidError;
    return validateProjectAudio(*this, false);
}

QString Project::validate() const {
    for (const auto *shot : projectArtworkShots(*this)) for (const auto &layer : shot->layers) for (const auto &object : layer.objects)
        if (object.brushStroke && !validateBrushTipImages(object.brushStroke->settings))
            return "A vector brush tip set is missing its image atlas.";
    if(!m_brushResourcesLoaded) return "Vector brush image resources have not been loaded from the project container.";
    if(!m_sceneResourcesLoaded) return "3D model resources have not been loaded from the project container.";
    if(!m_audioResourcesLoaded) return "Audio resources have not been loaded from the project container.";
    if(!m_maskResourcesLoaded) return "Layer mask resources have not been loaded from the project container.";
    const auto structure = validateStructure();
    if (!structure.isEmpty()) return structure;
    const auto audioError = validateProjectAudio(*this);
    if (!audioError.isEmpty()) return audioError;
    const auto models = modelResources(*this);
    for (const auto *shot : projectArtworkShots(*this)) for (const auto &layer : shot->layers) if (layer.scene3d)
        for (const auto &object : layer.scene3d->objects) if (object.skin) {
            const auto validation = validateSkin3D(*object.skin, object.rig, layer.scene3d->models.value(object.modelId).get());
            if (!validation.isEmpty()) return validation;
        }
    if (models.size() > maxProjectModelResources) return "Project has too many 3D model resources.";
    qint64 decodedModelBytes = 0;
    QSet<qint64> textureKeys;
    for (const auto &model : models) {
        const auto modelError = validateModel3D(*model);
        if (!modelError.isEmpty()) return modelError;
        for (const auto &mesh : model->meshes) {
            decodedModelBytes += mesh.positions.size() * qint64(sizeof(QVector3D))
                + mesh.normals.size() * qint64(sizeof(QVector3D))
                + mesh.texCoords.size() * qint64(sizeof(QVector2D))
                + mesh.indices.size() * qint64(sizeof(quint32));
            if (!mesh.baseColorTexture.isNull() && !textureKeys.contains(mesh.baseColorTexture.cacheKey())) {
                textureKeys.insert(mesh.baseColorTexture.cacheKey());
                decodedModelBytes += mesh.baseColorTexture.sizeInBytes();
            }
            if (decodedModelBytes > maxProjectModelDecodedBytes) return "Project 3D models exceed the 256 MB decoded-memory limit.";
        }
    }
    const auto resources = brushResources(*this);
    if(resources.size()>maxProjectVectorBrushResources) return "Project has too many vector brush resources.";
    qint64 brushPixels=0;
    for(const auto &image:resources) {
        brushPixels+=qint64(image.width())*image.height();
        if(brushPixels>maxProjectVectorBrushResourcePixels) return "Vector brush resources exceed the 16 megapixel limit.";
    }
    qint64 referencePixels = 0;
    for (const auto *shot : projectArtworkShots(*this)) for (const auto &reference : shot->references) {
        const auto &image = reference.image;
        if (image.isNull() || image.format() != QImage::Format_ARGB32_Premultiplied
            || image.width() > maxReferenceDimension || image.height() > maxReferenceDimension)
            return "Reference images must contain valid pixels and measure at most 4096 pixels per side.";
        referencePixels += qint64(image.width()) * image.height();
        if (referencePixels > maxProjectReferencePixels)
            return "Project reference images exceed the 16 megapixel limit.";
    }
    qint64 modelSheetPixels = 0;
    for (const auto &character : characters) for (const auto &sheet : character.sheets) {
        const auto &image = sheet.image;
        if (image.isNull() || image.format() != QImage::Format_ARGB32_Premultiplied
            || image.width() > maxCharacterSheetDimension || image.height() > maxCharacterSheetDimension)
            return "Character model sheets must contain valid pixels and measure at most 4096 pixels per side.";
        modelSheetPixels += qint64(image.width()) * image.height();
        if (modelSheetPixels > maxProjectCharacterSheetPixels)
            return "Character model sheets exceed the 64 megapixel project limit.";
    }
    return validateDrawingAids(drawingAids);
}

QJsonObject Project::toJson() const {
    bool sheetPresentation = false;
    for (const auto *shot : projectArtworkShots(*this))
        if (!shot->sheet.showHeader || !shot->sheet.showGuides) sheetPresentation = true;
    const bool persistentAids = hasDrawingAids(drawingAids);
    const bool cueMetadata = std::any_of(shots.cbegin(), shots.cend(), [](const Shot &shot) { return !shot.timelineCues.isEmpty(); });
    bool extendedCompositing = false;
    for (const auto *shot : projectArtworkShots(*this)) {
        for (const auto &layer : shot->layers) extendedCompositing |= layer.mask && layerMaskHasProperties(*layer.mask);
        for (const auto &animation : shot->layerAnimations) for (const auto &channel : animation.channels)
            extendedCompositing |= keyframePropertyIsEffect(channel.property) || keyframePropertyIsMask(channel.property);
    }
    bool nestedTracks = false;
    for (const auto *shot : projectArtworkShots(*this)) for (const auto &track : shot->animationTracks)
        for (const auto &layer : shot->layers) if (layer.id==track.rootGroupLayerId && !layer.parentId.isEmpty()) nestedTracks=true;
    bool symbolClips = false, symbolPoses = false;
    for (const auto *shot : projectArtworkShots(*this)) for (const auto &layer : shot->layers)
        if (layer.symbol) {
            symbolClips |= layer.symbol->durationFrames != -1 || layer.symbol->playbackOffset != 0;
            symbolPoses |= layer.symbol->playback == SymbolPlayback::Poses || !layer.symbol->poseKeys.isEmpty();
        }
    bool audioFinishing = false, advancedBrushTips = false, imageColorDynamics = false, modernBrushes = false;
    for (const auto *shot : projectArtworkShots(*this)) for (const auto &layer : shot->layers) for (const auto &object : layer.objects)
        if (object.brushStroke) {
            modernBrushes |= usesModernVectorBrushCapture(*object.brushStroke);
            advancedBrushTips |= usesAdvancedBrushTips(object.brushStroke->settings);
            imageColorDynamics |= object.brushStroke->settings.colorDynamics.applyToColorTip;
        }
    for (const auto *shot : projectArtworkShots(*this)) for (const auto &clip : shot->audioClips)
        if (audioClipHasFinishing(clip)) audioFinishing = true;
    bool sceneMetadata = extendedCompositing || nestedTracks || cueMetadata || !symbols.isEmpty() || persistentAids || !characters.isEmpty() || shots.size() > 1 || audioFinishing || advancedBrushTips || imageColorDynamics || modernBrushes;
    for (const auto *shot : projectArtworkShots(*this)) sceneMetadata |= shot->scene != "Scene 1";
    QJsonArray list;
    for (const auto *shot : projectArtworkShots(*this)) {
        const auto &s = shot->sheet;
        QJsonArray layers;
        for (const auto &layer : shot->layers) {
            QJsonArray tiles;
            if (layer.kind == LayerKind::Raster) for (auto tile = layer.tiles.cbegin(); tile != layer.tiles.cend(); ++tile)
                tiles.append(QJsonArray{tile.key().first, tile.key().second});
            QJsonObject metadata{{"id", layer.id}, {"name", layer.name}, {"visible", layer.visible},
                {"kind", layerKindKey(layer.kind)}, {"objects", editableObjectsToJson(layer.objects)},
                {"locked", layer.locked}, {"annotation", layer.annotation}, {"opacity", layer.opacity}, {"tiles", tiles},
                {"parentId", layer.parentId}, {"group", layer.group}, {"clipped", layer.clipped},
                {"alphaLocked", layer.alphaLocked}, {"positionLocked", layer.positionLocked},
                {"blendMode", layerBlendModeKey(layer.blendMode)},
                {"rowColor", layer.rowColor.isValid() ? layer.rowColor.name(QColor::HexRgb) : QString()}};
            if (layer.scene3d) metadata.insert("scene3d", scene3DToJson(*layer.scene3d));
            if (layer.symbol) {
                QJsonObject instance{{"symbolId", layer.symbol->symbolId}, {"playback", symbolPlaybackKey(layer.symbol->playback)},
                    {"startFrame", layer.symbol->startFrame}, {"firstFrame", layer.symbol->firstFrame}, {"speed", layer.symbol->speed}};
                if (layer.symbol->playback == SymbolPlayback::Poses || !layer.symbol->poseKeys.isEmpty())
                    instance.insert("poseKeys",symbolPoseKeysToJson(layer.symbol->poseKeys));
                if (layer.symbol->durationFrames != -1 || layer.symbol->playbackOffset != 0) {
                    instance.insert("durationFrames", layer.symbol->durationFrames);
                    instance.insert("playbackOffset", layer.symbol->playbackOffset);
                }
                metadata.insert("symbol", instance);
            }
            if (layer.effects != LayerEffects{}) metadata.insert("effects", layerEffectsToJson(layer.effects));
            if (layer.mask) metadata.insert("mask", layerMaskToJson(*layer.mask));
            if (layer.adjustment) metadata.insert("adjustment", true);
            layers.append(metadata);
        }
        QJsonArray savedSelections;
        for (const auto &selection : shot->savedSelections) {
            QJsonArray path;
            for (int i = 0; i < selection.path.elementCount(); ++i) {
                const auto point = selection.path.elementAt(i);
                path.append(QJsonArray{int(point.type),point.x,point.y});
            }
            savedSelections.append(QJsonObject{{"id",selection.id},{"name",selection.name},{"featherRadius",selection.featherRadius},
                {"fillRule",int(selection.path.fillRule())},{"path",path}});
        }
        QJsonArray references;
        for (const auto &reference : shot->references)
            references.append(QJsonObject{{"id", reference.id}, {"name", reference.name},
                {"width", reference.image.width()}, {"height", reference.image.height()}});
        QJsonObject shotMetadata{{"id", shot->id}, {"number", shot->number}, {"name", shot->name},
            {"durationFrames", shot->durationFrames}, {"layers", layers}, {"references", references},
            {"drawingGuides", guidesToJson(shot->guides, shot->guideSettings)}, {"savedSelections", savedSelections},
            {"sheet", QJsonObject{{"left", s.left}, {"right", s.right}, {"top", s.top},
                {"bottom", s.bottom}, {"header", s.header}, {"overscan", s.overscan}, {"safeArea", s.safeArea}}}};
        if (!s.showHeader || !s.showGuides) {
            auto sheetMetadata = shotMetadata["sheet"].toObject();
            if (!s.showHeader) sheetMetadata.insert("showHeader", false);
            if (!s.showGuides) sheetMetadata.insert("showGuides", false);
            shotMetadata.insert("sheet", sheetMetadata);
        }
        if (sceneMetadata || sheetPresentation) shotMetadata.insert("scene", shot->scene);
        if (!shot->timelineCues.isEmpty()) shotMetadata.insert("timelineCues", timelineCuesToJson(shot->timelineCues));
        if (!shot->animationTracks.isEmpty()) shotMetadata.insert("animationTracks", animationTracksToJson(shot->animationTracks));
        if (!shot->layerAnimations.isEmpty()) shotMetadata.insert("layerAnimations", layerAnimationsToJson(shot->layerAnimations));
        if (!shot->audioClips.isEmpty()) shotMetadata.insert("audioClips", audioClipsToJson(shot->audioClips));
        if (shot->camera2D != Camera2D() || !shot->camera2DKeys.isEmpty()) shotMetadata.insert("camera2D", shotCameraToJson(*shot));
        if (!shot->sceneCameraAnimations.isEmpty()) shotMetadata.insert("sceneCameraAnimations", sceneCameraAnimationsToJson(shot->sceneCameraAnimations));
        list.append(shotMetadata);
    }
    QJsonArray symbolList;
    for (int i = 0; i < symbols.size(); ++i)
        symbolList.append(QJsonObject{{"id", symbols[i].id}, {"name", symbols[i].name}, {"content", list[shots.size() + i]}});
    while (list.size() > shots.size()) list.removeLast();
    QJsonArray resources;
    const auto images=brushResources(*this);
    bool colored = false;
    for(auto it=images.cbegin();it!=images.cend();++it) {
        QJsonObject descriptor{{"id",it.key()},{"width",it->width()},{"height",it->height()}};
        if (it->format() == QImage::Format_ARGB32_Premultiplied) { descriptor.insert("format","rgba8"); colored = true; }
        resources.append(descriptor);
    }
    bool extendedBrushDynamics = false;
    for (const auto *shot : projectArtworkShots(*this)) for (const auto &layer : shot->layers) for (const auto &object : layer.objects)
        if (object.brushStroke && usesExtendedBrushDynamics(object.brushStroke->settings)) extendedBrushDynamics = true;
    QJsonArray models;
    for (const auto &id : modelResources(*this).keys()) models.append(QJsonObject{{"id", id}});
    const bool characterColors = std::any_of(characters.cbegin(), characters.cend(), [](const CharacterModel &character) { return !character.colors.isEmpty(); });
    const bool characterVariants = std::any_of(characters.cbegin(), characters.cend(), [](const CharacterModel &character) { return !character.colorVariants.isEmpty(); });
    QJsonObject result{{"format", "toon-pro"}, {"version", sheetPresentation ? 37 : extendedCompositing ? 36 : nestedTracks ? 35 : symbolPoses ? 34 : cueMetadata ? 33 : symbolClips ? 32 : !symbols.isEmpty() ? 31 : characterVariants ? 30 : persistentAids ? 29 : characterColors ? 28 : !characters.isEmpty() ? 27 : modernBrushes ? 26 : imageColorDynamics ? 25 : advancedBrushTips ? 24 : audioFinishing ? 23 : sceneMetadata ? 22 : extendedBrushDynamics ? 21 : colored ? 20 : 19}, {"movie", movie}, {"brushResources",resources}, {"modelResources",models}, {"audioResources", audioResourcesToJson(*this)},
        {"output", QJsonObject{{"width", width}, {"height", height}, {"fps", fps},
            {"profile", "sRGB"}, {"bitDepth", 8}, {"transparent", transparent}}}, {"shots", list}};
    if (!symbols.isEmpty() || cueMetadata || nestedTracks || extendedCompositing || sheetPresentation) result.insert("symbols", symbolList);
    if (!characters.isEmpty() || persistentAids || !symbols.isEmpty() || cueMetadata || nestedTracks || extendedCompositing || sheetPresentation) {
        QJsonArray characterList;
        for (const auto &character : characters) {
            QJsonArray sheets;
            for (const auto &sheet : character.sheets)
                sheets.append(QJsonObject{{"id", sheet.id}, {"name", sheet.name}, {"category", modelSheetCategoryKey(sheet.category)},
                    {"width", sheet.image.width()}, {"height", sheet.image.height()}});
            QJsonObject entry{{"id", character.id}, {"name", character.name}, {"sheets", sheets}};
            if (characterColors || persistentAids || characterVariants || !symbols.isEmpty() || cueMetadata || nestedTracks || extendedCompositing || sheetPresentation) {
                QJsonArray colors;
                for (const auto &swatch : character.colors)
                    colors.append(QJsonObject{{"id", swatch.id}, {"name", swatch.name},
                        {"category", characterColorCategoryKey(swatch.category)},
                        {"color", QColor::fromRgba(swatch.color.rgba()).name(QColor::HexArgb)}});
                entry.insert("colors", colors);
            }
            if (characterVariants || !symbols.isEmpty() || cueMetadata || nestedTracks || extendedCompositing || sheetPresentation) {
                QJsonArray variants;
                for (const auto &variant : character.colorVariants) {
                    QJsonObject colors;
                    for (auto color = variant.colors.cbegin(); color != variant.colors.cend(); ++color)
                        colors.insert(color.key(), QColor::fromRgba(color.value().rgba()).name(QColor::HexArgb));
                    variants.append(QJsonObject{{"id", variant.id}, {"name", variant.name}, {"colors", colors}});
                }
                entry.insert("colorVariants", variants);
            }
            characterList.append(entry);
        }
        result.insert("characters", characterList);
    }
    if (persistentAids || characterVariants || !symbols.isEmpty() || cueMetadata || nestedTracks || extendedCompositing || sheetPresentation) result.insert("drawingAids", drawingAidsToJson(drawingAids));
    return result;
}

std::optional<Project> Project::fromJson(const QJsonObject &o, QString *error) {
    auto fail = [error](const QString &message) -> std::optional<Project> {
        if (error) *error = message;
        return std::nullopt;
    };
    if (o.value("format") != "toon-pro" || (o.value("version") != 1 && o.value("version") != 2
        && o.value("version") != 3 && o.value("version") != 4 && o.value("version") != 5 && o.value("version") != 6 && o.value("version") != 7 && o.value("version") != 8 && o.value("version") != 9 && o.value("version") != 10 && o.value("version") != 11 && o.value("version") != 12 && o.value("version") != 13 && o.value("version") != 14 && o.value("version") != 15 && o.value("version") != 16 && o.value("version") != 17 && o.value("version") != 18 && o.value("version") != 19 && o.value("version") != 20 && o.value("version") != 21 && o.value("version") != 22 && o.value("version") != 23 && o.value("version") != 24 && o.value("version") != 25 && o.value("version") != 26 && o.value("version") != 27 && o.value("version") != 28 && o.value("version") != 29 && o.value("version") != 30 && o.value("version") != 31 && o.value("version") != 32 && o.value("version") != 33 && o.value("version") != 34 && o.value("version") != 35 && o.value("version") != 36 && o.value("version") != 37))
        return fail("This file is not a supported Toon Pro project (version 1 through 37 required).");
    const int version = o.value("version").toInt();
    if (version < 33) for (const auto &shot : artworkShotMetadata(o))
        if (shot.toObject().contains("timelineCues")) return fail("Timeline cues require project format 33.");
    if (version < 31 && o.contains("symbols")) return fail("Symbols require project format 31.");
    if (version >= 31) {
        if (!o.value("symbols").isArray() || (version < 33 && o.value("symbols").toArray().isEmpty())
            || o.value("symbols").toArray().size() > maxProjectSymbols)
            return fail("Invalid symbol library for this project format.");
        for (const auto &value : o.value("symbols").toArray()) {
            const auto symbol = value.toObject();
            if (!value.isObject() || symbol.size() != 3 || !symbol.value("id").isString()
                || !symbol.value("name").isString() || !symbol.value("content").isObject())
                return fail("Invalid symbol definition metadata.");
        }
    }
    if (!o.value("shots").isArray() || o.value("shots").toArray().isEmpty()
        || o.value("shots").toArray().size() > maxProjectShots) return fail("A movie must contain between 1 and 10,000 shots.");
    if (version < 31) for (const auto &shot : artworkShotMetadata(o)) for (const auto &value : shot.toObject().value("layers").toArray())
        if (value.toObject().contains("symbol") || value.toObject().value("kind") == "symbol") return fail("Symbol instances require project format 31.");
    if (version < 30) for (const auto &character : o.value("characters").toArray())
        if (character.toObject().contains("colorVariants")) return fail("Character color variants require project format 30.");
    if (version < 29 && o.contains("drawingAids")) return fail("Saved drawing aids require project format 29.");
    if (version < 28) for (const auto &character : o.value("characters").toArray())
        if (character.toObject().contains("colors")) return fail("Character color models require project format 28.");
    if (version < 27 && o.contains("characters")) return fail("Character model sheets require project format 27.");
    if (version < 26) for (const auto &shot : artworkShotMetadata(o))
        for (const auto &layer : shot.toObject().value("layers").toArray())
            for (const auto &object : layer.toObject().value("objects").toArray())
                if (object.toObject().value("brushStroke").toObject().value("version") == 6)
                    return fail("Connected brushes, surfaces and advanced dynamics require project format 26.");
    if (version < 25) for (const auto &shot : artworkShotMetadata(o))
        for (const auto &layer : shot.toObject().value("layers").toArray())
            for (const auto &object : layer.toObject().value("objects").toArray())
                if (object.toObject().value("brushStroke").toObject().value("version") == 5)
                    return fail("Image color dynamics require project format 25.");
    if (version < 24) for (const auto &shot : artworkShotMetadata(o))
        for (const auto &layer : shot.toObject().value("layers").toArray())
            for (const auto &object : layer.toObject().value("objects").toArray())
                if (object.toObject().value("brushStroke").toObject().value("version") == 4)
                    return fail("Advanced brush tips require project format 24.");
    if (version < 23) for (const auto &shot : artworkShotMetadata(o))
        for (const auto &clip : shot.toObject().value("audioClips").toArray())
            if (clip.toObject().contains("finishing")) return fail("Audio finishing requires project format 23.");
    if (version < 22) for (const auto &shot : artworkShotMetadata(o))
        if (shot.toObject().contains("scene")) return fail("Scene labels require project format 22.");
    if (version < 21) for (const auto &shot : artworkShotMetadata(o))
        for (const auto &layer : shot.toObject().value("layers").toArray())
            for (const auto &object : layer.toObject().value("objects").toArray())
                if (object.toObject().value("brushStroke").toObject().value("version") == 3)
                    return fail("Extended brush dynamics require project format 21.");
    if (version < 20) for (const auto &shot : artworkShotMetadata(o))
        for (const auto &layer : shot.toObject().value("layers").toArray())
            for (const auto &object : layer.toObject().value("objects").toArray())
                if (object.toObject().value("brushStroke").toObject().contains("colorTipResource"))
                    return fail("Colored stamps require project format 20.");
    if (version < 19) for (const auto &shot : artworkShotMetadata(o))
        for (const auto &track : shot.toObject().value("animationTracks").toArray())
            for (const auto &slot : track.toObject().value("drawingTemplate").toArray())
                if (slot.toObject().contains("kind")) return fail("Typed drawing templates require project format 19.");
    if (version < 18) for (const auto &shot : artworkShotMetadata(o))
        for (const auto &entry : shot.toObject().value("layerAnimations").toArray()) {
            const auto animation = entry.toObject();
            if (animation.contains("orientAlongPath")) return fail("Advanced motion controls require project format 18.");
            for (const auto collection : {"channels", "paths"})
                for (const auto &channel : animation.value(QLatin1String(collection)).toArray())
                    for (const auto &entry : channel.toObject().value("keys").toArray()) {
                        const auto key = entry.toObject();
                        if (key.contains("easing") || key.contains("spatial"))
                            return fail("Advanced motion controls require project format 18.");
                    }
        }
    if (version < 17) for (const auto &shot : artworkShotMetadata(o))
        for (const auto &value : shot.toObject().value("layers").toArray()) {
            const auto layer = value.toObject();
            if (layer.contains("effects") || layer.contains("mask") || layer.contains("adjustment"))
                return fail("Layer effects, masks and adjustments require project format 17.");
        }
    if (version < 16) for (const auto &value : artworkShotMetadata(o)) {
        const auto shot = value.toObject();
        if (shot.contains("camera2D") || shot.contains("sceneCameraAnimations")) return fail("Camera metadata requires project format 16.");
    }
    if (version < 15) {
        if (o.contains("audioResources") && (!o.value("audioResources").isArray() || !o.value("audioResources").toArray().isEmpty()))
            return fail("Audio resources require project format 15.");
        for (const auto &shot : artworkShotMetadata(o))
            if (shot.toObject().contains("audioClips")) return fail("Audio clips require project format 15.");
    }
    if (version < 14) for (const auto &shot : artworkShotMetadata(o))
        if (shot.toObject().contains("layerAnimations")) return fail("Property animation requires project format 14.");
    if (version < 13) for (const auto &shot : artworkShotMetadata(o))
        if (shot.toObject().contains("animationTracks")) return fail("Animation metadata requires project format 13.");
    qint64 packedSkinBytes = 0;
    for (const auto &shot : artworkShotMetadata(o)) for (const auto &layer : shot.toObject().value("layers").toArray()) {
        const auto scene = layer.toObject().value("scene3d").toObject();
        if (version < 12 && scene.value("version") == 4) return fail("Pose metadata requires project format 12.");
        if (version < 12) for (const auto &object : scene.value("objects").toArray())
            if (object.toObject().contains("pose")) return fail("Pose metadata requires project format 12.");
        if (version < 11 && scene.value("version") == 3) return fail("Skin metadata requires project format 11.");
        for (const auto &object : scene.value("objects").toArray()) if (object.toObject().contains("skin")) {
            if (version < 11) return fail("Skin metadata requires project format 11.");
            const auto skin = object.toObject().value("skin").toObject();
            for (const auto &count : skin.value("meshVertexCounts").toArray()) {
                const double n = count.toDouble(-1);
                if (!count.isDouble() || !std::isfinite(n) || n < 1 || n > maxModel3DVertices || std::floor(n) != n)
                    return fail("Invalid skin vertex count.");
                packedSkinBytes += qint64(n) * 24;
                if (packedSkinBytes > maxProjectSkin3DBytes) return fail("Project skin weights exceed the 48 MiB decoded limit.");
            }
        }
    }
    if (version < 10) for (const auto &shot : artworkShotMetadata(o)) for (const auto &layer : shot.toObject().value("layers").toArray()) {
        const auto scene = layer.toObject().value("scene3d").toObject();
        if (scene.value("version") == 2) return fail("Rig metadata requires project format 10.");
        for (const auto &object : scene.value("objects").toArray()) if (object.toObject().contains("rig"))
            return fail("Rig metadata requires project format 10.");
    }
    if(version>=8 && !validateResourceManifest(o,error)) return {};
    if(version>=9 && !validateModelManifest(o,error)) return {};
    Project p;
    p.m_brushResourcesLoaded=version<8 || o.value("brushResources").toArray().isEmpty();
    p.m_sceneResourcesLoaded=version<9 || o.value("modelResources").toArray().isEmpty();
    QMap<QString, AudioAssetPtr> pendingAudio;
    if (version >= 15) {
        if (!o.value("audioResources").isArray()) return fail("Missing audio resource list.");
        if (!audioResourcesFromJson(o.value("audioResources").toArray(), &pendingAudio, error)) return {};
    }
    p.m_audioResourcesLoaded = pendingAudio.isEmpty();
    // Metadata-only placeholders resolve scene references; loadProject replaces
    // them with verified immutable geometry before the project can be used.
    QHash<QString, Model3DPtr> pendingModels;
    if (version >= 9) for (const auto &value : o.value("modelResources").toArray()) {
        auto model = std::make_shared<Model3D>();
        model->id = value.toObject().value("id").toString();
        pendingModels.insert(model->id, model);
    }
    p.movie = o.value("movie").toString();
    const auto output = o.value("output").toObject();
    if (!integer(output, "width", p.width) || !integer(output, "height", p.height)
        || !output.value("fps").isDouble() || !output.value("transparent").isBool()
        || output.value("profile") != "sRGB" || output.value("bitDepth") != 8)
        return fail("Invalid output settings. This build supports sRGB, 8-bit projects.");
    p.fps = output.value("fps").toDouble();
    p.transparent = output.value("transparent").toBool();
    if (!o.value("shots").isArray()) return fail("The project has no shot list.");
    qint64 referencePixels = 0;
    int referenceCount = 0;
    int guideCount = 0;
    int maskTileCount = 0;
    int parsedCueCount = 0;
    qint64 brushSamples = 0;
    for (const auto &value : artworkShotMetadata(o)) {
        const auto entry = value.toObject();
        Shot shot;
        if (entry.contains("camera2D")) {
            if (!entry["camera2D"].isObject()) return fail("Invalid shot camera metadata.");
            if (!shotCameraFromJson(entry["camera2D"].toObject(), &shot, error)) return {};
        }
        if (entry.contains("sceneCameraAnimations")) {
            if (!entry["sceneCameraAnimations"].isArray()) return fail("Invalid 3D camera animation metadata.");
            if (!sceneCameraAnimationsFromJson(entry["sceneCameraAnimations"].toArray(), &shot.sceneCameraAnimations, error)) return {};
        }
        if (entry.contains("timelineCues")) {
            if (!entry.value("timelineCues").isArray()) return fail("Invalid timeline cue list.");
            parsedCueCount += entry.value("timelineCues").toArray().size();
            if (parsedCueCount > maxProjectTimelineCues) return fail("A project supports up to 16384 timeline cues.");
            if (!timelineCuesFromJson(entry.value("timelineCues").toArray(), &shot.timelineCues, error)) return {};
        }
        if (entry.contains("animationTracks")) {
            if (!entry.value("animationTracks").isArray()) return fail("Invalid animation track list.");
            if (!animationTracksFromJson(entry.value("animationTracks").toArray(), &shot.animationTracks, error)) return {};
        }
        if (entry.contains("layerAnimations")) {
            if (!entry.value("layerAnimations").isArray()) return fail("Invalid property animation list.");
            if (!layerAnimationsFromJson(entry.value("layerAnimations").toArray(), &shot.layerAnimations, error)) return {};
        }
        if (entry.contains("audioClips")) {
            if (!entry.value("audioClips").isArray()) return fail("Invalid audio clip list.");
            if (!audioClipsFromJson(entry.value("audioClips").toArray(), pendingAudio, &shot.audioClips, error)) return {};
        }
        shot.id = entry.value("id").toString();
        shot.number = entry.value("number").toString();
        if (!entry.value("name").isString()) return fail("Invalid shot name.");
        shot.name = entry.value("name").toString();
        if (version >= 22) {
            if (!entry.value("scene").isString()) return fail("A shot needs a valid scene name.");
            shot.scene = entry.value("scene").toString();
        }
        auto &s = shot.sheet;
        const auto sheet = entry.value("sheet").toObject();
        if (!integer(entry, "durationFrames", shot.durationFrames)
            || !integer(sheet, "left", s.left) || !integer(sheet, "right", s.right)
            || !integer(sheet, "top", s.top) || !integer(sheet, "bottom", s.bottom)
            || !integer(sheet, "header", s.header) || !integer(sheet, "overscan", s.overscan)
            || !sheet.value("safeArea").isBool()) return fail("Invalid shot timing or sheet settings.");
        s.safeArea = sheet.value("safeArea").toBool();
        for (const auto &key : {"showHeader", "showGuides"}) if (sheet.contains(key)) {
            if (version < 37) return fail("Sheet presentation settings require project format 37.");
            if (!sheet.value(key).isBool()) return fail("Invalid sheet presentation setting.");
        }
        s.showHeader = sheet.value("showHeader").toBool(true);
        s.showGuides = sheet.value("showGuides").toBool(true);
        if (version == 1) {
            shot.layers.append(RasterLayer::create());
        } else {
            if (!entry.value("layers").isArray()) return fail("Missing raster layers.");
            if (entry.value("layers").toArray().size() > (shot.animationTracks.isEmpty() ? maxDrawingLayers : maxAnimationLayers))
                return fail(shot.animationTracks.isEmpty()
                    ? "A static shot supports up to 64 layers and groups."
                    : "An animated shot supports up to 4096 layers and groups.");
            for (const auto &value : entry.value("layers").toArray()) {
                const auto l = value.toObject();
                RasterLayer layer;
                if (!l.value("name").isString() || !l.value("visible").isBool() || !l.value("locked").isBool()
                    || !l.value("annotation").isBool() || !l.value("opacity").isDouble() || !l.value("tiles").isArray())
                    return fail("Invalid raster layer settings.");
                layer.id = l.value("id").toString(); layer.name = l.value("name").toString();
                layer.visible = l.value("visible").toBool(); layer.locked = l.value("locked").toBool();
                layer.annotation = l.value("annotation").toBool(); layer.opacity = l.value("opacity").toDouble();
                if (version >= 4) {
                    if (!l.value("parentId").isString() || !l.value("group").isBool()
                        || !l.value("clipped").isBool() || !l.value("alphaLocked").isBool())
                        return fail("Invalid layer group, clipping or alpha lock settings.");
                    layer.parentId = l.value("parentId").toString(); layer.group = l.value("group").toBool();
                    layer.clipped = l.value("clipped").toBool(); layer.alphaLocked = l.value("alphaLocked").toBool();
                    if (layer.group && !l.value("tiles").toArray().isEmpty())
                        return fail("Groups cannot contain artwork tiles.");
                }
                if (version >= 6) {
                    if (!l.value("positionLocked").isBool() || !l.value("blendMode").isString() || !l.value("rowColor").isString())
                        return fail("Invalid layer blend, position lock or row color settings.");
                    const auto blend = layerBlendModeFromKey(l.value("blendMode").toString());
                    if (!blend) return fail("Unsupported layer blend mode.");
                    layer.blendMode = *blend; layer.positionLocked = l.value("positionLocked").toBool();
                    const auto color = l.value("rowColor").toString();
                    if (!color.isEmpty()) {
                        layer.rowColor = QColor(color);
                        if (color.size() != 7 || !color.startsWith('#') || !layer.rowColor.isValid()
                            || layer.rowColor.name(QColor::HexRgb) != color.toLower())
                            return fail("Invalid layer row color; use an opaque #RRGGBB color or an empty value.");
                    }
                }
                if (version >= 7) {
                    const auto kind = layerKindFromKey(l.value("kind").toString());
                    if (!kind || !l.value("objects").isArray()) return fail("Invalid editable layer settings.");
                    layer.kind = *kind;
                    if (layer.kind == LayerKind::Symbol) {
                        if (version < 31 || !l.value("symbol").isObject()) return fail("Missing symbol instance metadata.");
                        const auto data = l.value("symbol").toObject();
                        SymbolInstance instance;
                        const auto playback = symbolPlaybackFromKey(data.value("playback").toString());
                        const bool clipFields = data.contains("durationFrames") || data.contains("playbackOffset");
                        const bool poseFields = data.contains("poseKeys");
                        if ((poseFields || playback == SymbolPlayback::Poses) && version < 34)
                            return fail("Timed Symbol poses require project format 34.");
                        if (playback == SymbolPlayback::Poses && !poseFields) return fail("Missing Symbol pose-key data.");
                        if (data.size() != (clipFields ? 7 : 5) + (poseFields ? 1 : 0) || (clipFields && version < 32)
                            || !data.value("symbolId").isString() || !playback
                            || !integer(data, "startFrame", instance.startFrame) || !integer(data, "firstFrame", instance.firstFrame)
                            || !data.value("speed").isDouble()) return fail("Invalid symbol instance metadata.");
                        if (clipFields && (!boundedInteger(data, "durationFrames", -1, 1000000, instance.durationFrames)
                            || !boundedInteger(data, "playbackOffset", -1000000, 1000000, instance.playbackOffset)))
                            return fail("Invalid Symbol clip duration or playback offset.");
                        if (poseFields && (!data.value("poseKeys").isArray()
                            || !symbolPoseKeysFromJson(data.value("poseKeys").toArray(),&instance.poseKeys,error)))
                            return fail("Invalid Symbol pose-key data.");
                        instance.symbolId = data.value("symbolId").toString();
                        instance.playback = *playback;
                        instance.speed = data.value("speed").toDouble();
                        layer.symbol = instance;
                    } else if (l.contains("symbol")) return fail("Symbol instance data does not match its layer type.");
                    if (layer.kind == LayerKind::Scene3D) {
                        if (version < 9) return fail("3D scene layers require project format 9.");
                        QString sceneError;
                        layer.scene3d = scene3DFromJson(l.value("scene3d").toObject(), pendingModels, &sceneError);
                        if (!layer.scene3d) return fail(sceneError);
                    } else if (l.contains("scene3d")) return fail("3D scene data does not match its layer type.");
                    for(const auto &object:l.value("objects").toArray()) {
                        const auto brush=object.toObject().value("brushStroke");
                        if(!brush.isUndefined()) {
                            if(version<8) return fail("Editable brush strokes require project format 8.");
                            brushSamples+=brush.toObject().value("samples").toArray().size();
                            if(brushSamples>maxProjectVectorBrushSamples) return fail("Project exceeds the 262144 vector brush sample limit.");
                        }
                    }
                    QString objectError;
                    if (!editableObjectsFromJson(l.value("objects").toArray(), &layer.objects, &objectError)) return fail(objectError);
                    if (layer.kind != LayerKind::Raster && !l.value("tiles").toArray().isEmpty())
                        return fail("Editable layer caches must not be embedded in the project.");
                }
                if (l.contains("effects")) {
                    if (!l["effects"].isObject()) return fail("Invalid layer effect metadata.");
                    if (!layerEffectsFromJson(l["effects"].toObject(), &layer.effects, error)) return {};
                }
                if (l.contains("mask")) {
                    if (!l["mask"].isObject()) return fail("Invalid layer mask metadata.");
                    LayerMask mask;
                    if (!layerMaskFromJson(l["mask"].toObject(), &mask, error)) return {};
                    maskTileCount += mask.tiles.size();
                    if (maskTileCount > maxProjectMaskTiles) return fail("Project exceeds the 1024-mask-tile / 64 MiB decoded-memory limit.");
                    if (!mask.tiles.isEmpty()) p.m_maskResourcesLoaded = false;
                    layer.mask = std::move(mask);
                }
                if (l.contains("adjustment")) {
                    if (!l["adjustment"].isBool()) return fail("Invalid adjustment layer flag.");
                    layer.adjustment = l["adjustment"].toBool();
                    if (layer.adjustment && !l["tiles"].toArray().isEmpty())
                        return fail("Adjustment layers cannot contain artwork tiles.");
                }
                // Binary tile payloads are decoded by loadProject, after manifest validation.
                shot.layers.append(layer);
            }
        }
        if (version >= 3) {
            if (!entry.value("references").isArray()) return fail("Missing reference image list.");
            for (const auto &value : entry.value("references").toArray()) {
                if (++referenceCount > maxProjectReferences) return fail("A project supports up to 16 reference images.");
                const auto r = value.toObject();
                int width = 0, height = 0;
                if (!integer(r, "width", width) || !integer(r, "height", height)
                    || width < 1 || width > maxReferenceDimension || height < 1 || height > maxReferenceDimension)
                    return fail("Invalid reference image dimensions.");
                referencePixels += qint64(width) * height;
                if (referencePixels > maxProjectReferencePixels)
                    return fail("Project reference images exceed the 16 megapixel limit.");
                shot.references.append({r.value("id").toString(), r.value("name").toString(), {}});
            }
        }
        if (version >= 5) {
            QString guideError;
            if (!entry.value("drawingGuides").isObject()) return fail("Missing drawing guide settings.");
            if (!guidesFromJson(entry.value("drawingGuides").toObject(), &shot.guides, &shot.guideSettings, &guideError))
                return fail(guideError);
            guideCount += shot.guides.size();
            if (guideCount > maxProjectDrawingGuides) return fail("A project supports up to 256 drawing guides.");
        }
        if (version >= 7) {
            if (!entry.value("savedSelections").isArray() || entry.value("savedSelections").toArray().size() > 64)
                return fail("Invalid saved selection list.");
            for (const auto &value : entry.value("savedSelections").toArray()) {
                if (!value.isObject()) return fail("Invalid saved selection.");
                const auto selection = value.toObject(); NamedSelection saved;
                if (!selection.value("id").isString() || !selection.value("name").isString()
                    || !selection.value("featherRadius").isDouble() || !selection.value("path").isArray()
                    || (selection.value("fillRule") != 0 && selection.value("fillRule") != 1)) return fail("Invalid saved selection settings.");
                saved.id = selection.value("id").toString(); saved.name = selection.value("name").toString();
                saved.featherRadius = selection.value("featherRadius").toDouble();
                saved.path.setFillRule(Qt::FillRule(selection.value("fillRule").toInt()));
                const auto path = selection.value("path").toArray();
                if (path.size() > 262144) return fail("Saved selection has too many path elements.");
                auto point = [&](int at, int type, QPointF *result) {
                    if (at >= path.size()) return false;
                    const auto p = path[at].toArray();
                    if (p.size() != 3 || p[0] != type || !p[1].isDouble() || !p[2].isDouble()) return false;
                    *result = QPointF(p[1].toDouble(),p[2].toDouble());
                    return std::isfinite(result->x()) && std::isfinite(result->y()) && qAbs(result->x()) <= 32768 && qAbs(result->y()) <= 32768;
                };
                for (int i = 0; i < path.size(); ++i) {
                    const auto element = path[i].toArray();
                    if (element.size() != 3) return fail("Invalid saved selection element.");
                    QPointF a,b,c;
                    const int type = element[0].toInt(-1);
                    if (i == 0 && type != QPainterPath::MoveToElement) return fail("Saved selection must begin with a move.");
                    if (type == QPainterPath::MoveToElement && point(i,type,&a)) saved.path.moveTo(a);
                    else if (type == QPainterPath::LineToElement && point(i,type,&a)) saved.path.lineTo(a);
                    else if (type == QPainterPath::CurveToElement && point(i,type,&a)
                        && point(i+1,QPainterPath::CurveToDataElement,&b) && point(i+2,QPainterPath::CurveToDataElement,&c)) {
                        saved.path.cubicTo(a,b,c); i += 2;
                    } else return fail("Invalid saved selection curve.");
                }
                shot.savedSelections.append(saved);
            }
        }
        if (version < 19 && !shot.animationTracks.isEmpty()) {
            const auto ownership = animationOwners(shot);
            for (const auto &layer : shot.layers)
                if (layer.kind == LayerKind::Vector && !ownership.value(layer.id).drawingId.isEmpty())
                    return fail("Vector animation drawings require project format 19.");
        }
        if (version<36) {
            for (const auto &animation:shot.layerAnimations) for (const auto &channel:animation.channels)
                if (keyframePropertyIsEffect(channel.property) || keyframePropertyIsMask(channel.property))
                    return fail("Animated effects and masks require project format 36.");
            for (const auto &layer:entry.value("layers").toArray())
                if (layer.toObject().value("mask").toObject().contains("properties"))
                    return fail("Mask transform and compositing settings require project format 36.");
        }
        if (version<35) for (const auto &track:shot.animationTracks) for (const auto &layer:shot.layers)
            if (layer.id==track.rootGroupLayerId && !layer.parentId.isEmpty())
                return fail("Nested animation tracks require project format 35.");
        p.shots.append(shot);
    }
    const int movieShotCount = o.value("shots").toArray().size();
    for (int i = 0; i < o.value("symbols").toArray().size(); ++i) {
        const auto symbol = o.value("symbols").toArray()[i].toObject();
        p.symbols.append({symbol.value("id").toString(), symbol.value("name").toString(), std::move(p.shots[movieShotCount + i])});
    }
    p.shots.resize(movieShotCount);
    if (version >= 27) {
        if (!o.value("characters").isArray()) return fail("Missing character model sheet library.");
        const auto characterList = o.value("characters").toArray();
        if (characterList.size() > maxProjectCharacters) return fail("A project supports up to 128 characters.");
        int sheetCount = 0, colorCount = 0, colorSlots = 0;
        qint64 sheetPixels = 0;
        for (const auto &value : characterList) {
            if (!value.isObject()) return fail("Invalid character model sheet entry.");
            const auto entry = value.toObject();
            if (!entry.value("id").isString() || !entry.value("name").isString() || !entry.value("sheets").isArray())
                return fail("Invalid character model sheet metadata.");
            CharacterModel character{entry.value("id").toString(), entry.value("name").toString(), {}};
            for (const auto &sheetValue : entry.value("sheets").toArray()) {
                if (++sheetCount > maxProjectCharacterSheets) return fail("A project supports up to 256 character model sheets.");
                if (!sheetValue.isObject()) return fail("Invalid character model sheet.");
                const auto sheet = sheetValue.toObject();
                if (!sheet.value("id").isString() || !sheet.value("name").isString() || !sheet.value("category").isString())
                    return fail("Invalid character model sheet metadata.");
                ModelSheetCategory category;
                const auto key = sheet.value("category").toString();
                if (key == "turnaround") category = ModelSheetCategory::Turnaround;
                else if (key == "expression") category = ModelSheetCategory::Expression;
                else if (key == "costume") category = ModelSheetCategory::Costume;
                else return fail("Invalid character model sheet category.");
                int width = 0, height = 0;
                if (!integer(sheet, "width", width) || !integer(sheet, "height", height)
                    || width < 1 || width > maxCharacterSheetDimension || height < 1 || height > maxCharacterSheetDimension)
                    return fail("Invalid character model sheet image dimensions.");
                sheetPixels += qint64(width) * height;
                if (sheetPixels > maxProjectCharacterSheetPixels) return fail("Character model sheets exceed the 64 megapixel project limit.");
                character.sheets.append({sheet.value("id").toString(), sheet.value("name").toString(), category, {}});
            }
            if (version >= 28) {
                if (!entry.value("colors").isArray()) return fail("Missing character color model list.");
                const auto colors = entry.value("colors").toArray();
                if (colors.size() > maxCharacterColors) return fail("A character supports up to 256 color swatches.");
                for (const auto &colorValue : colors) {
                    if (++colorCount > maxProjectCharacterColors) return fail("A project supports up to 8192 character color swatches.");
                    if (!colorValue.isObject()) return fail("Invalid character color swatch.");
                    const auto swatch = colorValue.toObject();
                    if (!swatch.value("id").isString() || !swatch.value("name").isString() || !swatch.value("category").isString())
                        return fail("Invalid character color metadata.");
                    CharacterColorCategory category;
                    const auto key = swatch.value("category").toString();
                    if (key == "skin") category = CharacterColorCategory::Skin;
                    else if (key == "hair") category = CharacterColorCategory::Hair;
                    else if (key == "costume") category = CharacterColorCategory::Costume;
                    else if (key == "shadow") category = CharacterColorCategory::Shadow;
                    else return fail("Invalid character color category.");
                    const auto color = characterColorFromJson(swatch.value("color"));
                    if (!color) return fail("Character colors must be written as #AARRGGBB hexadecimal RGBA values.");
                    character.colors.append({swatch.value("id").toString(), swatch.value("name").toString(), category, *color});
                }
            }
            colorSlots += character.colors.size();
            if (version >= 30) {
                if (!entry.value("colorVariants").isArray()) return fail("Missing character color variant list.");
                const auto variants = entry.value("colorVariants").toArray();
                if (variants.size() > maxCharacterColorVariants) return fail("A character supports up to 16 lighting variants in addition to Day.");
                for (const auto &variantValue : variants) {
                    if (!variantValue.isObject()) return fail("Invalid character color variant.");
                    const auto object = variantValue.toObject();
                    if (object.size() != 3 || !object.value("id").isString() || !object.value("name").isString() || !object.value("colors").isObject())
                        return fail("Invalid character color variant metadata.");
                    const auto colors = object.value("colors").toObject();
                    if (colors.size() != character.colors.size()) return fail("Each color variant must contain every shared character color exactly once.");
                    colorSlots += colors.size();
                    if (colorSlots > maxProjectCharacterColorSlots) return fail("Character palettes exceed the 65536 project color-slot limit.");
                    CharacterColorVariant variant{object.value("id").toString(), object.value("name").toString(), {}};
                    for (auto color = colors.constBegin(); color != colors.constEnd(); ++color) {
                        const auto rgba = characterColorFromJson(color.value());
                        if (!rgba) return fail("Variant colors must be written as #AARRGGBB hexadecimal RGBA values.");
                        variant.colors.insert(color.key(), *rgba);
                    }
                    character.colorVariants.append(std::move(variant));
                }
            }
            p.characters.append(character);
        }
    }
    if (version >= 29) {
        if (!o.value("drawingAids").isObject()) return fail("Missing drawing-aid metadata.");
        auto aids = drawingAidsFromJson(o.value("drawingAids").toObject(), error);
        if (!aids) return {};
        p.drawingAids = std::move(*aids);
    }
    if (audioResources(p).keys() != pendingAudio.keys()) return fail("Project contains unused audio resources.");
    const auto validation = p.validateStructure();
    if (!validation.isEmpty()) return fail(validation);
    return p;
}

qint64 characterModelHistoryCost(const Project &before, const Project &after) {
    // Metadata and unchanged images remain implicitly shared across undo
    // snapshots. Charge distinct retained image buffers only when they change.
    QMap<qint64, qint64> previous, next;
    const auto collect = [](const Project &project, QMap<qint64, qint64> &images) {
        for (const auto &character : project.characters) for (const auto &sheet : character.sheets)
            if (!sheet.image.isNull()) images.insert(sheet.image.cacheKey(), sheet.image.sizeInBytes());
    };
    collect(before, previous); collect(after, next);
    qint64 bytes = 0;
    for (auto it = previous.cbegin(); it != previous.cend(); ++it) if (!next.contains(it.key())) bytes += it.value();
    for (auto it = next.cbegin(); it != next.cend(); ++it) if (!previous.contains(it.key())) bytes += it.value();
    // A changed swatch vector detaches from the older undo snapshot. Include
    // names/IDs conservatively; unchanged libraries stay implicitly shared.
    const auto colorBytes = [](const QVector<CharacterColorSwatch> &colors) {
        qint64 size = colors.size() * qint64(sizeof(CharacterColorSwatch));
        for (const auto &swatch : colors) size += 2 * (swatch.id.size() + swatch.name.size());
        return size;
    };
    QMap<QString, const QVector<CharacterColorSwatch> *> oldColors;
    for (const auto &character : before.characters) oldColors.insert(character.id, &character.colors);
    for (const auto &character : after.characters) {
        const auto old = oldColors.take(character.id);
        if (!old) bytes += colorBytes(character.colors);
        else if (*old != character.colors) bytes += colorBytes(*old) + colorBytes(character.colors);
    }
    for (const auto *colors : oldColors) bytes += colorBytes(*colors);
    const auto variantBytes = [](const QVector<CharacterColorVariant> &variants) {
        qint64 size = variants.size() * qint64(sizeof(CharacterColorVariant));
        for (const auto &variant : variants) {
            size += 2 * (variant.id.size() + variant.name.size());
            for (auto color = variant.colors.cbegin(); color != variant.colors.cend(); ++color)
                size += 2 * color.key().size() + sizeof(QString) + sizeof(QColor) + 4 * sizeof(void *);
        }
        return size;
    };
    QMap<QString, const QVector<CharacterColorVariant> *> oldVariants;
    for (const auto &character : before.characters) oldVariants.insert(character.id, &character.colorVariants);
    for (const auto &character : after.characters) {
        const auto old = oldVariants.take(character.id);
        if (!old) bytes += variantBytes(character.colorVariants);
        else if (*old != character.colorVariants) bytes += variantBytes(*old) + variantBytes(character.colorVariants);
    }
    for (const auto *variants : oldVariants) bytes += variantBytes(*variants);
    return bytes;
}

qint64 editableObjectHistoryCost(const Project &before, const Project &after) {
    QMap<QString,const EditableObject *> previous;
    QMap<qint64,qint64> oldImages,newImages;
    auto images=[](const Project &project,QMap<qint64,qint64> &result) {
        for (const auto *shot : projectArtworkShots(project)) for(const auto &layer:shot->layers) for(const auto &object:layer.objects)
            if(object.brushStroke) {
                const auto &b=object.brushStroke->settings;
                for(const auto *image:{&b.tipMask,&b.texture.pattern,&b.dual.tipMask,&b.colorTip,&b.surface.image})
                    if(!image->isNull()) result[image->cacheKey()]=image->sizeInBytes();
            }
    };
    images(before,oldImages); images(after,newImages);
    qint64 cost=0;
    for(auto i=oldImages.cbegin();i!=oldImages.cend();++i) if(!newImages.contains(i.key())) cost+=i.value();
    for(auto i=newImages.cbegin();i!=newImages.cend();++i) if(!oldImages.contains(i.key())) cost+=i.value();
    for (const auto *shot : projectArtworkShots(before)) for(const auto &layer:shot->layers) for(const auto &object:layer.objects) previous.insert(object.id,&object);
    auto sourceCost=[](const EditableObject &object) {
        qint64 size=object.nodes.size()*qint64(sizeof(BezierNode))+object.widthFactors.size()*qint64(sizeof(qreal))+object.text.size()*2;
        if(object.brushStroke) size+=object.brushStroke->samples.size()*qint64(sizeof(VectorBrushSample))
            +object.brushStroke->selection.elementCount()*qint64(sizeof(QPainterPath::Element))
            +object.brushStroke->settings.tipFrames.size()*qint64(sizeof(BrushTipFrame));
        return size;
    };
    for (const auto *shot : projectArtworkShots(after)) for(const auto &layer:shot->layers) for(const auto &object:layer.objects) {
        const auto *old=previous.take(object.id);
        if(!old) { cost+=sourceCost(object); continue; }
        if(old->nodes.constData()!=object.nodes.constData()) cost+=(old->nodes.size()+object.nodes.size())*qint64(sizeof(BezierNode));
        if(old->widthFactors.constData()!=object.widthFactors.constData()) cost+=(old->widthFactors.size()+object.widthFactors.size())*qint64(sizeof(qreal));
        if(old->text!=object.text) cost+=(old->text.size()+object.text.size())*2;
        if(bool(old->brushStroke)!=bool(object.brushStroke)) {
            if(old->brushStroke) cost+=old->brushStroke->samples.size()*qint64(sizeof(VectorBrushSample));
            if(object.brushStroke) cost+=object.brushStroke->samples.size()*qint64(sizeof(VectorBrushSample));
        } else if(object.brushStroke) {
            if(old->brushStroke->samples.constData()!=object.brushStroke->samples.constData())
                cost+=(old->brushStroke->samples.size()+object.brushStroke->samples.size())*qint64(sizeof(VectorBrushSample));
            if(old->brushStroke->selection!=object.brushStroke->selection)
                cost+=(old->brushStroke->selection.elementCount()+object.brushStroke->selection.elementCount())*qint64(sizeof(QPainterPath::Element));
        }
    }
    for(const auto *object:previous) cost+=sourceCost(*object);
    // Rig vectors use copy-on-write like editable paths. Count both retained
    // snapshots only when the rest skeleton actually changes.
    QHash<QString, const Rig3D *> oldRigs;
    auto rigCost = [](const Rig3D &rig) {
        qint64 bytes = rig.joints.size() * qint64(sizeof(RigJoint3D));
        for (const auto &joint : rig.joints)
            bytes += (joint.id.size() + joint.name.size() + joint.parentId.size() + joint.mirrorId.size()) * qint64(sizeof(QChar));
        return bytes;
    };
    for (const auto *shot : projectArtworkShots(before)) for (const auto &layer : shot->layers) if (layer.scene3d)
        for (const auto &object : layer.scene3d->objects) oldRigs.insert(shot->id + '/' + layer.id + '/' + object.id, &object.rig);
    for (const auto *shot : projectArtworkShots(after)) for (const auto &layer : shot->layers) if (layer.scene3d)
        for (const auto &object : layer.scene3d->objects) {
            const auto *old = oldRigs.take(shot->id + '/' + layer.id + '/' + object.id);
            if (!old) cost += rigCost(object.rig);
            else if (*old != object.rig) cost += rigCost(*old) + rigCost(object.rig);
        }
    for (const auto *rig : oldRigs) cost += rigCost(*rig);
    QHash<QString, const Skin3D *> oldSkins;
    for (const auto *shot : projectArtworkShots(before)) for (const auto &layer : shot->layers) if (layer.scene3d)
        for (const auto &object : layer.scene3d->objects) if (object.skin) oldSkins.insert(shot->id + '/' + layer.id + '/' + object.id, &*object.skin);
    for (const auto *shot : projectArtworkShots(after)) for (const auto &layer : shot->layers) if (layer.scene3d)
        for (const auto &object : layer.scene3d->objects) if (object.skin) {
            const auto *old = oldSkins.take(shot->id + '/' + layer.id + '/' + object.id);
            if (!old) cost += skin3DByteSize(*object.skin);
            else if (*old != *object.skin) cost += skin3DByteSize(*old) + skin3DByteSize(*object.skin);
        }
    for (const auto *skin : oldSkins) cost += skin3DByteSize(*skin);
    QHash<QString, const Pose3D *> oldPoses;
    for (const auto *shot : projectArtworkShots(before)) for (const auto &layer : shot->layers) if (layer.scene3d)
        for (const auto &object : layer.scene3d->objects) oldPoses.insert(shot->id + '/' + layer.id + '/' + object.id, &object.pose);
    for (const auto *shot : projectArtworkShots(after)) for (const auto &layer : shot->layers) if (layer.scene3d)
        for (const auto &object : layer.scene3d->objects) {
            const auto *old = oldPoses.take(shot->id + '/' + layer.id + '/' + object.id);
            if (!old) cost += pose3DByteSize(object.pose);
            else if (*old != object.pose) cost += pose3DByteSize(*old) + pose3DByteSize(object.pose);
        }
    for (const auto *pose : oldPoses) cost += pose3DByteSize(*pose);
    const auto oldModels = modelResources(before), newModels = modelResources(after);
    QSet<qint64> textures;
    auto modelCost = [&textures](const Model3D &model) {
        qint64 bytes = 0;
        for (const auto &mesh : model.meshes) {
            bytes += mesh.positions.size() * qint64(sizeof(QVector3D))
                + mesh.normals.size() * qint64(sizeof(QVector3D))
                + mesh.texCoords.size() * qint64(sizeof(QVector2D))
                + mesh.indices.size() * qint64(sizeof(quint32));
            if (!mesh.baseColorTexture.isNull() && !textures.contains(mesh.baseColorTexture.cacheKey())) {
                textures.insert(mesh.baseColorTexture.cacheKey());
                bytes += mesh.baseColorTexture.sizeInBytes();
            }
        }
        return bytes;
    };
    for (auto it = oldModels.cbegin(); it != oldModels.cend(); ++it)
        if (!newModels.contains(it.key()) && it.value()) cost += modelCost(*it.value());
    for (auto it = newModels.cbegin(); it != newModels.cend(); ++it)
        if (!oldModels.contains(it.key()) && it.value()) cost += modelCost(*it.value());
    return cost;
}

} // namespace toon
