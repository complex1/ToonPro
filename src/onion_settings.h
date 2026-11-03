#pragma once
#include "ui_theme.h"
#include <QColor>
#include <QJsonObject>
#include <QMetaType>

namespace toon {
inline constexpr int maxOnionDrawings = 8;
// Workspace display preferences only: never stored in artwork or exports.
struct OnionSkinSettings {
    int previousCount = 1, nextCount = 1;
    QColor previousColor = QColor(ui::onionPrevious), nextColor = QColor(ui::onionNext);
    qreal opacity = .25;
    bool operator==(const OnionSkinSettings &) const = default;
};
bool validOnionSkinSettings(const OnionSkinSettings &settings);
QJsonObject onionSkinSettingsToJson(const OnionSkinSettings &settings);
// A malformed preference is discarded as a whole, retaining safe defaults.
OnionSkinSettings onionSkinSettingsFromJson(const QJsonObject &json);
} // namespace toon
Q_DECLARE_METATYPE(toon::OnionSkinSettings)
