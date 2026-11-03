#ifdef __APPLE__
#import <AppKit/NSEvent.h>
#endif
#include "drawing_input.h"
#include "drawing_tool_controller.h"
#include "canvas.h"
#include "editable_object.h"
#include "main_window.h"
#include "mask_tool.h"
#include <QDockWidget>
#include <QHoverEvent>
#include <QLineEdit>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QWindow>
#include <QtTest>
#include <cmath>
#include <utility>

using namespace toon;
namespace {
QPointF viewport(const Canvas &canvas,QPointF point) {
    return canvas.viewTransform().map(sheetGeometry(canvas.project(),canvas.shot()).output.topLeft()+point);
}
void tablet(Canvas &canvas,const QPointingDevice &pen,QEvent::Type type,QPointF point,qreal pressure,bool queued,int time) {
    const auto local=viewport(canvas,point);
    auto *event=new QTabletEvent(type,&pen,local,canvas.mapToGlobal(local),pressure,12,-8,0,0,0,Qt::NoModifier,
        type==QEvent::TabletMove?Qt::NoButton:Qt::LeftButton,
        type==QEvent::TabletRelease?Qt::NoButton:Qt::LeftButton);
    event->setTimestamp(time);
    if(queued)QCoreApplication::postEvent(&canvas,event);
    else{QApplication::sendEvent(&canvas,event);delete event;}
}
void penEvent(Canvas &canvas, const QPointingDevice &pen, QEvent::Type type, QPointF artwork,
              qreal pressure, Qt::MouseButton button, Qt::MouseButtons buttons) {
    const auto local = viewport(canvas, artwork);
    QTabletEvent event(type, &pen, local, canvas.mapToGlobal(local), pressure, 0, 0, 0, 0, 0,
                       Qt::NoModifier, button, buttons);
    QApplication::sendEvent(&canvas, &event);
}
bool prepareWorkspace(MainWindow &window, const QTemporaryDir &directory) {
    auto project = Project::create(); project.width = project.height = 256; project.transparent = true;
    const auto path = directory.filePath("tablet.toonproj");
    if (!saveProject(project, path, nullptr) || !window.openProject(path)) return false;
    window.setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);
    for (auto *dock : window.findChildren<QDockWidget *>()) dock->hide();
    for (const auto &[name, area] : {std::pair{"toolsPanel", Qt::LeftDockWidgetArea},
                                   std::pair{"layersPanel", Qt::RightDockWidgetArea},
                                   std::pair{"animationPanel", Qt::BottomDockWidgetArea}}) {
        auto *dock = window.findChild<QDockWidget *>(name);
        if (!dock) return false;
        window.removeDockWidget(dock); window.addDockWidget(area, dock); dock->show();
    }
    window.resize(1250, 840); window.show(); window.activateWindow();
    if (!QTest::qWaitForWindowExposed(&window)) return false;
    auto *canvas = window.canvas(); canvas->setTool(Canvas::Tool::Brush);
    BrushSettings brush; brush.size = 8; brush.color = Qt::black; brush.pressureSize = false;
    brush.smoothingEnabled = false; canvas->setBrushSettings(brush); canvas->setStabilization(0);
    canvas->fitSheet(); canvas->setFocus(); QCoreApplication::processEvents();
    return true;
}
bool isSeparatorCursor(Qt::CursorShape shape) {
    return shape == Qt::SplitHCursor || shape == Qt::SplitVCursor;
}
QPoint armSeparator(MainWindow &window, int side) {
    // Exercise Qt's actual dock-layout helper; setting a resize cursor by hand
    // would not reproduce its cursorAdjusted state or CursorChange handling.
    const auto central = window.centralWidget()->geometry();
    const auto expected = side == 2 ? Qt::SplitVCursor : Qt::SplitHCursor;
    for (int distance = 1; distance <= 12; ++distance) {
        const QPoint local = side == 0 ? QPoint(central.left() - distance, central.center().y())
            : side == 1 ? QPoint(central.right() + distance, central.center().y())
                        : QPoint(central.center().x(), central.bottom() + distance);
        QHoverEvent hover(QEvent::HoverMove, local, window.mapToGlobal(local), central.center());
        QApplication::sendEvent(&window, &hover);
        if (window.cursor().shape() != expected) continue;
        // Establish the same last mouse receiver that precedes pen-only canvas
        // events. QWindow overload goes through QWidgetWindow hit testing.
        QTest::mouseMove(window.windowHandle(), local);
        if (window.cursor().shape() == expected) return local;
    }
    return {-1, -1};
}
class OverrideCursors {
public:
    ~OverrideCursors() { while (m_count) pop(); }
    void push(Qt::CursorShape shape) { QApplication::setOverrideCursor(shape); ++m_count; }
    void pop() { QApplication::restoreOverrideCursor(); --m_count; }
private:
    int m_count = 0;
};
class WindowTabletPacket final : public QObject {
public:
    int seen=0;
    bool eventFilter(QObject *,QEvent *event) override {
        if(event->type()!=QEvent::TabletRelease)return false;
        ++seen;return true;
    }
};
}
class TabletInputTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){configureDrawingInput();}
    void drawingPolicyPreservesNativeAndQtSamples() {
        QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents,true);
        QCoreApplication::setAttribute(Qt::AA_CompressTabletEvents,true);
