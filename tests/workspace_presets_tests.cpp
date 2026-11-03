#include "main_window.h"
#include "canvas.h"
#include "document.h"
#include "drawing_input.h"
#include "timeline_controller.h"
#include "movie_review_controller.h"
#include "audio_playback.h"
#include "workspace_library.h"
#include "sheet.h"
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScopeGuard>
#include <QSettings>
#include <QStackedWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QUuid>
#include <QVBoxLayout>
#include <QWindow>
#include <QtTest>
#include <functional>

using namespace toon;
namespace {
Project fixture() {
    auto project=Project::create();project.width=640;project.height=480;project.transparent=true;
    QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);
    QPainter painter(&tile);painter.fillRect(QRect(60,60,80,60),QColor("#ba655c"));painter.end();project.shots[0].layers[0].tiles[{0,0}]=tile;
    return project;
}
void settle(){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);QCoreApplication::processEvents();}
void fitWindow(MainWindow &window,QSize requested={1280,860}) {
    const auto available=window.screen()->availableGeometry();
    // A virtual offscreen screen may be smaller than the requested render;
    // only native windows need to remain reachable on a physical display.
    window.resize(QGuiApplication::platformName()=="offscreen"?requested:requested.boundedTo(available.size()-QSize(40,60)));
    window.move(available.topLeft()+QPoint(20,40));settle();
}
bool prepare(MainWindow &window,const QTemporaryDir &directory) {
    const auto path=directory.filePath("drawing.toonproj");QString error;
    if(!QFileInfo::exists(path) && !saveProject(fixture(),path,&error))return false;
    if(!window.openProject(path))return false;
    fitWindow(window);window.show();if(!QTest::qWaitForWindowExposed(&window))return false;
    window.raise();window.activateWindow();if(!QTest::qWaitForWindowActive(&window))return false;
    window.canvas()->setFocus();settle();return true;
}
QDockWidget *dock(MainWindow &window,const char *id){return window.findChild<QDockWidget *>(id);}
QTabBar *inspector(MainWindow &window){return window.findChild<QTabBar *>("framePropertiesTabs");}
bool selectPage(MainWindow &window,const QString &page){auto *tabs=inspector(window);if(!tabs)return false;for(int i=0;i<tabs->count();++i)if(tabs->tabData(i).toString()==page){tabs->setCurrentIndex(i);settle();return true;}return false;}
QString page(MainWindow &window){auto *tabs=inspector(window);return tabs?tabs->tabData(tabs->currentIndex()).toString():QString();}
QJsonObject arrangement(MainWindow &window){
    QJsonObject result;for(auto *panel:window.findChildren<QDockWidget *>()){
        QStringList tabs;for(auto *tab:window.tabifiedDockWidgets(panel))tabs.append(tab->objectName());tabs.sort();
        result.insert(panel->objectName(),QJsonObject{{"area",int(window.dockWidgetArea(panel))},{"floating",panel->isFloating()},{"hidden",panel->isHidden()},{"tabs",QJsonArray::fromStringList(tabs)}});
    }
    for(auto *toolbar:window.findChildren<QToolBar *>())result.insert(toolbar->objectName(),QJsonObject{{"area",int(window.toolBarArea(toolbar))},{"hidden",toolbar->isHidden()},{"orientation",int(toolbar->orientation())}});
    return result;
}
void capture(MainWindow &window,const QString &name){const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");if(output.isEmpty())return;QTest::qWait(350);settle();QVERIFY(QDir().mkpath(output));QVERIFY(window.grab().save(output+"/"+name+".png"));}
QString focusState(MainWindow &window){
    const auto describe=[](QWidget *widget){return widget?QString("%1(%2)").arg(widget->metaObject()->className(),widget->objectName()):QString("none");};
    return QString("app=%1 parentActive=%2 active=%3 focus=%4 modal=%5").arg(int(QGuiApplication::applicationState())).arg(window.isActiveWindow()).arg(describe(QApplication::activeWindow()),describe(QApplication::focusWidget()),describe(QApplication::activeModalWidget()));
}
QString invokeModal(QAction *action,const QString &name,const std::function<void(QDialog &)> &inspect){
    QString error;bool visited=false;QTimer poll,deadline;poll.setInterval(5);deadline.setSingleShot(true);
    QObject::connect(&poll,&QTimer::timeout,[&]{
        auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if(!dialog||dialog->objectName()!=name||!dialog->windowHandle()||!dialog->windowHandle()->isExposed()||!dialog->isActiveWindow())return;
        poll.stop();deadline.stop();visited=true;const auto closeOnFailure=qScopeGuard([dialog]{if(dialog->isVisible())dialog->reject();});inspect(*dialog);
    });
    QObject::connect(&deadline,&QTimer::timeout,[&]{error="Workspace modal did not become exposed and active: "+name;if(auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()))dialog->reject();});
    poll.start();deadline.start(20000);action->trigger();poll.stop();deadline.stop();if(!visited&&error.isEmpty())error="Workspace modal did not open: "+name;return error;
}
}
class WorkspacePresetsTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){configureDrawingInput();applyTheme();}
    void builtinsOrganizePanelsWithoutChangingArtworkOrBrush(){
        QTemporaryDir directory;MainWindow window(directory.filePath("workspace.ini"));QVERIFY(prepare(window,directory));
        auto *canvas=window.canvas();const auto project=canvas->project().toJson();const auto image=renderMovieFrame(canvas->project());const auto brush=canvas->brush;
        const int history=canvas->document()->history()->count();
        for(const auto &id:QStringList{"illustration","anime","storyboard","animatic","compact"}) {
            QVERIFY2(window.applyWorkspace(id),qPrintable(id));settle();QCOMPARE(window.activeWorkspaceId(),id);
            QCOMPARE(window.compactDrawingWorkspace(),id=="compact");
            auto *timeline=dock(window,"animationPanel"),*frames=dock(window,"framePropertiesPanel"),*shots=dock(window,"shotsPanel");QVERIFY(timeline&&frames&&shots);
            const bool timed=id=="anime"||id=="animatic";
            QCOMPARE(timeline->isHidden(),!timed);if(timed){QCOMPARE(window.dockWidgetArea(timeline),Qt::BottomDockWidgetArea);QVERIFY(!frames->isHidden());QCOMPARE(window.dockWidgetArea(frames),Qt::RightDockWidgetArea);}
            const bool shotList=id=="storyboard"||id=="animatic";QCOMPARE(shots->isHidden(),!shotList);if(shotList)QCOMPARE(window.dockWidgetArea(shots),Qt::LeftDockWidgetArea);
            for(const auto *name:{"toolsPanel","brushPresetPanel","propertiesPanel"}){auto *panel=dock(window,name);QVERIFY(panel);QCOMPARE(panel->isHidden(),id=="compact");QCOMPARE(window.dockWidgetArea(panel),Qt::LeftDockWidgetArea);}
            QVERIFY(!dock(window,"layersPanel")->isHidden());QCOMPARE(window.dockWidgetArea(dock(window,"layersPanel")),Qt::RightDockWidgetArea);
            QVERIFY(!window.findChild<QToolBar *>("toolboxPanel")->isHidden());QVERIFY(!window.findChild<QToolBar *>("quickAccessToolbar")->isHidden());
            QCOMPARE(canvas->project().toJson(),project);QCOMPARE(renderMovieFrame(canvas->project()),image);QVERIFY(canvas->brush==brush);QCOMPARE(canvas->document()->history()->count(),history);QVERIFY(canvas->document()->history()->isClean());
            QVERIFY(canvas->width()>200);QVERIFY(canvas->height()>140);capture(window,"workspace-"+id);
            fitWindow(window,{1024,740});QVERIFY(canvas->width()>180);QVERIFY(canvas->height()>120);capture(window,"workspace-"+id+"-1024");fitWindow(window);
        }
        QVERIFY(window.close());
    }
    void leavingReviewForADrawingLayoutStopsAudioAndRestoresTheDrawing_data(){
        QTest::addColumn<int>("destination");
        QTest::newRow("illustration")<<0;QTest::newRow("saved-layout")<<1;
        QTest::newRow("previous-layout")<<2;QTest::newRow("drawing-mode")<<3;
    }
    void leavingReviewForADrawingLayoutStopsAudioAndRestoresTheDrawing(){
        QFETCH(int,destination);QTemporaryDir directory;auto project=fixture();project.shots[0].durationFrames=48;project.shots[0].scene="Market";
        auto second=Project::create().shots.first();second.durationFrames=48;second.scene="Home";second.number="C002";project.shots.append(second);
        // Silent samples exercise the real audio stream without making the UI
        // regression suite play audible sound on the artist's speakers.
        const auto asset=makeAudioAsset("Review audio",QByteArray(audioSampleRate*audioBytesPerSampleFrame,0));
        for(auto &shot:project.shots){AudioClip clip;clip.id=QUuid::createUuid().toString(QUuid::WithoutBraces);clip.name="Review audio";clip.asset=asset;clip.sampleCount=asset->sampleFrameCount();shot.audioClips.append(clip);}
        QString error;QVERIFY2(saveProject(project,directory.filePath("drawing.toonproj"),&error),qPrintable(error));
        MainWindow window(directory.filePath("workspace.ini"));QVERIFY(prepare(window,directory));QVERIFY(window.applyWorkspace("anime"));
        QVERIFY(window.saveWorkspaceAs("Review layout",&error));const auto savedId=window.activeWorkspaceId();
        if(destination==1||destination==2)QVERIFY(window.applyWorkspace("illustration"));
        auto *controller=window.findChild<TimelineController *>("timelineController");QVERIFY(controller);QVERIFY(controller->setFrame(7));
        auto *document=window.canvas()->document();const auto source=document->project().toJson();const auto editingShot=document->activeShotId();const auto editingLayer=document->activeLayer();const int history=document->history()->count();
        auto *review=controller->movieReview();QVERIFY(review->setMode(ReviewMode::Movie));QVERIFY(review->setFrame(53));review->setPlaying(true);
        auto *audio=review->findChild<AudioPlayback *>();auto *overlay=window.canvas()->findChild<QWidget *>("movieReviewViewport");
        QVERIFY(review->playing());QVERIFY(audio&&audio->active());QVERIFY(overlay&&overlay->isVisible());
        if(destination==0){QVERIFY(window.applyWorkspace("illustration"));QCOMPARE(window.activeWorkspaceId(),QString("illustration"));}
        else if(destination==1){QVERIFY(window.applyWorkspace(savedId));QCOMPARE(window.activeWorkspaceId(),savedId);}
        else if(destination==2){auto *previous=window.findChild<QAction *>("previousWorkspaceAction");QVERIFY(previous&&previous->isEnabled());previous->trigger();QCOMPARE(window.activeWorkspaceId(),savedId);}
        else{window.setDrawingMode(true);QVERIFY(window.drawingMode());}
        settle();QCOMPARE(review->mode(),ReviewMode::Shot);QVERIFY(!review->playing());QVERIFY(!audio->active());QVERIFY(!overlay->isVisible());
        QTRY_VERIFY2(window.canvas()->hasFocus(),qPrintable(focusState(window)));
        QCOMPARE(document->activeShotId(),editingShot);QCOMPARE(document->currentFrame(),7);QCOMPARE(document->activeLayer(),editingLayer);
        QCOMPARE(document->project().toJson(),source);QCOMPARE(document->history()->count(),history);QVERIFY(document->history()->isClean());
        if(destination==3)window.setDrawingMode(false);QVERIFY(window.close());
    }
    void namedRestoreKeepsFloatingPanelsRailsVisibilityAndInspector(){
        QTemporaryDir directory;MainWindow window(directory.filePath("workspace.ini"));QVERIFY(prepare(window,directory));QVERIFY(window.applyWorkspace("anime"));
        const auto original=window.canvas()->project().toJson();const auto pixels=renderMovieFrame(window.canvas()->project());
        auto *refs=dock(window,"referencesPanel");refs->setFloating(true);refs->resize(300,380);refs->move(window.frameGeometry().topLeft()+QPoint(50,80));refs->show();
        auto *rail=window.findChild<QToolBar *>("toolboxPanel");auto *quick=window.findChild<QToolBar *>("quickAccessToolbar");window.addToolBar(Qt::RightToolBarArea,rail);quick->hide();
        QVERIFY(selectPage(window,"Cues"));dock(window,"framePropertiesPanel")->hide();settle();
        // A visible Timeline must not force a deliberately hidden inspector
        // back open when the saved dock state is restored.
        QVERIFY(!dock(window,"animationPanel")->isHidden());QString error;QVERIFY2(window.saveWorkspaceAs("My timing layout",&error),qPrintable(error));const auto id=window.activeWorkspaceId();QVERIFY(window.workspaceLibrary()->find(id));
        QVERIFY(window.applyWorkspace("illustration"));QVERIFY(window.applyWorkspace(id));settle();
        QVERIFY(refs->isFloating());QVERIFY(!refs->isHidden());QCOMPARE(window.toolBarArea(rail),Qt::RightToolBarArea);QVERIFY(quick->isHidden());QVERIFY(!dock(window,"animationPanel")->isHidden());QVERIFY(dock(window,"framePropertiesPanel")->isHidden());QCOMPARE(page(window),QString("Cues"));
        const QRect title(refs->frameGeometry().topLeft(),QSize(refs->width(),30));bool reachable=false;for(const auto *screen:QGuiApplication::screens())if(screen->availableGeometry().intersects(title))reachable=true;QVERIFY(reachable);
        QCOMPARE(window.canvas()->project().toJson(),original);QCOMPARE(renderMovieFrame(window.canvas()->project()),pixels);QVERIFY(window.canvas()->document()->history()->isClean());
        capture(window,"workspace-custom-floating");refs->hide();QVERIFY(window.close());
    }
    void saveUpdateAndRestartRetainCustomIdentityAndCompactDrawer(){
        QTemporaryDir directory;const auto settingsPath=directory.filePath("workspace.ini");QString id;QJsonObject original;
        {
            MainWindow window(settingsPath);QVERIFY(prepare(window,directory));original=window.canvas()->project().toJson();QVERIFY(window.applyWorkspace("compact"));window.toggleDrawingControls();
            auto *drawer=window.findChild<QWidget *>("drawingControlsDrawer");auto *drawerTabs=window.findChild<QTabBar *>("drawingDrawerTabs");QVERIFY(drawer&&drawerTabs);drawerTabs->setCurrentIndex(2);settle();QVERIFY(!drawer->isHidden());
            QString error;QVERIFY2(window.saveWorkspaceAs("My compact tools",&error),qPrintable(error));id=window.activeWorkspaceId();const auto first=window.workspaceLibrary()->find(id)->snapshot;QCOMPARE(first.drawerPage,2);QVERIFY(first.compactDrawing&&first.drawerOpen);
            drawerTabs->setCurrentIndex(1);window.toggleDrawingControls();QVERIFY(window.updateCurrentWorkspace(&error));const auto updated=window.workspaceLibrary()->find(id)->snapshot;QCOMPARE(updated.drawerPage,1);QVERIFY(updated.compactDrawing);QVERIFY(!updated.drawerOpen);
            QVERIFY(window.applyWorkspace("animatic"));QVERIFY(window.applyWorkspace(id));settle();QVERIFY(window.compactDrawingWorkspace());QVERIFY(drawer->isHidden());QCOMPARE(drawerTabs->currentIndex(),1);QCOMPARE(window.canvas()->project().toJson(),original);QVERIFY(window.close());
        }
        {
            MainWindow reopened(settingsPath);QVERIFY(reopened.workspaceLibrary()->find(id));QCOMPARE(reopened.activeWorkspaceId(),id);QVERIFY(reopened.compactDrawingWorkspace());QVERIFY(prepare(reopened,directory));QCOMPARE(reopened.activeWorkspaceId(),id);QVERIFY(reopened.compactDrawingWorkspace());
            QCOMPARE(reopened.findChild<QTabBar *>("drawingDrawerTabs")->currentIndex(),1);QVERIFY(reopened.findChild<QWidget *>("drawingControlsDrawer")->isHidden());QCOMPARE(reopened.canvas()->project().toJson(),original);QCOMPARE(reopened.canvas()->document()->history()->count(),0);
            reopened.toggleDrawingControls();QVERIFY(!reopened.findChild<QWidget *>("drawingControlsDrawer")->isHidden());QVERIFY(reopened.close());
        }
    }
    void previousLayoutTogglesArrangementsAndReloadKeepsUnsavedLayoutReachable(){
        QTemporaryDir directory;MainWindow window(directory.filePath("workspace.ini"));QVERIFY(prepare(window,directory));QVERIFY(window.applyWorkspace("illustration"));
        auto *colors=dock(window,"colorsPanel");colors->hide();settle();QVERIFY(window.applyWorkspace("anime"));auto *previous=window.findChild<QAction *>("previousWorkspaceAction");QVERIFY(previous&&previous->isEnabled());
        previous->trigger();settle();QCOMPARE(window.activeWorkspaceId(),QString("illustration"));QVERIFY(colors->isHidden());QVERIFY(dock(window,"animationPanel")->isHidden());
        previous->trigger();settle();QCOMPARE(window.activeWorkspaceId(),QString("anime"));QVERIFY(!dock(window,"animationPanel")->isHidden());
        QString error;QVERIFY(window.saveWorkspaceAs("My animation layout",&error));const auto id=window.activeWorkspaceId();dock(window,"animationPanel")->hide();
        auto *reload=window.findChild<QAction *>("reloadWorkspaceAction");QVERIFY(reload);reload->trigger();settle();QCOMPARE(window.activeWorkspaceId(),id);QVERIFY(!dock(window,"animationPanel")->isHidden());
        previous->trigger();settle();QCOMPARE(window.activeWorkspaceId(),id);QVERIFY(dock(window,"animationPanel")->isHidden());QCOMPARE(window.canvas()->document()->history()->count(),0);QVERIFY(window.close());
    }
    void damagedQtLayoutRollsBackWithoutLosingCompactDrawerOrArtwork(){
        QTemporaryDir directory;MainWindow window(directory.filePath("workspace.ini"));QVERIFY(prepare(window,directory));QVERIFY(window.applyWorkspace("compact"));window.toggleDrawingControls();auto *tabs=window.findChild<QTabBar *>("drawingDrawerTabs");tabs->setCurrentIndex(2);settle();
        const auto layout=arrangement(window);const auto canvasArea=window.canvas()->geometry();const auto project=window.canvas()->project().toJson();const auto image=renderMovieFrame(window.canvas()->project());
        WorkspaceSnapshot invalid;invalid.state=QByteArray::fromHex("000000ff00000003ff");QVERIFY(validateWorkspaceSnapshot(invalid).isEmpty());QString id,error;QVERIFY(window.workspaceLibrary()->add("Unreadable layout",invalid,&id,&error));
        QVERIFY(!window.applyWorkspace(id));settle();QCOMPARE(window.activeWorkspaceId(),QString("compact"));QVERIFY(window.compactDrawingWorkspace());QVERIFY(!window.findChild<QWidget *>("drawingControlsDrawer")->isHidden());QCOMPARE(tabs->currentIndex(),2);
        // Qt may normalize cached sizes of hidden docks when their widgets
        // reattach. Compare the artist's arrangement and drawing area instead.
        QCOMPARE(arrangement(window),layout);QCOMPARE(window.canvas()->geometry(),canvasArea);QCOMPARE(window.canvas()->project().toJson(),project);QCOMPARE(renderMovieFrame(window.canvas()->project()),image);QCOMPARE(window.canvas()->document()->history()->count(),0);QVERIFY(window.close());
    }
    void activeGesturesRejectSwitchSaveAndUpdate_data(){QTest::addColumn<int>("kind");QTest::newRow("pen-contact")<<0;QTest::newRow("pixel-transform")<<1;QTest::newRow("layer-opacity-draft")<<2;}
    void activeGesturesRejectSwitchSaveAndUpdate(){
        QFETCH(int,kind);QTemporaryDir directory;MainWindow window(directory.filePath("workspace.ini"));QVERIFY(prepare(window,directory));QVERIFY(window.applyWorkspace("anime"));QString error;QVERIFY(window.saveWorkspaceAs("Protected layout",&error));const auto id=window.activeWorkspaceId();
        auto *document=window.canvas()->document();
        if(kind==0)QVERIFY(document->beginStroke({200,160},1.,window.canvas()->brush,false));
        else if(kind==1){QVERIFY(document->beginTransform());TransformParameters parameters;parameters.translation={35,18};QVERIFY(document->previewTransform(parameters));}
        else{QVERIFY(document->beginLayerPropertyEdit());document->previewLayerOpacity(.45);}
        const auto matrix=document->transformMatrix();const int count=document->history()->count();const auto entries=window.workspaceLibrary()->entries();
        QVERIFY(!window.applyWorkspace("animatic"));QVERIFY(!window.saveWorkspaceAs("Must not save",&error));QVERIFY(!window.updateCurrentWorkspace(&error));QCOMPARE(window.activeWorkspaceId(),id);QCOMPARE(window.workspaceLibrary()->entries(),entries);QCOMPARE(document->history()->count(),count);
        if(kind==0){QVERIFY(document->strokeActive());document->cancelStroke();}else if(kind==1){QVERIFY(document->transformActive());QCOMPARE(document->transformMatrix(),matrix);document->cancelTransform();}else{QVERIFY(document->layerPropertyEditActive());document->cancelLayerPropertyEdit();}
        document->history()->setClean();QVERIFY(window.applyWorkspace("animatic"));QVERIFY(window.close());
    }
    void unfinishedDrawingNameBlocksWorkspaceChangesWithoutCommitting(){
        QTemporaryDir directory;Document source;source.load(fixture());QVERIFY(source.addAnimationTrack("Character",DrawingTemplate::Simple));
        const auto trackId=source.project().shots[0].animationTracks.first().id;
        const auto groupId=source.project().shots[0].animationTracks.first().drawings.first().groupLayerId;
        QString error;QVERIFY2(saveProject(source.project(),directory.filePath("drawing.toonproj"),&error),qPrintable(error));
        MainWindow window(directory.filePath("workspace.ini"));QVERIFY(prepare(window,directory));QVERIFY(window.applyWorkspace("anime"));
        auto *controller=window.findChild<TimelineController *>();QVERIFY(controller);QVERIFY(controller->selectTrack(trackId));
        auto *frames=dock(window,"framePropertiesPanel");frames->show();frames->raise();QVERIFY(selectPage(window,"Drawing"));
        QVERIFY(window.saveWorkspaceAs("Drawing review",&error));const auto workspaceId=window.activeWorkspaceId();
        auto *name=frames->findChild<QLineEdit *>("frameDrawingName");QVERIFY(name&&name->isEnabled());
        auto *document=window.canvas()->document();const auto project=document->project().toJson();const auto image=renderMovieFrame(document->project());const auto entries=window.workspaceLibrary()->entries();
        QCOMPARE(document->history()->count(),0);name->setFocus();QTRY_VERIFY(name->hasFocus());name->selectAll();QTest::keyClicks(name,"A001 clean line art");
        QVERIFY(controller->hasPendingEdits());QVERIFY(!document->editingGestureActive());QVERIFY(!document->layerPropertyEditActive());
        QVERIFY(!window.applyWorkspace("illustration"));QVERIFY(!window.saveWorkspaceAs("Unfinished layout",&error));QVERIFY(!window.updateCurrentWorkspace(&error));settle();
        QCOMPARE(window.activeWorkspaceId(),workspaceId);QCOMPARE(window.workspaceLibrary()->entries(),entries);QCOMPARE(name->text(),QString("A001 clean line art"));QVERIFY(controller->hasPendingEdits());
        QCOMPARE(document->project().toJson(),project);QCOMPARE(renderMovieFrame(document->project()),image);QCOMPARE(document->history()->count(),0);QVERIFY(document->history()->isClean());
        QTest::keyClick(name,Qt::Key_Return);QVERIFY(!controller->hasPendingEdits());QCOMPARE(document->history()->count(),1);
        QString drawingName;for(const auto &layer:document->project().shots[0].layers)if(layer.id==groupId)drawingName=layer.name;QCOMPARE(drawingName,QString("A001 clean line art"));QCOMPARE(renderMovieFrame(document->project()),image);
        QVERIFY(window.applyWorkspace("illustration"));QCOMPARE(document->history()->count(),1);document->history()->setClean();QVERIFY(window.close());
    }
    void restoringCameraInspectorKeepsTimelineSelection_data(){QTest::addColumn<bool>("keys");QTest::newRow("selected-cel")<<false;QTest::newRow("selected-scalar-key")<<true;}
    void restoringCameraInspectorKeepsTimelineSelection(){
        QFETCH(bool,keys);QTemporaryDir directory;Document source;source.load(fixture());QVERIFY(source.addAnimationTrack("Character",DrawingTemplate::Simple));
        auto project=source.project();auto &shot=project.shots[0];const auto track=shot.animationTracks.first();const auto layerId=shot.layers.first().id;
        shot.layerAnimations={{layerId,{},{{KeyframeProperty::PositionX,{{0,0},{8,20}}}}, {}}};
        QString error;QVERIFY2(saveProject(project,directory.filePath("drawing.toonproj"),&error),qPrintable(error));
        MainWindow window(directory.filePath("workspace.ini"));QVERIFY(prepare(window,directory));QVERIFY(window.applyWorkspace("anime"));
        auto *controller=window.findChild<TimelineController *>();QVERIFY(controller);auto *frames=dock(window,"framePropertiesPanel");frames->show();frames->raise();
        QVERIFY(selectPage(window,"Camera"));QVERIFY(controller->cameraSelected());QVERIFY(window.saveWorkspaceAs("Camera inspection",&error));const auto workspaceId=window.activeWorkspaceId();
        QVERIFY(selectPage(window,"Drawing"));
        if(keys){QVERIFY(controller->selectPropertyLayer(layerId));controller->setSelectedKeys({{layerId,KeyframeProperty::PositionX,{},8}});QCOMPARE(controller->selectedKeys().size(),1);}
        else{QVERIFY(controller->selectTrack(track.id));controller->setSelectedCels({{track.id,track.exposures.first().id}});QCOMPARE(controller->selectedCels().size(),1);}
        QVERIFY(!controller->cameraSelected());const auto cels=controller->selectedCels();const auto selectedKeys=controller->selectedKeys();const auto selectedTrack=controller->selectedTrackId();const auto selectedProperty=controller->selectedPropertyLayerId();
        auto *document=window.canvas()->document();const auto before=document->project().toJson();const auto pixels=renderMovieFrame(document->project());const int activeLayer=document->activeLayer();const int frame=controller->currentFrame();QCOMPARE(document->history()->count(),0);
        QVERIFY(window.applyWorkspace(workspaceId));settle();QCOMPARE(page(window),QString("Camera"));
        auto *pages=window.findChild<QStackedWidget *>("framePropertiesPages");auto *camera=frames->findChild<QWidget *>("cameraProperties");QVERIFY(pages&&camera);QCOMPARE(pages->currentWidget(),camera);QVERIFY(camera->isVisible());
        QVERIFY(!controller->cameraSelected());QCOMPARE(controller->selectedCels(),cels);QCOMPARE(controller->selectedKeys(),selectedKeys);QCOMPARE(controller->selectedTrackId(),selectedTrack);QCOMPARE(controller->selectedPropertyLayerId(),selectedProperty);QCOMPARE(document->activeLayer(),activeLayer);QCOMPARE(controller->currentFrame(),frame);
        QCOMPARE(document->project().toJson(),before);QCOMPARE(renderMovieFrame(document->project()),pixels);QCOMPARE(document->history()->count(),0);QVERIFY(document->history()->isClean());QVERIFY(window.close());
    }
    void saveDialogValidatesInlineAndCancelKeepsExistingWorkspaces(){
        QTemporaryDir directory;MainWindow window(directory.filePath("workspace.ini"));QVERIFY(prepare(window,directory));QVERIFY(window.applyWorkspace("anime"));QString error;QVERIFY(window.saveWorkspaceAs("Existing layout",&error));
        const auto existingId=window.activeWorkspaceId();const auto entries=window.workspaceLibrary()->entries();const auto project=window.canvas()->project().toJson();auto *save=window.findChild<QAction *>("saveWorkspaceAsAction");QVERIFY(save&&save->isEnabled());
        const auto saveError=invokeModal(save,"saveWorkspaceDialog",[&](QDialog &modal){
            auto *dialog=&modal;
            auto *name=dialog->findChild<QLineEdit *>("saveWorkspaceName");auto *message=dialog->findChild<QLabel *>("workspaceNameError");auto *confirm=dialog->findChild<QPushButton *>("saveWorkspaceConfirm");QVERIFY(name&&message&&confirm);
            for(const auto &invalid:QStringList{"   ","Anime","EXISTING LAYOUT",QString(101,'a')}){
                name->setText(invalid);confirm->click();settle();QVERIFY(dialog->isVisible());QVERIFY(message->isVisible());QVERIFY(!message->text().isEmpty());QCOMPARE(name->text(),invalid);QCOMPARE(window.workspaceLibrary()->entries(),entries);QCOMPARE(window.activeWorkspaceId(),existingId);
            }
            name->setFocus();name->selectAll();QTest::keyClicks(name,"  My corrected layout  ");confirm->click();QVERIFY(!dialog->isVisible());QCOMPARE(dialog->result(),int(QDialog::Accepted));
        });
        QVERIFY2(saveError.isEmpty(),qPrintable(saveError));QTRY_VERIFY2(window.canvas()->hasFocus(),qPrintable(focusState(window)));QCOMPARE(window.workspaceLibrary()->entries().size(),entries.size()+1);const auto createdId=window.activeWorkspaceId();QVERIFY(createdId!=existingId);QCOMPARE(window.workspaceLibrary()->find(createdId)->name,QString("My corrected layout"));QCOMPARE(window.workspaceLibrary()->find(existingId)->snapshot,entries.first().snapshot);
        const auto afterSave=window.workspaceLibrary()->entries();
        const auto cancelError=invokeModal(save,"saveWorkspaceDialog",[&](QDialog &modal){
            auto *dialog=&modal;
            auto *name=dialog->findChild<QLineEdit *>("saveWorkspaceName");auto *buttons=dialog->findChild<QDialogButtonBox *>();QVERIFY(name&&buttons);name->setText("Cancelled layout");buttons->button(QDialogButtonBox::Cancel)->click();QCOMPARE(dialog->result(),int(QDialog::Rejected));
        });
        QVERIFY2(cancelError.isEmpty(),qPrintable(cancelError));QTRY_VERIFY2(window.canvas()->hasFocus(),qPrintable(focusState(window)));QCOMPARE(window.workspaceLibrary()->entries(),afterSave);QCOMPARE(window.activeWorkspaceId(),createdId);QCOMPARE(window.canvas()->project().toJson(),project);QCOMPARE(window.canvas()->document()->history()->count(),0);QVERIFY(window.close());
    }
    void managerReturnsToCanvasAfterUseOrClose_data(){QTest::addColumn<bool>("use");QTest::newRow("use-saved-workspace")<<true;QTest::newRow("close-manager")<<false;}
    void managerReturnsToCanvasAfterUseOrClose(){
        QFETCH(bool,use);QTemporaryDir directory;MainWindow window(directory.filePath("workspace.ini"));QVERIFY(prepare(window,directory));QVERIFY(window.applyWorkspace("anime"));QString error;QVERIFY(window.saveWorkspaceAs("My timing layout",&error));const auto id=window.activeWorkspaceId();QVERIFY(window.applyWorkspace("illustration"));
        const auto project=window.canvas()->project().toJson();const auto entries=window.workspaceLibrary()->entries();auto *manage=window.findChild<QAction *>("manageWorkspacesAction");QVERIFY(manage);
        error=invokeModal(manage,"workspaceManagerDialog",[&](QDialog &dialog){
            auto *list=dialog.findChild<QListWidget *>("workspaceList");QVERIFY(list);QCOMPARE(list->count(),1);QCOMPARE(list->item(0)->data(Qt::UserRole).toString(),id);list->setCurrentRow(0);
            if(use){auto *button=dialog.findChild<QPushButton *>("workspaceUse");QVERIFY(button&&button->isEnabled());button->click();}
            else{auto *buttons=dialog.findChild<QDialogButtonBox *>();QVERIFY(buttons);buttons->button(QDialogButtonBox::Close)->click();}
        });
        QVERIFY2(error.isEmpty(),qPrintable(error));QTRY_VERIFY2(window.canvas()->hasFocus(),qPrintable(focusState(window)));QCOMPARE(window.activeWorkspaceId(),use?id:QString("illustration"));QCOMPARE(window.workspaceLibrary()->entries(),entries);QCOMPARE(window.canvas()->project().toJson(),project);QCOMPARE(window.canvas()->document()->history()->count(),0);QVERIFY(window.close());
    }
    void modalClosureDoesNotTakeFocusFromAnotherWindow_data(){
        QTest::addColumn<bool>("manage");QTest::addColumn<bool>("accept");QTest::newRow("save")<<false<<true;QTest::newRow("cancel-save")<<false<<false;QTest::newRow("use-workspace")<<true<<true;QTest::newRow("close-manager")<<true<<false;
    }
    void modalClosureDoesNotTakeFocusFromAnotherWindow(){
        QFETCH(bool,manage);QFETCH(bool,accept);QTemporaryDir directory;MainWindow window(directory.filePath("workspace.ini"));QVERIFY(prepare(window,directory));QString error;QVERIFY(window.saveWorkspaceAs("Existing layout",&error));const auto project=window.canvas()->project().toJson();
        QWidget other;other.setObjectName("otherWorkspace");auto *layout=new QVBoxLayout(&other);auto *text=new QLineEdit;layout->addWidget(text);
        auto *action=window.findChild<QAction *>(manage?"manageWorkspacesAction":"saveWorkspaceAsAction");QVERIFY(action);
        error=invokeModal(action,manage?"workspaceManagerDialog":"saveWorkspaceDialog",[&](QDialog &dialog){
            connect(&dialog,&QDialog::finished,&other,[&]{other.show();QApplication::setActiveWindow(&other);text->setFocus();});
            if(accept){auto *button=dialog.findChild<QPushButton *>(manage?"workspaceUse":"saveWorkspaceConfirm");QVERIFY(button&&button->isEnabled());button->click();}
            else dialog.reject();
        });
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(QApplication::activeWindow(),&other);QVERIFY(text->hasFocus());QVERIFY(!window.canvas()->hasFocus());QCOMPARE(window.canvas()->project().toJson(),project);QCOMPARE(window.canvas()->document()->history()->count(),0);QVERIFY(window.close());
    }
};
QTEST_MAIN(WorkspacePresetsTests)
#include "workspace_presets_tests.moc"
