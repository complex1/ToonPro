#include "canvas.h"
#include "drawing_tool_controller.h"
#include "drawing_panels.h"
#include "layer_panel.h"
#include "object_tools.h"
#include <QApplication>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QTreeWidget>
#include <QtTest>

using namespace toon;
namespace {
using Tool = Canvas::Tool;
void setup(Canvas &canvas) {
    auto project=Project::create(); project.width=640; project.height=480; project.transparent=true;
    canvas.resize(1000,800); canvas.setProject(project); canvas.show(); canvas.setZoom(1);
    QApplication::processEvents();
}
QPointF view(Canvas &canvas,QPointF point) {
    return canvas.viewTransform().map(point+sheetGeometry(canvas.project(),canvas.shot()).output.topLeft());
}
void mouse(Canvas &canvas,QEvent::Type type,QPointF point,Qt::KeyboardModifiers mods=Qt::NoModifier,
           Qt::MouseButton button=Qt::LeftButton) {
    const auto p=view(canvas,point);
    QMouseEvent event(type,p,p,canvas.mapToGlobal(p),type==QEvent::MouseMove?Qt::NoButton:button,
        type==QEvent::MouseButtonRelease?Qt::NoButton:button,mods);
    QApplication::sendEvent(&canvas,&event);
}
void hover(Canvas &canvas,QPointF point={100,100},Qt::KeyboardModifiers mods=Qt::NoModifier) {
    mouse(canvas,QEvent::MouseMove,point,mods,Qt::NoButton);
}
void click(Canvas &canvas,QPointF point={100,100},Qt::KeyboardModifiers mods=Qt::NoModifier) {
    mouse(canvas,QEvent::MouseButtonPress,point,mods); mouse(canvas,QEvent::MouseButtonRelease,point,mods);
}
const RasterLayer &active(Canvas &canvas) { return canvas.shot().layers[canvas.document()->activeLayer()]; }
void select(Canvas &canvas,const QString &id) {
    for(int i=0;i<canvas.shot().layers.size();++i)if(canvas.shot().layers[i].id==id){canvas.document()->selectLayer(i);return;}
}
QString path(Canvas &canvas) {
    return ObjectTools::addPath(canvas.document(),{BezierNode::corner({60,100}),BezierNode::corner({180,100})},false,Qt::red,12);
}
bool tablet(Canvas &canvas,const QPointingDevice &device,QEvent::Type type,QPointF point) {
    const auto p=view(canvas,point); const bool pressed=type==QEvent::TabletPress;
    QTabletEvent event(type,&device,p,canvas.mapToGlobal(p),pressed?.7:0,0,0,0,0,0,Qt::NoModifier,
        type==QEvent::TabletMove?Qt::NoButton:Qt::LeftButton,pressed?Qt::LeftButton:Qt::NoButton);
    QApplication::sendEvent(&canvas,&event); return event.isAccepted();
}
}

