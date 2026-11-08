#include "icons.h"
#include "ui_theme.h"

#include <QApplication>
#include <QFile>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QPixmapCache>
#include <QResource>
#include <QSvgRenderer>

#include <utility>

// This reference also retains the resource object when toon_workspace is static.
static void initializeIconResources()
{
    Q_INIT_RESOURCE(toon_icons);
}

namespace toon {
namespace {

class LucideIconEngine final : public QIconEngine {
public:
    LucideIconEngine(QString name, QByteArray svg)
        : m_name(std::move(name)), m_svg(std::move(svg)) {}

    QIconEngine *clone() const override { return new LucideIconEngine(m_name, m_svg); }
    QString key() const override { return QStringLiteral("ToonProLucide"); }
    QString iconName() override { return m_name; }
    bool isNull() override { return m_svg.isEmpty(); }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        const auto ratio = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, ratio));
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state,
                         qreal scale) override
    {
        if (size.isEmpty() || scale <= 0) return {};
        const auto color = qApp ? qApp->palette().color(mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active,
                                                       QPalette::ButtonText)
                               : QColor(mode == QIcon::Disabled ? ui::disabled : ui::text);
        const auto cacheKey = QString("toon-lucide/%1/%2x%3/%4/%5")
            .arg(m_name).arg(size.width()).arg(size.height()).arg(scale, 0, 'g', 12).arg(color.rgba());
        QPixmap cached;
        if (QPixmapCache::find(cacheKey, &cached)) return cached;
        QPixmap result(size * scale);
        result.setDevicePixelRatio(scale);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        QByteArray svg = m_svg;
        svg.replace("currentColor", color.name(QColor::HexRgb).toLatin1());
        QSvgRenderer renderer(svg);
        painter.setRenderHint(QPainter::Antialiasing);
        // List decorations may reserve a wide slot for brush previews. Keep
        // ordinary SVG icons proportional within that slot, including their
        // stroke weight, instead of stretching them to fill it.
        const QSizeF drawingSize = renderer.viewBoxF().size().scaled(QSizeF(size), Qt::KeepAspectRatio);
        const QPointF origin((size.width() - drawingSize.width()) / 2,
                             (size.height() - drawingSize.height()) / 2);
        renderer.render(&painter, QRectF(origin, drawingSize));
        painter.end();
        QPixmapCache::insert(cacheKey, result);
        Q_UNUSED(state);
        return result;
    }

private:
    QString m_name;
    QByteArray m_svg;
};

}

QIcon appIcon(const QString &name)
{
    static const bool initialized = [] {
        initializeIconResources();
        return true;
    }();
    Q_UNUSED(initialized);
    static QHash<QString, QIcon> icons;
    if (const auto entry = icons.constFind(name); entry != icons.cend()) return *entry;
    QFile source(name.startsWith("toon-")
        ? QStringLiteral(":/icons/toon/%1.svg").arg(name.mid(5))
        : QStringLiteral(":/icons/lucide/%1.svg").arg(name));
    if (!source.open(QIODevice::ReadOnly)) return {};
    const auto icon = QIcon(new LucideIconEngine(name, source.readAll()));
    icons.insert(name, icon);
    return icon;
}

namespace {
struct FeatureSymbol { const char *name; const char *label; };
FeatureSymbol symbol(FeatureIcon feature)
{
    switch (feature) {
    case FeatureIcon::Arc: return {"toon-arc", "Arc"};
    case FeatureIcon::Bezier: return {"toon-bezier", "Bézier curve"};
    case FeatureIcon::Simplify: return {"toon-simplify", "Simplify path"};
    case FeatureIcon::Pressure: return {"toon-pressure", "Pen pressure"};
    case FeatureIcon::Tilt: return {"toon-tilt", "Pen tilt and pose"};
    case FeatureIcon::Texture: return {"toon-texture", "Texture"};
    case FeatureIcon::Tone: return {"toon-tone", "Patterns and tones"};
    case FeatureIcon::PerspectiveGrid: return {"toon-perspective-grid", "Perspective grid"};
    case FeatureIcon::VanishingPoint: return {"toon-vanishing-point", "Vanishing point"};
    case FeatureIcon::TwoPointPerspective: return {"toon-two-point", "Two-point perspective"};
    case FeatureIcon::ParallelGuide: return {"toon-parallel", "Parallel ruler"};
    case FeatureIcon::ThreePointPerspective: return {"toon-three-point", "Three-point perspective"};
    case FeatureIcon::Fisheye: return {"toon-fisheye", "Fisheye perspective"};
    case FeatureIcon::Warp: return {"toon-warp", "Warp"};
    case FeatureIcon::LinearGradient: return {"toon-gradient-linear", "Linear gradient"};
    case FeatureIcon::RadialGradient: return {"toon-gradient-radial", "Radial gradient"};
    case FeatureIcon::Blur: return {"toon-blur", "Blur"};
    case FeatureIcon::AlphaLock: return {"toon-alpha-lock", "Lock transparent pixels"};
    case FeatureIcon::Clipping: return {"toon-clipping", "Clip to layer below"};
    case FeatureIcon::FillReference: return {"toon-fill-reference", "Fill reference"};
    case FeatureIcon::VectorNudge: return {"toon-vector-nudge", "Nudge vector strokes"};
    }
    return {"", ""};
}
}
QString featureIconName(FeatureIcon feature) { return QString::fromLatin1(symbol(feature).name); }
QString featureIconLabel(FeatureIcon feature) { return QString::fromUtf8(symbol(feature).label); }
QIcon featureIcon(FeatureIcon feature) { return appIcon(featureIconName(feature)); }

}
