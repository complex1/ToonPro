#include "onion_settings.h"
#include <cmath>

namespace toon {
bool validOnionSkinSettings(const OnionSkinSettings &settings) {
    return settings.previousCount >= 0 && settings.previousCount <= maxOnionDrawings
        && settings.nextCount >= 0 && settings.nextCount <= maxOnionDrawings
        && settings.previousColor.isValid() && settings.previousColor.alpha() == 255
        && settings.nextColor.isValid() && settings.nextColor.alpha() == 255
        && std::isfinite(settings.opacity) && settings.opacity >= 0 && settings.opacity <= 1;
}
QJsonObject onionSkinSettingsToJson(const OnionSkinSettings &settings) {
    const auto value = validOnionSkinSettings(settings) ? settings : OnionSkinSettings{};
    return {{"previousCount", value.previousCount}, {"nextCount", value.nextCount},
            {"previousColor", value.previousColor.name(QColor::HexRgb)},
            {"nextColor", value.nextColor.name(QColor::HexRgb)}, {"opacity", value.opacity}};
}
OnionSkinSettings onionSkinSettingsFromJson(const QJsonObject &json) {
    if (json.size() != 5 || !json.value("previousCount").isDouble()
        || !json.value("nextCount").isDouble() || !json.value("previousColor").isString()
        || !json.value("nextColor").isString() || !json.value("opacity").isDouble()) return {};
    const double previous = json.value("previousCount").toDouble();
    const double next = json.value("nextCount").toDouble();
    if (!std::isfinite(previous) || !std::isfinite(next) || previous != std::floor(previous)
        || next != std::floor(next) || previous < 0 || previous > maxOnionDrawings
        || next < 0 || next > maxOnionDrawings) return {};
    OnionSkinSettings settings;
    settings.previousCount = int(previous); settings.nextCount = int(next);
    settings.previousColor = QColor(json.value("previousColor").toString());
    settings.nextColor = QColor(json.value("nextColor").toString());
    settings.opacity = json.value("opacity").toDouble();
    return validOnionSkinSettings(settings) ? settings : OnionSkinSettings{};
}
} // namespace toon
