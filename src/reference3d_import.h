#pragma once

#include "reference3d.h"
#include <QStringList>
#include <atomic>

namespace toon {
struct ReferenceImportOptions {
    std::shared_ptr<std::atomic_bool> cancelled;
};
struct ReferenceImportResult {
    ReferenceAssetPtr asset;
    QStringList warnings;
    QString error;
    explicit operator bool() const { return bool(asset); }
};
// Pure data import: safe on a worker thread. Never resolves network resources or
// resources outside the source directory. Failed imports publish no asset.
ReferenceImportResult importReferenceAsset(const QString &path, const ReferenceImportOptions &options = {});
} // namespace toon
