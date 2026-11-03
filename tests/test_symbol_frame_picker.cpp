#include "symbol_frame_picker_dialog.h"
#include "animation_test_fixture.h"
#include "canvas.h"
#include "document.h"
#include "effects_preview_renderer.h"
#include "symbols.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include "value_slider.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTest>

using namespace toon;
namespace {
Project fixture() {
    auto project = Project::create(); project.width = 128; project.height = 128;
    auto content = animation_fixture::project().shots[0]; content.durationFrames = 30;
    content.animationTracks[0].exposures[0].durationFrames = 12;
    content.animationTracks[0].exposures[1].startFrame = 12;
    content.animationTracks[0].exposures[1].durationFrames = 18;
    SymbolDefinition symbol{animation_fixture::id(), "Character poses", content};
    project.symbols = {symbol};
    auto instance = RasterLayer::create("Character A"); instance.kind = LayerKind::Symbol;
    instance.symbol = SymbolInstance{symbol.id, SymbolPlayback::PingPong, 5, 2, 1.5, 18, 3};
    project.shots[0].layers = {instance};
    project.shots[0].layerAnimations = {{instance.id, {}, {{KeyframeProperty::PositionX, {{0, 21}}},
        {KeyframeProperty::Rotation, {{0, 15}}}}, {}}};
    return project;
}
Project nestedFixture() {
    auto project = fixture();
    auto content = Project::create().shots[0]; content.durationFrames = 1;
    auto nested = RasterLayer::create("Nested pose"); nested.kind = LayerKind::Symbol;
    nested.symbol = SymbolInstance{project.symbols[0].id, SymbolPlayback::SingleFrame, 0, 12};
    content.layers = {nested};
    SymbolDefinition wrapper{animation_fixture::id(), "Nested blue pose", content};
    project.symbols.append(wrapper); project.shots[0].layers[0].symbol->symbolId = wrapper.id;
    project.shots[0].layers[0].symbol->firstFrame = 0;
    return project;
}
QListWidget *frames(SymbolFramePickerDialog &dialog) { return dialog.findChild<QListWidget *>("symbolFramePickerFrames"); }
ValueSlider *frame(SymbolFramePickerDialog &dialog) { return dialog.findChild<ValueSlider *>("symbolFramePickerFrame"); }
QPushButton *apply(SymbolFramePickerDialog &dialog) { return dialog.findChild<QPushButton *>("symbolFramePickerApply"); }
QCheckBox *hold(SymbolFramePickerDialog &dialog) { return dialog.findChild<QCheckBox *>("symbolFramePickerHold"); }
QColor previewCenter(const QListWidgetItem *item) {
    const auto image = item->icon().pixmap(QSize(96, 64)).toImage();
    return image.isNull() ? QColor() : image.pixelColor(image.width() / 2, image.height() / 2);
}
bool contains(QWidget *parent, QWidget *child) {
    return child && parent->rect().contains(QRect(child->mapTo(parent, QPoint()), child->size()));
}
const RasterLayer *instance(const Shot &shot, const QString &id) {
    for (const auto &layer : shot.layers) if (layer.id == id) return &layer;
    return nullptr;
}
} // namespace

class SymbolFramePickerTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void previewPagesDoNotMutateAndApplyIsOneUndo() {
        const auto source = fixture(); QVERIFY2(source.validate().isEmpty(), qPrintable(source.validate()));
        Canvas canvas; canvas.setProject(source); TimelineController controller(&canvas);
        const auto id = source.shots[0].layers[0].id;
        const auto original = *source.shots[0].layers[0].symbol;
        const auto before = canvas.project().toJson();
        SymbolFramePickerDialog dialog(&controller, id); dialog.show();
        QCOMPARE(frame(dialog)->value(), 3.); QCOMPARE(frames(dialog)->count(), 12);
        QVERIFY(!hold(dialog)->isChecked());
        frames(dialog)->setCurrentRow(0);
        QTRY_VERIFY_WITH_TIMEOUT(previewCenter(frames(dialog)->item(0)).red() > 230
            && previewCenter(frames(dialog)->item(0)).blue() < 20, 10000);
        dialog.findChild<QToolButton *>("symbolFramePickerNextPage")->click();
        QCOMPARE(frame(dialog)->value(), 13.); QCOMPARE(frames(dialog)->item(0)->data(Qt::UserRole).toInt(), 12);
        QTRY_VERIFY_WITH_TIMEOUT(previewCenter(frames(dialog)->item(0)).blue() > 230
            && previewCenter(frames(dialog)->item(0)).red() < 20, 10000);
        QCOMPARE(canvas.project().toJson(), before); QCOMPARE(canvas.document()->history()->count(), 0);
        QCOMPARE(controller.currentFrame(), 0);
        apply(dialog)->click(); QCOMPARE(dialog.result(), int(QDialog::Accepted));
        const auto chosen = *canvas.shot().layers[0].symbol;
        QCOMPARE(chosen.firstFrame, 12); QCOMPARE(chosen.playbackOffset, 0);
        QCOMPARE(chosen.startFrame, original.startFrame); QCOMPARE(chosen.durationFrames, original.durationFrames);
        QCOMPARE(chosen.speed, original.speed); QCOMPARE(chosen.playback, original.playback);
        QCOMPARE(canvas.shot().layerAnimations, source.shots[0].layerAnimations);
        QCOMPARE(canvas.project().toJson().value("symbols"), source.toJson().value("symbols"));
        QCOMPARE(canvas.project().symbols[0].content.layers[3].tiles, source.symbols[0].content.layers[3].tiles);
        QCOMPARE(canvas.document()->history()->count(), 1);
        canvas.document()->history()->undo(); QCOMPARE(canvas.project().toJson(), before);
        canvas.document()->history()->redo(); QCOMPARE(canvas.shot().layers[0].symbol->firstFrame, 12);
    }
    void holdAppliesWholeClipAndTurningItOffRestoresLoop() {
        auto source = fixture(); Canvas canvas; canvas.setProject(source); TimelineController controller(&canvas);
        const auto id = source.shots[0].layers[0].id;
        SymbolFramePickerDialog dialog(&controller, id); dialog.show();
        frame(dialog)->setValue(9); hold(dialog)->setChecked(true); apply(dialog)->click();
        const auto chosen = *canvas.shot().layers[0].symbol;
        QCOMPARE(chosen.playback, SymbolPlayback::SingleFrame); QCOMPARE(chosen.firstFrame, 8);
        QCOMPARE(symbolLocalFrame(chosen, 5, 30), 8); QCOMPARE(symbolLocalFrame(chosen, 22, 30), 8);
        SymbolFramePickerDialog resume(&controller, id); resume.show(); QVERIFY(hold(resume)->isChecked());
        hold(resume)->setChecked(false); apply(resume)->click();
        QCOMPARE(canvas.shot().layers[0].symbol->playback, SymbolPlayback::Loop);
        QCOMPARE(canvas.document()->history()->count(), 2);
    }
    void cancelAndNoChangeApplyLeaveHistoryAlone() {
        auto source = fixture(); source.shots[0].layers[0].symbol->playbackOffset = 0;
        Canvas canvas; canvas.setProject(source); TimelineController controller(&canvas);
        const auto before = canvas.project().toJson(); const auto id = source.shots[0].layers[0].id;
        SymbolFramePickerDialog dialog(&controller, id); dialog.show(); frame(dialog)->setValue(24);
        hold(dialog)->setChecked(true); dialog.reject();
        QCOMPARE(canvas.project().toJson(), before); QCOMPARE(canvas.document()->history()->count(), 0);
        SymbolFramePickerDialog unchanged(&controller, id); unchanged.show(); apply(unchanged)->click();
        QCOMPARE(unchanged.result(), int(QDialog::Accepted)); QCOMPARE(canvas.project().toJson(), before);
        QCOMPARE(canvas.document()->history()->count(), 0);
    }
    void nestedThumbnailsUseExactSourceFrame() {
        const auto source = nestedFixture(); QVERIFY2(source.validate().isEmpty(), qPrintable(source.validate()));
        Canvas canvas; canvas.setProject(source); TimelineController controller(&canvas);
        SymbolFramePickerDialog dialog(&controller, source.shots[0].layers[0].id); dialog.show();
        QCOMPARE(frames(dialog)->count(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(previewCenter(frames(dialog)->item(0)).blue() > 230
            && previewCenter(frames(dialog)->item(0)).red() < 20, 10000);
        QCOMPARE(canvas.project().symbols[0].content.layers[5].tiles, source.symbols[0].content.layers[5].tiles);
        QCOMPARE(canvas.document()->history()->count(), 0);
    }
    void offOriginArtworkIsFittedWithoutChangingSourcePlacement() {
        auto source = fixture(); auto &content = source.symbols[0].content;
        content.animationTracks.clear(); content.layers = {RasterLayer::create("Off-origin prop")};
        QImage pixels(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); pixels.fill(Qt::green);
        content.layers[0].tiles.insert({5, -4}, pixels);
        const auto id = source.shots[0].layers[0].id;
        source.shots[0].layerAnimations = {{id, {}, {{KeyframeProperty::PositionX, {{0, -1280}}},
            {KeyframeProperty::PositionY, {{0, 1024}}}}, {}}};
        Canvas canvas; canvas.setProject(source); TimelineController controller(&canvas);
        SymbolFramePickerDialog dialog(&controller, id); dialog.show();
        QTRY_VERIFY_WITH_TIMEOUT(previewCenter(frames(dialog)->currentItem()).green() > 230
            && previewCenter(frames(dialog)->currentItem()).red() < 20, 10000);
        QCOMPARE(canvas.project().toJson(), source.toJson());
        QCOMPARE(canvas.project().symbols[0].content.layers[0].tiles, content.layers[0].tiles);
        QCOMPARE(canvas.document()->history()->count(), 0);
    }
    void transparentArtworkKeepsCheckerboardWithoutProductionHeader() {
        auto source = fixture(); auto &content = source.symbols[0].content;
        content.animationTracks.clear(); content.layers = {RasterLayer::create("Transparent pose")};
        QImage pixels(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); pixels.fill(Qt::transparent);
        { QPainter painter(&pixels); painter.fillRect(QRect(96, 96, 64, 64), Qt::red); }
        content.layers[0].tiles.insert({0, 0}, pixels);
        Canvas canvas; canvas.setProject(source); TimelineController controller(&canvas);
        SymbolFramePickerDialog dialog(&controller, source.shots[0].layers[0].id); dialog.show();
        QTRY_VERIFY_WITH_TIMEOUT(frames(dialog)->currentItem()->data(Qt::UserRole + 1).toBool(), 10000);
        const auto image = frames(dialog)->currentItem()->icon().pixmap(QSize(192, 128)).toImage();
        QVERIFY(!image.isNull());
        const auto sample = [&](int x, int y) { return image.pixelColor(x * image.width() / 192, y * image.height() / 128); };
        QCOMPARE(sample(3, 3), QColor(ui::base)); QCOMPARE(sample(15, 3), QColor(ui::raised));
        QCOMPARE(sample(3, 15), QColor(ui::raised)); QCOMPARE(sample(15, 15), QColor(ui::base));
        QCOMPARE(sample(96, 64), QColor(Qt::red));
        QCOMPARE(canvas.project().toJson(), source.toJson()); QCOMPARE(canvas.document()->history()->count(), 0);
    }
    void hugeDefinitionHasBoundedPagesAndRejectsOldCompletions() {
        auto source = fixture(); source.symbols[0].content.durationFrames = 1000000;
        Canvas canvas; canvas.setProject(source); TimelineController controller(&canvas);
        SymbolFramePickerDialog dialog(&controller, source.shots[0].layers[0].id); dialog.show();
        frame(dialog)->setValue(1000000);
        QCOMPARE(frames(dialog)->count(), 4); QCOMPARE(frames(dialog)->currentItem()->data(Qt::UserRole).toInt(), 999999);
        QVERIFY(!dialog.findChild<QToolButton *>("symbolFramePickerNextPage")->isEnabled());
        QVERIFY(dialog.findChild<QLabel *>("symbolFramePickerPage")->text().contains("1000000"));
        frame(dialog)->setValue(13); frame(dialog)->setValue(1);
        QCOMPARE(frames(dialog)->count(), 12); QCOMPARE(frames(dialog)->currentItem()->data(Qt::UserRole).toInt(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(previewCenter(frames(dialog)->item(0)).red() > 230
            && previewCenter(frames(dialog)->item(0)).blue() < 20, 10000);
        auto *renderer = dialog.findChild<EffectsPreviewRenderer *>(); QVERIFY(renderer);
        QVERIFY(renderer->latestRevision() <= 15); // At most one bounded page plus the rapid navigation requests.
        QCOMPARE(canvas.document()->history()->count(), 0);
    }
    void inactiveDrawingInstanceCanBePickedWithoutExposingIt() {
        auto source = animation_fixture::project();
        const auto symbolSource = fixture(); source.symbols = symbolSource.symbols;
        const auto id = source.shots[0].layers[3].id;
        auto &layer = source.shots[0].layers[3]; layer.tiles.clear(); layer.kind = LayerKind::Symbol;
        layer.symbol = SymbolInstance{source.symbols[0].id};
        Canvas canvas; canvas.setProject(source); TimelineController controller(&canvas);
        QVERIFY(controller.setFrame(10));
        const auto exposures = canvas.shot().animationTracks[0].exposures;
        SymbolFramePickerDialog dialog(&controller, id); dialog.show(); QVERIFY(apply(dialog)->isEnabled());
        frame(dialog)->setValue(14); apply(dialog)->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(instance(canvas.shot(), id)->symbol->firstFrame, 13);
        QCOMPARE(canvas.shot().animationTracks[0].exposures, exposures); QCOMPARE(controller.currentFrame(), 10);
        QCOMPARE(canvas.document()->history()->count(), 1);
    }
    void instanceChangesAndUndoCannotBeOverwrittenByStaleApply() {
        const auto source = fixture(); Canvas canvas; canvas.setProject(source); TimelineController controller(&canvas);
        const auto id = source.shots[0].layers[0].id;
        SymbolFramePickerDialog dialog(&controller, id); dialog.show(); frame(dialog)->setValue(20);
        auto changed = *canvas.shot().layers[0].symbol; changed.firstFrame = 4; QString error;
        QVERIFY2(setSymbolInstance(canvas.document(), id, changed, &error), qPrintable(error));
        QVERIFY(!apply(dialog)->isEnabled()); QVERIFY(dialog.findChild<QLabel *>("symbolFramePickerStatus")->text().contains("changed"));
        apply(dialog)->click(); QCOMPARE(canvas.shot().layers[0].symbol->firstFrame, 4);
        QCOMPARE(canvas.document()->history()->count(), 1);
        canvas.document()->history()->undo(); QVERIFY(!apply(dialog)->isEnabled());
        apply(dialog)->click(); QCOMPARE(canvas.project().toJson(), source.toJson());
    }
    void nestedSourceEditInvalidatesPicker() {
        const auto source = nestedFixture(); Canvas canvas; canvas.setProject(source); TimelineController controller(&canvas);
        SymbolFramePickerDialog dialog(&controller, source.shots[0].layers[0].id); dialog.show();
        const auto original = canvas.project().symbols[0].content; auto content = original;
        content.layers[5].tiles[{0, 0}].fill(Qt::green); QString error;
        QVERIFY2(updateSymbolContent(canvas.document(), source.symbols[0].id, content, original, &error), qPrintable(error));
        QVERIFY(!apply(dialog)->isEnabled()); const auto before = canvas.project().toJson();
        apply(dialog)->click(); QCOMPARE(canvas.project().toJson(), before); QCOMPARE(canvas.document()->history()->count(), 1);
    }
    void documentReloadShotChangeAndDeletedInstanceClosePicker() {
        auto source = fixture(); auto other = Project::create().shots[0]; source.shots.append(other);
        Canvas canvas; canvas.setProject(source); TimelineController controller(&canvas); const auto id = source.shots[0].layers[0].id;
        SymbolFramePickerDialog reload(&controller, id); reload.show(); canvas.setProject(source);
        QVERIFY(!reload.isVisible()); QVERIFY(!apply(reload)->isEnabled());
        SymbolFramePickerDialog change(&controller, id); change.show(); QVERIFY(canvas.document()->selectShot(other.id));
        QVERIFY(!change.isVisible()); QVERIFY(canvas.document()->selectShot(source.shots[0].id));
        SymbolFramePickerDialog deleted(&controller, id); deleted.show();
        auto replacement = canvas.project(); replacement.shots[0].layers = {RasterLayer::create()}; replacement.shots[0].layerAnimations.clear();
        QVERIFY(canvas.document()->replaceSymbolProject(replacement, "Remove instance"));
        QVERIFY(!deleted.isVisible()); QVERIFY(!apply(deleted)->isEnabled());
    }
    void fullPositionAndAncestorLocksBlockApply() {
        for (int lock = 0; lock < 4; ++lock) {
            auto source = fixture(); const auto id = source.shots[0].layers[0].id;
            if (lock < 2) {
                source.shots[0].layers[0].locked = lock == 0;
                source.shots[0].layers[0].positionLocked = lock == 1;
            } else {
                auto folder = RasterLayer::create("Locked folder"); folder.group = true;
                folder.locked = lock == 2; folder.positionLocked = lock == 3;
                source.shots[0].layers[0].parentId = folder.id; source.shots[0].layers.prepend(folder);
            }
            Canvas canvas; canvas.setProject(source); TimelineController controller(&canvas);
            SymbolFramePickerDialog dialog(&controller, id); dialog.show();
            QVERIFY(!apply(dialog)->isEnabled()); QVERIFY(!hold(dialog)->isEnabled());
            QVERIFY(!dialog.findChild<QLabel *>("symbolFramePickerStatus")->text().isEmpty());
            frame(dialog)->setValue(20); apply(dialog)->click();
            QCOMPARE(canvas.document()->history()->count(), 0); QCOMPARE(canvas.project().toJson(), source.toJson());
        }
    }
    void preciseEntryCommitsLocallyAndEscapeCancelsItsDraft() {
        const auto source = fixture(); Canvas canvas; canvas.setProject(source); TimelineController controller(&canvas);
        SymbolFramePickerDialog dialog(&controller, source.shots[0].layers[0].id); dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog)); dialog.raise(); dialog.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        auto *number = frame(dialog)->findChild<QDoubleSpinBox *>(); auto *text = number->findChild<QLineEdit *>();
        number->setFocus(); number->selectAll(); QTest::keyClicks(text, "17"); QTest::keyClick(text, Qt::Key_Return);
        QCOMPARE(frame(dialog)->value(), 17.); QCOMPARE(frames(dialog)->currentItem()->data(Qt::UserRole).toInt(), 16);
        QVERIFY(dialog.isVisible()); QCOMPARE(canvas.document()->history()->count(), 0);
        number->selectAll(); QTest::keyClicks(text, "23"); QTest::keyClick(text, Qt::Key_Escape);
        QCOMPARE(frame(dialog)->value(), 17.); QVERIFY(dialog.isVisible());
        number->selectAll(); QTest::keyClicks(text, "25"); apply(dialog)->click();
        QCOMPARE(canvas.shot().layers[0].symbol->firstFrame, 24); QCOMPARE(canvas.document()->history()->count(), 1);
    }
    void helperReturnsCanvasFocusAfterCancelAndApply() {
        QWidget workspace; auto *layout = new QVBoxLayout(&workspace); auto *canvas = new Canvas;
        const auto source = fixture(); canvas->setProject(source); layout->addWidget(canvas);
        TimelineController controller(canvas); workspace.resize(640, 480); workspace.show(); workspace.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&workspace)); const auto id = source.shots[0].layers[0].id;
        for (const bool accept : {false, true}) {
            bool visited = false; QTimer poll; poll.setInterval(10);
            connect(&poll, &QTimer::timeout, &workspace, [&] {
                auto *dialog = qobject_cast<SymbolFramePickerDialog *>(QApplication::activeModalWidget());
                if (!dialog) return;
                poll.stop(); visited = true;
                if (accept) { frame(*dialog)->setValue(8); apply(*dialog)->click(); }
                else dialog->reject();
            });
            QTimer deadline; deadline.setSingleShot(true);
            connect(&deadline, &QTimer::timeout, &workspace, [] { if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject(); });
            poll.start(); deadline.start(10000); openSymbolFramePicker(&controller, id, &workspace);
            poll.stop(); deadline.stop(); QVERIFY(visited); QTRY_VERIFY(canvas->hasFocus());
        }
        QCOMPARE(canvas->document()->history()->count(), 1);
    }
    void helperDoesNotTakeFocusFromAnotherWindow() {
        QWidget other, workspace; auto *otherLayout = new QVBoxLayout(&other);
        auto *otherEditor = new QLineEdit; otherLayout->addWidget(otherEditor); other.show();
        auto *layout = new QVBoxLayout(&workspace); auto *canvas = new Canvas;
        const auto source = fixture(); canvas->setProject(source); layout->addWidget(canvas);
        TimelineController controller(canvas); workspace.resize(640, 480); workspace.show(); workspace.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&workspace)); bool visited = false;
        QTimer poll; poll.setInterval(10); QTimer deadline; deadline.setSingleShot(true);
        connect(&poll, &QTimer::timeout, &workspace, [&] {
            auto *dialog = qobject_cast<SymbolFramePickerDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            poll.stop(); visited = true;
            QApplication::setActiveWindow(&other); otherEditor->setFocus(); dialog->reject();
        });
        connect(&deadline, &QTimer::timeout, &workspace, [] { if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject(); });
        poll.start(); deadline.start(10000); openSymbolFramePicker(&controller, source.shots[0].layers[0].id, &workspace);
        poll.stop(); deadline.stop(); QVERIFY(visited);
        QCOMPARE(QApplication::activeWindow(), &other); QVERIFY(otherEditor->hasFocus()); QVERIFY(!canvas->hasFocus());
        QCOMPARE(canvas->document()->history()->count(), 0);
    }
    void narrowLargerTextKeepsControlsAccessible() {
        auto source = fixture(); source.symbols[0].name = "Character with a long production name";
        Canvas canvas; canvas.setProject(source); TimelineController controller(&canvas);
        SymbolFramePickerDialog dialog(&controller, source.shots[0].layers[0].id);
        auto font = dialog.font(); font.setPointSizeF(font.pointSizeF() + 3); dialog.setFont(font);
        dialog.resize(280, 480); dialog.show(); QCoreApplication::processEvents();
        for (auto *control : dialog.findChildren<QToolButton *>()) if (control->isVisibleTo(&dialog)) {
            QVERIFY(!control->accessibleName().isEmpty()); QVERIFY(!control->toolTip().isEmpty());
            QCOMPARE(control->iconSize(), QSize(16, 16)); QVERIFY(contains(&dialog, control));
            QCOMPARE(control->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay, nullptr, control), 600);
        }
        auto *number = frame(dialog)->findChild<QDoubleSpinBox *>(); QCOMPARE(number->width(), 48); QVERIFY(number->suffix().isEmpty());
        QVERIFY(contains(&dialog, frames(dialog))); QVERIFY(contains(&dialog, frame(dialog)));
        QVERIFY(contains(&dialog, hold(dialog))); QVERIFY(contains(&dialog, apply(dialog)));
        QVERIFY(!frames(dialog)->accessibleName().isEmpty());
        for (int i = 0; i < frames(dialog)->count(); ++i) QVERIFY(!frames(dialog)->item(i)->data(Qt::AccessibleTextRole).toString().isEmpty());
    }
    void renderPreview() {
        const auto output = qEnvironmentVariable("TOON_UI_PREVIEW"); if (output.isEmpty()) QSKIP("Opt-in Symbol frame picker previews.");
        const auto source = fixture(); Canvas canvas; canvas.setProject(source); TimelineController controller(&canvas);
        SymbolFramePickerDialog dialog(&controller, source.shots[0].layers[0].id); dialog.show();
        QTRY_VERIFY_WITH_TIMEOUT(frames(dialog)->currentItem()->data(Qt::UserRole + 1).toBool(), 10000);
        QDir().mkpath(output); QVERIFY(dialog.grab().save(output + "/symbol-frame-picker.png"));
        auto font = dialog.font(); font.setPointSizeF(font.pointSizeF() + 3); dialog.setFont(font); dialog.resize(280, 480);
        QCoreApplication::processEvents(); QVERIFY(dialog.grab().save(output + "/symbol-frame-picker-narrow.png"));
    }
};
QTEST_MAIN(SymbolFramePickerTests)
#include "test_symbol_frame_picker.moc"
