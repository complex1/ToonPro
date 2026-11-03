#include "project_templates.h"
#include "project_dialog.h"
#include "canvas.h"
#include "sheet.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalSpy>
#include <QStyle>
#include <QSlider>
#include <QTabBar>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
using namespace toon;
namespace {
QJsonObject firstShot(const QJsonObject &json){return json["shots"].toArray()[0].toObject();}
QJsonObject withSheet(QJsonObject json,const QJsonObject &sheet){auto shots=json["shots"].toArray();auto shot=shots[0].toObject();shot["sheet"]=sheet;shots[0]=shot;json["shots"]=shots;return json;}
QTabBar *templates(ProjectDialog &dialog){return dialog.findChild<QTabBar *>("projectTemplateTabs");}
void choose(ProjectDialog &dialog,const QString &id){auto *tabs=templates(dialog);for(int i=0;i<tabs->count();++i)if(tabs->tabData(i)==id){tabs->setCurrentIndex(i);return;}}
ValueSlider *number(ProjectDialog &dialog,const char *name){return dialog.findChild<ValueSlider *>(name);}
void settle(){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);}
QVector<QString> layerIds(const Project &project){QVector<QString> result;for(const auto &layer:project.shots[0].layers)result.append(layer.id);return result;}
}
class ProjectStartersTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void fiveStartersHaveRealDistinctEditableContent(){
        QCOMPARE(projectStarterTemplates().size(),5);QSet<QString> ids;
        for(const auto &info:projectStarterTemplates()){
            QVERIFY(!ids.contains(info.id));ids.insert(info.id);auto project=projectFromStarterTemplate(info.id);QVERIFY(project);QVERIFY2(project->validate().isEmpty(),qPrintable(project->validate()));
            QCOMPARE(project->shots.size(),1);const auto &shot=project->shots[0];QVERIFY(!shot.layers.isEmpty());QVERIFY(shot.audioClips.isEmpty());
            const auto preferred=starterDrawingLayerId(*project,info.id);QVERIFY(!preferred.isEmpty());QVERIFY(activeLayerIds(shot,0).contains(preferred));
            const auto duplicate=projectFromStarterTemplate(info.id);QVERIFY(duplicate);QVERIFY(layerIds(*duplicate)!=layerIds(*project));
            if(info.id=="blank"||info.id=="illustration"){QCOMPARE(shot.durationFrames,1);QVERIFY(shot.animationTracks.isEmpty());}
            else {QCOMPARE(shot.animationTracks.size(),1);QCOMPARE(shot.animationTracks[0].drawings.size(),1);QCOMPARE(shot.animationTracks[0].exposures.size(),1);}
            if(info.id=="blank")QCOMPARE(shot.layers.size(),1);
            if(info.id=="illustration"){QCOMPARE(project->width,2048);QCOMPARE(project->height,2048);QCOMPARE(shot.layers.size(),3);}
            if(info.id=="anime"){QCOMPARE(shot.durationFrames,120);QCOMPARE(shot.animationTracks[0].drawingTemplate.size(),6);QCOMPARE(shot.animationTracks[0].exposures[0].durationFrames,2);}
            if(info.id=="storyboard"){QCOMPARE(shot.durationFrames,72);QCOMPARE(shot.animationTracks[0].exposures[0].durationFrames,72);}
            if(info.id=="animatic"){QCOMPARE(shot.durationFrames,240);QCOMPARE(shot.animationTracks[0].exposures[0].durationFrames,48);}
        }
        QVERIFY(!projectFromStarterTemplate("not-a-template"));
    }
    void plainCanvasHasExactGeometryAndNoProductionMarks(){
        auto project=*projectFromStarterTemplate("blank");project.width=176;project.height=128;project.movie=QString(190,'W');project.shots[0].name=QString(190,'x');
        const auto geometry=sheetGeometry(project,project.shots[0]);QCOMPARE(geometry.paper,QRectF(0,0,176,128));QCOMPARE(geometry.output,geometry.paper);QCOMPARE(geometry.drawing,geometry.paper);QVERIFY(geometry.header.isEmpty());
        const auto sheet=renderSheet(project,project.shots[0],true),frame=renderMovieFrame(project);QCOMPARE(sheet,frame);QCOMPARE(sheet.pixelColor(0,0),QColor(Qt::white));
        for(int y=0;y<sheet.height();++y)for(int x=0;x<sheet.width();++x)QCOMPARE(sheet.pixelColor(x,y),QColor(Qt::white));
        project.transparent=true;QCOMPARE(renderMovieFrame(project).pixelColor(20,20).alpha(),0);
        project.shots[0].sheet.header=4096;QCOMPARE(sheetGeometry(project,project.shots[0]).paper.size(),QSizeF(176,128));
    }
    void legacyZeroHeaderStillFitsMetadataAndGuidesAreIndependent(){
        auto project=Project::create();project.width=240;project.height=180;project.shots[0].sheet.header=0;
        const auto legacy=project.toJson();QCOMPARE(legacy["version"].toInt(),19);const auto metadata=firstShot(legacy)["sheet"].toObject();QVERIFY(!metadata.contains("showHeader"));QVERIFY(!metadata.contains("showGuides"));
        QString error;const auto loaded=Project::fromJson(legacy,&error);QVERIFY2(loaded,qPrintable(error));QVERIFY(loaded->shots[0].sheet.showHeader);QVERIFY(loaded->shots[0].sheet.showGuides);QVERIFY(sheetGeometry(*loaded,loaded->shots[0]).header.height()>100);
        const auto frame=renderMovieFrame(project),marked=renderSheet(project,project.shots[0],true);project.shots[0].sheet.showGuides=false;
        QCOMPARE(sheetGeometry(project,project.shots[0]).header.height(),sheetGeometry(*loaded,loaded->shots[0]).header.height());QCOMPARE(renderMovieFrame(project),frame);QVERIFY(renderSheet(project,project.shots[0],true)!=marked);
    }
    void presentationFlagsRoundTripWithPixelsAndPromoteOnlyWhenNeeded(){
        QTemporaryDir directory;QString error;
        for(const auto &info:projectStarterTemplates()){
            auto project=*projectFromStarterTemplate(info.id);auto &layer=project.shots[0].layers[0];QImage pixels(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);pixels.fill(Qt::transparent);pixels.setPixelColor(18,23,QColor("#66bbdd"));layer.tiles.insert({0,0},pixels);
            const auto json=project.toJson();const bool plain=info.id=="blank"||info.id=="illustration"||info.id=="animatic";QCOMPARE(json["version"].toInt(),plain?37:19);
            const auto path=directory.filePath(info.id+".toonproj");QVERIFY2(saveProject(project,path,&error),qPrintable(error));const auto restored=loadProject(path,&error);QVERIFY2(restored,qPrintable(error));QCOMPARE(restored->toJson(),json);QCOMPARE(restored->shots[0].layers[0].tiles,layer.tiles);
            QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.read(10),plain?QByteArray("TOONPRO37\n"):QByteArray("TOONPRO19\n"));
        }
    }
    void nestedSymbolPresentationIsSavedAndInvalidMetadataRejected(){
        auto project=Project::create();auto content=Project::create().shots[0];content.sheet.showHeader=false;project.symbols.append({QUuid::createUuid().toString(QUuid::WithoutBraces),"Plain symbol",content});
        QString error;QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));QCOMPARE(project.toJson()["version"].toInt(),37);
        QTemporaryDir directory;const auto path=directory.filePath("nested.toonproj");QVERIFY2(saveProject(project,path,&error),qPrintable(error));const auto restored=loadProject(path,&error);QVERIFY2(restored,qPrintable(error));QVERIFY(!restored->symbols[0].content.sheet.showHeader);
        auto json=projectFromStarterTemplate("blank")->toJson();auto sheet=firstShot(json)["sheet"].toObject();sheet["showHeader"]="false";QVERIFY(!Project::fromJson(withSheet(json,sheet),&error));
        sheet=firstShot(json)["sheet"].toObject();sheet["showGuides"]=0;QVERIFY(!Project::fromJson(withSheet(json,sheet),&error));
        json["version"]=36;QVERIFY(!Project::fromJson(json,&error));QVERIFY(error.contains("format 37"));
        QFile file(path);QVERIFY(file.open(QIODevice::ReadWrite));QCOMPARE(file.write("TOONPRO36\n"),qint64(10));file.close();QVERIFY(!loadProject(path,&error));QVERIFY(error.contains("container format"));
    }
    void templateSwitchPreservesNamesPathAndExplicitWorkspaceChoice(){
        ProjectDialog dialog(Project::create(),true);QCOMPARE(dialog.selectedTemplateId(),QString("anime"));QCOMPARE(dialog.requestedWorkspaceId(),QString("anime"));
        dialog.findChild<QLineEdit *>("movieTitle")->setText("My film");dialog.findChild<QLineEdit *>("shotNumber")->setText("C042");dialog.findChild<QLineEdit *>("shotName")->setText("The bridge");dialog.findChild<QLineEdit *>("projectPath")->setText("/tmp/artist-work.toonproj");
        choose(dialog,"blank");QCOMPARE(dialog.requestedWorkspaceId(),QString("illustration"));QVERIFY(!dialog.resultProject().shots[0].sheet.showHeader);QCOMPARE(dialog.resultProject().movie,QString("My film"));QCOMPARE(dialog.resultProject().shots[0].number,QString("C042"));QCOMPARE(dialog.resultProject().shots[0].name,QString("The bridge"));QCOMPARE(dialog.filePath(),QString("/tmp/artist-work.toonproj"));
        auto *workspace=dialog.findChild<QComboBox *>("projectWorkspace");workspace->setCurrentIndex(workspace->findData(QString()));choose(dialog,"storyboard");QVERIFY(dialog.requestedWorkspaceId().isEmpty());QCOMPARE(dialog.resultProject().shots[0].durationFrames,72);
        workspace->setCurrentIndex(workspace->findData("animatic"));choose(dialog,"illustration");QCOMPARE(dialog.requestedWorkspaceId(),QString("animatic"));
        const auto ids=layerIds(dialog.resultProject());number(dialog,"frameWidth")->setValue(1800);number(dialog,"frameRate")->setValue(23.976);QCOMPARE(layerIds(dialog.resultProject()),ids);
        const auto preferred=dialog.requestedLayerId();QVERIFY(ids.contains(preferred));for(const auto &layer:dialog.resultProject().shots[0].layers)if(layer.id==preferred)QCOMPARE(layer.name,QString("Sketch"));
        QCOMPARE(dialog.findChild<Canvas *>()->project().width,1800);QCOMPARE(dialog.findChild<Canvas *>()->tool(),Canvas::Tool::Hand);
    }
    void durationAdjustmentsKeepStarterExposuresValidAndControlsRemainEditable(){
        ProjectDialog dialog(Project::create(),true);
        for(const auto &id:{"anime","storyboard","animatic"}){
            choose(dialog,id);number(dialog,"durationFrames")->setValue(1);const auto project=dialog.resultProject();QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));QCOMPARE(project.shots[0].animationTracks[0].exposures[0].durationFrames,1);
        }
        choose(dialog,"storyboard");number(dialog,"durationFrames")->setValue(120);QCOMPARE(dialog.resultProject().shots[0].animationTracks[0].exposures[0].durationFrames,120);
        choose(dialog,"blank");dialog.findChild<QCheckBox *>("projectShowHeader")->setChecked(true);dialog.findChild<QCheckBox *>("projectShowGuides")->setChecked(true);number(dialog,"overscan")->setValue(60);number(dialog,"leftMargin")->setValue(32);
        const auto project=dialog.resultProject();QVERIFY(project.shots[0].sheet.showHeader);QVERIFY(project.shots[0].sheet.showGuides);QCOMPARE(project.shots[0].sheet.overscan,60);QCOMPARE(project.shots[0].sheet.left,32);QVERIFY(sheetGeometry(project,project.shots[0]).paper.width()>project.width);
    }
    void editSettingsDoesNotApplyTemplatesOrReplaceDrawings(){
        auto project=*projectFromStarterTemplate("anime");auto other=Project::create().shots[0];other.number="C002";project.shots.append(other);
        ProjectDialog dialog(project,false,nullptr,1);QVERIFY(!templates(dialog));QVERIFY(!dialog.findChild<QComboBox *>("projectWorkspace"));QVERIFY(dialog.requestedWorkspaceId().isEmpty());QVERIFY(dialog.requestedLayerId().isEmpty());QCOMPARE(dialog.resultProject().toJson(),project.toJson());
        dialog.findChild<QCheckBox *>("projectShowHeader")->setChecked(false);dialog.findChild<QLineEdit *>("shotName")->setText("Second cut");const auto updated=dialog.resultProject();QCOMPARE(updated.shots[0].animationTracks,project.shots[0].animationTracks);QCOMPARE(firstShot(updated.toJson()),firstShot(project.toJson()));QVERIFY(updated.shots[0].sheet.showHeader);QVERIFY(!updated.shots[1].sheet.showHeader);QCOMPARE(updated.shots[1].name,QString("Second cut"));
    }
    void numericDraftSurvivesResizeAndEnterDoesNotCreateProject(){
        ProjectDialog dialog(Project::create(),true);dialog.show();QVERIFY(QTest::qWaitForWindowExposed(&dialog));dialog.raise();dialog.activateWindow();QVERIFY(QTest::qWaitForWindowActive(&dialog));
        auto *fps=number(dialog,"frameRate");auto *field=fps->findChild<QDoubleSpinBox *>();dialog.findChild<QScrollArea *>()->ensureWidgetVisible(field);field->setFocus();QTRY_VERIFY(field->hasFocus());field->selectAll();QTest::keyClicks(field,"23.976");dialog.resize(700,680);settle();QCOMPARE(field->findChild<QLineEdit *>()->text(),QString("23.976"));QVERIFY(field->hasFocus());
        QSignalSpy accepted(&dialog,&QDialog::accepted);QTest::keyClick(field,Qt::Key_Return);QCOMPARE(accepted.count(),0);QCOMPARE(dialog.resultProject().fps,23.976);QVERIFY(dialog.isVisible());
        auto *width=number(dialog,"frameWidth");auto *height=number(dialog,"frameHeight");dialog.findChild<QCheckBox *>("projectAspectLock")->setChecked(true);width->setValue(1280);QCOMPARE(height->value(),720.);
    }
    void templateLayoutHasAccessibleNavigationAndPreviewCaptures(){
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");if(!output.isEmpty())QVERIFY(QDir().mkpath(output));
        ProjectDialog dialog(Project::create(),true);dialog.resize(960,720);dialog.show();settle();auto *tabs=templates(dialog);QCOMPARE(tabs->count(),5);
        for(int i=0;i<tabs->count();++i){tabs->setCurrentIndex(i);settle();QVERIFY(!tabs->tabToolTip(i).isEmpty());QVERIFY(!tabs->accessibleTabName(i).isEmpty());QVERIFY(!tabs->tabIcon(i).isNull());QCOMPARE(tabs->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,tabs),600);
            if(!output.isEmpty())QVERIFY(dialog.grab().save(output+"/new-project-"+tabs->tabData(i).toString()+".png"));}
        dialog.resize(680,640);QFont font=dialog.font();font.setPixelSize(dialog.fontMetrics().height()+3);dialog.setFont(font);settle();QCOMPARE(dialog.width(),680);QCOMPARE(dialog.height(),640);
        auto *preview=dialog.findChild<Canvas *>();QVERIFY(preview->isVisible());QVERIFY(preview->width()>=260);
        for(auto *value:dialog.findChildren<ValueSlider *>()){QVERIFY(value->findChild<QSlider *>());QVERIFY(value->findChild<QDoubleSpinBox *>());}
        if(!output.isEmpty())QVERIFY(dialog.grab().save(output+"/new-project-narrow-large-text.png"));
    }
};
QTEST_MAIN(ProjectStartersTests)
#include "project_starters_tests.moc"
