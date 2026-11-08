#include "feature_flags.h"
#include "canvas.h"
#include "catalog_brushes.h"
#include "object_tools.h"
#include "scene3d_controller.h"
#include <QApplication>
#include <QDir>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QFocusEvent>
#include <QTabletEvent>
#include <QtTest>
#include <cmath>

using namespace toon;

static void hover(Canvas &canvas, QPointF position) {
    QMouseEvent event(QEvent::MouseMove, position, canvas.mapToGlobal(position),
        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &event);
}

static bool saveCursorPreview(const QImage &image, const QString &name) {
    const auto directory = qEnvironmentVariable("TOON_CURSOR_PREVIEW");
    return directory.isEmpty() || (QDir().mkpath(directory) && image.save(QDir(directory).filePath(name)));
}

class CursorTest : public QObject {
    Q_OBJECT
private slots:
    void sceneToolsKeepVisibleCursors_data() {
        QTest::addColumn<bool>("camera");
        QTest::newRow("3D object") << false;
        QTest::newRow("3D camera") << true;
    }
    void sceneToolsKeepVisibleCursors() {
        if (!features::legacy3DEditor) QSKIP("Legacy 3D authoring is retired; compatibility coverage remains enabled.");
        QFETCH(bool, camera);
        Canvas canvas; canvas.resize(1000, 800);
        auto project = Project::create(); project.width = 640; project.height = 480;
        canvas.setProject(project); canvas.show();
        auto *scene = canvas.scene3D(); auto *doc = canvas.document();
        const auto tool = camera ? Canvas::Tool::SceneCamera : Canvas::Tool::SceneObject;
        const auto expected = camera ? Qt::OpenHandCursor : Qt::SizeAllCursor;
        const QPointF position(500, 400);
        canvas.setTool(tool); hover(canvas, position);
        QCOMPARE(canvas.cursor().shape(), Qt::ForbiddenCursor); QVERIFY(!canvas.interactionHint().isEmpty());
        QVERIFY(scene->addModel(builtInModels3D().first())); scene->selectTool(camera);
        const int history = doc->history()->count(); const auto before = canvas.project().toJson();
        for (int mode = 0; mode < 3; ++mode) {
            scene->setManipulation(static_cast<Scene3DController::Manipulation>(mode + (camera ? 3 : 0)));
            QEvent leave(QEvent::Leave); QApplication::sendEvent(&canvas, &leave);
            QEnterEvent enter(position, position, canvas.mapToGlobal(position)); QApplication::sendEvent(&canvas, &enter);
            QCOMPARE(canvas.cursor().shape(), expected); QVERIFY(canvas.brushCursorPath().isEmpty());
            hover(canvas, position + QPointF(30, 20)); QCOMPARE(canvas.cursor().shape(), expected);
        }
        QPointingDevice pen("3D cursor pen", 31, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
        QTabletEvent penHover(QEvent::TabletMove, &pen, position, canvas.mapToGlobal(position), 0, 0, 0, 0, 0, 0,
            Qt::NoModifier, Qt::NoButton, Qt::NoButton);
        QApplication::sendEvent(&canvas, &penHover); QCOMPARE(canvas.cursor().shape(), expected);
        QTest::keyPress(&canvas, Qt::Key_Space); QCOMPARE(canvas.cursor().shape(), Qt::OpenHandCursor);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, position.toPoint());
        QCOMPARE(canvas.cursor().shape(), Qt::ClosedHandCursor);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, position.toPoint());
        QTest::keyRelease(&canvas, Qt::Key_Space); QCOMPARE(canvas.cursor().shape(), expected);
        QCOMPARE(doc->history()->count(), history); QCOMPARE(canvas.project().toJson(), before);
        const auto layerId = scene->layerId();
        doc->setLayersLocked({layerId}, true);
        QCOMPARE(canvas.cursor().shape(), Qt::ForbiddenCursor); QVERIFY(!canvas.interactionHint().isEmpty());
        doc->setLayersLocked({layerId}, false); QCOMPARE(canvas.cursor().shape(), expected);
        doc->setLayersVisible({layerId}, false); QCOMPARE(canvas.cursor().shape(), Qt::ForbiddenCursor);
        doc->setLayersVisible({layerId}, true); QCOMPARE(canvas.cursor().shape(), expected);
        doc->setLayerPositionLocked(true); QCOMPARE(canvas.cursor().shape(), Qt::ForbiddenCursor);
        doc->setLayerPositionLocked(false); QCOMPARE(canvas.cursor().shape(), expected);
        canvas.setTool(Canvas::Tool::Brush); QCOMPARE(canvas.cursor().shape(), Qt::ForbiddenCursor);
        for (int i = 0; i < canvas.shot().layers.size(); ++i)
            if (canvas.shot().layers[i].id == project.shots.first().layers.first().id) doc->selectLayer(i);
        hover(canvas, position); QCOMPARE(canvas.cursor().shape(), Qt::BlankCursor);
        QVERIFY(!canvas.brushCursorPath().isEmpty());
        scene->selectTool(camera); QCOMPARE(canvas.cursor().shape(), expected);
        QVERIFY(canvas.brushCursorPath().isEmpty()); QVERIFY(canvas.interactionHint().isEmpty());
    }
    void coverageFollowsSizeAndView() {
        Canvas canvas; canvas.resize(1000, 800); canvas.show();
        QCOMPARE(canvas.cursor().shape(), Qt::ArrowCursor);
        QVERIFY(canvas.brushCursorRect().isEmpty());
        canvas.setProject(Project::create()); canvas.setZoom(1.5); canvas.setBrushSize(40);
        const QPointF position(500, 400);
        hover(canvas, position);
        QCOMPARE(canvas.cursor().shape(), Qt::BlankCursor);
        QCOMPARE(canvas.brushCursorRect(), QRectF(470, 370, 60, 60));
        canvas.setRotation(47);
        QCOMPARE(canvas.brushCursorRect().size(), QSizeF(60, 60));
        canvas.setZoom(.5);
        QCOMPARE(canvas.brushCursorRect().size(), QSizeF(20, 20));
        canvas.setBrushSize(128);
        QCOMPARE(canvas.brushCursorRect().size(), QSizeF(64, 64));
        canvas.setTool(Canvas::Tool::Eraser);
        QCOMPARE(canvas.brushCursorRect().size(), QSizeF(64, 64));
        canvas.setBrushSize(256); canvas.setZoom(16);
        QCOMPARE(canvas.brushCursorRect().size(), QSizeF(4096, 4096));
        QVERIFY(canvas.document()->history()->isClean());
    }
    void sampledBrushOutlineMatchesOnRasterAndVectorLayers() {
        Canvas canvas;canvas.resize(1000,800);canvas.setProject(Project::create());canvas.show();canvas.setZoom(1.5);
        auto brush=canvas.brush;brush.size=70;brush.tipMask=QImage(24,12,QImage::Format_Grayscale8);brush.tipMask.fill(0);
        for(int y=2;y<10;++y)for(int x=2;x<8;++x)brush.tipMask.scanLine(y)[x]=255;
        for(int y=7;y<10;++y)for(int x=2;x<22;++x)brush.tipMask.scanLine(y)[x]=255;
        canvas.setBrushSettings(brush);hover(canvas,{500,400});const auto raster=canvas.brushCursorPath();
        QVERIFY(!raster.isEmpty());QVERIFY(raster.boundingRect().width()>raster.boundingRect().height());
        QVERIFY(ObjectTools::addLayer(canvas.document(),LayerKind::Vector));hover(canvas,{500,400});
        QCOMPARE(canvas.brushCursorPath(),raster);QCOMPARE(canvas.cursor().shape(),Qt::BlankCursor);
    }
    void handAndLeavingRestoreNativeCursor() {
        Canvas canvas; canvas.resize(1000, 800); canvas.setProject(Project::create()); canvas.show();
        const QPointF position(500, 400); hover(canvas, position);
        QVERIFY(!canvas.brushCursorRect().isEmpty());
        QTest::keyPress(&canvas, Qt::Key_Space);
        QCOMPARE(canvas.cursor().shape(), Qt::OpenHandCursor);
        QVERIFY(canvas.brushCursorRect().isEmpty());
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, position.toPoint());
        QCOMPARE(canvas.cursor().shape(), Qt::ClosedHandCursor);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, position.toPoint());
        QCOMPARE(canvas.cursor().shape(), Qt::OpenHandCursor);
        QTest::keyRelease(&canvas, Qt::Key_Space);
        QCOMPARE(canvas.cursor().shape(), Qt::BlankCursor);
        QVERIFY(!canvas.brushCursorRect().isEmpty());
        canvas.setTool(Canvas::Tool::Hand);
        QCOMPARE(canvas.cursor().shape(), Qt::OpenHandCursor);
        QVERIFY(canvas.brushCursorRect().isEmpty());
        canvas.setTool(Canvas::Tool::Brush);
        QEvent leave(QEvent::Leave); QApplication::sendEvent(&canvas, &leave);
        QCOMPARE(canvas.cursor().shape(), Qt::ArrowCursor);
        QVERIFY(canvas.brushCursorRect().isEmpty());
        hover(canvas, {-5, 400});
        QVERIFY(canvas.brushCursorRect().isEmpty());
        hover(canvas, position);
        QVERIFY(!canvas.brushCursorRect().isEmpty());
        QVERIFY(canvas.document()->history()->isClean());
    }
    void penPressureMatchesDabCoverage() {
        Canvas canvas; canvas.resize(1000, 800); canvas.setProject(Project::create()); canvas.show();
        canvas.setZoom(2); canvas.setBrushSize(40);
        QPointingDevice pen("Test pen", 11, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
        auto send = [&](QEvent::Type type, qreal pressure) {
            const QPointF position(500, 400);
            QTabletEvent event(type, &pen, position, canvas.mapToGlobal(position), pressure, 0, 0, 0, 0, 0,
                Qt::NoModifier, type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton,
                type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton);
            QApplication::sendEvent(&canvas, &event);
            QVERIFY(event.isAccepted());
        };
        send(QEvent::TabletMove, 0);
        QCOMPARE(canvas.brushCursorRect().width(), 80);
        send(QEvent::TabletPress, .25);
        QCOMPARE(canvas.brushCursorRect().width(), 20);
        send(QEvent::TabletMove, .5);
        QCOMPARE(canvas.brushCursorRect().width(), 40);
        send(QEvent::TabletRelease, 0);
        QCOMPARE(canvas.brushCursorRect().width(), 80);
        canvas.setPressureSize(false);
        send(QEvent::TabletPress, .25);
        QCOMPARE(canvas.brushCursorRect().width(), 80);
        send(QEvent::TabletRelease, 0);
    }
    void circleIsAViewOverlayOnly() {
        Canvas canvas; canvas.resize(1000, 800); canvas.setProject(Project::create()); canvas.show();
        canvas.setZoom(1); canvas.setBrushSize(80);
        const QPointF position(500, 400); hover(canvas, position);
        const auto frameBefore = renderMovieFrame(canvas.project());
        const auto sheetBefore = renderSheet(canvas.project(), canvas.shot());
        canvas.setTool(Canvas::Tool::Hand);
        const auto clean = canvas.grab().toImage();
        canvas.setTool(Canvas::Tool::Brush);
        const auto outlined = canvas.grab().toImage();
        const auto physical = [&](QPointF point) { return (point * outlined.devicePixelRatio()).toPoint(); };
        QCOMPARE(outlined.pixelColor(physical(position)), clean.pixelColor(physical(position)));
        QVERIFY(outlined.pixelColor(physical({540, 400})) != clean.pixelColor(physical({540, 400})));
        QCOMPARE(outlined.pixelColor(physical({545, 400})), clean.pixelColor(physical({545, 400})));
        QCOMPARE(renderMovieFrame(canvas.project()), frameBefore);
        QCOMPARE(renderSheet(canvas.project(), canvas.shot()), sheetBefore);
        QVERIFY(canvas.document()->history()->isClean());
        QDir().mkpath("test-artifacts");
        QVERIFY(outlined.save("test-artifacts/brush-cursor.png"));
        QVERIFY(saveCursorPreview(outlined, "round-cursor.png"));
    }
    void responseCurveAndLockedRandomnessMatchTheCursor() {
        Canvas canvas;canvas.resize(1000,800);canvas.setProject(Project::create());canvas.show();canvas.setZoom(2);
        auto brush=canvas.brush;brush.size=40;brush.sizeDynamics.control=BrushControl::Pressure;
        brush.sizeDynamics.response={0,.05,.1,.4,1};canvas.setBrushSettings(brush);
        QPointingDevice pen("Curve pen",12,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
        const QPointF point(500,400);
        QTabletEvent press(QEvent::TabletPress,&pen,point,canvas.mapToGlobal(point),.5,0,0,0,0,0,
            Qt::NoModifier,Qt::LeftButton,Qt::LeftButton);
        QApplication::sendEvent(&canvas,&press);
        QVERIFY(canvas.brushCursorRect().width()>=7);QVERIFY(canvas.brushCursorRect().width()<=12);
        QTabletEvent release(QEvent::TabletRelease,&pen,point,canvas.mapToGlobal(point),0,0,0,0,0,0,
            Qt::NoModifier,Qt::LeftButton,Qt::NoButton);QApplication::sendEvent(&canvas,&release);
        brush=BrushSettings{};brush.size=60;brush.pressureSize=false;brush.sizeDynamics.jitter=.8;
        brush.sizeDynamics.jitterPerStroke=true;brush.lockRandomSeed=true;brush.randomSeed=7;
        canvas.setBrushSettings(brush);hover(canvas,point);const auto first=canvas.brushCursorRect().width();
        brush.randomSeed=17;canvas.setBrushSettings(brush);hover(canvas,point);
        const auto fixed=canvas.brushCursorRect().width();QVERIFY(std::abs(first-fixed)>1);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point.toPoint());
        QCOMPARE(canvas.brushCursorRect().width(),fixed);const auto builds=canvas.brushOutlineBuildCount();
        QTest::mouseMove(&canvas,{560,410});QCOMPARE(canvas.brushCursorRect().width(),fixed);
        QCOMPARE(canvas.brushOutlineBuildCount(),builds);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,{560,410});
    }
    void sampledContourTracksShapeFlipAndRotation() {
        Canvas canvas; canvas.resize(1000, 800); canvas.setProject(Project::create()); canvas.show(); canvas.setZoom(1);
        auto settings = canvas.brush; settings.size = 80; settings.pressureSize = false;
        settings.tipMask = QImage(8, 8, QImage::Format_Grayscale8); settings.tipMask.fill(0);
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x)
            if (x < 3 || y >= 5) settings.tipMask.scanLine(y)[x] = 255;
        canvas.setBrushSettings(settings);
        const QPointF center(500, 400); hover(canvas, center);
        auto path = canvas.brushCursorPath();
        QVERIFY(path.contains(center + QPointF(-30, -25)));
        QVERIFY(path.contains(center + QPointF(25, 30)));
        QVERIFY(!path.contains(center + QPointF(25, -25)));
        settings.flipX = true; canvas.setBrushSettings(settings); path = canvas.brushCursorPath();
        QVERIFY(path.contains(center + QPointF(30, -25)));
        QVERIFY(!path.contains(center + QPointF(-25, -25)));
        settings.flipX = false; settings.angle = 90; canvas.setBrushSettings(settings);
        const auto rotated = canvas.brushCursorPath();
        QVERIFY(rotated.contains(center + QPointF(25, -30)));
        QVERIFY(!rotated.contains(center + QPointF(25, 25)));
        settings.angle = 0; settings.roundness = .5; canvas.setBrushSettings(settings);
        QVERIFY(canvas.brushCursorRect().height() < canvas.brushCursorRect().width() * .6);
        const auto before = renderMovieFrame(canvas.project()); canvas.setRotation(90);
        QVERIFY(canvas.brushCursorRect().width() < canvas.brushCursorRect().height() * .6);
        QCOMPARE(renderMovieFrame(canvas.project()), before); QVERIFY(canvas.document()->history()->isClean());
        QDir().mkpath("test-artifacts"); QVERIFY(canvas.grab().save("test-artifacts/stamp-cursor.png"));
        QVERIFY(saveCursorPreview(canvas.grab().toImage(), "stamp-cursor.png"));
    }
    void largeDetailedCursorRemainsAnOverlay_data() {
        QTest::addColumn<bool>("longContour");
        QTest::newRow("noisy-holes") << false;
        QTest::newRow("long-contour") << true;
    }
    void largeDetailedCursorRemainsAnOverlay() {
        QFETCH(bool, longContour);
        Canvas canvas; canvas.resize(800, 600);
        auto project = Project::create(); project.width = project.height = 512;
        canvas.setProject(project); canvas.show(); canvas.setZoom(1);
        BrushSettings brush; brush.size = 256; brush.pressureSize = false;
        brush.lockRandomSeed = true; brush.randomSeed = 713; brush.noise = !longContour;
        brush.tipMask = QImage(256, 256, QImage::Format_Grayscale8); brush.tipMask.fill(0);
        if (longContour) {
            // Connected zigzag teeth make one contour with thousands of turns,
            // exercising filled-mask rasterization without a huge image.
            for (int y = 4; y < 240; ++y) for (int tooth = 0; tooth < 61; ++tooth) {
                const int x = 4 + tooth * 4 + y % 2;
                brush.tipMask.scanLine(y)[x] = brush.tipMask.scanLine(y)[x + 1] = 255;
            }
            for (int y = 240; y < 248; ++y) for (int x = 4; x < 248; ++x)
                brush.tipMask.scanLine(y)[x] = 255;
        } else {
            // Many disconnected particles plus a hollow center test detailed
            // rasterization and hole preservation. Noise varies per pixel.
            for (int y = 4; y < 252; ++y) for (int x = 4; x < 252; ++x) {
                const int radius = qMax(std::abs(x - 128), std::abs(y - 128));
                if ((radius >= 18 && radius < 38) || (radius > 48 && x % 8 < 3 && y % 8 < 3))
                    brush.tipMask.scanLine(y)[x] = 255;
            }
        }
        canvas.setBrushSettings(brush);
        const QPointF center(400, 300); hover(canvas, center);
        const auto path = canvas.brushCursorPath();
        QVERIFY(path.elementCount() > 256);
        int contours = 0, currentElements = 0, longest = 0;
        for (int i = 0; i < path.elementCount(); ++i) {
            if (path.elementAt(i).isMoveTo()) {
                longest = qMax(longest, currentElements); currentElements = 0; ++contours;
            }
            ++currentElements;
        }
        longest = qMax(longest, currentElements);
        if (longContour) QVERIFY(longest > 4096);
        else {
            QVERIFY(contours > 40);
            QVERIFY(!path.contains(center));
            QVERIFY(path.contains(center + QPointF(28, 0)));
        }
        const auto movie = renderMovieFrame(canvas.project());
        const auto sheet = renderSheet(canvas.project(), canvas.shot());
        const auto document = canvas.project().toJson();
        const auto builds = canvas.brushOutlineBuildCount();
        const auto history = canvas.document()->history()->count();
        canvas.setTool(Canvas::Tool::Hand);
        const auto clean = canvas.grab().toImage();
        canvas.setTool(Canvas::Tool::Brush); hover(canvas, center);
        const auto outlined = canvas.grab().toImage();
        QVERIFY(!outlined.isNull()); QCOMPARE(outlined.size(), clean.size());
        QCOMPARE(canvas.brushCursorPath(), path);
        QCOMPARE(canvas.brushCursorRect(), path.boundingRect());
        QCOMPARE(canvas.brushOutlineBuildCount(), builds);
        const qreal ratio = outlined.devicePixelRatio();
        const QRect affected = QRectF(path.boundingRect().topLeft() * ratio,
            path.boundingRect().size() * ratio).adjusted(-5 * ratio, -5 * ratio, 5 * ratio, 5 * ratio)
            .toAlignedRect().intersected(outlined.rect());
        int changed = 0;
        for (int y = 0; y < outlined.height(); ++y) for (int x = 0; x < outlined.width(); ++x) {
            if (outlined.pixel(x, y) == clean.pixel(x, y)) continue;
            QVERIFY2(affected.contains(x, y), "A cursor repaint changed pixels outside its outline bounds");
            ++changed;
        }
        QVERIFY(changed > 256);
        if (!longContour) {
            // The wide central hole stays clear in the raster overlay.
            const QRect hole = QRectF((center - QPointF(10, 10)) * ratio,
                QSizeF(20 * ratio, 20 * ratio)).toAlignedRect();
            QCOMPARE(outlined.copy(hole), clean.copy(hole));
        }
        QCOMPARE(renderMovieFrame(canvas.project()), movie);
        QCOMPARE(renderSheet(canvas.project(), canvas.shot()), sheet);
        QCOMPARE(canvas.project().toJson(), document);
        QCOMPARE(canvas.document()->history()->count(), history);
        QVERIFY(canvas.document()->history()->isClean());
        QVERIFY(saveCursorPreview(outlined, longContour ? "large-cursor-long-contour.png" : "large-cursor-noisy-holes.png"));
        canvas.setTool(Canvas::Tool::Hand);
        QCOMPARE(canvas.grab().toImage(), clean);
    }
    void connectedTabletCursorFollowsRepeatsInsteadOfInputPackets() {
        Canvas canvas;canvas.resize(1000,800);auto project=Project::create();canvas.setProject(project);canvas.show();canvas.setZoom(.6);canvas.setHoldShapeEnabled(false);QVERIFY(QTest::qWaitForWindowExposed(&canvas));QApplication::processEvents();
        BrushSettings brush;bool found=false;
        for(const auto &preset:catalogBrushPresets())if(preset.id=="builtin/catalog/smoke-trail"){brush=preset.brush;found=true;break;}
        QVERIFY(found);brush.size=512;brush.smoothingEnabled=false;canvas.applyBrushSettings(brush,0);
        QPointingDevice pen("Connected pen",42,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
        const auto send=[&](QEvent::Type type,QPointF art,qreal pressure=1,int tilt=0) {
            const auto p=canvas.viewTransform().map(art+sheetGeometry(canvas.project(),canvas.shot()).output.topLeft());
            const bool release=type==QEvent::TabletRelease;
            QTabletEvent e(type,&pen,p,canvas.mapToGlobal(p),release?0:pressure,tilt,tilt/2,0,0,0,Qt::NoModifier,
                type==QEvent::TabletMove?Qt::NoButton:Qt::LeftButton,release?Qt::NoButton:Qt::LeftButton);
            QApplication::sendEvent(&canvas,&e);QApplication::processEvents();QVERIFY(!canvas.grab().isNull());
        };
        send(QEvent::TabletPress,{200,300});send(QEvent::TabletMove,{202,301});
        const auto builds=canvas.brushCursorRasterBuildCount(),outlines=canvas.brushOutlineBuildCount();
        for(int i=2;i<=30;++i)send(QEvent::TabletMove,{200+i*2.13,300+i*.19},.2+i*.02,i);
        QCOMPARE(canvas.document()->brushCursorIndex(),quint64(0));
        QCOMPARE(canvas.brushCursorRasterBuildCount(),builds);QCOMPARE(canvas.brushOutlineBuildCount(),outlines);
        send(QEvent::TabletMove,{1350,300});QVERIFY(canvas.document()->brushCursorIndex()>0);
        QVERIFY(canvas.brushOutlineBuildCount()>outlines);
        send(QEvent::TabletRelease,{1350,300});QVERIFY(!canvas.document()->strokeActive());
        QVERIFY(!canvas.shot().layers.first().tiles.isEmpty());
        brush.pressureSize=true;canvas.applyBrushSettings(brush,0);
        send(QEvent::TabletPress,{200,300},.2);const auto small=canvas.brushCursorRect().width();
        const auto pressureBuilds=canvas.brushOutlineBuildCount();
        send(QEvent::TabletMove,{205,301},.8);
        QVERIFY(canvas.brushCursorRect().width()>small);QVERIFY(canvas.brushOutlineBuildCount()>pressureBuilds);
        send(QEvent::TabletRelease,{205,301});
    }
    void detailedCursorRasterCacheTracksViewAndClipping() {
        Canvas canvas; canvas.resize(640, 480);
        auto project = Project::create(); project.width = project.height = 512;
        canvas.setProject(project); canvas.show(); canvas.setZoom(1);
        BrushSettings brush; brush.size = 128; brush.pressureSize = false;
        brush.tipMask = QImage(256, 256, QImage::Format_Grayscale8); brush.tipMask.fill(0);
        for (int y = 24; y < 232; ++y) for (int x = 4; x < 252; ++x)
            if (x < 170 || (x >= 180 && x % 8 < 3 && y % 8 < 3)) brush.tipMask.scanLine(y)[x] = 255;
        canvas.setBrushSettings(brush); hover(canvas, {320, 240});
        QVERIFY(canvas.brushCursorPath().elementCount() > 256);
        const auto first = canvas.grab().toImage();
        auto builds = canvas.brushCursorRasterBuildCount();
        QVERIFY(builds > 0); QVERIFY(canvas.brushCursorRasterBytes() > 0);
        QVERIFY(canvas.brushCursorRasterBytes() <= 4 * 1024 * 1024 * 4);
        QCOMPARE(canvas.grab().toImage(), first); QCOMPARE(canvas.brushCursorRasterBuildCount(), builds);
        // Integer and fractional movement reuse coverage while the displayed
        // cursor retains its exact position; geometry changes rebuild it.
        const qreal ratio = canvas.devicePixelRatioF();
        QPointF point = QPointF(320, 240) + QPointF(14, 8) / ratio;
        hover(canvas, point); QVERIFY(!canvas.grab().isNull());
        QCOMPARE(canvas.brushCursorRasterBuildCount(), builds);
        point += QPointF(.25, .25) / ratio;
        hover(canvas, point); QVERIFY(!canvas.grab().isNull());
        QCOMPARE(canvas.brushCursorRasterBuildCount(), builds);
        const auto fractionalPath=canvas.brushCursorPath();
        for(int i=0;i<30;++i){point+=QPointF(.13,.19)/ratio;hover(canvas,point);QVERIFY(!canvas.grab().isNull());}
        QCOMPARE(canvas.brushCursorRasterBuildCount(), builds);
        QVERIFY(canvas.brushCursorPath()!=fractionalPath);
        for (int change = 0; change < 3; ++change) {
            if (change == 0) canvas.setZoom(1.4);
            if (change == 1) canvas.setRotation(23);
            if (change == 2) canvas.setBrushSize(144);
            const auto changed = canvas.grab().toImage();
            QVERIFY(canvas.brushCursorRasterBuildCount() > builds);
            // An independent rasterization after discarding the cache must
            // produce the same image as the normal invalidation path.
            canvas.setTool(Canvas::Tool::Hand); QVERIFY(!canvas.grab().isNull());
            QCOMPARE(canvas.brushCursorRasterBytes(), 0);
            canvas.setTool(Canvas::Tool::Brush); hover(canvas, point);
            QCOMPARE(canvas.grab().toImage(), changed);
            builds = canvas.brushCursorRasterBuildCount();
        }
        canvas.setRotation(0); canvas.setZoom(2);
        point = {3, 240}; hover(canvas, point);
        const auto clipped = canvas.grab().toImage();
        const auto clippedPath = canvas.brushCursorPath();
        QVERIFY(clippedPath.contains({0, point.y()}));
        QVERIFY(canvas.brushCursorRasterBytes() > 0);
        QVERIFY(canvas.brushCursorRasterBytes() <= 4 * 1024 * 1024 * 4);
        canvas.setTool(Canvas::Tool::Hand); const auto clean = canvas.grab().toImage();
        // Filled coverage continuing off-screen must not create a false
        // vertical border where the viewport clips the raster mask.
        const QRect edge = QRectF(0, (point.y() - 8) * ratio, 2 * ratio, 16 * ratio).toAlignedRect();
        QCOMPARE(clipped.copy(edge), clean.copy(edge));
        canvas.setTool(Canvas::Tool::Brush); hover(canvas, point);
        QCOMPARE(canvas.grab().toImage(), clipped); QCOMPARE(canvas.brushCursorPath(), clippedPath);
        QVERIFY(saveCursorPreview(clipped, "large-cursor-clipped.png"));
        const auto movie = renderMovieFrame(canvas.project());
        const auto history = canvas.document()->history()->count();
        canvas.resize(540, 400); hover(canvas, {3, 200});
        const auto resized = canvas.grab().toImage();
        canvas.setTool(Canvas::Tool::Hand); QVERIFY(!canvas.grab().isNull());
        canvas.setTool(Canvas::Tool::Brush); hover(canvas, {3, 200});
        QCOMPARE(canvas.grab().toImage(), resized);
        QCOMPARE(renderMovieFrame(canvas.project()), movie);
        QCOMPARE(canvas.document()->history()->count(), history);
        QEvent leave(QEvent::Leave); QApplication::sendEvent(&canvas, &leave);
        QVERIFY(!canvas.grab().isNull()); QCOMPARE(canvas.brushCursorRasterBytes(), 0);
    }
    void oversizedCursorFallsBackWithoutChangingArtwork() {
        Canvas canvas;
        auto project = Project::create(); project.width = project.height = 256;
        canvas.setProject(project);
        // A hidden canvas avoids changing the native screen layout. Keep the
        // capture near 4.8 million physical pixels at both 1x and 2x, just over
        // the overlay budget rather than allocating a giant Retina viewport.
        canvas.winId();
        const qreal ratio = canvas.devicePixelRatioF();
        canvas.resize(int(std::ceil(2200 / ratio)), int(std::ceil(2200 / ratio)));
        canvas.setZoom(16);
        BrushSettings brush; brush.size = 256; brush.pressureSize = false;
        brush.tipMask = QImage(64, 64, QImage::Format_Grayscale8); brush.tipMask.fill(255);
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x)
            if (x % 8 >= 2 && x % 8 < 6 && y % 8 >= 2 && y % 8 < 6)
                brush.tipMask.scanLine(y)[x] = 0;
        canvas.setBrushSettings(brush);
        const QPointF center = QRectF(canvas.rect()).center(); hover(canvas, center);
        const auto path = canvas.brushCursorPath();
        QVERIFY(path.elementCount() > 256);
        const auto movie = renderMovieFrame(canvas.project());
        const auto document = canvas.project().toJson();
        const auto history = canvas.document()->history()->count();
        canvas.setTool(Canvas::Tool::Hand); const auto clean = canvas.grab().toImage();
        canvas.setTool(Canvas::Tool::Brush); hover(canvas, center);
        const auto outlined = canvas.grab().toImage();
        QVERIFY(!outlined.isNull()); QCOMPARE(outlined.size(), clean.size());
        QVERIFY(qint64(outlined.width()) * outlined.height() > 4 * 1024 * 1024);
        QCOMPARE(canvas.brushCursorRasterBytes(), 0);
        QCOMPARE(canvas.brushCursorRasterBuildCount(), quint64(0));
        QVERIFY(outlined != clean); // The direct-edge fallback still shows a cursor.
        QCOMPARE(canvas.brushCursorPath(), path);
        QCOMPARE(renderMovieFrame(canvas.project()), movie);
        QCOMPARE(canvas.project().toJson(), document);
        QCOMPARE(canvas.document()->history()->count(), history);
        QVERIFY(canvas.document()->history()->isClean());
        canvas.setTool(Canvas::Tool::Hand); QCOMPARE(canvas.grab().toImage(), clean);
    }
    void staticSampledOutlineIsReusedWhileDrawing() {
        Canvas canvas; canvas.resize(1000, 800); canvas.setProject(Project::create()); canvas.show(); canvas.setZoom(.5);
        auto brush = canvas.brush; brush.size = 100; brush.pressureSize = false;
        brush.tipMask = QImage(32, 32, QImage::Format_Grayscale8); brush.tipMask.fill(0);
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x)
            if (x < 10 || y > 20) brush.tipMask.scanLine(y)[x] = 255;
        canvas.setBrushSettings(brush);
        hover(canvas, {500, 400});
        const auto initial = canvas.brushCursorPath();
        const auto builds = canvas.brushOutlineBuildCount();
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {500, 400});
        for (const QPoint point : {QPoint(530, 420), QPoint(550, 390), QPoint(480, 370)}) {
            QTest::mouseMove(&canvas, point);
            const auto path = canvas.brushCursorPath();
            QVERIFY(!path.isEmpty());
            QCOMPARE(path.boundingRect().size(), initial.boundingRect().size());
            QCOMPARE(canvas.brushOutlineBuildCount(), builds);
        }
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, {480, 370});
        QCOMPARE(canvas.brushOutlineBuildCount(), builds);
        brush.angleDynamics.control = BrushControl::Direction;
        canvas.setBrushSettings(brush);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {500, 400});
        QTest::mouseMove(&canvas, {540, 400}); canvas.brushCursorPath();
        const auto directionalBuilds = canvas.brushOutlineBuildCount();
        QTest::mouseMove(&canvas, {540, 440}); canvas.brushCursorPath();
        QVERIFY(canvas.brushOutlineBuildCount() > directionalBuilds);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, {540, 440});
    }
    void variedPenInputDefersSampledOutlineUntilDisplay_data() {
        QTest::addColumn<bool>("eraser");
        QTest::newRow("textured-particle-brush") << false;
        QTest::newRow("textured-particle-eraser") << true;
    }
    void variedPenInputDefersSampledOutlineUntilDisplay() {
        QFETCH(bool, eraser);
        auto project = Project::create(); project.width = project.height = 256; project.transparent = true;
        if (eraser) {
            QImage tile(256, 256, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::black);
            project.shots[0].layers[0].tiles[{0, 0}] = tile;
        }
        BrushSettings brush; brush.size = 48; brush.spacing = .18; brush.lockRandomSeed = true; brush.randomSeed = 17;
        brush.noise = true; brush.scatter = .25; brush.count = 2;
        brush.tipMask = QImage(24, 24, QImage::Format_Grayscale8); brush.tipMask.fill(0);
        for (int y = 2; y < 22; ++y) for (int x = 2; x < 22; ++x)
            if (x < 9 || y > 15) brush.tipMask.scanLine(y)[x] = 255;
        brush.texture.enabled = true; brush.texture.eachTip = true; brush.texture.depth = .5;
        brush.texture.pattern = QImage(4, 4, QImage::Format_Grayscale8);
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x)
            brush.texture.pattern.scanLine(y)[x] = (x + y) % 2 ? 255 : 100;
        Canvas deferred, reference;
        for (auto *canvas : {&deferred, &reference}) {
            canvas->resize(640, 480); canvas->setProject(project); canvas->setZoom(.9); canvas->setRotation(17);
            canvas->setTool(eraser ? Canvas::Tool::Eraser : Canvas::Tool::Brush);
            canvas->setBrushSettings(brush); canvas->setStabilization(0);
        }
        QPointingDevice pen("Dynamic outline pen", 73, QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
        const auto send = [&](Canvas &canvas, QEvent::Type type, int sample) {
            const qreal angle = sample * 2 * 3.141592653589793 / 48;
            const auto artwork = QPointF(128, 128) + QPointF(60 * std::cos(angle), 60 * std::sin(angle));
            const auto position = canvas.viewTransform().map(sheetGeometry(project, project.shots[0]).output.topLeft() + artwork);
            QTabletEvent event(type, &pen, position, canvas.mapToGlobal(position),
                type == QEvent::TabletRelease ? 0 : .2 + .7 * sample / 48, 0, 0, 0, 0, 0, Qt::NoModifier,
                type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton,
                type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton);
            QApplication::sendEvent(&canvas, &event);
        };
        const auto builds = deferred.brushOutlineBuildCount();
        for (int sample = 0; sample <= 48; ++sample) {
            const auto type = sample ? QEvent::TabletMove : QEvent::TabletPress;
            send(deferred, type, sample); send(reference, type, sample);
            QVERIFY(!reference.brushCursorPath().isEmpty());
        }
        QVERIFY(deferred.document()->strokeActive());
        QCOMPARE(deferred.brushOutlineBuildCount(), builds);
        QVERIFY(reference.brushOutlineBuildCount() > 24);
        const auto expected = reference.brushCursorPath();
        QCOMPARE(deferred.brushCursorPath(), expected);
        QCOMPARE(deferred.brushOutlineBuildCount(), builds + 1);
        QCOMPARE(deferred.brushCursorRect(), expected.boundingRect());
        QCOMPARE(deferred.brushOutlineBuildCount(), builds + 1);
        QCOMPARE(renderMovieFrame(deferred.project()), renderMovieFrame(reference.project()));
        const auto beforePaint = renderMovieFrame(deferred.project());
        QVERIFY(!deferred.grab().isNull());
        QCOMPARE(deferred.brushOutlineBuildCount(), builds + 1);
        QCOMPARE(renderMovieFrame(deferred.project()), beforePaint);
        send(deferred, QEvent::TabletRelease, 48); send(reference, QEvent::TabletRelease, 48);
        QCOMPARE(deferred.document()->history()->count(), 1);
        QCOMPARE(renderMovieFrame(deferred.project()), renderMovieFrame(reference.project()));
    }
    void textureLockAndPaletteSurvivePresetSelection() {
        Canvas canvas;
        auto first = canvas.brush; first.color = QColor("#a331cc"); first.backgroundColor = QColor("#dd6633");
        first.texture.enabled = true; first.texture.name = "Original";
        first.texture.pattern = QImage(4, 4, QImage::Format_Grayscale8); first.texture.pattern.fill(128);
        first.protectTexture = true; canvas.setBrushSettings(first);
        BrushSettings next; next.texture.name = "Replacement"; next.texture.pattern = QImage(2, 2, QImage::Format_Grayscale8);
        next.texture.pattern.fill(255); next.texture.enabled = true; next.size = 45;
        canvas.applyBrushSettings(next, 55);
        QCOMPARE(canvas.brush.texture, first.texture); QCOMPARE(canvas.brush.color, first.color);
        QCOMPARE(canvas.brush.backgroundColor, first.backgroundColor); QCOMPARE(canvas.brush.size, 45);
        auto unlocked = canvas.brush; unlocked.protectTexture = false; canvas.setBrushSettings(unlocked);
        canvas.applyBrushSettings(next, 0); QCOMPARE(canvas.brush.texture, next.texture);
    }
    void cursorFollowsFadeAndCanvasAnchoredTexture() {
        Canvas canvas; canvas.resize(1100, 850); auto project = Project::create(); canvas.setProject(project);
        canvas.show(); canvas.setZoom(.75);
        auto brush = canvas.brush; brush.size = 60; brush.pressureSize = false; brush.spacing = .5;
        brush.sizeDynamics.control = BrushControl::Fade; brush.sizeDynamics.fade = 8;
        canvas.setBrushSettings(brush);
        auto screen = [&](QPointF p) { return canvas.viewTransform().map(sheetGeometry(project, project.shots.first()).output.topLeft() + p); };
        const auto start = screen({800, 400}), end = screen({920, 400});
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, start.toPoint());
        const auto initial = canvas.brushCursorRect().width();
        QTest::mouseMove(&canvas, end.toPoint());
        QVERIFY(canvas.brushCursorRect().width() < initial * .7);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, end.toPoint());
        brush = BrushSettings{}; brush.size = 100; brush.pressureSize = false;
        brush.tipMask = QImage(8, 8, QImage::Format_Grayscale8); brush.tipMask.fill(255);
        brush.texture.enabled = true; brush.texture.scale = 5; brush.texture.pattern = QImage(8, 1, QImage::Format_Grayscale8);
        for (int x = 0; x < 8; ++x) brush.texture.pattern.scanLine(0)[x] = x < 4 ? 0 : 255;
        canvas.setBrushSettings(brush);
        hover(canvas, screen({800, 400}));
        const bool coveredBefore = canvas.brushCursorPath().contains(screen({827, 400}));
        const bool clearBefore = canvas.brushCursorPath().contains(screen({807, 400}));
        QVERIFY(coveredBefore); QVERIFY(!clearBefore);
        hover(canvas, screen({810, 400}));
        QCOMPARE(canvas.brushCursorPath().contains(screen({827, 400})), coveredBefore);
        QCOMPARE(canvas.brushCursorPath().contains(screen({807, 400})), clearBefore);
    }
    void stationaryAirbrushStopsAndCancelsAsOneGesture() {
        Canvas canvas; canvas.resize(1000, 800); auto project = Project::create(); project.transparent = true;
        canvas.setProject(project); canvas.show(); canvas.setZoom(.5);
        auto brush = canvas.brush; brush.size = 40; brush.pressureSize = false;
        brush.opacity = .03; brush.strokeOpacity = .5; brush.airbrush = true; brush.airbrushRate = 60;
        canvas.setBrushSettings(brush);
        const QPointF art(800, 400);
        const auto screen = canvas.viewTransform().map(sheetGeometry(project, project.shots.first()).output.topLeft() + art).toPoint();
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, screen);
        const int initial = renderMovieFrame(canvas.project()).pixelColor(art.toPoint()).alpha();
        QTest::qWait(100);
        const int accumulated = renderMovieFrame(canvas.project()).pixelColor(art.toPoint()).alpha();
        QVERIFY(accumulated > initial); QVERIFY(accumulated <= 128);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, screen);
        QCOMPARE(canvas.document()->history()->count(), 1);
        const auto released = renderMovieFrame(canvas.project()); QTest::qWait(70);
        QCOMPARE(renderMovieFrame(canvas.project()), released);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, screen); QTest::qWait(40);
        QTest::keyClick(&canvas, Qt::Key_Escape); QTest::qWait(60);
        QCOMPARE(renderMovieFrame(canvas.project()), released);
        QCOMPARE(canvas.document()->history()->count(), 1);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, screen);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, screen); QTest::qWait(30);
        QFocusEvent focus(QEvent::FocusOut); QApplication::sendEvent(&canvas, &focus);
        QVERIFY(!canvas.document()->strokeActive()); const auto unfocused = renderMovieFrame(canvas.project());
        QTest::qWait(60); QCOMPARE(renderMovieFrame(canvas.project()), unfocused);
    }
};
QTEST_MAIN(CursorTest)
#include "test_cursor.moc"
