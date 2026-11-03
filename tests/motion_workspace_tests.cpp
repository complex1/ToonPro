#include "canvas.h"
#include "keyframe_transform_tool.h"
#include "main_window.h"
#include "motion_controls.h"
#include "sheet.h"
#include "timeline_controller.h"
#include "value_slider.h"
#include <QAction>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QMouseEvent>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>
using namespace toon;
namespace {
Project fixture(){
    auto project=Project::create();project.width=192;project.height=144;project.transparent=true;
    auto &shot=project.shots[0];shot.durationFrames=24;shot.layers.resize(1);
    auto &layer=shot.layers[0];QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);
    QPainter painter(&tile);painter.fillRect(QRect(32,24,64,48),QColor("#e76f51"));painter.end();layer.tiles.insert({0,0},tile);
    shot.layerAnimations={{layer.id,{64,48},{
        {KeyframeProperty::PositionX,{{0,8},{12,54,KeyframeInterpolation::EaseOut}}},
        {KeyframeProperty::PositionY,{{0,5},{12,24}}},
        {KeyframeProperty::Rotation,{{0,30},{12,80}}},
        {KeyframeProperty::ScaleX,{{0,1.6}}},
        {KeyframeProperty::ScaleY,{{0,.75}}}
    },{}}};return project;
}
bool samePose(const QTransform &a,const QTransform &b){
    for(const auto &point:QVector<QPointF>{{0,0},{32,24},{96,72}})
        if(QLineF(a.map(point),b.map(point)).length()>.00001)return false;
    return true;
}
void mouse(Canvas &canvas,QEvent::Type type,QPointF point){
    QMouseEvent event(type,point,canvas.mapToGlobal(point.toPoint()),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                      type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&canvas,&event);
}
}
class MotionWorkspaceTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void nativePendingPivotEntrySaveUndoAndEscape(){
        QTemporaryDir dir;QVERIFY(dir.isValid());QString error;const auto project=fixture();const auto id=project.shots[0].layers[0].id;
        const auto path=dir.filePath("motion.toonproj");QVERIFY2(saveProject(project,path,&error),qPrintable(error));
        MainWindow window(dir.filePath("workspace.ini"));QVERIFY(window.openProject(path));window.resize(1320,900);window.show();
        auto *controller=window.findChild<TimelineController *>();auto *dock=window.findChild<QDockWidget *>("framePropertiesPanel");
        QVERIFY(controller&&dock);dock->show();dock->raise();
        auto *tabs=dock->findChild<QTabBar *>("framePropertiesTabs");QVERIFY(tabs);tabs->setCurrentIndex(1);
        QVERIFY(controller->setFrame(6));controller->editAnimationPivot();QCoreApplication::processEvents();
        auto *field=dock->findChild<ValueSlider *>("keyframePivotX");QVERIFY(field);QVERIFY(field->isEnabled());
        auto *spin=field->findChild<QDoubleSpinBox *>();auto *undo=window.findChild<QAction *>("undoAction");auto *save=window.findChild<QAction *>("saveProjectAction");QVERIFY(spin&&undo&&save);
        const auto original=window.canvas()->project().toJson();const auto pose=worldLayerTransform(window.canvas()->shot(),id,6);
        auto *history=controller->document()->history();
        const auto enter=[&](const QString &text){spin->setFocus();spin->selectAll();QTest::keyClicks(spin,text);};
        enter("96");QVERIFY(controller->hasPendingEdits());QVERIFY(undo->isEnabled());undo->trigger();
        QVERIFY(!controller->hasPendingEdits());QVERIFY(!controller->document()->keyframeEditActive());QCOMPARE(window.canvas()->project().toJson(),original);QCOMPARE(history->count(),0);
        enter("104");QVERIFY(controller->hasPendingEdits());QTest::keyClick(spin,Qt::Key_Escape);
        QVERIFY(!controller->hasPendingEdits());QCOMPARE(window.canvas()->project().toJson(),original);
        enter("96");QVERIFY(controller->hasPendingEdits());save->trigger();QVERIFY(!controller->hasPendingEdits());
        QCOMPARE(history->count(),1);QVERIFY(history->isClean());QCOMPARE(controller->animationPivot().x(),96.);
        QVERIFY(samePose(worldLayerTransform(window.canvas()->shot(),id,6),pose));
        const auto saved=loadProject(path,&error);QVERIFY2(saved.has_value(),qPrintable(error));
        QCOMPARE(layerAnimation(saved->shots[0],id)->pivot.x(),96.);QVERIFY(samePose(worldLayerTransform(saved->shots[0],id,6),pose));
        window.canvas()->setFocus();QCoreApplication::processEvents();QCOMPARE(history->count(),1);QVERIFY(history->isClean());QVERIFY(window.close());
    }
    void nativePendingPivotDragUndoAndSaveResolveOnce(){
        QTemporaryDir dir;QVERIFY(dir.isValid());QString error;const auto project=fixture();const auto path=dir.filePath("drag.toonproj");
        QVERIFY2(saveProject(project,path,&error),qPrintable(error));MainWindow window(dir.filePath("workspace.ini"));QVERIFY(window.openProject(path));
        window.resize(1280,900);window.show();QCoreApplication::processEvents();auto *controller=window.findChild<TimelineController *>();QVERIFY(controller);
        QVERIFY(controller->setFrame(6));controller->editAnimationPivot();auto *canvas=window.canvas();canvas->fitSheet();QCoreApplication::processEvents();
        auto *tool=canvas->keyframeTransformTool();QVERIFY(tool->enabled());QVERIFY(controller->pivotEditing());
        const auto original=canvas->project().toJson();const auto id=controller->selectedPropertyLayerId();const auto pose=worldLayerTransform(canvas->shot(),id,6);
        const auto from=tool->pivotHandle(),to=from+QPointF(42,26);const auto begin=[&]{mouse(*canvas,QEvent::MouseButtonPress,from);mouse(*canvas,QEvent::MouseMove,to);};
        auto *undo=window.findChild<QAction *>("undoAction");auto *save=window.findChild<QAction *>("saveProjectAction");QVERIFY(undo&&save);
        begin();QVERIFY(tool->dragging());QVERIFY(undo->isEnabled());undo->trigger();QVERIFY(!tool->dragging());
        mouse(*canvas,QEvent::MouseButtonRelease,to);QCOMPARE(canvas->project().toJson(),original);QCOMPARE(controller->document()->history()->count(),0);
        begin();QVERIFY(tool->dragging());QVERIFY(save->isEnabled());
        QVERIFY(window.findChild<QAction *>("saveProjectAsAction")->isEnabled());
        save->trigger();QVERIFY(!tool->dragging());QCOMPARE(controller->document()->history()->count(),1);
        QVERIFY(controller->document()->history()->isClean());QVERIFY(samePose(worldLayerTransform(canvas->shot(),id,6),pose));
        const auto saved=loadProject(path,&error);QVERIFY2(saved.has_value(),qPrintable(error));QCOMPARE(saved->toJson(),canvas->project().toJson());
        mouse(*canvas,QEvent::MouseButtonRelease,to);QCOMPARE(controller->document()->history()->count(),1);QVERIFY(window.close());
    }
};
QTEST_MAIN(MotionWorkspaceTests)
#include "motion_workspace_tests.moc"
