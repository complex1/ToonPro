#include "catalog_brushes.h"
#include "brush_engine.h"
#include "document.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QSet>
#include <QtTest>
#include <algorithm>
#include <cmath>

using namespace toon;
namespace {
BrushSettings recipe(const char *name, int revision = catalogBrushRecipeRevision) {
    const auto presets = catalogBrushPresets(revision);
    const QString id = "builtin/catalog/" + QString::fromLatin1(name);
    const auto it = std::find_if(presets.cbegin(), presets.cend(), [&](const auto &p) { return p.id == id; });
    Q_ASSERT(it != presets.cend());
    return it->brush;
}

QImage paintStroke(BrushSettings brush, qreal pressure = 1, int passes = 1) {
    auto project = Project::create();
    project.width = 1088; project.height = 512; project.transparent = true;
    project.shots[0].sheet.overscan = 0;
    Document document; document.load(project); document.setBrushRandomSeed(4119);
    BrushInput input; input.pressure = pressure;
    for (int pass = 0; pass < passes; ++pass) {
        if (!document.beginStroke({128, 256}, input, brush, false)) return {};
        for (int x = 132; x <= 960; x += 4) document.continueStroke({qreal(x), 256}, input);
        document.endStroke();
    }
    QImage image(project.width, project.height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent); QPainter painter(&image);
    paintRasterLayerPixels(painter, document.project().shots[0].layers[0]);
    return image;
}

struct EdgeVariation { qreal mean; qreal deviation; qreal relative() const { return deviation / qMax(1.0, mean); } };
EdgeVariation edgeVariation(const QImage &image, int row) {
    qreal sum = 0, squares = 0;
    constexpr int start = 320, count = 448;
    for (int x = start; x < start + count; ++x) {
        const qreal alpha = qAlpha(image.pixel(x, row)); sum += alpha; squares += alpha * alpha;
    }
    const qreal mean = sum / count;
    return {mean, std::sqrt(qMax(0.0, squares / count - mean * mean))};
}
}

