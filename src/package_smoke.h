#pragma once
#include <QString>
namespace toon {
// Uses only the caller's fresh output folder and an isolated workspace profile.
// Returns 0 only when startup, resources and real media round trips pass.
int runPackageSmoke(const QString &outputDirectory, bool require3D);
} // namespace toon
