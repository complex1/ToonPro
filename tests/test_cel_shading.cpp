#include "cel_shading.h"
#include "cel_shading_panel.h"
#include "canvas.h"
#include "ui_theme.h"
#include "drawing_aids.h"
#include "value_slider.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QToolButton>
#include <QLabel>
#include <QBitArray>
#include <QMouseEvent>
#include <QPainter>
#include <QPointingDevice>
#include <QSignalSpy>
#include <QSlider>
#include <QTabBar>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QtTest>

using namespace toon;
namespace {
QImage outlineImage(){QImage image(64,64,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter painter(&image);painter.setPen(QPen(Qt::black,2));painter.drawRect(QRectF(8,8,48,48));return image;}
QPainterPath divider(){QPainterPath path(QPointF(8,32));path.lineTo(56,32);return path;}
Project drawing(){auto project=Project::create();project.width=64;project.height=64;auto &layer=project.shots[0].layers[0];layer.name="Ink";QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);{QPainter p(&tile);p.drawImage(0,0,outlineImage());}layer.tiles[{0,0}]=tile;return project;}
QPointF view(Canvas &canvas,QPointF point){auto transform=canvas.viewTransform();const auto origin=sheetGeometry(canvas.project(),canvas.shot()).output.topLeft();transform.translate(origin.x(),origin.y());return (canvas.cameraTransform()*transform).map(point);}
void mouse(Canvas &canvas,QEvent::Type type,QPointF point){const auto position=view(canvas,point);QMouseEvent event(type,position,position,canvas.mapToGlobal(position),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&canvas,&event);}
}
class CelShadingTest:public QObject{
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void enclosedRegionsUseGuideCenterlineWithoutWritingGuideColor(){
        const auto input=outlineImage();const auto original=input.copy();CelBoundary boundary{divider(),CelShadeKind::Shadow,QColor("#14cc90"),8};
        const auto result=renderCelShading(input,{boundary},{{{20,20},CelShadeKind::Shadow,Qt::red},{{20,44},CelShadeKind::Highlight,Qt::blue}},CelBoundaryFinish::Hidden);
        QVERIFY(result.error.isEmpty());QVERIFY(result.rejectedFills.isEmpty());QCOMPARE(input,original);
        QCOMPARE(result.shadow.pixelColor(20,20),QColor(Qt::red));QCOMPARE(result.highlight.pixelColor(20,44),QColor(Qt::blue));
        QCOMPARE(result.shadow.pixelColor(20,44).alpha(),0);QCOMPARE(result.highlight.pixelColor(20,20).alpha(),0);
        // Both sides reach the divider, with no transparent center seam.
        for(int y=30;y<=34;++y)QVERIFY(result.shadow.pixelColor(20,y).alpha()||result.highlight.pixelColor(20,y).alpha());
        for(const auto &image:{result.shadow,result.highlight})for(int y=0;y<64;++y)for(int x=0;x<64;++x)QVERIFY(image.pixelColor(x,y)!=boundary.guideColor);
    }
    void boundaryFinishRecolorsWidthAndOpenRegionsAreRejected(){
        const QVector<CelBoundary> boundaries={{divider(),CelShadeKind::Shadow,Qt::cyan,8}};
        const QVector<CelShadeFill> fills={{{20,20},CelShadeKind::Shadow,Qt::red},{{2,2},CelShadeKind::Highlight,Qt::yellow}};
        const auto hidden=renderCelShading(outlineImage(),boundaries,fills,CelBoundaryFinish::Hidden);
        const auto colored=renderCelShading(outlineImage(),boundaries,fills,CelBoundaryFinish::RegionColor);
        QCOMPARE(hidden.rejectedFills,QVector<int>({1}));QCOMPARE(colored.rejectedFills,QVector<int>({1}));
        QCOMPARE(hidden.shadow.pixelColor(20,34).alpha(),0);QVERIFY(colored.shadow.pixelColor(20,34).alpha()>0);
        QCOMPARE(colored.shadow.pixelColor(5,32).alpha(),0); // Recolored guide cannot leak past the outline endpoint.
        QCOMPARE(colored.shadow.pixelColor(20,34).red(),255);QCOMPARE(colored.highlight.pixelColor(2,2).alpha(),0);
        auto cancelled=std::make_shared<std::atomic_bool>(true);QVERIFY(renderCelShading(outlineImage(),boundaries,fills,CelBoundaryFinish::Hidden,cancelled).cancelled);
    }
    void diagonalSilhouetteDoesNotDilateIntoOutsidePixels(){
        QImage ink(64,64,QImage::Format_ARGB32_Premultiplied);ink.fill(Qt::transparent);
        {QPainter painter(&ink);painter.setPen(QPen(Qt::black,1));painter.drawPolygon(QPolygonF({{32,8},{56,32},{32,56},{8,32}}));}
        const auto result=renderCelShading(ink,{},{{{32,32},CelShadeKind::Shadow,Qt::red}},CelBoundaryFinish::Hidden);QVERIFY(result.rejectedFills.isEmpty());
        QBitArray outside(4096);QVector<QPoint> queue{{0,0}};outside.setBit(0);
        for(int at=0;at<queue.size();++at){const auto p=queue[at];QCOMPARE(result.shadow.pixelColor(p).alpha(),0);
            for(const auto delta:{QPoint(-1,0),QPoint(1,0),QPoint(0,-1),QPoint(0,1)}){const auto next=p+delta;if(!ink.rect().contains(next))continue;const int index=next.y()*64+next.x();if(!outside.testBit(index)&&ink.pixelColor(next).alpha()<32){outside.setBit(index);queue.append(next);}}}
    }
    void compositedPreviewMatchesAppliedLayerOrderAndOpacity(){
        auto project=drawing();project.shots[0].layers[0].opacity=.4;project.shots[0].layers[0].blendMode=LayerBlendMode::Multiply;
        auto upper=RasterLayer::create("Upper detail");upper.opacity=.7;QImage pixels(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);pixels.fill(Qt::transparent);{QPainter painter(&pixels);painter.fillRect(18,18,12,12,QColor(20,220,100,140));}upper.tiles[{0,0}]=pixels;project.shots[0].layers.append(upper);
        Canvas canvas;canvas.setProject(project);CelShadingPanel panel(&canvas);panel.setOutlineLayerId(project.shots[0].layers[0].id);
        QVERIFY(panel.addFill({20,20},CelShadeKind::Shadow,QColor(180,30,70,160)));QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),5000);
        const auto preview=panel.previewComposite();QVERIFY(!preview.isNull());QVERIFY(panel.apply());const auto applied=renderMovieFrame(canvas.project());QCOMPARE(preview,applied);
    }
    void previewCancelAndApplyPreserveSourceAndSupportUndoSave(){
        Canvas canvas;canvas.setProject(drawing());canvas.resize(600,500);canvas.show();canvas.fitSheet();CelShadingPanel panel(&canvas);panel.resize(250,360);panel.show();
        auto *document=canvas.document();const auto source=document->activeShot().layers[0];
        QVERIFY(panel.addBoundary(divider(),CelShadeKind::Shadow));QVERIFY(panel.addFill({20,20},CelShadeKind::Shadow,QColor("#77597e")));QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),5000);
        QCOMPARE(document->history()->count(),0);QCOMPARE(document->activeShot().layers.size(),1);QCOMPARE(document->activeShot().layers[0].tiles,source.tiles);
        panel.cancelEditing();QVERIFY(!panel.pending());QVERIFY(panel.preview().shadow.isNull());QCOMPARE(document->history()->count(),0);
        QVERIFY(panel.addBoundary(divider(),CelShadeKind::Shadow));QVERIFY(panel.addFill({20,20},CelShadeKind::Shadow,Qt::red));QVERIFY(panel.addFill({20,44},CelShadeKind::Highlight,Qt::blue));QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),5000);QVERIFY(panel.apply());
        QCOMPARE(document->history()->count(),1);QCOMPARE(document->activeShot().layers.size(),3);QCOMPARE(document->activeShot().layers.last().id,source.id);QCOMPARE(document->activeShot().layers.last().tiles,source.tiles);
        QCOMPARE(document->activeShot().layers[0].name,QString("Shadows"));QCOMPARE(document->activeShot().layers[1].name,QString("Highlights"));
        QTemporaryDir directory;QString error;const auto file=directory.filePath("shading.toonproj");QVERIFY2(saveProject(document->project(),file,&error),qPrintable(error));const auto loaded=loadProject(file,&error);QVERIFY2(loaded.has_value(),qPrintable(error));QCOMPARE(loaded->shots[0].layers.size(),3);QCOMPARE(loaded->shots[0].layers.last().tiles,source.tiles);
        document->history()->undo();QCOMPARE(document->activeShot().layers.size(),1);QCOMPARE(document->activeShot().layers[0].tiles,source.tiles);document->history()->redo();QCOMPARE(document->activeShot().layers.size(),3);
    }
    void staleWorkerAndLockedDestinationCannotApply(){
        Canvas canvas;canvas.setProject(drawing());CelShadingPanel panel(&canvas);panel.show();QVERIFY(panel.addFill({20,20},CelShadeKind::Shadow,Qt::red));canvas.document()->addLayer();QVERIFY(panel.pending());QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),5000);QCOMPARE(panel.fills().size(),1);QVERIFY(!panel.preview().shadow.isNull());
        auto project=drawing();auto folder=RasterLayer::create("Locked drawing");folder.group=true;folder.locked=true;project.shots[0].layers[0].parentId=folder.id;project.shots[0].layers.append(folder);canvas.setProject(project);panel.setOutlineLayerId(project.shots[0].layers[0].id);
        QVERIFY(!panel.addFill({20,20},CelShadeKind::Shadow,Qt::red));QVERIFY(panel.findChild<QLabel*>("celShadingStatus")->text().contains("Unlock"));QCOMPARE(canvas.document()->history()->count(),0);
        // Locking the reference ink alone is allowed: it is read, never modified.
        project=drawing();project.shots[0].layers[0].locked=true;canvas.setProject(project);QVERIFY(panel.addFill({20,20},CelShadeKind::Shadow,Qt::red));QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),5000);QVERIFY(panel.apply());QVERIFY(canvas.document()->activeShot().layers.last().locked);
    }
    void saveReopenRestoresGuidesFillsSettingsWithoutApplying(){
        Canvas canvas;canvas.setProject(drawing());CelShadingPanel panel(&canvas);panel.resize(250,360);panel.show();
        const auto original=canvas.document()->activeShot().layers[0].tiles;
        auto *kind=panel.findChild<QTabBar*>("celShadingKind");auto *mode=panel.findChild<QTabBar*>("celShadingMode");
        auto *width=panel.findChild<ValueSlider*>("celShadingWidth");QVERIFY(kind&&mode&&width);
        width->setValue(7);kind->setCurrentIndex(1);canvas.setBrushColor(QColor("#e4b982"));panel.findChild<QToolButton*>("celShadingColor")->click();
        QPainterPath curve(QPointF(8,32));curve.cubicTo(20,14,40,50,56,32);
        QVERIFY(panel.addBoundary(curve,CelShadeKind::Highlight));QVERIFY(panel.addFill({20,20},CelShadeKind::Shadow,QColor("#914477")));
        panel.setBoundaryFinish(CelBoundaryFinish::RegionColor);mode->setCurrentIndex(1);
        QVERIFY(panel.finishPersistence());QCOMPARE(canvas.document()->project().drawingAids.shadingDrafts.size(),1);
        const auto savedDraft=canvas.project().drawingAids.shadingDrafts.first();QCOMPARE(savedDraft.boundaries.first().path,curve);
        QCOMPARE(savedDraft.width,7.);QCOMPARE(savedDraft.kind,1);QCOMPARE(savedDraft.mode,1);QCOMPARE(savedDraft.highlight,QColor("#e4b982"));
        QVERIFY(canvas.document()->drawingAidsDirty());QCOMPARE(canvas.document()->history()->count(),0);
        QTemporaryDir directory;QString error;const auto file=directory.filePath("unfinished.toonproj");
        QVERIFY2(saveProject(canvas.project(),file,&error),qPrintable(error));const auto loaded=loadProject(file,&error);QVERIFY2(loaded.has_value(),qPrintable(error));
        // Save contains inputs immediately, even if the asynchronous preview was not ready.
        canvas.setProject(*loaded);QVERIFY(panel.pending());QVERIFY(!panel.active());QVERIFY(!canvas.document()->drawingAidsDirty());
        QCOMPARE(panel.boundaries().size(),1);QCOMPARE(panel.boundaries().first().path,curve);QCOMPARE(panel.fills().size(),1);
        QVERIFY(celShadingDraftEqual(canvas.project().drawingAids.shadingDrafts.first(),savedDraft));
        QCOMPARE(kind->currentIndex(),1);QCOMPARE(mode->currentIndex(),1);QCOMPARE(width->value(),7.);
        QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),5000);QVERIFY(!panel.previewComposite().isNull());
        QCOMPARE(canvas.document()->activeShot().layers.size(),1);QCOMPARE(canvas.document()->activeShot().layers[0].tiles,original);
        QCOMPARE(canvas.document()->history()->count(),0);QVERIFY(!canvas.document()->drawingAidsDirty());
    }
    void draftsRestorePerOutlineFrameAndShotWithoutCreatingArtworkHistory(){
        auto project=drawing();auto second=drawing().shots.first();second.number="C002";project.shots.append(second);
        Canvas canvas;canvas.setProject(project);CelShadingPanel panel(&canvas);auto *document=canvas.document();
        const auto firstShot=document->activeShotId(),firstOutline=panel.outlineLayerId();
        QVERIFY(panel.addBoundary(divider(),CelShadeKind::Shadow));
        QVERIFY(document->setCurrentFrame(5));QVERIFY(!panel.pending());
        QPainterPath other(QPointF(8,20));other.lineTo(56,40);QVERIFY(panel.addBoundary(other,CelShadeKind::Highlight));
        QVERIFY(document->selectShot(second.id));QVERIFY(!panel.pending());QVERIFY(panel.addBoundary(divider(),CelShadeKind::Highlight));
        QCOMPARE(document->project().drawingAids.shadingDrafts.size(),3);QCOMPARE(document->history()->count(),0);
        QVERIFY(document->selectShot(firstShot));QVERIFY(document->setCurrentFrame(0));
        QCOMPARE(panel.outlineLayerId(),firstOutline);QCOMPARE(panel.boundaries().size(),1);QCOMPARE(panel.boundaries()[0].kind,CelShadeKind::Shadow);
        QVERIFY(document->setCurrentFrame(5));QCOMPARE(panel.boundaries()[0].path,other);QCOMPARE(panel.boundaries()[0].kind,CelShadeKind::Highlight);
        panel.cancelEditing();QCOMPARE(document->project().drawingAids.shadingDrafts.size(),2);QVERIFY(!panel.pending());
        QVERIFY(document->setCurrentFrame(0));QVERIFY(panel.pending());QCOMPARE(panel.boundaries()[0].kind,CelShadeKind::Shadow);
    }
    void sourceDeletionRetainsSavedDraftAndUndoRestoresItsAvailability(){
        Canvas canvas;canvas.setProject(drawing());CelShadingPanel panel(&canvas);panel.show();auto *document=canvas.document();
        const auto outline=panel.outlineLayerId();QVERIFY(panel.addBoundary(divider(),CelShadeKind::Shadow));QVERIFY(panel.addFill({20,20},CelShadeKind::Shadow,Qt::red));
        QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),5000);document->addLayer();QVERIFY(panel.pending());
        document->deleteLayers({outline});QVERIFY(panel.pending());QCOMPARE(panel.outlineLayerId(),outline);QVERIFY(!panel.apply());
        QCOMPARE(document->project().drawingAids.shadingDrafts.size(),1);QVERIFY(panel.findChild<QComboBox*>("celShadingOutline")->currentText().contains("Unavailable"));
        QString error;QTemporaryDir directory;const auto file=directory.filePath("missing-outline.toonproj");QVERIFY2(saveProject(document->project(),file,&error),qPrintable(error));
        document->history()->undo();QVERIFY(panel.pending());QCOMPARE(panel.outlineLayerId(),outline);QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),5000);
        QVERIFY(panel.apply());QVERIFY(!panel.pending());QVERIFY(document->project().drawingAids.shadingDrafts.isEmpty());
        document->history()->undo();QVERIFY(document->project().drawingAids.shadingDrafts.isEmpty()); // Artwork Undo does not resurrect discarded editor drafts.
    }
    void focusedWidthAndNewProjectCannotLoseOrLeakDrafts(){
        Canvas canvas;canvas.setProject(drawing());CelShadingPanel panel(&canvas);panel.resize(260,380);panel.show();panel.activateWindow();
        QVERIFY(panel.addBoundary(divider(),CelShadeKind::Shadow));
        auto *width=panel.findChild<ValueSlider*>("celShadingWidth");auto *edit=width->findChild<QDoubleSpinBox*>()->findChild<QLineEdit*>();QVERIFY(edit);
        edit->setFocus();edit->selectAll();QTest::keyClicks(edit,"11");QVERIFY(panel.finishPersistence());
        QCOMPARE(canvas.project().drawingAids.shadingDrafts.first().width,11.);QCOMPARE(canvas.document()->history()->count(),0);
        canvas.setProject(drawing());QVERIFY(!panel.pending());QVERIFY(canvas.project().drawingAids.shadingDrafts.isEmpty());QVERIFY(!canvas.document()->drawingAidsDirty());
    }
    void guideContactPublishesOnlyAtRelease(){
        Canvas canvas;canvas.setProject(drawing());canvas.resize(600,500);canvas.show();canvas.fitSheet();CelShadingPanel panel(&canvas);panel.show();panel.setActive(true);
        QSignalSpy metadata(canvas.document(),&Document::drawingAidsChanged);
        mouse(canvas,QEvent::MouseButtonPress,{8,25});mouse(canvas,QEvent::MouseMove,{20,25});mouse(canvas,QEvent::MouseMove,{32,25});
        QCOMPARE(metadata.count(),0);QVERIFY(canvas.project().drawingAids.shadingDrafts.isEmpty());QVERIFY(!panel.finishPersistence());
        mouse(canvas,QEvent::MouseButtonRelease,{56,25});QCOMPARE(metadata.count(),1);QCOMPARE(canvas.project().drawingAids.shadingDrafts.size(),1);
        QCOMPARE(canvas.document()->history()->count(),0);
    }
    void otherDrawingAidChangesPreserveLiveGuideAndWidthAdjustment(){
        Canvas canvas;canvas.setProject(drawing());canvas.resize(600,500);canvas.show();canvas.fitSheet();CelShadingPanel panel(&canvas);panel.show();panel.setActive(true);
        mouse(canvas,QEvent::MouseButtonPress,{8,25});mouse(canvas,QEvent::MouseMove,{32,25});
        auto aids=canvas.project().drawingAids;aids.lightTable.visible=false;
        QVERIFY(canvas.document()->setDrawingAids(aids));QVERIFY(!panel.finishPersistence());
        mouse(canvas,QEvent::MouseButtonRelease,{56,25});QCOMPARE(panel.boundaries().size(),1);
        QCOMPARE(panel.boundaries().first().path.currentPosition(),QPointF(56,25));
        auto *width=panel.findChild<ValueSlider*>("celShadingWidth");auto *slider=width->findChild<QSlider*>();QVERIFY(slider);
        slider->setSliderDown(true);slider->setValue(9);QVERIFY(width->editingActive());QCOMPARE(width->value(),9.);
        aids=canvas.project().drawingAids;aids.lightTable.visible=true;
        QVERIFY(canvas.document()->setDrawingAids(aids));QCOMPARE(width->value(),9.);QVERIFY(width->editingActive());
        QCOMPARE(canvas.project().drawingAids.shadingDrafts.first().width,2.);
        slider->setSliderDown(false);QCOMPARE(canvas.project().drawingAids.shadingDrafts.first().width,9.);
        QCOMPARE(panel.boundaries().size(),1);QCOMPARE(canvas.document()->history()->count(),0);
    }
    void canvasCanBeDestroyedBeforeDockPanel(){
        auto *canvas=new Canvas;canvas->setProject(drawing());auto *panel=new CelShadingPanel(canvas);panel->show();panel->setActive(true);delete canvas;delete panel;
    }
    void mouseAndTabletGuidesDoNotBecomeArtworkAndHideRestoresDrawing(){
        Canvas canvas;canvas.setProject(drawing());canvas.resize(600,500);canvas.show();canvas.fitSheet();CelShadingPanel panel(&canvas);panel.resize(250,360);panel.show();panel.setActive(true);QVERIFY(panel.active());
        mouse(canvas,QEvent::MouseButtonPress,{8,25});mouse(canvas,QEvent::MouseMove,{32,25});mouse(canvas,QEvent::MouseButtonRelease,{56,25});QCOMPARE(panel.boundaries().size(),1);QCOMPARE(canvas.document()->history()->count(),0);QVERIFY(!canvas.document()->strokeActive());
        QPointingDevice pen("Shading pen",302,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
        for(auto type:{QEvent::TabletPress,QEvent::TabletMove,QEvent::TabletRelease}){const auto pos=view(canvas,{type==QEvent::TabletPress?8.:type==QEvent::TabletMove?32.:56.,40.});QTabletEvent event(type,&pen,pos,canvas.mapToGlobal(pos),type==QEvent::TabletRelease?0:.7,0,0,0,0,0,Qt::NoModifier,type==QEvent::TabletMove?Qt::NoButton:Qt::LeftButton,type==QEvent::TabletRelease?Qt::NoButton:Qt::LeftButton);QApplication::sendEvent(&canvas,&event);}
        QCOMPARE(panel.boundaries().size(),2);QCOMPARE(canvas.document()->history()->count(),0);QVERIFY(!canvas.document()->strokeActive());
        panel.hide();QVERIFY(!panel.active());QVERIFY(panel.pending());panel.cancelEditing();canvas.setTool(Canvas::Tool::Brush);mouse(canvas,QEvent::MouseButtonPress,{20,20});mouse(canvas,QEvent::MouseButtonRelease,{22,22});QCOMPARE(canvas.document()->history()->count(),1);
    }
};
QTEST_MAIN(CelShadingTest)
#include "test_cel_shading.moc"
