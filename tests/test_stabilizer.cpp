#include "canvas.h"
#include "stroke_filter.h"
#include <QApplication>
#include <QFocusEvent>
#include <QLineF>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QUuid>
#include <QtTest>
#include <cmath>
#include <limits>

using namespace toon;
static QPointF screenPoint(const Canvas &canvas, QPointF artwork) {
    return canvas.artworkViewTransform().map(artwork);
}
static void mouse(Canvas &canvas, QEvent::Type type, QPointF artwork) {
    const auto position = screenPoint(canvas, artwork);
    QMouseEvent event(type, position, canvas.mapToGlobal(position),
        type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
        type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &event);
}
static Project drawingProject() {
    auto project = Project::create(); project.transparent = true; return project;
}
namespace {
struct FilterTrace { QVector<StrokeSample> live,tail; bool failed=false; };
FilterTrace filterTrace(const QVector<StrokeSample> &path,StrokeFilterSettings settings,int divisions=1) {
    StrokeFilter filter;FilterTrace trace;trace.live.append(filter.begin(path.first().point,path.first().pressure,settings));
    for(int i=1;i<path.size();++i)for(int j=1;j<=divisions;++j) {
        const qreal fraction=qreal(j)/divisions;
        trace.live+=filter.advance(path[i-1].point+(path[i].point-path[i-1].point)*fraction,
            path[i-1].pressure+(path[i].pressure-path[i-1].pressure)*fraction);
    }
    trace.tail=filter.finish(path.last().point,path.last().pressure);trace.failed=filter.failed();return trace;
}
QVector<StrokeSample> noisyLine(qreal step=1,int count=700) {
    QVector<StrokeSample> path{{{0,0},.7}};
    for(int i=1;i<=count;++i)path.append({{i*step,3.5*std::sin(i*.45)},.7});return path;
}
qreal rmsY(const QVector<StrokeSample> &samples,qreal after=0) {
    qreal square=0;int count=0;for(const auto &sample:samples)if(sample.point.x()>=after){square+=sample.point.y()*sample.point.y();++count;}
    return count?std::sqrt(square/count):0;
}
QJsonArray jsonSamples(const QVector<StrokeSample> &samples) {
    QJsonArray result;for(const auto &sample:samples)result.append(QJsonArray{sample.point.x(),sample.point.y(),sample.pressure});return result;
}
}
class StabilizerTest : public QObject {
    Q_OBJECT
private slots:
    void disabledPassesInputExactly() {
        StrokeFilter filter;
        const auto start = filter.begin({10.3, 20.7}, .35, 0);
        QCOMPARE(start.point, QPointF(10.3, 20.7)); QCOMPARE(start.pressure, .35);
        const auto samples = filter.advance({11.23, 22.51}, .61);
        QCOMPARE(samples.size(), 1);
        QCOMPARE(samples.first().point, QPointF(11.23, 22.51)); QCOMPARE(samples.first().pressure, .61);
        const auto end = filter.finish({11.4, 22.7}, .7);
        QCOMPARE(end.size(), 1); QCOMPARE(end.first().point, QPointF(11.4, 22.7));
        QVERIFY(!filter.active());
    }
    void subdivisionDoesNotChangeGeometryOrPressure() {
        const QVector<StrokeSample> path{{{0, 0}, .2}, {{40, 0}, .8}, {{70, 40}, .6}, {{70, 90}, .3}};
        auto draw = [&](int divisions) {
            StrokeFilter filter; filter.begin(path.first().point, path.first().pressure, 65);
            QVector<StrokeSample> result;
            for (int i = 1; i < path.size(); ++i) {
                for (int j = 1; j <= divisions; ++j) {
                    const qreal part = qreal(j) / divisions;
                    result += filter.advance(path[i - 1].point + (path[i].point - path[i - 1].point) * part,
                        path[i - 1].pressure + (path[i].pressure - path[i - 1].pressure) * part);
                }
            }
            result += filter.finish(path.last().point, path.last().pressure);
            return result;
        };
        const auto coarse = draw(1), dense = draw(8);
        QCOMPARE(coarse.size(), dense.size());
        for (int i = 0; i < coarse.size(); ++i) {
            QVERIFY(QLineF(coarse[i].point, dense[i].point).length() < 1e-8);
            QVERIFY(std::abs(coarse[i].pressure - dense[i].pressure) < 1e-8);
        }
    }
    void reducesJitterAndClosesBoundedGap() {
        StrokeFilter filter; filter.begin({0, 0}, .8, 100);
        qreal totalSquared = 0; int count = 0;
        for (int i = 1; i <= 100; ++i) {
            const QPointF raw(i * 2, (i % 2 ? 3 : -3));
            const auto samples = filter.advance(raw, .8);
            for (const auto &sample : samples) {
                if (sample.point.x() > 30) { totalSquared += sample.point.y() * sample.point.y(); ++count; }
                QVERIFY(sample.pressure > .79999 && sample.pressure < .80001);
            }
            QVERIFY(QLineF(filter.tip().point, raw).length() <= 25.0001);
        }
        QVERIFY(count > 100);
        QVERIFY(std::sqrt(totalSquared / count) < 1);
        const auto finish = filter.finish({200, -3}, .8);
        QVERIFY(!finish.isEmpty()); QCOMPARE(finish.last().point, QPointF(200, -3));
        QVERIFY(!filter.active()); QVERIFY(filter.advance({300, 100}, 1).isEmpty());
        QCOMPARE(filter.begin({800, 400}, .25, 100).point, QPointF(800, 400));
        QCOMPARE(filter.tip().pressure, .25);
    }
    void mouseEndpointUndoCancelAndNewStroke() {
        Canvas canvas; canvas.resize(1200, 900); canvas.setProject(drawingProject()); canvas.show();
        canvas.setZoom(.8); canvas.setStabilization(100); canvas.setBrushSize(8);
        mouse(canvas, QEvent::MouseButtonPress, {800, 400});
        mouse(canvas, QEvent::MouseMove, {900, 400});
        const auto rawScreen = screenPoint(canvas, {900, 400});
        QVERIFY(canvas.brushCursorRect().center().x() < rawScreen.x() - 10);
        mouse(canvas, QEvent::MouseButtonRelease, {920, 400});
        QCOMPARE(canvas.document()->history()->count(), 1);
        auto picture = renderMovieFrame(canvas.project());
        QVERIFY(picture.pixelColor(919, 400).alpha() > 240);
        canvas.document()->history()->undo();
        QCOMPARE(renderMovieFrame(canvas.project()).pixelColor(850, 400).alpha(), 0);
        canvas.document()->history()->redo();
        mouse(canvas, QEvent::MouseButtonPress, {800, 500});
        mouse(canvas, QEvent::MouseMove, {900, 500});
        QTest::keyClick(&canvas, Qt::Key_Escape);
        mouse(canvas, QEvent::MouseButtonRelease, {920, 500});
        QCOMPARE(canvas.document()->history()->count(), 1);
        QCOMPARE(renderMovieFrame(canvas.project()), picture);
        mouse(canvas, QEvent::MouseButtonPress, {1100, 550});
        mouse(canvas, QEvent::MouseButtonRelease, {1100, 550});
        QCOMPARE(canvas.document()->history()->count(), 2);
        picture = renderMovieFrame(canvas.project());
        QVERIFY(picture.pixelColor(1100, 550).alpha() > 240);
        QCOMPARE(picture.pixelColor(1000, 480).alpha(), 0);
        canvas.setTool(Canvas::Tool::Eraser);
        mouse(canvas, QEvent::MouseButtonPress, {810, 400});
        mouse(canvas, QEvent::MouseMove, {900, 400});
        canvas.finishStroke();
        QCOMPARE(renderMovieFrame(canvas.project()).pixelColor(899, 400).alpha(), 0);
        QCOMPARE(canvas.document()->history()->count(), 3);
    }
    void tabletPressureAndViewIndependentCursor() {
        Canvas canvas; canvas.resize(1200, 900); canvas.setProject(drawingProject()); canvas.show();
        canvas.setZoom(.85); canvas.setRotation(33); canvas.setBrushSize(40); canvas.setStabilization(70);
        QPointingDevice pen("Test pen", 12, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
        auto send = [&](QEvent::Type type, QPointF point, qreal pressure) {
            const auto position = screenPoint(canvas, point);
            QTabletEvent event(type, &pen, position, canvas.mapToGlobal(position), pressure, 0, 0, 0, 0, 0,
                Qt::NoModifier, type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton,
                type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton);
            QApplication::sendEvent(&canvas, &event);
        };
        StrokeFilter reference;StrokeFilterSettings settings;settings.strength=70;settings.pixelScale=1/.85;
        reference.begin({800, 450}, .2, settings);
        send(QEvent::TabletPress, {800, 450}, .2);
        reference.advance({900, 470}, .8);
        send(QEvent::TabletMove, {900, 470}, .8);
        QVERIFY(QLineF(canvas.brushCursorRect().center(), screenPoint(canvas, reference.tip().point)).length() < 1e-7);
        QVERIFY(std::abs(canvas.brushCursorRect().width() - 40 * .85 * reference.tip().pressure) < 1e-7);
        send(QEvent::TabletRelease, {910, 470}, 0);
        QVERIFY(renderMovieFrame(canvas.project()).pixelColor(909, 470).alpha() > 240);
        QCOMPARE(canvas.document()->history()->count(), 1);
    }
    void finishingOnToolChangeAndFocusLoss() {
        Canvas canvas; canvas.resize(1200, 900); canvas.setProject(drawingProject()); canvas.show();
        canvas.setZoom(.8); canvas.setStabilization(100); canvas.setBrushSize(10);
        mouse(canvas, QEvent::MouseButtonPress, {800, 400}); mouse(canvas, QEvent::MouseMove, {900, 400});
        canvas.setTool(Canvas::Tool::Hand);
        QVERIFY(!canvas.document()->strokeActive());
        QVERIFY(renderMovieFrame(canvas.project()).pixelColor(899, 400).alpha() > 240);
        canvas.setTool(Canvas::Tool::Brush);
        mouse(canvas, QEvent::MouseButtonPress, {800, 500}); mouse(canvas, QEvent::MouseMove, {900, 500});
        QFocusEvent focus(QEvent::FocusOut); QApplication::sendEvent(&canvas, &focus);
        QVERIFY(!canvas.document()->strokeActive());
        QVERIFY(renderMovieFrame(canvas.project()).pixelColor(899, 500).alpha() > 240);
        QCOMPARE(canvas.document()->history()->count(), 2);
    }
    void configuredModesAttenuateSlowAndFastTremor_data() {
        QTest::addColumn<int>("mode");QTest::addColumn<double>("step");
        for(int mode=0;mode<2;++mode)for(qreal step:{.65,4.})
            QTest::newRow(qPrintable(QString("%1-%2").arg(mode).arg(step)))<<mode<<step;
    }
    void configuredModesAttenuateSlowAndFastTremor() {
        QFETCH(int,mode);QFETCH(double,step);StrokeFilterSettings settings;
        settings.mode=StrokeFilterMode(mode);settings.strength=100;settings.deadZone=mode?1.2:0;
        const auto raw=noisyLine(step);const auto trace=filterTrace(raw,settings);QVERIFY(!trace.failed);
        const qreal input=rmsY(raw,step*250),live=rmsY(trace.live,step*250);
        QVERIFY2(live<input*(mode?.30:.60),qPrintable(QString("input RMS %1, live RMS %2").arg(input).arg(live)));
        QVERIFY(!trace.tail.isEmpty());QVERIFY(rmsY(trace.tail)<input*.75);
        QCOMPARE(trace.tail.last().point,raw.last().point);
        auto previous=trace.live.last().point;
        for(const auto &sample:trace.tail){QVERIFY(QLineF(previous,sample.point).length()<=1.000001);previous=sample.point;QVERIFY(sample.pressure>=0&&sample.pressure<=1);}
    }
    void configuredSubdivisionRetainsGeometryPressureAndTail_data() {
        QTest::addColumn<int>("mode");QTest::newRow("smooth")<<0;QTest::newRow("pulled-string")<<1;
    }
    void configuredSubdivisionRetainsGeometryPressureAndTail() {
        QFETCH(int,mode);StrokeFilterSettings settings;settings.mode=StrokeFilterMode(mode);settings.strength=70;
        settings.pixelScale=.7;settings.deadZone=2;
        const QVector<StrokeSample> path{{{0,0},.2},{{70,0},.8},{{100,40},.6},{{100,130},.3}};
        const auto coarse=filterTrace(path,settings,1),dense=filterTrace(path,settings,13);
        QVERIFY(!coarse.failed&&!dense.failed);const auto a=coarse.live+coarse.tail,b=dense.live+dense.tail;
        QCOMPARE(a.size(),b.size());
        for(int i=0;i<a.size();++i){QVERIFY(QLineF(a[i].point,b[i].point).length()<1e-7);QVERIFY(qAbs(a[i].pressure-b[i].pressure)<1e-7);}
    }
    void stationaryNoiseCannotWalkTheStableTip() {
        StrokeFilterSettings settings;settings.mode=StrokeFilterMode::Stabilizer;settings.strength=100;settings.deadZone=2;
        StrokeFilter filter;filter.begin({40,30},.6,settings);
        for(int i=1;i<=2000;++i) {
            const auto samples=filter.advance({40+1.1*std::sin(i*.7),30+.8*std::cos(i*.9)},.6);
            QVERIFY(samples.isEmpty());QCOMPARE(filter.tip().point,QPointF(40,30));
        }
        QVERIFY(filter.finish({40.5,30.4},.6).isEmpty());QCOMPARE(filter.tip().point,QPointF(40,30));
        QVERIFY(!filter.failed());QVERIFY(!filter.active());
    }
    void completionFollowsThePendingCornerInsteadOfAnEndpointChord() {
        StrokeFilterSettings settings;settings.mode=StrokeFilterMode::Stabilizer;settings.strength=100;
        const QVector<StrokeSample> path{{{0,0},.7},{{200,0},.7},{{200,20},.7}};
        const auto trace=filterTrace(path,settings);QVERIFY(!trace.failed);QVERIFY(!trace.tail.isEmpty());
        qreal cornerDistance=1000;auto previous=trace.live.last().point;
        for(const auto &sample:trace.tail){cornerDistance=qMin(cornerDistance,QLineF(sample.point,{200,0}).length());QVERIFY(QLineF(previous,sample.point).length()<=1.000001);previous=sample.point;}
        QVERIFY2(cornerDistance<10,qPrintable(QString("tail missed corner by %1 pixels").arg(cornerDistance)));
        QCOMPARE(trace.tail.last().point,QPointF(200,20));
        settings.finishAtEndpoint=false;const auto stopped=filterTrace(path,settings);
        QVERIFY(!stopped.failed);QVERIFY(stopped.tail.isEmpty());
        QVERIFY(QLineF(stopped.live.last().point,path.last().point).length()>50);
    }
    void repeatedReleaseSampleCannotJumpTheFilteredPressure() {
        for(const auto mode:{StrokeFilterMode::Smooth,StrokeFilterMode::Stabilizer}) {
            StrokeFilterSettings settings;settings.mode=mode;settings.strength=100;
            StrokeFilter filter;filter.begin({0,0},.1,settings);const auto live=filter.advance({300,0},.9);
            QVERIFY(!live.isEmpty());const auto tip=filter.tip();
            QVERIFY(tip.pressure<.9);QVERIFY(filter.advance({300,0},.9).isEmpty());QCOMPARE(filter.tip().pressure,tip.pressure);
            const auto tail=filter.finish({300,0},.9);QVERIFY(!tail.isEmpty());QVERIFY(!filter.failed());
            QVERIFY(qAbs(tail.first().pressure-tip.pressure)<.03);QVERIFY(qAbs(tail.last().pressure-.9)<1e-9);
        }
    }
    void intentionalLoopRetainsItsOrderAndBoundedCompletion() {
        QVector<StrokeSample> path;constexpr qreal radius=240;
        for(int i=0;i<=600;++i){const qreal angle=2*M_PI*i/600;path.append({{radius*std::cos(angle),radius*std::sin(angle)},.8});}
        StrokeFilterSettings settings;settings.mode=StrokeFilterMode::Stabilizer;settings.strength=80;
        const auto trace=filterTrace(path,settings);QVERIFY(!trace.failed);QVERIFY(trace.live.size()>500);QVERIFY(!trace.tail.isEmpty());
        QRectF bounds(trace.live.first().point,QSizeF());for(const auto &sample:trace.live)bounds|=QRectF(sample.point,QSizeF(.001,.001));
        QVERIFY(bounds.width()>400&&bounds.height()>400);
        for(const auto &sample:trace.tail)QVERIFY(QLineF({},sample.point).length()>radius*.85);
        QVERIFY(QLineF(trace.tail.last().point,path.first().point).length()<1e-8);
    }
    void invalidInputAndExcessiveCompletionFailWithoutGeneratedTail() {
        StrokeFilterSettings settings;settings.mode=StrokeFilterMode::Stabilizer;settings.strength=100;
        StrokeFilter filter;filter.begin({0,0},.5,settings);
        QVERIFY(filter.advance({std::numeric_limits<qreal>::quiet_NaN(),0},.5).isEmpty());QVERIFY(filter.failed());QVERIFY(!filter.active());
        filter.begin({0,0},.5,settings);QVERIFY(filter.advance({50001,0},.5).isEmpty());QVERIFY(filter.failed());
        filter.begin({0,0},.5,settings);
        // Long repeated motion entirely inside the leash must not trigger an
        // unbounded history replay when lifting the pen.
        for(int i=1;i<=1500;++i)filter.advance({30*std::sin(i*.2),30*std::cos(i*.2)},.5);
        QVERIFY(filter.finish(filter.raw().point,.5).isEmpty());QVERIFY(filter.failed());QVERIFY(!filter.active());
        settings.strength=0;settings.deadZone=20;filter.begin({0,0},.5,settings);
        const auto direct=filter.advance({.2,.3},1.2);QCOMPARE(direct.size(),1);QCOMPARE(direct.first().point,QPointF(.2,.3));QCOMPARE(direct.first().pressure,1.);
        QVERIFY(!filter.failed());
    }
    void canvasUsesScreenSpaceSettingsAndLatchesThemAtContact() {
        QVector<QPointF> reference;
        for(qreal zoom:{.25,1.,2.}) {
            auto project=drawingProject();project.width=4096;project.height=2048;project.shots.first().camera2D.zoom=1.3;
            Canvas canvas;canvas.resize(1200,900);canvas.setProject(project);canvas.show();canvas.setZoom(zoom);canvas.setHoldShapeEnabled(false);
            canvas.setStabilizationMode(StrokeFilterMode::Stabilizer);canvas.setStabilization(80);canvas.setStabilizationDeadZone(1.5);
            canvas.setBrushSize(3);const QPointF start=canvas.artworkPoint(canvas.rect().center());const qreal scale=1/(zoom*1.3);
            mouse(canvas,QEvent::MouseButtonPress,start);QVector<QPointF> observed;
            for(int i=1;i<=180;++i) {
                mouse(canvas,QEvent::MouseMove,start+QPointF(i*2*scale,3*std::sin(i*.45)*scale));
                observed.append((canvas.brushCursorRect().center()-screenPoint(canvas,start)));
                if(i==90){canvas.setStabilization(0);canvas.setStabilizationMode(StrokeFilterMode::Smooth);canvas.setStabilizationDeadZone(0);}
            }
            if(reference.isEmpty())reference=observed;
            else for(int i=0;i<reference.size();++i)QVERIFY(QLineF(reference[i],observed[i]).length()<1e-6);
            mouse(canvas,QEvent::MouseButtonRelease,start+QPointF(360*scale,3*std::sin(180*.45)*scale));
            QCOMPARE(canvas.document()->history()->count(),1);
            auto brush=canvas.brush;brush.smoothingEnabled=false;canvas.setBrushSettings(brush);canvas.setStabilization(100);
            canvas.setStabilizationMode(StrokeFilterMode::Stabilizer);mouse(canvas,QEvent::MouseButtonPress,start);
            const auto raw=start+QPointF(30*scale,10*scale);mouse(canvas,QEvent::MouseMove,raw);
            QVERIFY(QLineF(canvas.brushCursorRect().center(),screenPoint(canvas,raw)).length()<1e-6);
            mouse(canvas,QEvent::MouseButtonRelease,raw);
        }
    }
    void canvasRejectsAnExcessiveFinishTailAsOneCancelledStroke() {
        Canvas canvas;canvas.resize(1200,900);canvas.setProject(drawingProject());canvas.show();canvas.setZoom(1);
        canvas.setHoldShapeEnabled(false);canvas.setStabilizationMode(StrokeFilterMode::Stabilizer);
        canvas.setStabilization(100);canvas.setStabilizationDeadZone(0);canvas.setStabilizationFinishAtEndpoint(true);canvas.setBrushSize(3);
        const auto before=canvas.project().toJson();const auto tiles=canvas.shot().layers[canvas.document()->activeLayer()].tiles;
        QSignalSpy warnings(canvas.document(),&Document::warning);const QPointF center(800,500);
        mouse(canvas,QEvent::MouseButtonPress,center);
        QPointF last=center;
        for(int i=1;i<=1500;++i){last=center+QPointF(30*std::sin(i*.2),30*std::cos(i*.2));mouse(canvas,QEvent::MouseMove,last);}
        QVERIFY(canvas.document()->strokeActive());mouse(canvas,QEvent::MouseButtonRelease,last);
        QVERIFY(!canvas.document()->strokeActive());QCOMPARE(canvas.document()->history()->count(),0);
        QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.shot().layers[canvas.document()->activeLayer()].tiles,tiles);
        QCOMPARE(warnings.size(),1);QVERIFY(warnings.first().first().toString().contains("stabil",Qt::CaseInsensitive));
    }
    void invalidPressureDuringGuideDirectionChoiceCancelsAllArtwork() {
        auto project=drawingProject();DrawingGuide guide;guide.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        guide.name="Direction choice";guide.type=GuideType::ParallelRuler;guide.points={{0,0},{1920,0}};
        project.shots.first().guides.append(guide);project.shots.first().guideSettings.snapping=true;
        Canvas canvas;canvas.resize(1200,900);canvas.setProject(project);canvas.show();canvas.setZoom(1);
        canvas.setHoldShapeEnabled(false);canvas.setStabilizationMode(StrokeFilterMode::Stabilizer);canvas.setStabilization(100);
        const auto before=canvas.project().toJson();const auto tiles=canvas.shot().layers.first().tiles;
        QPointingDevice pen("Invalid guide pressure pen",905,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
        auto send=[&](QEvent::Type type,QPointF point,qreal pressure) {
            const auto position=screenPoint(canvas,point);const bool release=type==QEvent::TabletRelease;
            QTabletEvent event(type,&pen,position,canvas.mapToGlobal(position),pressure,0,0,0,0,0,Qt::NoModifier,
                type==QEvent::TabletMove?Qt::NoButton:Qt::LeftButton,release?Qt::NoButton:Qt::LeftButton);
            QApplication::sendEvent(&canvas,&event);
        };
        const QPointF start(800,500);send(QEvent::TabletPress,start,.7);QVERIFY(canvas.document()->strokeActive());
        // The ruler deliberately waits for three view pixels before choosing
        // its direction. These two moves stay inside that unresolved region.
        send(QEvent::TabletMove,start+QPointF(1,0),.7);QVERIFY(canvas.document()->strokeActive());
        send(QEvent::TabletMove,start+QPointF(1.5,0),std::numeric_limits<qreal>::quiet_NaN());
        QVERIFY(!canvas.document()->strokeActive());QCOMPARE(canvas.document()->history()->count(),0);
        send(QEvent::TabletRelease,start+QPointF(1.5,0),0);
        QVERIFY(!canvas.document()->strokeActive());QCOMPARE(canvas.document()->history()->count(),0);
        QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.shot().layers.first().tiles,tiles);
    }
    void optionalEngineReviewFixtures() {
        const auto directory=qEnvironmentVariable("TOON_STABILIZER_REVIEW");if(directory.isEmpty())return;
        QVERIFY(QDir().mkpath(directory));QJsonArray fixtures;
        auto capture=[&](const QString &name,const QVector<StrokeSample> &raw,StrokeFilterSettings settings) {
            const auto trace=filterTrace(raw,settings);QVERIFY(!trace.failed);
            fixtures.append(QJsonObject{{"name",name},{"settings",QJsonObject{{"mode",settings.mode==StrokeFilterMode::Smooth?"Smooth":"Stabilizer"},
                {"strength",settings.strength},{"pixelScale",settings.pixelScale},{"deadZone",settings.deadZone},{"finishAtEndpoint",settings.finishAtEndpoint}}},
                {"raw",jsonSamples(raw)},{"live",jsonSamples(trace.live)},{"tail",jsonSamples(trace.tail)},
                {"metrics",QJsonObject{{"rawRmsYAfter250",rmsY(raw,250)},{"liveRmsYAfter250",rmsY(trace.live,250)},{"tailRmsY",rmsY(trace.tail)}}}});
        };
        StrokeFilterSettings settings;settings.strength=100;
        capture("noisy-horizontal-smooth",noisyLine(),settings);settings.mode=StrokeFilterMode::Stabilizer;settings.deadZone=1.2;
        capture("noisy-horizontal-stabilizer",noisyLine(),settings);
        capture("corner-pending-tail",{{{0,0},.7},{{200,0},.7},{{200,20},.7}},settings);
        QVector<StrokeSample> loop;for(int i=0;i<=600;++i){const qreal angle=2*M_PI*i/600;loop.append({{240*std::cos(angle),240*std::sin(angle)},.8});}
        settings.strength=80;capture("intentional-loop",loop,settings);
        QVector<StrokeSample> rest{{{0,0},.7}};for(int i=1;i<=500;++i)rest.append({{1.1*std::sin(i*.7),.8*std::cos(i*.9)},.7});
        settings.strength=100;settings.deadZone=2;capture("stationary-tremor",rest,settings);
        QFile file(directory+"/stabilizer-engine-fixtures.json");QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write(QJsonDocument(QJsonObject{{"schema",1},{"source","actual StrokeFilter engine output"},{"fixtures",fixtures}}).toJson())>0);
    }
};
QTEST_MAIN(StabilizerTest)
#include "test_stabilizer.moc"
