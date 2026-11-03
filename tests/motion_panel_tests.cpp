#include "canvas.h"
#include "document.h"
#include "keyframe_panel.h"
#include "keyframe_transform_tool.h"
#include "motion_controls.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QScrollArea>
#include <QSlider>
#include <QStyle>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>
using namespace toon;
namespace {
Project fixture(){auto p=Project::create();p.width=160;p.height=120;p.shots[0].durationFrames=30;return p;}
void settle(){QCoreApplication::processEvents();for(int i=0;i<4;++i)QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);}
QToolButton *button(QWidget &panel,const QString &id){return panel.findChild<QToolButton *>(id);}
ValueSlider *field(QWidget &panel,const QString &id){return panel.findChild<ValueSlider *>(id);}
QDoubleSpinBox *number(QWidget &panel,const QString &id){return field(panel,id)->findChild<QDoubleSpinBox *>();}
void type(QWidget &panel,const QString &id,const QString &text){auto *spin=number(panel,id);spin->setFocus();spin->selectAll();QTest::keyClicks(spin,text);}
void show(KeyframePanel &panel){panel.resize(240,760);panel.show();settle();}
}
class MotionPanelTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void modesAreExclusiveAndRepeatClickExits(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);KeyframePanel panel(&controller);show(panel);
        QVERIFY(!panel.findChild<QWidget *>("keyframePivotControls")->isVisibleTo(&panel));
        button(panel,"keyframeTransform")->click();QVERIFY(controller.animatedTransformEditing());QVERIFY(button(panel,"keyframeTransform")->isChecked());
        button(panel,"keyframeMotionPath")->click();QVERIFY(controller.motionPathEditing());QVERIFY(!controller.animatedTransformEditing());QVERIFY(!button(panel,"keyframeTransform")->isChecked());
        button(panel,"keyframePivot")->click();QVERIFY(controller.pivotEditing());QVERIFY(!controller.motionPathEditing());QVERIFY(panel.findChild<QWidget *>("keyframePivotControls")->isVisibleTo(&panel));
        QCOMPARE(number(panel,"keyframePivotX")->value(),controller.animationPivot().x());QCOMPARE(number(panel,"keyframePivotY")->value(),controller.animationPivot().y());
        button(panel,"keyframePivot")->click();QVERIFY(!controller.pivotEditing());QVERIFY(!canvas.keyframeTransformTool()->enabled());QVERIFY(!panel.findChild<QWidget *>("keyframePivotControls")->isVisibleTo(&panel));
    }
    void pivotTextSurvivesRefreshSavesAndUndoesOnce(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);const auto id=controller.selectedPropertyLayerId();QVERIFY(controller.setFrame(10));QVERIFY(canvas.document()->setKeyframeValue(id,KeyframeProperty::Rotation,60));
        KeyframePanel panel(&controller);show(panel);button(panel,"keyframePivot")->click();const auto beforePivot=controller.animationPivot();const auto beforePoint=worldLayerTransform(canvas.shot(),id,10).map(QPointF(15,20));const int before=canvas.document()->history()->count();
        type(panel,"keyframePivotX","45.5");QVERIFY(controller.hasPendingEdits());QVERIFY(canvas.document()->keyframeEditActive());
        for(int i=0;i<4;++i){emit controller.changed();QCOMPARE(number(panel,"keyframePivotX")->findChild<QLineEdit *>()->text(),QString("45.5"));}
        QCOMPARE(canvas.document()->history()->count(),before);controller.finishEditing();QVERIFY(!controller.hasPendingEdits());QVERIFY(!canvas.document()->keyframeEditActive());QCOMPARE(controller.animationPivot(),QPointF(45.5,beforePivot.y()));QCOMPARE(canvas.document()->history()->count(),before+1);
        QVERIFY(QLineF(worldLayerTransform(canvas.shot(),id,10).map(QPointF(15,20)),beforePoint).length()<1e-8);
        QTemporaryDir directory;QString error;QVERIFY2(saveProject(canvas.project(),directory.filePath("pivot.toonproj"),&error),qPrintable(error));const auto loaded=loadProject(directory.filePath("pivot.toonproj"),&error);QVERIFY2(loaded.has_value(),qPrintable(error));QCOMPARE(effectiveAnimationPivot(loaded->shots.first(),id,QSizeF(loaded->width,loaded->height)),controller.animationPivot());
        canvas.document()->history()->undo();QCOMPARE(controller.animationPivot(),beforePivot);QCOMPARE(number(panel,"keyframePivotX")->value(),beforePivot.x());
    }
    void pivotSliderCancelAndModeSwitchResolveDrafts(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);KeyframePanel panel(&controller);show(panel);button(panel,"keyframePivot")->click();const auto beforePivot=controller.animationPivot();const int before=canvas.document()->history()->count();
        auto *value=field(panel,"keyframePivotX");auto *slider=value->findChild<QSlider *>();slider->setSliderDown(true);slider->setValue(5600);slider->setValue(6200);
        QCOMPARE(controller.animationPivot().x(),62.0);QCOMPARE(canvas.document()->history()->count(),before);QVERIFY(controller.hasPendingEdits());value->cancelEditing();QCOMPARE(controller.animationPivot(),beforePivot);QCOMPARE(canvas.document()->history()->count(),before);QVERIFY(!controller.hasPendingEdits());
        type(panel,"keyframePivotY","27");controller.cancelEditing();QCOMPARE(controller.animationPivot(),beforePivot);QCOMPARE(canvas.document()->history()->count(),before);
        type(panel,"keyframePivotY","35");button(panel,"keyframeMotionPath")->click();QVERIFY(controller.motionPathEditing());QCOMPARE(controller.animationPivot().y(),35.0);QCOMPARE(canvas.document()->history()->count(),before+1);QVERIFY(!controller.hasPendingEdits());
        button(panel,"keyframePivot")->click();type(panel,"keyframePivotX","17");QTest::keyClick(number(panel,"keyframePivotX"),Qt::Key_Escape);QCOMPARE(controller.animationPivot().x(),beforePivot.x());QCOMPARE(canvas.document()->history()->count(),before+1);QVERIFY(!controller.hasPendingEdits());
    }
    void resetUsesSourceCenterAndKeepsCurrentPose(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);const auto id=controller.selectedPropertyLayerId();QVERIFY(controller.setFrame(8));QVERIFY(canvas.document()->setKeyframeValue(id,KeyframeProperty::Rotation,90));QVERIFY(canvas.document()->setKeyframeValue(id,KeyframeProperty::ScaleX,1.5));
        KeyframePanel panel(&controller);show(panel);button(panel,"keyframePivot")->click();number(panel,"keyframePivotX")->setValue(30);number(panel,"keyframePivotY")->setValue(20);const auto changed=controller.animationPivot();
        const auto point=worldLayerTransform(canvas.shot(),id,8).map(QPointF(12,18));const int before=canvas.document()->history()->count();button(panel,"keyframePivotReset")->click();
        QCOMPARE(controller.animationPivot(),defaultAnimationPivot(canvas.shot(),id,QSizeF(160,120)));QVERIFY(QLineF(worldLayerTransform(canvas.shot(),id,8).map(QPointF(12,18)),point).length()<1e-8);QCOMPARE(canvas.document()->history()->count(),before+1);
        canvas.document()->history()->undo();QCOMPARE(controller.animationPivot(),changed);
    }
    void sourceLocksAndPlaybackGateMotionControls(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);KeyframePanel panel(&controller);show(panel);const auto id=controller.selectedPropertyLayerId();
        button(panel,"keyframePivot")->click();button(panel,"keyframeSourceEditing")->click();QVERIFY(controller.sourceEditing());QVERIFY(!controller.pivotEditing());QVERIFY(!panel.findChild<QWidget *>("keyframePivotControls")->isVisibleTo(&panel));
        button(panel,"keyframeMotionPath")->click();QVERIFY(controller.motionPathEditing());QVERIFY(!controller.sourceEditing());button(panel,"keyframePivot")->click();
        canvas.document()->setLayerPositionLocked(true);for(const auto &name:QStringList{"keyframeTransform","keyframeMotionPath","keyframePivot"})QVERIFY(!button(panel,name)->isEnabled());QVERIFY(!field(panel,"keyframePivotX")->isEnabled());
        canvas.document()->setLayerPositionLocked(false);QVERIFY(button(panel,"keyframePivot")->isEnabled());canvas.document()->setLayersLocked({id},true);QVERIFY(!button(panel,"keyframeMotionPath")->isEnabled());canvas.document()->setLayersLocked({id},false);
        controller.setPlaying(true);QVERIFY(controller.playing());QVERIFY(!controller.pivotEditing());for(const auto &name:QStringList{"keyframeTransform","keyframeMotionPath","keyframePivot","keyframeSourceEditing"})QVERIFY(!button(panel,name)->isEnabled());
        controller.setPlaying(false);QVERIFY(button(panel,"keyframePivot")->isEnabled());
    }
    void narrowControlsStayStableAccessibleAndUnitless(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);KeyframePanel panel(&controller);show(panel);button(panel,"keyframePivot")->click();settle();QCOMPARE(panel.width(),240);
        for(auto *spin:panel.findChildren<QDoubleSpinBox *>()){QCOMPARE(spin->width(),48);QVERIFY(spin->suffix().isEmpty());}
        for(const auto &name:QStringList{"keyframeTransform","keyframeMotionPath","keyframePivot","keyframePivotReset"}){auto *control=button(panel,name);QVERIFY(!control->toolTip().isEmpty());QVERIFY(!control->accessibleName().isEmpty());QCOMPARE(control->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,control),600);QVERIFY(QRect(control->mapTo(&panel,QPoint()),control->size()).right()<panel.width());}
        const auto x=field(panel,"keyframePivotX")->mapTo(&panel,QPoint()),y=field(panel,"keyframePivotY")->mapTo(&panel,QPoint());const auto status=panel.findChild<QLabel *>("keyframeStatus");const int height=status->height();
        for(int value:{10,20,100,1000}){number(panel,"keyframePivotX")->setValue(value);settle();QCOMPARE(field(panel,"keyframePivotX")->mapTo(&panel,QPoint()),x);QCOMPARE(field(panel,"keyframePivotY")->mapTo(&panel,QPoint()),y);QCOMPARE(status->height(),height);}
    }
    void renderPreview(){
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");if(output.isEmpty())QSKIP("Opt-in motion panel previews.");
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);const auto id=controller.selectedPropertyLayerId();QVERIFY(controller.setFrame(12));QVERIFY(canvas.document()->setKeyframeValue(id,KeyframeProperty::PositionX,60));QVERIFY(canvas.document()->setKeyframeValue(id,KeyframeProperty::Rotation,30));
        KeyframePanel panel(&controller);show(panel);button(panel,"keyframePivot")->click();settle();QDir().mkpath(output);QVERIFY(panel.grab().save(output+"/motion-pivot-240.png"));panel.resize(320,760);settle();QVERIFY(panel.grab().save(output+"/motion-pivot-320.png"));button(panel,"keyframeMotionPath")->click();settle();QVERIFY(panel.grab().save(output+"/motion-path-320.png"));
    }
};
QTEST_MAIN(MotionPanelTests)
#include "motion_panel_tests.moc"
