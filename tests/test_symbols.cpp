#include "symbols.h"
#include "sheet.h"
#include "animation_render.h"
#include "keyframe_render.h"
#include "animation_test_fixture.h"
#include <QTemporaryDir>
#include <QPainter>
#include <QtTest>

using namespace toon;
namespace {
QString id(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
RasterLayer instance(const QString &symbolId){auto layer=RasterLayer::create("Instance");layer.kind=LayerKind::Symbol;layer.symbol=SymbolInstance{symbolId};return layer;}
QImage art(QColor color){QImage image(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter painter(&image);painter.fillRect(QRect(8,8,12,12),color);return image;}
Project fixture(){
    auto project=Project::create();project.width=128;project.height=128;project.transparent=true;
    SymbolDefinition symbol{id(),"Eye",Project::create().shots[0]};symbol.content.durationFrames=4;
    symbol.content.layers[0].tiles[{0,0}]=art(Qt::red);
    project.symbols.append(symbol);project.shots[0].layers={instance(symbol.id)};
    return project;
}
}
class SymbolTests:public QObject{
    Q_OBJECT
private slots:
    void localTiming(){
        SymbolInstance value{id(),SymbolPlayback::Loop,10,1,1};
        QCOMPARE(symbolLocalFrame(value,9,4),-1);QCOMPARE(symbolLocalFrame(value,10,4),1);
        QCOMPARE(symbolLocalFrame(value,13,4),0);QCOMPARE(symbolLocalFrame(value,17,4),0);
        value.speed=.5;QCOMPARE(symbolLocalFrame(value,11,4),1);QCOMPARE(symbolLocalFrame(value,12,4),2);
        value.speed=2;QCOMPARE(symbolLocalFrame(value,11,4),3);
        value.playback=SymbolPlayback::Once;QCOMPARE(symbolLocalFrame(value,200,4),3);
        value.playback=SymbolPlayback::SingleFrame;QCOMPARE(symbolLocalFrame(value,200,4),1);
        value.playback=SymbolPlayback::PingPong;value.firstFrame=0;value.speed=1;
        const QList<int> expected{0,1,2,3,2,1,0,1};
        for(int i=0;i<expected.size();++i)QCOMPARE(symbolLocalFrame(value,10+i,4),expected[i]);
        QCOMPARE(symbolLocalFrame(value,50,1),0);value.speed=0;QCOMPARE(symbolLocalFrame(value,12,4),-1);
    }
    void linkedMasterAndIndependentTransformsAcrossShots(){
        auto project=fixture();auto second=Project::create().shots[0];second.number="C002";second.layers={instance(project.symbols[0].id)};
        second.layerAnimations={{second.layers[0].id,{},{{KeyframeProperty::PositionX,{{0,30}}}}}};project.shots.append(second);
        QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        auto first=renderMovieFrame(project,0,0),other=renderMovieFrame(project,0,1);
        QCOMPARE(first.pixelColor(10,10),QColor(Qt::red));QCOMPARE(other.pixelColor(40,10),QColor(Qt::red));QCOMPARE(other.pixelColor(10,10).alpha(),0);
        project.symbols[0].content.layers[0].tiles[{0,0}]=art(Qt::blue);
        first=renderMovieFrame(project,0,0);other=renderMovieFrame(project,0,1);
        QCOMPARE(first.pixelColor(10,10),QColor(Qt::blue));QCOMPARE(other.pixelColor(40,10),QColor(Qt::blue));
        QVERIFY(project.shots[0].layers[0].tiles.isEmpty());QCOMPARE(symbolUseCount(project,project.symbols[0].id),2);
    }
    void animatedExposureAndKeyframesExport(){
        auto project=fixture();auto animation=animation_fixture::project().shots[0];animation.durationFrames=8;
        project.symbols[0].content=animation;
        auto &instance=*project.shots[0].layers[0].symbol;instance.startFrame=2;
        QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        QCOMPARE(renderMovieFrame(project,0).pixelColor(10,10).alpha(),0);
        QCOMPARE(renderMovieFrame(project,2).pixelColor(10,10),QColor(Qt::red));
        QCOMPARE(renderMovieFrame(project,5).pixelColor(10,10).alpha(),0);
        QCOMPARE(renderMovieFrame(project,6).pixelColor(10,10),QColor(Qt::blue));
        QCOMPARE(renderMovieFrame(project,10).pixelColor(10,10),QColor(Qt::red));
        QVERIFY(animationDisplayKey(project.shots[0],2)!=animationDisplayKey(project.shots[0],3));
        QTemporaryDir folder;QString error;
        QVERIFY2(exportPngSequence(project,0,2,3,folder.path(),{},&error),qPrintable(error));
        QImage exported(folder.filePath("frame-0003.png"));QVERIFY(!exported.isNull());QCOMPARE(exported.pixelColor(10,10),QColor(Qt::red));
        auto &content=project.symbols[0].content;const auto ink=content.animationTracks[0].drawings[0].templateLayers.value("ink");
        content.layerAnimations={{ink,{},{{KeyframeProperty::Opacity,{{0,1},{2,0}}}}}};
        QCOMPARE(renderMovieFrame(project,4).pixelColor(10,10).alpha(),0);
    }
    void nestedSymbolsAndEffects(){
        auto project=fixture();SymbolDefinition nested{id(),"Face",Project::create().shots[0]};
        nested.content.layers={instance(project.symbols[0].id)};nested.content.durationFrames=4;
        nested.content.layerAnimations={{nested.content.layers[0].id,{},{{KeyframeProperty::PositionX,{{0,20}}}}}};
        project.symbols.append(nested);project.shots[0].layers[0].symbol->symbolId=nested.id;
        auto &outer=project.shots[0].layers[0];outer.effects.shadowEnabled=true;outer.effects.shadowRadius=0;
        outer.effects.shadowOffset={30,0};outer.effects.shadowColor=Qt::blue;outer.effects.shadowOpacity=1;
        QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        auto image=renderMovieFrame(project,0);QVERIFY2(!image.isNull(),qPrintable(keyframeRenderError()));
        QCOMPARE(image.pixelColor(30,10),QColor(Qt::red));QCOMPARE(image.pixelColor(60,10),QColor(Qt::blue));
        project.symbols[0].content.layers[0].tiles[{0,0}]=art(Qt::green);
        image=renderMovieFrame(project,0);QCOMPARE(image.pixelColor(30,10),QColor(Qt::green));
        QVERIFY(symbolArtworkBounds(project,nested.id).contains(QPointF(30,10)));
    }
    void tightBoundsAndOffOriginSourceCrop(){
        auto project=fixture();const auto symbolId=project.symbols[0].id;
        QCOMPARE(symbolArtworkBounds(project,symbolId),QRectF(8,8,12,12));
        auto &source=project.symbols[0].content.layers[0];source.tiles.clear();source.tiles[{4,0}]=art(Qt::red);
        const auto instanceId=project.shots[0].layers[0].id;
        project.shots[0].layerAnimations={{instanceId,{},{{KeyframeProperty::PositionX,{{0,-1000}}}}}};
        QCOMPARE(symbolArtworkBounds(project,symbolId),QRectF(1032,8,12,12));
        auto image=renderMovieFrame(project,0);QVERIFY2(!image.isNull(),qPrintable(keyframeRenderError()));
        QCOMPARE(image.pixelColor(34,10),QColor(Qt::red));QCOMPARE(image.pixelColor(10,10).alpha(),0);
        project.shots[0].layerAnimations[0].channels[0].keys[0].value=-1010;
        image=renderMovieFrame(project,0);QCOMPARE(image.pixelColor(24,10),QColor(Qt::red));
    }
    void invalidCyclesAndWorkBoundsFailSafely(){
        auto project=fixture();const auto id=project.symbols[0].id;
        project.symbols[0].content.layers={instance(id)};
        QVERIFY(!project.validate().isEmpty());QString error;
        QVERIFY(evaluateLayers(project,project.shots[0],0,false,QRectF(0,0,128,128),&error).isEmpty());QVERIFY(!error.isEmpty());
        project=fixture();project.symbols[0].content.layers[0].tiles.clear();
        project.symbols[0].content.layers[0].tiles[{-128,-128}]=art(Qt::red);
        project.symbols[0].content.layers[0].tiles[{128,128}]=art(Qt::red);
        QVERIFY(evaluateLayers(project,project.shots[0],0,false,{},&error).isEmpty());QVERIFY(!error.isEmpty());
        project=fixture();QVERIFY(!renderMovieFrame(project,0).isNull());QVERIFY(keyframeRenderError().isEmpty());
    }
};
QTEST_MAIN(SymbolTests)
#include "test_symbols.moc"
