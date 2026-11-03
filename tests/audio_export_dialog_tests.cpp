#include "audio_export_dialog.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

using namespace toon;
namespace {
Project fixture() {
    auto project = Project::create(); project.fps = 23.976; project.shots[0].durationFrames = 120;
    auto second = Project::create().shots[0]; second.number = "C002"; second.durationFrames = 240;
    project.shots.append(second); return project;
}
QByteArray fileBytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
}
class AudioExportDialogTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void shotMovieAndPlaybackRangesKeepIndependentDrafts() {
        const auto project = fixture(); const auto original = project.toJson(); AudioExportDialog dialog(project, 1, 36, 83, true);
        QCOMPARE(dialog.settings().scope, AudioExportScope::Shot); QCOMPARE(dialog.settings().shotIndex, 1);
        QCOMPARE(dialog.settings().firstFrame, 0); QCOMPARE(dialog.settings().lastFrame, 239);
        auto *scope = dialog.findChild<QTabBar *>("audioExportScope"); auto *range = dialog.findChild<QTabBar *>("audioExportRange");
        auto *first = dialog.findChild<ValueSlider *>("audioExportFirst"); auto *last = dialog.findChild<ValueSlider *>("audioExportLast");
        auto *playback = dialog.findChild<QToolButton *>("audioExportUsePlayback");
        QVERIFY(!first->isEnabled()); playback->click(); QCOMPARE(range->currentIndex(), 1);
        QCOMPARE(first->value(), 37); QCOMPARE(last->value(), 84);
        first->setValue(42); last->setValue(100); range->setCurrentIndex(0); QCOMPARE(first->value(), 1); QCOMPARE(last->value(), 240);
        range->setCurrentIndex(1); QCOMPARE(first->value(), 42); QCOMPARE(last->value(), 100);
        scope->setCurrentIndex(1); QCOMPARE(dialog.settings().scope, AudioExportScope::Movie);
        QCOMPARE(dialog.settings().firstFrame, 0); QCOMPARE(dialog.settings().lastFrame, 359); QVERIFY(!playback->isEnabled());
        range->setCurrentIndex(1); first->setValue(111); last->setValue(270);
        scope->setCurrentIndex(0); QCOMPARE(first->value(), 42); QCOMPARE(last->value(), 100); QVERIFY(playback->isEnabled());
        scope->setCurrentIndex(1); QCOMPARE(first->value(), 111); QCOMPARE(last->value(), 270); QCOMPARE(project.toJson(), original);
    }
    void invalidRangeAndLargeRiffAreExplained() {
        QTemporaryDir dir; AudioExportDialog dialog(fixture(), 0, 0, 0, false); dialog.setDestination(dir.filePath("mix"));
        QCOMPARE(dialog.destination(), dir.filePath("mix.wav"));
        dialog.findChild<QTabBar *>("audioExportRange")->setCurrentIndex(1);
        dialog.findChild<ValueSlider *>("audioExportFirst")->setValue(100); dialog.findChild<ValueSlider *>("audioExportLast")->setValue(20);
        QVERIFY(!dialog.findChild<QPushButton *>("audioExportStart")->isEnabled());
        QVERIFY(!dialog.findChild<QLabel *>("audioExportStatus")->accessibleName().isEmpty());
        QVERIFY(!dialog.findChild<QToolButton *>("audioExportUsePlayback")->isEnabled());
        auto project = fixture(); project.fps = 1; project.shots[0].durationFrames = 1000000;
        AudioExportDialog huge(project, 0, 0, 0, false); huge.setDestination(dir.filePath("large.wav"));
        QVERIFY(!huge.findChild<QPushButton *>("audioExportStart")->isEnabled());
        QVERIFY(huge.findChild<QLabel *>("audioExportStatus")->accessibleName().contains("WAV file size limit"));
    }
    void exactEntryDoesNotExportAndCancellationKeepsDraft() {
        QTemporaryDir dir; AudioExportDialog dialog(fixture(), 0, 0, 0, false); dialog.setDestination(dir.filePath("entry.wav"));
        dialog.findChild<QTabBar *>("audioExportRange")->setCurrentIndex(1); dialog.show(); QTest::qWait(20);
        auto *number = dialog.findChild<ValueSlider *>("audioExportFirst")->findChild<QDoubleSpinBox *>();
        auto *exporter = dialog.findChild<AudioExport *>();
        number->setFocus(); number->selectAll(); QTest::keyClicks(number, "24"); QTest::keyClick(number, Qt::Key_Return);
        QCOMPARE(dialog.settings().firstFrame, 23); QVERIFY(dialog.isVisible()); QVERIFY(!exporter->busy());
        number->selectAll(); QTest::keyClicks(number, "48"); QTest::keyClick(number, Qt::Key_Escape);
        QCOMPARE(dialog.settings().firstFrame, 23); QVERIFY(dialog.isVisible()); QVERIFY(!QFileInfo::exists(dialog.destination())); dialog.reject();
    }
    void existingNormalizedTargetPromptsOnceAndCancelPreservesIt() {
        QTemporaryDir dir; const auto path = dir.filePath("existing.wav"); QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("original"); file.close();
        AudioExportDialog dialog(fixture(), 0, 0, 0, false); dialog.setDestination(dir.filePath("existing")); dialog.show();
        int prompts = 0; QTimer answer; answer.setInterval(5);
        connect(&answer, &QTimer::timeout, this, [&] {
            if (auto *box = dialog.findChild<QMessageBox *>("audioExportOverwrite"); box && box->isVisible()) {
                ++prompts; box->done(QMessageBox::Cancel); answer.stop();
            }
        });
        QTimer::singleShot(3000, &dialog, [&] { for (auto *box : dialog.findChildren<QMessageBox *>()) box->done(QMessageBox::Cancel); });
        answer.start(); dialog.findChild<QPushButton *>("audioExportStart")->click(); answer.stop();
        QCOMPARE(prompts, 1); QCOMPARE(fileBytes(path), QByteArray("original")); QVERIFY(!dialog.findChild<AudioExport *>()->busy()); dialog.reject();
    }
    void browserDefersOverwriteQuestionUntilExport() {
        QTemporaryDir dir; const auto path = dir.filePath("chosen.wav"); QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("keep"); file.close();
        AudioExportDialog dialog(fixture(), 0, 0, 0, false); dialog.show(); bool chose = false, deferred = false;
        QTimer choose; choose.setInterval(5);
        connect(&choose, &QTimer::timeout, this, [&] {
            if (auto *picker = dialog.findChild<QFileDialog *>(); picker && picker->isVisible()) {
                deferred = picker->testOption(QFileDialog::DontConfirmOverwrite) && picker->testOption(QFileDialog::DontUseNativeDialog);
                picker->selectFile(path); chose = QMetaObject::invokeMethod(picker, "accept", Qt::DirectConnection); choose.stop();
            }
        });
        QTimer::singleShot(3000, &dialog, [&] { for (auto *picker : dialog.findChildren<QFileDialog *>()) picker->reject(); });
        choose.start(); dialog.findChild<QToolButton *>("audioExportBrowse")->click(); choose.stop();
        QVERIFY(chose); QVERIFY(deferred); QCOMPARE(dialog.destination(), path); QCOMPARE(fileBytes(path), QByteArray("keep")); dialog.reject();
    }
    void cancelAndWindowCloseWaitForWorker_data() {
        QTest::addColumn<bool>("windowClose"); QTest::newRow("cancel") << false; QTest::newRow("window-close") << true;
    }
    void cancelAndWindowCloseWaitForWorker() {
        QFETCH(bool, windowClose); QTemporaryDir dir; auto project = fixture(); project.shots[0].durationFrames = 100000;
        AudioExportDialog dialog(project, 0, 0, 0, false); dialog.setDestination(dir.filePath("cancel.wav")); dialog.show();
        auto *exporter = dialog.findChild<AudioExport *>(); QSignalSpy finished(exporter, &AudioExport::finished);
        dialog.findChild<QPushButton *>("audioExportStart")->click(); QVERIFY(exporter->busy());
        QVERIFY(!dialog.findChild<QWidget *>("audioExportControls")->isEnabled());
        if (windowClose) dialog.close(); else dialog.findChild<QPushButton *>("audioExportCancel")->click();
        QVERIFY(dialog.isVisible()); QVERIFY(!dialog.findChild<QPushButton *>("audioExportCancel")->isEnabled());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000); QTRY_VERIFY(!dialog.isVisible());
        QCOMPARE(qvariant_cast<AudioExportResult>(finished[0][0]).outcome, AudioExportOutcome::Cancelled);
        QVERIFY(!QFileInfo::exists(dialog.destination()));
    }
    void failureRetainsSettingsAndSuccessfulRetrySignalsPath() {
        QTemporaryDir dir; auto project = fixture(); project.shots[0].durationFrames = 2;
        AudioExportDialog dialog(project, 0, 0, 0, false); dialog.show(); dialog.setDestination(dir.filePath("missing/file.wav"));
        auto *exporter = dialog.findChild<AudioExport *>(); QSignalSpy finished(exporter, &AudioExport::finished), exported(&dialog, &AudioExportDialog::exported);
        dialog.findChild<QPushButton *>("audioExportStart")->click(); QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QCOMPARE(qvariant_cast<AudioExportResult>(finished[0][0]).outcome, AudioExportOutcome::Failed);
        QVERIFY(dialog.isVisible()); QVERIFY(dialog.findChild<QWidget *>("audioExportControls")->isEnabled());
        QCOMPARE(dialog.settings().lastFrame, 1); dialog.setDestination(dir.filePath("success.wav"));
        dialog.findChild<QPushButton *>("audioExportStart")->click(); QTRY_COMPARE_WITH_TIMEOUT(exported.count(), 1, 10000);
        QCOMPARE(exported[0][0].toString(), dir.filePath("success.wav")); QVERIFY(!dialog.isVisible());
        QCOMPARE(fileBytes(dialog.destination()).first(4), QByteArray("RIFF"));
    }
    void compactFieldsAndStableLayoutPreview() {
        QTemporaryDir dir; AudioExportDialog dialog(fixture(), 1, 36, 83, true); dialog.setDestination(dir.filePath("movie-audio.wav"));
        dialog.findChild<QToolButton *>("audioExportUsePlayback")->click(); dialog.show();
        const auto output = qEnvironmentVariable("TOON_UI_PREVIEW", "test-artifacts"); QVERIFY(QDir().mkpath(output));
        for (int width : {480, 360}) {
            dialog.resize(width, 400); QTest::qWait(20); QCOMPARE(dialog.width(), width);
            auto *status = dialog.findChild<QLabel *>("audioExportStatus"); const int height = status->height();
            const auto progressPos = dialog.findChild<QProgressBar *>("audioExportProgress")->pos();
            dialog.findChild<ValueSlider *>("audioExportFirst")->setValue(200); QCOMPARE(status->height(), height);
            QCOMPARE(dialog.findChild<QProgressBar *>("audioExportProgress")->pos(), progressPos);
            dialog.findChild<ValueSlider *>("audioExportFirst")->setValue(37);
            for (auto *number : dialog.findChildren<QDoubleSpinBox *>()) { QCOMPARE(number->width(), 48); QVERIFY(number->suffix().isEmpty()); }
            for (auto *button : dialog.findChildren<QToolButton *>()) { QVERIFY(!button->accessibleName().isEmpty()); QVERIFY(!button->toolTip().isEmpty()); }
            QVERIFY(dialog.grab().save(QDir(output).filePath(QString("audio-export-dialog-%1.png").arg(width))));
        }
        dialog.reject();
    }
};
QTEST_MAIN(AudioExportDialogTest)
#include "audio_export_dialog_tests.moc"
