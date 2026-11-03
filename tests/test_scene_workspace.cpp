#include "canvas.h"
#include "drawing_panels.h"
#include "main_window.h"
#include "scene3d_controller.h"
#include "scene3d_cache.h"
#include "scene3d_panel.h"
#include "model_library.h"
#include "model_library_panel.h"
#include "scene_model_io.h"
#include "value_slider.h"
#include "property_row.h"
#include "ui_theme.h"
#include <QComboBox>
#include <QAction>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDockWidget>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressDialog>
#include <QSignalSpy>
#include <QScrollBar>
#include <QSlider>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

using namespace toon;
namespace {
Project smallProject() { auto p = Project::create(); p.width = 480; p.height = 320; return p; }
QToolButton *button(QWidget &panel, const char *id) { return panel.findChild<QToolButton *>(id); }
ValueSlider *value(QWidget &panel, const char *id) { return panel.findChild<ValueSlider *>(id); }
QString writeTriangle(const QString &path) {
    const float vertices[] = {-.5f, 0, 0, .5f, 0, 0, 0, 1, 0}; const quint16 indices[] = {0, 1, 2};
    QByteArray bytes(reinterpret_cast<const char *>(vertices), sizeof(vertices)); bytes.append(reinterpret_cast<const char *>(indices), sizeof(indices));
    const QJsonObject root{{"asset", QJsonObject{{"version", "2.0"}}},
        {"buffers", QJsonArray{QJsonObject{{"byteLength", bytes.size()}, {"uri", "data:application/octet-stream;base64," + QString::fromLatin1(bytes.toBase64())}}}},
        {"bufferViews", QJsonArray{QJsonObject{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", int(sizeof(vertices))}}, QJsonObject{{"buffer", 0}, {"byteOffset", int(sizeof(vertices))}, {"byteLength", int(sizeof(indices))}}}},
        {"accessors", QJsonArray{QJsonObject{{"bufferView", 0}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}}, QJsonObject{{"bufferView", 1}, {"componentType", 5123}, {"count", 3}, {"type", "SCALAR"}}}},
        {"meshes", QJsonArray{QJsonObject{{"name", "Triangle"}, {"primitives", QJsonArray{QJsonObject{{"attributes", QJsonObject{{"POSITION", 0}}}, {"indices", 1}}}}}}},
        {"nodes", QJsonArray{QJsonObject{{"mesh", 0}}}}, {"scenes", QJsonArray{QJsonObject{{"nodes", QJsonArray{0}}}}}, {"scene", 0}};
    QFile file(path); if (!file.open(QIODevice::WriteOnly)) return {}; file.write(QJsonDocument(root).toJson()); return path;
}
QPointF projectedCenter(const Scene3D &scene, const ModelInstance3D &object, QSize size) {
    const auto model = scene.models.value(object.modelId);
    const auto matrix = cameraProjectionMatrix(scene.camera, float(size.width()) / size.height()) * cameraViewMatrix(scene.camera) * instanceMatrix(object);
    auto point = matrix * QVector4D(model->bounds.center(), 1); point /= point.w();
    return {(point.x() + 1) * size.width() / 2., (1 - point.y()) * size.height() / 2.};
}
}
class SceneWorkspaceTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void browsingDoesNotCreateSceneOrRender() {
        QTemporaryDir directory; ModelLibrary library(directory.path()); QVERIFY(library.load());
        Canvas canvas; canvas.setProject(smallProject()); auto *controller = canvas.scene3D();
        ModelLibraryPanel panel(&library, controller); const auto original = canvas.project().toJson();
        auto *models = panel.findChild<QListWidget *>("modelLibraryModels"); QVERIFY(models->count() >= 5);
        models->setCurrentRow(1); panel.findChild<QLineEdit *>("modelLibrarySearch")->setText("sphere");
        QCOMPARE(models->count(), 1); QVERIFY(!controller->scene()); QCOMPARE(controller->renderCount(), quint64(0));
        QCOMPARE(canvas.project().toJson(), original); QCOMPARE(canvas.document()->history()->count(), 0);
        panel.findChild<QLineEdit *>("modelLibrarySearch")->clear(); models->setCurrentRow(0);
        const auto id = models->currentItem()->data(Qt::UserRole).toString();
        button(panel, "favorite3DModel")->click(); QVERIFY(library.entry(id)->favorite);
        QVERIFY(library.setTags(id, {"Drawing reference"}));
        auto *tags = panel.findChild<QComboBox *>("modelLibraryTagFilter"); tags->setCurrentIndex(tags->findData("Drawing reference"));
        QCOMPARE(models->count(), 1); QCOMPARE(models->item(0)->data(Qt::UserRole).toString(), id);
        button(panel, "modelLibraryFavoritesOnly")->click(); QCOMPARE(models->count(), 1);
        panel.findChild<QLineEdit *>("modelLibrarySearch")->setText("nothing matches");
        QCOMPARE(models->count(), 0); QVERIFY(!button(panel, "add3DModel")->isEnabled());
        QCOMPARE(canvas.project().toJson(), original); QCOMPARE(controller->renderCount(), quint64(0));
    }
    void placementWithoutProjectExplainsWhy() {
        QTemporaryDir directory;ModelLibrary library(directory.path());QVERIFY(library.load());
        Canvas canvas;QSignalSpy feedback(canvas.scene3D(),&Scene3DController::warning);
        ModelLibraryPanel panel(&library,canvas.scene3D());button(panel,"add3DModel")->click();
        QCOMPARE(feedback.count(),1);QVERIFY(feedback.first().first().toString().contains("project"));
        QVERIFY(!canvas.scene3D()->scene());QCOMPARE(canvas.scene3D()->renderCount(),quint64(0));
    }
    void placementAndLibraryRemovalAreIndependent() {
        QTemporaryDir directory; ModelLibrary library(directory.filePath("library")); QVERIFY(library.load());
        const auto path = writeTriangle(directory.filePath("triangle.gltf")); QString error;
        const auto model = importModel3D(path, &error); QVERIFY2(model, qPrintable(error));
        const auto id = library.addModel(model, "My triangle", &error); QVERIFY2(!id.isEmpty(), qPrintable(error));
        Canvas canvas; canvas.setProject(smallProject()); auto *controller = canvas.scene3D();
        ModelLibraryPanel panel(&library, controller); panel.findChild<QTabBar *>("modelLibraryCollections")->setCurrentIndex(1);
        auto *models = panel.findChild<QListWidget *>("modelLibraryModels"); QCOMPARE(models->count(), 1); models->setCurrentRow(0);
        button(panel, "add3DModel")->click(); QVERIFY(controller->scene()); QCOMPARE(controller->scene()->objects.size(), 1);
        const auto first = *controller->selectedObject(); button(panel, "add3DModel")->click(); QCOMPARE(controller->scene()->objects.size(), 2);
        auto second = *controller->selectedObject(); QVERIFY(first.id != second.id); second.position.setX(3); controller->updateObject(second);
        QCOMPARE(controller->scene()->objects.first(), first);
        button(panel, "remove3DModel")->click(); QVERIFY(!library.entry(id)); QCOMPARE(controller->scene()->objects.size(), 2);
        QVERIFY(controller->scene()->models.contains(model->id));
        const auto count = canvas.document()->history()->count(); controller->removeSelected(); QCOMPARE(controller->scene()->objects.size(), 1);
        QCOMPARE(canvas.document()->history()->count(), count + 1); canvas.document()->history()->undo(); QCOMPARE(controller->scene()->objects.size(), 2);
    }
    void importIsAsyncAtomicAndCancellable() {
        QTemporaryDir directory; ModelLibrary library(directory.filePath("library")); QVERIFY(library.load());
        Canvas canvas; canvas.setProject(smallProject()); ModelLibraryPanel panel(&library, canvas.scene3D());
        QSignalSpy finished(&panel, &ModelLibraryPanel::importFinished); const auto count = library.entries().size();
        const auto path = writeTriangle(directory.filePath("triangle.gltf"));
        panel.importFileAsync(path); QVERIFY(panel.importing()); QVERIFY(!button(panel, "import3DModel")->isEnabled());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000); QVERIFY(finished.first().first().toBool());
        QCOMPARE(library.entries().size(), count + 1); QVERIFY(!panel.importing()); QVERIFY(!canvas.scene3D()->scene());
        QCOMPARE(panel.findChild<QTabBar *>("modelLibraryCollections")->currentIndex(), 1);
        QVERIFY(button(panel, "add3DModel")->isEnabled());
        panel.importFileAsync(directory.filePath("missing.gltf")); QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 10000);
        QVERIFY(!finished.last().first().toBool()); QCOMPARE(library.entries().size(), count + 1);
        panel.importFileAsync(path); QVERIFY(panel.importing());
        const auto progressDialogs = panel.findChildren<QProgressDialog *>("modelImportProgress"); QVERIFY(!progressDialogs.isEmpty());
        auto *progress = progressDialogs.last();
        QVERIFY(QMetaObject::invokeMethod(progress, "canceled"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 3, 10000); QVERIFY(!finished.last().first().toBool());
        QCOMPARE(library.entries().size(), count + 1); QCOMPARE(canvas.document()->history()->count(), 0);
    }
    void objectControlsGroupUndoAndPreserveUntouchedPrecision() {
        Canvas canvas; canvas.setProject(smallProject()); auto *controller = canvas.scene3D();
        QVERIFY(controller->addModel(builtInModels3D().first())); Scene3DPanel panel(controller);
        auto object = *controller->selectedObject(); object.position = {0, .123456f, 0}; controller->updateObject(object);
        const auto count = canvas.document()->history()->count(); auto *x = value(panel, "scene3DX");
        QVERIFY(QMetaObject::invokeMethod(x, "editingStarted")); x->setValue(2); x->setValue(3);
        QCOMPARE(canvas.document()->history()->count(), count); QCOMPARE(controller->selectedObject()->position.y(), object.position.y());
        QVERIFY(QMetaObject::invokeMethod(x, "editingFinished")); QCOMPARE(canvas.document()->history()->count(), count + 1);
        QCOMPARE(controller->selectedObject()->position.x(), 3.f); canvas.document()->history()->undo();
        QCOMPARE(controller->selectedObject()->position, object.position); canvas.document()->history()->redo();
        QVERIFY(QMetaObject::invokeMethod(x, "editingStarted")); x->setValue(7); QVERIFY(QMetaObject::invokeMethod(x, "editingCancelled"));
        QCOMPARE(controller->selectedObject()->position.x(), 3.f); QCOMPARE(canvas.document()->history()->count(), count + 1);
        panel.findChild<QTabBar *>("scene3DObjectMode")->setCurrentIndex(1);
        QCOMPARE(controller->manipulation(), Scene3DController::Manipulation::Rotate);
        value(panel, "scene3DY")->setValue(45); QCOMPARE(controller->selectedObject()->rotation.y(), 45.f);
        panel.findChild<QTabBar *>("scene3DObjectMode")->setCurrentIndex(2);
        value(panel, "scene3DZ")->setValue(2); QCOMPARE(controller->selectedObject()->scale.z(), 2.f);
        button(panel, "reset3DObject")->click(); QCOMPARE(controller->selectedObject()->position, QVector3D());
        QCOMPARE(controller->selectedObject()->scale, QVector3D(1, 1, 1));
    }
    void objectAndLayerLocksBlockEditsButRemainUnlockable() {
        Canvas canvas; canvas.setProject(smallProject()); auto *controller = canvas.scene3D(); QVERIFY(controller->addModel(builtInModels3D().first()));
        Scene3DPanel panel(controller); panel.resize(280, 680); panel.show();
        auto *list = panel.findChild<QTreeWidget *>("scene3DObjects"); QVERIFY(list->topLevelItemCount() == 1);
        auto *item = list->topLevelItem(0); const auto row = list->visualItemRect(item);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, {list->columnViewportPosition(2) + 14, row.center().y()});
        QVERIFY(controller->selectedObject()->locked); QVERIFY(!value(panel, "scene3DX")->isEnabled());
        const auto original = *controller->selectedObject(); auto moved = original; moved.position.setX(6); controller->updateObject(moved);
        QCOMPARE(*controller->selectedObject(), original);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, {list->columnViewportPosition(2) + 14, row.center().y()});
        QVERIFY(!controller->selectedObject()->locked); QVERIFY(value(panel, "scene3DX")->isEnabled());
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, {list->columnViewportPosition(0) + 14, row.center().y()});
        QVERIFY(!controller->selectedObject()->visible);
        canvas.document()->setLayersLocked({controller->layerId()}, true); QVERIFY(!controller->canEdit());
        QVERIFY(!value(panel, "scene3DX")->isEnabled()); const auto count = canvas.document()->history()->count();
        controller->updateObject(moved); QCOMPARE(canvas.document()->history()->count(), count);
        canvas.document()->setLayersLocked({controller->layerId()}, false); QVERIFY(controller->canEdit());
        canvas.setTool(Canvas::Tool::Brush); QVERIFY(!canvas.toolBlockReason().isEmpty());
    }
    void cameraControlsAndCanvasDragsCommitOnce() {
        Canvas canvas; canvas.setProject(smallProject()); auto *controller = canvas.scene3D(); QVERIFY(controller->addModel(builtInModels3D().first()));
        Scene3DPanel panel(controller); panel.findChild<QTabWidget *>("scene3DPropertyTabs")->setCurrentIndex(1);
        QCOMPARE(canvas.tool(), Canvas::Tool::SceneCamera);
        auto *projection = panel.findChild<QTabBar *>("scene3DProjection"); projection->setCurrentIndex(1); QVERIFY(controller->scene()->camera.orthographic);
        QVERIFY(!value(panel, "scene3DFov")->isEnabled()); QVERIFY(value(panel, "scene3DOrthoScale")->isEnabled());
        value(panel, "scene3DOrthoScale")->setValue(7); QCOMPARE(controller->scene()->camera.orthoScale, 7.f);
        button(panel, "lock3DCamera")->click(); QVERIFY(controller->scene()->camera.locked);
        QVERIFY(!value(panel, "scene3DOrthoScale")->isEnabled()); const auto locked = controller->scene()->camera;
        controller->pointerPress({150, 120}, {}); controller->pointerMove({200, 150}, {}); controller->pointerRelease(); QCOMPARE(controller->scene()->camera, locked);
        button(panel, "lock3DCamera")->click(); QVERIFY(!controller->scene()->camera.locked);
        auto *modes = panel.findChild<QTabBar *>("scene3DCameraMode"); modes->setCurrentIndex(1);
        QCOMPARE(controller->manipulation(), Scene3DController::Manipulation::Pan);
        const auto before = controller->scene()->camera; const auto count = canvas.document()->history()->count();
        controller->pointerPress({150, 120}, {}); controller->pointerMove({200, 150}, {}); controller->pointerMove({230, 160}, {});
        QCOMPARE(canvas.document()->history()->count(), count); controller->pointerRelease();
        QCOMPARE(canvas.document()->history()->count(), count + 1); QVERIFY(controller->scene()->camera.target != before.target);
        canvas.document()->history()->undo(); QCOMPARE(controller->scene()->camera, before);
        controller->selectTool(false); const auto initial = *controller->selectedObject();
        const auto center = projectedCenter(*controller->scene(), initial, {480, 320});
        controller->pointerPress(center, {}); controller->pointerMove(center + QPointF(20, 10), {}); controller->pointerRelease();
        QVERIFY(controller->selectedObject()); QVERIFY(controller->selectedObject()->position != initial.position);
    }
    void panelsRemainNarrowAccessibleAndModelRefreshDoesNotEdit() {
        QTemporaryDir directory; ModelLibrary library(directory.path()); QVERIFY(library.load());
        Canvas canvas; canvas.setProject(smallProject()); auto *controller = canvas.scene3D(); QVERIFY(controller->addModel(builtInModels3D().first()));
        Scene3DPanel panel(controller); ModelLibraryPanel models(&library, controller);
        panel.resize(240, 660); models.resize(240, 550); panel.show(); models.show(); QTest::qWait(10);
        QVERIFY(panel.minimumSizeHint().width() <= 240); QVERIFY(models.minimumSizeHint().width() <= 240);
        const auto count = canvas.document()->history()->count();
        for (auto *widget : {static_cast<QWidget *>(&panel), static_cast<QWidget *>(&models)}) {
            for (auto *control : widget->findChildren<QToolButton *>()) {
                QVERIFY2(!control->toolTip().isEmpty(), qPrintable(control->objectName()));
                QVERIFY2(!control->accessibleName().isEmpty(), qPrintable(control->objectName()));
            }
            for (auto *number : widget->findChildren<QDoubleSpinBox *>()) { QCOMPARE(number->width(), 48); QVERIFY(number->suffix().isEmpty()); }
        }
        QVERIFY(QMetaObject::invokeMethod(controller, "changed")); QCOMPARE(canvas.document()->history()->count(), count);
    }
    void compactPagesMeasureOnlyActiveProperties() {
        Canvas canvas; canvas.setProject(smallProject()); auto *controller = canvas.scene3D();
        QVERIFY(controller->addModel(builtInModels3D().first()));
        Scene3DPanel panel(controller); panel.resize(240, 500); panel.show(); QTest::qWait(20);
        QCOMPARE(panel.verticalScrollBar()->maximum(), 0);
        auto *tabs = panel.findChild<QTabWidget *>("scene3DPropertyTabs");
        QCOMPARE(tabs->iconSize(), QSize(18,18));
        for (const auto *id : {"create3DScene", "duplicate3DObject", "remove3DObject", "textured3DScene",
                              "reset3DObject", "fit3DSelected", "fit3DScene", "lock3DCamera"}) {
            QCOMPARE(button(panel,id)->size(), QSize(28,28)); QCOMPARE(button(panel,id)->iconSize(), QSize(18,18));
        }
        tabs->setCurrentIndex(1); QTest::qWait(20); QCOMPARE(panel.verticalScrollBar()->maximum(), 0);
        for (const auto *id : {"scene3DX", "scene3DY", "scene3DZ", "scene3DFov", "scene3DOrthoScale",
                              "scene3DDistance", "scene3DYaw", "scene3DPitch"}) {
            auto *field = value(panel,id); QVERIFY(qobject_cast<PropertyRow *>(field->parentWidget()));
            QCOMPARE(field->findChild<QDoubleSpinBox *>()->width(), 48);
        }
        panel.setRigMode(); QTest::qWait(20); QCOMPARE(panel.verticalScrollBar()->maximum(), 0);
        panel.findChild<QTabWidget *>("rig3DEditMode")->setCurrentIndex(2);
        QTest::qWait(20); panel.ensureWidgetVisible(value(panel,"pose3DLimitMax"));
        QVERIFY(panel.viewport()->rect().contains(QRect(value(panel,"pose3DLimitMax")->mapTo(panel.viewport(),QPoint()),value(panel,"pose3DLimitMax")->size())));
        panel.setCameraMode(false); QTest::qWait(20); QCOMPARE(panel.verticalScrollBar()->maximum(), 0);
        QVERIFY(value(panel,"scene3DX")->findChild<QSlider *>()->width() >= 74);
    }
    void shortPropertiesKeepContextModesAndActionsVisible() {
        Canvas canvas; canvas.setProject(smallProject()); auto *controller = canvas.scene3D();
        QVERIFY(controller->addModel(builtInModels3D().first()));
        for (int i=0; i<3; ++i) controller->duplicateSelected();
        Scene3DPanel panel(controller); panel.resize(240,300); panel.setCameraMode(true); panel.show(); QTest::qWait(20);
        auto *header = panel.findChild<QWidget *>("scene3DPinnedHeader");
        auto *footer = panel.findChild<QWidget *>("scene3DPinnedActions");
        auto *tabs = panel.findChild<QTabWidget *>("scene3DPropertyTabs");
        auto *modes = panel.findChild<QTabBar *>("scene3DCameraMode");
        auto *list = panel.findChild<QTreeWidget *>("scene3DObjects");
        const auto rectInPanel = [&panel](QWidget *widget) { return QRect(widget->mapTo(&panel,QPoint()),widget->size()); };
        const auto verifyPinnedWidth = [&] {
            const auto body = panel.viewport()->geometry();
            QCOMPARE(header->x(),body.x()); QCOMPARE(header->width(),body.width());
            QCOMPARE(footer->x(),body.x()); QCOMPARE(footer->width(),body.width());
            if (panel.verticalScrollBar()->isVisible()) {
                const auto bar = rectInPanel(panel.verticalScrollBar());
                QVERIFY(!bar.intersects(rectInPanel(button(panel,"textured3DScene"))));
                QVERIFY(!bar.intersects(rectInPanel(list)));
                QVERIFY(bar.top() >= body.top()); QVERIFY(bar.bottom() <= body.bottom());
            }
        };
        verifyPinnedWidth();
        const auto headerRect = rectInPanel(header), footerRect = rectInPanel(footer);
        QVERIFY(panel.rect().contains(headerRect)); QVERIFY(panel.rect().contains(footerRect));
        QVERIFY(headerRect.contains(rectInPanel(tabs))); QVERIFY(headerRect.contains(rectInPanel(modes)));
        QVERIFY(footerRect.contains(rectInPanel(button(panel,"lock3DCamera"))));
        QVERIFY(list->verticalScrollBar()->maximum() > 0);
        QVERIFY(panel.verticalScrollBar()->maximum() > 0);
        panel.ensureWidgetVisible(value(panel,"scene3DPitch")); QTest::qWait(10);
        const int cameraScroll = panel.verticalScrollBar()->value(); QVERIFY(cameraScroll > 0);
        const QRect fieldRect(value(panel,"scene3DPitch")->mapTo(panel.viewport(),QPoint()),value(panel,"scene3DPitch")->size());
        QVERIFY(panel.viewport()->rect().contains(fieldRect));
        QCOMPARE(rectInPanel(header),headerRect); QCOMPARE(rectInPanel(footer),footerRect);
        panel.setCameraMode(false); QTest::qWait(10);
        verifyPinnedWidth();
        QVERIFY(panel.rect().contains(rectInPanel(button(panel,"reset3DObject"))));
        panel.setCameraMode(true); QTest::qWait(10); QCOMPARE(panel.verticalScrollBar()->value(),cameraScroll);
        verifyPinnedWidth();
        panel.resize(240,500); panel.setCameraMode(false); QTest::qWait(20);
        QVERIFY(!panel.verticalScrollBar()->isVisible()); verifyPinnedWidth();
        panel.setRigMode(); panel.findChild<QTabWidget *>("rig3DEditMode")->setCurrentIndex(2);
        QTest::qWait(20); QVERIFY(panel.verticalScrollBar()->isVisible()); verifyPinnedWidth();
        panel.setCameraMode(false); QTest::qWait(20); QVERIFY(!panel.verticalScrollBar()->isVisible()); verifyPinnedWidth();
    }
    void propertyDraftsSurviveResizingAndRefresh_data() {
        QTest::addColumn<bool>("camera"); QTest::newRow("object-position") << false; QTest::newRow("camera-orbit") << true;
    }
    void propertyDraftsSurviveResizingAndRefresh() {
        QFETCH(bool,camera);
        Canvas canvas; canvas.setProject(smallProject()); auto *controller = canvas.scene3D();
        QVERIFY(controller->addModel(builtInModels3D().first()));
        Scene3DPanel panel(controller); panel.resize(320,520); panel.setCameraMode(camera); panel.show();
        panel.raise(); panel.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&panel));
        auto *field = value(panel,camera?"scene3DYaw":"scene3DX");
        auto *number = field->findChild<QDoubleSpinBox *>(); auto *editor = number->findChild<QLineEdit *>();
        const double original = field->value(); const int history = canvas.document()->history()->count();
        number->setFocus(); QTRY_VERIFY(number->hasFocus() || editor->hasFocus());
        number->selectAll(); QTest::keyClicks(number,"-12.");
        QCOMPARE(editor->text(),QString("-12.")); QVERIFY(controller->editing());
        for (const auto &size : {QSize(240,300),QSize(380,520),QSize(240,520)}) {
            panel.resize(size); QVERIFY(QMetaObject::invokeMethod(controller,"changed")); QTest::qWait(10);
            QCOMPARE(editor->text(),QString("-12.")); QVERIFY(number->hasFocus() || editor->hasFocus());
            QCOMPARE(number->width(),48); QVERIFY(field->findChild<QSlider *>()->width() >= 74);
            QCOMPARE(canvas.document()->history()->count(),history);
        }
        QTest::keyClick(number,Qt::Key_Escape); QCOMPARE(field->value(),original);
        QCOMPARE(canvas.document()->history()->count(),history); QVERIFY(!controller->editing());
    }
    void propertyControlsStayStillWhileRendering_data() {
        QTest::addColumn<QSize>("panelSize");
        QTest::addColumn<QString>("fieldId");
        QTest::addColumn<bool>("camera");
        QTest::addColumn<bool>("numberEntry");
        QTest::newRow("model-slider-narrow") << QSize(240, 680) << QString("scene3DX") << false << false;
        QTest::newRow("model-number-short") << QSize(280, 360) << QString("scene3DY") << false << true;
        QTest::newRow("camera-slider-scrolled") << QSize(240, 360) << QString("scene3DYaw") << true << false;
        QTest::newRow("camera-number-narrow") << QSize(240, 680) << QString("scene3DFov") << true << true;
    }
    void propertyControlsStayStillWhileRendering() {
#ifndef TOON_HAS_FILAMENT
        QSKIP("GPU rendering requires the Filament build");
#else
        QFETCH(QSize, panelSize); QFETCH(QString, fieldId); QFETCH(bool, camera); QFETCH(bool, numberEntry);
        Canvas canvas; canvas.setProject(smallProject()); auto *controller = canvas.scene3D();
        QVERIFY(controller->addModel(builtInModels3D().first())); controller->selectTool(camera);
        QMainWindow window; window.setCentralWidget(new QWidget);
        auto *dock = new QDockWidget("Tool Properties", &window);
        dock->setWidget(drawingPanel(&canvas, "propertiesPanel")); dock->setFixedWidth(panelSize.width());
        window.addDockWidget(Qt::RightDockWidgetArea, dock);
        window.resize(600, panelSize.height()); window.show();
        auto *panel = dock->findChild<Scene3DPanel *>(); QVERIFY(panel);
        auto *field = panel->findChild<ValueSlider *>(fieldId); QVERIFY(field);
        auto *track = field->findChild<QSlider *>(); auto *number = field->findChild<QDoubleSpinBox *>();
        auto *tabs = panel->findChild<QTabWidget *>("scene3DPropertyTabs");
        auto *scroll = panel->verticalScrollBar();
        QTRY_VERIFY_WITH_TIMEOUT(!controller->rendering(), 15000);
        panel->ensureWidgetVisible(field); QTest::qWait(20);
        const auto rectangle = [&window](QWidget *widget) { return QRect(widget->mapTo(&window, QPoint()), widget->size()); };
        const auto tabRect = rectangle(tabs), fieldRect = rectangle(field), trackRect = rectangle(track), numberRect = rectangle(number);
        const auto viewportSize = panel->viewport()->size();
        const int scrollValue = scroll->value(), scrollMaximum = scroll->maximum();
        const bool scrollVisible = scroll->isVisible();
        QVERIFY(panel->viewport()->rect().contains(QRect(field->mapTo(panel->viewport(),QPoint()),field->size())));
        const int historyCount = canvas.document()->history()->count();
        const double original = field->value();
        if (!numberEntry) track->setSliderDown(true);
        for (int i = 0; i < 3; ++i) {
            if (numberEntry) number->setValue(number->value() + 1);
            else track->setValue(track->value() + track->singleStep() * 10);
            QVERIFY(controller->rendering());
            // Settle layout requests without delivering the worker's queued frameReady signal.
            for (int pass = 0; pass < 4; ++pass) QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
            QCOMPARE(rectangle(tabs), tabRect); QCOMPARE(rectangle(field), fieldRect);
            QCOMPARE(rectangle(track), trackRect); QCOMPARE(rectangle(number), numberRect);
            QCOMPARE(panel->viewport()->size(), viewportSize);
            QCOMPARE(scroll->value(), scrollValue); QCOMPARE(scroll->maximum(), scrollMaximum); QCOMPARE(scroll->isVisible(), scrollVisible);
            QTRY_VERIFY_WITH_TIMEOUT(!controller->rendering(), 15000);
            for (int pass = 0; pass < 4; ++pass) QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
            QCOMPARE(rectangle(tabs), tabRect); QCOMPARE(rectangle(field), fieldRect);
            QCOMPARE(rectangle(track), trackRect); QCOMPARE(rectangle(number), numberRect);
            QCOMPARE(panel->viewport()->size(), viewportSize);
            QCOMPARE(scroll->value(), scrollValue); QCOMPARE(scroll->maximum(), scrollMaximum); QCOMPARE(scroll->isVisible(), scrollVisible);
            QCOMPARE(canvas.document()->history()->count(), historyCount + (numberEntry ? i + 1 : 0));
            if (!numberEntry) { QVERIFY(track->isSliderDown()); QVERIFY(controller->editing()); }
        }
        QVERIFY(field->value() > original);
        if (!numberEntry) {
            track->setSliderDown(false); QCOMPARE(canvas.document()->history()->count(), historyCount + 1);
            canvas.document()->history()->undo(); QCOMPARE(field->value(), original);
        }
#endif
    }
    void referencesCompositeBelowArtworkRespectGroupsAndNeverExport() {
        const QSize frame(480, 320); Scene3D scene; scene.camera.yaw = 121;
        QImage red(frame, QImage::Format_ARGB32_Premultiplied); red.fill(Qt::red);
        cacheScene3DPreview(scene3DCacheKey(scene, frame), red);
        auto folder = RasterLayer::create("Reference group"); folder.group = true; folder.opacity = .5;
        auto reference = RasterLayer::create("3D reference"); reference.kind = LayerKind::Scene3D; reference.scene3d = scene; reference.parentId = folder.id;
        auto drawing = RasterLayer::create("Ink"); QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::transparent);
        { QPainter painter(&tile); painter.fillRect(QRect(0, 0, 100, 100), Qt::blue); } drawing.tiles.insert({0, 0}, tile);
        QVector<RasterLayer> layers{folder, reference, drawing};
        auto display = [&] {
            QImage image(frame, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
            QPainter painter(&image); paintLayers(painter, layers, true, nullptr, false, frame); return image;
        };
        auto image = display(); QCOMPARE(image.pixelColor(20, 20), QColor(Qt::blue));
        const auto redPixel = image.pixelColor(300, 100); QCOMPARE(redPixel.red(), 255); QVERIFY(qAbs(redPixel.alpha() - 128) <= 1);
        layers[1].opacity = .5; image = display(); QVERIFY(qAbs(image.pixelColor(300, 100).alpha() - 64) <= 1);
        layers[0].visible = false; QCOMPARE(display().pixelColor(300, 100).alpha(), 0);
        layers[0].visible = true; layers[1].visible = false; QCOMPARE(display().pixelColor(300, 100).alpha(), 0);
        layers[1].visible = true;
        auto project = smallProject(); project.transparent = true; project.shots[0].layers = layers;
        auto artworkOnly = project; artworkOnly.shots[0].layers = {drawing};
        QCOMPARE(renderMovieFrame(project), renderMovieFrame(artworkOnly));
        QCOMPARE(renderSheet(project, project.shots.first()), renderSheet(artworkOnly, artworkOnly.shots.first()));
        // A completed new render with the same scene key invalidates the derived tile cache.
        QImage green(frame, QImage::Format_ARGB32_Premultiplied); green.fill(Qt::green);
        cacheScene3DPreview(scene3DCacheKey(scene, frame), green); QCOMPARE(display().pixelColor(300, 100).green(), 255);
    }
    void completedFramesSurviveInterimPreviewWrites() {
        Scene3D scene; scene.camera.yaw = -237; const QSize size(13, 11);
        QImage complete(size, QImage::Format_ARGB32_Premultiplied); complete.fill(Qt::red);
        QImage interim(size, QImage::Format_ARGB32_Premultiplied); interim.fill(Qt::blue);
        const auto key = scene3DCacheKey(scene, size);
        cacheScene3DPreview(key, complete); cacheScene3DPreview(key, interim, false);
        QCOMPARE(scene3DPreview(scene, size), complete); QCOMPARE(scene3DPreview(scene, size, true), complete);
        scene.camera.yaw = -238; const auto nextKey = scene3DCacheKey(scene, size);
        cacheScene3DPreview(nextKey, interim, false); QCOMPARE(scene3DPreview(scene, size), interim);
        QVERIFY(scene3DPreview(scene, size, true).isNull());
        cacheScene3DPreview(nextKey, complete); QCOMPARE(scene3DPreview(scene, size, true), complete);
    }
    void globalUndoCancelsPending3DEditAndPreservesRedo() {
        QTemporaryDir directory; MainWindow window(directory.filePath("workspace.ini"));
        auto *canvas = window.canvas(); canvas->setProject(smallProject()); auto *controller = canvas->scene3D();
        QVERIFY(controller->addModel(builtInModels3D().first())); canvas->document()->history()->clear();
        const auto initial = *controller->selectedObject();
        auto *undo = window.findChild<QAction *>("undoAction"); auto *redo = window.findChild<QAction *>("redoAction"); QVERIFY(undo); QVERIFY(redo);
        QVERIFY(controller->beginEdit("Move model")); auto preview = initial; preview.position.setX(3); controller->updateObject(preview);
        QVERIFY(undo->isEnabled()); QVERIFY(!redo->isEnabled()); undo->trigger();
        QVERIFY(!controller->editing()); QCOMPARE(controller->scene()->objects.size(), 1); QCOMPARE(*controller->selectedObject(), initial);
        QCOMPARE(canvas->document()->history()->count(), 0);
        auto committed = initial; committed.position.setX(4); controller->updateObject(committed);
        QCOMPARE(canvas->document()->history()->count(), 1); canvas->document()->history()->undo(); QVERIFY(redo->isEnabled());
        QVERIFY(controller->beginEdit("Move model")); preview.position.setY(2); controller->updateObject(preview);
        QVERIFY(undo->isEnabled()); QVERIFY(!redo->isEnabled()); undo->trigger();
        QCOMPARE(*controller->selectedObject(), initial); QCOMPARE(canvas->document()->history()->count(), 1);
        QVERIFY(canvas->document()->history()->canRedo()); QVERIFY(redo->isEnabled()); redo->trigger();
        QCOMPARE(*controller->selectedObject(), committed);
    }
    void savingCommitsOnePending3DEditAndMarksItClean() {
        QTemporaryDir directory; auto project = smallProject(); const auto model = builtInModels3D().first();
        Scene3D scene; scene.models.insert(model->id, model);
        ModelInstance3D object; object.id = "saved-model"; object.name = "Cube"; object.modelId = model->id; scene.objects.append(object);
        auto reference = RasterLayer::create("3D reference"); reference.kind = LayerKind::Scene3D; reference.scene3d = scene;
        project.shots[0].layers.prepend(reference);
        const auto path = directory.filePath("scene.toonproj"); QString error; QVERIFY2(saveProject(project, path, &error), qPrintable(error));
        MainWindow window(directory.filePath("workspace.ini")); QVERIFY(window.openProject(path));
        auto *controller = window.canvas()->scene3D(); controller->selectObject(object.id);
        QVERIFY(controller->beginEdit("Adjust model")); object.position = {2, 3, 4}; controller->updateObject(object);
        auto *history = window.canvas()->document()->history(); QCOMPARE(history->count(), 0);
        auto *save = window.findChild<QAction *>("saveProjectAction"); QVERIFY(save); QVERIFY(save->isEnabled()); save->trigger();
        QVERIFY(!controller->editing()); QCOMPARE(history->count(), 1); QVERIFY(history->isClean());
        const auto loaded = loadProject(path, &error); QVERIFY2(loaded.has_value(), qPrintable(error));
        QCOMPARE(loaded->shots.first().layers.first().scene3d->objects.first().position, object.position);
        history->undo(); QCOMPARE(controller->scene()->objects.first().position, QVector3D()); QVERIFY(!history->isClean());
    }
    void switchingProjectsNeverCarriesThePreviousReference() {
#ifndef TOON_HAS_FILAMENT
        QSKIP("GPU rendering requires the Filament build");
#else
        Canvas canvas; canvas.setProject(smallProject()); auto *controller = canvas.scene3D();
        auto model = *builtInModels3D().first(); model.name = "Black reference cube";
        for (auto &mesh : model.meshes) mesh.baseColor = Qt::black;
        QVERIFY(controller->addModel(finalizeModel3D(std::move(model))));
        QTRY_VERIFY_WITH_TIMEOUT(!controller->rendering(), 15000);
        QVERIFY(!scene3DPreview(*controller->scene(), {480, 320}, true).isNull());
        QVERIFY(controller->beginEdit("Unfinished model move"));
        auto object = *controller->selectedObject(); object.position.setX(2); controller->updateObject(object);
        const auto pending = *controller->scene();
        auto next = smallProject(); auto reference = RasterLayer::create("Other project reference");
        reference.kind = LayerKind::Scene3D; reference.scene3d = Scene3D(); reference.scene3d->camera.yaw = -117;
        next.shots[0].layers.prepend(reference); canvas.setProject(next);
        QVERIFY(!controller->editing()); QVERIFY(controller->scene()); QVERIFY(controller->scene()->objects.isEmpty());
        QVERIFY(controller->selectedObjectId().isEmpty());
        QVERIFY(scene3DPreview(pending, {480, 320}).isNull());
        const auto immediately = scene3DPreview(*controller->scene(), {480, 320});
        QVERIFY(immediately.isNull());
        QTRY_VERIFY_WITH_TIMEOUT(!controller->rendering(), 15000);
        const auto empty = scene3DPreview(*controller->scene(), {480, 320}, true); QVERIFY(!empty.isNull());
        for (int y = 0; y < empty.height(); ++y) for (int x = 0; x < empty.width(); ++x) QCOMPARE(empty.pixelColor(x, y).alpha(), 0);
        QCOMPARE(canvas.document()->history()->count(), 0);
#endif
    }
    void gpuReferenceAppearsOnCanvasAndDrawingReusesIt() {
#ifndef TOON_HAS_FILAMENT
        QSKIP("GPU rendering requires the Filament build");
#else
        Canvas canvas; canvas.resize(900, 700); canvas.setProject(smallProject()); canvas.show(); canvas.fitSheet();
        auto *controller = canvas.scene3D(); QSignalSpy previews(controller, &Scene3DController::previewChanged);
        QVERIFY(controller->addModel(builtInModels3D().first()));
        QTRY_VERIFY_WITH_TIMEOUT(!controller->rendering(), 15000);
        const auto rendered = scene3DPreview(*controller->scene(), {480, 320});
        QVERIFY2(!rendered.isNull(), "The 3D renderer must publish a real reference image"); QVERIFY(previews.count() > 0);
        bool hasVisibleModel = false;
        for (int y = 0; y < rendered.height() && !hasVisibleModel; ++y) for (int x = 0; x < rendered.width(); ++x)
            if (rendered.pixelColor(x, y).alpha() > 100) { hasVisibleModel = true; break; }
        QVERIFY(hasVisibleModel);
        const auto center = projectedCenter(*controller->scene(), *controller->selectedObject(), {480, 320});
        canvas.document()->addLayer(); canvas.setTool(Canvas::Tool::Brush); QTest::qWait(20);
        const auto canvasImage = canvas.grab().toImage();
        const auto outputOrigin = sheetGeometry(canvas.project(), canvas.shot()).output.topLeft();
        const QPoint pixel = (canvas.viewTransform().map(center + outputOrigin) * canvasImage.devicePixelRatio()).toPoint();
        QVERIFY(canvasImage.rect().contains(pixel)); QVERIFY(canvasImage.pixelColor(pixel) != QColor(Qt::white));
        QCOMPARE(renderMovieFrame(canvas.project()).pixelColor(center.toPoint()), QColor(Qt::white));
        const auto renders = controller->renderCount(); BrushSettings brush; brush.color = Qt::blue; brush.size = 12;
        QVERIFY(canvas.document()->beginStroke({20, 20}, 1, brush, false));
        canvas.document()->continueStroke({90, 20}, 1); canvas.document()->continueStroke({150, 20}, 1); canvas.document()->endStroke();
        QTest::qWait(40); QCOMPARE(controller->renderCount(), renders); QVERIFY(!controller->rendering());
        const auto drawnPixel = renderMovieFrame(canvas.project()).pixelColor(90, 20);
        QVERIFY(drawnPixel.blue() > 200); QVERIFY(drawnPixel.red() < 100);
#endif
    }
    void panelPreviews() {
        if (qEnvironmentVariableIsEmpty("TOON_UI_PREVIEW")) QSKIP("Preview output is opt-in");
        QTemporaryDir directory; ModelLibrary library(directory.path()); QVERIFY(library.load());
        Canvas canvas; canvas.setProject(smallProject()); auto *controller = canvas.scene3D(); QVERIFY(controller->addModel(builtInModels3D().first()));
        Scene3DPanel panel(controller); ModelLibraryPanel models(&library, controller); panel.resize(320, 480); models.resize(320, 480);
        panel.show(); models.show(); QTest::qWait(30); const auto output = qEnvironmentVariable("TOON_UI_PREVIEW"); QDir().mkpath(output);
        QVERIFY(panel.grab().save(output + "/scene3d-model-properties.png"));
        panel.findChild<QTabWidget *>("scene3DPropertyTabs")->setCurrentIndex(1); QTest::qWait(10);
        QVERIFY(panel.grab().save(output + "/scene3d-camera-properties.png")); QVERIFY(models.grab().save(output + "/scene3d-library.png"));
        panel.resize(240,360); QTest::qWait(20);
        QVERIFY(panel.grab().save(output + "/scene3d-camera-narrow.png"));
        panel.resize(240,300); panel.ensureWidgetVisible(value(panel,"scene3DPitch")); QTest::qWait(20);
        QVERIFY(panel.grab().save(output + "/scene3d-camera-short.png"));
        panel.setCameraMode(false); QTest::qWait(20);
        QVERIFY(panel.grab().save(output + "/scene3d-model-short.png"));
    }
};
QTEST_MAIN(SceneWorkspaceTests)
#include "test_scene_workspace.moc"
