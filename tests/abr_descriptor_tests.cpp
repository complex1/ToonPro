#include "abr_descriptor.h"
#include "abr_import.h"
#include "brush_panel.h"
#include "brush_library.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <cstring>

using namespace toon;
namespace {
const QString aId = "11111111-2222-4333-8444-555555555555", bId = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
void u16(QByteArray &bytes, quint16 n) { char data[2]; qToBigEndian(n, data); bytes.append(data, 2); }
void u32(QByteArray &bytes, quint32 n) { char data[4]; qToBigEndian(n, data); bytes.append(data, 4); }
QByteArray text(const QString &value) { QByteArray bytes; u32(bytes, value.size() + 1); for (auto c : value) u16(bytes, c.unicode()); u16(bytes, 0); return bytes; }
QByteArray key(const QByteArray &value) { QByteArray bytes; u32(bytes, value.size() == 4 ? 0 : value.size()); return bytes + value; }
QByteArray number(double value) { quint64 bits; std::memcpy(&bits, &value, 8); char data[8]; qToBigEndian(bits, data); return QByteArray(data, 8); }
QByteArray integer(int value) { QByteArray bytes; u32(bytes, quint32(value)); return bytes; }
struct Field { QByteArray name, kind, bytes; };
Field unit(const QByteArray &name, double value, const QByteArray &units = "#Prc") { return {name, "UntF", units + number(value)}; }
Field flag(const QByteArray &name, bool value) { return {name, "bool", QByteArray(1, char(value))}; }
QByteArray object(const QByteArray &classId, const QList<Field> &fields) {
    QByteArray bytes = text("") + key(classId); u32(bytes, fields.size());
    for (const auto &field : fields) bytes += key(field.name) + field.kind + field.bytes;
    return bytes;
}
Field obj(const QByteArray &name, const QByteArray &classId, const QList<Field> &fields) { return {name, "Objc", object(classId, fields)}; }
QList<Field> sampleShape(const QString &id) {
    return {{"sampledData", "TEXT", text(id)}, unit("Dmtr", 48, "#Pxl"), unit("Angl", 0, "#Ang"), unit("Rndn", 100), unit("Spcn", 25), flag("Intr", true)};
}
QByteArray preset(const QString &name, const QString &id, QList<Field> options = {}, QList<Field> shape = {}) {
    if (shape.isEmpty()) shape = sampleShape(id);
    QList<Field> fields{{"Nm  ", "TEXT", text(name)}, obj("Brsh", "sampledBrush", shape)}; fields += options;
    return object("brushPreset", fields);
}
QByteArray descriptors(const QList<QByteArray> &presets) {
    QByteArray list; u32(list, presets.size()); for (const auto &p : presets) list += "Objc" + p;
    return integer(16) + object("null", {{"Brsh", "VlLs", list}});
}
QByteArray section(const QByteArray &name, const QByteArray &body) { return "8BIM" + name + integer(body.size()) + body; }
QByteArray sample(const QString &id, quint8 opacity = 255, int subversion = 2, int depth = 8) {
    QByteArray body(subversion == 1 ? 47 : 301, '\0'); body[0] = 36;
    const auto ascii = id.toLatin1(); if (ascii.size() == 36) std::memcpy(body.data() + 1, ascii.constData(), 36);
    u32(body, 0); u32(body, 0); u32(body, 2); u32(body, 3); u16(body, depth); body.append(char(0));
    body.append(QByteArray(6 * (depth / 8), char(opacity)));
    return integer(body.size()) + body + QByteArray((4 - body.size() % 4) % 4, '\0');
}
QByteArray file(const QByteArray &desc, const QByteArray &samples, bool descriptorFirst = false, int version = 10, int subversion = 2) {
    QByteArray bytes; u16(bytes, version); u16(bytes, subversion);
    if (descriptorFirst) bytes += section("desc", desc);
    if (!samples.isEmpty()) bytes += section("samp", samples);
    if (!descriptorFirst) bytes += section("desc", desc);
    return bytes;
}
AbrImportResult importBytes(const QByteArray &data) {
    QTemporaryDir directory; QFile out(directory.filePath("pack.abr"));
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size()) { AbrImportResult result; result.error = "Cannot write fixture"; return result; }
    out.close(); return importAbr(out.fileName());
}
}