class CanvasAvailabilityTest : public QObject {
    Q_OBJECT
private slots:
    void vectorLayerToolCompatibility() {
        Canvas canvas; setup(canvas); QVERIFY(ObjectTools::addLayer(canvas.document(),LayerKind::Vector)); hover(canvas);
        for(auto tool:{Tool::Bucket,Tool::Gradient,Tool::Retouch}) {
            canvas.setTool(tool); QVERIFY(canvas.toolBlockReason().contains("raster"));
            QCOMPARE(canvas.cursor().shape(),Qt::ForbiddenCursor); QVERIFY(!canvas.interactionHint().isEmpty());
        }
        for(auto tool:{Tool::Brush,Tool::Eraser,Tool::Line,Tool::Rectangle,Tool::Ellipse,Tool::Polygon,Tool::Curve}) {
            canvas.setTool(tool); QVERIFY2(canvas.toolBlockReason().isEmpty(),qPrintable(canvas.toolBlockReason()));
            QVERIFY(canvas.cursor().shape()!=Qt::ForbiddenCursor);
        }
        for(auto tool:{Tool::VectorNode,Tool::VectorSimplify,Tool::VectorWidth,Tool::VectorEraser}) {
            canvas.setTool(tool); QVERIFY(canvas.toolBlockReason().contains("Draw a vector path"));
        }
        QVERIFY(!path(canvas).isEmpty());
        for(auto tool:{Tool::VectorNode,Tool::VectorSimplify,Tool::VectorWidth,Tool::VectorEraser}) {
            canvas.setTool(tool); QVERIFY2(canvas.toolBlockReason().isEmpty(),qPrintable(canvas.toolBlockReason()));
        }
        canvas.document()->selectLayer(0); canvas.setTool(Tool::VectorNode);
        QVERIFY(canvas.toolBlockReason().contains("vector layer"));
    }
    void ancestorRestrictionsRefreshWithoutPointerMovement() {
        Canvas canvas; setup(canvas); auto *doc=canvas.document(); const auto child=active(canvas).id;
        doc->groupLayers({child}); const auto folder=active(canvas).id; select(canvas,child); hover(canvas);
        QSignalSpy hints(&canvas,&Canvas::interactionHintChanged);
        doc->setLayersLocked({folder},true);
        QCOMPARE(canvas.cursor().shape(),Qt::ForbiddenCursor); QVERIFY(canvas.interactionHint().contains("parent folder"));
        QVERIFY(!hints.isEmpty()); doc->setLayersLocked({folder},false);
        QCOMPARE(canvas.cursor().shape(),Qt::BlankCursor); QVERIFY(canvas.interactionHint().isEmpty());
        doc->setLayersVisible({folder},false); QCOMPARE(canvas.cursor().shape(),Qt::ForbiddenCursor);
        QVERIFY(canvas.interactionHint().contains("Show")); doc->setLayersVisible({folder},true);
        select(canvas,folder); QVERIFY(canvas.toolBlockReason().contains("inside the folder"));
        select(canvas,child); doc->setLayersLocked({child},true); QVERIFY(canvas.toolBlockReason().contains("Unlock the layer"));
    }
    void selectionsAndSamplingDoNotRequireEditableArtwork() {
        Canvas canvas; setup(canvas); auto *doc=canvas.document(); const auto id=active(canvas).id;
        doc->setLayersLocked({id},true); doc->setLayersVisible({id},false);
        for(auto tool:{Tool::RectangleSelect,Tool::LassoSelect,Tool::EllipseSelect,Tool::PolygonSelect,
                      Tool::MagicWand,Tool::ColorRange,Tool::Eyedropper,Tool::Hand}) {
            canvas.setTool(tool); QVERIFY2(canvas.toolBlockReason().isEmpty(),qPrintable(canvas.toolBlockReason()));
        }
        canvas.setTool(Tool::RectangleSelect); mouse(canvas,QEvent::MouseButtonPress,{60,60});
        mouse(canvas,QEvent::MouseMove,{160,160}); mouse(canvas,QEvent::MouseButtonRelease,{160,160});
        QVERIFY(doc->rasterSelection().path.contains({100,100}));
        auto *tools=canvas.drawingTools(); tools->selectionBoundary=true; canvas.setTool(Tool::AdvancedTransform);
        QVERIFY(canvas.toolBlockReason().isEmpty());
        tools->fillSettings.source=RasterSampleSource::ReferenceLayer; tools->fillSettings.referenceLayerId.clear();
        canvas.setTool(Tool::Eyedropper); QVERIFY(canvas.toolBlockReason().contains("reference layer"));
        tools->fillSettings.referenceLayerId=id; tools->notifySettings(); QVERIFY(canvas.toolBlockReason().isEmpty());
    }
    void alphaAndPositionLocksHaveToolSpecificRules() {
        Canvas canvas; setup(canvas); canvas.document()->setLayerAlphaLocked(true);
        for(auto tool:{Tool::Brush,Tool::Bucket,Tool::Gradient,Tool::Retouch,Tool::Rectangle}) {
            canvas.setTool(tool); QVERIFY(canvas.toolBlockReason().isEmpty());
        }
        canvas.setTool(Tool::Eraser); QVERIFY(canvas.toolBlockReason().contains("alpha lock"));
        QVERIFY(ObjectTools::addLayer(canvas.document(),LayerKind::Vector)); QVERIFY(!path(canvas).isEmpty());
        canvas.document()->setLayerPositionLocked(true); canvas.setTool(Tool::VectorNode);
        auto *tools=canvas.drawingTools(); tools->setPathEditMode(DrawingToolController::PathEditMode::Transform);
        QVERIFY(canvas.toolBlockReason().contains("position"));
        tools->setPathEditMode(DrawingToolController::PathEditMode::Nodes); QVERIFY(!canvas.toolBlockReason().isEmpty());
        tools->setPathEditMode(DrawingToolController::PathEditMode::Width); QVERIFY(canvas.toolBlockReason().isEmpty());
        canvas.setTool(Tool::VectorSimplify); QVERIFY(canvas.toolBlockReason().contains("position"));
    }
    void blockedMouseAndPenAttemptsLeaveArtworkAndHistoryUntouched() {
        Canvas canvas; setup(canvas); QVERIFY(ObjectTools::addLayer(canvas.document(),LayerKind::Vector));
        canvas.setTool(Tool::Gradient); hover(canvas); const auto before=canvas.project().toJson();
        const auto frame=renderMovieFrame(canvas.project()); const int history=canvas.document()->history()->count();
        QSignalSpy blocked(&canvas,&Canvas::actionBlocked); click(canvas); QCOMPARE(blocked.size(),1);
        QPointingDevice pen("Availability pen",42,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
        const auto p=view(canvas,{100,100});
        for(auto type:{QEvent::TabletPress,QEvent::TabletRelease}) {
            QTabletEvent event(type,&pen,p,canvas.mapToGlobal(p),type==QEvent::TabletPress?.7:0,0,0,0,0,0,
                Qt::NoModifier,Qt::LeftButton,type==QEvent::TabletRelease?Qt::NoButton:Qt::LeftButton);
            QApplication::sendEvent(&canvas,&event); QVERIFY(event.isAccepted());
        }
        QCOMPARE(blocked.size(),2); QCOMPARE(canvas.project().toJson(),before);
        QCOMPARE(renderMovieFrame(canvas.project()),frame); QCOMPARE(canvas.document()->history()->count(),history);
        QVERIFY(!canvas.document()->editingGestureActive()); QVERIFY(!canvas.drawingTools()->active());
    }
    void panningAndTemporaryEyedropperWorkWhilePaintingIsBlocked() {
        Canvas canvas; setup(canvas); auto *doc=canvas.document();
        BrushSettings brush; brush.color=Qt::red; brush.size=30; brush.pressureSize=false;
        QVERIFY(doc->beginStroke({100,100},1,brush,false)); doc->endStroke(); canvas.setBrushColor(Qt::blue);
        doc->setLayersLocked({active(canvas).id},true); hover(canvas); QCOMPARE(canvas.cursor().shape(),Qt::ForbiddenCursor);
        const int history=doc->history()->count(); QSignalSpy blocked(&canvas,&Canvas::actionBlocked);
        hover(canvas,{100,100},Qt::AltModifier); QCOMPARE(canvas.cursor().shape(),Qt::CrossCursor);
        QVERIFY(canvas.interactionHint().isEmpty()); click(canvas,{100,100},Qt::AltModifier);
        QCOMPARE(canvas.brush.color,QColor(Qt::red)); hover(canvas); QCOMPARE(canvas.cursor().shape(),Qt::ForbiddenCursor);
        QTest::keyPress(&canvas,Qt::Key_Space); QCOMPARE(canvas.cursor().shape(),Qt::OpenHandCursor);
        auto center=canvas.center(); mouse(canvas,QEvent::MouseButtonPress,{100,100});
        QCOMPARE(canvas.cursor().shape(),Qt::ClosedHandCursor); mouse(canvas,QEvent::MouseMove,{150,120});
        mouse(canvas,QEvent::MouseButtonRelease,{150,120}); QVERIFY(canvas.center()!=center);
        QTest::keyRelease(&canvas,Qt::Key_Space); QCOMPARE(canvas.cursor().shape(),Qt::ForbiddenCursor);
        center=canvas.center(); mouse(canvas,QEvent::MouseButtonPress,{100,100},Qt::NoModifier,Qt::MiddleButton);
        mouse(canvas,QEvent::MouseMove,{140,110},Qt::NoModifier,Qt::MiddleButton);
        mouse(canvas,QEvent::MouseButtonRelease,{140,110},Qt::NoModifier,Qt::MiddleButton);
        QVERIFY(canvas.center()!=center); QVERIFY(blocked.isEmpty()); QCOMPARE(doc->history()->count(),history);
    }
    void guideVisibilityAndHoverFeedbackDoNotInvalidateArtwork() {
        Canvas canvas; setup(canvas); canvas.setTool(Tool::Guides);
        auto settings=canvas.guideSettings(); settings.visible=false; canvas.setGuideSettings(settings); hover(canvas);
        QVERIFY(canvas.toolBlockReason().contains("Show guides")); QCOMPARE(canvas.cursor().shape(),Qt::ForbiddenCursor);
        canvas.beginGuideCreation(GuideType::Perspective); QVERIFY(canvas.toolBlockReason().isEmpty()); canvas.cancelGuideCreation();
        canvas.setTool(Tool::Brush); canvas.document()->setLayersLocked({active(canvas).id},true); hover(canvas);
        canvas.grab(); const auto renders=canvas.artworkRenderCount(); const int history=canvas.document()->history()->count();
        QSignalSpy blocked(&canvas,&Canvas::actionBlocked); QSignalSpy hints(&canvas,&Canvas::interactionHintChanged);
        for(int i=0;i<20;++i){hover(canvas,{100.+i,100.});canvas.grab();}
        QCOMPARE(canvas.artworkRenderCount(),renders); QVERIFY(blocked.isEmpty()); QVERIFY(hints.isEmpty());
        QCOMPARE(canvas.document()->history()->count(),history);
        QEvent leave(QEvent::Leave); QApplication::sendEvent(&canvas,&leave); QVERIFY(canvas.interactionHint().isEmpty());
        QCOMPARE(hints.size(),1);
    }
    void detailedSelectionBlocksMeshWarpWithoutTrappingTheCanvas() {
        Canvas canvas; setup(canvas); auto *tools = canvas.drawingTools();
        QPainterPath islands;
        for (int y = 10; y < 460; y += 8) for (int x = 10; x < 620; x += 8) islands.addRect(x, y, 3, 3);
        canvas.document()->setRasterSelection(islands);
        tools->selectionBoundary = true; tools->warpMode = true;
        canvas.setTool(Tool::AdvancedTransform); hover(canvas);
        QVERIFY(canvas.toolBlockReason().contains("too detailed for mesh warp"));
        QCOMPARE(canvas.cursor().shape(), Qt::ForbiddenCursor);
        QVERIFY(!tools->transform()->active());
        tools->warpMode = false; tools->notifySettings(); canvas.setTool(Tool::AdvancedTransform);
        QVERIFY(canvas.toolBlockReason().isEmpty()); QVERIFY(tools->transform()->active());
        tools->transform()->cancel();
        canvas.document()->clearRasterSelection(); QVERIFY(!canvas.document()->rasterSelection().active);
    }
    void vectorTransformExplainsWarpAndPixelSelectionRestrictions() {
        Canvas canvas; setup(canvas); QVERIFY(ObjectTools::addLayer(canvas.document(),LayerKind::Vector));
        QVERIFY(!path(canvas).isEmpty()); auto *tools=canvas.drawingTools();
        tools->warpMode=true; canvas.setTool(Tool::AdvancedTransform); hover(canvas);
        QVERIFY(canvas.toolBlockReason().contains("Mesh Warp"));
        QCOMPARE(canvas.cursor().shape(),Qt::ForbiddenCursor); QVERIFY(!tools->transform()->active());
        tools->warpMode=false; tools->notifySettings(); canvas.setTool(Tool::AdvancedTransform);
        QVERIFY(canvas.toolBlockReason().isEmpty()); QVERIFY(tools->transform()->active());
        tools->transform()->cancel(); canvas.document()->selectAllPixels();
        canvas.setTool(Tool::AdvancedTransform);
        QVERIFY(canvas.toolBlockReason().contains("Clear the pixel selection"));
        QVERIFY(!tools->transform()->active());
        tools->selectionBoundary=true; canvas.setTool(Tool::AdvancedTransform);
        QVERIFY(canvas.toolBlockReason().isEmpty()); QVERIFY(tools->transform()->active());
    }
    void lockedGuideClickExplainsFailureWithoutChangingArtwork() {
        Canvas canvas; setup(canvas); canvas.setTool(Tool::Guides);
        canvas.addDefaultGuide(GuideType::VanishingPoint);
        auto guide=canvas.drawingGuides().first(); guide.locked=true; canvas.updateGuide(guide);
        const auto before=canvas.project().toJson(); const int history=canvas.document()->history()->count();
        QSignalSpy warnings(canvas.document(),&Document::warning);
        click(canvas,guide.points.first());
        QCOMPARE(warnings.size(),1); QVERIFY(warnings.first().first().toString().contains("Unlock this guide"));
        QCOMPARE(canvas.project().toJson(),before); QCOMPARE(canvas.document()->history()->count(),history);
    }
    void transformWithoutSelectedPixelsStaysBlockedUntilSelectionChanges() {
        Canvas canvas; setup(canvas); auto *doc=canvas.document();
        BrushSettings brush; brush.size=20; QVERIFY(doc->beginStroke({100,100},1,brush,false)); doc->endStroke();
        QPainterPath selection; selection.addRect(300,300,40,40); doc->setRasterSelection(selection);
        canvas.setTool(Tool::Transform); hover(canvas);
        QVERIFY(!doc->transformActive()); QVERIFY(!canvas.toolBlockReason().isEmpty());
        QCOMPARE(canvas.cursor().shape(),Qt::ForbiddenCursor);
        const auto before=canvas.project().toJson(); const int history=doc->history()->count();
        QSignalSpy blocked(&canvas,&Canvas::actionBlocked); click(canvas,{320,320}); QCOMPARE(blocked.size(),1);
        QCOMPARE(canvas.project().toJson(),before); QCOMPARE(doc->history()->count(),history);
        doc->clearRasterSelection(); QVERIFY(canvas.toolBlockReason().isEmpty());
        click(canvas); QVERIFY(doc->transformActive()); doc->cancelTransform();
        doc->setRasterSelection(selection); canvas.setTool(Tool::AdvancedTransform);
        QVERIFY(!canvas.drawingTools()->transform()->active()); QVERIFY(!canvas.toolBlockReason().isEmpty());
        QCOMPARE(canvas.cursor().shape(),Qt::ForbiddenCursor);
        doc->clearRasterSelection(); QVERIFY(canvas.toolBlockReason().isEmpty()); click(canvas);
        QVERIFY(canvas.drawingTools()->transform()->active());
    }
    void emptySelectionBlocksPaintingButNotSelectionOrVectorShapes() {
        Canvas canvas; setup(canvas); auto *doc=canvas.document();
        doc->setRasterSelection(QPainterPath()); QVERIFY(doc->rasterSelection().active);
        for(auto tool:{Tool::Brush,Tool::Eraser,Tool::Bucket,Tool::Gradient,Tool::Retouch,
                      Tool::Line,Tool::Rectangle,Tool::Ellipse,Tool::Polygon,Tool::Curve}) {
            canvas.setTool(tool); QVERIFY2(!canvas.toolBlockReason().isEmpty(),"An empty selection cannot receive painted pixels.");
            QCOMPARE(canvas.cursor().shape(),Qt::ForbiddenCursor);
        }
        canvas.setTool(Tool::RectangleSelect); QVERIFY(canvas.toolBlockReason().isEmpty());
        canvas.setTool(Tool::Eyedropper); QVERIFY(canvas.toolBlockReason().isEmpty());
        QVERIFY(ObjectTools::addLayer(doc,LayerKind::Vector)); doc->setRasterSelection(QPainterPath());
        canvas.setTool(Tool::Brush); QVERIFY(!canvas.toolBlockReason().isEmpty());
        canvas.setTool(Tool::Rectangle); QVERIFY(canvas.toolBlockReason().isEmpty());
        mouse(canvas,QEvent::MouseButtonPress,{50,50}); mouse(canvas,QEvent::MouseMove,{150,150});
        mouse(canvas,QEvent::MouseButtonRelease,{150,150}); QCOMPARE(active(canvas).objects.size(),1);
        canvas.setTool(Tool::VectorNode); QVERIFY(canvas.toolBlockReason().isEmpty());
        canvas.setTool(Tool::Eraser); QVERIFY(canvas.toolBlockReason().isEmpty());
        canvas.setTool(Tool::Brush); doc->clearRasterSelection(); QVERIFY(canvas.toolBlockReason().isEmpty());
    }
    void penEraserHoverUsesAlphaLockAndResetsForPenOrMouse() {
        Canvas canvas; setup(canvas); auto *doc=canvas.document(); doc->setLayerAlphaLocked(true); hover(canvas);
        QPointingDevice eraser("Eraser end",43,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Eraser,
            QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
        QPointingDevice pen("Pen end",44,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
        const int history=doc->history()->count(); QSignalSpy blocked(&canvas,&Canvas::actionBlocked);
        QVERIFY(tablet(canvas,eraser,QEvent::TabletMove,{100,100}));
        QCOMPARE(canvas.tool(),Tool::Brush); QCOMPARE(canvas.cursor().shape(),Qt::ForbiddenCursor);
        QVERIFY(canvas.interactionHint().contains("alpha lock")); QVERIFY(canvas.brushCursorPath().isEmpty());
        QVERIFY(tablet(canvas,eraser,QEvent::TabletPress,{100,100}));
        QVERIFY(tablet(canvas,eraser,QEvent::TabletRelease,{100,100})); QCOMPARE(blocked.size(),1);
        QVERIFY(tablet(canvas,pen,QEvent::TabletMove,{100,100}));
        QCOMPARE(canvas.cursor().shape(),Qt::BlankCursor); QVERIFY(canvas.interactionHint().isEmpty());
        QVERIFY(tablet(canvas,eraser,QEvent::TabletMove,{100,100})); QCOMPARE(canvas.cursor().shape(),Qt::ForbiddenCursor);
        hover(canvas); QCOMPARE(canvas.cursor().shape(),Qt::BlankCursor); QVERIFY(canvas.interactionHint().isEmpty());
        QCOMPARE(doc->history()->count(),history); QVERIFY(active(canvas).tiles.isEmpty());
    }
    void physicalEraserEditsVectorPathsAndPenEndResumesBrushDrawing() {
        Canvas canvas; setup(canvas); auto *doc=canvas.document();
        QVERIFY(ObjectTools::addLayer(doc,LayerKind::Vector)); const auto id=path(canvas); QVERIFY(!id.isEmpty());
        canvas.setTool(Tool::Brush); canvas.setBrushSize(20); canvas.drawingTools()->vectorEraseMode=VectorEraseMode::WholeStroke;
        const auto before=canvas.project().toJson(); const auto frame=renderMovieFrame(canvas.project());
        const int history=doc->history()->count(); QSignalSpy blocked(&canvas,&Canvas::actionBlocked);
        QPointingDevice eraser("Vector eraser end",45,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Eraser,
            QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
        QPointingDevice pen("Vector pen end",46,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
        QVERIFY(tablet(canvas,eraser,QEvent::TabletMove,{100,100})); QVERIFY(canvas.interactionHint().isEmpty());
        QVERIFY(tablet(canvas,eraser,QEvent::TabletPress,{100,100}));
        QVERIFY(tablet(canvas,eraser,QEvent::TabletRelease,{100,100}));
        QVERIFY(active(canvas).objects.isEmpty()); QCOMPARE(doc->history()->count(),history+1);
        QCOMPARE(canvas.tool(),Tool::Brush); QVERIFY(blocked.isEmpty());
        doc->history()->undo(); QCOMPARE(canvas.project().toJson(),before); QCOMPARE(renderMovieFrame(canvas.project()),frame);
        QVERIFY(tablet(canvas,pen,QEvent::TabletMove,{240,200})); QVERIFY(canvas.interactionHint().isEmpty());
        QVERIFY(tablet(canvas,pen,QEvent::TabletPress,{240,200}));
        QVERIFY(tablet(canvas,pen,QEvent::TabletRelease,{240,200})); QCOMPARE(active(canvas).objects.size(),2);
        QVERIFY(active(canvas).objects.last().brushStroke.has_value()); QCOMPARE(active(canvas).objects.first().id,id);
    }
    void transformRetryTracksMultiSelectionWithoutChangingActiveRow() {
        QWidget host; auto *canvas=new Canvas(&host); setup(*canvas); auto *doc=canvas->document();
        BrushSettings brush; brush.size=20;
        doc->changeLayer("Left artwork",true,false,false,1);
        QVERIFY(doc->beginStroke({100,100},1,brush,false)); doc->endStroke(); const auto left=active(*canvas).id;
        doc->addLayer(false); doc->changeLayer("Right artwork",true,false,false,1);
        QVERIFY(doc->beginStroke({320,320},1,brush,false)); doc->endStroke(); const auto right=active(*canvas).id;
        select(*canvas,left);
        auto *panel=qobject_cast<LayerPanel *>(drawingPanel(canvas,"layersPanel")); QVERIFY(panel); panel->setParent(&host);
        auto *tree=panel->findChild<QTreeWidget *>("layerTree"); QVERIFY(tree);
        QTreeWidgetItem *leftRow=nullptr,*rightRow=nullptr;
        for(int i=0;i<tree->topLevelItemCount();++i) {
            auto *item=tree->topLevelItem(i);
            if(item->text(1)=="Left artwork")leftRow=item;
            if(item->text(1)=="Right artwork")rightRow=item;
        }
        QVERIFY(leftRow); QVERIFY(rightRow); QCOMPARE(tree->currentItem(),leftRow);
        QCOMPARE(panel->selectedLayerIds(),QStringList{left});
        QPainterPath selection; selection.addRect(300,300,40,40); doc->setRasterSelection(selection);
        canvas->setTool(Tool::AdvancedTransform); hover(*canvas);
        QVERIFY(!canvas->drawingTools()->transform()->active()); QCOMPARE(canvas->cursor().shape(),Qt::ForbiddenCursor);
        QSignalSpy activeChanges(doc,&Document::selectionChanged);
        QSignalSpy selectedChanges(panel,&LayerPanel::selectedLayersChanged);
        rightRow->setSelected(true);
        QCOMPARE(tree->currentItem(),leftRow); QCOMPARE(active(*canvas).id,left); QVERIFY(activeChanges.isEmpty());
        QCOMPARE(selectedChanges.size(),1); QVERIFY(canvas->toolBlockReason().isEmpty());
        QCOMPARE(canvas->cursor().shape(),Qt::SizeAllCursor); QVERIFY(canvas->interactionHint().isEmpty());
        click(*canvas,{320,320}); auto *transform=canvas->drawingTools()->transform(); QVERIFY(transform->active());
        QVERIFY(transform->preview()); QCOMPARE(transform->preview()->layerId,right);
        transform->cancel();
    }
};
QTEST_MAIN(CanvasAvailabilityTest)
#include "test_canvas_availability.moc"
