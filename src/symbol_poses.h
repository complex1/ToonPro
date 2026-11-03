#pragma once
#include "symbol_types.h"
#include <QJsonArray>

namespace toon {
class Document;
struct Shot;
struct Project;
inline constexpr int maxSymbolPoseKeys = 4096;
inline constexpr int maxProjectSymbolPoseKeys = 65536;
inline constexpr int minSymbolPoseClock = -2000000;
inline constexpr int maxSymbolPoseClock = 2000000;
int symbolPoseClockFrame(const SymbolInstance &instance, int parentFrame);
int symbolPoseParentFrame(const SymbolInstance &instance, int clockFrame);
const SymbolPoseKey *symbolPoseKeyAtParent(const SymbolInstance &instance, int parentFrame);
// Evaluates the held pose without clip visibility, for editing and ripple seams.
int evaluatedSymbolPose(const SymbolInstance &instance, int parentFrame);
QString validateSymbolPoses(const SymbolInstance &instance, int sourceDuration);
QString validateProjectSymbolPoses(const Project &project);
QJsonArray symbolPoseKeysToJson(const QVector<SymbolPoseKey> &keys);
bool symbolPoseKeysFromJson(const QJsonArray &array, QVector<SymbolPoseKey> *keys, QString *error);
qint64 symbolPoseHistoryCost(const Shot &before, const Shot &after);
QString symbolPoseEditError(const Shot &shot, const QString &layerId);
bool setSymbolPose(Document *document, const QString &layerId, int parentFrame, int poseFrame, QString *error = nullptr);
bool removeSymbolPose(Document *document, const QString &layerId, int parentFrame, QString *error = nullptr);
} // namespace toon
