#include "drawing_reuse_dialog.h"
#include "animation_test_fixture.h"
#include "canvas.h"
#include "cel_edit.h"
#include "document.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include "value_slider.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyle>
#include <QToolButton>
#include <QtTest>

using namespace toon;
namespace {
QListWidget *list(DrawingReuseDialog &dialog) { return dialog.findChild<QListWidget *>("drawingReuseList"); }
QPushButton *reuse(DrawingReuseDialog &dialog) { return dialog.findChild<QPushButton *>("drawingReuseConfirm"); }
ValueSlider *hold(DrawingReuseDialog &dialog) { return dialog.findChild<ValueSlider *>("drawingReuseHold"); }
bool containsWidget(QWidget *parent, QWidget *child) {
    return child && parent->rect().contains(QRect(child->mapTo(parent, QPoint()), child->size()));
}
QColor previewCenter(const QListWidgetItem *item) {
    const auto image = item->icon().pixmap(QSize(72, 48)).toImage();
    return image.isNull() ? QColor() : image.pixelColor(image.width() / 2, image.height() / 2);
}
}

class DrawingReuseDialogTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void reuseSharesArtworkAndCreatesOneUndo() {
        const auto original = animation_fixture::project(); Canvas canvas; canvas.setProject(original);
        TimelineController controller(&canvas); const auto trackId = original.shots[0].animationTracks[0].id;
        QVERIFY(controller.selectTrack(trackId)); QVERIFY(controller.setFrame(10));
        const auto before = canvas.project(); const int history = canvas.document()->history()->count();
        DrawingReuseDialog dialog(&controller); dialog.show();
        auto *drawings = list(dialog); QCOMPARE(drawings->count(), 2); drawings->setCurrentRow(0);
        const auto drawingId = drawings->item(0)->data(Qt::UserRole).toString();
        hold(dialog)->setValue(2); reuse(dialog)->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        const auto &track = canvas.shot().animationTracks[0]; QCOMPARE(track.drawings.size(), 2);
        QCOMPARE(track.exposures.size(), 3); QCOMPARE(exposureAtFrame(track, 10)->drawingId, drawingId);
        QCOMPARE(exposureAtFrame(track, 10)->durationFrames, 2);
        QCOMPARE(canvas.shot().layers.size(), before.shots[0].layers.size());
        for (int i = 0; i < canvas.shot().layers.size(); ++i) QCOMPARE(canvas.shot().layers[i].tiles, before.shots[0].layers[i].tiles);
        QCOMPARE(canvas.document()->history()->count(), history + 1);
        canvas.document()->history()->undo(); QCOMPARE(canvas.project().toJson(), before.toJson());
    }
    void unusedDrawingPreviewsFiltersAndCancelsWithoutChangingSource() {
        auto source = animation_fixture::project(); source.shots[0].animationTracks[0].exposures.removeFirst();
        Canvas canvas; canvas.setProject(source); TimelineController controller(&canvas);
        QVERIFY(controller.selectTrack(source.shots[0].animationTracks[0].id)); QVERIFY(controller.setFrame(10));
        const auto before = canvas.project().toJson(); const auto pixels = canvas.shot().layers[3].tiles;
        DrawingReuseDialog dialog(&controller); dialog.show(); auto *drawings = list(dialog);
        QVERIFY(drawings->item(0)->text().contains("D001")); QVERIFY(drawings->item(0)->text().contains("Unused"));
        QTRY_VERIFY_WITH_TIMEOUT(previewCenter(drawings->item(0)).red() > 230 && previewCenter(drawings->item(0)).blue() < 20, 10000);
        auto *search = dialog.findChild<QLineEdit *>("drawingReuseSearch"); search->setText("d002");
        QVERIFY(drawings->item(0)->isHidden()); QVERIFY(!drawings->item(1)->isHidden());
        QCOMPARE(drawings->currentItem()->data(Qt::UserRole).toString(), source.shots[0].animationTracks[0].drawings[1].id);
        search->setText("no such drawing"); QVERIFY(!reuse(dialog)->isEnabled());
        search->setText("unused"); QVERIFY(!drawings->item(0)->isHidden()); QVERIFY(reuse(dialog)->isEnabled());
        dialog.reject(); QCOMPARE(canvas.project().toJson(), before); QCOMPARE(canvas.shot().layers[3].tiles, pixels);
        QCOMPARE(canvas.document()->history()->count(), 0);
    }
    void collisionKeepsDialogOpenAndShowsReason() {
        const auto source = animation_fixture::project(); Canvas canvas; canvas.setProject(source);
        TimelineController controller(&canvas); QVERIFY(controller.selectTrack(source.shots[0].animationTracks[0].id));
        const auto before = canvas.project().toJson(); DrawingReuseDialog dialog(&controller); dialog.show();
        list(dialog)->setCurrentRow(1); QVERIFY(reuse(dialog)->isEnabled()); reuse(dialog)->click();
        QVERIFY(dialog.isVisible()); QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QVERIFY(!dialog.findChild<QLabel *>("drawingReuseStatus")->text().isEmpty());
        QCOMPARE(canvas.project().toJson(), before); QCOMPARE(canvas.document()->history()->count(), 0);
    }
    void deleteUnusedDrawingCanBeUndoneAndUsedDrawingsAreProtected() {
        auto source = animation_fixture::project(); source.shots[0].animationTracks[0].exposures.removeFirst();
        Canvas canvas; canvas.setProject(source); TimelineController controller(&canvas);
        QVERIFY(controller.selectTrack(source.shots[0].animationTracks[0].id)); QVERIFY(controller.setFrame(10));
        DrawingReuseDialog dialog(&controller); dialog.show(); auto *drawings = list(dialog);
        auto *remove = dialog.findChild<QToolButton *>("drawingReuseRemoveUnused");
        drawings->setCurrentRow(1); QVERIFY(!remove->isEnabled());
        drawings->setCurrentRow(0); QVERIFY(remove->isEnabled()); remove->click();
        QCOMPARE(drawings->count(), 1); QCOMPARE(canvas.shot().animationTracks[0].drawings.size(), 1);
        canvas.document()->history()->undo(); QCOMPARE(drawings->count(), 2);
        QCOMPARE(canvas.project().toJson(), source.toJson());
    }
    void contextChangeClosesPickerWithoutExposingAnything() {
        const auto source = animation_fixture::project(); Canvas canvas; canvas.setProject(source);
        TimelineController controller(&canvas); QVERIFY(controller.selectTrack(source.shots[0].animationTracks[0].id));
        DrawingReuseDialog dialog(&controller); dialog.show(); QSignalSpy rejected(&dialog, &QDialog::rejected);
        QVERIFY(controller.addTrack(true, DrawingTemplate::Simple, "Effects"));
        QVERIFY(!dialog.isVisible()); QVERIFY(!rejected.isEmpty());
        QCOMPARE(canvas.shot().animationTracks[0].exposures, source.shots[0].animationTracks[0].exposures);
    }
    void narrowLargerTextUsesSharedAccessibleControls() {
        auto source = animation_fixture::project(); source.shots[0].layers[1].name = "Character with a long production name";
        Canvas canvas; canvas.setProject(source); TimelineController controller(&canvas);
        QVERIFY(controller.selectTrack(source.shots[0].animationTracks[0].id)); QVERIFY(controller.setFrame(10));
        DrawingReuseDialog dialog(&controller); auto font = dialog.font(); font.setPointSizeF(font.pointSizeF() + 3); dialog.setFont(font);
        dialog.resize(280, 480); dialog.show(); dialog.findChild<QLineEdit *>("drawingReuseSearch")->setText("Drawing"); QCoreApplication::processEvents();
        for (auto *control : dialog.findChildren<QToolButton *>()) if (control->isVisibleTo(&dialog)) {
            QVERIFY(!control->accessibleName().isEmpty()); QVERIFY(!control->toolTip().isEmpty());
            QCOMPARE(control->iconSize(), QSize(16, 16));
            QCOMPARE(control->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay, nullptr, control), 600);
            QVERIFY(containsWidget(&dialog, control));
        }
        auto *number = hold(dialog)->findChild<QDoubleSpinBox *>(); QVERIFY(number); QCOMPARE(number->width(), 48); QVERIFY(number->suffix().isEmpty());
        QVERIFY(containsWidget(&dialog, list(dialog))); QVERIFY(containsWidget(&dialog, hold(dialog))); QVERIFY(containsWidget(&dialog, reuse(dialog)));
        QVERIFY(!list(dialog)->accessibleName().isEmpty());
    }
    void renderPreview() {
        const auto output = qEnvironmentVariable("TOON_UI_PREVIEW"); if (output.isEmpty()) QSKIP("Opt-in reuse drawing previews.");
        auto source = animation_fixture::project(); source.shots[0].animationTracks[0].exposures.removeFirst();
        Canvas canvas; canvas.setProject(source); TimelineController controller(&canvas);
        QVERIFY(controller.selectTrack(source.shots[0].animationTracks[0].id)); QVERIFY(controller.setFrame(10));
        DrawingReuseDialog dialog(&controller); dialog.show(); auto *drawings = list(dialog);
        QTRY_VERIFY_WITH_TIMEOUT(previewCenter(drawings->item(0)).red() > 230 && previewCenter(drawings->item(0)).blue() < 20, 10000);
        QDir().mkpath(output); QVERIFY(dialog.grab().save(output + "/drawing-reuse.png"));
        auto font = dialog.font(); font.setPointSizeF(font.pointSizeF() + 3); dialog.setFont(font); dialog.resize(280, 480);
        QCoreApplication::processEvents(); QVERIFY(dialog.grab().save(output + "/drawing-reuse-narrow.png"));
    }
};
QTEST_MAIN(DrawingReuseDialogTests)
#include "test_drawing_reuse_dialog.moc"
