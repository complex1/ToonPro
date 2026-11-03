#include "abr_import.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace toon;

namespace {
void u16(QByteArray &out, quint16 value) {
    out.append(char(value >> 8)); out.append(char(value));
}
void u32(QByteArray &out, quint32 value) {
    out.append(char(value >> 24)); out.append(char(value >> 16));
    out.append(char(value >> 8)); out.append(char(value));
}
QByteArray sample(int width, int height, const QByteArray &pixels,
                  int compression = 0, int depth = 8, qint32 top = 0, qint32 left = 0) {
    QByteArray out;
    u32(out, quint32(top)); u32(out, quint32(left));
    u32(out, quint32(qint64(top) + height)); u32(out, quint32(qint64(left) + width));
    u16(out, quint16(depth)); out.append(char(compression)); out.append(pixels);
    return out;
}
QByteArray legacyRecord(int version, const QByteArray &sampleData, int type = 2, const QString &name = "Ink tip") {
    QByteArray body(6, '\0');
    if (version == 2) {
        u32(body, quint32(name.size() + 1));
        for (const QChar character : name) u16(body, character.unicode());
        u16(body, 0);
    }
    body.append(QByteArray(9, '\0')); body.append(sampleData);
    QByteArray out; u16(out, quint16(type)); u32(out, quint32(body.size())); out.append(body);
    return out;
}
QByteArray legacy(int version, const QList<QByteArray> &records) {
    QByteArray out; u16(out, quint16(version)); u16(out, quint16(records.size()));
    for (const auto &record : records) out.append(record);
    return out;
}
QByteArray modernRecord(int subversion, const QByteArray &sampleData) {
    QByteArray body(subversion == 1 ? 47 : 301, '\0'); body.append(sampleData);
    QByteArray out; u32(out, quint32(body.size())); out.append(body);
    out.append(QByteArray((4 - body.size() % 4) % 4, '\0'));
    return out;
}
QByteArray section(const char *key, const QByteArray &payload) {
    QByteArray out("8BIM", 4); out.append(key, 4); u32(out, quint32(payload.size())); out.append(payload);
    return out;
}
QByteArray modern(int subversion, const QList<QByteArray> &records, bool extras = false, int version = 6) {
    QByteArray out; u16(out, quint16(version)); u16(out, quint16(subversion));
    if (extras) out.append(section("desc", "opaque-descriptor"));
    QByteArray samples; for (const auto &record : records) samples.append(record);
    out.append(section("samp", samples));
    if (extras) out.append(section("patt", "opaque-pattern"));
    return out;
}
AbrImportResult importBytes(const QByteArray &bytes) {
    QTemporaryDir temp;
    const auto path = temp.filePath("test.abr"); QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        return {{}, {}, {}, {}, "Could not write test fixture."};
    file.close();
    return importAbr(path);
}
QByteArray literalRows(const QByteArray &pixels, int width, int height) {
    QByteArray out;
    for (int y = 0; y < height; ++y) u16(out, quint16(width + 1));
    for (int y = 0; y < height; ++y) { out.append(char(width - 1)); out.append(pixels.mid(y * width, width)); }
    return out;
}
QByteArray solidRows(int width, int height, quint8 value = 255) {
    QByteArray row;
    for (int remaining = width; remaining > 0;) {
        const int count = qMin(remaining, 128);
        row.append(char(257 - count)); row.append(char(value)); remaining -= count;
    }
    QByteArray out;
    for (int y = 0; y < height; ++y) u16(out, quint16(row.size()));
    for (int y = 0; y < height; ++y) out.append(row);
    return out;
}
}

