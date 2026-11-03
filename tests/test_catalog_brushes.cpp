#include "catalog_brushes.h"
#include "brush_engine.h"
#include "document.h"
#include "object_tools.h"
#include "sheet.h"
#include "clipboard_tools.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <cmath>

using namespace toon;
namespace {
QByteArray digest(const QImage &image) {
    return QCryptographicHash::hash(QByteArrayView(
        reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes()), QCryptographicHash::Sha256);
}
Project sampleProject(bool vector) {
    auto project = Project::create();
    project.width = 512; project.height = 384; project.transparent = true;
    project.shots[0].sheet.overscan = 0;
    project.shots[0].layers[0].kind = vector ? LayerKind::Vector : LayerKind::Raster;
    return project;
}
const RasterLayer &layer(const Document &document) { return document.project().shots[0].layers[0]; }
void stroke(Document &document, BrushSettings settings) {
    document.setBrushRandomSeed(4119);
    BrushInput input; input.pressure = .25; input.tiltX = 25;
    QVERIFY(document.beginStroke({128, 192}, input, settings, false));
    for (int i = 1; i <= 32; ++i) {
        input.pressure = .25 + .7 * std::sin(i * 3.141592653589793 / 33);
        input.rotation = i * 2;
        document.continueStroke({128 + i * 8., 192 + std::sin(i * .2) * 40}, input);
    }
    document.endStroke();
}
}

class CatalogBrushesTests : public QObject {
    Q_OBJECT
private slots:
    void manifestMatchesDefaultsAndResourceBudget() {
        QFile manifest(QStringLiteral(TOON_SOURCE_DIR "/docs/brush-catalog.json"));
        QVERIFY(manifest.open(QIODevice::ReadOnly));
        const auto rows = QJsonDocument::fromJson(manifest.readAll()).object()["brushes"].toArray();
        QHash<QString, QJsonObject> expected;
        for (const auto &value : rows) {
            const auto row = value.toObject();
            if (row["status"] == "built-in")
                expected.insert("builtin/catalog/" + row["id"].toString().section('/', 1), row);
        }
        const auto presets = catalogBrushPresets();
        QCOMPARE(presets.size(), 200); QCOMPARE(expected.size(), presets.size());
        QSet<QString> seen, families;
        qint64 bytes = 0; int colored = 0;
        for (const auto &preset : presets) {
            QString error;
            QVERIFY2(validateBrushSettings(preset.brush, &error), qPrintable(preset.name + ": " + error));
            QVERIFY(preset.builtIn); QVERIFY(!preset.source.isEmpty());
            QVERIFY(expected.contains(preset.id)); QVERIFY(!seen.contains(preset.id)); seen.insert(preset.id);
            QCOMPARE(preset.name, expected[preset.id]["name"].toString());
            QCOMPARE(catalogBrushCategory(preset.id), expected[preset.id]["family"].toString());
            families.insert(catalogBrushCategory(preset.id));
            QVERIFY(!catalogBrushTags(preset.id).isEmpty());
            QVERIFY(preset.brush.size <= maxBrushSize); QVERIFY(preset.brush.count <= (preset.brush.clusterSpray.enabled ? 16 : 2));
            for (const auto &resource : {preset.brush.tipMask, preset.brush.texture.pattern, preset.brush.dual.tipMask}) {
                if (resource.isNull()) continue;
                QCOMPARE(resource.format(), QImage::Format_Grayscale8);
                QVERIFY(resource.width() <= 2048); QVERIFY(resource.height() <= 2048);
                bytes += resource.sizeInBytes();
            }
            bytes += preset.brush.surface.image.sizeInBytes();
            if (!preset.brush.colorTip.isNull()) {
                ++colored; bytes += preset.brush.colorTip.sizeInBytes();
                QCOMPARE(preset.brush.colorTip.format(), QImage::Format_ARGB32_Premultiplied);
                QVERIFY(preset.brush.colorTip.width() <= 2048); QVERIFY(preset.brush.colorTip.height() <= 2048);
            }
        }
        QCOMPARE(families.size(), 15); QCOMPARE(colored, 11);
        QVERIFY(bytes < 48 * 1024 * 1024);
        qInfo() << "Catalog decoded source bytes:" << bytes;
        QVERIFY(catalogBrushCategory("user/unknown").isEmpty());
    }

