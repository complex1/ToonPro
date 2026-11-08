#include "feature_flags.h"
#include "reference3d.h"
#include "reference3d_review.h"
#include "reference3d_layer_panel.h"
#include "reference3d_canvas.h"
#include "document.h"
#include "brush_library.h"
#include "camera_panel.h"
#include "canvas.h"
#include "main_window.h"
#include "project_workspace_window.h"
#include "scene3d_cache.h"
#include "scene3d_controller.h"
#include "scene3d_panel.h"
#include "scene_model_io.h"
#include "timeline_controller.h"
#include "tool_catalog.h"
#include "tool_session.h"
#include "ui_theme.h"
#include <QDockWidget>
#include <QAction>
#include <QDir>
#include <QAbstractButton>
#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QtTest>

using namespace toon;
namespace {
QJsonObject readJson(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
bool writeJson(const QString &path, const QJsonObject &root) {
    QFile file(path);
    const auto bytes = QJsonDocument(root).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool sceneTool(Canvas::Tool tool) {
    return tool == Canvas::Tool::SceneObject || tool == Canvas::Tool::SceneCamera || tool == Canvas::Tool::SceneRig;
}
struct ReferenceCloseResponse {
    QTimer timer;
    int prompts = 0;
    QString unexpectedTitle;
    QPointer<QMessageBox> answered;
    explicit ReferenceCloseResponse(QMessageBox::StandardButton answer) {
        timer.setInterval(10);
        QObject::connect(&timer, &QTimer::timeout, [this, answer] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box) {
                if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                    unexpectedTitle = dialog->windowTitle(); dialog->reject();
                }
                return;
            }
            if (box == answered) return;
            // macOS intentionally ignores QMessageBox window titles.
            if (box->text() != "Save this reference scene before continuing?") {
                unexpectedTitle = box->text();
                if (auto *cancel=box->button(QMessageBox::Cancel)) cancel->click(); else box->reject();
                return;
            }
            if (auto *button = box->button(answer)) { answered=box; ++prompts; button->click(); }
        });
        timer.start();
    }
};
}

