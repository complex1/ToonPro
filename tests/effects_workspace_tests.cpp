#include "canvas.h"
#include "effects_panel.h"
#include "main_window.h"
#include "mask_tool.h"
#include "reference_panel.h"
#include "timeline_controller.h"
#include "value_slider.h"
#include <QAction>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QScreen>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QUuid>
#include <QtTest>
using namespace toon;
namespace {
Project fixture(){
    auto project=Project::create();project.width=192;project.height=128;project.shots[0].durationFrames=30;
    auto &layer=project.shots[0].layers[0];layer.name="Character";layer.effects.blurEnabled=true;layer.mask=LayerMask{};
    QImage pixels(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);pixels.fill(QColor("#5993cb"));layer.tiles.insert({0,0},pixels);
    project.shots[0].layers.append(RasterLayer::create("Background"));return project;
}
void mouse(Canvas *canvas,QEvent::Type type,QPointF point){
    QMouseEvent event(type,point,point,canvas->mapToGlobal(point),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
        type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(canvas,&event);
}
QPointF viewport(Canvas *canvas,QPointF source){return canvas->viewTransform().map(source+sheetGeometry(canvas->project(),canvas->shot()).output.topLeft());}
EffectsPanel *showEffects(MainWindow &window){
    auto *dock=window.findChild<QDockWidget *>("effectsPanel");if(!dock)return nullptr;dock->show();dock->raise();QCoreApplication::processEvents();return dock->findChild<EffectsPanel *>();
}
QDoubleSpinBox *blurNumber(EffectsPanel *panel){
    panel->findChild<QTabBar *>("effectsTabs")->setCurrentIndex(1);QCoreApplication::processEvents();return panel->findChild<ValueSlider *>("effectBlurRadius")->findChild<QDoubleSpinBox *>();
}
void enter(QDoubleSpinBox *spin,const QString &value){spin->setFocus();spin->selectAll();QTest::keyClicks(spin,value);}
}
class EffectsWorkspaceTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void pendingNumberUndoEscapeSaveAndReload(){
        QTemporaryDir dir;QString error;const auto path=dir.filePath("effects.toonproj");QVERIFY2(saveProject(fixture(),path,&error),qPrintable(error));
        MainWindow window(dir.filePath("settings.ini"));QVERIFY(window.openProject(path));window.resize(1280,900);window.show();
        auto *canvas=window.canvas();canvas->document()->selectLayer(0);auto *panel=showEffects(window);QVERIFY(panel);auto *spin=blurNumber(panel);QVERIFY(spin->isEnabled());
        auto *timeline=window.findChild<TimelineController *>();QVERIFY(timeline);auto *history=canvas->document()->history();
        auto *undo=window.findChild<QAction *>("undoAction"),*save=window.findChild<QAction *>("saveProjectAction");QVERIFY(undo&&save);
        const auto original=canvas->project().toJson();
        enter(spin,"19.5");QVERIFY(panel->editing());QVERIFY(timeline->hasPendingEdits());QVERIFY(undo->isEnabled());undo->trigger();
        QVERIFY(!panel->editing());QVERIFY(!timeline->hasPendingEdits());QVERIFY(!canvas->document()->effectsEditActive());QCOMPARE(canvas->project().toJson(),original);QCOMPARE(history->count(),0);
        enter(spin,"23.5");QTest::keyClick(spin,Qt::Key_Escape);QCOMPARE(canvas->project().toJson(),original);QVERIFY(!panel->editing());QVERIFY(!timeline->hasPendingEdits());
        enter(spin,"19.5");QVERIFY(save->isEnabled());save->trigger();QVERIFY(!panel->editing());QVERIFY(!timeline->hasPendingEdits());QVERIFY(!canvas->document()->effectsEditActive());
        QCOMPARE(history->count(),1);QVERIFY(history->isClean());QCOMPARE(canvas->shot().layers[0].effects.blurRadius,19.5);
        const auto saved=loadProject(path,&error);QVERIFY2(saved.has_value(),qPrintable(error));QCOMPARE(saved->toJson(),canvas->project().toJson());
        canvas->setFocus();QCoreApplication::processEvents();QCOMPARE(history->count(),1);QVERIFY(window.openProject(path));QCOMPARE(canvas->shot().layers[0].effects.blurRadius,19.5);QCOMPARE(history->count(),0);QVERIFY(window.close());
    }
    void unfinishedMaskStrokeUndoEscapeAndSave(){
        QTemporaryDir dir;QString error;const auto path=dir.filePath("mask.toonproj");QVERIFY2(saveProject(fixture(),path,&error),qPrintable(error));
        MainWindow window(dir.filePath("settings.ini"));QVERIFY(window.openProject(path));window.resize(1280,900);window.show();QCoreApplication::processEvents();
        auto *canvas=window.canvas();canvas->document()->selectLayer(0);canvas->fitSheet();canvas->setFocus();auto *tool=canvas->maskTool();tool->setEnabled(true);tool->setRevealing(false);tool->setSize(32);tool->setHardness(1);QVERIFY(tool->enabled());
        const auto original=canvas->project().toJson();const auto pixels=canvas->shot().layers[0].tiles;auto *history=canvas->document()->history();
        auto *undo=window.findChild<QAction *>("undoAction"),*save=window.findChild<QAction *>("saveProjectAction");QVERIFY(undo&&save);
        const QPointF from=viewport(canvas,{70,60}),to=viewport(canvas,{110,68});
        mouse(canvas,QEvent::MouseButtonPress,from);mouse(canvas,QEvent::MouseMove,to);QVERIFY(tool->dragging());QVERIFY(canvas->document()->effectsEditActive());QVERIFY(undo->isEnabled());undo->trigger();
        QVERIFY(!tool->dragging());mouse(canvas,QEvent::MouseButtonRelease,to);QCOMPARE(canvas->project().toJson(),original);QCOMPARE(history->count(),0);QCOMPARE(canvas->shot().layers[0].tiles,pixels);
        mouse(canvas,QEvent::MouseButtonPress,from);mouse(canvas,QEvent::MouseMove,to);QVERIFY(tool->dragging());QTest::keyClick(canvas,Qt::Key_Escape);QVERIFY(!tool->dragging());mouse(canvas,QEvent::MouseButtonRelease,to);QCOMPARE(canvas->project().toJson(),original);QCOMPARE(history->count(),0);
        mouse(canvas,QEvent::MouseButtonPress,from);mouse(canvas,QEvent::MouseMove,to);QVERIFY(tool->dragging());QVERIFY(save->isEnabled());save->trigger();
        QVERIFY(!tool->dragging());QVERIFY(!canvas->document()->effectsEditActive());QCOMPARE(history->count(),1);QVERIFY(history->isClean());QCOMPARE(canvas->shot().layers[0].tiles,pixels);
        const auto &mask=canvas->shot().layers[0].mask;QVERIFY(mask);QVERIFY(!mask->tiles.isEmpty());QVERIFY(mask->tiles.value({0,0}).constScanLine(60)[70]<255);
        const auto saved=loadProject(path,&error);QVERIFY2(saved.has_value(),qPrintable(error));QCOMPARE(saved->toJson(),canvas->project().toJson());QCOMPARE(saved->shots[0].layers[0].mask,mask);QCOMPARE(saved->shots[0].layers[0].tiles,pixels);
        mouse(canvas,QEvent::MouseButtonRelease,to);QCOMPARE(history->count(),1);QVERIFY(window.openProject(path));QCOMPARE(canvas->shot().layers[0].mask,saved->shots[0].layers[0].mask);QVERIFY(window.close());
    }
    void layerAndFrameTransitionsFlushOnlyTheOldTarget(){
        QTemporaryDir dir;QString error;const auto path=dir.filePath("transitions.toonproj");QVERIFY(saveProject(fixture(),path,&error));
        MainWindow window(dir.filePath("settings.ini"));QVERIFY(window.openProject(path));window.resize(1280,900);window.show();auto *canvas=window.canvas();canvas->document()->selectLayer(0);
        auto *panel=showEffects(window);QVERIFY(panel);auto *spin=blurNumber(panel);auto *timeline=window.findChild<TimelineController *>();QVERIFY(timeline);
        enter(spin,"27.2");canvas->document()->selectLayer(1);QCOMPARE(canvas->shot().layers[0].effects.blurRadius,27.2);QCOMPARE(canvas->shot().layers[1].effects.blurRadius,4.0);QVERIFY(!panel->editing());QVERIFY(!timeline->hasPendingEdits());
        canvas->document()->selectLayer(0);enter(spin,"31.4");QVERIFY(timeline->setFrame(8));QCOMPARE(canvas->shot().layers[0].effects.blurRadius,31.4);QVERIFY(!panel->editing());QVERIFY(!timeline->hasPendingEdits());QCOMPARE(canvas->document()->history()->count(),2);
        auto *save=window.findChild<QAction *>("saveProjectAction");QVERIFY(save);save->trigger();QVERIFY(canvas->document()->history()->isClean());QVERIFY(window.close());
    }
    void compactReferenceEffectsDocksFloatAndRestoreWithoutChangingArtwork(){
        QTemporaryDir dir;QString error;const auto path=dir.filePath("compact-panels.toonproj");const auto settings=dir.filePath("workspace.ini");
        auto project=fixture();QImage study(320,240,QImage::Format_ARGB32_Premultiplied);study.fill(QColor("#c7b698"));
        project.shots[0].references.append({QUuid::createUuid().toString(QUuid::WithoutBraces),"Character color and lighting study with a long descriptive name",study});
        QVERIFY2(saveProject(project,path,&error),qPrintable(error));
        QSize floatingSize;QPoint floatingPosition;int sharedWidth=0;
        {
            MainWindow window(settings);QVERIFY(window.openProject(path));window.resize(1280,900);window.show();
            auto *layers=window.findChild<QDockWidget *>("layersPanel");
            auto *references=window.findChild<QDockWidget *>("referencesPanel");
            auto *effects=window.findChild<QDockWidget *>("effectsPanel");
            QVERIFY(layers&&references&&effects);
            for(auto *dock:{layers,references,effects})dock->show();effects->raise();QCoreApplication::processEvents();
            QVERIFY(window.tabifiedDockWidgets(window.findChild<QDockWidget *>("colorsPanel")).contains(references));
            // Exercise a deliberately customized group as well as the new default.
            window.tabifyDockWidget(layers,references);effects->raise();QCoreApplication::processEvents();
            // This fixture measures the three drawing panels at a deliberately
            // small allocation. The new preset also groups optional workflow
            // inspectors here; their different minimums are not this contract.
            for(const auto *id:{"framePropertiesPanel","historyPanel","autoActionsPanel"}){
                auto *optional=window.findChild<QDockWidget *>(id);QVERIFY(optional);
                optional->setFloating(true);optional->hide();
            }
            effects->raise();QCoreApplication::processEvents();
            QVERIFY(window.tabifiedDockWidgets(layers).contains(references));QVERIFY(window.tabifiedDockWidgets(layers).contains(effects));
            auto *panel=effects->findChild<EffectsPanel *>();auto *referencePanel=references->findChild<ReferencePanel *>();QVERIFY(panel&&referencePanel);
            const int titleHeight=effects->height()-panel->height();
            window.resizeDocks({effects},{240},Qt::Horizontal);
            // QMainWindow allocates this tabbed group together with its dock
            // tab strip; leave that strip's height inside the 240–300 target.
            window.resizeDocks({effects},{252+titleHeight},Qt::Vertical);
            // The three tabbed panels share the same allocation. None may force
            // the everyday Layers dock back to a large inspector-sized minimum.
            QTRY_VERIFY(panel->width()>=220&&panel->width()<=252);
            QTRY_VERIFY2(panel->height()>=240&&panel->height()<=300,
                qPrintable(QString("Effects content height %1, dock %2, content minimum %3")
                    .arg(panel->height()).arg(effects->height()).arg(panel->minimumSizeHint().height())));
            auto *tabs=panel->findChild<QTabBar *>("effectsTabs");auto *scroll=panel->findChild<QScrollArea *>("effectsScroll");QVERIFY(tabs&&scroll);
            auto contained=[](QWidget *child,QWidget *parent){return child&&child->isVisibleTo(parent)&&parent->rect().contains(QRect(child->mapTo(parent,QPoint()),child->size()));};
            tabs->setCurrentIndex(1);QCoreApplication::processEvents();
            QVERIFY(contained(tabs,panel));QVERIFY(contained(panel->findChild<QLabel *>("effectsTargetName"),panel));
            QVERIFY(contained(panel->findChild<ValueSlider *>("effectBlurRadius"),panel));
            QCOMPARE(scroll->horizontalScrollBar()->maximum(),0);QCOMPARE(scroll->verticalScrollBar()->maximum(),0);
            tabs->setCurrentIndex(2);QCoreApplication::processEvents();
            const auto pinnedTabs=tabs->geometry();const auto pinnedTarget=panel->findChild<QLabel *>("effectsTargetName")->geometry();
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());QCoreApplication::processEvents();
            QCOMPARE(tabs->geometry(),pinnedTabs);QCOMPARE(panel->findChild<QLabel *>("effectsTargetName")->geometry(),pinnedTarget);
            for(int i=0;i<tabs->count();++i)QVERIFY(tabs->rect().contains(tabs->tabRect(i)));
            QCOMPARE(scroll->horizontalScrollBar()->maximum(),0);
            references->raise();QCoreApplication::processEvents();
            QVERIFY(referencePanel->width()<=252);QVERIFY(referencePanel->height()<=300);
            for(const auto *id:{"importReference","removeReference","fitReference"})
                QVERIFY2(contained(referencePanel->findChild<QToolButton *>(id),referencePanel),id);
            QVERIFY(contained(referencePanel->findChild<ValueSlider *>("referenceZoom"),referencePanel));
            auto *view=referencePanel->findChild<ReferenceView *>("referenceView");QVERIFY(contained(view,referencePanel));QVERIFY(view->height()>=80);QCOMPARE(view->image(),study);
            effects->setFloating(true);effects->resize(244,330);effects->show();QTRY_VERIFY(effects->isFloating());
            QCOMPARE(window.canvas()->project().toJson(),project.toJson());
            effects->setFloating(false);window.tabifyDockWidget(layers,effects);effects->show();effects->raise();
            QTRY_VERIFY(!effects->isFloating());QVERIFY(window.tabifiedDockWidgets(layers).contains(effects));
            window.resizeDocks({effects},{240},Qt::Horizontal);QCoreApplication::processEvents();
            // Save a deliberate floating reference size, after testing that the
            // Effects dock can return to the artist's tabbed workspace.
            references->setFloating(true);references->resize(318,410);references->move(window.frameGeometry().topLeft()+QPoint(80,80));references->show();
            QTRY_VERIFY(references->isFloating());QCoreApplication::processEvents();
            if(QGuiApplication::platformName()=="offscreen") {
                // At 2x the plugin's small logical display can be shorter than
                // this deliberately customized dock. Qt correctly clips an
                // off-screen floating window during restore. Save an on-screen
                // fixture instead, retaining the exact restoration assertions.
                const QRect available=references->screen()->availableGeometry();
                if(!available.contains(references->frameGeometry())) {
                    const QSize decoration=references->frameGeometry().size()-references->size();
                    const QPoint frameOffset=references->pos()-references->frameGeometry().topLeft();
                    const QSize room=(available.size()-decoration-QSize(48,48)).expandedTo(QSize(1,1));
                    references->resize(QSize(318,410).boundedTo(room));
                    references->move(available.topLeft()+QPoint(24,24)+frameOffset);QCoreApplication::processEvents();
                    qInfo().noquote()<<QString("Offscreen floating fixture: screen %1x%2, dock %3x%4 at %5,%6")
                        .arg(available.width()).arg(available.height()).arg(references->width()).arg(references->height()).arg(references->x()).arg(references->y());
                    QVERIFY2(available.contains(references->frameGeometry()),"Customized floating fixture must fit the virtual screen before saving");
                }
            }
            floatingSize=references->size();floatingPosition=references->pos();sharedWidth=effects->width();
            QCOMPARE(window.canvas()->project().toJson(),project.toJson());QCOMPARE(window.canvas()->document()->history()->count(),0);
            QVERIFY(window.close());
        }
        QSettings saved(settings,QSettings::IniFormat);QVERIFY(!saved.value("layoutV3").toByteArray().isEmpty());
        {
            MainWindow restored(settings);restored.show();
            // Qt clips restored window geometry to the offscreen plugin's small
            // virtual display. Compare dock allocation at the original window
            // size, as on the native desktop, rather than at that clipped size.
            if(QGuiApplication::platformName()=="offscreen")restored.resize(1280,900);
            QVERIFY(restored.openProject(path));
            auto *layers=restored.findChild<QDockWidget *>("layersPanel");auto *effects=restored.findChild<QDockWidget *>("effectsPanel");auto *references=restored.findChild<QDockWidget *>("referencesPanel");
            QVERIFY(layers&&effects&&references);QTRY_VERIFY(references->isVisible()&&references->isFloating());
            QVERIFY(!effects->isFloating());QVERIFY(restored.tabifiedDockWidgets(layers).contains(effects));
            QCOMPARE(restored.dockWidgetArea(effects),Qt::RightDockWidgetArea);
            QTRY_VERIFY2(qAbs(references->width()-floatingSize.width())<=4&&qAbs(references->height()-floatingSize.height())<=4,
                qPrintable(QString("Restored floating dock %1x%2, saved %3x%4, screen %5x%6")
                    .arg(references->width()).arg(references->height()).arg(floatingSize.width()).arg(floatingSize.height())
                    .arg(references->screen()->availableGeometry().width()).arg(references->screen()->availableGeometry().height())));
            QVERIFY((references->pos()-floatingPosition).manhattanLength()<=12);
            // The offscreen virtual display rebalances splitter space during
            // restoration; native desktops retain the original window size.
            if(QGuiApplication::platformName()=="offscreen")QTRY_VERIFY(effects->width()>=220&&effects->width()<=252);
            else QTRY_VERIFY2(qAbs(effects->width()-sharedWidth)<=12,qPrintable(QString("Restored dock width %1, saved %2").arg(effects->width()).arg(sharedWidth)));
            QCOMPARE(restored.canvas()->project().toJson(),project.toJson());QCOMPARE(restored.canvas()->document()->history()->count(),0);
            QVERIFY(restored.close());
        }
    }
};
QTEST_MAIN(EffectsWorkspaceTests)
#include "effects_workspace_tests.moc"
