#include "canvas.h"
#include "character_color_panel.h"
#include "cel_paint_check_panel.h"
#include "document.h"
#include "main_window.h"
#include "reference_panel.h"
#include <QAction>
#include <QDockWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace toon;
namespace {
Project fixture() {
    auto project = Project::create(); project.width = 160; project.height = 120;
    auto second = Project::create().shots[0]; second.number = "C002"; project.shots.append(second);
    QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(QColor("#edb995"));
    project.shots[0].layers[0].tiles[{0, 0}] = tile;
    return project;
}
QString variant(const CharacterModel &character, const QString &name) {
    for (const auto &entry : character.colorVariants) if (entry.name == name) return entry.id;
    return {};
}
}
class CharacterVariantsWorkspaceTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void selectedLightingPaletteFeedsPaintCheckAndExplicitColorPickingAcrossShots() {
        QTemporaryDir directory; MainWindow window(directory.filePath("workspace.ini"));
        window.canvas()->setProject(fixture()); auto *document = window.canvas()->document();
        const auto character = document->addCharacter("Hana");
        const auto role = document->addCharacterColor(character, "Skin base", CharacterColorCategory::Skin, QColor("#edb995"));
        const auto night = variant(document->project().characters[0], "Night"); QVERIFY(!night.isEmpty());
        QVERIFY(document->updateCharacterColor(character, role, "Skin base", CharacterColorCategory::Skin, QColor("#6071a0"), nullptr, night));
        auto *colors = window.findChild<CharacterColorPanel *>(); auto *check = window.findChild<CelPaintCheckPanel *>();
        QVERIFY(colors && check); colors->selectCharacter(character);
        const auto history = document->history()->count(); const auto foreground = window.canvas()->brush.color;
        const auto pixels = document->project().shots[0].layers[0].tiles;
        colors->selectVariant(night);
        QCOMPARE(check->selectedCharacterId(), character); QCOMPARE(check->selectedVariantId(), night);
        QCOMPARE(window.canvas()->brush.color, foreground);
        auto *list = colors->findChild<QListWidget *>("characterColorList"); QVERIFY(list); QCOMPARE(list->count(), 1);
        QCOMPARE(list->item(0)->data(Qt::UserRole + 1).value<QColor>(), QColor("#6071a0"));
        QVERIFY(QMetaObject::invokeMethod(list, "itemClicked", Qt::DirectConnection, Q_ARG(QListWidgetItem *, list->item(0))));
        QCOMPARE(window.canvas()->brush.color, QColor("#6071a0"));
        QVERIFY(document->selectShot(document->project().shots[1].id));
        QCOMPARE(colors->selectedVariantId(), night); QCOMPARE(check->selectedVariantId(), night);
        QCOMPARE(document->history()->count(), history); QCOMPARE(document->project().shots[0].layers[0].tiles, pixels);
        colors->selectVariant({}); QCOMPARE(check->selectedVariantId(), QString());
        QCOMPARE(window.canvas()->brush.color, QColor("#6071a0"));
    }
    void saveFlushesSharedNameFromLightingPaletteAndReopensIndependentColors() {
        QTemporaryDir directory; QString error; const auto path = directory.filePath("variants.toonproj");
        QVERIFY(saveProject(fixture(), path, &error)); MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(window.openProject(path)); window.show(); window.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *document = window.canvas()->document(); const auto character = document->addCharacter("Hana");
        const auto role = document->addCharacterColor(character, "Skin", CharacterColorCategory::Skin, QColor("#edb995"));
        const auto night = variant(document->project().characters[0], "Night");
        QVERIFY(document->updateCharacterColor(character, role, "Skin", CharacterColorCategory::Skin, QColor("#6071a0"), nullptr, night));
        auto *dock = window.findChild<QDockWidget *>("referencesPanel"); QVERIFY(dock); dock->show(); dock->raise();
        auto *references = dock->findChild<ReferencePanel *>(); QVERIFY(references);
        references->findChild<QTabBar *>("referenceTabs")->setCurrentIndex(2);
        auto *colors = dock->findChild<CharacterColorPanel *>(); QVERIFY(colors); colors->selectCharacter(character); colors->selectVariant(night);
        auto *name = colors->findChild<QLineEdit *>("characterColorName"); QVERIFY(name);
        name->setFocus(); name->selectAll(); QTest::keyClicks(name, "Face base"); QVERIFY(name->isModified());
        const auto pixels = document->project().shots[0].layers[0].tiles;
        auto *save = window.findChild<QAction *>("saveProjectAction"); QVERIFY(save); save->trigger();
        QVERIFY(!window.isWindowModified()); QVERIFY(document->history()->isClean());
        const auto saved = loadProject(path, &error); QVERIFY2(saved, qPrintable(error));
        QCOMPARE(saved->characters[0].colors[0].name, QString("Face base"));
        QCOMPARE(saved->characters[0].colors[0].color, QColor("#edb995"));
        QCOMPARE(characterVariantColors(saved->characters[0], night)[0].color, QColor("#6071a0"));
        QCOMPARE(saved->shots[0].layers[0].tiles, pixels);
        QVERIFY(window.openProject(path)); colors->selectCharacter(character); colors->selectVariant(night);
        QCOMPARE(colors->findChild<QListWidget *>("characterColorList")->item(0)->text(), QString("Face base"));
        QVERIFY(!window.isWindowModified()); QVERIFY(window.close());
    }
    void paletteEditsUseNormalUnsavedCloseAndArtworkUndoKeepsPixels() {
        QTemporaryDir directory; QString error; const auto path = directory.filePath("close.toonproj");
        QVERIFY(saveProject(fixture(), path, &error)); MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(window.openProject(path)); auto *document = window.canvas()->document();
        const auto character = document->addCharacter("Hana");
        document->addCharacterColor(character, "Base", CharacterColorCategory::Skin, Qt::red);
        auto *save = window.findChild<QAction *>("saveProjectAction"); QVERIFY(save); save->trigger();
        const auto pixels = document->project().shots[0].layers[0].tiles;
        const auto custom = document->addCharacterColorVariant(character, "Moonlit", {}, &error); QVERIFY2(!custom.isEmpty(), qPrintable(error));
        QVERIFY(window.isWindowModified()); int prompts = 0; QTimer answer; answer.setInterval(5);
        connect(&answer, &QTimer::timeout, [&] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                ++prompts; if (auto *cancel = message->button(QMessageBox::Cancel)) cancel->click();
            }
        });
        answer.start(); QVERIFY(!window.close()); answer.stop(); QCOMPARE(prompts, 1);
        document->history()->undo(); QVERIFY(!window.isWindowModified());
        QCOMPARE(document->project().shots[0].layers[0].tiles, pixels); QVERIFY(window.close());
    }
};
QTEST_MAIN(CharacterVariantsWorkspaceTests)
#include "test_character_variants_workspace.moc"