    void authoredRepeatsPreserveBoundaryPosts() {
        for(const auto &preset:catalogBrushPresets()) {
            const int step=preset.id.endsWith("/fence-posts")?64:preset.id.endsWith("/railings-and-bars")?32:0;
            if(!step)continue;
            const auto &tip=preset.brush.tipMask;
            for(int x=step;x<tip.width();x+=step)
                QCOMPARE(tip.copy(x,0,step,tip.height()),tip.copy(0,0,step,tip.height()));
        }
    }
    void correctedStoneTileClosesStaggeredLeftEdge() {
        for(const auto &preset:catalogBrushPresets())if(preset.id.endsWith("/stone-perspective-path")) {
            const auto tile=preset.brush.surface.image;
            // The staggered second row needs the stone whose origin is left
            // of the tile, otherwise its entire left half remains empty.
            int covered=0;
            for(int y=65;y<125;++y)for(int x=0;x<28;++x)covered+=qAlpha(tile.pixel(x,y))>0;
            QVERIFY(covered>50);
            QVERIFY(qAlpha(tile.pixel(15,85))==0); // interiors remain transparent
            const auto output=qEnvironmentVariable("TOON_CONNECTED_QUALITY");
            if(!output.isEmpty()) {
                QVERIFY(QDir().mkpath(output));QImage repeated(768,768,QImage::Format_ARGB32_Premultiplied);repeated.fill(Qt::white);QPainter p(&repeated);
                for(int y=0;y<3;++y)for(int x=0;x<3;++x)p.drawImage(x*256,y*256,tile);
                p.end();QVERIFY(repeated.save(output+"/stone-tile.png"));
            }
        }
    }
    void visibleRepeatableDistinctPreviews() {
        QSet<QByteArray> rendered;
        for (const auto &preset : catalogBrushPresets()) {
            const auto preview = brushStrokePreview(preset.brush, {300, 90}, Qt::white);
            QCOMPARE(brushStrokePreview(preset.brush, {300, 90}, Qt::white), preview);
            QImage blank(preview.size(), preview.format()); blank.fill(Qt::white);
            QVERIFY2(preview != blank, qPrintable(preset.name + " must paint"));
            const auto hash = digest(preview);
            QVERIFY2(!rendered.contains(hash), qPrintable(preset.name + " duplicates an earlier mark"));
            rendered.insert(hash);
        }
    }
    void advancedCloudFramesHaveTransparentBorders() {
        int checked=0;
        for(const auto &preset:catalogBrushPresets()){
            if(preset.brush.connected.mode!=BrushConnectedMode::Off||preset.brush.tipFrames.isEmpty()||(catalogBrushCategory(preset.id)!="Clouds and atmosphere"&&!preset.id.contains("painted-cloud")))continue;
            for(int i=0;i<preset.brush.tipFrames.size();++i){const auto image=brushTipFrameImage(preset.brush,i);
                auto coverage=[&](int x,int y){return image.format()==QImage::Format_Grayscale8?image.constScanLine(y)[x]:qAlpha(image.pixel(x,y));};
                for(int x=0;x<image.width();++x){QCOMPARE(coverage(x,0),0);QCOMPARE(coverage(x,image.height()-1),0);}
                for(int y=0;y<image.height();++y){QCOMPARE(coverage(0,y),0);QCOMPARE(coverage(image.width()-1,y),0);}++checked;
            }
        }
        QVERIFY(checked>=42);
    }
    void authoredTipSetsContainDistinctImages() {
        int sets=0;
        for(const auto &preset:catalogBrushPresets()){
            if(preset.brush.tipFrames.isEmpty())continue;QSet<QByteArray> images;
            for(int i=0;i<preset.brush.tipFrames.size();++i){const auto hash=digest(brushTipFrameImage(preset.brush,i));QVERIFY2(!images.contains(hash),qPrintable(preset.name+QString(" repeats tip %1").arg(i+1)));images.insert(hash);}
            QCOMPARE(images.size(),preset.brush.tipFrames.size());++sets;
        }
        QCOMPARE(sets,73);
    }

