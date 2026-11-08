#include "effects_panel.h"
#include "canvas.h"
#include "document.h"
#include "layer_panel.h"
#include "mask_tool.h"
#include "property_row.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include "value_slider.h"
#include <QColorDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSlider>
#include <QStyle>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>
using namespace toon;
namespace {
Project fixture(){auto p=Project::create();p.width=160;p.height=120;p.shots[0].durationFrames=30;p.shots[0].layers[0].name="Character line art";return p;}
QToolButton *button(QWidget &panel,const char *name){return panel.findChild<QToolButton *>(name);}
ValueSlider *value(QWidget &panel,const char *name){return panel.findChild<ValueSlider *>(name);}
QDoubleSpinBox *number(QWidget &panel,const char *name){return value(panel,name)->findChild<QDoubleSpinBox *>();}
const RasterLayer &active(Canvas &canvas){return canvas.shot().layers[canvas.document()->activeLayer()];}
void type(QWidget &panel,const char *name,const QString &text){auto *spin=number(panel,name);spin->setFocus();spin->selectAll();QTest::keyClicks(spin,text);}
void settle(){QCoreApplication::processEvents();for(int i=0;i<4;++i)QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);}
void category(QWidget &panel,int index){panel.findChild<QTabBar *>("effectsTabs")->setCurrentIndex(index);settle();}
void drag(QWidget *widget,QPoint from,QPoint to){QTest::mousePress(widget,Qt::LeftButton,Qt::NoModifier,from);QMouseEvent move(QEvent::MouseMove,to,widget->mapToGlobal(to),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(widget,&move);QTest::mouseRelease(widget,Qt::LeftButton,Qt::NoModifier,to);}
}
class EffectsPanelTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void liveDragTypedDraftCancelAndNoChangeUndo(){
        Canvas canvas;canvas.setProject(fixture());EffectsPanel panel(&canvas);panel.resize(240,640);panel.show();category(panel,1);
        button(panel,"effectBlurEnabled")->click();QVERIFY(active(canvas).effects.blurEnabled);
        auto *history=canvas.document()->history();const int before=history->count();QSignalSpy pending(&panel,&EffectsPanel::pendingEditsChanged);
        auto *field=value(panel,"effectBlurRadius");auto *slider=field->findChild<QSlider *>();slider->setSliderDown(true);slider->setValue(100);slider->setValue(125);
        QCOMPARE(active(canvas).effects.blurRadius,12.5);QCOMPARE(history->count(),before);QVERIFY(panel.editing());QVERIFY(canvas.document()->effectsEditActive());
        slider->setSliderDown(false);QCOMPARE(history->count(),before+1);QVERIFY(!panel.editing());QCOMPARE(pending.count(),2);
        slider->setSliderDown(true);slider->setValue(220);field->cancelEditing();QCOMPARE(active(canvas).effects.blurRadius,12.5);QCOMPARE(history->count(),before+1);
        type(panel,"effectBlurRadius","17.3");for(int i=0;i<3;++i){emit canvas.document()->changed();QCOMPARE(number(panel,"effectBlurRadius")->findChild<QLineEdit *>()->text(),QString("17.3"));}
        panel.cancelEditing();QCOMPARE(active(canvas).effects.blurRadius,12.5);QCOMPARE(history->count(),before+1);
        type(panel,"effectBlurRadius","21.6");panel.finishEditing();QCOMPARE(active(canvas).effects.blurRadius,21.6);QCOMPARE(history->count(),before+2);
        slider->setSliderDown(true);slider->setValue(350);slider->setValue(216);slider->setSliderDown(false);QCOMPARE(history->count(),before+2);
        history->undo();QCOMPARE(active(canvas).effects.blurRadius,12.5);QCOMPARE(number(panel,"effectBlurRadius")->value(),12.5);
    }
    void targetAndFrameTransitionsCommitTheOriginalDraft(){
        auto project=fixture();project.shots[0].layers.append(RasterLayer::create("Background"));Canvas canvas;canvas.setProject(project);EffectsPanel panel(&canvas);panel.resize(240,640);panel.show();
        canvas.document()->selectLayer(0);category(panel,1);button(panel,"effectBlurEnabled")->click();type(panel,"effectBlurRadius","24");
        canvas.document()->selectLayer(1);QCOMPARE(canvas.shot().layers[0].effects.blurRadius,24.0);QCOMPARE(canvas.shot().layers[1].effects.blurRadius,4.0);QVERIFY(!panel.editing());
        QCOMPARE(panel.findChild<QLabel *>("effectsTargetName")->text(),QString("Background"));button(panel,"effectBlurEnabled")->click();type(panel,"effectBlurRadius","33");
        QVERIFY(canvas.document()->setCurrentFrame(5));QCOMPARE(active(canvas).effects.blurRadius,33.0);QVERIFY(!panel.editing());
        type(panel,"effectBlurRadius","47");category(panel,4);QCOMPARE(active(canvas).effects.blurRadius,47.0);QVERIFY(!panel.editing());
    }
    void effectModesPrecisionAndShadowPad(){
        Canvas canvas;canvas.setProject(fixture());EffectsPanel panel(&canvas);panel.resize(240,640);panel.show();category(panel,2);
        button(panel,"effectShadowEnabled")->click();number(panel,"effectShadowX")->setValue(-511.5);number(panel,"effectShadowY")->setValue(512);number(panel,"effectShadowOpacity")->setValue(32.5);
        QCOMPARE(active(canvas).effects.shadowOffset,QPointF(-511.5,512));QCOMPARE(active(canvas).effects.shadowOpacity,.325);
        auto *pad=panel.findChild<QWidget *>("effectShadowOffset");const int count=canvas.document()->history()->count();drag(pad,pad->rect().center(),pad->rect().center()+QPoint(20,10));
        QVERIFY(active(canvas).effects.shadowOffset.x()>0);QVERIFY(active(canvas).effects.shadowOffset.y()>0);QCOMPARE(canvas.document()->history()->count(),count+1);
        QCOMPARE(number(panel,"effectShadowX")->value(),active(canvas).effects.shadowOffset.x());
        const QPointF before=active(canvas).effects.shadowOffset;QTest::mousePress(pad,Qt::LeftButton,Qt::NoModifier,pad->rect().center());QTest::keyClick(pad,Qt::Key_Escape);QTest::mouseRelease(pad,Qt::LeftButton);QCOMPARE(active(canvas).effects.shadowOffset,before);
        QTest::keyClick(pad,Qt::Key_Right,Qt::ShiftModifier);QCOMPARE(active(canvas).effects.shadowOffset.x(),before.x()+10);
        category(panel,3);button(panel,"effectGlowEnabled")->click();number(panel,"effectGlowRadius")->setValue(128);number(panel,"effectGlowOpacity")->setValue(70);
        QCOMPARE(active(canvas).effects.glowRadius,128.0);QCOMPARE(active(canvas).effects.glowOpacity,.7);
        category(panel,4);button(panel,"effectColorEnabled")->click();number(panel,"effectExposure")->setValue(1.25);number(panel,"effectHue")->setValue(-180);number(panel,"effectSaturation")->setValue(-65);
        QCOMPARE(active(canvas).effects.exposure,1.25);QCOMPARE(active(canvas).effects.hue,-180.0);QCOMPARE(active(canvas).effects.saturation,-65.0);
        button(panel,"effectsMasterEnabled")->click();QVERIFY(!active(canvas).effects.enabled);QVERIFY(!value(panel,"effectExposure")->isEnabled());QVERIFY(active(canvas).effects.colorEnabled);QVERIFY(active(canvas).effects.shadowEnabled);
        button(panel,"effectsMasterEnabled")->click();QVERIFY(value(panel,"effectExposure")->isEnabled());QCOMPARE(active(canvas).effects.exposure,1.25);
    }
    void colorDialogPreviewsAndCancelsOrCommitsOnce(){
        Canvas canvas;canvas.setProject(fixture());EffectsPanel panel(&canvas);panel.show();category(panel,2);button(panel,"effectShadowEnabled")->click();const int before=canvas.document()->history()->count();bool previewed=false;
        QTimer::singleShot(0,&panel,[&]{auto *dialog=panel.findChild<QColorDialog *>("effectColorDialog");if(!dialog)return;dialog->setCurrentColor(QColor("#c76130"));previewed=active(canvas).effects.shadowColor==QColor("#c76130");dialog->reject();});
        button(panel,"effectShadowColor")->click();QVERIFY(previewed);QCOMPARE(active(canvas).effects.shadowColor,QColor(Qt::black));QCOMPARE(canvas.document()->history()->count(),before);
        QTimer::singleShot(0,&panel,[&]{auto *dialog=panel.findChild<QColorDialog *>("effectColorDialog");if(!dialog)return;dialog->setCurrentColor(QColor("#406bc8"));dialog->accept();});
        button(panel,"effectShadowColor")->click();QCOMPARE(active(canvas).effects.shadowColor,QColor("#406bc8"));QCOMPARE(canvas.document()->history()->count(),before+1);QVERIFY(!panel.editing());
    }
    void shadowPadLostReleaseAndFocusFinishOnce(){
        Canvas canvas;canvas.setProject(fixture());EffectsPanel panel(&canvas);panel.resize(240,640);panel.show();category(panel,2);button(panel,"effectShadowEnabled")->click();
        auto *pad=panel.findChild<QWidget *>("effectShadowOffset");QVERIFY(pad->hasMouseTracking());const int before=canvas.document()->history()->count();
        QTest::mousePress(pad,Qt::LeftButton,Qt::NoModifier,pad->rect().center());QVERIFY(panel.editing());
        QMouseEvent lost(QEvent::MouseMove,QPointF(20,20),pad->mapToGlobal(QPoint(20,20)),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(pad,&lost);
        QVERIFY(!panel.editing());QCOMPARE(canvas.document()->history()->count(),before+1);const auto pose=active(canvas).effects.shadowOffset;
        QTest::mouseRelease(pad,Qt::LeftButton,Qt::NoModifier,QPoint(20,20));QCOMPARE(active(canvas).effects.shadowOffset,pose);QCOMPARE(canvas.document()->history()->count(),before+1);
        QTest::mousePress(pad,Qt::LeftButton,Qt::NoModifier,pad->rect().center()+QPoint(18,12));QVERIFY(panel.editing());
        QFocusEvent focus(QEvent::FocusOut);QApplication::sendEvent(pad,&focus);QVERIFY(!panel.editing());QCOMPARE(canvas.document()->history()->count(),before+2);QTest::mouseRelease(pad,Qt::LeftButton);
    }
    void maskActionsSelectionAndToolSettings(){
        Canvas canvas;canvas.setProject(fixture());EffectsPanel panel(&canvas);panel.resize(240,640);panel.show();settle();
        QVERIFY(!button(panel,"maskFromSelection")->isEnabled());button(panel,"maskAddWhite")->click();QVERIFY(active(canvas).mask);QCOMPARE(active(canvas).mask->defaultValue,255);
        QVERIFY(!button(panel,"maskAddWhite")->isEnabled());button(panel,"maskPaint")->click();QVERIFY(canvas.maskTool()->enabled());
        panel.findChild<QTabBar *>("maskMode")->setCurrentIndex(0);QVERIFY(canvas.maskTool()->revealing());panel.findChild<QTabBar *>("maskMode")->setCurrentIndex(1);QVERIFY(!canvas.maskTool()->revealing());
        const int toolHistory=canvas.document()->history()->count();number(panel,"maskSize")->setValue(63.5);number(panel,"maskStrength")->setValue(35);number(panel,"maskHardness")->setValue(22);
        QCOMPARE(canvas.maskTool()->size(),63.5);QCOMPARE(canvas.maskTool()->strength(),.35);QCOMPARE(canvas.maskTool()->hardness(),.22);QCOMPARE(canvas.document()->history()->count(),toolHistory);
        button(panel,"maskHideAll")->click();QCOMPARE(active(canvas).mask->defaultValue,0);button(panel,"maskInvert")->click();QCOMPARE(active(canvas).mask->defaultValue,0);QVERIFY(active(canvas).mask->inverted);button(panel,"maskRevealAll")->click();QCOMPARE(active(canvas).mask->defaultValue,255);QVERIFY(active(canvas).mask->inverted);
        button(panel,"effectsMasterEnabled")->click();QVERIFY(active(canvas).mask->enabled);QVERIFY(button(panel,"maskPaint")->isEnabled());button(panel,"maskEnabled")->click();QVERIFY(!active(canvas).mask->enabled);QVERIFY(!button(panel,"maskPaint")->isEnabled());
        button(panel,"maskRemove")->click();QVERIFY(!active(canvas).mask);QVERIFY(!canvas.maskTool()->enabled());
        QPainterPath selection;selection.addRect(QRectF(20,20,50,40));canvas.document()->setRasterSelection(selection);QVERIFY(button(panel,"maskFromSelection")->isEnabled());button(panel,"maskFromSelection")->click();
        QVERIFY(active(canvas).mask);QCOMPARE(active(canvas).mask->defaultValue,0);QVERIFY(!active(canvas).mask->tiles.isEmpty());QCOMPARE(active(canvas).mask->tiles.first().constScanLine(30)[30],uchar(255));QCOMPARE(active(canvas).mask->tiles.first().constScanLine(5)[5],uchar(0));
        QVERIFY(!panel.findChild<QLabel *>("maskThumbnail")->pixmap().isNull());
    }
    void targetEligibilityAndAdjustmentLayer(){
        auto project=fixture();auto folder=RasterLayer::create("Character folder");folder.group=true;project.shots[0].layers[0].parentId=folder.id;project.shots[0].layers.append(folder);
        auto notes=RasterLayer::create("Production notes");notes.annotation=true;project.shots[0].layers.append(notes);auto scene=RasterLayer::create("City reference");scene.kind=LayerKind::Scene3D;scene.scene3d=Scene3D{};project.shots[0].layers.append(scene);
        Canvas canvas;canvas.setProject(project);EffectsPanel panel(&canvas);panel.resize(240,640);panel.show();canvas.document()->selectLayer(0);category(panel,1);
        QVERIFY(button(panel,"effectBlurEnabled")->isEnabled());canvas.document()->setLayersLocked({folder.id},true);QVERIFY(!button(panel,"effectsMasterEnabled")->isEnabled());QVERIFY(!button(panel,"effectsAddAdjustment")->isEnabled());QVERIFY(panel.findChild<QLabel *>("effectsStatus")->text().contains("Unlock"));
        canvas.document()->setLayersLocked({folder.id},false);canvas.document()->selectLayer(1);QVERIFY(button(panel,"effectBlurEnabled")->isEnabled());
        canvas.document()->selectLayer(2);QVERIFY(!button(panel,"effectsMasterEnabled")->isEnabled());QVERIFY(!button(panel,"effectsAddAdjustment")->isEnabled());QVERIFY(panel.findChild<QLabel *>("effectsStatus")->text().contains("notes"));canvas.document()->selectLayer(3);QVERIFY(!button(panel,"effectsMasterEnabled")->isEnabled());QVERIFY(panel.findChild<QLabel *>("effectsStatus")->text().contains("3D"));
        canvas.document()->selectLayer(0);button(panel,"effectsAddAdjustment")->click();QVERIFY(active(canvas).adjustment);QVERIFY(active(canvas).effects.colorEnabled);
        auto *tabs=panel.findChild<QTabBar *>("effectsTabs");QCOMPARE(tabs->currentIndex(),4);QVERIFY(!tabs->isTabEnabled(1));QVERIFY(!tabs->isTabEnabled(2));QVERIFY(!tabs->isTabEnabled(3));QVERIFY(tabs->isTabEnabled(0));
        number(panel,"effectContrast")->setValue(27.5);QCOMPARE(active(canvas).effects.contrast,27.5);category(panel,0);button(panel,"maskAddWhite")->click();QVERIFY(active(canvas).mask);
        canvas.setAnimationPlayback(true);QVERIFY(!button(panel,"effectsMasterEnabled")->isEnabled());QVERIFY(panel.findChild<QLabel *>("effectsStatus")->text().contains("playback"));canvas.setAnimationPlayback(false);
    }
    void narrowAccessibleLayoutAndScreenshots(){
        Canvas canvas;canvas.setProject(fixture());EffectsPanel panel(&canvas);panel.resize(240,640);panel.show();button(panel,"maskAddWhite")->click();
        auto *categoryTabs=panel.findChild<QTabBar *>("effectsTabs");QVERIFY(categoryTabs);
        for(int i=0;i<categoryTabs->count();++i)QVERIFY(categoryTabs->rect().contains(categoryTabs->tabRect(i)));
        const QString output=qEnvironmentVariable("TOON_UI_PREVIEW");if(!output.isEmpty())QVERIFY(QDir().mkpath(output));
        for(int tab=0;tab<5;++tab){category(panel,tab);if(tab>0){const char *ids[]={"effectBlurEnabled","effectShadowEnabled","effectGlowEnabled","effectColorEnabled"};button(panel,ids[tab-1])->click();}settle();QCOMPARE(panel.width(),240);
            auto *scroll=panel.findChild<QScrollArea *>("effectsScroll");QCOMPARE(scroll->horizontalScrollBar()->maximum(),0);
            for(auto *field:panel.findChildren<QDoubleSpinBox *>()){QCOMPARE(field->width(),48);QVERIFY(field->suffix().isEmpty());}
            for(auto *control:panel.findChildren<QToolButton *>())if(control->isVisibleTo(&panel)){QVERIFY(!control->toolTip().isEmpty());QVERIFY(!control->accessibleName().isEmpty());QCOMPARE(control->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,control),600);QVERIFY2(!control->icon().isNull()||control->arrowType()!=Qt::NoArrow,qPrintable(control->objectName()));const auto area=QRect(control->mapTo(&panel,QPoint()),control->size());QVERIFY2(area.left()>=0&&area.right()<panel.width(),qPrintable(control->objectName()));if(control->objectName().startsWith("effect")||control->objectName().startsWith("mask")){QCOMPARE(control->size(),QSize(28,28));QCOMPARE(control->iconSize(),QSize(ui::metrics::actionIcon,ui::metrics::actionIcon));}}
            if(!output.isEmpty())QVERIFY(panel.grab().save(QDir(output).filePath(QString("effects-%1-240.png").arg(tab))));}
    }
    void compactRowsWrapWithoutLosingFocusedDraft(){
        Canvas canvas;canvas.setProject(fixture());EffectsPanel panel(&canvas);panel.resize(240,440);panel.show();panel.activateWindow();category(panel,4);
        button(panel,"effectColorEnabled")->click();auto *field=value(panel,"effectExposure");auto *row=qobject_cast<PropertyRow *>(field->parentWidget());QVERIFY(row);
        auto *spin=number(panel,"effectExposure");auto *track=field->findChild<QSlider *>();settle();
        QVERIFY(row->labelWidget()->geometry().right()<field->geometry().left());QVERIFY(track->width()>=74);
        type(panel,"effectExposure","1.25");QTRY_VERIFY(spin->hasFocus());const int before=canvas.document()->history()->count();
        panel.resize(180,440);settle();QCOMPARE(panel.width(),180);QVERIFY(row->labelWidget()->geometry().bottom()<field->geometry().top());QVERIFY(track->width()>=74);
        emit canvas.document()->changed();settle();QVERIFY(spin->hasFocus());QCOMPARE(spin->findChild<QLineEdit *>()->text(),QString("1.25"));QVERIFY(panel.editing());QCOMPARE(canvas.document()->history()->count(),before);
        panel.resize(320,440);settle();QVERIFY(row->labelWidget()->geometry().right()<field->geometry().left());QVERIFY(spin->hasFocus());QCOMPARE(spin->findChild<QLineEdit *>()->text(),QString("1.25"));
        panel.finishEditing();QCOMPARE(active(canvas).effects.exposure,1.25);QCOMPARE(canvas.document()->history()->count(),before+1);
        const QString output=qEnvironmentVariable("TOON_UI_PREVIEW");if(!output.isEmpty()){panel.resize(180,440);settle();QVERIFY(panel.grab().save(QDir(output).filePath("effects-color-narrow.png")));}
    }
    void shortDockKeepsNavigationAndEachCategoryScroll(){
        auto project=fixture();project.shots[0].layers[0].name="Character line art with a long production name";
        Canvas canvas;canvas.setProject(project);EffectsPanel panel(&canvas);panel.resize(240,280);panel.show();category(panel,2);button(panel,"effectShadowEnabled")->click();settle();
        auto *scroll=panel.findChild<QScrollArea *>("effectsScroll");auto *bar=scroll->verticalScrollBar();QTRY_VERIFY(bar->maximum()>0);bar->setValue(bar->maximum());const int position=bar->value();
        auto *tabs=panel.findChild<QTabBar *>("effectsTabs");auto *target=panel.findChild<QLabel *>("effectsTargetName");auto *status=panel.findChild<QLabel *>("effectsStatus");const QRect targetGeometry=target->geometry(),statusGeometry=status->geometry(),tabsGeometry=tabs->geometry();
        const int before=canvas.document()->history()->count();emit canvas.document()->changed();settle();QCOMPARE(bar->value(),position);QCOMPARE(tabs->currentIndex(),2);QCOMPARE(canvas.document()->history()->count(),before);
        category(panel,1);QTRY_COMPARE(bar->maximum(),0);QCOMPARE(panel.size(),QSize(240,280));
        category(panel,2);QTRY_COMPARE(bar->value(),position);QCOMPARE(target->geometry(),targetGeometry);QCOMPARE(status->geometry(),statusGeometry);QCOMPARE(tabs->geometry(),tabsGeometry);
        QVERIFY(target->toolTip().contains("production name"));QCOMPARE(target->accessibleName(),target->toolTip());
        canvas.document()->setLayersLocked({active(canvas).id},true);settle();QCOMPARE(status->geometry(),statusGeometry);QCOMPARE(tabs->geometry(),tabsGeometry);QVERIFY(status->text().contains("Unlock"));QCOMPARE(status->accessibleName(),status->toolTip());
        canvas.document()->setLayersLocked({active(canvas).id},false);settle();
        for(int i=0;i<tabs->count();++i){QVERIFY(tabs->rect().contains(tabs->tabRect(i)));QVERIFY(!tabs->tabToolTip(i).isEmpty());QVERIFY(!tabs->accessibleTabName(i).isEmpty());}
        const QString output=qEnvironmentVariable("TOON_UI_PREVIEW");if(!output.isEmpty()){QVERIFY(panel.grab().save(QDir(output).filePath("effects-shadow-short-bottom.png")));bar->setValue(0);settle();QVERIFY(panel.grab().save(QDir(output).filePath("effects-shadow-short-top.png")));category(panel,1);QVERIFY(panel.grab().save(QDir(output).filePath("effects-blur-short.png")));}
    }
    void inactiveEffectsKeepTheirValuesAndPinnedEnableActions(){
        Canvas canvas;canvas.setProject(fixture());EffectsPanel panel(&canvas);panel.resize(240,280);panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));panel.activateWindow();QVERIFY(QTest::qWaitForWindowActive(&panel));
        auto *scroll=panel.findChild<QScrollArea *>("effectsScroll");QVERIFY(scroll);
        auto *history=canvas.document()->history();
        const char *enabledIds[]={"effectBlurEnabled","effectShadowEnabled","effectGlowEnabled","effectColorEnabled"};
        const char *valueIds[]={"effectBlurRadius","effectShadowRadius","effectGlowRadius","effectExposure"};
        const double configured[]={17.5,23.5,27.5,1.25};
        for(int i=0;i<4;++i){
            category(panel,i+1);auto *enable=button(panel,enabledIds[i]);QVERIFY(enable);
            auto *details=panel.findChild<QWidget *>(QString::fromLatin1(enabledIds[i])+"Details");QVERIFY(details);
            QVERIFY(details->isHidden());QTRY_COMPARE(scroll->verticalScrollBar()->maximum(),0);
            QVERIFY(enable->isVisibleTo(&panel));QVERIFY(!scroll->isAncestorOf(enable));
            enable->click();QTRY_VERIFY(!details->isHidden());number(panel,valueIds[i])->setValue(configured[i]);
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
            QVERIFY(panel.rect().contains(QRect(enable->mapTo(&panel,QPoint()),enable->size())));
            const int before=history->count();enable->click();QTRY_VERIFY(details->isHidden());QCOMPARE(history->count(),before+1);
            QCOMPARE(number(panel,valueIds[i])->value(),configured[i]);
            QTRY_COMPARE(scroll->verticalScrollBar()->maximum(),0);
            history->undo();QTRY_VERIFY(!details->isHidden());QVERIFY(enable->isChecked());
            QCOMPARE(number(panel,valueIds[i])->value(),configured[i]);
        }
        // A master bypass keeps configured controls readable while disabling edits.
        auto *colorDetails=panel.findChild<QWidget *>("effectColorEnabledDetails");
        button(panel,"effectsMasterEnabled")->click();QVERIFY(!colorDetails->isHidden());QVERIFY(!colorDetails->isEnabled());
        button(panel,"effectsMasterEnabled")->click();QVERIFY(colorDetails->isEnabled());
        category(panel,1);type(panel,"effectBlurRadius","19.5");const int before=history->count();
        button(panel,"effectBlurEnabled")->click();QCOMPARE(active(canvas).effects.blurRadius,19.5);QVERIFY(!active(canvas).effects.blurEnabled);
        QVERIFY(!panel.editing());QCOMPARE(history->count(),before+2);
    }
    void maskControlsFollowAvailabilityWithoutChangingBrushSettings(){
        Canvas canvas;canvas.setProject(fixture());EffectsPanel panel(&canvas);panel.resize(240,300);panel.show();settle();
        auto *controls=panel.findChild<QWidget *>("maskControls");QVERIFY(controls);QVERIFY(controls->isHidden());
        QVERIFY(button(panel,"maskAddWhite")->isVisibleTo(&panel));QVERIFY(button(panel,"maskFromSelection")->isVisibleTo(&panel));
        QVERIFY(button(panel,"maskEnabled")->isHidden());
        canvas.maskTool()->setSize(63.5);canvas.maskTool()->setStrength(.35);
        button(panel,"maskAddWhite")->click();QVERIFY(!controls->isHidden());QVERIFY(button(panel,"maskAddWhite")->isHidden());
        QVERIFY(button(panel,"maskEnabled")->isVisibleTo(&panel));QCOMPARE(value(panel,"maskSize")->value(),63.5);
        button(panel,"maskRemove")->click();QVERIFY(controls->isHidden());
        canvas.document()->history()->undo();QVERIFY(!controls->isHidden());QCOMPARE(value(panel,"maskSize")->value(),63.5);
        QCOMPARE(value(panel,"maskStrength")->value(),35.);
    }
    void fiveCategoriesAndPinnedContextFitNarrowAndLargerFonts(){
        auto project=fixture();project.shots[0].layers[0].name="Character with a long production name";
        Canvas canvas;canvas.setProject(project);EffectsPanel panel(&canvas);panel.resize(180,320);panel.show();settle();
        auto *tabs=panel.findChild<QTabBar *>("effectsTabs");auto *target=panel.findChild<QLabel *>("effectsTargetName");
        auto *status=panel.findChild<QLabel *>("effectsStatus");QVERIFY(tabs&&target&&status);QCOMPARE(tabs->count(),6);
        for(int i=0;i<tabs->count();++i){QVERIFY(tabs->rect().contains(tabs->tabRect(i)));QVERIFY(tabs->tabRect(i).width()>=ui::metrics::actionTarget);}
        const int history=canvas.document()->history()->count();
        auto font=panel.font();font.setPixelSize(24);panel.setFont(font);panel.resize(240,420);settle();
        QVERIFY(target->height()>=target->fontMetrics().height());QVERIFY(status->height()>=status->fontMetrics().height());
        for(int i=0;i<tabs->count();++i)QVERIFY(tabs->rect().contains(tabs->tabRect(i)));
        category(panel,2);button(panel,"effectShadowEnabled")->click();settle();
        auto *scroll=panel.findChild<QScrollArea *>("effectsScroll");QCOMPARE(scroll->horizontalScrollBar()->maximum(),0);
        auto *enable=button(panel,"effectShadowEnabled");QVERIFY(panel.rect().contains(QRect(enable->mapTo(&panel,QPoint()),enable->size())));
        const auto targetGeometry=target->geometry(),statusGeometry=status->geometry(),tabsGeometry=tabs->geometry();
        canvas.document()->setLayersLocked({active(canvas).id},true);settle();
        QCOMPARE(target->geometry(),targetGeometry);QCOMPARE(status->geometry(),statusGeometry);QCOMPARE(tabs->geometry(),tabsGeometry);
        QVERIFY(status->text().contains("Unlock"));QCOMPARE(status->accessibleName(),status->toolTip());
        QCOMPARE(canvas.document()->history()->count(),history+2);
        const QString output=qEnvironmentVariable("TOON_UI_PREVIEW");if(!output.isEmpty()){
            QVERIFY(panel.grab().save(QDir(output).filePath("effects-shadow-larger-font.png")));
            canvas.document()->setLayersLocked({active(canvas).id},false);panel.setFont(QApplication::font());panel.resize(180,320);category(panel,4);settle();
            QVERIFY(panel.grab().save(QDir(output).filePath("effects-disabled-narrow.png")));
        }
    }
    void layerBadgesHaveAccessibleMaskAndEffectsState(){
        Canvas canvas;canvas.setProject(fixture());LayerPanel layers(canvas.document());EffectsPanel effects(&canvas);layers.resize(240,420);layers.show();
        button(effects,"maskAddWhite")->click();category(effects,1);button(effects,"effectBlurEnabled")->click();settle();
        auto *tree=layers.findChild<QTreeWidget *>();QVERIFY(tree);QVERIFY(tree->currentItem());
        QTRY_VERIFY(tree->currentItem()->toolTip(1).contains("Mask"));QVERIFY(tree->currentItem()->toolTip(1).contains("Effects"));
        QVERIFY(tree->currentItem()->data(1,Qt::AccessibleTextRole).toString().contains("Mask"));
        button(effects,"effectsMasterEnabled")->click();QTRY_VERIFY(tree->currentItem()->toolTip(1).contains("Effects off"));
        button(effects,"effectsAddAdjustment")->click();QVERIFY(active(canvas).adjustment);QTRY_VERIFY(tree->currentItem()->toolTip(1).contains("Color adjustment"));
        for(const char *id:{"layerAlphaLocked","layerClipped","layerAnnotation","mergeLayerDown"})QVERIFY2(!button(layers,id)->isEnabled(),id);
        QVERIFY(button(layers,"removeLayer")->isEnabled());
        QCOMPARE(tree->visualItemRect(tree->currentItem()).height(),ui::metrics::layerRow);
        canvas.document()->selectLayer(0);QVERIFY(!button(layers,"removeLayer")->isEnabled());
    }
};
QTEST_MAIN(EffectsPanelTests)
#include "effects_panel_tests.moc"
