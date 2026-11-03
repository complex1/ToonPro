#include "shots_panel.h"
#include "document.h"
#include "ui_theme.h"
#include <QComboBox>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStyle>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

using namespace toon;
namespace {
Project movie() {
    auto project = Project::create(); project.movie = "A day in the city"; project.fps = 24;
    project.shots.append(Project::create().shots.first()); project.shots.append(Project::create().shots.first());
    const QStringList names{"City sunrise", "A chance encounter", "Forest farewell"};
    for (int i = 0; i < project.shots.size(); ++i) {
        auto &shot = project.shots[i]; shot.number = QString("C%1").arg(i + 1, 3, 10, QLatin1Char('0'));
        shot.name = names[i]; shot.durationFrames = (i + 1) * 24; shot.scene = i == 2 ? "Forest" : "City";
    }
    return project;
}
QTreeWidgetItem *row(QTreeWidget *tree, const QString &id) {
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto *item = tree->topLevelItem(i); if (item->data(0, Qt::UserRole).toString() == id) return item;
    }
    return nullptr;
}
void selectOnly(QTreeWidget *tree, const QString &id) {
    // Match the view's SelectRows interaction. Without Rows this selects only
    // the name cell; replacing an already selected row then marks its item
    // deselected in QTreeWidget even while that one cell stays selected.
    tree->setCurrentItem(row(tree, id), 1, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}
QStringList movieIds(const Document &document) {
    QStringList ids; for (const auto &shot : document.project().shots) ids.append(shot.id); return ids;
}
QToolButton *button(ShotsPanel &panel, const char *name) { return panel.findChild<QToolButton *>(name); }
void assignScene(ShotsPanel &panel, const QString &name) {
    auto *combo = panel.findChild<QComboBox *>("shotsScene"); auto *edit = combo->lineEdit();
    edit->setFocus(); edit->selectAll(); QTest::keyClicks(edit, name); QTest::keyClick(edit, Qt::Key_Return);
}
}

class ShotsPanelTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }

    void switchingUsesGuardAndCanceledSelectionIsRestored() {
        Document document; auto project = movie(); document.load(project);
        bool allowed = false; int transitions = 0;
        ShotsPanel panel(&document, [&] { ++transitions; return allowed; }); panel.resize(300, 420); panel.show();
        auto *tree = panel.findChild<QTreeWidget *>("shotsList"); QVERIFY(tree); QCOMPARE(tree->topLevelItemCount(), 3);
        const auto first = project.shots[0].id, second = project.shots[1].id;
        selectOnly(tree, second); QTRY_COMPARE(transitions, 1); QCOMPARE(document.activeShotId(), first);
        QCOMPARE(panel.selectedShotIds(), QStringList{first}); QCOMPARE(tree->currentItem(), row(tree, first));
        QCOMPARE(document.history()->count(), 0);
        allowed = true; selectOnly(tree, second); QTRY_COMPARE(document.activeShotId(), second);
        QCOMPARE(transitions, 2); QCOMPARE(panel.selectedShotIds(), QStringList{second});
        QVERIFY(row(tree, second)->font(1).bold()); QVERIFY(!row(tree, first)->font(1).bold());
        QCOMPARE(row(tree, second)->text(2), QString("2.00 s"));
        QVERIFY(panel.findChild<QLabel *>("shotsSummary")->text().contains("6.00 s"));
        QCOMPARE(document.history()->count(), 0);
        // An external navigation/Undo must supersede a click not yet settled.
        selectOnly(tree, first); QVERIFY(document.selectShot(project.shots[2].id)); QTest::qWait(1);
        QCOMPARE(document.activeShotId(), project.shots[2].id);
        QCOMPARE(panel.selectedShotIds(), QStringList{project.shots[2].id}); QCOMPARE(transitions, 2);
    }

    void createDuplicateDeleteAndUndo() {
        Document document; document.load(movie()); ShotsPanel panel(&document, [] { return true; }); panel.show();
        const auto initial = document.activeShotId(); auto *tree = panel.findChild<QTreeWidget *>("shotsList");
        button(panel, "shotsAdd")->click(); QCOMPARE(document.project().shots.size(), 4);
        const auto created = document.activeShotId(); QVERIFY(created != initial);
        QCOMPARE(panel.selectedShotIds(), QStringList{created}); QCOMPARE(document.activeShot().scene, QString("City"));
        button(panel, "shotsDuplicate")->click(); QCOMPARE(document.project().shots.size(), 5);
        const auto duplicate = document.activeShotId(); QVERIFY(duplicate != created);
        QVERIFY(document.activeShot().name.endsWith(" copy"));
        button(panel, "shotsDelete")->click(); QCOMPARE(document.project().shots.size(), 4); QVERIFY(!row(tree, duplicate));
        document.history()->undo(); QCOMPARE(document.project().shots.size(), 5); QVERIFY(row(tree, duplicate));
        QCOMPARE(document.activeShotId(), duplicate);
        document.history()->redo(); QCOMPARE(document.project().shots.size(), 4);
        QVERIFY(document.activeShotId() != duplicate);
    }

    void canceledMutationsLeaveMovieAndSceneDraftUntouched() {
        Document document; document.load(movie()); bool allowed = false; int transitions = 0;
        ShotsPanel panel(&document, [&] { ++transitions; return allowed; }); panel.show();
        const auto before = movieIds(document); const auto originalScene = document.activeShot().scene;
        for (const auto *id : {"shotsAdd", "shotsDuplicate", "shotsDelete", "shotsMoveDown"}) {
            button(panel, id)->click(); QCOMPARE(movieIds(document), before); QCOMPARE(document.history()->count(), 0);
        }
        QSignalSpy exports(&panel, &ShotsPanel::exportRequested); button(panel, "shotsExport")->click(); QCOMPARE(exports.count(), 0);
        assignScene(panel, "Rooftop"); QCOMPARE(document.activeShot().scene, originalScene);
        QCOMPARE(panel.findChild<QComboBox *>("shotsScene")->currentText(), QString("Rooftop"));
        QVERIFY(transitions >= 6); QCOMPARE(document.history()->count(), 0);
        allowed = true;
        QMetaObject::invokeMethod(panel.findChild<QComboBox *>("shotsScene")->lineEdit(), "editingFinished", Qt::DirectConnection);
        QCOMPARE(document.activeShot().scene, QString("Rooftop"));
    }

    void inlineNumberAndNamePreserveDraftsOnUnrelatedRefresh() {
        Document document; document.load(movie()); ShotsPanel panel(&document, [] { return true; });
        panel.resize(340, 440); panel.show(); auto *tree = panel.findChild<QTreeWidget *>("shotsList");
        const auto id = document.activeShotId(); tree->editItem(row(tree, id), 1);
        auto *edit = tree->findChild<QLineEdit *>("shotsInlineEditor"); QVERIFY(edit);
        edit->selectAll(); QTest::keyClicks(edit, "The last train"); document.addLayer();
        QCOMPARE(edit->text(), QString("The last train")); QCOMPARE(document.activeShot().name, QString("City sunrise"));
        QTest::keyClick(edit, Qt::Key_Return); QTRY_COMPARE(document.activeShot().name, QString("The last train"));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        tree->editItem(row(tree, id), 0); edit = tree->findChild<QLineEdit *>("shotsInlineEditor"); QVERIFY(edit);
        edit->selectAll(); QTest::keyClicks(edit, "C105"); QTest::keyClick(edit, Qt::Key_Return);
        QTRY_COMPARE(document.activeShot().number, QString("C105"));
        document.history()->undo(); QCOMPARE(document.activeShot().number, QString("C001"));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        tree->editItem(row(tree, id), 2); QVERIFY(!tree->findChild<QLineEdit *>("shotsInlineEditor"));
    }

    void multiSelectionExportsInMovieOrderAndBatchScenesUndoOnce() {
        Document document; auto project = movie(); document.load(project);
        ShotsPanel panel(&document, [] { return true; }); panel.resize(340, 420); panel.show(); auto *tree = panel.findChild<QTreeWidget *>("shotsList");
        const auto first = project.shots[0].id, third = project.shots[2].id;
        row(tree, third)->setSelected(true); QTest::qWait(1);
        QCOMPARE(panel.selectedShotIds(), (QStringList{first, third}));
        QSignalSpy exports(&panel, &ShotsPanel::exportRequested); button(panel, "shotsExport")->click(); QCOMPARE(exports.count(), 1);
        QCOMPARE(exports.first().first().toStringList(), (QStringList{first, third}));
        const auto oldHistory = document.history()->count(); assignScene(panel, "Finale");
        QCOMPARE(document.project().shots[0].scene, QString("Finale")); QCOMPARE(document.project().shots[2].scene, QString("Finale"));
        QCOMPARE(document.project().shots[1].scene, QString("City")); QCOMPARE(document.activeShotId(), first);
        QCOMPARE(document.history()->count(), oldHistory + 1); document.history()->undo();
        QCOMPARE(document.project().shots[0].scene, QString("City")); QCOMPARE(document.project().shots[2].scene, QString("Forest"));
        // Use real row clicks after Undo: the newest multi-selection must
        // survive the queued selection refresh and become the delete targets.
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, tree->visualItemRect(row(tree, first)).center());
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::ControlModifier, tree->visualItemRect(row(tree, third)).center());
        QCOMPARE(panel.selectedShotIds(), (QStringList{first, third}));
        QTest::qWait(1);
        QCOMPARE(panel.selectedShotIds(), (QStringList{first, third}));
        QCOMPARE(document.activeShotId(), third);
        const auto beforeDelete = document.history()->count(); button(panel, "shotsDelete")->click();
        QCOMPARE(document.project().shots.size(), 1); QVERIFY(!button(panel, "shotsDelete")->isEnabled());
        // The preceding scene command was undone, so deleting may replace its redo branch.
        QVERIFY(document.history()->count() <= beforeDelete + 1); document.history()->undo();
        QCOMPARE(document.project().shots.size(), 3);
    }

    void sceneFilterHidesSelectionsWithoutChangingActiveShot() {
        Document document; auto project = movie(); document.load(project); int transitions = 0;
        ShotsPanel panel(&document, [&] { ++transitions; return true; }); panel.show();
        auto *filter = panel.findChild<QComboBox *>("shotsSceneFilter"); auto *tree = panel.findChild<QTreeWidget *>("shotsList");
        filter->setCurrentIndex(filter->findData("Forest")); QCOMPARE(document.activeShotId(), project.shots[0].id);
        QVERIFY(panel.selectedShotIds().isEmpty()); QVERIFY(!button(panel, "shotsExport")->isEnabled());
        QVERIFY(row(tree, project.shots[0].id)->isHidden()); QVERIFY(!row(tree, project.shots[2].id)->isHidden());
        QCOMPARE(transitions, 0); selectOnly(tree, project.shots[2].id); QTRY_COMPARE(document.activeShotId(), project.shots[2].id);
        button(panel, "shotsAdd")->click(); QCOMPARE(document.activeShot().scene, QString("Forest"));
        QCOMPARE(filter->currentData().toString(), QString("Forest"));
        document.selectShot(project.shots[0].id); QCOMPARE(filter->currentData().toString(), QString());
        QCOMPARE(panel.selectedShotIds(), QStringList{project.shots[0].id});
    }

    void moveButtonsAndDropCommandShareGuardAndStableOrder() {
        Document document; auto project = movie(); document.load(project); bool allowed = true;
        ShotsPanel panel(&document, [&] { return allowed; }); panel.show();
        const auto first = project.shots[0].id, second = project.shots[1].id, third = project.shots[2].id;
        QVERIFY(!button(panel, "shotsMoveUp")->isEnabled()); button(panel, "shotsMoveDown")->click();
        QCOMPARE(movieIds(document), (QStringList{second, first, third})); QCOMPARE(document.activeShotId(), first);
        QCOMPARE(panel.selectedShotIds(), QStringList{first}); document.history()->undo();
        QVERIFY(QMetaObject::invokeMethod(&panel, "moveDropped", Qt::DirectConnection,
            Q_ARG(QString, first), Q_ARG(QString, third), Q_ARG(bool, false)));
        QCOMPARE(movieIds(document), (QStringList{second, third, first}));
        allowed = false;
        QVERIFY(QMetaObject::invokeMethod(&panel, "moveDropped", Qt::DirectConnection,
            Q_ARG(QString, first), Q_ARG(QString, second), Q_ARG(bool, true)));
        QCOMPARE(movieIds(document), (QStringList{second, third, first}));
        auto *tree = panel.findChild<QTreeWidget *>("shotsList"); QCOMPARE(tree->dragDropMode(), QAbstractItemView::InternalMove);
        QVERIFY(tree->dragEnabled()); QVERIFY(tree->acceptDrops());
    }

    void narrowPanelAndUnavailableDocument() {
        auto *document = new Document; ShotsPanel panel(document, [] { return true; }); panel.resize(260, 300); panel.show();
        auto *tree = panel.findChild<QTreeWidget *>("shotsList"); QVERIFY(!tree->isEnabled());
        document->load(movie()); QTest::qWait(1); QVERIFY(tree->isEnabled()); QCOMPARE(panel.width(), 260);
        QCOMPARE(tree->horizontalScrollBar()->maximum(), 0);
        for (const auto *id : {"shotsAdd", "shotsDuplicate", "shotsDelete", "shotsMoveUp", "shotsMoveDown", "shotsExport"}) {
            auto *control = button(panel, id); QVERIFY(control); QVERIFY(!control->icon().isNull());
            QVERIFY(!control->toolTip().isEmpty()); QVERIFY(!control->accessibleName().isEmpty());
            QCOMPARE(control->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay, nullptr, control), 600);
            const QRect rect(control->mapTo(&panel, QPoint()), control->size());
            QVERIFY(rect.left() >= 0 && rect.right() < panel.width());
        }
        delete document; QVERIFY(!tree->isEnabled()); QCOMPARE(tree->topLevelItemCount(), 0);
    }

    void shotPreviews() {
        const auto path = qEnvironmentVariable("TOON_UI_PREVIEW"); if (path.isEmpty()) QSKIP("Set TOON_UI_PREVIEW to save shot-panel previews.");
        QVERIFY(QDir().mkpath(path)); Document document; document.load(movie()); ShotsPanel panel(&document, [] { return true; });
        panel.resize(330, 480); panel.show(); QTest::qWait(1); QVERIFY(panel.grab().save(QDir(path).filePath("shots-panel.png")));
        panel.resize(260, 300); QTest::qWait(1); QVERIFY(panel.grab().save(QDir(path).filePath("shots-panel-narrow.png")));
    }
};
QTEST_MAIN(ShotsPanelTest)
#include "test_shots_panel.moc"