    void rasterVectorReplayUndoAndProjectPersistence() {
        for (const auto &preset : catalogBrushPresets()) {
            Document raster, vector;
            raster.load(sampleProject(false)); vector.load(sampleProject(true));
            stroke(raster, preset.brush); stroke(vector, preset.brush);
            QVERIFY2(!layer(raster).tiles.isEmpty(), qPrintable(preset.name));
            QCOMPARE(layer(vector).tiles, layer(raster).tiles);
            QCOMPARE(layer(vector).objects.size(), 1);
            const auto expected = layer(vector).tiles;
            vector.history()->undo(); QVERIFY(layer(vector).tiles.isEmpty());
            vector.history()->redo(); QCOMPARE(layer(vector).tiles, expected);
            auto rebuilt = layer(vector); QString error;
            QVERIFY2(rebuildObjectLayer(rebuilt, vector.drawingBounds(), maxProjectTiles, &error), qPrintable(error));
            QCOMPARE(rebuilt.tiles, expected);
            QTemporaryDir temporary;
            const auto path = temporary.filePath("catalog.toonproj");
            QVERIFY2(saveProject(vector.project(), path, &error), qPrintable(error));
            const auto reloaded = loadProject(path, &error);
            QVERIFY2(reloaded.has_value(), qPrintable(error));
            QCOMPARE(reloaded->shots[0].layers[0].tiles, expected);
        }
    }

    void customCopiesRetainAllResources() {
        auto copies = catalogBrushPresets();
        for (auto &preset : copies) { preset.id.replace("builtin/", "user/"); preset.builtIn = false; }
        for (int offset=0; offset<copies.size(); offset+=BrushLibrary::maxCustomPresets) {
            QTemporaryDir temporary;const auto path=temporary.filePath("brushes.json");
            BrushLibrary library(path);QVERIFY(library.load());
            const auto batch=copies.mid(offset,BrushLibrary::maxCustomPresets);QString error;
            QVERIFY2(library.addPresets(batch,&error),qPrintable(error));
            BrushLibrary loaded(path);QVERIFY2(loaded.load(&error),qPrintable(error));
            for(const auto &copy:batch){const auto found=std::find_if(loaded.presets().cbegin(),loaded.presets().cend(),[&](const BrushPreset &preset){return preset.id==copy.id;});QVERIFY(found!=loaded.presets().cend());QCOMPARE(found->brush,copy.brush);QCOMPARE(found->smoothing,copy.smoothing);}
        }
    }

