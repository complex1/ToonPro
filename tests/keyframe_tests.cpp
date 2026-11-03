#include "project.h"
#include <QtTest>
using namespace toon;
namespace {
Project fixture(){auto p=Project::create();p.width=p.height=128;p.shots[0].durationFrames=24;auto&l=p.shots[0].layers[0];l.kind=LayerKind::Vector;auto o=EditableObject::create();o.nodes={BezierNode::corner({10,10}),BezierNode::corner({40,30})};o.strokeWidth=4;l.objects={o};return p;}
}
class KeyframeTests:public QObject{
Q_OBJECT
private slots:
void scalarInterpolationAndEndpointHold(){auto p=fixture();auto&s=p.shots[0];const auto id=s.layers[0].id;s.layerAnimations={{id,{},{{KeyframeProperty::PositionX,{{2,10},{10,90}}}}, {}}};QVERIFY(validatePropertyAnimation(s).isEmpty());QCOMPARE(evaluateProperty(s,id,KeyframeProperty::PositionX,0),10.0);QCOMPARE(evaluateProperty(s,id,KeyframeProperty::PositionX,6),50.0);QCOMPARE(evaluateProperty(s,id,KeyframeProperty::PositionX,23),90.0);QCOMPARE(evaluateProperty(s,id,KeyframeProperty::ScaleX,4),1.0);auto&key=s.layerAnimations[0].channels[0].keys[0];key.interpolation=KeyframeInterpolation::Hold;QCOMPARE(evaluateProperty(s,id,KeyframeProperty::PositionX,9.99),10.0);QCOMPARE(evaluateProperty(s,id,KeyframeProperty::PositionX,10),90.0);key.interpolation=KeyframeInterpolation::EaseIn;QCOMPARE(evaluateProperty(s,id,KeyframeProperty::PositionX,6),30.0);key.interpolation=KeyframeInterpolation::EaseOut;QCOMPARE(evaluateProperty(s,id,KeyframeProperty::PositionX,6),70.0);key.interpolation=KeyframeInterpolation::EaseInOut;QCOMPARE(evaluateProperty(s,id,KeyframeProperty::PositionX,6),50.0);}
void defaultPivotUsesPaintedPixelsAndNestedGroupContents() {
    auto p=Project::create();auto &shot=p.shots[0];
    QImage pixels(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);pixels.fill(Qt::transparent);
    for(int y=8;y<24;++y)for(int x=8;x<24;++x)pixels.setPixelColor(x,y,Qt::red);
    const auto leafId=shot.layers[0].id;shot.layers[0].tiles.insert({0,0},pixels);
    QImage empty(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);empty.fill(Qt::transparent);
    shot.layers[0].tiles.insert({10,10},empty);
    QCOMPARE(defaultAnimationPivot(shot,leafId,QSizeF(1920,1080)),QPointF(16,16));
    auto group=RasterLayer::create("Animated group");group.group=true;
    auto nested=RasterLayer::create("Nested");nested.group=true;nested.parentId=group.id;
    shot.layers[0].parentId=nested.id;shot.layers.append(group);shot.layers.append(nested);
    auto other=RasterLayer::create("Other group content");other.parentId=group.id;
    pixels.fill(Qt::transparent);for(int y=8;y<24;++y)for(int x=240;x<248;++x)pixels.setPixelColor(x,y,Qt::blue);
    other.tiles.insert({-1,0},pixels);shot.layers.append(other);
    auto unrelated=RasterLayer::create("Unrelated");unrelated.tiles.insert({8,8},pixels);shot.layers.append(unrelated);
    QCOMPARE(defaultAnimationPivot(shot,group.id,QSizeF(1920,1080)),QPointF(4,16));
    auto blank=RasterLayer::create();shot.layers.append(blank);
    QCOMPARE(defaultAnimationPivot(shot,blank.id,QSizeF(1920,1080)),QPointF(960,540));
}
void transformPivotAndAncestorOrder(){auto p=fixture();auto&s=p.shots[0];auto group=RasterLayer::create("Group");group.group=true;s.layers[0].parentId=group.id;s.layers.append(group);const auto id=s.layers[0].id;s.layerAnimations={{id,{10,10},{{KeyframeProperty::ScaleX,{{0,2}}},{KeyframeProperty::ScaleY,{{0,2}}}},{}},{group.id,{},{{KeyframeProperty::Rotation,{{0,90}}},{KeyframeProperty::PositionX,{{0,100}}}}, {}}};QCOMPARE(evaluateLayerTransform(s,id,0).map(QPointF(12,10)),QPointF(14,10));const auto result=worldLayerTransform(s,id,0).map(QPointF(12,10));QVERIFY(QLineF(result,{90,14}).length()<.00001);}
void morphInterpolatesGeometryStyleWithoutChangingSource(){auto p=fixture();auto&s=p.shots[0];const auto id=s.layers[0].id;const auto original=s.layers[0].objects[0];auto a=pathPose(original),b=a;b.nodes[1].anchor={60,50};b.nodes[1].inControl={70,50};b.nodes[1].outControl={80,50};b.widthFactors={2,4};b.stroke=Qt::red;b.opacity=.5;b.strokeWidth=12;b.transform.translate(20,0);s.layerAnimations={{id,{}, {},{{original.id,{{0,a},{10,b}}}}}};QVERIFY2(validatePropertyAnimation(s).isEmpty(),qPrintable(validatePropertyAnimation(s)));const auto evaluated=evaluateAnimatedObject(s,id,original,5);QCOMPARE(evaluated.nodes[1].anchor,QPointF(50,40));QCOMPARE(evaluated.nodes[1].inControl,QPointF(55,40));QCOMPARE(evaluated.widthFactors,QVector<qreal>({1.5,2.5}));QCOMPARE(evaluated.strokeWidth,8.0);QCOMPARE(evaluated.opacity,.75);QCOMPARE(evaluated.transform.dx(),10.0);QCOMPARE(s.layers[0].objects[0],original);QCOMPARE(evaluateAnimatedObject(s,id,original,23).nodes,b.nodes);}
void pathRotationUsesAnglesRatherThanCollapsingMatrices(){auto p=fixture();auto&s=p.shots[0];const auto&id=s.layers[0].id;const auto source=s.layers[0].objects[0];auto first=pathPose(source),last=first;last.transform.rotate(180);s.layerAnimations={{id,{}, {},{{source.id,{{0,first},{10,last}}}}}};QVERIFY(validatePropertyAnimation(s).isEmpty());auto midpoint=evaluateAnimatedObject(s,id,source,5);QVERIFY(midpoint.transform.isInvertible());QVERIFY(QLineF(midpoint.transform.map(QPointF(10,0)),QPointF(0,10)).length()<.00001);last.transform=QTransform::fromScale(-1,1);s.layerAnimations[0].paths[0].keys[1].pose=last;QVERIFY(!validatePropertyAnimation(s).isEmpty());s.layerAnimations[0].paths[0].keys[0].interpolation=KeyframeInterpolation::Hold;QVERIFY(validatePropertyAnimation(s).isEmpty());}
void topologyAndMalformedDataRejected(){auto p=fixture();auto&s=p.shots[0];const auto id=s.layers[0].id;const auto o=s.layers[0].objects[0];s.layerAnimations={{id,{}, {},{{o.id,{{0,pathPose(o)}}}}}};auto original=s;auto changed=o;changed.nodes.append(BezierNode::corner({90,90}));QString error;QVERIFY(!writePathKeyframe(s,id,o.id,5,changed,&error));QCOMPARE(s.layerAnimations,original.layerAnimations);s.layerAnimations[0].paths[0].keys[0].pose.closed=true;QVERIFY(!validatePropertyAnimation(s).isEmpty());s=original;s.layerAnimations[0].paths[0].keys[0].frame=24;QVERIFY(!validatePropertyAnimation(s).isEmpty());s=original;s.layers[0].objects[0].nodes[0].anchor.setX(20);QVERIFY(!validatePropertyAnimationMutation(original,s).isEmpty());s=original;s.layers[0].annotation=true;QVERIFY(!validatePropertyAnimation(s).isEmpty());auto json=layerAnimationsToJson(original.layerAnimations);auto layer=json[0].toObject();layer["unexpected"]=1;json[0]=layer;QVector<LayerAnimation> parsed;QVERIFY(!layerAnimationsFromJson(json,&parsed,&error));}
void persistenceMetadataIsStrictAndBounded(){auto p=fixture();auto&s=p.shots[0];const auto id=s.layers[0].id;s.layerAnimations={{id,{},{{KeyframeProperty::Opacity,{{0,1},{12,.25,KeyframeInterpolation::EaseInOut}}}}, {}}};QString error;QVector<LayerAnimation> parsed;QVERIFY(layerAnimationsFromJson(layerAnimationsToJson(s.layerAnimations),&parsed,&error));QCOMPARE(parsed,s.layerAnimations);auto copy=s;copy.layerAnimations[0].channels[0].keys.append({12,.8});QVERIFY(!validatePropertyAnimation(copy).isEmpty());copy=s;copy.layerAnimations[0].channels[0].keys[0].value=qQNaN();QVERIFY(!validatePropertyAnimation(copy).isEmpty());copy=s;copy.layerAnimations[0].layerId="missing";QVERIFY(!validatePropertyAnimation(copy).isEmpty());QVERIFY(propertyAnimationHistoryCost(s,s)==0);QVERIFY(propertyAnimationHistoryCost(s,copy)>0);}
};
QTEST_MAIN(KeyframeTests)
#include "keyframe_tests.moc"
