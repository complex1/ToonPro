#include "canvas.h"
#include "color_panel.h"
#include "palette_library.h"
#include "ui_theme.h"
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QScrollArea>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>
using namespace toon;
namespace {
void answerName(ColorPanel &panel, const QString &name) {
    auto *timer = new QTimer(&panel); timer->setInterval(5);
    QObject::connect(timer, &QTimer::timeout, &panel, [&panel, timer, name] {
        auto *dialog = panel.findChild<QInputDialog *>("colorPaletteNameDialog");
        if (!dialog || !dialog->isVisible()) return;
        timer->stop(); dialog->setTextValue(name); dialog->accept(); timer->deleteLater();
    });
    QTimer::singleShot(3000, timer, [timer] { timer->stop(); timer->deleteLater(); }); timer->start();
}
void chooseAction(ColorPanel &panel, const QString &menuName, const QString &actionName) {
    auto *timer = new QTimer(&panel); timer->setInterval(5);
    QObject::connect(timer, &QTimer::timeout, &panel, [&panel, timer, menuName, actionName] {
        auto *menu = panel.findChild<QMenu *>(menuName); if (!menu || !menu->isVisible()) return;
        QAction *action = nullptr;
        for (auto *candidate : menu->actions()) if (candidate->objectName() == actionName) { action = candidate; break; }
        if (!action) return;
        timer->stop(); menu->setActiveAction(action); QTest::keyClick(menu, Qt::Key_Return); timer->deleteLater();
    });
    QTimer::singleShot(3000, timer, [timer] { timer->stop(); timer->deleteLater(); }); timer->start();
}
void openSwatchMenu(QToolButton *button) {
    QMetaObject::invokeMethod(button, "customContextMenuRequested", Qt::DirectConnection, Q_ARG(QPoint, QPoint(5, 5)));
}
void openPaletteAction(ColorPanel &panel, const QString &actionName) {
    chooseAction(panel, "colorPaletteMenu", actionName);
    panel.findChild<QToolButton *>("colorPaletteMore")->click();
}
}
class PalettePanelTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void namedPaletteAndSwatchWorkflowPreservesArtworkHistory() {
        QTemporaryDir dir; PaletteLibrary library(dir.filePath("palettes.json")); QVERIFY(library.load());
        Canvas canvas; canvas.setProject(Project::create()); ColorPanel panel(&canvas); panel.setPaletteLibrary(&library); panel.resize(260, 480); panel.show();
        // A native popup opened before its first parent window becomes active
        // is dismissed by the delayed window activation event on macOS.
        panel.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&panel));
        const auto original = canvas.project().toJson(); QSignalSpy selected(&panel, &ColorPanel::paletteSelected);
        answerName(panel, "Hero colors"); openPaletteAction(panel, "colorPaletteAdd");
        QCOMPARE(library.entries().size(), 2); QCOMPARE(panel.currentPaletteId(), library.entries()[1].id);
        QVERIFY(!selected.isEmpty()); QVERIFY(!panel.findChild<QToolButton *>("colorSwatch0"));
        canvas.setBrushColor(QColor("#80442211")); answerName(panel, "Skin shadow"); panel.findChild<QToolButton *>("colorSwatchAdd")->click();
        const auto paletteId = panel.currentPaletteId(); QCOMPARE(library.palette(paletteId)->swatches.size(), 1);
        auto *swatch = panel.findChild<QToolButton *>("colorSwatch0"); QVERIFY(swatch); QVERIFY(swatch->isChecked());
        QVERIFY(swatch->toolTip().contains("Skin shadow")); QVERIFY(swatch->accessibleName().contains("#80442211"));
        canvas.setBrushColor(Qt::blue); swatch->click(); QCOMPARE(canvas.brush.color, QColor("#80442211"));
        canvas.setBrushColor(QColor("#bc673b")); openPaletteAction(panel, "colorSwatchUpdate");
        QCOMPARE(library.palette(paletteId)->swatches[0].color, QColor("#bc673b"));
        panel.findChild<QToolButton *>("backgroundColor")->click(); panel.findChild<QToolButton *>("colorSwatch0")->click();
        QCOMPARE(canvas.brush.backgroundColor, QColor("#bc673b")); QCOMPARE(canvas.brush.color, QColor("#bc673b"));
        answerName(panel, "Main skin"); chooseAction(panel, "colorSwatchMenu", "colorSwatchRenameAction");
        openSwatchMenu(panel.findChild<QToolButton *>("colorSwatch0")); QCOMPARE(library.palette(paletteId)->swatches[0].name, QString("Main skin"));
        answerName(panel, "Character A"); chooseAction(panel, "colorPaletteMenu", "colorPaletteRename");
        panel.findChild<QToolButton *>("colorPaletteMore")->click(); QCOMPARE(library.palette(paletteId)->name, QString("Character A"));
        QCOMPARE(panel.findChild<QComboBox *>("colorPaletteChoice")->currentText(), QString("Character A"));
        openPaletteAction(panel, "colorSwatchDelete"); QVERIFY(library.palette(paletteId)->swatches.isEmpty());
        QVERIFY(!panel.findChild<QAction *>("colorSwatchDelete")->isEnabled());
        chooseAction(panel, "colorPaletteMenu", "colorPaletteDelete"); panel.findChild<QToolButton *>("colorPaletteMore")->click();
        QCOMPARE(library.entries().size(), 1); QCOMPARE(panel.currentPaletteId(), standardColorPalette().id);
        QCOMPARE(canvas.project().toJson(), original); QCOMPARE(canvas.document()->history()->count(), 0); QVERIFY(canvas.document()->history()->isClean());
        PaletteLibrary reopened(library.filePath()); QVERIFY(reopened.load()); QCOMPARE(reopened.entries(), library.entries());
    }
    void selectingPalettesRetainsColorsAndDraftHex() {
        QTemporaryDir dir; PaletteLibrary library(dir.filePath("palettes.json")); QVERIFY(library.load());
        const auto custom = library.createPalette("Effects"); QVERIFY(!library.addSwatch(custom, "Glow", QColor("#aaffdd22")).isEmpty());
        Canvas canvas; canvas.setProject(Project::create()); ColorPanel panel(&canvas); panel.setPaletteLibrary(&library); panel.resize(220, 460); panel.show();
        const auto color = canvas.brush.color; panel.setCurrentPaletteId(custom); QCOMPARE(panel.currentPaletteId(), custom); QCOMPARE(canvas.brush.color, color);
        panel.findChild<QToolButton *>("colorSwatch0")->click(); QCOMPARE(canvas.brush.color, QColor("#aaffdd22"));
        panel.activateWindow(); QTest::qWait(20);
        auto *hex = panel.findChild<QLineEdit *>("colorHex"); hex->setFocus(); QTRY_VERIFY(hex->hasFocus());
        hex->selectAll(); QTest::keyClicks(hex, "#abcd");
        QVERIFY(hex->isModified()); canvas.setBrushColor(Qt::blue); QCOMPARE(hex->text(), QString("#abcd"));
        QVERIFY(library.renamePalette(custom, "Fire and glow")); QCOMPARE(hex->text(), QString("#abcd"));
        QVERIFY(QMetaObject::invokeMethod(hex, "editingFinished", Qt::DirectConnection)); QCOMPARE(hex->text(), QString("#0000FF"));
        QCOMPARE(panel.currentPaletteId(), custom); panel.setCurrentPaletteId("missing"); QCOMPARE(panel.currentPaletteId(), custom);
    }
    void damagedLibraryLeavesPickerAndFallbackSwatchesUsable() {
        QTemporaryDir dir; const auto path = dir.filePath("palettes.json"); QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("damaged"); file.close();
        PaletteLibrary library(path); QVERIFY(!library.load()); Canvas canvas; canvas.setProject(Project::create());
        ColorPanel panel(&canvas); panel.setPaletteLibrary(&library); panel.show();
        QVERIFY(panel.isEnabled()); QVERIFY(!panel.findChild<QAction *>("colorPaletteAdd")->isEnabled());
        QVERIFY(!panel.findChild<QToolButton *>("colorSwatchAdd")->isEnabled());
        QVERIFY(panel.findChild<QLabel *>("colorPaletteStatus")->accessibleName().contains("preserved"));
        panel.findChild<QToolButton *>("colorSwatch3")->click(); QCOMPARE(canvas.brush.color, QColor("#b54e55"));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("damaged"));
    }
    void externalChangeIsReportedWithoutOverwritingOrChangingColors() {
        QTemporaryDir dir; const auto path = dir.filePath("palettes.json"); PaletteLibrary first(path), second(path); QVERIFY(first.load()); QVERIFY(second.load());
        Canvas canvas; canvas.setProject(Project::create()); ColorPanel panel(&canvas); panel.setPaletteLibrary(&first); panel.show();
        panel.findChild<QToolButton *>("colorSwatch0")->click(); const auto before = canvas.brush.color;
        QVERIFY(!second.createPalette("Other window").isEmpty());
        openPaletteAction(panel, "colorSwatchUpdate"); QVERIFY(!first.writable());
        QVERIFY(panel.findChild<QLabel *>("colorPaletteStatus")->accessibleName().contains("changed outside"));
        QVERIFY(!panel.findChild<QAction *>("colorSwatchUpdate")->isEnabled()); QCOMPARE(canvas.brush.color, before);
        PaletteLibrary reopened(path); QVERIFY(reopened.load()); QCOMPARE(reopened.entries(), second.entries());
    }
    void compactPanelAndLongNamesStayWithinDock() {
        QTemporaryDir dir; PaletteLibrary library(dir.filePath("palettes.json")); QVERIFY(library.load());
        const auto palette = library.createPalette(QString(100, 'P')); QVERIFY(!palette.isEmpty());
        for (int i = 0; i < 20; ++i) QVERIFY(!library.addSwatch(palette, QString("Long descriptive swatch %1").arg(i), QColor::fromHsv(i * 17, 180, 220)).isEmpty());
        Canvas canvas; canvas.setProject(Project::create()); ColorPanel panel(&canvas); panel.setPaletteLibrary(&library); panel.setCurrentPaletteId(palette);
        panel.resize(220, 480); panel.show(); QTest::qWait(20); QCOMPARE(panel.width(), 220); QVERIFY(panel.minimumSizeHint().width() <= 220);
        auto *status = panel.findChild<QLabel *>("colorPaletteStatus"); const auto height = status->height();
        panel.findChild<QToolButton *>("colorSwatch0")->click(); QCOMPARE(status->height(), height);
        for (auto *button : panel.findChildren<QToolButton *>()) {
            QVERIFY(!button->accessibleName().isEmpty()); QVERIFY(!button->toolTip().isEmpty()); QVERIFY(!button->icon().isNull());
            const auto point = button->mapTo(&panel, QPoint()); QVERIFY(point.x() >= 0); QVERIFY(point.x() + button->width() <= panel.width());
        }
        const auto output = qEnvironmentVariable("TOON_UI_PREVIEW", "test-artifacts"); QVERIFY(QDir().mkpath(output));
        QVERIFY(panel.grab().save(QDir(output).filePath("named-color-palettes.png")));
    }
    void emptyPaletteLibraryCanBeRecreatedFromKeyboardOverflow() {
        QTemporaryDir dir; PaletteLibrary library(dir.filePath("palettes.json")); QVERIFY(library.load());
        QVERIFY(library.removePalette(library.entries().first().id));
        Canvas canvas; canvas.setProject(Project::create()); ColorPanel panel(&canvas); panel.setPaletteLibrary(&library);
        panel.resize(220, 350); panel.show(); panel.activateWindow(); QTest::qWait(20);
        auto *more = panel.findChild<QToolButton *>("colorPaletteMore"); QVERIFY(more->isEnabled());
        QVERIFY(!panel.findChild<QToolButton *>("colorSwatchAdd")->isEnabled());
        answerName(panel, "New working colors"); chooseAction(panel, "colorPaletteMenu", "colorPaletteAdd");
        more->setFocus(); QTest::keyClick(more, Qt::Key_Space);
        QCOMPARE(library.entries().size(), 1); QCOMPARE(library.entries().first().name, QString("New working colors"));
        QVERIFY(panel.findChild<QToolButton *>("colorSwatchAdd")->isEnabled());
        QCOMPARE(canvas.document()->history()->count(), 0);
    }
};
QTEST_MAIN(PalettePanelTest)
#include "palette_panel_tests.moc"
