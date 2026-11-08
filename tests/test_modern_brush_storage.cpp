#include "brush_engine.h"
#include "brush_library.h"
#include "brush_settings_panel.h"
#include "brush_surface_pattern_widget.h"
#include "advanced_brush_dynamics_widget.h"
#include "catalog_brushes.h"
#include "document.h"
#include "object_tools.h"
#include "tool_catalog.h"
#include "tool_catalog_legacy_fixture.h"
#include "value_slider.h"
#include "vector_brush_storage.h"
#include "ui_theme.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTabBar>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
using namespace toon;
namespace {
BrushSettings fixture() {
    BrushSettings b;b.size=64;b.pressureSize=false;b.spacing=.15;
    b.surface.kind=BrushSurfaceKind::Image;b.surface.image=QImage(16,16,QImage::Format_ARGB32_Premultiplied);
    b.surface.image.fill(Qt::transparent);
    for(int y=0;y<16;++y)for(int x=0;x<16;++x)if(x%4<2)b.surface.image.setPixelColor(x,y,QColor(120,60+y*8,30,160));
    b.surface.tileSize={32,48};b.surface.perspective=true;b.surface.quad={QPointF(0,0),QPointF(300,32),QPointF(280,240),QPointF(0,240)};
    b.speedDynamics.enabled=true;b.colorDynamics.tintAmount=.45;return b;
}
QJsonObject read(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return QJsonDocument::fromJson(f.readAll()).object();}
bool write(const QString &path,const QJsonObject &o){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(QJsonDocument(o).toJson())>0;}
}
class ModernBrushStorageTests:public QObject {
 Q_OBJECT
private slots:
 void initTestCase(){applyTheme();}
 void legacyDefaultsStrictAtomicSchema(){
    BrushSettings b;const auto original=brushSettingsToJson(b);QVERIFY(!original.contains("surface"));QVERIFY(!original.contains("connected"));QVERIFY(!original.contains("speedDynamics"));
    auto modified=fixture();QString error;QVERIFY(brushSettingsFromJson(original,modified,&error));QVERIFY(!modified.speedDynamics.enabled);QCOMPARE(modified.surface.kind,BrushSurfaceKind::Off);
    // Resources belong to the outer decoder, which replaces them explicitly.
    QCOMPARE(modified.surface.image,fixture().surface.image);
    auto valid=brushSettingsToJson(fixture());auto bad=valid;auto surface=bad["surface"].toObject();surface["frequency"]="ten";bad["surface"]=surface;
    const auto before=modified;QVERIFY(!brushSettingsFromJson(bad,modified,&error));QCOMPARE(modified,before);
    bad=valid;auto connected=QJsonObject{{"mode",1},{"repeatLength",0},{"flipAlternate",false}};bad["connected"]=connected;
    QVERIFY(!brushSettingsFromJson(bad,modified,&error));QCOMPARE(modified,before);
    bad=valid;auto colors=bad["colorDynamics"].toObject();colors["tintAmount"]=2;bad["colorDynamics"]=colors;QVERIFY(!brushSettingsFromJson(bad,modified,&error));QCOMPARE(modified,before);
 }
 void libraryAndCatalogKeepSurfaceAndRejectDowngrades(){
    QTemporaryDir dir;QString error;auto b=fixture();BrushLibrary library(dir.filePath("brushes.json"));QVERIFY(library.load());
    QVERIFY2(library.saveCurrent("Surface",b,0,&error),qPrintable(error));auto json=read(library.path());QCOMPARE(json["version"].toInt(),7);
    BrushLibrary restored(library.path());QVERIFY2(restored.load(&error),qPrintable(error));QCOMPARE(restored.presets().last().brush,b);
    json["version"]=6;QVERIFY(write(library.path(),json));QVERIFY(!restored.load(&error));QCOMPARE(restored.presets().last().brush,b);
    ToolCatalog catalog(nullptr,dir.filePath("tools.json"));QVERIFY(catalog.load());const auto id=catalog.createBrush("Surface",BrushType::Custom,{b,0,{}});QVERIFY(!id.isEmpty());QVERIFY2(catalog.save(&error),qPrintable(error));
    json=read(catalog.path());QCOMPARE(json["version"].toInt(),14);ToolCatalog loaded(nullptr,catalog.path());QVERIFY2(loaded.load(&error),qPrintable(error));QCOMPARE(loaded.tool(id)->state.brush,b);
    json=test_fixture::toolCatalogBefore14(json,11);QVERIFY(write(catalog.path(),json));QVERIFY(!loaded.load(&error));QCOMPARE(loaded.tool(id)->state.brush,b);
    json=read(library.path());json["version"]=7;auto presets=json["presets"].toArray();auto row=presets[0].toObject();row.remove("surfaceImage");presets[0]=row;json["presets"]=presets;QVERIFY(write(library.path(),json));QVERIFY(!restored.load(&error));QVERIFY(error.contains("missing"));
 }
 void projectImageResourcesAndSpeedReplay(){
    auto p=Project::create();p.width=320;p.height=240;p.shots[0].sheet.overscan=0;p.shots[0].layers[0].kind=LayerKind::Vector;
    Document d;d.load(p);const auto b=fixture();BrushInput input;QVERIFY(d.beginStroke({40,120},input,b,false));
    for(int i=1;i<20;++i){input.speed=100+i*55;d.continueStroke({40+i*12.,120+std::sin(i*.3)*30},input);}d.endStroke();
    const auto &layer=d.activeShot().layers[0];QCOMPARE(layer.objects.size(),1);const auto &capture=*layer.objects[0].brushStroke;
    auto json=vectorBrushStrokeToJson(capture);QCOMPARE(json["version"].toInt(),6);QCOMPARE(json["samples"].toArray().last().toArray().size(),12);
    VectorBrushStroke restoredCapture;QString error;QVERIFY2(vectorBrushStrokeFromJson(json,&restoredCapture,&error),qPrintable(error));QCOMPARE(restoredCapture.samples,capture.samples);
    json["version"]=5;QVERIFY(!vectorBrushStrokeFromJson(json,&restoredCapture,&error));
    auto manifest=d.project().toJson();QCOMPARE(manifest["version"].toInt(),26);manifest["version"]=25;QVERIFY(!Project::fromJson(manifest,&error));
    QTemporaryDir dir;auto path=dir.filePath("surface.toonproj");QVERIFY2(saveProject(d.project(),path,&error),qPrintable(error));QFile f(path);QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.read(10),QByteArray("TOONPRO26\n"));f.close();
    auto loaded=loadProject(path,&error);QVERIFY2(loaded.has_value(),qPrintable(error));auto replay=loaded->shots[0].layers[0];QCOMPARE(replay.objects[0].brushStroke->settings.surface.image,b.surface.image);
    QVERIFY2(rebuildObjectLayer(replay,d.drawingBounds(),maxProjectTiles,&error),qPrintable(error));QCOMPARE(replay.tiles,layer.tiles);
 }
 void integratedCompactPropertiesDoNotEditArtwork(){
    Canvas canvas;canvas.setProject(Project::create());canvas.setBrushSettings(fixture());BrushSettingsPanel panel(&canvas);panel.show();panel.openAdvancedSettings();
    auto *tabs=panel.findChild<QTabBar*>("brushSettingsCategory");QVERIFY(tabs);QCOMPARE(tabs->count(),12);
    auto *mode=panel.findChild<QTabBar*>("brushConnectedMode");QVERIFY(mode);mode->setCurrentIndex(2);QCOMPARE(canvas.brush.connected.mode,BrushConnectedMode::Repeat);QVERIFY(!canvas.stampPlacementAvailable());
    auto *length=panel.findChild<ValueSlider*>("brushRepeatLength");QVERIFY(length);length->setValue(2.5);QCOMPARE(canvas.brush.connected.repeatLength,2.5);
    mode->setCurrentIndex(0);auto *surface=panel.findChild<BrushSurfacePatternWidget*>();QVERIFY(surface);auto changed=canvas.brush.surface;changed.kind=BrushSurfaceKind::Dots;changed.frequency=7;
    emit surface->patternEdited(changed);QCOMPARE(canvas.brush.surface.frequency,7.);QCOMPARE(canvas.brush.surface.kind,BrushSurfaceKind::Dots);
    auto *tint=panel.findChild<ValueSlider*>("brushColorTint");QVERIFY(tint);tint->setValue(60);QCOMPARE(canvas.brush.colorDynamics.tintAmount,.6);
    QCOMPARE(canvas.document()->history()->count(),0);
 }
 void recipeUpgradeKeepsCustomizations(){
    QTemporaryDir dir;BrushLibrary library(dir.filePath("brushes.json"));QVERIFY(library.load());ToolCatalog catalog(&library,dir.filePath("tools.json"));QVERIFY(catalog.load());
    QCOMPARE(library.presets().size(),219);const QString id="brush/builtin/catalog/meadow-tuft-set";QVERIFY(catalog.tool(id));
    auto state=catalog.tool(id)->state;state.brush.size=173;QVERIFY(catalog.updateToolState(id,state));QString error;QVERIFY(catalog.save(&error));
    auto json=read(catalog.path());auto tools=json["tools"].toArray();for(int i=0;i<tools.size();++i){auto row=tools[i].toObject();if(row["id"]==id){row["builtinRecipeRevision"]=3;tools[i]=row;}}json["tools"]=tools;QVERIFY(write(catalog.path(),json));
    ToolCatalog loaded(&library,catalog.path());QVERIFY2(loaded.load(&error),qPrintable(error));QCOMPARE(loaded.tool(id)->state.brush,state.brush);QCOMPARE(loaded.tool(id)->builtinRecipeRevision,catalogBrushRecipeRevision);
 }
};
QTEST_MAIN(ModernBrushStorageTests)
#include "test_modern_brush_storage.moc"