class Experimental3DTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        applyTheme();
        QVERIFY(!features::legacy3DEditor);
    }
    void flagExposesOnlyTheNewReferenceWorkspace() {
        QTemporaryDir directory;
        MainWindow window(directory.filePath("workspace.ini"));
        auto project=Project::create(ProjectKind::DrawingOnly);project.width=320;project.height=240;
        window.canvas()->setProject(project);
        window.findChild<QAction *>("studioDrawingAction")->trigger();
        window.resize(1280,800);window.show();QCoreApplication::processEvents();
        const auto captureDirectory=qEnvironmentVariable("TOON_REFERENCE3D_APP_CAPTURE_DIR");
        if(!captureDirectory.isEmpty())QVERIFY(QDir().mkpath(captureDirectory));
        auto *action=window.findChild<QAction *>("reference3DReviewAction");
        QCOMPARE(bool(action),features::experimental3D);
        QVERIFY(!window.findChild<QDockWidget *>("modelLibraryPanel"));
        if(!action){QVERIFY(!window.findChild<QDockWidget *>("reference3DLayerDock"));return;}
        action->trigger();
        auto *dock=window.findChild<QDockWidget *>("reference3DLayerDock");QVERIFY(dock);QVERIFY(dock->isVisible());
        auto *panel=dock->findChild<Reference3DLayerPanel *>("reference3DLayerPanel");QVERIFY(panel);
        auto *review=panel->editor();QVERIFY(review);QVERIFY(!review->isWindow());QVERIFY(!review->viewportWidget()->isVisible());
        QVERIFY(!review->findChild<Scene3DPanel *>());
        action->trigger();QCOMPARE(window.findChildren<QDockWidget *>("reference3DLayerDock").size(),1);QCOMPARE(panel->editor(),review);
        QVERIFY(window.findChildren<Reference3DReviewWindow *>("reference3DReviewWindow",Qt::FindDirectChildrenOnly).isEmpty());
        bool inMenu=false,inToolbar=false;for(auto *menu:window.findChildren<QMenu *>())inMenu|=menu->actions().contains(action);for(auto *toolbar:window.findChildren<QToolBar *>())inToolbar|=toolbar->actions().contains(action);QVERIFY(inMenu);QVERIFY(inToolbar);
        auto *document=window.canvas()->document();const int before=document->history()->count();const int originalLayers=document->activeShot().layers.size();QString error;
        QVERIFY2(review->placeCatalogModel("anime/seed-san",&error),qPrintable(error));
        QCOMPARE(document->activeShot().layers.size(),originalLayers+1);const auto layerId=document->activeShot().layers[document->activeLayer()].id;
        QCOMPARE(document->activeShot().layers[document->activeLayer()].kind,LayerKind::Reference3D);QVERIFY(document->activeShot().layers[document->activeLayer()].reference3d);
        QCOMPARE(document->history()->count(),before+1);QCOMPARE(review->undoStack(),document->history());
        QVERIFY(document->activeShot().layers[document->activeLayer()].reference3d->instances.first().asset->humanoidBones.contains("head"));
        QVERIFY(!document->activeShot().layers[document->activeLayer()].reference3d->instances.first().asset->expressions.isEmpty());
        QVERIFY2(review->placeCatalogModel("pet/dog",&error),qPrintable(error));
        QCOMPARE(document->activeShot().layers.size(),originalLayers+1);QCOMPARE(document->activeShot().layers[document->activeLayer()].id,layerId);
        const auto &native=*document->activeShot().layers[document->activeLayer()].reference3d;QCOMPARE(native.instances.size(),2);QCOMPARE(native.instances.last().asset->metadata.value("referencePetKind").toString(),QString("dog"));
        QCOMPARE(serializeReferenceScene(native),serializeReferenceScene(review->scene()));QCOMPARE(document->history()->count(),before+2);
        auto *controller=window.canvas()->reference3D();QVERIFY(controller);QVERIFY(controller->available());QVERIFY(!layerCanPaint(document->activeShot().layers,document->activeLayer()));
        controller->prepareLayerPreviews({project.width,project.height});QTRY_VERIFY_WITH_TIMEOUT(controller->previewReady(),30000);QVERIFY2(controller->previewError().isEmpty(),qPrintable(controller->previewError()));
        if(!captureDirectory.isEmpty()){QVERIFY(window.grab().save(QDir(captureDirectory).filePath("main-app-toolbar.png")));QVERIFY(dock->grab().save(QDir(captureDirectory).filePath("main-app-reference.png")));QVERIFY(window.canvas()->grab().save(QDir(captureDirectory).filePath("main-app-canvas.png")));}
        auto *drawAbove=panel->findChild<QToolButton *>("referenceDrawAbove");QVERIFY(drawAbove);QVERIFY(drawAbove->isEnabled());drawAbove->click();
        QCOMPARE(document->activeShot().layers.size(),originalLayers+2);QCOMPARE(document->activeShot().layers[document->activeLayer()].kind,LayerKind::Raster);QVERIFY(layerCanPaint(document->activeShot().layers,document->activeLayer()));QCOMPARE(window.canvas()->tool(),Canvas::Tool::Brush);QVERIFY(!controller->available());
        QCOMPARE(document->history()->count(),before+3);document->history()->undo();QCOMPARE(document->activeShot().layers.size(),originalLayers+1);QVERIFY(document->activeShot().layers[document->activeLayer()].reference3d);document->history()->redo();QCOMPARE(document->activeShot().layers.size(),originalLayers+2);
        window.markManagedSaved();
    }
    void ordinaryEditorCloseProtectsUnsavedReferenceScenes() {
        if (!features::experimental3D) return;
        QTemporaryDir directory;
        MainWindow window(directory.filePath("workspace.ini")); window.show();
        QPointer<Reference3DReviewWindow> reference=new Reference3DReviewWindow(&window,directory.filePath("reference-materials"));
        reference->setAttribute(Qt::WA_DeleteOnClose);reference->show();
        QString error; QVERIFY2(reference->placeCatalogModel("pet/dog", &error), qPrintable(error));
        const auto source = serializeReferenceScene(reference->scene(), &error); QVERIFY2(!source.isEmpty(), qPrintable(error));
        {
            ReferenceCloseResponse response(QMessageBox::Cancel);
            QVERIFY(!window.close()); QCOMPARE(response.prompts, 1); QVERIFY(response.unexpectedTitle.isEmpty());
        }
        QVERIFY(window.isVisible()); QVERIFY(reference); QVERIFY(reference->isWindowModified());
        QCOMPARE(serializeReferenceScene(reference->scene()), source);
        {
            ReferenceCloseResponse response(QMessageBox::Discard);
            QVERIFY(window.close()); QCOMPARE(response.prompts, 1); QVERIFY(response.unexpectedTitle.isEmpty());
        }
        QVERIFY(!window.isVisible()); QTRY_VERIFY(reference.isNull());
    }
    void parentCloseCanSaveAReferenceSceneAndCleanScenesNeedNoPrompt() {
        if (!features::experimental3D) return;
        QTemporaryDir directory;
        MainWindow window(directory.filePath("workspace.ini")); window.show();
        QPointer<Reference3DReviewWindow> reference=new Reference3DReviewWindow(&window,directory.filePath("reference-materials"));
        reference->setAttribute(Qt::WA_DeleteOnClose);reference->show();
        QString error; const auto path = directory.filePath("reference.toon3d");
        QVERIFY2(reference->placeCatalogModel("pet/dog", &error), qPrintable(error));
        QVERIFY2(reference->saveScene(path, &error), qPrintable(error));
        QVERIFY2(reference->placeCatalogModel("pet/cat", &error), qPrintable(error));
        {
            ReferenceCloseResponse response(QMessageBox::Save);
            QVERIFY(window.close()); QCOMPARE(response.prompts, 1); QVERIFY(response.unexpectedTitle.isEmpty());
        }
        QTRY_VERIFY(reference.isNull()); const auto saved = loadReferenceScene(path, &error); QVERIFY2(saved, qPrintable(error));
        QCOMPARE(saved->instances.size(), 2);
        window.show();reference=new Reference3DReviewWindow(&window,directory.filePath("reference-materials"));
        reference->setAttribute(Qt::WA_DeleteOnClose);reference->show();
        QVERIFY2(reference->openScene(path, &error), qPrintable(error)); QVERIFY(!reference->isWindowModified());
        {
            ReferenceCloseResponse response(QMessageBox::Cancel);
            QVERIFY(window.close()); QCOMPARE(response.prompts, 0); QVERIFY(response.unexpectedTitle.isEmpty());
        }
        QTRY_VERIFY(reference.isNull());
    }
    void managedTabAndProjectCloseProtectHiddenReferenceScenes() {
        if (!features::experimental3D) return;
        QTemporaryDir directory;
        ProjectWorkspaceWindow shell(directory.filePath("management.ini")); QString error;
        QVERIFY2(shell.createManagedProject(directory.filePath("References.toonstudio"), "References", "drawing", &error), qPrintable(error));
        const auto document = newManagedDocument("Drawing", false, 320, 240, Qt::white, false);
        const auto id = shell.store()->createDocument("Drawing", "drawing", document, {}, {}, &error); QVERIFY2(!id.isEmpty(), qPrintable(error));
        QVERIFY(shell.openItems({id})); shell.show();
        QPointer<MainWindow> editor = shell.editorForItem(id); QVERIFY(editor); QVERIFY(!editor->managedDirty());
        QPointer<Reference3DReviewWindow> reference=new Reference3DReviewWindow(editor,directory.filePath("reference-materials"));
        reference->setAttribute(Qt::WA_DeleteOnClose);reference->show();
        QVERIFY2(reference->placeCatalogModel("pet/cat", &error), qPrintable(error));
        const auto source = serializeReferenceScene(reference->scene()); QVERIFY(!source.isEmpty());
        auto *tabs = shell.findChild<QTabWidget *>("documentTabs"); QVERIFY(tabs); QCOMPARE(tabs->count(), 2);
        tabs->setCurrentIndex(0); reference->hide(); QVERIFY(!reference->isVisible()); QVERIFY(!editor->managedDirty());
        {
            ReferenceCloseResponse response(QMessageBox::Cancel);
            tabs->tabCloseRequested(tabs->indexOf(editor));
            QCOMPARE(response.prompts, 1); QVERIFY(response.unexpectedTitle.isEmpty());
        }
        QVERIFY(editor); QVERIFY(reference); QCOMPARE(shell.editorForItem(id), editor.data()); QCOMPARE(tabs->count(), 2);
        QCOMPARE(serializeReferenceScene(reference->scene()), source);
        {
            ReferenceCloseResponse response(QMessageBox::Cancel);
            QVERIFY(!shell.close()); QCOMPARE(response.prompts, 1); QVERIFY(response.unexpectedTitle.isEmpty());
        }
        QVERIFY(shell.isVisible()); QVERIFY(editor); QVERIFY(reference);
        {
            ReferenceCloseResponse response(QMessageBox::Discard);
            tabs->tabCloseRequested(tabs->indexOf(editor));
            QCOMPARE(response.prompts, 1); QVERIFY(response.unexpectedTitle.isEmpty());
        }
        QVERIFY(editor.isNull()); QVERIFY(reference.isNull()); QVERIFY(!shell.editorForItem(id)); QCOMPARE(tabs->count(), 1);
        QVERIFY(shell.close());
    }
    void normalWorkspaceDoesNotExposeLegacy3D() {
        QTemporaryDir directory;
        MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(!window.findChild<QDockWidget *>("modelLibraryPanel"));
        QVERIFY(!window.findChild<Scene3DPanel *>());
        auto *mode = window.findChild<QTabBar *>("cameraMode");
        QVERIFY(mode); QCOMPARE(mode->count(), 1); QVERIFY(mode->isHidden());
        auto *catalog = window.findChild<ToolCatalog *>(); QVERIFY(catalog);
        for (const auto &tool : catalog->availableTools()) QVERIFY(!sceneTool(tool.engine));
        for (const auto &placement : catalog->availableEntries())
            for (const auto &id : placement.tools) QVERIFY(catalog->toolEnabled(id));
    }
    void savedCustomToolsStayStoredButCannotActivate() {
        QTemporaryDir directory;
        BrushLibrary brushes(directory.filePath("brushes.json")); QVERIFY(brushes.load());
        const auto path = directory.filePath("tools.json");
        ToolCatalog catalog(&brushes, path); QVERIFY(catalog.load());
        QVERIFY(catalog.renameEntry("group/scene3d", "My reference room", "video"));
        const auto mixed = catalog.addGroup("Mixed tools", "folder-open", {"line", "scene-object"});
        QVERIFY(!mixed.isEmpty()); QVERIFY(catalog.save());
        auto root = readJson(path); auto entries = root["entries"].toArray();
        for (int i = 0; i < entries.size(); ++i) {
            auto placement = entries[i].toObject();
            if (placement["id"] != "group/scene3d") continue;
            placement["id"] = "group/custom-room";
            placement["lastToolId"] = "scene-rig"; entries[i] = placement;
        }
        root["entries"] = entries; root["selectedEntryId"] = "group/custom-room"; root["activeToolId"] = "scene-rig";
        QVERIFY(writeJson(path, root)); QVERIFY(catalog.load());
        QVERIFY(catalog.entry("group/custom-room"));
        QCOMPARE(catalog.entry("group/custom-room")->name, QString("My reference room"));
        QVERIFY(!catalog.selectEntry("group/custom-room"));
        for (const auto &tool : catalog.availableTools()) QVERIFY(!sceneTool(tool.engine));
        bool foundMixed = false;
        for (const auto &placement : catalog.availableEntries()) {
            QVERIFY(placement.id != "group/custom-room");
            if (placement.id == mixed) { foundMixed = true; QCOMPARE(placement.tools, QStringList{"line"}); }
        }
        QVERIFY(foundMixed);
        Canvas canvas; canvas.setProject(Project::create()); ToolSession session(&canvas, &catalog);
        QVERIFY(!sceneTool(canvas.tool()));
        for (const auto &id : {"scene-object", "scene-camera", "scene-rig"}) QVERIFY(!catalog.activateTool(id));
        QVERIFY(catalog.selectEntry(mixed)); QVERIFY(catalog.activateTool("line"));
        QCOMPARE(canvas.tool(), Canvas::Tool::Line);
        session.flush();
        const auto saved = readJson(path);
        QCOMPARE(saved["tools"].toArray().size(), root["tools"].toArray().size());
        QCOMPARE(saved["entries"].toArray().size(), entries.size());
        QCOMPARE(catalog.entry("group/custom-room")->tools, (QStringList{"scene-object", "scene-camera", "scene-rig"}));
        QVERIFY(catalog.entry(mixed)->tools.contains("scene-object"));
    }
    void directEntryPointsCannotCreateOrEditScenes() {
        Canvas canvas; canvas.setProject(Project::create());
        const auto original = canvas.project().toJson();
        const auto tool = canvas.tool(); auto *controller = canvas.scene3D();
        QVERIFY(!controller->createScene()); QVERIFY(!controller->addModel(builtInModels3D().first()));
        QVERIFY(!controller->beginEdit("Bypass")); QVERIFY(!controller->beginCameraEdit());
        for (const auto requested : {Canvas::Tool::SceneObject, Canvas::Tool::SceneCamera, Canvas::Tool::SceneRig}) {
            canvas.setTool(requested); QCOMPARE(canvas.tool(), tool);
        }
        QVERIFY(!controller->createRig(true)); QVERIFY(!controller->canPose()); QVERIFY(!controller->canPaintWeights());
        QCOMPARE(canvas.project().toJson(), original); QCOMPARE(canvas.document()->history()->count(), 0);
    }
    void hiddenSavedNavigationGuardsAndSynchronizesFallbackChanges() {
        QTemporaryDir directory; const auto path = directory.filePath("tools.json");
        ToolCatalog catalog(nullptr, path); QVERIFY(catalog.load());
        const auto empty = catalog.addGroup("Empty group", "folder-open"); QVERIFY(!empty.isEmpty());
        QVERIFY(catalog.moveEntry(empty, 0)); QVERIFY(catalog.save());
        auto root = readJson(path); auto entries = root["entries"].toArray();
        for (int i = 0; i < entries.size(); ++i) {
            auto placement = entries[i].toObject();
            if (placement["id"] == "group/scene3d") { placement["lastToolId"] = "scene-rig"; entries[i] = placement; }
        }
        root["entries"] = entries; root["selectedEntryId"] = "group/scene3d"; root["activeToolId"] = "scene-rig";
        QVERIFY(writeJson(path, root)); QVERIFY(catalog.load());
        Canvas canvas; canvas.setProject(Project::create()); ToolSession session(&canvas, &catalog);
        const auto fallbackEntry = catalog.selectedEntryId(), fallbackTool = catalog.activeToolId();
        QVERIFY(fallbackEntry != empty);
        QCOMPARE(canvas.tool(), catalog.tool(fallbackTool)->engine);
        bool permit = false; int guardCalls = 0;
        catalog.setActivationGuard([&] { ++guardCalls; return permit; });
        QSignalSpy activations(&catalog, &ToolCatalog::activationRequested);
        QVERIFY(!catalog.removeTool(fallbackEntry, fallbackTool));
        QCOMPARE(guardCalls, 1); QCOMPARE(catalog.activeToolId(), fallbackTool); QCOMPARE(activations.size(), 0);
        permit = true; QVERIFY(catalog.removeTool(fallbackEntry, fallbackTool));
        QVERIFY(catalog.activeToolId() != fallbackTool); QCOMPARE(activations.size(), 1);
        QCOMPARE(canvas.tool(), catalog.tool(catalog.activeToolId())->engine);
        catalog.history()->undo(); QCOMPARE(catalog.activeToolId(), fallbackTool);
        QCOMPARE(canvas.tool(), catalog.tool(fallbackTool)->engine);
        QCOMPARE(catalog.entry("group/scene3d")->lastToolId, QString("scene-rig"));
        QVERIFY(catalog.selectEntry(empty)); QCOMPARE(catalog.selectedEntryId(), empty);
        QVERIFY(catalog.save()); root = readJson(path); entries = root["entries"].toArray();
        for (int i = 0; i < entries.size(); ++i) {
            auto placement = entries[i].toObject();
            if (placement["id"] != "group/shapes") continue;
            auto members = placement["tools"].toArray(); members.append("scene-rig");
            placement["tools"] = members; placement["lastToolId"] = "scene-rig"; entries[i] = placement;
        }
        root["entries"] = entries; root["selectedEntryId"] = "group/shapes"; root["activeToolId"] = "scene-rig";
        QVERIFY(writeJson(path, root)); QVERIFY(catalog.load());
        QCOMPARE(canvas.tool(), Canvas::Tool::Line);
        QVERIFY(catalog.selectEntry(empty)); QCOMPARE(catalog.selectedEntryId(), empty);
        QCOMPARE(canvas.tool(), Canvas::Tool::Line); QCOMPARE(catalog.activeToolId(), QString("line"));
    }
    void existingSceneDataAndPreviewSurviveRoundTrip() {
        auto project = Project::create(); project.width = 128; project.height = 96;
        auto layer = RasterLayer::create("Existing 3D reference"); layer.kind = LayerKind::Scene3D; layer.scene3d = Scene3D{};
        auto model = builtInModels3D().first();
        layer.scene3d->models.insert(model->id, model);
        ModelInstance3D object; object.id = "existing-model"; object.name = "Existing character"; object.modelId = model->id;
        object.rig = createHumanoidRig3D(model->bounds);
        QString error; object.skin = autoBindSkin3D(*model, object.rig, &error); QVERIFY2(object.skin, qPrintable(error));
        object.pose.rootTranslation = {0, .2f, 0};
        layer.scene3d->objects.append(object); project.shots[0].layers.append(layer);
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        QImage image(128, 96, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::green);
        cacheScene3DPreview(scene3DCacheKey(*layer.scene3d, {128, 96}), image);
        Canvas canvas; canvas.setProject(project); auto *controller = canvas.scene3D();
        QVERIFY(controller->scene()); QVERIFY(!controller->canEdit());
        QCOMPARE(scene3DPreview(*controller->scene(), {128, 96}), image);
        QCOMPARE(controller->renderCount(), quint64(0));
        auto camera = controller->scene()->camera; camera.yaw += 20;
        controller->updateCamera(camera); controller->setTextured(false); controller->selectObject(object.id);
        controller->removeSelected(); controller->duplicateSelected(); controller->resetAllPose();
        TimelineController timeline(&canvas); QVERIFY(!timeline.selectCamera(layer.id));
        QCOMPARE(canvas.project().toJson(), project.toJson()); QCOMPARE(canvas.document()->history()->count(), 0);
        QTemporaryDir directory; const auto path = directory.filePath("preserved.toonproj");
        QVERIFY2(saveProject(canvas.project(), path, &error), qPrintable(error));
        const auto loaded = loadProject(path, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->toJson(), project.toJson());
        const auto &scene = *loaded->shots[0].layers.last().scene3d;
        QCOMPARE(scene.objects.first(), object);
        QCOMPARE(serializeModel3D(*scene.models.value(model->id)), serializeModel3D(*model));
    }
};
QTEST_MAIN(Experimental3DTests)
#include "test_experimental_3d.moc"
