#include "symbols.h"
#include "document.h"
#include "sheet.h"
#include "animation_test_fixture.h"
#include "canvas.h"
#include "motion_controls.h"
#include "timeline_controller.h"
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

using namespace toon;
namespace {
QImage tile(QColor color){QImage image(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter painter(&image);painter.fillRect(8,8,12,12,color);return image;}
Project fixture(){auto project=Project::create();project.width=128;project.height=128;project.transparent=true;project.shots[0].layers[0].tiles[{0,0}]=tile(Qt::red);return project;}
QString convert(Document &document,QString *error){return convertLayersToSymbol(&document,{document.activeShot().layers[0].id},"Eyes",error);}
Project pivotFixture() {
    auto project = Project::create(); project.width = 320; project.height = 180; project.transparent = true;
    SymbolDefinition symbol{animation_fixture::id(), "Prop", Project::create().shots[0]};
    QImage pixels(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied); pixels.fill(Qt::transparent);
    QPainter painter(&pixels); painter.fillRect(QRect(45,35,40,60),Qt::red); painter.end();
    symbol.content.layers[0].tiles[{0,0}] = pixels;
    auto instance = RasterLayer::create("Prop instance"); instance.kind = LayerKind::Symbol;
    instance.symbol = SymbolInstance{symbol.id};
    project.symbols = {symbol}; project.shots[0].layers = {instance}; return project;
}
}
class SymbolDocumentTests:public QObject{
    Q_OBJECT
private slots:
    void symbolRotationAndScaleKeepArtworkCentered() {
        Document document; document.load(pivotFixture()); const auto id = document.activeShot().layers[0].id;
        const auto source = document.project().symbols[0].content;
        const QPointF center(65,65);
        QCOMPARE(defaultAnimationPivot(document.project(),document.activeShot(),id),center);
        QVERIFY(document.enableKeyframeChannel(id,KeyframeProperty::Rotation));
        QCOMPARE(layerAnimation(document.activeShot(),id)->pivot,center);
        QVERIFY(document.setKeyframeValue(id,KeyframeProperty::Rotation,90));
        QVERIFY(QLineF(evaluateLayerTransform(document.activeShot(),id,0).map(center),center).length()<1e-8);
        auto image = renderMovieFrame(document.project(),0);
        QCOMPARE(image.pixelColor(65,65),QColor(Qt::red));
        QCOMPARE(image.pixelColor(38,65),QColor(Qt::red));
        QCOMPARE(image.pixelColor(65,38).alpha(),0);
        QVERIFY(document.setKeyframeValue(id,KeyframeProperty::ScaleX,2));
        QVERIFY(document.setKeyframeValue(id,KeyframeProperty::ScaleY,2));
        QVERIFY(QLineF(evaluateLayerTransform(document.activeShot(),id,0).map(center),center).length()<1e-8);
        QCOMPARE(document.project().symbols[0].content.layers[0].tiles,source.layers[0].tiles);
        QCOMPARE(document.project().symbols[0].content.layerAnimations,source.layerAnimations);
        QVERIFY(document.activeShot().layers[0].tiles.isEmpty());
        document.history()->undo();
        QCOMPARE(evaluateProperty(document.activeShot(),id,KeyframeProperty::ScaleY,0),1.);
        document.history()->redo();
        QCOMPARE(layerAnimation(document.activeShot(),id)->pivot,center);
    }
    void symbolPivotUsesChosenFrameThenStaysFixed() {
        auto project = pivotFixture(); auto &symbol = project.symbols[0]; symbol.content.durationFrames = 4;
        symbol.content.layerAnimations = {{symbol.content.layers[0].id,{},{{KeyframeProperty::PositionX,{{0,0},{3,30}}}}}};
        project.shots[0].layers[0].symbol->startFrame = 10;
        project.shots[0].layers[0].symbol->firstFrame = 1;
        const auto id = project.shots[0].layers[0].id;
        QCOMPARE(defaultAnimationPivot(project,project.shots[0],id,0),QPointF(75,65));
        QCOMPARE(defaultAnimationPivot(project,project.shots[0],id,11),QPointF(85,65));
        Document document; document.load(project); QVERIFY(document.setCurrentFrame(11));
        QVERIFY(document.setKeyframeValue(id,KeyframeProperty::ScaleX,2));
        QCOMPARE(layerAnimation(document.activeShot(),id)->pivot,QPointF(85,65));
        QVERIFY(document.setCurrentFrame(12));
        QCOMPARE(effectiveAnimationPivot(document.project(),document.activeShot(),id,12),QPointF(85,65));
        QVERIFY(document.setKeyframePivot(id,{20,30}));
        QVERIFY(document.enableKeyframeChannel(id,KeyframeProperty::Rotation));
        QCOMPARE(layerAnimation(document.activeShot(),id)->pivot,QPointF(20,30));
        QTemporaryDir directory; QString error; const auto path=directory.filePath("pivot.toon");
        QVERIFY2(saveProject(document.project(),path,&error),qPrintable(error));
        const auto loaded=loadProject(path,&error); QVERIFY2(loaded.has_value(),qPrintable(error));
        QCOMPARE(effectiveAnimationPivot(*loaded,loaded->shots[0],id,12),QPointF(20,30));
    }
    void groupedNestedSymbolUsesDescendantsButNotAncestorTransforms() {
        auto project=pivotFixture();
        SymbolDefinition wrapper{animation_fixture::id(),"Wrapper",Project::create().shots[0]};
        auto nested=project.shots[0].layers[0]; nested.id=animation_fixture::id();
        wrapper.content.layers={nested};
        wrapper.content.layerAnimations={{nested.id,{},{{KeyframeProperty::PositionX,{{0,30}}},{KeyframeProperty::PositionY,{{0,10}}}}}};
        project.symbols.append(wrapper);
        auto &shot=project.shots[0]; auto instance=shot.layers[0]; instance.symbol->symbolId=wrapper.id;
        auto ancestor=RasterLayer::create("Ancestor"); ancestor.group=true;
        auto group=RasterLayer::create("Group"); group.group=true; group.parentId=ancestor.id; instance.parentId=group.id;
        shot.layers={ancestor,group,instance};
        shot.layerAnimations={{instance.id,{},{{KeyframeProperty::PositionX,{{0,20}}},{KeyframeProperty::PositionY,{{0,-5}}}}},
                              {ancestor.id,{},{{KeyframeProperty::PositionX,{{0,100}}},{KeyframeProperty::ScaleX,{{0,2}}}}}};
        QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        QCOMPARE(defaultAnimationPivot(project,shot,instance.id),QPointF(95,75));
        QCOMPARE(defaultAnimationPivot(project,shot,group.id),QPointF(115,70));
        Document document; document.load(project);
        QVERIFY(document.setKeyframeValue(group.id,KeyframeProperty::Rotation,90));
        QCOMPARE(layerAnimation(document.activeShot(),group.id)->pivot,QPointF(115,70));
        const auto centerBefore=worldLayerTransform(shot,instance.id,0).map(QPointF(95,75));
        const auto centerAfter=worldLayerTransform(document.activeShot(),instance.id,0).map(QPointF(95,75));
        QVERIFY(QLineF(centerBefore,centerAfter).length()<1e-8);
        QVERIFY(document.setKeyframeValue(group.id,KeyframeProperty::ScaleX,0));
        QCOMPARE(defaultAnimationPivot(document.project(),document.activeShot(),group.id),QPointF(115,70));
    }
    void resetSymbolPivotKeepsCurrentPoseAndIsUndoable() {
        Canvas canvas; canvas.setProject(pivotFixture()); TimelineController controller(&canvas);
        auto *document=canvas.document(); const auto id=document->activeShot().layers[0].id;
        QCOMPARE(controller.animationPivot(),QPointF(65,65));
        QVERIFY(document->setKeyframeValue(id,KeyframeProperty::Rotation,90));
        QVERIFY(document->setKeyframeValue(id,KeyframeProperty::ScaleX,1.5));
        QVERIFY(document->setKeyframePivot(id,{20,30}));
        const auto before=worldLayerTransform(document->activeShot(),id,0);
        const int count=document->history()->count();
        QVERIFY(controller.resetAnimationPivot());
        QCOMPARE(controller.animationPivot(),QPointF(65,65));
        const auto after=worldLayerTransform(document->activeShot(),id,0);
        for(const auto point:{QPointF(45,35),QPointF(85,95)})
            QVERIFY(QLineF(before.map(point),after.map(point)).length()<1e-8);
        QCOMPARE(document->history()->count(),count+1);
        document->history()->undo(); QCOMPARE(controller.animationPivot(),QPointF(20,30));
        document->history()->redo(); QCOMPARE(controller.animationPivot(),QPointF(65,65));
    }
    void conversionKeepsArtworkAndOneUndo(){
        Document document;document.load(fixture());const auto original=document.project();const auto pixels=renderMovieFrame(original,0);QString error;
        const auto id=convert(document,&error);QVERIFY2(!id.isEmpty(),qPrintable(error));
        QCOMPARE(document.project().symbols.size(),1);QCOMPARE(document.activeShot().layers.size(),1);
        QCOMPARE(document.activeShot().layers[0].kind,LayerKind::Symbol);QVERIFY(document.activeShot().layers[0].tiles.isEmpty());
        QCOMPARE(document.project().symbols[0].content.layers[0].tiles,original.shots[0].layers[0].tiles);
        QCOMPARE(renderMovieFrame(document.project(),0),pixels);QCOMPARE(document.history()->count(),1);
        document.history()->undo();QCOMPARE(document.project().symbols.size(),0);QCOMPARE(renderMovieFrame(document.project(),0),pixels);
        document.history()->redo();QCOMPARE(document.project().symbols[0].id,id);QCOMPARE(document.activeShot().layers[document.activeLayer()].kind,LayerKind::Symbol);
    }
    void libraryActionsAndAtomicUnique(){
        Document document;document.load(fixture());QString error;const auto id=convert(document,&error);QVERIFY2(!id.isEmpty(),qPrintable(error));
        const auto layer=insertSymbolInstance(&document,id,&error);QVERIFY2(!layer.isEmpty(),qPrintable(error));QCOMPARE(symbolUseCount(document.project(),id),2);
        QVERIFY(!removeSymbol(&document,id,&error));QVERIFY(error.contains("in use"));
        QVERIFY(renameSymbol(&document,id,"Blink",&error));QCOMPARE(findSymbol(document.project(),id)->name,QString("Blink"));
        const auto unique=makeSymbolInstanceUnique(&document,layer,&error);QVERIFY2(!unique.isEmpty(),qPrintable(error));QVERIFY(unique!=id);
        QCOMPARE(symbolUseCount(document.project(),id),1);QCOMPARE(symbolUseCount(document.project(),unique),1);
        QVERIFY(findSymbol(document.project(),unique)->content.layers[0].id!=findSymbol(document.project(),id)->content.layers[0].id);
        const auto duplicate=duplicateSymbol(&document,id,&error);QVERIFY2(!duplicate.isEmpty(),qPrintable(error));
        QCOMPARE(symbolUseCount(document.project(),duplicate),0);QVERIFY(removeSymbol(&document,duplicate,&error));
        QVERIFY2(document.project().validate().isEmpty(),qPrintable(document.project().validate()));
    }
    void editsUpdateEveryShotAndRejectStaleEditor(){
        Document document;document.load(fixture());QString error;const auto id=convert(document,&error);QVERIFY2(!id.isEmpty(),qPrintable(error));
        auto after=document.project();auto other=Project::create().shots[0];other.number="C002";after.shots.append(other);document.replaceProject(after,"Add second shot");
        QVERIFY(document.selectShot(other.id));QVERIFY(!insertSymbolInstance(&document,id,&error).isEmpty());
        const auto original=findSymbol(document.project(),id)->content;auto changed=original;changed.layers[0].tiles[{0,0}]=tile(Qt::blue);
        QVERIFY2(updateSymbolContent(&document,id,changed,original,&error),qPrintable(error));
        QCOMPARE(renderMovieFrame(document.project(),0,0).pixelColor(10,10),QColor(Qt::blue));QCOMPARE(renderMovieFrame(document.project(),0,1).pixelColor(10,10),QColor(Qt::blue));
        QVERIFY(!updateSymbolContent(&document,id,original,original,&error));QVERIFY(error.contains("changed"));
        document.history()->undo();QCOMPARE(renderMovieFrame(document.project(),0,0).pixelColor(10,10),QColor(Qt::red));
    }
    void isolatedWorkspaceKeepsOnlyRequiredDefinitions(){
        Document document;document.load(fixture());QString error;const auto id=convert(document,&error);
        const auto unused=createEmptySymbol(&document,"Unused",&error);QVERIFY2(!unused.isEmpty(),qPrintable(error));
        auto workspace=editProjectForSymbol(document.project(),id,&error);QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(workspace.shots.size(),1);QVERIFY(workspace.symbols.isEmpty());QVERIFY(workspace.transparent);QVERIFY(workspace.validate().isEmpty());
        const auto original=findSymbol(document.project(),id)->content;
        Document editor;editor.load(workspace);editor.addLayer();
        QVERIFY2(updateSymbolContent(&document,id,editor.activeShot(),original,&error),qPrintable(error));QCOMPARE(findSymbol(document.project(),id)->content.layers.size(),2);
        QCOMPARE(document.project().symbols.size(),2);
    }
    void convertsWholeAnimationTrackWithoutFlattening(){
        auto project=animation_fixture::project();project.transparent=true;Document document;document.load(project);QString error;
        const auto root=project.shots[0].animationTracks[0].rootGroupLayerId;
        const auto id=convertLayersToSymbol(&document,{root},"Walk cycle",&error);QVERIFY2(!id.isEmpty(),qPrintable(error));
        QVERIFY(document.activeShot().animationTracks.isEmpty());const auto *definition=findSymbol(document.project(),id);
        QCOMPARE(definition->content.animationTracks.size(),1);QCOMPARE(definition->content.animationTracks[0].drawings.size(),2);
        QCOMPARE(definition->content.durationFrames,20);QVERIFY2(document.project().validate().isEmpty(),qPrintable(document.project().validate()));
        for(int frame:{0,2,3,4,6,7})QCOMPARE(renderMovieFrame(document.project(),frame),renderMovieFrame(project,frame));
        document.history()->undo();QCOMPARE(document.activeShot().animationTracks.size(),1);QVERIFY(document.project().symbols.isEmpty());
    }
    void refusesPartialCelsAndUnsafeSelections(){
        Document document;auto project=animation_fixture::project();document.load(project);QString error;
        const auto ink=project.shots[0].animationTracks[0].drawings[0].templateLayers.value("ink");
        QVERIFY(convertLayersToSymbol(&document,{ink},"Partial",&error).isEmpty());QVERIFY(error.contains("complete animation track"));
        QCOMPARE(document.history()->count(),0);
        project=fixture();project.shots[0].layers.append(RasterLayer::create("Middle"));project.shots[0].layers.append(RasterLayer::create("Top"));document.load(project);
        QVERIFY(convertLayersToSymbol(&document,{project.shots[0].layers[0].id,project.shots[0].layers[2].id},"Gap",&error).isEmpty());
        QVERIFY(error.contains("continuous"));QCOMPARE(document.history()->count(),0);
        project.shots[0].layers[1].clipped=true;document.load(project);
        QVERIFY(convert(document,&error).isEmpty());QVERIFY(error.contains("clipping"));
    }
    void convertingNestedSymbolPreservesItsClock(){
        auto project=fixture();const auto layer=project.shots[0].layers[0].id;
        project.shots[0].layerAnimations={{layer,{},{{KeyframeProperty::Opacity,{{0,1},{5,0}}}}}};
        Document document;document.load(project);QString error;
        const auto inner=convert(document,&error);QVERIFY2(!inner.isEmpty(),qPrintable(error));
        const auto before=renderMovieFrame(document.project(),5);
        const auto outer=convertLayersToSymbol(&document,{document.activeShot().layers[0].id},"Nested animation",&error);
        QVERIFY2(!outer.isEmpty(),qPrintable(error));
        QCOMPARE(findSymbol(document.project(),outer)->content.durationFrames,project.shots[0].durationFrames);
        QCOMPARE(renderMovieFrame(document.project(),5),before);
        QCOMPARE(renderMovieFrame(document.project(),0).pixelColor(10,10),QColor(Qt::red));
    }
    void insertionIntoExposedCelAndTiming(){
        Document document;document.load(animation_fixture::project());QString error;const auto id=createEmptySymbol(&document,"Mouth",&error);QVERIFY2(!id.isEmpty(),qPrintable(error));
        const auto track=document.activeShot().animationTracks[0];QVERIFY(document.selectAnimationTrack(track.id));
        const auto layer=insertSymbolInstance(&document,id,&error);QVERIFY2(!layer.isEmpty(),qPrintable(error));
        QCOMPARE(animationOwner(document.activeShot(),layer).drawingId,track.drawings[0].id);
        auto timing=*document.activeShot().layers[document.activeLayer()].symbol;timing.playback=SymbolPlayback::SingleFrame;timing.startFrame=2;
        QVERIFY(setSymbolInstance(&document,layer,timing,&error));QCOMPARE(document.activeShot().layers[document.activeLayer()].symbol->startFrame,2);
        timing.speed=0;QVERIFY(!setSymbolInstance(&document,layer,timing,&error));QVERIFY(!error.isEmpty());
        document.history()->undo();QCOMPARE(document.activeShot().layers[document.activeLayer()].symbol->startFrame,0);
    }
    void breakApartRetainsEditableArtworkAndInstanceTransform(){
        Document document;document.load(fixture());QString error;const auto id=convert(document,&error);QVERIFY2(!id.isEmpty(),qPrintable(error));
        const auto layer=document.activeShot().layers[0].id;
        QVERIFY(document.enableKeyframeChannel(layer,KeyframeProperty::PositionX));QVERIFY(document.setKeyframeValue(layer,KeyframeProperty::PositionX,25));
        const auto before=renderMovieFrame(document.project(),0);
        QVERIFY2(breakApartSymbolInstance(&document,layer,&error),qPrintable(error));QCOMPARE(document.activeShot().layers.size(),2);
        QVERIFY(document.activeShot().layers[0].group);QVERIFY(!document.activeShot().layers[0].symbol);
        QCOMPARE(document.activeShot().layers[1].parentId,layer);QCOMPARE(document.activeShot().layers[1].kind,LayerKind::Raster);
        QCOMPARE(renderMovieFrame(document.project(),0),before);QCOMPARE(symbolUseCount(document.project(),id),0);
        document.history()->undo();QVERIFY(document.activeShot().layers[0].symbol.has_value());QCOMPARE(renderMovieFrame(document.project(),0),before);
    }
    void breakApartSamplesAnimatedPoseAndRetainsObjects(){
        auto project=fixture();auto &layer=project.shots[0].layers[0];
        project.shots[0].layerAnimations={{layer.id,{},{{KeyframeProperty::PositionX,{{0,0},{5,30}}},{KeyframeProperty::Opacity,{{0,1},{5,.5}}}}}};
        Document document;document.load(project);QString error;const auto id=convert(document,&error);QVERIFY2(!id.isEmpty(),qPrintable(error));
        QVERIFY(document.setCurrentFrame(5));const auto before=renderMovieFrame(document.project(),5);
        QVERIFY2(breakApartSymbolInstance(&document,document.activeShot().layers[0].id,&error),qPrintable(error));
        QCOMPARE(renderMovieFrame(document.project(),5),before);QVERIFY(!document.activeShot().layers[1].tiles.isEmpty());
        QCOMPARE(renderMovieFrame(document.project(),8),before);QVERIFY2(document.project().validate().isEmpty(),qPrintable(document.project().validate()));
    }
    void breakApartKeepsVectorPathsEditable(){
        auto project=fixture();auto &source=project.shots[0].layers[0];source.kind=LayerKind::Vector;source.tiles.clear();
        auto path=EditableObject::create();path.nodes={BezierNode::corner({10,10}),BezierNode::corner({30,30})};path.strokeWidth=5;source.objects={path};
        QString error;QVERIFY(rebuildObjectLayer(source,QRectF(0,0,128,128),maxProjectTiles,&error));
        Document document;document.load(project);const auto id=convert(document,&error);QVERIFY2(!id.isEmpty(),qPrintable(error));
        const auto before=renderMovieFrame(document.project(),0);
        QVERIFY2(breakApartSymbolInstance(&document,document.activeShot().layers[0].id,&error),qPrintable(error));
        const auto &copy=document.activeShot().layers[1];QCOMPARE(copy.kind,LayerKind::Vector);QCOMPARE(copy.objects.size(),1);
        QVERIFY(copy.objects[0].id!=path.id);QCOMPARE(copy.objects[0].nodes,path.nodes);QCOMPARE(renderMovieFrame(document.project(),0),before);
    }
    void activeGestureRejectsLibraryMutation(){
        Document document;document.load(fixture());BrushSettings brush;brush.size=5;
        QVERIFY(document.beginStroke({20,20},1,brush,false));QString error;
        QVERIFY(createEmptySymbol(&document,"Blocked",&error).isEmpty());QVERIFY(error.contains("Finish"));
        document.cancelStroke();QVERIFY(document.project().symbols.isEmpty());QCOMPARE(document.history()->count(),0);
    }
};
QTEST_MAIN(SymbolDocumentTests)
#include "test_symbols_document.moc"
