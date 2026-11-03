#include "canvas.h"
#include "scene3d_controller.h"
#include "scene3d_cache.h"
#include "sheet.h"
#include "tool_session.h"
#include "ui_theme.h"
#include <QDir>
#include <QLineF>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QtTest>

using namespace toon;

namespace {
Project smallProject() {
    auto project = Project::create(); project.width = 480; project.height = 320; return project;
}
QTransform artworkViewTransform(const Canvas &canvas) {
    auto transform = canvas.viewTransform();
    const auto origin = sheetGeometry(canvas.project(), canvas.shot()).output.topLeft();
    transform.translate(origin.x(), origin.y());
    return transform;
}
QPointF projectJoint(const Canvas &canvas, const RigJoint3D &joint) {
    const auto *controller = canvas.scene3D();
    const auto &scene = *controller->scene();
    const QSize frame(canvas.project().width, canvas.project().height);
    const auto matrix = cameraProjectionMatrix(scene.camera, float(frame.width()) / frame.height())
        * cameraViewMatrix(scene.camera) * instanceMatrix(*controller->selectedObject());
    auto clip = matrix * QVector4D(joint.position, 1); clip /= clip.w();
    return {(clip.x() + 1) * frame.width() / 2., (1 - clip.y()) * frame.height() / 2.};
}
void pointer(Canvas &canvas, QEvent::Type type, QPointF position, Qt::MouseButton button,
             Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = {}) {
    QMouseEvent event(type, position, canvas.mapToGlobal(position), button, buttons, modifiers);
    QCoreApplication::sendEvent(&canvas, &event);
}
const ModelInstance3D *instance(const Scene3D &scene, const QString &id) {
    for (const auto &object : scene.objects) if (object.id == id) return &object;
    return nullptr;
}
}

class RigWorkspaceTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }

    void createSkeletonOnlyOnEmptyEditableModel_data() {
        QTest::addColumn<bool>("humanoid");
        QTest::newRow("root") << false;
        QTest::newRow("humanoid") << true;
    }
    void createSkeletonOnlyOnEmptyEditableModel() {
        QFETCH(bool, humanoid);
        Canvas canvas; auto *controller = canvas.scene3D();
        QSignalSpy warnings(controller, &Scene3DController::warning);
        QVERIFY(!controller->createRig(humanoid)); QVERIFY(!warnings.isEmpty());
        canvas.setProject(smallProject()); QVERIFY(!controller->createRig(humanoid));
        QVERIFY(controller->addModel(builtInModels3D().first()));
        QVERIFY(controller->selectedRig()->joints.isEmpty());
        const int history = canvas.document()->history()->count();
        QVERIFY(controller->createRig(humanoid));
        QCOMPARE(canvas.tool(), Canvas::Tool::SceneRig); QVERIFY(controller->selectedJoint());
        QCOMPARE(canvas.document()->history()->count(), history + 1);
        QVERIFY(validateRig3D(*controller->selectedRig()).isEmpty());
        const auto skeleton = *controller->selectedRig();
        if (humanoid) QVERIFY(skeleton.joints.size() > 10);
        else {
            QCOMPARE(skeleton.joints.size(), 1); QCOMPARE(skeleton.joints.first().name, QString("Root"));
            QVERIFY(skeleton.joints.first().parentId.isEmpty());
            QCOMPARE(skeleton.joints.first().position, builtInModels3D().first()->bounds.center());
        }
        warnings.clear();
        QVERIFY(!controller->createRig(!humanoid)); QCOMPARE(warnings.count(), 1);
        QCOMPARE(*controller->selectedRig(), skeleton); QCOMPARE(canvas.document()->history()->count(), history + 1);
        canvas.document()->history()->undo(); QVERIFY(controller->selectedRig()->joints.isEmpty());
        canvas.document()->history()->redo(); QCOMPARE(*controller->selectedRig(), skeleton);
    }

    void jointHierarchyRenameReparentAndBranchDeletion() {
        Canvas canvas; canvas.setProject(smallProject()); auto *controller = canvas.scene3D();
        QVERIFY(controller->addModel(builtInModels3D().first())); QVERIFY(controller->createRig(false));
        const QString root = controller->selectedJointId();
        const QString child = controller->addJoint(); QVERIFY(!child.isEmpty());
        QCOMPARE(controller->selectedJoint()->parentId, root);
        const QString grandchild = controller->addJoint(); QVERIFY(!grandchild.isEmpty());
        QCOMPARE(controller->selectedJoint()->parentId, child);
        const QString otherRoot = controller->addJoint(false); QVERIFY(!otherRoot.isEmpty());
        QVERIFY(controller->selectedJoint()->parentId.isEmpty());
        QVERIFY(controller->selectedJoint()->name != rigJoint(*controller->selectedRig(), root)->name);
        const auto childPosition = rigJoint(*controller->selectedRig(), child)->position;
        const auto grandchildPosition = rigJoint(*controller->selectedRig(), grandchild)->position;
        controller->selectJoint(child);
        auto value = *controller->selectedJoint(); value.name = " Shoulder "; value.parentId = otherRoot;
        const int beforeReparent = canvas.document()->history()->count(); controller->updateJoint(value);
        QCOMPARE(controller->selectedJoint()->name, QString("Shoulder"));
        QCOMPARE(controller->selectedJoint()->parentId, otherRoot); QCOMPARE(controller->selectedJoint()->position, childPosition);
        QCOMPARE(rigJoint(*controller->selectedRig(), grandchild)->position, grandchildPosition);
        QCOMPARE(canvas.document()->history()->count(), beforeReparent + 1);
        canvas.document()->history()->undo(); QCOMPARE(rigJoint(*controller->selectedRig(), child)->parentId, root);
        canvas.document()->history()->redo();
        controller->selectJoint(otherRoot);
        const auto valid = *controller->selectedRig(); const int beforeInvalid = canvas.document()->history()->count();
        QSignalSpy warnings(controller, &Scene3DController::warning);
        value = *controller->selectedJoint(); value.parentId = grandchild; controller->updateJoint(value);
        QCOMPARE(*controller->selectedRig(), valid); QCOMPARE(canvas.document()->history()->count(), beforeInvalid);
        QCOMPARE(warnings.count(), 1); QVERIFY(!controller->editing());
        controller->selectJoint(child); controller->removeSelectedJoint();
        QCOMPARE(controller->selectedJointId(), otherRoot); QCOMPARE(controller->selectedRig()->joints.size(), 2);
        QVERIFY(rigJoint(*controller->selectedRig(), root)); QVERIFY(rigJoint(*controller->selectedRig(), otherRoot));
        QVERIFY(!rigJoint(*controller->selectedRig(), child)); QVERIFY(!rigJoint(*controller->selectedRig(), grandchild));
        canvas.document()->history()->undo(); QCOMPARE(*controller->selectedRig(), valid);
        canvas.document()->history()->redo(); QCOMPARE(controller->selectedRig()->joints.size(), 2);
    }

    void flatModelRejectsHumanoidWithoutAddingAnEmptyUndoStep() {
        Model3DPtr plane;
        for (const auto &model : builtInModels3D())
            if (model->bounds.maximum.y() == model->bounds.minimum.y()) { plane = model; break; }
        QVERIFY(plane);
        Canvas canvas; canvas.setProject(smallProject()); auto *controller = canvas.scene3D();
        QVERIFY(controller->addModel(plane)); const int history = canvas.document()->history()->count();
        QSignalSpy warnings(controller, &Scene3DController::warning);
        QVERIFY(!controller->createRig(true)); QVERIFY(!warnings.isEmpty());
        QVERIFY(controller->selectedRig()->joints.isEmpty()); QCOMPARE(canvas.document()->history()->count(), history);
        QVERIFY(controller->createRig(false)); QCOMPARE(controller->selectedRig()->joints.size(), 1);
    }

    void mirroredBranchesAndEditsKeepReciprocalPairs() {
        Canvas canvas; canvas.setProject(smallProject()); auto *controller = canvas.scene3D();
        QVERIFY(controller->addModel(builtInModels3D().first())); QVERIFY(controller->createRig(false));
        const QString root = controller->selectedJointId();
        const QString child = controller->addJoint(); auto value = *controller->selectedJoint();
        value.name = "Left arm"; value.position.setX(controller->selectedRig()->mirrorPlaneX + .4f); controller->updateJoint(value);
        const QString tip = controller->addJoint(); controller->selectJoint(child);
        const int beforeMirror = canvas.document()->history()->count(); controller->mirrorSelectedBranch();
        QCOMPARE(canvas.document()->history()->count(), beforeMirror + 1); QCOMPARE(controller->selectedRig()->joints.size(), 5);
        const QString mirroredChild = controller->selectedJoint()->mirrorId;
        const QString mirroredTip = rigJoint(*controller->selectedRig(), tip)->mirrorId;
        QVERIFY(!mirroredChild.isEmpty()); QVERIFY(!mirroredTip.isEmpty());
        QCOMPARE(rigJoint(*controller->selectedRig(), mirroredChild)->parentId, root);
        QCOMPARE(rigJoint(*controller->selectedRig(), mirroredTip)->parentId, mirroredChild);
        QCOMPARE(rigJoint(*controller->selectedRig(), mirroredChild)->mirrorId, child);
        QCOMPARE(rigJoint(*controller->selectedRig(), mirroredTip)->mirrorId, tip);
        controller->setMirrorEdits(true);
        value = *controller->selectedJoint(); value.position += QVector3D(.15f, .2f, .3f); controller->updateJoint(value);
        auto reflected = value.position; reflected.setX(2 * controller->selectedRig()->mirrorPlaneX - reflected.x());
        QCOMPARE(rigJoint(*controller->selectedRig(), mirroredChild)->position, reflected);
        controller->setMirrorEdits(false);
        value = *controller->selectedJoint(); value.position.setY(value.position.y() + .1f); controller->updateJoint(value);
        QCOMPARE(rigJoint(*controller->selectedRig(), mirroredChild)->position, reflected);
        // A deliberate asymmetric pose must survive a later name or hierarchy
        // edit even after mirror editing has been turned back on.
        const auto asymmetricPosition = controller->selectedJoint()->position;
        controller->setMirrorEdits(true); value = *controller->selectedJoint(); value.name = "Upper arm";
        controller->updateJoint(value);
        QCOMPARE(controller->selectedJoint()->position, asymmetricPosition);
        QCOMPARE(rigJoint(*controller->selectedRig(), mirroredChild)->position, reflected);
        value = *controller->selectedJoint(); value.parentId.clear(); controller->updateJoint(value);
        QVERIFY(controller->selectedJoint()->parentId.isEmpty());
        QCOMPARE(controller->selectedJoint()->position, asymmetricPosition);
        QCOMPARE(rigJoint(*controller->selectedRig(), mirroredChild)->position, reflected);
        const auto paired = *controller->selectedRig(); controller->removeSelectedJoint();
        QVERIFY(!rigJoint(*controller->selectedRig(), child)); QVERIFY(!rigJoint(*controller->selectedRig(), tip));
        QVERIFY(rigJoint(*controller->selectedRig(), mirroredChild)->mirrorId.isEmpty());
        QVERIFY(rigJoint(*controller->selectedRig(), mirroredTip)->mirrorId.isEmpty());
        QVERIFY(validateRig3D(*controller->selectedRig()).isEmpty());
        canvas.document()->history()->undo(); QCOMPARE(*controller->selectedRig(), paired);
    }

    void numericGesturesCommitOnceAndCancelWithoutHistory() {
        Canvas canvas; canvas.setProject(smallProject()); auto *controller = canvas.scene3D();
        QVERIFY(controller->addModel(builtInModels3D().first())); QVERIFY(controller->createRig(false));
        const auto original = *controller->selectedRig(); const int history = canvas.document()->history()->count();
        QVERIFY(controller->beginEdit("Move skeleton joint"));
        for (float x : {.2f, .4f, .7f}) { auto joint = *controller->selectedJoint(); joint.position.setX(x); controller->updateJoint(joint); }
        QCOMPARE(canvas.document()->history()->count(), history); QVERIFY(canvas.document()->externalEditActive());
        controller->endEdit(); const auto edited = *controller->selectedRig();
        QCOMPARE(canvas.document()->history()->count(), history + 1); QVERIFY(!canvas.document()->externalEditActive());
        canvas.document()->history()->undo(); QCOMPARE(*controller->selectedRig(), original);
        canvas.document()->history()->redo(); QCOMPARE(*controller->selectedRig(), edited);
        QVERIFY(controller->beginEdit("Move skeleton joint"));
        auto joint = *controller->selectedJoint(); joint.position.setX(1.2f); controller->updateJoint(joint);
        QVERIFY(*controller->selectedRig() != edited); controller->cancelEdit();
        QCOMPARE(*controller->selectedRig(), edited); QCOMPARE(canvas.document()->history()->count(), history + 1);
        QVERIFY(!controller->editing()); QVERIFY(!canvas.document()->externalEditActive());
        QVERIFY(controller->beginEdit("Move skeleton joint")); controller->endEdit();
        QCOMPARE(canvas.document()->history()->count(), history + 1);
    }

    void transformedModelCanvasDragTracksPointerAndEscapeCancels_data() {
        QTest::addColumn<bool>("orthographic");
        QTest::newRow("perspective") << false;
        QTest::newRow("orthographic") << true;
    }
    void transformedModelCanvasDragTracksPointerAndEscapeCancels() {
        QFETCH(bool, orthographic);
        Canvas canvas; canvas.resize(900, 700); canvas.setProject(smallProject()); canvas.show(); canvas.fitSheet();
        auto *controller = canvas.scene3D(); QVERIFY(controller->addModel(builtInModels3D().first()));
        auto object = *controller->selectedObject(); object.position = {.2f, -.1f, .4f};
        object.rotation = {28, 37, -19}; object.scale = {1.7f, .65f, 1.25f}; controller->updateObject(object);
        auto camera = controller->scene()->camera; camera.orthographic = orthographic;
        camera.yaw = -18; camera.pitch = 13; camera.distance = 6; camera.orthoScale = 4;
        camera.target = instanceMatrix(object).map(builtInModels3D().first()->bounds.center()); controller->updateCamera(camera);
        QVERIFY(controller->createRig(false)); auto joint = *controller->selectedJoint();
        joint.position += QVector3D(.18f, .21f, -.1f); controller->updateJoint(joint);
        const auto original = *controller->selectedRig(); const auto objectBefore = *controller->selectedObject();
        const auto cameraBefore = controller->scene()->camera; const int history = canvas.document()->history()->count();
        const auto projected = projectJoint(canvas, joint); const auto start = artworkViewTransform(canvas).map(projected);
        QVERIFY(QRectF(canvas.rect()).contains(start));
        pointer(canvas, QEvent::MouseMove, start, Qt::NoButton, {});
        QCOMPARE(canvas.cursor().shape(), Qt::SizeAllCursor);
        pointer(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
        QVERIFY(controller->editing()); QCOMPARE(controller->selectedJointId(), joint.id);
        const auto end = start + QPointF(45, -32);
        pointer(canvas, QEvent::MouseMove, start + QPointF(25, -16), Qt::NoButton, Qt::LeftButton);
        pointer(canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
        QCOMPARE(canvas.document()->history()->count(), history); QVERIFY(!canvas.document()->strokeActive());
        const auto artworkDelta = artworkViewTransform(canvas).inverted().map(end)
            - artworkViewTransform(canvas).inverted().map(start);
        const auto movedProjection = projectJoint(canvas, *controller->selectedJoint());
        const auto expectedProjection = projected + artworkDelta;
        const auto projectionError = QLineF(movedProjection, expectedProjection).length();
        QVERIFY2(projectionError < .02, qPrintable(QString("Projected joint missed cursor by %1 px: actual (%2, %3), expected (%4, %5)")
            .arg(projectionError, 0, 'g', 9).arg(movedProjection.x(), 0, 'g', 9).arg(movedProjection.y(), 0, 'g', 9)
            .arg(expectedProjection.x(), 0, 'g', 9).arg(expectedProjection.y(), 0, 'g', 9)));
        pointer(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, {});
        QCOMPARE(canvas.document()->history()->count(), history + 1); QVERIFY(!controller->editing());
        const auto moved = *controller->selectedRig(); QVERIFY(moved != original);
        QCOMPARE(controller->selectedObject()->position, objectBefore.position);
        QCOMPARE(controller->selectedObject()->rotation, objectBefore.rotation); QCOMPARE(controller->selectedObject()->scale, objectBefore.scale);
        QCOMPARE(controller->scene()->camera, cameraBefore);
        canvas.document()->history()->undo(); QCOMPARE(*controller->selectedRig(), original);
        canvas.document()->history()->redo(); QCOMPARE(*controller->selectedRig(), moved);
        const auto next = artworkViewTransform(canvas).map(projectJoint(canvas, *controller->selectedJoint()));
        pointer(canvas, QEvent::MouseButtonPress, next, Qt::LeftButton, Qt::LeftButton);
        pointer(canvas, QEvent::MouseMove, next + QPointF(-30, 20), Qt::NoButton, Qt::LeftButton);
        QVERIFY(controller->editing()); QVERIFY(*controller->selectedRig() != moved);
        QTest::keyClick(&canvas, Qt::Key_Escape);
        QCOMPARE(*controller->selectedRig(), moved); QVERIFY(!controller->editing());
        pointer(canvas, QEvent::MouseButtonRelease, next + QPointF(-30, 20), Qt::LeftButton, {});
        QCOMPARE(canvas.document()->history()->count(), history + 1);
    }

    void duplicatedInstancesKeepIndependentSkeletons() {
        Canvas canvas; canvas.setProject(smallProject()); auto *controller = canvas.scene3D();
        const auto model = builtInModels3D().first(); QVERIFY(controller->addModel(model)); QVERIFY(controller->createRig(true));
        const QString firstId = controller->selectedObjectId(); const auto firstRig = *controller->selectedRig();
        controller->duplicateSelected(); const QString duplicateId = controller->selectedObjectId(); QVERIFY(duplicateId != firstId);
        QCOMPARE(*controller->selectedRig(), firstRig); QCOMPARE(controller->scene()->models.size(), 1);
        controller->selectJoint(controller->selectedRig()->joints.first().id);
        auto edited = *controller->selectedJoint(); edited.position += QVector3D(.2f, .3f, -.1f); controller->updateJoint(edited);
        QVERIFY(*controller->selectedRig() != firstRig); const auto duplicateRig = *controller->selectedRig();
        QCOMPARE(instance(*controller->scene(), firstId)->rig, firstRig);
        QVERIFY(controller->addModel(model)); const auto thirdId = controller->selectedObjectId();
        QVERIFY(controller->selectedRig()->joints.isEmpty()); QCOMPARE(controller->scene()->models.size(), 1);
        controller->selectObject(firstId); QCOMPARE(*controller->selectedRig(), firstRig);
        controller->selectObject(duplicateId); QCOMPARE(*controller->selectedRig(), duplicateRig);
        controller->selectObject(thirdId); QVERIFY(controller->selectedRig()->joints.isEmpty());
    }

    void tabletLostContactCommitsOneJointGestureAndAllowsMouseAgain() {
        Canvas canvas; canvas.resize(900, 700); canvas.setProject(smallProject()); canvas.show(); canvas.fitSheet();
        auto *controller = canvas.scene3D(); QVERIFY(controller->addModel(builtInModels3D().first())); QVERIFY(controller->createRig(false));
        const auto original = *controller->selectedRig(); const int history = canvas.document()->history()->count();
        const auto start = artworkViewTransform(canvas).map(projectJoint(canvas, *controller->selectedJoint()));
        QPointingDevice device("Rig test pen", 62, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
        const auto pen = [&](QEvent::Type type, QPointF position, Qt::MouseButtons buttons) {
            QTabletEvent event(type, &device, position, canvas.mapToGlobal(position), buttons ? .7 : 0,
                0, 0, 0, 0, 0, Qt::NoModifier, type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton, buttons);
            QCoreApplication::sendEvent(&canvas, &event);
        };
        pen(QEvent::TabletPress, start, Qt::LeftButton); QVERIFY(controller->editing());
        const auto end = start + QPointF(32, -18);
        pen(QEvent::TabletMove, end, Qt::LeftButton); QVERIFY(*controller->selectedRig() != original);
        const auto moved = *controller->selectedRig(); QCOMPARE(canvas.document()->history()->count(), history);
        // The next hover can arrive without a TabletRelease from the driver.
        // Its new position must end the contact, rather than drag the joint.
        pen(QEvent::TabletMove, end + QPointF(40, 25), {});
        QVERIFY(!controller->editing()); QVERIFY(!canvas.document()->externalEditActive());
        QCOMPARE(*controller->selectedRig(), moved); QCOMPARE(canvas.document()->history()->count(), history + 1);
        pen(QEvent::TabletRelease, end + QPointF(40, 25), {});
        QCOMPARE(canvas.document()->history()->count(), history + 1);
        canvas.document()->history()->undo(); QCOMPARE(*controller->selectedRig(), original);
        canvas.document()->history()->redo(); QCOMPARE(*controller->selectedRig(), moved);
        const auto next = artworkViewTransform(canvas).map(projectJoint(canvas, *controller->selectedJoint()));
        pointer(canvas, QEvent::MouseButtonPress, next, Qt::LeftButton, Qt::LeftButton);
        QVERIFY(controller->editing()); pointer(canvas, QEvent::MouseMove, next + QPointF(15, 9), Qt::NoButton, Qt::LeftButton);
        pointer(canvas, QEvent::MouseButtonRelease, next + QPointF(15, 9), Qt::LeftButton, {});
        QVERIFY(!controller->editing()); QCOMPARE(canvas.document()->history()->count(), history + 2);
    }

    void lockedOrHiddenTargetsRejectRigCommandsAndShowForbiddenCursor_data() {
        QTest::addColumn<QString>("restriction");
        for (const auto *restriction : {"model-lock", "layer-lock", "position-lock", "hidden-model", "hidden-layer", "hidden-scene", "locked-folder", "no-model"})
            QTest::newRow(restriction) << QString::fromLatin1(restriction);
    }
    void lockedOrHiddenTargetsRejectRigCommandsAndShowForbiddenCursor() {
        QFETCH(QString, restriction);
        Canvas canvas; canvas.resize(900, 700); canvas.setProject(smallProject()); canvas.show(); canvas.fitSheet();
        auto *controller = canvas.scene3D(); QVERIFY(controller->addModel(builtInModels3D().first())); QVERIFY(controller->createRig(false));
        const auto joint = *controller->selectedJoint(); const auto at = artworkViewTransform(canvas).map(projectJoint(canvas, joint));
        auto *doc = canvas.document(); const auto layer = controller->layerId();
        if (restriction == "model-lock" || restriction == "hidden-model") {
            auto object = *controller->selectedObject();
            if (restriction == "model-lock") object.locked = true; else object.visible = false;
            controller->updateObject(object);
        } else if (restriction == "layer-lock") doc->setLayersLocked({layer}, true);
        else if (restriction == "position-lock") doc->setLayerPositionLocked(true);
        else if (restriction == "hidden-layer") doc->setLayersVisible({layer}, false);
        else if (restriction == "hidden-scene") {
            const auto selectedModel = controller->selectedObjectId();
            auto after = doc->project();
            for (auto &entry : after.shots.first().layers) if (entry.id == layer) entry.scene3d->visible = false;
            canvas.setProject(after);
            controller->selectObject(selectedModel); controller->selectRigTool(); controller->selectJoint(joint.id);
        }
        else if (restriction == "locked-folder") {
            doc->groupLayers({layer}); const auto folder = doc->project().shots.first().layers[doc->activeLayer()].id;
            doc->setLayersLocked({folder}, true);
        } else controller->selectObject({});
        const auto before = canvas.project().toJson(); const int history = doc->history()->count();
        QVERIFY(!controller->canEditRig()); QVERIFY(!canvas.toolBlockReason().isEmpty());
        pointer(canvas, QEvent::MouseMove, at, Qt::NoButton, {}); QCOMPARE(canvas.cursor().shape(), Qt::ForbiddenCursor);
        QVERIFY(!controller->createRig(false)); QVERIFY(controller->addJoint().isEmpty()); QVERIFY(controller->addJoint(false).isEmpty());
        auto changed = joint; changed.position += QVector3D(1, 1, 1); controller->updateJoint(changed);
        controller->mirrorSelectedBranch(); controller->removeSelectedJoint();
        QSignalSpy blocked(&canvas, &Canvas::actionBlocked);
        pointer(canvas, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
        pointer(canvas, QEvent::MouseMove, at + QPointF(30, 20), Qt::NoButton, Qt::LeftButton);
        pointer(canvas, QEvent::MouseButtonRelease, at + QPointF(30, 20), Qt::LeftButton, {});
        QVERIFY(!blocked.isEmpty()); QVERIFY(!controller->editing()); QVERIFY(!doc->strokeActive());
        QCOMPARE(canvas.project().toJson(), before); QCOMPARE(doc->history()->count(), history);
    }

    void catalogRoutesRigWithoutCreatingArtwork() {
        QTemporaryDir directory;
        BrushLibrary library(directory.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, directory.filePath("tools.json")); QVERIFY(catalog.load());
        Canvas canvas; canvas.setProject(smallProject()); ToolSession session(&canvas, &catalog);
        auto *controller = canvas.scene3D(); QVERIFY(controller->addModel(builtInModels3D().first()));
        QVERIFY(controller->createRig(false));
        QCOMPARE(catalog.activeToolId(), QString("scene-rig")); QCOMPARE(canvas.tool(), Canvas::Tool::SceneRig);
        const auto before = canvas.project().toJson(); const int history = canvas.document()->history()->count();
        controller->selectTool(true); QCOMPARE(catalog.activeToolId(), QString("scene-camera"));
        controller->selectRigTool(); QCOMPARE(catalog.activeToolId(), QString("scene-rig"));
        QCOMPARE(canvas.project().toJson(), before); QCOMPARE(canvas.document()->history()->count(), history);
    }

    void restSkeletonEditsReuseMeshPreviewAndLeaveExportsUnchanged() {
#ifndef TOON_HAS_FILAMENT
        QSKIP("GPU render reuse requires the Filament build");
#else
        Canvas canvas; canvas.resize(900, 700); canvas.setProject(smallProject()); canvas.show(); canvas.fitSheet();
        auto *controller = canvas.scene3D();
        QVERIFY(controller->addModel(builtInModels3D().first()));
        QTRY_VERIFY_WITH_TIMEOUT(!controller->rendering(), 15000);
        const QSize frame(canvas.project().width, canvas.project().height);
        const auto image = scene3DPreview(*controller->scene(), frame, true); QVERIFY(!image.isNull());
        const auto mesh = controller->scene()->models.value(controller->selectedObject()->modelId);
        const auto key = scene3DCacheKey(*controller->scene(), frame); const auto renders = controller->renderCount();
        const auto movie = renderMovieFrame(canvas.project()); const auto sheet = renderSheet(canvas.project(), canvas.shot());
        QVERIFY(!canvas.grab().isNull()); const auto artworkRenders = canvas.artworkRenderCount();
        QVERIFY(controller->createRig(true)); controller->setRigVisible(false); controller->setRigVisible(true);
        controller->selectJoint(controller->selectedRig()->joints.last().id);
        QVERIFY(!canvas.grab().isNull()); QCOMPARE(canvas.artworkRenderCount(), artworkRenders);
        QVERIFY(controller->beginEdit("Move skeleton joint"));
        auto joint = *controller->selectedJoint(); joint.position += QVector3D(.2f, .1f, .3f); controller->updateJoint(joint);
        QVERIFY(!canvas.grab().isNull()); QCOMPARE(canvas.artworkRenderCount(), artworkRenders);
        joint.position.setY(joint.position.y() + .15f); controller->updateJoint(joint);
        QVERIFY(!canvas.grab().isNull()); QCOMPARE(canvas.artworkRenderCount(), artworkRenders);
        controller->endEdit(); QVERIFY(!canvas.grab().isNull()); QCOMPARE(canvas.artworkRenderCount(), artworkRenders);
        QTest::qWait(30);
        QVERIFY(!controller->rendering()); QCOMPARE(controller->renderCount(), renders);
        QCOMPARE(scene3DCacheKey(*controller->scene(), frame), key); QCOMPARE(scene3DPreview(*controller->scene(), frame, true), image);
        QCOMPARE(controller->scene()->models.value(controller->selectedObject()->modelId), mesh);
        QCOMPARE(renderMovieFrame(canvas.project()), movie); QCOMPARE(renderSheet(canvas.project(), canvas.shot()), sheet);
        canvas.document()->history()->undo(); QTest::qWait(10); QCOMPARE(controller->renderCount(), renders);
#endif
    }

    void canvasPreviews() {
        const auto output = qEnvironmentVariable("TOON_UI_PREVIEW");
        if (output.isEmpty()) QSKIP("Set TOON_UI_PREVIEW to save the rig canvas previews");
#ifndef TOON_HAS_FILAMENT
        QSKIP("Rendered mannequin previews require the Filament build");
#else
        Model3DPtr mannequin;
        for (const auto &model : builtInModels3D())
            if (model->name.contains("mannequin", Qt::CaseInsensitive)) { mannequin = model; break; }
        QVERIFY(mannequin);
        Canvas canvas; canvas.resize(1100, 820); canvas.setProject(smallProject()); canvas.show(); canvas.fitSheet();
        auto *controller = canvas.scene3D(); QVERIFY(controller->addModel(mannequin)); QVERIFY(controller->createRig(true));
        for (const auto &joint : controller->selectedRig()->joints)
            if (joint.name == "Chest") { controller->selectJoint(joint.id); break; }
        QVERIFY(QDir().mkpath(output));
        controller->setRigView(Scene3DController::RigView::Front);
        QTRY_VERIFY_WITH_TIMEOUT(!controller->rendering(), 15000);
        QVERIFY(!scene3DPreview(*controller->scene(), {480, 320}, true).isNull());
        QVERIFY(canvas.grab().save(output + "/rig3d-canvas-front.png"));
        auto camera = controller->scene()->camera; camera.orthographic = false; camera.yaw = 30; camera.pitch = 15;
        camera = fitCamera3D(camera, sceneBounds(*controller->scene()), 1.5f); controller->updateCamera(camera);
        QTRY_VERIFY_WITH_TIMEOUT(!controller->rendering(), 15000);
        QVERIFY(canvas.grab().save(output + "/rig3d-canvas-perspective.png"));
#endif
    }
};

QTEST_MAIN(RigWorkspaceTests)
#include "test_rig_workspace.moc"
