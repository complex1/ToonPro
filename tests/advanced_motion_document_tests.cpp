#include "document.h"
#include "motion_edit.h"
#include "motion_controls.h"
#include <QtTest>
#include <QTemporaryDir>
using namespace toon;
namespace {
Project fixture(){
    auto p=Project::create();p.width=p.height=128;auto &s=p.shots[0];s.durationFrames=80;
    s.layerAnimations={{s.layers[0].id,{16,16},{{KeyframeProperty::PositionX,{{0,0},{20,80},{40,120}}},{KeyframeProperty::PositionY,{{0,0},{20,40},{40,0}}}}, {}}};return p;
}
const LayerAnimation &animation(const Document &d){return d.project().shots[0].layerAnimations[0];}
}
class AdvancedMotionDocumentTests:public QObject {
    Q_OBJECT
private slots:
    void curveHandleUndoAndPersistence(){
        auto p=fixture();const auto id=p.shots[0].layers[0].id;Document d;d.load(p);
        QVERIFY(d.setMotionPathCurved(id,true,0));QVERIFY(positionChannelsPaired(animation(d)));
        QVERIFY(d.beginKeyframeEdit(id));QVERIFY(d.previewKeyframeMotionHandle(0,false,{0,80}));QVERIFY(d.previewKeyframeMotionHandle(20,true,{-20,30}));
        d.commitKeyframeEdit();QCOMPARE(d.history()->count(),2);QVERIFY(d.setOrientAlongPath(id,true));
        const auto curve=d.project().toJson();d.history()->undo();QVERIFY(!animation(d).orientAlongPath);d.history()->redo();QCOMPARE(d.project().toJson(),curve);
        QTemporaryDir dir;QString error;QVERIFY2(saveProject(d.project(),dir.filePath("curve.toonproj"),&error),qPrintable(error));
        const auto loaded=loadProject(dir.filePath("curve.toonproj"),&error);QVERIFY2(loaded,qPrintable(error));QCOMPARE(loaded->toJson(),curve);
        QVERIFY(d.beginKeyframeEdit(id));QVERIFY(d.previewKeyframeMotionHandle(0,false,{10,10}));d.cancelKeyframeEdit();QCOMPARE(d.project().toJson(),curve);
    }
    void easingPresetResetPairsAndAtomicTiming(){
        auto p=fixture();const auto id=p.shots[0].layers[0].id;Document d;d.load(p);QVERIFY(d.setMotionPathCurved(id,true,0));
        QVERIFY(d.beginKeyframeEdit(id));QVERIFY(d.previewKeyframeEasing(KeyframeProperty::PositionY,0,CubicEasing{{.1,.8},{.7,.9}}));d.commitKeyframeEdit();
        const auto *x=propertyChannel(d.project().shots[0],id,KeyframeProperty::PositionX),*y=propertyChannel(d.project().shots[0],id,KeyframeProperty::PositionY);
        QCOMPARE(x->keys[0].easing,y->keys[0].easing);QVERIFY(x->keys[0].easing);
        QVERIFY(d.setKeyframeInterpolation(id,KeyframeProperty::PositionY,0,KeyframeInterpolation::Hold));
        x=propertyChannel(d.project().shots[0],id,KeyframeProperty::PositionX);QVERIFY(!x->keys[0].easing);QCOMPARE(x->keys[0].interpolation,KeyframeInterpolation::Hold);
        QVERIFY(d.moveKeyframe(id,KeyframeProperty::PositionY,20,25));QVERIFY(positionChannelsPaired(animation(d)));
        QCOMPARE(propertyChannel(d.project().shots[0],id,KeyframeProperty::PositionX)->keys[1].frame,25);
        const auto before=d.project().toJson();QVERIFY(!d.moveKeyframe(id,KeyframeProperty::PositionX,25,40));QCOMPARE(d.project().toJson(),before);
        QVERIFY(d.removeKeyframe(id,KeyframeProperty::PositionY,25));QVERIFY(positionChannelsPaired(animation(d)));
        QVERIFY(d.enableKeyframeChannel(id,KeyframeProperty::PositionX,false));QVERIFY(!layerAnimation(d.project().shots[0],id));
    }
    void pairedClipboardAndBatchEdits(){
        auto p=fixture();const auto id=p.shots[0].layers[0].id;Document d;d.load(p);QVERIFY(d.setMotionPathCurved(id,true,0));
        TimelineKeyClipboard clip;QVERIFY(d.copyTimelineKeys({{id,KeyframeProperty::PositionY,{},0}},&clip));QCOMPARE(clip.keys.size(),2);
        QVERIFY(d.pasteTimelineKeys(clip,60));QVERIFY(positionChannelsPaired(animation(d)));
        QVERIFY(d.moveTimelineKeys({{id,KeyframeProperty::PositionX,{},60}},5));QVERIFY(positionChannelsPaired(animation(d)));
        QVERIFY(d.removeTimelineKeys({{id,KeyframeProperty::PositionY,{},65}}));QVERIFY(positionChannelsPaired(animation(d)));
    }
    void insertingPositionKeySubdividesExistingCurve(){
        auto p=fixture();const auto id=p.shots[0].layers[0].id;Document d;d.load(p);QVERIFY(d.setMotionPathCurved(id,true,0));
        QVERIFY(d.beginKeyframeEdit(id));QVERIFY(d.previewKeyframeMotionHandle(0,false,{0,90}));QVERIFY(d.previewKeyframeMotionHandle(20,true,{-25,35}));
        QVERIFY(d.previewKeyframeEasing(KeyframeProperty::PositionX,0,CubicEasing{{.15,.05},{.6,.9}}));d.commitKeyframeEdit();
        const auto before=animation(d);const auto point=evaluateAnimatedPosition(before,7);
        QVERIFY(d.setCurrentFrame(7));QVERIFY(d.setKeyframeValue(id,KeyframeProperty::PositionX,point.x()));
        QVERIFY(positionChannelsPaired(animation(d)));
        for(qreal frame=0;frame<=20;frame+=.25)QVERIFY2(QLineF(evaluateAnimatedPosition(before,frame),evaluateAnimatedPosition(animation(d),frame)).length()<.0001,qPrintable(QString::number(frame)));
    }
    void insertedScalarKeyPreservesCustomTiming(){
        auto p=fixture();const auto id=p.shots[0].layers[0].id;Document d;d.load(p);
        QVERIFY(d.setKeyframeValue(id,KeyframeProperty::Opacity,0));QVERIFY(d.setCurrentFrame(20));QVERIFY(d.setKeyframeValue(id,KeyframeProperty::Opacity,1));
        QVERIFY(d.beginKeyframeEdit(id));QVERIFY(d.previewKeyframeEasing(KeyframeProperty::Opacity,0,CubicEasing{{.2,0},{.8,.1}}));d.commitKeyframeEdit();
        const auto before=d.project().shots[0];const qreal value=evaluateProperty(before,id,KeyframeProperty::Opacity,7);
        QVERIFY(d.setCurrentFrame(7));QVERIFY(d.setKeyframeValue(id,KeyframeProperty::Opacity,value));
        for(qreal frame=0;frame<=20;frame+=.25)QVERIFY(qAbs(evaluateProperty(before,id,KeyframeProperty::Opacity,frame)-evaluateProperty(d.project().shots[0],id,KeyframeProperty::Opacity,frame))<.000001);
    }
    void independentAxesNormalizeOnlyWhenCurveEnabled(){
        auto p=fixture();auto &a=p.shots[0].layerAnimations[0];a.channels[1].keys={{0,0},{12,50},{40,0}};
        const auto original=a;const auto id=p.shots[0].layers[0].id;Document d;d.load(p);
        const auto unchanged=d.project().toJson();QVERIFY(d.setMotionPathCurved(id,false,0));QCOMPARE(d.project().toJson(),unchanged);
        QVERIFY(d.setOrientAlongPath(id,true));QCOMPARE(animation(d).channels,original.channels);
        QVERIFY(d.setMotionPathCurved(id,true,0));QVERIFY(positionChannelsPaired(animation(d)));
        for(int frame:{0,12,20,40})QCOMPARE(evaluateAnimatedPosition(animation(d),frame),evaluateAnimatedPosition(original,frame));
    }
    void invalidAndLockedEditsDoNotMutate(){
        auto p=fixture();const auto id=p.shots[0].layers[0].id;Document d;d.load(p);QVERIFY(d.setMotionPathCurved(id,true,0));
        const auto before=d.project().toJson();QVERIFY(d.beginKeyframeEdit(id));
        QVERIFY(!d.previewKeyframeEasing(KeyframeProperty::PositionX,0,CubicEasing{{.8,.2},{.1,.9}}));
        QVERIFY(!d.previewKeyframeMotionHandle(0,false,{1e6,1e6}));d.cancelKeyframeEdit();QCOMPARE(d.project().toJson(),before);
        p.shots[0].layers[0].locked=true;d.load(p);QVERIFY(!d.setMotionPathCurved(id,true,0));QVERIFY(!d.setOrientAlongPath(id,true));
    }
};
QTEST_MAIN(AdvancedMotionDocumentTests)
#include "advanced_motion_document_tests.moc"
