#include "animation_test_fixture.h"
#include "canvas.h"
#include "drawing_template_dialog.h"
#include "drawing_template_library.h"
#include "main_window.h"
#include "onion_settings_dialog.h"
#include "timeline_controller.h"
#include "timeline_panel.h"
#include "timeline_ripple_dialog.h"
#include "value_slider.h"
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QMenu>
#include <QtTest>

using namespace toon;
namespace {
ValueSlider *value(QWidget &widget,const char *name){return widget.findChild<ValueSlider *>(name);}
QToolButton *button(QWidget &widget,const char *name){return widget.findChild<QToolButton *>(name);}
void enter(ValueSlider *control,const QString &text){auto *number=control->findChild<QDoubleSpinBox *>();number->setFocus();number->selectAll();QTest::keyClicks(number,text);}
void preview(QWidget &widget,const QString &name){
    const auto directory=qEnvironmentVariable("TOON_UI_PREVIEW");if(directory.isEmpty())return;
    QDir().mkpath(directory);widget.grab().save(directory+"/"+name+".png");
}
}
class TimelineImprovementsTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void rippleDialogCommitsTypedDraftAndSingleUndo(){
        Canvas canvas;canvas.setProject(animation_fixture::project());TimelineController controller(&canvas);
        const auto original=canvas.project().toJson();const int originalDuration=controller.frameCount();
        QVERIFY(controller.setFrame(5));QVERIFY(controller.setPlaybackRange(4,6));controller.setPlaybackRangeEnabled(true);
        TimelineRippleDialog dialog(&controller);dialog.resize(240,430);dialog.show();QCoreApplication::processEvents();
        auto *modes=dialog.findChild<QTabBar *>("rippleKind");QVERIFY(modes->rect().contains(modes->tabRect(2)));
        QCOMPARE(value(dialog,"rippleStart")->value(),5.);QCOMPARE(value(dialog,"rippleCount")->value(),3.);
        value(dialog,"rippleStart")->setValue(3);enter(value(dialog,"rippleCount"),"4");
        QCOMPARE(canvas.project().toJson(),original);preview(dialog,"ripple-time");
        dialog.findChild<QPushButton *>("applyTimelineRipple")->click();
        QCOMPARE(dialog.result(),int(QDialog::Accepted));QCOMPARE(controller.frameCount(),originalDuration+4);
        QCOMPARE(controller.currentFrame(),9);QCOMPARE(controller.playbackFirstFrame(),8);QCOMPARE(controller.playbackLastFrame(),10);
        QCOMPARE(canvas.document()->history()->count(),1);canvas.document()->history()->undo();QCOMPARE(canvas.project().toJson(),original);
        canvas.document()->history()->redo();QCOMPARE(controller.frameCount(),originalDuration+4);
    }
    void rippleMoveDeleteCancelAndLockedFeedback(){
        Canvas canvas;canvas.setProject(animation_fixture::project());TimelineController controller(&canvas);
        const auto original=canvas.project().toJson();
        const auto movedDrawing=canvas.shot().animationTracks[0].exposures[1].drawingId;
        {
            TimelineRippleDialog dialog(&controller);dialog.findChild<QTabBar *>("rippleKind")->setCurrentIndex(2);
            value(dialog,"rippleStart")->setValue(5);value(dialog,"rippleCount")->setValue(3);value(dialog,"rippleDestination")->setValue(6);
            QVERIFY(!dialog.findChild<QPushButton *>("applyTimelineRipple")->isEnabled());
            value(dialog,"rippleDestination")->setValue(1);dialog.findChild<QPushButton *>("applyTimelineRipple")->click();
            QCOMPARE(dialog.result(),int(QDialog::Accepted));QVERIFY(exposureAtFrame(canvas.shot().animationTracks[0],0));
            QCOMPARE(canvas.shot().animationTracks[0].exposures.first().drawingId,movedDrawing);
        }
        canvas.document()->history()->undo();QCOMPARE(canvas.project().toJson(),original);
        {
            TimelineRippleDialog dialog(&controller);dialog.findChild<QTabBar *>("rippleKind")->setCurrentIndex(1);
            value(dialog,"rippleStart")->setValue(1);value(dialog,"rippleCount")->setValue(controller.frameCount());
            QVERIFY(!dialog.findChild<QPushButton *>("applyTimelineRipple")->isEnabled());dialog.reject();
        }
        QCOMPARE(canvas.project().toJson(),original);
        auto locked=canvas.project();locked.shots[0].layers.last().locked=true;canvas.setProject(locked);
        TimelineRippleDialog dialog(&controller);dialog.show();value(dialog,"rippleStart")->setValue(3);
        dialog.findChild<QPushButton *>("applyTimelineRipple")->click();
        QVERIFY(dialog.isVisible());QVERIFY(dialog.findChild<QLabel *>("rippleResult")->text().contains("Unlock"));
        QCOMPARE(canvas.project().toJson(),locked.toJson());QCOMPARE(canvas.document()->history()->count(),0);dialog.reject();
    }
    void timelineActionsFitNarrowPanelAndRemainDiscoverable(){
        Canvas canvas;canvas.setProject(animation_fixture::project());TimelineController controller(&canvas);TimelinePanel panel(&controller);
        panel.resize(240,360);panel.show();QCoreApplication::processEvents();QCOMPARE(panel.width(),240);
        auto *frameNumber=value(panel,"timelineCurrentFrame")->findChild<QDoubleSpinBox *>();
        QVERIFY(panel.rect().contains(QRect(frameNumber->mapTo(&panel,QPoint(0,0)),frameNumber->size())));
        for(const auto *id:{"timelineOnionSettings","timelineRipple","timelineImportAudio"}){
            const auto *action=button(panel,id);QVERIFY(action);QVERIFY(action->isEnabled());QVERIFY(!action->accessibleName().isEmpty());QVERIFY(!action->toolTip().isEmpty());
            // Secondary commands move into the compact Timeline menu; keep
            // their source controls for shared handlers and direct wide access.
            auto *more=button(panel,"timelineMore");QVERIFY(more&&more->isVisible());
            auto *option=more->menu()->findChild<QAction *>(QString::fromLatin1(id)+"Option");QVERIFY(option&&option->isEnabled());
            QVERIFY(!option->text().isEmpty());
        }
        preview(panel,"timeline-compact");
        QTimer::singleShot(0,&panel,[]{auto *dialog=qobject_cast<TimelineRippleDialog *>(QApplication::activeModalWidget());QVERIFY(dialog);dialog->reject();});
        button(panel,"timelineRipple")->click();QCOMPARE(canvas.document()->history()->count(),0);
    }
    void acceptedOnionSettingsPersistCancelAndResetStayTransient(){
        QTemporaryDir directory;const auto preferences=directory.filePath("workspace.ini"),projectPath=directory.filePath("shot.toonproj");QString error;
        QVERIFY2(saveProject(animation_fixture::project(),projectPath,&error),qPrintable(error));
        OnionSkinSettings expected;
        {
            MainWindow window(preferences);QVERIFY(window.openProject(projectPath));auto *controller=window.findChild<TimelineController *>();auto *panel=window.findChild<TimelinePanel *>();QVERIFY(panel&&controller);
            const auto artwork=window.canvas()->project().toJson();
            QTimer::singleShot(0,panel,[&]{auto *dialog=qobject_cast<OnionSettingsDialog *>(QApplication::activeModalWidget());QVERIFY(dialog);
                value(*dialog,"onionPreviousCount")->setValue(4);value(*dialog,"onionNextCount")->setValue(2);value(*dialog,"onionOpacity")->setValue(65);
                QCOMPARE(controller->onionSkinSettings().previousCount,4);dialog->accept();});
            button(*panel,"timelineOnionSettings")->click();expected=controller->onionSkinSettings();QCOMPARE(expected.opacity,.65);
            QTimer::singleShot(0,panel,[&]{auto *dialog=qobject_cast<OnionSettingsDialog *>(QApplication::activeModalWidget());QVERIFY(dialog);
                dialog->resetDefaults();QCOMPARE(controller->onionSkinSettings(),OnionSkinSettings{});dialog->reject();});
            button(*panel,"timelineOnionSettings")->click();QCOMPARE(controller->onionSkinSettings(),expected);
            QCOMPARE(window.canvas()->project().toJson(),artwork);QCOMPARE(window.canvas()->document()->history()->count(),0);QVERIFY(window.close());
        }
        MainWindow reopened(preferences);QCOMPARE(reopened.findChild<TimelineController *>()->onionSkinSettings(),expected);QVERIFY(reopened.close());
        QSettings saved(preferences,QSettings::IniFormat);QCOMPARE(onionSkinSettingsFromJson(QJsonDocument::fromJson(saved.value("timeline/onionSkin").toByteArray()).object()),expected);
    }
    void globalTemplateCreatesMixedDrawingsAcrossProjects(){
        QTemporaryDir directory;QString error;const auto preferences=directory.filePath("workspace.ini"),path=directory.filePath("shot.toonproj");
        DrawingTemplateLibrary library(directory.filePath("drawing-templates.json"));QVERIFY(library.load(&error));
        const QVector<DrawingTemplateSlot> recipe{{"paint","Colors",LayerKind::Raster},{"ink","Lines",LayerKind::Vector}};
        QVERIFY2(library.saveTemplate("Character",recipe,&error),qPrintable(error));QVERIFY(saveProject(animation_fixture::project(),path,&error));
        MainWindow window(preferences);QVERIFY(window.openProject(path));auto *panel=window.findChild<TimelinePanel *>();QVERIFY(panel);
        QTimer::singleShot(0,panel,[&]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());QVERIFY(dialog);
            auto *presets=dialog->findChild<QComboBox *>("animationDrawingTemplate");QVERIFY(presets);const int index=presets->findText("Character");QVERIFY(index>=0);presets->setCurrentIndex(index);
            QCOMPARE(dialog->findChild<DrawingTemplateWidget *>()->templateSlots(),recipe);dialog->findChild<QPushButton *>("confirmAnimationTrack")->click();});
        button(*panel,"timelineAddTrack")->click();QCOMPARE(window.canvas()->shot().animationTracks.last().drawingTemplate,recipe);
        window.findChild<QAction *>("saveProjectAction")->trigger();QVERIFY(window.canvas()->document()->history()->isClean());QVERIFY(window.close());
        const auto loaded=loadProject(path,&error);QVERIFY2(loaded.has_value(),qPrintable(error));QCOMPARE(loaded->shots[0].animationTracks.last().drawingTemplate,recipe);
        DrawingTemplateLibrary reopened(directory.filePath("drawing-templates.json"));QVERIFY(reopened.load(&error));QCOMPARE(reopened.entries().first().layers,recipe);
    }
};
QTEST_MAIN(TimelineImprovementsTests)
#include "timeline_improvements_tests.moc"
