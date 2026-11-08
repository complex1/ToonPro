#pragma once
#include "reference3d.h"

namespace toon {
enum class ReferenceCatalogKind { Body, Head, Hand, Anime, Pet };
struct ReferenceCatalogEntry {
    QString id, name, description;
    ReferenceCatalogKind kind;
    QString sourceUrl;
};
// Metadata only: enumerating the catalog does not decode or generate meshes.
QVector<ReferenceCatalogEntry> referenceModelCatalog();
// Prepared models are decoded lazily and shared as immutable assets.
ReferenceAssetPtr createReferenceCatalogAsset(const QString &id, QString *error = nullptr);
} // namespace toon
