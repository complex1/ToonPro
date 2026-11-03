#include "connected_brush.h"
#include "catalog_brushes.h"
#include "document.h"
#include "object_tools.h"
#include "raster_tools.h"
#include "vector_brush.h"
#include <QDir>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace toon;
namespace {
Project project(bool vector = false) {
    auto p = Project::create(); p.width = 640; p.height = 400; p.transparent = true;
    p.shots[0].sheet.overscan = 0;
    p.shots[0].layers[0].kind = vector ? LayerKind::Vector : LayerKind::Raster;
    return p;
}
const RasterLayer &layer(const Document &d) { return d.project().shots[0].layers[0]; }
QImage motif(bool alternate = false) {
    QImage image(64, 32, QImage::Format_Grayscale8); image.fill(0);
    QPainter p(&image); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Qt::white, 4));
    p.drawLine(QPointF(0, 16), QPointF(64, 16));
    p.drawEllipse(QRectF(4, alternate ? 3 : 9, 52, 20));
    return image;
}
BrushSettings brush() {
    BrushSettings b; b.size = 32; b.pressureSize = false; b.spacing = 4;
    b.connected.mode = BrushConnectedMode::Ribbon; b.tipMask = motif();
    b.lockRandomSeed = true; b.randomSeed = 513; return b;
}
void draw(Document &doc, const BrushSettings &b) {
    BrushInput input; input.pressure = .6; input.speed = 100;
    QVERIFY(doc.beginStroke({40, 130}, input, b, false));
    for (int i = 1; i <= 40; ++i) {
        input.pressure = .5 + .4 * i / 40.; input.speed = 100 + i * 30;
        doc.continueStroke({40 + i * 13., 130 + std::sin(i * .12) * 70}, input);
    }
    doc.endStroke();
}
QImage render(const RasterLayer &layer) {
    QImage image(640, 400, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    QPainter painter(&image); paintRasterLayerPixels(painter, layer); return image;
}
}
class ConnectedBrushTest : public QObject {
    Q_OBJECT
private slots:
    void translucentSegmentBoundariesPaintOnce() {
        auto b=brush();b.tipMask.fill(255);b.size=32;b.opacity=.4;
        Document doc;doc.load(project());
        QVERIFY(doc.beginStroke({40,40},1,b,false));
        for(int i=1;i<=120;++i)doc.continueStroke({40.+i,40.+i},1);
        doc.endStroke();const auto image=render(layer(doc));
        for(int i=45;i<155;++i)for(int j=-4;j<=4;++j)
            QVERIFY2(std::abs(qAlpha(image.pixel(i,i+j))-102)<=1,"Shared segment edges must not accumulate twice");
    }
    void alternatingRepeatsReflectWithoutOppositeEdgeBleed() {
        auto b=brush();b.size=32;b.connected.mode=BrushConnectedMode::Repeat;b.connected.flipAlternate=true;
        b.tipMask=QImage(8,8,QImage::Format_Grayscale8);
        for(int y=0;y<8;++y)for(int x=0;x<8;++x)b.tipMask.scanLine(y)[x]=qRound(x*255./7);
        Document doc;doc.load(project());QVERIFY(doc.beginStroke({32,100},1,b,false));doc.continueStroke({200,100},1);doc.endStroke();
        const auto image=render(layer(doc));
        for(int x:{63,64,127,128})QVERIFY(qAlpha(image.pixel(x,100))>=254);
        for(int x:{95,96,159,160})QVERIFY(qAlpha(image.pixel(x,100))<=1);
    }
    void sharpCornerHasOuterBevel_data() {
        QTest::addColumn<bool>("mirror");QTest::newRow("left")<<false;QTest::newRow("right")<<true;
    }
    void sharpCornerHasOuterBevel() {
        QFETCH(bool,mirror);
        auto b=brush();b.tipMask.fill(255);b.size=80;
        Document doc;doc.load(project());
        QVERIFY(doc.beginStroke({80,220},1,b,false));doc.continueStroke({300,220},1);doc.continueStroke({300,mirror?360.:80.},1);doc.endStroke();
        const auto image=render(layer(doc));
        for(int y=225;y<240;++y)for(int x=305;x<320;++x)
            QVERIFY2(qAlpha(image.pixel(x,mirror?439-y:y))>250,"Sharp turn leaves a missing outer join");
    }
    void cornerBridgeKeepsTransparentMotifPixelsAndVectorReplay() {
        auto b=brush();b.size=80;b.tipMask.fill(0);
        for(int y=12;y<20;++y)std::fill_n(b.tipMask.scanLine(y),b.tipMask.width(),uchar(255));
        Document raster,vector;raster.load(project());vector.load(project(true));
        for(auto *doc:{&raster,&vector}){
            QVERIFY(doc->beginStroke({80,220},1,b,false));doc->continueStroke({300,220},1);doc->continueStroke({300,80},1);doc->endStroke();
        }
        const auto image=render(layer(raster));
        QVERIFY(qAlpha(image.pixel(302,222))>250);
        QCOMPARE(qAlpha(image.pixel(315,235)),0);
        QCOMPARE(layer(raster).tiles,layer(vector).tiles);
        auto rebuilt=layer(vector);clearObjectLayerCaches();clearVectorBrushReplayCache();QString error;
        QVERIFY2(rebuildObjectLayer(rebuilt,vector.drawingBounds(),maxProjectTiles,&error),qPrintable(error));
        QCOMPARE(rebuilt.tiles,layer(raster).tiles);
    }
    void catalogQualitySheets() {
        const auto output=qEnvironmentVariable("TOON_CONNECTED_QUALITY");
        if(output.isEmpty())QSKIP("Set TOON_CONNECTED_QUALITY for authored brush review sheets");
        QVERIFY(QDir().mkpath(output));int row=0,page=0;QImage sheet;QPainter painter;
        for(const auto &preset:catalogBrushPresets()) {
            if(preset.brush.connected.mode==BrushConnectedMode::Off)continue;
            if(row==0){sheet=QImage(1280,1200,QImage::Format_ARGB32_Premultiplied);sheet.fill(Qt::white);painter.begin(&sheet);}
            auto b=preset.brush;b.size=80;b.lockRandomSeed=true;b.randomSeed=513;
            for(int column=0;column<2;++column){Document doc;doc.load(project());
                if(column==0){QVERIFY(doc.beginStroke({40,180},1,b,false));for(int i=1;i<=1000;++i)doc.continueStroke({40+i*.56,180+55*std::sin(i*.007)},1);}
                else {QVERIFY(doc.beginStroke({40,210},1,b,false));doc.continueStroke({240,210},1);doc.continueStroke({240,95},1);doc.continueStroke({570,95},1);}
                doc.endStroke();painter.drawImage(QRect(column*640,row*240+28,640,210),render(layer(doc)),QRect(0,40,640,260));
            }
            painter.setPen(Qt::black);painter.drawText(12,row*240+20,preset.name+"  /  slow curve                         sharp corners");
            if(++row==5){painter.end();QVERIFY(sheet.save(output+QString("/sheet-%1.png").arg(++page)));row=0;}
        }
        if(row){painter.end();QVERIFY(sheet.save(output+QString("/sheet-%1.png").arg(++page)));}
    }
    void narrowBandsMatchFullTileReference_data() {
        QTest::addColumn<int>("size");QTest::addColumn<int>("variant");
        for(int size:{128,512,1024})for(int variant=0;variant<3;++variant)
            QTest::newRow(qPrintable(QString("%1-%2").arg(size).arg(variant)))<<size<<variant;
    }
    void narrowBandsMatchFullTileReference() {
        QFETCH(int,size);QFETCH(int,variant);auto b=brush();b.size=size;b.opacity=.45;
        if(variant==1){b.colorTip=QImage(64,32,QImage::Format_ARGB32_Premultiplied);b.colorTip.fill(QColor(180,90,20,130));b.tipMask={};b.colorDynamics.tintAmount=.3;}
        if(variant==2){b.texture.enabled=true;b.texture.pattern=motif();b.texture.eachTip=false;b.surface.kind=BrushSurfaceKind::Dots;b.surface.density=.4;b.symmetry.mode=SymmetryMode::Vertical;}
        RasterLayer narrow,wide,original,paintNarrow,paintWide;ConnectedBrushState stateNarrow,stateWide;
        QPainterPath selection;selection.addEllipse(QRectF(15,20,610,360));
        QTransform transform;if(variant==1){transform.translate(10,5);transform.shear(.1,.05);}
        const QVector<QPointF> path{{60,120},{85,135},{105,130},{120,155},{110,145},{145,170},{200,175}};
        int index=0;
        for(const auto &point:path){BrushInput in;in.pressure=.6+index*.04;
            QVERIFY(appendConnectedBrush(narrow,paintNarrow,original,stateNarrow,b,point,in,index,513,{640,400},{0,0,640,400},maxProjectTiles,false,&selection,nullptr,transform,16));
            QVERIFY(appendConnectedBrush(wide,paintWide,original,stateWide,b,point,in,index,513,{640,400},{0,0,640,400},maxProjectTiles,false,&selection,nullptr,transform,tileSize));++index;}
        QVERIFY(!narrow.tiles.isEmpty());QCOMPARE(narrow.tiles,wide.tiles);QCOMPARE(paintNarrow.tiles,paintWide.tiles);
    }
    void phaseIsIndependentOfPackets() {
        auto b = brush(); b.connected.mode = BrushConnectedMode::Repeat; b.connected.repeatLength = 2;
        Document sparse, dense; sparse.load(project()); dense.load(project());
        QVERIFY(sparse.beginStroke({32, 100}, 1, b, false)); sparse.continueStroke({600, 100}, 1); sparse.endStroke();
        QVERIFY(dense.beginStroke({32, 100}, 1, b, false));
        for (int x = 36; x <= 600; x += 4) dense.continueStroke({qreal(x), 100}, 1);
        dense.endStroke(); QCOMPARE(layer(sparse).tiles, layer(dense).tiles);
        const auto pixels = render(layer(dense));
        for (int x = 33; x < 600; ++x) QVERIFY2(qAlpha(pixels.pixel(x, 100)) > 200, qPrintable(QString::number(x)));
    }
    void slowAndFastCurveSampling_data() {
        QTest::addColumn<QString>("preset");
        for (const char *id : {"fixture", "solid", "fence-posts", "roof-tile-border", "hanging-ivy", "chain-border", "cable-bundle", "smoke-trail"})
            QTest::newRow(id) << QString(id);
    }
    void slowAndFastCurveSampling() {
        QFETCH(QString, preset);
        auto b = brush(); b.size = 60;
        if(preset == "solid") b.tipMask.fill(255);
        else if(preset != "fixture") { bool found=false;for(const auto &p:catalogBrushPresets())if(p.id.endsWith("/"+preset)){b=p.brush;found=true;break;}QVERIFY(found); }

        QVector<QPointF> path;
        for (int i=0;i<=40;++i) path.append({40+i*13.,170+std::sin(i*.12)*70});
        QVector<QImage> images;
        auto stroke=[&](int divisions) {
            Document doc; doc.load(project()); QVERIFY(doc.beginStroke(path[0],1,b,false));
            for(int i=1;i<path.size();++i) for(int j=1;j<=divisions;++j)
                doc.continueStroke(path[i-1]+(path[i]-path[i-1])*(qreal(j)/divisions),1);
            doc.endStroke(); images.append(render(layer(doc)));
        };
        stroke(1); stroke(100);
        const auto output=qEnvironmentVariable("TOON_SLOW_CONNECTED_PREVIEW");
        if(!output.isEmpty()) { QImage sheet(640,800,QImage::Format_ARGB32_Premultiplied);sheet.fill(Qt::white);QPainter p(&sheet);p.drawImage(0,0,images[0]);p.drawImage(0,400,images[1]);p.end();QVERIFY(sheet.save(output+"-"+preset+".png")); }
        qint64 difference=0,total=0;
        for(int y=0;y<400;++y)for(int x=0;x<640;++x){const int a=qAlpha(images[0].pixel(x,y)),b=qAlpha(images[1].pixel(x,y));difference+=std::abs(a-b);total+=qMax(a,b);}
        QVERIFY2(double(difference)/total<.08,qPrintable(QString("Sample-density difference %1").arg(double(difference)/total)));
        if(preset=="solid") for(const auto &image:images) {
            // Stay clear of the angled butt caps at the stroke endpoints.
            for(int i=3;i<path.size()-2;++i) for(qreal t=.1;t<1;t+=.1) {
                const auto p=path[i-1]+(path[i]-path[i-1])*t;
                for(int offset=-18;offset<=18;++offset)
                    QVERIFY2(qAlpha(image.pixel(qRound(p.x()),qRound(p.y())+offset))>250,"Connected strip has an interior gap");
            }
        }
        images.clear();
    }
    void minifiedCablesRemainContinuousAlongCurve() {
        auto b = brush(); b.size = 24; b.tipMask = QImage(256, 128, QImage::Format_Grayscale8); b.tipMask.fill(0);
        for (const int center : {35, 48, 64, 80, 93}) for (int y = center - 1; y <= center + 1; ++y)
            std::fill_n(b.tipMask.scanLine(y), b.tipMask.width(), uchar(255));
        Document doc; doc.load(project()); draw(doc, b); const auto pixels = render(layer(doc));
        for (int x = 55; x < 545; ++x) {
            int alpha = 0; for (int y = 0; y < pixels.height(); ++y) alpha += qAlpha(pixels.pixel(x, y));
            QVERIFY2(alpha > 400, qPrintable(QString("Cable coverage fell to %1 in column %2").arg(alpha).arg(x)));
        }
    }
    void rasterVectorAndColdReplay_data() {
        QTest::addColumn<int>("variant");
        for (int i = 0; i < 5; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void rasterVectorAndColdReplay() {
        QFETCH(int, variant); auto b = brush();
        if (variant == 1) { b.connected.mode = BrushConnectedMode::Repeat; b.connected.flipAlternate = true; b.opacity = .45; }
        if (variant == 2) { b.pressureSize = true; b.speedDynamics.enabled = true; b.symmetry.mode = SymmetryMode::Vertical; }
        if (variant == 3) {
            QVERIFY(setBrushTipImages(b, {motif(), motif(true)})); b.tipOrder = BrushTipOrder::Ordered;
        }
        if (variant == 4) {
            b.colorTip = QImage(64, 32, QImage::Format_ARGB32_Premultiplied); b.colorTip.fill(QColor(190, 40, 80, 100));
            b.tipMask = {}; b.strokeOpacity = .6;
            b.colorDynamics.enabled = true; b.colorDynamics.applyToColorTip = true;
            b.colorDynamics.hue = .2; b.colorDynamics.saturation = .3; b.colorDynamics.tintAmount = .4;
        }
        Document raster, vector; raster.load(project()); vector.load(project(true));
        QSignalSpy warnings(&vector, &Document::warning); draw(raster, b); draw(vector, b);
        QVERIFY2(warnings.isEmpty(), warnings.isEmpty() ? "" : qPrintable(warnings.first().first().toString()));
        QCOMPARE(layer(vector).objects.size(), 1); QCOMPARE(layer(vector).tiles, layer(raster).tiles);
        auto rebuilt = layer(vector); clearObjectLayerCaches(); clearVectorBrushReplayCache(); QString error;
        QVERIFY2(rebuildObjectLayer(rebuilt, vector.drawingBounds(), maxProjectTiles, &error), qPrintable(error));
        QCOMPARE(rebuilt.tiles, layer(raster).tiles);
        QTemporaryDir directory; const auto path = directory.filePath("connected.toon");
        QVERIFY2(saveProject(vector.project(), path, &error), qPrintable(error));
        const auto loaded = loadProject(path, &error); QVERIFY2(loaded.has_value(), qPrintable(error));
        rebuilt = loaded->shots[0].layers[0]; clearObjectLayerCaches(); clearVectorBrushReplayCache();
        QVERIFY2(rebuildObjectLayer(rebuilt, vector.drawingBounds(), maxProjectTiles, &error), qPrintable(error));
        QCOMPARE(rebuilt.tiles, layer(raster).tiles);
    }
    void selectionAlphaLockAndUndo() {
        Document doc; doc.load(project());
        auto b = brush(); draw(doc, b); const auto before = layer(doc).tiles;
        QPainterPath selected; selected.addRect({0, 0, 260, 400}); doc.setRasterSelection(selected);
        QVERIFY(RasterTools::featherSelection(&doc, 4)); doc.setLayerAlphaLocked(true);
        b.color = Qt::red; b.size = 70; draw(doc, b);
        const auto after = render(layer(doc)); RasterLayer previous; previous.tiles = before; const auto old = render(previous);
        for (int y = 0; y < after.height(); ++y) for (int x = 0; x < after.width(); ++x) {
            QCOMPARE(qAlpha(after.pixel(x, y)), qAlpha(old.pixel(x, y)));
            if (x > 265) QCOMPARE(after.pixel(x, y), old.pixel(x, y));
        }
        doc.history()->undo(); QCOMPARE(layer(doc).tiles, before);
        doc.history()->redo(); QCOMPARE(render(layer(doc)), after);
    }
    void pickingUsesMappedTransparencyAndEditedGeometry() {
        Document doc; doc.load(project(true)); auto b = brush(); b.connected.mode = BrushConnectedMode::Repeat;
        QVERIFY(doc.beginStroke({40, 100}, 1, b, false)); doc.continueStroke({540, 100}, 1); doc.endStroke();
        auto object = layer(doc).objects.first(); QPainterPath hit, miss;
        hit.addRect({120, 99, 2, 2}); miss.addRect({120, 84, 2, 2});
        QVERIFY(vectorBrushIntersects(object, hit)); QVERIFY(!vectorBrushIntersects(object, miss));
        object.nodes.last().anchor.setY(200); object.nodes.last().inControl = object.nodes.last().outControl = object.nodes.last().anchor;
        object.transform.translate(5, 10); object.strokeWidth = 48;
        RasterLayer rebuilt; QString error; QVERIFY2(replayVectorBrushStroke(rebuilt, object, doc.drawingBounds(), maxProjectTiles, &error), qPrintable(error));
        QVERIFY(!rebuilt.tiles.isEmpty()); QVERIFY(rebuilt.tiles != layer(doc).tiles);
        QPainterPath whole; whole.addRect({0, 0, 640, 400}); QVERIFY(vectorBrushIntersects(object, whole));
    }
    void normalTonePickingMatchesVisiblePixels() {
        Document doc; doc.load(project(true)); BrushSettings b; b.size = 96; b.pressureSize = false;
        b.surface.kind = BrushSurfaceKind::Dots; b.surface.frequency = 10; b.surface.density = .25;
        QVERIFY(doc.beginStroke({100, 100}, 1, b, false)); doc.endStroke();
        const auto pixels = render(layer(doc)); const auto object = layer(doc).objects.first();
        int hits = 0, holes = 0;
        for (int y = 80; y < 120; y += 3) for (int x = 80; x < 120; x += 3) {
            QPainterPath query; query.addRect(QRectF(x + .2, y + .2, .6, .6));
            const bool painted = qAlpha(pixels.pixel(x, y)) > 0;
            QCOMPARE(vectorBrushIntersects(object, query), painted);
            painted ? ++hits : ++holes;
        }
        QVERIFY(hits > 0 && holes > 0);
    }
    void tinyVectorFragmentRetainsEndpointsAndRepeatPhase() {
        Document doc; doc.load(project(true)); auto b = brush();
        QVERIFY(setBrushTipImages(b, {motif(), motif(true)})); b.tipOrder = BrushTipOrder::Ordered;
        QVERIFY(doc.beginStroke({40, 100}, 1, b, false)); doc.continueStroke({540, 100}, 1); doc.endStroke();
        const auto before = render(layer(doc)); auto fragment = layer(doc).objects.first();
        fragment.nodes = {BezierNode::corner({165, 100}), BezierNode::corner({173, 100})};
        sliceVectorBrushStroke(fragment, .25, .266, 500);
        QCOMPARE(fragment.brushStroke->samples.size(), 2);
        QCOMPARE(fragment.brushStroke->samples.first().position, 0.);
        QCOMPARE(fragment.brushStroke->samples.last().position, 1.);
        QCOMPARE(fragment.brushStroke->settings.connected.phaseOffset, 125.);
        RasterLayer result; QString error;
        QVERIFY2(replayVectorBrushStroke(result, fragment, doc.drawingBounds(), maxProjectTiles, &error), qPrintable(error));
        const auto after = render(result);
        QCOMPARE(after.copy(166, 80, 6, 40), before.copy(166, 80, 6, 40));
        QVERIFY(!result.tiles.isEmpty());
        auto p = doc.project(); p.shots[0].layers[0].objects = {fragment}; p.shots[0].layers[0].tiles = result.tiles;
        QTemporaryDir directory; const auto path = directory.filePath("fragment.toon");
        QVERIFY2(saveProject(p, path, &error), qPrintable(error)); const auto loaded = loadProject(path, &error);
        QVERIFY2(loaded.has_value(), qPrintable(error));
        QCOMPARE(loaded->shots[0].layers[0].objects.first().brushStroke->settings.connected.phaseOffset, 125.);
    }
    void vectorSegmentEraseKeepsRemainingMotifsAndUndo() {
        Document doc; doc.load(project(true)); auto b = brush();
        QVERIFY(doc.beginStroke({40, 100}, 1, b, false));
        for (int x = 90; x <= 540; x += 50) doc.continueStroke({qreal(x), 100}, 1);
        doc.endStroke(); const auto before = layer(doc).tiles; const auto pixels = render(layer(doc));
        QVERIFY(ObjectTools::eraseAt(&doc, {265, 100}, 3, true));
        QCOMPARE(layer(doc).objects.size(), 2);
        const auto after = render(layer(doc));
        QCOMPARE(after.copy(45, 80, 190, 40), pixels.copy(45, 80, 190, 40));
        QCOMPARE(after.copy(295, 80, 240, 40), pixels.copy(295, 80, 240, 40));
        QCOMPARE(qAlpha(after.pixel(265, 100)), 0);
        doc.history()->undo(); QCOMPARE(layer(doc).tiles, before);
    }
    void previewArtifact() {
        const auto output = qEnvironmentVariable("TOON_CONNECTED_BRUSH_PREVIEW");
        if (output.isEmpty()) QSKIP("Set TOON_CONNECTED_BRUSH_PREVIEW to save the actual-render comparison.");
        QImage image(640, 800, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::white); QPainter p(&image);
        auto b = brush();
        for (int row = 0; row < 3; ++row) {
            Document doc; doc.load(project());
            if (row == 1) { b.connected.mode = BrushConnectedMode::Repeat; b.connected.repeatLength = 1.3; }
            if (row == 2) { QVERIFY(setBrushTipImages(b, {motif(), motif(true)})); b.tipOrder = BrushTipOrder::Ordered; }
            draw(doc, b); p.drawImage(QPoint(0, row * 240), render(layer(doc)));
        }
        p.end(); QVERIFY(image.save(output));
    }
};
QTEST_MAIN(ConnectedBrushTest)
#include "test_connected_brush.moc"
