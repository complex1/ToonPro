#include "canvas.h"
#include "main_window.h"
#include "guide_geometry.h"
#include "guide_tool.h"
#include <cmath>
#include <QDir>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QSignalSpy>
#include <QHelpEvent>
#include <QToolTip>
#include <QtTest>
using namespace toon;
namespace {
Project project() { auto p=Project::create(); p.width=640; p.height=480; p.transparent=true; return p; }
QPoint view(Canvas &canvas,QPointF p) { return canvas.viewTransform().map(p+sheetGeometry(canvas.project(),canvas.shot()).output.topLeft()).toPoint(); }
void dragMove(Canvas &canvas,QPoint pos) {
    QMouseEvent move(QEvent::MouseMove,QPointF(pos),QPointF(canvas.mapToGlobal(pos)),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&canvas,&move);
}
void setup(Canvas &canvas) { canvas.resize(1000,760); canvas.setProject(project()); canvas.show(); canvas.setZoom(1); QCoreApplication::processEvents(); }
}
class GuideCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void multipleClickPlacementAndUndo() {
        Canvas canvas; setup(canvas); auto *doc=canvas.document();
        const auto original=renderMovieFrame(canvas.project());
        canvas.beginGuideCreation(GuideType::Perspective);
        for(auto point:{QPointF(80,80),QPointF(420,100),QPointF(460,380),QPointF(60,380)}) QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,view(canvas,point));
        QVERIFY(!canvas.guideCreating()); QCOMPARE(canvas.drawingGuides().size(),1); QCOMPARE(doc->history()->count(),1);
        const auto firstId=canvas.selectedGuideId(); QVERIFY(!firstId.isEmpty());
        canvas.beginGuideCreation(GuideType::VanishingPoint);
        QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,view(canvas,{300,200}));
        QCOMPARE(canvas.drawingGuides().size(),2); QVERIFY(canvas.selectedGuideId()!=firstId);
        QCOMPARE(renderMovieFrame(canvas.project()),original); QVERIFY(canvas.shot().layers.first().tiles.isEmpty());
        doc->history()->undo(); QCOMPARE(canvas.drawingGuides().size(),1);
        doc->history()->redo(); QCOMPARE(canvas.drawingGuides().size(),2);
        canvas.beginGuideCreation(GuideType::Perspective); QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,view(canvas,{40,40}));
        QTest::keyClick(&canvas,Qt::Key_Escape); QVERIFY(!canvas.guideCreating()); QCOMPARE(doc->history()->count(),2);
    }
    void threePointAndFisheyePlacementCoexist() {
        Canvas canvas; setup(canvas); const auto frame = renderMovieFrame(canvas.project());
        canvas.beginGuideCreation(GuideType::ThreePoint);
        for (const auto p : {QPointF(40,140),QPointF(600,140),QPointF(320,20),QPointF(320,380)})
            QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, view(canvas,p));
        QCOMPARE(canvas.drawingGuides().size(), 1); QVERIFY(!canvas.guideCreating());
        QCOMPARE(canvas.drawingGuides().first().type, GuideType::ThreePoint);
        canvas.beginGuideCreation(GuideType::Fisheye);
        for (const auto p : {QPointF(100,240),QPointF(540,240),QPointF(380,60)})
            QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, view(canvas,p));
        QCOMPARE(canvas.drawingGuides().size(), 2); QVERIFY(!canvas.guideCreating());
        const auto fish = canvas.drawingGuides().last();
        QCOMPARE(fish.type, GuideType::Fisheye);
        QVERIFY(QLineF(fish.points.last(), QPointF(320,60)).length() < 1); // Integer input coordinates round the view mapping.
        QCOMPARE(renderMovieFrame(canvas.project()), frame);
        QCOMPARE(canvas.document()->history()->count(), 2);
        canvas.document()->history()->undo(); QCOMPARE(canvas.drawingGuides().size(), 1);
        canvas.document()->history()->redo(); QCOMPARE(canvas.drawingGuides().last(), fish);
    }
    void ellipsePlacementHandlesLocksAndUndoFollowMirroredRotatedView() {
        Canvas canvas; setup(canvas); canvas.setRotation(29); canvas.setZoom(1.2); canvas.setCanvasMirrored(true);
        const auto artwork = renderMovieFrame(canvas.project()); canvas.beginGuideCreation(GuideType::Ellipse);
        for (const auto point : {QPointF(150, 180), QPointF(470, 300), QPointF(350, 150)})
            QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, view(canvas, point));
        QVERIFY(!canvas.guideCreating()); QCOMPARE(canvas.drawingGuides().size(), 1);
        auto guide = canvas.drawingGuides().first(); QCOMPARE(guide.type, GuideType::Ellipse); QVERIFY(validGuideGeometry(guide));
        QCOMPARE(canvas.document()->history()->count(), 1); QCOMPARE(renderMovieFrame(canvas.project()), artwork);
        const auto original = guide; const auto middle = (guide.points[0] + guide.points[1]) / 2;
        const auto moveStart = view(canvas, middle), moveEnd = moveStart + QPoint(25, -17);
        const auto delta = canvas.artworkPoint(moveEnd) - canvas.artworkPoint(moveStart);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, moveStart); dragMove(canvas, moveEnd);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, moveEnd);
        QCOMPARE(canvas.document()->history()->count(), 2); guide = canvas.drawingGuides().first();
        for (int i = 0; i < 3; ++i) QVERIFY(QLineF(guide.points[i], original.points[i] + delta).length() < .01);
        canvas.document()->history()->undo(); QCOMPARE(canvas.drawingGuides().first(), original);
        const auto axisStart = view(canvas, original.points[1]), axisEnd = axisStart + QPoint(-35, 22);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, axisStart); dragMove(canvas, axisEnd);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, axisEnd); guide = canvas.drawingGuides().first();
        const auto newCenter = (guide.points[0] + guide.points[1]) / 2, newAxis = guide.points[1] - guide.points[0];
        QVERIFY(std::abs(QPointF::dotProduct(guide.points[2] - newCenter, newAxis)) < .01);
        QVERIFY(std::abs(QLineF(newCenter, guide.points[2]).length() - QLineF(middle, original.points[2]).length()) < .01);
        canvas.document()->history()->undo(); guide = canvas.drawingGuides().first();
        const auto heightStart = view(canvas, guide.points[2]), heightEnd = heightStart + QPoint(15, 18);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, heightStart); dragMove(canvas, heightEnd);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::ShiftModifier, heightEnd); guide = canvas.drawingGuides().first();
        QVERIFY(std::abs(QLineF((guide.points[0] + guide.points[1]) / 2, guide.points[2]).length() - QLineF(guide.points[0], guide.points[1]).length() / 2) < .01);
        guide.locked = true; canvas.updateGuide(guide); const int history = canvas.document()->history()->count();
        const auto locked = canvas.drawingGuides().first(); const auto lockedHandle = view(canvas, locked.points[0]);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, lockedHandle); dragMove(canvas, lockedHandle + QPoint(30, 20));
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, lockedHandle + QPoint(30, 20));
        QCOMPARE(canvas.drawingGuides().first(), locked); QCOMPARE(canvas.document()->history()->count(), history);
    }
    void fixedEllipseCapturesInitialDabAndCompletesLoops_data() {
        QTest::addColumn<qreal>("smoothing"); QTest::addColumn<bool>("mirrored");
        for (qreal smoothing : {qreal(0), qreal(100)}) for (bool mirrored : {false, true})
            QTest::newRow(qPrintable(QString("%1-%2").arg(smoothing).arg(mirrored))) << smoothing << mirrored;
    }
    void fixedEllipseCapturesInitialDabAndCompletesLoops() {
        QFETCH(qreal, smoothing); QFETCH(bool, mirrored);
        Canvas canvas; setup(canvas); canvas.setRotation(23); canvas.setZoom(1.2); canvas.setCanvasMirrored(mirrored);
        canvas.addDefaultGuide(GuideType::Ellipse); auto guide = canvas.drawingGuides().first();
        const qreal rotation = .33; const QPointF center(320, 240);
        const QPointF axis(std::cos(rotation) * 180, std::sin(rotation) * 180), height(-std::sin(rotation) * 64, std::cos(rotation) * 64);
        guide.points = {center - axis, center + axis, center + height}; guide.visible = false; guide.locked = true; canvas.updateGuide(guide);
        auto settings = canvas.guideSettings(); settings.snapping = true; settings.visible = false; canvas.setGuideSettings(settings);
        canvas.setTool(Canvas::Tool::Brush); canvas.brush.size = 4; canvas.brush.color = Qt::black;
        canvas.setPressureSize(false); canvas.setStabilization(smoothing);
        const auto ellipse = guideSnapConstraints(guide, center + axis).first();
        const qreal first = 170 * std::acos(-1.0) / 180;
        const auto onCurve = guideEllipsePoint(ellipse, first);
        const auto away = onCurve + (onCurve - center) / QLineF(center, onCurve).length() * 6;
        const auto contact = view(canvas, away); const auto actualContact = canvas.artworkPoint(contact);
        const auto projectedContact = projectOntoGuideConstraint(actualContact, ellipse);
        const int before = canvas.document()->history()->count();
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, contact);
        auto frame = renderMovieFrame(canvas.project());
        QVERIFY(frame.pixelColor(qRound(projectedContact.x()), qRound(projectedContact.y())).alpha() > 0);
        QCOMPARE(frame.pixelColor(qRound(actualContact.x()), qRound(actualContact.y())).alpha(), 0);
        for (int i = 1; i <= 72; ++i) {
            const auto point = guideEllipsePoint(ellipse, first + i * std::acos(-1.0) / 18);
            dragMove(canvas, view(canvas, point));
        }
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, view(canvas, onCurve)); frame = renderMovieFrame(canvas.project());
        for (int i = 0; i < 12; ++i) {
            const auto point = guideEllipsePoint(ellipse, i * std::acos(-1.0) / 6);
            QVERIFY2(frame.pixelColor(qRound(point.x()), qRound(point.y())).alpha() > 0, qPrintable(QString("Missing ellipse arc at %1,%2").arg(point.x()).arg(point.y())));
        }
        QCOMPARE(frame.pixelColor(320, 240).alpha(), 0); QCOMPARE(canvas.document()->history()->count(), before + 1);
        canvas.document()->history()->undo(); QVERIFY(canvas.shot().layers.first().tiles.isEmpty());
    }
    void fixedEllipseCaptureRadiusAndCenterFallbackAreStable() {
        Canvas canvas; setup(canvas); canvas.addDefaultGuide(GuideType::Ellipse);
        auto guide = canvas.drawingGuides().first(); guide.points = {{100, 240}, {540, 240}, {320, 170}}; canvas.updateGuide(guide);
        auto settings = canvas.guideSettings(); settings.snapping = true; canvas.setGuideSettings(settings); GuideTool tool(&canvas);
        const auto ellipse = guideSnapConstraints(guide, {540, 240}).first();
        const auto first = tool.beginSnap({546, 240}); QVERIFY(QLineF(first, QPointF(540, 240)).length() < .01);
        auto point = tool.snap({320, 170}); QVERIFY(point.has_value()); auto samples = tool.strokePoints(*point); QVERIFY(!samples.isEmpty());
        const auto last = samples.last(); point = tool.snap({320, 240}); QVERIFY(point.has_value());
        QVERIFY(QLineF(*point, last).length() < .01); samples = tool.strokePoints(*point);
        for (const auto sample : samples) QVERIFY(guideConstraintDistance(sample, ellipse) < 1e-6);
        tool.endSnap(); QCOMPARE(tool.beginSnap({320, 240}), QPointF(320, 240));
        point = tool.snap({330, 240}); QVERIFY(point.has_value()); QCOMPARE(*point, QPointF(330, 240));
    }
    void threePointSnapsTowardThirdVanishingPoint() {
        Canvas canvas; setup(canvas); canvas.addDefaultGuide(GuideType::ThreePoint);
        auto guide = canvas.drawingGuides().first(); guide.points = {{-100,100},{700,100},{300,-200},{300,380}};
        canvas.updateGuide(guide); auto settings = canvas.guideSettings(); settings.snapping = true; canvas.setGuideSettings(settings);
        canvas.brush.size = 5; canvas.brush.color = Qt::black; canvas.setPressureSize(false);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, view(canvas,{300,300}));
        dragMove(canvas,view(canvas,{305,220})); dragMove(canvas,view(canvas,{365,110}));
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, view(canvas,{365,110}));
        const auto frame = renderMovieFrame(canvas.project());
        QVERIFY(frame.pixelColor(300,180).alpha() > 0); QCOMPARE(frame.pixelColor(335,180).alpha(), 0);
    }
    void fisheyeDrawsCurvesWithSparseEventsAndStabilization_data() {
        QTest::addColumn<qreal>("smoothing"); QTest::newRow("direct") << qreal(0); QTest::newRow("stabilized") << qreal(100);
    }
    void fisheyeDrawsCurvesWithSparseEventsAndStabilization() {
        QFETCH(qreal,smoothing);
        Canvas canvas; setup(canvas); canvas.addDefaultGuide(GuideType::Fisheye);
        auto guide = canvas.drawingGuides().first(); guide.points = {{100,240},{540,240},{320,60}}; canvas.updateGuide(guide);
        auto settings = canvas.guideSettings(); settings.snapping = true; canvas.setGuideSettings(settings);
        canvas.brush.size = 4; canvas.brush.color = Qt::black; canvas.setPressureSize(false); canvas.setStabilization(smoothing);
        const auto start = view(canvas,{188,168});
        const auto constraints = guideSnapConstraints(guide, canvas.artworkPoint(start)); QCOMPARE(constraints.size(), 2);
        const auto history = canvas.document()->history()->count();
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, start);
        dragMove(canvas,view(canvas,{220,160})); dragMove(canvas,view(canvas,{452,168}));
        const auto tip = canvas.artworkPoint(canvas.brushCursorRect().center());
        QVERIFY2(guideConstraintDistance(tip, constraints.first()) < .01,
            qPrintable(QString("Cursor distance from active curve: %1").arg(guideConstraintDistance(tip, constraints.first()))));
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, view(canvas,{452,168}));
        const auto frame = renderMovieFrame(canvas.project());
        for (int x = 260; x <= 400; x += 20) {
            const auto y = qRound(240 - 90 * std::sqrt(1 - std::pow((x - 320.) / 220,2)));
            QVERIFY2(frame.pixelColor(x,y).alpha() > 0, qPrintable(QString("Missing curved stroke at %1,%2").arg(x).arg(y)));
        }
        QCOMPARE(frame.pixelColor(320,168).alpha(), 0); // A chord would incorrectly paint here.
        QCOMPARE(canvas.document()->history()->count(), history + 1);
        canvas.document()->history()->undo(); QVERIFY(canvas.shot().layers.first().tiles.isEmpty());
    }
    void fisheyeHeightAndAxisHandlesPreserveShapeAndCaches() {
        Canvas canvas; setup(canvas); canvas.addDefaultGuide(GuideType::ParallelRuler); canvas.addDefaultGuide(GuideType::Fisheye);
        canvas.setTool(Canvas::Tool::Guides); auto guide = canvas.drawingGuides().last();
        const auto before = guide; const auto start = view(canvas, guide.points[2]);
        const auto target = view(canvas,{360,100});
        QImage image(canvas.size(),QImage::Format_ARGB32_Premultiplied); canvas.render(&image);
        const auto artCount = canvas.artworkRenderCount(), guideCount = canvas.guideCacheRenderCount();
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,start); dragMove(canvas,target); canvas.render(&image);
        QCOMPARE(canvas.artworkRenderCount(),artCount); QCOMPARE(canvas.guideCacheRenderCount(),guideCount);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::ShiftModifier,target);
        guide = canvas.drawingGuides().last();
        const auto center = (guide.points[0] + guide.points[1]) / 2;
        QVERIFY(qAbs(QLineF(center,guide.points[2]).length() - QLineF(guide.points[0],guide.points[1]).length()/2) < .01);
        canvas.document()->history()->undo(); QCOMPARE(canvas.drawingGuides().last(),before);
        const auto axisStart = view(canvas,before.points[1]); const auto axisEnd = axisStart + QPoint(-30,40);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,axisStart); dragMove(canvas,axisEnd); QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,axisEnd);
        guide = canvas.drawingGuides().last(); const auto newCenter = (guide.points[0] + guide.points[1])/2;
        QVERIFY(qAbs(QPointF::dotProduct(guide.points[2]-newCenter,guide.points[1]-guide.points[0])) < .01);
        QVERIFY(qAbs(QLineF(newCenter,guide.points[2]).length() - QLineF((before.points[0]+before.points[1])/2,before.points[2]).length()) < .01);
    }
    void stabilizedFisheyePenKeepsStationaryPressureAndAirbrushOnCurve() {
        Canvas canvas; setup(canvas); canvas.addDefaultGuide(GuideType::Fisheye);
        auto guide = canvas.drawingGuides().first(); guide.points = {{100,240},{540,240},{320,60}}; canvas.updateGuide(guide);
        auto settings = canvas.guideSettings(); settings.snapping = true; canvas.setGuideSettings(settings);
        canvas.brush.size = 40; canvas.brush.airbrush = true; canvas.brush.airbrushRate = 60;
        canvas.setStabilization(100); canvas.setPressureSize(true);
        QPointingDevice device("Guide test pen",7,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
        auto pen = [&](QEvent::Type type, QPoint point, qreal pressure) {
            QTabletEvent event(type,&device,QPointF(point),QPointF(canvas.mapToGlobal(point)),pressure,0,0,0,0,0,Qt::NoModifier,
                type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton,
                type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton);
            QApplication::sendEvent(&canvas,&event);
        };
        const auto start = view(canvas,{188,168}), next = view(canvas,{270,152});
        const auto curve = guideSnapConstraints(guide,canvas.artworkPoint(start)).first();
        pen(QEvent::TabletPress,start,.25); pen(QEvent::TabletMove,start,.5);
        QVERIFY2(qAbs(canvas.brushCursorRect().width() - 20 * canvas.zoom()) < .01,
            qPrintable(QString("Undecided guide cursor: %1, zoom: %2, drawing: %3").arg(canvas.brushCursorRect().width()).arg(canvas.zoom()).arg(canvas.document()->strokeActive())));
        pen(QEvent::TabletMove,next,.5); pen(QEvent::TabletMove,next,.75);
        QVERIFY2(qAbs(canvas.brushCursorRect().width() - 30 * canvas.zoom()) < .01,
            qPrintable(QString("Curved cursor: %1, zoom: %2, drawing: %3").arg(canvas.brushCursorRect().width()).arg(canvas.zoom()).arg(canvas.document()->strokeActive())));
        const auto tip = canvas.artworkPoint(canvas.brushCursorRect().center());
        QVERIFY(guideConstraintDistance(tip,curve) < .01);
        const auto beforeTicks = canvas.document()->brushDabIndex();
        QTRY_VERIFY_WITH_TIMEOUT(canvas.document()->brushDabIndex() > beforeTicks, 200);
        QVERIFY(QLineF(canvas.artworkPoint(canvas.brushCursorRect().center()),tip).length() < .01);
        pen(QEvent::TabletRelease,next,0); QVERIFY(!canvas.document()->strokeActive());
    }
    void handlesUseArtworkCoordinatesOneUndoAndCachedPreview() {
        Canvas canvas; setup(canvas); canvas.addDefaultGuide(GuideType::Perspective); canvas.setTool(Canvas::Tool::Guides);
        canvas.setRotation(21); auto original=canvas.drawingGuides().first();
        QImage screenshot(canvas.size(),QImage::Format_ARGB32_Premultiplied); canvas.render(&screenshot);
        const auto start=view(canvas,original.points.first()); const auto end=start+QPoint(32,18);
        const int history=canvas.document()->history()->count();
        const auto renders=canvas.artworkRenderCount();
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,start); dragMove(canvas,end);
        canvas.render(&screenshot); QCOMPARE(canvas.artworkRenderCount(),renders);
        QCOMPARE(canvas.shot().guides.first().points,original.points); // Only overlay preview changes during drag.
        QVERIFY(canvas.drawingGuides().first().points!=original.points);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,end);
        QCOMPARE(canvas.document()->history()->count(),history+1);
        QVERIFY(QLineF(canvas.shot().guides.first().points.first(),canvas.artworkPoint(end)).length()<.01);
        canvas.document()->history()->undo(); QCOMPARE(canvas.shot().guides.first(),original);
        auto locked=original; locked.locked=true; canvas.updateGuide(locked);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,start); dragMove(canvas,end); QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,end);
        QCOMPARE(canvas.shot().guides.first().points,original.points);
    }
    void lostReleaseAndFocusCancelGuidePreview() {
        Canvas canvas; setup(canvas); canvas.addDefaultGuide(GuideType::ParallelRuler); canvas.setTool(Canvas::Tool::Guides);
        const auto original=canvas.drawingGuides().first(); auto start=view(canvas,original.points.first());
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,start); dragMove(canvas,start+QPoint(30,20));
        QFocusEvent focus(QEvent::FocusOut); QApplication::sendEvent(&canvas,&focus);
        QCOMPARE(canvas.drawingGuides().first(),original); QCOMPARE(canvas.document()->history()->count(),1);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,start+QPoint(30,20));
        QCOMPARE(canvas.drawingGuides().first(),original);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,start); dragMove(canvas,start+QPoint(20,20));
        QMouseEvent hover(QEvent::MouseMove,QPointF(start),QPointF(canvas.mapToGlobal(start)),Qt::NoButton,Qt::NoButton,Qt::NoModifier); QApplication::sendEvent(&canvas,&hover);
        QCOMPARE(canvas.drawingGuides().first(),original);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,start); QCOMPARE(canvas.document()->history()->count(),1);
    }
    void snappingChoosesDirectionOnceAndHiddenGuidesStillWork() {
        Canvas canvas; setup(canvas); canvas.addDefaultGuide(GuideType::ParallelRuler);
        auto guide=canvas.drawingGuides().first(); guide.points={{0,100},{640,100}}; guide.visible=false; canvas.updateGuide(guide);
        canvas.addDefaultGuide(GuideType::ParallelRuler); auto vertical=canvas.drawingGuides().last(); vertical.points={{300,0},{300,480}}; canvas.updateGuide(vertical);
        auto settings=canvas.guideSettings(); settings.visible=false; settings.snapping=true; canvas.setGuideSettings(settings);
        canvas.setTool(Canvas::Tool::Brush); canvas.brush.size=6; canvas.brush.color=Qt::black; canvas.setPressureSize(false);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,view(canvas,{100,200}));
        dragMove(canvas,view(canvas,{240,206})); dragMove(canvas,view(canvas,{310,280}));
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,view(canvas,{310,280}));
        auto frame=renderMovieFrame(canvas.project()); QVERIFY(frame.pixelColor(250,200).alpha()>0); QCOMPARE(frame.pixelColor(250,240).alpha(),0); QVERIFY(frame.pixelColor(305,200).alpha()>0);
        canvas.document()->history()->undo(); QVERIFY(canvas.shot().layers.first().tiles.isEmpty());
        settings.snapping=false; canvas.setGuideSettings(settings);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,view(canvas,{100,200})); dragMove(canvas,view(canvas,{300,280})); QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,view(canvas,{300,280}));
        frame=renderMovieFrame(canvas.project()); QVERIFY(frame.pixelColor(250,260).alpha()>0);
    }
    void undoArtworkCancelsUncommittedGuideEdits() {
        Canvas canvas; setup(canvas); canvas.addDefaultGuide(GuideType::Perspective);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, view(canvas, {100,100}));
        auto *history = canvas.document()->history(); QCOMPARE(history->count(), 2);
        canvas.setTool(Canvas::Tool::Guides); const auto guide = canvas.drawingGuides().first();
        const auto start = view(canvas, guide.points.first()), end = start + QPoint(30,20);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, start); dragMove(canvas, end);
        QVERIFY(canvas.drawingGuides().first() != guide);
        history->undo();
        QCOMPARE(canvas.drawingGuides().first(), guide);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, end);
        QVERIFY(history->canRedo()); QCOMPARE(history->count(), 2);
        canvas.beginGuideCreation(GuideType::TwoPoint);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, view(canvas, {20,100}));
        history->redo(); QVERIFY(!canvas.guideCreating()); QCOMPARE(canvas.drawingGuides().size(), 1);
    }
    void limitedAssistantReleasesStrokeOutsideItsArea() {
        Canvas canvas; setup(canvas); canvas.addDefaultGuide(GuideType::ParallelRuler);
        auto guide = canvas.drawingGuides().first(); guide.points = {{0,100},{640,100}};
        guide.limited = true; guide.area = QRectF(0,0,280,480); canvas.updateGuide(guide);
        auto settings = canvas.guideSettings(); settings.snapping = true; canvas.setGuideSettings(settings);
        canvas.brush.size = 6; canvas.brush.color = Qt::black; canvas.setPressureSize(false);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, view(canvas, {100,150}));
        dragMove(canvas, view(canvas, {240,160})); dragMove(canvas, view(canvas, {400,300}));
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, view(canvas, {400,300}));
        const auto frame = renderMovieFrame(canvas.project());
        QVERIFY(frame.pixelColor(200,150).alpha() > 0);
        QVERIFY(frame.pixelColor(397,297).alpha() > 0);
        QCOMPARE(frame.pixelColor(350,150).alpha(), 0);
    }
    void canvasHandleHelpUsesDelayedTooltipSurface() {
        Canvas canvas; setup(canvas); canvas.addDefaultGuide(GuideType::Perspective); canvas.setTool(Canvas::Tool::Guides);
        const auto point = view(canvas, canvas.drawingGuides().first().points.first());
        QHelpEvent help(QEvent::ToolTip, point, canvas.mapToGlobal(point)); QApplication::sendEvent(&canvas, &help);
        QVERIFY(QToolTip::text().contains("Move guide handle"));
        QToolTip::hideText();
    }
    void guideCacheSurvivesBrushDabsAndOtherGuideDrags() {
        Canvas canvas; setup(canvas); canvas.addDefaultGuide(GuideType::ParallelRuler);
        canvas.addDefaultGuide(GuideType::Perspective);
        QImage image(canvas.size(), QImage::Format_ARGB32_Premultiplied); canvas.render(&image);
        auto renders = canvas.guideCacheRenderCount(); QVERIFY(renders > 0);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, view(canvas, {100,100}));
        dragMove(canvas, view(canvas, {200,200})); canvas.render(&image);
        QCOMPARE(canvas.guideCacheRenderCount(), renders);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, view(canvas, {200,200}));
        canvas.setTool(Canvas::Tool::Guides); canvas.render(&image);
        renders = canvas.guideCacheRenderCount();
        const auto start = view(canvas, canvas.drawingGuides().last().points.first()), end = start + QPoint(30,20);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, start); dragMove(canvas, end); canvas.render(&image);
        QCOMPARE(canvas.guideCacheRenderCount(), renders);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, end); canvas.render(&image);
        QCOMPARE(canvas.guideCacheRenderCount(), renders);
        auto settings = canvas.guideSettings(); settings.color = Qt::red; canvas.setGuideSettings(settings); canvas.render(&image);
        QCOMPARE(canvas.guideCacheRenderCount(), renders + 1);
        const auto colored = image;
        settings.visible = false; canvas.setGuideSettings(settings); canvas.render(&image);
        QVERIFY(image != colored);
    }
    void tabletEditsGuideWithoutPaintingAndExportsIgnoreOverlays() {
        Canvas canvas; setup(canvas); canvas.addDefaultGuide(GuideType::TwoPoint); canvas.setTool(Canvas::Tool::Guides);
        auto guide=canvas.drawingGuides().first(); const auto original=renderSheet(canvas.project(),canvas.shot());
        const auto start=view(canvas,guide.points.last()), end=start+QPoint(30,10);
        QPointingDevice device("Test pen",3,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
        auto pen=[&](QEvent::Type type,QPoint point,Qt::MouseButton button,Qt::MouseButtons buttons) {
            QTabletEvent event(type,&device,QPointF(point),QPointF(canvas.mapToGlobal(point)),.7,0,0,0,0,0,Qt::NoModifier,button,buttons); QApplication::sendEvent(&canvas,&event);
        };
        pen(QEvent::TabletPress,start,Qt::LeftButton,Qt::LeftButton); pen(QEvent::TabletMove,end,Qt::NoButton,Qt::LeftButton); pen(QEvent::TabletRelease,end,Qt::LeftButton,Qt::NoButton);
        QVERIFY(canvas.shot().guides.first().points!=guide.points); QVERIFY(canvas.shot().layers.first().tiles.isEmpty());
        QCOMPARE(renderSheet(canvas.project(),canvas.shot()),original);
        const auto committed = canvas.drawingGuides().first(); const auto undoCount = canvas.document()->history()->count();
        pen(QEvent::TabletPress,end,Qt::LeftButton,Qt::LeftButton); pen(QEvent::TabletMove,end+QPoint(20,10),Qt::NoButton,Qt::LeftButton);
        QVERIFY(canvas.drawingGuides().first() != committed);
        pen(QEvent::TabletMove,end+QPoint(40,30),Qt::NoButton,Qt::NoButton);
        QCOMPARE(canvas.drawingGuides().first(), committed); QCOMPARE(canvas.document()->history()->count(), undoCount);
        canvas.beginGuideCreation(GuideType::Perspective);
        pen(QEvent::TabletPress,start,Qt::RightButton,Qt::RightButton); QVERIFY(!canvas.guideCreating());
        canvas.addDefaultGuide(GuideType::Perspective); auto second=canvas.drawingGuides().last(); second.customColor=true; second.color=QColor("#e9a668"); canvas.updateGuide(second);
        QDir().mkpath("test-artifacts"); QVERIFY(canvas.grab().save("test-artifacts/perspective-guides.png"));
    }
};
QTEST_MAIN(GuideCanvasTests)
#include "test_guide_canvas.moc"
