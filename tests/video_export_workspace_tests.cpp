#include "audio.h"
#include "canvas.h"
#include "main_window.h"
#include "timeline_controller.h"
#include "value_slider.h"
#include "video_export_dialog.h"
#include <QAction>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
using namespace toon;
class VideoExportWorkspaceTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void exportResolvesPendingAudioAndPreservesPlayhead() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        MainWindow window(dir.filePath("workspace.ini"));
        auto *action = window.findChild<QAction *>("videoExportAction");
        QVERIFY(action); QVERIFY(!action->isEnabled());
        auto project = Project::create(); project.width = 192; project.height = 128;
        QString error;
        const auto asset = makeAudioAsset("Dialogue", QByteArray(audioSampleRate * audioBytesPerSampleFrame, '\0'), &error);
        QVERIFY2(asset, qPrintable(error));
        project.shots[0].audioClips = {{"e7b19cad-3356-4b9c-9990-bb8e5b9f8bc2", "Dialogue", asset, 3, 0, asset->sampleFrameCount(), .8, false}};
        const auto path = dir.filePath("export.toonproj");
        QVERIFY2(saveProject(project, path, &error), qPrintable(error));
        QVERIFY(window.openProject(path)); window.resize(1280, 900); window.show();
        auto *controller = window.findChild<TimelineController *>(); QVERIFY(controller);
        QVERIFY(controller->setFrame(12));
        QVERIFY(controller->selectAudioClip(project.shots[0].audioClips[0].id));
        auto *field = window.findChild<ValueSlider *>("audioClipVolume"); QVERIFY(field);
        auto *spin = field->findChild<QDoubleSpinBox *>(); QVERIFY(spin);
        spin->setFocus(); spin->selectAll(); QTest::keyClicks(spin, "35");
        QVERIFY(controller->hasPendingEdits()); QVERIFY(action->isEnabled());
        bool opened = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<VideoExportDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            opened = true;
            QCOMPARE(dialog->settings().firstFrame, 0);
            QCOMPARE(dialog->settings().lastFrame, 119);
            QCOMPARE(dialog->settings().size, QSize(192, 128));
            QVERIFY(dialog->settings().includeAudio);
            dialog->reject();
        });
        action->trigger();
        QVERIFY(opened); QVERIFY(!controller->hasPendingEdits());
        QCOMPARE(window.canvas()->shot().audioClips[0].gain, .35);
        QCOMPARE(window.canvas()->document()->currentFrame(), 12);
        QCOMPARE(window.canvas()->document()->history()->count(), 1);
        window.canvas()->document()->history()->setClean();
        QVERIFY(window.close());
    }
};
QTEST_MAIN(VideoExportWorkspaceTests)
#include "video_export_workspace_tests.moc"
