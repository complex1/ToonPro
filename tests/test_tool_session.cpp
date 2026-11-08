#include "feature_flags.h"
#include "tool_session.h"
#include "drawing_tool_controller.h"
#include "drawing_tool_panel.h"
#include "guide_panel.h"
#include "value_slider.h"
#include "brush_settings_panel.h"
#include "quick_access_toolbar.h"
#include <QAction>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QMainWindow>
#include <QSettings>
#include <QTabBar>
#include <QToolButton>
#include <QTemporaryDir>
#include <QFile>
#include <QtTest>
using namespace toon;

class ToolSessionTests final : public QObject {
    Q_OBJECT
    static QString toolId(const ToolCatalog &catalog, Canvas::Tool engine, int variant=0, int ordinal=0) {
        for (const auto &tool : catalog.tools())
            if (tool.engine==engine && tool.variant==variant && ordinal--==0) return tool.id;
        return {};
    }
private slots:
    void nudgeAndStrokeFilterOptionsPersistPerTool(){
        QTemporaryDir dir;ToolCatalog catalog(nullptr,dir.filePath("tools.json"));QVERIFY(catalog.load());Canvas canvas;canvas.setProject(Project::create());ToolSession session(&canvas,&catalog);
        const auto brush=catalog.activeToolId(),nudge=toolId(catalog,Canvas::Tool::VectorNudge),ellipse=toolId(catalog,Canvas::Tool::Guides,int(GuideType::Ellipse));QVERIFY(!nudge.isEmpty());QVERIFY(!ellipse.isEmpty());
        canvas.setStabilizationMode(StrokeFilterMode::Stabilizer);canvas.setStabilizationDeadZone(3.5);canvas.setStabilizationFinishAtEndpoint(false);canvas.setStabilization(64);
        QVERIFY(catalog.activateTool(nudge));QCOMPARE(canvas.stabilizationMode(),StrokeFilterMode::Smooth);QCOMPARE(canvas.stabilizationDeadZone(),0.);QVERIFY(canvas.stabilizationFinishAtEndpoint());
        canvas.drawingTools()->nudgeRadius=67;canvas.drawingTools()->nudgeStrength=.42;canvas.notifyToolSettingsEdited();
        QVERIFY(catalog.activateTool(ellipse));QCOMPARE(canvas.guideCreationType(),GuideType::Ellipse);
        QVERIFY(catalog.activateTool(brush));QCOMPARE(canvas.stabilizationMode(),StrokeFilterMode::Stabilizer);QCOMPARE(canvas.stabilizationDeadZone(),3.5);QVERIFY(!canvas.stabilizationFinishAtEndpoint());QCOMPARE(canvas.stabilization(),64.);
        QVERIFY(catalog.activateTool(nudge));QCOMPARE(canvas.drawingTools()->nudgeRadius,67.);QCOMPARE(canvas.drawingTools()->nudgeStrength,.42);session.flush();
        ToolCatalog restored(nullptr,catalog.path());QVERIFY(restored.load());Canvas second;ToolSession other(&second,&restored);QCOMPARE(second.drawingTools()->nudgeRadius,67.);QCOMPARE(second.drawingTools()->nudgeStrength,.42);QVERIFY(restored.activateTool(brush));QCOMPARE(second.stabilizationMode(),StrokeFilterMode::Stabilizer);QCOMPARE(second.stabilizationDeadZone(),3.5);QVERIFY(!second.stabilizationFinishAtEndpoint());
    }
    void fillOptionsAndEraseModeSurviveSwitchesAndReopen(){
        QTemporaryDir dir;BrushLibrary library(dir.filePath("brushes.json"));QVERIFY(library.load());const auto path=dir.filePath("tools.json");
        ToolCatalog catalog(&library,path);QVERIFY(catalog.load());Canvas canvas;canvas.setProject(Project::create());ToolSession session(&canvas,&catalog);DrawingToolPanel panel(&canvas);
        const auto bucket=toolId(catalog,Canvas::Tool::Bucket),eraser=toolId(catalog,Canvas::Tool::VectorEraser);QVERIFY(!bucket.isEmpty());QVERIFY(!eraser.isEmpty());
        auto legacy=catalog.tool(eraser)->state;legacy.options.remove("vectorEraseMode");legacy.options["eraseSegments"]=true;QVERIFY(catalog.updateToolState(eraser,legacy));
        QVERIFY(catalog.activateTool(eraser));QCOMPARE(canvas.drawingTools()->vectorEraseMode,VectorEraseMode::Segment);
        panel.findChild<QTabBar*>("vectorEraseMode")->setCurrentIndex(2);QCOMPARE(canvas.drawingTools()->vectorEraseMode,VectorEraseMode::Intersection);
        QVERIFY(catalog.activateTool(bucket));panel.findChild<QTabBar*>("fillMode")->setCurrentIndex(1);panel.findChild<ValueSlider*>("fillExpansion")->setValue(-4);
        QVERIFY(catalog.activateTool(eraser));QCOMPARE(canvas.drawingTools()->vectorEraseMode,VectorEraseMode::Intersection);
        QVERIFY(catalog.activateTool(bucket));QVERIFY(canvas.drawingTools()->encloseFillMode);QCOMPARE(canvas.drawingTools()->fillSettings.expansion,-4);session.flush();
        ToolCatalog restored(&library,path);QVERIFY(restored.load());Canvas second;second.setProject(Project::create());ToolSession restoredSession(&second,&restored);
        QVERIFY(second.drawingTools()->encloseFillMode);QCOMPARE(second.drawingTools()->fillSettings.expansion,-4);QVERIFY(restored.activateTool(eraser));QCOMPARE(second.drawingTools()->vectorEraseMode,VectorEraseMode::Intersection);
        QCOMPARE(canvas.document()->history()->count(),0);QCOMPARE(second.document()->history()->count(),0);
    }
    void acceptedToolSwitchCommitsDraftToSource_data() {
        QTest::addColumn<qreal>("targetSize"); QTest::addColumn<bool>("floating");
        QTest::newRow("same-value-docked") << qreal(12) << false;
        QTest::newRow("different-value-docked") << qreal(48) << false;
        QTest::newRow("same-value-floating") << qreal(12) << true;
        QTest::newRow("different-value-floating") << qreal(48) << true;
    }
    void acceptedToolSwitchCommitsDraftToSource() {
        QFETCH(qreal, targetSize); QFETCH(bool, floating);
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        QSettings settings(dir.filePath("workspace.ini"), QSettings::IniFormat);
        QMainWindow window; auto *canvas = new Canvas; canvas->setProject(Project::create()); window.setCentralWidget(canvas);
        ToolSession session(canvas, &catalog);
        const auto source = toolId(catalog, Canvas::Tool::Brush), target = toolId(catalog, Canvas::Tool::Brush, 0, 1);
        QVERIFY(!source.isEmpty()); QVERIFY(!target.isEmpty());
        QVERIFY(catalog.activateTool(target)); canvas->setBrushSize(targetSize);
        QVERIFY(catalog.activateTool(source)); canvas->setBrushSize(12);
        auto *properties = new QDockWidget("Properties", &window);
        auto *panel = new BrushSettingsPanel(canvas, &library); properties->setWidget(panel);
        window.addDockWidget(Qt::LeftDockWidgetArea, properties);
        auto *toolbar = new QuickAccessToolbar(canvas, &catalog, &settings, &window); window.addToolBar(toolbar);
        QVERIFY(toolbar->addTool(target));
        window.resize(1000, 720); window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        properties->setFloating(floating); properties->show();
        QWidget *editorWindow = floating ? static_cast<QWidget *>(properties) : &window;
        QVERIFY(QTest::qWaitForWindowExposed(editorWindow)); editorWindow->raise(); editorWindow->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(editorWindow));
        auto *size = panel->findChild<ValueSlider *>("quickBrushSize"); QVERIFY(size);
        auto *number = size->findChild<QDoubleSpinBox *>(); auto *text = number->findChild<QLineEdit *>();
        number->setFocus(); QTRY_VERIFY(number->hasFocus()); number->selectAll(); QTest::keyClicks(number, "27.5");
        QVERIFY(size->editingActive()); QCOMPARE(canvas->brush.size, 12.);
        QSignalSpy committed(size, &ValueSlider::editingFinished);
        catalog.setActivationGuard([] { return false; }); toolbar->itemAction("tool:" + target)->trigger();
        QCOMPARE(catalog.activeToolId(), source); QCOMPARE(text->text(), QString("27.5"));
        QVERIFY(number->hasFocus()); QVERIFY(size->editingActive()); QCOMPARE(committed.count(), 0);
        catalog.setActivationGuard([] { return true; }); toolbar->itemAction("tool:" + target)->trigger();
        QCOMPARE(catalog.activeToolId(), target); QCOMPARE(catalog.tool(source)->state.brush.size, 27.5);
        QCOMPARE(canvas->brush.size, targetSize); QCOMPARE(catalog.tool(target)->state.brush.size, targetSize);
        QCOMPARE(committed.count(), 1); QVERIFY(!size->editingActive());
        window.activateWindow(); canvas->setFocus(); QTRY_VERIFY(canvas->hasFocus());
        QCOMPARE(canvas->brush.size, targetSize); QCOMPARE(size->value(), targetSize);
        QVERIFY(catalog.activateTool(source)); QCOMPARE(canvas->brush.size, 27.5); QCOMPARE(size->value(), 27.5);
        QCOMPARE(catalog.tool(target)->state.brush.size, targetSize); QCOMPARE(canvas->document()->history()->count(), 0);
    }
    void switchingToolDoesNotCommitAnotherWindowsDraft() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        QMainWindow window; auto *canvas = new Canvas; canvas->setProject(Project::create()); window.setCentralWidget(canvas);
        ToolSession session(canvas, &catalog); const auto source = catalog.activeToolId();
        const auto target = toolId(catalog, Canvas::Tool::Brush, 0, 1); QVERIFY(!target.isEmpty());
        Canvas otherCanvas; otherCanvas.setProject(Project::create()); otherCanvas.setBrushSize(9);
        BrushSettingsPanel unrelated(&otherCanvas, &library); unrelated.resize(260, 600); unrelated.show();
        QVERIFY(QTest::qWaitForWindowExposed(&unrelated)); unrelated.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&unrelated));
        auto *size = unrelated.findChild<ValueSlider *>("quickBrushSize"); QVERIFY(size);
        auto *number = size->findChild<QDoubleSpinBox *>(); number->setFocus(); QTRY_VERIFY(number->hasFocus());
        number->selectAll(); QTest::keyClicks(number, "31.5");
        QSignalSpy committed(size, &ValueSlider::editingFinished);
        QVERIFY(catalog.activateTool(target)); QVERIFY(catalog.activeToolId() != source);
        QCOMPARE(committed.count(), 0); QVERIFY(size->editingActive()); QCOMPARE(size->value(), 9.); QCOMPARE(otherCanvas.brush.size, 9.);
        QCOMPARE(number->findChild<QLineEdit *>()->text(), QString("31.5"));
        size->cancelEditing();
    }
    void backgroundCatalogSaveWaitsUntilThePenIsReleased() {
        QTemporaryDir dir;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        Canvas canvas; canvas.setProject(Project::create());
        ToolSession session(&canvas, &catalog);
        session.flush();
        const auto bytes = [&] {
            QFile file(catalog.path());
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
        };
        const auto before = bytes(); QVERIFY(!before.isEmpty());
        canvas.setBrushSize(27);
        QVERIFY(canvas.document()->beginStroke({500, 300}, 1, canvas.brush, false));
        QTest::qWait(650);
        QVERIFY(canvas.document()->strokeActive());
        QCOMPARE(bytes(), before);
        canvas.document()->endStroke();
        QTRY_VERIFY_WITH_TIMEOUT(bytes() != before, 1500);
        const auto after = bytes();
        canvas.setBrushSize(39);
        QVERIFY(canvas.document()->beginStroke({600, 300}, 1, canvas.brush, false));
        // A deliberate flush (e.g. closing the workspace) is still synchronous.
        session.flush();
        QVERIFY(bytes() != after);
        canvas.document()->endStroke();
    }
    void brushEraserAndDuplicateSettingsStayIndependent() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library,dir.filePath("tools.json")); QVERIFY(catalog.load());
        Canvas canvas; canvas.setProject(Project::create()); ToolSession session(&canvas,&catalog);
        const QString brush=toolId(catalog,Canvas::Tool::Brush), second=toolId(catalog,Canvas::Tool::Brush,0,1), eraser=toolId(catalog,Canvas::Tool::Eraser);
        QVERIFY(!brush.isEmpty()); QVERIFY(!second.isEmpty()); QVERIFY(!eraser.isEmpty());
        QVERIFY(catalog.activateTool(brush)); canvas.setBrushSize(83); canvas.setStabilization(61);
        auto settings=canvas.brush; settings.protectTexture=true; settings.texture.depth=.43; canvas.setBrushSettings(settings);
        canvas.setBrushColor(Qt::red); settings=canvas.brush; settings.backgroundColor=Qt::green; canvas.setBrushSettings(settings);
        QVERIFY(catalog.activateTool(second)); canvas.setBrushSize(12);
        QVERIFY(catalog.activateTool(eraser)); canvas.setBrushSize(47); canvas.setStabilization(5);
        QVERIFY(catalog.activateTool(brush)); QCOMPARE(canvas.brush.size,83.); QCOMPARE(canvas.stabilization(),61.);
        QVERIFY(canvas.brush.protectTexture); QCOMPARE(canvas.brush.texture.depth,.43);
        QCOMPARE(canvas.brush.color,QColor(Qt::red)); QCOMPARE(canvas.brush.backgroundColor,QColor(Qt::green));
        canvas.setBrushColor(Qt::blue); QVERIFY(catalog.activateTool(eraser));
        QCOMPARE(canvas.brush.size,47.); QCOMPARE(canvas.stabilization(),5.); QVERIFY(!canvas.brush.protectTexture);
        QCOMPARE(canvas.brush.color,QColor(Qt::blue)); QCOMPARE(canvas.brush.backgroundColor,QColor(Qt::green));
        const auto duplicate=catalog.duplicateTool(eraser,catalog.selectedEntryId()); QVERIFY(!duplicate.isEmpty());
        QVERIFY(catalog.activateTool(duplicate)); canvas.setBrushSize(91);
        QVERIFY(catalog.activateTool(eraser)); QCOMPARE(canvas.brush.size,47.);
        QVERIFY(catalog.activateTool(duplicate)); QCOMPARE(canvas.brush.size,91.);
    }
    void individualToolsRestoreControllerAndPropertyValues() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library,dir.filePath("tools.json")); QVERIFY(catalog.load());
        Canvas canvas; canvas.setProject(Project::create()); ToolSession session(&canvas,&catalog);
        DrawingToolPanel panel(&canvas); panel.resize(240,600); panel.show();
        const auto rectangle=toolId(catalog,Canvas::Tool::Rectangle), ellipse=toolId(catalog,Canvas::Tool::Ellipse);
        QVERIFY(catalog.activateTool(rectangle)); auto *width=panel.findChild<ValueSlider*>("shapeStrokeWidth"); QVERIFY(width); width->setValue(17.5);
        QCOMPARE(catalog.tool(rectangle)->state.options.value("strokeWidth").toDouble(),17.5);
        QVERIFY(catalog.activateTool(ellipse)); width->setValue(4.2);
        QVERIFY(catalog.activateTool(rectangle)); QCOMPARE(canvas.drawingTools()->strokeWidth,17.5); QCOMPARE(width->value(),17.5);
        const auto linear=toolId(catalog,Canvas::Tool::Gradient,0), radial=toolId(catalog,Canvas::Tool::Gradient,1);
        QVERIFY(catalog.activateTool(linear)); auto *stops=panel.findChild<GradientStopsWidget*>(); QVERIFY(stops); stops->setSelectedColor(Qt::red);
        QVERIFY(catalog.activateTool(radial)); QVERIFY(canvas.drawingTools()->radialGradient); QCOMPARE(stops->stops().first().second,QColor(Qt::black));
        QVERIFY(catalog.activateTool(linear)); QVERIFY(!canvas.drawingTools()->radialGradient); QCOMPARE(stops->stops().first().second,QColor(Qt::red));
        QVERIFY(panel.findChild<QTabBar*>("gradientMode")->isHidden());
        const auto smudge=toolId(catalog,Canvas::Tool::Retouch,0), blur=toolId(catalog,Canvas::Tool::Retouch,2);
        QVERIFY(catalog.activateTool(smudge)); auto *size=panel.findChild<ValueSlider*>("retouchSize"); size->setValue(67);
        QVERIFY(catalog.activateTool(blur)); size->setValue(21);
        QVERIFY(catalog.activateTool(smudge)); QCOMPARE(canvas.drawingTools()->retouchSettings.mode,RetouchMode::Smudge); QCOMPARE(size->value(),67.);
        QVERIFY(panel.findChild<QTabBar*>("retouchMode")->isHidden());
    }
    void directCanvasRequestsKeepCatalogInSyncAndRememberTools() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library,dir.filePath("tools.json")); QVERIFY(catalog.load());
        Canvas canvas; canvas.setProject(Project::create()); ToolSession session(&canvas,&catalog);
        const auto brush=toolId(catalog,Canvas::Tool::Brush,0,1); QVERIFY(catalog.activateTool(brush)); canvas.setBrushSize(71);
        canvas.setTool(Canvas::Tool::Eraser); QCOMPARE(catalog.tool(catalog.activeToolId())->engine,Canvas::Tool::Eraser);
        canvas.setBrushSize(29); canvas.setTool(Canvas::Tool::Brush); QCOMPARE(catalog.activeToolId(),brush); QCOMPARE(canvas.brush.size,71.);
        const auto before=catalog.activeToolId(); canvas.setTool(Canvas::Tool::VectorPen); QCOMPARE(catalog.activeToolId(),before); QCOMPARE(canvas.tool(),Canvas::Tool::Brush);
        const auto fisheye=toolId(catalog,Canvas::Tool::Guides,int(GuideType::Fisheye)); QVERIFY(catalog.activateTool(fisheye));
        GuidePanel panel(&canvas); QCOMPARE(canvas.guideCreationType(),GuideType::Fisheye); QVERIFY(panel.findChild<QTabBar*>("guideTypes")->isHidden());
        const auto warp=toolId(catalog,Canvas::Tool::AdvancedTransform,1); QVERIFY(!warp.isEmpty()); QVERIFY(catalog.activateTool(warp)); QVERIFY(canvas.drawingTools()->warpMode);
    }
    void sceneToolsKeepDrawingSettingsAndRouteRigRequests() {
        if (!features::legacy3DEditor) QSKIP("Legacy 3D authoring is retired; compatibility coverage remains enabled.");
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        Canvas canvas; canvas.setProject(Project::create()); ToolSession session(&canvas, &catalog);
        const auto brushId = catalog.activeToolId();
        canvas.setBrushSize(73); canvas.setStabilization(42);
        auto brush = canvas.brush; brush.texture.depth = .37; brush.color = Qt::red; canvas.setBrushSettings(brush);
        const auto before = canvas.brush; const auto brushState = catalog.tool(brushId)->state;
        canvas.drawingTools()->strokeWidth = 28.5;
        canvas.drawingTools()->fillSettings.tolerance = 81;
        for (const auto engine : {Canvas::Tool::SceneObject, Canvas::Tool::SceneCamera, Canvas::Tool::SceneRig}) {
            const auto id = toolId(catalog, engine); QVERIFY(!id.isEmpty());
            const auto stored = catalog.tool(id)->state;
            canvas.setTool(engine);
            QCOMPARE(catalog.activeToolId(), id); QCOMPARE(canvas.tool(), engine);
            QCOMPARE(canvas.brush, before); QCOMPARE(canvas.stabilization(), 42.);
            QCOMPARE(canvas.drawingTools()->strokeWidth, 28.5); QCOMPARE(canvas.drawingTools()->fillSettings.tolerance, 81);
            session.flush(); QCOMPARE(catalog.tool(id)->state, stored);
        }
        QCOMPARE(catalog.activeToolId(), QString("scene-rig"));
        session.flush();
        ToolCatalog restored(&library, dir.filePath("tools.json")); QVERIFY(restored.load());
        Canvas other; auto customBrush = other.brush; customBrush.size = 46; other.setBrushSettings(customBrush);
        ToolSession restoredSession(&other, &restored);
        QCOMPARE(other.tool(), Canvas::Tool::SceneRig); QCOMPARE(other.brush.size, 46.);
        QVERIFY(catalog.activateTool(brushId)); QCOMPARE(canvas.brush, before);
        QCOMPARE(catalog.tool(brushId)->state.brush, brushState.brush);
        QCOMPARE(canvas.document()->history()->count(), 0);
    }
    void menuBoundaryTransformKeepsRequestedTarget() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library,dir.filePath("tools.json")); QVERIFY(catalog.load());
        Canvas canvas; canvas.setProject(Project::create()); ToolSession session(&canvas,&catalog);
        DrawingToolPanel panel(&canvas); panel.show();
        canvas.document()->selectAllPixels();
        QVERIFY(canvas.drawingTools()->beginAdvanced({},true));
        QCOMPARE(canvas.tool(),Canvas::Tool::AdvancedTransform);
        QCOMPARE(catalog.tool(catalog.activeToolId())->engine,Canvas::Tool::AdvancedTransform);
        QVERIFY(canvas.drawingTools()->selectionBoundary);
        QVERIFY(panel.findChild<QToolButton*>("transformBoundaryTarget")->isChecked());
        QVERIFY(!panel.findChild<QToolButton*>("transformLayerTarget")->isChecked());
        QVERIFY(catalog.tool(catalog.activeToolId())->state.options.value("selectionBoundary").toBool());
        canvas.drawingTools()->cancel();
        QVERIFY(session.activateEngine(Canvas::Tool::Brush));
        QVERIFY(session.activateEngine(Canvas::Tool::AdvancedTransform));
        QVERIFY(canvas.drawingTools()->selectionBoundary);
        QVERIFY(panel.findChild<QToolButton*>("transformBoundaryTarget")->isChecked());
    }
    void emptyGroupKeepsToolAndNoPlacementsSuspendDrawing() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library,dir.filePath("tools.json")); QVERIFY(catalog.load());
        Canvas canvas; canvas.resize(800,600); canvas.setProject(Project::create()); ToolSession session(&canvas,&catalog); canvas.show();
        const auto active=catalog.activeToolId();
        const auto empty=catalog.addGroup("Empty","folder",{}); QVERIFY(!empty.isEmpty()); QVERIFY(catalog.selectEntry(empty));
        QCOMPARE(catalog.activeToolId(),active); QVERIFY(canvas.toolAvailable()); QVERIFY(catalog.visibleToolIds().isEmpty());
        const auto placements=catalog.entries();
        for (const auto &entry : placements) QVERIFY(catalog.removeEntry(entry.id));
        QVERIFY(!canvas.toolAvailable());
        const int history=canvas.document()->history()->count();
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,{390,300}); QTest::mouseMove(&canvas,{430,320}); QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,{430,320});
        QCOMPARE(canvas.document()->history()->count(),history); QVERIFY(!canvas.document()->strokeActive());
        const auto center=canvas.center(); QTest::mousePress(&canvas,Qt::MiddleButton,Qt::NoModifier,{400,300}); QTest::mouseMove(&canvas,{430,320}); QTest::mouseRelease(&canvas,Qt::MiddleButton,Qt::NoModifier,{430,320}); QVERIFY(canvas.center()!=center);
        QVERIFY(session.activateEngine(Canvas::Tool::Brush)); QVERIFY(canvas.toolAvailable()); QVERIFY(!catalog.activeToolId().isEmpty());
    }
    void shortcutsRestoreLastGroupToolAfterReopen() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        const auto path=dir.filePath("tools.json");
        QString brush;
        {
            ToolCatalog catalog(&library,path); QVERIFY(catalog.load()); Canvas canvas; ToolSession session(&canvas,&catalog);
            brush=toolId(catalog,Canvas::Tool::Brush,0,1); QVERIFY(catalog.activateTool(brush)); canvas.setBrushSize(72);
            QVERIFY(catalog.activateTool(toolId(catalog,Canvas::Tool::Rectangle))); session.flush();
        }
        ToolCatalog catalog(&library,path); QVERIFY(catalog.load()); Canvas canvas; ToolSession session(&canvas,&catalog);
        QVERIFY(session.activateEngine(Canvas::Tool::Brush)); QCOMPARE(catalog.activeToolId(),brush); QCOMPARE(canvas.brush.size,72.);
    }
    void stateRoundTripsAndCanvasCanDieBeforeSession() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        const auto path=dir.filePath("tools.json"); ToolCatalog catalog(&library,path); QVERIFY(catalog.load());
        auto *canvas=new Canvas; canvas->setProject(Project::create()); auto *session=new ToolSession(canvas,&catalog);
        const auto rectangle=toolId(catalog,Canvas::Tool::Rectangle); QVERIFY(catalog.activateTool(rectangle));
        canvas->drawingTools()->strokeWidth=33.5; canvas->drawingTools()->shapeFill=true; canvas->drawingTools()->fillColor=QColor("#52ab72"); session->flush();
        ToolCatalog restored(&library,path); QVERIFY(restored.load()); Canvas second; second.setProject(Project::create()); ToolSession secondSession(&second,&restored);
        QVERIFY(restored.activateTool(rectangle)); QCOMPARE(second.drawingTools()->strokeWidth,33.5); QVERIFY(second.drawingTools()->shapeFill); QCOMPARE(second.drawingTools()->fillColor,QColor("#52ab72"));
        delete canvas; delete session;
    }
};
QTEST_MAIN(ToolSessionTests)
#include "test_tool_session.moc"
