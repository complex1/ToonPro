#include "brush_engine.h"
#include "brush_library.h"
#include "brush_settings_panel.h"
#include "document.h"
#include "object_tools.h"
#include "tool_catalog.h"
#include "ui_theme.h"
#include "value_slider.h"
#include "vector_brush.h"
#include "vector_brush_storage.h"
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTabBar>
#include <QTemporaryDir>
#include <QtTest>
using namespace toon;
namespace {
BrushSettings brush() {
    auto b=brushSettingsForType(BrushType::DynamicStamp); b.size=40;
    b.colorDynamics.applyToColorTip=true;
    QVector<QImage> images;
    for(int i=0;i<3;++i){QImage image(24,24,QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor::fromHsv(30+i*50,150,210,180));images.append(image);}
    if(!setBrushTipImages(b,images))qFatal("Invalid fixture");return b;
}
QJsonObject read(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return QJsonDocument::fromJson(f.readAll()).object();}
bool write(const QString &path,const QJsonObject &o){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(QJsonDocument(o).toJson())>0;}
}
class DynamicStampStorageTests:public QObject {
 Q_OBJECT
private slots:
 void initTestCase(){applyTheme();}
 void legacyOptInAndStrictParsing(){
    auto b=brush();auto old=brushSettingsToJson(b);auto color=old["colorDynamics"].toObject();color.remove("applyToColorTip");old["colorDynamics"]=color;
    QString error;QVERIFY(brushSettingsFromJson(old,b,&error));QVERIFY(!b.colorDynamics.applyToColorTip);
    color["applyToColorTip"]="true";old["colorDynamics"]=color;const auto before=b;
    QVERIFY(!brushSettingsFromJson(old,b,&error));QCOMPARE(b,before);
 }
 void versionedVectorProjectRoundTrip(){
    QString futureError;auto future=Project::create().toJson();future["version"]=latestProjectFormatVersion+1;
    QVERIFY(!Project::fromJson(future,&futureError));QVERIFY(futureError.contains("supported Toon Pro project"));
    auto p=Project::create();p.width=320;p.height=240;p.shots[0].layers[0].kind=LayerKind::Vector;
    Document d;d.load(p);QVERIFY(d.beginStroke({40,100},1,brush(),false));d.continueStroke({250,100},1);d.endStroke();
    const auto &layer=d.activeShot().layers[0];QCOMPARE(layer.objects.size(),1);
    auto capture=vectorBrushStrokeToJson(*layer.objects[0].brushStroke);QCOMPARE(capture["version"].toInt(),5);
    VectorBrushStroke parsed;QString error;QVERIFY(vectorBrushStrokeFromJson(capture,&parsed,&error));
    capture["version"]=4;QVERIFY(!vectorBrushStrokeFromJson(capture,&parsed,&error));QVERIFY(error.contains("version 5"));
    auto json=d.project().toJson();QCOMPARE(json["version"].toInt(),25);json["version"]=24;
    QVERIFY(!Project::fromJson(json,&error));QVERIFY(error.contains("format 25"));
    QTemporaryDir dir;auto path=dir.filePath("dynamic.toonproj");QVERIFY2(saveProject(d.project(),path,&error),qPrintable(error));
    QFile f(path);QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.read(10),QByteArray("TOONPRO25\n"));f.close();
    auto restored=loadProject(path,&error);QVERIFY2(restored.has_value(),qPrintable(error));auto replay=restored->shots[0].layers[0];
    QVERIFY(rebuildObjectLayer(replay,d.drawingBounds(),maxProjectTiles,&error));QCOMPARE(replay.tiles,layer.tiles);
 }
 void libraryAndToolCatalogRejectDowngradesWithoutReplacingState(){
    QTemporaryDir dir;QString error;auto b=brush();BrushLibrary library(dir.filePath("brushes.json"));QVERIFY(library.load());
    QVERIFY(library.saveCurrent("Dynamic colors",b,0,&error));auto json=read(library.path());QCOMPARE(json["version"].toInt(),6);
    BrushLibrary restored(library.path());QVERIFY(restored.load(&error));QCOMPARE(restored.presets().last().brush,b);
    json["version"]=5;QVERIFY(write(library.path(),json));QVERIFY(!restored.load(&error));QVERIFY(error.contains("format 6"));QCOMPARE(restored.presets().last().brush,b);
    ToolCatalog catalog(nullptr,dir.filePath("tools.json"));QVERIFY(catalog.load());
    const auto id=catalog.createBrush("Dynamic stamp",BrushType::DynamicStamp,{b,0,{}});QVERIFY(!id.isEmpty());QVERIFY(catalog.save(&error));
    json=read(catalog.path());QCOMPARE(json["version"].toInt(),11);ToolCatalog loaded(nullptr,catalog.path());QVERIFY(loaded.load(&error));
    QCOMPARE(loaded.tool(id)->state.brush,b);QCOMPARE(loaded.tool(id)->brushType,BrushType::DynamicStamp);
    json["version"]=10;QVERIFY(write(catalog.path(),json));QVERIFY(!loaded.load(&error));QVERIFY(error.contains("version 11"));QCOMPARE(loaded.tool(id)->state.brush,b);
 }
 void unifiedTipVariationsAndImageColorOptIn(){
    Canvas canvas;canvas.setProject(Project::create());auto b=brush();b.colorDynamics.applyToColorTip=false;canvas.setBrushSettings(b);
    BrushSettingsPanel panel(&canvas);panel.show();panel.openAdvancedSettings();
    auto *tabs=panel.findChild<QTabBar*>("brushSettingsCategory");QVERIFY(tabs);tabs->setCurrentIndex(1);
    auto *size=panel.findChild<ValueSlider*>("stampSizeVariation"),*angle=panel.findChild<ValueSlider*>("stampRotationVariation"),*saturation=panel.findChild<ValueSlider*>("stampSaturationVariation");
    QVERIFY(size&&angle&&saturation);QCOMPARE(saturation->value(),0.);
    size->setValue(40);angle->setValue(20);saturation->setValue(30);
    QCOMPARE(canvas.brush.sizeDynamics.jitter,.4);QVERIFY(!canvas.brush.sizeDynamics.jitterPerStroke);
    QCOMPARE(canvas.brush.angleDynamics.jitter,.2);QVERIFY(!canvas.brush.angleDynamics.jitterPerStroke);
    QCOMPARE(canvas.brush.colorDynamics.saturation,.3);QVERIFY(canvas.brush.colorDynamics.enabled&&canvas.brush.colorDynamics.perTip&&canvas.brush.colorDynamics.applyToColorTip);
    QCOMPARE(canvas.brush.colorTip,b.colorTip);QCOMPARE(canvas.document()->history()->count(),0);
    const auto output=qEnvironmentVariable("TOON_DYNAMIC_STAMP_PROPERTIES_PREVIEW");
    if(!output.isEmpty()){QVERIFY(QDir().mkpath(output));auto *dialog=panel.findChild<QDialog*>("advancedBrushDialog");dialog->resize(480,800);QApplication::processEvents();QVERIFY(dialog->grab().save(QDir(output).filePath("dynamic-stamp-properties.png")));}
    tabs->setCurrentIndex(6);auto *optIn=panel.findChild<QCheckBox*>("brushVaryImageColors");QVERIFY(optIn&&optIn->isVisible());
    auto *hue=panel.findChild<ValueSlider*>("brushHueJitter");QVERIFY(hue->isEnabled());optIn->click();QVERIFY(!canvas.brush.colorDynamics.applyToColorTip);QVERIFY(!hue->isEnabled());
    QCOMPARE(saturation->value(),0.);QVERIFY(!panel.findChild<ValueSlider*>("brushForegroundBackground")->isVisible());
 }
};
QTEST_MAIN(DynamicStampStorageTests)
#include "test_dynamic_stamp_storage.moc"