    void renderContactSheets() {
        const QString directory = qEnvironmentVariable("TOON_CATALOG_PREVIEW");
        if (directory.isEmpty()) QSKIP("Set TOON_CATALOG_PREVIEW to export review sheets.");
        QVERIFY(QDir().mkpath(directory));
        QMap<QString, QVector<BrushPreset>> families;
        for (const auto &preset : catalogBrushPresets()) families[catalogBrushCategory(preset.id)].append(preset);
        for (auto it = families.cbegin(); it != families.cend(); ++it) {
            constexpr int width = 330, height = 186, columns = 3;
            QImage page(width * columns, 58 + ((it->size() + columns - 1) / columns) * height, QImage::Format_ARGB32_Premultiplied);
            page.fill(QColor("#ecebe6")); QPainter painter(&page);
            painter.setPen(QColor("#24282a")); painter.setFont(QFont("Arial", 19, QFont::Bold));
            painter.drawText(QRect(18, 8, page.width() - 36, 40), Qt::AlignVCenter, it.key());
            for (int i = 0; i < it->size(); ++i) {
                const auto &preset = it.value()[i];
                const QPoint origin((i % columns) * width, 58 + (i / columns) * height);
                painter.fillRect(QRect(origin + QPoint(6, 3), QSize(width - 12, height - 9)), Qt::white);
                painter.setFont(QFont("Arial", 12, QFont::Bold));
                painter.drawText(QRect(origin + QPoint(14, 9), QSize(width - 28, 24)), Qt::AlignVCenter, preset.name);
                painter.drawImage(origin + QPoint(15, 41), brushStrokePreview(preset.brush, {300, 78}, Qt::white));
                auto stamp = preset.brush; stamp.size = std::min<qreal>(stamp.size, 50);
                const auto dab = evaluateBrushDab(stamp, {}, {}, 0, 0, 0, 4119);
                QImage single = dab.colorTip;
                if (single.isNull()) {
                    single = QImage(dab.mask.size(), QImage::Format_ARGB32_Premultiplied); single.fill(Qt::transparent);
                    for (int y = 0; y < single.height(); ++y) for (int x = 0; x < single.width(); ++x) {
                        QColor color = dab.color; color.setAlpha(dab.mask.constScanLine(y)[x]); single.setPixelColor(x, y, color);
                    }
                }
                if (stamp.surface.kind != BrushSurfaceKind::Off) single = brushStrokePreview(stamp, {68, 40}, Qt::transparent);
                painter.drawImage(origin + QPoint(45 - single.width() / 2, 150 - single.height() / 2), single);
                painter.setFont(QFont("Arial", 10));
                painter.drawText(QRect(origin + QPoint(86, 127), QSize(225, 42)), Qt::AlignVCenter | Qt::TextWordWrap,
                    QString("Size %1 • Flow %2 • Spacing %3").arg(preset.brush.size).arg(qRound(preset.brush.opacity * 100)).arg(preset.brush.spacing));
            }
            painter.end();
            QString file = it.key().toLower(); file.replace(' ', '-');
            QVERIFY(page.save(QDir(directory).filePath(file + ".png")));
        }
    }
    void renderBackgroundReview() {
        const QString directory=qEnvironmentVariable("TOON_CATALOG_PREVIEW");
        if(directory.isEmpty())QSKIP("Set TOON_CATALOG_PREVIEW to export the synthetic brush scene.");
        QVERIFY(QDir().mkpath(directory));auto p=Project::create();p.width=1408;p.height=960;p.transparent=false;
        Document doc;doc.load(p);QImage sky(p.width,p.height,QImage::Format_ARGB32_Premultiplied);
        {QPainter painter(&sky);QLinearGradient gradient(0,0,0,960);gradient.setColorAt(0,QColor("#7bafce"));gradient.setColorAt(.68,QColor("#d4dfd2"));gradient.setColorAt(.71,QColor("#809877"));gradient.setColorAt(1,QColor("#4e7058"));painter.fillRect(sky.rect(),gradient);}
        QVERIFY(ClipboardTools::importImage(&doc,sky,"Sky",{}));doc.addLayer();
        const auto presets=catalogBrushPresets();
        auto brush=[&](const QString &id){for(const auto &preset:presets)if(preset.id=="builtin/catalog/"+id)return preset.brush;return BrushSettings{};};
        auto strokeLine=[&](BrushSettings b,QPointF a,QPointF z,int steps){b.lockRandomSeed=true;b.randomSeed=71;QVERIFY(doc.beginStroke(a,1,b,false));for(int i=1;i<=steps;++i)doc.continueStroke(a+(z-a)*(i/qreal(steps)),1);doc.endStroke();};
        auto clouds=brush("cumulus-mass-set");clouds.color=Qt::white;strokeLine(clouds,{100,260},{1200,210},100);
        auto mountain=brush("mountain-ridge-set");mountain.color=QColor("#8199a5");mountain.size=900;mountain.spacing=.6;strokeLine(mountain,{150,550},{1250,530},30);
        auto building=brush("painted-building");building.size=260;strokeLine(building,{560,745},{900,745},20);
        auto trees=brush("forest-builder");trees.color=QColor("#42695d");trees.size=360;strokeLine(trees,{50,790},{1400,790},70);
        trees.color=QColor("#325b51");trees.size=460;strokeLine(trees,{110,840},{400,840},20);
        auto rocks=brush("boulder-set");rocks.color=QColor("#6b7774");rocks.size=160;strokeLine(rocks,{990,810},{1250,825},16);
        auto grass=brush("meadow-tuft-set");grass.color=QColor("#2a5946");grass.size=130;strokeLine(grass,{-20,940},{1430,940},110);
        auto birds=brush("soaring-bird-flock");birds.color=QColor("#3e5661");birds.size=32;strokeLine(birds,{640,130},{1010,160},40);
        QVERIFY(renderMovieFrame(doc.project()).save(QDir(directory).filePath("advanced-brush-background.png")));
    }
};
QTEST_MAIN(CatalogBrushesTests)
#include "test_catalog_brushes.moc"
