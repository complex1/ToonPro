#include "audio.h"
#include "canvas.h"
#include "main_window.h"
#include "timeline_controller.h"
#include "value_slider.h"
#include <QAction>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QTabBar>
#include <QTemporaryDir>
#include <QtTest>
using namespace toon;
class AudioWorkspaceTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void pendingAudioFieldSaveUndoEscapeAndReopen(){
        QTemporaryDir dir;QVERIFY(dir.isValid());QString error;auto project=Project::create();project.width=project.height=128;
        const auto asset=makeAudioAsset("Dialogue",QByteArray(audioSampleRate*audioBytesPerSampleFrame,'\0'),&error);QVERIFY2(asset,qPrintable(error));
        project.shots[0].audioClips={{"73ff0779-85ab-41ad-929c-fd644382af24","Dialogue",asset,3,0,asset->sampleFrameCount(),.8,false}};
        const auto path=dir.filePath("audio.toonproj");QVERIFY2(saveProject(project,path,&error),qPrintable(error));
        MainWindow window(dir.filePath("workspace.ini"));QVERIFY(window.openProject(path));window.resize(1280,900);window.show();
        auto *controller=window.findChild<TimelineController *>();QVERIFY(controller);QVERIFY(controller->selectAudioClip("73ff0779-85ab-41ad-929c-fd644382af24"));QCoreApplication::processEvents();
        auto *dock=window.findChild<QDockWidget *>("framePropertiesPanel");QVERIFY(dock);QVERIFY(dock->isVisible());
        auto *field=dock->findChild<ValueSlider *>("audioClipVolume");QVERIFY(field);QVERIFY(field->isEnabled());
        auto *spin=field->findChild<QDoubleSpinBox *>();auto *undo=window.findChild<QAction *>("undoAction");auto *save=window.findChild<QAction *>("saveProjectAction");QVERIFY(spin&&undo&&save);
        const auto original=window.canvas()->project().toJson();const auto enter=[&](const QString &value){spin->setFocus();spin->selectAll();QTest::keyClicks(spin,value);};
        enter("35");QVERIFY(controller->hasPendingEdits());QVERIFY(undo->isEnabled());undo->trigger();
        QVERIFY(!controller->hasPendingEdits());QCOMPARE(window.canvas()->project().toJson(),original);QCOMPARE(controller->document()->history()->count(),0);
        enter("45");QVERIFY(controller->hasPendingEdits());QTest::keyClick(spin,Qt::Key_Escape);QVERIFY(!controller->hasPendingEdits());QCOMPARE(window.canvas()->project().toJson(),original);
        enter("60");QVERIFY(controller->hasPendingEdits());QVERIFY(save->isEnabled());save->trigger();
        QVERIFY(!controller->hasPendingEdits());QCOMPARE(controller->document()->history()->count(),1);QVERIFY(controller->document()->history()->isClean());
        QCOMPARE(window.canvas()->shot().audioClips.first().gain,.6);
        const auto saved=loadProject(path,&error);QVERIFY2(saved,qPrintable(error));QCOMPARE(saved->toJson(),window.canvas()->project().toJson());QCOMPARE(saved->shots[0].audioClips.first().asset->pcm,asset->pcm);
        window.canvas()->setFocus();QCoreApplication::processEvents();QCOMPARE(controller->document()->history()->count(),1);QVERIFY(window.close());
    }
};
QTEST_MAIN(AudioWorkspaceTests)
#include "audio_workspace_tests.moc"