class AbrDescriptorTests final : public QObject {
    Q_OBJECT
private slots:
    void mappedAndComputedPresetsPersistInTheArtistLibrary() {
        QTemporaryDir dir; QString error; const auto path=dir.filePath("mapped.abr");
        const auto computed=object("brushPreset",{{"Nm  ","TEXT",text("Soft oval")},obj("Brsh","computedBrush",{unit("Dmtr",64,"#Pxl"),unit("Hrdn",40),unit("Rndn",50)})});
        const auto source=file(descriptors({preset("Mapped ink",aId,{flag("useTipDynamics",false)}),computed}),sample(aId));
        QFile out(path);QVERIFY(out.open(QIODevice::WriteOnly));out.write(source);out.close();
        BrushLibrary library(dir.filePath("brushes.json"));QVERIFY(library.load(&error));QString report;QStringList ids;
        QVERIFY2(importAbrIntoLibrary(&library,path,&report,&error,&ids),qPrintable(error));QCOMPARE(ids.size(),2);
        BrushLibrary reopened(library.path());QVERIFY2(reopened.load(&error),qPrintable(error));
        const auto &ink=reopened.presets()[reopened.presets().size()-2];const auto &oval=reopened.presets().last();
        QCOMPARE(ink.name,QString("Mapped ink"));QCOMPARE(ink.brush.size,48.);QCOMPARE(ink.brush.spacing,.25);QVERIFY(!ink.brush.pressureSize);QCOMPARE(ink.brush.tipMask.size(),QSize(3,2));
        QCOMPARE(oval.name,QString("Soft oval"));QVERIFY(oval.brush.tipMask.isNull());QCOMPARE(oval.brush.size,64.);QCOMPARE(oval.brush.hardness,.4);QCOMPARE(oval.brush.roundness,.5);
        QVERIFY(!oval.compatibility.isEmpty());QVERIFY(report.contains("native round"));QVERIFY(report.contains("Mapped ink"));
    }
    void namesFollowSampleIdsNotRecordOrderAndSharedTipsKeepDistinctPresets() {
        auto desc = descriptors({preset("Blue ink", bId), preset("墨 pen", aId),
                                preset("Same tip, larger", aId, {}, {{"sampledData", "TEXT", text(aId)}, unit("Dmtr", 80, "#Pxl")})});
        for (bool before : {false, true}) {
            const auto data = file(desc, sample(aId, 77) + sample(bId, 201), before);
            const auto imported = importBytes(data); QVERIFY2(imported.error.isEmpty(), qPrintable(imported.error));
            QCOMPARE(imported.brushes.size(), 3);
            QCOMPARE(imported.brushes[0].name, QString("Blue ink")); QCOMPARE(imported.brushes[0].mask.constScanLine(0)[0], quint8(201));
            QCOMPARE(imported.brushes[1].name, QString::fromUtf8("墨 pen")); QCOMPARE(imported.brushes[1].mask.constScanLine(0)[0], quint8(77));
            QCOMPARE(imported.brushes[2].mask, imported.brushes[1].mask);
            QCOMPARE(imported.brushes[1].settings->size, 48.); QCOMPARE(imported.brushes[2].settings->size, 80.);
            QVERIFY(imported.brushes[1].sourceId != imported.brushes[2].sourceId);
            QCOMPARE(imported.brushes[0].sourceId, importBytes(data).brushes[0].sourceId);
            QCOMPARE(imported.brushes[0].category, QString("Photoshop"));
        }
    }
    void geometryDynamicsTransferAndNativeLimitsAreReported() {
        auto shape = sampleShape(aId); shape.removeAt(1); shape.append(unit("Dmtr", 512, "#Pxl"));
        shape.removeAt(1); shape.append(unit("Angl", 35, "#Ang"));
        shape.removeAt(1); shape.append(unit("Rndn", 40)); shape.append(flag("flipX", true));
        const auto source = preset("Dynamic", aId, {
            flag("useTipDynamics", true), obj("szVr", "brVr", {{"bVTy", "long", integer(2)}, unit("jitter", 12), {"fStp", "long", integer(55)}}),
            unit("minimumDiameter", 30), obj("angleDynamics", "brVr", {{"bVTy", "long", integer(6)}, unit("jitter", 50)}),
            flag("usePaintDynamics", true), obj("opVr", "brVr", {{"bVTy", "long", integer(2)}, unit("jitter", 10)}),
            obj("prVr", "brVr", {{"bVTy", "long", integer(1)}, {"fStp", "long", integer(35)}}),
            flag("useScatter", true), obj("scatterDynamics", "brVr", {unit("jitter", 250), unit("Mnm ", 20), {"bVTy", "long", integer(3)}}),
            {"Cnt ", "doub", number(19)}, flag("bothAxes", true),
            obj("countDynamics", "brVr", {unit("jitter", 25), unit("Mnm ", 40)}),
            obj("toolOptions", "PbTl", {{"flow", "doub", number(25)}, {"Opct", "doub", number(60)}}),
            flag("useTexture", true), obj("dualBrush", "dualBrush", {flag("useDualBrush", true)})}, shape);
        const auto imported = importBytes(file(descriptors({source}), sample(aId)));
        QVERIFY2(imported.error.isEmpty(), qPrintable(imported.error)); QCOMPARE(imported.brushes.size(), 1);
        QVERIFY(imported.brushes[0].settings); const auto &b = *imported.brushes[0].settings;
        QCOMPARE(b.size, 256.); QCOMPARE(b.angle, 35.); QCOMPARE(b.roundness, .4); QCOMPARE(b.spacing, .25); QVERIFY(b.flipX);
        QVERIFY(!b.pressureSize); QCOMPARE(b.sizeDynamics.control, BrushControl::Pressure); QCOMPARE(b.sizeDynamics.jitter, .12);
        QCOMPARE(b.sizeDynamics.minimum, .3); QCOMPARE(b.sizeDynamics.fade, 55);
        QCOMPARE(b.angleDynamics.control, BrushControl::Direction); QCOMPARE(b.angleDynamics.jitter, .5);
        QCOMPARE(b.opacityDynamics.control, BrushControl::Pressure); QCOMPARE(b.opacityDynamics.jitter, .1);
        QCOMPARE(b.flowDynamics.control, BrushControl::Fade); QCOMPARE(b.flowDynamics.fade, 35);
        QCOMPARE(b.scatter, 2.5); QCOMPARE(b.scatterControl, BrushControl::Tilt); QVERIFY(b.scatterBothAxes);
        QCOMPARE(b.count, 16); QCOMPARE(b.countJitter, .25); QCOMPARE(b.opacity, .25); QCOMPARE(b.strokeOpacity, .6);
        QVERIFY(!b.texture.enabled); QVERIFY(!b.dual.enabled);
        const auto report = imported.brushes[0].compatibility.join(' ');
        QVERIFY(report.contains("Diameter limited")); QVERIFY(report.contains("Scatter count limited"));
        QVERIFY(report.contains("Pattern textures")); QVERIFY(report.contains("Dual brush")); QVERIFY(report.contains("not guarantee"));
        QVERIFY(!report.contains("Scatter jitter limited"));
        QVERIFY(report.contains("Scatter minimum (not supported by the native engine)"));
        QVERIFY(report.contains("Count minimum (not supported by the native engine)"));
        for (const auto &line : imported.brushes[0].compatibility) if (line.startsWith("Transferred:")) {
            QVERIFY(!line.contains("Scatter minimum")); QVERIFY(!line.contains("Count minimum"));
        }
    }
    void disabledDynamicsAreNotAppliedAndUnsupportedControlsAreExplicit() {
        const auto source = preset("Static", aId, {flag("useTipDynamics", false), obj("szVr", "brVr", {{"bVTy", "long", integer(2)}}),
            flag("usePaintDynamics", false), obj("opVr", "brVr", {unit("jitter", 100)})});
        const auto imported = importBytes(file(descriptors({source}), sample(aId))); QVERIFY(imported.error.isEmpty());
        const auto &b = *imported.brushes[0].settings; QVERIFY(!b.pressureSize);
        QCOMPARE(b.sizeDynamics.control, BrushControl::Off); QCOMPARE(b.opacityDynamics.jitter, 0.);
        const auto unsupported = preset("Rotation", aId, {flag("useTipDynamics", true),
            obj("angleDynamics", "brVr", {{"bVTy", "long", integer(7)}})});
        const auto result = importBytes(file(descriptors({unsupported}), sample(aId))); QVERIFY(result.error.isEmpty());
        QCOMPARE(result.brushes[0].settings->angleDynamics.control, BrushControl::Off);
        QVERIFY(result.brushes[0].compatibility.join(' ').contains("initial rotation"));
        QVERIFY(!result.brushes[0].compatibility.join(' ').contains("Transferred: Angle control"));
    }
    void computedRoundPresetsWorkWithoutAnySampleSection() {
        const auto computed = object("brushPreset", {{"Nm  ", "TEXT", text("Soft oval")},
            obj("Brsh", "computedBrush", {unit("Dmtr", 64, "#Pxl"), unit("Hrdn", 40), unit("Rndn", 50), unit("Spcn", 10)})});
        const auto imported = importBytes(file(descriptors({computed}), {}));
        QVERIFY2(imported.error.isEmpty(), qPrintable(imported.error)); QCOMPARE(imported.brushes.size(), 1);
        const auto &brush = imported.brushes[0]; QVERIFY(brush.computed); QVERIFY(brush.mask.isNull()); QVERIFY(brush.settings);
        QCOMPARE(brush.name, QString("Soft oval")); QCOMPARE(brush.settings->size, 64.); QCOMPARE(brush.settings->hardness, .4);
        QCOMPARE(brush.settings->roundness, .5); QCOMPARE(brush.sourceSize, QSize(64, 64));
        QVERIFY(brush.compatibility.join(' ').contains("native round"));
    }
    void danglingAndAmbiguousIdsNeverAssignSettingsByPosition() {
        const auto missing = importBytes(file(descriptors({preset("Wrong association", bId)}), sample(aId)));
        QVERIFY(missing.error.isEmpty()); QCOMPARE(missing.brushes.size(), 1); QVERIFY(!missing.brushes[0].settings);
        QCOMPARE(missing.brushes[0].name, QString("pack 1")); QVERIFY(missing.warnings.join(' ').contains("missing"));
        const auto duplicate = importBytes(file(descriptors({preset("Ambiguous", aId)}), sample(aId, 30) + sample(aId, 90)));
        QVERIFY(duplicate.error.isEmpty()); QCOMPARE(duplicate.brushes.size(), 2);
        QVERIFY(!duplicate.brushes[0].settings); QVERIFY(!duplicate.brushes[1].settings);
        QVERIFY(duplicate.warnings.join(' ').contains("ambiguous"));
        const auto skippedDuplicate = importBytes(file(descriptors({preset("Ambiguous unsupported", aId)}),
            sample(aId, 30) + sample(aId, 90, 2, 16)));
        QVERIFY(skippedDuplicate.error.isEmpty()); QCOMPARE(skippedDuplicate.brushes.size(), 1);
        QVERIFY(!skippedDuplicate.brushes[0].settings);
        QVERIFY(skippedDuplicate.warnings.join(' ').contains("ambiguous"));
    }
    void unsupportedDescriptorsFallbackButDamagedKnownDescriptorsFailAtomically() {
        const auto good = descriptors({preset("Valid", aId)});
        const auto truncated = importBytes(file(good.left(good.size() - 3), sample(aId)));
        QVERIFY(!truncated.error.isEmpty()); QVERIFY(truncated.brushes.isEmpty());
        const auto unknown = integer(16) + object("null", {{"future", "XXXX", ""}});
        const auto fallback = importBytes(file(unknown, sample(aId)));
        QVERIFY2(fallback.error.isEmpty(), qPrintable(fallback.error)); QCOMPARE(fallback.brushes.size(), 1);
        QVERIFY(!fallback.brushes[0].settings); QVERIFY(fallback.warnings.join(' ').contains("unsupported type"));
        const auto trailing = importBytes(file(good + "junk", sample(aId)));
        QVERIFY(!trailing.error.isEmpty()); QVERIFY(trailing.brushes.isEmpty());
        const auto invalidNumber = descriptors({preset("Invalid", aId, {}, {{"sampledData", "TEXT", text(aId)}, unit("Dmtr", qQNaN(), "#Pxl")})});
        QVERIFY(!importBytes(file(invalidNumber, sample(aId))).error.isEmpty());
    }
    void descriptorBudgetsAndUnknownGeometryAreReported() {
        auto deep = object("node", {});
        for (int i = 0; i < 24; ++i) deep = object("node", {{"child", "Objc", deep}});
        QVERIFY(!readAbrDescriptors(integer(16) + deep).error.isEmpty());
        const auto oversized = integer(16) + QByteArray(8 * 1024 * 1024, 0);
        QVERIFY(!readAbrDescriptors(oversized).error.isEmpty());
        const auto opaque = readAbrDescriptors(QByteArray(8 * 1024 * 1024 + 1, 0));
        QVERIFY(opaque.error.isEmpty()); QVERIFY(opaque.unsupported);
        QList<QByteArray> excessive; for (int i = 0; i < 129; ++i) excessive.append(preset("Many", aId));
        QVERIFY(!readAbrDescriptors(descriptors(excessive)).error.isEmpty());
        const auto odd = object("brushPreset", {{"Nm  ", "TEXT", text("Bristles")}, obj("Brsh", "dBrush", {})});
        const auto imported = importBytes(file(descriptors({odd}), sample(aId)));
        QVERIFY(imported.error.isEmpty()); QVERIFY(imported.warnings.join(' ').contains("dBrush"));
        QVERIFY(!imported.brushes[0].settings);
    }
};
QTEST_GUILESS_MAIN(AbrDescriptorTests)
#include "abr_descriptor_tests.moc"
