#include "document.h"
#include "managed_project_store.h"
#include "reference3d_canvas.h"
#include "reference3d_layer_panel.h"
#include "reference3d_review.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QSignalSpy>
#include <QStandardPaths>
#include <QSlider>
#include <QTabBar>
#include <QToolButton>
#include <QtTest>

using namespace toon;
class ReferenceLayerPanelTests final:public QObject{
    Q_OBJECT
private slots:
    void initTestCase(){QStandardPaths::setTestModeEnabled(true);applyTheme();}
    void placingAndPosingUsesTheDrawingHistoryAndDrawAboveKeepsTheReference(){
        Document document;document.load(newManagedDocument("Drawing",false,320,240,Qt::white,false));QWidget canvas;canvas.resize(640,480);Reference3DCanvasController controller(&document,&canvas);Reference3DLayerPanel panel(&document,&controller);panel.resize(360,760);panel.show();
        auto *editor=panel.editor();QVERIFY(editor);QString error;QVERIFY2(editor->placeCatalogModel("pet/cat",&error),qPrintable(error));QCOMPARE(document.history()->count(),1);QCOMPARE(document.activeShot().layers.size(),2);const auto layerId=document.activeShot().layers[document.activeLayer()].id;QVERIFY(document.activeShot().layers[document.activeLayer()].reference3d);
        auto *field=editor->findChild<ValueSlider*>("referencePositionX");QVERIFY(field);auto *track=field->findChild<QSlider*>();QVERIFY(track);track->setSliderDown(true);field->setValue(.25);field->setValue(.5);QCOMPARE(document.history()->count(),1);track->setSliderDown(false);QCOMPARE(document.history()->count(),2);QCOMPARE(document.activeShot().layers[document.activeLayer()].reference3d->instances[0].translation.x(),.5f);
        document.history()->undo();QCOMPARE(document.activeShot().layers[document.activeLayer()].reference3d->instances[0].translation.x(),0.f);document.history()->redo();QCOMPARE(document.activeShot().layers[document.activeLayer()].reference3d->instances[0].translation.x(),.5f);
        QSignalSpy drawing(&panel,&Reference3DLayerPanel::drawAboveRequested);auto *above=panel.findChild<QToolButton*>("referenceDrawAbove");QVERIFY(above);above->click();QCOMPARE(drawing.count(),1);QCOMPARE(document.activeShot().layers.size(),3);QCOMPARE(document.activeShot().layers[document.activeLayer()].kind,LayerKind::Raster);QVERIFY(editor->scene().instances.isEmpty());
        bool retained=false;for(const auto &layer:document.activeShot().layers)if(layer.id==layerId){retained=bool(layer.reference3d);QCOMPARE(layer.reference3d->instances[0].translation.x(),.5f);}QVERIFY(retained);
    }
    void modeSelectionAndLockedLayersKeepDocumentOwnership(){
        Document document;document.load(newManagedDocument("Drawing",false,320,240,Qt::white,false));QWidget canvas;Reference3DCanvasController controller(&document,&canvas);Reference3DLayerPanel panel(&document,&controller);QString error;QVERIFY2(panel.editor()->placeCatalogModel("pet/dog",&error),qPrintable(error));
        auto *modes=panel.findChild<QTabBar*>("referenceCanvasModes"),*axes=panel.findChild<QTabBar*>("referenceCanvasAxes");QVERIFY(modes);QVERIFY(axes);modes->setCurrentIndex(3);axes->setCurrentIndex(2);QCOMPARE(controller.mode(),Reference3DCanvasMode::Pose);QCOMPARE(controller.axis(),1);
        const auto id=document.activeShot().layers[document.activeLayer()].id;document.setLayersLocked({id},true);QVERIFY(!panel.editor()->isEnabled());const int history=document.history()->count();QSignalSpy warning(&panel,&Reference3DLayerPanel::warning);QVERIFY(panel.editor()->placeCatalogModel("pet/cat",&error));QCOMPARE(document.history()->count(),history);QCOMPARE(document.activeShot().layers[document.activeLayer()].reference3d->instances.size(),1);QVERIFY(!warning.isEmpty());
        auto *above=panel.findChild<QToolButton*>("referenceDrawAbove");QVERIFY(above->isEnabled());above->click();QCOMPARE(document.activeShot().layers[document.activeLayer()].kind,LayerKind::Raster);QVERIFY(panel.editor()->isEnabled());
    }
    void unrelatedDrawingEditsDoNotCancelBackgroundModelPlacement(){
        Document document;document.load(newManagedDocument("Drawing",false,320,240,Qt::white,false));QWidget canvas;Reference3DCanvasController controller(&document,&canvas);Reference3DLayerPanel panel(&document,&controller);
        panel.editor()->placeCatalogModelAsync("pet/dog");QVERIFY(panel.editor()->catalogPlacementInProgress());document.addLayer();QCOMPARE(document.activeShot().layers[document.activeLayer()].kind,LayerKind::Raster);
        QTRY_VERIFY_WITH_TIMEOUT(!panel.editor()->catalogPlacementInProgress(),15000);QTRY_VERIFY_WITH_TIMEOUT(document.activeShot().layers[document.activeLayer()].reference3d.has_value(),15000);
        QCOMPARE(document.history()->count(),2);QCOMPARE(document.activeShot().layers[document.activeLayer()].reference3d->instances[0].asset->metadata.value("referencePetKind").toString(),QString("dog"));
    }
};
QTEST_MAIN(ReferenceLayerPanelTests)
#include "test_reference3d_layer_panel.moc"
