#include "canvas.h"
#include "cel_paint_check_panel.h"
#include "character_color_panel.h"
#include "character_recolor_dialog.h"
#include "document.h"
#include "main_window.h"
#include "reference_panel.h"
#include "timeline_controller.h"
#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>
#include <functional>

using namespace toon;
namespace {
const QColor dayColor("#edb995"), nightColor("#6071a0");
struct Fixture { Project project; QString character, night; };
Fixture fixture() {
    auto project = Project::create(); project.width = 128; project.height = 96;
    auto second = Project::create().shots[0]; second.number = "C002"; project.shots.append(second);
    for (auto &shot : project.shots) {
        QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::transparent);
        QPainter painter(&tile); painter.fillRect(QRect(15, 15, 40, 40), dayColor); painter.end();
        shot.layers[0].tiles[{0, 0}] = tile;
    }
    Document document; document.load(project); Fixture result;
    result.character = document.addCharacter("Hana");
    const auto role = document.addCharacterColor(result.character, "Skin base", CharacterColorCategory::Skin, dayColor);
    for (const auto &variant : document.project().characters[0].colorVariants)
        if (variant.name == "Night") result.night = variant.id;
    document.updateCharacterColor(result.character, role, "Skin base", CharacterColorCategory::Skin, nightColor, nullptr, result.night);
    result.project = document.project(); return result;
}
CharacterColorPanel *showColors(MainWindow &window, const Fixture &source) {
    auto *dock = window.findChild<QDockWidget *>("referencesPanel");
    if (!dock) return nullptr;
    dock->show(); dock->raise();
    auto *references = dock->findChild<ReferencePanel *>();
    if (!references) return nullptr;
    auto *tabs = references->findChild<QTabBar *>("referenceTabs");
    if (!tabs) return nullptr;
    tabs->setCurrentIndex(2);
    auto *colors = references->findChild<CharacterColorPanel *>();
    if (colors) { colors->selectCharacter(source.character); colors->selectVariant(source.night); }
    return colors;
}
// Exercise the actual launch button while its exec() runs a nested event loop.
// Every failure closes the modal, so a failed assertion cannot hang the suite.
QString withDialog(QToolButton *button, const std::function<QString(CharacterRecolorDialog &)> &inspect) {
    bool opened = false; QString error;
    QTimer inspectTimer, timeout; inspectTimer.setInterval(5); timeout.setSingleShot(true);
    QObject::connect(&inspectTimer, &QTimer::timeout, [&] {
        if (auto *dialog = qobject_cast<CharacterRecolorDialog *>(QApplication::activeModalWidget())) {
            inspectTimer.stop(); opened = true; error = inspect(*dialog);
            if (dialog->isVisible()) dialog->reject();
        } else if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            error = "Unexpected message: " + message->text(); inspectTimer.stop(); message->reject();
        }
    });
    QObject::connect(&timeout, &QTimer::timeout, [&] {
        error = "Timed out waiting for the recolor dialog";
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
    });
    inspectTimer.start(); timeout.start(20000); button->click(); inspectTimer.stop(); timeout.stop();
    if (!opened && error.isEmpty()) error = "The Recolor drawings button did not open its dialog";
    return error;
}
QString preview(CharacterRecolorDialog &dialog) {
    QSignalSpy finished(&dialog, &CharacterRecolorDialog::previewFinished);
    dialog.preview();
    if (finished.isEmpty() && !finished.wait(10000)) return "Recolor preview did not finish";
    return dialog.previewResult().error;
}
}

class CharacterRecolorWorkspaceTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void paletteLaunchPausesPlaybackAndCancelRestoresDrawingFocusWithoutEdits() {
        const auto source = fixture(); QTemporaryDir directory; QString error;
        const auto path = directory.filePath("cancel.toonproj"); QVERIFY(saveProject(source.project, path, &error));
        MainWindow window(directory.filePath("workspace.ini")); QVERIFY(window.openProject(path));
        window.show(); window.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *colors = showColors(window, source); QVERIFY(colors);
        auto *launch = colors->findChild<QToolButton *>("recolorCharacterDrawings"); QVERIFY(launch && launch->isEnabled());
        auto *timeline = window.findChild<TimelineController *>(); QVERIFY(timeline);
        auto *document = window.canvas()->document(); const auto foreground = window.canvas()->brush.color;
        timeline->setPlaying(true); QVERIFY(timeline->playing());
        error = withDialog(launch, [&](CharacterRecolorDialog &dialog) -> QString {
            if (timeline->playing()) return "Playback continued while recoloring";
            auto *from = dialog.findChild<QComboBox *>("characterRecolorSourcePalette");
            auto *to = dialog.findChild<QComboBox *>("characterRecolorTargetPalette");
            if (!from || !to || !from->currentData().toString().isEmpty() || to->currentData().toString() != source.night)
                return "Recolor did not start with Day to the selected Night palette";
            const auto failure = preview(dialog); if (!failure.isEmpty()) return failure;
            if (!dialog.previewResult().changedPixels) return "Expected a visible recolor preview";
            if (!document->history()->isClean() || document->project().shots[0].layers[0].tiles != source.project.shots[0].layers[0].tiles)
                return "Preview changed the artwork";
            QTest::keyClick(&dialog, Qt::Key_Escape); return {};
        });
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!timeline->playing()); QVERIFY(document->history()->isClean()); QCOMPARE(document->history()->count(), 0);
        QCOMPARE(window.canvas()->brush.color, foreground);
        QCOMPARE(document->project().shots[0].layers[0].tiles, source.project.shots[0].layers[0].tiles);
        QTRY_VERIFY(window.canvas()->hasFocus()); QVERIFY(window.close());
    }
    void selectedDrawingsAcrossShotsApplyOnceAndSaveReopenKeepsRecoloredPixels() {
        const auto source = fixture(); QTemporaryDir directory; QString error;
        const auto path = directory.filePath("applied.toonproj"); QVERIFY(saveProject(source.project, path, &error));
        MainWindow window(directory.filePath("workspace.ini")); QVERIFY(window.openProject(path));
        auto *colors = showColors(window, source); QVERIFY(colors);
        auto *launch = colors->findChild<QToolButton *>("recolorCharacterDrawings"); QVERIFY(launch);
        auto *document = window.canvas()->document(); const auto originalShot = document->activeShotId();
        const auto originalLayer = document->activeLayer(); const auto foreground = window.canvas()->brush.color;
        error = withDialog(launch, [&](CharacterRecolorDialog &dialog) -> QString {
            auto *scope = dialog.findChild<QComboBox *>("characterRecolorScope");
            auto *drawings = dialog.findChild<QListWidget *>("characterRecolorDrawings");
            if (!scope || !drawings) return "Drawing selection controls are missing";
            scope->setCurrentIndex(1);
            if (drawings->count() != 2) return "All shots should expose both raster drawings";
            for (int index = 0; index < drawings->count(); ++index) drawings->item(index)->setCheckState(Qt::Checked);
            const auto failure = preview(dialog); if (!failure.isEmpty()) return failure;
            if (dialog.previewResult().drawings.size() != 2) return "Preview omitted a checked drawing";
            if (document->history()->count() != 0) return "Preview created an Undo entry";
            if (!dialog.applyPreview()) return "Applying the preview failed";
            return {};
        });
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(document->history()->count(), 1); QVERIFY(window.isWindowModified());
        QCOMPARE(document->activeShotId(), originalShot); QCOMPARE(document->activeLayer(), originalLayer);
        QCOMPARE(window.canvas()->brush.color, foreground);
        for (const auto &shot : document->project().shots) QCOMPARE(shot.layers[0].tiles.value({0, 0}).pixelColor(25, 25), nightColor);
        document->history()->undo(); QVERIFY(document->history()->isClean());
        for (const auto &shot : document->project().shots) QCOMPARE(shot.layers[0].tiles.value({0, 0}).pixelColor(25, 25), dayColor);
        document->history()->redo();
        auto *save = window.findChild<QAction *>("saveProjectAction"); QVERIFY(save); save->trigger();
        QVERIFY(!window.isWindowModified()); QVERIFY(document->history()->isClean());
        const auto saved = loadProject(path, &error); QVERIFY2(saved.has_value(), qPrintable(error));
        for (const auto &shot : saved->shots) QCOMPARE(shot.layers[0].tiles.value({0, 0}).pixelColor(25, 25), nightColor);
        QCOMPARE(saved->characters[0].colors[0].color, dayColor);
        QCOMPARE(characterVariantColors(saved->characters[0], source.night)[0].color, nightColor);
        QVERIFY(window.openProject(path));
        for (const auto &shot : document->project().shots) QCOMPARE(shot.layers[0].tiles.value({0, 0}).pixelColor(25, 25), nightColor);
        QVERIFY(window.close());
    }
    void pendingPaintRepairCanCancelRecolorLaunchWithoutLosingItsPreview() {
        const auto source = fixture(); QTemporaryDir directory; MainWindow window(directory.filePath("workspace.ini"));
        window.canvas()->setProject(source.project); auto *colors = showColors(window, source); QVERIFY(colors);
        auto *check = window.findChild<CelPaintCheckPanel *>(); QVERIFY(check);
        QVERIFY(check->selectPalette(source.character, source.night));
        QSignalSpy checked(check, &CelPaintCheckPanel::inspectionFinished);
        check->startCheck(); QTRY_COMPARE_WITH_TIMEOUT(checked.size(), 1, 10000);
        auto *problems = check->findChild<QListWidget *>("celPaintCheckProblems"); QVERIFY(problems);
        bool selected = false;
        for (int index = 0; index < check->inspection().issues.size(); ++index)
            if (check->inspection().issues[index].kind == CelPaintIssueKind::OutsidePalette) {
                problems->item(index)->setCheckState(Qt::Checked); selected = true; break;
            }
        QVERIFY(selected); QSignalSpy prepared(check, &CelPaintCheckPanel::previewFinished);
        check->previewRepairs(); QTRY_COMPARE_WITH_TIMEOUT(prepared.size(), 1, 10000); QVERIFY(check->pending());
        auto *launch = colors->findChild<QToolButton *>("recolorCharacterDrawings"); QVERIFY(launch);
        int questions = 0; bool recolorOpened = false; QTimer answer, timeout; answer.setInterval(5); timeout.setSingleShot(true);
        connect(&answer, &QTimer::timeout, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                ++questions; if (auto *cancel = box->button(QMessageBox::Cancel)) cancel->click(); else box->reject();
            } else if (auto *dialog = qobject_cast<CharacterRecolorDialog *>(QApplication::activeModalWidget())) {
                recolorOpened = true; dialog->reject();
            }
        });
        connect(&timeout, &QTimer::timeout, [&] { if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject(); });
        answer.start(); timeout.start(15000); launch->click(); answer.stop(); timeout.stop();
        QCOMPARE(questions, 1); QVERIFY(!recolorOpened); QVERIFY(check->pending());
        QCOMPARE(window.canvas()->document()->history()->count(), 0);
        QCOMPARE(window.canvas()->project().shots[0].layers[0].tiles, source.project.shots[0].layers[0].tiles);
        check->cancelEditing();
    }
};
QTEST_MAIN(CharacterRecolorWorkspaceTests)
#include "test_character_recolor_workspace.moc"
