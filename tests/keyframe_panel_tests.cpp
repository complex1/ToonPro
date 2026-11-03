#include "timeline_test_geometry.h"
#include "canvas.h"
#include "document.h"
#include "drawing_tool_controller.h"
#include "keyframe_panel.h"
#include "object_tools.h"
#include "property_animation.h"
#include "property_row.h"
#include "ui_metrics.h"
#include "timeline_controller.h"
#include "timeline_panel.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QAbstractScrollArea>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QSlider>
#include <QStyle>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>
using namespace toon;
namespace {
Project fixture(){auto project=Project::create();project.width=160;project.height=120;project.shots[0].durationFrames=30;return project;}
QToolButton *button(QWidget &panel,const QString &id){return panel.findChild<QToolButton *>(id);}
ValueSlider *value(QWidget &panel,const QString &id){return panel.findChild<ValueSlider *>(id);}
QDoubleSpinBox *number(QWidget &panel,const QString &id){return value(panel,id)->findChild<QDoubleSpinBox *>();}
void settle(){QCoreApplication::processEvents();for(int i=0;i<4;++i)QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);}
void writeNumber(QWidget &panel,const QString &id,const QString &text){auto *spin=number(panel,id);spin->setFocus();spin->selectAll();QTest::keyClicks(spin,text);}
QVector<int> frames(const Canvas &canvas,const QString &id,KeyframeProperty property){QVector<int> result;if(const auto *channel=propertyChannel(canvas.shot(),id,property))for(const auto &key:channel->keys)result.append(key.frame);return result;}
}
class KeyframePanelTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void compactRowsKeepDraftsAndDirectChannelSelection(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);KeyframePanel panel(&controller);
        panel.resize(240,700);panel.show();settle();
        for(const auto &key:QStringList{"positionX","positionY","scaleX","scaleY","rotation","opacity"}){
            auto *row=panel.findChild<PropertyRow *>("keyframeRow_"+key);QVERIFY(row);
            auto *field=value(panel,"keyframeValue_"+key);auto *select=button(panel,"keyframeSelect_"+key);auto *enable=button(panel,"keyframeEnable_"+key);
            QCOMPARE(enable->iconSize(),QSize(16,16));QCOMPARE(enable->size(),QSize(28,28));
            QVERIFY2(row->height()<=qMax(field->height(),select->height())+2,qPrintable(key));
            QVERIFY(field->findChild<QSlider *>()->width()>=76);
            select->click();QVERIFY(select->isChecked());
            const QRect editor(field->mapTo(row,QPoint()),field->size());
            const QRect label(select->mapTo(row,QPoint()),select->size());QVERIFY(!editor.intersects(label));
        }
        button(panel,"keyframeEnable_positionX")->click();const int count=canvas.document()->history()->count();
        writeNumber(panel,"keyframeValue_positionX","75");auto *spin=number(panel,"keyframeValue_positionX");
        QFont larger=panel.font();larger.setPixelSize(panel.fontMetrics().height()+6);panel.setFont(larger);
        for(int width:{320,190,240}){
            panel.resize(width,700);settle();QCOMPARE(panel.width(),width);
            QCOMPARE(spin->findChild<QLineEdit *>()->text(),QString("75"));QVERIFY(spin->hasFocus());
            QVERIFY(controller.hasPendingEdits());QCOMPARE(canvas.document()->history()->count(),count);
            auto *field=value(panel,"keyframeValue_positionX");QVERIFY(field->findChild<QSlider *>()->width()>=76);
            const QRect bounds(field->mapTo(&panel,QPoint()),field->size());QVERIFY(bounds.left()>=0&&bounds.right()<panel.width());
        }
        controller.cancelEditing();QCOMPARE(canvas.document()->history()->count(),count);
        QCOMPARE(evaluateProperty(canvas.shot(),controller.selectedPropertyLayerId(),KeyframeProperty::PositionX,0),0.);
    }
    void propertiesEnableAndAutoKeyWithoutChangingSource(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);FramePropertiesPanel panel(&controller);panel.resize(240,680);panel.show();settle();
        const auto id=controller.selectedPropertyLayerId();const auto source=canvas.shot().layers[canvas.document()->activeLayer()].tiles;
        QCOMPARE(panel.findChild<QTabBar *>("framePropertiesTabs")->currentIndex(),1);QVERIFY(!value(panel,"keyframeValue_positionX")->isEnabled());
        button(panel,"keyframeEnable_positionX")->click();QVERIFY(value(panel,"keyframeValue_positionX")->isEnabled());QCOMPARE(frames(canvas,id,KeyframeProperty::PositionX),QVector<int>{0});
        QVERIFY(controller.setFrame(10));const int before=canvas.document()->history()->count();number(panel,"keyframeValue_positionX")->setValue(80);
        QCOMPARE(canvas.document()->history()->count(),before+1);QCOMPARE(frames(canvas,id,KeyframeProperty::PositionX),(QVector<int>{0,10}));
        QCOMPARE(evaluateProperty(canvas.shot(),id,KeyframeProperty::PositionX,5),40.0);QCOMPARE(canvas.shot().layers[canvas.document()->activeLayer()].tiles,source);
        button(panel,"keyframeEnable_opacity")->click();number(panel,"keyframeValue_opacity")->setValue(25);
        QCOMPARE(evaluateProperty(canvas.shot(),id,KeyframeProperty::Opacity,10),.25);QVERIFY(!value(panel,"keyframeValue_positionY")->isEnabled());
        button(panel,"keyframeEnable_positionX")->click();QVERIFY(!propertyChannel(canvas.shot(),id,KeyframeProperty::PositionX));canvas.document()->history()->undo();QVERIFY(propertyChannel(canvas.shot(),id,KeyframeProperty::PositionX));
    }
    void scalarDraftsLiveDragSaveAndCancelAreOneUndo(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);KeyframePanel panel(&controller);panel.resize(240,660);panel.show();settle();
        const auto id=controller.selectedPropertyLayerId();button(panel,"keyframeEnable_positionX")->click();QVERIFY(controller.setFrame(8));auto *history=canvas.document()->history();const int initial=history->count();
        auto *field=value(panel,"keyframeValue_positionX");auto *slider=field->findChild<QSlider *>();slider->setSliderDown(true);slider->setValue(2000);slider->setValue(4000);
        QCOMPARE(evaluateProperty(canvas.shot(),id,KeyframeProperty::PositionX,8),40.0);QCOMPARE(history->count(),initial);QVERIFY(controller.hasPendingEdits());
        slider->setSliderDown(false);QCOMPARE(history->count(),initial+1);QVERIFY(!controller.hasPendingEdits());
        slider->setSliderDown(true);slider->setValue(6000);field->cancelEditing();QCOMPARE(evaluateProperty(canvas.shot(),id,KeyframeProperty::PositionX,8),40.0);QCOMPARE(history->count(),initial+1);
        writeNumber(panel,"keyframeValue_positionX","75");QVERIFY(controller.hasPendingEdits());QCOMPARE(history->count(),initial+1);
        for(int i=0;i<3;++i){QVERIFY(QMetaObject::invokeMethod(&controller,"changed"));QCOMPARE(number(panel,"keyframeValue_positionX")->findChild<QLineEdit *>()->text(),QString("75"));}
        controller.cancelEditing();QVERIFY(!controller.hasPendingEdits());QCOMPARE(evaluateProperty(canvas.shot(),id,KeyframeProperty::PositionX,8),40.0);QCOMPARE(history->count(),initial+1);
        writeNumber(panel,"keyframeValue_positionX","90");controller.finishEditing();QCOMPARE(evaluateProperty(canvas.shot(),id,KeyframeProperty::PositionX,8),90.0);QCOMPARE(history->count(),initial+2);
        QTemporaryDir directory;QString error;QVERIFY2(saveProject(canvas.project(),directory.filePath("keys.toonproj"),&error),qPrintable(error));const auto loaded=loadProject(directory.filePath("keys.toonproj"),&error);QVERIFY2(loaded.has_value(),qPrintable(error));
        QCOMPARE(evaluateProperty(loaded->shots.first(),id,KeyframeProperty::PositionX,8),90.0);number(panel,"keyframeValue_positionX")->clearFocus();settle();QCOMPARE(history->count(),initial+2);
    }
    void easingNavigationAndTimingRejectOccupiedFrames(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);KeyframePanel panel(&controller);panel.resize(240,700);panel.show();settle();
        const auto id=controller.selectedPropertyLayerId();button(panel,"keyframeEnable_rotation")->click();QVERIFY(controller.setFrame(10));number(panel,"keyframeValue_rotation")->setValue(90);
        button(panel,"keyframePrevious")->click();QCOMPARE(controller.currentFrame(),0);auto *easing=panel.findChild<QTabBar *>("keyframeInterpolation");easing->setCurrentIndex(int(KeyframeInterpolation::Hold));
        QCOMPARE(evaluateProperty(canvas.shot(),id,KeyframeProperty::Rotation,5),0.0);button(panel,"keyframeNext")->click();QCOMPARE(controller.currentFrame(),10);
        const int before=canvas.document()->history()->count();number(panel,"keyframeTime")->setValue(1);QCOMPARE(frames(canvas,id,KeyframeProperty::Rotation),(QVector<int>{0,10}));QCOMPARE(number(panel,"keyframeTime")->value(),11.0);QCOMPARE(canvas.document()->history()->count(),before);
        number(panel,"keyframeTime")->setValue(15);QCOMPARE(controller.currentFrame(),14);QCOMPARE(frames(canvas,id,KeyframeProperty::Rotation),(QVector<int>{0,14}));QCOMPARE(canvas.document()->history()->count(),before+1);
        QVERIFY(controller.setFrame(6));QVERIFY(button(panel,"keyframeAdd")->isEnabled());button(panel,"keyframeAdd")->click();QCOMPARE(frames(canvas,id,KeyframeProperty::Rotation),(QVector<int>{0,6,14}));
        button(panel,"keyframeRemove")->click();QCOMPARE(frames(canvas,id,KeyframeProperty::Rotation),(QVector<int>{0,14}));
    }
    void selectedChildLaneDragRetimesOnceAndCollisionDoesNotOverwrite(){
        Canvas canvas;canvas.setProject(fixture());auto *document=canvas.document();document->addGroup();const auto parent=canvas.shot().layers[document->activeLayer()].id;
        document->addLayer();document->setLayerParent(parent);TimelineController controller(&canvas);const auto id=controller.selectedPropertyLayerId();QVERIFY(id!=parent);
        QVERIFY(controller.setPropertyEnabled(KeyframeProperty::PositionX,true));QVERIFY(controller.setFrame(10));QVERIFY(document->setKeyframeValue(id,KeyframeProperty::PositionX,60));
        TimelinePanel panel(&controller);panel.resize(700,320);panel.show();settle();auto *grid=panel.findChild<QAbstractScrollArea *>("timelineGrid");QVERIFY(grid);grid->horizontalScrollBar()->setValue(0);grid->verticalScrollBar()->setValue(0);
        const auto rowIds=grid->property("timelineRowIds").toStringList();const int propertyRow=rowIds.indexOf("property:"+id+":"+keyframePropertyKey(KeyframeProperty::PositionX));
        QVERIFY(rowIds.contains(parent));QVERIFY(rowIds.contains(id));QVERIFY(rowIds.indexOf(parent)<rowIds.indexOf(id));QVERIFY(propertyRow>rowIds.indexOf(id));
        const int label=timeline_test::labelWidth(grid),y=timeline_test::rowCenter(grid,propertyRow);const QPoint from(label+10*20+10,y),to(label+14*20+10,y);
        const int before=document->history()->count();QTest::mousePress(grid->viewport(),Qt::LeftButton,Qt::NoModifier,from);
        QMouseEvent movement(QEvent::MouseMove,to,grid->viewport()->mapToGlobal(to),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(grid->viewport(),&movement);
        QCOMPARE(document->history()->count(),before);QTest::mouseRelease(grid->viewport(),Qt::LeftButton,Qt::NoModifier,to);
        QCOMPARE(frames(canvas,id,KeyframeProperty::PositionX),(QVector<int>{0,14}));QCOMPARE(controller.currentFrame(),14);QCOMPARE(document->history()->count(),before+1);QCOMPARE(controller.selectedPropertyLayerId(),id);
        const QPoint collision(label+10,y);QTest::mousePress(grid->viewport(),Qt::LeftButton,Qt::NoModifier,to);QMouseEvent occupied(QEvent::MouseMove,collision,grid->viewport()->mapToGlobal(collision),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(grid->viewport(),&occupied);QTest::mouseRelease(grid->viewport(),Qt::LeftButton,Qt::NoModifier,collision);
        QCOMPARE(frames(canvas,id,KeyframeProperty::PositionX),(QVector<int>{0,14}));QCOMPARE(document->history()->count(),before+1);
        QTest::keyClick(grid,Qt::Key_Delete);QCOMPARE(frames(canvas,id,KeyframeProperty::PositionX),(QVector<int>{0}));document->history()->undo();QCOMPARE(frames(canvas,id,KeyframeProperty::PositionX),(QVector<int>{0,14}));
    }
    void pathMorphSelectionTargetsExactPath(){
        Canvas canvas;canvas.setProject(fixture());QVERIFY(ObjectTools::addLayer(canvas.document(),LayerKind::Vector));
        const auto first=ObjectTools::addPath(canvas.document(),{BezierNode::corner({10,10}),BezierNode::corner({50,10})},false,Qt::black,3);
        const auto second=ObjectTools::addPath(canvas.document(),{BezierNode::corner({10,50}),BezierNode::corner({50,50})},false,Qt::black,3);QVERIFY(!first.isEmpty());QVERIFY(!second.isEmpty());
        TimelineController controller(&canvas);KeyframePanel panel(&controller);panel.resize(240,700);panel.show();settle();const auto id=controller.selectedPropertyLayerId();
        QVERIFY(!button(panel,"keyframeEnablePath")->isEnabled());QVERIFY(canvas.drawingTools()->selectAnimatedPath(first));QVERIFY(button(panel,"keyframeEnablePath")->isEnabled());button(panel,"keyframeEnablePath")->click();
        QVERIFY(pathAnimation(canvas.shot(),id,first));QVERIFY(!pathAnimation(canvas.shot(),id,second));QVERIFY(controller.setFrame(10));button(panel,"keyframeAdd")->click();QCOMPARE(pathAnimation(canvas.shot(),id,first)->keys.size(),2);
        emit controller.pathChannelRequested(second);QCOMPARE(canvas.drawingTools()->selectedObjectId(),second);QVERIFY(!button(panel,"keyframeEnablePath")->isChecked());button(panel,"keyframeEnablePath")->click();QVERIFY(pathAnimation(canvas.shot(),id,second));
        emit controller.pathChannelRequested(first);QVERIFY(button(panel,"keyframeEnablePath")->isChecked());button(panel,"keyframeRemove")->click();QCOMPARE(pathAnimation(canvas.shot(),id,first)->keys.size(),1);QCOMPARE(pathAnimation(canvas.shot(),id,second)->keys.size(),2);
    }
    void drawingDraftOwnershipAndNarrowAccessibleControls(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);QVERIFY(controller.addTrack(true,DrawingTemplate::Simple,"Character"));FramePropertiesPanel panel(&controller);panel.resize(240,620);panel.show();settle();
        auto *tabs=panel.findChild<QTabBar *>("framePropertiesTabs");QCOMPARE(tabs->currentIndex(),0);writeNumber(panel,"frameExposureHold","4");QVERIFY(controller.hasPendingEdits());tabs->setCurrentIndex(1);QCOMPARE(canvas.shot().animationTracks.first().exposures.first().durationFrames,4);QVERIFY(!controller.hasPendingEdits());
        button(panel,"keyframeEnable_positionX")->click();writeNumber(panel,"keyframeValue_positionX","12");QVERIFY(controller.hasPendingEdits());QVERIFY(QMetaObject::invokeMethod(&controller,"changed"));QVERIFY(controller.hasPendingEdits());controller.cancelEditing();QVERIFY(!controller.hasPendingEdits());
        settle();QCOMPARE(panel.width(),240);for(auto *spin:panel.findChildren<QDoubleSpinBox *>()){QCOMPARE(spin->width(),48);QVERIFY(spin->suffix().isEmpty());}
        for(auto *control:panel.findChildren<QToolButton *>()){
            QVERIFY2(!control->toolTip().isEmpty(),qPrintable(QString("%1 / %2").arg(control->objectName(),control->parentWidget()->objectName())));QVERIFY(!control->accessibleName().isEmpty());QCOMPARE(control->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,control),600);
            if(control->isVisibleTo(&panel)){if(control->objectName()=="ScrollLeftButton"||control->objectName()=="ScrollRightButton")QVERIFY(control->width()>=28);const QRect bounds(control->mapTo(&panel,QPoint()),control->size());QVERIFY2(bounds.left()>=0&&bounds.right()<panel.width(),qPrintable(QString("%1 (%2): x=%3, right=%4, panel=%5").arg(control->objectName(),control->accessibleName()).arg(bounds.left()).arg(bounds.right()).arg(panel.width())));}
        }
        canvas.document()->setLayersLocked({controller.selectedPropertyLayerId()},true);QVERIFY(!button(panel,"keyframeEnable_positionX")->isEnabled());QVERIFY(!value(panel,"keyframeValue_positionX")->isEnabled());
    }
    void addKeyframeTrackCreatesDrawableSource(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);TimelinePanel panel(&controller);
        QTimer::singleShot(0,&panel,[]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());QVERIFY(dialog);auto *tabs=dialog->findChild<QTabBar *>("animationTrackType");QCOMPARE(tabs->count(),3);tabs->setCurrentIndex(2);dialog->findChild<QLineEdit *>("animationTrackName")->setText("Moving artwork");dialog->findChild<QPushButton *>("confirmAnimationTrack")->click();});
        button(panel,"timelineAddTrack")->click();QVERIFY(canvas.shot().animationTracks.isEmpty());QVERIFY(!controller.selectedPropertyLayerId().isEmpty());QVERIFY(!canvas.shot().layers[canvas.document()->activeLayer()].group);
    }
    void renderPreview(){
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");if(output.isEmpty())QSKIP("Opt-in keyframe previews.");
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);QVERIFY(controller.addTrack(false,DrawingTemplate::Simple,"Moving character"));
        const auto id=controller.selectedPropertyLayerId();for(const auto property:{KeyframeProperty::PositionX,KeyframeProperty::Rotation,KeyframeProperty::Opacity})QVERIFY(controller.setPropertyEnabled(property,true));
        QVERIFY(controller.setFrame(18));QVERIFY(canvas.document()->setKeyframeValue(id,KeyframeProperty::PositionX,80));QVERIFY(canvas.document()->setKeyframeValue(id,KeyframeProperty::Rotation,45));QVERIFY(canvas.document()->setKeyframeValue(id,KeyframeProperty::Opacity,.5));QVERIFY(controller.setFrame(9));
        TimelinePanel timeline(&controller);timeline.resize(800,300);timeline.show();FramePropertiesPanel properties(&controller);properties.resize(280,700);properties.show();settle();QDir().mkpath(output);
        QVERIFY(timeline.grab().save(output+"/keyframe-timeline.png"));QVERIFY(properties.grab().save(output+"/keyframe-properties-280.png"));properties.resize(240,700);timeline.resize(240,300);settle();
        QVERIFY(properties.grab().save(output+"/keyframe-properties-240.png"));QVERIFY(timeline.grab().save(output+"/keyframe-timeline-240.png"));
    }
};
QTEST_MAIN(KeyframePanelTests)
#include "keyframe_panel_tests.moc"
