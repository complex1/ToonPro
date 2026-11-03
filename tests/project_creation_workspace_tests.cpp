#include "canvas.h"
#include "main_window.h"
#include "project_dialog.h"
#include "project_templates.h"
#include "ui_theme.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include <QtTest>
#include <functional>

using namespace toon;
namespace {
void settle() { QCoreApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest); }
bool expose(MainWindow &window) {
    const auto available=window.screen()->availableGeometry();
    window.resize(QSize(1320,900).boundedTo(available.size()-QSize(40,60)));
    window.move(available.topLeft()+QPoint(20,40));window.show();
    if(!QTest::qWaitForWindowExposed(&window))return false;
    window.raise();window.activateWindow();return QTest::qWaitForWindowActive(&window);
}
QDockWidget *dock(MainWindow &window,const char *name) { return window.findChild<QDockWidget *>(name); }
QMap<QString,QString> layout(MainWindow &window) {
    QMap<QString,QString> result;
    for(auto *panel:window.findChildren<QDockWidget *>()) {
        QStringList peers;for(auto *peer:window.tabifiedDockWidgets(panel))peers.append(peer->objectName());peers.sort();
        result.insert(panel->objectName(),QString("%1|%2|%3|%4").arg(panel->isHidden()).arg(int(window.dockWidgetArea(panel))).arg(panel->isFloating()).arg(peers.join(',')));
    }
    return result;
}
QString choose(ProjectDialog &dialog,const QString &id,const QString &path) {
    auto *tabs=dialog.findChild<QTabBar *>("projectTemplateTabs");auto *location=dialog.findChild<QLineEdit *>("projectPath");
    if(!tabs||!location)return "New Project is missing its template or save-location control.";
    for(int i=0;i<tabs->count();++i)if(tabs->tabData(i).toString()==id){tabs->setCurrentIndex(i);location->setText(path);return {};}
    return "The requested template is missing: "+id;
}
// Exercise the real QAction -> exec() -> submit path, and always dismiss a
// dialog when setup fails so an assertion cannot strand the test in exec().
QString interact(MainWindow &window,const char *actionName,const std::function<QString(ProjectDialog &)> &configure,bool accept=true) {
    auto *action=window.findChild<QAction *>(actionName);if(!action||!action->isEnabled())return QString("Unavailable action: %1").arg(actionName);
    QString error;bool seen=false;QTimer responder,timeout;responder.setInterval(5);timeout.setSingleShot(true);
    QObject::connect(&responder,&QTimer::timeout,&window,[&]{
        auto *dialog=qobject_cast<ProjectDialog *>(QApplication::activeModalWidget());
        if(!dialog){if(auto *message=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())){error=message->text();responder.stop();message->reject();}return;}
        // Wait for native presentation, as real pointer/keyboard input does.
        // Rejecting an unexposed Cocoa modal does not model a user dismissal.
        if(!dialog->windowHandle()||!dialog->windowHandle()->isExposed()||!dialog->isActiveWindow())return;
        responder.stop();
        seen=true;error=configure(*dialog);
        if(!error.isEmpty()||!accept){dialog->reject();return;}
        auto *submit=dialog->findChild<QPushButton *>("submitProject");
        if(!submit||!submit->isEnabled()){error="Project submission is unavailable.";dialog->reject();return;}
        submit->click();
        if(dialog->isVisible()){error="Project submission did not accept the dialog.";if(auto *validation=dialog->findChild<QLabel *>("validation"))error+=' '+validation->text();dialog->reject();}
    });
    QObject::connect(&timeout,&QTimer::timeout,&window,[&]{
        error="The project dialog did not finish native presentation or submission.";
        if(auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()))dialog->reject();
    });
    responder.start();timeout.start(20000);action->trigger();responder.stop();timeout.stop();settle();
    return !seen&&error.isEmpty()?QString("The project dialog was not visited."):error;
}
Project existingMovie() {
    auto project=Project::create();project.width=320;project.height=240;
    project.shots.append(Project::create().shots[0]);project.shots[1].number="C002";project.shots[1].name="Existing second shot";
    for(int i=0;i<project.shots.size();++i){
        auto &shot=project.shots[i];shot.durationFrames=48+i*12;
        QImage image(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);image.setPixelColor(18+i,23,Qt::red);
        shot.layers[0].tiles.insert({0,0},image);
    }
    return project;
}
}

class ProjectCreationWorkspaceTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void newProjectUsesChosenWorkspaceAndSelectsPaintableDrawing_data(){
        QTest::addColumn<QString>("templateId");QTest::addColumn<QString>("workspaceId");QTest::addColumn<QString>("layerName");
        QTest::newRow("blank")<<QString("blank")<<QString("illustration")<<QString("Drawing");
        QTest::newRow("illustration")<<QString("illustration")<<QString("illustration")<<QString("Sketch");
        QTest::newRow("anime")<<QString("anime")<<QString("anime")<<QString("Rough");
        QTest::newRow("storyboard")<<QString("storyboard")<<QString("storyboard")<<QString("Rough");
        QTest::newRow("animatic")<<QString("animatic")<<QString("animatic")<<QString("Rough");
    }
    void newProjectUsesChosenWorkspaceAndSelectsPaintableDrawing(){
        QFETCH(QString,templateId);QFETCH(QString,workspaceId);QFETCH(QString,layerName);
        QTemporaryDir directory;MainWindow window(directory.filePath("workspace.ini"));QVERIFY(expose(window));QVERIFY(!window.canvas()->hasProject());
        const auto path=directory.filePath(templateId+".toonproj");
        const auto error=interact(window,"newProjectAction",[&](ProjectDialog &dialog){return choose(dialog,templateId,path);});QVERIFY2(error.isEmpty(),qPrintable(error));
        auto *canvas=window.canvas();auto *document=canvas->document();QVERIFY(canvas->hasProject());QCOMPARE(window.activeWorkspaceId(),workspaceId);QCOMPARE(document->currentFrame(),0);QVERIFY(document->history()->isClean());
        QTRY_COMPARE(QApplication::focusWidget(),canvas);
        const int active=document->activeLayer();const auto selected=canvas->shot().layers[active].id;
        QCOMPARE(canvas->shot().layers[active].name,layerName);QVERIFY(!canvas->shot().layers[active].group);QCOMPARE(canvas->shot().layers[active].kind,LayerKind::Raster);QVERIFY(activeLayerIds(canvas->shot(),0).contains(selected));
        QVERIFY(!dock(window,"layersPanel")->isHidden());QCOMPARE(dock(window,"animationPanel")->isHidden(),workspaceId!="anime"&&workspaceId!="animatic");
        QCOMPARE(dock(window,"framePropertiesPanel")->isHidden(),workspaceId!="anime"&&workspaceId!="animatic");
        if(workspaceId=="storyboard"||workspaceId=="animatic"){QVERIFY(!dock(window,"shotsPanel")->isHidden());QCOMPARE(window.dockWidgetArea(dock(window,"shotsPanel")),Qt::LeftDockWidgetArea);}
        if(workspaceId=="anime"||workspaceId=="animatic")QCOMPARE(window.dockWidgetArea(dock(window,"animationPanel")),Qt::BottomDockWidgetArea);
        QString loadError;const auto saved=loadProject(path,&loadError);QVERIFY2(saved,qPrintable(loadError));QCOMPARE(saved->toJson(),canvas->project().toJson());
        // The first contact must paint the selected rough/sketch layer rather
        // than the static background or an unpaintable animation folder.
        canvas->setTool(Canvas::Tool::Brush);canvas->setBrushSize(8);canvas->setBrushColor(Qt::black);canvas->setStabilization(0);canvas->setHoldShapeEnabled(false);
        QVERIFY(canvas->toolBlockReason().isEmpty());canvas->fitSheet();settle();
        const auto point=canvas->viewTransform().map(sheetGeometry(canvas->project(),canvas->shot()).output.center()).toPoint();QVERIFY(canvas->rect().contains(point));
        QTest::mouseMove(canvas,point);QTest::mouseClick(canvas,Qt::LeftButton,Qt::NoModifier,point);QTRY_COMPARE(document->history()->count(),1);
        QVERIFY(!canvas->shot().layers[active].tiles.isEmpty());for(int i=0;i<canvas->shot().layers.size();++i)if(i!=active)QVERIFY(canvas->shot().layers[i].tiles.isEmpty());
        document->history()->undo();QVERIFY(document->history()->isClean());QCOMPARE(canvas->project().toJson(),saved->toJson());QVERIFY(window.close());
    }
    void keepCurrentRetainsCustomLayoutOnFirstProject(){
        QTemporaryDir directory;MainWindow window(directory.filePath("workspace.ini"));QVERIFY(expose(window));QVERIFY(window.applyWorkspace("animatic"));
        dock(window,"referencesPanel")->hide();dock(window,"effectsPanel")->show();window.addDockWidget(Qt::LeftDockWidgetArea,dock(window,"effectsPanel"));settle();
        QString error;QVERIFY2(window.saveWorkspaceAs("My production desk",&error),qPrintable(error));const auto id=window.activeWorkspaceId();const auto before=layout(window);const auto entries=window.workspaceLibrary()->entries();
        const auto path=directory.filePath("blank-in-my-workspace.toonproj");
        error=interact(window,"newProjectAction",[&](ProjectDialog &dialog){
            auto failure=choose(dialog,"blank",path);if(!failure.isEmpty())return failure;
            auto *workspace=dialog.findChild<QComboBox *>("projectWorkspace");if(!workspace)return QString("Missing workspace selector.");workspace->setCurrentIndex(workspace->findData(QString()));return QString();
        });QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(window.activeWorkspaceId(),id);QCOMPARE(layout(window),before);QCOMPARE(window.workspaceLibrary()->entries(),entries);QVERIFY(!window.canvas()->shot().sheet.showHeader);QVERIFY(!window.canvas()->shot().sheet.showGuides);QCOMPARE(window.canvas()->shot().layers.size(),1);QTRY_COMPARE(QApplication::focusWidget(),window.canvas());QVERIFY(window.close());
    }
    void cancelNewProjectPreservesExistingArtworkAndWorkspace(){
        QTemporaryDir directory;const auto project=existingMovie();const auto path=directory.filePath("existing.toonproj");QString error;QVERIFY2(saveProject(project,path,&error),qPrintable(error));
        MainWindow window(directory.filePath("workspace.ini"));QVERIFY(window.openProject(path));QVERIFY(expose(window));QVERIFY(window.applyWorkspace("storyboard"));
        const auto before=layout(window);const auto createdPath=directory.filePath("cancelled.toonproj");
        error=interact(window,"newProjectAction",[&](ProjectDialog &dialog){return choose(dialog,"animatic",createdPath);},false);QVERIFY2(error.isEmpty(),qPrintable(error));
        QVERIFY(!QFileInfo::exists(createdPath));QCOMPARE(window.canvas()->project().toJson(),project.toJson());QCOMPARE(window.canvas()->shot().layers[0].tiles,project.shots[0].layers[0].tiles);QCOMPARE(window.activeWorkspaceId(),QString("storyboard"));QCOMPARE(layout(window),before);QCOMPARE(window.canvas()->document()->history()->count(),0);QTRY_COMPARE(QApplication::focusWidget(),window.canvas());QVERIFY(window.close());
    }
    void settingsEditsOnlyCurrentShotAndDoesNotApplyAStarterOrWorkspace(){
        QTemporaryDir directory;const auto original=existingMovie();const auto path=directory.filePath("existing.toonproj");QString error;QVERIFY2(saveProject(original,path,&error),qPrintable(error));
        MainWindow window(directory.filePath("workspace.ini"));QVERIFY(window.openProject(path));QVERIFY(expose(window));QVERIFY(window.applyWorkspace("animatic"));
        auto *document=window.canvas()->document();QVERIFY(document->selectShot(original.shots[1].id));QVERIFY(document->setCurrentFrame(3));const auto before=layout(window);
        error=interact(window,"projectSettingsAction",[](ProjectDialog &dialog){
            if(dialog.findChild<QTabBar *>("projectTemplateTabs")||dialog.findChild<QComboBox *>("projectWorkspace"))return QString("Existing project settings must not apply starter templates or workspaces.");
            auto *name=dialog.findChild<QLineEdit *>("shotName");auto *guides=dialog.findChild<QCheckBox *>("projectShowGuides");if(!name||!guides)return QString("Missing shot settings.");
            name->setText("Revised second cut");guides->setChecked(false);return QString();
        });QVERIFY2(error.isEmpty(),qPrintable(error));
        auto expected=original;expected.shots[1].name="Revised second cut";expected.shots[1].sheet.showGuides=false;
        QCOMPARE(window.canvas()->project().toJson(),expected.toJson());QCOMPARE(document->activeShotId(),original.shots[1].id);QCOMPARE(document->currentFrame(),3);QCOMPARE(window.activeWorkspaceId(),QString("animatic"));QCOMPARE(layout(window),before);QCOMPARE(document->history()->count(),1);QTRY_COMPARE(QApplication::focusWidget(),window.canvas());
        for(int i=0;i<original.shots.size();++i)QCOMPARE(window.canvas()->project().shots[i].layers[0].tiles,original.shots[i].layers[0].tiles);
        document->history()->undo();QCOMPARE(window.canvas()->project().toJson(),original.toJson());QVERIFY(document->history()->isClean());QVERIFY(window.close());
    }
    void cancelFirstProjectRestoresWelcomeFocus(){
        QTemporaryDir directory;MainWindow window(directory.filePath("workspace.ini"));QVERIFY(expose(window));
        const auto error=interact(window,"newProjectAction",[](ProjectDialog &){return QString();},false);QVERIFY2(error.isEmpty(),qPrintable(error));
        QVERIFY(!window.canvas()->hasProject());QTRY_COMPARE(QApplication::focusWidget(),window.findChild<QPushButton *>("newProjectButton"));QVERIFY(window.close());
    }
    void cancelSettingsRetainsArtworkAndRestoresCanvasFocus(){
        QTemporaryDir directory;const auto original=existingMovie();const auto path=directory.filePath("existing.toonproj");QString error;QVERIFY(saveProject(original,path,&error));
        MainWindow window(directory.filePath("workspace.ini"));QVERIFY(window.openProject(path));QVERIFY(expose(window));
        error=interact(window,"projectSettingsAction",[](ProjectDialog &dialog){dialog.findChild<QLineEdit *>("shotName")->setText("Cancelled draft");return QString();},false);QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(window.canvas()->project().toJson(),original.toJson());QCOMPARE(window.canvas()->document()->history()->count(),0);QTRY_COMPARE(QApplication::focusWidget(),window.canvas());QVERIFY(window.close());
    }
    void projectDialogsRespectAnotherWindowFocus_data(){
        QTest::addColumn<QByteArray>("actionName");QTest::addColumn<bool>("accept");
        QTest::newRow("new-accept")<<QByteArray("newProjectAction")<<true;
        QTest::newRow("new-cancel")<<QByteArray("newProjectAction")<<false;
        QTest::newRow("settings-accept")<<QByteArray("projectSettingsAction")<<true;
        QTest::newRow("settings-cancel")<<QByteArray("projectSettingsAction")<<false;
    }
    void projectDialogsRespectAnotherWindowFocus(){
        QFETCH(QByteArray,actionName);QFETCH(bool,accept);
        QTemporaryDir directory;const auto original=existingMovie();const auto path=directory.filePath("existing.toonproj");QString error;QVERIFY(saveProject(original,path,&error));
        QWidget other;other.setObjectName("otherWorkspace");auto *box=new QVBoxLayout(&other);auto *editor=new QLineEdit;box->addWidget(editor);
        MainWindow window(directory.filePath("workspace.ini"));QVERIFY(window.openProject(path));QVERIFY(expose(window));
        error=interact(window,actionName.constData(),[&](ProjectDialog &dialog){
            if(actionName=="newProjectAction"){const auto failure=choose(dialog,"blank",directory.filePath("new.toonproj"));if(!failure.isEmpty())return failure;}
            // Model an explicit destination chosen as the modal closes, after
            // its pre-hide ownership has been recorded by done().
            QObject::connect(&dialog,&QDialog::finished,&other,[&]{other.show();QApplication::setActiveWindow(&other);editor->setFocus();});
            return QString();
        },accept);QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(QApplication::activeWindow(),&other);QVERIFY(editor->hasFocus());QVERIFY(!window.canvas()->hasFocus());
        if(actionName=="newProjectAction"&&accept){QCOMPARE(window.canvas()->shot().layers.size(),1);QVERIFY(!window.canvas()->shot().sheet.showHeader);}
        else QCOMPARE(window.canvas()->project().toJson(),original.toJson());
        QVERIFY(window.canvas()->document()->history()->isClean());QVERIFY(window.close());
    }
};
QTEST_MAIN(ProjectCreationWorkspaceTests)
#include "project_creation_workspace_tests.moc"
