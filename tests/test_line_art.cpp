#include "line_art.h"
#include "line_art_dialog.h"
#include "line_art_document.h"
#include "document.h"
#include "canvas.h"
#include "main_window.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <limits>

using namespace toon;
namespace {
QImage drawingImage(){QImage image(192,128,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);QPainter p(&image);p.setPen(QPen(Qt::black,3));p.drawRect(20,20,140,85);p.drawLine(20,55,160,55);p.drawLine(70,20,70,105);return image;}
int coverage(const QImage &image){int count=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)count+=image.pixelColor(x,y).alpha()>20;return count;}
Project drawingProject(QImage image=drawingImage()){
    auto p=Project::create();p.width=image.width();p.height=image.height();auto &layer=p.shots[0].layers[0];layer.name="Reference";
    for(int y=0;y<image.height();y+=tileSize)for(int x=0;x<image.width();x+=tileSize){QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);QPainter painter(&tile);painter.drawImage(-x,-y,image);painter.end();layer.tiles[{x/tileSize,y/tileSize}]=tile;}return p;
}

}
class LineArtTest:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void imageLinesPreserveSourceAndTransparency(){
        const auto image=drawingImage(),before=image.copy();LineArtSettings s;auto result=convertLineArt({image,image.size()},s);
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QVERIFY(coverage(result.lines)>200);QVERIFY(coverage(result.lines)<5000);QCOMPARE(result.lines.pixelColor(100,85).alpha(),0);QCOMPARE(image,before);
        QImage empty(64,64,QImage::Format_ARGB32_Premultiplied);empty.fill(Qt::transparent);result=convertLineArt({empty,empty.size()},s);QCOMPARE(coverage(result.lines),0);
        empty.fill(Qt::white);result=convertLineArt({empty,empty.size()},s);QCOMPARE(coverage(result.lines),0);
    }
    void toneBandsAreSeparateAndAlphaIsPreserved(){
        QImage image(120,40,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter p(&image);p.fillRect(0,0,40,40,QColor(40,40,40,128));p.fillRect(40,0,40,40,QColor(120,120,120));p.fillRect(80,0,40,40,QColor(230,230,230));p.end();
        LineArtSettings s;s.shading=LineArtShading::Cel;s.base=true;const auto result=convertLineArt({image,image.size()},s);
        QVERIFY(result.error.isEmpty());QCOMPARE(result.deepShade.pixelColor(20,20).alpha(),128);QCOMPARE(result.shade.pixelColor(20,20).alpha(),0);
        QCOMPARE(result.shade.pixelColor(60,20),s.shade);QCOMPARE(result.deepShade.pixelColor(60,20).alpha(),0);QCOMPARE(result.shade.pixelColor(100,20).alpha(),0);QCOMPARE(result.base.pixelColor(100,20),QColor(Qt::white));
        QVERIFY(qAbs(result.composite.pixelColor(20,20).alpha()-128)<=1);
        s.shading=LineArtShading::Grayscale;const auto gray=convertLineArt({image,image.size()},s);QCOMPARE(gray.shade.pixelColor(60,20),QColor(120,120,120));
        image.fill(QColor(245,245,245,128));s.shading=LineArtShading::Screentone;
        const auto light=convertLineArt({image,image.size()},s);QVERIFY(qAbs(light.composite.pixelColor(20,20).alpha()-128)<=1);
    }
    void screentonesAreStableAndPreviewIsNotFinalResolution(){
        QImage image(512,256,QImage::Format_ARGB32_Premultiplied);image.fill(QColor(120,120,120));LineArtSettings s;s.shading=LineArtShading::Screentone;
        const auto a=convertLineArt({image,image.size()},s),b=convertLineArt({image,image.size()},s);QCOMPARE(a.shade,b.shade);QVERIFY(coverage(a.shade)>1000);QVERIFY(coverage(a.shade)<image.width()*image.height());
        const auto preview=convertLineArt({image,image.size()},s,{},128);QCOMPARE(preview.lines.size(),QSize(128,64));QCOMPARE(a.lines.size(),image.size());
    }
    void cancellationAndInvalidInputsDoNotPublish(){
        auto cancellation=std::make_shared<std::atomic_bool>(true);auto image=drawingImage();QVERIFY(convertLineArt({image,image.size()},{},cancellation).cancelled);
        LineArtSettings s;s.detail=std::numeric_limits<double>::quiet_NaN();QVERIFY(!convertLineArt({image,image.size()},s).error.isEmpty());
        QVERIFY(!convertLineArt({image,QSize(90000,90000)},{}).error.isEmpty());s={};s.darkCutoff=s.lightCutoff;QVERIFY(!validateLineArtSettings(s).isEmpty());
    }
    void applyIsAtomicEditableAndSurvivesStorage(){
        Document doc;doc.load(drawingProject());const auto source=doc.activeShot().layers[0];QString error;const auto input=lineArtSource(doc.project(),0,source.id,0,&error);QVERIFY2(error.isEmpty(),qPrintable(error));
        LineArtSettings s;s.shading=LineArtShading::Cel;s.base=true;const auto result=convertLineArt(input,s);QVERIFY(doc.createLineArtLayers(source.id,doc.contentRevision(),0,result,true));
        QCOMPARE(doc.history()->count(),1);QCOMPARE(doc.activeShot().layers.first().tiles,source.tiles);QVERIFY(!doc.activeShot().layers.first().visible);QCOMPARE(doc.activeShot().layers.last().kind,LayerKind::Raster);QVERIFY(!doc.activeShot().layers.last().parentId.isEmpty());
        QTemporaryDir d;QVERIFY2(saveProject(doc.project(),d.filePath("conversion.toonproj"),&error),qPrintable(error));const auto loaded=loadProject(d.filePath("conversion.toonproj"),&error);QVERIFY2(loaded.has_value(),qPrintable(error));QCOMPARE(loaded->shots[0].layers.last().tiles,doc.activeShot().layers.last().tiles);
        doc.history()->undo();QCOMPARE(doc.activeShot().layers.size(),1);QVERIFY(doc.activeShot().layers[0].visible);doc.history()->redo();QVERIFY(doc.activeShot().layers.size()>1);
        const auto count=doc.history()->count();doc.beginStroke({40,40},1,BrushSettings{},false);doc.endStroke();QVERIFY(doc.history()->count()>count);
    }
    void staleResultLockedFolderAndPreviewPixelsCannotCommit(){
        Document doc;doc.load(drawingProject());const auto id=doc.activeShot().layers[0].id;const auto revision=doc.contentRevision();const auto result=convertLineArt(lineArtSource(doc.project(),0,id,0),{});
        doc.changeLayer("Changed",true,false,false,1);QVERIFY(!doc.createLineArtLayers(id,revision,0,result,false));doc.history()->undo();QVERIFY(doc.contentRevision()!=revision);QVERIFY(!doc.createLineArtLayers(id,revision,0,result,false));
        const auto preview=convertLineArt(lineArtSource(doc.project(),0,id,0),{},{},64);QVERIFY(!doc.createLineArtLayers(id,doc.contentRevision(),0,preview,false));
        auto p=drawingProject();auto folder=RasterLayer::create("Locked folder");folder.group=true;folder.locked=true;p.shots[0].layers[0].parentId=folder.id;p.shots[0].layers.append(folder);QVERIFY(!lineArtSourceReason(p,0,p.shots[0].layers[0].id,0).isEmpty());
        p=drawingProject();p.shots[0].layers[0].locked=true;doc.load(p);const auto locked=p.shots[0].layers[0].id;const auto lockedResult=convertLineArt(lineArtSource(p,0,locked,0),{});
        QVERIFY(!doc.createLineArtLayers(locked,doc.contentRevision(),0,lockedResult,true));QVERIFY(doc.createLineArtLayers(locked,doc.contentRevision(),0,lockedResult,false));QVERIFY(doc.activeShot().layers[0].locked);
    }
    void conversionRemainsInsideTheCurrentAnimationDrawing(){
        Document doc;doc.load(drawingProject());QVERIFY(doc.convertLayerToAnimation("Independent shot"));
        const auto id=doc.activeShot().layers[doc.activeLayer()].id,parent=doc.activeShot().layers[doc.activeLayer()].parentId;
        const auto input=lineArtSource(doc.project(),0,id,0);const auto result=convertLineArt(input,{});
        QVERIFY(doc.createLineArtLayers(id,doc.contentRevision(),0,result,false));
        const auto &layers=doc.activeShot().layers;const auto output=layers[doc.activeLayer()];
        bool found=false;for(const auto &layer:layers)if(layer.id==output.parentId){QCOMPARE(layer.parentId,parent);found=true;}
        QVERIFY(found);QVERIFY(doc.project().validate().isEmpty());QCOMPARE(doc.activeShot().animationTracks.size(),1);
    }
    void clippingStacksAndDirect3DInputAreRefused(){
        auto p=drawingProject();const auto id=p.shots[0].layers[0].id;auto clipped=RasterLayer::create("Clipped detail");clipped.clipped=true;p.shots[0].layers.append(clipped);
        QVERIFY(lineArtSourceReason(p,0,id,0).contains("clipping"));
        p=drawingProject();p.shots[0].layers[0].kind=LayerKind::Scene3D;p.shots[0].layers[0].scene3d=Scene3D{};
        QVERIFY(lineArtSourceReason(p,0,p.shots[0].layers[0].id,0).contains("deferred"));
    }
    void multiTileOutputMatchesTheFullResolutionComposite(){
        QImage image(420,300,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);
        {QPainter painter(&image);painter.fillRect(270,260,130,30,QColor(90,90,90));painter.setPen(QPen(Qt::black,3));painter.drawRect(15,15,390,270);}
        Document doc;doc.load(drawingProject(image));const auto id=doc.activeShot().layers[0].id;LineArtSettings s;s.shading=LineArtShading::Cel;s.base=true;
        const auto result=convertLineArt(lineArtSource(doc.project(),0,id,0),s);QVERIFY(doc.createLineArtLayers(id,doc.contentRevision(),0,result,true));
        const auto actual=renderMovieFrame(doc.project());
        if(actual!=result.composite){
            int differences=0;for(int y=0;y<actual.height();++y)for(int x=0;x<actual.width();++x)
                if(actual.pixel(x,y)!=result.composite.pixel(x,y))++differences;
            qWarning()<<"Output/preview pixel differences:"<<differences<<actual.size()<<result.composite.size();
        }
        QCOMPARE(actual,result.composite);
    }
    void dialogPreviewExactEntryAndCancelNeverEditSource(){
        Document doc;doc.load(drawingProject());const auto before=doc.project().toJson();LineArtDialog dialog(&doc);dialog.show();QSignalSpy ready(&dialog,&LineArtDialog::previewReady);QTRY_VERIFY_WITH_TIMEOUT(ready.count()>0,10000);QVERIFY(!dialog.busy());
        auto *number=dialog.findChild<ValueSlider *>("lineArtWidth")->findChild<QDoubleSpinBox *>();QVERIFY(number);number->setFocus();QTest::keyClick(number,Qt::Key_Return);QCOMPARE(doc.history()->count(),0);
        auto cel=dialog.settings();cel.shading=LineArtShading::Cel;dialog.setSettings(cel);dialog.findChild<QTabBar *>("lineArtCategories")->setCurrentIndex(1);
        auto *ramp=dialog.findChild<QWidget *>("lineArtRamp");QVERIFY(ramp);QTest::keyClick(ramp,Qt::Key_Right);QCOMPARE(dialog.settings().darkCutoff,86);
        QTest::mousePress(ramp,Qt::LeftButton,{},QPoint(10+(ramp->width()-20)/3,25));QTest::mouseMove(ramp,QPoint(ramp->width()-10,25));QTest::mouseRelease(ramp,Qt::LeftButton,{},QPoint(ramp->width()-10,25));
        QVERIFY(dialog.settings().darkCutoff<dialog.settings().lightCutoff);
        auto settings=dialog.settings();settings.detail=25;dialog.setSettings(settings);settings.detail=90;dialog.setSettings(settings);QTRY_VERIFY_WITH_TIMEOUT(!dialog.busy(),10000);QCOMPARE(dialog.settings().detail,90.);
        QCOMPARE(doc.project().toJson(),before);dialog.close();QCOMPARE(doc.history()->count(),0);
    }
    void dialogCreatesFullResolutionAndRefusesChangedSource(){
        Document doc;doc.load(drawingProject());LineArtDialog dialog(&doc);dialog.show();QTRY_VERIFY_WITH_TIMEOUT(dialog.findChild<QPushButton *>("lineArtCreate")->isEnabled(),10000);dialog.createLayers();QTRY_COMPARE_WITH_TIMEOUT(dialog.result(),int(QDialog::Accepted),10000);QCOMPARE(doc.history()->count(),1);
        doc.history()->undo();doc.selectLayer(0);LineArtDialog stale(&doc);stale.show();doc.changeLayer("New name",true,false,false,1);QTRY_VERIFY_WITH_TIMEOUT(!stale.busy(),10000);QVERIFY(!stale.findChild<QPushButton *>("lineArtCreate")->isEnabled());stale.createLayers();QCOMPARE(doc.activeShot().layers.size(),1);
    }
    void backgroundCleanupAndDenoiseOnlyChangePreviewSettings(){
        Document doc;doc.load(drawingProject());const auto before=doc.project().toJson();
        LineArtDialog dialog(&doc);auto settings=dialog.settings();settings.ink=Qt::red;settings.shading=LineArtShading::Cel;settings.base=true;dialog.setSettings(settings);dialog.show();
        auto *preset=dialog.findChild<QPushButton *>("lineArtBackgroundPreset");QVERIFY(preset);QVERIFY(!preset->accessibleName().isEmpty());QTest::mouseClick(preset,Qt::LeftButton);
        QCOMPARE(dialog.settings().shading,LineArtShading::Cel);QCOMPARE(dialog.settings().ink,QColor(Qt::red));QVERIFY(dialog.settings().base);
        auto *denoise=dialog.findChild<ValueSlider *>("lineArtTextureSuppression");QVERIFY(denoise);QCOMPARE(denoise->value(),70.);
        auto *entry=denoise->findChild<QDoubleSpinBox *>();QVERIFY(entry);entry->setFocus();entry->selectAll();QTest::keyClicks(entry,"42");QTest::keyClick(entry,Qt::Key_Return);QCOMPARE(dialog.settings().textureSuppression,42.);
        QTRY_VERIFY_WITH_TIMEOUT(!dialog.busy(),10000);QVERIFY(dialog.previewResult().error.isEmpty());QCOMPARE(doc.project().toJson(),before);QCOMPARE(doc.history()->count(),0);QCOMPARE(dialog.result(),0);
        dialog.close();
    }
    void mainWindowActionIsReachableAndModalCancelIsSafe(){
        QTemporaryDir d;MainWindow window(d.filePath("workspace.ini"));auto *action=window.findChild<QAction *>("convertToLineArtAction");QVERIFY(action);QVERIFY(!action->isEnabled());window.canvas()->setProject(drawingProject());QVERIFY(action->isEnabled());window.resize(1150,800);window.show();
        bool seen=false;QTimer::singleShot(150,&window,[&]{auto *dialog=window.findChild<LineArtDialog *>();seen=dialog!=nullptr;if(dialog)dialog->close();});action->trigger();QVERIFY(seen);QCOMPARE(window.canvas()->document()->history()->count(),0);
    }
    void emptyConversionExplainsTheFailureWithoutHidingTheSource(){
        QImage white(64,64,QImage::Format_ARGB32_Premultiplied);white.fill(Qt::white);Document doc;doc.load(drawingProject(white));
        LineArtDialog dialog(&doc);dialog.show();QTRY_VERIFY_WITH_TIMEOUT(dialog.findChild<QPushButton *>("lineArtCreate")->isEnabled(),10000);dialog.createLayers();
        QTRY_VERIFY_WITH_TIMEOUT(dialog.findChild<QLabel *>("lineArtStatus")->text().contains("No lines or tones"),10000);
        QVERIFY(doc.activeShot().layers[0].visible);QCOMPARE(doc.activeShot().layers.size(),1);QCOMPARE(doc.history()->count(),0);
    }
    void captureReviewedWorkflows(){
        const QString directory=qEnvironmentVariable("TOON_LINE_ART_CAPTURE_DIR");if(directory.isEmpty())return;QDir().mkpath(directory);
        QImage art(QString(TOON_TEST_SOURCE_DIR)+"/tests/fixtures/managed-project/blue-hour.png");if(art.isNull())art=drawingImage();art=art.scaled(1280,720,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        Document doc;doc.load(drawingProject(art));LineArtDialog image(&doc);image.resize(1080,740);image.show();LineArtSettings options;options.smoothing=1.5;image.setSettings(options);QTRY_VERIFY_WITH_TIMEOUT(!image.busy(),30000);QVERIFY(image.previewResult().error.isEmpty());
        QVERIFY(image.grab().save(directory+"/image-lines.png"));QVERIFY(image.previewResult().lines.save(directory+"/lines-result.png"));
        options.shading=LineArtShading::Cel;options.base=true;image.setSettings(options);QTRY_VERIFY_WITH_TIMEOUT(!image.busy(),30000);
        QVERIFY(image.grab().save(directory+"/image-conversion.png"));QVERIFY(image.previewResult().composite.save(directory+"/image-result.png"));image.close();
        Document compactDoc;compactDoc.load(drawingProject(art));LineArtDialog compact(&compactDoc);compact.resize(760,500);compact.show();options.shading=LineArtShading::Screentone;compact.setSettings(options);compact.findChild<QTabBar *>("lineArtCategories")->setCurrentIndex(1);QTRY_VERIFY_WITH_TIMEOUT(!compact.busy(),30000);
        QVERIFY(compact.grab().save(directory+"/compact-conversion.png"));
        compact.close();
        const QImage photo(qEnvironmentVariable("TOON_LINE_ART_QUALITY_PHOTO"));
        if(!photo.isNull()){
            Document backgroundDoc;backgroundDoc.load(drawingProject(photo.convertToFormat(QImage::Format_ARGB32_Premultiplied)));LineArtDialog background(&backgroundDoc);background.resize(760,500);background.show();
            QTest::mouseClick(background.findChild<QPushButton *>("lineArtBackgroundPreset"),Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(!background.busy(),30000);QVERIFY(background.previewResult().error.isEmpty());
            QVERIFY(background.grab().save(directory+"/background-cleanup.png"));
        }
    }
};
QTEST_MAIN(LineArtTest)
#include "test_line_art.moc"
