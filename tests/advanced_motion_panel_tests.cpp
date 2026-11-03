#include "canvas.h"
#include "document.h"
#include "drawing_tool_controller.h"
#include "easing_graph_widget.h"
#include "keyframe_panel.h"
#include "keyframe_transform_tool.h"
#include "object_tools.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>
using namespace toon;
namespace {
Project fixture() { auto p=Project::create();p.width=160;p.height=120;p.shots[0].durationFrames=30;return p; }
void settle(){QCoreApplication::processEvents();for(int i=0;i<4;++i)QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);}
QToolButton *button(QWidget &panel,const QString &id){return panel.findChild<QToolButton *>(id);}
EasingGraphWidget *graph(QWidget &panel){return panel.findChild<EasingGraphWidget *>("keyframeEasingGraph");}
void show(KeyframePanel &panel){panel.resize(240,760);panel.show();settle();}
void pointer(QWidget &widget,QEvent::Type type,QPointF point){QMouseEvent event(type,point,widget.mapToGlobal(point),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&widget,&event);}
void seedScalar(Canvas &canvas,TimelineController &controller,KeyframeProperty property=KeyframeProperty::Opacity){
    const auto id=controller.selectedPropertyLayerId();controller.setFrame(0);canvas.document()->setKeyframeValue(id,property,property==KeyframeProperty::Opacity?.2:0);
    controller.setFrame(10);canvas.document()->setKeyframeValue(id,property,property==KeyframeProperty::Opacity?1:80);controller.setFrame(0);
}
void seedPosition(Canvas &canvas,TimelineController &controller){
    const auto id=controller.selectedPropertyLayerId();controller.setFrame(0);canvas.document()->setKeyframeValue(id,KeyframeProperty::PositionX,0);canvas.document()->setKeyframeValue(id,KeyframeProperty::PositionY,0);
    controller.setFrame(10);canvas.document()->setKeyframeValue(id,KeyframeProperty::PositionX,80);canvas.document()->setKeyframeValue(id,KeyframeProperty::PositionY,40);controller.setFrame(0);
}
void beginCurveDrag(KeyframePanel &panel){auto *widget=graph(panel);const auto from=widget->handlePosition(0);pointer(*widget,QEvent::MouseButtonPress,from);pointer(*widget,QEvent::MouseMove,from+QPointF(-20,-30));}
const ScalarKeyframe &firstKey(Canvas &canvas,const QString &id,KeyframeProperty property=KeyframeProperty::Opacity){return propertyChannel(canvas.shot(),id,property)->keys.first();}
}
class AdvancedMotionPanelTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void customGraphPreviewsCommitsAndUndoesOnce(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);seedScalar(canvas,controller);KeyframePanel panel(&controller);show(panel);button(panel,"keyframeSelect_opacity")->click();
        const auto id=controller.selectedPropertyLayerId();const int count=canvas.document()->history()->count();const auto source=canvas.shot().layers[canvas.document()->activeLayer()].tiles;
        QVERIFY(graph(panel)->isEnabled());beginCurveDrag(panel);QVERIFY(controller.hasPendingEdits());QVERIFY(canvas.document()->keyframeEditActive());QVERIFY(firstKey(canvas,id).easing);QCOMPARE(canvas.document()->history()->count(),count);
        QVERIFY(evaluateProperty(canvas.shot(),id,KeyframeProperty::Opacity,5)>.6);const auto first=graph(panel)->firstControl();emit controller.changed();QCOMPARE(graph(panel)->firstControl(),first);
        pointer(*graph(panel),QEvent::MouseButtonRelease,graph(panel)->handlePosition(0));QCOMPARE(canvas.document()->history()->count(),count+1);QVERIFY(!controller.hasPendingEdits());QCOMPARE(canvas.shot().layers[canvas.document()->activeLayer()].tiles,source);
        QCOMPARE(panel.findChild<QTabBar *>("keyframeInterpolation")->currentIndex(),5);canvas.document()->history()->undo();QVERIFY(!firstKey(canvas,id).easing);QCOMPARE(evaluateProperty(canvas.shot(),id,KeyframeProperty::Opacity,5),.6);
    }
    void escapeCancelAndSaveResolveGraphGesture(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);seedScalar(canvas,controller);KeyframePanel panel(&controller);show(panel);button(panel,"keyframeSelect_opacity")->click();
        const auto id=controller.selectedPropertyLayerId();const auto before=canvas.project().toJson();const int count=canvas.document()->history()->count();beginCurveDrag(panel);QTest::keyClick(graph(panel),Qt::Key_Escape);
        QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),count);QVERIFY(!controller.hasPendingEdits());
        beginCurveDrag(panel);controller.cancelEditing();QCOMPARE(canvas.project().toJson(),before);QVERIFY(!graph(panel)->editing());
        beginCurveDrag(panel);controller.finishEditing();QCOMPARE(canvas.document()->history()->count(),count+1);QVERIFY(!graph(panel)->editing());QVERIFY(!controller.hasPendingEdits());
        QTemporaryDir directory;QString error;QVERIFY2(saveProject(canvas.project(),directory.filePath("curve.toonproj"),&error),qPrintable(error));auto loaded=loadProject(directory.filePath("curve.toonproj"),&error);QVERIFY2(loaded.has_value(),qPrintable(error));QCOMPARE(propertyChannel(loaded->shots.first(),id,KeyframeProperty::Opacity)->keys.first().easing,firstKey(canvas,id).easing);
    }
    void clickAndReturnToOriginDoNotCreateHistory(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);seedScalar(canvas,controller);KeyframePanel panel(&controller);show(panel);button(panel,"keyframeSelect_opacity")->click();
        const auto before=canvas.project().toJson();const int count=canvas.document()->history()->count();auto *widget=graph(panel);const auto from=widget->handlePosition(0)+QPointF(3,2);
        pointer(*widget,QEvent::MouseButtonPress,from);pointer(*widget,QEvent::MouseButtonRelease,from);QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),count);
        pointer(*widget,QEvent::MouseButtonPress,from);pointer(*widget,QEvent::MouseMove,from+QPointF(12,-15));pointer(*widget,QEvent::MouseMove,from);pointer(*widget,QEvent::MouseButtonRelease,from);
        QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),count);QVERIFY(!controller.hasPendingEdits());
    }
    void toolSwitchResolvesGraphWithoutFocusChange(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);seedScalar(canvas,controller);KeyframePanel panel(&controller);show(panel);button(panel,"keyframeSelect_opacity")->click();
        const int count=canvas.document()->history()->count();beginCurveDrag(panel);QVERIFY(canvas.document()->keyframeEditActive());
        canvas.setTool(Canvas::Tool::Eraser);QVERIFY(!canvas.document()->keyframeEditActive());QVERIFY(!controller.hasPendingEdits());QVERIFY(!graph(panel)->editing());QCOMPARE(canvas.document()->history()->count(),count+1);
    }
    void presetsResetAndUneditableSegments(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);seedScalar(canvas,controller);KeyframePanel panel(&controller);show(panel);button(panel,"keyframeSelect_opacity")->click();const auto id=controller.selectedPropertyLayerId();
        auto *tabs=panel.findChild<QTabBar *>("keyframeInterpolation");tabs->setCurrentIndex(int(KeyframeInterpolation::EaseIn));QVERIFY(!firstKey(canvas,id).easing);
        beginCurveDrag(panel);graph(panel)->finishEditing();QVERIFY(firstKey(canvas,id).easing);button(panel,"keyframeEasingReset")->click();QVERIFY(!firstKey(canvas,id).easing);QCOMPARE(firstKey(canvas,id).interpolation,KeyframeInterpolation::EaseIn);
        tabs->setCurrentIndex(5);QVERIFY(firstKey(canvas,id).easing);tabs->setCurrentIndex(int(KeyframeInterpolation::EaseOut));QVERIFY(!firstKey(canvas,id).easing);
        QVERIFY(controller.setFrame(5));QVERIFY(!graph(panel)->isEnabled());QCOMPARE(panel.findChild<QLabel *>("keyframeEasingContext")->text(),QString("Select a key for easing"));
        QVERIFY(controller.setFrame(10));QVERIFY(!graph(panel)->isEnabled());QCOMPARE(panel.findChild<QLabel *>("keyframeEasingContext")->text(),QString("No following key"));
        QVERIFY(controller.setFrame(0));canvas.document()->setLayersLocked({id},true);QVERIFY(!graph(panel)->isEnabled());canvas.document()->setLayersLocked({id},false);QVERIFY(graph(panel)->isEnabled());
        controller.setPlaying(true);QVERIFY(!graph(panel)->isEnabled());controller.setPlaying(false);
    }
    void curveAndFollowPathHaveDirectControls(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);seedPosition(canvas,controller);KeyframePanel panel(&controller);show(panel);const auto id=controller.selectedPropertyLayerId();
        QVERIFY(!panel.findChild<QWidget *>("keyframeMotionControls")->isVisibleTo(&panel));button(panel,"keyframeMotionPath")->click();QVERIFY(panel.findChild<QWidget *>("keyframeMotionControls")->isVisibleTo(&panel));
        QVERIFY(button(panel,"keyframeMotionCurve")->isEnabled());const int count=canvas.document()->history()->count();button(panel,"keyframeMotionCurve")->click();
        QVERIFY(firstKey(canvas,id,KeyframeProperty::PositionX).spatial);QVERIFY(firstKey(canvas,id,KeyframeProperty::PositionX).spatial->curved);QCOMPARE(canvas.document()->history()->count(),count+1);
        button(panel,"keyframeOrientAlongPath")->click();QVERIFY(layerAnimation(canvas.shot(),id)->orientAlongPath);QCOMPARE(button(panel,"keyframeSelect_rotation")->text(),QString("Offset"));QVERIFY(button(panel,"keyframeSelect_rotation")->accessibleName().contains("motion path direction"));
        canvas.document()->history()->undo();QVERIFY(!layerAnimation(canvas.shot(),id)->orientAlongPath);QCOMPARE(button(panel,"keyframeSelect_rotation")->text(),QString("Rotate"));QVERIFY(button(panel,"keyframeSelect_rotation")->accessibleName().contains("Rotation"));
        canvas.document()->setLayerPositionLocked(true);QVERIFY(!button(panel,"keyframeMotionCurve")->isEnabled());QVERIFY(!button(panel,"keyframeOrientAlongPath")->isEnabled());
    }
    void morphGraphEditsOnlySelectedObject(){
        Canvas canvas;canvas.setProject(fixture());QVERIFY(ObjectTools::addLayer(canvas.document(),LayerKind::Vector));
        const auto id=ObjectTools::addPath(canvas.document(),{BezierNode::corner({10,10}),BezierNode::corner({50,10})},false,Qt::black,3);QVERIFY(!id.isEmpty());TimelineController controller(&canvas);
        QVERIFY(canvas.drawingTools()->selectAnimatedPath(id));QVERIFY(controller.setPathEnabled(id,true));QVERIFY(controller.setFrame(10));QVERIFY(controller.setPathKey(id));QVERIFY(controller.setFrame(0));
        KeyframePanel panel(&controller);show(panel);button(panel,"keyframeSelectPath")->click();QVERIFY(graph(panel)->isEnabled());beginCurveDrag(panel);graph(panel)->finishEditing();
        const auto *path=pathAnimation(canvas.shot(),controller.selectedPropertyLayerId(),id);QVERIFY(path);QVERIFY(path->keys.first().easing);QVERIFY(!propertyChannel(canvas.shot(),controller.selectedPropertyLayerId(),KeyframeProperty::Opacity));
        button(panel,"keyframeSourceEditing")->click();QVERIFY(!graph(panel)->isEnabled());
    }
    void scalarDraftsAndAccessibleNarrowControls(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);seedPosition(canvas,controller);KeyframePanel panel(&controller);show(panel);button(panel,"keyframeMotionPath")->click();
        auto *number=panel.findChild<ValueSlider *>("keyframeValue_positionX")->findChild<QDoubleSpinBox *>();number->setFocus();number->selectAll();QTest::keyClicks(number,"35.5");
        for(int i=0;i<3;++i){emit controller.changed();QCOMPARE(number->findChild<QLineEdit *>()->text(),QString("35.5"));}controller.cancelEditing();
        for(const auto &name:QStringList{"keyframeMotionCurve","keyframeOrientAlongPath","keyframeEasingReset"}){auto *control=button(panel,name);QVERIFY(!control->toolTip().isEmpty());QVERIFY(!control->accessibleName().isEmpty());QCOMPARE(control->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,control),600);QVERIFY(QRect(control->mapTo(&panel,QPoint()),control->size()).right()<panel.width());}
        for(auto *spin:panel.findChildren<QDoubleSpinBox *>()){QCOMPARE(spin->width(),48);QVERIFY(spin->suffix().isEmpty());}QCOMPARE(panel.width(),240);
        auto *tabs=panel.findChild<QTabBar *>("keyframeInterpolation");tabs->setCurrentIndex(5);settle();
        for(int i=0;i<tabs->count();++i){QVERIFY(tabs->tabRect(i).left()>=0);QVERIFY(tabs->tabRect(i).right()<tabs->width());}
    }
    void renderPreview(){
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");if(output.isEmpty())QSKIP("Opt-in advanced motion panel previews.");
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);seedPosition(canvas,controller);KeyframePanel panel(&controller);show(panel);button(panel,"keyframeMotionPath")->click();button(panel,"keyframeMotionCurve")->click();button(panel,"keyframeOrientAlongPath")->click();
        beginCurveDrag(panel);graph(panel)->finishEditing();QDir().mkpath(output);auto *scroll=panel.findChild<QScrollArea *>("keyframePropertiesScroll");scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());settle();QVERIFY(panel.grab().save(output+"/advanced-motion-easing-240.png"));
        scroll->verticalScrollBar()->setValue(0);settle();QVERIFY(panel.grab().save(output+"/advanced-motion-path-240.png"));panel.resize(320,760);settle();QVERIFY(panel.grab().save(output+"/advanced-motion-path-320.png"));
    }
};
QTEST_MAIN(AdvancedMotionPanelTests)
#include "advanced_motion_panel_tests.moc"
