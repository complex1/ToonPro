#include "project.h"
#include "project_dialog.h"
#include "project_templates.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QTabBar>
#include <QTemporaryDir>
#include <QtTest>

using namespace toon;
namespace {
void chooseKind(ProjectDialog &dialog, ProjectKind kind) {
    auto *tabs=dialog.findChild<QTabBar *>("projectKindTabs");
    QVERIFY(tabs);
    for(int i=0;i<tabs->count();++i)if(tabs->tabData(i).toString()==projectKindKey(kind)){tabs->setCurrentIndex(i);return;}
    QFAIL("Missing project kind");
}
QJsonObject withShot(QJsonObject json, const char *key, const QJsonValue &value) {
    auto shots=json["shots"].toArray();auto shot=shots[0].toObject();shot.insert(key,value);shots[0]=shot;json["shots"]=shots;return json;
}
ValueSlider *number(ProjectDialog &dialog,const char *id){return dialog.findChild<ValueSlider *>(id);}
}

class ProjectKindTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void legacyProjectsRetainFilmAndPlanningDefaults(){
        const auto original=Project::create();const auto json=original.toJson();
        QCOMPARE(json["version"].toInt(),19);QVERIFY(!json.contains("projectKind"));
        QString error;const auto restored=Project::fromJson(json,&error);QVERIFY2(restored,qPrintable(error));
        QCOMPARE(restored->kind,ProjectKind::Film);QVERIFY(restored->shots[0].episode.isEmpty());
        QCOMPARE(restored->shots[0].productionStage,QString("planning"));QVERIFY(restored->shots[0].productionNotes.isEmpty());
        QCOMPARE(restored->toJson(),json);
    }
    void productionMetadataRoundTripsWithArtwork_data(){
        QTest::addColumn<int>("kind");
        QTest::newRow("film")<<int(ProjectKind::Film);
        QTest::newRow("series")<<int(ProjectKind::Series);
        QTest::newRow("drawing-only")<<int(ProjectKind::DrawingOnly);
    }
    void productionMetadataRoundTripsWithArtwork(){
        QFETCH(int,kind);auto project=Project::create(ProjectKind(kind));auto &shot=project.shots[0];
        shot.productionStage="layout";shot.productionNotes="Establish the platform.\nKeep the character silhouette readable.";
        if(project.kind==ProjectKind::Series)shot.episode="Episode 04";
        QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);tile.setPixelColor(7,19,QColor("#6a9bcd"));shot.layers[0].tiles.insert({0,0},tile);
        QTemporaryDir directory;QString error;const auto path=directory.filePath("production.toonproj");
        QVERIFY2(saveProject(project,path,&error),qPrintable(error));QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.read(10),QByteArray("TOONPRO38\n"));file.close();
        const auto restored=loadProject(path,&error);QVERIFY2(restored,qPrintable(error));
        QCOMPARE(restored->toJson(),project.toJson());QCOMPARE(restored->kind,ProjectKind(kind));
        QCOMPARE(restored->shots[0].layers[0].tiles,shot.layers[0].tiles);QCOMPARE(restored->shots[0].id,shot.id);
    }
    void malformedOrDowngradedPlanningMetadataIsRejected(){
        auto project=Project::create(ProjectKind::Series);QString error;auto json=project.toJson();
        json["projectKind"]="unknown";QVERIFY(!Project::fromJson(json,&error));QVERIFY(error.contains("project type"));
        json=project.toJson();json["projectKind"]=3;QVERIFY(!Project::fromJson(json,&error));
        json=project.toJson();json["version"]=37;QVERIFY(!Project::fromJson(json,&error));QVERIFY(error.contains("format 38"));
        for(const auto &entry:QList<QPair<QString,QJsonValue>>{{"episode",QString(101,'x')},{"episode",QString("Episode\n01")},{"productionStage","bogus"},{"productionStage",3},{"productionNotes",QString(16001,'x')},{"productionNotes",QString(QChar(0))}})
            QVERIFY2(!Project::fromJson(withShot(project.toJson(),entry.first.toUtf8().constData(),entry.second),&error),qPrintable(entry.first));
        project.kind=static_cast<ProjectKind>(99);QVERIFY(!project.validate().isEmpty());
    }
    void drawingOnlyHasAPlainStaticEditableCanvas(){
        const auto project=projectFromStarterTemplate("illustration",ProjectKind::DrawingOnly);QVERIFY(project);
        QVERIFY2(project->validate().isEmpty(),qPrintable(project->validate()));QCOMPARE(project->kind,ProjectKind::DrawingOnly);
        QCOMPARE(project->width,1920);QCOMPARE(project->height,1080);QCOMPARE(project->shots.size(),1);
        const auto &shot=project->shots[0];QCOMPARE(shot.durationFrames,1);QCOMPARE(shot.layers.size(),3);
        QVERIFY(shot.animationTracks.isEmpty());QVERIFY(shot.layerAnimations.isEmpty());QVERIFY(shot.camera2DKeys.isEmpty());QVERIFY(shot.sceneCameraAnimations.isEmpty());
        QVERIFY(!shot.sheet.showHeader);QVERIFY(!shot.sheet.showGuides);QCOMPARE(shot.sheet.left,0);QCOMPARE(shot.sheet.overscan,0);
        const auto active=starterDrawingLayerId(*project,"illustration");QVERIFY(!active.isEmpty());
        QVERIFY(!projectFromStarterTemplate("anime",ProjectKind::DrawingOnly));
        QVERIFY(!projectFromStarterTemplate("animatic",ProjectKind::DrawingOnly));
    }
    void creatingDrawingOnlyHidesTimingAndSelectsIllustration(){
        ProjectDialog dialog(Project::create(),true);chooseKind(dialog,ProjectKind::DrawingOnly);
        const auto project=dialog.resultProject();QCOMPARE(project.kind,ProjectKind::DrawingOnly);QCOMPARE(project.shots[0].durationFrames,1);
        QVERIFY(project.shots[0].animationTracks.isEmpty());QCOMPARE(dialog.selectedTemplateId(),QString("illustration"));QCOMPARE(dialog.requestedWorkspaceId(),QString("illustration"));
        QVERIFY(number(dialog,"frameRate")->parentWidget()->isHidden());QVERIFY(number(dialog,"durationFrames")->parentWidget()->isHidden());
        QVERIFY(dialog.findChild<QLineEdit *>("shotNumber")->isHidden());QVERIFY(dialog.findChild<QLineEdit *>("episodeName")->isHidden());
        QCOMPARE(dialog.findChild<QLineEdit *>("shotName")->accessibleName(),QString("Drawing name"));
        auto *tabs=dialog.findChild<QTabBar *>("projectTemplateTabs");for(int i=0;i<tabs->count();++i)QCOMPARE(tabs->isTabVisible(i),i<2);
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");
        if(!output.isEmpty()){QVERIFY(QDir().mkpath(output));dialog.show();QCoreApplication::processEvents();QVERIFY(dialog.grab().save(output+"/new-drawing-only-project.png"));}
    }
    void switchingTypesPreservesNamesDimensionsAndExplicitFilePath(){
        ProjectDialog dialog(Project::create(),true);
        dialog.findChild<QLineEdit *>("movieTitle")->setText("Mountain backgrounds");
        dialog.findChild<QLineEdit *>("shotName")->setText("Morning valley");
        dialog.findChild<QLineEdit *>("shotNumber")->setText("BG042");
        dialog.findChild<QLineEdit *>("projectPath")->setText("/tmp/backgrounds.toonproj");
        number(dialog,"frameWidth")->setValue(1536);number(dialog,"frameHeight")->setValue(864);number(dialog,"frameRate")->setValue(23.976);
        dialog.findChild<QCheckBox *>("projectTransparent")->setChecked(true);
        chooseKind(dialog,ProjectKind::DrawingOnly);auto result=dialog.resultProject();
        QCOMPARE(result.width,1536);QCOMPARE(result.height,864);QCOMPARE(result.fps,23.976);QVERIFY(result.transparent);
        QCOMPARE(result.movie,QString("Mountain backgrounds"));QCOMPARE(result.shots[0].name,QString("Morning valley"));QCOMPARE(result.shots[0].number,QString("BG042"));
        QCOMPARE(dialog.filePath(),QString("/tmp/backgrounds.toonproj"));QVERIFY(!dialog.projectFolderRequested());
        chooseKind(dialog,ProjectKind::Series);result=dialog.resultProject();QCOMPARE(result.kind,ProjectKind::Series);QCOMPARE(result.shots[0].episode,QString("Episode 01"));
        QCOMPARE(result.width,1536);QCOMPARE(result.height,864);QCOMPARE(dialog.selectedTemplateId(),QString("anime"));
        QVERIFY(!number(dialog,"frameRate")->parentWidget()->isHidden());QVERIFY(!dialog.findChild<QLineEdit *>("episodeName")->isHidden());
        dialog.findChild<QLineEdit *>("episodeName")->setText("The crossing");chooseKind(dialog,ProjectKind::Film);chooseKind(dialog,ProjectKind::Series);
        QCOMPARE(dialog.resultProject().shots[0].episode,QString("The crossing"));
    }
    void editingAnExistingDrawingDoesNotStripContent(){
        auto project=*projectFromStarterTemplate("anime");project.kind=ProjectKind::DrawingOnly;
        project.shots[0].productionNotes="Retain this imported animation.";ProjectDialog dialog(project,false);
        QVERIFY(!dialog.findChild<QTabBar *>("projectKindTabs"));QVERIFY(!dialog.findChild<QTabBar *>("projectTemplateTabs"));
        QCOMPARE(dialog.findChild<QLabel *>("projectKindLabel")->text(),QString("Drawing only"));
        QCOMPARE(dialog.resultProject().toJson(),project.toJson());QCOMPARE(dialog.resultProject().shots[0].animationTracks,project.shots[0].animationTracks);
    }
    void folderStorageIsExplicitAndLegacyPathsRemainUsable(){
        ProjectDialog dialog(Project::create(),true);auto *folder=dialog.findChild<QCheckBox *>("projectFolderStorage");auto *path=dialog.findChild<QLineEdit *>("projectPath");
        QVERIFY(folder);QVERIFY(folder->isChecked());QVERIFY(dialog.projectFolderRequested());
        path->setText("/tmp/My film");QCOMPARE(dialog.filePath(),QString("/tmp/My film.toonstudio"));
        path->setText("/tmp/My film.toonstudio");QCOMPARE(dialog.filePath(),QString("/tmp/My film.toonstudio"));
        path->setText("/tmp/Legacy.TOONPROJ");QVERIFY(!dialog.projectFolderRequested());QCOMPARE(dialog.filePath(),QString("/tmp/Legacy.TOONPROJ"));
        folder->setChecked(false);path->setText("/tmp/My drawing");QVERIFY(!dialog.projectFolderRequested());QCOMPARE(dialog.filePath(),QString("/tmp/My drawing.toonproj"));
    }
};
QTEST_MAIN(ProjectKindTests)
#include "project_kind_tests.moc"
