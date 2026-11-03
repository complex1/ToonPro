#include "canvas.h"
#include "main_window.h"
#include "pose3d_panel.h"
#include "property_row.h"
#include "rig3d_panel.h"
#include "scene3d_controller.h"
#include "scene3d_panel.h"
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
Project poseProject(bool bound=true){
    auto project=Project::create();project.width=480;project.height=320;
    const auto model=builtInModels3D().first();Scene3D scene;scene.models.insert(model->id,model);
    ModelInstance3D object;object.id="pose-model";object.name="Cube";object.modelId=model->id;
    object.rig.joints={
        {"root","Root",{},{},{0,0,0}},
        {"left-shoulder","Left shoulder","root","right-shoulder",{-.2f,.5f,0}},
        {"left-elbow","Left elbow","left-shoulder","right-elbow",{-.65f,.5f,0}},
        {"left-hand","Left hand","left-elbow","right-hand",{-1.05f,.5f,0}},
        {"right-shoulder","Right shoulder","root","left-shoulder",{.2f,.5f,0}},
        {"right-elbow","Right elbow","right-shoulder","left-elbow",{.65f,.5f,0}},
        {"right-hand","Right hand","right-elbow","left-hand",{1.05f,.5f,0}}};
    if(bound)object.skin=autoBindSkin3D(*model,object.rig);
    scene.objects.append(object);auto layer=RasterLayer::create("3D reference");layer.kind=LayerKind::Scene3D;layer.scene3d=scene;
    project.shots[0].layers.prepend(layer);return project;
}
void choosePose(Scene3DController *controller,const QString &joint="left-hand"){
    controller->selectObject("pose-model");controller->selectRigTool();controller->selectJoint(joint);
    controller->setRigEditMode(Scene3DController::RigEditMode::Pose);
}
QToolButton *action(QWidget &panel,const char *name){return panel.findChild<QToolButton *>(name);}
ValueSlider *value(QWidget &panel,const char *name){return panel.findChild<ValueSlider *>(name);}
QDoubleSpinBox *number(QWidget &panel,const char *name){return value(panel,name)->findChild<QDoubleSpinBox *>();}
void settleLayout(){for(int i=0;i<4;++i)QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);}
QTreeWidgetItem *jointItem(QTreeWidget *tree,const QString &id){
    for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString()==id)return *it;return nullptr;
}
void showPose(MainWindow &window,const QString &joint){
    window.show();choosePose(window.canvas()->scene3D(),joint);auto *panel=window.findChild<Scene3DPanel *>();panel->setRigMode();
    for(QWidget *parent=panel;parent;parent=parent->parentWidget())if(auto *dock=qobject_cast<QDockWidget *>(parent)){dock->show();dock->raise();}
}
}
class Pose3DPanelTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void fkEditsOnlyOneAxisAndCommitsOneGesture(){
        Canvas canvas;canvas.setProject(poseProject());auto *controller=canvas.scene3D();choosePose(controller);
        controller->updatePoseRotation({0,12.3456f,-7.6543f});Rig3DPanel panel(controller);
        auto *modes=panel.findChild<QTabWidget *>("rig3DEditMode");QCOMPARE(modes->count(),3);QCOMPARE(modes->currentIndex(),2);
        auto *field=value(panel,"pose3DRotationX");auto *track=field->findChild<QSlider *>();const auto before=controller->poseRotation();
        auto *history=canvas.document()->history();const int initial=history->index();
        track->setSliderDown(true);track->setValue(100);track->setValue(200);QVERIFY(controller->editing());QCOMPARE(history->index(),initial);
        track->setSliderDown(false);QVERIFY(!controller->editing());QCOMPARE(history->index(),initial+1);
        QCOMPARE(controller->poseRotation().x(),20.f);QCOMPARE(controller->poseRotation().y(),before.y());QCOMPARE(controller->poseRotation().z(),before.z());
        const auto committed=*controller->selectedPose();track->setSliderDown(true);track->setValue(450);field->cancelEditing();
        QCOMPARE(*controller->selectedPose(),committed);QVERIFY(!controller->editing());QCOMPARE(history->index(),initial+1);
        action(panel,"pose3DRotationAxisY")->click();QCOMPARE(controller->poseRotationAxis(),1);QCOMPARE(history->index(),initial+1);
        history->undo();QCOMPARE(controller->poseRotation(),before);
        QVERIFY(panel.findChild<QWidget *>("pose3DBody")->isHidden());controller->selectJoint("root");QVERIFY(!panel.findChild<QWidget *>("pose3DBody")->isHidden());
        number(panel,"pose3DBodyY")->setValue(.625);QCOMPARE(controller->poseRootTranslation(),QVector3D(0,.625f,0));
    }
    void ikControlsKeepTargetAndPoleIndependentAndPinLocksFk(){
        Canvas canvas;canvas.setProject(poseProject());auto *controller=canvas.scene3D();choosePose(controller);Rig3DPanel panel(controller);
        auto *modes=panel.findChild<QTabBar *>("pose3DMode");modes->setCurrentIndex(1);QCOMPARE(controller->poseMode(),Scene3DController::PoseMode::IK);
        QVERIFY(action(panel,"pose3DIKEnabled")->isEnabled());QVERIFY(!value(panel,"pose3DIKX")->isEnabled());
        action(panel,"pose3DIKEnabled")->click();QVERIFY(controller->selectedIKTarget());QVERIFY(value(panel,"pose3DIKX")->isEnabled());
        const auto initial=*controller->selectedIKTarget();QVERIFY(!initial.pinned);
        number(panel,"pose3DIKX")->setValue(-.775);auto control=*controller->selectedIKTarget();
        QCOMPARE(control.target,QVector3D(-.775f,initial.target.y(),initial.target.z()));QCOMPARE(control.pole,initial.pole);
        auto *handles=panel.findChild<QTabBar *>("pose3DIKHandle");handles->setCurrentIndex(1);QCOMPARE(controller->ikHandle(),Scene3DController::IKHandle::Pole);
        const auto beforePole=control;number(panel,"pose3DIKZ")->setValue(.325);control=*controller->selectedIKTarget();
        QCOMPARE(control.target,beforePole.target);QCOMPARE(control.pole,QVector3D(beforePole.pole.x(),beforePole.pole.y(),.325f));
        action(panel,"pose3DIKPinned")->click();QVERIFY(controller->selectedIKTarget()->pinned);
        QVERIFY(action(panel,"pose3DIKPinned")->toolTip().contains("model-local"));
        controller->selectJoint("left-elbow");modes->setCurrentIndex(0);QVERIFY(!value(panel,"pose3DRotationX")->isEnabled());
        QCOMPARE(panel.findChild<QLabel *>("pose3DStatus")->text(),QString("Unpin to rotate this joint"));
        controller->selectJoint("left-hand");modes->setCurrentIndex(1);action(panel,"pose3DIKPinned")->click();QVERIFY(!controller->selectedIKTarget()->pinned);
        controller->selectJoint("left-elbow");modes->setCurrentIndex(0);QVERIFY(value(panel,"pose3DRotationX")->isEnabled());
        controller->selectJoint("left-hand");modes->setCurrentIndex(1);action(panel,"pose3DIKEnabled")->click();QVERIFY(!controller->selectedIKTarget());
        canvas.document()->history()->undo();QVERIFY(controller->selectedIKTarget());
        controller->selectJoint("root");QVERIFY(!action(panel,"pose3DIKEnabled")->isEnabled());QVERIFY(!value(panel,"pose3DIKX")->isEnabled());
    }
    void limitsMirrorAndResetsRemainUndoable(){
        Canvas canvas;canvas.setProject(poseProject());auto *controller=canvas.scene3D();choosePose(controller);Rig3DPanel panel(controller);
        QVERIFY(!value(panel,"pose3DLimitMin")->isEnabled());action(panel,"pose3DLimitsEnabled")->click();QVERIFY(controller->selectedPoseLimit().enabled);
        auto *axes=panel.findChild<QTabBar *>("pose3DLimitAxis");axes->setCurrentIndex(2);
        number(panel,"pose3DLimitMin")->setValue(-20);number(panel,"pose3DLimitMax")->setValue(35);
        const auto limit=controller->selectedPoseLimit();QCOMPARE(limit.minimum,QVector3D(-180,-180,-20));QCOMPARE(limit.maximum,QVector3D(180,180,35));
        number(panel,"pose3DRotationZ")->setValue(70);QCOMPARE(controller->poseRotation().z(),35.f);QCOMPARE(number(panel,"pose3DRotationZ")->value(),35.0);
        const auto posed=*controller->selectedPose();auto mirrored=posed;QString error;QVERIFY2(mirrorPose3D(mirrored,*controller->selectedRig(),&error),qPrintable(error));
        action(panel,"pose3DMirror")->click();QCOMPARE(*controller->selectedPose(),mirrored);
        canvas.document()->history()->undo();QCOMPARE(*controller->selectedPose(),posed);
        action(panel,"pose3DResetSelected")->click();QVERIFY(controller->poseRotation().isNull());QCOMPARE(controller->selectedPose()->limits,posed.limits);
        canvas.document()->history()->undo();QCOMPARE(*controller->selectedPose(),posed);
        controller->selectJoint("root");number(panel,"pose3DBodyX")->setValue(.4);QVERIFY(!controller->poseRootTranslation().isNull());
        const auto withBody=*controller->selectedPose();action(panel,"pose3DResetAll")->click();QVERIFY(pose3DIsIdentity(*controller->selectedPose()));
        QCOMPARE(controller->selectedPose()->limits,withBody.limits);canvas.document()->history()->undo();QCOMPARE(*controller->selectedPose(),withBody);
    }
    void boundAndLockRequirementsPreserveHierarchySelection(){
        Canvas canvas;canvas.setProject(poseProject(false));auto *controller=canvas.scene3D();choosePose(controller);Rig3DPanel panel(controller);
        QVERIFY(!value(panel,"pose3DRotationX")->isEnabled());QVERIFY(!action(panel,"pose3DIKEnabled")->isEnabled());
        QCOMPARE(panel.findChild<QLabel *>("pose3DStatus")->text(),QString("Bind the model to pose"));
        canvas.setProject(poseProject());choosePose(controller);QVERIFY(value(panel,"pose3DRotationX")->isEnabled());
        auto object=*controller->selectedObject();object.locked=true;controller->updateObject(object);
        QVERIFY(!value(panel,"pose3DRotationX")->isEnabled());QVERIFY(!action(panel,"pose3DMirror")->isEnabled());
        auto *tree=panel.findChild<QTreeWidget *>("rig3DJoints");auto *root=jointItem(tree,"root");QVERIFY(root);QVERIFY(root->flags()&Qt::ItemIsSelectable);
        tree->setCurrentItem(root);QCOMPARE(controller->selectedJointId(),QString("root"));QVERIFY(!value(panel,"pose3DBodyX")->isEnabled());
        object.locked=false;controller->updateObject(object);QVERIFY(value(panel,"pose3DBodyX")->isEnabled());
        canvas.document()->setLayersLocked({controller->layerId()},true);QVERIFY(!value(panel,"pose3DBodyX")->isEnabled());
    }
    void saveKnownPathCommitsPendingPoseValue_data(){
        QTest::addColumn<QString>("kind");QTest::addColumn<QString>("fieldName");QTest::addColumn<QString>("typed");
        QTest::newRow("rotation")<<QString("rotation")<<QString("pose3DRotationY")<<QString("31.5");
        QTest::newRow("body")<<QString("body")<<QString("pose3DBodyX")<<QString("0.625");
        QTest::newRow("target")<<QString("target")<<QString("pose3DIKX")<<QString("-0.775");
        QTest::newRow("pole")<<QString("pole")<<QString("pose3DIKZ")<<QString("0.325");
        QTest::newRow("limit")<<QString("limit")<<QString("pose3DLimitMax")<<QString("37.5");
    }
    void saveKnownPathCommitsPendingPoseValue(){
        QFETCH(QString,kind);QFETCH(QString,fieldName);QFETCH(QString,typed);QTemporaryDir directory;const auto path=directory.filePath("pose.toonproj");QString error;
        QVERIFY2(saveProject(poseProject(),path,&error),qPrintable(error));MainWindow window(directory.filePath("workspace.ini"));QVERIFY(window.openProject(path));
        showPose(window,kind=="body"?"root":"left-hand");auto *controller=window.canvas()->scene3D();auto *panel=window.findChild<Scene3DPanel *>();
        if(kind=="target"||kind=="pole"){
            controller->setPoseMode(Scene3DController::PoseMode::IK);controller->enableSelectedIK(true);QVERIFY(controller->selectedIKTarget());
            if(kind=="pole")controller->setIKHandle(Scene3DController::IKHandle::Pole);
        }else if(kind=="limit")action(window,"pose3DLimitsEnabled")->click();
        auto *save=window.findChild<QAction *>("saveProjectAction");save->trigger();
        auto *history=window.canvas()->document()->history();const int initial=history->count();const auto before=*controller->selectedPose();
        auto *field=value(window,fieldName.toUtf8().constData());auto *spin=field->findChild<QDoubleSpinBox *>();panel->ensureWidgetVisible(field);
        spin->setFocus();spin->selectAll();QTest::keyClicks(spin,typed);settleLayout();QVERIFY(controller->editing());QCOMPARE(*controller->selectedPose(),before);
        const auto rectangle=QRect(field->mapTo(panel,QPoint()),field->size());const int scroll=panel->verticalScrollBar()->value();
        for(int i=0;i<3;++i){QVERIFY(QMetaObject::invokeMethod(controller,"changed"));settleLayout();QCOMPARE(spin->findChild<QLineEdit *>()->text(),typed);
            QCOMPARE(QRect(field->mapTo(panel,QPoint()),field->size()),rectangle);QCOMPARE(panel->verticalScrollBar()->value(),scroll);}
        QVERIFY(save->isEnabled());save->trigger();QVERIFY(!controller->editing());QCOMPARE(history->count(),initial+1);QVERIFY(history->isClean());
        QCOMPARE(spin->value(),typed.toDouble());
        if(kind=="rotation")QCOMPARE(controller->poseRotation().y(),typed.toFloat());
        else if(kind=="body")QCOMPARE(controller->poseRootTranslation().x(),typed.toFloat());
        else if(kind=="target")QCOMPARE(controller->selectedIKTarget()->target.x(),typed.toFloat());
        else if(kind=="pole")QCOMPARE(controller->selectedIKTarget()->pole.z(),typed.toFloat());
        else QCOMPARE(controller->selectedPoseLimit().maximum.x(),typed.toFloat());
        const auto loaded=loadProject(path,&error);QVERIFY2(loaded.has_value(),qPrintable(error));
        QCOMPARE(loaded->shots.first().layers.first().scene3d->objects.first().pose,*controller->selectedPose());
        spin->clearFocus();QCoreApplication::processEvents();QCOMPARE(history->count(),initial+1);QVERIFY(history->isClean());
    }
    void undoAndEscapeDiscardPendingPoseInput_data(){
        QTest::addColumn<bool>("drag");QTest::addColumn<bool>("escape");
        QTest::newRow("typed-undo")<<false<<false;QTest::newRow("drag-undo")<<true<<false;QTest::newRow("typed-escape")<<false<<true;
    }
    void undoAndEscapeDiscardPendingPoseInput(){
        QFETCH(bool,drag);QFETCH(bool,escape);QTemporaryDir directory;const auto path=directory.filePath("pose.toonproj");QString error;
        QVERIFY2(saveProject(poseProject(),path,&error),qPrintable(error));MainWindow window(directory.filePath("workspace.ini"));QVERIFY(window.openProject(path));showPose(window,"left-hand");
        auto *controller=window.canvas()->scene3D();const auto before=*controller->selectedPose();auto *history=window.canvas()->document()->history();
        auto *field=value(window,"pose3DRotationX");auto *spin=field->findChild<QDoubleSpinBox *>();auto *track=field->findChild<QSlider *>();
        window.findChild<Scene3DPanel *>()->ensureWidgetVisible(field);
        if(drag){track->setSliderDown(true);track->setValue(300);QVERIFY(*controller->selectedPose()!=before);}
        else{spin->setFocus();spin->selectAll();QTest::keyClicks(spin,"32.5");}
        QVERIFY(controller->editing());
        if(escape)QTest::keyClick(spin,Qt::Key_Escape);else{auto *undo=window.findChild<QAction *>("undoAction");QVERIFY(undo->isEnabled());undo->trigger();}
        QVERIFY(!controller->editing());QCOMPARE(*controller->selectedPose(),before);QCOMPARE(history->count(),0);
        spin->clearFocus();track->setSliderDown(false);window.canvas()->setFocus();QCoreApplication::processEvents();
        QCOMPARE(*controller->selectedPose(),before);QCOMPARE(history->count(),0);QVERIFY(history->isClean());QCOMPARE(field->value(),0.0);
    }
    void narrowLayoutKeepsStatusAndAccessibleControlsStable(){
        Canvas canvas;canvas.setProject(poseProject());auto *controller=canvas.scene3D();choosePose(controller,"root");
        Scene3DPanel panel(controller);panel.resize(240,900);panel.setRigMode();panel.show();settleLayout();
        auto *pose=panel.findChild<Pose3DPanel *>();QVERIFY(pose);QVERIFY(pose->width()<=240);QCOMPARE(panel.horizontalScrollBar()->maximum(),0);
        auto *status=pose->findChild<QLabel *>("pose3DStatus");const auto statusRect=QRect(status->mapTo(pose,QPoint()),status->size());
        for(auto *button:pose->findChildren<QToolButton *>()){
            if(!button->isVisibleTo(pose))continue;QVERIFY(!button->toolTip().isEmpty());QVERIFY(!button->accessibleName().isEmpty());
            QVERIFY(!button->icon().isNull()||!button->text().isEmpty());QCOMPARE(button->size(),QSize(28,28));
            if(!button->icon().isNull())QCOMPARE(button->iconSize(),QSize(18,18));
            QVERIFY(QRect(button->mapTo(pose,QPoint()),button->size()).right()<pose->width());
        }
        for(auto *tabs:pose->findChildren<QTabBar *>())for(int i=0;i<tabs->count();++i){QVERIFY(!tabs->tabToolTip(i).isEmpty());QVERIFY(!tabs->accessibleTabName(i).isEmpty());}
        for(auto *spin:pose->findChildren<QDoubleSpinBox *>()){QCOMPARE(spin->width(),48);QVERIFY(spin->suffix().isEmpty());}
        controller->setPoseMode(Scene3DController::PoseMode::IK);settleLayout();QCOMPARE(QRect(status->mapTo(pose,QPoint()),status->size()),statusRect);
        controller->selectJoint("left-hand");controller->enableSelectedIK(true);settleLayout();QCOMPARE(QRect(status->mapTo(pose,QPoint()),status->size()),statusRect);
        QCOMPARE(panel.horizontalScrollBar()->maximum(),0);
    }
    void activePoseModeDoesNotReserveHiddenBodySpace(){
        Canvas canvas;canvas.setProject(poseProject());auto *controller=canvas.scene3D();choosePose(controller,"root");
        Pose3DPanel panel(controller);panel.resize(240,700);panel.show();settleLayout();
        auto *pages=panel.findChild<QWidget *>("pose3DPages");auto *body=panel.findChild<QWidget *>("pose3DBody");
        QVERIFY(pages);QVERIFY(body->isVisibleTo(&panel));QVERIFY(pages->hasHeightForWidth());
        const int rootHeight=pages->heightForWidth(pages->width());
        controller->setPoseMode(Scene3DController::PoseMode::IK);settleLayout();
        const int ikHeight=pages->heightForWidth(pages->width());QVERIFY(ikHeight<rootHeight);
        QVERIFY(!body->isVisibleTo(&panel));QVERIFY(panel.findChild<QWidget *>("pose3DIKPage")->isVisibleTo(&panel));
        controller->selectJoint("left-hand");controller->setPoseMode(Scene3DController::PoseMode::FK);settleLayout();
        const int jointHeight=pages->heightForWidth(pages->width());QVERIFY(jointHeight<ikHeight);QVERIFY(body->isHidden());
        for(auto *field:panel.findChildren<ValueSlider *>()){
            if(!field->isVisibleTo(&panel))continue;
            QVERIFY(qobject_cast<PropertyRow *>(field->parentWidget()));
            QVERIFY(field->findChild<QSlider *>()->width()>=74);QCOMPARE(field->findChild<QDoubleSpinBox *>()->width(),48);
        }
        controller->selectJoint("root");settleLayout();QCOMPARE(pages->heightForWidth(pages->width()),rootHeight);
    }
    void poseDraftSurvivesNarrowAndShortResize_data(){
        QTest::addColumn<QString>("kind");QTest::addColumn<QString>("fieldName");
        QTest::newRow("rotation")<<QString("rotation")<<QString("pose3DRotationX");
        QTest::newRow("body")<<QString("body")<<QString("pose3DBodyX");
        QTest::newRow("target")<<QString("target")<<QString("pose3DIKX");
        QTest::newRow("pole")<<QString("pole")<<QString("pose3DIKZ");
        QTest::newRow("limit")<<QString("limit")<<QString("pose3DLimitMin");
    }
    void poseDraftSurvivesNarrowAndShortResize(){
        QFETCH(QString,kind);QFETCH(QString,fieldName);
        Canvas canvas;canvas.setProject(poseProject());auto *controller=canvas.scene3D();choosePose(controller,kind=="body"?"root":"left-hand");
        Scene3DPanel panel(controller);panel.resize(320,520);panel.setRigMode();panel.show();
        panel.raise();panel.activateWindow();QVERIFY(QTest::qWaitForWindowActive(&panel));
        if(kind=="target"||kind=="pole"){
            controller->setPoseMode(Scene3DController::PoseMode::IK);controller->enableSelectedIK(true);
            if(kind=="pole")controller->setIKHandle(Scene3DController::IKHandle::Pole);
        }else if(kind=="limit")action(panel,"pose3DLimitsEnabled")->click();
        auto *field=value(panel,fieldName.toUtf8().constData());auto *spin=field->findChild<QDoubleSpinBox *>();auto *editor=spin->findChild<QLineEdit *>();
        settleLayout();panel.ensureWidgetVisible(field);const auto original=*controller->selectedPose();const double originalValue=field->value();
        const int history=canvas.document()->history()->count();spin->setFocus();QTRY_VERIFY(spin->hasFocus()||editor->hasFocus());
        spin->selectAll();QTest::keyClicks(spin,"-12.");QCOMPARE(editor->text(),QString("-12."));QVERIFY(controller->editing());
        for(const auto size:{QSize(240,300),QSize(360,600),QSize(240,420)}){
            panel.resize(size);QVERIFY(QMetaObject::invokeMethod(controller,"changed"));QTest::qWait(10);panel.ensureWidgetVisible(field);
            QCOMPARE(editor->text(),QString("-12."));QVERIFY(spin->hasFocus()||editor->hasFocus());
            QCOMPARE(spin->width(),48);QVERIFY(field->findChild<QSlider *>()->width()>=74);
            QCOMPARE(panel.horizontalScrollBar()->maximum(),0);QCOMPARE(*controller->selectedPose(),original);
            QCOMPARE(canvas.document()->history()->count(),history);
        }
        QTest::keyClick(spin,Qt::Key_Escape);QCOMPARE(field->value(),originalValue);QVERIFY(!controller->editing());
        QCOMPARE(*controller->selectedPose(),original);QCOMPARE(canvas.document()->history()->count(),history);
    }
    void switchingFkIkAndTargetPoleResolvesPendingEntryOnce(){
        Canvas canvas;canvas.setProject(poseProject());auto *controller=canvas.scene3D();choosePose(controller,"left-hand");
        Pose3DPanel panel(controller);panel.resize(240,480);panel.show();panel.raise();panel.activateWindow();QVERIFY(QTest::qWaitForWindowActive(&panel));
        auto *history=canvas.document()->history();const int initial=history->count();
        auto *rotation=number(panel,"pose3DRotationX");rotation->setFocus();rotation->selectAll();QTest::keyClicks(rotation,"31.5");QVERIFY(controller->editing());
        panel.findChild<QTabBar *>("pose3DMode")->setCurrentIndex(1);QVERIFY(!controller->editing());
        QCOMPARE(controller->poseRotation().x(),31.5f);QCOMPARE(history->count(),initial+1);
        panel.findChild<QTabBar *>("pose3DMode")->setCurrentIndex(0);QCOMPARE(history->count(),initial+1);
        panel.findChild<QTabBar *>("pose3DMode")->setCurrentIndex(1);action(panel,"pose3DIKEnabled")->click();QVERIFY(controller->selectedIKTarget());
        const auto before=*controller->selectedIKTarget();const int targetHistory=history->count();
        auto *target=number(panel,"pose3DIKX");target->setFocus();target->selectAll();QTest::keyClicks(target,"-0.725");QVERIFY(controller->editing());
        panel.findChild<QTabBar *>("pose3DIKHandle")->setCurrentIndex(1);QVERIFY(!controller->editing());
        QCOMPARE(controller->selectedIKTarget()->target.x(),-.725f);QCOMPARE(controller->selectedIKTarget()->pole,before.pole);
        QCOMPARE(history->count(),targetHistory+1);QCOMPARE(target->value(),double(qRound(before.pole.x()*1000))/1000.);
        panel.findChild<QTabBar *>("pose3DIKHandle")->setCurrentIndex(0);QCOMPARE(target->value(),-.725);QCOMPARE(history->count(),targetHistory+1);
        history->undo();QCOMPARE(*controller->selectedIKTarget(),before);
    }
    void preview(){
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");if(output.isEmpty())QSKIP("Opt-in pose panel preview.");
        Canvas canvas;canvas.setProject(poseProject());auto *controller=canvas.scene3D();choosePose(controller,"root");
        Scene3DPanel panel(controller);panel.resize(320,720);panel.setRigMode();panel.show();QTest::qWait(20);settleLayout();QDir().mkpath(output);
        QVERIFY(panel.grab().save(output+"/pose3d-fk-panel.png"));panel.resize(240,720);settleLayout();QVERIFY(panel.grab().save(output+"/pose3d-fk-panel-narrow.png"));
        controller->selectJoint("left-hand");controller->setPoseMode(Scene3DController::PoseMode::IK);controller->enableSelectedIK(true);controller->setIKPinned(true);settleLayout();
        QVERIFY(panel.grab().save(output+"/pose3d-ik-panel-narrow.png"));panel.resize(320,720);settleLayout();QVERIFY(panel.grab().save(output+"/pose3d-ik-panel.png"));
        panel.resize(240,360);QTest::qWait(20);auto *ikField=value(panel,"pose3DIKZ");panel.ensureWidgetVisible(ikField);QTest::qWait(20);
        QVERIFY(panel.viewport()->rect().contains(QRect(ikField->mapTo(panel.viewport(),QPoint()),ikField->size())));
        QVERIFY(panel.grab().save(output+"/pose3d-ik-panel-short.png"));
        controller->setIKPinned(false);controller->setPoseMode(Scene3DController::PoseMode::FK);controller->selectJoint("root");
        panel.resize(240,300);QTest::qWait(20);auto *bodyField=value(panel,"pose3DBodyZ");panel.ensureWidgetVisible(bodyField);QTest::qWait(20);
        QVERIFY(panel.viewport()->rect().contains(QRect(bodyField->mapTo(panel.viewport(),QPoint()),bodyField->size())));
        QVERIFY(panel.grab().save(output+"/pose3d-fk-panel-short.png"));
    }
};
QTEST_MAIN(Pose3DPanelTests)
#include "test_pose3d_panel.moc"
