#include "abr_import.h"
#include "abr_descriptor.h"
#include <QByteArrayView>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QUuid>
#include <QtEndian>
#include <algorithm>
#include <cstring>

namespace toon {
namespace {
constexpr qint64 maxFileBytes = 256 * 1024 * 1024;
constexpr int maxBrushes = 128;
constexpr int maxSourceDimension = 8192;
// Decode one source tip at a time. Store a bounded, smoothly filtered copy at
// four times the engine's current maximum paint size, keeping large packs usable.
constexpr int maxImportedDimension = 1024;
constexpr qint64 maxSourcePixels = 256 * 1024 * 1024;
constexpr qint64 maxImportedPixels = 64 * 1024 * 1024;
struct PixelBudget { qint64 source = 0, imported = 0; };

// Each record gets its own bounded view; a size field can never seek into the
// next sample. Format references and fixture provenance are in tests/fixtures/abr.
class Reader {
public:
    Reader(QByteArrayView data, QString &error) : m_data(data), m_error(error) {}
    qsizetype remaining() const { return m_data.size() - m_position; }
    bool ok() const { return m_error.isEmpty(); }
    void fail(const QString &message) { if (ok()) m_error = message; }
    QByteArrayView take(quint64 count) {
        if (!ok()) return {};
        if (count > quint64(remaining())) {
            fail("The ABR file is truncated or contains an invalid record length.");
            return {};
        }
        auto bytes = m_data.sliced(m_position, qsizetype(count));
        m_position += qsizetype(count);
        return bytes;
    }
    quint8 u8() { const auto bytes = take(1); return bytes.size() == 1 ? quint8(bytes[0]) : 0; }
    quint16 u16() { const auto bytes = take(2); return bytes.size() == 2 ? qFromBigEndian<quint16>(bytes.data()) : 0; }
    quint32 u32() { const auto bytes = take(4); return bytes.size() == 4 ? qFromBigEndian<quint32>(bytes.data()) : 0; }
    qint64 i32() {
        const quint32 value = u32();
        return value <= 0x7fffffffU ? qint64(value) : qint64(value) - 0x100000000LL;
    }
    Reader record(quint64 count) { return Reader(take(count), m_error); }
private:
    QByteArrayView m_data;
    QString &m_error;
    qsizetype m_position = 0;
};

void warn(AbrImportResult &result, const QString &message) {
    if (!result.warnings.contains(message)) result.warnings.append(message);
}

QString sampleName(Reader &reader) {
    const auto count = reader.u32();
    if (count > 4096) {
        reader.fail("An ABR brush name exceeds the supported length.");
        return {};
    }
    QString name;
    name.reserve(int(count));
    for (quint32 i = 0; i < count && reader.ok(); ++i) name.append(QChar(reader.u16()));
    while (name.endsWith(QChar(0))) name.chop(1);
    name.replace(QChar(0), QChar(' '));
    return name.trimmed();
}

QImage decodeMask(Reader &reader, int width, int height, quint8 compression) {
    QImage mask(width, height, QImage::Format_Grayscale8);
    if (mask.isNull()) {
        reader.fail("There is not enough memory to import this brush tip.");
        return {};
    }
    mask.fill(0); // Initialize row padding as well as visible pixels.
    if (compression == 0) {
        const auto bytes = reader.take(quint64(width) * height);
        if (!reader.ok()) return {};
        for (int y = 0; y < height; ++y)
            std::memcpy(mask.scanLine(y), bytes.data() + qsizetype(y) * width, size_t(width));
        return mask;
    }

    QVector<quint16> rowLengths;
    rowLengths.reserve(height);
    for (int y = 0; y < height && reader.ok(); ++y) rowLengths.append(reader.u16());
    for (int y = 0; y < height && reader.ok(); ++y) {
        auto row = reader.record(rowLengths[y]);
        int produced = 0;
        while (row.remaining() && row.ok()) {
            const int control = row.u8();
            if (control == 128) continue;
            const int count = control < 128 ? control + 1 : 257 - control;
            if (count > width - produced) {
                row.fail("An ABR PackBits row expands beyond the brush width.");
                break;
            }
            if (control < 128) {
                const auto literal = row.take(quint64(count));
                if (row.ok()) std::memcpy(mask.scanLine(y) + produced, literal.data(), size_t(count));
            } else {
                const auto value = row.u8();
                if (row.ok()) std::memset(mask.scanLine(y) + produced, value, size_t(count));
            }
            produced += count;
        }
        if (produced != width) row.fail("An ABR PackBits row does not cover the brush width.");
    }
    return reader.ok() ? mask : QImage();
}

void parseSample(Reader &reader, AbrImportResult &result, const QString &name,
                 const QString &id, PixelBudget &pixels) {
    const qint64 top = reader.i32();
    const qint64 left = reader.i32();
    const qint64 bottom = reader.i32();
    const qint64 right = reader.i32();
    const quint16 depth = reader.u16();
    const quint8 compression = reader.u8();
    if (!reader.ok()) return;
    const qint64 width = right - left;
    const qint64 height = bottom - top;
    if (width < 1 || height < 1 || width > maxSourceDimension || height > maxSourceDimension) {
        reader.fail("ABR source brush tips must be between 1 and 8192 pixels on each side.");
        return;
    }
    if (depth != 8 || compression > 1) {
        warn(result, QString("Skipped sampled tips with unsupported depth or compression (%1-bit, method %2).")
                         .arg(depth).arg(compression));
        return;
    }
    if (width * height > maxSourcePixels - pixels.source) {
        reader.fail("This ABR file exceeds the 256-million source-pixel import limit.");
        return;
    }
    const QSize sourceSize{int(width), int(height)};
    QSize importedSize = sourceSize;
    if (width > maxImportedDimension || height > maxImportedDimension) {
        importedSize.scale(maxImportedDimension, maxImportedDimension, Qt::KeepAspectRatio);
        // Very thin sampled strokes must retain at least one row/column.
        importedSize = importedSize.expandedTo(QSize(1, 1));
    }
    const qint64 storedPixels = qint64(importedSize.width()) * importedSize.height();
    if (storedPixels > maxImportedPixels - pixels.imported) {
        reader.fail("This ABR file exceeds the 64-million imported-pixel limit.");
        return;
    }
    auto mask = decodeMask(reader, int(width), int(height), compression);
    if (reader.ok()) {
        pixels.source += width * height;
        if (importedSize != sourceSize) {
            mask = mask.scaled(importedSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                       .convertToFormat(QImage::Format_Grayscale8);
            if (mask.isNull()) {
                reader.fail("There is not enough memory to resize this brush tip.");
                return;
            }
            warn(result, "Large sampled tips were resized to at most 1024 pixels on the longest edge. Original ABR files are unchanged.");
        }
        bool hasCoverage = false;
        for (int y = 0; y < mask.height() && !hasCoverage; ++y) {
            const auto *row = mask.constScanLine(y);
            for (int x = 0; x < mask.width(); ++x) {
                if (row[x]) { hasCoverage = true; break; }
            }
        }
        if (!hasCoverage) {
            warn(result, "Fully transparent sampled tips were skipped.");
            return;
        }
        pixels.imported += storedPixels;
        AbrBrush brush; brush.name = name; brush.sourceId = id; brush.mask = mask; brush.sourceSize = sourceSize;
        result.brushes.append(std::move(brush));
    }
}

void parseLegacy(Reader &reader, AbrImportResult &result, int count,
                 const QString &baseName, const QString &digest) {
    if (count > maxBrushes) {
        reader.fail("An ABR file may contain at most 128 brush records.");
        return;
    }
    PixelBudget pixels;
    for (int i = 0; i < count && reader.ok(); ++i) {
        const auto type = reader.u16();
        const auto length = reader.u32();
        auto record = reader.record(length);
        if (!reader.ok()) return;
        if (type != 2) {
            warn(result, type == 1 ? "Computed Photoshop brush tips were skipped."
                                   : "Unknown Photoshop brush record types were skipped.");
            continue;
        }
        record.take(6); // Legacy miscellaneous and spacing fields.
        QString name = result.version == 2 ? sampleName(record) : QString();
        if (name.isEmpty()) name = QString("%1 %2").arg(baseName).arg(i + 1);
        record.take(9); // Antialias flag and legacy 16-bit bounds.
        parseSample(record, result, name, digest + ":" + QString::number(i + 1), pixels);
    }
    if (reader.ok() && reader.remaining()) reader.fail("Unexpected data follows the ABR brush records.");
}

void parseModern(Reader &reader, AbrImportResult &result,
                 const QString &baseName, const QString &digest) {
    PixelBudget pixels;
    int samples = 0;
    int sections = 0;
    bool foundSamples = false;
    QVector<AbrDescriptorPreset> presets;
    QSet<QString> seenSampleIds, ambiguousSampleIds;
    bool usableDescriptors = false, unsupportedDescriptors = false;
    qint64 descriptorBytes = 0;
    while (reader.remaining() && reader.ok()) {
        if (++sections > 1024) {
            reader.fail("The ABR file contains too many resource sections.");
            return;
        }
        const auto signature = reader.take(4);
        const auto key = reader.take(4);
        const auto length = reader.u32();
        auto section = reader.record(length);
        if (!reader.ok()) return;
        if (signature != QByteArrayView("8BIM", 4)) {
            reader.fail("The ABR resource section signature is invalid.");
            return;
        }
        if (key != QByteArrayView("samp", 4)) {
            if (key == QByteArrayView("desc", 4)) {
                const auto descriptorData = section.take(length);
                if (descriptorData.size() >= 4 && qFromBigEndian<quint32>(descriptorData.data()) == 16) {
                    descriptorBytes += length;
                    if (descriptorBytes > 8 * 1024 * 1024) { reader.fail("ABR preset descriptors exceed the 8 MiB limit."); return; }
                }
                auto descriptors = readAbrDescriptors(descriptorData);
                if (!descriptors.error.isEmpty()) { reader.fail(descriptors.error); return; }
                for (const auto &warning : descriptors.warnings) warn(result, warning);
                unsupportedDescriptors |= descriptors.unsupported;
                usableDescriptors |= !descriptors.unsupported;
                presets += descriptors.presets;
                if (presets.size() > maxBrushes) { reader.fail("An ABR file may contain at most 128 brush presets."); return; }
            } else if (key == QByteArrayView("patt", 4) && length)
                warn(result, "Photoshop pattern resources were skipped.");
            else if (key != QByteArrayView("patt", 4))
                warn(result, "Additional Photoshop resources were skipped.");
            continue;
        }
        foundSamples = true;
        while (section.remaining() && section.ok()) {
            if (++samples > maxBrushes) {
                section.fail("An ABR file may contain at most 128 brush records.");
                return;
            }
            const auto sampleLength = section.u32();
            auto sample = section.record(sampleLength);
            section.take((4 - (quint64(sampleLength) % 4)) % 4);
            if (!section.ok()) return;
            const auto prefix = sample.take(result.subversion == 1 ? 47 : 301);
            QString sampleId;
            if (sample.ok() && quint8(prefix[0]) == 36) {
                const QUuid uuid(QString::fromLatin1(prefix.data() + 1, 36));
                if (!uuid.isNull()) sampleId = uuid.toString(QUuid::WithoutBraces);
            }
            // Even a skipped sample makes a repeated identifier ambiguous. Do
            // not bind its preset to a different, supported record by accident.
            if (!sampleId.isEmpty()) {
                if (seenSampleIds.contains(sampleId)) ambiguousSampleIds.insert(sampleId);
                seenSampleIds.insert(sampleId);
            }
            const auto before = result.brushes.size();
            parseSample(sample, result, QString("%1 %2").arg(baseName).arg(samples),
                        digest + ":" + QString::number(samples), pixels);
            if (result.brushes.size() > before) result.brushes.last().sampleId = sampleId;
        }
    }
    if (!reader.ok()) return;
    if (usableDescriptors && !unsupportedDescriptors) {
        QHash<QString, int> samplesById;
        for (int i = 0; i < result.brushes.size(); ++i) {
            const auto &id = result.brushes[i].sampleId; if (id.isEmpty()) continue;
            samplesById.insert(id, i);
        }
        QVector<AbrBrush> mapped; QSet<int> matched; int presetIndex = 0;
        for (const auto &preset : presets) {
            ++presetIndex;
            AbrBrush brush;
            if (preset.computed) {
                brush.computed = true; brush.sourceId = digest + ":computed:" + QString::number(presetIndex);
                brush.sourceSize = QSize(qRound(preset.settings.size), qRound(preset.settings.size));
            } else {
                const QUuid uuid(preset.sampleId);
                const auto id = uuid.isNull() ? QString() : uuid.toString(QUuid::WithoutBraces);
                if (id.isEmpty() || !samplesById.contains(id) || ambiguousSampleIds.contains(id)) {
                    warn(result, QString("Preset '%1' has a missing, unsupported or ambiguous sampled tip; its settings were not assigned to another tip.").arg(preset.name));
                    continue;
                }
                const int index = samplesById.value(id); brush = result.brushes[index];
                if (matched.contains(index)) brush.sourceId += ":preset:" + QString::number(presetIndex);
                matched.insert(index);
            }
            brush.name = preset.name; brush.settings = preset.settings; brush.settings->tipMask = brush.mask;
            brush.compatibility = preset.compatibility; brush.category = "Photoshop";
            mapped.append(std::move(brush));
        }
        // Keep genuinely unreferenced samples available as tips, with an explicit
        // defaults report. Unknown descriptors never use sample-list positions.
        for (int i = 0; i < result.brushes.size(); ++i) if (!matched.contains(i)) {
            auto brush = result.brushes[i]; brush.compatibility.append("No matching supported preset descriptor; native sampled-tip defaults are used.");
            mapped.append(std::move(brush));
        }
        if (mapped.size() > maxBrushes) { reader.fail("Mapped ABR presets and unmatched tips exceed the 128-brush import limit."); return; }
        result.brushes = std::move(mapped);
    } else warn(result, "Modern preset names and settings were unavailable; numbered tip names and native defaults are used.");
    if (!foundSamples && result.brushes.isEmpty()) reader.fail("This ABR file contains no supported sampled or computed brush tips.");
}
} // namespace

AbrImportResult importAbr(const QString &path) {
    AbrImportResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = "Could not open the ABR file: " + file.errorString();
        return result;
    }
    if (file.size() > maxFileBytes) {
        result.error = "ABR files must be no larger than 256 MiB.";
        return result;
    }
    const auto data = file.read(maxFileBytes + 1);
    if (file.error() != QFileDevice::NoError || data.size() > maxFileBytes) {
        result.error = "Could not read the ABR file within the 256 MiB limit.";
        return result;
    }
    Reader reader(data, result.error);
    result.version = reader.u16();
    const auto secondary = reader.u16();
    if (!reader.ok()) return result;
    const QString baseName = QFileInfo(path).completeBaseName();
    const QString digest = "abr-" + QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    if (result.version == 1 || result.version == 2) {
        parseLegacy(reader, result, secondary, baseName, digest);
    } else if ((result.version == 6 || result.version == 10) && (secondary == 1 || secondary == 2)) {
        result.subversion = secondary;
        parseModern(reader, result, baseName, digest);
    } else {
        result.subversion = result.version >= 6 ? secondary : 0;
        result.error = QString("Unsupported ABR version %1.%2. Supported versions are 1, 2, 6.1, 6.2, 10.1, and 10.2.")
                           .arg(result.version).arg(secondary);
    }
    if (!result.error.isEmpty()) {
        result.brushes.clear();
    } else if (result.brushes.isEmpty()) {
        result.error = "No supported 8-bit sampled brush tips were found in this ABR file.";
    } else {
        const bool mapped = std::any_of(result.brushes.cbegin(), result.brushes.cend(), [](const auto &brush) { return brush.settings.has_value(); });
        warn(result, mapped ? "Supported preset settings are mapped to Toon Pro's native engine; see each brush's compatibility details. Photoshop rendering is not reproduced exactly."
                            : "Only sampled tips are imported; Photoshop dynamics, textures, dual brushes, and preset settings are not mapped.");
    }
    return result;
}
} // namespace toon
