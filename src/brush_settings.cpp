#include "brush_settings.h"
#include "brush_surface_pattern.h"

#include <QJsonValue>
#include <QJsonArray>
#include <QCache>
#include <QSet>
#include <cmath>
#include <utility>

namespace toon {
namespace {
bool fail(QString *error, const QString &message) {
    if (error) *error = message;
    return false;
}
bool inRange(qreal value, qreal low, qreal high) {
    return std::isfinite(value) && value >= low && value <= high;
}
template <typename Enum> bool enumIn(Enum value, Enum last) {
    return int(value) >= 0 && int(value) <= int(last);
}
bool validDynamic(const BrushDynamic &value) {
    for (qreal sample : value.response) if (!inRange(sample, 0, 1)) return false;
    return inRange(value.jitter, 0, 1) && inRange(value.minimum, 0, 1) &&
           enumIn(value.control, BrushControl::Rotation) && inRange(value.fade, 1, 10000);
}
QJsonObject dynamicJson(const BrushDynamic &value) {
    QJsonObject result{{"jitter", value.jitter}, {"minimum", value.minimum},
                       {"control", int(value.control)}, {"fade", value.fade}};
    if (value.response != BrushDynamic{}.response) {
        QJsonArray response;
        for (qreal sample : value.response) response.append(sample);
        result["response"] = response;
    }
    if (value.jitterPerStroke) result["jitterPerStroke"] = true;
    return result;
}
// Exact schemas prevent a newer or misspelled setting from being silently lost
// when an older application saves the library again.
class Reader {
public:
    Reader(QJsonObject object, QString context, QString *error)
        : m_object(std::move(object)), m_context(std::move(context)), m_error(error) {}
    bool number(const char *key, qreal &value) {
        const auto json = take(key);
        if (!json.isDouble() || !std::isfinite(json.toDouble())) return invalid(key);
        value = json.toDouble();
        return true;
    }
    bool integer(const char *key, int &value) {
        qreal numberValue;
        if (!number(key, numberValue)) return false;
        if (numberValue < -2147483647.0 || numberValue > 2147483647.0 ||
            numberValue != std::floor(numberValue)) return invalid(key);
        value = int(numberValue);
        return true;
    }
    template <typename Enum> bool enumeration(const char *key, Enum &value) {
        int numberValue;
        if (!integer(key, numberValue)) return false;
        value = Enum(numberValue);
        return true;
    }
    bool boolean(const char *key, bool &value) {
        const auto json = take(key);
        if (!json.isBool()) return invalid(key);
        value = json.toBool();
        return true;
    }
    bool unsignedInteger(const char *key, quint32 &value) {
        qreal numberValue;
        if (!number(key, numberValue)) return false;
        if (numberValue < 0 || numberValue > 4294967295.0 || numberValue != std::floor(numberValue))
            return invalid(key);
        value = quint32(numberValue);
        return true;
    }
    bool response(const char *key, std::array<qreal, 5> &value) {
        const auto json = take(key);
        if (!json.isArray() || json.toArray().size() != 5) return invalid(key);
        const auto array = json.toArray();
        for (int i = 0; i < 5; ++i) {
            if (!array[i].isDouble() || !inRange(array[i].toDouble(), 0, 1)) return invalid(key);
            value[i] = array[i].toDouble();
        }
        return true;
    }
    bool string(const char *key, QString &value) {
        const auto json = take(key);
        if (!json.isString()) return invalid(key);
        value = json.toString();
        return true;
    }
    bool object(const char *key, QJsonObject &value) {
        const auto json = take(key);
        if (!json.isObject()) return invalid(key);
        value = json.toObject();
        return true;
    }
    bool array(const char *key, QJsonArray &value) {
        const auto json = take(key);
        if (!json.isArray()) return invalid(key);
        value = json.toArray(); return true;
    }
    bool finish() {
        for (const auto &key : m_object.keys())
            if (!m_seen.contains(key))
                return fail(m_error, QString("Unknown brush setting: %1.%2.").arg(m_context, key));
        return true;
    }
private:
    QJsonValue take(const char *key) {
        m_seen.insert(QString::fromLatin1(key));
        return m_object.value(QLatin1String(key));
    }
    bool invalid(const char *key) {
        return fail(m_error, QString("Missing or invalid brush setting: %1.%2.")
                    .arg(m_context, QString::fromLatin1(key)));
    }
    QJsonObject m_object;
    QString m_context;
    QString *m_error;
    QSet<QString> m_seen;
};
bool readDynamic(Reader &parent, const char *key, BrushDynamic &value, QString *error) {
    QJsonObject object;
    if (!parent.object(key, object)) return false;
    Reader reader(object, QString::fromLatin1(key), error);
    value.response = BrushDynamic{}.response;
    value.jitterPerStroke = false;
    if (object.contains("response") && !reader.response("response", value.response)) return false;
    if (object.contains("jitterPerStroke") && !reader.boolean("jitterPerStroke", value.jitterPerStroke)) return false;
    return reader.number("jitter", value.jitter) && reader.number("minimum", value.minimum) &&
           reader.enumeration("control", value.control) && reader.integer("fade", value.fade) &&
           reader.finish();
}
} // namespace

bool usesModernBrushFeatures(const BrushSettings &b) {
    return b.connected != BrushConnectedSettings{} || b.surface != BrushSurfacePattern{}
        || b.clusterSpray != BrushClusterSpray{} || b.speedDynamics != BrushSpeedDynamics{}
        || b.colorDynamics.tintAmount != 0;
}
bool usesExtendedBrushDynamics(const BrushSettings &b) {
    if (b.lockRandomSeed || b.randomSeed != 1 || b.scatterControl == BrushControl::Rotation ||
        b.countControl == BrushControl::Rotation || b.colorDynamics.control == BrushControl::Rotation) return true;
    for (const auto *d : {&b.sizeDynamics, &b.angleDynamics, &b.roundnessDynamics,
                           &b.texture.dynamics, &b.opacityDynamics, &b.flowDynamics})
        if (d->control == BrushControl::Rotation || d->jitterPerStroke ||
            d->response != BrushDynamic{}.response) return true;
    return false;
}
bool usesAdvancedBrushTips(const BrushSettings &b) { return !b.tipFrames.isEmpty() || b.size > 256; }
bool validateBrushTipImages(const BrushSettings &b, QString *error) {
    if (b.surface.kind == BrushSurfaceKind::Image && b.surface.image.isNull())
        return fail(error, "This surface brush is missing its pattern image.");
    return b.tipFrames.isEmpty() || !b.tipMask.isNull() || !b.colorTip.isNull()
        || fail(error, "This brush is missing its tip images.");
}

bool validateBrushSettings(const BrushSettings &b, QString *error) {
    if (error) error->clear();
    if (!enumIn(b.connected.mode, BrushConnectedMode::Repeat) || !inRange(b.connected.repeatLength, .1, 16) || !inRange(b.connected.phaseOffset, 0, 10000000))
        return fail(error, "The connected brush has invalid repeat settings.");
    if (!validateBrushSurface(b.surface, error) || !validateAdvancedBrushDynamics(b.clusterSpray, b.speedDynamics, error)) return false;
    if (!b.colorTip.isNull()) {
        if (b.colorTip.format() != QImage::Format_ARGB32_Premultiplied ||
            b.colorTip.width() > 2048 || b.colorTip.height() > 2048)
            return fail(error, "Colored stamps must be premultiplied RGBA images no larger than 2048 × 2048.");
        // Resource identity changes on pixel edits; do not scan a large stamp on each pen event.
        static thread_local QCache<qint64, int> validatedTips(256);
        const auto cached = validatedTips.object(b.colorTip.cacheKey());
        int status = cached ? *cached : 0; // 0 empty, 1 valid, 2 malformed premultiplication.
        if (!cached) {
            for (int y = 0; y < b.colorTip.height() && status != 2; ++y) {
                const auto *row = reinterpret_cast<const QRgb *>(b.colorTip.constScanLine(y));
                for (int x = 0; x < b.colorTip.width(); ++x) {
                    const int alpha = qAlpha(row[x]);
                    if (qRed(row[x]) > alpha || qGreen(row[x]) > alpha || qBlue(row[x]) > alpha) { status = 2; break; }
                    if (alpha) status = 1;
                }
            }
            validatedTips.insert(b.colorTip.cacheKey(), new int(status));
        }
        if (!status) return fail(error, "The colored stamp is completely transparent.");
        if (status == 2) return fail(error, "The colored stamp contains invalid premultiplied color pixels.");
    }
    if (b.tipFrames.size() > 16 || !enumIn(b.tipOrder, BrushTipOrder::Direction))
        return fail(error, "A brush tip set supports up to 16 tips and a valid selection mode.");
    const auto &atlas = b.colorTip.isNull() ? b.tipMask : b.colorTip;
    for (const auto &frame : b.tipFrames) {
        if (frame.rect.isEmpty() || frame.rect.x() < 0 || frame.rect.y() < 0 ||
            frame.rect.right() >= 2048 || frame.rect.bottom() >= 2048 ||
            (!atlas.isNull() && !atlas.rect().contains(frame.rect)) ||
            !inRange(frame.anchor.x(), 0, 1) || !inRange(frame.anchor.y(), 0, 1) || !inRange(frame.weight, .01, 100))
            return fail(error, "A brush tip has an invalid crop, root or weight.");
    }
    if (!inRange(b.size, 1, maxBrushSize) || !inRange(b.opacity, 0, 1) ||
        !inRange(b.strokeOpacity, 0, 1) || !inRange(b.spacing, .01, 4) ||
        !inRange(b.hardness, 0, 1) || !enumIn(b.blendMode, BrushBlend::Dissolve) ||
        !inRange(b.angle, -360, 360) || !inRange(b.roundness, .01, 1))
        return fail(error, "The brush has invalid tip or general settings.");
    if (!validDynamic(b.sizeDynamics) || !validDynamic(b.angleDynamics) ||
        !validDynamic(b.roundnessDynamics) || !inRange(b.tiltScale, 0, 4))
        return fail(error, "The brush has invalid shape dynamics.");
    if (!inRange(b.scatter, 0, 10) || !enumIn(b.scatterControl, BrushControl::Rotation) ||
        !inRange(b.scatterFade, 1, 10000) || !inRange(b.count, 1, 16) ||
        !inRange(b.countJitter, 0, 1) || !enumIn(b.countControl, BrushControl::Rotation) ||
        !inRange(b.countFade, 1, 10000))
        return fail(error, "The brush has invalid scattering settings.");
    const auto &t = b.texture;
    if (t.name.size() > 120 || !inRange(t.scale, .1, 10) ||
        !inRange(t.brightness, -1, 1) || !inRange(t.contrast, -1, 1) ||
        !enumIn(t.mode, MaskBlend::Difference) || !inRange(t.depth, 0, 1) ||
        !validDynamic(t.dynamics))
        return fail(error, "The brush has invalid texture settings.");
    const auto &d = b.dual;
    if (d.name.size() > 120 || !enumIn(d.mode, MaskBlend::Difference) ||
        !inRange(d.size, 1, 256) || !inRange(d.spacing, .01, 4) ||
        !inRange(d.scatter, 0, 10) || !inRange(d.count, 1, 16))
        return fail(error, "The brush has invalid dual-tip settings.");
    const auto &c = b.colorDynamics;
    if (!inRange(c.tintAmount, 0, 1) || !inRange(c.foregroundBackground, 0, 1) || !enumIn(c.control, BrushControl::Rotation) ||
        !inRange(c.fade, 1, 10000) || !inRange(c.hue, 0, 1) ||
        !inRange(c.saturation, 0, 1) || !inRange(c.brightness, 0, 1) || !inRange(c.purity, -1, 1))
        return fail(error, "The brush has invalid color dynamics.");
    if (!validDynamic(b.opacityDynamics) || !validDynamic(b.flowDynamics))
        return fail(error, "The brush has invalid transfer dynamics.");
    const auto &p = b.pose;
    if (!inRange(p.tiltX, -90, 90) || !inRange(p.tiltY, -90, 90) ||
        !inRange(p.rotation, -360, 360) || !inRange(p.pressure, 0, 1) ||
        !inRange(b.airbrushRate, 1, 60))
        return fail(error, "The brush has invalid pose or build-up settings.");
    const auto &s = b.symmetry;
    if (!enumIn(s.mode, SymmetryMode::Radial) || !inRange(s.center.x(), 0, 1) ||
        !inRange(s.center.y(), 0, 1) || !inRange(s.angle, -360, 360) ||
        !inRange(s.count, 2, 12) || !inRange(s.radius, 1, 8192) ||
        !inRange(s.distance, 1, 8192) || !inRange(s.amplitude, 0, 8192) ||
        !inRange(s.wavelength, 1, 8192))
        return fail(error, "The brush has invalid symmetry settings.");
    return true;
}

QJsonObject brushSettingsToJson(const BrushSettings &b) {
    const auto &t = b.texture;
    const auto &d = b.dual;
    const auto &c = b.colorDynamics;
    const auto &p = b.pose;
    const auto &s = b.symmetry;
    QJsonObject result{{"size", b.size}, {"flow", b.opacity}, {"strokeOpacity", b.strokeOpacity},
        {"pressureSize", b.pressureSize}, {"spacing", b.spacing}, {"hardness", b.hardness},
        {"blendMode", int(b.blendMode)}, {"angle", b.angle}, {"roundness", b.roundness},
        {"flipX", b.flipX}, {"flipY", b.flipY}, {"sizeDynamics", dynamicJson(b.sizeDynamics)},
        {"angleDynamics", dynamicJson(b.angleDynamics)},
        {"roundnessDynamics", dynamicJson(b.roundnessDynamics)}, {"tiltScale", b.tiltScale},
        {"projection", b.projection}, {"scatter", b.scatter}, {"scatterBothAxes", b.scatterBothAxes},
        {"scatterControl", int(b.scatterControl)}, {"scatterFade", b.scatterFade},
        {"count", b.count}, {"countJitter", b.countJitter}, {"countControl", int(b.countControl)},
        {"countFade", b.countFade},
        {"texture", QJsonObject{{"enabled", t.enabled}, {"name", t.name}, {"invert", t.invert},
            {"scale", t.scale}, {"brightness", t.brightness}, {"contrast", t.contrast},
            {"eachTip", t.eachTip}, {"mode", int(t.mode)}, {"depth", t.depth},
            {"dynamics", dynamicJson(t.dynamics)}}},
        {"dual", QJsonObject{{"enabled", d.enabled}, {"name", d.name}, {"mode", int(d.mode)},
            {"size", d.size}, {"spacing", d.spacing}, {"scatter", d.scatter}, {"count", d.count}}},
        {"colorDynamics", QJsonObject{{"enabled", c.enabled}, {"perTip", c.perTip},
            {"foregroundBackground", c.foregroundBackground}, {"control", int(c.control)},
            {"fade", c.fade}, {"hue", c.hue}, {"saturation", c.saturation},
            {"brightness", c.brightness}, {"purity", c.purity}}},
        {"opacityDynamics", dynamicJson(b.opacityDynamics)}, {"flowDynamics", dynamicJson(b.flowDynamics)},
        {"pose", QJsonObject{{"overrideTiltX", p.overrideTiltX}, {"overrideTiltY", p.overrideTiltY},
            {"overrideRotation", p.overrideRotation}, {"overridePressure", p.overridePressure},
            {"tiltX", p.tiltX}, {"tiltY", p.tiltY}, {"rotation", p.rotation}, {"pressure", p.pressure}}},
        {"noise", b.noise}, {"wetEdges", b.wetEdges}, {"airbrush", b.airbrush},
        {"airbrushRate", b.airbrushRate}, {"smoothingEnabled", b.smoothingEnabled},
        {"protectTexture", b.protectTexture},
        {"symmetry", QJsonObject{{"mode", int(s.mode)}, {"centerX", s.center.x()}, {"centerY", s.center.y()},
            {"angle", s.angle}, {"count", s.count}, {"radius", s.radius}, {"distance", s.distance},
            {"amplitude", s.amplitude}, {"wavelength", s.wavelength}}}};
    if (b.lockRandomSeed) result["lockRandomSeed"] = true;
    if (b.connected != BrushConnectedSettings{}) result["connected"] = QJsonObject{{"mode", int(b.connected.mode)}, {"repeatLength", b.connected.repeatLength}, {"flipAlternate", b.connected.flipAlternate}, {"phaseOffset", b.connected.phaseOffset}};
    if (b.surface != BrushSurfacePattern{}) result["surface"] = brushSurfaceToJson(b.surface);
    if (b.clusterSpray != BrushClusterSpray{}) result["clusterSpray"] = clusterSprayToJson(b.clusterSpray);
    if (b.speedDynamics != BrushSpeedDynamics{}) result["speedDynamics"] = speedDynamicsToJson(b.speedDynamics);
    if (c.tintAmount != 0) { auto color = result["colorDynamics"].toObject(); color["tintAmount"] = c.tintAmount; result["colorDynamics"] = color; }
    if (c.applyToColorTip) {
        auto color = result["colorDynamics"].toObject();
        color["applyToColorTip"] = true; result["colorDynamics"] = color;
    }
    if (b.randomSeed != 1) result["randomSeed"] = double(b.randomSeed);
    if (!b.tipFrames.isEmpty()) {
        QJsonArray frames;
        for (const auto &f : b.tipFrames) frames.append(QJsonArray{f.rect.x(), f.rect.y(), f.rect.width(), f.rect.height(), f.anchor.x(), f.anchor.y(), f.weight});
        result["tipSet"] = QJsonObject{{"order", int(b.tipOrder)}, {"frames", frames}};
    }
    return result;
}

bool brushSettingsFromJson(const QJsonObject &object, BrushSettings &settings, QString *error) {
    if (error) error->clear();
    // Palette colors and decoded image resources belong to their caller. On any
    // error leave the entire input untouched, including these external values.
    auto b = settings;
    Reader r(object, "settings", error);
    b.connected = {}; b.clusterSpray = {}; b.speedDynamics = {};
    const QImage surfaceImage = b.surface.image; b.surface = {}; b.surface.image = surfaceImage;
    if (object.contains("connected")) {
        QJsonObject child; if (!r.object("connected", child)) return false;
        Reader connected(child, "connected", error);
        if (child.contains("phaseOffset") && !connected.number("phaseOffset", b.connected.phaseOffset)) return false;
        if (!connected.enumeration("mode", b.connected.mode) || !connected.number("repeatLength", b.connected.repeatLength)
            || !connected.boolean("flipAlternate", b.connected.flipAlternate) || !connected.finish()) return false;
    }
    if (object.contains("surface")) { QJsonObject child; if (!r.object("surface", child) || !brushSurfaceFromJson(child, b.surface, error)) return false; }
    if (object.contains("clusterSpray")) { QJsonObject child; if (!r.object("clusterSpray", child) || !clusterSprayFromJson(child, b.clusterSpray, error)) return false; }
    if (object.contains("speedDynamics")) { QJsonObject child; if (!r.object("speedDynamics", child) || !speedDynamicsFromJson(child, b.speedDynamics, error)) return false; }
    b.lockRandomSeed = false;
    b.randomSeed = 1;
    b.tipFrames.clear(); b.tipOrder = BrushTipOrder::Shuffle;
    if (object.contains("tipSet")) {
        QJsonObject set;
        if (!r.object("tipSet", set)) return false;
        Reader reader(set, "tipSet", error); QJsonArray frames;
        if (!reader.enumeration("order", b.tipOrder) || !reader.array("frames", frames) || !reader.finish() || frames.isEmpty() || frames.size() > 16)
            return fail(error, "Invalid brush tip set.");
        for (const auto &value : frames) {
            const auto a = value.toArray();
            if (!value.isArray() || a.size() != 7) return fail(error, "Invalid brush tip frame.");
            for (int i = 0; i < 7; ++i) if (!a[i].isDouble() || !std::isfinite(a[i].toDouble()) ||
                (i < 4 && (!inRange(a[i].toDouble(), 0, 2048) || a[i].toDouble() != std::floor(a[i].toDouble()))))
                return fail(error, "Invalid brush tip frame coordinates.");
            b.tipFrames.append({QRect(a[0].toInt(), a[1].toInt(), a[2].toInt(), a[3].toInt()), {a[4].toDouble(), a[5].toDouble()}, a[6].toDouble()});
        }
    }
    if (object.contains("lockRandomSeed") && !r.boolean("lockRandomSeed", b.lockRandomSeed)) return false;
    if (object.contains("randomSeed") && !r.unsignedInteger("randomSeed", b.randomSeed)) return false;
    if (!(r.number("size", b.size) && r.number("flow", b.opacity) &&
        r.number("strokeOpacity", b.strokeOpacity) && r.boolean("pressureSize", b.pressureSize) &&
        r.number("spacing", b.spacing) && r.number("hardness", b.hardness) &&
        r.enumeration("blendMode", b.blendMode) && r.number("angle", b.angle) &&
        r.number("roundness", b.roundness) && r.boolean("flipX", b.flipX) && r.boolean("flipY", b.flipY) &&
        readDynamic(r, "sizeDynamics", b.sizeDynamics, error) &&
        readDynamic(r, "angleDynamics", b.angleDynamics, error) &&
        readDynamic(r, "roundnessDynamics", b.roundnessDynamics, error) &&
        r.number("tiltScale", b.tiltScale) && r.boolean("projection", b.projection) &&
        r.number("scatter", b.scatter) && r.boolean("scatterBothAxes", b.scatterBothAxes) &&
        r.enumeration("scatterControl", b.scatterControl) && r.integer("scatterFade", b.scatterFade) &&
        r.integer("count", b.count) && r.number("countJitter", b.countJitter) &&
        r.enumeration("countControl", b.countControl) && r.integer("countFade", b.countFade))) return false;
    QJsonObject child;
    if (!r.object("texture", child)) return false;
    Reader texture(child, "texture", error);
    auto &t = b.texture;
    if (!(texture.boolean("enabled", t.enabled) && texture.string("name", t.name) &&
        texture.boolean("invert", t.invert) && texture.number("scale", t.scale) &&
        texture.number("brightness", t.brightness) && texture.number("contrast", t.contrast) &&
        texture.boolean("eachTip", t.eachTip) && texture.enumeration("mode", t.mode) &&
        texture.number("depth", t.depth) && readDynamic(texture, "dynamics", t.dynamics, error) &&
        texture.finish())) return false;
    if (!r.object("dual", child)) return false;
    Reader dual(child, "dual", error);
    auto &d = b.dual;
    if (!(dual.boolean("enabled", d.enabled) && dual.string("name", d.name) && dual.enumeration("mode", d.mode) &&
        dual.number("size", d.size) && dual.number("spacing", d.spacing) && dual.number("scatter", d.scatter) &&
        dual.integer("count", d.count) && dual.finish())) return false;
    if (!r.object("colorDynamics", child)) return false;
    Reader color(child, "colorDynamics", error);
    auto &c = b.colorDynamics;
    c.applyToColorTip = false; c.tintAmount = 0;
    if (child.contains("tintAmount") && !color.number("tintAmount", c.tintAmount)) return false;
    if (child.contains("applyToColorTip") && !color.boolean("applyToColorTip", c.applyToColorTip)) return false;
    if (!(color.boolean("enabled", c.enabled) && color.boolean("perTip", c.perTip) &&
        color.number("foregroundBackground", c.foregroundBackground) && color.enumeration("control", c.control) &&
        color.integer("fade", c.fade) && color.number("hue", c.hue) && color.number("saturation", c.saturation) &&
        color.number("brightness", c.brightness) && color.number("purity", c.purity) && color.finish())) return false;
    if (!readDynamic(r, "opacityDynamics", b.opacityDynamics, error) ||
        !readDynamic(r, "flowDynamics", b.flowDynamics, error) || !r.object("pose", child)) return false;
    Reader pose(child, "pose", error);
    auto &p = b.pose;
    if (!(pose.boolean("overrideTiltX", p.overrideTiltX) && pose.boolean("overrideTiltY", p.overrideTiltY) &&
        pose.boolean("overrideRotation", p.overrideRotation) && pose.boolean("overridePressure", p.overridePressure) &&
        pose.number("tiltX", p.tiltX) && pose.number("tiltY", p.tiltY) && pose.number("rotation", p.rotation) &&
        pose.number("pressure", p.pressure) && pose.finish())) return false;
    if (!(r.boolean("noise", b.noise) && r.boolean("wetEdges", b.wetEdges) &&
        r.boolean("airbrush", b.airbrush) && r.number("airbrushRate", b.airbrushRate) &&
        r.boolean("smoothingEnabled", b.smoothingEnabled) && r.boolean("protectTexture", b.protectTexture) &&
        r.object("symmetry", child))) return false;
    Reader symmetry(child, "symmetry", error);
    auto &s = b.symmetry;
    qreal centerX, centerY;
    if (!(symmetry.enumeration("mode", s.mode) && symmetry.number("centerX", centerX) &&
        symmetry.number("centerY", centerY) && symmetry.number("angle", s.angle) && symmetry.integer("count", s.count) &&
        symmetry.number("radius", s.radius) && symmetry.number("distance", s.distance) &&
        symmetry.number("amplitude", s.amplitude) && symmetry.number("wavelength", s.wavelength) &&
        symmetry.finish() && r.finish())) return false;
    s.center = {centerX, centerY};
    if (!validateBrushSettings(b, error)) return false;
    settings = std::move(b);
    return true;
}
} // namespace toon
