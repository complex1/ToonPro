#pragma once
#include <QJsonObject>
#include <QPointF>
#include <QString>

namespace toon {
struct BrushClusterSpray {
    bool enabled = false;
    int clusters = 3;
    // Distances are multiples of the configured brush size.
    qreal radius = 1;
    qreal spread = .15;
    // 0 concentrates centers near the origin, .5 is area-uniform, 1 forms a ring.
    qreal radialBias = .5;
    qreal angularSpread = 360;
    qreal angle = 0;
    bool operator==(const BrushClusterSpray &) const = default;
};
struct BrushSpeedDynamics {
    bool enabled = false;
    qreal referenceSpeed = 1000;
    // Signed changes at reference speed: negative is smaller/lighter when fast.
    qreal sizeAmount = -.5;
    qreal opacityAmount = -.3;
    bool operator==(const BrushSpeedDynamics &) const = default;
};
bool validateAdvancedBrushDynamics(const BrushClusterSpray &spray, const BrushSpeedDynamics &speed, QString *error = nullptr);
QJsonObject clusterSprayToJson(const BrushClusterSpray &settings);
QJsonObject speedDynamicsToJson(const BrushSpeedDynamics &settings);
bool clusterSprayFromJson(const QJsonObject &json, BrushClusterSpray &settings, QString *error = nullptr);
bool speedDynamicsFromJson(const QJsonObject &json, BrushSpeedDynamics &settings, QString *error = nullptr);
qreal brushSpeedFactor(const BrushSpeedDynamics &settings, qreal pixelsPerSecond, qreal amount);
QPointF brushClusterOffset(const BrushClusterSpray &settings, qreal size, qreal direction,
                          quint64 dabIndex, quint32 strokeSeed, int copy);

// Derives velocity from event timestamps, not painting/compositing duration.
// Duplicate timestamps retain the last timed sample, so the next packet includes
// the complete distance instead of fabricating a high-speed jump.
class BrushSpeedTracker {
public:
    void reset(QPointF point, qreal milliseconds);
    qreal update(QPointF point, qreal milliseconds);
    qreal speed() const { return m_speed; }
private:
    QPointF m_point;
    qreal m_time = 0, m_speed = 0;
    bool m_active = false, m_hasVelocity = false;
};
} // namespace toon
