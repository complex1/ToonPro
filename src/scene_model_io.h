#pragma once
#include "scene3d.h"
namespace toon {
// Parsing is independent of QObject state and can run in a worker thread.
Model3DPtr importModel3D(const QString &filePath, QString *error = nullptr);
QByteArray serializeModel3D(const Model3D &model, QString *error = nullptr);
Model3DPtr deserializeModel3D(const QByteArray &bytes, QString *error = nullptr);
// Validates and computes bounds/content ID. Input is moved into immutable storage.
Model3DPtr finalizeModel3D(Model3D model, QString *error = nullptr);
} // namespace toon
