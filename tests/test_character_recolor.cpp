#include "character_recolor.h"
#include "document.h"
#include <QUuid>
#include <QtTest>
#include <array>

using namespace toon;
namespace {
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QImage tile() { QImage image(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); return image; }
Project fixture(bool secondShot = false) {
    auto project = Project::create(); project.width = 64; project.height = 64;
    CharacterModel character{id(), "Hana", {}, {{id(), "Skin", CharacterColorCategory::Skin, QColor("#ebac82")},
                                               {id(), "Hair", CharacterColorCategory::Hair, QColor("#3b281e")}}};
    character.colorVariants = standardCharacterColorVariants(character.colors);
    character.colorVariants[0].colors[character.colors[0].id] = QColor("#536b9b");
    character.colorVariants[0].colors[character.colors[1].id] = QColor("#172744");
    project.characters.append(character);
    auto image = tile(); image.setPixelColor(5, 5, character.colors[0].color); image.setPixelColor(6, 5, character.colors[1].color);
    image.setPixelColor(7, 5, Qt::green); project.shots[0].layers[0].tiles[{0, 0}] = image;
    if (secondShot) {
        auto shot = project.shots[0]; shot.id = id(); shot.number = "C002"; shot.name = "Close-up";
        shot.layers[0].id = id(); project.shots.append(shot);
    }
    return project;
}
RecolorSettings settings(const Project &project) {
    return {project.characters[0], {}, project.characters[0].colorVariants[0].id, {project.width, project.height}};
}
QVector<RecolorTarget> targets(const Project &project) {
    QVector<RecolorTarget> output;
    for (const auto &shot : project.shots) for (const auto &layer : shot.layers)
        output.append({shot.id, layer.id, shot.number + " / " + layer.name, layer});
    return output;
}
QRgb pixel(const RasterLayer &layer, int x, int y, TileKey key = {0, 0}) { return layer.tiles.value(key).pixel(x, y); }
}
class CharacterRecolorTests : public QObject {
    Q_OBJECT
private slots:
    void mapsRolesWithoutChangingUnmatchedPixelsOrSourceArtwork() {
        const auto project = fixture(); const auto options = settings(project);
        const auto result = prepareCharacterRecolor(targets(project), options);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error)); QCOMPARE(result.drawings.size(), 1);
        QCOMPARE(result.changedPixels, 2); QCOMPARE(result.matchedPixels, 2); QCOMPARE(result.ambiguousEdgePixels, 0);
        QCOMPARE(pixel(result.drawings[0].replacement, 5, 5), options.character.colorVariants[0].colors.value(options.character.colors[0].id).rgba());
        QCOMPARE(pixel(result.drawings[0].replacement, 6, 5), options.character.colorVariants[0].colors.value(options.character.colors[1].id).rgba());
        QCOMPARE(pixel(result.drawings[0].replacement, 7, 5), QColor(Qt::green).rgba());
        QCOMPARE(pixel(project.shots[0].layers[0], 5, 5), options.character.colors[0].color.rgba());
        QCOMPARE(result.drawings[0].before.pixel(5, 5), pixel(project.shots[0].layers[0], 5, 5));
        QCOMPARE(result.drawings[0].after.pixel(5, 5), pixel(result.drawings[0].replacement, 5, 5));
    }
    void preservesEveryAlphaLevelAndHandlesPremultipliedQuantization() {
        auto project = fixture(); auto &character = project.characters[0];
        character.colors.resize(1); for (auto &variant : character.colorVariants) {
            const auto color = variant.colors.value(character.colors[0].id); variant.colors = {{character.colors[0].id, color}};
        }
        auto &image = project.shots[0].layers[0].tiles[{0, 0}]; image.fill(Qt::transparent);
        const auto source = character.colors[0].color.rgb(); const auto destination = QColor(73, 102, 181, 16);
        character.colorVariants[0].colors[character.colors[0].id] = destination;
        for (int alpha = 1; alpha < 256; ++alpha) image.setPixel(alpha, 0, qPremultiply((source & 0xffffffu) | (quint32(alpha) << 24)));
        const auto result = prepareCharacterRecolor(targets(project), settings(project)); QVERIFY(result.error.isEmpty());
        QCOMPARE(result.matchedPixels, 255); QCOMPARE(result.ambiguousEdgePixels, 0);
        for (int alpha = 1; alpha < 256; ++alpha) {
            const auto actual = pixel(result.drawings[0].replacement, alpha, 0);
            QCOMPARE(qAlpha(actual), alpha);
            QCOMPARE(actual, qPremultiply((destination.rgb() & 0xffffffu) | (quint32(alpha) << 24)));
        }
        QCOMPARE(pixel(result.drawings[0].replacement, 0, 0), QRgb(0));
    }
    void mappingsDoNotCascadeAndEquivalentDuplicateRolesAreAllowed() {
        auto project = fixture(); auto &character = project.characters[0];
        character.colors[0].color = Qt::red; character.colors[1].color = Qt::blue;
        character.colorVariants[0].colors[character.colors[0].id] = Qt::blue;
        character.colorVariants[0].colors[character.colors[1].id] = Qt::green;
        auto &image = project.shots[0].layers[0].tiles[{0, 0}]; image.fill(Qt::transparent);
        image.setPixelColor(0, 0, Qt::red); image.setPixelColor(1, 0, Qt::blue);
        auto result = prepareCharacterRecolor(targets(project), settings(project)); QVERIFY(result.error.isEmpty());
        QCOMPARE(pixel(result.drawings[0].replacement, 0, 0), QColor(Qt::blue).rgba());
        QCOMPARE(pixel(result.drawings[0].replacement, 1, 0), QColor(Qt::green).rgba());
        character.colors[1].color = Qt::red;
        result = prepareCharacterRecolor(targets(project), settings(project)); QVERIFY(result.error.contains("Skin")); QVERIFY(result.error.contains("Hair")); QVERIFY(result.drawings.isEmpty());
        character.colorVariants[0].colors[character.colors[1].id] = Qt::blue;
        result = prepareCharacterRecolor(targets(project), settings(project)); QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.changedPixels, 1);
    }
    void existingNightPaintCanReturnToDayOrAnotherVariant() {
        auto project = fixture(); const auto night = settings(project);
        const auto first = prepareCharacterRecolor(targets(project), night); QVERIFY(first.error.isEmpty());
        project.shots[0].layers[0].tiles = first.drawings[0].replacement.tiles;
        auto options = settings(project); options.sourceVariantId = options.targetVariantId; options.targetVariantId.clear();
        const auto day = prepareCharacterRecolor(targets(project), options); QVERIFY(day.error.isEmpty());
        QCOMPARE(pixel(day.drawings[0].replacement, 5, 5), project.characters[0].colors[0].color.rgba());
        auto &sunset = project.characters[0].colorVariants[1]; sunset.colors[project.characters[0].colors[0].id] = QColor("#e89070");
        options.character = project.characters[0]; options.targetVariantId = sunset.id;
        const auto evening = prepareCharacterRecolor(targets(project), options); QVERIFY(evening.error.isEmpty());
        QCOMPARE(pixel(evening.drawings[0].replacement, 5, 5), QColor("#e89070").rgba());
    }
    void ambiguousQuantizedEdgesStayUntouched() {
        auto project = fixture(); auto &character = project.characters[0];
        character.colors[0].color = QColor(200, 100, 20); character.colors[1].color = QColor(201, 101, 21);
        character.colorVariants[0].colors[character.colors[0].id] = Qt::white;
        character.colorVariants[0].colors[character.colors[1].id] = Qt::black;
        auto &image = project.shots[0].layers[0].tiles[{0, 0}]; image.fill(Qt::transparent);
        const auto ambiguous = qPremultiply(qRgba(200, 100, 20, 1));
        QCOMPARE(ambiguous, qPremultiply(qRgba(201, 101, 21, 1))); image.setPixel(0, 0, ambiguous);
        image.setPixelColor(1, 0, character.colors[0].color);
        const auto result = prepareCharacterRecolor(targets(project), settings(project)); QVERIFY(result.error.isEmpty());
        QCOMPARE(result.changedPixels, 1); QCOMPARE(result.ambiguousEdgePixels, 1);
        QCOMPARE(pixel(result.drawings[0].replacement, 0, 0), ambiguous);
    }
    void opaqueAntialiasedBoundaryUsesOriginalNeighborsAcrossTileSeams() {
        auto project = fixture(); auto &character = project.characters[0];
        character.colors[0].color = QColor(20, 20, 20); character.colors[1].color = QColor(220, 220, 220);
        character.colorVariants[0].colors[character.colors[0].id] = QColor(20, 80, 200);
        character.colorVariants[0].colors[character.colors[1].id] = QColor(200, 120, 40);
        auto &layer = project.shots[0].layers[0]; layer.tiles.clear(); layer.tiles[{0, 0}] = tile(); layer.tiles[{1, 0}] = tile(); layer.tiles[{1, 1}] = tile();
        layer.tiles[{0, 0}].setPixelColor(254, 40, character.colors[0].color);
        layer.tiles[{0, 0}].setPixelColor(255, 40, QColor(120, 120, 120));
        layer.tiles[{1, 0}].setPixelColor(0, 40, character.colors[1].color);
        // A diagonal edge at the meeting point of four tiles uses the same original neighborhood.
        layer.tiles[{0, 0}].setPixelColor(254, 254, character.colors[0].color);
        layer.tiles[{0, 0}].setPixelColor(255, 255, QColor(120, 120, 120));
        layer.tiles[{1, 1}].setPixelColor(0, 0, character.colors[1].color);
        const auto result = prepareCharacterRecolor(targets(project), settings(project)); QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.changedPixels, 6); QCOMPARE(result.ambiguousEdgePixels, 0);
        QCOMPARE(pixel(result.drawings[0].replacement, 255, 40), qRgb(110, 100, 120));
        QCOMPARE(pixel(result.drawings[0].replacement, 255, 255), qRgb(110, 100, 120));
        QCOMPARE(pixel(layer, 255, 40), qRgb(120, 120, 120));
        auto identity = settings(project); identity.targetVariantId.clear();
        const auto unchanged = prepareCharacterRecolor(targets(project), identity); QCOMPARE(unchanged.changedPixels, 0);
        QCOMPARE(unchanged.drawings[0].replacement.tiles, layer.tiles);
    }
    void opaqueMixturesNeedBothExactNeighborsAndDoNotRecolorGradients() {
        auto project = fixture(); auto &character = project.characters[0];
        character.colors[0].color = QColor(20, 20, 20); character.colors[1].color = QColor(220, 220, 220);
        character.colorVariants[0].colors[character.colors[0].id] = QColor(20, 80, 200);
        character.colorVariants[0].colors[character.colors[1].id] = QColor(200, 120, 40);
        auto &image = project.shots[0].layers[0].tiles[{0, 0}]; image.fill(Qt::transparent);
        // A six-pixel gradient has no mixed pixel directly touching both endpoints.
        for (int x = 0; x < 6; ++x) image.setPixelColor(10 + x, 10, QColor(20 + x * 40, 20 + x * 40, 20 + x * 40));
        image.setPixelColor(30, 30, QColor(120, 120, 120)); // No supporting colors.
        image.setPixelColor(39, 40, character.colors[0].color);
        image.setPixelColor(40, 40, QColor(120, 50, 120)); // Near both colors, but not their mixture.
        image.setPixelColor(41, 40, character.colors[1].color);
        image.setPixelColor(49, 50, character.colors[0].color);
        image.setPixelColor(50, 50, QColor(120, 120, 120, 128)); // Partial-alpha mixtures are not guessed.
        image.setPixelColor(51, 50, character.colors[1].color);
        const auto result = prepareCharacterRecolor(targets(project), settings(project)); QVERIFY(result.error.isEmpty());
        QCOMPARE(result.changedPixels, 6);
        for (int x = 1; x < 5; ++x) QCOMPARE(pixel(result.drawings[0].replacement, 10 + x, 10), image.pixel(10 + x, 10));
        for (const auto point : {QPoint(30, 30), QPoint(40, 40), QPoint(50, 50)})
            QCOMPARE(pixel(result.drawings[0].replacement, point.x(), point.y()), image.pixel(point));
    }
    void competingBoundaryPairsRemainUnchangedAndAreCounted() {
        auto project = fixture(); auto &character = project.characters[0];
        character.colors[0].color = QColor(20, 20, 20); character.colors[1].color = QColor(220, 220, 220);
        character.colors.append({id(), "Costume", CharacterColorCategory::Costume, QColor(60, 60, 60)});
        character.colors.append({id(), "Shadow", CharacterColorCategory::Shadow, QColor(180, 180, 180)});
        for (auto &variant : character.colorVariants) for (const auto &role : character.colors) variant.colors[role.id] = role.color;
        const std::array<QColor, 4> replacement{QColor(Qt::red), QColor(Qt::blue), QColor(Qt::green), QColor(Qt::cyan)};
        for (int i = 0; i < 4; ++i) character.colorVariants[0].colors[character.colors[i].id] = replacement[i];
        auto &image = project.shots[0].layers[0].tiles[{0, 0}]; image.fill(Qt::transparent);
        image.setPixelColor(10, 10, QColor(120, 120, 120));
        image.setPixelColor(9, 10, character.colors[0].color); image.setPixelColor(11, 10, character.colors[1].color);
        image.setPixelColor(10, 9, character.colors[2].color); image.setPixelColor(10, 11, character.colors[3].color);
        const auto result = prepareCharacterRecolor(targets(project), settings(project)); QVERIFY(result.error.isEmpty());
        QCOMPARE(result.changedPixels, 4); QCOMPARE(result.ambiguousEdgePixels, 1);
        QCOMPARE(pixel(result.drawings[0].replacement, 10, 10), image.pixel(10, 10));
    }
    void processesOverscanWithoutAllocatingSparseBounds() {
        auto project = fixture(); auto &layer = project.shots[0].layers[0];
        auto image = tile(); image.setPixelColor(3, 4, project.characters[0].colors[0].color);
        layer.tiles[{-128, -128}] = image; layer.tiles[{128, 128}] = image;
        const auto result = prepareCharacterRecolor(targets(project), settings(project)); QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.changedPixels, 4);
        QVERIFY(result.drawings[0].before.width() <= 2048); QVERIFY(result.drawings[0].before.height() <= 2048);
        QVERIFY(result.drawings[0].previewBounds.left() < 0); QVERIFY(result.drawings[0].previewBounds.right() > 32000);
        QVERIFY(pixel(result.drawings[0].replacement, 3, 4, {-128, -128}) != pixel(layer, 3, 4, {-128, -128}));
        QVERIFY(pixel(result.drawings[0].replacement, 3, 4, {128, 128}) != pixel(layer, 3, 4, {128, 128}));
    }
    void batchAcrossShotsIsOneUndoAndLeavesUnselectedDrawingsAlone() {
        auto project = fixture(true); auto extra = RasterLayer::create("Do not recolor"); extra.tiles = project.shots[0].layers[0].tiles;
        project.shots[0].layers.append(extra); Document document; document.load(project);
        const auto all = targets(project); const auto result = prepareCharacterRecolor({all[0], all[2]}, settings(project));
        QVERIFY(result.error.isEmpty()); QCOMPARE(document.history()->count(), 0); QCOMPARE(document.project().shots[0].layers[0].tiles, project.shots[0].layers[0].tiles);
        QString error; QVERIFY2(applyCharacterRecolor(&document, result, &error), qPrintable(error)); QCOMPARE(document.history()->count(), 1);
        QCOMPARE(document.history()->undoText(), QString("Recolor character drawings"));
        QCOMPARE(document.activeShotId(), project.shots[0].id); QCOMPARE(document.project().shots[0].layers[1].tiles, extra.tiles);
        QCOMPARE(document.project().shots[1].layers[0].tiles, result.drawings[1].replacement.tiles);
        document.history()->undo(); QCOMPARE(document.project().shots[0].layers[0].tiles, project.shots[0].layers[0].tiles);
        QCOMPARE(document.project().shots[1].layers[0].tiles, project.shots[1].layers[0].tiles);
        document.history()->redo(); QCOMPARE(document.project().shots[0].layers[0].tiles, result.drawings[0].replacement.tiles);
    }
    void stalePixelsPaletteDimensionsOrLocksRejectWholeBatch() {
        const auto project = fixture(true); const auto result = prepareCharacterRecolor(targets(project), settings(project));
        Document document; QString error;
        for (int mutation = 0; mutation < 4; ++mutation) {
            auto changed = project;
            if (mutation == 0) changed.shots[1].layers[0].tiles[{0, 0}].setPixelColor(9, 9, Qt::magenta);
            if (mutation == 1) changed.characters[0].colorVariants[0].colors[changed.characters[0].colors[0].id] = Qt::yellow;
            if (mutation == 2) changed.width = 65;
            if (mutation == 3) changed.shots[1].layers[0].locked = true;
            document.load(changed); QVERIFY(!applyCharacterRecolor(&document, result, &error)); QVERIFY(!error.isEmpty());
            QCOMPARE(document.history()->count(), 0); QCOMPARE(document.project().shots[0].layers[0].tiles, changed.shots[0].layers[0].tiles);
        }
    }
    void parentLocksNotesAndActiveGesturesAreBlockedButAlphaLockIsSafe() {
        auto project = fixture(); auto folder = RasterLayer::create("Paint"); folder.group = true;
        project.shots[0].layers[0].parentId = folder.id; project.shots[0].layers.append(folder);
        QCOMPARE(characterRecolorTargetError(project.shots[0], 0), QString());
        project.shots[0].layers[1].visible = false; QVERIFY(!characterRecolorTargetError(project.shots[0], 0).isEmpty());
        project.shots[0].layers[1].visible = true; project.shots[0].layers[1].locked = true; QVERIFY(!characterRecolorTargetError(project.shots[0], 0).isEmpty());
        project.shots[0].layers[1].locked = false; project.shots[0].layers[1].annotation = true; QVERIFY(!characterRecolorTargetError(project.shots[0], 0).isEmpty());
        project = fixture(); project.shots[0].layers[0].alphaLocked = true;
        Document document; document.load(project); const auto result = prepareCharacterRecolor(targets(project), settings(project));
        QString error; BrushSettings brush; QVERIFY(document.beginStroke({20, 20}, 1, brush, false));
        QVERIFY(!applyCharacterRecolor(&document, result, &error)); QVERIFY(document.strokeActive()); document.cancelStroke();
        QVERIFY2(applyCharacterRecolor(&document, result, &error), qPrintable(error)); QCOMPARE(document.history()->count(), 1);
        QCOMPARE(qAlpha(pixel(document.project().shots[0].layers[0], 5, 5)), 255);
    }
    void noChangePalettesAndNoMatchesDoNotAddHistory() {
        const auto project = fixture(); auto options = settings(project); options.targetVariantId = project.characters[0].colorVariants[1].id;
        Document document; document.load(project); auto result = prepareCharacterRecolor(targets(project), options);
        QVERIFY(result.error.isEmpty()); QCOMPARE(result.changedPixels, 0); QVERIFY(applyCharacterRecolor(&document, result)); QCOMPARE(document.history()->count(), 0);
        options = settings(project); for (auto &role : options.character.colors) role.color = Qt::yellow;
        for (auto &variant : options.character.colorVariants) variant.colors[options.character.colors[1].id] = variant.colors[options.character.colors[0].id];
        result = prepareCharacterRecolor(targets(project), options); QVERIFY(result.error.isEmpty()); QCOMPARE(result.matchedPixels, 0); QCOMPARE(result.changedPixels, 0);
    }
    void invalidTargetsCancellationAndBudgetsAreBounded() {
        const auto project = fixture(); auto chosen = targets(project); const auto options = settings(project);
        auto flag = std::make_shared<std::atomic_bool>(true);
        const auto stopped = prepareCharacterRecolor(chosen, options, flag); QVERIFY(stopped.cancelled); QVERIFY(stopped.drawings.isEmpty());
        chosen.append(chosen.first()); QVERIFY(!prepareCharacterRecolor(chosen, options).error.isEmpty());
        chosen = targets(project); auto &layer = chosen[0].original;
        const auto image = tile(); for (int i = 0; i < 1025; ++i) layer.tiles[{i % 128, i / 128}] = image;
        QVERIFY(prepareCharacterRecolor(chosen, options).error.contains("64 million"));
        chosen = targets(project); chosen[0].original.kind = LayerKind::Vector; QVERIFY(!prepareCharacterRecolor(chosen, options).error.isEmpty());
        auto bad = options; bad.targetVariantId = "missing"; QVERIFY(!prepareCharacterRecolor(targets(project), bad).error.isEmpty());
    }
    void previewMemoryBudgetAppliesAcrossManySparseDrawings() {
        const auto project = fixture(); auto options = settings(project); options.canvasSize = {8192, 8192};
        QVector<RecolorTarget> chosen;
        for (int index = 0; index < 64; ++index) { auto layer = RasterLayer::create(); chosen.append({project.shots[0].id, layer.id, layer.name, layer}); }
        const auto result = prepareCharacterRecolor(chosen, options); QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.drawings.size(), 64); qint64 beforePixels = 0, afterPixels = 0;
        for (const auto &drawing : result.drawings) {
            beforePixels += qint64(drawing.before.width()) * drawing.before.height();
            afterPixels += qint64(drawing.after.width()) * drawing.after.height();
        }
        QVERIFY(beforePixels <= 16LL * 1024 * 1024); QVERIFY(afterPixels <= 16LL * 1024 * 1024);
        chosen.append(chosen.first()); QVERIFY(!prepareCharacterRecolor(chosen, options).error.isEmpty());
    }
};
QTEST_MAIN(CharacterRecolorTests)
#include "test_character_recolor.moc"
