#include "studio_project.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
using namespace toon;
namespace {
bool write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool changeCatalog(const QString &root, const std::function<void(QJsonObject &)> &change) {
    const auto path = QDir(root).filePath("catalog.json");
    auto object = QJsonDocument::fromJson(read(path)).object();
    change(object);
    return write(path, QJsonDocument(object).toJson());
}
}
class StudioProjectTests : public QObject {
    Q_OBJECT
private slots:
    void packageRetainsVersion38ProductionMetadata() {
        QTemporaryDir dir;
        auto project = Project::create(ProjectKind::Series);
        project.shots[0].episode = "Episode 02";
        project.shots[0].productionStage = "layout";
        project.shots[0].productionNotes = "Match the station background.";
        QString error;
        const auto root = dir.filePath("Series.toonstudio");
        QVERIFY2(StudioProject::create(root, project, &error), qPrintable(error));
        const auto store = StudioProject::open(root, &error);
        QVERIFY2(store, qPrintable(error));
        QVERIFY(read(store->artworkPath()).startsWith("TOONPRO38\n"));
        const auto loaded = loadProject(store->artworkPath(), &error);
        QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->kind, ProjectKind::Series);
        QCOMPARE(loaded->shots[0].episode, QString("Episode 02"));
        QCOMPARE(loaded->shots[0].productionStage, QString("layout"));
        QCOMPARE(loaded->shots[0].productionNotes, QString("Match the station background."));
    }
    void createAndRelocateWithAssets() {
        QTemporaryDir dir;
        const auto root = dir.filePath("Original.toonstudio");
        auto project = Project::create();
        project.movie = "After the rain";
        QString error;
        QVERIFY2(StudioProject::create(root, project, &error), qPrintable(error));
        auto store = StudioProject::open(root, &error);
        QVERIFY2(store, qPrintable(error));
        QVERIFY(QFileInfo::exists(store->entryPath()));
        const auto drawing = loadProject(store->artworkPath(), &error);
        QVERIFY2(drawing, qPrintable(error));
        QCOMPARE(drawing->movie, project.movie);
        const auto source = dir.filePath("rain.wav");
        QVERIFY(write(source, "sample audio source"));
        QVERIFY2(store->importFiles({source}, "Sound/Weather", {"Rain", "rain", "Weather"}, &error), qPrintable(error));
        QCOMPARE(store->assets().size(), 1);
        const auto entry = store->assets().first();
        QCOMPARE(entry.tags, QStringList({"Rain", "Weather"}));
        QCOMPARE(entry.type, QString("audio"));
        QCOMPARE(read(store->assetPath(entry.id)), QByteArray("sample audio source"));
        QVERIFY(QFile::remove(source));
        const auto relocated = dir.filePath("Moved.toonstudio");
        QVERIFY(QDir().rename(root, relocated));
        auto moved = StudioProject::open(QDir(relocated).filePath("project.toonstudio"), &error);
        QVERIFY2(moved, qPrintable(error));
        QCOMPARE(read(moved->assetPath(entry.id)), QByteArray("sample audio source"));
        QVERIFY(moved->folders().contains("Sound/Weather"));
        QVERIFY(!moved->assetPath(entry.id).contains("Original"));
    }
    void duplicateBasenamesNeverOverwriteAndMetadataPersists() {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkdir("one"));
        QVERIFY(QDir(dir.path()).mkdir("two"));
        const auto one = dir.filePath("one/same.png"), two = dir.filePath("two/same.png");
        QVERIFY(write(one, "first"));
        QVERIFY(write(two, "second"));
        QString error;
        const auto root = dir.filePath("Film.toonstudio");
        QVERIFY(StudioProject::create(root, Project::create(), &error));
        auto store = StudioProject::open(root, &error);
        QVERIFY(store);
        QVERIFY2(store->importFiles({one, two}, {}, {}, &error), qPrintable(error));
        QCOMPARE(store->assets().size(), 2);
        const auto first = store->assets()[0], second = store->assets()[1];
        QVERIFY(first.id != second.id);
        QVERIFY(first.relativePath != second.relativePath);
        QCOMPARE(read(store->assetPath(first.id)), QByteArray("first"));
        QCOMPARE(read(store->assetPath(second.id)), QByteArray("second"));
        auto changed = first;
        changed.name = "Station background"; changed.folder = "Backgrounds/Station";
        changed.tags = {"Exterior", "Night"}; changed.shotIds = {"shot-one", "shot-two"};
        QVERIFY2(store->updateAsset(changed, &error), qPrintable(error));
        auto reopened = StudioProject::open(root, &error);
        QVERIFY(reopened);
        QCOMPARE(*reopened->asset(first.id), changed);
        QCOMPARE(read(one), QByteArray("first"));
    }
    void removalIsRestorableAndNeverDeletesOriginals() {
        QTemporaryDir dir;
        const auto root = dir.filePath("Film.toonstudio"), source = dir.filePath("image.png");
        QString error;
        QVERIFY(write(source, "original"));
        QVERIFY(StudioProject::create(root, Project::create(), &error));
        auto store = StudioProject::open(root, &error);
        QVERIFY(store && store->importFiles({source}, {}, {}, &error));
        const auto id = store->assets().first().id, copy = store->assetPath(id);
        QVERIFY(store->removeAsset(id, &error));
        QVERIFY(store->asset(id)->removed);
        QCOMPARE(read(copy), QByteArray("original"));
        auto reopened = StudioProject::open(root, &error);
        QVERIFY(reopened && reopened->asset(id)->removed);
        QVERIFY(reopened->restoreAsset(id, &error));
        QVERIFY(!reopened->asset(id)->removed);
        QCOMPARE(read(source), QByteArray("original"));
    }
    void batchFailureAndCancellationLeaveNoPartialImports() {
        QTemporaryDir dir;
        const auto root = dir.filePath("Film.toonstudio"), source = dir.filePath("large.wav");
        QString error;
        QVERIFY(write(source, QByteArray(4 * 1024 * 1024, 'a')));
        QVERIFY(StudioProject::create(root, Project::create(), &error));
        auto store = StudioProject::open(root, &error);
        QVERIFY(store);
        const auto old = read(QDir(root).filePath("catalog.json"));
        QVERIFY(!store->importFiles({source, dir.filePath("missing.wav")}, {}, {}, &error));
        QCOMPARE(store->assets().size(), 0);
        QCOMPARE(read(QDir(root).filePath("catalog.json")), old);
        QVERIFY(QDir(QDir(root).filePath("assets")).entryList(QDir::Files).isEmpty());
        int checks = 0;
        QVERIFY(!store->importFiles({source}, {}, {}, &error, [&] { return ++checks >= 4; }));
        QVERIFY(error.contains("cancelled", Qt::CaseInsensitive));
        QCOMPARE(store->assets().size(), 0);
        QCOMPARE(read(QDir(root).filePath("catalog.json")), old);
        QVERIFY(QDir(QDir(root).filePath("assets")).entryList(QDir::Files).isEmpty());
    }
    void cancellationImmediatelyBeforeCatalogPublicationRollsBack() {
        QTemporaryDir dir;
        const auto root = dir.filePath("Film.toonstudio"), source = dir.filePath("small.wav");
        QString error;
        QVERIFY(write(source, "sound"));
        QVERIFY(StudioProject::create(root, Project::create(), &error));
        auto store = StudioProject::open(root, &error);
        QVERIFY(store);
        const auto before = read(QDir(root).filePath("catalog.json"));
        int checks = 0;
        QVERIFY(!store->importFiles({source}, {}, {}, &error, [&] { return ++checks == 4; }));
        QCOMPARE(checks, 4);
        QVERIFY(error.contains("cancelled", Qt::CaseInsensitive));
        QCOMPARE(read(QDir(root).filePath("catalog.json")), before);
        QVERIFY(QDir(QDir(root).filePath("assets")).entryList(QDir::Files).isEmpty());
        QVERIFY(!QFileInfo::exists(QDir(root).filePath("catalog.lock")));
    }
    void staleWriterCannotOverwriteAnotherOperation() {
        QTemporaryDir dir;
        const auto root = dir.filePath("Film.toonstudio"), source = dir.filePath("image.png");
        QString error;
        QVERIFY(write(source, "pixels"));
        QVERIFY(StudioProject::create(root, Project::create(), &error));
        auto first = StudioProject::open(root, &error), stale = StudioProject::open(root, &error);
        QVERIFY(first && stale);
        QVERIFY(first->createFolder("Backgrounds", &error));
        const auto catalog = read(QDir(root).filePath("catalog.json"));
        QVERIFY(!stale->importFiles({source}, {}, {}, &error));
        QVERIFY(error.contains("Reload"));
        QCOMPARE(read(QDir(root).filePath("catalog.json")), catalog);
        QVERIFY(QDir(QDir(root).filePath("assets")).entryList(QDir::Files).isEmpty());
        QVERIFY(!stale->createFolder("Props", &error));
        QCOMPARE(read(QDir(root).filePath("catalog.json")), catalog);
    }
    void invalidMetadataAndEscapingPathsAreRejected() {
        QTemporaryDir dir;
        const auto root = dir.filePath("Film.toonstudio"), source = dir.filePath("image.png");
        QString error;
        QVERIFY(write(source, "pixels"));
        QVERIFY(StudioProject::create(root, Project::create(), &error));
        auto store = StudioProject::open(root, &error);
        QVERIFY(store && store->importFiles({source}, {}, {}, &error));
        const auto original = read(QDir(root).filePath("catalog.json"));
        auto changed = store->assets().first();
        changed.relativePath = "../outside.png";
        QVERIFY(!store->updateAsset(changed, &error));
        QVERIFY(!store->createFolder("../outside", &error));
        QVERIFY(!store->createFolder("a//b", &error));
        QVERIFY(changeCatalog(root, [](QJsonObject &o) {
            auto array = o["assets"].toArray(); auto item = array[0].toObject();
            item["path"] = "../outside.png"; array[0] = item; o["assets"] = array;
        }));
        QVERIFY(!StudioProject::open(root, &error));
        QVERIFY(write(QDir(root).filePath("catalog.json"), original));
        QVERIFY(changeCatalog(root, [](QJsonObject &o) {
            auto array = o["assets"].toArray(); array.append(array.first()); o["assets"] = array;
        }));
        QVERIFY(!StudioProject::open(root, &error));
        QVERIFY(write(QDir(root).filePath("catalog.json"), original));
        QVERIFY(changeCatalog(root, [](QJsonObject &o) { o["version"] = 2; }));
        QVERIFY(!StudioProject::open(root, &error));
    }
    void missingMediaRemainsInspectable() {
        QTemporaryDir dir;
        const auto root = dir.filePath("Film.toonstudio"), source = dir.filePath("image.png");
        QString error;
        QVERIFY(write(source, "pixels"));
        QVERIFY(StudioProject::create(root, Project::create(), &error));
        auto store = StudioProject::open(root, &error);
        QVERIFY(store && store->importFiles({source}, {}, {}, &error));
        const auto id = store->assets().first().id;
        QVERIFY(QFile::remove(store->assetPath(id)));
        auto reopened = StudioProject::open(root, &error);
        QVERIFY2(reopened, qPrintable(error));
        QVERIFY(reopened->assetPath(id, &error).isEmpty());
        QVERIFY(error.contains("missing"));
    }
    void projectCopyIsPortableAndCancellationPreservesDestination() {
        QTemporaryDir dir;
        const auto sourceRoot = dir.filePath("Source.toonstudio"), targetRoot = dir.filePath("Copy.toonstudio");
        const auto image = dir.filePath("image.png");
        auto project = Project::create();
        QString error;
        QVERIFY(write(image, "pixels"));
        QVERIFY(StudioProject::create(sourceRoot, project, &error));
        auto store = StudioProject::open(sourceRoot, &error);
        QVERIFY(store && store->importFiles({image}, "Paintings", {"BG"}, &error));
        const auto id = store->assets().first().id;
        project.movie = "Copy";
        QVERIFY2(StudioProject::create(targetRoot, project, &error, sourceRoot), qPrintable(error));
        auto copy = StudioProject::open(targetRoot, &error);
        QVERIFY(copy);
        QCOMPARE(copy->assets(), store->assets());
        QCOMPARE(read(copy->assetPath(id)), QByteArray("pixels"));
        const auto copiedArtwork = loadProject(copy->artworkPath(), &error);
        QVERIFY(copiedArtwork); QCOMPARE(copiedArtwork->movie, QString("Copy"));
        const auto artworkBefore = read(copy->artworkPath());
        QVERIFY(!StudioProject::create(targetRoot, Project::create(), &error));
        QCOMPARE(read(copy->artworkPath()), artworkBefore);
        const auto cancelledTarget = dir.filePath("Cancelled.toonstudio");
        QVERIFY(!StudioProject::create(cancelledTarget, project, &error, sourceRoot, [] { return true; }));
        QVERIFY(!QFileInfo::exists(cancelledTarget));
        QVERIFY(QDir(dir.path()).entryList({".toon-studio-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
        QVERIFY(!StudioProject::create(QDir(sourceRoot).filePath("Nested.toonstudio"), project, &error, sourceRoot));
        QVERIFY(error.contains("outside"));
        QVERIFY(!QFileInfo::exists(QDir(sourceRoot).filePath("Nested.toonstudio")));
    }
    void cancellationAfterPreparingPackageDoesNotPublish() {
        QTemporaryDir dir;
        const auto target = dir.filePath("Cancelled.toonstudio");
        QString error;
        bool sawPreparedMarker = false;
        QVERIFY(!StudioProject::create(target, Project::create(), &error, {}, [&] {
            const auto staging = QDir(dir.path()).entryList({".toon-studio-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
            for (const auto &folder : staging)
                if (QFileInfo::exists(dir.filePath(folder + "/project.toonstudio"))) {
                    sawPreparedMarker = true;
                    return true;
                }
            return false;
        }));
        QVERIFY(sawPreparedMarker);
        QVERIFY(error.contains("cancelled", Qt::CaseInsensitive));
        QVERIFY(!QFileInfo::exists(target));
        QVERIFY(QDir(dir.path()).entryList({".toon-studio-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
    }
    void symlinkedAssetFolderAndCatalogNeverEscape() {
#ifdef Q_OS_WIN
        QSKIP("Creating symbolic links requires privileges on Windows.");
#else
        QTemporaryDir dir;
        const auto root = dir.filePath("Film.toonstudio"), source = dir.filePath("image.png");
        const auto outside = dir.filePath("outside");
        QString error;
        QVERIFY(write(source, "pixels"));
        QVERIFY(QDir().mkdir(outside));
        QVERIFY(StudioProject::create(root, Project::create(), &error));
        auto store = StudioProject::open(root, &error);
        QVERIFY(store);
        const auto assets = QDir(root).filePath("assets");
        QVERIFY(QDir().rmdir(assets));
        QVERIFY(QFile::link(outside, assets));
        QVERIFY(!store->importFiles({source}, {}, {}, &error));
        QVERIFY(QDir(outside).entryList(QDir::Files).isEmpty());
        QVERIFY(!StudioProject::open(root, &error));
        QVERIFY(QFile::remove(assets));
        QVERIFY(QDir().mkdir(assets));
        const auto catalog = QDir(root).filePath("catalog.json");
        const auto original = read(catalog);
        QVERIFY(QFile::remove(catalog));
        const auto victim = QDir(outside).filePath("victim.json");
        QVERIFY(write(victim, original));
        QVERIFY(QFile::link(victim, catalog));
        QVERIFY(!store->createFolder("No escape", &error));
        QCOMPARE(read(victim), original);
        QVERIFY(!StudioProject::open(root, &error));
#endif
    }
    void cancellationCleanupCannotFollowReplacedAssetFolder() {
#ifdef Q_OS_WIN
        QSKIP("Creating symbolic links requires privileges on Windows.");
#else
        QTemporaryDir dir;
        const auto root = dir.filePath("Film.toonstudio"), source = dir.filePath("image.png");
        const auto outside = dir.filePath("outside");
        QString error;
        QVERIFY(write(source, QByteArray(2 * 1024 * 1024, 'a')));
        QVERIFY(QDir().mkdir(outside));
        QVERIFY(StudioProject::create(root, Project::create(), &error));
        auto store = StudioProject::open(root, &error);
        QVERIFY(store);
        const auto assets = QDir(root).filePath("assets");
        QString victim;
        bool replaced = false;
        int checks = 0;
        QVERIFY(!store->importFiles({source}, {}, {}, &error, [&] {
            if (++checks != 2) return false;
            const auto filenames = QDir(assets).entryList(QDir::Files);
            if (filenames.size() != 1 || !QDir(root).rename("assets", "held-assets")) return true;
            victim = QDir(outside).filePath(filenames.first());
            if (!write(victim, "do not delete") || !QFile::link(outside, assets)) return true;
            replaced = true;
            return true;
        }));
        QVERIFY(replaced);
        QCOMPARE(read(victim), QByteArray("do not delete"));
        QVERIFY(error.contains("cancelled", Qt::CaseInsensitive));
#endif
    }
};
QTEST_GUILESS_MAIN(StudioProjectTests)
#include "studio_project_tests.moc"
