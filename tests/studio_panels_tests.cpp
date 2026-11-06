#include "project_home.h"
#include "project_assets_panel.h"
#include "studio_project.h"
#include "document.h"
#include <QComboBox>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMediaPlayer>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

using namespace toon;
namespace {
QString imageFile(const QTemporaryDir &dir, const QString &name = "Background.png") {
    QImage image(64, 32, QImage::Format_ARGB32_Premultiplied); image.fill(QColor("#8a94a7"));
    const auto path = dir.filePath(name); if (!image.save(path)) return {}; return path;
}
QByteArray bytes(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
void edit(QLineEdit *field, const QString &text) {
    field->setText(text);
    QMetaObject::invokeMethod(field, "textEdited", Qt::DirectConnection, Q_ARG(QString, text));
}
}
class StudioPanelsTests final : public QObject {
    Q_OBJECT
private slots:
    void recentProjectsKeepPinsArchivesAndNeverDeleteFiles() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QSettings settings(dir.filePath("workspace.ini"), QSettings::IniFormat);
        const auto path = imageFile(dir, "existing.png"); QVERIFY(!path.isEmpty());
        const auto original = bytes(path);
        ProjectHome home(&settings); home.rememberProject(path, "Station background", "drawing-only");
        auto *list = home.findChild<QTreeWidget *>("homeProjects"); QCOMPARE(list->topLevelItemCount(), 1);
        QCOMPARE(list->topLevelItem(0)->text(1), QString("Drawing only"));
        QSignalSpy opened(&home, &ProjectHome::projectRequested);
        home.findChild<QPushButton *>("homeOpenSelected")->click(); QCOMPARE(opened.count(), 1);
        home.findChild<QToolButton *>("homePin")->click();
        home.findChild<QTabBar *>("homeProjectTabs")->setCurrentIndex(1); QCOMPARE(list->topLevelItemCount(), 1);
        home.findChild<QPushButton *>("homeArchive")->click(); QCOMPARE(list->topLevelItemCount(), 0);
        home.findChild<QTabBar *>("homeProjectTabs")->setCurrentIndex(2); QCOMPARE(list->topLevelItemCount(), 1);
        ProjectHome reopened(&settings); auto *again = reopened.findChild<QTreeWidget *>("homeProjects");
        reopened.findChild<QTabBar *>("homeProjectTabs")->setCurrentIndex(2); QCOMPARE(again->topLevelItemCount(), 1);
        reopened.findChild<QPushButton *>("homeArchive")->click();
        reopened.findChild<QTabBar *>("homeProjectTabs")->setCurrentIndex(1); QCOMPARE(again->topLevelItemCount(), 1);
        reopened.findChild<QToolButton *>("homeRemove")->click(); QCOMPARE(again->topLevelItemCount(), 0);
        QCOMPARE(bytes(path), original);
    }
    void missingAndMalformedRecentEntriesRemainSafe() {
        QTemporaryDir dir; QSettings settings(dir.filePath("workspace.ini"), QSettings::IniFormat);
        ProjectHome home(&settings); home.rememberProject(dir.filePath("gone.toonstudio"), "Missing movie", "film");
        QVERIFY(!home.findChild<QPushButton *>("homeOpenSelected")->isEnabled());
        QVERIFY(home.findChild<QLabel *>("homeStatus")->text().contains("unavailable"));
        settings.setValue("studio/recentProjectsV1", QByteArray("{ broken")); settings.sync();
        ProjectHome broken(&settings); broken.rememberProject(dir.filePath("another"), "Ignored", "film");
        QCOMPARE(settings.value("studio/recentProjectsV1").toByteArray(), QByteArray("{ broken"));
        QVERIFY(broken.findChild<QLabel *>("homeStatus")->text().contains("preserved"));
    }
    void importOrganizeAttachRemoveAndRestorePreserveArtwork() {
        QTemporaryDir dir; const auto source = imageFile(dir); QVERIFY(!source.isEmpty());
        const auto original = bytes(source); auto project = Project::create(); QString error;
        const auto root = dir.filePath("Film.toonstudio"); QVERIFY2(StudioProject::create(root, project, &error), qPrintable(error));
        Document document; document.load(project); const auto artwork = document.project().toJson();
        ProjectAssetsPanel panel(&document, [] { return true; }); panel.setProjectRoot(root);
        QSignalSpy imported(&panel, &ProjectAssetsPanel::importFinished);
        panel.importFiles({source}); QVERIFY(panel.busy()); QVERIFY(!panel.finishEditing());
        QTRY_COMPARE_WITH_TIMEOUT(imported.count(), 1, 10000);
        QVERIFY(imported.first().first().toBool()); QVERIFY(!panel.busy());
        auto *list = panel.findChild<QTreeWidget *>("projectAssetsList"); QCOMPARE(list->topLevelItemCount(), 1);
        auto catalog = StudioProject::open(root, &error); QVERIFY(catalog); QCOMPARE(catalog->assets().size(), 1);
        const auto id = catalog->assets().first().id;
        const auto copy = catalog->assetPath(id, &error); QVERIFY(!copy.isEmpty()); QVERIFY(copy != source);
        QCOMPARE(bytes(copy), original);
        edit(panel.findChild<QLineEdit *>("assetName"), "Station · sunset");
        edit(panel.findChild<QLineEdit *>("assetTags"), "background, station, sunset");
        QVERIFY(panel.finishEditing());
        catalog = StudioProject::open(root, &error); QVERIFY(catalog);
        QCOMPARE(catalog->asset(id)->name, QString("Station · sunset")); QCOMPARE(catalog->asset(id)->tags.size(), 3);
        QVERIFY(panel.recordCurrentShotUse());
        QCOMPARE(panel.findChild<QListWidget *>("assetUsedIn")->count(), 1);
        catalog = StudioProject::open(root, &error); QVERIFY(catalog);
        QCOMPARE(catalog->asset(id)->shotIds, QStringList{document.activeShotId()});
        QCOMPARE(document.project().toJson(), artwork); QCOMPARE(document.history()->count(), 0);
        panel.findChild<QToolButton *>("assetsRemove")->click(); QCOMPARE(list->topLevelItemCount(), 0);
        panel.findChild<QTabBar *>("assetsCollectionTabs")->setCurrentIndex(1); QCOMPARE(list->topLevelItemCount(), 1);
        QVERIFY(QFile::exists(copy)); panel.findChild<QToolButton *>("assetsRestore")->click();
        panel.findChild<QTabBar *>("assetsCollectionTabs")->setCurrentIndex(0); QCOMPARE(list->topLevelItemCount(), 1);
        QCOMPARE(bytes(source), original); QCOMPARE(bytes(copy), original);
        panel.findChild<QLineEdit *>("assetsSearch")->setText("sunset"); QCOMPARE(list->topLevelItemCount(), 1);
        panel.findChild<QLineEdit *>("assetsSearch")->setText("no match"); QCOMPARE(list->topLevelItemCount(), 0);
        QVERIFY(!panel.findChild<QToolButton *>("assetsUse")->isEnabled());
    }
    void invalidMetadataBlocksNavigationWithoutLosingInput() {
        QTemporaryDir dir; const auto source = imageFile(dir); auto project = Project::create(); QString error;
        const auto root = dir.filePath("Drawing.toonstudio"); QVERIFY(StudioProject::create(root, project, &error));
        auto store = StudioProject::open(root, &error); QVERIFY(store); QVERIFY(store->importFiles({source}, {}, {}, &error));
        Document document; document.load(project); ProjectAssetsPanel panel(&document, [] { return true; }); panel.setProjectRoot(root);
        auto *name = panel.findChild<QLineEdit *>("assetName"); edit(name, "");
        QVERIFY(!panel.finishEditing()); QCOMPARE(name->text(), QString());
        auto unchanged = StudioProject::open(root, &error); QVERIFY(unchanged);
        QCOMPARE(unchanged->assets().first().name, store->assets().first().name);
        edit(name, "Repaired name"); QVERIFY(panel.finishEditing());
        unchanged = StudioProject::open(root, &error); QCOMPARE(unchanged->assets().first().name, QString("Repaired name"));
    }
    void projectSwitchRejectsStaleImportUiResults() {
        QTemporaryDir dir; const auto source = imageFile(dir); auto project = Project::create(); QString error;
        const auto first = dir.filePath("First.toonstudio"), second = dir.filePath("Second.toonstudio");
        QVERIFY(StudioProject::create(first, project, &error)); QVERIFY(StudioProject::create(second, project, &error));
        Document document; document.load(project); ProjectAssetsPanel panel(&document, [] { return true; }); panel.setProjectRoot(first);
        QSignalSpy imported(&panel, &ProjectAssetsPanel::importFinished); panel.importFiles({source});
        QVERIFY(panel.busy()); panel.setProjectRoot(second);
        QTRY_COMPARE_WITH_TIMEOUT(imported.count(), 1, 10000);
        QCOMPARE(panel.projectRoot(), QFileInfo(second).canonicalFilePath());
        QCOMPARE(panel.findChild<QTreeWidget *>("projectAssetsList")->topLevelItemCount(), 0);
        auto store = StudioProject::open(second, &error); QVERIFY(store); QVERIFY(store->assets().isEmpty());
    }
    void mediaPreviewReleasesItsSourceWhenHidden() {
        QTemporaryDir dir; const auto source = dir.filePath("scratch.wav");
        QFile file(source); QVERIFY(file.open(QIODevice::WriteOnly)); QDataStream stream(&file); stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData("RIFF", 4); stream << quint32(36 + 1920); stream.writeRawData("WAVEfmt ", 8);
        stream << quint32(16) << quint16(1) << quint16(2) << quint32(48000) << quint32(192000) << quint16(4) << quint16(16);
        stream.writeRawData("data", 4); stream << quint32(1920); stream.writeRawData(QByteArray(1920, '\0').constData(), 1920); file.close();
        auto project = Project::create(); QString error; const auto root = dir.filePath("Audio.toonstudio");
        QVERIFY(StudioProject::create(root, project, &error)); auto store = StudioProject::open(root, &error); QVERIFY(store);
        QVERIFY(store->importFiles({source}, {}, {}, &error));
        Document document; document.load(project); ProjectAssetsPanel panel(&document, [] { return true; });
        panel.setProjectRoot(root); panel.resize(350, 650); panel.show(); QTest::qWait(10);
        panel.findChild<QToolButton *>("assetsPlay")->click();
        auto *player = panel.findChild<QMediaPlayer *>("projectAssetMediaPlayer"); QVERIFY(player); QVERIFY(!player->source().isEmpty());
        panel.hide(); QVERIFY(player->source().isEmpty()); QCOMPARE(player->playbackState(), QMediaPlayer::StoppedState);
    }
};
QTEST_MAIN(StudioPanelsTests)
#include "studio_panels_tests.moc"
