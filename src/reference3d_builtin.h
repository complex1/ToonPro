#pragma once
#include "reference3d.h"

namespace toon {
enum class ReferenceStarterKind { Figure, Head, Hand };
struct ReferenceStarter { QString id, name; ReferenceStarterKind kind; ReferenceAssetPtr asset; };
struct ReferenceShapeParameter { QString id, name; float minimum, maximum, value; int decimals = 2; QString group; };
struct ReferenceStarterPreset { QString id, name, description; ReferenceStarterKind kind; };
struct ReferenceShapeFamilyWeight { QString id, name; float weight = 0; };
ReferenceAssetPtr createReferenceStarter(ReferenceStarterKind kind, QString *error = nullptr);
ReferenceAssetPtr createReferenceStarter(const QString &presetId, QString *error = nullptr);
// Cheap metadata catalog; unlike referenceStarters(), this creates no meshes.
QVector<ReferenceStarterPreset> referenceStarterPresets();
QVector<ReferenceStarter> referenceStarters();
QVector<ReferenceShapeParameter> referenceBodyShapeParameters(const ReferenceInstance &instance);
bool setReferenceBodyShape(ReferenceInstance &instance, const QString &parameter, float value, QString *error = nullptr);
// One validated regeneration/pose-preserving commit for a multi-axis edit.
bool setReferenceBodyShapeValues(ReferenceInstance &instance, const QHash<QString, float> &values, QString *error = nullptr);
QVector<ReferenceShapeFamilyWeight> referenceShapeFamilyMix(const ReferenceInstance &instance, ReferenceStarterKind family);
bool referenceShapeFamilyMixLimited(const ReferenceInstance &instance, ReferenceStarterKind family);
bool setReferenceShapeFamilyMix(ReferenceInstance &instance, ReferenceStarterKind family,
    const QHash<QString, float> &weights, bool limit, QString *error = nullptr);
bool applyReferenceShapePreset(ReferenceInstance &instance, const QString &presetId, QString *error = nullptr);
bool applyReferenceBuiltinShape(const ReferenceInstance &source, ReferenceInstance &target, QString *error = nullptr);
} // namespace toon
