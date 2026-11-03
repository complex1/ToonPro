#include "canvas.h"
#include "main_window.h"
#include "property_row.h"
#include "rig3d_panel.h"
#include "scene3d_controller.h"
#include "scene3d_panel.h"
#include "skin3d_panel.h"
#include "skin3d_view.h"
#include "value_slider.h"
#include "ui_theme.h"
#include <QAction>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QScrollBar>
#include <QSlider>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

using namespace toon;
namespace {
Project skinProject(bool bound=true){
    auto project=Project::create();project.width=480;project.height=320;
    const auto model=builtInModels3D().first();Scene3D scene;scene.models.insert(model->id,model);
    ModelInstance3D object;object.id="skin-model";object.name="Cube";object.modelId=model->id;
    object.rig.joints={{"root","Root",{},{},{0,0,0}},{"tip","Tip","root",{},{0,.5f,0}}};
    if(bound)object.skin=autoBindSkin3D(*model,object.rig);
    scene.objects.append(object);auto layer=RasterLayer::create("3D reference");layer.kind=LayerKind::Scene3D;layer.scene3d=scene;
    project.shots[0].layers.prepend(layer);return project;
}
void chooseSkin(Scene3DController *controller){
    controller->selectObject("skin-model");controller->selectRigTool();controller->selectJoint("tip");
    controller->setRigEditMode(Scene3DController::RigEditMode::Weights);
}
QToolButton *action(QWidget &panel,const char *name){return panel.findChild<QToolButton *>(name);}
ValueSlider *value(QWidget &panel,const char *name){return panel.findChild<ValueSlider *>(name);}
QDoubleSpinBox *number(QWidget &panel,const char *name){return value(panel,name)->findChild<QDoubleSpinBox *>();}
void settleLayout(){for(int i=0;i<4;++i)QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);}
QTreeWidgetItem *jointItem(QTreeWidget *tree,const QString &id){
    for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString()==id)return *it;return nullptr;
}
}
class Skin3DPanelTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void bindingBusyKeepsLayoutAndLocksRestEdits(){
        Canvas canvas;canvas.setProject(skinProject(false));auto *controller=canvas.scene3D();chooseSkin(controller);
        Scene3DPanel panel(controller);panel.resize(240,820);panel.setRigMode();panel.show();settleLayout();
        auto *modes=panel.findChild<QTabWidget *>("rig3DEditMode");QCOMPARE(modes->currentIndex(),1);
        auto *tree=panel.findChild<QTreeWidget *>("rig3DJoints");auto *item=jointItem(tree,"tip");QVERIFY(item);
        auto *status=panel.findChild<QLabel *>("skin3DStatus");const auto rectangle=QRect(status->mapTo(&panel,QPoint()),status->size());
        QVERIFY(action(panel,"skin3DBind")->isEnabled());QVERIFY(!action(panel,"skin3DUnbind")->isEnabled());
        const int history=canvas.document()->history()->count();action(panel,"skin3DBind")->click();
        QVERIFY(controller->skinBindingBusy());QVERIFY(!action(panel,"skin3DBind")->isEnabled());
        QVERIFY(!action(panel,"rig3DAddChild")->isEnabled());QVERIFY(!value(panel,"rig3DX")->isEnabled());
        QCOMPARE(status->text(),QString("Binding…"));settleLayout();QCOMPARE(QRect(status->mapTo(&panel,QPoint()),status->size()),rectangle);
        QTRY_VERIFY_WITH_TIMEOUT(!controller->skinBindingBusy(),10000);QVERIFY(controller->selectedSkin());
        QCOMPARE(canvas.document()->history()->count(),history+1);QCOMPARE(jointItem(tree,"tip"),item);
        QVERIFY(!(item->flags()&Qt::ItemIsEditable));QVERIFY(item->flags()&Qt::ItemIsSelectable);
        QVERIFY(!value(panel,"rig3DX")->isEnabled());QCOMPARE(panel.findChild<QLabel *>("rig3DRestLabel")->text(),QString("Unbind to edit rest joints"));
        QVERIFY(value(panel,"rig3DX")->toolTip().contains("Unbind"));QVERIFY(item->toolTip(0).contains("Unbind"));
        tree->setCurrentItem(jointItem(tree,"root"));QCOMPARE(controller->selectedJointId(),QString("root"));
        settleLayout();QCOMPARE(QRect(status->mapTo(&panel,QPoint()),status->size()),rectangle);
        QVERIFY(action(panel,"skin3DUnbind")->isEnabled());action(panel,"skin3DUnbind")->click();
        QVERIFY(!controller->selectedSkin());QVERIFY(value(panel,"rig3DX")->isEnabled());
        canvas.document()->history()->undo();QVERIFY(controller->selectedSkin());QVERIFY(!value(panel,"rig3DX")->isEnabled());
    }
    void brushModesValuesAndBendCheckAreTransient(){
        Canvas canvas;canvas.setProject(skinProject());auto *controller=canvas.scene3D();chooseSkin(controller);
        Rig3DPanel panel(controller);const auto before=canvas.project().toJson();const int history=canvas.document()->history()->count();
        auto *modes=panel.findChild<QTabBar *>("skin3DBrushMode");QCOMPARE(modes->count(),3);
        modes->setCurrentIndex(1);QCOMPARE(controller->weightPaintMode(),Scene3DController::WeightPaintMode::Subtract);
        modes->setCurrentIndex(2);QCOMPARE(controller->weightPaintMode(),Scene3DController::WeightPaintMode::Smooth);
        number(panel,"skin3DBrushRadius")->setValue(86);number(panel,"skin3DBrushStrength")->setValue(42);
        QCOMPARE(controller->weightBrushRadius(),86.0);QCOMPARE(controller->weightBrushStrength(),.42);
        auto *axes=panel.findChild<QTabBar *>("skin3DPreviewAxis");number(panel,"skin3DPreviewAngle")->setValue(20);
        QCOMPARE(controller->skinPreviewRotation(),QVector3D(20,0,0));QVERIFY(!controller->canPaintWeights());
        QVERIFY(!value(panel,"skin3DBrushRadius")->isEnabled());QVERIFY(action(panel,"skin3DResetPreview")->isEnabled());
        axes->setCurrentIndex(1);QCOMPARE(number(panel,"skin3DPreviewAngle")->value(),0.0);number(panel,"skin3DPreviewAngle")->setValue(-15);
        QCOMPARE(controller->skinPreviewRotation(),QVector3D(20,-15,0));action(panel,"skin3DResetPreview")->click();
        QVERIFY(controller->skinPreviewRotation().isNull());QVERIFY(controller->canPaintWeights());
        QVERIFY(value(panel,"skin3DBrushRadius")->isEnabled());number(panel,"skin3DPreviewAngle")->setValue(30);
        panel.findChild<QTabWidget *>("rig3DEditMode")->setCurrentIndex(0);QVERIFY(controller->skinPreviewRotation().isNull());
        QCOMPARE(controller->rigEditMode(),Scene3DController::RigEditMode::Rest);QVERIFY(!controller->canPaintWeights());
        QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),history);
    }
    void locksDisablePaintingButKeepJointSelection(){
        Canvas canvas;canvas.setProject(skinProject());auto *controller=canvas.scene3D();chooseSkin(controller);Rig3DPanel panel(controller);
        auto object=*controller->selectedObject();object.locked=true;controller->updateObject(object);
        QVERIFY(!action(panel,"skin3DUnbind")->isEnabled());QVERIFY(!value(panel,"skin3DBrushRadius")->isEnabled());
        QVERIFY(!value(panel,"skin3DPreviewAngle")->isEnabled());auto *tree=panel.findChild<QTreeWidget *>("rig3DJoints");
        tree->setCurrentItem(jointItem(tree,"root"));QCOMPARE(controller->selectedJointId(),QString("root"));
        object.locked=false;controller->updateObject(object);QVERIFY(value(panel,"skin3DBrushRadius")->isEnabled());
        controller->selectJoint({});QVERIFY(!value(panel,"skin3DBrushRadius")->isEnabled());QVERIFY(!value(panel,"skin3DPreviewAngle")->isEnabled());
        QCOMPARE(panel.findChild<QLabel *>("skin3DSelectedJoint")->text(),QString("Select a joint"));
    }
    void pendingNumericEntrySurvivesRefreshAndSaveStaysTransient_data(){
        QTest::addColumn<QString>("fieldName");QTest::addColumn<QString>("typed");
        QTest::newRow("radius")<<QString("skin3DBrushRadius")<<QString("87");
        QTest::newRow("strength")<<QString("skin3DBrushStrength")<<QString("43");
        QTest::newRow("bend")<<QString("skin3DPreviewAngle")<<QString("31.5");
    }
    void pendingNumericEntrySurvivesRefreshAndSaveStaysTransient(){
        QFETCH(QString,fieldName);QFETCH(QString,typed);QTemporaryDir directory;const auto path=directory.filePath("skin.toonproj");QString error;
        const auto project=skinProject();QVERIFY2(saveProject(project,path,&error),qPrintable(error));
        MainWindow window(directory.filePath("workspace.ini"));QVERIFY(window.openProject(path));window.show();
        auto *controller=window.canvas()->scene3D();chooseSkin(controller);auto *panel=window.findChild<Scene3DPanel *>();panel->setRigMode();
        for(QWidget *parent=panel;parent;parent=parent->parentWidget())if(auto *dock=qobject_cast<QDockWidget *>(parent)){dock->show();dock->raise();}
        auto *field=value(window,fieldName.toUtf8().constData());auto *spin=field->findChild<QDoubleSpinBox *>();
        panel->ensureWidgetVisible(field);spin->setFocus();spin->selectAll();QTest::keyClicks(spin,typed);settleLayout();
        const auto rectangle=QRect(field->mapTo(panel,QPoint()),field->size());const int scroll=panel->verticalScrollBar()->value();
        for(int i=0;i<4;++i){QVERIFY(QMetaObject::invokeMethod(controller,"changed"));settleLayout();
            QCOMPARE(spin->findChild<QLineEdit *>()->text(),typed);QCOMPARE(QRect(field->mapTo(panel,QPoint()),field->size()),rectangle);QCOMPARE(panel->verticalScrollBar()->value(),scroll);}
        auto *save=window.findChild<QAction *>("saveProjectAction");QVERIFY(save->isEnabled());save->trigger();
        QCOMPARE(spin->value(),typed.toDouble());QCOMPARE(window.canvas()->document()->history()->count(),0);
        QVERIFY(window.canvas()->document()->history()->isClean());
        if(fieldName=="skin3DBrushRadius")QCOMPARE(controller->weightBrushRadius(),typed.toDouble());
        else if(fieldName=="skin3DBrushStrength")QCOMPARE(controller->weightBrushStrength(),typed.toDouble()/100.);
        else QCOMPARE(controller->skinPreviewRotation().x(),typed.toFloat());
        const auto loaded=loadProject(path,&error);QVERIFY2(loaded.has_value(),qPrintable(error));
        QCOMPARE(loaded->toJson(),project.toJson());spin->clearFocus();QCoreApplication::processEvents();
        QCOMPARE(window.canvas()->document()->history()->count(),0);
    }
    void narrowLayoutAccessibleControlsAndHeatLegend(){
        Canvas canvas;canvas.setProject(skinProject());auto *controller=canvas.scene3D();chooseSkin(controller);
        Scene3DPanel panel(controller);panel.resize(240,820);panel.setRigMode();panel.show();settleLayout();
        auto *skin=panel.findChild<Skin3DPanel *>();QVERIFY(skin);QVERIFY(skin->width()<=240);QCOMPARE(panel.horizontalScrollBar()->maximum(),0);
        for(auto *button:skin->findChildren<QToolButton *>()){
            if(!button->isVisibleTo(skin))continue;
            QVERIFY(!button->toolTip().isEmpty());QVERIFY(!button->accessibleName().isEmpty());QVERIFY(!button->icon().isNull());
            QCOMPARE(button->size(),QSize(28,28));QCOMPARE(button->iconSize(),QSize(18,18));
            QVERIFY(QRect(button->mapTo(skin,QPoint()),button->size()).right()<skin->width());
        }
        for(auto *tabs:skin->findChildren<QTabBar *>())for(int i=0;i<tabs->count();++i){QVERIFY(!tabs->tabToolTip(i).isEmpty());QVERIFY(!tabs->accessibleTabName(i).isEmpty());}
        for(auto *spin:skin->findChildren<QDoubleSpinBox *>()){QCOMPARE(spin->width(),48);QVERIFY(spin->suffix().isEmpty());}
        auto *legend=skin->findChild<QWidget *>("skin3DWeightLegend");QVERIFY(legend);const auto image=legend->grab().toImage();
        const auto left=image.pixelColor(1,2),right=image.pixelColor(image.width()-2,2);
        QVERIFY(left.blue()>left.red());QVERIFY(right.red()>right.blue());
        QVERIFY(skin->findChild<QLabel *>("skin3DSurfaceLabel")->toolTip().contains("front"));
    }
    void compactRowsKeepUsefulTracksAndStableJointContext(){
        auto project=skinProject();const QString longName="Character / Right shoulder / Upper arm / Painted influence";
        project.shots[0].layers[0].scene3d->objects[0].rig.joints[1].name=longName;
        Canvas canvas;canvas.setProject(project);auto *controller=canvas.scene3D();chooseSkin(controller);
        Skin3DPanel panel(controller);panel.resize(240,340);panel.show();settleLayout();
        auto *joint=panel.findChild<QLabel *>("skin3DSelectedJoint");auto *status=panel.findChild<QLabel *>("skin3DStatus");
        QCOMPARE(joint->text(),longName);QCOMPARE(joint->accessibleName(),longName);QVERIFY(joint->toolTip().contains(longName));
        const auto rows=panel.findChildren<PropertyRow *>();QCOMPARE(rows.size(),3);
        for(auto *row:rows){
            const auto *field=qobject_cast<ValueSlider *>(row->control());QVERIFY(field);
            QCOMPARE(field->findChild<QDoubleSpinBox *>()->width(),48);
            QVERIFY(field->findChild<QSlider *>()->width()>=74);
            QCOMPARE(row->labelWidget()->geometry().center().y(),field->geometry().center().y());
            QVERIFY(row->height()<=32);
        }
        auto *axes=panel.findChild<QTabBar *>("skin3DPreviewAxis");auto *reset=action(panel,"skin3DResetPreview");
        // A native 29px tab bar and a 28px action round their centers differently.
        QVERIFY(qAbs(axes->geometry().center().y()-reset->geometry().center().y())<=1);
        QVERIFY(axes->width()>=84);QVERIFY(axes->height()<=30);
        const auto jointBounds=joint->geometry(),statusBounds=status->geometry(),angleBounds=value(panel,"skin3DPreviewAngle")->parentWidget()->geometry();
        controller->selectJoint("root");settleLayout();
        QCOMPARE(joint->text(),QString("Root"));QCOMPARE(joint->geometry(),jointBounds);
        controller->setSkinPreviewRotation({20,0,0});settleLayout();
        QCOMPARE(status->text(),QString("Reset to paint"));QCOMPARE(status->geometry(),statusBounds);
        QCOMPARE(value(panel,"skin3DPreviewAngle")->parentWidget()->geometry(),angleBounds);
        QVERIFY(!value(panel,"skin3DBrushStrength")->isEnabled());QVERIFY(reset->isEnabled());
        action(panel,"skin3DResetPreview")->click();settleLayout();
        QCOMPARE(status->geometry(),statusBounds);QCOMPARE(joint->geometry(),jointBounds);
        QVERIFY(value(panel,"skin3DBrushStrength")->isEnabled());
        QVERIFY(panel.minimumSizeHint().height()<300);
    }
    void draftsSurviveCompactRowWrapping_data(){
        QTest::addColumn<QString>("fieldName");QTest::addColumn<QString>("draft");
        QTest::newRow("radius")<<QString("skin3DBrushRadius")<<QString("87");
        QTest::newRow("strength")<<QString("skin3DBrushStrength")<<QString("43");
        QTest::newRow("bend")<<QString("skin3DPreviewAngle")<<QString("-12.");
    }
    void draftsSurviveCompactRowWrapping(){
        QFETCH(QString,fieldName);QFETCH(QString,draft);
        Canvas canvas;canvas.setProject(skinProject());auto *controller=canvas.scene3D();chooseSkin(controller);
        Skin3DPanel panel(controller);panel.resize(320,420);panel.show();panel.raise();panel.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&panel));
        auto *field=value(panel,fieldName.toUtf8().constData());auto *spin=field->findChild<QDoubleSpinBox *>();
        auto *editor=spin->findChild<QLineEdit *>();const double original=field->value();
        const auto before=canvas.project().toJson();const int history=canvas.document()->history()->count();
        spin->setFocus();QTRY_VERIFY(spin->hasFocus()||editor->hasFocus());spin->selectAll();QTest::keyClicks(spin,draft);
        for(const auto &size:{QSize(240,380),QSize(190,420),QSize(320,420),QSize(240,380)}){
            panel.resize(size);QVERIFY(QMetaObject::invokeMethod(controller,"changed"));QTest::qWait(10);
            QCOMPARE(editor->text(),draft);QVERIFY(spin->hasFocus()||editor->hasFocus());
            QCOMPARE(spin->width(),48);QVERIFY(field->findChild<QSlider *>()->width()>=74);
            QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),history);
        }
        QTest::keyClick(spin,Qt::Key_Escape);QCOMPARE(field->value(),original);
        QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),history);
    }
    void shortDockKeepsBendControlsReachable(){
        Canvas canvas;canvas.setProject(skinProject());auto *controller=canvas.scene3D();chooseSkin(controller);
        Scene3DPanel panel(controller);panel.resize(240,360);panel.setRigMode();panel.show();QTest::qWait(20);
        auto *field=value(panel,"skin3DPreviewAngle");panel.ensureWidgetVisible(field);QTest::qWait(20);
        QCOMPARE(panel.horizontalScrollBar()->maximum(),0);
        QVERIFY(panel.viewport()->rect().contains(QRect(field->mapTo(panel.viewport(),QPoint()),field->size())));
        QVERIFY(field->findChild<QSlider *>()->width()>=74);QCOMPARE(field->findChild<QDoubleSpinBox *>()->width(),48);
        const auto rectangle=QRect(field->mapTo(panel.viewport(),QPoint()),field->size());
        const int scroll=panel.verticalScrollBar()->value();
        for(int i=0;i<3;++i){QVERIFY(QMetaObject::invokeMethod(controller,"changed"));QTest::qWait(20);
            QCOMPARE(QRect(field->mapTo(panel.viewport(),QPoint()),field->size()),rectangle);QCOMPARE(panel.verticalScrollBar()->value(),scroll);}
        auto *reset=action(panel,"skin3DResetPreview");panel.ensureWidgetVisible(reset);QTest::qWait(10);
        QVERIFY(panel.viewport()->rect().contains(QRect(reset->mapTo(panel.viewport(),QPoint()),reset->size())));
    }
    void preview(){
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");if(output.isEmpty())QSKIP("Opt-in skin weight panel preview.");
        Canvas canvas;canvas.setProject(skinProject());auto *controller=canvas.scene3D();chooseSkin(controller);
        Scene3DPanel panel(controller);panel.resize(320,640);panel.setRigMode();panel.show();settleLayout();QDir().mkpath(output);
        QVERIFY(panel.grab().save(output+"/skin3d-panel.png"));panel.resize(240,640);settleLayout();QVERIFY(panel.grab().save(output+"/skin3d-panel-narrow.png"));
        panel.resize(240,360);QTest::qWait(20);panel.ensureWidgetVisible(value(panel,"skin3DPreviewAngle"));QTest::qWait(20);
        auto *angle=value(panel,"skin3DPreviewAngle");
        QVERIFY(panel.viewport()->rect().contains(QRect(angle->mapTo(panel.viewport(),QPoint()),angle->size())));
        QVERIFY(panel.grab().save(output+"/skin3d-panel-short.png"));
    }
};
QTEST_MAIN(Skin3DPanelTests)
#include "test_skin3d_panel.moc"
