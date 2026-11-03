#pragma once

#include <QColor>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVector>
#include <optional>
#include <functional>
#include "drawing_guide.h"
#include "raster.h"
#include "animation.h"
#include "property_animation.h"
#include "audio.h"
#include "camera_animation.h"
#include "drawing_aids.h"
#include "timeline_cues.h"

namespace toon {

inline constexpr int latestProjectFormatVersion = 37;
inline constexpr int maxProjectSymbols = 128;
inline constexpr int maxSymbolNestingDepth = 4;
inline constexpr int maxProjectShots = 10000;
inline constexpr int maxProjectReferences = 16;
inline constexpr qint64 maxProjectReferencePixels = 16000000;
inline constexpr int maxReferenceDimension = 4096;
inline constexpr int maxProjectCharacters = 128;
inline constexpr int maxProjectCharacterSheets = 256;
inline constexpr qint64 maxProjectCharacterSheetPixels = 64000000;
inline constexpr int maxCharacterSheetDimension = 4096;
inline constexpr int maxCharacterColors = 256;
inline constexpr int maxProjectCharacterColors = 8192;
inline constexpr int maxCharacterColorVariants = 16;
inline constexpr int maxProjectCharacterColorSlots = 65536;
inline constexpr int maxProjectDrawingGuides = 256;
inline constexpr int maxProjectModelResources = 64;
inline constexpr qint64 maxProjectModelDecodedBytes = 256LL * 1024 * 1024;

struct ReferenceImage {
    QString id;
    QString name;
    QImage image;
};

enum class ModelSheetCategory { Turnaround, Expression, Costume };

struct CharacterModelSheet {
    QString id;
    QString name;
    ModelSheetCategory category = ModelSheetCategory::Turnaround;
    QImage image;
};

enum class CharacterColorCategory { Skin, Hair, Costume, Shadow };

struct CharacterColorSwatch {
    QString id, name;
    CharacterColorCategory category = CharacterColorCategory::Skin;
    QColor color;
    bool operator==(const CharacterColorSwatch &) const = default;
};

struct CharacterColorVariant {
    QString id, name;
    // Every variant keeps one independent color for every shared swatch role.
    QMap<QString, QColor> colors;
    bool operator==(const CharacterColorVariant &) const = default;
};

struct CharacterModel {
    QString id;
    QString name;
    QVector<CharacterModelSheet> sheets;
    QVector<CharacterColorSwatch> colors = {};
    QVector<CharacterColorVariant> colorVariants = {};
};

// An empty variant ID selects Day. Unknown variants return no colors.
QVector<CharacterColorSwatch> characterVariantColors(const CharacterModel &character, const QString &variantId = {});
QVector<CharacterColorVariant> standardCharacterColorVariants(const QVector<CharacterColorSwatch> &colors);

struct SheetSettings {
    int left = 100;
    int right = 100;
    int top = 60;
    int bottom = 100;
    int header = 160;
    int overscan = 120;
    bool safeArea = false;
    bool showHeader = true;
    bool showGuides = true;
};

struct NamedSelection {
    QString id;
    QString name;
    QPainterPath path;
    qreal featherRadius = 0;
};

struct Shot {
    QString id;
    QString number = "C001";
    QString name = "First shot";
    int durationFrames = 120;
    SheetSettings sheet;
    QVector<RasterLayer> layers;
    QVector<ReferenceImage> references;
    QVector<DrawingGuide> guides;
    QVector<NamedSelection> savedSelections;
    GuideSettings guideSettings;
    QVector<AnimationTrack> animationTracks;
    QVector<LayerAnimation> layerAnimations;
    QVector<AudioClip> audioClips;
    Camera2D camera2D;
    QVector<Camera2DKeyframe> camera2DKeys;
    QVector<SceneCameraAnimation> sceneCameraAnimations;
    QString scene = "Scene 1";
    QVector<TimelineCue> timelineCues;
};

struct SymbolDefinition {
    QString id, name;
    Shot content;
};

// Project data contains no workspace zoom, rotation, pan or docking state.
struct Project {
    QString movie = "Untitled movie";
    int width = 1920;
    int height = 1080;
    double fps = 24.0;
    bool transparent = false;
    QVector<Shot> shots;
    QVector<SymbolDefinition> symbols;
    // Shared production references: stored once and available in every shot.
    QVector<CharacterModel> characters;
    // Saved drawing aids are separate from the renderable artwork.
    DrawingAids drawingAids;

    static Project create();
    QString validate() const;
    QJsonObject toJson() const;
    // Parses metadata only. Binary artwork and reference pixels are loaded by loadProject.
    static std::optional<Project> fromJson(const QJsonObject &object, QString *error);

private:
    QString validateStructure() const;
    bool m_brushResourcesLoaded = true;
    bool m_sceneResourcesLoaded = true;
    bool m_audioResourcesLoaded = true;
    bool m_maskResourcesLoaded = true;
    friend std::optional<Project> loadProject(const QString &path, QString *error);
};

// Canonical artwork only: movie shots followed by reusable definitions, once each.
QVector<const Shot *> projectArtworkShots(const Project &project);
QVector<Shot *> projectArtworkShots(Project &project);
int projectTileCount(const Project &project, const QString &excludedLayerId = {});
int projectMaskTileCount(const Project &project, const QString &excludedLayerId = {});

bool saveProject(const Project &project, const QString &path, QString *error,
                 const std::function<bool()> &cancelled = {});
std::optional<Project> loadProject(const QString &path, QString *error);
// Additional history storage for detached path/tablet data and stamp resources.
qint64 editableObjectHistoryCost(const Project &before, const Project &after);
qint64 characterModelHistoryCost(const Project &before, const Project &after);

} // namespace toon
