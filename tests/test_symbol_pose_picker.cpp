#include "animation_test_fixture.h"
#include "canvas.h"
#include "document.h"
#include "symbol_frame_picker_dialog.h"
#include "symbol_poses.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QtTest>
using namespace toon;
namespace {
Project fixture(){
    auto p=Project::create();p.width=p.height=128;p.shots[0].durationFrames=48;
    auto content=animation_fixture::project().shots[0];const auto sid=animation_fixture::id();p.symbols={{sid,"Mouth poses",content}};
    auto layer=RasterLayer::create("Mouth");layer.kind=LayerKind::Symbol;layer.symbol=SymbolInstance{sid,SymbolPlayback::Poses,4,1,1.5,30,2};layer.symbol->poseKeys={{2,1},{8,5},{16,8}};
    p.shots[0].layers={layer};return p;
}
SymbolInstance instance(Canvas &canvas){return *canvas.shot().layers[0].symbol;}
QString layer(Canvas &canvas){return canvas.shot().layers[0].id;}
ValueSlider *frame(SymbolFramePickerDialog &dialog){return dialog.findChild<ValueSlider *>("symbolFramePickerFrame");}
QPushButton *apply(SymbolFramePickerDialog &dialog){return dialog.findChild<QPushButton *>("symbolFramePickerApply");}
}
class SymbolPosePickerTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();QVERIFY2(fixture().validate().isEmpty(),qPrintable(fixture().validate()));}
    void timedPickerChangesOnePoseAndKeepsClipClock(){
        Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);QVERIFY(c.setFrame(12));const auto before=instance(canvas);
        SymbolFramePickerDialog dialog(&c,layer(canvas),nullptr,SymbolFramePickerMode::TimedPose);dialog.show();
        QVERIFY(dialog.findChild<QCheckBox *>("symbolFramePickerHold")->isHidden());QCOMPARE(frame(dialog)->value(),6.);frame(dialog)->setValue(14);apply(dialog)->click();
        QCOMPARE(dialog.result(),int(QDialog::Accepted));const auto after=instance(canvas);QCOMPARE(after.startFrame,before.startFrame);QCOMPARE(after.durationFrames,before.durationFrames);QCOMPARE(after.playbackOffset,before.playbackOffset);QCOMPARE(after.speed,before.speed);
        QCOMPARE(after.poseKeys.size(),before.poseKeys.size()+1);QCOMPARE(symbolPoseKeyAtParent(after,12)->poseFrame,13);QCOMPARE(canvas.document()->history()->count(),1);QCOMPARE(c.selectedSymbolPoseLayerId(),layer(canvas));
        canvas.document()->history()->undo();QCOMPARE(instance(canvas),before);
    }
    void cancelAndCapturedPlayheadChangesDoNotWrite(){
        Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);QVERIFY(c.setFrame(12));const auto before=canvas.project().toJson();
        {SymbolFramePickerDialog dialog(&c,layer(canvas),nullptr,SymbolFramePickerMode::TimedPose);frame(dialog)->setValue(14);dialog.reject();}
        QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),0);
        SymbolFramePickerDialog stale(&c,layer(canvas),nullptr,SymbolFramePickerMode::TimedPose);QVERIFY(c.setFrame(13));QVERIFY(!apply(stale)->isEnabled());apply(stale)->click();QCOMPARE(canvas.project().toJson(),before);
    }
    void explicitTargetFrameAndSourceReplacementGuards(){
        Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);QVERIFY(c.setFrame(5));
        SymbolFramePickerDialog dialog(&c,layer(canvas),nullptr,SymbolFramePickerMode::TimedPose,10);frame(dialog)->setValue(9);apply(dialog)->click();QCOMPARE(symbolPoseKeyAtParent(instance(canvas),10)->poseFrame,8);QCOMPARE(c.currentFrame(),5);
        SymbolFramePickerDialog stale(&c,layer(canvas),nullptr,SymbolFramePickerMode::TimedPose,10);QVERIFY(setSymbolPose(canvas.document(),layer(canvas),10,3));QVERIFY(!apply(stale)->isEnabled());frame(stale)->setValue(12);apply(stale)->click();QCOMPARE(symbolPoseKeyAtParent(instance(canvas),10)->poseFrame,3);
    }
    void clipPickerChangesFallbackWithoutRetimingPoses(){
        Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);const auto before=instance(canvas);
        SymbolFramePickerDialog dialog(&c,layer(canvas));frame(dialog)->setValue(12);apply(dialog)->click();QCOMPARE(instance(canvas).firstFrame,11);QCOMPARE(instance(canvas).playbackOffset,before.playbackOffset);QCOMPARE(instance(canvas).poseKeys,before.poseKeys);QCOMPARE(instance(canvas).playback,SymbolPlayback::Poses);
        SymbolFramePickerDialog still(&c,layer(canvas));still.findChild<QCheckBox *>("symbolFramePickerHold")->setChecked(true);apply(still)->click();QCOMPARE(instance(canvas).playback,SymbolPlayback::SingleFrame);QCOMPARE(instance(canvas).poseKeys,before.poseKeys);
    }
    void locksAndOutsideClipPreventApplication(){
        Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);QVERIFY(c.setFrame(0));
        SymbolFramePickerDialog outside(&c,layer(canvas),nullptr,SymbolFramePickerMode::TimedPose);QVERIFY(!apply(outside)->isEnabled());
        QVERIFY(c.setFrame(10));canvas.document()->setLayersLocked({layer(canvas)},true);SymbolFramePickerDialog locked(&c,layer(canvas),nullptr,SymbolFramePickerMode::TimedPose);QVERIFY(!apply(locked)->isEnabled());
        const int count=canvas.document()->history()->count();apply(locked)->click();QCOMPARE(canvas.document()->history()->count(),count);
    }
    void hiddenExactKeyCanBeReplacedButOutsideNewKeyCannot(){
        Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);QVERIFY(setSymbolClipTiming(canvas.document(),layer(canvas),12,22,10));
        QVERIFY(c.setFrame(10));SymbolFramePickerDialog existing(&c,layer(canvas),nullptr,SymbolFramePickerMode::TimedPose);QVERIFY(apply(existing)->isEnabled());
        frame(existing)->setValue(10);apply(existing)->click();QCOMPARE(existing.result(),int(QDialog::Accepted));QCOMPARE(symbolPoseKeyAtParent(instance(canvas),10)->poseFrame,9);
        QVERIFY(c.setFrame(9));SymbolFramePickerDialog outside(&c,layer(canvas),nullptr,SymbolFramePickerMode::TimedPose);QVERIFY(!apply(outside)->isEnabled());
        const int history=canvas.document()->history()->count();apply(outside)->click();QCOMPARE(canvas.document()->history()->count(),history);
    }
    void thumbnailsAndCompactPosePicker(){
        Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);QVERIFY(c.setFrame(10));SymbolFramePickerDialog dialog(&c,layer(canvas),nullptr,SymbolFramePickerMode::TimedPose);dialog.resize(320,500);dialog.show();QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto *frames=dialog.findChild<QListWidget *>("symbolFramePickerFrames");QCOMPARE(frames->count(),12);
        const int selectedRow=frames->currentRow(),scrollPosition=frames->verticalScrollBar()->value();
        QElapsedTimer thumbnailWait;thumbnailWait.start();
        // Thumbnails render lazily. Visit the whole page without changing the
        // chosen pose, then restore the compact viewport before capturing it.
        for(int i=0;i<frames->count();++i){
            auto *item=frames->item(i);frames->scrollToItem(item,QAbstractItemView::PositionAtCenter);
            QTRY_VERIFY_WITH_TIMEOUT(item->data(Qt::UserRole+1).toBool(),qMax<qint64>(1,15000-thumbnailWait.elapsed()));
            QVERIFY2(item->data(Qt::UserRole+2).toString().isEmpty(),qPrintable(item->data(Qt::UserRole+2).toString()));
            QVERIFY(!item->icon().isNull());
        }
        QCOMPARE(frames->count(),12);QCOMPARE(frames->currentRow(),selectedRow);frames->verticalScrollBar()->setValue(scrollPosition);
        QVERIFY(dialog.rect().contains(QRect(frame(dialog)->mapTo(&dialog,QPoint()),frame(dialog)->size())));
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");if(!output.isEmpty()){QDir().mkpath(output);QVERIFY(dialog.grab().save(output+"/symbol-timed-pose-picker.png"));}
        dialog.reject();QCOMPARE(canvas.document()->history()->count(),0);
    }
};
QTEST_MAIN(SymbolPosePickerTests)
#include "test_symbol_pose_picker.moc"
