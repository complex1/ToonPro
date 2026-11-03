#include "canvas.h"
#include "document.h"
#include "keyframe_panel.h"
#include "timeline_controller.h"
#include "timeline_panel.h"
#include "timeline_test_geometry.h"
#include "ui_theme.h"
#include <QAbstractScrollArea>
#include <QKeySequence>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTabBar>
#include <QToolButton>
#include <QtTest>
using namespace toon;
namespace {
using Property = KeyframeProperty;
Project fixture() {
    auto project = Project::create(); project.width = project.height = 128;
    auto &shot = project.shots[0]; shot.durationFrames = 40;
    auto &layer = shot.layers[0]; layer.mask = LayerMask{};
    layer.effects.blurEnabled = true; layer.effects.blurRadius = 12;
    shot.layerAnimations = {{layer.id, {}, {
        {Property::BlurRadius, {{0, 12}, {4, 20}, {10, 40}}},
        {Property::EffectsEnabled, {{0, 1, KeyframeInterpolation::Hold}, {8, 0, KeyframeInterpolation::Hold}}},
        {Property::MaskPositionX, {{0, 0}, {4, 30}, {10, 90}}},
        {Property::MaskInverted, {{0, 0, KeyframeInterpolation::Hold}, {6, 1, KeyframeInterpolation::Hold}}}
    }, {}}}; return project;
}
TimelineKeyRef ref(const QString &id, Property property, int frame) { return {id, property, {}, frame}; }
void settle() {
    QCoreApplication::processEvents();
    for (int i=0;i<4;++i) QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);
}
QAbstractScrollArea *show(TimelinePanel &panel) {
    panel.resize(1000,360); panel.show(); settle();
    auto *view=panel.findChild<QAbstractScrollArea *>("timelineGrid");
    if (view) {view->horizontalScrollBar()->setValue(0);view->verticalScrollBar()->setValue(0);}
    return view;
}
QPoint cell(QAbstractScrollArea *view,int frame,int row) {
    return {timeline_test::labelWidth(view)+frame*20+10-view->horizontalScrollBar()->value(),timeline_test::rowCenter(view,row)};
}
void click(QAbstractScrollArea *view,int frame,int row,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
    QTest::mouseClick(view->viewport(),Qt::LeftButton,modifiers,cell(view,frame,row));
}
void move(QAbstractScrollArea *view,QPoint point) {
    QMouseEvent event(QEvent::MouseMove,point,view->viewport()->mapToGlobal(point),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(view->viewport(),&event);
}
QVector<int> frames(const Canvas &canvas,const QString &id,Property property) {
    QVector<int> result; if (const auto *channel=propertyChannel(canvas.shot(),id,property))
        for (const auto &key:channel->keys)result.append(key.frame);
    return result;
}
}
class AnimatedCompositingTimelineTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void channelRowsSelectMatchingInspectorCategoriesAndNavigateKeys() {
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);
        TimelinePanel timeline(&controller);KeyframePanel inspector(&controller);
        inspector.resize(320,800);inspector.show();auto *view=show(timeline);QVERIFY(view);
        auto *categories=inspector.findChild<QTabBar *>("keyframeCategories");QVERIFY(categories);
        QVector<Property> requested;connect(&controller,&TimelineController::propertyChannelRequested,this,[&](Property property){requested.append(property);});
        const auto id=controller.selectedPropertyLayerId();const auto original=canvas.project().toJson();
        click(view,4,1); // Blur radius channel follows the artwork row.
        QCOMPARE(requested.last(),Property::BlurRadius);QCOMPARE(categories->currentIndex(),1);
        QCOMPARE(controller.selectedKeys(),QVector<TimelineKeyRef>({ref(id,Property::BlurRadius,4)}));
        auto *blur=inspector.findChild<QToolButton *>("keyframeSelect_blurRadius");QVERIFY(blur);QVERIFY(blur->isChecked());
        QTest::keyClick(view,Qt::Key_Down);QCOMPARE(requested.last(),Property::EffectsEnabled);
        QCOMPARE(categories->currentIndex(),1);
        click(view,4,3);QCOMPARE(requested.last(),Property::MaskPositionX);QCOMPARE(categories->currentIndex(),2);
        click(view,6,4);QCOMPARE(requested.last(),Property::MaskInverted);QCOMPARE(categories->currentIndex(),2);
        auto *interpolation=inspector.findChild<QTabBar *>("keyframeInterpolation");QVERIFY(interpolation);QVERIFY(!interpolation->isEnabled());
        auto *previous=inspector.findChild<QToolButton *>("keyframePrevious");auto *next=inspector.findChild<QToolButton *>("keyframeNext");
        QVERIFY(previous);QVERIFY(next);previous->click();QCOMPARE(controller.currentFrame(),0);
        next->click();QCOMPARE(controller.currentFrame(),6);
        QCOMPARE(canvas.project().toJson(),original);QCOMPARE(canvas.document()->history()->count(),0);
    }
    void dragMovesMixedEffectAndMaskKeysOnceAndRetainsHeldSemantics() {
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);TimelinePanel timeline(&controller);
        auto *view=show(timeline);QVERIFY(view);const auto id=controller.selectedPropertyLayerId();
        click(view,4,1);click(view,6,4,Qt::ControlModifier);QCOMPARE(controller.selectedKeys().size(),2);
        const auto original=canvas.project().toJson();const int count=canvas.document()->history()->count();
        QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,4,1));move(view,cell(view,7,1));
        QVERIFY(controller.hasPendingEdits());QCOMPARE(canvas.project().toJson(),original);
        QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,cell(view,7,1));
        QVERIFY(!controller.hasPendingEdits());QCOMPARE(canvas.document()->history()->count(),count+1);
        QCOMPARE(frames(canvas,id,Property::BlurRadius),QVector<int>({0,7,10}));
        QCOMPARE(frames(canvas,id,Property::MaskInverted),QVector<int>({0,9}));
        QCOMPARE(frames(canvas,id,Property::MaskPositionX),QVector<int>({0,4,10}));
        QCOMPARE(evaluateProperty(canvas.shot(),id,Property::MaskInverted,8.999),0.0);
        QCOMPARE(evaluateProperty(canvas.shot(),id,Property::MaskInverted,9),1.0);
        QCOMPARE(propertyChannel(canvas.shot(),id,Property::MaskInverted)->keys.last().interpolation,KeyframeInterpolation::Hold);
        QVERIFY(controller.selectedKeys().contains(ref(id,Property::BlurRadius,7)));
        QVERIFY(controller.selectedKeys().contains(ref(id,Property::MaskInverted,9)));
        canvas.document()->history()->undo();QCOMPARE(canvas.project().toJson(),original);
    }
    void clipboardUsesSnapshotValuesAndRelativeTimingAcrossCategories() {
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);const auto id=controller.selectedPropertyLayerId();
        controller.setSelectedKeys({ref(id,Property::BlurRadius,4),ref(id,Property::MaskInverted,6)});
        QVERIFY(controller.copySelectedKeys());QVERIFY(controller.canPasteKeys());
        QVERIFY(controller.setFrame(4));QVERIFY(canvas.document()->setKeyframeValue(id,Property::BlurRadius,70));
        QVERIFY(controller.deleteSelectedKeys());QVERIFY(controller.setFrame(15));
        const int count=canvas.document()->history()->count();QVERIFY(controller.pasteKeys());
        QCOMPARE(canvas.document()->history()->count(),count+1);
        QCOMPARE(controller.selectedKeys(),QVector<TimelineKeyRef>({ref(id,Property::BlurRadius,15),ref(id,Property::MaskInverted,17)}));
        QCOMPARE(propertyChannel(canvas.shot(),id,Property::BlurRadius)->keys.last().value,20.0);
        QCOMPARE(propertyChannel(canvas.shot(),id,Property::MaskInverted)->keys.last().value,1.0);
        QCOMPARE(propertyChannel(canvas.shot(),id,Property::MaskInverted)->keys.last().interpolation,KeyframeInterpolation::Hold);
        QCOMPARE(evaluateProperty(canvas.shot(),id,Property::MaskInverted,16.99),0.0);
        QCOMPARE(evaluateProperty(canvas.shot(),id,Property::MaskInverted,17),1.0);
        const auto pasted=canvas.project().toJson();QVERIFY(!controller.pasteKeys());QCOMPARE(canvas.project().toJson(),pasted);
    }
    void pasteAvailabilityTracksMaskExistenceAndPropertySpecificLocks() {
        Canvas canvas;canvas.setProject(fixture());TimelineController controller(&canvas);const auto id=controller.selectedPropertyLayerId();
        controller.setSelectedKeys({ref(id,Property::BlurRadius,4),ref(id,Property::MaskPositionX,4)});
        QVERIFY(controller.copySelectedKeys());QVERIFY(controller.canPasteKeys());
        canvas.document()->setLayerPositionLocked(true);QVERIFY(!controller.canPasteKeys());
        QVERIFY(controller.setFrame(15));const auto locked=canvas.project().toJson();
        QVERIFY(!controller.pasteKeys());QCOMPARE(canvas.project().toJson(),locked);
        controller.setSelectedKeys({ref(id,Property::BlurRadius,4),ref(id,Property::MaskInverted,6)});
        QVERIFY(controller.copySelectedKeys());QVERIFY(controller.canPasteKeys()); // Neither channel moves the mask.
        canvas.document()->setLayersLocked({id},true);QVERIFY(!controller.canPasteKeys());
        canvas.document()->setLayersLocked({id},false);QVERIFY(controller.canPasteKeys());
        canvas.document()->setLayerPositionLocked(false);QVERIFY(canvas.document()->removeLayerMask());
        QVERIFY(!controller.canPasteKeys());const auto removed=canvas.project().toJson();
        QVERIFY(!controller.pasteKeys());QCOMPARE(canvas.project().toJson(),removed);
        canvas.document()->history()->undo();QVERIFY(controller.canPasteKeys());
        QVERIFY(controller.pasteKeys());
        QCOMPARE(propertyChannel(canvas.shot(),id,Property::MaskInverted)->keys.last().frame,17);
    }
};
QTEST_MAIN(AnimatedCompositingTimelineTests)
#include "animated_compositing_timeline_tests.moc"
