#pragma once
#include <QColor>
#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <optional>

namespace toon {
enum class GuideType { Perspective, VanishingPoint, TwoPoint, ParallelRuler, ThreePoint, Fisheye, Ellipse };
inline constexpr int maxDrawingGuides = 64;
struct DrawingGuide {
    QString id;
    QString name;
    GuideType type = GuideType::Perspective;
    // Quad: 4 corners; VP: 1; two/three-point: VPs + grid anchor;
    // parallel: 2; fisheye/ellipse: 2 axis endpoints + perpendicular-radius handle.
    QVector<QPointF> points;
    bool visible = true;
    bool locked = false;
    bool snapping = true;
    bool customColor = false;
    QColor color = QColor("#5681ff");
    qreal opacity = .6;
    int subdivisions = 8;
    bool vertical = true;
    bool limited = false;
    QRectF area{0, 0, 1920, 1080};
    bool operator==(const DrawingGuide &) const = default;
};
struct GuideSettings {
    bool visible = true;
    bool snapping = false;
    bool handles = true;
    QColor color = QColor("#5681ff");
    qreal opacity = .6;
    bool operator==(const GuideSettings &) const = default;
};
int guidePointCount(GuideType type);
QString guideTypeName(GuideType type);
QString validateGuides(const QVector<DrawingGuide> &guides, const GuideSettings &settings);
QJsonObject guidesToJson(const QVector<DrawingGuide> &guides, const GuideSettings &settings);
bool guidesFromJson(const QJsonObject &object, QVector<DrawingGuide> *guides, GuideSettings *settings, QString *error);
bool saveGuideSet(const QVector<DrawingGuide> &guides, const GuideSettings &settings, const QString &path, QString *error);
bool loadGuideSet(const QString &path, QVector<DrawingGuide> *guides, GuideSettings *settings, QString *error);
} // namespace toon
