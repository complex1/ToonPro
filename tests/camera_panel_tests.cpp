#include "timeline_test_geometry.h"
#include "camera_panel.h"
#include "camera_animation.h"
#include "canvas.h"
#include "document.h"
#include "property_row.h"
#include "timeline_controller.h"
#include "timeline_panel.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QAbstractScrollArea>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QScrollBar>
#include <QSlider>
#include <QStyle>
#include <QTabBar>
#include <QToolButton>
#include <QtTest>
using namespace toon;
namespace {
Project fixture(bool scenes=false) {
    auto project=Project::create();project.width=160;project.height=120;project.shots[0].durationFrames=40;
    if(scenes)for(int i=0;i<2;++i){auto layer=RasterLayer::create(QString("City reference %1").arg(i+1));layer.kind=LayerKind::Scene3D;layer.scene3d=Scene3D{};project.shots[0].layers.append(layer);}
    return project;
}
QToolButton *button(QWidget &panel,const char *id){return panel.findChild<QToolButton *>(id);}
ValueSlider *value(QWidget &panel,const char *id){return panel.findChild<ValueSlider *>(id);}
QDoubleSpinBox *number(QWidget &panel,const char *id){return value(panel,id)->findChild<QDoubleSpinBox *>();}
void settle(){QCoreApplication::processEvents();for(int i=0;i<4;++i)QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);}
void type(QWidget &panel,const char *id,const QString &text){auto *spin=number(panel,id);spin->setFocus();spin->selectAll();QTest::keyClicks(spin,text);}
QVector<int> frames(const Canvas &canvas,const QString &layerId={}) {
    QVector<int> result;if(layerId.isEmpty())for(const auto &key:canvas.shot().camera2DKeys)result.append(key.frame);
    else if(const auto *animation=sceneCameraAnimation(canvas.shot(),layerId))for(const auto &key:animation->keys)result.append(key.frame);return result;
}
void drag(QAbstractScrollArea *grid,QPoint from,QPoint to,Qt::KeyboardModifiers modifiers=Qt::NoModifier){
    QTest::mousePress(grid->viewport(),Qt::LeftButton,modifiers,from);
    QMouseEvent movement(QEvent::MouseMove,to,grid->viewport()->mapToGlobal(to),Qt::NoButton,Qt::LeftButton,modifiers);
    QApplication::sendEvent(grid->viewport(),&movement);QTest::mouseRelease(grid->viewport(),Qt::LeftButton,modifiers,to);
}
int cameraRow(const Canvas &canvas) {
    // The fixture has no animated layer lanes: cameras follow top-level artwork
    // and audio so existing drawing-row positions remain unchanged.
    int rows=canvas.shot().audioClips.size();for(const auto &layer:canvas.shot().layers)if(layer.parentId.isEmpty())++rows;return rows;
}
QPoint cell(QAbstractScrollArea *grid,int row,int frame){
    const int label=timeline_test::labelWidth(grid);
    return {label+frame*20+10-grid->horizontalScrollBar()->value(),timeline_test::rowCenter(grid,row)};
}
int lightPixels(const QImage &image){
    int count=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto pixel=image.pixelColor(x,y);if(pixel.alpha()>100&&qGray(pixel.rgb())>150)++count;}return count;
}
}
class CameraPanelTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void staticCameraAndWholePoseKeysPreserveArtworkSelection(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);const auto artwork=controller.selectedPropertyLayerId();
        FramePropertiesPanel panel(&controller);panel.resize(240,650);panel.show();settle();
        auto *tabs=panel.findChild<QTabBar *>("framePropertiesTabs");QCOMPARE(tabs->count(),6);tabs->setCurrentIndex(3);
        QVERIFY(controller.cameraSelected());QCOMPARE(controller.selectedPropertyLayerId(),artwork);
        QVERIFY(value(panel,"cameraX")->isEnabled());number(panel,"cameraX")->setValue(12);
        QCOMPARE(canvas.shot().camera2D.position.x(),12.0);QVERIFY(canvas.shot().camera2DKeys.isEmpty());
        button(panel,"cameraEnable")->click();QCOMPARE(frames(canvas),QVector<int>{0});
        QVERIFY(controller.setFrame(10));number(panel,"cameraX")->setValue(80);number(panel,"cameraZoom")->setValue(200);number(panel,"cameraRotation")->setValue(45);
        QCOMPARE(frames(canvas),(QVector<int>{0,10}));const auto pose=evaluateShotCamera(canvas.shot(),10);
        QCOMPARE(pose.position.x(),80.0);QCOMPARE(pose.zoom,2.0);QCOMPARE(pose.rotation,45.0);QCOMPARE(controller.selectedPropertyLayerId(),artwork);
        button(panel,"cameraEnable")->click();QVERIFY(canvas.shot().camera2DKeys.isEmpty());QCOMPARE(canvas.shot().camera2D,pose);
        canvas.document()->history()->undo();QCOMPARE(frames(canvas),(QVector<int>{0,10}));
    }
    void draftRefreshDragCancelAndNoChangeHaveSingleUndo(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);CameraPanel panel(&controller);panel.resize(240,650);panel.show();panel.activate();settle();
        button(panel,"cameraEnable")->click();QVERIFY(controller.setFrame(8));auto *history=canvas.document()->history();const int before=history->count();
        auto *field=value(panel,"cameraX");auto *slider=field->findChild<QSlider *>();slider->setSliderDown(true);slider->setValue(2000);slider->setValue(4000);
        QCOMPARE(evaluateShotCamera(canvas.shot(),8).position.x(),40.0);QCOMPARE(history->count(),before);QVERIFY(controller.hasPendingEdits());
        slider->setSliderDown(false);QCOMPARE(history->count(),before+1);QVERIFY(!controller.hasPendingEdits());
        slider->setSliderDown(true);slider->setValue(6000);field->cancelEditing();QCOMPARE(evaluateShotCamera(canvas.shot(),8).position.x(),40.0);QCOMPARE(history->count(),before+1);
        type(panel,"cameraX","75");for(int i=0;i<3;++i){emit controller.changed();QCOMPARE(number(panel,"cameraX")->findChild<QLineEdit *>()->text(),QString("75"));}
        controller.cancelEditing();QCOMPARE(evaluateShotCamera(canvas.shot(),8).position.x(),40.0);QCOMPARE(history->count(),before+1);
        type(panel,"cameraX","90");controller.finishEditing();QCOMPARE(evaluateShotCamera(canvas.shot(),8).position.x(),90.0);QCOMPARE(history->count(),before+2);
        slider->setSliderDown(true);slider->setValue(11000);slider->setValue(9000);slider->setSliderDown(false);QCOMPARE(history->count(),before+2);
    }
    void navigationEasingAndRetimeRejectOccupiedKeys(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);CameraPanel panel(&controller);panel.show();panel.activate();
        button(panel,"cameraEnable")->click();QVERIFY(controller.setFrame(10));number(panel,"cameraX")->setValue(80);
        button(panel,"cameraPrevious")->click();QCOMPARE(controller.currentFrame(),0);panel.findChild<QTabBar *>("cameraInterpolation")->setCurrentIndex(int(KeyframeInterpolation::Hold));
        QCOMPARE(evaluateShotCamera(canvas.shot(),5).position.x(),0.0);button(panel,"cameraNext")->click();QCOMPARE(controller.currentFrame(),10);
        const int before=canvas.document()->history()->count();number(panel,"cameraKeyTime")->setValue(1);QCOMPARE(frames(canvas),(QVector<int>{0,10}));QCOMPARE(number(panel,"cameraKeyTime")->value(),11.0);QCOMPARE(canvas.document()->history()->count(),before);
        number(panel,"cameraKeyTime")->setValue(15);QCOMPARE(controller.currentFrame(),14);QCOMPARE(frames(canvas),(QVector<int>{0,14}));
        QVERIFY(controller.setFrame(6));button(panel,"cameraAdd")->click();QCOMPARE(frames(canvas),(QVector<int>{0,6,14}));button(panel,"cameraRemove")->click();QCOMPARE(frames(canvas),(QVector<int>{0,14}));
    }
    void sceneTargetSwitchFlushesDraftAndHonorsLocks(){
        Canvas canvas;canvas.setProject(fixture(true));TimelineController controller(&canvas);FramePropertiesPanel panel(&controller);panel.resize(240,680);panel.show();
        const auto first=canvas.shot().layers[canvas.shot().layers.size()-2].id,last=canvas.shot().layers.last().id;const auto artwork=controller.selectedPropertyLayerId();
        QVERIFY(controller.selectCamera(last));settle();QCOMPARE(panel.findChild<QTabBar *>("framePropertiesTabs")->currentIndex(),3);QCOMPARE(panel.findChild<QTabBar *>("cameraMode")->currentIndex(),1);
        QCOMPARE(panel.findChild<QLabel *>("cameraTargetName")->text(),QString("City reference 2"));QCOMPARE(controller.selectedPropertyLayerId(),artwork);
        button(panel,"cameraEnable")->click();QVERIFY(controller.setFrame(5));number(panel,"cameraTargetX")->setValue(3);number(panel,"cameraYaw")->setValue(60);
        QCOMPARE(frames(canvas,last),(QVector<int>{0,5}));QVERIFY(!sceneCameraAnimation(canvas.shot(),first));
        const auto pose=evaluateSceneCamera(canvas.shot(),last,5);QCOMPARE(pose.target.x(),3.0f);QCOMPARE(pose.yaw,60.0f);
        QVERIFY(value(panel,"cameraFov")->isEnabled());QVERIFY(!value(panel,"cameraOrthoScale")->isEnabled());
        type(panel,"cameraDistance","8");panel.findChild<QTabBar *>("cameraMode")->setCurrentIndex(0);
        QCOMPARE(evaluateSceneCamera(canvas.shot(),last,5).distance,8.0f);QVERIFY(controller.selectedCameraLayerId().isEmpty());QCOMPARE(canvas.shot().camera2D.position,QPointF());
        QVERIFY(controller.selectCamera(last));canvas.document()->setLayersLocked({last},true);
        QVERIFY(!button(panel,"cameraEnable")->isEnabled());QVERIFY(!value(panel,"cameraTargetX")->isEnabled());QVERIFY(!button(panel,"cameraFraming")->isEnabled());
    }
    void viewAndFramingAreTransient(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);CameraPanel panel(&controller);panel.activate();
        const auto before=canvas.project().toJson();const int count=canvas.document()->history()->count();
        QVERIFY(controller.cameraView());button(panel,"cameraView")->click();QVERIFY(!controller.cameraView());
        button(panel,"cameraView")->click();QVERIFY(controller.cameraView());button(panel,"cameraFraming")->click();QVERIFY(controller.cameraFramingEditing());QVERIFY(!controller.cameraView());
        button(panel,"cameraView")->click();QVERIFY(controller.cameraView());QVERIFY(!controller.cameraFramingEditing());
        QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),count);
    }
    void timelineSelectionDragClipboardAndSceneSeek(){
        Canvas canvas;canvas.setProject(fixture(true));TimelineController controller(&canvas);const auto artwork=controller.selectedPropertyLayerId();
        QVERIFY(controller.selectCamera());QVERIFY(controller.setCameraEnabled(true));QVERIFY(controller.setFrame(10));QVERIFY(controller.setCameraKey());
        const auto scene=canvas.shot().layers.last().id;QVERIFY(controller.selectCamera(scene));QVERIFY(controller.setCameraEnabled(true));
        QVERIFY(controller.selectPropertyLayer(artwork));TimelinePanel panel(&controller);FramePropertiesPanel properties(&controller);panel.resize(760,450);properties.resize(240,650);panel.show();properties.show();settle();
        auto *grid=panel.findChild<QAbstractScrollArea *>("timelineGrid");grid->verticalScrollBar()->setValue(0);grid->horizontalScrollBar()->setValue(0);
        const int row=cameraRow(canvas);QTest::mouseClick(grid->viewport(),Qt::LeftButton,Qt::NoModifier,cell(grid,row,10));
        QVERIFY(controller.cameraSelected());QVERIFY(controller.selectedCameraLayerId().isEmpty());QCOMPARE(controller.selectedPropertyLayerId(),artwork);QCOMPARE(properties.findChild<QTabBar *>("framePropertiesTabs")->currentIndex(),3);
        const int before=canvas.document()->history()->count();drag(grid,cell(grid,row,10),cell(grid,row,14));QCOMPARE(frames(canvas),(QVector<int>{0,14}));QCOMPARE(controller.currentFrame(),14);QCOMPARE(canvas.document()->history()->count(),before+1);
        QTest::mouseClick(grid->viewport(),Qt::LeftButton,Qt::NoModifier,cell(grid,row,0));QTest::mouseClick(grid->viewport(),Qt::LeftButton,Qt::ShiftModifier,cell(grid,row,14));QCOMPARE(controller.selectedKeys().size(),2);
        QTest::keyClick(grid,Qt::Key_C,Qt::ControlModifier);QVERIFY(controller.setFrame(20));QTest::keyClick(grid,Qt::Key_V,Qt::ControlModifier);QCOMPARE(frames(canvas),(QVector<int>{0,14,20,34}));
        grid->horizontalScrollBar()->setValue(0);QTest::mouseClick(grid->viewport(),Qt::LeftButton,Qt::NoModifier,cell(grid,row+2,10));
        QVERIFY(controller.cameraSelected());QCOMPARE(controller.selectedCameraLayerId(),scene);QCOMPARE(controller.currentFrame(),10);QCOMPARE(controller.selectedPropertyLayerId(),artwork);
        QCOMPARE(properties.findChild<QLabel *>("cameraTargetName")->text(),QString("City reference 2"));
        QTest::keyClick(grid,Qt::Key_Delete);QCOMPARE(frames(canvas,scene),QVector<int>{0});canvas.document()->history()->undo();QCOMPARE(frames(canvas,scene),(QVector<int>{0,10}));
    }
    void compactCameraRowsResizeWithoutCommittingDraft(){
        Canvas canvas;canvas.setProject(fixture(true));TimelineController controller(&canvas);CameraPanel panel(&controller);
        panel.resize(240,650);panel.show();panel.activate();settle();
        for(const auto *id:{"cameraX","cameraY","cameraZoom","cameraRotation"}){
            auto *row=panel.findChild<PropertyRow *>(QString(id)+"Row");QVERIFY(row);
            auto *field=value(panel,id);QVERIFY(row->height()<=field->height()+2);QVERIFY(field->findChild<QSlider *>()->width()>=76);
        }
        for(const auto *id:{"cameraEnable","cameraFraming","cameraView"}){QCOMPARE(button(panel,id)->iconSize(),QSize(16,16));QCOMPARE(button(panel,id)->size(),QSize(28,28));}
        button(panel,"cameraEnable")->click();const int count=canvas.document()->history()->count();type(panel,"cameraX","75");
        auto *spin=number(panel,"cameraX");QFont larger=panel.font();larger.setPixelSize(panel.fontMetrics().height()+6);panel.setFont(larger);
        for(int width:{320,190,240}){
            panel.resize(width,650);settle();QCOMPARE(panel.width(),width);QVERIFY(spin->hasFocus());
            QCOMPARE(spin->findChild<QLineEdit *>()->text(),QString("75"));QVERIFY(controller.hasPendingEdits());QCOMPARE(canvas.document()->history()->count(),count);
            auto *field=value(panel,"cameraX");QVERIFY(field->findChild<QSlider *>()->width()>=76);
            const QRect bounds(field->mapTo(&panel,QPoint()),field->size());
            QVERIFY2(bounds.left()>=0&&bounds.right()<panel.width(),qPrintable(QString("Camera editor %1..%2 exceeds panel width %3").arg(bounds.left()).arg(bounds.right()).arg(panel.width())));
        }
        controller.cancelEditing();QCOMPARE(evaluateShotCamera(canvas.shot(),0).position.x(),0.);QCOMPARE(canvas.document()->history()->count(),count);
    }
    void narrowAccessibleLayoutAndScreenshots(){
        Canvas canvas;canvas.setProject(fixture(true));TimelineController controller(&canvas);FramePropertiesPanel panel(&controller);panel.resize(240,650);panel.show();panel.findChild<QTabBar *>("framePropertiesTabs")->setCurrentIndex(3);settle();
        const QString output=qEnvironmentVariable("TOON_UI_PREVIEW","test-artifacts");QVERIFY(QDir().mkpath(output));
        for(int mode:{0,1}){
            panel.findChild<QTabBar *>("cameraMode")->setCurrentIndex(mode);settle();QCOMPARE(panel.width(),240);
            for(auto *spin:panel.findChildren<QDoubleSpinBox *>()){QCOMPARE(spin->width(),48);QVERIFY(spin->suffix().isEmpty());}
            for(auto *control:panel.findChildren<QToolButton *>())if(control->isVisibleTo(&panel)){
                QVERIFY(!control->toolTip().isEmpty());QVERIFY(!control->accessibleName().isEmpty());QCOMPARE(control->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,control),600);
                const auto bounds=QRect(control->mapTo(&panel,QPoint()),control->size());QVERIFY2(bounds.left()>=0&&bounds.right()<panel.width(),qPrintable(control->objectName()));
            }
            QPixmap screenshot;
            const auto completeActionRow=[&]{
                screenshot=panel.grab();const auto pixels=screenshot.toImage();const qreal scale=screenshot.devicePixelRatio();
                for(const char *id:{"cameraEnable","cameraFraming","cameraView","cameraKeysLabel"}){
                    auto *control=panel.findChild<QWidget *>(id);if(!control||!control->isVisibleTo(&panel))return false;
                    const auto area=QRect(control->mapTo(&panel,QPoint()),control->size()).adjusted(6,6,-6,-6);
                    if(!panel.rect().contains(area))return false;
                    const QRect physical(qRound(area.x()*scale),qRound(area.y()*scale),qRound(area.width()*scale),qRound(area.height()*scale));
                    if(lightPixels(pixels.copy(physical))<=5)return false;
                }
                return true;
            };
            // Tab changes resize the scroll contents. Give queued Qt paints a
            // chance to finish, while still rejecting persistently missing UI.
            QTRY_VERIFY2_WITH_TIMEOUT(completeActionRow(),"Camera action row remained incompletely painted",1000);
            const auto pixels=screenshot.toImage();const qreal scale=screenshot.devicePixelRatio();
            QVERIFY(screenshot.save(QDir(output).filePath(QString("camera-properties-%1d-240.png").arg(mode?3:2))));
            for(const char *id:{"cameraEnable","cameraFraming","cameraView","cameraKeysLabel"}){
                auto *control=panel.findChild<QWidget *>(id);QVERIFY(control);QVERIFY(control->isVisibleTo(&panel));
                // Inspect the actual panel capture, excluding the button border:
                // an icon object alone does not prove the visible action painted.
                const auto area=QRect(control->mapTo(&panel,QPoint()),control->size()).adjusted(6,6,-6,-6);
                QVERIFY2(panel.rect().contains(area),id);
                const QRect physical(qRound(area.x()*scale),qRound(area.y()*scale),qRound(area.width()*scale),qRound(area.height()*scale));
                QVERIFY2(lightPixels(pixels.copy(physical))>5,qPrintable(QString("%1 did not paint in %2D camera mode").arg(id).arg(mode?3:2)));
            }
        }
    }
};
QTEST_MAIN(CameraPanelTests)
#include "camera_panel_tests.moc"
