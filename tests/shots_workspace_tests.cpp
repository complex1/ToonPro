#include "canvas.h"
#include "camera_tool.h"
#include "keyframe_transform_tool.h"
#include "mask_tool.h"
#include "main_window.h"
#include "project_dialog.h"
#include "shots_panel.h"
#include "timeline_controller.h"
#include "value_slider.h"
#include <QAction>
#include <QDockWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest>
using namespace toon;
namespace {
Project movie() {
    auto p = Project::create(); p.width = p.height = 128; p.transparent = true;
    p.shots.append(Project::create().shots[0]); p.shots.append(Project::create().shots[0]);
    for (int i = 0; i < 3; ++i) {
        auto &s = p.shots[i]; s.number = QString("C%1").arg(i + 1, 3, 10, QChar('0'));
        s.name = QString("Shot %1").arg(i + 1); s.durationFrames = 24 + i * 12;
        s.sheet.overscan = 0;
        QImage image(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
        QPainter painter(&image); painter.fillRect(QRect(40,40,40,40), i == 0 ? Qt::red : i == 1 ? Qt::green : Qt::blue);
        painter.end(); s.layers[0].tiles[{0,0}] = image;
    }
    return p;
}
void answer(const char *name) {
    QTimer::singleShot(0, [name] {
        auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(dialog);
        auto *button = dialog->findChild<QPushButton *>(name); QVERIFY(button); button->click();
    });
}
}
class ShotsWorkspaceTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void canvasDrawingGuidesAndReferencesFollowActiveShot() {
        const auto p = movie(); Canvas canvas; canvas.resize(800,700); canvas.setProject(p,1); canvas.show();
        QCOMPARE(canvas.shot().id,p.shots[1].id); QCOMPARE(canvas.document()->activeShotIndex(),1);
        canvas.setTool(Canvas::Tool::Brush); canvas.setBrushColor(Qt::black); canvas.setBrushSize(12);
        const QPoint at = canvas.viewTransform().map(sheetGeometry(p,p.shots[1]).drawing.topLeft() + QPointF(18,18)).toPoint();
        QTest::mouseMove(&canvas,at); QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,at);
        QVERIFY(canvas.project().shots[1].layers[0].tiles != p.shots[1].layers[0].tiles);
        QCOMPARE(canvas.project().shots[0].layers[0].tiles,p.shots[0].layers[0].tiles);
        QCOMPARE(canvas.project().shots[2].layers[0].tiles,p.shots[2].layers[0].tiles);
        canvas.addDefaultGuide(GuideType::Perspective); QCOMPARE(canvas.shot().guides.size(),1);
        QVERIFY(canvas.project().shots[0].guides.isEmpty()); QVERIFY(canvas.project().shots[2].guides.isEmpty());
        QImage ref(8,8,QImage::Format_RGB32); ref.fill(Qt::magenta); QString error;
        QVERIFY2(canvas.document()->addReference("Reference",ref,&error),qPrintable(error));
        QCOMPARE(canvas.shot().references.size(),1); QVERIFY(canvas.project().shots[0].references.isEmpty());
        QVERIFY(canvas.document()->selectShot(p.shots[2].id));
        QCOMPARE(canvas.shot().id,p.shots[2].id); QVERIFY(canvas.drawingGuides().isEmpty());
        QCOMPARE(renderMovieFrame(canvas.project(),0,2).pixelColor(60,60),QColor(Qt::blue));
    }
    void timelineShotSwitchResolvesOrPreservesTransform() {
        Canvas canvas; const auto p=movie(); canvas.setProject(p); TimelineController timeline(&canvas);
        ShotsPanel panel(canvas.document(),[&] { return timeline.prepareTimelineEdit(); }); panel.show();
        auto *list=panel.findChild<QTreeWidget *>("shotsList"); QVERIFY(list); QCOMPARE(list->topLevelItemCount(),3);
        canvas.setTool(Canvas::Tool::Transform); QVERIFY(canvas.document()->transformActive());
        TransformParameters transform; transform.translation={12,8}; QVERIFY(canvas.document()->previewTransform(transform));
        list->setCurrentItem(list->topLevelItem(1)); answer("cancelPendingTransform");
        QTRY_COMPARE(list->currentItem(),list->topLevelItem(0));
        QCOMPARE(canvas.document()->activeShotId(),p.shots[0].id); QVERIFY(canvas.document()->transformActive());
        list->setCurrentItem(list->topLevelItem(1)); answer("applyPendingTransform");
        QTRY_COMPARE(canvas.document()->activeShotId(),p.shots[1].id); QVERIFY(!canvas.document()->transformActive());
        QCOMPARE(timeline.frameCount(),36); QCOMPARE(canvas.document()->history()->count(),1);
        QCOMPARE(renderMovieFrame(canvas.project(),0,0).pixelColor(86,70),QColor(Qt::red));
        QCOMPARE(canvas.project().shots[1].layers[0].tiles,p.shots[1].layers[0].tiles);
        canvas.document()->history()->undo(); QCOMPARE(canvas.document()->activeShotId(),p.shots[0].id);
        QCOMPARE(canvas.project().shots[0].layers[0].tiles,p.shots[0].layers[0].tiles);
        QCOMPARE(timeline.frameCount(),24);
    }
    void switchingShotClearsPreviousCanvasEditingMode() {
        Canvas canvas; const auto p=movie(); canvas.setProject(p); TimelineController timeline(&canvas);
        QVERIFY(timeline.selectCamera()); timeline.editCameraFraming(); QVERIFY(canvas.cameraTool()->enabled());
        QVERIFY(timeline.prepareTimelineEdit()); QVERIFY(canvas.document()->selectShot(p.shots[1].id));
        QVERIFY(!timeline.cameraSelected()); QVERIFY(!canvas.cameraTool()->enabled());
        canvas.keyframeTransformTool()->setEnabled(true);
        QVERIFY(canvas.document()->selectShot(p.shots[2].id)); QVERIFY(!canvas.keyframeTransformTool()->enabled());
        canvas.maskTool()->setEnabled(true);
        QVERIFY(canvas.document()->selectShot(p.shots[0].id)); QVERIFY(!canvas.maskTool()->enabled());
    }
    void projectSettingsEditSelectedShotAndSaveKeepsMovie() {
        const auto p=movie(); ProjectDialog settings(p,false,nullptr,2);
        settings.findChild<QLineEdit *>("shotName")->setText("Closing scene");
        const auto edited=settings.resultProject(); QCOMPARE(edited.shots[2].name,QString("Closing scene"));
        QCOMPARE(edited.shots[0].name,p.shots[0].name); QCOMPARE(edited.shots[1].name,p.shots[1].name);
        QTemporaryDir dir; QString error; const auto path=dir.filePath("movie.toonproj");
        QVERIFY2(saveProject(edited,path,&error),qPrintable(error));
        MainWindow window(dir.filePath("workspace.ini")); QVERIFY(window.openProject(path)); window.show();
        auto *dock=window.findChild<QDockWidget *>("shotsPanel"); QVERIFY(dock); dock->show();
        auto *list=dock->findChild<QTreeWidget *>("shotsList"); QVERIFY(list);
        list->setCurrentItem(list->topLevelItem(2)); QTRY_COMPARE(window.canvas()->document()->activeShotIndex(),2);
        QVERIFY(window.windowTitle().contains("C003"));
        window.canvas()->document()->changeLayer("Closing ink",true,false,false,1);
        auto *save=window.findChild<QAction *>("saveProjectAction"); QVERIFY(save); save->trigger();
        const auto loaded=loadProject(path,&error); QVERIFY2(loaded,qPrintable(error));
        QCOMPARE(loaded->shots.size(),3); QCOMPARE(loaded->shots[2].layers[0].name,QString("Closing ink"));
        QCOMPARE(loaded->shots[0].layers[0].name,p.shots[0].layers[0].name);
        QVERIFY(window.canvas()->document()->history()->isClean()); QVERIFY(window.close());
    }
};
QTEST_MAIN(ShotsWorkspaceTests)
#include "shots_workspace_tests.moc"
