#include "canvas.h"
#include "document.h"
#include "effects_panel.h"
#include "keyframe_panel.h"
#include "property_animation.h"
#include "property_row.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QColorDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QStyle>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QtTest>
#include <algorithm>
#include <cmath>

using namespace toon;
namespace {
Project fixture() {
    auto project=Project::create();project.width=160;project.height=120;
    project.shots[0].durationFrames=30;
    auto &layer=project.shots[0].layers[0];layer.name="Character colors";
    layer.effects.blurEnabled=true;layer.effects.blurRadius=6;
    layer.effects.shadowEnabled=true;layer.effects.shadowOffset={12,8};
    layer.effects.colorEnabled=true;
    LayerMask mask;mask.defaultValue=255;mask.position={5,7};layer.mask=mask;
    return project;
}
QToolButton *button(QWidget &panel,const QString &name){return panel.findChild<QToolButton *>(name);}
ValueSlider *value(QWidget &panel,const QString &name){return panel.findChild<ValueSlider *>(name);}
QDoubleSpinBox *number(QWidget &panel,const QString &name){return value(panel,name)->findChild<QDoubleSpinBox *>();}
QTabBar *tabs(QWidget &panel,const QString &name){return panel.findChild<QTabBar *>(name);}
const RasterLayer &active(const Canvas &canvas){return canvas.shot().layers[canvas.document()->activeLayer()];}
void settle(){QCoreApplication::processEvents();for(int i=0;i<4;++i)QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);}
void bind(EffectsPanel &panel,TimelineController &controller){
    panel.setTimelineController(&controller);
    QObject::connect(&panel,&EffectsPanel::pendingEditsChanged,&controller,[&](bool pending){controller.setPendingEdits(&panel,pending);});
    QObject::connect(&controller,&TimelineController::editResolutionRequested,&panel,&EffectsPanel::finishEditing);
    QObject::connect(&controller,&TimelineController::editCancellationRequested,&panel,&EffectsPanel::cancelEditing);
}
void writeNumber(QWidget &panel,const QString &name,const QString &text){auto *spin=number(panel,name);spin->setFocus();spin->selectAll();QTest::keyClicks(spin,text);}
void drag(QWidget *widget,QPoint from,QPoint to){
    QTest::mousePress(widget,Qt::LeftButton,Qt::NoModifier,from);
    QMouseEvent move(QEvent::MouseMove,to,widget->mapToGlobal(to),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(widget,&move);
    QTest::mouseRelease(widget,Qt::LeftButton,Qt::NoModifier,to);
}
QVector<int> frames(const Canvas &canvas,KeyframeProperty property){
    QVector<int> result;if(const auto *channel=propertyChannel(canvas.shot(),active(canvas).id,property))for(const auto &key:channel->keys)result.append(key.frame);return result;
}
qreal evaluated(const Canvas &canvas,KeyframeProperty property,int frame){return evaluateProperty(canvas.shot(),active(canvas).id,property,frame);}
}

class AnimatedEffectsPanelTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void directDiamondsSeedCurrentValuesAndPreviewWithoutBakingSource(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);
        EffectsPanel effects(&canvas);bind(effects,controller);KeyframePanel keys(&controller);
        effects.resize(240,680);effects.show();tabs(effects,"effectsTabs")->setCurrentIndex(1);settle();
        const auto source=active(canvas).tiles;const auto baseEffects=active(canvas).effects;
        button(effects,"effectsKey_blurRadius")->click();
        QCOMPARE(frames(canvas,KeyframeProperty::BlurRadius),QVector<int>{0});
        QCOMPARE(evaluated(canvas,KeyframeProperty::BlurRadius,0),6.);
        QCOMPARE(tabs(keys,"keyframeCategories")->currentIndex(),1);
        QVERIFY(button(keys,"keyframeSelect_blurRadius")->isChecked());
        QVERIFY(controller.setFrame(10));const int before=canvas.document()->history()->count();
        auto *track=value(effects,"effectBlurRadius")->findChild<QSlider *>();
        track->setSliderDown(true);track->setValue(180);track->setValue(240);
        QCOMPARE(evaluated(canvas,KeyframeProperty::BlurRadius,10),24.);
        QCOMPARE(active(canvas).effects.blurRadius,baseEffects.blurRadius);
        QCOMPARE(canvas.document()->history()->count(),before);
        track->setSliderDown(false);QCOMPARE(canvas.document()->history()->count(),before+1);
        QCOMPARE(frames(canvas,KeyframeProperty::BlurRadius),(QVector<int>{0,10}));
        QCOMPARE(evaluated(canvas,KeyframeProperty::BlurRadius,5),15.);
        QVERIFY(controller.setFrame(5));QCOMPARE(number(effects,"effectBlurRadius")->value(),15.);
        QCOMPARE(number(keys,"keyframeValue_blurRadius")->value(),15.);
        QCOMPARE(active(canvas).tiles,source);
        canvas.document()->history()->undo();QCOMPARE(frames(canvas,KeyframeProperty::BlurRadius),QVector<int>{0});
        QCOMPARE(evaluated(canvas,KeyframeProperty::BlurRadius,5),6.);
    }
    void keyedDraftSurvivesRefreshAndResizeThenCancelsOrCommitsOnNavigation(){
        Canvas canvas;auto project=fixture();project.shots[0].layers.append(RasterLayer::create("Other drawing"));canvas.setProject(project);
        canvas.document()->selectLayer(0);TimelineController controller(&canvas);EffectsPanel panel(&canvas);bind(panel,controller);
        panel.resize(240,680);panel.show();QVERIFY(QTest::qWaitForWindowExposed(&panel));panel.raise();panel.activateWindow();QVERIFY(QTest::qWaitForWindowActive(&panel));
        tabs(panel,"effectsTabs")->setCurrentIndex(1);button(panel,"effectsKey_blurRadius")->click();QVERIFY(controller.setFrame(10));
        const int before=canvas.document()->history()->count();writeNumber(panel,"effectBlurRadius","31.5");
        for(int width:{180,320,240}){panel.resize(width,680);emit canvas.document()->changed();settle();QCOMPARE(panel.width(),width);QCOMPARE(number(panel,"effectBlurRadius")->findChild<QLineEdit *>()->text(),QString("31.5"));QVERIFY(number(panel,"effectBlurRadius")->hasFocus());}
        QVERIFY(controller.hasPendingEdits());QCOMPARE(canvas.document()->history()->count(),before);
        controller.cancelEditing();QCOMPARE(evaluated(canvas,KeyframeProperty::BlurRadius,10),6.);QCOMPARE(frames(canvas,KeyframeProperty::BlurRadius),QVector<int>{0});
        writeNumber(panel,"effectBlurRadius","22");QVERIFY(controller.setFrame(15));
        QCOMPARE(evaluated(canvas,KeyframeProperty::BlurRadius,10),22.);QCOMPARE(canvas.document()->history()->count(),before+1);
        writeNumber(panel,"effectBlurRadius","29");const auto id=active(canvas).id;canvas.document()->selectLayer(1);
        QCOMPARE(evaluateProperty(canvas.shot(),id,KeyframeProperty::BlurRadius,15),29.);
        QCOMPARE(canvas.shot().layers[0].effects.blurRadius,6.);QCOMPARE(canvas.shot().layers[1].effects.blurRadius,4.);QVERIFY(!panel.editing());
    }
    void shadowPadMixesKeysAndStaticValuesInOneUndoAndColorDoesNotBake(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);EffectsPanel panel(&canvas);bind(panel,controller);
        panel.resize(240,700);panel.show();tabs(panel,"effectsTabs")->setCurrentIndex(2);settle();
        button(panel,"effectsKey_shadowOffsetX")->click();QVERIFY(controller.setFrame(10));const int before=canvas.document()->history()->count();
        auto *pad=panel.findChild<QWidget *>("effectShadowOffset");QVERIFY(pad);
        drag(pad,pad->rect().center(),pad->rect().center()+QPoint(20,10));
        const qreal changedX=evaluated(canvas,KeyframeProperty::ShadowOffsetX,10),changedY=active(canvas).effects.shadowOffset.y();
        QVERIFY(changedX!=12.);QVERIFY(changedY!=8.);QCOMPARE(active(canvas).effects.shadowOffset.x(),12.);
        QCOMPARE(canvas.document()->history()->count(),before+1);QVERIFY(!propertyChannel(canvas.shot(),active(canvas).id,KeyframeProperty::ShadowOffsetY));
        canvas.document()->history()->undo();QCOMPARE(frames(canvas,KeyframeProperty::ShadowOffsetX),QVector<int>{0});QCOMPARE(active(canvas).effects.shadowOffset,QPointF(12,8));
        canvas.document()->history()->redo();QCOMPARE(evaluated(canvas,KeyframeProperty::ShadowOffsetX,10),changedX);QCOMPARE(active(canvas).effects.shadowOffset.y(),changedY);
        QVERIFY(controller.setFrame(5));const auto sampled=evaluated(canvas,KeyframeProperty::ShadowOffsetX,5);const int colorBefore=canvas.document()->history()->count();
        QTimer::singleShot(0,&panel,[&]{auto *dialog=panel.findChild<QColorDialog *>("effectColorDialog");QVERIFY(dialog);dialog->setCurrentColor(QColor("#694b89"));dialog->accept();});
        button(panel,"effectShadowColor")->click();QCOMPARE(active(canvas).effects.shadowColor,QColor("#694b89"));
        QCOMPARE(evaluated(canvas,KeyframeProperty::ShadowOffsetX,5),sampled);QCOMPARE(active(canvas).effects.shadowOffset.x(),12.);
        QCOMPARE(frames(canvas,KeyframeProperty::ShadowOffsetX),(QVector<int>{0,10}));QCOMPARE(canvas.document()->history()->count(),colorBefore+1);
    }
    void heldSwitchesAndMaskPlacementPreserveMaskSource(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);EffectsPanel panel(&canvas);bind(panel,controller);KeyframePanel keys(&controller);
        panel.resize(240,700);panel.show();tabs(panel,"effectsTabs")->setCurrentIndex(1);
        button(panel,"effectsKey_blurEnabled")->click();QVERIFY(controller.setFrame(10));button(panel,"effectBlurEnabled")->click();
        QVERIFY(active(canvas).effects.blurEnabled);QCOMPARE(evaluated(canvas,KeyframeProperty::BlurEnabled,9),1.);QCOMPARE(evaluated(canvas,KeyframeProperty::BlurEnabled,10),0.);
        const auto *switches=propertyChannel(canvas.shot(),active(canvas).id,KeyframeProperty::BlurEnabled);QVERIFY(switches);
        for(const auto &key:switches->keys)QCOMPARE(key.interpolation,KeyframeInterpolation::Hold);
        emit controller.propertyChannelRequested(KeyframeProperty::BlurEnabled);
        QVERIFY(!tabs(keys,"keyframeInterpolation")->isEnabled());QVERIFY(button(keys,"keyframeToggle_blurEnabled")->isEnabled());QVERIFY(!button(keys,"keyframeToggle_blurEnabled")->isChecked());
        QVERIFY(keys.findChild<QLabel *>("keyframeEasingContext")->text().contains("hold"));
        QVERIFY(controller.setFrame(0));emit controller.propertyChannelRequested(KeyframeProperty::MaskPositionX);settle();
        QCOMPARE(tabs(panel,"effectsTabs")->currentIndex(),0);QCOMPARE(tabs(panel,"maskPropertyTabs")->currentIndex(),1);QCOMPARE(tabs(keys,"keyframeCategories")->currentIndex(),2);
        const auto source=*active(canvas).mask;
        for(const QString &key:{QString("maskPositionX"),QString("maskOpacity"),QString("maskFeather"),QString("maskInverted"),QString("maskEnabled")})button(panel,"effectsKey_"+key)->click();
        QVERIFY(controller.setFrame(10));number(panel,"maskPositionX")->setValue(45);number(panel,"maskOpacity")->setValue(50);number(panel,"maskFeather")->setValue(12);
        button(panel,"maskPresentationInverted")->click();button(panel,"maskEnabled")->click();
        QCOMPARE(active(canvas).mask->position,source.position);QCOMPARE(active(canvas).mask->opacity,source.opacity);QCOMPARE(active(canvas).mask->feather,source.feather);
        QCOMPARE(active(canvas).mask->inverted,source.inverted);QCOMPARE(active(canvas).mask->enabled,source.enabled);QCOMPARE(active(canvas).mask->tiles,source.tiles);QCOMPARE(active(canvas).mask->defaultValue,source.defaultValue);
        QCOMPARE(evaluated(canvas,KeyframeProperty::MaskPositionX,5),25.);QCOMPARE(evaluated(canvas,KeyframeProperty::MaskOpacity,5),.75);QCOMPARE(evaluated(canvas,KeyframeProperty::MaskFeather,5),6.);
        QCOMPARE(evaluated(canvas,KeyframeProperty::MaskInverted,9),0.);QCOMPARE(evaluated(canvas,KeyframeProperty::MaskInverted,10),1.);
        QCOMPARE(evaluated(canvas,KeyframeProperty::MaskEnabled,9),1.);QCOMPARE(evaluated(canvas,KeyframeProperty::MaskEnabled,10),0.);
        QVERIFY(controller.setFrame(5));QCOMPARE(number(panel,"maskPositionX")->value(),25.);QCOMPARE(number(panel,"maskOpacity")->value(),75.);QCOMPARE(number(panel,"maskFeather")->value(),6.);
    }
    void channelCategoriesAndLocksKeepUnrelatedControlsUsable(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);EffectsPanel effects(&canvas);bind(effects,controller);KeyframePanel keys(&controller);
        effects.resize(240,700);keys.resize(240,700);effects.show();keys.show();
        emit controller.propertyChannelRequested(KeyframeProperty::Exposure);settle();
        QCOMPARE(tabs(effects,"effectsTabs")->currentIndex(),4);QCOMPARE(tabs(keys,"keyframeCategories")->currentIndex(),1);
        QVERIFY(keys.findChild<PropertyRow *>("keyframeRow_exposure")->isVisibleTo(&keys));QVERIFY(!keys.findChild<PropertyRow *>("keyframeRow_positionX")->isVisibleTo(&keys));QVERIFY(!keys.findChild<QWidget *>("keyframeTransformControls")->isVisibleTo(&keys));
        canvas.document()->setLayerPositionLocked(true);
        QVERIFY(button(keys,"keyframeEnable_exposure")->isEnabled());QVERIFY(button(effects,"effectsKey_shadowOffsetX")->isEnabled());
        emit controller.propertyChannelRequested(KeyframeProperty::MaskPositionX);QVERIFY(!button(keys,"keyframeEnable_maskPositionX")->isEnabled());QVERIFY(!value(effects,"maskPositionX")->isEnabled());QVERIFY(button(effects,"effectsKey_maskOpacity")->isEnabled());
        QVERIFY(keys.findChild<QLabel *>("keyframeStatus")->text().contains("position"));
        canvas.document()->setLayerPositionLocked(false);button(effects,"maskRemove")->click();
        QVERIFY(!button(keys,"keyframeEnable_maskPositionX")->isEnabled());QVERIFY(keys.findChild<QLabel *>("keyframeStatus")->text().contains("mask",Qt::CaseInsensitive));
        tabs(keys,"keyframeCategories")->setCurrentIndex(1);QVERIFY(button(keys,"keyframeEnable_blurRadius")->isEnabled());
        canvas.document()->setLayersLocked({active(canvas).id},true);QVERIFY(!button(effects,"effectsKey_blurRadius")->isEnabled());QVERIFY(!button(keys,"keyframeEnable_blurRadius")->isEnabled());
    }
    void heldSwitchRowsStayInlineWithAccessibleLargeText(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);KeyframePanel panel(&controller);
        panel.resize(240,700);panel.show();settle();
        const auto baseFont=panel.font();
        for(bool large:{false,true}){
            QFont font=baseFont;if(large)font.setPixelSize(QFontMetrics(baseFont).height()+6);panel.setFont(font);
            panel.resize(large?190:240,700);settle();QCOMPARE(panel.width(),large?190:240);
            for(const auto property:{KeyframeProperty::EffectsEnabled,KeyframeProperty::BlurEnabled,KeyframeProperty::ShadowEnabled,KeyframeProperty::GlowEnabled,KeyframeProperty::ColorEnabled,KeyframeProperty::MaskEnabled,KeyframeProperty::MaskInverted}){
                emit controller.propertyChannelRequested(property);settle();const QString key=keyframePropertyKey(property);
                auto *row=panel.findChild<QWidget *>("keyframeRow_"+key);auto *label=button(panel,"keyframeSelect_"+key);auto *toggle=button(panel,"keyframeToggle_"+key);auto *diamond=button(panel,"keyframeEnable_"+key);
                QVERIFY(row&&label&&toggle&&diamond);QVERIFY(row->isVisibleTo(&panel));
                const QRect labelBounds(label->mapTo(row,QPoint()),label->size()),toggleBounds(toggle->mapTo(row,QPoint()),toggle->size());
                QVERIFY2(std::abs(labelBounds.center().y()-toggleBounds.center().y())<=1,qPrintable(key));QVERIFY(labelBounds.right()<toggleBounds.left());
                QVERIFY(row->height()<=std::max(label->height(),toggle->height())+2);QCOMPARE(toggle->width(),48);QCOMPARE(toggle->minimumWidth(),48);QCOMPARE(toggle->maximumWidth(),48);QVERIFY(toggle->height()>=28);QVERIFY(label->width()>=28);
                for(auto *control:{label,toggle,diamond}){QVERIFY(!control->toolTip().isEmpty());QVERIFY(!control->accessibleName().isEmpty());QCOMPARE(control->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,control),600);const QRect bounds(control->mapTo(&panel,QPoint()),control->size());QVERIFY2(bounds.left()>=0&&bounds.right()<panel.width(),qPrintable(control->objectName()));}
                QCOMPARE(label->text(),keyframePropertyName(property));
            }
        }
    }
    void compactAnimationPanelsKeepControlsAccessible(){
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);EffectsPanel effects(&canvas);bind(effects,controller);KeyframePanel keys(&controller);
        effects.resize(240,640);keys.resize(240,700);effects.show();keys.show();
        const QString output=qEnvironmentVariable("TOON_UI_PREVIEW");if(!output.isEmpty())QVERIFY(QDir().mkpath(output));
        for(const auto property:{KeyframeProperty::BlurRadius,KeyframeProperty::ShadowOffsetX,KeyframeProperty::Exposure,KeyframeProperty::MaskPositionX}){
            emit controller.propertyChannelRequested(property);settle();
            for(QWidget *panel:{static_cast<QWidget *>(&effects),static_cast<QWidget *>(&keys)}){
                auto *scroll=panel->findChild<QScrollArea *>();QVERIFY(scroll);QCOMPARE(scroll->horizontalScrollBar()->maximum(),0);
                for(auto *field:panel->findChildren<QDoubleSpinBox *>()){QCOMPARE(field->width(),48);QVERIFY(field->suffix().isEmpty());}
                for(auto *control:panel->findChildren<QToolButton *>())if(control->isVisibleTo(panel)&&control->objectName().startsWith(panel==&effects?"effectsKey_":"keyframeEnable_")){
                    QCOMPARE(control->size(),QSize(28,28));QCOMPARE(control->iconSize(),QSize(16,16));QVERIFY(!control->toolTip().isEmpty());QVERIFY(!control->accessibleName().isEmpty());
                    QCOMPARE(control->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,control),600);
                    const QRect bounds(control->mapTo(panel,QPoint()),control->size());QVERIFY(bounds.left()>=0&&bounds.right()<panel->width());
                }
            }
            if(!output.isEmpty()){QVERIFY(effects.grab().save(output+"/animated-effects-"+keyframePropertyKey(property)+".png"));QVERIFY(keys.grab().save(output+"/animated-properties-"+keyframePropertyKey(property)+".png"));}
        }
        effects.resize(180,640);QFont larger=keys.font();larger.setPixelSize(keys.fontMetrics().height()+6);keys.setFont(larger);keys.resize(190,700);settle();
        QCOMPARE(effects.width(),180);QCOMPARE(keys.width(),190);
        for(auto *panel:{static_cast<QWidget *>(&effects),static_cast<QWidget *>(&keys)}){
            auto *category=panel->findChild<QTabBar *>(panel==&effects?"effectsTabs":"keyframeCategories");
            for(int i=0;i<category->count();++i){QVERIFY(category->rect().contains(category->tabRect(i)));QVERIFY(!category->tabToolTip(i).isEmpty());QVERIFY(!category->accessibleTabName(i).isEmpty());}
            for(auto *field:panel->findChildren<ValueSlider *>())if(field->isVisibleTo(panel)){const QRect bounds(field->mapTo(panel,QPoint()),field->size());QVERIFY2(bounds.left()>=0&&bounds.right()<panel->width(),qPrintable(field->objectName()));}
        }
        if(!output.isEmpty()){QVERIFY(effects.grab().save(output+"/animated-mask-placement-180.png"));QVERIFY(keys.grab().save(output+"/animated-mask-properties-large-font-190.png"));}
    }
};
QTEST_MAIN(AnimatedEffectsPanelTests)
#include "test_animated_effects_panel.moc"
