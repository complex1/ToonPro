#include "animation_test_fixture.h"
#include "canvas.h"
#include "document.h"
#include "property_row.h"
#include "timeline_controller.h"
#include "timeline_cue_panel.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSlider>
#include <QTabBar>
#include <QToolButton>
#include <QtTest>

using namespace toon;
namespace {
Project fixture() {
    auto project = animation_fixture::project(); auto &shot = project.shots[0]; shot.durationFrames = 48;
    TimelineCue marker; marker.id = animation_fixture::id(); marker.name = "Turn"; marker.startFrame = 3;
    TimelineCue dialogue; dialogue.id = animation_fixture::id(); dialogue.name = "Greeting"; dialogue.kind = TimelineCueKind::Dialogue;
    dialogue.startFrame = 10; dialogue.durationFrames = 12; dialogue.speaker = "Hana"; dialogue.text = "Welcome home.";
    TimelineCue overlapping = marker; overlapping.id = animation_fixture::id(); overlapping.name = "Key pose";
    overlapping.kind = TimelineCueKind::KeyPose; overlapping.startFrame = 10;
    shot.timelineCues = {marker, dialogue, overlapping};
    const auto ink = shot.animationTracks[0].drawings[0].templateLayers.value("ink");
    shot.layerAnimations = {{ink, {}, {{KeyframeProperty::PositionX, {{0,0},{2,10}}}}}};
    const auto asset = makeAudioAsset("Guide",QByteArray(audioBytesPerSampleFrame * 48000,0));
    shot.audioClips.append({animation_fixture::id(),"Guide",asset,0,0,asset->sampleFrameCount()});
    auto source = Project::create().shots[0]; source.durationFrames = 12;
    const auto symbolId = animation_fixture::id(); project.symbols.append({symbolId,"Mouth",source});
    auto symbol = RasterLayer::create("Mouth symbol"); symbol.kind = LayerKind::Symbol; symbol.symbol = SymbolInstance{symbolId}; shot.layers.append(symbol);
    return project;
}
const TimelineCue &cue(const Canvas &canvas, const QString &id) {
    const auto *value = timelineCue(canvas.shot(),id); if (!value) qFatal("Missing test cue"); return *value;
}
void enter(ValueSlider *field, double value) { field->setValue(value); QMetaObject::invokeMethod(field,"editingFinished"); }
void typeName(QLineEdit *name, const QString &text) { name->setText(text); QMetaObject::invokeMethod(name,"textEdited",Q_ARG(QString,text)); }
}
class TimelineCueControlsTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); QVERIFY2(fixture().validate().isEmpty(),qPrintable(fixture().validate())); }
    void selectionSeeksWithoutEditingAndSurvivesScrubbing() {
        Canvas canvas; TimelineController controller(&canvas); canvas.setProject(fixture());
        canvas.document()->selectLayer(0); const auto layer = controller.selectedPropertyLayerId();
        const auto cues = canvas.shot().timelineCues; const auto before = canvas.project().toJson();
        QVERIFY(controller.selectCue(cues[0].id)); QCOMPARE(controller.currentFrame(),3); QCOMPARE(controller.selectedPropertyLayerId(),layer);
        QVERIFY(controller.beginScrub()); QVERIFY(controller.scrubToFrame(18)); controller.endScrub();
        QCOMPARE(controller.selectedCueId(),cues[0].id); QVERIFY(controller.setFrame(6)); QCOMPARE(controller.selectedCueId(),cues[0].id);
        QVERIFY(controller.stepCue(1)); QCOMPARE(controller.selectedCueId(),cues[1].id); QCOMPARE(controller.currentFrame(),10);
        QVERIFY(controller.stepCue(1)); QCOMPARE(controller.selectedCueId(),cues[2].id); QCOMPARE(controller.currentFrame(),10);
        QVERIFY(!controller.stepCue(1)); QVERIFY(controller.stepCue(-1)); QCOMPARE(controller.selectedCueId(),cues[1].id);
        controller.clearCueSelection(); QVERIFY(controller.setFrame(0)); QVERIFY(controller.stepCue(1)); QCOMPARE(controller.selectedCueId(),cues[0].id);
        QCOMPARE(canvas.project().toJson(),before); QCOMPARE(canvas.document()->history()->count(),0);
    }
    void selectionDomainsAndReloadNeverLeaveStaleCues() {
        Canvas canvas; TimelineController controller(&canvas); const auto project = fixture(); canvas.setProject(project);
        const auto id = canvas.shot().timelineCues[0].id, symbol = canvas.shot().layers.last().id;
        const auto track = canvas.shot().animationTracks[0]; const TimelineCelRef cel{track.id,track.exposures[0].id};
        QVERIFY(controller.selectCue(id)); controller.setSelectedCels({cel}); QVERIFY(controller.selectedCueId().isEmpty());
        QVERIFY(controller.selectCue(id)); QVERIFY(controller.selectedCels().isEmpty());
        QVERIFY(controller.selectSymbolClip(symbol)); QVERIFY(controller.selectedCueId().isEmpty());
        QVERIFY(controller.selectCue(id)); QVERIFY(controller.selectedSymbolClipId().isEmpty());
        QVERIFY(controller.selectCamera()); QVERIFY(controller.selectedCueId().isEmpty());
        QVERIFY(controller.selectCue(id)); QVERIFY(!controller.cameraSelected());
        QVERIFY(controller.selectAudioClip(canvas.shot().audioClips[0].id)); QVERIFY(controller.selectedCueId().isEmpty());
        QVERIFY(controller.selectCue(id)); QVERIFY(controller.selectedAudioClipId().isEmpty());
        QVERIFY(controller.setFrame(0)); const auto ink = track.drawings[0].templateLayers.value("ink");
        QVERIFY(controller.selectPropertyLayer(ink)); QVERIFY(controller.selectedCueId().isEmpty());
        QVERIFY(controller.selectCue(id,false)); controller.setSelectedKeys({{ink,KeyframeProperty::PositionX,{},0}});
        QVERIFY(controller.selectedCueId().isEmpty()); QVERIFY(!controller.selectedKeys().isEmpty());
        QVERIFY(controller.selectCue(id)); QVERIFY(controller.selectedKeys().isEmpty());
        QVERIFY(controller.selectTrack(track.id)); QVERIFY(controller.selectedCueId().isEmpty());
        QVERIFY(controller.selectCue(id)); canvas.setProject(project); QVERIFY(controller.selectedCueId().isEmpty());
        QVERIFY(controller.selectCue(id)); QVERIFY(controller.removeSelectedCue()); QVERIFY(controller.selectedCueId().isEmpty());
        canvas.document()->history()->undo(); QVERIFY(timelineCue(canvas.shot(),id)); QVERIFY(controller.selectedCueId().isEmpty());
    }
    void editsAreAtomicNoOpSafeAndIndependentOfArtworkLocks() {
        Canvas canvas; TimelineController controller(&canvas); canvas.setProject(fixture());
        const auto id = canvas.shot().timelineCues[0].id; QVERIFY(controller.selectCue(id));
        QVERIFY(controller.setSelectedCueTiming(4,7)); QCOMPARE(cue(canvas,id).startFrame,4); QCOMPARE(cue(canvas,id).durationFrames,7);
        QCOMPARE(canvas.document()->history()->count(),1); QVERIFY(controller.setSelectedCueTiming(4,7)); QCOMPARE(canvas.document()->history()->count(),1);
        QVERIFY(!controller.setSelectedCueTiming(46,7)); QCOMPARE(canvas.document()->history()->count(),1);
        controller.setTransitionGuard([] { return false; }); QVERIFY(!controller.setSelectedCueTiming(5,7));
        QVERIFY(!controller.selectCue(canvas.shot().timelineCues[1].id)); QCOMPARE(controller.selectedCueId(),id);
        controller.setTransitionGuard({}); canvas.document()->setLayersLocked({controller.selectedPropertyLayerId()},true); const int count = canvas.document()->history()->count();
        auto changed = cue(canvas,id); changed.text = "Keep the face on model"; QVERIFY(controller.updateSelectedCue(changed));
        QCOMPARE(canvas.document()->history()->count(),count+1); canvas.document()->history()->undo(); QVERIFY(cue(canvas,id).text.isEmpty());
        QVERIFY(controller.setFrame(47)); const auto dialogue = controller.addCue(TimelineCueKind::Dialogue);
        QVERIFY(!dialogue.isEmpty()); QCOMPARE(cue(canvas,dialogue).durationFrames,1); QCOMPARE(cue(canvas,dialogue).startFrame,47);
        QCOMPARE(controller.selectedCueId(),dialogue);
    }
    void draftsPersistThroughRefreshCommitOnceAndCancel() {
        Canvas canvas; TimelineController controller(&canvas); canvas.setProject(fixture()); const auto id = canvas.shot().timelineCues[1].id;
        QVERIFY(controller.selectCue(id)); TimelineCuePanel panel(&controller); panel.resize(240,620); panel.show();
        auto *name = panel.findChild<QLineEdit *>("cueName"); auto *text = panel.findChild<QPlainTextEdit *>("cueText");
        typeName(name,"A long greeting"); text->setPlainText("Hello again\nWelcome home!");
        QVERIFY(controller.hasPendingEdits()); emit controller.changed(); QCOMPARE(name->text(),QString("A long greeting"));
        QCOMPARE(text->toPlainText(),QString("Hello again\nWelcome home!")); QCOMPARE(canvas.document()->history()->count(),0);
        QCOMPARE(cue(canvas,id).name,QString("Greeting")); controller.finishEditing();
        QCOMPARE(cue(canvas,id).name,QString("A long greeting")); QCOMPARE(cue(canvas,id).text,text->toPlainText());
        QCOMPARE(canvas.document()->history()->count(),1); QVERIFY(!controller.hasPendingEdits());
        typeName(name,"A long greeting"); controller.finishEditing(); QCOMPARE(canvas.document()->history()->count(),1);
        typeName(name,"Discard this draft"); controller.cancelEditing(); QCOMPARE(name->text(),QString("A long greeting"));
        QVERIFY(!controller.hasPendingEdits()); QCOMPARE(canvas.document()->history()->count(),1);
        auto *hold = panel.findChild<ValueSlider *>("cueHold"); auto *slider = hold->findChild<QSlider *>();
        slider->setSliderDown(true); slider->setValue(15); slider->setValue(18);
        QCOMPARE(cue(canvas,id).durationFrames,12); emit controller.changed(); QCOMPARE(qRound(hold->value()),18);
        slider->setSliderDown(false); QCOMPARE(cue(canvas,id).durationFrames,18); QCOMPARE(canvas.document()->history()->count(),2);
        enter(panel.findChild<ValueSlider *>("cueStart"),5); QCOMPARE(cue(canvas,id).startFrame,4); QCOMPARE(canvas.document()->history()->count(),3);
    }
    void draftResolutionCannotOverwriteNewerCueWithSameId() {
        Canvas canvas; TimelineController controller(&canvas); canvas.setProject(fixture()); const auto id = canvas.shot().timelineCues[0].id;
        QVERIFY(controller.selectCue(id)); auto wanted = cue(canvas,id); wanted.name = "Stale replacement";
        bool resolved = false;
        const auto connection = connect(&controller,&TimelineController::editResolutionRequested,&controller,[&] {
            if (resolved) return; resolved = true; auto newer = cue(canvas,id); newer.text = "Newer notes";
            QVERIFY(updateTimelineCue(canvas.document(),newer));
        });
        controller.setPendingEdits(true); QVERIFY(!controller.updateSelectedCue(wanted)); disconnect(connection);
        QCOMPARE(cue(canvas,id).text,QString("Newer notes")); QCOMPARE(cue(canvas,id).name,QString("Turn"));
        QCOMPARE(canvas.document()->history()->count(),1);
        // Passing a reference into the project must also remain safe when a
        // transition replaces the backing cue vector before the call returns.
        resolved = false;
        const auto aliasedConnection = connect(&controller,&TimelineController::editResolutionRequested,&controller,[&] {
            if (resolved) return; resolved = true; auto newer = cue(canvas,id); newer.name = "Resolved name";
            QVERIFY(updateTimelineCue(canvas.document(),newer));
        });
        const auto &alias = cue(canvas,id); controller.setPendingEdits(true);
        QVERIFY(!controller.updateSelectedCue(alias)); disconnect(aliasedConnection);
        QCOMPARE(cue(canvas,id).name,QString("Resolved name")); QCOMPARE(canvas.document()->history()->count(),2);
    }
    void sourceChangesDiscardDraftAndListSelectionFlushesIt() {
        Canvas canvas; TimelineController controller(&canvas); const auto project = fixture(); canvas.setProject(project);
        const auto first = canvas.shot().timelineCues[0].id, second = canvas.shot().timelineCues[1].id;
        QVERIFY(controller.selectCue(first)); TimelineCuePanel panel(&controller);
        auto *name = panel.findChild<QLineEdit *>("cueName"); typeName(name,"Saved before switching");
        auto *list = panel.findChild<QListWidget *>("cueList"); list->setCurrentRow(1);
        QCOMPARE(controller.selectedCueId(),second); QCOMPARE(cue(canvas,first).name,QString("Saved before switching"));
        QCOMPARE(canvas.document()->history()->count(),1);
        typeName(name,"Must not survive reload"); canvas.setProject(project); QVERIFY(!controller.hasPendingEdits()); QVERIFY(controller.selectedCueId().isEmpty());
        QVERIFY(controller.selectCue(second)); QCOMPARE(name->text(),QString("Greeting")); controller.finishEditing(); QCOMPARE(canvas.document()->history()->count(),0);
        typeName(name,"Must not override an external edit"); auto updated = cue(canvas,second); updated.name = "External change";
        QVERIFY(updateTimelineCue(canvas.document(),updated)); QCOMPARE(name->text(),QString("External change")); QVERIFY(!controller.hasPendingEdits());
        controller.finishEditing(); QCOMPARE(cue(canvas,second).name,QString("External change")); QCOMPARE(canvas.document()->history()->count(),1);
        typeName(name,"Cancel before undo"); controller.cancelEditing(); canvas.document()->history()->undo();
        QCOMPARE(cue(canvas,second).name,QString("Greeting")); QCOMPARE(name->text(),QString("Greeting"));
    }
    void invalidNamePreservesOtherFieldsAndPastedNotesStayValid() {
        Canvas canvas; TimelineController controller(&canvas); canvas.setProject(fixture()); const auto id = canvas.shot().timelineCues[1].id;
        QVERIFY(controller.selectCue(id)); TimelineCuePanel panel(&controller);
        auto *name = panel.findChild<QLineEdit *>("cueName"); auto *text = panel.findChild<QPlainTextEdit *>("cueText");
        typeName(name,"   "); text->setPlainText(QString("Hello") + QChar(1) + " again\nNext line");
        controller.finishEditing(); QCOMPARE(cue(canvas,id).name,QString("Greeting"));
        QCOMPARE(cue(canvas,id).text,QString("Hello again\nNext line")); QVERIFY(!controller.hasPendingEdits());
        QCOMPARE(canvas.document()->history()->count(),1); QVERIFY(validateTimelineCues(canvas.shot()).isEmpty());
        text->setPlainText(QString(5000,QLatin1Char('a'))); QCOMPARE(text->toPlainText().size(),4096);
        QCOMPARE(canvas.document()->history()->count(),1); controller.finishEditing();
        QCOMPARE(cue(canvas,id).text.size(),4096); QCOMPARE(canvas.document()->history()->count(),2);
    }
    void searchTimingNamesAndCompactAccessibleLayout() {
        Canvas canvas; TimelineController controller(&canvas); canvas.setProject(fixture()); const auto id = canvas.shot().timelineCues[1].id;
        QVERIFY(controller.selectCue(id)); TimelineCuePanel panel(&controller); panel.resize(240,700); panel.show();
        auto *list = panel.findChild<QListWidget *>("cueList"); auto *search = panel.findChild<QLineEdit *>("cueSearch");
        QCOMPARE(list->count(),3); auto *original = list->item(1); emit controller.changed(); QCOMPARE(list->item(1),original);
        QVERIFY(original->text().contains("11–22")); QVERIFY(original->text().contains("Hana")); QVERIFY(original->toolTip().contains("Welcome home"));
        search->setText("hana"); QCOMPARE(list->count(),1); search->setText("welcome"); QCOMPARE(list->count(),1);
        search->setText("key pose"); QCOMPARE(list->count(),1); QCOMPARE(controller.selectedCueId(),id); search->clear(); QCOMPARE(list->count(),3);
        for (int pass = 0; pass < 2; ++pass) {
            if (pass) { auto font = panel.font(); font.setPointSize(16); panel.setFont(font); }
            QTest::qWait(10); auto *tabs = panel.findChild<QTabBar *>("cueKindTabs"); QCOMPARE(tabs->count(),5);
            for (int i = 0; i < tabs->count(); ++i) {
                const auto tab = tabs->tabRect(i), area = tabs->rect();
                const auto diagnostic = QString("Pass %1 tab %2: [%3,%4 %5x%6] outside [%7,%8 %9x%10]")
                    .arg(pass).arg(i).arg(tab.x()).arg(tab.y()).arg(tab.width()).arg(tab.height())
                    .arg(area.x()).arg(area.y()).arg(area.width()).arg(area.height());
                QVERIFY2(area.contains(tab),qPrintable(diagnostic)); QVERIFY(!tabs->tabIcon(i).isNull());
                QVERIFY(!tabs->tabToolTip(i).isEmpty()); QVERIFY(!tabs->accessibleTabName(i).isEmpty());
            }
            for (const auto *id : {"cueAdd","cueDelete","cuePrevious","cueNext","cueColor"}) {
                auto *button = panel.findChild<QToolButton *>(id); QVERIFY(button); QCOMPARE(button->size(),QSize(28,28));
                QCOMPARE(button->iconSize(),QSize(16,16)); QVERIFY(!button->toolTip().isEmpty()); QVERIFY(!button->accessibleName().isEmpty());
            }
            for (auto *field : panel.findChildren<ValueSlider *>()) {
                QCOMPARE(field->findChild<QDoubleSpinBox *>()->width(),48); QVERIFY(field->findChild<QSlider *>()->width() >= 76);
                QVERIFY(field->rect().contains(field->findChild<QSlider *>()->geometry()));
            }
        }
    }
    void symbolEditorBlocksCuesEvenWhenCalledDirectly() {
        Canvas canvas; TimelineController controller(&canvas); canvas.setProject(fixture()); const auto id = canvas.shot().timelineCues[0].id;
        QVERIFY(controller.selectCue(id)); controller.setProperty("symbolEditor",true); TimelineCuePanel panel(&controller);
        const auto before = canvas.project().toJson(); QVERIFY(controller.addCue(TimelineCueKind::Marker).isEmpty());
        QVERIFY(!controller.selectCue(id)); QVERIFY(!controller.updateSelectedCue(cue(canvas,id)));
        QVERIFY(!controller.setSelectedCueTiming(5,3)); QVERIFY(!controller.removeSelectedCue()); QVERIFY(!controller.stepCue(1));
        QVERIFY(!panel.findChild<QToolButton *>("cueAdd")->isEnabled());
        QVERIFY(panel.findChild<QLabel *>("cueStatus")->text().contains("shots")); QCOMPARE(canvas.project().toJson(),before);
        QCOMPARE(canvas.document()->history()->count(),0);
    }
};
QTEST_MAIN(TimelineCueControlsTest)
#include "test_timeline_cue_controls.moc"
