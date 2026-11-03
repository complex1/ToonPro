#include "brush_engine.h"
#include <QDir>
#include <QPainterPath>
#include <QtTest>

using namespace toon;
namespace {
QImage emptyTile() {
    QImage image(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    return image;
}
RasterLayer solid(const QString &name, QColor color, const QString &parent = {}, TileKey key = {0, 0}) {
    auto layer = RasterLayer::create(name);
    layer.parentId = parent;
    auto image = emptyTile(); image.fill(color); layer.tiles.insert(key, image);
    return layer;
}
RasterLayer group(const QString &name, qreal opacity = 1, const QString &parent = {}) {
    auto layer = RasterLayer::create(name); layer.group = true; layer.opacity = opacity; layer.parentId = parent;
    return layer;
}
QImage render(const QVector<RasterLayer> &layers, bool annotations = true, QPoint origin = {}, QSize size = {256, 256}) {
    QImage image(size, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    QPainter painter(&image); painter.translate(-origin); paintLayers(painter, layers, annotations);
    return image;
}
BrushDab stamp(QColor color) {
    BrushDab dab; dab.color = color; dab.mask = QImage(32, 32, QImage::Format_Grayscale8); dab.mask.fill(255);
    return dab;
}
RasterLayer imageLayer(const QString &name, const QImage &image, const QString &parent) {
    auto layer = RasterLayer::create(name); layer.parentId = parent;
    for (int y = 0; y < image.height(); y += tileSize) for (int x = 0; x < image.width(); x += tileSize) {
        auto tile = emptyTile(); QPainter painter(&tile); painter.drawImage(QPoint(-x, -y), image); painter.end();
        layer.tiles.insert({x / tileSize, y / tileSize}, tile);
    }
    return layer;
}
}

class LayerRenderingTest : public QObject {
    Q_OBJECT
private slots:
    void blendModesAffectPixelsAndInvalidateCompositeCache() {
        auto lower = solid("Base", QColor(120, 160, 200)), upper = solid("Light", QColor(200, 80, 120));
        QCOMPARE(render({lower, upper}).pixelColor(30, 30), QColor(200, 80, 120));
        upper.blendMode = LayerBlendMode::Multiply;
        auto color = render({lower, upper}).pixelColor(30, 30);
        QVERIFY(qAbs(color.red() - 94) <= 1); QVERIFY(qAbs(color.green() - 50) <= 1); QVERIFY(qAbs(color.blue() - 94) <= 1);
        upper.blendMode = LayerBlendMode::Screen;
        color = render({lower, upper}).pixelColor(30, 30);
        QVERIFY(qAbs(color.red() - 226) <= 1); QVERIFY(qAbs(color.green() - 190) <= 1); QVERIFY(qAbs(color.blue() - 226) <= 1);
        upper.opacity = .5;
        color = render({lower, upper}).pixelColor(30, 30);
        QVERIFY(qAbs(color.red() - 173) <= 1); QVERIFY(qAbs(color.green() - 175) <= 1); QVERIFY(qAbs(color.blue() - 213) <= 1);
        upper.visible = false; QCOMPARE(render({lower, upper}).pixelColor(30, 30), QColor(120, 160, 200));
    }
    void groupsBlendAsIsolatedUnits() {
        auto lower = solid("Background", QColor(120, 160, 200));
        auto folder = group("Colors", .5); folder.blendMode = LayerBlendMode::Multiply;
        auto child = solid("Base", QColor(200, 80, 120), folder.id);
        auto top = solid("Detail", QColor(60, 100, 140), folder.id);
        const auto color = render({lower, folder, child, top}).pixelColor(30, 30);
        // The top child completely occludes its sibling before group blending.
        QVERIFY(qAbs(color.red() - 74) <= 1); QVERIFY(qAbs(color.green() - 111) <= 1); QVERIFY(qAbs(color.blue() - 155) <= 1);
    }
    void clippedBlendingPreservesFractionalBaseAlphaAndNoOutsidePixels() {
        auto base = solid("Base", QColor(120, 160, 200, 128));
        base.tiles[{0, 0}].setPixelColor(30, 30, Qt::transparent);
        auto overlay = solid("Shadow", QColor(200, 80, 120)); overlay.clipped = true;
        overlay.blendMode = LayerBlendMode::Multiply; overlay.opacity = .5;
        const auto image = render({base, overlay}); const auto pixel = image.pixelColor(80, 80);
        QCOMPARE(pixel.alpha(), 128); QCOMPARE(image.pixelColor(30, 30).alpha(), 0);
        QVERIFY(qAbs(pixel.red() - 107) <= 2); QVERIFY(qAbs(pixel.green() - 105) <= 2); QVERIFY(qAbs(pixel.blue() - 147) <= 2);
        // The clipped tint must not alter either saved source image.
        QCOMPARE(base.tiles.first().pixelColor(80, 80).alpha(), 128);
        QCOMPARE(overlay.tiles.first().pixelColor(80, 80), QColor(200, 80, 120));
    }
    void allModesMatchPreviewAndRespectAnnotations_data() {
        QTest::addColumn<int>("pixelRatio");
        QTest::newRow("standard") << 1;
        QTest::newRow("retina") << 2;
    }
    void allModesMatchPreviewAndRespectAnnotations() {
        QFETCH(int, pixelRatio);
        auto lower = solid("Background", QColor(70, 140, 220, 200));
        auto upper = solid("Paint", QColor(180, 90, 120, 170)); upper.opacity = .7;
        for (int mode = int(LayerBlendMode::Normal); mode <= int(LayerBlendMode::Exclusion); ++mode) {
            upper.blendMode = LayerBlendMode(mode);
            RasterLayerPreview preview{upper.id, RasterLayer::create(), upper, QTransform::fromTranslate(24, 12)};
            QImage image(256 * pixelRatio, 256 * pixelRatio, QImage::Format_ARGB32_Premultiplied);
            image.setDevicePixelRatio(pixelRatio); image.fill(Qt::transparent);
            QPainter painter(&image); paintLayers(painter, {lower, upper}, true, &preview); painter.end();
            QCOMPARE(image.pixelColor(80 * pixelRatio, 80 * pixelRatio), render({lower, upper}).pixelColor(80, 80));
            QCOMPARE(image.pixelColor(5 * pixelRatio, 5 * pixelRatio), lower.tiles.first().pixelColor(5, 5));
            upper.annotation = true;
            QCOMPARE(render({lower, upper}, false).pixelColor(80, 80), lower.tiles.first().pixelColor(80, 80));
            upper.annotation = false;
            upper.clipped = true;
            image.fill(Qt::transparent); painter.begin(&image); paintLayers(painter, {lower, upper}, true, &preview); painter.end();
            QCOMPARE(image.pixelColor(80 * pixelRatio, 80 * pixelRatio), render({lower, upper}).pixelColor(80, 80));
            upper.clipped = false;
        }
    }
    void layerThumbnailFitsOwnPixelsAndUpdatesWithoutCompositingOtherLayers() {
        auto layer = RasterLayer::create("Detail"); auto tile = emptyTile();
        { QPainter painter(&tile); painter.fillRect(100, 120, 40, 20, QColor(220, 60, 80, 128)); }
        layer.tiles[{0, 0}] = tile; layer.visible = false; layer.opacity = 0; layer.blendMode = LayerBlendMode::Multiply;
        const auto first = renderLayerThumbnail(layer, {0, 0, 1920, 1080}, {32, 32});
        QCOMPARE(first.size(), QSize(32, 32)); QCOMPARE(first.pixelColor(16, 16).alpha(), 128);
        QCOMPARE(first.pixelColor(16, 1).alpha(), 0);
        layer.locked = true; layer.rowColor = QColor("#ff0000");
        QCOMPARE(renderLayerThumbnail(layer, {0, 0, 1920, 1080}, {32, 32}), first);
        { QPainter painter(&layer.tiles[{0, 0}]); painter.fillRect(100, 120, 40, 20, Qt::blue); }
        const auto changed = renderLayerThumbnail(layer, {0, 0, 1920, 1080}, {32, 32});
        QCOMPARE(changed.pixelColor(16, 16), QColor(Qt::blue)); QVERIFY(changed != first);
        QCOMPARE(renderLayerThumbnail(layer, {500, 500, 200, 200}, {32, 32}).pixelColor(16, 16).alpha(), 0);
        layer.group = true; QVERIFY(renderLayerThumbnail(layer, {0, 0, 1920, 1080}, {32, 32}).isNull());
    }
    void positionLocksAreInheritedWithoutPreventingPainting() {
        auto folder = group("Character"), child = solid("Colors", Qt::red, folder.id);
        QVector<RasterLayer> layers{folder, child};
        QVERIFY(!layerPositionLocked(layers, 1)); QVERIFY(layerCanPaint(layers, 1));
        layers[0].positionLocked = true; QVERIFY(layerPositionLocked(layers, 1)); QVERIFY(layerCanPaint(layers, 1));
        layers[0].positionLocked = false; layers[1].positionLocked = true; QVERIFY(layerPositionLocked(layers, 1));
        layers[1].positionLocked = false; layers[0].locked = true;
        QVERIFY(layerPositionLocked(layers, 1)); QVERIFY(!layerCanPaint(layers, 1));
    }
    void groupsIsolateOpacityAndRetainSiblingOrder() {
        auto outer = group("Character", .5), inner = group("Face", .5, outer.id);
        auto red = solid("Base", Qt::red, inner.id), blue = solid("Detail", Qt::blue, inner.id);
        { QPainter painter(&blue.tiles[{0, 0}]); painter.setCompositionMode(QPainter::CompositionMode_Clear); painter.fillRect(0, 0, 128, 256, Qt::transparent); }
        // Flat storage may interleave children with other nodes; parent IDs own the hierarchy.
        QVector<RasterLayer> layers{red, outer, blue, inner};
        auto image = render(layers);
        QCOMPARE(image.pixelColor(30, 30).alpha(), image.pixelColor(200, 30).alpha());
        QVERIFY(qAbs(image.pixelColor(30, 30).alpha() - 64) <= 1);
        QCOMPARE(image.pixelColor(30, 30).red(), 255); QCOMPARE(image.pixelColor(200, 30).blue(), 255);
        layers[1].visible = false; QCOMPARE(render(layers).pixelColor(30, 30).alpha(), 0);
    }
    void clippingRunPreservesBaseAlphaAndAppliesBaseOpacityOnce() {
        auto base = solid("Skin", QColor(255, 0, 0, 128)); base.opacity = .5;
        base.tiles[{0, 0}].setPixelColor(30, 30, Qt::transparent);
        auto green = solid("Shadow", Qt::green); green.clipped = true;
        auto blue = solid("Highlight", Qt::blue); blue.clipped = true; blue.opacity = .5;
        QVector<RasterLayer> layers{base, green, blue}; const auto before = layers;
        auto image = render(layers); const auto pixel = image.pixelColor(100, 100);
        QVERIFY(qAbs(pixel.alpha() - 64) <= 1); QVERIFY(qAbs(pixel.green() - 128) <= 3); QVERIFY(qAbs(pixel.blue() - 128) <= 3);
        QCOMPARE(pixel.red(), 0); QCOMPARE(image.pixelColor(30, 30).alpha(), 0);
        // Compositing must detach its images, never edit saved layer pixels.
        for (int i = 0; i < layers.size(); ++i) QCOMPARE(layers[i].tiles, before[i].tiles);
    }
    void hiddenAndMissingBasesAreClippingBarriers() {
        auto lower = solid("Background", Qt::blue), hidden = solid("Hidden base", Qt::red);
        hidden.visible = false;
        auto clipped = solid("Clipped", Qt::green); clipped.clipped = true;
        QCOMPARE(render({lower, hidden, clipped}).pixelColor(40, 40), QColor(Qt::blue));
        QCOMPARE(render({clipped}).pixelColor(40, 40).alpha(), 0);
        auto folder = group("Empty group");
        QCOMPARE(render({lower, folder, clipped}).pixelColor(40, 40), QColor(Qt::blue));
        folder.visible = false;
        auto child = solid("Child", Qt::red, folder.id);
        QCOMPARE(render({lower, folder, child, clipped}).pixelColor(40, 40), QColor(Qt::blue));
    }
    void clippingStaysWithinSiblingsAndCanUseGroupBase() {
        auto folder = group("Face", .5), child = solid("Skin", QColor(255, 0, 0, 128), folder.id);
        auto clipped = solid("Group tint", Qt::green); clipped.clipped = true;
        auto image = render({child, folder, clipped});
        QVERIFY(qAbs(image.pixelColor(40, 40).alpha() - 64) <= 1); QCOMPARE(image.pixelColor(40, 40).green(), 255);
        // The root-level clip cannot borrow a child as a base independently.
        auto orphan = solid("Orphan child clip", Qt::blue, folder.id); orphan.clipped = true;
        image = render({folder, orphan, clipped}); QCOMPARE(image.pixelColor(40, 40).alpha(), 0);
        // A child's clipped run must not use an earlier root layer as its base.
        auto rootBase = solid("Root base", Qt::red);
        image = render({rootBase, folder, orphan}); QCOMPARE(image.pixelColor(40, 40), QColor(Qt::red));
    }
    void annotationsExcludeWholeGroupsAndTheirClippingRuns() {
        auto lower = solid("Background", Qt::blue), notes = group("Notes"); notes.annotation = true;
        auto child = solid("Note child", Qt::red, notes.id), clipped = solid("Clipped note tint", Qt::green); clipped.clipped = true;
        QVector<RasterLayer> layers{lower, notes, child, clipped};
        QCOMPARE(render(layers, true).pixelColor(40, 40), QColor(Qt::green));
        QCOMPARE(render(layers, false).pixelColor(40, 40), QColor(Qt::blue));
        layers = {lower, clipped}; layers[1].annotation = true;
        QCOMPARE(render(layers, false).pixelColor(40, 40), QColor(Qt::blue));
        auto nested = group("Nested", 1, notes.id); child.parentId = nested.id;
        QCOMPARE(render({lower, notes, nested, child}, false).pixelColor(40, 40), QColor(Qt::blue));
    }
    void negativeSparseTilesKeepClipsAndSeamsAligned() {
        auto folder = group("Overscan", .5), base = solid("Base", QColor(255, 0, 0, 128), folder.id, {-1, 0});
        base.tiles.insert({0, 0}, base.tiles.first());
        auto clip = solid("Shadow", Qt::green, folder.id, {-1, 0}); clip.clipped = true;
        clip.tiles.insert({0, 0}, clip.tiles.first()); clip.tiles.insert({3, 0}, clip.tiles.first());
        const auto image = render({folder, base, clip}, true, {-256, 0}, {1280, 256});
        for (int x : {0, 128, 255, 256, 300, 511}) {
            QCOMPARE(image.pixelColor(x, 50), image.pixelColor(10, 50));
            QVERIFY(qAbs(image.pixelColor(x, 50).alpha() - 64) <= 1);
        }
        QCOMPARE(image.pixelColor(1100, 50).alpha(), 0);
        QImage scaled(320, 160, QImage::Format_ARGB32_Premultiplied); scaled.fill(Qt::transparent);
        QPainter painter(&scaled); painter.scale(.625, .625); painter.translate(256, 0); paintLayers(painter, {folder, base, clip}, true); painter.end();
        for (int x : {158, 159, 160, 161}) QCOMPARE(scaled.pixelColor(x, 30), scaled.pixelColor(50, 30));
    }
    void alphaLockPreservesFractionalCoverage_data() {
        QTest::addColumn<int>("mode");
        for (int mode = int(BrushBlend::Normal); mode <= int(BrushBlend::Dissolve); ++mode)
            QTest::newRow(qPrintable(QString::number(mode))) << mode;
    }
    void alphaLockPreservesFractionalCoverage() {
        QFETCH(int, mode);
        auto original = RasterLayer::create("Ink"); original.alphaLocked = true;
        auto tile = emptyTile(); tile.setPixelColor(250, 100, QColor(160, 100, 50, 128)); tile.setPixelColor(251, 100, QColor(160, 100, 50, 64));
        original.tiles.insert({0, 0}, tile); auto output = original; RasterLayer accumulation;
        const auto dab = stamp(QColor(80, 140, 220));
        QVERIFY(applyBrushDab(output, accumulation, original, dab, {252, 100}, {}, BrushBlend(mode), false, {0, 0, 640, 480}, 0, 17));
        QCOMPARE(output.tiles.size(), 1); QCOMPARE(accumulation.tiles.size(), 1);
        for (int y = 0; y < 256; ++y) for (int x = 0; x < 256; ++x)
            QCOMPARE(output.tiles.first().pixelColor(x, y).alpha(), tile.pixelColor(x, y).alpha());
        const auto color = output.tiles.first().pixelColor(250, 100);
        if (mode == int(BrushBlend::Normal)) { QVERIFY(qAbs(color.red() - 80) <= 2); QVERIFY(qAbs(color.blue() - 220) <= 2); }
        if (mode == int(BrushBlend::Multiply)) { QVERIFY(qAbs(color.red() - 50) <= 3); QVERIFY(qAbs(color.blue() - 43) <= 3); }
    }
    void alphaLockRespectsFlowAndEraserWithoutCreatingTiles() {
        auto original = RasterLayer::create("Ink"); original.alphaLocked = true;
        auto tile = emptyTile(); tile.setPixelColor(100, 100, QColor(255, 0, 0, 80)); original.tiles.insert({0, 0}, tile);
        auto output = original; RasterLayer accumulation; auto dab = stamp(Qt::blue); dab.flow = .2; dab.opacity = .4;
        for (int i = 0; i < 80; ++i) QVERIFY(applyBrushDab(output, accumulation, original, dab, {100, 100}, {}, BrushBlend::Normal, false, {0, 0, 640, 480}, 0, 17));
        auto color = output.tiles.first().pixelColor(100, 100); QCOMPARE(color.alpha(), 80);
        QVERIFY(qAbs(color.red() - 153) <= 4); QVERIFY(qAbs(color.blue() - 102) <= 4);
        const auto before = output.tiles;
        QVERIFY(applyBrushDab(output, accumulation, original, dab, {100, 100}, {}, BrushBlend::Normal, true, {0, 0, 640, 480}, 0, 17));
        QCOMPARE(output.tiles, before);
        accumulation.tiles.clear();
        QVERIFY(applyBrushDab(output, accumulation, original, dab, {150, 150}, {}, BrushBlend::Normal, false, {0, 0, 640, 480}, 0, 17));
        QVERIFY(accumulation.tiles.isEmpty()); QCOMPARE(output.tiles, before);
        QVERIFY(applyBrushDab(output, accumulation, original, dab, {400, 100}, {}, BrushBlend::Normal, false, {0, 0, 640, 480}, 0, 17));
        QCOMPARE(output.tiles.size(), 1); QVERIFY(accumulation.tiles.isEmpty());
    }
    void characterStackStudy() {
        auto character = group("Character");
        QImage flats({256, 336}, QImage::Format_ARGB32_Premultiplied); flats.fill(Qt::transparent);
        QPainterPath coat; coat.moveTo(45, 327); coat.lineTo(55, 254); coat.quadTo(65, 231, 106, 221); coat.lineTo(153, 221); coat.quadTo(192, 231, 202, 254); coat.lineTo(212, 327); coat.closeSubpath();
        QPainterPath hair; hair.moveTo(64, 171); hair.lineTo(47, 132); hair.lineTo(59, 79); hair.lineTo(48, 68); hair.lineTo(89, 60); hair.lineTo(84, 38); hair.lineTo(126, 53); hair.lineTo(148, 27); hair.lineTo(161, 48); hair.quadTo(211, 65, 207, 118); hair.lineTo(188, 176); hair.closeSubpath();
        QPainterPath fringe; fringe.moveTo(70, 111); fringe.lineTo(80, 76); fringe.lineTo(179, 75); fringe.lineTo(194, 130); fringe.lineTo(167, 112); fringe.lineTo(172, 142); fringe.lineTo(134, 107); fringe.lineTo(128, 130); fringe.lineTo(109, 105); fringe.lineTo(86, 129); fringe.lineTo(86, 106); fringe.closeSubpath();
        {
            QPainter painter(&flats); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(Qt::NoPen);
            painter.setBrush(QColor("#343b62")); painter.drawPath(hair); painter.setBrush(QColor("#bcd3de")); painter.drawPath(coat);
            painter.setBrush(QColor("#f3cbb9")); painter.drawRoundedRect(QRectF(109, 201, 41, 47), 8, 8); painter.drawEllipse(QRectF(70, 93, 123, 124));
            painter.setBrush(QColor("#343b62")); painter.drawPath(fringe);
        }
        auto colors = imageLayer("Colors", flats, character.id); colors.alphaLocked = true;
        QImage shadow(flats.size(), flats.format()); shadow.fill(Qt::transparent);
        { QPainter painter(&shadow); painter.setPen(Qt::NoPen); painter.setBrush(QColor("#674b89")); QPolygonF shape{{136, 0}, {256, 0}, {256, 336}, {143, 336}, {127, 245}, {152, 211}, {165, 146}, {118, 90}}; painter.drawPolygon(shape); }
        auto shadows = imageLayer("Shadows", shadow, character.id); shadows.clipped = true; shadows.opacity = .42;
        QImage lines(flats.size(), flats.format()); lines.fill(Qt::transparent);
        {
            QPainter painter(&lines); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(QPen(QColor("#202b43"), 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); painter.setBrush(Qt::NoBrush);
            painter.drawPath(coat); painter.drawPath(hair); painter.drawPath(fringe);
            painter.drawArc(QRectF(70, 93, 123, 124), 184 * 16, 172 * 16);
            painter.drawLine(QPointF(88, 152), QPointF(116, 147)); painter.drawLine(QPointF(145, 147), QPointF(176, 152));
            painter.setBrush(QColor("#88bfd0")); painter.drawEllipse(QRectF(97, 151, 12, 17)); painter.drawEllipse(QRectF(155, 151, 12, 17));
            painter.setBrush(QColor("#202b43")); painter.drawEllipse(QRectF(101, 154, 4, 11)); painter.drawEllipse(QRectF(159, 154, 4, 11));
            painter.drawLine(QPointF(132, 165), QPointF(128, 179)); painter.drawLine(QPointF(120, 194), QPointF(144, 194));
            painter.setBrush(Qt::NoBrush); painter.drawLine(QPointF(109, 222), QPointF(128, 247)); painter.drawLine(QPointF(151, 222), QPointF(128, 247)); painter.drawLine(QPointF(128, 247), QPointF(128, 327));
        }
        auto ink = imageLayer("Ink", lines, character.id);
        QImage study(960, 430, QImage::Format_ARGB32_Premultiplied); study.fill(QColor("#20252f"));
        QPainter painter(&study); painter.setRenderHint(QPainter::Antialiasing);
        const QStringList labels{"Flat colors", "Clipped shadows", "Group opacity 50%"};
        for (int i = 0; i < 3; ++i) {
            painter.setPen(Qt::NoPen); painter.setBrush(QColor("#eef0f1")); painter.drawRoundedRect(QRectF(18 + i * 320, 20, 284, 355), 8, 8);
            shadows.visible = i != 0; character.opacity = i == 2 ? .5 : 1;
            painter.save(); painter.translate(32 + i * 320, 27); paintLayers(painter, {character, colors, shadows, ink}, true); painter.restore();
            painter.setPen(QColor("#e4e8f0")); painter.setFont(QFont("Sans Serif", 15)); painter.drawText(QRectF(i * 320, 389, 320, 30), Qt::AlignCenter, labels[i]);
        }
        painter.end(); QDir().mkpath("test-artifacts"); QVERIFY(study.save("test-artifacts/layer-character-study.png"));
    }
};
QTEST_MAIN(LayerRenderingTest)
#include "test_layer_rendering.moc"