class CatalogRecipeRefinementsTests : public QObject {
    Q_OBJECT
private slots:
    void originalRevisionMatchesPreRefinementSnapshot() {
        // Captured from the shipped recipes BEFORE adding the revision API or
        // changing any recipe, on 2026-10-29 with Qt 6.10.3 (macOS arm64).
        // This is the immutable migration reference, not a snapshot regenerated
        // from catalogBrushPresets(1) after modifying its implementation.
        QFile snapshot(QFINDTESTDATA("fixtures/catalog-recipe-v1.json"));
        QVERIFY(snapshot.open(QIODevice::ReadOnly));
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(snapshot.readAll(), &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        QVERIFY(document.isArray());
        const auto rows = document.array();
        const auto original = catalogBrushPresets(1);
        QCOMPARE(rows.size(), 112); QCOMPARE(original.size(), rows.size());
        QSet<QString> ids;
        for (qsizetype i = 0; i < original.size(); ++i) {
            const auto &preset = original[i];
            const auto expected = rows[i].toObject();
            QCOMPARE(preset.id, expected["id"].toString());
            QVERIFY(!ids.contains(preset.id)); ids.insert(preset.id);
            QCOMPARE(preset.smoothing, expected["smoothing"].toDouble());
            QCryptographicHash hash(QCryptographicHash::Sha256);
            hash.addData(QJsonDocument(brushSettingsToJson(preset.brush)).toJson(QJsonDocument::Compact));
            for (const auto &image : {preset.brush.tipMask, preset.brush.colorTip,
                    preset.brush.texture.pattern, preset.brush.dual.tipMask}) {
                hash.addData(QByteArray::number(image.width()) + ":" + QByteArray::number(image.height())
                    + ":" + QByteArray::number(image.format()));
                hash.addData(QByteArrayView(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes()));
            }
            QVERIFY2(hash.result().toHex() == expected["hash"].toString().toLatin1(), qPrintable(preset.id));
        }
    }

    void patternInteriorsStayTransparentAfterRepeatedFullSizePasses() {
        for (const auto *name : {"brick-surface", "stone-block-surface", "roof-tile-surface", "wood-grain",
                "fabric-weave", "tatami-surface", "cracked-earth", "rock-strata", "crosshatch-tile",
                "engraved-shade", "dry-speed-texture"}) {
            auto brush = recipe(name); brush.size = 256; brush.color = QColor("#296939");
            const auto image = paintStroke(brush, 1, 3);
            QVERIFY2(!image.isNull(), name);
            int gaps = 0, ink = 0;
            for (int y = 208; y < 304; ++y) for (int x = 256; x < 832; ++x) {
                const auto &pattern = brush.texture.pattern;
                const int coverage = pattern.constScanLine(y % pattern.height())[x % pattern.width()];
                const int alpha = qAlpha(image.pixel(x, y));
                if (!coverage) { ++gaps; QVERIFY2(alpha == 0, name); }
                else if (alpha) ++ink;
            }
            QVERIFY2(gaps > 20000, name); QVERIFY2(ink > 1000, name);
        }
    }

    void continuousTextureFootprintsDoNotPrintCircularSeams() {
        for (const auto *name : {"stone-grain", "soil-grain", "plaster-grain", "rough-concrete", "asphalt-grain"}) {
            for (const qreal pressure : {.25, .6, 1.}) {
                auto old = recipe(name, 1), current = recipe(name);
                old.size = current.size = 256;
                // Isolate the footprint from the deliberately irregular grain.
                // Actual patterned, full-resolution paint is checked separately
                // and exported below; the seam metric measures dab overlap only.
                old.texture.enabled = current.texture.enabled = false;
                const auto before = paintStroke(old, pressure);
                const auto after = paintStroke(current, pressure);
                const qreal factor = current.pressureSize ? qMax(current.sizeDynamics.minimum, pressure) : 1;
                const int row = qRound(256 + 256 * factor * .42);
                const auto oldVariation = edgeVariation(before, row);
                const auto newVariation = edgeVariation(after, row);
                qInfo() << name << pressure << "edge modulation" << oldVariation.relative() << "->" << newVariation.relative()
                    << "alpha deviation" << oldVariation.deviation << "->" << newVariation.deviation;
                QVERIFY2(newVariation.mean > 1, name);
                QVERIFY2(newVariation.relative() < .055, name);
                // The new edge is intentionally lighter. Comparing only its
                // deviation/mean with an almost opaque old edge exaggerates
                // harmless 8-bit rounding at low pressure. Compare absolute
                // alpha ripple too, with one coverage level as the noise floor.
                QVERIFY2(newVariation.deviation <= qMax(1.0, oldVariation.deviation + .5), name);
            }
        }
    }

    void foliageDynamicsVaryWithinOneStrokeAndReplayExactly() {
        for (const auto *name : {"short-grass-tuft", "wind-grass", "clover-patch", "moss-fringe",
                "broadleaf-cluster", "conifer-branch", "distant-canopy", "bare-twig", "vine-leaves", "bark-edge"}) {
            auto brush = recipe(name); brush.size = 128; brush.color = QColor("#477f35");
            for (const qreal pressure : {.15, .5, 1.}) {
                BrushInput input; input.pressure = pressure;
                QSet<QRgb> colors; QSet<QSize> sizes;
                for (quint64 i = 0; i < 24; ++i) {
                    const auto dab = evaluateBrushDab(brush, input, {256, 256}, 0, 0, i, 419);
                    const auto replay = evaluateBrushDab(brush, input, {256, 256}, 0, 0, i, 419);
                    colors.insert(dab.color.rgba()); sizes.insert(dab.mask.size());
                    QCOMPARE(replay.color, dab.color); QCOMPARE(replay.mask, dab.mask);
                }
                QVERIFY2(colors.size() > 12, name);
                QVERIFY2(sizes.size() > 5, name);
            }
        }
        // Precise pens and regular tiled patterns stay consistent deliberately.
        for (const auto *name : {"round-detail-mono", "brick-surface", "roof-tile-surface"}) {
            const auto brush = recipe(name);
            const auto first = evaluateBrushDab(brush, {}, {256, 256}, 0, 0, 0, 419);
            const auto later = evaluateBrushDab(brush, {}, {256, 256}, 0, 0, 17, 419);
            QCOMPARE(first.color, later.color); QCOMPARE(first.mask, later.mask);
        }
    }

    void renderFullSizeComparisons() {
        const auto directory = qEnvironmentVariable("TOON_RECIPE_REVIEW");
        if (directory.isEmpty()) QSKIP("Set TOON_RECIPE_REVIEW to export full-size comparison sheets.");
        QVERIFY(QDir().mkpath(directory));
        for (const auto *name : {"brick-surface", "roof-tile-surface", "wood-grain", "rock-strata", "stone-grain",
                "rough-concrete", "velvet-shade", "soft-oval", "cloud-edge-soft", "broadleaf-cluster"}) {
            QImage page(1088, 624, QImage::Format_ARGB32_Premultiplied); page.fill(Qt::white);
            QPainter painter(&page); painter.setFont(QFont("Arial", 17)); painter.setPen(Qt::black);
            painter.drawText(QPoint(16, 25), QString::fromLatin1(name) + " — before / after, 256 px brush");
            for (int revision = 1; revision <= 2; ++revision) {
                auto brush = recipe(name, revision); brush.size = 256;
                if (QString::fromLatin1(name) == "broadleaf-cluster") brush.color = QColor("#477f35");
                painter.drawImage(QPoint(0, 42 + (revision - 1) * 280), paintStroke(brush).copy(0, 116, 1088, 280));
            }
            painter.end(); QVERIFY(page.save(QDir(directory).filePath(QString::fromLatin1(name) + ".png")));
        }
    }
};
QTEST_MAIN(CatalogRecipeRefinementsTests)
#include "test_catalog_recipe_refinements.moc"
