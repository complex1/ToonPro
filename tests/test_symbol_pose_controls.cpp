#include "animation_test_fixture.h"
#include "canvas.h"
#include "document.h"
#include "symbol_clip_panel.h"
#include "symbol_library_panel.h"
#include "symbol_poses.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QDir>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSlider>
#include <QStyle>
#include <QTabBar>
#include <QToolButton>
#include <QtTest>
#include <limits>
using namespace toon;
namespace {
Project fixture() {
    auto p=animation_fixture::project();auto source=animation_fixture::project().shots[0];
    const auto definition=animation_fixture::id();p.symbols.append({definition,"Mouth poses",source});p.shots[0].durationFrames=48;
    auto layer=RasterLayer::create("Mouth");layer.kind=LayerKind::Symbol;
    layer.symbol=SymbolInstance{definition,SymbolPlayback::Poses,4,1,1.5,30,2};layer.symbol->poseKeys={{2,1},{8,5},{16,8}};
    p.shots[0].layers.append(layer);
    TimelineCue cue;cue.id=animation_fixture::id();cue.name="Speech";cue.startFrame=4;cue.durationFrames=30;cue.kind=TimelineCueKind::Dialogue;p.shots[0].timelineCues={cue};
    p.shots[0].layerAnimations={{layer.id,{},{{KeyframeProperty::PositionX,{{0,0},{12,20}}}}}};
    return p;
}
QString id(const Canvas &canvas){return canvas.shot().layers.last().id;}
SymbolInstance instance(const Canvas &canvas){return *canvas.shot().layers.last().symbol;}
TimelineKeyRef ref(const QString &layer,int frame){return {layer,KeyframeProperty::PositionX,{},frame,TimelineKeyKind::SymbolPose};}
ValueSlider *field(QWidget &panel,const char *name){return panel.findChild<ValueSlider *>(name);}
void enter(ValueSlider *value,double number){value->setValue(number);QMetaObject::invokeMethod(value,"editingFinished");}
}
class SymbolPoseControlsTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();QVERIFY2(fixture().validate().isEmpty(),qPrintable(fixture().validate()));}
    void poseSelectionIsSeparateAndNavigationKeepsClock(){
        Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);const auto layer=id(canvas);
        QVERIFY(c.selectSymbolClip(layer));QVERIFY(c.selectSymbolPoseLayer(layer));QVERIFY(c.selectedSymbolClipId().isEmpty());QCOMPARE(c.selectedSymbolPoseLayerId(),layer);
        QVERIFY(c.setFrame(4));QVERIFY(c.stepSymbolPose(1));QCOMPARE(c.currentFrame(),10);QCOMPARE(c.selectedKeys(),QVector<TimelineKeyRef>{ref(layer,10)});
        QVERIFY(c.stepSymbolPose(1));QCOMPARE(c.currentFrame(),18);QVERIFY(c.stepSymbolPose(-1));QCOMPARE(c.currentFrame(),10);
        QVERIFY(c.selectCue(canvas.shot().timelineCues.first().id));QVERIFY(c.selectedSymbolPoseLayerId().isEmpty());QVERIFY(c.selectedKeys().isEmpty());
        QVERIFY(c.selectSymbolPoseLayer(layer));QVERIFY(c.selectedCueId().isEmpty());QVERIFY(c.selectSymbolClip(layer));QVERIFY(c.selectedSymbolPoseLayerId().isEmpty());
        QCOMPARE(canvas.document()->history()->count(),0);
    }
    void poseEditsAreSingleUndoPreserveClipAndNoOpSafe(){
        Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);const auto layer=id(canvas);const auto before=instance(canvas);QVERIFY(c.selectSymbolPoseLayer(layer));
        QVERIFY(c.setSelectedSymbolPose(12,6));auto after=instance(canvas);QCOMPARE(after.startFrame,before.startFrame);QCOMPARE(after.durationFrames,before.durationFrames);QCOMPARE(after.playbackOffset,before.playbackOffset);QCOMPARE(after.speed,before.speed);
        QCOMPARE(symbolPoseKeyAtParent(after,12)->poseFrame,6);QCOMPARE(canvas.document()->history()->count(),1);
        QVERIFY(c.setSelectedSymbolPose(12,6));QCOMPARE(canvas.document()->history()->count(),1);canvas.document()->history()->undo();QCOMPARE(instance(canvas),before);
        canvas.document()->history()->redo();QVERIFY(c.removeSelectedSymbolPose(12));QVERIFY(!symbolPoseKeyAtParent(instance(canvas),12));canvas.document()->history()->undo();QVERIFY(symbolPoseKeyAtParent(instance(canvas),12));
        QVERIFY(!c.moveSelectedSymbolPose(std::numeric_limits<int>::min(),std::numeric_limits<int>::max()));
    }
    void genericKeyEditingNeverTouchesTransformOrClip(){
        Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);const auto layer=id(canvas);QVERIFY(c.selectSymbolPoseLayer(layer));
        const auto motion=canvas.shot().layerAnimations; c.setSelectedKeys({ref(layer,10),ref(layer,18)});QCOMPARE(c.selectedKeys().size(),2);
        QVERIFY(c.moveSelectedKeys(2));QVERIFY(symbolPoseKeyAtParent(instance(canvas),12));QVERIFY(symbolPoseKeyAtParent(instance(canvas),20));
        QVERIFY(c.copySelectedKeys());QVERIFY(c.setFrame(26));QVERIFY(c.pasteKeys());QVERIFY(symbolPoseKeyAtParent(instance(canvas),26));QVERIFY(symbolPoseKeyAtParent(instance(canvas),34));
        const auto before=instance(canvas);QVERIFY(!c.moveSelectedKeys(-14));QCOMPARE(instance(canvas),before);
        QVERIFY(c.deleteSelectedKeys());QVERIFY(!symbolPoseKeyAtParent(instance(canvas),26));QCOMPARE(canvas.shot().layerAnimations,motion);QCOMPARE(instance(canvas).durationFrames,30);
    }
    void propertiesEditPoseTimingAndPreserveFallbackSlip(){
        Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);QVERIFY(c.selectSymbolPoseLayer(id(canvas)));QVERIFY(c.setFrame(10));SymbolClipPanel panel(&c);
        QVERIFY(!field(panel,"symbolClipSpeed")->isEnabled());QVERIFY(field(panel,"symbolClipSlip")->isEnabled());
        enter(field(panel,"symbolPoseFrame"),8);QCOMPARE(symbolPoseKeyAtParent(instance(canvas),10)->poseFrame,7);QCOMPARE(canvas.document()->history()->count(),1);
        enter(field(panel,"symbolPoseTime"),13);QVERIFY(!symbolPoseKeyAtParent(instance(canvas),10));QCOMPARE(symbolPoseKeyAtParent(instance(canvas),12)->poseFrame,7);
        const auto keys=instance(canvas).poseKeys;enter(field(panel,"symbolClipFirstFrame"),4);QCOMPARE(instance(canvas).firstFrame,3);QCOMPARE(instance(canvas).playbackOffset,2);QCOMPARE(instance(canvas).poseKeys,keys);
        panel.findChild<QTabBar *>("symbolClipPlayback")->setCurrentIndex(int(SymbolPlayback::Loop));QVERIFY(c.selectedSymbolPoseLayerId().isEmpty());QCOMPARE(c.selectedSymbolClipId(),id(canvas));QCOMPARE(instance(canvas).poseKeys,keys);
    }
    void draftsSurviveRefreshAndRejectStaleSource(){
        Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);QVERIFY(c.selectSymbolPoseLayer(id(canvas)));QVERIFY(c.setFrame(10));SymbolClipPanel panel(&c);panel.resize(240,720);panel.show();QTest::qWait(10);
        auto *number=field(panel,"symbolPoseFrame")->findChild<QDoubleSpinBox *>();auto *line=number->findChild<QLineEdit *>();line->selectAll();QTest::keyClicks(number,"9");
        QVERIFY(c.hasPendingEdits());emit c.changed();QCOMPARE(line->text(),QString("9"));QCOMPARE(symbolPoseKeyAtParent(instance(canvas),10)->poseFrame,5);
        QTest::keyClick(number,Qt::Key_Return);QCOMPARE(symbolPoseKeyAtParent(instance(canvas),10)->poseFrame,8);QCOMPARE(canvas.document()->history()->count(),1);
        line->selectAll();QTest::keyClicks(number,"7");QVERIFY(c.hasPendingEdits());QVERIFY(setSymbolPose(canvas.document(),id(canvas),10,4));
        QVERIFY(!c.hasPendingEdits());c.finishEditing();QCOMPARE(symbolPoseKeyAtParent(instance(canvas),10)->poseFrame,4);
    }
    void locksReloadAndInactiveDrawingSelectionAreSafe(){
        Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);const auto layer=id(canvas);QVERIFY(c.selectSymbolPoseLayer(layer));
        canvas.document()->setLayersLocked({layer},true);SymbolClipPanel panel(&c);QVERIFY(!field(panel,"symbolPoseFrame")->isEnabled());QVERIFY(!c.setSelectedSymbolPose(10,4));
        canvas.document()->setLayersLocked({layer},false);QVERIFY(c.setFrame(10));c.setSelectedKeys({ref(layer,10)});canvas.setProject(fixture());QVERIFY(c.selectedSymbolPoseLayerId().isEmpty());QVERIFY(c.selectedKeys().isEmpty());
    }
    void hiddenExactKeysStayEditableWhileNewOutsideKeysAreBlocked(){
        Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);const auto layer=id(canvas);
        QVERIFY(c.selectSymbolPoseLayer(layer));QVERIFY(setSymbolClipTiming(canvas.document(),layer,12,22,10));
        QVERIFY(c.setFrame(12));QVERIFY(c.stepSymbolPose(-1));QCOMPARE(c.currentFrame(),10);
        SymbolClipPanel panel(&c);QVERIFY(field(panel,"symbolPoseFrame")->isEnabled());QVERIFY(panel.findChild<QToolButton *>("symbolPosePick")->isEnabled());
        enter(field(panel,"symbolPoseFrame"),7);QCOMPARE(symbolPoseKeyAtParent(instance(canvas),10)->poseFrame,6);
        QVERIFY(c.setFrame(9));QVERIFY(!field(panel,"symbolPoseFrame")->isEnabled());QVERIFY(!panel.findChild<QToolButton *>("symbolPosePick")->isEnabled());
        QVERIFY(!c.setSelectedSymbolPose(9,3));QVERIFY(!symbolPoseKeyAtParent(instance(canvas),9));
    }
    void resolvingSlipDraftRejectsStalePoseActions(){
        for(int action=0;action<3;++action){
            Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);QVERIFY(c.selectSymbolPoseLayer(id(canvas)));QVERIFY(c.setFrame(10));
            SymbolClipPanel panel(&c);panel.resize(240,720);panel.show();QTest::qWait(10);
            auto *number=field(panel,"symbolClipSlip")->findChild<QDoubleSpinBox *>();auto *line=number->findChild<QLineEdit *>();line->selectAll();QTest::keyClicks(number,"5");QVERIFY(c.hasPendingEdits());
            const auto keys=instance(canvas).poseKeys;
            const bool changed=action==0?c.setSelectedSymbolPose(10,9):action==1?c.removeSelectedSymbolPose(10):c.moveSelectedSymbolPose(10,11);
            QVERIFY(!changed);QCOMPARE(instance(canvas).playbackOffset,5);QCOMPARE(instance(canvas).poseKeys,keys);QCOMPARE(canvas.document()->history()->count(),1);
        }
    }
    void fiveTabsAndActionsFitNarrowLargerText(){
        Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);QVERIFY(c.setFrame(10));QVERIFY(c.selectSymbolPoseLayer(id(canvas)));
        SymbolClipPanel clip(&c);SymbolLibraryPanel library(&canvas,&c);clip.resize(240,800);library.resize(240,900);clip.show();library.show();
        for(int pass=0;pass<2;++pass){
            if(pass){for(QWidget *panel:{static_cast<QWidget *>(&clip),static_cast<QWidget *>(&library)}){auto f=panel->font();f.setPointSize(16);panel->setFont(f);}}
            QTest::qWait(10);
            for(auto pair:{qMakePair(static_cast<QWidget *>(&clip),QString("symbolClipPlayback")),qMakePair(static_cast<QWidget *>(&library),QString("symbolPlayback"))}){
                auto *tabs=pair.first->findChild<QTabBar *>(pair.second);QCOMPARE(tabs->count(),5);
                for(int i=0;i<5;++i){QVERIFY2(tabs->rect().contains(tabs->tabRect(i)),qPrintable(QString("%1 pass%2 tab%3 width%4 right%5").arg(pair.second).arg(pass).arg(i).arg(tabs->width()).arg(tabs->tabRect(i).right())));QVERIFY(!tabs->tabIcon(i).isNull());QVERIFY(!tabs->tabToolTip(i).isEmpty());}
            }
            for(const char *name:{"symbolPosePick","symbolPosePrevious","symbolPoseDelete","symbolPoseNext"}){auto *b=clip.findChild<QToolButton *>(name);QVERIFY(!b->icon().isNull());QCOMPARE(b->size(),QSize(28,28));QVERIFY(!b->accessibleName().isEmpty());QVERIFY(!b->toolTip().isEmpty());QCOMPARE(b->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,b),600);}
        }
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");if(!output.isEmpty()){QDir().mkpath(output);QVERIFY(clip.grab().save(output+"/symbol-poses-properties-240.png"));QVERIFY(library.grab().save(output+"/symbol-poses-library-240.png"));}
    }
};
QTEST_MAIN(SymbolPoseControlsTests)
#include "test_symbol_pose_controls.moc"
