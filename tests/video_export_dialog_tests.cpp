#include "video_export_dialog.h"
#include "video_export.h"
#include "value_slider.h"
#include "ui_theme.h"
#include <QCheckBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QtTest>

using namespace toon;
namespace {
Project fixture() {
    auto project = Project::create(); project.width = 1920; project.height = 1080;
    project.fps = 23.976; project.shots.first().durationFrames = 240; return project;
}
QByteArray fileBytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
}
class VideoExportDialogTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void fullShotCustomAndPlaybackRangesStayExplicit() {
        auto project = fixture(); const auto original = project.toJson();
        VideoExportDialog dialog(project, 36, 83, true);
        QCOMPARE(dialog.settings().firstFrame, 0); QCOMPARE(dialog.settings().lastFrame, 239);
        QCOMPARE(dialog.settings().quality, VideoExportQuality::High); QVERIFY(!dialog.settings().includeAudio);
        QVERIFY(!dialog.findChild<QCheckBox *>("videoExportAudio")->isEnabled());
        auto *range = dialog.findChild<QTabBar *>("videoExportRange");
        auto *first = dialog.findChild<ValueSlider *>("videoExportFirst"); auto *last = dialog.findChild<ValueSlider *>("videoExportLast");
        QVERIFY(!first->isEnabled()); QVERIFY(!last->isEnabled());
        dialog.findChild<QToolButton *>("videoExportUsePlayback")->click();
        QCOMPARE(range->currentIndex(), 1); QCOMPARE(first->value(), 37); QCOMPARE(last->value(), 84);
        QCOMPARE(dialog.settings().firstFrame, 36); QCOMPARE(dialog.settings().lastFrame, 83);
        first->setValue(42); last->setValue(100); range->setCurrentIndex(0);
        QCOMPARE(first->value(), 1); QCOMPARE(last->value(), 240); QCOMPARE(dialog.settings().lastFrame, 239);
        range->setCurrentIndex(1); QCOMPARE(first->value(), 42); QCOMPARE(last->value(), 100);
        QCOMPARE(dialog.settings().firstFrame, 41); QCOMPARE(dialog.settings().lastFrame, 99);
        QCOMPARE(project.toJson(), original);
        project.shots.first().durationFrames = 2; QCOMPARE(dialog.settings().lastFrame, 99);
    }
    void dimensionsAspectQualityAndRangeValidation() {
        QTemporaryDir temp; VideoExportDialog dialog(fixture(), 0, 0, false); dialog.setDestination(temp.filePath("movie"));
        QCOMPARE(dialog.destination(), temp.filePath("movie.mp4"));
        auto *width = dialog.findChild<ValueSlider *>("videoExportWidth"); auto *height = dialog.findChild<ValueSlider *>("videoExportHeight");
        width->findChild<QDoubleSpinBox *>()->setValue(1280); QCOMPARE(dialog.settings().size, QSize(1280, 720));
        height->findChild<QDoubleSpinBox *>()->setValue(360); QCOMPARE(dialog.settings().size, QSize(640, 360));
        width->findChild<QDoubleSpinBox *>()->setValue(1281);
        QCOMPARE(dialog.settings().size.width() % 2, 0); QCOMPARE(dialog.settings().size.height() % 2, 0);
        dialog.findChild<QToolButton *>("videoExportAspect")->setChecked(false);
        height->findChild<QDoubleSpinBox *>()->setValue(1000); QCOMPARE(dialog.settings().size, QSize(1282, 1000));
        dialog.findChild<QTabBar *>("videoExportQuality")->setCurrentIndex(0); QCOMPARE(dialog.settings().quality, VideoExportQuality::Draft);
        dialog.findChild<QTabBar *>("videoExportRange")->setCurrentIndex(1);
        dialog.findChild<ValueSlider *>("videoExportFirst")->setValue(120);
        dialog.findChild<ValueSlider *>("videoExportLast")->setValue(12);
        QVERIFY(!dialog.findChild<QPushButton *>("videoExportStart")->isEnabled());
        QVERIFY(dialog.findChild<QLabel *>("videoExportStatus")->accessibleName().contains("end frame"));
        QVERIFY(!dialog.findChild<QToolButton *>("videoExportUsePlayback")->isEnabled());
        dialog.setDestination(temp.filePath("MOVIE.MP4")); QCOMPARE(dialog.destination(), temp.filePath("MOVIE.MP4"));
    }
    void audioChoiceUsesUnmutedEmbeddedClips() {
        auto project = fixture(); QString error;
        AudioClip clip; clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces); clip.name = "Test audio";
        clip.asset = makeAudioAsset("Silence", QByteArray(audioSampleRate * audioBytesPerSampleFrame, '\0'), &error);
        QVERIFY2(clip.asset, qPrintable(error)); clip.sampleCount = audioSampleRate; project.shots.first().audioClips = {clip};
        VideoExportDialog dialog(project, 0, 0, false);
        auto *audio = dialog.findChild<QCheckBox *>("videoExportAudio"); QVERIFY(audio->isEnabled()); QVERIFY(audio->isChecked());
        QVERIFY(dialog.settings().includeAudio); audio->setChecked(false); QVERIFY(!dialog.settings().includeAudio);
        project.shots.first().audioClips.first().muted = true; VideoExportDialog muted(project, 0, 0, false);
        QVERIFY(!muted.findChild<QCheckBox *>("videoExportAudio")->isEnabled()); QVERIFY(!muted.settings().includeAudio);
    }
    void exactEntryEnterAndEscapeDoNotExport() {
        QTemporaryDir temp; VideoExportDialog dialog(fixture(), 0, 0, false); dialog.setDestination(temp.filePath("entry.mp4"));
        dialog.show(); QTest::qWait(20);
        auto *width = dialog.findChild<ValueSlider *>("videoExportWidth")->findChild<QDoubleSpinBox *>();
        auto *exporter = dialog.findChild<VideoExport *>(); QSignalSpy finished(exporter, &VideoExport::finished);
        width->setFocus(); width->selectAll(); QTest::keyClicks(width, "640"); QTest::keyClick(width, Qt::Key_Return);
        QCOMPARE(dialog.settings().size, QSize(640, 360)); QVERIFY(dialog.isVisible()); QVERIFY(!exporter->busy()); QCOMPARE(finished.count(), 0);
        width->selectAll(); QTest::keyClicks(width, "1200"); QTest::keyClick(width, Qt::Key_Escape);
        QCOMPARE(dialog.settings().size, QSize(640, 360)); QVERIFY(dialog.isVisible()); QVERIFY(!QFileInfo::exists(dialog.destination()));
        dialog.reject();
    }
    void existingNormalizedTargetGetsOneConfirmationAndCancelPreservesIt() {
        if (!VideoExport::availabilityError(false).isEmpty()) QSKIP("H.264 encoder unavailable");
        QTemporaryDir temp; const auto target = temp.filePath("existing.mp4"); QFile file(target);
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("original file"); file.close();
        VideoExportDialog dialog(fixture(), 0, 0, false); dialog.setDestination(temp.filePath("existing")); dialog.show();
        int confirmations = 0;
        QTimer answer; answer.setInterval(5);
        connect(&answer, &QTimer::timeout, this, [&] {
            auto *box = dialog.findChild<QMessageBox *>("videoExportOverwrite"); if (!box || !box->isVisible()) return;
            ++confirmations; box->done(QMessageBox::Cancel); answer.stop();
        });
        QTimer::singleShot(3000, &dialog, [&dialog] {
            for (auto *box : dialog.findChildren<QMessageBox *>()) if (box->isVisible()) box->done(QMessageBox::Cancel);
        });
        answer.start(); dialog.findChild<QPushButton *>("videoExportStart")->click(); answer.stop();
        QCOMPARE(confirmations, 1); QCOMPARE(fileBytes(target), QByteArray("original file"));
        QVERIFY(!dialog.findChild<VideoExport *>()->busy()); QVERIFY(dialog.isVisible()); dialog.reject();
    }
    void browseExistingFileDefersOverwriteUntilExport() {
        QTemporaryDir temp; const auto target = temp.filePath("chosen.mp4"); QFile original(target);
        QVERIFY(original.open(QIODevice::WriteOnly)); original.write("keep me"); original.close();
        VideoExportDialog dialog(fixture(), 0, 0, false); dialog.show();
        bool chose = false, nonNative = false, noEarlyConfirmation = false; int unexpectedConfirmations = 0;
        QTimer choose; choose.setInterval(5);
        connect(&choose, &QTimer::timeout, this, [&] {
            for (auto *box : dialog.findChildren<QMessageBox *>()) if (box->isVisible()) {
                ++unexpectedConfirmations; box->done(QMessageBox::Cancel);
            }
            auto *picker = dialog.findChild<QFileDialog *>(); if (!picker || !picker->isVisible()) return;
            nonNative = picker->testOption(QFileDialog::DontUseNativeDialog);
            noEarlyConfirmation = picker->testOption(QFileDialog::DontConfirmOverwrite);
            picker->selectFile(target); chose = QMetaObject::invokeMethod(picker, "accept", Qt::DirectConnection);
        });
        QTimer::singleShot(3000, &dialog, [&dialog] {
            for (auto *box : dialog.findChildren<QMessageBox *>()) if (box->isVisible()) box->done(QMessageBox::Cancel);
            for (auto *picker : dialog.findChildren<QFileDialog *>()) if (picker->isVisible()) picker->reject();
        });
        choose.start(); dialog.findChild<QToolButton *>("videoExportBrowse")->click(); choose.stop();
        QVERIFY(chose); QVERIFY(nonNative); QVERIFY(noEarlyConfirmation); QCOMPARE(unexpectedConfirmations, 0);
        QCOMPARE(dialog.destination(), target); QCOMPARE(fileBytes(target), QByteArray("keep me"));
        QVERIFY(!dialog.findChild<VideoExport *>()->busy()); dialog.reject();
    }
    void cancelAndWindowCloseWaitForWorker_data() {
        QTest::addColumn<bool>("windowClose"); QTest::newRow("cancel-button") << false; QTest::newRow("window-close") << true;
    }
    void cancelAndWindowCloseWaitForWorker() {
        if (!VideoExport::availabilityError(false).isEmpty()) QSKIP("H.264 encoder unavailable");
        QFETCH(bool, windowClose); QTemporaryDir temp; const auto target = temp.filePath("cancel.mp4");
        auto project = fixture(); project.width = 64; project.height = 64; project.shots.first().durationFrames = 100000;
        VideoExportDialog dialog(project, 0, 0, false); dialog.setDestination(target); dialog.show();
        auto *exporter = dialog.findChild<VideoExport *>(); QSignalSpy finished(exporter, &VideoExport::finished);
        dialog.findChild<QPushButton *>("videoExportStart")->click(); QVERIFY(exporter->busy());
        QVERIFY(!dialog.findChild<QWidget *>("videoExportControls")->isEnabled());
        if (windowClose) dialog.close(); else dialog.findChild<QPushButton *>("videoExportCancel")->click();
        QVERIFY(dialog.isVisible()); QVERIFY(!dialog.findChild<QPushButton *>("videoExportCancel")->isEnabled());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000); QTRY_VERIFY_WITH_TIMEOUT(!dialog.isVisible(), 15000);
        QVERIFY(!exporter->busy()); QVERIFY(!QFileInfo::exists(target));
        QCOMPARE(qvariant_cast<VideoExportResult>(finished.first().first()).outcome, VideoExportOutcome::Cancelled);
    }
    void failureRetainsSettingsAndAllowsRetry() {
        if (!VideoExport::availabilityError(false).isEmpty()) QSKIP("H.264 encoder unavailable");
        QTemporaryDir temp; auto project = fixture(); project.width = 64; project.height = 64; project.shots.first().durationFrames = 2;
        VideoExportDialog dialog(project, 0, 0, false); dialog.show(); dialog.setDestination(temp.filePath("missing/video.mp4"));
        auto *exporter = dialog.findChild<VideoExport *>(); QSignalSpy finished(exporter, &VideoExport::finished), exported(&dialog, &VideoExportDialog::exported);
        dialog.findChild<QPushButton *>("videoExportStart")->click();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
        QCOMPARE(qvariant_cast<VideoExportResult>(finished.first().first()).outcome, VideoExportOutcome::Failed);
        QVERIFY(dialog.isVisible()); QVERIFY(dialog.findChild<QWidget *>("videoExportControls")->isEnabled());
        QVERIFY(!dialog.findChild<QLabel *>("videoExportStatus")->accessibleName().isEmpty());
        QCOMPARE(dialog.settings().size, QSize(64, 64)); QCOMPARE(dialog.settings().lastFrame, 1);
        dialog.setDestination(temp.filePath("success.mp4")); dialog.findChild<QPushButton *>("videoExportStart")->click();
        QTRY_COMPARE_WITH_TIMEOUT(exported.count(), 1, 20000); QVERIFY(!dialog.isVisible());
        QVERIFY(QFileInfo(dialog.destination()).size() > 0);
    }
    void layoutKeepsFixedStatusAndCompactFields() {
        QTemporaryDir temp; VideoExportDialog dialog(fixture(), 24, 143, true); dialog.setDestination(temp.filePath("anime-shot.mp4"));
        dialog.findChild<QToolButton *>("videoExportUsePlayback")->click();
        dialog.findChild<ValueSlider *>("videoExportWidth")->findChild<QDoubleSpinBox *>()->setValue(1280);
        dialog.findChild<QTabBar *>("videoExportQuality")->setCurrentIndex(1); dialog.show();
        const QString output = qEnvironmentVariable("TOON_UI_PREVIEW", "test-artifacts"); QVERIFY(QDir().mkpath(output));
        auto *status = dialog.findChild<QLabel *>("videoExportStatus");
        auto *progress = dialog.findChild<QProgressBar *>("videoExportProgress");
        for (const int width : {520, 360}) {
            dialog.resize(width, 560); QTest::qWait(20); QCOMPARE(dialog.width(), width);
            const auto statusHeight = status->height(); const auto progressPosition = progress->pos();
            dialog.findChild<ValueSlider *>("videoExportFirst")->setValue(240);
            QCOMPARE(status->height(), statusHeight); QCOMPARE(progress->pos(), progressPosition);
            dialog.findChild<ValueSlider *>("videoExportFirst")->setValue(25);
            for (auto *number : dialog.findChildren<QDoubleSpinBox *>()) QCOMPARE(number->width(), 48);
            for (auto *control : dialog.findChildren<ValueSlider *>()) {
                const auto rect = QRect(control->mapTo(&dialog, QPoint()), control->size());
                QVERIFY2(rect.left() >= 0 && rect.right() < dialog.width(), qPrintable(control->objectName()));
            }
            QVERIFY(dialog.grab().save(QDir(output).filePath(QString("video-export-dialog-%1.png").arg(width))));
        }
        dialog.reject();
    }
};
QTEST_MAIN(VideoExportDialogTest)
#include "video_export_dialog_tests.moc"
