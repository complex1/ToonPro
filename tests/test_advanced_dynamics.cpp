#include "advanced_brush_dynamics_settings.h"
#include "advanced_brush_dynamics_widget.h"
#include "brush_engine.h"
#include "brush_settings_panel.h"
#include "canvas.h"
#include "drawing_input.h"
#include "object_tools.h"
#include "vector_brush.h"
#include "vector_brush_storage.h"
#include "value_slider.h"
#include "ui_theme.h"
#include <QCheckBox>
#include <QElapsedTimer>
#include <QDialog>
#include <QJsonArray>
#include <QListWidget>
#include <QPointingDevice>
#include <QMouseEvent>
#include <QPainter>
#include <QTabletEvent>
#include <QTabBar>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
using namespace toon;
namespace {
Project fixture(bool vector=false) {
    auto p=Project::create();p.width=p.height=256;p.transparent=true;
    p.shots[0].sheet.overscan=0;p.shots[0].layers[0].kind=vector?LayerKind::Vector:LayerKind::Raster;return p;
}
QImage colored() {
    QImage image(48,24,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
    for(int y=2;y<22;++y)for(int x=2;x<46;++x) {
        if(x>18&&x<25&&y>7&&y<16)continue;
        image.setPixelColor(x,y,QColor(x<24?240:90,60,30,y<12?255:128));
    }return image;
}
void tablet(Canvas &canvas,const QPointingDevice &pen,QEvent::Type type,QPointF artwork,ulong time,bool queued) {
    const auto local=canvas.viewTransform().map(sheetGeometry(canvas.project(),canvas.shot()).output.topLeft()+artwork);
    auto *event=new QTabletEvent(type,&pen,local,canvas.mapToGlobal(local),type==QEvent::TabletRelease?0:.7,0,0,0,0,0,Qt::NoModifier,
        type==QEvent::TabletMove?Qt::NoButton:Qt::LeftButton,type==QEvent::TabletRelease?Qt::NoButton:Qt::LeftButton);
    event->setTimestamp(time);
    if(queued)QCoreApplication::postEvent(&canvas,event);else{QApplication::sendEvent(&canvas,event);delete event;}
}
}
class AdvancedDynamicsTest:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){configureDrawingInput();applyTheme();}
    void strictSettingsPreserveOnFailure() {
        BrushClusterSpray spray;spray.enabled=true;spray.radius=2.3;spray.angle=-41;spray.radialBias=.9;
        BrushClusterSpray parsed;QString error;QVERIFY(clusterSprayFromJson(clusterSprayToJson(spray),parsed,&error));QCOMPARE(parsed,spray);
        auto bad=clusterSprayToJson(spray);bad["clusters"]=3.5;QVERIFY(!clusterSprayFromJson(bad,parsed,&error));QCOMPARE(parsed,spray);
        bad=clusterSprayToJson(spray);bad["mystery"]=1;QVERIFY(!clusterSprayFromJson(bad,parsed,&error));QCOMPARE(parsed,spray);
        BrushSpeedDynamics speed;speed.enabled=true;speed.referenceSpeed=645;speed.sizeAmount=.4;
        BrushSpeedDynamics restored;QVERIFY(speedDynamicsFromJson(speedDynamicsToJson(speed),restored,&error));QCOMPARE(restored,speed);
        auto badSpeed=speedDynamicsToJson(speed);badSpeed["referenceSpeed"]=0;QVERIFY(!speedDynamicsFromJson(badSpeed,restored,&error));QCOMPARE(restored,speed);
    }
    void timestampVelocityIsIndependentOfDeliveryAndRejectsDuplicates() {
        BrushSpeedTracker tracker;tracker.reset({0,0},100);
        QCOMPARE(tracker.update({10,0},110),1000.);
        QCOMPARE(tracker.update({15,0},110),1000.);
        QCOMPARE(tracker.update({20,0},120),1000.);
        const auto slowed=tracker.update({20,0},144);QVERIFY(slowed>300&&slowed<400);
        QCOMPARE(tracker.update({40,0},143),0.);
        QCOMPARE(tracker.update({50,0},153),1000.);
        BrushSpeedDynamics response;QCOMPARE(brushSpeedFactor(response,1000,-.5),1.);
        response.enabled=true;QCOMPARE(brushSpeedFactor(response,500,-.5),.75);QCOMPARE(brushSpeedFactor(response,2000,-.5),.5);
    }
    void clusteredSpraySharesCentersAndSeparatesRadialAngularDistribution() {
        BrushClusterSpray spray;spray.enabled=true;spray.clusters=3;spray.spread=0;
        const auto a=brushClusterOffset(spray,100,0,7,0,0),b=brushClusterOffset(spray,100,0,7,0,3);
        QCOMPARE(a,b);QVERIFY(a!=brushClusterOffset(spray,100,0,7,0,1));
        QCOMPARE(a,brushClusterOffset(spray,100,0,7,0,0));QVERIFY(a!=brushClusterOffset(spray,100,0,7,1,0));
        spray.angularSpread=0;spray.angle=90;
        const auto vertical=brushClusterOffset(spray,100,0,7,0,0);QVERIFY(std::abs(vertical.x())<.0001);QVERIFY(vertical.y()>0);
        const auto radius=std::hypot(vertical.x(),vertical.y());spray.angle=0;
        const auto horizontal=brushClusterOffset(spray,100,0,7,0,0);QVERIFY(std::abs(horizontal.y())<.0001);QVERIFY(std::abs(horizontal.x()-radius)<.0001);
        spray.radialBias=0;const auto inner=brushClusterOffset(spray,100,0,7,0,0);spray.radialBias=1;
        QVERIFY(brushClusterOffset(spray,100,0,7,0,0).x()>inner.x());
        BrushSettings brush;brush.clusterSpray=spray;brush.size=100;brush.pressureSize=false;
        const auto first=evaluateBrushDab(brush,{}, {},0,0,7,0,{},BrushDabPurpose::Painting,0,0);
        const auto fourth=evaluateBrushDab(brush,{}, {},0,0,7,3*0x9e3779b9U,{},BrushDabPurpose::Painting,3,0);
        QCOMPARE(first.offset,fourth.offset);
    }
    void tintRetainsSourceShadingCoverageAndSourceImage() {
        BrushSettings brush;brush.colorTip=colored();brush.size=48;brush.pressureSize=false;brush.color=QColor(30,170,240);
        const auto source=brush.colorTip;const auto baseline=evaluateBrushDab(brush,{}, {},0,0,0,1);
        brush.colorDynamics.tintAmount=1;const auto tinted=evaluateBrushDab(brush,{}, {},0,0,0,1);
        QCOMPARE(tinted.mask,baseline.mask);QCOMPARE(brush.colorTip,source);QVERIFY(tinted.colorTip!=baseline.colorTip);
        bool light=false,dark=false,partial=false;
        for(int y=0;y<tinted.colorTip.height();++y)for(int x=0;x<tinted.colorTip.width();++x) {
            const auto before=baseline.colorTip.pixelColor(x,y),after=tinted.colorTip.pixelColor(x,y);QCOMPARE(after.alpha(),before.alpha());
            if(after.alpha()<20)continue;
            QVERIFY(std::abs(after.hsvHueF()-brush.color.hsvHueF())<.025);
            QVERIFY(std::abs(after.valueF()-before.valueF()*brush.color.valueF())<.02);
            light|=after.valueF()>.75;dark|=after.valueF()<.4;partial|=after.alpha()<255;
        }
        QVERIFY(light&&dark&&partial);brush.colorDynamics.tintAmount=0;
        QCOMPARE(evaluateBrushDab(brush,{}, {},0,0,0,1).colorTip,baseline.colorTip);
    }
    void speedChangesSizeAndOpacityWithContinuousIntermediateValues() {
        BrushSettings brush;brush.size=100;brush.pressureSize=false;brush.strokeOpacity=.8;
        brush.speedDynamics.enabled=true;brush.speedDynamics.sizeAmount=-.5;brush.speedDynamics.opacityAmount=-.75;
        BrushInput input;const auto slow=evaluateBrushDab(brush,input,{},0,0,0,1);
        input.speed=500;const auto medium=evaluateBrushDab(brush,input,{},0,0,0,1);
        input.speed=1000;const auto fast=evaluateBrushDab(brush,input,{},0,0,0,1);
        QVERIFY(slow.mask.width()>medium.mask.width()&&medium.mask.width()>fast.mask.width());
        QVERIFY(std::abs(slow.opacity-.8)<.0001);QVERIFY(std::abs(medium.opacity-.5)<.0001);QVERIFY(std::abs(fast.opacity-.2)<.0001);
        brush.speedDynamics.enabled=false;QCOMPARE(evaluateBrushDab(brush,input,{},0,0,0,1).mask,slow.mask);
    }
    void queuedTabletTimestampsMatchDirectDeliveryAndCaptureSpeed() {
        Canvas queued,direct;BrushSettings brush;brush.size=18;brush.pressureSize=false;brush.smoothingEnabled=false;
        brush.speedDynamics.enabled=true;brush.speedDynamics.referenceSpeed=1200;brush.lockRandomSeed=true;
        for(auto *canvas:{&queued,&direct}){canvas->resize(640,480);canvas->setProject(fixture(true));canvas->setBrushSettings(brush);canvas->setStabilization(0);}
        QPointingDevice pen("Speed pen",87,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
        for(auto *canvas:{&queued,&direct}) {
            const bool post=canvas==&queued;tablet(*canvas,pen,QEvent::TabletPress,{30,120},100,post);
            for(int i=1;i<=24;++i)tablet(*canvas,pen,QEvent::TabletMove,{30+7.*i,120+15*std::sin(i*.3)},100+10*i,post);
            tablet(*canvas,pen,QEvent::TabletRelease,{198,120+15*std::sin(24*.3)},341,post);
        }
        QCoreApplication::sendPostedEvents(&queued);
        QCOMPARE(renderMovieFrame(queued.project()),renderMovieFrame(direct.project()));
        const auto &a=queued.shot().layers[0].objects.first(),&b=direct.shot().layers[0].objects.first();
        QVERIFY(a.brushStroke&&b.brushStroke);QCOMPARE(a.brushStroke->samples,b.brushStroke->samples);
        bool speed=false;for(const auto &sample:a.brushStroke->samples)speed|=sample.input.speed>500;QVERIFY(speed);
        auto rebuilt=queued.shot().layers[0];QString error;clearVectorBrushReplayCache();
        QVERIFY2(rebuildObjectLayer(rebuilt,queued.document()->drawingBounds(),maxProjectTiles,&error),qPrintable(error));
        QCOMPARE(rebuilt.tiles,queued.shot().layers[0].tiles);
    }
    void combinedClusterTintAndSpeedReplayMatchesRaster() {
        BrushSettings brush;brush.colorTip=colored();brush.color=QColor(45,160,90);brush.colorDynamics.tintAmount=.85;
        brush.size=22;brush.pressureSize=false;brush.spacing=.4;brush.count=6;brush.clusterSpray.enabled=true;
        brush.clusterSpray.radius=.9;brush.clusterSpray.spread=.2;brush.speedDynamics.enabled=true;
        brush.lockRandomSeed=true;brush.randomSeed=0;
        Document raster,vector;raster.load(fixture());vector.load(fixture(true));
        for(auto *document:{&raster,&vector}){
            BrushInput input;QVERIFY(document->beginStroke({40,125},input,brush,false));
            for(int i=1;i<=20;++i){input.speed=i*50.;document->continueStroke({40+i*8.,125+22*std::sin(i*.25)},input);}document->endStroke();
        }
        QCOMPARE(vector.project().shots[0].layers[0].tiles,raster.project().shots[0].layers[0].tiles);
        auto rebuilt=vector.project().shots[0].layers[0];QString error;clearVectorBrushReplayCache();
        QVERIFY2(rebuildObjectLayer(rebuilt,vector.drawingBounds(),maxProjectTiles,&error),qPrintable(error));
        QCOMPARE(rebuilt.tiles,raster.project().shots[0].layers[0].tiles);
        const auto capture=vectorBrushStrokeToJson(*rebuilt.objects.first().brushStroke);VectorBrushStroke decoded;
        QVERIFY2(vectorBrushStrokeFromJson(capture,&decoded,&error),qPrintable(error));
        QCOMPARE(decoded.samples,rebuilt.objects.first().brushStroke->samples);
        const auto directory=qEnvironmentVariable("TOON_ADVANCED_DYNAMICS_PREVIEW");
        if(!directory.isEmpty())renderMovieFrame(raster.project()).save(directory+"/cluster-tint-speed-stroke.png");
    }
    void mouseEventsAlsoUseTimestampedArtworkVelocity() {
        Canvas canvas;canvas.resize(640,480);canvas.setProject(fixture(true));
        BrushSettings brush;brush.size=15;brush.pressureSize=false;brush.smoothingEnabled=false;brush.speedDynamics.enabled=true;
        canvas.setBrushSettings(brush);canvas.setStabilization(0);canvas.setZoom(.5);
        const auto send=[&](QEvent::Type type,QPointF artwork,ulong time){
            const auto local=canvas.viewTransform().map(sheetGeometry(canvas.project(),canvas.shot()).output.topLeft()+artwork);
            QMouseEvent event(type,local,canvas.mapToGlobal(local),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
            event.setTimestamp(time);QApplication::sendEvent(&canvas,&event);
        };
        send(QEvent::MouseButtonPress,{40,120},100);
        for(int i=1;i<=10;++i)send(QEvent::MouseMove,{40+i*10.,120},100+i*10);
        send(QEvent::MouseButtonRelease,{140,120},201);
        const auto &object=canvas.shot().layers[0].objects.first();QVERIFY(object.brushStroke);
        qreal maximum=0;for(const auto &sample:object.brushStroke->samples)maximum=qMax(maximum,sample.input.speed);
        QVERIFY(std::abs(maximum-1000)<.001);
    }
    void largeColoredPreviewsAreBoundedAndPreserveSource() {
        BrushSettings brush;brush.colorTip=colored().scaled(2048,1024);brush.size=1024;brush.pressureSize=false;
        brush.colorDynamics.enabled=true;brush.colorDynamics.applyToColorTip=true;brush.colorDynamics.perTip=false;
        brush.colorDynamics.hue=.2;brush.colorDynamics.tintAmount=.65;
        const auto original=brush.colorTip;QElapsedTimer timer;timer.start();
        const auto first=brushStrokePreview(brush);const auto cold=timer.nsecsElapsed();timer.restart();
        const auto second=brushStrokePreview(brush);const auto warm=timer.nsecsElapsed();
        QCOMPARE(first,second);QCOMPARE(first.size(),QSize(200,60));QCOMPARE(brush.colorTip,original);
        qInfo("2048px source preview: cold %.3f ms, cached %.3f ms",cold/1000000.,warm/1000000.);
    }
    void largeStampCursorPreservesOutlineHolesWithoutColorWork() {
        BrushSettings brush;brush.size=1024;brush.pressureSize=false;
        brush.colorTip=QImage(2048,2048,QImage::Format_ARGB32_Premultiplied);brush.colorTip.fill(Qt::transparent);
        {QPainter p(&brush.colorTip);p.setBrush(Qt::red);p.setPen(Qt::NoPen);p.drawEllipse(QRectF(0,0,2048,2048));
            p.setCompositionMode(QPainter::CompositionMode_Clear);p.drawEllipse(QRectF(512,512,1024,1024));}
        brush.colorDynamics.tintAmount=1;brush.colorDynamics.enabled=true;brush.colorDynamics.applyToColorTip=true;brush.colorDynamics.hue=.7;
        Canvas canvas;canvas.resize(640,480);canvas.setProject(fixture());canvas.setBrushSettings(brush);canvas.setZoom(.25);
        const QPointF point(320,240);QMouseEvent event(QEvent::MouseMove,point,canvas.mapToGlobal(point),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(&canvas,&event);const auto path=canvas.brushCursorPath();
        QVERIFY(!path.isEmpty());QVERIFY(!path.contains(point));QVERIFY(path.contains(point+QPointF(100,0)));
        QVERIFY(std::abs(path.boundingRect().width()-256)<3);QVERIFY(path.elementCount()<12000);
        auto plain=brush;plain.colorDynamics={};canvas.setBrushSettings(plain);QCOMPARE(canvas.brushCursorPath(),path);
        QCOMPARE(canvas.document()->history()->count(),0);
    }
    void surfacePreviewsShowRepeatsAndFitDistantPerspectivePlanes() {
        BrushSettings brush;brush.size=180;brush.pressureSize=false;
        brush.surface.kind=BrushSurfaceKind::Image;brush.surface.tileSize={256,256};
        brush.surface.image=QImage(16,16,QImage::Format_ARGB32_Premultiplied);brush.surface.image.fill(Qt::transparent);
        for(int y=0;y<16;++y)for(int x=0;x<2;++x)brush.surface.image.setPixelColor(x,y,Qt::black);
        const auto original=brush;const auto repeating=brushStrokePreview(brush,{300,78},Qt::white);
        int runs=0;bool previous=false;
        for(int x=10;x<290;++x){const bool ink=repeating.pixelColor(x,39).red()<200;if(ink&&!previous)++runs;previous=ink;}
        QVERIFY2(runs>=4,"The preview must show several complete repetitions, not isolated pieces at artwork-scale spacing.");
        QCOMPARE(brush,original);
        brush.surface.perspective=true;brush.surface.quad={QPointF(2000,3000),QPointF(2512,3100),QPointF(2512,3412),QPointF(2000,3512)};
        const auto positioned=brush;const auto plane=brushStrokePreview(brush,{300,78},Qt::white);
        int inkPixels=0;QRect inkBounds;
        for(int y=0;y<plane.height();++y)for(int x=0;x<plane.width();++x)if(plane.pixelColor(x,y).red()<240){++inkPixels;inkBounds|=QRect(x,y,1,1);}
        QVERIFY2(inkPixels>200,"A valid plane outside thumbnail coordinates must still yield a visible fitted surface swatch.");
        QVERIFY(inkBounds.height()>45);QVERIFY(inkBounds.left()>100&&inkBounds.right()<200);
        QCOMPARE(brush,positioned);
        for(auto &corner:brush.surface.quad)corner+=QPointF(-1700,-2400);
        QCOMPARE(brushStrokePreview(brush,{300,78},Qt::white),plane);
    }
    void connectedControlsDisableIgnoredSettingsAndKeepSupportedEdits() {
        Canvas canvas;canvas.setProject(fixture());
        BrushSettings brush;QVERIFY(setBrushTipImages(brush,{colored(),colored()}));
        brush.tipOrder=BrushTipOrder::Random;brush.connected.mode=BrushConnectedMode::Ribbon;
        brush.airbrush=true;brush.speedDynamics.enabled=true;
        brush.angleDynamics.jitter=brush.roundnessDynamics.jitter=.5;
        brush.angleDynamics.control=brush.roundnessDynamics.control=BrushControl::Pressure;
        canvas.setBrushSettings(brush);const auto before=canvas.brush;
        BrushSettingsPanel panel(&canvas);panel.show();panel.openAdvancedSettings();
        auto *dialog=panel.findChild<QDialog *>("advancedBrushDialog");QVERIFY(dialog);
        auto *frames=panel.findChild<QListWidget *>("brushTipFrames");QVERIFY(frames);QCOMPARE(frames->count(),2);
        auto *mode=panel.findChild<QTabBar *>("brushConnectedMode");QVERIFY(mode);
        const QStringList ignored{"brushSpacing","brushSpacingVisual","brushTipSpacing","brushTipVisual","brushTipAnchor",
            "stampRotationVariation","brushHardness","quickBrushHardness","brushAirbrush","brushAirbrushRate",
            "angleJitter","angleControl","angleResponse","angleFade","angleJitterPerStroke",
            "roundnessJitter","roundnessControl","roundnessResponse","roundnessMinimum","roundnessFade","roundnessJitterPerStroke"};
        const QStringList supported{"brushTipFrames","brushTipAdd","brushTipRemove","brushTipOrder","brushTipWeight",
            "stampSizeVariation","stampSaturationVariation","quickBrushSize","quickBrushStrokeOpacity","quickBrushFlow",
            "sizeControl","sizeResponse","speedDynamicsEnabled","speedSizeAmount","brushRepeatLength"};
        for(int selectedMode:{int(BrushConnectedMode::Ribbon),int(BrushConnectedMode::Repeat),int(BrushConnectedMode::Off)}) {
            mode->setCurrentIndex(selectedMode);
            for(const auto &name:ignored) {
                auto *control=panel.findChild<QWidget *>(name);QVERIFY2(control,qPrintable(name));
                QCOMPARE(control->isEnabled(),selectedMode==int(BrushConnectedMode::Off));
            }
            for(const auto &name:supported) {
                auto *control=panel.findChild<QWidget *>(name);QVERIFY2(control,qPrintable(name));QVERIFY2(control->isEnabled(),qPrintable(name));
            }
            frames->setCurrentRow(1-frames->currentRow());
            QCOMPARE(panel.findChild<QWidget *>("brushTipAnchor")->isEnabled(),selectedMode==int(BrushConnectedMode::Off));
        }
        auto expected=before;expected.connected.mode=BrushConnectedMode::Off;QCOMPARE(canvas.brush,expected);
        dialog->close();expected.connected.mode=BrushConnectedMode::Repeat;canvas.setBrushSettings(expected);
        QVERIFY(!panel.findChild<QWidget *>("quickBrushHardness")->isEnabled());
        panel.openAdvancedSettings();QVERIFY(!panel.findChild<QWidget *>("brushTipAnchor")->isEnabled());
        QCOMPARE(canvas.brush,expected);QCOMPARE(canvas.document()->history()->count(),0);dialog->close();
    }
    void dynamicsControlsRetainSettingsWithoutRefreshEdits() {
        AdvancedBrushDynamicsWidget spray(AdvancedBrushDynamicsWidget::Section::Spray),speed(AdvancedBrushDynamicsWidget::Section::Speed);
        int edits=0;connect(&spray,&AdvancedBrushDynamicsWidget::clusterSprayEdited,[&](const BrushClusterSpray &){++edits;});
        BrushSettings brush;brush.clusterSpray.enabled=true;brush.clusterSpray.radius=2.2;brush.speedDynamics.enabled=true;
        spray.setSettings(brush);speed.setSettings(brush);QCOMPARE(edits,0);
        QCOMPARE(spray.findChild<ValueSlider *>("sprayRadius")->value(),220.);
        spray.findChild<ValueSlider *>("sprayRadius")->setValue(180);QCOMPARE(edits,1);
        QCOMPARE(speed.findChild<ValueSlider *>("speedSizeAmount")->value(),-50.);
        const auto directory=qEnvironmentVariable("TOON_ADVANCED_DYNAMICS_PREVIEW");
        if(!directory.isEmpty()){spray.resize(310,480);spray.show();speed.resize(310,180);speed.show();QCoreApplication::processEvents();spray.grab().save(directory+"/cluster-spray.png");speed.grab().save(directory+"/speed-dynamics.png");}
    }
    void optionalDynamicsCollapseWithoutResettingConfiguredValues() {
        for (auto section : {AdvancedBrushDynamicsWidget::Section::Spray, AdvancedBrushDynamicsWidget::Section::Speed}) {
            const bool spray = section == AdvancedBrushDynamicsWidget::Section::Spray;
            AdvancedBrushDynamicsWidget widget(section);
            BrushSettings settings;settings.clusterSpray.radius=2.2;settings.speedDynamics.referenceSpeed=1750;
            int edits=0;
            connect(&widget,&AdvancedBrushDynamicsWidget::clusterSprayEdited,[&](const BrushClusterSpray &s){++edits;settings.clusterSpray=s;});
            connect(&widget,&AdvancedBrushDynamicsWidget::speedDynamicsEdited,[&](const BrushSpeedDynamics &s){++edits;settings.speedDynamics=s;});
            widget.setSettings(settings);widget.resize(240,500);widget.show();QCoreApplication::processEvents();
            auto *toggle=widget.findChild<QCheckBox *>(spray?"clusterSprayEnabled":"speedDynamicsEnabled");
            auto *body=widget.findChild<QWidget *>(spray?"clusterSprayControls":"speedDynamicsControls");
            auto *value=widget.findChild<ValueSlider *>(spray?"sprayRadius":"speedReference");
            QVERIFY(toggle&&body&&value);QVERIFY(toggle->isVisible());QVERIFY(body->isHidden());
            const int collapsed=widget.sizeHint().height();const auto expected=spray?220.:1750.;
            QCOMPARE(value->value(),expected);QCOMPARE(edits,0);
            toggle->setChecked(true);QCoreApplication::processEvents();
            QVERIFY(body->isVisible());QVERIFY(widget.sizeHint().height()>collapsed+50);QCOMPARE(value->value(),expected);
            auto font=widget.font();font.setPointSizeF(font.pointSizeF()+4);widget.setFont(font);widget.resize(300,550);
            QCoreApplication::processEvents();QCOMPARE(value->value(),expected);QCOMPARE(edits,1);
            toggle->setChecked(false);QVERIFY(body->isHidden());QCOMPARE(value->value(),expected);
            widget.setSettings(settings);QCOMPARE(edits,2);QVERIFY(body->isHidden());
            toggle->setChecked(true);QCOMPARE(edits,3);QCOMPARE(value->value(),expected);
        }
    }
};
QTEST_MAIN(AdvancedDynamicsTest)
#include "test_advanced_dynamics.moc"
