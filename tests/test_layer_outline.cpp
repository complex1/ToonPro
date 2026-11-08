#include "layer_outline.h"
#include "effect_render.h"
#include "effects_panel.h"
#include "canvas.h"
#include "document.h"
#include "editable_object.h"
#include "main_window.h"
#include "animation_render.h"
#include "sheet.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QColorDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>
#include <limits>
#include <cmath>
#include <cstring>

using namespace toon;
namespace {
void rectangle(RasterLayer &layer,QRect rect,QColor color){
    const int left=int(std::floor(rect.left()/qreal(tileSize))),right=int(std::floor(rect.right()/qreal(tileSize))),top=int(std::floor(rect.top()/qreal(tileSize))),bottom=int(std::floor(rect.bottom()/qreal(tileSize)));
    for(int y=top;y<=bottom;++y)for(int x=left;x<=right;++x){auto &tile=layer.tiles[{x,y}];if(tile.isNull()){tile=QImage(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);}QPainter p(&tile);p.setCompositionMode(QPainter::CompositionMode_Source);p.fillRect(rect.translated(-x*tileSize,-y*tileSize),color);}
}
Project fixture(){auto p=Project::create(ProjectKind::DrawingOnly);p.width=512;p.height=256;p.transparent=true;p.shots[0].sheet.overscan=0;rectangle(p.shots[0].layers[0],{40,40,40,40},Qt::red);return p;}
QImage render(const QVector<RasterLayer> &layers,QRect bounds={0,0,512,256}){QImage image(bounds.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter p(&image);p.translate(-bounds.topLeft());paintLayers(p,layers,true);return image;}
void outline(RasterLayer &layer,qreal width=3,QColor color=Qt::blue){layer.effects.outlineEnabled=true;layer.effects.outlineWidth=width;layer.effects.outlineColor=color;}
}
class LayerOutlineTest:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void widthColorRoundCornersAndHoles(){
        auto p=fixture();auto &layer=p.shots[0].layers[0];rectangle(layer,{52,52,16,16},Qt::transparent);const auto source=layer.tiles;outline(layer);const auto image=render(p.shots[0].layers);
        QCOMPARE(image.pixelColor(45,45),QColor(Qt::red));QCOMPARE(image.pixelColor(38,60),QColor(Qt::blue));QCOMPARE(image.pixelColor(53,60),QColor(Qt::blue));QCOMPARE(image.pixelColor(60,60).alpha(),0);QCOMPARE(image.pixelColor(35,60).alpha(),0);QCOMPARE(image.pixelColor(37,37).alpha(),0);QCOMPARE(layer.tiles,source);
        layer.effects.outlineWidth=6;layer.effects.outlineColor=Qt::green;QCOMPARE(render(p.shots[0].layers).pixelColor(35,60),QColor(Qt::green));layer.effects.outlineEnabled=false;QCOMPARE(render(p.shots[0].layers).pixelColor(38,60).alpha(),0);
    }
    void fractionalCoverageTransparencyCancellationAndValidation(){
        QImage image(100,100,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);{QPainter p(&image);p.fillRect(40,40,20,20,QColor(200,30,20,128));}
        const auto source=image;QVERIFY(applyLayerOutline(image,2.5,Qt::blue));QCOMPARE(image.pixel(50,50),source.pixel(50,50));QCOMPARE(image.pixelColor(38,50).alpha(),128);const auto corner=image.pixelColor(38,38).alpha();QVERIFY(corner>0&&corner<128);
        auto cancelled=source;QVERIFY(!applyLayerOutline(cancelled,20,Qt::blue,[]{return true;}));QCOMPARE(cancelled,source);QVERIFY(!applyLayerOutline(cancelled,std::numeric_limits<double>::quiet_NaN(),Qt::blue));QCOMPARE(cancelled,source);
        auto settings=LayerEffects{};settings.outlineWidth=129;QVERIFY(!validateLayerEffects(settings).isEmpty());settings.outlineWidth=3;settings.outlineColor=QColor(0,0,0,20);QVERIFY(!validateLayerEffects(settings).isEmpty());
    }
    void tileHalosCropsMasksAndOtherEffectsAgree(){
        auto p=fixture();auto &layer=p.shots[0].layers[0];layer.tiles.clear();rectangle(layer,{250,50,24,50},Qt::red);outline(layer,8);const auto full=render(p.shots[0].layers);const QRect crop(256,40,40,80);QCOMPARE(render(p.shots[0].layers,crop),full.copy(crop));QCOMPARE(full.pixelColor(245,70),QColor(Qt::blue));
        LayerMask mask;QImage pixels(tileSize,tileSize,QImage::Format_Grayscale8);pixels.fill(255);for(int y=0;y<tileSize;++y)std::memset(pixels.scanLine(y),0,256);mask.tiles[{0,0}]=pixels;layer.mask=mask;
        const auto masked=render(p.shots[0].layers);QCOMPARE(masked.pixelColor(245,70).alpha(),0);QCOMPARE(masked.pixelColor(254,70),QColor(Qt::blue));
        layer.mask.reset();layer.effects.shadowEnabled=true;layer.effects.shadowRadius=0;layer.effects.shadowOpacity=1;layer.effects.shadowOffset={12,0};const auto shadow=render(p.shots[0].layers);QCOMPARE(shadow.pixelColor(290,70),QColor(Qt::black));
    }
    void tiledAntialiasedContoursMatchTheWholeImage(){
        QImage image(512,256,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);{QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.setPen(Qt::NoPen);p.setBrush(QColor(200,80,30,180));p.drawEllipse(QRectF(242.3,54.7,35.6,55.8));}
        auto reference=image;QVERIFY(applyLayerOutline(reference,9.3,Qt::blue));auto layer=RasterLayer::create("AA across tiles");for(int x=0;x<512;x+=tileSize)layer.tiles[{x/tileSize,0}]=image.copy(x,0,tileSize,tileSize);outline(layer,9.3);QCOMPARE(render({layer}),reference);
    }
    void cancellationDuringExpansionLeavesTheInputIntact(){
        QImage image(2048,1024,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::red);const auto before=image;
        // Cancel after both distance passes and sixteen output rows, without
        // depending on how long a platform's event loop takes to wake up.
        int checks=0;const int stopAfter=1+image.height()+image.width()+16;
        QVERIFY(!applyLayerOutline(image,128,Qt::white,[&]{return ++checks>=stopAfter;}));QCOMPARE(checks,stopAfter);QCOMPARE(image,before);
    }
    void foldersVectorsAndExportsUseTheSameOutline(){
        auto p=fixture();auto first=p.shots[0].layers[0];auto folder=RasterLayer::create("Outlined folder");folder.group=true;outline(folder);first.parentId=folder.id;auto second=RasterLayer::create("Green");second.parentId=folder.id;rectangle(second,{80,40,40,40},Qt::green);p.shots[0].layers={folder,first,second};const auto group=render(p.shots[0].layers);QCOMPARE(group.pixelColor(79,60),QColor(Qt::red));QCOMPARE(group.pixelColor(80,60),QColor(Qt::green));QCOMPARE(group.pixelColor(122,60),QColor(Qt::blue));
        auto vector=RasterLayer::create("Vector");vector.kind=LayerKind::Vector;auto object=EditableObject::create();object.closed=true;object.stroke=QColor();object.fill=Qt::red;object.nodes={BezierNode::corner({40,40}),BezierNode::corner({80,40}),BezierNode::corner({80,80}),BezierNode::corner({40,80})};vector.objects={object};QString error;QVERIFY(rebuildObjectLayer(vector,{0,0,512,256},1024,&error));outline(vector);p.shots[0].layers={vector};QCOMPARE(render(p.shots[0].layers).pixelColor(38,60),QColor(Qt::blue));QCOMPARE(renderMovieFrame(p).pixelColor(38,60),QColor(Qt::blue));
    }
    void liveEditsUndoAndStoredSettingsKeepSourcePixels(){
        auto p=fixture();Document doc;doc.load(p);const auto source=doc.activeShot().layers[0].tiles;auto fx=LayerEffects{};fx.outlineEnabled=true;fx.outlineWidth=4.5;fx.outlineColor=QColor("#8040cf");const auto id=doc.activeShot().layers[0].id;QVERIFY(doc.setLayerEffects(id,fx));QCOMPARE(doc.activeShot().layers[0].tiles,source);QVERIFY(layerCanPaint(doc.activeShot().layers,0));
        QTemporaryDir directory;QString error;const auto file=directory.filePath("outline.toonproj");QVERIFY(saveProject(doc.project(),file,&error));const auto saved=loadProject(file,&error);QVERIFY2(saved.has_value(),qPrintable(error));QCOMPARE(saved->shots[0].layers[0].effects,fx);QCOMPARE(saved->shots[0].layers[0].tiles,source);QCOMPARE(saved->toJson()["version"].toInt(),42);
        doc.history()->undo();QVERIFY(!doc.activeShot().layers[0].effects.outlineEnabled);doc.history()->redo();QCOMPARE(doc.activeShot().layers[0].effects,fx);
        rectangle(p.shots[0].layers[0],{140,40,20,20},Qt::red);outline(p.shots[0].layers[0]);QCOMPARE(render(p.shots[0].layers).pixelColor(138,50),QColor(Qt::blue));
        fx.outlineEnabled=false;fx.enabled=false;QVERIFY(doc.setLayerEffects(id,fx));QVERIFY(saveProject(doc.project(),file,&error));const auto disabled=loadProject(file,&error);QVERIFY(disabled);QCOMPARE(disabled->shots[0].layers[0].effects,fx);
    }
    void legacyDefaultsAndMalformedOutlineMetadata(){
        const auto defaults=layerEffectsToJson({});QVERIFY(!defaults.contains("outlineEnabled"));LayerEffects parsed;QString error;QVERIFY(layerEffectsFromJson(defaults,&parsed,&error));QCOMPARE(parsed,LayerEffects{});
        auto fx=LayerEffects{};fx.outlineEnabled=true;auto metadata=layerEffectsToJson(fx);metadata["outlineWidth"]=-1;const auto before=parsed;QVERIFY(!layerEffectsFromJson(metadata,&parsed,&error));QCOMPARE(parsed,before);
        auto p=fixture();p.kind=ProjectKind::Film;outline(p.shots[0].layers[0]);auto manifest=p.toJson();QCOMPARE(manifest["version"].toInt(),42);QVERIFY(Project::fromJson(manifest,&error));manifest["version"]=41;QVERIFY(!Project::fromJson(manifest,&error));QVERIFY(error.contains("format 42"));
        Document doc;doc.load(fixture());QVERIFY(doc.addAdjustmentLayer());fx.outlineEnabled=true;QVERIFY(!doc.setLayerEffects(doc.activeShot().layers[doc.activeLayer()].id,fx));
    }
    void outlineInspectorWidthColorCancelAndSourcePreservation(){
        Canvas canvas;canvas.setProject(fixture());EffectsPanel panel(&canvas);panel.resize(230,330);panel.show();auto *tabs=panel.findChild<QTabBar*>("effectsTabs");QVERIFY(tabs);QCOMPARE(tabs->count(),6);tabs->setCurrentIndex(5);auto *enable=panel.findChild<QToolButton*>("effectOutlineEnabled");QVERIFY(enable);enable->click();auto *doc=canvas.document();QVERIFY(doc->activeShot().layers[0].effects.outlineEnabled);const auto source=doc->activeShot().layers[0].tiles;
        auto *width=panel.findChild<ValueSlider*>("effectOutlineWidth");QVERIFY(width);auto *entry=width->findChild<QDoubleSpinBox*>();entry->setFocus();entry->selectAll();QTest::keyClicks(entry,"6.5");QTest::keyClick(entry,Qt::Key_Return);QCOMPARE(doc->activeShot().layers[0].effects.outlineWidth,6.5);
        auto *color=panel.findChild<QToolButton*>("effectOutlineColor");QVERIFY(color);const auto old=doc->activeShot().layers[0].effects;const int history=doc->history()->count();
        QTimer::singleShot(20,&panel,[&]{auto *dialog=panel.findChild<QColorDialog*>("effectColorDialog");if(dialog){dialog->setCurrentColor(Qt::green);dialog->reject();}});color->click();QCOMPARE(doc->activeShot().layers[0].effects,old);QCOMPARE(doc->history()->count(),history);
        QTimer::singleShot(20,&panel,[&]{auto *dialog=panel.findChild<QColorDialog*>("effectColorDialog");if(dialog){dialog->setCurrentColor(Qt::green);dialog->accept();}});color->click();QCOMPARE(doc->activeShot().layers[0].effects.outlineColor,QColor(Qt::green));QCOMPARE(doc->history()->count(),history+1);QCOMPARE(doc->activeShot().layers[0].tiles,source);
    }
    void captureReviewedOutline(){
        const auto directory=qEnvironmentVariable("TOON_OUTLINE_CAPTURE_DIR");if(directory.isEmpty())return;QDir().mkpath(directory);QTemporaryDir temp;
        auto p=fixture();p.width=640;p.height=420;p.transparent=false;auto &layer=p.shots[0].layers[0];layer.name="Outlined drawing";layer.tiles.clear();
        QImage art(640,420,QImage::Format_ARGB32_Premultiplied);art.fill(Qt::transparent);{QPainter paint(&art);paint.setRenderHint(QPainter::Antialiasing);paint.setPen(Qt::NoPen);paint.setBrush(QColor("#e9ae55"));paint.drawRoundedRect(QRectF(100,100,150,170),28,28);paint.setBrush(QColor("#5daec4"));paint.drawEllipse(QRectF(330,90,170,170));paint.setCompositionMode(QPainter::CompositionMode_Clear);paint.drawEllipse(QRectF(382,142,66,66));}
        for(int y=0;y<420;y+=tileSize)for(int x=0;x<640;x+=tileSize){QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);QPainter paint(&tile);paint.drawImage(-x,-y,art);paint.end();layer.tiles[{x/tileSize,y/tileSize}]=tile;}
        outline(layer,6,QColor("#23364b"));MainWindow window(temp.filePath("workspace.ini"));window.resize(1280,840);window.canvas()->setProject(p);window.showDrawingWorkspacePage();window.applyDrawingWorkspace();window.show();
        EffectsPanel panel(window.canvas());panel.resize(240,350);panel.show();panel.findChild<QTabBar*>("effectsTabs")->setCurrentIndex(5);QTest::qWait(30);QTRY_VERIFY_WITH_TIMEOUT(!window.canvas()->effectsPreviewPending(),15000);QTest::qWait(20);QVERIFY(panel.grab().save(directory+"/outline-controls.png"));QVERIFY(window.canvas()->grab().save(directory+"/outline-canvas.png"));
        const auto output=render(p.shots[0].layers,{0,0,640,420});QVERIFY(output.save(directory+"/outline-result.png"));
    }
};
QTEST_MAIN(LayerOutlineTest)
#include "test_layer_outline.moc"
