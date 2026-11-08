#include "puppet_warp_fixture.h"
#include "canvas.h"
#include "drawing_tool_controller.h"
#include "drawing_tool_panel.h"
#include "main_window.h"
#include "tool_catalog.h"
#include "tool_session.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QAction>
#include <QDir>
#include <QDoubleSpinBox>
#include <QMessageBox>
#include <QPushButton>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

using namespace toon;
namespace {
void start(Canvas &canvas){canvas.resize(700,540);canvas.setProject(puppetWarpFixture());canvas.drawingTools()->warpMode=true;canvas.drawingTools()->puppetMode=true;canvas.setTool(Canvas::Tool::AdvancedTransform);canvas.show();}
QPoint view(Canvas &canvas,QPointF point){return canvas.viewTransform().map(point+sheetGeometry(canvas.project(),canvas.shot()).output.topLeft()).toPoint();}
void click(Canvas &canvas,QPointF point,Qt::KeyboardModifiers modifiers={}){QTest::mouseClick(&canvas,Qt::LeftButton,modifiers,view(canvas,point));}
void drag(Canvas &canvas,QPointF from,QPointF to){
    QTest::mousePress(&canvas,Qt::LeftButton,{},view(canvas,from));QTest::mouseMove(&canvas,view(canvas,to));QTest::mouseRelease(&canvas,Qt::LeftButton,{},view(canvas,to));
}
}
class PuppetWarpUiTest:public QObject{
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void canvasPinsDeformRasterAndApplyOnce(){
        Canvas canvas;start(canvas);auto *c=canvas.drawingTools();auto *transform=c->transform();QVERIFY(transform->puppet());const auto before=canvas.document()->project().shots[0].layers[0].tiles;
        click(canvas,{100,95});click(canvas,{380,95});click(canvas,{240,178});QCOMPARE(transform->pins().size(),3);const auto fixed=transform->pins();
        drag(canvas,{240,178},{260,198});QVERIFY(transform->modified());QCOMPARE(transform->pins()[0],fixed[0]);QCOMPARE(transform->pins()[1],fixed[1]);QVERIFY(QLineF(transform->pins()[2].target,{260,198}).length()<2);
        QCOMPARE(canvas.document()->project().shots[0].layers[0].tiles,before);QCOMPARE(canvas.document()->history()->count(),0);QTest::keyClick(&canvas,Qt::Key_Return);QCOMPARE(canvas.document()->history()->count(),1);QVERIFY(!transform->active());
        canvas.document()->history()->undo();QCOMPARE(canvas.document()->project().shots[0].layers[0].tiles,before);
    }
    void inspectorDensityAndMeshVisibilityPreservePinsAndHistory(){
        Canvas canvas;start(canvas);DrawingToolPanel panel(&canvas);panel.resize(240,350);panel.show();auto *c=canvas.drawingTools();click(canvas,{100,100});click(canvas,{380,100});
        const auto pins=c->transform()->pins();auto *density=panel.findChild<ValueSlider*>("puppetMeshDensity");QVERIFY(density);auto *entry=density->findChild<QDoubleSpinBox*>();QVERIFY(entry);
        entry->setFocus();entry->selectAll();QTest::keyClicks(entry,"24");QTest::keyClick(entry,Qt::Key_Return);QCOMPARE(c->puppetDensity,24);QCOMPARE(c->transform()->puppetDensity(),24);QCOMPARE(c->transform()->pins(),pins);QVERIFY(c->transform()->active());
        panel.findChild<QToolButton*>("showPuppetMesh")->click();QVERIFY(!c->showPuppetMesh);QCOMPARE(canvas.document()->history()->count(),0);
        for(const char *id:{"showPuppetMesh","deletePuppetPins","resetPuppetWarp"}){auto *button=panel.findChild<QToolButton*>(id);QVERIFY(button);QVERIFY(!button->icon().isNull());QVERIFY(!button->accessibleName().isEmpty());}
        auto *footer=panel.findChild<QWidget*>("advancedTransformFooter");QVERIFY(footer->isVisible());QVERIFY(panel.rect().contains(QRect(footer->mapTo(&panel,QPoint()),footer->size())));
        c->cancel();
    }
    void multiPinSelectionAltDeleteAndReset(){
        Canvas canvas;start(canvas);DrawingToolPanel panel(&canvas);panel.resize(240,400);panel.show();auto *c=canvas.drawingTools();click(canvas,{100,100});click(canvas,{380,100});click(canvas,{240,250});
        click(canvas,{100,100});click(canvas,{380,100},Qt::ShiftModifier);QCOMPARE(c->selectedPuppetPinCount(),2);
        drag(canvas,{100,100},{110,110});const auto pins=c->transform()->pins();QVERIFY(QLineF(pins[0].target-pins[0].source,{10,10}).length()<2);QVERIFY(QLineF(pins[1].target-pins[1].source,{10,10}).length()<2);QCOMPARE(pins[2].target,pins[2].source);
        panel.findChild<QToolButton*>("resetPuppetWarp")->click();QVERIFY(puppetMeshIdentity(c->transform()->pins()));QCOMPARE(c->transform()->pins().size(),3);
        click(canvas,{240,250},Qt::AltModifier);QCOMPARE(c->transform()->pins().size(),2);click(canvas,{100,100});click(canvas,{380,100},Qt::ShiftModifier);QCOMPARE(c->selectedPuppetPinCount(),2);QVERIFY(c->key(Qt::Key_Delete,{}));QVERIFY(c->transform()->pins().isEmpty());QCOMPARE(canvas.document()->history()->count(),0);c->cancel();
    }
    void pendingDeformationCanCancelToolSwitchAndDiscard(){
        Canvas canvas;start(canvas);auto *c=canvas.drawingTools();click(canvas,{240,178});drag(canvas,{240,178},{255,190});QVERIFY(c->transform()->modified());
        QTimer::singleShot(10,&canvas,[&]{for(auto *widget:QApplication::topLevelWidgets())if(auto *box=qobject_cast<QMessageBox*>(widget))if(auto *button=box->findChild<QPushButton*>("cancelPendingTransform"))button->click();});
        canvas.setTool(Canvas::Tool::Brush);QCOMPARE(canvas.tool(),Canvas::Tool::AdvancedTransform);QVERIFY(c->transform()->puppet());
        QTimer::singleShot(10,&canvas,[&]{for(auto *widget:QApplication::topLevelWidgets())if(auto *box=qobject_cast<QMessageBox*>(widget))if(auto *button=box->findChild<QPushButton*>("discardPendingTransform"))button->click();});
        canvas.setTool(Canvas::Tool::Brush);QCOMPARE(canvas.tool(),Canvas::Tool::Brush);QVERIFY(!c->transform()->active());QCOMPARE(canvas.document()->history()->count(),0);
    }
    void directActionAndSavedToolSettingsWork(){
        QTemporaryDir directory;const auto settings=directory.filePath("workspace.ini");
        {
            MainWindow window(settings);auto *action=window.findChild<QAction*>("puppetWarpAction");QVERIFY(action);QVERIFY(!action->isEnabled());window.canvas()->setProject(puppetWarpFixture());QVERIFY(action->isEnabled());action->trigger();
            auto *c=window.canvas()->drawingTools();QVERIFY(c->puppetMode);QVERIFY(c->transform()->puppet());QVERIFY(c->setPuppetDensity(22));c->showPuppetMesh=false;window.canvas()->notifyToolSettingsEdited();c->cancel();QString error;QVERIFY2(window.toolCatalog()->save(&error),qPrintable(error));
        }
        MainWindow reopened(settings);reopened.canvas()->setProject(puppetWarpFixture());reopened.findChild<QAction*>("puppetWarpAction")->trigger();auto *c=reopened.canvas()->drawingTools();QVERIFY(c->puppetMode);QCOMPARE(c->puppetDensity,22);QVERIFY(!c->showPuppetMesh);QVERIFY(c->transform()->pins().isEmpty());c->cancel();
    }
    void captureReviewedWorkflow(){
        const auto directory=qEnvironmentVariable("TOON_PUPPET_CAPTURE_DIR");if(directory.isEmpty())return;QDir().mkpath(directory);QTemporaryDir temp;
        MainWindow window(temp.filePath("workspace.ini"));window.resize(1280,840);window.canvas()->setProject(puppetWarpFixture());window.showDrawingWorkspacePage();window.applyDrawingWorkspace();window.show();window.findChild<QAction*>("puppetWarpAction")->trigger();QTest::qWait(60);QVERIFY(window.canvas()->isVisible());
        auto *canvas=window.canvas();auto *c=canvas->drawingTools();auto *transform=c->transform();QVERIFY(transform->puppet());
        for(const auto &point:{QPointF(100,95),QPointF(380,95),QPointF(100,270),QPointF(380,270),QPointF(240,178)})QVERIFY(transform->addPuppetPin(point)>=0);
        QImage before(480,360,QImage::Format_ARGB32_Premultiplied);before.fill(Qt::white);{QPainter p(&before);paintLayers(p,canvas->document()->activeShot().layers,true);}QVERIFY(before.save(directory+"/source.png"));
        QVector<QPointF> targets;for(const auto &pin:transform->pins())targets.append(pin.target);targets.last()+=QPointF(35,25);QVERIFY(transform->setPuppetTargets(targets));QTest::qWait(30);QVERIFY(window.grab().save(directory+"/puppet-workspace.png"));
        auto *panel=window.findChild<DrawingToolPanel*>();if(panel)QVERIFY(panel->grab().save(directory+"/puppet-controls.png"));
        QVERIFY(transform->apply());QImage after(480,360,QImage::Format_ARGB32_Premultiplied);after.fill(Qt::white);{QPainter p(&after);paintLayers(p,canvas->document()->activeShot().layers,true);}QVERIFY(after.save(directory+"/result.png"));
        QImage comparison(960,396,QImage::Format_ARGB32_Premultiplied);comparison.fill(Qt::white);{QPainter p(&comparison);p.setPen(Qt::black);p.drawText(QRect(12,0,456,36),Qt::AlignVCenter,"Original raster");p.drawText(QRect(492,0,456,36),Qt::AlignVCenter,"Pin-deformed raster");p.drawImage(0,36,before);p.drawImage(480,36,after);}QVERIFY(comparison.save(directory+"/before-after.png"));
        if(panel)QVERIFY(panel->grab().save(directory+"/saved-mesh-controls.png"));
        window.findChild<QAction*>("puppetWarpAction")->trigger();QVERIFY(transform->puppet());QCOMPARE(transform->pins().size(),5);QVERIFY(!transform->modified());QTest::qWait(20);
        QVERIFY(window.grab().save(directory+"/reopened-workspace.png"));transform->cancel();
    }
    void appliedLayerReopensItsOwnPinsAndCanBakeWithUndo(){
        QTemporaryDir directory;MainWindow window(directory.filePath("retained.ini"));window.canvas()->setProject(puppetWarpFixture());window.showDrawingWorkspacePage();window.show();auto *action=window.findChild<QAction*>("puppetWarpAction");QVERIFY(action);action->trigger();auto *c=window.canvas()->drawingTools();
        QVERIFY(c->setPuppetDensity(24));QVERIFY(c->transform()->addPuppetPin({100,100})>=0);QVERIFY(c->transform()->addPuppetPin({380,100})>=0);QVERIFY(c->transform()->addPuppetPin({240,178})>=0);QVERIFY(c->transform()->setPuppetTargets({{100,100},{380,100},{260,195}}));QVERIFY(c->transform()->apply());
        auto *doc=window.canvas()->document();QVERIFY(doc->activeShot().layers[0].puppetWarp);const auto state=*doc->activeShot().layers[0].puppetWarp;const auto pixels=doc->activeShot().layers[0].tiles;
        QPainterPath detailed;for(int i=0;i<110;++i)detailed.addRect(i*3,0,1,80);doc->setRasterSelection(detailed);
        c->puppetDensity=4;action->trigger();QVERIFY(c->transform()->puppet());QCOMPARE(c->transform()->pins(),state.pins);QCOMPARE(c->puppetDensity,24);QCOMPARE(window.findChild<ValueSlider*>("puppetMeshDensity")->value(),24.);QVERIFY(!c->transform()->modified());c->cancel();QCOMPARE(doc->activeShot().layers[0].tiles,pixels);QCOMPARE(doc->rasterSelection().path,detailed);
        auto *bake=window.findChild<QAction*>("bakePuppetWarpAction");QVERIFY(bake);QVERIFY(bake->isEnabled());bake->trigger();QVERIFY(!doc->activeShot().layers[0].puppetWarp);QCOMPARE(doc->activeShot().layers[0].tiles,pixels);doc->history()->undo();QCOMPARE(*doc->activeShot().layers[0].puppetWarp,state);
    }
};
QTEST_MAIN(PuppetWarpUiTest)
#include "test_puppet_warp_ui.moc"
