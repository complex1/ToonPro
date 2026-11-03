#include "palette_library.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
using namespace toon;
namespace {
QByteArray bytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
bool write(const QString &path, const QByteArray &data) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size(); }
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
}
class PaletteLibraryTest final : public QObject {
    Q_OBJECT
private slots:
    void defaultsAreLazyAndNamedEditsRoundTripWithAlpha() {
        QTemporaryDir dir; const auto path = dir.filePath("workspace/palettes.json"); PaletteLibrary library(path); QString error;
        QVERIFY(library.load(&error)); QVERIFY(library.writable()); QCOMPARE(library.entries().size(), 1);
        QCOMPARE(library.entries()[0], standardColorPalette()); QVERIFY(!QFileInfo::exists(path));
        QSignalSpy changed(&library, &PaletteLibrary::changed);
        const auto paletteId = library.createPalette("  Character colors  ", &error); QVERIFY2(!paletteId.isEmpty(), qPrintable(error));
        QVERIFY(!QUuid(paletteId).isNull()); QCOMPARE(library.palette(paletteId)->name, QString("Character colors"));
        const QColor color = QColor::fromHsvF(.173, .631, .852, .41);
        const auto swatchId = library.addSwatch(paletteId, "Eyes", color, &error); QVERIFY2(!swatchId.isEmpty(), qPrintable(error));
        QCOMPARE(library.palette(paletteId)->swatches[0].color, QColor::fromRgba(color.rgba()));
        QVERIFY(library.renamePalette(paletteId, "Hero", &error)); QVERIFY(library.renameSwatch(paletteId, swatchId, "Eye highlight", &error));
        QVERIFY(library.updateSwatch(paletteId, swatchId, QColor("#80442211"), &error));
        QCOMPARE(changed.count(), 5);
        PaletteLibrary reopened(path); QVERIFY2(reopened.load(&error), qPrintable(error)); QCOMPARE(reopened.entries(), library.entries());
        QCOMPARE(reopened.palette(paletteId)->swatches[0].color.alpha(), 128);
        QVERIFY(library.removeSwatch(paletteId, swatchId)); QVERIFY(library.palette(paletteId)->swatches.isEmpty());
        QVERIFY(library.removePalette(paletteId)); QVERIFY(!library.palette(paletteId));
        QVERIFY(library.removePalette(library.entries()[0].id)); QVERIFY(library.entries().isEmpty());
        QVERIFY(reopened.load()); QVERIFY(reopened.entries().isEmpty()); // Empty is deliberate; don't resurrect deleted defaults.
    }
    void rejectedEditsDoNotAlterTheSavedLibrary() {
        QTemporaryDir dir; const auto path = dir.filePath("palettes.json"); PaletteLibrary library(path); QVERIFY(library.load());
        const auto paletteId = library.createPalette("Hero"); const auto swatchId = library.addSwatch(paletteId, "Skin", QColor("#c38c6a"));
        const auto original = bytes(path); const auto entries = library.entries(); QString error;
        QVERIFY(library.createPalette(" HERO ", &error).isEmpty()); QVERIFY(!error.isEmpty());
        QVERIFY(library.createPalette("\n", &error).isEmpty()); QVERIFY(library.createPalette(QString(101, 'x'), &error).isEmpty());
        QVERIFY(!library.renamePalette(paletteId, "Standard", &error));
        QVERIFY(library.addSwatch(paletteId, " skin ", Qt::white, &error).isEmpty());
        QVERIFY(library.addSwatch(paletteId, "Invalid", QColor{}, &error).isEmpty());
        QVERIFY(!library.updateSwatch(paletteId, swatchId, QColor{}, &error));
        QVERIFY(!library.renameSwatch(paletteId, swatchId, "Line\nBreak", &error));
        QVERIFY(!library.removeSwatch(paletteId, "missing", &error)); QVERIFY(!library.removePalette("missing", &error));
        QCOMPARE(bytes(path), original); QCOMPARE(library.entries(), entries); QVERIFY(library.writable());
    }
    void malformedNewerAndOversizeLibrariesArePreserved() {
        QTemporaryDir dir; const auto path = dir.filePath("palettes.json"); PaletteLibrary valid(path); QVERIFY(valid.load()); QVERIFY(!valid.createPalette("Hero").isEmpty());
        const auto original = QJsonDocument::fromJson(bytes(path)).object();
        QVector<QByteArray> invalids{"{broken", QByteArray(2 * 1024 * 1024 + 1, 'x')};
        auto newer = original; newer["version"] = 2; invalids.append(QJsonDocument(newer).toJson());
        auto unknown = original; unknown["future"] = true; invalids.append(QJsonDocument(unknown).toJson());
        auto duplicate = original; auto palettes = duplicate["palettes"].toArray(); palettes.append(palettes[0]); duplicate["palettes"] = palettes;
        invalids.append(QJsonDocument(duplicate).toJson());
        auto badColor = original; palettes = badColor["palettes"].toArray(); auto palette = palettes[0].toObject();
        auto swatches = palette["swatches"].toArray(); auto swatch = swatches[0].toObject(); swatch["color"] = "red";
        swatches[0] = swatch; palette["swatches"] = swatches; palettes[0] = palette; badColor["palettes"] = palettes;
        invalids.append(QJsonDocument(badColor).toJson());
        for (const auto &data : invalids) {
            QVERIFY(write(path, data)); PaletteLibrary damaged(path); QString error; QVERIFY(!damaged.load(&error));
            QVERIFY(!error.isEmpty()); QVERIFY(!damaged.writable()); QVERIFY(damaged.createPalette("Overwrite", &error).isEmpty()); QCOMPARE(bytes(path), data);
        }
    }
    void outsideChangesRemovalAndWriterLockPreserveOriginals() {
        QTemporaryDir dir; const auto path = dir.filePath("palettes.json"); PaletteLibrary first(path), second(path);
        QVERIFY(first.load()); QVERIFY(second.load()); QVERIFY(!first.createPalette("First").isEmpty());
        const auto original = bytes(path); QString error; QVERIFY(second.createPalette("Second", &error).isEmpty());
        QVERIFY(error.contains("changed outside")); QVERIFY(!second.writable()); QCOMPARE(bytes(path), original);
        QVERIFY(second.load()); QLockFile lock(path + ".lock"); QVERIFY(lock.tryLock(0));
        QVERIFY(second.createPalette("Locked", &error).isEmpty()); QVERIFY(error.contains("Another Toon Pro")); QVERIFY(second.writable()); QCOMPARE(bytes(path), original);
        lock.unlock(); QVERIFY(!second.createPalette("After lock", &error).isEmpty());
        QVERIFY(first.load()); QVERIFY(QFile::remove(path)); QVERIFY(first.createPalette("Deleted elsewhere", &error).isEmpty());
        QVERIFY(!first.writable()); QVERIFY(!QFileInfo::exists(path));
    }
    void paletteAndSwatchCountsAreBounded() {
        QTemporaryDir dir; const auto path = dir.filePath("palettes.json"); PaletteLibrary seed(path); QVERIFY(seed.load()); QVERIFY(!seed.createPalette("Seed").isEmpty());
        auto json = QJsonDocument::fromJson(bytes(path)).object(); QJsonArray palettes;
        for (int i = 0; i < maxColorPalettes; ++i) palettes.append(QJsonObject{{"id", id()}, {"name", QString("Palette %1").arg(i)}, {"swatches", QJsonArray{}}});
        json["palettes"] = palettes; QVERIFY(write(path, QJsonDocument(json).toJson())); PaletteLibrary library(path); QVERIFY(library.load());
        QString error; QVERIFY(library.createPalette("Too many", &error).isEmpty()); QVERIFY(error.contains("64"));
        auto palette = palettes[0].toObject(); QJsonArray swatches;
        for (int i = 0; i < maxPaletteSwatches; ++i) swatches.append(QJsonObject{{"id", id()}, {"name", QString("Swatch %1").arg(i)}, {"color", "#ff123456"}});
        palette["swatches"] = swatches; palettes[0] = palette; json["palettes"] = palettes; QVERIFY(write(path, QJsonDocument(json).toJson())); QVERIFY(library.load());
        QVERIFY(library.addSwatch(library.entries()[0].id, "Too many", Qt::red, &error).isEmpty()); QVERIFY(error.contains("256"));
        swatches.append(QJsonObject{{"id", id()}, {"name", "Too many"}, {"color", "#ff123456"}});
        palette["swatches"] = swatches; palettes[0] = palette; json["palettes"] = palettes; const auto oversized = QJsonDocument(json).toJson();
        QVERIFY(write(path, oversized)); QVERIFY(!library.load()); QCOMPARE(bytes(path), oversized);
    }
    void unwritableLocationDoesNotChangeEntries() {
        QTemporaryDir dir; const auto blocked = dir.filePath("block"); QVERIFY(write(blocked, "file"));
        PaletteLibrary library(blocked + "/palettes.json"); QVERIFY(library.load()); const auto entries = library.entries(); QString error;
        QVERIFY(library.createPalette("Cannot save", &error).isEmpty()); QVERIFY(!error.isEmpty()); QCOMPARE(library.entries(), entries); QCOMPARE(bytes(blocked), QByteArray("file"));
    }
};
QTEST_APPLESS_MAIN(PaletteLibraryTest)
#include "palette_library_tests.moc"
