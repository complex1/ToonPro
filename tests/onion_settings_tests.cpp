#include "animation_render.h"
#include "canvas.h"
#include "keyframe_render.h"
#include "onion_settings_dialog.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QColorDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QJsonDocument>
#include <QPushButton>
#include <QSignalSpy>
#include <QToolButton>
#include <QUuid>
#include <QtTest>
#include <limits>

using namespace toon;
namespace {
QString id(const QString &name) { return QUuid::createUuidV5(QUuid("{b9d4655e-033c-451e-a622-e471e3ede0f8}"),name.toUtf8()).toString(QUuid::WithoutBraces); }
Project fixture() {
    auto project = Project::create(); project.width = project.height = 128; project.transparent = true;
    auto &shot = project.shots[0]; shot.durationFrames = 24; shot.sheet.overscan = 0;
    auto background = RasterLayer::create("Background");
    auto root = RasterLayer::create("Character"); root.group = true;
    shot.layers = {background,root};
    AnimationTrack track; track.id = id("track"); track.rootGroupLayerId = root.id; track.drawingTemplate = {{"ink","Ink"}};
    for (int i = 0; i < 5; ++i) {
        auto group = RasterLayer::create(QString("A%1").arg(i)); group.group = true; group.parentId = root.id;
        auto layer = RasterLayer::create("Ink"); layer.parentId = group.id;
        QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::transparent);
        {QPainter painter(&tile); painter.fillRect(QRect(4+i*23,16,16,16),Qt::black);}
        layer.tiles.insert({0,0},tile);
        track.drawings.append({id(QString("drawing%1").arg(i)),group.id,{{"ink",layer.id}}});
        shot.layers.append(group); shot.layers.append(layer);
    }
    track.exposures = {{id("e0"),id("drawing0"),0,3},{id("e1"),id("drawing1"),3,3},{id("e2"),id("drawing1"),7,2},
        {id("e3"),id("drawing2"),9,3},{id("e4"),id("drawing3"),13,3},{id("e5"),id("drawing3"),16,2},{id("e6"),id("drawing4"),20,4}};
    shot.animationTracks = {track}; return project;
}
ValueSlider *field(OnionSettingsDialog &dialog,const char *name) { return dialog.findChild<ValueSlider *>(name); }
QImage composite(const QVector<RasterLayer> &layers) {
    QImage image(128,128,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
    QPainter p(&image);paintLayers(p,layers,false);return image;
}
}
class OnionSettingsTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void safePreferencesRoundTripAndFallback() {
        OnionSkinSettings defaults; QCOMPARE(defaults.opacity,.25);
        OnionSkinSettings configured; configured.previousCount=0; configured.nextCount=8;
        configured.previousColor=QColor("#123456");configured.nextColor=QColor("#abcdef");configured.opacity=.67;
        QVERIFY(validOnionSkinSettings(configured));
        QCOMPARE(onionSkinSettingsFromJson(onionSkinSettingsToJson(configured)),configured);
        QCOMPARE(onionSkinSettingsFromJson({}),defaults);
        auto invalid = onionSkinSettingsToJson(configured);invalid["previousCount"]=1.5;
        QCOMPARE(onionSkinSettingsFromJson(invalid),defaults);
        invalid["previousCount"]=9;QCOMPARE(onionSkinSettingsFromJson(invalid),defaults);
        invalid=onionSkinSettingsToJson(configured);invalid["nextColor"]="bad-color";
        QCOMPARE(onionSkinSettingsFromJson(invalid),defaults);
        invalid=onionSkinSettingsToJson(configured);invalid["opacity"]=2;
        QCOMPARE(onionSkinSettingsFromJson(invalid),defaults);
        invalid=onionSkinSettingsToJson(configured);invalid["previousColor"]="#00123456";
        QCOMPARE(onionSkinSettingsFromJson(invalid),defaults);
        configured.opacity=std::numeric_limits<double>::quiet_NaN();QVERIFY(!validOnionSkinSettings(configured));
        QCOMPARE(onionSkinSettingsFromJson(onionSkinSettingsToJson(configured)),defaults);
    }
    void samplesSkipHoldsReusedDrawingsAndGaps() {
        const auto project=fixture();const auto &shot=project.shots.first();
        QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        const QVector<OnionSkinSample> before{{id("drawing1"),8},{id("drawing0"),2}},after{{id("drawing3"),13},{id("drawing4"),20}};
        QCOMPARE(onionSkinSamples(shot,10,id("track"),-1,8),before);
        QCOMPARE(onionSkinSamples(shot,10,id("track"),1,8),after);
        QCOMPARE(onionSkinSamples(shot,11,id("track"),-1,8),before);
        QCOMPARE(onionSkinSamples(shot,10,id("track"),-1,1),before.mid(0,1));
        QVERIFY(onionSkinSamples(shot,10,id("track"),-1,0).isEmpty());
        QVERIFY(onionSkinSamples(shot,0,id("track"),-1,8).isEmpty());
        QVERIFY(onionSkinSamples(shot,23,id("track"),1,8).isEmpty());
        QVERIFY(onionSkinSamples(shot,10,"missing",1,8).isEmpty());
        QVERIFY(onionSkinSamples(shot,10,id("track"),0,8).isEmpty());
        QCOMPARE(onionSkinSamples(shot,6,id("track"),-1,8).first(),(OnionSkinSample{id("drawing1"),5}));
        QCOMPARE(onionSkinSamples(shot,6,id("track"),1,8).first(),(OnionSkinSample{id("drawing1"),7}));
        // Reusing the current drawing farther away still does not add its ghost.
        auto reused=shot;reused.animationTracks[0].exposures[6].drawingId=id("drawing2");
        QCOMPARE(onionSkinSamples(reused,10,id("track"),1,8).size(),1);
        auto hidden=shot;hidden.layers[8].visible=false;
        QCOMPARE(onionSkinSamples(hidden,10,id("track"),1,8).first().drawingId,QString(id("drawing4")));
        hidden.layers[1].visible=false;QVERIFY(onionSkinSamples(hidden,10,id("track"),-1,8).isEmpty());
    }
    void multipleSamplesKeepTheirOwnPosesAndExcludeOtherDrawings() {
        auto project=fixture();auto &shot=project.shots[0];
        const auto &track=shot.animationTracks[0];
        const auto layerId=track.drawings[1].templateLayers.value("ink");
        LayerAnimation animation;animation.layerId=layerId;
        animation.channels={{KeyframeProperty::PositionY,{{0,0},{20,40}}}};shot.layerAnimations={animation};
        const auto samples=onionSkinSamples(shot,10,track.id,-1,8);QCOMPARE(samples.size(),2);
        const auto layers=onionSkinLayersForSample(shot,10,track.id,samples[0],{0,0,128,128});
        QCOMPARE(layers.size(),3);QCOMPARE(layers.last().id,layerId);
        const auto image=composite(layers);QVERIFY(image.pixelColor(30,34).alpha()>0);QCOMPARE(image.pixelColor(8,20).alpha(),0);
        QCOMPARE(composite(onionSkinLayersForSample(shot,10,track.id,samples[1],{0,0,128,128})).pixelColor(8,20),QColor(Qt::black));
        QVERIFY(onionSkinLayersForSample(shot,10,track.id,{"wrong",8}).isEmpty());
        QVERIFY(onionSkinLayersForSample(shot,10,track.id,{id("drawing1"),24}).isEmpty());
    }
    void vectorGhostUsesNeighborMorphAndCurrentCamera() {
        auto project=fixture();auto &shot=project.shots[0];auto &layer=shot.layers[3];layer.kind=LayerKind::Vector;layer.tiles.clear();
        auto object=EditableObject::create();object.closed=true;object.stroke=QColor();object.strokeWidth=0;object.fill=Qt::red;
        object.nodes={BezierNode::corner({180,20}),BezierNode::corner({196,20}),BezierNode::corner({196,36}),BezierNode::corner({180,36})};
        layer.objects={object};auto end=object;for(auto &node:end.nodes){node.anchor.ry()+=20;node.inControl.ry()+=20;node.outControl.ry()+=20;}
        LayerAnimation animation;animation.layerId=layer.id;
        animation.paths={{object.id,{{0,pathPose(object)},{2,pathPose(end)}}}};shot.layerAnimations={animation};
        shot.camera2DKeys={{0,{},KeyframeInterpolation::Hold},{9,{{160,0},1,0},KeyframeInterpolation::Linear}};
        const auto samples=onionSkinSamples(shot,10,id("track"),-1,8);QCOMPARE(samples.last().frame,2);
        const auto layers=onionSkinLayersForSample(shot,10,id("track"),samples.last(),{160,0,128,128});
        QImage image(128,128,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
        {QPainter p(&image);p.translate(-160,0);paintLayers(p,layers,false);}
        QCOMPARE(image.pixelColor(26,48),QColor(Qt::red));QCOMPARE(image.pixelColor(26,25).alpha(),0);
        QVERIFY(layer.tiles.isEmpty());
    }
    void canvasSettingsPreviewIsTransientAndCacheResponds() {
        auto project=fixture();Canvas canvas;TimelineController controller(&canvas);canvas.resize(800,600);canvas.setProject(project);canvas.show();
        QVERIFY(controller.selectTrack(id("track")));QVERIFY(controller.setFrame(10));
        controller.setOnionSkin(true);const auto baseline=canvas.grab().toImage();const auto renderCount=canvas.artworkRenderCount();
        auto settings=canvas.onionSkinSettings();settings.previousCount=2;settings.nextCount=2;settings.opacity=.6;
        canvas.setOnionSkinSettings(settings);const auto multiple=canvas.grab().toImage();QVERIFY(multiple!=baseline);
        QVERIFY(canvas.artworkRenderCount()>renderCount);const auto stableCount=canvas.artworkRenderCount();
        canvas.grab();QCOMPARE(canvas.artworkRenderCount(),stableCount);
        QVERIFY(controller.setFrame(11));canvas.grab();QCOMPARE(canvas.artworkRenderCount(),stableCount);
        settings.previousColor=Qt::yellow;canvas.setOnionSkinSettings(settings);QVERIFY(canvas.grab().toImage()!=multiple);
        settings.previousCount=settings.nextCount=0;canvas.setOnionSkinSettings(settings);const auto zero=canvas.grab().toImage();
        controller.setOnionSkin(false);QCOMPARE(canvas.grab().toImage(),zero);
        settings.previousCount=settings.nextCount=2;canvas.setOnionSkinSettings(settings);controller.setOnionSkin(true);canvas.grab();
        canvas.setAnimationPlayback(true);QCOMPARE(canvas.grab().toImage(),zero);canvas.setAnimationPlayback(false);
        QVERIFY(canvas.grab().toImage()!=zero);
        QCOMPARE(canvas.project().toJson(),project.toJson());QCOMPARE(canvas.document()->history()->count(),0);
        QCOMPARE(renderMovieFrame(canvas.project(),10),renderMovieFrame(project,10));
    }
    void dialogPreviewResetCancelAndExactEntry() {
        OnionSkinSettings initial;initial.previousCount=3;initial.nextCount=4;
        OnionSettingsDialog dialog(initial);QSignalSpy previews(&dialog,&OnionSettingsDialog::settingsChanged);
        dialog.resize(240,310);dialog.show();QTest::qWait(30);
        for(auto *number:dialog.findChildren<QDoubleSpinBox *>()){QCOMPARE(number->width(),48);QVERIFY(number->suffix().isEmpty());}
        field(dialog,"onionPreviousCount")->setValue(6);QCOMPARE(dialog.settings().previousCount,6);QVERIFY(!previews.isEmpty());
        dialog.resetDefaults();QCOMPARE(dialog.settings(),OnionSkinSettings{});
        field(dialog,"onionNextCount")->setValue(8);dialog.reject();QCOMPARE(dialog.settings(),initial);
        OnionSettingsDialog accepted(initial);accepted.show();
        auto *number=field(accepted,"onionOpacity")->findChild<QDoubleSpinBox *>();number->setFocus();number->selectAll();QTest::keyClicks(number,"72");
        accepted.accept();QCOMPARE(accepted.settings().opacity,.72);QCOMPARE(accepted.result(),int(QDialog::Accepted));
        OnionSettingsDialog narrow(initial);narrow.resize(240,310);narrow.show();QTest::qWait(20);QCOMPARE(narrow.width(),240);
        const auto preview=qEnvironmentVariable("TOON_UI_PREVIEW");if(!preview.isEmpty()){QDir().mkpath(preview);QVERIFY(narrow.grab().save(QDir(preview).filePath("onion-settings.png")));}
        narrow.reject();
    }
    void cancellingColorPickerRestoresOnlyItsColor() {
        OnionSettingsDialog dialog({});dialog.show();
        field(dialog,"onionPreviousCount")->setValue(4);
        QTimer::singleShot(0,qApp,[]{for(auto *widget:QApplication::topLevelWidgets())if(auto *color=qobject_cast<QColorDialog *>(widget)){color->setCurrentColor(Qt::green);color->reject();}});
        dialog.findChild<QToolButton *>("onionPreviousColor")->click();
        QCOMPARE(dialog.settings().previousColor,OnionSkinSettings{}.previousColor);QCOMPARE(dialog.settings().previousCount,4);
        dialog.reject();
    }
};
QTEST_MAIN(OnionSettingsTests)
#include "onion_settings_tests.moc"