#ifdef Q_OS_MACOS
        [NSEvent setMouseCoalescingEnabled:YES];
        QVERIFY([NSEvent isMouseCoalescingEnabled]);
#endif
        configureDrawingInput();
        QVERIFY(!QCoreApplication::testAttribute(Qt::AA_CompressHighFrequencyEvents));
        QVERIFY(!QCoreApplication::testAttribute(Qt::AA_CompressTabletEvents));
#ifdef Q_OS_MACOS
        QVERIFY(![NSEvent isMouseCoalescingEnabled]);
#endif
        // Reapplying the startup policy is harmless.
        configureDrawingInput();
#ifdef Q_OS_MACOS
        QVERIFY(![NSEvent isMouseCoalescingEnabled]);
#endif
    }
    void nativeWindowReleaseWaitsForTheCanvasReceiver_data(){
        QTest::addColumn<int>("kind");
        QTest::newRow("brush")<<0;QTest::newRow("retouch")<<1;QTest::newRow("mask")<<2;
    }
    void nativeWindowReleaseWaitsForTheCanvasReceiver(){
        QFETCH(int,kind);
        auto p=Project::create();p.width=p.height=128;p.transparent=true;
        QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(QColor(100,120,140));
        p.shots[0].layers[0].tiles.insert({0,0},tile);if(kind==2)p.shots[0].layers[0].mask=LayerMask{};
        Canvas canvas;canvas.resize(640,480);canvas.setProject(p);canvas.setStabilization(0);canvas.show();QCoreApplication::processEvents();
        QVERIFY(canvas.windowHandle());canvas.setBrushColor(Qt::black);canvas.setBrushSize(12);
        if(kind==1){canvas.setTool(Canvas::Tool::Retouch);canvas.drawingTools()->retouchSettings.mode=RetouchMode::Dodge;}
        if(kind==2){canvas.maskTool()->setEnabled(true);canvas.maskTool()->setSize(16);}
        const auto before=renderMovieFrame(canvas.project());
        QPointingDevice pen("Window routing pen",182,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,3);
        penEvent(canvas,pen,QEvent::TabletPress,{30,64},.8,Qt::LeftButton,Qt::LeftButton);
        penEvent(canvas,pen,QEvent::TabletMove,{70,64},.8,Qt::NoButton,Qt::LeftButton);
        const auto preview=renderMovieFrame(canvas.project());QVERIFY(preview!=before);QVERIFY(canvas.document()->editingGestureActive());
        WindowTabletPacket packet;canvas.windowHandle()->installEventFilter(&packet);
        const auto position=viewport(canvas,{80,64});
        QTabletEvent release(QEvent::TabletRelease,&pen,position,canvas.mapToGlobal(position),0,0,0,0,0,0,
            Qt::NoModifier,Qt::LeftButton,Qt::NoButton);
        // Qt sees a native QWindow packet before it delivers the translated
        // QWidget event. Hold that handoff here to observe its intermediate state.
        QApplication::sendEvent(canvas.windowHandle(),&release);QCOMPARE(packet.seen,1);
        QVERIFY(canvas.document()->editingGestureActive());QCOMPARE(canvas.document()->history()->count(),0);
        QCOMPARE(renderMovieFrame(canvas.project()),preview);canvas.windowHandle()->removeEventFilter(&packet);
        penEvent(canvas,pen,QEvent::TabletRelease,{80,64},0,Qt::LeftButton,Qt::NoButton);
        QVERIFY(!canvas.document()->editingGestureActive());QCOMPARE(canvas.document()->history()->count(),1);
        const auto after=renderMovieFrame(canvas.project());QVERIFY(after!=before);
        canvas.document()->history()->undo();QCOMPARE(renderMovieFrame(canvas.project()),before);
        canvas.document()->history()->redo();QCOMPARE(renderMovieFrame(canvas.project()),after);
    }
    void queuedFastCurvesMatchUnhurriedInput_data() {
        QTest::addColumn<bool>("vector");QTest::addColumn<int>("smoothing");
        QTest::newRow("raster-raw")<<false<<0;
        QTest::newRow("vector-raw")<<true<<0;
        QTest::newRow("raster-stabilized")<<false<<40;
        QTest::newRow("vector-stabilized")<<true<<40;
    }
    void queuedFastCurvesMatchUnhurriedInput() {
        QFETCH(bool,vector);QFETCH(int,smoothing);
        auto project=Project::create();project.width=project.height=256;project.transparent=true;
        if(vector)project.shots[0].layers[0].kind=LayerKind::Vector;
        Canvas fast,slow;
        for(auto *canvas:{&fast,&slow}) {
            canvas->resize(640,480);canvas->setProject(project);canvas->setZoom(.8);canvas->setRotation(17);
            canvas->setBrushSize(6);canvas->setStabilization(smoothing);canvas->setBrushColor(Qt::black);
        }
        QPointingDevice pen("Burst pen",43,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position|QInputDevice::Capability::Pressure|QInputDevice::Capability::XTilt|QInputDevice::Capability::YTilt,1,1);
        const auto draw=[&](Canvas &canvas,bool queued) {
            for(int stroke=0;stroke<2;++stroke) {
                const QPointF center(76+104*stroke,128);
                const auto point=[&](int i){const qreal angle=i*2*3.141592653589793/240;return center+QPointF(40*std::cos(angle),40*std::sin(angle));};
                tablet(canvas,pen,QEvent::TabletPress,point(0),.7,queued,stroke*300);
                for(int i=1;i<=240;++i)tablet(canvas,pen,QEvent::TabletMove,point(i),.7+.25*std::sin(i*.04),queued,stroke*300+i);
                tablet(canvas,pen,QEvent::TabletRelease,point(240),0,queued,stroke*300+241);
            }
        };
        draw(slow,false);draw(fast,true);
        // Deliver a complete high-rate backlog without discarding/reordering moves.
        QCoreApplication::sendPostedEvents(&fast);
        QVERIFY(!fast.document()->strokeActive());QCOMPARE(fast.document()->history()->count(),2);
        const auto image=renderMovieFrame(fast.project());QCOMPARE(image,renderMovieFrame(slow.project()));
        QVERIFY(image.pixelColor(116,128).alpha()>0);
        QCOMPARE(image.pixelColor(76,128).alpha(),0);QCOMPARE(image.pixelColor(128,128).alpha(),0);
        if(vector) {
            const auto &a=fast.shot().layers[0].objects,&b=slow.shot().layers[0].objects;
            QCOMPARE(a.size(),2);QCOMPARE(b.size(),2);
            for(int i=0;i<2;++i){QVERIFY(a[i].brushStroke);QCOMPARE(a[i].brushStroke->samples,b[i].brushStroke->samples);}
        }
        fast.document()->history()->undo();fast.document()->history()->redo();QCOMPARE(renderMovieFrame(fast.project()),image);
    }
    void penCrossingDockSeparatorRestoresCanvasCursor_data() {
        QTest::addColumn<int>("side");
        QTest::newRow("left-tools") << 0;
        QTest::newRow("right-layers") << 1;
        QTest::newRow("bottom-timeline") << 2;
    }
    void penCrossingDockSeparatorRestoresCanvasCursor() {
        QFETCH(int, side);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        MainWindow window(directory.filePath("workspace.ini")); QVERIFY(prepareWorkspace(window, directory));
        auto *canvas = window.canvas();
        QPointingDevice pen("Separator pen", 141, QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3);
        const auto crossIntoCanvas = [&] {
            penEvent(*canvas, pen, QEvent::TabletMove, {128, 128}, 0, Qt::NoButton, Qt::NoButton);
        };
        QVERIFY(armSeparator(window, side).x() >= 0);
        QVERIFY(isSeparatorCursor(window.cursor().shape()));
        crossIntoCanvas();
        QVERIFY(!isSeparatorCursor(window.cursor().shape()));
        QCOMPARE(canvas->cursor().shape(), Qt::BlankCursor);
        QCOMPARE(window.windowHandle()->cursor().shape(), Qt::BlankCursor);
        QVERIFY(!canvas->brushCursorPath().isEmpty());
        QVERIFY(!canvas->document()->strokeActive()); QCOMPARE(canvas->document()->history()->count(), 0);

        // No synthesized mouse move occurs between the divider and these tool
        // modes, including unavailable painting with its bottom-bar explanation.
        canvas->setTool(Canvas::Tool::Hand);
        QVERIFY(armSeparator(window, side).x() >= 0); crossIntoCanvas();
        QVERIFY(!isSeparatorCursor(window.cursor().shape()));
        QCOMPARE(canvas->cursor().shape(), Qt::OpenHandCursor);
        QCOMPARE(window.windowHandle()->cursor().shape(), Qt::OpenHandCursor);
        canvas->setTool(Canvas::Tool::Brush); canvas->setToolAvailable(false);
        QVERIFY(armSeparator(window, side).x() >= 0); crossIntoCanvas();
        QVERIFY(!isSeparatorCursor(window.cursor().shape()));
        QCOMPARE(canvas->cursor().shape(), Qt::ForbiddenCursor);
        QCOMPARE(window.windowHandle()->cursor().shape(), Qt::ForbiddenCursor);
        QVERIFY(!canvas->interactionHint().isEmpty());
        canvas->setToolAvailable(true);

        QVERIFY(armSeparator(window, side).x() >= 0);
        penEvent(*canvas, pen, QEvent::TabletPress, {80, 128}, .6, Qt::LeftButton, Qt::LeftButton);
        penEvent(*canvas, pen, QEvent::TabletMove, {176, 128}, .7, Qt::NoButton, Qt::LeftButton);
        QCOMPARE(window.windowHandle()->cursor().shape(), Qt::BlankCursor);
        penEvent(*canvas, pen, QEvent::TabletRelease, {176, 128}, 0, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(canvas->document()->history()->count(), 1);
        QVERIFY(renderMovieFrame(canvas->project()).pixelColor(128, 128).alpha() > 0);
        canvas->document()->history()->setClean(); QVERIFY(window.close());
    }
    void penCursorRecoveryPreservesOverridesAndDockResizing() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        MainWindow window(directory.filePath("workspace.ini")); QVERIFY(prepareWorkspace(window, directory));
        auto *canvas = window.canvas();
        QPointingDevice pen("Busy cursor pen", 142, QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3);
        QVERIFY(armSeparator(window, 0).x() >= 0);
        QVERIFY(!QApplication::overrideCursor());
        OverrideCursors overrides; overrides.push(Qt::WaitCursor); overrides.push(Qt::BusyCursor);
        penEvent(*canvas, pen, QEvent::TabletMove, {128, 128}, 0, Qt::NoButton, Qt::NoButton);
        QVERIFY(QApplication::overrideCursor()); QCOMPARE(QApplication::overrideCursor()->shape(), Qt::BusyCursor);
        overrides.pop(); QVERIFY(QApplication::overrideCursor());
        QCOMPARE(QApplication::overrideCursor()->shape(), Qt::WaitCursor);
        overrides.pop(); QVERIFY(!QApplication::overrideCursor());
        penEvent(*canvas, pen, QEvent::TabletMove, {129, 128}, 0, Qt::NoButton, Qt::NoButton);
        QCOMPARE(window.windowHandle()->cursor().shape(), Qt::BlankCursor);

        const auto separator = armSeparator(window, 0); QVERIFY(separator.x() >= 0);
        auto *tools = window.findChild<QDockWidget *>("toolsPanel"); QVERIFY(tools);
        const int previousWidth = tools->width();
        QTest::mousePress(window.windowHandle(), Qt::LeftButton, Qt::NoModifier, separator);
        QTest::mouseMove(window.windowHandle(), separator + QPoint(45, 0));
        QTest::mouseRelease(window.windowHandle(), Qt::LeftButton, Qt::NoModifier, separator + QPoint(45, 0));
        QTRY_VERIFY(tools->width() > previousWidth);
        QCOMPARE(canvas->document()->history()->count(), 0); QVERIFY(window.close());
    }
    void penLeavingCanvasRestoresThePanelCursorWithoutMouseMotion() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        MainWindow window(directory.filePath("workspace.ini")); QVERIFY(prepareWorkspace(window, directory));
        auto *canvas = window.canvas();
        auto *dock = window.findChild<QDockWidget *>("toolsPanel"); QVERIFY(dock);
        QLineEdit field(dock->widget()); field.setText("Unfinished brush name"); field.show();
        QPointingDevice pen("Panel return pen", 148, QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3);
        QSignalSpy documentChanges(canvas, &Canvas::documentChanged);
        for (const auto cursor : {Qt::IBeamCursor, Qt::CrossCursor, Qt::ArrowCursor}) {
            field.setCursor(cursor);
            penEvent(*canvas, pen, QEvent::TabletMove, {128, 128}, 0, Qt::NoButton, Qt::NoButton);
            QCOMPARE(window.windowHandle()->cursor().shape(), Qt::BlankCursor);
            QVERIFY(!canvas->brushCursorPath().isEmpty());
            // A pen crossing may deliver Leave or omit it; both must hand off
            // the native cursor without waiting for a synthesized mouse move.
            if (cursor == Qt::CrossCursor) {
                QEvent leave(QEvent::Leave); QApplication::sendEvent(canvas, &leave);
            }
            QTabletEvent hover(QEvent::TabletMove, &pen, {8, 8}, field.mapToGlobal(QPoint(8, 8)),
                               0, 0, 0, 0, 0, 0, Qt::NoModifier, Qt::NoButton, Qt::NoButton);
            QApplication::sendEvent(&field, &hover);
            QCOMPARE(window.windowHandle()->cursor().shape(), cursor);
            QVERIFY(canvas->brushCursorPath().isEmpty());
            QCOMPARE(field.text(), QString("Unfinished brush name"));
        }
        QCOMPARE(documentChanges.count(), 0);
        QCOMPARE(canvas->document()->history()->count(), 0);
        QVERIFY(!canvas->document()->strokeActive());
        QVERIFY(window.close());
    }
    void maskPenHoverAlsoClearsRealDockSeparatorCursor() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        MainWindow window(directory.filePath("workspace.ini")); QVERIFY(prepareWorkspace(window, directory));
        auto *canvas = window.canvas(); QVERIFY(canvas->document()->addLayerMask());
        auto *mask = canvas->maskTool(); mask->setEnabled(true); QVERIFY(mask->enabled());
        QPointingDevice pen("Mask separator pen", 147, QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3);
        QVERIFY(armSeparator(window, 0).x() >= 0); QVERIFY(isSeparatorCursor(window.cursor().shape()));
        // MaskTool consumes this event before Canvas::tabletEvent is called.
        penEvent(*canvas, pen, QEvent::TabletMove, {128, 128}, 0, Qt::NoButton, Qt::NoButton);
        QVERIFY(!isSeparatorCursor(window.cursor().shape()));
        QCOMPARE(canvas->cursor().shape(), Qt::BlankCursor);
        QCOMPARE(window.windowHandle()->cursor().shape(), Qt::BlankCursor);
        QVERIFY(!mask->cursorPath().isEmpty()); QVERIFY(!mask->dragging());
        QCOMPARE(canvas->document()->history()->count(), 1);
        canvas->document()->history()->setClean(); QVERIFY(window.close());
    }
    void lostPenReleaseEndsAtLastContactAndNextStrokeIsIndependent_data() {
        QTest::addColumn<bool>("vector");
        QTest::newRow("raster") << false;
        QTest::newRow("vector") << true;
    }
    void lostPenReleaseEndsAtLastContactAndNextStrokeIsIndependent() {
        QFETCH(bool, vector);
        auto project = Project::create(); project.width = project.height = 256; project.transparent = true;
        if (vector) project.shots[0].layers[0].kind = LayerKind::Vector;
        Canvas canvas; canvas.resize(640, 480); canvas.setProject(project); canvas.setStabilization(0);
        BrushSettings brush; brush.size = 8; brush.pressureSize = false; brush.color = Qt::black;
        brush.smoothingEnabled = false; canvas.setBrushSettings(brush);
        QPointingDevice pen("Lost release pen", 143, QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3);
        penEvent(canvas, pen, QEvent::TabletPress, {30, 50}, .5, Qt::LeftButton, Qt::LeftButton);
        penEvent(canvas, pen, QEvent::TabletMove, {100, 50}, .7, Qt::NoButton, Qt::LeftButton);
        QVERIFY(canvas.document()->strokeActive());
        // Some tablet drivers lose the release packet. The next hover is not
        // part of the stroke and must never connect to the distant position.
        penEvent(canvas, pen, QEvent::TabletMove, {210, 210}, 0, Qt::NoButton, Qt::NoButton);
        QVERIFY(!canvas.document()->strokeActive()); QCOMPARE(canvas.document()->history()->count(), 1);
        const auto first = renderMovieFrame(canvas.project());
        QVERIFY(first.pixelColor(60, 50).alpha() > 0);
        QCOMPARE(first.pixelColor(155, 130).alpha(), 0); QCOMPARE(first.pixelColor(210, 210).alpha(), 0);
        penEvent(canvas, pen, QEvent::TabletMove, {220, 210}, 0, Qt::NoButton, Qt::NoButton);
        // A delayed release must not produce a second undo entry.
        penEvent(canvas, pen, QEvent::TabletRelease, {220, 210}, 0, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(canvas.document()->history()->count(), 1); QCOMPARE(renderMovieFrame(canvas.project()), first);
        penEvent(canvas, pen, QEvent::TabletPress, {170, 210}, .4, Qt::LeftButton, Qt::LeftButton);
        penEvent(canvas, pen, QEvent::TabletMove, {220, 210}, .8, Qt::NoButton, Qt::LeftButton);
        penEvent(canvas, pen, QEvent::TabletRelease, {220, 210}, 0, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(canvas.document()->history()->count(), 2);
        const auto both = renderMovieFrame(canvas.project()); QVERIFY(both.pixelColor(190, 210).alpha() > 0);
        QCOMPARE(both.pixelColor(135, 130).alpha(), 0);
        canvas.document()->history()->undo(); QCOMPARE(renderMovieFrame(canvas.project()), first);
        canvas.document()->history()->redo(); QCOMPARE(renderMovieFrame(canvas.project()), both);
        if (vector) QCOMPARE(canvas.shot().layers[0].objects.size(), 2);
    }
    void interruptedStrokeAlsoRecoversOutsideCanvas_data() {
        QTest::addColumn<int>("interruption");
        QTest::newRow("leave-proximity") << 0;
        QTest::newRow("hover-over-panel") << 1;
        QTest::newRow("next-tip-press") << 2;
        QTest::newRow("release-over-panel") << 3;
        QTest::newRow("new-tip-press-over-panel") << 4;
        QTest::newRow("new-tip-press-over-floating-panel") << 5;
    }
    void interruptedStrokeAlsoRecoversOutsideCanvas() {
        QFETCH(int, interruption);
        auto project = Project::create(); project.width = project.height = 256; project.transparent = true;
        QWidget workspace;
        Canvas canvas(&workspace); canvas.resize(640, 480); canvas.setProject(project); canvas.setStabilization(0);
        BrushSettings brush; brush.size = 8; brush.pressureSize = false; brush.smoothingEnabled = false;
        canvas.setBrushSettings(brush);
        QPointingDevice pen("Interrupt pen", 144, QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3);
        penEvent(canvas, pen, QEvent::TabletPress, {30, 50}, .5, Qt::LeftButton, Qt::LeftButton);
        penEvent(canvas, pen, QEvent::TabletMove, {100, 50}, .7, Qt::NoButton, Qt::LeftButton);
        const auto first = renderMovieFrame(canvas.project()); QVERIFY(canvas.document()->strokeActive());
        if (interruption == 0) {
            QTabletEvent event(QEvent::TabletLeaveProximity, &pen, {}, {}, 0, 0, 0, 0, 0, 0,
                               Qt::NoModifier, Qt::NoButton, Qt::NoButton);
            QApplication::sendEvent(qApp, &event);
        } else if (interruption == 1) {
            QWidget panel(&workspace);
            QTabletEvent event(QEvent::TabletMove, &pen, {10, 10}, panel.mapToGlobal(QPoint(10, 10)),
                               0, 0, 0, 0, 0, 0, Qt::NoModifier, Qt::NoButton, Qt::NoButton);
            QApplication::sendEvent(&panel, &event);
        } else if (interruption == 2) {
            penEvent(canvas, pen, QEvent::TabletPress, {170, 210}, .4, Qt::LeftButton, Qt::LeftButton);
        } else {
            QWidget panel(&workspace, interruption == 5 ? Qt::Tool : Qt::Widget);
            if (interruption == 5) {
                QVERIFY(panel.isWindow()); QCOMPARE(panel.parentWidget(), &workspace);
                QVERIFY(panel.window() != canvas.window());
            }
            QTabletEvent event(interruption == 3 ? QEvent::TabletRelease : QEvent::TabletPress,
                               &pen, {10, 10}, panel.mapToGlobal(QPoint(10, 10)),
                               interruption == 3 ? 0 : .7, 0, 0, 0, 0, 0, Qt::NoModifier,
                               Qt::LeftButton, interruption == 3 ? Qt::NoButton : Qt::LeftButton);
            QApplication::sendEvent(&panel, &event);
        }
        QCOMPARE(canvas.document()->history()->count(), 1);
        QCOMPARE(canvas.document()->strokeActive(), interruption == 2);
        if (interruption != 2) {
            QCOMPARE(renderMovieFrame(canvas.project()), first);
            penEvent(canvas, pen, QEvent::TabletPress, {170, 210}, .4, Qt::LeftButton, Qt::LeftButton);
        }
        penEvent(canvas, pen, QEvent::TabletMove, {220, 210}, .8, Qt::NoButton, Qt::LeftButton);
        penEvent(canvas, pen, QEvent::TabletRelease, {220, 210}, 0, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(canvas.document()->history()->count(), 2);
        const auto both = renderMovieFrame(canvas.project()); QVERIFY(both.pixelColor(190, 210).alpha() > 0);
        QCOMPARE(both.pixelColor(135, 130).alpha(), 0);
        canvas.document()->history()->undo(); QCOMPARE(renderMovieFrame(canvas.project()), first);
    }
    void lostReleaseDoesNotContinuePanSelectionOrTransform() {
        auto project = Project::create(); project.width = project.height = 256; project.transparent = true;
        Canvas canvas; canvas.resize(640, 480); canvas.setProject(project); canvas.setZoom(1);
        QPointingDevice pen("Navigation pen", 145, QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3);
        canvas.setTool(Canvas::Tool::Hand); const auto originalCenter = canvas.center();
        penEvent(canvas, pen, QEvent::TabletPress, {80, 80}, .6, Qt::LeftButton, Qt::LeftButton);
        penEvent(canvas, pen, QEvent::TabletMove, {100, 90}, .6, Qt::NoButton, Qt::LeftButton);
        const auto movedCenter = canvas.center(); QVERIFY(movedCenter != originalCenter);
        penEvent(canvas, pen, QEvent::TabletMove, {220, 220}, 0, Qt::NoButton, Qt::NoButton);
        QCOMPARE(canvas.center(), movedCenter); QVERIFY(!canvas.selectionGestureActive());
        QCOMPARE(canvas.cursor().shape(), Qt::OpenHandCursor);
        canvas.centerOn(originalCenter); canvas.setTool(Canvas::Tool::RectangleSelect);
        penEvent(canvas, pen, QEvent::TabletPress, {80, 80}, .6, Qt::LeftButton, Qt::LeftButton);
        penEvent(canvas, pen, QEvent::TabletMove, {160, 160}, .6, Qt::NoButton, Qt::LeftButton);
        QVERIFY(canvas.selectionGestureActive());
        penEvent(canvas, pen, QEvent::TabletMove, {220, 220}, 0, Qt::NoButton, Qt::NoButton);
        QVERIFY(!canvas.selectionGestureActive());
        QCOMPARE(canvas.document()->rasterSelection().path.boundingRect(), QRectF(80, 80, 80, 80));
        QCOMPARE(canvas.document()->history()->count(), 0);
        QVERIFY(canvas.document()->fillRasterSelection(Qt::red));
        const auto beforeTransform = renderMovieFrame(canvas.project());
        canvas.setTool(Canvas::Tool::Transform);
        penEvent(canvas, pen, QEvent::TabletPress, {120, 120}, .6, Qt::LeftButton, Qt::LeftButton);
        penEvent(canvas, pen, QEvent::TabletMove, {140, 130}, .6, Qt::NoButton, Qt::LeftButton);
        const auto transform = canvas.document()->transformMatrix(); QVERIFY(!transform.isIdentity());
        penEvent(canvas, pen, QEvent::TabletMove, {220, 220}, 0, Qt::NoButton, Qt::NoButton);
        QCOMPARE(canvas.document()->transformMatrix(), transform);
        QVERIFY(canvas.document()->transformActive()); QVERIFY(!canvas.selectionGestureActive());
        QCOMPARE(canvas.document()->history()->count(), 1);
        QVERIFY(canvas.document()->applyTransform()); QCOMPARE(canvas.document()->history()->count(), 2);
        canvas.document()->history()->undo(); QCOMPARE(renderMovieFrame(canvas.project()), beforeTransform);
    }
    void barrelButtonReleaseDoesNotEndTipStroke() {
        auto project = Project::create(); project.width = project.height = 256; project.transparent = true;
        Canvas canvas; canvas.resize(640, 480); canvas.setProject(project); canvas.setStabilization(0);
        BrushSettings brush; brush.size = 8; brush.pressureSize = false; brush.smoothingEnabled = false;
        canvas.setBrushSettings(brush);
        QPointingDevice pen("Barrel pen", 146, QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3);
        penEvent(canvas, pen, QEvent::TabletPress, {40, 80}, 0, Qt::RightButton, Qt::RightButton);
        penEvent(canvas, pen, QEvent::TabletMove, {80, 80}, 0, Qt::NoButton, Qt::RightButton);
        penEvent(canvas, pen, QEvent::TabletRelease, {80, 80}, 0, Qt::RightButton, Qt::NoButton);
        QVERIFY(!canvas.document()->strokeActive()); QCOMPARE(canvas.document()->history()->count(), 0);
        penEvent(canvas, pen, QEvent::TabletPress, {40, 80}, .7, Qt::LeftButton, Qt::LeftButton);
        penEvent(canvas, pen, QEvent::TabletPress, {80, 80}, .7, Qt::RightButton, Qt::LeftButton | Qt::RightButton);
        penEvent(canvas, pen, QEvent::TabletMove, {120, 80}, .7, Qt::NoButton, Qt::LeftButton | Qt::RightButton);
        penEvent(canvas, pen, QEvent::TabletRelease, {120, 80}, .7, Qt::RightButton, Qt::LeftButton);
        QVERIFY(canvas.document()->strokeActive()); QCOMPARE(canvas.document()->history()->count(), 0);
        penEvent(canvas, pen, QEvent::TabletMove, {180, 80}, .7, Qt::NoButton, Qt::LeftButton);
        penEvent(canvas, pen, QEvent::TabletRelease, {180, 80}, 0, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(canvas.document()->history()->count(), 1);
        QVERIFY(renderMovieFrame(canvas.project()).pixelColor(160, 80).alpha() > 0);
    }
};
QTEST_MAIN(TabletInputTests)
#include "tablet_input_tests.moc"
