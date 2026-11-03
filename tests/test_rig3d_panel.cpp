#include "canvas.h"
#include "scene3d_controller.h"
#include "scene3d_panel.h"
#include "rig3d_panel.h"
#include "rig3d.h"
#include "value_slider.h"
#include "property_row.h"
#include "ui_theme.h"
#include "main_window.h"
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
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
Project smallRigProject(){auto project=Project::create();project.width=480;project.height=320;return project;}
Project savedRigProject(){
    auto project=smallRigProject();const auto model=builtInModels3D().first();Scene3D scene;scene.models.insert(model->id,model);
    ModelInstance3D object;object.id="rig-model";object.name="Cube";object.modelId=model->id;
    object.rig.joints.append({"root-joint","Root",{},{},{.125f,.25f,.375f}});scene.objects.append(object);
    auto layer=RasterLayer::create("3D reference");layer.kind=LayerKind::Scene3D;layer.scene3d=scene;project.shots[0].layers.prepend(layer);return project;
}
QToolButton *action(QWidget &panel,const char *id){return panel.findChild<QToolButton *>(id);}
ValueSlider *scalar(QWidget &panel,const char *id){return panel.findChild<ValueSlider *>(id);}
QTreeWidgetItem *jointItem(QTreeWidget *tree,const QString &id){
    QTreeWidgetItemIterator iterator(tree);while(*iterator){if((*iterator)->data(0,Qt::UserRole).toString()==id)return *iterator;++iterator;}return nullptr;
}
void chooseParent(QComboBox *combo,const QString &id){
    const int index=combo->findData(id);QVERIFY(index>=0);combo->setCurrentIndex(index);
    QVERIFY(QMetaObject::invokeMethod(combo,"activated",Q_ARG(int,index)));
}
void settleLayout(){for(int i=0;i<4;++i)QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);}
void showRigEditor(MainWindow &window){
    window.show();auto *controller=window.canvas()->scene3D();controller->selectObject("rig-model");
    controller->selectRigTool();controller->selectJoint("root-joint");
    auto *panel=window.findChild<Scene3DPanel *>();panel->setRigMode();
    for(QWidget *parent=panel;parent;parent=parent->parentWidget())if(auto *dock=qobject_cast<QDockWidget *>(parent)){dock->show();dock->raise();}
    settleLayout();
}
}
class Rig3DPanelTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void creationHierarchyParentingAndUndo(){
        Canvas canvas;canvas.setProject(smallRigProject());auto *controller=canvas.scene3D();
        QVERIFY(controller->addModel(builtInModels3D().front()));
        Scene3DPanel panel(controller);auto *tabs=panel.findChild<QTabWidget *>("scene3DPropertyTabs");
        QCOMPARE(tabs->count(),3);tabs->setCurrentIndex(2);QCOMPARE(canvas.tool(),Canvas::Tool::SceneRig);
        QVERIFY(action(panel,"rig3DCreateRoot")->isEnabled());
        action(panel,"rig3DCreateRoot")->click();QVERIFY(controller->selectedRig());QCOMPARE(controller->selectedRig()->joints.size(),1);
        QVERIFY(controller->selectedJoint());const QString root=controller->selectedJointId();
        QVERIFY(!action(panel,"rig3DCreateRoot")->isEnabled());QVERIFY(!action(panel,"rig3DCreateHumanoid")->isEnabled());
        action(panel,"rig3DAddChild")->click();QCOMPARE(controller->selectedRig()->joints.size(),2);const QString child=controller->selectedJointId();
        QCOMPARE(controller->selectedJoint()->parentId,root);
        auto *tree=panel.findChild<QTreeWidget *>("rig3DJoints");QVERIFY(tree);QVERIFY(jointItem(tree,child));
        QCOMPARE(jointItem(tree,child)->parent(),jointItem(tree,root));
        auto *parent=panel.findChild<QComboBox *>("rig3DParent");QCOMPARE(parent->findData(child),-1);QVERIFY(parent->findData(root)>=0);
        controller->selectJoint(root);QCOMPARE(parent->findData(root),-1);QCOMPARE(parent->findData(child),-1);
        action(panel,"rig3DAddRoot")->click();const QString separate=controller->selectedJointId();QCOMPARE(controller->selectedJoint()->parentId,QString());
        controller->selectJoint(child);const auto position=controller->selectedJoint()->position;
        chooseParent(parent,separate);QCOMPARE(controller->selectedJoint()->parentId,separate);QCOMPARE(controller->selectedJoint()->position,position);
        QCOMPARE(jointItem(tree,child)->parent(),jointItem(tree,separate));
        const int count=controller->selectedRig()->joints.size();controller->selectJoint(separate);
        action(panel,"rig3DDeleteBranch")->click();QCOMPARE(controller->selectedRig()->joints.size(),count-2);
        canvas.document()->history()->undo();QCOMPARE(controller->selectedRig()->joints.size(),count);
        QVERIFY(validateRig3D(*controller->selectedRig()).isEmpty());
        panel.setCameraMode(true);QCOMPARE(tabs->currentIndex(),1);panel.setCameraMode(false);QCOMPARE(tabs->currentIndex(),0);
        panel.setRigMode();QCOMPARE(tabs->currentIndex(),2);
    }
    void humanoidTemplateAndObjectLocks(){
        Canvas canvas;canvas.setProject(smallRigProject());auto *controller=canvas.scene3D();
        QVERIFY(controller->addModel(builtInModels3D()[3]));Rig3DPanel panel(controller);
        action(panel,"rig3DCreateHumanoid")->click();QVERIFY(controller->selectedRig()->joints.size()>=20);
        QVERIFY(validateRig3D(*controller->selectedRig()).isEmpty());
        QVERIFY(!action(panel,"rig3DCreateHumanoid")->isEnabled());
        auto object=*controller->selectedObject();object.locked=true;controller->updateObject(object);
        QVERIFY(!action(panel,"rig3DAddChild")->isEnabled());QVERIFY(!action(panel,"rig3DDeleteBranch")->isEnabled());
        QVERIFY(!scalar(panel,"rig3DX")->isEnabled());
        auto *tree=panel.findChild<QTreeWidget *>("rig3DJoints");QTreeWidgetItemIterator iterator(tree);
        while(*iterator){QVERIFY(!((*iterator)->flags()&Qt::ItemIsEditable));++iterator;}
        object.locked=false;controller->updateObject(object);QVERIFY(action(panel,"rig3DAddRoot")->isEnabled());
        canvas.document()->setLayersLocked({controller->layerId()},true);
        QVERIFY(!action(panel,"rig3DAddRoot")->isEnabled());
    }
    void mirroredEditingChangesOnlyEditedAxisAndCommitsOneGesture(){
        Canvas canvas;canvas.setProject(smallRigProject());auto *controller=canvas.scene3D();
        QVERIFY(controller->addModel(builtInModels3D().front()));Rig3DPanel panel(controller);
        action(panel,"rig3DCreateRoot")->click();action(panel,"rig3DAddChild")->click();
        const auto selected=controller->selectedJointId();auto joint=*controller->selectedJoint();
        joint.position={.3f,.334455f,.998877f};controller->updateJoint(joint);
        action(panel,"rig3DMirrorBranch")->click();const auto *source=rigJoint(*controller->selectedRig(),selected);QVERIFY(source);QVERIFY(!source->mirrorId.isEmpty());
        const auto mirrored=source->mirrorId;const auto y=source->position.y(),z=source->position.z();
        controller->selectJoint(selected);
        auto *field=scalar(panel,"rig3DX");auto *track=field->findChild<QSlider *>();
        const int history=canvas.document()->history()->index();
        track->setSliderDown(true);track->setValue(track->value()+20);track->setValue(track->value()+20);
        QCOMPARE(canvas.document()->history()->index(),history);QVERIFY(controller->editing());
        track->setSliderDown(false);QVERIFY(!controller->editing());QCOMPARE(canvas.document()->history()->index(),history+1);
        source=rigJoint(*controller->selectedRig(),selected);const auto *pair=rigJoint(*controller->selectedRig(),mirrored);
        QCOMPARE(source->position.y(),y);QCOMPARE(source->position.z(),z);QCOMPARE(pair->position.y(),y);QCOMPARE(pair->position.z(),z);
        QCOMPARE(pair->position.x(),2*controller->selectedRig()->mirrorPlaneX-source->position.x());
        const auto beforeCancel=*controller->selectedRig();track->setSliderDown(true);track->setValue(track->value()+30);field->cancelEditing();
        QCOMPARE(*controller->selectedRig(),beforeCancel);QVERIFY(!controller->editing());
        action(panel,"rig3DMirrorEdits")->click();QVERIFY(!controller->mirrorEdits());
        const auto fixed=rigJoint(*controller->selectedRig(),mirrored)->position;
        field->findChild<QDoubleSpinBox *>()->setValue(.8);QCOMPARE(rigJoint(*controller->selectedRig(),mirrored)->position,fixed);
        canvas.document()->history()->undo();QVERIFY(validateRig3D(*controller->selectedRig()).isEmpty());
    }
    void rejectedMirroredPositionRestoresNumber(){
        Canvas canvas;canvas.setProject(smallRigProject());auto *controller=canvas.scene3D();
        QVERIFY(controller->addModel(builtInModels3D().front()));Rig3DPanel panel(controller);
        action(panel,"rig3DCreateRoot")->click();action(panel,"rig3DMirrorBranch")->click();
        auto object=*controller->selectedObject();object.rig.mirrorPlaneX=750000;
        for(auto &joint:object.rig.joints)joint.position.setX(750000);
        controller->updateObject(object);QVERIFY(controller->selectedJoint());
        const auto accepted=*controller->selectedRig();const int history=canvas.document()->history()->index();
        QSignalSpy warning(controller,&Scene3DController::warning);
        auto *field=scalar(panel,"rig3DX");field->findChild<QDoubleSpinBox *>()->setValue(-1000000);
        QCOMPARE(warning.count(),1);QCOMPARE(*controller->selectedRig(),accepted);
        QCOMPARE(field->value(),750000.0);QCOMPARE(canvas.document()->history()->index(),history);
        QVERIFY(!controller->editing());
    }
    void hierarchyRowsAndUnfinishedEditorsSurviveRefresh(){
        Canvas canvas;canvas.setProject(smallRigProject());auto *controller=canvas.scene3D();
        QVERIFY(controller->addModel(builtInModels3D().front()));Scene3DPanel panel(controller);panel.resize(280,650);panel.setRigMode();panel.show();
        action(panel,"rig3DCreateRoot")->click();const auto selected=controller->selectedJointId();
        auto *tree=panel.findChild<QTreeWidget *>("rig3DJoints");auto *item=jointItem(tree,selected);QVERIFY(item);
        panel.ensureWidgetVisible(tree);tree->editItem(item,0);
        auto *editor=tree->findChild<QLineEdit *>();QVERIFY(editor);editor->selectAll();QTest::keyClicks(editor,"Pelvis custom");
        for(int i=0;i<4;++i){QVERIFY(QMetaObject::invokeMethod(controller,"changed"));settleLayout();QCOMPARE(jointItem(tree,selected),item);QCOMPARE(editor->text(),QString("Pelvis custom"));}
        QTest::keyClick(editor,Qt::Key_Return);
        // The item delegate queues Return-key commit/close until key handling ends.
        QTRY_COMPARE(controller->selectedJoint()->name,QString("Pelvis custom"));
        auto joint=*controller->selectedJoint();joint.position.setY(.334455f);controller->updateJoint(joint);
        auto *field=scalar(panel,"rig3DY");panel.ensureWidgetVisible(field);auto *number=field->findChild<QDoubleSpinBox *>();
        number->setFocus();number->selectAll();QTest::keyClicks(number,"1.25");
        const QString typed=number->findChild<QLineEdit *>()->text();QCOMPARE(typed,QString("1.25"));
        const auto rectangle=QRect(field->mapTo(&panel,QPoint()),field->size());const auto scroll=panel.verticalScrollBar()->value();
        for(int i=0;i<4;++i){QVERIFY(QMetaObject::invokeMethod(controller,"changed"));settleLayout();QCOMPARE(number->findChild<QLineEdit *>()->text(),typed);QCOMPARE(QRect(field->mapTo(&panel,QPoint()),field->size()),rectangle);QCOMPARE(panel.verticalScrollBar()->value(),scroll);}
        QTest::keyClick(number,Qt::Key_Escape);QCOMPARE(controller->selectedJoint()->position.y(),.334455f);
        auto moved=*controller->selectedJoint();moved.position.setX(.5);controller->updateJoint(moved);QCOMPARE(jointItem(tree,selected),item);
    }
    void viewsVisibilityAndNarrowLayout(){
        Canvas canvas;canvas.setProject(smallRigProject());auto *controller=canvas.scene3D();QVERIFY(controller->addModel(builtInModels3D().front()));
        Scene3DPanel panel(controller);panel.resize(240,680);panel.setRigMode();panel.show();action(panel,"rig3DCreateHumanoid")->click();settleLayout();
        const auto rig=*controller->selectedRig();const int history=canvas.document()->history()->index();
        action(panel,"rig3DVisible")->click();QVERIFY(!controller->rigVisible());QCOMPARE(*controller->selectedRig(),rig);QCOMPARE(canvas.document()->history()->index(),history);
        action(panel,"rig3DVisible")->click();QVERIFY(controller->rigVisible());
        action(panel,"rig3DFrontView")->click();QVERIFY(controller->scene()->camera.orthographic);
        action(panel,"rig3DSideView")->click();QVERIFY(controller->scene()->camera.orthographic);
        action(panel,"rig3DTopView")->click();QVERIFY(controller->scene()->camera.orthographic);
        QCOMPARE(*controller->selectedRig(),rig);
        auto camera=controller->scene()->camera;camera.locked=true;controller->updateCamera(camera);
        QVERIFY(!action(panel,"rig3DFrontView")->isEnabled());
        auto *rigPanel=panel.findChild<Rig3DPanel *>();QVERIFY(rigPanel);QVERIFY(rigPanel->width()<=240);
        for(auto *button:rigPanel->findChildren<QToolButton *>()){
            if(!button->isVisibleTo(rigPanel))continue;
            QVERIFY2(!button->toolTip().isEmpty(),qPrintable(button->objectName()));QVERIFY(!button->accessibleName().isEmpty());
            const QRect rect(button->mapTo(rigPanel,QPoint()),button->size());QVERIFY2(rect.right()<rigPanel->width(),qPrintable(button->objectName()));
        }
        for(auto *number:rigPanel->findChildren<QDoubleSpinBox *>()){QCOMPARE(number->width(),48);QVERIFY(number->suffix().isEmpty());}
        QCOMPARE(panel.horizontalScrollBar()->maximum(),0);
    }
    void compactRigUsesOnlyActivePageAndPinsNavigation(){
        Canvas canvas;canvas.setProject(smallRigProject());auto *controller=canvas.scene3D();
        QVERIFY(controller->addModel(builtInModels3D()[3]));Scene3DPanel panel(controller);
        panel.resize(240,600);panel.setRigMode();panel.show();action(panel,"rig3DCreateHumanoid")->click();QTest::qWait(20);
        auto *modes=panel.findChild<QTabWidget *>("rig3DEditMode");QCOMPARE(modes->iconSize(),QSize(18,18));
        auto *tree=panel.findChild<QTreeWidget *>("rig3DJoints");QVERIFY(tree->height()<=116);QVERIFY(tree->verticalScrollBar()->maximum()>0);
        QCOMPARE(panel.verticalScrollBar()->maximum(),0);
        for(const char *name:{"rig3DX","rig3DY","rig3DZ"}){
            auto *field=scalar(panel,name);QVERIFY(qobject_cast<PropertyRow *>(field->parentWidget()));
            QVERIFY(field->findChild<QSlider *>()->width()>=74);
        }
        for(const char *name:{"rig3DCreateRoot","rig3DCreateHumanoid","rig3DAddChild","rig3DAddRoot","rig3DDeleteBranch","rig3DMirrorBranch","rig3DMirrorEdits","rig3DVisible"}){
            auto *button=action(panel,name);QCOMPARE(button->size(),QSize(28,28));QCOMPARE(button->iconSize(),QSize(18,18));
            QVERIFY(!button->accessibleName().isEmpty());QVERIFY(!button->toolTip().isEmpty());
        }
        panel.resize(240,340);QTest::qWait(20);QVERIFY(tree->height()<=60);
        auto *header=panel.findChild<QWidget *>("scene3DPinnedHeader"),*footer=panel.findChild<QWidget *>("scene3DPinnedActions");
        const auto rectInPanel=[&](QWidget *widget){return QRect(widget->mapTo(&panel,QPoint()),widget->size());};
        const auto headerRect=rectInPanel(header),footerRect=rectInPanel(footer);
        QVERIFY(headerRect.contains(rectInPanel(modes)));
        QVERIFY(headerRect.contains(rectInPanel(action(panel,"rig3DVisible"))));
        for(const char *name:{"rig3DFrontView","rig3DSideView","rig3DTopView"})QVERIFY(footerRect.contains(rectInPanel(action(panel,name))));
        panel.ensureWidgetVisible(scalar(panel,"rig3DZ"));QTest::qWait(20);
        const QRect fieldRect(scalar(panel,"rig3DZ")->mapTo(panel.viewport(),QPoint()),scalar(panel,"rig3DZ")->size());
        QVERIFY(panel.viewport()->rect().contains(fieldRect));
        QCOMPARE(rectInPanel(header),headerRect);QCOMPARE(rectInPanel(footer),footerRect);
        const auto bar=rectInPanel(panel.verticalScrollBar());QVERIFY(!bar.intersects(rectInPanel(modes)));QVERIFY(!bar.intersects(footerRect));
        const auto history=canvas.document()->history()->count();
        modes->setCurrentIndex(2);QTest::qWait(20);panel.verticalScrollBar()->setValue(panel.verticalScrollBar()->maximum());
        const int poseScroll=panel.verticalScrollBar()->value();QVERIFY(poseScroll>0);
        modes->setCurrentIndex(1);QTest::qWait(20);panel.verticalScrollBar()->setValue(panel.verticalScrollBar()->maximum()/2);
        const int weightScroll=panel.verticalScrollBar()->value();QVERIFY(weightScroll>0);
        modes->setCurrentIndex(0);QTest::qWait(20);modes->setCurrentIndex(2);QTest::qWait(20);
        QCOMPARE(panel.verticalScrollBar()->value(),poseScroll);
        modes->setCurrentIndex(1);QTest::qWait(20);QCOMPARE(panel.verticalScrollBar()->value(),weightScroll);
        panel.setCameraMode(false);QTest::qWait(20);panel.setRigMode();QTest::qWait(20);
        QCOMPARE(panel.verticalScrollBar()->value(),weightScroll);QCOMPARE(canvas.document()->history()->count(),history);
    }
    void restDraftSurvivesResizeAndRenderEcho(){
        Canvas canvas;canvas.setProject(savedRigProject());auto *controller=canvas.scene3D();
        controller->selectObject("rig-model");controller->selectRigTool();controller->selectJoint("root-joint");
        Scene3DPanel panel(controller);panel.resize(320,600);panel.setRigMode();panel.show();panel.raise();panel.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&panel));
        auto *field=scalar(panel,"rig3DX");auto *number=field->findChild<QDoubleSpinBox *>();auto *edit=number->findChild<QLineEdit *>();
        number->setFocus();QTRY_VERIFY(number->hasFocus()||edit->hasFocus());number->selectAll();QTest::keyClicks(number,"-1.");
        const auto history=canvas.document()->history()->count();const auto original=*controller->selectedJoint();
        for(const auto size:{QSize(240,340),QSize(380,620),QSize(240,520)}){
            panel.resize(size);QVERIFY(QMetaObject::invokeMethod(controller,"changed"));QTest::qWait(20);
            QCOMPARE(edit->text(),QString("-1."));QVERIFY(number->hasFocus()||edit->hasFocus());
            QCOMPARE(number->width(),48);QVERIFY(field->findChild<QSlider *>()->width()>=74);
            QCOMPARE(*controller->selectedJoint(),original);QCOMPARE(canvas.document()->history()->count(),history);
        }
        QTest::keyClick(number,Qt::Key_Escape);QCOMPARE(*controller->selectedJoint(),original);QVERIFY(!controller->editing());
    }
    void saveKnownPathCommitsUnfinishedNumber_data(){
        QTest::addColumn<int>("axis");QTest::newRow("X")<<0;QTest::newRow("Y")<<1;QTest::newRow("Z")<<2;
    }
    void saveKnownPathCommitsUnfinishedNumber(){
        QFETCH(int,axis);QTemporaryDir directory;const auto path=directory.filePath("rig.toonproj");QString error;
        QVERIFY2(saveProject(savedRigProject(),path,&error),qPrintable(error));
        MainWindow window(directory.filePath("workspace.ini"));QVERIFY(window.openProject(path));showRigEditor(window);
        auto *controller=window.canvas()->scene3D();auto *history=window.canvas()->document()->history();
        const auto original=controller->selectedJoint()->position;const char *names[]={"rig3DX","rig3DY","rig3DZ"};
        auto *field=scalar(window,names[axis]);auto *number=field->findChild<QDoubleSpinBox *>();
        window.findChild<Scene3DPanel *>()->ensureWidgetVisible(field);number->setFocus();number->selectAll();QTest::keyClicks(number,"1.625");
        QCOMPARE(controller->selectedJoint()->position,original);QVERIFY(controller->editing());
        auto *save=window.findChild<QAction *>("saveProjectAction");QVERIFY(save->isEnabled());save->trigger();
        auto expected=original;expected[axis]=1.625f;QCOMPARE(controller->selectedJoint()->position,expected);
        QVERIFY(!controller->editing());QCOMPARE(history->count(),1);QVERIFY(history->isClean());
        const auto loaded=loadProject(path,&error);QVERIFY2(loaded.has_value(),qPrintable(error));
        QCOMPARE(loaded->shots.first().layers.first().scene3d->objects.first().rig.joints.first().position,expected);
        number->clearFocus();QCoreApplication::processEvents();QCOMPARE(history->count(),1);QVERIFY(history->isClean());
    }
    void saveKnownPathCommitsUnfinishedJointName(){
        QTemporaryDir directory;const auto path=directory.filePath("rig.toonproj");QString error;
        QVERIFY2(saveProject(savedRigProject(),path,&error),qPrintable(error));
        MainWindow window(directory.filePath("workspace.ini"));QVERIFY(window.openProject(path));showRigEditor(window);
        auto *controller=window.canvas()->scene3D();auto *history=window.canvas()->document()->history();
        auto *tree=window.findChild<QTreeWidget *>("rig3DJoints");tree->editItem(jointItem(tree,"root-joint"));
        auto *editor=tree->findChild<QLineEdit *>();QVERIFY(editor);editor->selectAll();QTest::keyClicks(editor,"Pelvis rest");
        QCOMPARE(controller->selectedJoint()->name,QString("Root"));QVERIFY(controller->editing());
        window.findChild<QAction *>("saveProjectAction")->trigger();
        QCOMPARE(controller->selectedJoint()->name,QString("Pelvis rest"));QVERIFY(!controller->editing());QCOMPARE(history->count(),1);QVERIFY(history->isClean());
        const auto loaded=loadProject(path,&error);QVERIFY2(loaded.has_value(),qPrintable(error));
        QCOMPARE(loaded->shots.first().layers.first().scene3d->objects.first().rig.joints.first().name,QString("Pelvis rest"));
    }
    void globalUndoCancelsEditorAndLaterBlurCannotReapply_data(){
        QTest::addColumn<QString>("editorKind");QTest::addColumn<bool>("escape");
        QTest::newRow("typed-number")<<QString("number")<<false;
        QTest::newRow("slider-drag")<<QString("slider")<<false;QTest::newRow("inline-name")<<QString("name")<<false;
        QTest::newRow("number-escape")<<QString("number")<<true;QTest::newRow("name-escape")<<QString("name")<<true;
    }
    void globalUndoCancelsEditorAndLaterBlurCannotReapply(){
        QFETCH(QString,editorKind);QFETCH(bool,escape);QTemporaryDir directory;const auto path=directory.filePath("rig.toonproj");QString error;
        QVERIFY2(saveProject(savedRigProject(),path,&error),qPrintable(error));
        MainWindow window(directory.filePath("workspace.ini"));QVERIFY(window.openProject(path));showRigEditor(window);
        auto *controller=window.canvas()->scene3D();auto *history=window.canvas()->document()->history();const auto original=*controller->selectedJoint();
        auto *field=scalar(window,"rig3DX");auto *number=field->findChild<QDoubleSpinBox *>();auto *track=field->findChild<QSlider *>();
        QPointer<QLineEdit> nameEditor;
        if(editorKind=="name"){
            auto *tree=window.findChild<QTreeWidget *>("rig3DJoints");tree->editItem(jointItem(tree,"root-joint"));
            nameEditor=tree->findChild<QLineEdit *>();QVERIFY(nameEditor);nameEditor->selectAll();QTest::keyClicks(nameEditor,"Abandoned name");
        }else if(editorKind=="slider"){
            track->setSliderDown(true);track->setValue(1500);QVERIFY(controller->selectedJoint()->position!=original.position);
        }else{
            window.findChild<Scene3DPanel *>()->ensureWidgetVisible(field);number->setFocus();number->selectAll();QTest::keyClicks(number,"1.625");
        }
        QVERIFY(controller->editing());
        if(escape)QTest::keyClick(editorKind=="name"?static_cast<QWidget *>(nameEditor):static_cast<QWidget *>(number),Qt::Key_Escape);
        else{auto *undo=window.findChild<QAction *>("undoAction");QVERIFY(undo->isEnabled());undo->trigger();}
        QVERIFY(!controller->editing());QCOMPARE(*controller->selectedJoint(),original);QCOMPARE(history->count(),0);QVERIFY(history->isClean());
        number->clearFocus();track->setSliderDown(false);if(nameEditor)nameEditor->clearFocus();window.canvas()->setFocus();
        QCoreApplication::processEvents();
        if(nameEditor)QVERIFY(!nameEditor->isVisible());
        QCOMPARE(*controller->selectedJoint(),original);QCOMPARE(history->count(),0);QVERIFY(!controller->editing());
        QCOMPARE(field->value(),double(original.position.x()));QVERIFY(!track->isSliderDown());
    }
    void preview(){
        const QString output=qEnvironmentVariable("TOON_UI_PREVIEW");if(output.isEmpty())QSKIP("Opt-in rig panel preview.");
        Canvas canvas;canvas.setProject(smallRigProject());auto *controller=canvas.scene3D();QVERIFY(controller->addModel(builtInModels3D()[3]));
        Scene3DPanel panel(controller);panel.resize(320,600);panel.setRigMode();panel.show();action(panel,"rig3DCreateHumanoid")->click();settleLayout();QTest::qWait(20);
        QDir().mkpath(output);QVERIFY(panel.grab().save(output+"/rig3d-panel.png"));
        panel.resize(240,600);settleLayout();QTest::qWait(20);QVERIFY(panel.grab().save(output+"/rig3d-panel-narrow.png"));
        panel.resize(240,340);settleLayout();QTest::qWait(20);panel.ensureWidgetVisible(scalar(panel,"rig3DZ"));QTest::qWait(20);
        QVERIFY(panel.viewport()->rect().contains(QRect(scalar(panel,"rig3DZ")->mapTo(panel.viewport(),QPoint()),scalar(panel,"rig3DZ")->size())));
        QVERIFY(panel.grab().save(output+"/rig3d-panel-short.png"));
    }
};
QTEST_MAIN(Rig3DPanelTests)
#include "test_rig3d_panel.moc"
