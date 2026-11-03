#include "workspace_library.h"
#include <QCryptographicHash>
#include <QDockWidget>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolBar>
#include <QtTest>

using namespace toon;
namespace {
WorkspaceSnapshot snapshot(bool compact=false) {
    QMainWindow window;window.resize(900,650);window.setCentralWidget(new QLabel("Canvas"));
    auto *layers=new QDockWidget("Layers",&window);layers->setObjectName("layersPanel");layers->setWidget(new QLabel("Layers"));window.addDockWidget(Qt::RightDockWidgetArea,layers);
    auto *tools=new QToolBar("Toolbox",&window);tools->setObjectName("toolboxPanel");window.addToolBar(Qt::LeftToolBarArea,tools);
    return {window.saveState(3),compact,compact,compact?2:0,"Animation"};
}
QByteArray libraryBytes(QSettings &settings){settings.sync();return settings.value(WorkspaceLibrary::settingsKey()).toByteArray();}
QByteArray replaceFirst(const QByteArray &bytes,const QJsonObject &entry){auto root=QJsonDocument::fromJson(bytes).object();auto items=root["workspaces"].toArray();items[0]=entry;root["workspaces"]=items;return QJsonDocument(root).toJson(QJsonDocument::Compact);}
}
class WorkspaceLibraryTests final:public QObject {
    Q_OBJECT
private slots:
    void preservesStartupSettingsAndRoundTripsExplicitNamedSnapshots() {
        QTemporaryDir directory;const auto path=directory.filePath("workspace.ini");QSettings settings(path,QSettings::IniFormat);
        settings.setValue("layoutV3",QByteArray("existing live layout"));settings.setValue("workspace/compactDrawing",true);settings.setValue("brush/size",37);settings.sync();
        WorkspaceLibrary library(&settings);QString error,id;
        QVERIFY(library.load(&error));QVERIFY(library.writable());QVERIFY(library.entries().isEmpty());QVERIFY(!settings.contains(WorkspaceLibrary::settingsKey()));
        const auto original=snapshot();QVERIFY2(library.add("  My Painting  ",original,&id,&error),qPrintable(error));QVERIFY(!id.isEmpty());QCOMPARE(library.find(id)->name,QString("My Painting"));
        QCOMPARE(settings.value("layoutV3").toByteArray(),QByteArray("existing live layout"));QCOMPARE(settings.value("workspace/compactDrawing").toBool(),true);QCOMPARE(settings.value("brush/size").toInt(),37);
        QSettings reopenedSettings(path,QSettings::IniFormat);WorkspaceLibrary reopened(&reopenedSettings);QVERIFY2(reopened.load(&error),qPrintable(error));QCOMPARE(reopened.entries(),library.entries());
        auto compact=original;compact.compactDrawing=true;compact.drawerOpen=true;compact.drawerPage=1;compact.inspectorPage="Drawing";
        QVERIFY(library.update(id,compact,&error));QCOMPARE(library.find(id)->snapshot,compact);QCOMPARE(library.entries().size(),1);
        QVERIFY(library.rename(id,"Characters",&error));QCOMPARE(library.find(id)->name,QString("Characters"));QCOMPARE(library.find(id)->snapshot,compact);
        const auto before=libraryBytes(settings);QVERIFY(library.rename(id,"Characters",&error));QVERIFY(library.update(id,compact,&error));QCOMPARE(libraryBytes(settings),before);
        QVERIFY(reopened.load());QCOMPARE(reopened.entries(),library.entries());QVERIFY(library.remove(id,&error));QVERIFY(library.entries().isEmpty());QVERIFY(reopened.load());QVERIFY(reopened.entries().isEmpty());
        QCOMPARE(settings.value("layoutV3").toByteArray(),QByteArray("existing live layout"));
    }
    void namesBuiltinsAndSnapshotLimitsRejectWithoutOverwriting() {
        QTemporaryDir directory;QSettings settings(directory.filePath("workspace.ini"),QSettings::IniFormat);WorkspaceLibrary library(&settings);QVERIFY(library.load());
        const auto state=snapshot();QString first,second,error;QVERIFY(library.add("Pens",state,&first));QVERIFY(library.add("Paint",state,&second));const auto before=libraryBytes(settings);
        for(const auto &name:QStringList{"", " \n ", "two\nlines",QString(101,'a'),"PENS"," illustration ","Anime","Storyboarding","Animatics","Compact drawing","Drawing"}){
            QVERIFY2(!library.add(name,state,nullptr,&error),qPrintable(name));QVERIFY(!error.isEmpty());QCOMPARE(libraryBytes(settings),before);
        }
        QVERIFY(!library.rename(first,"paint",&error));QVERIFY(!library.rename(first,"Anime",&error));
        QVERIFY(!library.update("illustration",state,&error));QVERIFY(!library.rename("anime","Custom",&error));QVERIFY(!library.remove("animatic",&error));QCOMPARE(libraryBytes(settings),before);
        for(int invalid=0;invalid<6;++invalid){auto bad=state;
            if(invalid==0)bad.state.clear();if(invalid==1)bad.state=QByteArray(maxWorkspaceStateBytes+1,'x');
            if(invalid==2)bad.state[7]=2;if(invalid==3)bad.drawerPage=3;if(invalid==4)bad.drawerPage=-1;if(invalid==5)bad.inspectorPage="Missing";
            QVERIFY(!library.update(first,bad,&error));QCOMPARE(libraryBytes(settings),before);
        }
        // Output aliasing must not erase an input name before validation.
        QString name="Aliased name";QVERIFY(library.add(name,state,&name,&error));QCOMPARE(library.find(name)->name,QString("Aliased name"));
    }
    void corruptedFutureAndOversizedLibrariesRemainReadOnlyAndUntouched() {
        QTemporaryDir directory;QSettings settings(directory.filePath("workspace.ini"),QSettings::IniFormat);WorkspaceLibrary seed(&settings);QVERIFY(seed.load());QVERIFY(seed.add("Saved",snapshot()));
        const auto good=libraryBytes(settings);const auto original=QJsonDocument::fromJson(good).object();const auto saved=original["workspaces"].toArray()[0].toObject();
        QVector<QByteArray> damaged{QByteArray("{broken"),QByteArray("{}"),QByteArray(maxWorkspaceLibraryBytes+1,'x')};
        auto root=original;root["version"]=2;damaged.append(QJsonDocument(root).toJson());root=original;root["stateVersion"]=2;damaged.append(QJsonDocument(root).toJson());root=original;root["unknown"]=1;damaged.append(QJsonDocument(root).toJson());
        for(const auto &field:QStringList{"compactDrawing","drawerOpen","drawerPage","inspectorPage","name","id","sha256"}){auto entry=saved;entry[field]=QJsonArray{};damaged.append(replaceFirst(good,entry));}
        {auto entry=saved;entry["drawerPage"]=.5;damaged.append(replaceFirst(good,entry));}
        {auto entry=saved;entry["name"]="Anime";damaged.append(replaceFirst(good,entry));}
        {auto entry=saved;entry["layout"]=entry["layout"].toString()+"!";damaged.append(replaceFirst(good,entry));}
        {auto entry=saved;entry["sha256"]=QString(64,'0');damaged.append(replaceFirst(good,entry));}
        {auto entry=saved;entry["layout"]=QString::fromLatin1(QByteArray("not a Qt layout").toBase64());entry["sha256"]=QString::fromLatin1(QCryptographicHash::hash("not a Qt layout",QCryptographicHash::Sha256).toHex());damaged.append(replaceFirst(good,entry));}
        for(const auto &bytes:damaged){
            settings.setValue(WorkspaceLibrary::settingsKey(),bytes);settings.sync();WorkspaceLibrary library(&settings);QString error;
            QVERIFY(!library.load(&error));QVERIFY(!library.writable());QVERIFY(library.entries().isEmpty());QVERIFY(!error.isEmpty());
            QVERIFY(!library.add("Replacement",snapshot(),nullptr,&error));QCOMPARE(libraryBytes(settings),bytes);
        }
        settings.setValue(WorkspaceLibrary::settingsKey(),QString::fromUtf8(good));settings.sync();WorkspaceLibrary wrongType(&settings);QVERIFY(!wrongType.load());QVERIFY(!wrongType.writable());QVERIFY(settings.value(WorkspaceLibrary::settingsKey()).metaType()==QMetaType::fromType<QString>());
    }
    void concurrentChangesAndFailedWritesDoNotReplaceSavedOrInMemoryLayouts() {
        QTemporaryDir directory;const auto path=directory.filePath("workspace.ini");QSettings firstSettings(path,QSettings::IniFormat),secondSettings(path,QSettings::IniFormat);
        WorkspaceLibrary first(&firstSettings),second(&secondSettings);QVERIFY(first.load());QVERIFY(second.load());QString id,error;const auto state=snapshot();
        QVERIFY(first.add("One",state,&id));const auto saved=libraryBytes(firstSettings);QVERIFY(!second.add("Two",state,nullptr,&error));QVERIFY(!second.writable());QVERIFY(second.entries().isEmpty());QCOMPARE(libraryBytes(firstSettings),saved);
        QVERIFY(second.load());QCOMPARE(second.entries(),first.entries());QVERIFY(first.rename(id,"Renamed"));const auto newer=libraryBytes(firstSettings);
        // Even a local no-op must notice a newer library before reporting save.
        QVERIFY(!second.update(id,state,&error));QVERIFY(!second.writable());QCOMPARE(second.find(id)->name,QString("One"));QCOMPARE(libraryBytes(firstSettings),newer);
        const auto blocker=directory.filePath("block");QFile file(blocker);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write("blocked"),qint64(7));file.close();
        QSettings impossible(blocker+"/workspace.ini",QSettings::IniFormat);WorkspaceLibrary broken(&impossible);
        if(broken.load())QVERIFY(!broken.add("Not written",state,nullptr,&error));QVERIFY(broken.entries().isEmpty());QVERIFY(!broken.writable());
    }
    void entryLimitsAndDuplicateStoredIdentitiesAreValidated() {
        QTemporaryDir directory;QSettings settings(directory.filePath("workspace.ini"),QSettings::IniFormat);WorkspaceLibrary library(&settings);QVERIFY(library.load());const auto state=snapshot();
        for(int index=0;index<maxSavedWorkspaces;++index)QVERIFY(library.add(QString("Layout %1").arg(index),state));
        const auto good=libraryBytes(settings);QVERIFY(!library.add("One too many",state));QCOMPARE(libraryBytes(settings),good);
        auto root=QJsonDocument::fromJson(good).object();auto items=root["workspaces"].toArray();items[1]=items[0];root["workspaces"]=items;const auto bad=QJsonDocument(root).toJson();settings.setValue(WorkspaceLibrary::settingsKey(),bad);settings.sync();
        WorkspaceLibrary corrupt(&settings);QVERIFY(!corrupt.load());QVERIFY(!corrupt.writable());QCOMPARE(libraryBytes(settings),bad);
    }
};
QTEST_MAIN(WorkspaceLibraryTests)
#include "workspace_library_tests.moc"