class AbrImportTest : public QObject {
    Q_OBJECT
private slots:
    void supportedVersions_data() {
        QTest::addColumn<int>("version"); QTest::addColumn<int>("subversion"); QTest::addColumn<bool>("compressed");
        for (int version : {1, 2, 6, 10}) {
            for (int subversion : (version >= 6 ? QList<int>{1, 2} : QList<int>{0})) {
                for (bool compressed : {false, true}) {
                    const auto name = QString("v%1.%2-%3").arg(version).arg(subversion).arg(compressed ? "rle" : "raw").toLatin1();
                    QTest::newRow(name.constData()) << version << subversion << compressed;
                }
            }
        }
    }
    void supportedVersions() {
        QFETCH(int, version); QFETCH(int, subversion); QFETCH(bool, compressed);
        const QByteArray coverage = QByteArray::fromHex("00ff803f10e0");
        const auto payload = compressed ? literalRows(coverage, 3, 2) : coverage;
        const auto data = sample(3, 2, payload, compressed ? 1 : 0, 8, -50, -100);
        const auto bytes = version >= 6 ? modern(subversion, {modernRecord(subversion, data)}, true, version)
                                       : legacy(version, {legacyRecord(version, data, 2, QString::fromUtf8("墨 ink"))});
        const auto imported = importBytes(bytes);
        QVERIFY2(imported.error.isEmpty(), qPrintable(imported.error));
        QCOMPARE(imported.version, version); QCOMPARE(imported.subversion, subversion);
        QCOMPARE(imported.brushes.size(), 1); QVERIFY(!imported.warnings.isEmpty());
        const auto &brush = imported.brushes.first();
        QCOMPARE(brush.mask.format(), QImage::Format_Grayscale8); QCOMPARE(brush.mask.size(), QSize(3, 2));
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 3; ++x)
            QCOMPARE(brush.mask.constScanLine(y)[x], quint8(coverage[y * 3 + x]));
        QCOMPARE(brush.name, version == 2 ? QString::fromUtf8("墨 ink") : QString("test 1"));
        QCOMPARE(brush.sourceId, importBytes(bytes).brushes.first().sourceId);
    }
    void packBitsRepeatedRunsAndNoOps() {
        // Three pixels from a repeated run, two literal pixels, then a no-op.
        QByteArray packed; u16(packed, 6); packed.append(QByteArray::fromHex("feff01008080"));
        const auto imported = importBytes(modern(1, {modernRecord(1, sample(5, 1, packed, 1))}));
        QVERIFY2(imported.error.isEmpty(), qPrintable(imported.error));
        const auto &mask = imported.brushes[0].mask;
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(mask.constScanLine(0)), 5), QByteArray::fromHex("ffffff0080"));
    }
    void unsupportedFeaturesAreReported() {
        const auto valid = sample(1, 1, QByteArray(1, char(255)));
        const auto imported = importBytes(legacy(2, {legacyRecord(2, valid, 1), legacyRecord(2, valid),
            legacyRecord(2, sample(1, 1, QByteArray(2, char(255)), 0, 16))}));
        QVERIFY2(imported.error.isEmpty(), qPrintable(imported.error));
        QCOMPARE(imported.brushes.size(), 1);
        QVERIFY(imported.warnings.join(' ').contains("Computed"));
        QVERIFY(imported.warnings.join(' ').contains("16-bit"));
        QVERIFY(imported.warnings.join(' ').contains("dynamics"));
        QVERIFY(!importBytes(legacy(1, {legacyRecord(1, valid, 1)})).error.isEmpty());
        QVERIFY(!importBytes(QByteArray::fromHex("000b0001")).error.isEmpty());
        QVERIFY(!importBytes(QByteArray::fromHex("000a0003")).error.isEmpty());
        QVERIFY(!importBytes(QByteArray::fromHex("00060003")).error.isEmpty());
        const auto unsupportedCompression = importBytes(legacy(1, {legacyRecord(1, sample(1, 1, "x", 2))}));
        QVERIFY(!unsupportedCompression.error.isEmpty());
        QVERIFY(unsupportedCompression.warnings.join(' ').contains("method 2"));
        const auto withEmptyTip = importBytes(legacy(1, {legacyRecord(1, valid),
            legacyRecord(1, sample(2, 1, QByteArray(2, '\0')))}));
        QVERIFY(withEmptyTip.error.isEmpty()); QCOMPARE(withEmptyTip.brushes.size(), 1);
        QVERIFY(withEmptyTip.warnings.join(' ').contains("transparent"));
    }
    void truncationAndRecordLengthsAreAtomic() {
        const auto small = sample(3, 2, QByteArray::fromHex("00ff803f10e0"));
        const QList<QByteArray> examples{
            legacy(1, {legacyRecord(1, small), legacyRecord(1, small)}),
            legacy(2, {legacyRecord(2, small), legacyRecord(2, small)}),
            modern(1, {modernRecord(1, small), modernRecord(1, small)}),
            modern(2, {modernRecord(2, small), modernRecord(2, small)}),
            modern(2, {modernRecord(2, small), modernRecord(2, small)}, false, 10)};
        for (const auto &bytes : examples) {
            for (qsizetype cut = 0; cut < bytes.size(); ++cut) {
                const auto result = importBytes(bytes.first(cut));
                QVERIFY2(!result.error.isEmpty(), qPrintable(QString("Accepted truncated ABR at byte %1").arg(cut)));
                QVERIFY(result.brushes.isEmpty());
            }
        }
        auto corruptLength = examples.first();
        corruptLength.replace(6, 4, QByteArray::fromHex("ffffffff"));
        const auto result = importBytes(corruptLength);
        QVERIFY(!result.error.isEmpty()); QVERIFY(result.brushes.isEmpty());
        auto signature = examples.last(); signature[4] = 'x';
        QVERIFY(!importBytes(signature).error.isEmpty());
        QVERIFY(!importBytes(examples.first() + "trailing").error.isEmpty());
    }
    void invalidRowsNeverSpillIntoTheNextRow() {
        QList<QByteArray> invalid;
        QByteArray overflow; u16(overflow, 2); overflow.append(QByteArray::fromHex("fcff")); invalid << overflow;
        QByteArray shortRow; u16(shortRow, 2); shortRow.append(QByteArray::fromHex("00ff")); invalid << shortRow;
        QByteArray shortLiteral; u16(shortLiteral, 2); shortLiteral.append(QByteArray::fromHex("01ff")); invalid << shortLiteral;
        QByteArray shortRepeat; u16(shortRepeat, 1); shortRepeat.append(QByteArray::fromHex("fe")); invalid << shortRepeat;
        QByteArray missingRow; u16(missingRow, 10); missingRow.append(QByteArray::fromHex("feff")); invalid << missingRow;
        for (const auto &data : invalid) {
            const auto result = importBytes(legacy(1, {legacyRecord(1, sample(3, 1, data, 1))}));
            QVERIFY(!result.error.isEmpty()); QVERIFY(result.brushes.isEmpty());
        }
    }
    void limitsAndSignedBounds() {
        for (const auto dimensions : {QSize(0, 2), QSize(8193, 1), QSize(1, 8193)}) {
            const auto result = importBytes(legacy(1, {legacyRecord(1, sample(dimensions.width(), dimensions.height(), {}))}));
            QVERIFY(!result.error.isEmpty()); QVERIFY(result.brushes.isEmpty());
        }
        auto bounds = sample(1, 1, "x");
        bounds.replace(4, 4, QByteArray::fromHex("80000000"));
        bounds.replace(12, 4, QByteArray::fromHex("7fffffff"));
        QVERIFY(!importBytes(legacy(1, {legacyRecord(1, bounds)})).error.isEmpty());
        QByteArray tooMany; u16(tooMany, 1); u16(tooMany, 129);
        QVERIFY(importBytes(tooMany).error.contains("128"));
        QList<QByteArray> manyModern;
        for (int i = 0; i < 129; ++i) manyModern << modernRecord(1, sample(1, 1, "x"));
        const auto tooManyModern = importBytes(modern(1, manyModern));
        QVERIFY(tooManyModern.error.contains("128")); QVERIFY(tooManyModern.brushes.isEmpty());
        const auto maximum = sample(8192, 8192, solidRows(8192, 8192), 1);
        const auto record = modernRecord(2, maximum);
        QVERIFY(importBytes(modern(2, {record, record, record, record}, false, 10)).error.isEmpty());
        const auto oversized = importBytes(modern(2, {record, record, record, record, record}, false, 10));
        QVERIFY(oversized.error.contains("pixel")); QVERIFY(oversized.brushes.isEmpty());
        QList<QByteArray> stored;
        const auto storedRecord = modernRecord(2, sample(1024, 1024, solidRows(1024, 1024), 1));
        for (int i = 0; i < 64; ++i) stored.append(storedRecord);
        QVERIFY(importBytes(modern(2, stored)).error.isEmpty());
        stored.append(storedRecord);
        const auto overStored = importBytes(modern(2, stored));
        QVERIFY(overStored.error.contains("imported-pixel")); QVERIFY(overStored.brushes.isEmpty());
        QTemporaryDir temp; const auto path = temp.filePath("oversized.abr");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(256 * 1024 * 1024 + 1)); file.close();
        QVERIFY(importAbr(path).error.contains("256 MiB"));
        QVERIFY(!importAbr(temp.filePath("missing.abr")).error.isEmpty());
    }
    void largeTipsKeepCoverageAndAspectRatio() {
        QByteArray pixels;
        for (int y = 0; y < 1200; ++y) {
            pixels.append(QByteArray(1500, char(255)));
            pixels.append(QByteArray(1500, char(0)));
        }
        // Real packs can retain opaque sample tail fields before 4-byte padding.
        const auto raw = sample(3000, 1200, pixels) + QByteArray(8, '\0');
        const auto packed = sample(2500, 2500, solidRows(2500, 2500, 137), 1) + QByteArray(8, '\0');
        const auto thin = sample(8192, 1, solidRows(8192, 1), 1);
        const auto result = importBytes(modern(2, {modernRecord(2, raw), modernRecord(2, packed), modernRecord(2, thin)}, true, 10));
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.brushes.size(), 3);
        QCOMPARE(result.brushes[0].sourceSize, QSize(3000, 1200));
        const auto &mask = result.brushes[0].mask;
        QCOMPARE(mask.size(), QSize(1024, 409));
        QCOMPARE(mask.format(), QImage::Format_Grayscale8);
        QCOMPARE(mask.constScanLine(200)[100], 255);
        QCOMPARE(mask.constScanLine(200)[900], 0);
        QCOMPARE(result.brushes[1].mask.size(), QSize(1024, 1024));
        QCOMPARE(result.brushes[1].mask.constScanLine(500)[500], 137);
        QCOMPARE(result.brushes[2].mask.size(), QSize(1024, 1));
        QVERIFY(result.warnings.join(' ').contains("resized"));
        // A broken later sample must not leave a successfully resized first tip.
        const auto broken = importBytes(modern(2, {modernRecord(2, raw), modernRecord(2, packed.first(packed.size() / 2))}, false, 10));
        QVERIFY(!broken.error.isEmpty()); QVERIFY(broken.brushes.isEmpty());
    }
    void filesAboveOldLimitImport() {
        QTemporaryDir temp;
        QFile file(temp.filePath("large.abr")); QVERIFY(file.open(QIODevice::WriteOnly));
        QByteArray header; u16(header, 6); u16(header, 2);
        header.append("8BIMdesc", 8); u32(header, 65 * 1024 * 1024);
        QCOMPARE(file.write(header), header.size());
        QVERIFY(file.seek(header.size() + 65 * 1024 * 1024));
        const auto tail = section("samp", modernRecord(2, sample(1, 1, QByteArray(1, char(255)))));
        QCOMPARE(file.write(tail), tail.size()); file.close();
        const auto result = importAbr(file.fileName());
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.brushes.size(), 1);
        QVERIFY(result.warnings.join(' ').contains("Unsupported preset descriptor version"));
        QVERIFY(!result.brushes[0].settings);
        // The same large section claiming the recognized version is not an
        // opaque fallback: reject it before parsing beyond the descriptor cap.
        QVERIFY(file.open(QIODevice::ReadWrite)); QVERIFY(file.seek(header.size()));
        QByteArray recognized; u32(recognized, 16);
        QCOMPARE(file.write(recognized), recognized.size()); file.close();
        const auto oversized = importAbr(file.fileName());
        QVERIFY(oversized.error.contains("8 MiB limit")); QVERIFY(oversized.brushes.isEmpty());
    }
};

QTEST_GUILESS_MAIN(AbrImportTest)
#include "test_abr.moc"
