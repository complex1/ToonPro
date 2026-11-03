#include "animation_test_fixture.h"
#include "canvas.h"
#include "document.h"
#include "symbol_clip_panel.h"
#include "symbol_library_panel.h"
#include "symbols.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QStyle>
#include <QTabBar>
#include <QToolButton>
#include <QtTest>

using namespace toon;
namespace {
Project fixture() {
    auto project = animation_fixture::project(); auto source = Project::create().shots[0]; source.durationFrames = 12;
    const auto sourceId = animation_fixture::id(); project.symbols.append({sourceId,"Walk cycle",source});
    auto &shot = project.shots[0]; shot.durationFrames = 48;
    auto top = RasterLayer::create("Top symbol"); top.kind = LayerKind::Symbol; top.symbol = SymbolInstance{sourceId}; shot.layers.append(top);
    auto &track = shot.animationTracks[0];
    const auto nestedId = track.drawings[1].templateLayers.value("ink");
    for (auto &layer : shot.layers) if (layer.id == nestedId) { layer.tiles.clear(); layer.kind = LayerKind::Symbol; layer.symbol = SymbolInstance{sourceId}; layer.name = "Nested symbol"; }
    auto group = RasterLayer::create("Unused drawing"); group.group = true; group.parentId = track.rootGroupLayerId;
    auto unused = RasterLayer::create("Unused symbol"); unused.kind = LayerKind::Symbol; unused.parentId = group.id;
    unused.symbol = SymbolInstance{sourceId}; unused.symbol->durationFrames = 0;
    track.drawings.append({animation_fixture::id(),group.id,{{"ink",unused.id}}}); shot.layers.append(group); shot.layers.append(unused);
    const auto ink = track.drawings[0].templateLayers.value("ink");
    shot.layerAnimations = {{ink, {}, {{KeyframeProperty::PositionX, {{0, 0}, {2, 8}}}}}};
    return project;
}
QString named(const Canvas &canvas, const QString &name) { for (const auto &layer : canvas.shot().layers) if (layer.name == name) return layer.id; return {}; }
const SymbolInstance &instance(const Canvas &canvas, const QString &id) {
    for (const auto &layer : canvas.shot().layers) if (layer.id == id && layer.symbol) return *layer.symbol;
    qFatal("Missing test Symbol");
}
ValueSlider *field(QWidget &panel, const char *id) { return panel.findChild<ValueSlider *>(id); }
void enter(ValueSlider *field, double value) { field->setValue(value); QMetaObject::invokeMethod(field,"editingFinished"); }
}
class SymbolClipControlsTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); QVERIFY2(fixture().validate().isEmpty(), qPrintable(fixture().validate())); }
    void clipSelectionSeeksExposuresAndKeepsUnusedDrawingInactive() {
        Canvas canvas; TimelineController controller(&canvas); canvas.setProject(fixture());
        const auto top = named(canvas,"Top symbol"), nested = named(canvas,"Nested symbol"), unused = named(canvas,"Unused symbol");
        QSignalSpy requested(&controller,&TimelineController::symbolPropertiesRequested);
        QVERIFY(controller.selectSymbolClip(top)); QCOMPARE(controller.selectedSymbolClipId(),top);
        QVERIFY(controller.selectSymbolClip(nested)); QCOMPARE(controller.currentFrame(),4);
        QCOMPARE(canvas.shot().layers[canvas.document()->activeLayer()].id,nested);
        QVERIFY(controller.setFrame(20)); QCOMPARE(controller.selectedSymbolClipId(),nested);
        const int active = canvas.document()->activeLayer();
        QVERIFY(controller.selectSymbolClip(unused)); QCOMPARE(controller.selectedSymbolClipId(),unused);
        QCOMPARE(canvas.document()->activeLayer(),active); QCOMPARE(controller.currentFrame(),20);
        QVERIFY(!layerPresentAtFrame(canvas.shot(),unused,20)); QCOMPARE(requested.size(),3);
        QCOMPARE(canvas.document()->history()->count(),0);
    }
    void selectionDomainsAndSameIdReloadReset() {
        Canvas canvas; TimelineController controller(&canvas); auto project = fixture(); canvas.setProject(project);
        const auto top = named(canvas,"Top symbol"); const auto track = canvas.shot().animationTracks[0];
        const TimelineCelRef cel{track.id,track.exposures[0].id};
        controller.setSelectedCels({cel}); QVERIFY(controller.selectSymbolClip(top)); QVERIFY(controller.selectedCels().isEmpty());
        controller.setSelectedCels({cel}); QVERIFY(controller.selectedSymbolClipId().isEmpty());
        QVERIFY(controller.selectSymbolClip(top)); QVERIFY(controller.selectCamera()); QVERIFY(controller.selectedSymbolClipId().isEmpty());
        QVERIFY(controller.selectSymbolClip(top)); QVERIFY(!controller.cameraSelected());
        const auto ink = track.drawings[0].templateLayers.value("ink"); QVERIFY(controller.selectPropertyLayer(ink));
        controller.setSelectedKeys({{ink,KeyframeProperty::PositionX,{},0}}); QVERIFY(controller.selectedSymbolClipId().isEmpty());
        QVERIFY(controller.selectSymbolClip(top)); QVERIFY(controller.selectedKeys().isEmpty());
        canvas.setProject(project); QVERIFY(controller.selectedSymbolClipId().isEmpty());
        canvas.document()->selectLayer(animation_fixture::layerIndex(canvas.shot(),top)); QCOMPARE(controller.selectedSymbolClipId(),top);
    }
    void clipEditsAreAtomicAndRespectGuardLocksAndPlayback() {
        Canvas canvas; TimelineController controller(&canvas); canvas.setProject(fixture()); const auto top = named(canvas,"Top symbol");
        QVERIFY(controller.selectSymbolClip(top));
        QVERIFY(controller.setSelectedSymbolClipTiming(2,12,3)); QCOMPARE(canvas.document()->history()->count(),1);
        QVERIFY(controller.editSelectedSymbolClip(SymbolClipOperation::Move,4)); QCOMPARE(instance(canvas,top).startFrame,6);
        QCOMPARE(instance(canvas,top).durationFrames,12); QCOMPARE(instance(canvas,top).playbackOffset,3);
        QCOMPARE(canvas.document()->history()->count(),2); canvas.document()->history()->undo(); QCOMPARE(instance(canvas,top).startFrame,2);
        auto value = instance(canvas,top); value.speed = 1.5; value.firstFrame = 3; value.playback = SymbolPlayback::PingPong;
        QVERIFY(controller.setSelectedSymbolPlayback(value)); QCOMPARE(instance(canvas,top),value);
        controller.setTransitionGuard([] { return false; });
        const auto before = canvas.project().toJson(); const int history = canvas.document()->history()->index();
        QVERIFY(!controller.editSelectedSymbolClip(SymbolClipOperation::Slip,5)); QVERIFY(!controller.selectSymbolClip(named(canvas,"Nested symbol")));
        QCOMPARE(canvas.project().toJson(),before); QCOMPARE(controller.selectedSymbolClipId(),top); QCOMPARE(canvas.document()->history()->index(),history);
        controller.setTransitionGuard({}); controller.setPlaying(true); QVERIFY(controller.playing());
        QVERIFY(controller.editSelectedSymbolClip(SymbolClipOperation::Slip,1)); QVERIFY(!controller.playing());
        canvas.document()->setLayerPositionLocked(true); const auto locked = canvas.project().toJson();
        QVERIFY(!controller.setSelectedSymbolClipTiming(1,4,0)); QCOMPARE(canvas.project().toJson(),locked);
    }
    void compactPropertiesPreserveDraftAndCommitOnce() {
        Canvas canvas; TimelineController controller(&canvas); canvas.setProject(fixture()); const auto top = named(canvas,"Top symbol");
        QVERIFY(controller.selectSymbolClip(top)); SymbolClipPanel panel(&controller); panel.resize(240,520); panel.show(); QTest::qWait(10);
        auto *through = panel.findChild<QToolButton *>("symbolClipThroughEnd"); QVERIFY(through->isChecked());
        QVERIFY(!field(panel,"symbolClipDuration")->isEnabled()); through->click();
        QCOMPARE(instance(canvas,top).durationFrames,48); const int history = canvas.document()->history()->count();
        auto *duration = field(panel,"symbolClipDuration"); auto *number = duration->findChild<QDoubleSpinBox *>();
        auto *line = number->findChild<QLineEdit *>(); number->setFocus(); line->selectAll(); QTest::keyClicks(number,"15");
        QVERIFY(controller.hasPendingEdits()); QCOMPARE(instance(canvas,top).durationFrames,48);
        emit controller.changed(); QCOMPARE(line->text(),QString("15")); QCOMPARE(canvas.document()->history()->count(),history);
        QTest::keyClick(number,Qt::Key_Return); QCOMPARE(instance(canvas,top).durationFrames,15); QCOMPARE(canvas.document()->history()->count(),history+1);
        line->selectAll(); QTest::keyClicks(number,"9"); QTest::keyClick(number,Qt::Key_Escape);
        QCOMPARE(instance(canvas,top).durationFrames,15); QVERIFY(!controller.hasPendingEdits());
        auto *slider = field(panel,"symbolClipSpeed")->findChild<QSlider *>(); slider->setSliderDown(true); slider->setValue(150); slider->setValue(175);
        QCOMPARE(instance(canvas,top).speed,1.); slider->setSliderDown(false); QCOMPARE(instance(canvas,top).speed,1.75);
        QCOMPARE(canvas.document()->history()->count(),history+2);
    }
    void holdRestoresEmptyAndFirstFrameResetsSlip() {
        Canvas canvas; TimelineController controller(&canvas); canvas.setProject(fixture()); const auto top = named(canvas,"Top symbol");
        QVERIFY(controller.selectSymbolClip(top)); QVERIFY(controller.setSelectedSymbolClipTiming(4,0,7));
        SymbolClipPanel panel(&controller); panel.resize(240,520); panel.show();
        QVERIFY(panel.findChild<QLabel *>("symbolClipStatus")->text().contains("Empty"));
        enter(field(panel,"symbolClipDuration"),10); QCOMPARE(instance(canvas,top).durationFrames,10);
        enter(field(panel,"symbolClipStart"),8); QCOMPARE(instance(canvas,top).startFrame,7); QCOMPARE(instance(canvas,top).playbackOffset,7);
        enter(field(panel,"symbolClipFirstFrame"),5); QCOMPARE(instance(canvas,top).firstFrame,4); QCOMPARE(instance(canvas,top).playbackOffset,0);
        auto *tabs = panel.findChild<QTabBar *>("symbolClipPlayback"); tabs->setCurrentIndex(int(SymbolPlayback::SingleFrame));
        QVERIFY(!field(panel,"symbolClipSpeed")->isEnabled()); QVERIFY(!field(panel,"symbolClipSlip")->isEnabled());
        QVERIFY(field(panel,"symbolClipFirstFrame")->isEnabled());
        canvas.document()->history()->undo(); QCOMPARE(instance(canvas,top).playback,SymbolPlayback::Loop);
    }
    void rejectedNavigationPreservesDraftAndLibraryKeepsClipTiming() {
        Canvas canvas; TimelineController controller(&canvas); canvas.setProject(fixture()); const auto top = named(canvas,"Top symbol");
        QVERIFY(controller.selectSymbolClip(top)); QVERIFY(controller.setSelectedSymbolClipTiming(2,10,5));
        SymbolClipPanel panel(&controller); panel.show();
        auto *number = field(panel,"symbolClipDuration")->findChild<QDoubleSpinBox *>(); auto *line = number->findChild<QLineEdit *>();
        number->setFocus(); line->selectAll(); QTest::keyClicks(number,"6"); controller.setTransitionGuard([] { return false; });
        QVERIFY(!controller.setFrame(8)); QCOMPARE(line->text(),QString("6")); QVERIFY(controller.hasPendingEdits());
        QCOMPARE(instance(canvas,top).durationFrames,10); controller.setTransitionGuard({}); controller.cancelEditing();
        SymbolLibraryPanel library(&canvas,&controller); library.show();
        enter(field(library,"symbolSpeed"),125); QCOMPARE(instance(canvas,top).speed,1.25);
        QCOMPARE(instance(canvas,top).durationFrames,10); QCOMPARE(instance(canvas,top).playbackOffset,5);
        QSignalSpy requested(&controller,&TimelineController::symbolPropertiesRequested);
        library.findChild<QToolButton *>("symbolClipPropertiesButton")->click(); QCOMPARE(requested.size(),1); QCOMPARE(controller.selectedSymbolClipId(),top);
        enter(field(library,"symbolFirstFrame"),3); QCOMPARE(instance(canvas,top).playbackOffset,0);
    }
    void compactTargetsAndLargerFont() {
        Canvas canvas; TimelineController controller(&canvas); canvas.setProject(fixture()); QVERIFY(controller.selectSymbolClip(named(canvas,"Top symbol")));
        SymbolClipPanel panel(&controller); panel.resize(240,560); panel.show();
        const QStringList regularActions{"symbolClipPickFrame","symbolPosePick","symbolClipThroughEnd"};
        const QStringList poseActions{"symbolPosePrevious","symbolPoseDelete","symbolPoseNext"};
        for (const auto &id : regularActions + poseActions) QVERIFY2(panel.findChild<QToolButton *>(id),qPrintable(id));
        auto *tabs = panel.findChild<QTabBar *>("symbolClipPlayback"); QVERIFY(tabs); QCOMPARE(tabs->count(),5);
        for (int pass = 0; pass < 2; ++pass) {
            if (pass) { auto font = panel.font(); font.setPointSize(16); panel.setFont(font); }
            for (const auto mode : {SymbolPlayback::Loop,SymbolPlayback::Poses}) {
                tabs->setCurrentIndex(int(mode)); QTest::qWait(10); QCOMPARE(panel.width(),240);
                QCOMPARE(tabs->count(),5); QCOMPARE(tabs->iconSize(),QSize(16,16));
                QCOMPARE(tabs->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,tabs),600);
                for (int i = 0; i < tabs->count(); ++i) {
                    QVERIFY(tabs->rect().contains(tabs->tabRect(i))); QVERIFY(!tabs->tabToolTip(i).isEmpty());
                    QVERIFY(!tabs->tabIcon(i).isNull()); QVERIFY(!tabs->accessibleTabName(i).isEmpty());
                }
                int visibleActions = 0;
                for (auto *control : panel.findChildren<QToolButton *>()) {
                    // Only Qt's tab arrows and the named pose-only actions may
                    // be hidden; an unexpected hidden action remains a failure.
                    const auto id = control->objectName();
                    if (control->parentWidget() == tabs && (id == "ScrollLeftButton" || id == "ScrollRightButton")) {
                        QVERIFY(!control->isVisibleTo(&panel)); continue;
                    }
                    QVERIFY2(regularActions.contains(id) || poseActions.contains(id),qPrintable(id));
                    const bool expectedVisible = regularActions.contains(id) || mode == SymbolPlayback::Poses;
                    QCOMPARE(control->isVisibleTo(&panel),expectedVisible);
                    if (!expectedVisible) continue;
                    ++visibleActions;
                    QVERIFY(!control->accessibleName().isEmpty()); QVERIFY(!control->toolTip().isEmpty());
                    QVERIFY2(!control->icon().isNull(),qPrintable(id));
                    QCOMPARE(control->iconSize(),QSize(16,16)); QCOMPARE(control->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,control),600);
                    QVERIFY(control->width() >= 28 && control->height() >= 28);
                    const QRect bounds(control->mapTo(&panel,QPoint()),control->size()); QVERIFY(bounds.left() >= 0 && bounds.right() < panel.width());
                }
                QCOMPARE(visibleActions,mode == SymbolPlayback::Poses ? 6 : 3);
                for (auto *number : panel.findChildren<QDoubleSpinBox *>()) { QCOMPARE(number->width(),48); QVERIFY(number->suffix().isEmpty()); }
            }
        }
        if (qEnvironmentVariableIsSet("TOON_UI_PREVIEW")) panel.grab().save(qEnvironmentVariable("TOON_UI_PREVIEW") + "/symbol-clip-properties.png");
    }
    void inspectionRetainsSavedOutOfShotValuesWithoutHistory() {
        for (bool finite : {false,true}) {
            auto project = fixture(); QString id;
            for (auto &layer : project.shots[0].layers) if (layer.name == "Top symbol") {
                id = layer.id; layer.symbol->startFrame = finite ? 42 : 72;
                layer.symbol->durationFrames = finite ? 10 : -1;
            }
            QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
            Canvas canvas; TimelineController controller(&canvas); canvas.setProject(project); QVERIFY(controller.selectSymbolClip(id));
            const auto before = canvas.project().toJson(); SymbolClipPanel panel(&controller); panel.resize(240,520); panel.show();
            SymbolLibraryPanel library(&canvas,&controller); library.show(); QTest::qWait(10);
            QCOMPARE(field(panel,"symbolClipStart")->value(),finite ? 43. : 73.);
            QCOMPARE(field(panel,"symbolClipDuration")->value(),finite ? 10. : 0.);
            QCOMPARE(field(library,"symbolStartFrame")->value(),finite ? 43. : 73.);
            QVERIFY(panel.findChild<QLabel *>("symbolClipStatus")->text().contains("outside"));
            emit controller.changed(); QCOMPARE(canvas.project().toJson(),before); QCOMPARE(canvas.document()->history()->count(),0);
        }
    }
};
QTEST_MAIN(SymbolClipControlsTest)
#include "test_symbol_clip_controls.moc"
