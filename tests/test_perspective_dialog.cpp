#include "perspective_dialog.h"
#include "perspective_source.h"
#include "document.h"
#include "canvas.h"
#include "guide_tool.h"
#include "guide_geometry.h"
#include "guide_panel.h"
#include "main_window.h"
#include "effect_render.h"
#include "keyframe_render.h"
#include "property_animation.h"
#include "animation_render.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QDialogButtonBox>
#include <QAction>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QtTest>
#include <cmath>
#include <limits>

using namespace toon;
namespace {
QImage streetImage(){
    QImage image(640,480,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);
    QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(QColor(30,38,46),2));const QPointF vp(322,192);
    const QVector<QPointF> ends{{24,25},{220,15},{460,20},{615,90},{620,330},{540,460},{300,460},{90,460},{20,280}};
    for(const auto end:ends)p.drawLine(vp+(end-vp)*.22,end);p.drawRect(QRectF(170,100,285,205));return image;
}
RasterLayer imageLayer(const QImage &image){
    auto layer=RasterLayer::create("Street reference");
    for(int y=0;y<image.height();y+=tileSize)for(int x=0;x<image.width();x+=tileSize){QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);{QPainter p(&tile);p.drawImage(-x,-y,image);}layer.tiles.insert({x/tileSize,y/tileSize},tile);}return layer;
}
Project fixture(bool street=true){
    auto project=Project::create(ProjectKind::DrawingOnly);project.width=640;project.height=480;project.transparent=true;project.shots[0].durationFrames=8;project.shots[0].sheet.overscan=0;
    auto image=streetImage();if(!street)image.fill(Qt::white);auto source=imageLayer(image);source.locked=true;
    project.shots[0].layers={source,RasterLayer::create("Colors")};return project;
}
QString status(const PerspectiveDialog &dialog){const auto *label=dialog.findChild<QLabel*>("perspectiveStatus");return label?label->text():QString();}
bool waitForPreview(PerspectiveDialog &dialog){QElapsedTimer timer;timer.start();while(dialog.busy()&&timer.elapsed()<20000)QTest::qWait(10);return !dialog.busy();}
QPushButton *addButton(PerspectiveDialog &dialog){return dialog.findChild<QPushButton*>("perspectiveAddGuides");}
int nearestFinite(const PerspectiveDetectionResult &result,QPointF point){int index=-1;qreal best=1e30;for(int i=0;i<result.candidates.size();++i)if(!result.candidates[i].parallel){const auto distance=QLineF(result.candidates[i].point,point).length();if(distance<best){index=i;best=distance;}}return index;}
void chooseOnly(PerspectiveDialog &dialog,int candidate){auto *list=dialog.findChild<QListWidget*>("perspectiveCandidates");for(int i=0;i<list->count();++i)list->item(i)->setCheckState(i==candidate?Qt::Checked:Qt::Unchecked);}
QPoint previewPoint(PerspectiveDialog &dialog,QPointF imagePoint){
    auto *view=dialog.findChild<QWidget*>("perspectivePreview");const auto size=dialog.source().image.size();const qreal zoom=qMin((view->width()-44.)/size.width(),(view->height()-44.)/size.height());
    return (QPointF(view->width()/2.,view->height()/2.)+(imagePoint-QPointF(size.width()/2.,size.height()/2.))*zoom).toPoint();
}
void drawLine(PerspectiveDialog &dialog,QLineF line){auto *view=dialog.findChild<QWidget*>("perspectivePreview");const auto a=previewPoint(dialog,line.p1()),b=previewPoint(dialog,line.p2());QTest::mousePress(view,Qt::LeftButton,Qt::NoModifier,a);QMouseEvent move(QEvent::MouseMove,b,view->mapToGlobal(b),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&move);QTest::mouseRelease(view,Qt::LeftButton,Qt::NoModifier,b);}
}
class PerspectiveDialogTests:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void lockedNestedLayerAndFolderCapturePreserveWorldPlacement(){
        auto project=fixture(false);QImage image(640,480,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);{QPainter p(&image);p.fillRect(30,40,40,30,Qt::red);}
        auto child=imageLayer(image),folder=RasterLayer::create("Placed folder");child.locked=true;folder.group=true;folder.locked=true;child.parentId=folder.id;project.shots[0].layers={child,folder};
        project.shots[0].layerAnimations={{folder.id,{},{{KeyframeProperty::PositionX,{{0,80}}},{KeyframeProperty::PositionY,{{0,40}}}}},{child.id,{},{{KeyframeProperty::PositionX,{{0,12}}},{KeyframeProperty::PositionY,{{0,18}}}}}};
        const auto before=project.toJson();const auto tiles=project.shots[0].layers[0].tiles;
        const auto captured=capturePerspectiveSource(project,0,child.id,0,false);QVERIFY2(captured.error.isEmpty(),qPrintable(captured.error));QVERIFY(!captured.image.isNull());QCOMPARE(captured.image.pixelColor(130,110),QColor(Qt::red));QCOMPARE(captured.image.pixelColor(40,50).alpha(),0);QCOMPARE(captured.imageToArtwork.map(QPointF(130,110)),QPointF(130,110));
        const auto group=capturePerspectiveSource(project,0,folder.id,0,false);QVERIFY2(group.error.isEmpty(),qPrintable(group.error));QCOMPARE(group.image,captured.image);
        QCOMPARE(project.toJson(),before);QCOMPARE(project.shots[0].layers[0].tiles,tiles);
    }
    void localImageFitMappingAndFailuresKeepTheFileIntact(){
        QTemporaryDir directory;const auto path=directory.filePath("street.png");const auto image=streetImage();QVERIFY(image.save(path));QFile original(path);QVERIFY(original.open(QIODevice::ReadOnly));const auto bytes=original.readAll();original.close();
        const auto loaded=loadPerspectiveImage(path,{1000,1000});QVERIFY2(loaded.error.isEmpty(),qPrintable(loaded.error));QCOMPARE(loaded.image,image);QCOMPARE(loaded.imageToArtwork.map(QPointF(0,0)),QPointF(0,125));QCOMPARE(loaded.imageToArtwork.map(QPointF(640,480)),QPointF(1000,875));
        QVERIFY(!loadPerspectiveImage(directory.filePath("missing.png"),{1000,1000}).error.isEmpty());std::atomic_bool cancel{true};QVERIFY(loadPerspectiveImage(path,{1000,1000},&cancel).image.isNull());
        QVERIFY(original.open(QIODevice::ReadOnly));QCOMPARE(original.readAll(),bytes);
    }
    void selectedBranchRetainsAncestorMaskEffectsAndOpacity(){
        auto project=fixture(false);QImage image(640,480,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);{QPainter p(&image);p.fillRect(30,40,100,60,Qt::red);}auto source=imageLayer(image),folder=RasterLayer::create("Masked outlined parent"),unrelated=imageLayer(image);source.parentId=folder.id;source.locked=true;folder.group=true;folder.locked=true;folder.opacity=.5;folder.effects.outlineEnabled=true;folder.effects.outlineWidth=4;folder.effects.outlineColor=Qt::blue;
        LayerMask mask;QImage coverage(tileSize,tileSize,QImage::Format_Grayscale8);coverage.fill(255);for(int y=0;y<tileSize;++y)for(int x=0;x<80;++x)coverage.scanLine(y)[x]=0;mask.tiles[{0,0}]=coverage;folder.mask=mask;
        project.shots[0].layers={source,folder,unrelated};const auto before=project.toJson();const auto captured=capturePerspectiveSource(project,0,source.id,0,false);QVERIFY2(captured.error.isEmpty(),qPrintable(captured.error));
        auto expectedShot=project.shots[0];expectedShot.layers={source,folder};QString error;const auto expectedLayers=evaluateLayers(project,expectedShot,0,false,{0,0,640,480},&error);QVERIFY2(error.isEmpty(),qPrintable(error));QImage expected(640,480,QImage::Format_ARGB32_Premultiplied);expected.fill(Qt::transparent);{QPainter p(&expected);paintLayers(p,expectedLayers,true,nullptr,true,{640,480});}
        QCOMPARE(captured.image,expected);QCOMPARE(captured.image.pixelColor(40,50).alpha(),0);QVERIFY(captured.image.pixelColor(90,60).alpha()>100);QVERIFY(captured.image.pixelColor(78,70).blue()>0);QCOMPARE(project.toJson(),before);
    }
    void detectedGuideAddsOneUndoSavesExactlyAndStaysOutOfExports(){
        Document document;document.load(fixture());const auto source=document.activeShot().layers[0].tiles;const auto originalFrame=renderMovieFrame(document.project());PerspectiveDialog dialog(&document);dialog.show();
        QVERIFY2(waitForPreview(dialog),qPrintable(status(dialog)));const int candidate=nearestFinite(dialog.previewResult(),{322,192});QVERIFY2(candidate>=0,qPrintable(status(dialog)));QVERIFY(QLineF(dialog.previewResult().candidates[candidate].point,{322,192}).length()<10);chooseOnly(dialog,candidate);
        auto *density=dialog.findChild<ValueSlider*>("perspectiveDensity");QVERIFY(density);auto *entry=density->findChild<QDoubleSpinBox*>();QVERIFY(entry);entry->setFocus();entry->selectAll();QTest::keyClicks(entry,"12");QTest::keyClick(entry,Qt::Key_Return);
        const auto expected=dialog.previewResult().candidates[candidate].point;QVERIFY(addButton(dialog)->isEnabled());QSignalSpy added(&dialog,&PerspectiveDialog::guidesAdded);addButton(dialog)->click();QCOMPARE(added.count(),1);QCOMPARE(dialog.result(),int(QDialog::Accepted));QCOMPARE(document.history()->count(),1);QCOMPARE(document.activeShot().guides.size(),1);const auto guide=document.activeShot().guides[0];QCOMPARE(guide.type,GuideType::VanishingPoint);QCOMPARE(guide.points,QVector<QPointF>{expected});QCOMPARE(guide.subdivisions,12);QVERIFY(!guide.id.isEmpty());QCOMPARE(document.activeShot().layers[0].tiles,source);QCOMPARE(renderMovieFrame(document.project()),originalFrame);
        QTemporaryDir directory;const auto path=directory.filePath("perspective.toonproj");QString error;QVERIFY2(saveProject(document.project(),path,&error),qPrintable(error));const auto reopened=loadProject(path,&error);QVERIFY2(reopened,qPrintable(error));QCOMPARE(reopened->shots[0].guides,document.activeShot().guides);QCOMPARE(reopened->shots[0].layers[0].tiles,source);
        document.history()->undo();QVERIFY(document.activeShot().guides.isEmpty());document.history()->redo();QCOMPARE(document.activeShot().guides[0],guide);
        auto snapping=*reopened;snapping.shots[0].guideSettings.snapping=true;Canvas canvas;canvas.setProject(snapping);GuideTool assistance(&canvas);const QPointF start(410,390);assistance.beginSnap(start);const auto point=assistance.snap({430,410});QVERIFY(point);QVERIFY(guideLineDistance(*point,QLineF(start,expected))<.001);
    }
    void candidateSelectionEdgeExclusionAndRegionAreTemporary(){
        Document document;document.load(fixture());const auto before=document.project().toJson();PerspectiveDialog dialog(&document);dialog.show();QVERIFY2(waitForPreview(dialog),qPrintable(status(dialog)));auto *list=dialog.findChild<QListWidget*>("perspectiveCandidates");QVERIFY(list);QVERIFY(list->count()>0);
        chooseOnly(dialog,-1);QVERIFY(!addButton(dialog)->isEnabled());const int finite=nearestFinite(dialog.previewResult(),{322,192});QVERIFY(finite>=0);chooseOnly(dialog,finite);QVERIFY(addButton(dialog)->isEnabled());
        auto *view=dialog.findChild<QWidget*>("perspectivePreview");dialog.findChild<QToolButton*>("perspectiveFitImage")->click();int segment=-1;
        for(int i=0;i<dialog.previewResult().segments.size();++i){const auto midpoint=dialog.previewResult().segments[i].line.center();if(QLineF(midpoint,dialog.previewResult().candidates[finite].point).length()>35&&QLineF(midpoint,{320,336}).length()>35&&QRectF(20,20,600,440).contains(midpoint)){segment=i;break;}}
        QVERIFY(segment>=0);const auto midpoint=dialog.previewResult().segments[segment].line.center();QTest::mouseClick(view,Qt::LeftButton,Qt::NoModifier,previewPoint(dialog,midpoint));QVERIFY(waitForPreview(dialog));int excluded=0;for(const auto &edge:dialog.previewResult().segments)excluded+=!edge.enabled;QCOMPARE(excluded,1);
        auto *tools=dialog.findChild<QTabBar*>("perspectiveInteractionTabs");tools->setCurrentIndex(1);QSignalSpy region(view,SIGNAL(regionChanged(QRect)));drawLine(dialog,{{80,60},{560,420}});QCOMPARE(region.count(),1);const auto selected=qvariant_cast<QRect>(region[0][0]);QVERIFY(std::abs(selected.x()-80)<3);QVERIFY(std::abs(selected.y()-60)<3);QVERIFY(waitForPreview(dialog));QCOMPARE(dialog.previewResult().sourceSize,QSize(640,480));QCOMPARE(document.history()->count(),0);QCOMPARE(document.project().toJson(),before);
        static_cast<QDialog*>(&dialog)->reject();QCOMPARE(document.history()->count(),0);
    }
    void blankImageSupportsManualTwoLineCorrectionAndSourceSwitching(){
        Document document;document.load(fixture(false));const auto before=document.project().toJson();PerspectiveDialog dialog(&document);dialog.show();QVERIFY(waitForPreview(dialog));QVERIFY(dialog.previewResult().candidates.isEmpty());QVERIFY(!addButton(dialog)->isEnabled());
        auto *tools=dialog.findChild<QTabBar*>("perspectiveInteractionTabs");tools->setCurrentIndex(2);dialog.findChild<QToolButton*>("perspectiveFitImage")->click();drawLine(dialog,{{100,400},{210,270}});drawLine(dialog,{{540,400},{430,270}});QCOMPARE(dialog.previewResult().candidates.size(),1);QVERIFY(QLineF(dialog.previewResult().candidates[0].point,{320,140}).length()<5);QVERIFY(addButton(dialog)->isEnabled());
        tools->setCurrentIndex(0);const auto oldPoint=dialog.previewResult().candidates[0].point;drawLine(dialog,{oldPoint,oldPoint+QPointF(24,12)});QVERIFY(QLineF(dialog.previewResult().candidates[0].point,oldPoint+QPointF(24,12)).length()<3);
        QTemporaryDir directory;QVERIFY2(directory.isValid(),qPrintable(directory.errorString()));const auto path=directory.filePath("street.png");QVERIFY(streetImage().save(path));dialog.useImageFile(path);QVERIFY(waitForPreview(dialog));QCOMPARE(dialog.findChild<QTabBar*>("perspectiveSourceTabs")->currentIndex(),1);QVERIFY(!dialog.previewResult().candidates.isEmpty());QVERIFY(dialog.source().description.contains("street.png"));dialog.useSelectedLayer();QVERIFY(waitForPreview(dialog));QVERIFY(dialog.previewResult().candidates.isEmpty());QCOMPARE(dialog.findChild<QTabBar*>("perspectiveSourceTabs")->currentIndex(),0);QCOMPARE(document.project().toJson(),before);QCOMPARE(document.history()->count(),0);
        static_cast<QDialog*>(&dialog)->reject();
    }
    void contextChangesCannotCommitStaleGuides_data(){QTest::addColumn<int>("change");QTest::newRow("content")<<0;QTest::newRow("frame")<<1;QTest::newRow("selection")<<2;QTest::newRow("replacement document")<<3;}
    void contextChangesCannotCommitStaleGuides(){
        QFETCH(int,change);Document document;document.load(fixture());PerspectiveDialog dialog(&document);dialog.show();QVERIFY(waitForPreview(dialog));QVERIFY(!dialog.previewResult().candidates.isEmpty());
        if(change==0)QVERIFY(document.setFillReferences({document.activeShot().layers[0].id},true));else if(change==1)QVERIFY(document.setCurrentFrame(1));else if(change==2)document.selectLayer(1);else document.load(fixture());
        const int history=document.history()->count();QVERIFY(!addButton(dialog)->isEnabled());QVERIFY(status(dialog).contains("changed"));dialog.addGuides();QCOMPARE(document.history()->count(),history);QVERIFY(document.activeShot().guides.isEmpty());
    }
    void rejectingPendingAnalysisCannotPublishOrCommitLater(){
        Document document;document.load(fixture());PerspectiveDialog dialog(&document);QSignalSpy added(&dialog,&PerspectiveDialog::guidesAdded);dialog.show();QVERIFY(waitForPreview(dialog));QVERIFY(!dialog.previewResult().candidates.isEmpty());
        dialog.findChild<ValueSlider*>("perspectiveSensitivity")->findChild<QDoubleSpinBox*>()->setValue(70);static_cast<QDialog*>(&dialog)->reject();QTest::qWait(300);dialog.addGuides();QCOMPARE(added.count(),0);QCOMPARE(document.history()->count(),0);QVERIFY(document.activeShot().guides.isEmpty());QVERIFY(!addButton(dialog)->isEnabled());
    }
    void resetAndFileSwitchDiscardHalfEnteredManualLines(){
        Document document;document.load(fixture(false));PerspectiveDialog dialog(&document);dialog.show();QVERIFY(waitForPreview(dialog));auto *tools=dialog.findChild<QTabBar*>("perspectiveInteractionTabs");tools->setCurrentIndex(2);dialog.findChild<QToolButton*>("perspectiveFitImage")->click();const QLineF left({100,400},{210,270}),right({540,400},{430,270});
        drawLine(dialog,left);dialog.findChild<QToolButton*>("perspectiveReset")->click();QVERIFY(waitForPreview(dialog));drawLine(dialog,right);QVERIFY(dialog.previewResult().candidates.isEmpty());drawLine(dialog,left);QCOMPARE(dialog.previewResult().candidates.size(),1);
        dialog.findChild<QToolButton*>("perspectiveReset")->click();QVERIFY(waitForPreview(dialog));drawLine(dialog,left);QTemporaryDir directory;const auto path=directory.filePath("blank.png");QImage blank(640,480,QImage::Format_ARGB32_Premultiplied);blank.fill(Qt::white);QVERIFY(blank.save(path));dialog.useImageFile(path);QVERIFY(waitForPreview(dialog));drawLine(dialog,right);QVERIFY(dialog.previewResult().candidates.isEmpty());drawLine(dialog,left);QCOMPARE(dialog.previewResult().candidates.size(),1);QCOMPARE(document.history()->count(),0);static_cast<QDialog*>(&dialog)->reject();
    }
    void closingTheDocumentCancelsAndDisablesTheDialog(){
        auto document=std::make_unique<Document>();document->load(fixture());PerspectiveDialog dialog(document.get());dialog.show();QVERIFY(waitForPreview(dialog));QVERIFY(!dialog.previewResult().candidates.isEmpty());QSignalSpy ready(&dialog,&PerspectiveDialog::previewReady),added(&dialog,&PerspectiveDialog::guidesAdded);dialog.findChild<ValueSlider*>("perspectiveSensitivity")->findChild<QDoubleSpinBox*>()->setValue(65);document.reset();QVERIFY(!addButton(dialog)->isEnabled());QVERIFY(status(dialog).contains("closed"));QTest::qWait(250);dialog.useSelectedLayer();dialog.addGuides();QCOMPARE(ready.count(),0);QCOMPARE(added.count(),0);QVERIFY(!addButton(dialog)->isEnabled());
    }
    void normalEditorActionAndGuidesButtonOpenTheSameCancelableWorkflow(){
        QTemporaryDir directory;MainWindow window(directory.filePath("workspace.ini"));window.resize(1250,850);window.canvas()->setProject(fixture());window.showDrawingWorkspacePage();window.applyDrawingWorkspace();window.show();QCoreApplication::processEvents();auto *document=window.canvas()->document();const auto before=document->project().toJson();
        auto openAndCancel=[&](const std::function<void()> &open){bool opened=false;QTimer close;close.setInterval(20);QObject::connect(&close,&QTimer::timeout,&window,[&]{auto *dialog=window.findChild<PerspectiveDialog*>("perspectiveDialog");if(!dialog||!dialog->isVisible())return;opened=true;auto *buttons=dialog->findChild<QDialogButtonBox*>();if(buttons)buttons->button(QDialogButtonBox::Cancel)->click();else static_cast<QDialog*>(dialog)->reject();});close.start();open();close.stop();return opened;};
        auto *action=window.findChild<QAction*>("detectPerspectiveAction");QVERIFY(action);QVERIFY(action->isEnabled());QVERIFY(openAndCancel([&]{action->trigger();}));QCOMPARE(document->history()->count(),0);QCOMPARE(document->project().toJson(),before);
        auto *guides=window.findChild<GuidePanel*>();QVERIFY(guides);auto *button=guides->findChild<QToolButton*>("detectPerspective");QVERIFY(button);QVERIFY(button->isEnabled());QVERIFY(openAndCancel([&]{button->click();}));QCOMPARE(document->history()->count(),0);QCOMPARE(document->project().toJson(),before);
    }
    void insertionValidatesCapacityAndGeometryAtomically(){
        Document document;document.load(fixture());DrawingGuide guide;guide.type=GuideType::VanishingPoint;guide.points={{std::numeric_limits<double>::quiet_NaN(),100}};guide.area={0,0,640,480};
        QVERIFY(!document.addPerspectiveGuides({guide},document.activeShotId(),document.contentRevision(),document.currentFrame(),document.activeShot().layers[0].id));QCOMPARE(document.history()->count(),0);QVERIFY(document.activeShot().guides.isEmpty());
        auto project=fixture();guide.points={{320,192}};guide.name="Existing perspective";for(int i=0;i<maxDrawingGuides;++i){guide.id=QUuid::createUuid().toString(QUuid::WithoutBraces);project.shots[0].guides.append(guide);}QVERIFY(project.validate().isEmpty());document.load(project);const auto before=document.activeShot().guides;
        QVERIFY(!document.addPerspectiveGuides({guide},document.activeShotId(),document.contentRevision(),document.currentFrame(),document.activeShot().layers[0].id));QCOMPARE(document.history()->count(),0);QCOMPARE(document.activeShot().guides,before);
    }
    void captureReviewedDetection(){
        const auto directory=qEnvironmentVariable("TOON_PERSPECTIVE_CAPTURE_DIR");if(directory.isEmpty())return;QDir().mkpath(directory);Canvas canvas;canvas.resize(900,680);canvas.setProject(fixture());canvas.show();PerspectiveDialog dialog(canvas.document());dialog.show();QVERIFY2(waitForPreview(dialog),qPrintable(status(dialog)));QVERIFY(!dialog.previewResult().candidates.isEmpty());QVERIFY(dialog.grab().save(directory+"/perspective-detection-dialog.png"));
        const int finite=nearestFinite(dialog.previewResult(),{322,192});QVERIFY(finite>=0);chooseOnly(dialog,finite);dialog.addGuides();canvas.setTool(Canvas::Tool::Guides);QTest::qWait(30);QVERIFY(canvas.grab().save(directory+"/perspective-guides-canvas.png"));
    }
};
QTEST_MAIN(PerspectiveDialogTests)
#include "test_perspective_dialog.moc"
