#include "advanced_brush_dynamics_settings.h"
#include "brush_engine.h"
#include <QSet>
#include <cmath>

namespace toon {
namespace {
constexpr qreal pi = 3.14159265358979323846;
bool range(qreal v, qreal low, qreal high) { return std::isfinite(v) && v >= low && v <= high; }
bool fail(QString *error) { if (error) *error = "Invalid advanced brush dynamics."; return false; }
bool fields(const QJsonObject &json, const QSet<QString> &keys) {
    if (json.size() != keys.size()) return false;
    for (auto it = json.begin(); it != json.end(); ++it) if (!keys.contains(it.key())) return false;
    return true;
}
bool number(const QJsonObject &json, const char *key, qreal &value) {
    if (!json.value(key).isDouble() || !std::isfinite(json.value(key).toDouble())) return false;
    value = json.value(key).toDouble(); return true;
}
}
bool validateAdvancedBrushDynamics(const BrushClusterSpray &s, const BrushSpeedDynamics &v, QString *error) {
    if (s.clusters < 1 || s.clusters > 16 || !range(s.radius, 0, 5) || !range(s.spread, 0, 2)
        || !range(s.radialBias, 0, 1) || !range(s.angularSpread, 0, 360) || !range(s.angle, -180, 180)
        || !range(v.referenceSpeed, 1, 1000000) || !range(v.sizeAmount, -.95, 1) || !range(v.opacityAmount, -1, 1))
        return fail(error);
    return true;
}
QJsonObject clusterSprayToJson(const BrushClusterSpray &s) {
    return {{"enabled", s.enabled}, {"clusters", s.clusters}, {"radius", s.radius}, {"spread", s.spread},
        {"radialBias", s.radialBias}, {"angularSpread", s.angularSpread}, {"angle", s.angle}};
}
QJsonObject speedDynamicsToJson(const BrushSpeedDynamics &s) {
    return {{"enabled", s.enabled}, {"referenceSpeed", s.referenceSpeed}, {"sizeAmount", s.sizeAmount}, {"opacityAmount", s.opacityAmount}};
}
bool clusterSprayFromJson(const QJsonObject &json, BrushClusterSpray &settings, QString *error) {
    BrushClusterSpray s; qreal clusters;
    if (!fields(json, {"enabled", "clusters", "radius", "spread", "radialBias", "angularSpread", "angle"})
        || !json.value("enabled").isBool() || !number(json, "clusters", clusters) || clusters != std::floor(clusters)
        || !range(clusters, 1, 16) || !number(json, "radius", s.radius) || !number(json, "spread", s.spread)
        || !number(json, "radialBias", s.radialBias) || !number(json, "angularSpread", s.angularSpread)
        || !number(json, "angle", s.angle)) return fail(error);
    s.enabled = json.value("enabled").toBool(); s.clusters = int(clusters);
    if (!validateAdvancedBrushDynamics(s, {}, error)) return false;
    settings = s; return true;
}
bool speedDynamicsFromJson(const QJsonObject &json, BrushSpeedDynamics &settings, QString *error) {
    BrushSpeedDynamics s;
    if (!fields(json, {"enabled", "referenceSpeed", "sizeAmount", "opacityAmount"}) || !json.value("enabled").isBool()
        || !number(json, "referenceSpeed", s.referenceSpeed) || !number(json, "sizeAmount", s.sizeAmount)
        || !number(json, "opacityAmount", s.opacityAmount)) return fail(error);
    s.enabled = json.value("enabled").toBool();
    if (!validateAdvancedBrushDynamics({}, s, error)) return false;
    settings = s; return true;
}
qreal brushSpeedFactor(const BrushSpeedDynamics &s, qreal speed, qreal amount) {
    if (!s.enabled) return 1;
    const qreal t = qBound(0., std::isfinite(speed) ? speed / qMax(1., s.referenceSpeed) : 0., 1.);
    return qMax(0., 1 + amount * t);
}
QPointF brushClusterOffset(const BrushClusterSpray &s, qreal size, qreal direction, quint64 index, quint32 seed, int copy) {
    if (!s.enabled) return {};
    // All Count copies share centers; their local radial/angle samples remain
    // independent. Balanced assignment guarantees every configured cluster is
    // represented when Count is at least the number of clusters.
    const int cluster = qMax(0, copy) % qBound(1, s.clusters, 16);
    const quint32 channel = 510 + quint32(cluster) * 4;
    const qreal radial = brushRandom(seed, index, channel);
    const qreal exponent = std::pow(2., (0.5 - s.radialBias) * 6) * .5;
    const qreal radius = size * s.radius * std::pow(radial, exponent);
    const qreal angle = (direction + s.angle + (brushRandom(seed, index, channel + 1) - .5) * s.angularSpread) * pi / 180;
    const qreal localRadius = size * s.spread * std::sqrt(brushRandom(seed, index, 610 + quint32(qMax(0, copy)) * 2));
    const qreal localAngle = brushRandom(seed, index, 611 + quint32(qMax(0, copy)) * 2) * pi * 2;
    return {radius * std::cos(angle) + localRadius * std::cos(localAngle), radius * std::sin(angle) + localRadius * std::sin(localAngle)};
}
void BrushSpeedTracker::reset(QPointF point, qreal milliseconds) {
    m_point = point; m_time = milliseconds; m_speed = 0; m_active = true; m_hasVelocity = false;
}
qreal BrushSpeedTracker::update(QPointF point, qreal milliseconds) {
    if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || !std::isfinite(milliseconds)) return m_speed;
    if (!m_active || milliseconds < m_time) { reset(point, milliseconds); return 0; }
    const qreal elapsed = milliseconds - m_time;
    if (elapsed <= 0) return m_speed;
    const qreal instantaneous = qBound(0., std::hypot(point.x() - m_point.x(), point.y() - m_point.y()) * 1000 / elapsed, 1000000.);
    const qreal blend = m_hasVelocity ? 1 - std::exp(-elapsed / 24.) : 1;
    m_speed += (instantaneous - m_speed) * blend;
    m_point = point; m_time = milliseconds; m_hasVelocity = true;
    return m_speed;
}
} // namespace toon
