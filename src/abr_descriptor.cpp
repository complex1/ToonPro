#include "abr_descriptor.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>

namespace toon {
namespace {
// Independently implemented from Adobe's action-descriptor specification.
// Binary layout is separate from the deliberately limited brush-field mapping.
class DescriptorReader {
public:
    DescriptorReader(QByteArrayView bytes, AbrDescriptorResult &result) : m_bytes(bytes), m_result(result) {}
    bool ok() const { return m_result.error.isEmpty() && !m_result.unsupported; }
    qsizetype remaining() const { return m_bytes.size() - m_position; }
    void fail(const QString &message) { if (ok()) m_result.error = message; }
    QByteArrayView take(quint64 count) {
        if (!ok()) return {};
        if (count > quint64(remaining())) { fail("The ABR preset descriptor is truncated."); return {}; }
        const auto result = m_bytes.sliced(m_position, qsizetype(count)); m_position += qsizetype(count); return result;
    }
    quint32 u32() { const auto bytes = take(4); return bytes.size() == 4 ? qFromBigEndian<quint32>(bytes.data()) : 0; }
    QString text() {
        const auto length = u32();
        if (length > 4096) { fail("An ABR descriptor string exceeds 4096 characters."); return {}; }
        const auto bytes = take(quint64(length) * 2); if (!ok()) return {};
        QString value; value.reserve(int(length));
        for (quint32 i = 0; i < length; ++i) value.append(QChar(qFromBigEndian<quint16>(bytes.data() + i * 2)));
        while (value.endsWith(QChar(0))) value.chop(1);
        value.replace(QChar(0), QChar(' ')); return value;
    }
    QString key() {
        const auto length = u32();
        if (length > 256) { fail("An ABR descriptor key exceeds 256 bytes."); return {}; }
        const auto value = take(length ? length : 4); return QString::fromLatin1(value.data(), value.size());
    }
    QJsonObject descriptor(int depth) {
        if (depth > 20) { fail("The ABR descriptor nesting limit was exceeded."); return {}; }
        const auto name = text(), classId = key(); const auto count = u32();
        if (count > 4096) { fail("An ABR descriptor contains too many fields."); return {}; }
        QJsonObject result{{"_name", name}, {"_class", classId}};
        for (quint32 i = 0; i < count && ok(); ++i) {
            const auto field = key(); const auto kind = take(4);
            if (result.contains(field)) { fail("An ABR descriptor contains duplicate fields."); break; }
            result.insert(field, value(kind, depth + 1));
        }
        return result;
    }
    QJsonValue value(QByteArrayView kind, int depth) {
        if (!ok()) return {};
        if (++m_values > 32768 || depth > 20) { fail("The ABR descriptor complexity limit was exceeded."); return {}; }
        if (kind == "Objc" || kind == "GlbO") return descriptor(depth);
        if (kind == "VlLs") {
            const auto count = u32(); if (count > 32768) { fail("The ABR descriptor list is too large."); return {}; }
            QJsonArray result;
            for (quint32 i = 0; i < count && ok(); ++i) { const auto type = take(4); result.append(value(type, depth + 1)); }
            return result;
        }
        if (kind == "TEXT") return text();
        if (kind == "bool") {
            const auto bytes = take(1); if (!ok()) return {};
            const auto n = quint8(bytes[0]); if (n > 1) fail("Invalid ABR descriptor boolean."); return n != 0;
        }
        if (kind == "long") return double(qint32(u32()));
        if (kind == "doub" || kind == "UntF") {
            QString unit; if (kind == "UntF") { const auto bytes = take(4); unit = QString::fromLatin1(bytes.data(), bytes.size()); }
            const auto bytes = take(8); if (!ok()) return {};
            const auto bits = qFromBigEndian<quint64>(bytes.data()); double number; std::memcpy(&number, &bits, sizeof(number));
            if (!std::isfinite(number)) { fail("Invalid ABR descriptor number."); return {}; }
            return unit.isEmpty() ? QJsonValue(number) : QJsonValue(QJsonObject{{"unit", unit}, {"value", number}});
        }
        if (kind == "comp") {
            const auto bytes = take(8); if (!ok()) return {};
            return double(qFromBigEndian<qint64>(bytes.data()));
        }
        if (kind == "enum") { const auto type = key(); const auto selection = key(); return QJsonObject{{"type", type}, {"enum", selection}}; }
        if (kind == "type" || kind == "GlbC") { const auto name = text(); const auto classId = key(); return QJsonObject{{"_name", name}, {"_class", classId}}; }
        if (kind == "alis" || kind == "tdta") { const auto length = u32(); take(length); return QJsonObject{{"skippedData", true}}; }
        // Unknown OSType payloads have no universal byte length: never guess
        // their size or apply partially decoded settings to unrelated samples.
        m_result.unsupported = true;
        m_result.warnings.append(QString("Preset descriptors use unsupported type '%1'; their settings were not transferred.")
            .arg(QString::fromLatin1(kind.data(), kind.size())));
        return {};
    }
private:
    QByteArrayView m_bytes;
    AbrDescriptorResult &m_result;
    qsizetype m_position = 0;
    int m_values = 0;
};

struct Mapper {
    AbrDescriptorPreset result;
    QStringList mapped, skipped;
    void skip(const QString &label) { if (!skipped.contains(label)) skipped.append(label); }
    void note(const QString &label) { if (!mapped.contains(label)) mapped.append(label); }
    bool flag(const QJsonObject &object, const QString &key, bool fallback = false) {
        if (!object.contains(key)) return fallback;
        if (!object.value(key).isBool()) { skip(key + " (invalid boolean)"); return fallback; }
        return object.value(key).toBool();
    }
    std::optional<double> number(const QJsonObject &object, const QString &key, const QString &unit = {}) {
        if (!object.contains(key)) return {};
        const auto value = object.value(key);
        if (value.isDouble()) return value.toDouble();
        if (value.isObject() && !unit.isEmpty() && value.toObject().value("unit") == unit && value.toObject().value("value").isDouble())
            return value.toObject().value("value").toDouble();
        skip(key + " (unsupported value or units)"); return {};
    }
    void scalar(const QJsonObject &object, const QString &key, const QString &unit, qreal &destination,
                double minimum, double maximum, double scale, const QString &label) {
        const auto source = number(object, key, unit); if (!source) return;
        const double value = *source * scale;
        destination = std::clamp(value, minimum, maximum); note(label);
        if (destination != value) result.compatibility.append(QString("%1 limited to Toon Pro's supported range (%2–%3).")
            .arg(label).arg(minimum).arg(maximum));
    }
    BrushControl control(int value, const QString &label) {
        switch (value) {
        case 0: return BrushControl::Off;
        case 1: return BrushControl::Fade;
        case 2: return BrushControl::Pressure;
        case 3: return BrushControl::Tilt;
        case 4: return BrushControl::Wheel;
        case 5: return BrushControl::InitialDirection;
        case 6: return BrushControl::Direction;
        case 8: return BrushControl::Rotation;
        default: skip(label + " control (including initial rotation)"); return BrushControl::Off;
        }
    }
    void dynamics(const QJsonObject &preset, const QString &key, BrushDynamic &destination, const QString &label,
                  bool supportsMinimum = true) {
        if (!preset.contains(key)) return;
        if (!preset.value(key).isObject()) { skip(label + " (invalid descriptor)"); return; }
        const auto object = preset.value(key).toObject();
        scalar(object, "jitter", "#Prc", destination.jitter, 0, 1, .01, label + " jitter");
        if (supportsMinimum) scalar(object, "Mnm ", "#Prc", destination.minimum, 0, 1, .01, label + " minimum");
        else if (const auto minimum = number(object, "Mnm ", "#Prc"); minimum && *minimum != 0)
            skip(label + " minimum (not supported by the native engine)");
        if (const auto selector = number(object, "bVTy")) {
            if (*selector >= 0 && *selector <= 8 && std::floor(*selector) == *selector) {
                destination.control = control(int(*selector), label);
                if (*selector != 7) note(label + " control");
            } else skip(label + " control");
        }
        if (const auto steps = number(object, "fStp")) {
            destination.fade = int(std::clamp(std::round(*steps), 1., 10000.));
            if (*steps < 1 || *steps > 10000) result.compatibility.append(label + " fade steps limited to 1–10000.");
        }
    }
    AbrDescriptorPreset map(const QJsonObject &preset) {
        const auto shape = preset.value("Brsh").toObject();
        result.name = preset.value("Nm  ").toString().trimmed();
        if (result.name.isEmpty()) result.name = shape.value("Nm  ").toString().trimmed();
        if (result.name.isEmpty()) result.name = "Imported brush";
        result.name = result.name.left(120);
        result.sampleId = shape.value("sampledData").toString();
        result.computed = shape.value("_class") == "computedBrush";
        auto &b = result.settings;
        b.pressureSize = false; b.spacing = .1;
        scalar(shape, "Dmtr", "#Pxl", b.size, 1, 256, 1, "Diameter");
        scalar(shape, "Angl", "#Ang", b.angle, -360, 360, 1, "Tip angle");
        scalar(shape, "Rndn", "#Prc", b.roundness, .01, 1, .01, "Roundness");
        if (result.computed) scalar(shape, "Hrdn", "#Prc", b.hardness, 0, 1, .01, "Hardness");
        scalar(shape, "Spcn", "#Prc", b.spacing, .01, 4, .01, "Spacing");
        if (preset.contains("Spcn")) scalar(preset, "Spcn", "#Prc", b.spacing, .01, 4, .01, "Spacing");
        if (shape.contains("Intr") && !flag(shape, "Intr", true)) skip("Spacing-disabled behavior (native spacing remains active)");
        b.flipX = flag(shape, "flipX"); b.flipY = flag(shape, "flipY");
        if (shape.contains("flipX") || shape.contains("flipY")) note("Tip flips");
        if (flag(preset, "useTipDynamics")) {
            dynamics(preset, "szVr", b.sizeDynamics, "Size");
            dynamics(preset, "angleDynamics", b.angleDynamics, "Angle");
            dynamics(preset, "roundnessDynamics", b.roundnessDynamics, "Roundness");
            scalar(preset, "minimumDiameter", "#Prc", b.sizeDynamics.minimum, 0, 1, .01, "Minimum diameter");
            scalar(preset, "minimumRoundness", "#Prc", b.roundnessDynamics.minimum, .01, 1, .01, "Minimum roundness");
            scalar(preset, "tiltScale", "#Prc", b.tiltScale, 0, 4, .01, "Tilt scale");
            b.projection = flag(preset, "brushProjection");
            if (flag(preset, "flipX") || flag(preset, "flipY")) skip("Random tip flips");
        }
        if (flag(preset, "usePaintDynamics")) {
            dynamics(preset, "opVr", b.opacityDynamics, "Opacity");
            dynamics(preset, "prVr", b.flowDynamics, "Flow");
            if (preset.contains("wtVr") || preset.contains("mxVr")) skip("Wetness and mixing dynamics");
        }
        if (flag(preset, "useScatter")) {
            const auto scatter = preset.value("scatterDynamics").toObject();
            scalar(scatter, "jitter", "#Prc", b.scatter, 0, 10, .01, "Scatter amount");
            if (preset.contains("scatterDynamics") && !preset.value("scatterDynamics").isObject()) skip("Scatter (invalid descriptor)");
            auto controlOnly = scatter; controlOnly.remove("jitter");
            BrushDynamic scatterDynamics; dynamics(QJsonObject{{"scatterDynamics", controlOnly}}, "scatterDynamics", scatterDynamics, "Scatter", false);
            b.scatterControl = scatterDynamics.control; b.scatterFade = scatterDynamics.fade;
            b.scatterBothAxes = flag(preset, "bothAxes");
            if (const auto count = number(preset, "Cnt ")) {
                b.count = int(std::clamp(std::round(*count), 1., 16.)); note("Scatter count");
                if (*count != b.count) result.compatibility.append("Scatter count limited to 1–16 whole stamps.");
            }
            BrushDynamic count; dynamics(preset, "countDynamics", count, "Count", false);
            b.countJitter = count.jitter; b.countControl = count.control; b.countFade = count.fade;
        }
        const auto tool = preset.value("toolOptions").toObject();
        if (!tool.isEmpty()) {
            const auto type = tool.value("_class").toString();
            if (type == "PbTl" || type == "paintbrushTool" || type == "_") {
                scalar(tool, "flow", "#Prc", b.opacity, 0, 1, .01, "Flow");
                scalar(tool, "Opct", "#Prc", b.strokeOpacity, 0, 1, .01, "Opacity");
                skip("Photoshop smoothing, blend modes and tool overrides");
            } else skip("Non-paintbrush tool preset");
        }
        if (flag(preset, "useTexture")) skip("Pattern textures");
        if (flag(preset.value("dualBrush").toObject(), "useDualBrush")) skip("Dual brush");
        if (flag(preset, "useColorDynamics")) skip("Color dynamics");
        if (flag(preset, "useBrushPose")) skip("Brush pose overrides");
        if (flag(preset, "Wtdg")) skip("Photoshop wet edges");
        if (flag(preset, "Nose")) skip("Photoshop noise");
        if (flag(preset, "Rpt ")) skip("Photoshop build-up timing");
        result.compatibility.prepend(result.computed ? "Computed round geometry mapped to Toon Pro's native round brush."
                                                    : "Sampled tip matched to its preset by stored sample identifier.");
        if (!mapped.isEmpty()) result.compatibility.append("Transferred: " + mapped.join(", ") + '.');
        if (!skipped.isEmpty()) result.compatibility.append("Not transferred: " + skipped.join(", ") + '.');
        result.compatibility.append("Unspecified settings use Toon Pro defaults. Native dynamics do not guarantee identical Photoshop rendering.");
        return result;
    }
};
}
AbrDescriptorResult readAbrDescriptors(QByteArrayView bytes) {
    AbrDescriptorResult result;
    DescriptorReader reader(bytes, result);
    if (reader.u32() != 16) {
        if (result.error.isEmpty()) { result.unsupported = true; result.warnings.append("Unsupported preset descriptor version; sampled tips use native defaults."); }
        return result;
    }
    // Unknown descriptor versions are opaque resources: skip them without
    // parsing, while keeping the existing whole-file/section bounds. Only a
    // recognized layout enters this separate descriptor-processing budget.
    if (bytes.size() > 8 * 1024 * 1024) { result.error = "ABR preset descriptors exceed the 8 MiB limit."; return result; }
    const auto root = reader.descriptor(0);
    if (!reader.ok()) return result;
    if (reader.remaining()) { result.error = "Unexpected data follows the ABR preset descriptor."; return result; }
    if (!root.value("Brsh").isArray()) { result.unsupported = true; result.warnings.append("No recognized brush preset list; sampled tips use native defaults."); return result; }
    const auto presets = root.value("Brsh").toArray();
    if (presets.size() > 128) { result.error = "An ABR file may contain at most 128 brush presets."; return result; }
    for (const auto &value : presets) {
        const auto preset = value.toObject(), shape = preset.value("Brsh").toObject();
        const auto type = shape.value("_class").toString();
        if (type != "sampledBrush" && type != "computedBrush") {
            result.warnings.append(QString("Skipped preset '%1': unsupported %2 brush geometry.")
                .arg(preset.value("Nm  ").toString(), type.isEmpty() ? "unknown" : type)); continue;
        }
        Mapper mapper; auto converted = mapper.map(preset); QString error;
        if (!validateBrushSettings(converted.settings, &error)) { result.error = "Invalid mapped brush settings: " + error; result.presets.clear(); return result; }
        result.presets.append(std::move(converted));
    }
    return result;
}
}
