#include "audio_export_dialog.h"
#include "canvas.h"
#include "clipboard_tools.h"
#include "dashboard_gallery.h"
#include "document.h"
#include "main_window.h"
#include "managed_project_store.h"
#include "movie_review_controller.h"
#include "project_workspace_window.h"
#include "sheet.h"
#include "timeline_controller.h"
#include "value_slider.h"
#include "video_export_dialog.h"
#include "workspace_setup_widgets.h"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>
using namespace toon;
namespace {
Project drawing(const QString &name = "Drawing", int width = 320, int height = 240) {
    return newManagedDocument(name, false, width, height, Qt::white, false);
}
void paint(Document *doc) {
    BrushSettings brush;
    brush.color = Qt::red;
    brush.size = 14;
    doc->beginStroke({30, 30}, 1, brush, false);
    doc->continueStroke({80, 70}, 1);
    doc->endStroke();
}
bool write(const QString &p, const QByteArray &bytes) {
    QFile f(p);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
} // namespace
class ManagedProjectTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        applyTheme();
        if (qEnvironmentVariableIsSet("TOON_MANAGED_LARGE_TEXT")) {
            auto font = qApp->font();
            font.setPointSizeF(16);
            qApp->setFont(font);
        }
        QVERIFY(QSqlDatabase::isDriverAvailable("QSQLITE"));
    }
    void independentDocumentsAndCustomBackground() {
        QTemporaryDir d;
        QString e;
        auto store = ManagedProjectStore::create(d.filePath("Test.toonstudio"), "Test", "drawing", &e);
        QVERIFY2(store, qPrintable(e));
        auto a = store->createDocument("A", "drawing", drawing("A"), {}, {}, &e);
        auto b = store->createDocument("B", "drawing", drawing("B", 512, 384), {}, {}, &e);
        QVERIFY2(!a.isEmpty() && !b.isEmpty(), qPrintable(e));
        auto items = store->items();
        QCOMPARE(items.size(), 2);
        auto pa = store->loadDocument(a, false, &e), pb = store->loadDocument(b, false, &e);
        QVERIFY(pa && pb);
        QCOMPARE(pa->width, 320);
        QCOMPARE(pb->width, 512);
        QCOMPARE(pb->height, 384);
        Document doc;
        doc.load(*pa);
        paint(&doc);
        auto revised = store->saveDocument(a, items[0].revision, doc.project(), false, &e);
        QVERIFY2(!revised.isEmpty(), qPrintable(e));
        auto unchanged = store->loadDocument(b, false, &e);
        QCOMPARE(unchanged->toJson(), pb->toJson());
        auto colored = newManagedDocument("Color", false, 320, 240, QColor("#5888aa"), false);
        QCOMPARE(renderMovieFrame(colored).pixelColor(20, 20), QColor("#5888aa"));
    }
    void singleShotAndProjectTypeConstraints() {
        QTemporaryDir d;
        QString e;
        auto store = ManagedProjectStore::create(d.filePath("Test.toonstudio"), "Test", "animation", &e);
        auto p = newManagedDocument("Shot", true, 320, 240, Qt::white, false, 30, 90);
        QCOMPARE(p.shots[0].animationTracks.size(), 1);
        QCOMPARE(p.shots[0].animationTracks[0].exposures.size(), 1);
        QVERIFY2(!store->createDocument("Shot", "animation", p, {}, {}, &e).isEmpty(), qPrintable(e));
        QVERIFY(store->createDocument("Bad", "drawing", drawing(), {}, {}, &e).isEmpty());
        p.shots.append(drawing().shots[0]);
        QVERIFY(store->createDocument("Two shots", "animation", p, {}, {}, &e).isEmpty());
        Document doc;
        doc.load(drawing());
        doc.setSingleShotEditing(true);
        QVERIFY(doc.createShot().isEmpty());
        QVERIFY(!doc.deleteShot(doc.activeShotId()));
        QCOMPARE(doc.project().shots.size(), 1);
    }
    void staleCancelledSaveAndRecovery() {
        QTemporaryDir d;
        QString e;
        auto s = ManagedProjectStore::create(d.filePath("Test.toonstudio"), "Test", "drawing", &e);
        auto id = s->createDocument("A", "drawing", drawing(), {}, {}, &e);
        auto first = s->items().first().revision;
        Document doc;
        doc.load(drawing());
        paint(&doc);
        QVERIFY(s->saveDocument(id, first, doc.project(), false, &e, [] { return true; }).isEmpty());
        QCOMPARE(s->items().first().revision, first);
        QVERIFY(s->saveDocument(id, "stale", doc.project(), false, &e).isEmpty());
        auto recovery = s->saveDocument(id, first, doc.project(), true, &e);
        QVERIFY2(!recovery.isEmpty(), qPrintable(e));
        QCOMPARE(s->items().first().revision, first);
        QCOMPARE(s->items().first().recovery, recovery);
        auto recovered = s->loadDocument(id, true, &e);
        QVERIFY(recovered);
        QVERIFY(!recovered->shots[0].layers[0].tiles.isEmpty());
        auto saved = s->saveDocument(id, first, doc.project(), false, &e);
        QVERIFY2(!saved.isEmpty(), qPrintable(e));
        QVERIFY(s->items().first().recovery.isEmpty());
    }
    void folderTagsMoveUndoRetainSource() {
        QTemporaryDir d;
        QString e;
        auto s = ManagedProjectStore::create(d.filePath("Test.toonstudio"), "Test", "movie", &e);
        auto parent = s->createFolder("Opening", {}, "content", &e);
        auto child = s->createFolder("Station", parent, "content", &e);
        auto assets = s->createFolder("Artwork", {}, "assets", &e);
        QVERIFY(!parent.isEmpty() && !child.isEmpty() && !assets.isEmpty());
        QVERIFY(s->createFolder("Wrong", parent, "assets", &e).isEmpty());
        auto id = s->createDocument("A", "drawing", drawing(), child, {"Background"}, &e);
        auto before = s->items().first();
        QVERIFY2(s->organize({id}, "assets", assets, {"Background", "Night", "night"}, &e), qPrintable(e));
        auto moved = s->items().first();
        QCOMPARE(moved.id, before.id);
        QCOMPARE(moved.path, before.path);
        QCOMPARE(moved.revision, before.revision);
        QCOMPARE(moved.tags, QStringList({"Background", "Night"}));
        QVERIFY(s->loadDocument(id, false, &e));
        QVERIFY2(s->undoOrganization(&e), qPrintable(e));
        QCOMPARE(s->items().first().collection, QString("content"));
        QCOMPARE(s->items().first().folder, child);
        QVERIFY(s->setRemoved({id}, true, &e));
        QVERIFY(s->loadDocument(id, false, &e));
        QVERIFY(s->setRemoved({id}, false, &e));
    }
    void batchMoveRetainsEachItemsTags() {
        QTemporaryDir d;
        QString e;
        auto store = ManagedProjectStore::create(d.filePath("Test.toonstudio"), "Test", "movie", &e);
        QVERIFY(store);
        auto a = store->createDocument("A", "drawing", drawing("A"), {}, {"Background"}, &e);
        auto b = store->createDocument("B", "drawing", drawing("B"), {}, {"Character sheet"}, &e);
        QVERIFY(!a.isEmpty() && !b.isEmpty());
        QVERIFY(store->organize({a, b}, "assets", {}, {"Reusable"}, &e, true));
        auto items = store->items();
        QCOMPARE(items[0].tags, QStringList({"Background", "Reusable"}));
        QCOMPARE(items[1].tags, QStringList({"Character sheet", "Reusable"}));
        QVERIFY(store->undoOrganization(&e));
        QCOMPARE(store->items()[0].tags, QStringList({"Background"}));
        QCOMPARE(store->items()[1].tags, QStringList({"Character sheet"}));
    }
    void importsAreAtomicAndPreserveOriginals() {
        QTemporaryDir d;
        QString e;
        auto s = ManagedProjectStore::create(d.filePath("Test.toonstudio"), "Test", "movie", &e);
        auto source = d.filePath("source.mp4");
        QVERIFY(write(source, "original media"));
        QVERIFY(!s->importFiles({source, source}, {}, {"Reference"}, &e, [] { return true; }));
        QVERIFY(s->items().isEmpty());
        int progress = 0;
        QVERIFY(!s->importFiles(
            {source, source}, {}, {}, &e, [&] { return progress == 1; }, [&](int n, int) { progress = n; }));
        QVERIFY(s->items().isEmpty());
        QVERIFY2(s->importFiles({source, source}, {}, {"Reference"}, &e), qPrintable(e));
        QCOMPARE(s->items().size(), 2);
        QVERIFY(s->items()[0].id != s->items()[1].id);
        QVERIFY(QFileInfo::exists(source));
        auto path = s->assetPath(s->items().first().id, &e);
        QVERIFY(QFileInfo::exists(path));
        QVERIFY(QFile::remove(source));
        QVERIFY(QFileInfo::exists(path));
    }
    void backupReopenAndWriterLease() {
        QTemporaryDir d;
        QString e;
        auto s = ManagedProjectStore::create(d.filePath("Test.toonstudio"), "Test", "movie", &e);
        auto id = s->createDocument("A", "drawing", drawing(), {}, {"Asset"}, &e);
        QVERIFY(!id.isEmpty());
        QVERIFY(!ManagedProjectStore::open(s->root(), &e));
        const auto backup = d.filePath("Backup.toonstudio");
        QVERIFY2(s->backup(backup, &e), qPrintable(e));
        auto copied = ManagedProjectStore::open(backup, &e);
        QVERIFY2(copied, qPrintable(e));
        QVERIFY(copied->loadDocument(id, false, &e));
        QCOMPARE(copied->items().first().tags, QStringList({"Asset"}));
        QVERIFY(!s->backup(d.filePath("Cancel.toonstudio"), &e, [] { return true; }));
        QVERIFY(!QFileInfo::exists(d.filePath("Cancel.toonstudio")));
    }
    void tabsRetainIndependentArtworkHistoryAndView() {
        QTemporaryDir d;
        ProjectWorkspaceWindow w(d.filePath("management.ini"));
        QString e;
        QVERIFY(w.createManagedProject(d.filePath("Test.toonstudio"), "Test", "movie", &e));
        auto a = w.store()->createDocument("A", "drawing", drawing("A"), {}, {}, &e);
        auto b = w.store()->createDocument("B", "drawing", drawing("B", 512, 384), {}, {}, &e);
        w.show();
        QApplication::processEvents();
        QVERIFY2(w.openItems({a, b}), qPrintable(e));
        auto *tabs = w.findChild<QTabWidget *>("documentTabs");
        QCOMPARE(tabs->count(), 3);
        auto *navigation = w.findChild<QToolBar *>("managementNavigation");
        const auto singleHeaderRow = [&] {
            const QRect actions(navigation->mapTo(&w, QPoint()), navigation->size());
            const QRect documents(tabs->tabBar()->mapTo(&w, QPoint()), tabs->tabBar()->size());
            return navigation->isVisible() && actions.right() < documents.left() &&
                   actions.united(documents).height() <= qMax(actions.height(), documents.height()) + 4;
        };
        QTRY_VERIFY(singleHeaderRow());
        auto *ea = w.editorForItem(a), *eb = w.editorForItem(b);
        QVERIFY(ea && eb);
        paint(ea->canvas()->document());
        QVERIFY(ea->managedDirty());
        QCOMPARE(ea->canvas()->document()->history()->count(), 1);
        QCOMPARE(eb->canvas()->document()->history()->count(), 0);
        ea->canvas()->setRotation(23);
        tabs->setCurrentWidget(ea);
        QCOMPARE(tabs->currentWidget(), ea);
        tabs->setCurrentIndex(0);
        tabs->setCurrentWidget(ea);
        QCOMPARE(ea->canvas()->rotation(), 23.);
        QCOMPARE(ea->canvas()->document()->history()->count(), 1);
        QVERIFY(ea->saveManaged());
        QVERIFY(!ea->managedDirty());
        QCOMPARE(eb->canvas()->project().width, 512);
        tabs->setCurrentWidget(ea);
        BrushSettings pendingBrush;
        QVERIFY(ea->canvas()->document()->beginStroke({40, 40}, 1, pendingBrush, false));
        QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                          tabs->tabBar()->tabRect(tabs->indexOf(eb)).center());
        QCOMPARE(tabs->currentWidget(), ea);
        QVERIFY(ea->canvas()->document()->strokeActive());
        ea->canvas()->document()->cancelStroke();
        QVERIFY(w.openItems({a}));
        QCOMPARE(tabs->count(), 3);
        auto *timeline = ea->findChild<TimelineController *>();
        QVERIFY(!timeline->movieReview()->setMode(ReviewMode::Movie));
        QVERIFY(!ea->applyWorkspace("storyboard"));
        QVERIFY(!ea->findChild<QDockWidget *>("shotsPanel")->toggleViewAction()->isEnabled());
        QVERIFY(!ea->findChild<QTabBar *>("timelineReviewModes")->isVisible());
        const auto output = qEnvironmentVariable("TOON_MANAGED_CAPTURE");
        if (!output.isEmpty()) {
            QDir().mkpath(output);
            QTest::qWait(40); // Settle deferred dock layout before capturing its final geometry.
            for(auto *bar:ea->findChildren<QTabBar *>(QString(),Qt::FindDirectChildrenOnly))
                if(bar->isVisible())QVERIFY(!bar->geometry().intersects(ea->menuBar()->geometry()));
            w.grab().save(output + "/editor-tabs.png");
            w.resize(960, 680);
            QTest::qWait(40);
            QVERIFY(singleHeaderRow());
            w.grab().save(output + "/editor-tabs-narrow.png");
            w.resize(1440, 960);
            tabs->setCurrentIndex(0);
            w.refreshDashboard();
            QApplication::processEvents();
            w.grab().save(output + "/project-dashboard.png");
        }
        w.close();
    }
    void retainedRecoveryIsBoundedAndMissingCatalogNeverRecreated() {
        QTemporaryDir d;
        QString e;
        auto store = ManagedProjectStore::create(d.filePath("Test.toonstudio"), "Test", "drawing", &e);
        QVERIFY(store);
        auto p = drawing();
        auto id = store->createDocument("A", "drawing", p, {}, {}, &e);
        QVERIFY(!id.isEmpty());
        auto revision = store->items().first().revision;
        for (int i = 0; i < 5; ++i) {
            revision = store->saveDocument(id, revision, p, false, &e);
            QVERIFY2(!revision.isEmpty(), qPrintable(e));
        }
        for (int i = 0; i < 5; ++i)
            QVERIFY(!store->saveDocument(id, revision, p, true, &e).isEmpty());
        auto dir = QDir(store->root() + "/documents/" + id + "/revisions");
        QCOMPARE(dir.entryList({"*.toonproj"}, QDir::Files).size(), 3);
        const auto catalog = store->root() + "/project.sqlite";
        QVERIFY(QFile::rename(catalog, catalog + ".offline"));
        QVERIFY(store->items(&e).isEmpty());
        QVERIFY(!QFileInfo::exists(catalog));
        QVERIFY(!e.isEmpty());
        QVERIFY(QFile::rename(catalog + ".offline", catalog));
    }
    void realCreationDialogsAndIndependentAnimationTabs() {
        QTemporaryDir d;
        ProjectWorkspaceWindow w(d.filePath("management.ini"));
        w.show();
        QApplication::processEvents();
        QTimer projectResponder;
        projectResponder.setInterval(5);
        connect(&projectResponder, &QTimer::timeout, &w, [&] {
            auto *dialog = w.findChild<QDialog *>("managedProjectDialog");
            if (!dialog || !dialog->isVisible())
                return;
            projectResponder.stop();
            dialog->findChild<QLineEdit *>("managedProjectName")->setText("Created");
            auto *types = dialog->findChild<QTabBar *>("managedProjectType");
            QVERIFY(types);
            types->setCurrentIndex(2);
            dialog->findChild<QPushButton *>("managedCreateProject")->click();
        });
        // Write the destination through the same settings key the location UI owns.
        QSettings settings(d.filePath("management.ini"), QSettings::IniFormat);
        settings.setValue("workspace", d.path());
        settings.sync();
        projectResponder.start();
        w.findChild<QAction *>("managedNewProject")->trigger();
        projectResponder.stop();
        QVERIFY(w.store());
        QCOMPARE(w.store()->type(), QString("movie"));
        QTimer drawingResponder;
        drawingResponder.setInterval(5);
        connect(&drawingResponder, &QTimer::timeout, &w, [&] {
            auto *dialog = w.findChild<QDialog *>("managedDocumentDialog");
            if (!dialog || !dialog->isVisible())
                return;
            drawingResponder.stop();
            dialog->findChild<QLineEdit *>("newDocumentName")->setText("Created drawing");
            dialog->findChild<QPushButton *>("createManagedDocument")->click();
        });
        drawingResponder.start();
        w.findChild<QPushButton *>("managedNewDrawing")->click();
        QTRY_COMPARE(w.findChild<QTabWidget *>("documentTabs")->count(), 2);
        auto items = w.store()->items();
        QCOMPARE(items.size(), 1);
        auto a = items[0].id;
        auto shot = newManagedDocument("Shot", true, 320, 240, Qt::white, false, 30, 90);
        QString e;
        auto id = w.store()->createDocument("Shot", "animation", shot, {}, {}, &e);
        QVERIFY2(!id.isEmpty(), qPrintable(e));
        QVERIFY(w.openItems({a, id}));
        QCOMPARE(w.findChild<QTabWidget *>("documentTabs")->count(), 3);
        auto *editor = w.editorForItem(id);
        QVERIFY(editor);
        QCOMPARE(editor->canvas()->project().fps, 30.);
        QCOMPARE(editor->canvas()->shot().durationFrames, 90);
        auto *doc = editor->canvas()->document();
        QVERIFY(doc->setCurrentFrame(2));
        QVERIFY(doc->createAnimationDrawing(false, 2));
        QVERIFY(editor->saveManaged());
        VideoExportDialog video(editor->canvas()->project(), 0, 0, 89, false, &w);
        video.setSingleShotOnly();
        video.findChild<QTabBar *>("videoExportScope")->setCurrentIndex(1);
        QCOMPARE(video.settings().scope, VideoExportScope::Shot);
        QVERIFY(video.findChild<QTabBar *>("videoExportScope")->isHidden());
        AudioExportDialog audio(editor->canvas()->project(), 0, 0, 89, false, &w);
        audio.setSingleShotOnly();
        audio.findChild<QTabBar *>("audioExportScope")->setCurrentIndex(1);
        QCOMPARE(audio.settings().scope, AudioExportScope::Shot);
        const auto output = qEnvironmentVariable("TOON_MANAGED_CAPTURE");
        if (!output.isEmpty()) {
            QDir().mkpath(output);
            QApplication::processEvents();
            for (auto *tb : editor->findChildren<QTabBar *>(QString(), Qt::FindDirectChildrenOnly))
                if (tb->isVisible())
                    QVERIFY(!tb->geometry().intersects(editor->menuBar()->geometry()));
            w.grab().save(output + "/animation-tab.png");
        }
        w.close();
    }
    void dashboardGalleriesFollowProjectTypeAndRenderArtwork() {
        QTemporaryDir d;
        ProjectWorkspaceWindow w(d.filePath("management.ini"));
        w.show();
        auto *items = w.findChild<DashboardGallery *>("managedItems");
        auto *collections = w.findChild<QTabWidget *>("projectCollections");
        auto *folders = w.findChild<QTreeWidget *>("managedFolders");
        auto *inspector = w.findChild<QWidget *>("assetInspector");
        auto *typeFilters = w.findChild<QTabBar *>("assetTypeFilters");
        const auto output = qEnvironmentVariable("TOON_MANAGED_CAPTURE");
        auto capture = [&](const QString &name) {
            if (!output.isEmpty()) {
                QDir().mkpath(output);
                QApplication::processEvents();
                auto *surface = QApplication::activeModalWidget();
                (surface ? surface : &w)->grab().save(output + "/" + name + ".png");
            }
        };
        QImage art(QFINDTESTDATA("../website/assets/art/blue-hour.png"));
        QVERIFY(!art.isNull());
        art = art.scaled(640, 360, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        auto illustrated = [&](const QString &name, bool animation = false) {
            auto p = newManagedDocument(name, animation, 640, 360, Qt::white, false, 24, 72);
            Document doc;
            doc.load(p);
            QString error;
            if (!ClipboardTools::importImage(&doc, art, name, {}, &error))
                qFatal("Failed to build artwork fixture");
            return doc.project();
        };
        QString error;
        const QStringList types{"drawing", "animation", "movie"},
            titles{"Sketchbook", "Motion studies", "The last train"};
        for (int t = 0; t < types.size(); ++t) {
            QVERIFY2(
                w.createManagedProject(d.filePath(titles[t] + ".toonstudio"), titles[t], types[t], &error),
                qPrintable(error));
            QString parent, child;
            if (t == 2) {
                parent = w.store()->createFolder("Opening", {}, "content", &error);
                child = w.store()->createFolder("Station", parent, "content", &error);
                QVERIFY(!child.isEmpty());
            }
            for (int n = 0; n < 6; ++n) {
                auto name = QStringList{"Evening station", "Character study", "City rooftops",
                                        "Platform light",  "Rain studies",    "Train arrival"}[n];
                auto p = illustrated(name, t == 1);
                QVERIFY(!w.store()
                             ->createDocument(
                                 name, t == 1 ? "animation" : "drawing", p, t == 2 ? parent : QString(),
                                 t == 2 ? QStringList{"Background", "Night"} : QStringList{}, &error)
                             .isEmpty());
            }
            w.refreshDashboard();
            QVERIFY(!inspector->isVisible());
            QVERIFY(!typeFilters->isVisible());
            QCOMPARE(folders->isVisible(), t == 2);
            QCOMPARE(items->cards(), t != 1);
            if (t == 2) {
                QCOMPARE(items->count(), 1);
                folders->setCurrentItem(
                    folders->findItems("Opening", Qt::MatchExactly | Qt::MatchRecursive).first());
                QCOMPARE(items->count(), 7);
            }
            QTRY_VERIFY_WITH_TIMEOUT(
                !qvariant_cast<QPixmap>(items->item(t == 2 ? 1 : 0)->data(DashboardGallery::CoverRole))
                     .isNull(),
                10000);
            if (t == 1)
                QTRY_COMPARE(items->item(0)->data(DashboardGallery::RateRole).toString(), QString("24 fps"));
            QTRY_VERIFY_WITH_TIMEOUT(
                [&] {
                    for (int n = 0; n < items->count(); ++n) {
                        auto *item = items->item(n);
                        if (!item->data(DashboardGallery::FolderRole).toBool() &&
                            items->viewport()->rect().intersects(items->visualItemRect(item)) &&
                            qvariant_cast<QPixmap>(item->data(DashboardGallery::CoverRole)).isNull())
                            return false;
                    }
                    return true;
                }(),
                15000);
            if (t == 0)
                QCOMPARE(items->visualItemRect(items->item(0)).top(),
                         items->visualItemRect(items->item(2)).top());
            capture(types[t] + "-gallery");
            if (t == 0) {
                w.resize(960, 680);
                QApplication::processEvents();
                capture("drawing-narrow");
                w.resize(1440, 960);
            }
            if (t == 2) {
                const auto id = items->item(1)->data(Qt::UserRole).toString();
                QVERIFY(w.store()->organize({id}, "assets", {}, {"Background", "Night"}, &error));
                const auto imagePath = d.filePath("Station reference.png");
                QVERIFY(art.save(imagePath));
                QVERIFY(w.store()->importFiles({imagePath}, {}, {"Reference"}, &error));
                collections->setCurrentIndex(1);
                QVERIFY(inspector->isVisible());
                QVERIFY(typeFilters->isVisible());
                QCOMPARE(items->count(), 2);
                QVERIFY(items->cards());
                items->setCurrentRow(0);
                QTRY_VERIFY_WITH_TIMEOUT(
                    !qvariant_cast<QPixmap>(items->item(0)->data(DashboardGallery::CoverRole)).isNull(),
                    10000);
                capture("assets-gallery");
                w.resize(960, 680);
                QApplication::processEvents();
                QVERIFY(w.width() <= 960);
                QVERIFY(inspector->isVisible());
                capture("assets-narrow");
                w.resize(1440, 960);
                auto *custom = inspector->findChild<QLineEdit *>("customTags");
                custom->setText("Unfinished tag");
                items->setCurrentRow(1);
                QVERIFY(custom->text().isEmpty());
                custom->setText("Reusable");
                QTest::keyClick(custom, Qt::Key_Return);
                QVERIFY(!QApplication::activeModalWidget());
                bool found = false;
                for (const auto &i : w.store()->items())
                    if (i.id == items->currentItem()->data(Qt::UserRole).toString())
                        found = i.tags.contains("Reusable");
                QVERIFY(found);
                QTimer::singleShot(0, &w, [&] {
                    auto *dialog = w.findChild<QDialog *>("managedImportDialog");
                    QVERIFY(dialog);
                    capture("import-assets");
                    dialog->reject();
                });
                w.findChild<QPushButton *>("managedImportAssets")->click();
                collections->setCurrentIndex(0);
                QTimer::singleShot(0, &w, [&] {
                    auto *dialog = w.findChild<QDialog *>("managedDocumentDialog");
                    QVERIFY(dialog);
                    auto *preset = dialog->findChild<QTabBar *>("canvasPresets");
                    auto *width = dialog->findChild<ValueSlider *>("newDocumentWidth"),
                         *height = dialog->findChild<ValueSlider *>("newDocumentHeight");
                    preset->setCurrentIndex(1);
                    QCOMPARE(width->value(), 2048.);
                    QCOMPARE(height->value(), 2048.);
                    dialog->findChild<QToolButton *>("canvasAspectLock")->click();
                    width->setValue(1024);
                    QCOMPARE(height->value(), 1024.);
                    dialog->findChild<QToolButton *>("canvasAspectLock")->click();
                    preset->setCurrentIndex(0);
                    dialog->findChild<QToolButton *>("canvasSwap")->click();
                    QCOMPARE(width->value(), 1080.);
                    QCOMPARE(height->value(), 1920.);
                    preset->setCurrentIndex(0);
                    auto *custom = dialog->findChild<QLineEdit *>("customTags");
                    custom->setText("Night");
                    QTest::keyClick(custom, Qt::Key_Return);
                    QVERIFY(dialog->isVisible());
                    capture("new-drawing-movie");
                    dialog->findChild<QTabBar *>("canvasBackground")->setCurrentIndex(1);
                    QVERIFY(dialog->findChild<QWidget *>("newDrawingPreview")
                                ->accessibleDescription()
                                .contains("transparent"));
                    dialog->reject();
                });
                w.findChild<QPushButton *>("managedNewDrawing")->click();
            }
        }
        // Home returns to the agreed thumbnail gallery and has no open editor.
        auto *navigation = w.findChild<QToolBar *>("managementNavigation");
        navigation->actions().first()->trigger();
        auto *projects = w.findChild<DashboardGallery *>("managedProjects");
        QVERIFY(projects->isVisible());
        QCOMPARE(projects->count(), 3);
        QVERIFY(projects->cards());
        capture("projects-home");
        QTimer::singleShot(0, &w, [&] {
            auto *dialog = w.findChild<QDialog *>("managedProjectDialog");
            QVERIFY(dialog);
            const auto closeOnFailure = qScopeGuard([dialog] {
                if (dialog->isVisible())
                    dialog->reject();
            });
            auto *types = dialog->findChild<QTabBar *>("managedProjectType");
            QCOMPARE(types->count(), 3);
            QVERIFY(types->height() >= 200);
            QVERIFY(types->tabRect(0).left() >= 0);
            QVERIFY(types->tabRect(2).right() < types->width());
            types->setCurrentIndex(2);
            dialog->findChild<QLineEdit *>("managedProjectName")->setText("My next film");
            capture("new-project-cards");
            dialog->reject();
        });
        w.findChild<QPushButton *>("homeNewProject")->click();
        w.close();
    }

    void importReviewAndMoveUseProjectIdentity() {
        QTemporaryDir d;
        ProjectWorkspaceWindow w(d.filePath("management.ini"));
        QString error;
        QVERIFY(w.createManagedProject(d.filePath("Movie.toonstudio"), "Movie", "movie", &error));
        auto id = w.store()->createDocument("Character drawing", "drawing", drawing(), {},
                                            {"Character sheet"}, &error);
        QVERIFY(!id.isEmpty());
        w.show();
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QTest::qWait(80);
        w.refreshDashboard();
        auto *items = w.findChild<DashboardGallery *>("managedItems");
        items->setCurrentRow(0);
        const auto before = w.store()->items().first();
        const auto output = qEnvironmentVariable("TOON_MANAGED_CAPTURE");
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, &w, [&] {
            for (auto *box : w.findChildren<QMessageBox *>())
                qWarning() << "Import diagnostic" << box->text();
            for (auto *picker : w.findChildren<QFileDialog *>())
                qWarning() << "Picker diagnostic" << picker->directory().absolutePath()
                           << picker->selectedFiles() << picker->options();
            for (auto *dialog : w.findChildren<QDialog *>())
                if (dialog->isVisible())
                    dialog->reject();
            for (auto *menu : w.findChildren<QMenu *>())
                if (menu->isVisible())
                    menu->close();
        });
        timeout.start(20000);
        QTimer responder;
        responder.setInterval(10);
        bool moved = false;
        connect(&responder, &QTimer::timeout, &w, [&] {
            if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                for (auto *action : menu->actions())
                    if (action->text() == "Move to Assets…") {
                        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                                          menu->actionGeometry(action).center());
                        return;
                    }
            }
            auto *dialog = w.findChild<QDialog *>("managedOrganizeDialog");
            if (!dialog || !dialog->isVisible())
                return;
            responder.stop();
            auto *tag = dialog->findChild<QLineEdit *>("customTags");
            tag->setText("Reusable");
            QTest::keyClick(tag, Qt::Key_Return);
            if (!output.isEmpty()) {
                QDir().mkpath(output);
                dialog->grab().save(output + "/move-to-assets.png");
            }
            dialog->findChild<QPushButton *>("confirmOrganize")->click();
            moved = true;
        });
        responder.start();
        QTest::keyClick(items, Qt::Key_F10, Qt::ShiftModifier);
        responder.stop();
        QVERIFY(moved);
        auto after = w.store()->items().first();
        QCOMPARE(after.id, before.id);
        QCOMPARE(after.path, before.path);
        QCOMPARE(after.collection, QString("assets"));
        QVERIFY(after.tags.contains("Character sheet"));
        QVERIFY(after.tags.contains("Reusable"));
        w.findChild<QTabWidget *>("projectCollections")->setCurrentIndex(1);
        QImage image(256, 128, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor("#7388a3"));
        const auto source = d.filePath("Reference.png");
        QVERIFY(image.save(source));
        const bool oldNative = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        int phase = 0;
        bool imported = false;
        QObject::disconnect(&responder, nullptr, &w, nullptr);
        connect(&responder, &QTimer::timeout, &w, [&] {
            if (auto *picker = w.findChild<QFileDialog *>()) {
                if (phase == 1) {
                    phase = 2;
                    picker->setDirectory(QFileInfo(source).absolutePath());
                }
                // directoryLoaded may precede the first timer tick. Wait for the
                // actual source index instead of relying on that one-shot signal.
                auto *model = picker->findChild<QFileSystemModel *>();
                if (phase == 2 && model && model->index(source).isValid()) {
                    phase = 3;
                    QTimer::singleShot(0, picker, [picker, source] {
                        picker->selectFile(source);
                        // Enter the absolute path through the chooser's filename
                        // field; selection can be reset by its asynchronous view.
                        auto *entry = picker->findChild<QLineEdit *>("fileNameEdit");
                        if (entry)
                            entry->setText(source);
                        QMetaObject::invokeMethod(picker, "accept", Qt::DirectConnection);
                    });
                }
                return;
            }
            auto *dialog = w.findChild<QDialog *>("managedImportDialog");
            if (!dialog || !dialog->isVisible())
                return;
            if (phase == 0) {
                phase = 1;
                QTimer::singleShot(0, dialog->findChild<QPushButton *>("addImportFiles"),
                                   &QPushButton::click);
                return;
            }
            if (phase != 3)
                return;
            responder.stop();
            auto *files = dialog->findChild<QTreeWidget *>("importFiles");
            QCOMPARE(files->topLevelItemCount(), 1);
            QCOMPARE(w.store()->items().size(), 1);
            auto *tag = dialog->findChild<QLineEdit *>("customTags");
            tag->setText("Reference");
            QTest::keyClick(tag, Qt::Key_Return);
            if (!output.isEmpty())
                dialog->grab().save(output + "/import-assets-populated.png");
            dialog->findChild<QPushButton *>("confirmImportAssets")->click();
            imported = true;
        });
        responder.start();
        w.findChild<QPushButton *>("managedImportAssets")->click();
        responder.stop();
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, oldNative);
        if (!imported)
            qWarning() << "Import picker phase" << phase << "pickers"
                       << w.findChildren<QFileDialog *>().size();
        QVERIFY(imported);
        QCOMPARE(w.store()->items().size(), 2);
        QVERIFY(QFileInfo::exists(source));
        const auto all = w.store()->items();
        auto media = std::find_if(all.begin(), all.end(), [&](const auto &i) { return i.id != id; });
        QVERIFY(media != all.end());
        QCOMPARE(media->kind, QString("image"));
        QVERIFY(media->tags.contains("Reference"));
        QVERIFY(QFileInfo::exists(w.store()->assetPath(media->id, &error)));
        w.close();
    }

    void closingDirtyTabCanCancel() {
        QTemporaryDir d;
        ProjectWorkspaceWindow w(d.filePath("management.ini"));
        QString e;
        QVERIFY(w.createManagedProject(d.filePath("Test.toonstudio"), "Test", "drawing", &e));
        auto id = w.store()->createDocument("A", "drawing", drawing(), {}, {}, &e);
        w.show();
        QApplication::processEvents();
        QVERIFY(w.openItems({id}));
        auto *editor = w.editorForItem(id);
        paint(editor->canvas()->document());
        QVERIFY(editor->managedDirty());
        QTimer::singleShot(0, [] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                box->button(QMessageBox::Cancel)->click();
        });
        QVERIFY(!w.close());
        QVERIFY(w.editorForItem(id) == editor);
        QVERIFY(editor->managedDirty());
        QVERIFY(editor->saveManaged());
        w.close();
    }
};
QTEST_MAIN(ManagedProjectTests)
#include "managed_project_tests.moc"
