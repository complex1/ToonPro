#include "project_save.h"
#include "canvas.h"
#include "main_window.h"
#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
using namespace toon;
namespace {
Project layered(){
 auto project=Project::create();project.width=project.height=2048;
 for(int l=0;l<6;++l){if(l)project.shots[0].layers.append(RasterLayer::create(QString("Drawing %1").arg(l+1)));
  for(int y=0;y<5;++y)for(int x=0;x<5;++x){QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);
   for(int py=0;py<tileSize;++py){auto *row=reinterpret_cast<QRgb *>(tile.scanLine(py));for(int px=0;px<tileSize;++px)row[px]=qRgb((px*13+py*17+l*23)%256,(px*7+py*29)%256,(px*31+py*3+x*19+y*13)%256);}
   project.shots[0].layers[l].tiles.insert({x,y},tile);
  }
 }return project;
}
QByteArray bytes(const QString &path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return file.readAll();}
}
class ProjectSaveTests:public QObject{
 Q_OBJECT
private slots:
 void snapshotIsImmutableAndEventsRemainResponsive(){
  QTemporaryDir dir;auto project=layered();const auto original=project.toJson();const auto pixel=project.shots[0].layers[0].tiles.first().pixel(10,10);
  ProjectSave writer;QSignalSpy done(&writer,&ProjectSave::finished);int ticks=0;QTimer heartbeat;heartbeat.setInterval(1);connect(&heartbeat,&QTimer::timeout,this,[&]{++ticks;});heartbeat.start();
  QVERIFY(writer.start(project,dir.filePath("movie.toonproj")));QVERIFY(writer.busy());QVERIFY(!writer.start(project,dir.filePath("second.toonproj")));
  project.movie="Changed while saving";project.shots[0].layers[0].tiles.begin().value().fill(Qt::red);
  QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,15000);QVERIFY(done[0][0].toBool());QVERIFY(!writer.busy());QVERIFY(ticks>1);
  QString error;const auto saved=loadProject(dir.filePath("movie.toonproj"),&error);QVERIFY2(saved,qPrintable(error));QCOMPARE(saved->toJson(),original);QCOMPARE(saved->shots[0].layers[0].tiles.first().pixel(10,10),pixel);
  QCOMPARE(QDir(dir.path()).entryList({".toon-save-*"},QDir::Files|QDir::Hidden).size(),0);
 }
 void cancelledAndFailedWritesPreserveTheDestination(){
  QTemporaryDir dir;const auto path=dir.filePath("movie.toonproj");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write("original");file.close();
  ProjectSave writer;QSignalSpy done(&writer,&ProjectSave::finished);QVERIFY(writer.start(layered(),path));writer.cancel();
  QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);QVERIFY(done[0][1].toBool());QVERIFY(!done[0][0].toBool());QCOMPARE(bytes(path),QByteArray("original"));
  auto invalid=Project::create();invalid.width=0;QVERIFY(writer.start(invalid,path));QTRY_COMPARE(done.size(),2);QVERIFY(!done[1][0].toBool());QVERIFY(!done[1][3].toString().isEmpty());QCOMPARE(bytes(path),QByteArray("original"));
  QVERIFY(writer.start(Project::create(),dir.path()));QTRY_COMPARE(done.size(),3);QVERIFY(!done[2][0].toBool());QVERIFY(QFileInfo(dir.path()).isDir());
 }
 void destructionCannotPublishLateRecovery(){
  QTemporaryDir dir;const auto path=dir.filePath("late.recovery");
  {ProjectSave writer;QVERIFY(writer.start(layered(),path));}
  QTest::qWait(200);QVERIFY(!QFileInfo::exists(path));
 }
 void storageCancellationIsAtomic(){
  QTemporaryDir dir;const auto path=dir.filePath("movie.toonproj");QString error;QVERIFY(saveProject(Project::create(),path,&error));const auto old=bytes(path);int checks=0;
  QVERIFY(!saveProject(layered(),path,&error,[&]{return ++checks>8;}));QVERIFY(error.contains("cancelled"));QCOMPARE(bytes(path),old);
 }
 void manualSaveDoesNotMarkNewerEditsClean(){
  QTemporaryDir dir;const auto path=dir.filePath("movie.toonproj");QString error;auto p=layered();QVERIFY(saveProject(p,path,&error));
  MainWindow window(dir.filePath("workspace.ini"));QVERIFY(window.openProject(path));auto *doc=window.canvas()->document();auto first=doc->project();first.movie="Snapshot";doc->replaceProject(first,"Rename");
  QTimer::singleShot(0,&window,[doc]{auto newer=doc->project();newer.movie="Newer edit";doc->replaceProject(newer,"Rename again");});
  auto *save=window.findChild<QAction *>("saveProjectAction");QVERIFY(save);save->trigger();
  const auto stored=loadProject(path,&error);QVERIFY(stored);QCOMPARE(stored->movie,QString("Snapshot"));QCOMPARE(doc->project().movie,QString("Newer edit"));QVERIFY(!doc->history()->isClean());doc->history()->setClean();
 }
 void successfulSavePreservesPermissions(){
  QTemporaryDir dir;const auto path=dir.filePath("permissions.toonproj");QString error;QVERIFY(saveProject(Project::create(),path,&error));
  QVERIFY(QFile::setPermissions(path,QFile::ReadOwner|QFile::WriteOwner|QFile::ReadGroup));const auto permissions=QFileInfo(path).permissions();
  ProjectSave writer;QSignalSpy done(&writer,&ProjectSave::finished);QVERIFY(writer.start(Project::create(),path));QTRY_COMPARE(done.size(),1);QVERIFY(done[0][0].toBool());QCOMPARE(QFileInfo(path).permissions(),permissions);
 }
 void dirtyUndoRemovesAnAlreadyPublishedRecovery(){
  QTemporaryDir dir;const auto path=dir.filePath("movie.toonproj");QString error;QVERIFY(saveProject(Project::create(),path,&error));
  MainWindow window(dir.filePath("workspace.ini"));QVERIFY(window.openProject(path));auto *doc=window.canvas()->document();
  auto first=doc->project();first.movie="A";doc->replaceProject(first,"A");auto second=doc->project();second.movie="B";doc->replaceProject(second,"B");
  auto *timer=window.findChild<QTimer *>("recoveryTimer");auto *writer=window.findChild<ProjectSave *>("recoveryWriter");QSignalSpy done(writer,&ProjectSave::finished);
  QVERIFY(QMetaObject::invokeMethod(timer,"timeout",Qt::DirectConnection));QTRY_COMPARE(done.size(),1);QVERIFY(QFileInfo::exists(path+".recovery"));
  doc->history()->undo();QVERIFY(!doc->history()->isClean());QVERIFY(!QFileInfo::exists(path+".recovery"));doc->history()->setClean();
 }
 void recoveryIsBackgroundAndCannotResurrectAfterUndo(){
  QTemporaryDir dir;const auto path=dir.filePath("movie.toonproj");QString error;auto project=layered();QVERIFY(saveProject(project,path,&error));
  MainWindow window(dir.filePath("workspace.ini"));QVERIFY(window.openProject(path));auto *document=window.canvas()->document();
  auto changed=document->project();changed.movie="Edited";document->replaceProject(changed,"Rename");QVERIFY(!document->history()->isClean());
  auto *timer=window.findChild<QTimer *>("recoveryTimer");auto *writer=window.findChild<ProjectSave *>("recoveryWriter");QVERIFY(timer&&writer);QSignalSpy done(writer,&ProjectSave::finished);
  auto second=document->project();second.movie="Second edit";document->replaceProject(second,"Second rename");
  QVERIFY(QMetaObject::invokeMethod(timer,"timeout",Qt::DirectConnection));QVERIFY(writer->busy());document->history()->undo();
  QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);QVERIFY(!QFileInfo::exists(path+".recovery"));QVERIFY(!document->history()->isClean());document->history()->undo();QVERIFY(document->history()->isClean());
 }
};
QTEST_MAIN(ProjectSaveTests)
#include "project_save_tests.moc"
