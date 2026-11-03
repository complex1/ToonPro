#include "brush_engine.h"
#include "brush_library.h"
#include "brush_management_panel.h"
#include "brush_tip_set_widget.h"
#include "brush_settings_panel.h"
#include "canvas.h"
#include "document.h"
#include "drawing_panels.h"
#include "catalog_brushes.h"
#include "object_tools.h"
#include "tool_catalog.h"
#include "value_slider.h"
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QListWidget>
#include <QLabel>
#include <QPainter>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>
#include <cmath>
using namespace toon;
namespace {
BrushSettings tipSet(bool color=false) {
    BrushSettings b;b.size=64;b.pressureSize=false;b.lockRandomSeed=true;b.randomSeed=901;b.spacing=.6;
    QVector<QImage> images;
    for(int i=0;i<6;++i){QImage image(32,32,QImage::Format_ARGB32_Premultiplied);image.fill(color?Qt::transparent:Qt::black);QPainter p(&image);p.setPen(Qt::NoPen);p.setBrush(color?QColor::fromHsv(i*55,220,200,190):Qt::white);p.drawRect(2+i*2,4,4+i,22-i*2);p.end();images.append(color?image:image.convertToFormat(QImage::Format_Grayscale8));}
    if(!setBrushTipImages(b,images))qFatal("Cannot create fixture");return b;
}
Project project(bool vector=false){auto p=Project::create();p.width=640;p.height=480;p.transparent=true;p.shots[0].layers[0].kind=vector?LayerKind::Vector:LayerKind::Raster;return p;}
void draw(Document &d,const BrushSettings &b){QVERIFY(d.beginStroke({80,200},1,b,false));for(int i=1;i<=30;++i)d.continueStroke({80+i*14.,200+std::sin(i*.2)*30},.8);d.endStroke();}
QByteArray read(const QString &path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return file.readAll();}
}
class AdvancedBrushTipsTests:public QObject{
 Q_OBJECT
private slots:
 void initTestCase(){applyTheme();}
 void deterministicBagsAndIndependentInputs(){
    auto b=tipSet();b.tipOrder=BrushTipOrder::Shuffle;bool differs=false;int previous=-1;
    for(quint64 block=0;block<100;++block){QSet<int> selected;for(quint64 i=0;i<6;++i){const auto dab=block*6+i;const int index=brushTipFrameIndex(b,{},0,dab,71);QVERIFY(index>=0&&index<6);QVERIFY(index!=previous);QVERIFY(!selected.contains(index));selected.insert(index);QCOMPARE(brushTipFrameIndex(b,{},0,dab,71),index);differs|=brushTipFrameIndex(b,{},0,dab,917)!=index;previous=index;}QCOMPARE(selected.size(),6);}QVERIFY(differs);
    b.tipOrder=BrushTipOrder::Ordered;for(int i=0;i<40;++i)QCOMPARE(brushTipFrameIndex(b,{},0,i,33),i%6);
    b.tipOrder=BrushTipOrder::Pressure;BrushInput input;input.pressure=0;QCOMPARE(brushTipFrameIndex(b,input,0,9,33),0);input.pressure=1;QCOMPARE(brushTipFrameIndex(b,input,0,9,33),5);
    b.tipOrder=BrushTipOrder::Direction;QCOMPARE(brushTipFrameIndex(b,{},180,0,1),3);QCOMPARE(brushTipFrameIndex(b,{},-180,0,1),3);
    b.tipOrder=BrushTipOrder::Random;int counts[6]{};b.tipFrames[0].weight=20;for(int i=0;i<10000;++i)++counts[brushTipFrameIndex(b,{},0,i,73)];QVERIFY(counts[0]>7000);for(int i=1;i<6;++i)QVERIFY(counts[i]>200);
 }
 void croppedFramesAndAnchoredRotation(){
    auto b=tipSet();b.tipOrder=BrushTipOrder::Ordered;for(int i=0;i<6;++i){const auto frame=brushTipFrameImage(b,i);QCOMPARE(frame.size(),QSize(32,32));QVERIFY(frame!=brushTipFrameImage(b,(i+1)%6));}
    b.tipFrames[0].anchor={.5,1};auto centered=b;centered.tipFrames[0].anchor={.5,.5};
    auto root=evaluateBrushDab(b,{}, {},0,0,0,1),middle=evaluateBrushDab(centered,{}, {},0,0,0,1);QCOMPARE(root.mask,middle.mask);QCOMPARE(root.offset,QPointF(0,-32));
    b.angle=90;root=evaluateBrushDab(b,{}, {},0,0,0,1);QVERIFY(QLineF(root.offset,QPointF(32,0)).length()<1e-8);
    const auto before=b;auto bad=brushSettingsToJson(b);auto set=bad["tipSet"].toObject();auto frames=set["frames"].toArray();auto f=frames[0].toArray();f[4]=2.;frames[0]=f;set["frames"]=frames;bad["tipSet"]=set;QString error;QVERIFY(!brushSettingsFromJson(bad,b,&error));QCOMPARE(b,before);
 }
 void packingIsAtomicAndMetadataIsStrict(){
    auto b=tipSet();const auto original=b;QString error;QVector<QImage> oversized(17,brushTipFrameImage(b,0));QVERIFY(!setBrushTipImages(b,oversized,{},&error));QCOMPARE(b,original);
    auto json=brushSettingsToJson(b);auto metadata=b;metadata.tipMask={};QVERIFY(brushSettingsFromJson(json,metadata,&error));QCOMPARE(metadata.tipFrames,b.tipFrames);metadata.tipMask=b.tipMask;QCOMPARE(metadata,b);
    json["tipSet"]=QJsonObject{{"order",99},{"frames",QJsonArray{QJsonArray{0,0,32,32,.5,.5,1}}}};QVERIFY(!brushSettingsFromJson(json,metadata,&error));
    auto invalid=b;invalid.tipFrames[0].rect=QRect(2040,0,32,32);QVERIFY(!validateBrushSettings(invalid));invalid=b;invalid.size=1024;QVERIFY(validateBrushSettings(invalid));invalid.size=1024.1;QVERIFY(!validateBrushSettings(invalid));
 }
 void rasterVectorReplayAndPersistence_data(){QTest::addColumn<bool>("colored");QTest::newRow("mask")<<false;QTest::newRow("color")<<true;}
 void rasterVectorReplayAndPersistence(){
    QFETCH(bool,colored);auto b=tipSet(colored);for(auto &f:b.tipFrames)f.anchor={.5,1};Document raster,vector;raster.load(project());vector.load(project(true));draw(raster,b);draw(vector,b);
    const auto expected=raster.activeShot().layers[0].tiles;QVERIFY(!expected.isEmpty());QCOMPARE(vector.activeShot().layers[0].tiles,expected);QCOMPARE(vector.activeShot().layers[0].objects.size(),1);
    auto layer=vector.activeShot().layers[0];QString error;QVERIFY2(rebuildObjectLayer(layer,vector.drawingBounds(),maxProjectTiles,&error),qPrintable(error));QCOMPARE(layer.tiles,expected);
    vector.history()->undo();QVERIFY(vector.activeShot().layers[0].tiles.isEmpty());vector.history()->redo();QCOMPARE(vector.activeShot().layers[0].tiles,expected);
    QTemporaryDir dir;const auto path=dir.filePath("tips.toonproj");QVERIFY2(saveProject(vector.project(),path,&error),qPrintable(error));QVERIFY(read(path).startsWith("TOONPRO24\n"));auto loaded=loadProject(path,&error);QVERIFY2(loaded.has_value(),qPrintable(error));QCOMPARE(loaded->toJson()["version"].toInt(),24);auto restored=loaded->shots[0].layers[0];QCOMPARE(restored.objects[0].brushStroke->settings,b);QVERIFY(rebuildObjectLayer(restored,vector.drawingBounds(),maxProjectTiles,&error));QCOMPARE(restored.tiles,expected);
    auto legacy=vector.project().toJson();legacy["version"]=23;QVERIFY(!Project::fromJson(legacy,&error));QVERIFY(error.contains("format 24"));
 }
 void libraryAndCatalogKeepImagesAndRefuseOldVersion(){
    QTemporaryDir dir;auto b=tipSet(true);QString error;BrushLibrary library(dir.filePath("brushes.json"));QVERIFY(library.load());QVERIFY(library.saveCurrent("Painted set",b,4,&error));auto saved=QJsonDocument::fromJson(read(library.path())).object();QCOMPARE(saved["version"].toInt(),5);BrushLibrary reloaded(library.path());QVERIFY(reloaded.load(&error));QCOMPARE(reloaded.presets().last().brush,b);
    ToolCatalog catalog(&library,dir.filePath("tools.json"));QVERIFY(catalog.load());const auto id=catalog.activeToolId();auto state=catalog.tool(id)->state;state.brush=b;QVERIFY(catalog.updateToolState(id,state));QVERIFY(catalog.save(&error));ToolCatalog next(&library,catalog.path());QVERIFY2(next.load(&error),qPrintable(error));QCOMPARE(next.tool(id)->state.brush,b);QCOMPARE(QJsonDocument::fromJson(read(catalog.path())).object()["version"].toInt(),12);
    saved["version"]=4;QFile file(library.path());QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(saved).toJson());file.close();QVERIFY(!reloaded.load(&error));QVERIFY(error.contains("format 5"));QCOMPARE(reloaded.presets().last().brush,b);
 }
 void missingAtlasAndDeviceScaleAreHandled(){
    auto b=tipSet();b.tipMask={};QVERIFY(!validateBrushTipImages(b));Document d;d.load(project());QVERIFY(!d.beginStroke({100,100},1,b,false));
    QTemporaryDir dir;BrushLibrary library(dir.filePath("brushes.json"));QVERIFY(library.load());QString error;QVERIFY(!library.saveCurrent("Missing tips",b,0,&error));QVERIFY(!QFile::exists(library.path()));
    QImage image(32,32,QImage::Format_Grayscale8);image.fill(255);image.setDevicePixelRatio(2);QVERIFY(setBrushTipImages(b,{image}));QCOMPARE(brushTipFrameImage(b,0).pixelColor(31,31),QColor(Qt::white));QCOMPARE(brushTipFrameImage(b,0).devicePixelRatio(),1.);
 }
 void creatingAReplacementStampClearsOldFrameMetadata(){
    QTemporaryDir dir;BrushLibrary library(dir.filePath("brushes.json"));QVERIFY(library.load());ToolCatalog catalog(&library,dir.filePath("tools.json"));QVERIFY(catalog.load());Canvas canvas;canvas.setProject(project());canvas.setBrushSettings(tipSet());BrushManagementPanel manager(&canvas,&library,&catalog);
    QImage image(24,32,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);{QPainter painter(&image);painter.fillRect(4,6,12,18,Qt::black);}
    QString error;const auto id=manager.addBrush("Single stamp",BrushType::Stamp,"My stamps",{}, {},image,true,&error);QVERIFY2(!id.isEmpty(),qPrintable(error));QVERIFY(catalog.tool(id));QVERIFY(catalog.tool(id)->state.brush.tipFrames.isEmpty());QCOMPARE(catalog.tool(id)->state.brush.tipMask.size(),image.size());
 }
 void largerTipsAreRealAndClipped(){
    auto b=tipSet();b.size=1024;b.tipOrder=BrushTipOrder::Ordered;const auto dab=evaluateBrushDab(b,{}, {},0,0,0,1);QVERIFY(dab.mask.width()>=1024);Document d;d.load(project());QVERIFY(d.beginStroke({320,240},1,b,false));d.endStroke();QVERIFY(!d.activeShot().layers[0].tiles.isEmpty());QVERIFY(d.activeShot().layers[0].tiles.size()<=16);d.history()->undo();QVERIFY(d.activeShot().layers[0].tiles.isEmpty());
 }
 void editorImportsRemovesOrdersAndAnchors(){
    QTemporaryDir dir;QImage image(24,32,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);{QPainter p(&image);p.fillRect(4,6,12,18,Qt::white);}const auto path=dir.filePath("tip.png");QVERIFY(image.save(path));BrushTipSetWidget widget;auto initial=tipSet();initial.tipFrames[0].weight=2;widget.setBrush(initial);widget.resize(240,290);widget.show();QSignalSpy edited(&widget,&BrushTipSetWidget::brushEdited);QString error;QVERIFY2(widget.addImages({path},&error),qPrintable(error));QVERIFY(!edited.isEmpty());auto b=edited.last()[0].value<BrushSettings>();QCOMPARE(b.tipFrames.size(),7);QCOMPARE(b.tipFrames[0].weight,2.);QVERIFY(!brushTipFrameImage(b,6).isNull());
    auto *list=widget.findChild<QListWidget*>("brushTipFrames");QVERIFY(list);list->setCurrentRow(6);widget.findChild<QToolButton*>("brushTipEarlier")->click();b=edited.last()[0].value<BrushSettings>();QCOMPARE(brushTipFrameImage(b,5).size(),QSize(24,32));widget.findChild<QToolButton*>("brushTipRemove")->click();b=edited.last()[0].value<BrushSettings>();QCOMPARE(b.tipFrames.size(),6);
    list->setCurrentRow(0);auto *anchor=widget.findChild<QWidget*>("brushTipAnchor");QVERIFY(anchor);QTest::keyClick(anchor,Qt::Key_End);b=edited.last()[0].value<BrushSettings>();QCOMPARE(b.tipFrames[0].anchor,QPointF(.5,1));
    const auto directory=qEnvironmentVariable("TOON_BRUSH_TIPS_PREVIEW");if(!directory.isEmpty()){QDir().mkpath(directory);QVERIFY(widget.grab().save(directory+"/tip-editor.png"));}
    const auto original=b;QVERIFY(!widget.addImages({dir.filePath("missing.png")},&error));QCOMPARE(edited.last()[0].value<BrushSettings>(),original);
 }
 void editorAddsMultipleImagesAtomically_data(){QTest::addColumn<bool>("colored");QTest::newRow("foreground")<<false;QTest::newRow("image-colors")<<true;}
 void editorAddsMultipleImagesAtomically(){
    QFETCH(bool,colored);QTemporaryDir dir;QVERIFY(dir.isValid());QStringList paths;
    for(int i=0;i<2;++i){QImage image(24,32,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);image.setPixelColor(5,6,QColor(24+i*40,160,210,128));const auto path=dir.filePath(QString("stamp-%1.png").arg(i));QVERIFY(image.save(path));paths.append(path);}
    BrushTipSetWidget widget;const auto initial=tipSet(colored);widget.setBrush(initial);QSignalSpy edited(&widget,&BrushTipSetWidget::brushEdited);QString error;
    QVERIFY2(widget.addImages(paths,&error),qPrintable(error));QCOMPARE(edited.size(),1);auto result=edited.last()[0].value<BrushSettings>();QCOMPARE(result.tipFrames.size(),8);
    for(int i=0;i<6;++i)QCOMPARE(brushTipFrameImage(result,i),brushTipFrameImage(initial,i));
    const auto added=brushTipFrameImage(result,6);QCOMPARE(added.size(),QSize(24,32));
    if(colored){
        QCOMPARE(added.format(),QImage::Format_ARGB32_Premultiplied);
        // QColor expands premultiplied pixels to 16-bit precision; compare the
        // complete imported image in its actual 8-bit storage representation.
        QCOMPARE(added,QImage(paths[0]).convertToFormat(QImage::Format_ARGB32_Premultiplied));
    }
    else{QCOMPARE(added.format(),QImage::Format_Grayscale8);QCOMPARE(added.constScanLine(6)[5],128);QCOMPARE(added.constScanLine(0)[0],0);}
    QCOMPARE(widget.findChild<QLabel*>("brushTipCount")->text(),QString("Stamps 8/16"));
    // A failure after one readable image must not append a partial set or emit an edit.
    QVERIFY(!widget.addImages({paths[0],dir.filePath("missing.png")},&error));QCOMPARE(edited.size(),1);
    QStringList remaining;for(int i=0;i<8;++i)remaining.append(paths[i%2]);QVERIFY2(widget.addImages(remaining,&error),qPrintable(error));QCOMPARE(edited.last()[0].value<BrushSettings>().tipFrames.size(),16);
    QVERIFY(!widget.findChild<QToolButton*>("brushTipAdd")->isEnabled());QCOMPARE(widget.findChild<QLabel*>("brushTipCount")->text(),QString("Stamps 16/16"));
 }
 void deletingEditorWithAValueDraftIsSafe(){
    auto *widget=new BrushTipSetWidget;auto b=tipSet();b.tipOrder=BrushTipOrder::Random;widget->setBrush(b);widget->show();QSignalSpy edited(widget,&BrushTipSetWidget::brushEdited);
    auto *weight=widget->findChild<ValueSlider*>("brushTipWeight");QVERIFY(weight);auto *number=weight->findChild<QDoubleSpinBox*>();QVERIFY(number);number->setFocus();number->selectAll();QSignalSpy started(weight,&ValueSlider::editingStarted);QTest::keyClicks(number,"2.3");QVERIFY(!started.isEmpty());const auto count=edited.size();delete widget;QCOMPARE(edited.size(),count);
 }
 void roundTipsKeepSetControlsCompact(){
    BrushTipSetWidget widget;widget.setBrush(BrushSettings{});widget.show();
    auto *list=widget.findChild<QListWidget*>("brushTipFrames");auto *order=widget.findChild<QTabBar*>("brushTipOrder");auto *weight=widget.findChild<ValueSlider*>("brushTipWeight");QVERIFY(list);QVERIFY(order);QVERIFY(weight);
    QVERIFY(list->isHidden());QVERIFY(order->isHidden());QVERIFY(widget.findChild<QWidget*>("brushTipAnchor")->isHidden());QVERIFY(weight->parentWidget()->isHidden());QVERIFY(!widget.findChild<QToolButton*>("brushTipAdd")->isHidden());
    widget.setBrush(tipSet());QVERIFY(!list->isHidden());QVERIFY(!order->isHidden());QVERIFY(weight->parentWidget()->isHidden());order->setCurrentIndex(int(BrushTipOrder::Random));QVERIFY(!weight->parentWidget()->isHidden());QVERIFY(weight->isEnabled());order->setCurrentIndex(int(BrushTipOrder::Ordered));QVERIFY(weight->parentWidget()->isHidden());
 }
 void choosingRoundClearsTheCollectionConsistently(){
    Canvas canvas;canvas.setProject(project());auto b=tipSet();b.tipOrder=BrushTipOrder::Random;canvas.setBrushSettings(b);BrushSettingsPanel panel(&canvas);panel.openAdvancedSettings();auto *picker=panel.findChild<QComboBox*>("brushTipPicker");QVERIFY(picker);picker->setCurrentIndex(0);QVERIFY(canvas.brush.tipFrames.isEmpty());QVERIFY(canvas.brush.tipMask.isNull());QCOMPARE(canvas.brush.tipOrder,BrushTipOrder::Shuffle);auto restored=canvas.brush;QVERIFY(brushSettingsFromJson(brushSettingsToJson(canvas.brush),restored));QCOMPARE(restored,canvas.brush);panel.findChild<QDialog*>("advancedBrushDialog")->close();
 }
};
QTEST_MAIN(AdvancedBrushTipsTests)
#include "test_advanced_brush_tips.moc"
