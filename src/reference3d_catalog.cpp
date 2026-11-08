#include "reference3d_catalog.h"
#include "reference3d_builtin.h"
#include <QFile>
#include <QMutex>
#include <QMutexLocker>

static void initializeReferenceCatalogResource() { Q_INIT_RESOURCE(reference3d_catalog); }

namespace toon {
QVector<ReferenceCatalogEntry> referenceModelCatalog() {
    QVector<ReferenceCatalogEntry> entries;
    for (const auto &preset : referenceStarterPresets()) {
        const auto kind = preset.kind == ReferenceStarterKind::Figure ? ReferenceCatalogKind::Body
            : preset.kind == ReferenceStarterKind::Head ? ReferenceCatalogKind::Head : ReferenceCatalogKind::Hand;
        entries.append({preset.id, preset.name, preset.description, kind, {}});
    }
    entries.append({"anime/seed-san", "Seed-san", "Anime character with a humanoid rig, fingers and facial expressions.",
        ReferenceCatalogKind::Anime, "https://github.com/vrm-c/vrm-specification/tree/94e82dd346fa6cf0337c4421728640e5252dd38e/samples/Seed-san"});
    entries.append({"anime/alicia-solid", "Alicia", "Anime character with a humanoid rig, fingers and facial expressions.",
        ReferenceCatalogKind::Anime, "https://github.com/vrm-c/UniVRM/tree/efd20f62f1bf0ac3fcc34de1e497324c5197d146/Tests/Models/Alicia_vrm-0.51"});
    const QString animals = "https://opengameart.org/content/animated-animales-low-poly";
    entries.append({"pet/dog", "Dog", "Lightweight stylized dog with an animal rig and pose presets.", ReferenceCatalogKind::Pet, animals});
    entries.append({"pet/cat", "Cat", "Lightweight stylized cat with an animal rig and pose presets.", ReferenceCatalogKind::Pet, animals});
    entries.append({"pet/bird", "Eagle", "Lightweight stylized eagle with a wing rig and pose presets.", ReferenceCatalogKind::Pet, animals});
    return entries;
}

ReferenceAssetPtr createReferenceCatalogAsset(const QString &id, QString *error) {
    for (const auto &preset : referenceStarterPresets())
        if (preset.id == id) return createReferenceStarter(id, error);
    static const QHash<QString, QString> resources{
        {"anime/seed-san", ":/reference3d/seed-san-v1.toon3dasset"},
        {"anime/alicia-solid", ":/reference3d/alicia-solid-v1.toon3dasset"},
        {"pet/dog", ":/reference3d/dog-v1.toon3dasset"},
        {"pet/cat", ":/reference3d/cat-v1.toon3dasset"},
        {"pet/bird", ":/reference3d/bird-v1.toon3dasset"}
    };
    const auto path = resources.value(id);
    if (path.isEmpty()) { if (error) *error = "Unknown model preset: " + id; return {}; }
    static const bool initialized = [] { initializeReferenceCatalogResource(); return true; }();
    Q_UNUSED(initialized);
    static QMutex mutex;
    static QHash<QString, ReferenceAssetPtr> cache;
    QMutexLocker lock(&mutex);
    if (const auto cached = cache.value(id)) { if (error) error->clear(); return cached; }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { if (error) *error = "The prepared model is missing from this build: " + id; return {}; }
    auto asset = deserializeReferenceAsset(file.readAll(), error);
    if (asset) cache.insert(id, asset);
    return asset;
}
} // namespace toon
