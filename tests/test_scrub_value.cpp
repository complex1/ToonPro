#include "scrub_value.h"
#include "ui_theme.h"

#include <QApplication>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QtTest>
#include <limits>

using namespace toon;
namespace {
void moveMouse(QWidget &widget, QPoint position, Qt::MouseButtons buttons = Qt::LeftButton,
               Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QMouseEvent event(QEvent::MouseMove, position, widget.mapToGlobal(position),
                      Qt::NoButton, buttons, modifiers);
    QApplication::sendEvent(&widget, &event);
}
void showWidget(QWidget &widget)
{
    widget.resize(90, 30);
    widget.show();
    QApplication::processEvents();
}
}
class ScrubValueTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void externalUpdatesAreSilentAndBounded()
    {
        ScrubValue field("Opacity", 0, 100, 75);
        QSignalSpy changes(&field, &ScrubValue::valueChanged);
        QSignalSpy starts(&field, &ScrubValue::editingStarted);
        QSignalSpy finishes(&field, &ScrubValue::editingFinished);
        field.setValue(34);
        QCOMPARE(field.value(), 34);
        field.setValue(900);
        QCOMPARE(field.value(), 100);
        field.setValue(std::numeric_limits<double>::quiet_NaN());
        QCOMPARE(field.value(), 100);
        field.setRange(5, 50);
        QCOMPARE(field.value(), 50);
        QCOMPARE(changes.count(), 0);
        QCOMPARE(starts.count(), 0);
        QCOMPARE(finishes.count(), 0);
        QVERIFY(field.minimumSizeHint().width() >= 64);
        QVERIFY(field.minimumSizeHint().height() >= 28);
        QCOMPARE(field.accessibleName(), QString("Opacity"));
    }
    void clickOpensExactEntryAndCommitsOnce()
    {
        ScrubValue field("Opacity", 0, 100, 75);
        showWidget(field);
        QSignalSpy starts(&field, &ScrubValue::editingStarted);
        QSignalSpy finishes(&field, &ScrubValue::editingFinished);
        QSignalSpy cancels(&field, &ScrubValue::editingCancelled);
        // A document typically feeds the new value back synchronously.
        connect(&field, &ScrubValue::valueChanged, &field, &ScrubValue::setValue);
        QTest::mouseClick(&field, Qt::LeftButton);
        auto *editor = field.findChild<QLineEdit *>("scrubEditor");
        QVERIFY(editor && editor->isVisible());
        QVERIFY(field.rect().contains(editor->geometry()));
        QCOMPARE(editor->selectedText(), QString("75"));
        QTest::keyClicks(editor, "42");
        QCOMPARE(editor->text(), QString("42"));
        QCOMPARE(field.value(), 42);
        QCOMPARE(finishes.count(), 0);
        QTest::keyClick(editor, Qt::Key_Return);
        QVERIFY(!editor->isVisible());
        QCOMPARE(field.value(), 42);
        QCOMPARE(starts.count(), 1);
        QCOMPARE(finishes.count(), 1);
        QCOMPARE(cancels.count(), 0);
    }
    void escapeRestoresExactEntryWithoutCommit()
    {
        ScrubValue field("Opacity", 0, 100, 75);
        showWidget(field);
        QSignalSpy finishes(&field, &ScrubValue::editingFinished);
        QSignalSpy cancels(&field, &ScrubValue::editingCancelled);
        QTest::mouseClick(&field, Qt::LeftButton);
        auto *editor = field.findChild<QLineEdit *>("scrubEditor");
        QTest::keyClicks(editor, "23");
        QCOMPARE(field.value(), 23);
        QTest::keyClick(editor, Qt::Key_Escape);
        QCOMPARE(field.value(), 75);
        QVERIFY(!editor->isVisible());
        QCOMPARE(finishes.count(), 0);
        QCOMPARE(cancels.count(), 1);
    }
    void dragProducesLiveValuesAndOneCommit()
    {
        ScrubValue field("Opacity", 0, 100, 50);
        showWidget(field);
        QSignalSpy starts(&field, &ScrubValue::editingStarted);
        QSignalSpy finishes(&field, &ScrubValue::editingFinished);
        QSignalSpy changes(&field, &ScrubValue::valueChanged);
        QTest::mousePress(&field, Qt::LeftButton, Qt::NoModifier, {30, 15});
        moveMouse(field, {50, 15});
        QCOMPARE(field.value(), 60);
        moveMouse(field, {70, 15});
        QCOMPARE(field.value(), 70);
        QCOMPARE(finishes.count(), 0);
        QTest::mouseRelease(&field, Qt::LeftButton, Qt::NoModifier, {70, 15});
        QCOMPARE(starts.count(), 1);
        QCOMPARE(finishes.count(), 1);
        QCOMPARE(changes.count(), 2);
        QVERIFY(!field.findChild<QLineEdit *>("scrubEditor")->isVisible());
    }
    void escapeCancelsDragAndReleaseDoesNotOpenEditor()
    {
        ScrubValue field("Opacity", 0, 100, 50);
        showWidget(field);
        QSignalSpy finishes(&field, &ScrubValue::editingFinished);
        QSignalSpy cancels(&field, &ScrubValue::editingCancelled);
        QTest::mousePress(&field, Qt::LeftButton, Qt::NoModifier, {30, 15});
        moveMouse(field, {70, 15});
        QCOMPARE(field.value(), 70);
        QTest::keyClick(&field, Qt::Key_Escape);
        QTest::mouseRelease(&field, Qt::LeftButton, Qt::NoModifier, {70, 15});
        QCOMPARE(field.value(), 50);
        QCOMPARE(finishes.count(), 0);
        QCOMPARE(cancels.count(), 1);
        QVERIFY(!field.findChild<QLineEdit *>("scrubEditor")->isVisible());
    }
    void lostReleaseFinishesGestureOnce()
    {
        ScrubValue field("Opacity", 0, 100, 50);
        showWidget(field);
        QSignalSpy finishes(&field, &ScrubValue::editingFinished);
        QTest::mousePress(&field, Qt::LeftButton, Qt::NoModifier, {30, 15});
        moveMouse(field, {60, 15});
        moveMouse(field, {61, 15}, Qt::NoButton);
        QCOMPARE(field.value(), 65);
        QCOMPARE(finishes.count(), 1);
        QEvent ungrab(QEvent::UngrabMouse);
        QApplication::sendEvent(&field, &ungrab);
        QTest::mouseRelease(&field, Qt::LeftButton, Qt::NoModifier, {60, 15});
        QCOMPARE(finishes.count(), 1);
    }
    void keyboardStepsAndCoarseDragRespectBounds()
    {
        ScrubValue field("Opacity", 0, 100, 50);
        showWidget(field);
        QSignalSpy finishes(&field, &ScrubValue::editingFinished);
        QTest::keyClick(&field, Qt::Key_Right);
        QCOMPARE(field.value(), 51);
        QTest::keyClick(&field, Qt::Key_Down, Qt::ShiftModifier);
        QCOMPARE(field.value(), 41);
        QCOMPARE(finishes.count(), 2);
        QTest::mousePress(&field, Qt::LeftButton, Qt::NoModifier, {30, 15});
        moveMouse(field, {70, 15}, Qt::LeftButton, Qt::ShiftModifier);
        QCOMPARE(field.value(), 100);
        QTest::mouseRelease(&field, Qt::LeftButton, Qt::NoModifier, {70, 15});
        QCOMPARE(finishes.count(), 3);
    }
    void invalidTextAndFocusLossDoNotLeaveOpenTransaction()
    {
        ScrubValue field("Opacity", 0, 100, 50);
        showWidget(field);
        QSignalSpy finishes(&field, &ScrubValue::editingFinished);
        QTest::keyClick(&field, Qt::Key_Return);
        auto *editor = field.findChild<QLineEdit *>("scrubEditor");
        QTest::keyClick(editor, Qt::Key_Backspace);
        QCOMPARE(editor->text(), QString());
        QFocusEvent blur(QEvent::FocusOut);
        QApplication::sendEvent(editor, &blur);
        QCOMPARE(field.value(), 50);
        QCOMPARE(finishes.count(), 1);
        QVERIFY(!editor->isVisible());
        QTest::mousePress(&field, Qt::LeftButton, Qt::NoModifier, {30, 15});
        moveMouse(field, {60, 15});
        QEvent deactivate(QEvent::WindowDeactivate);
        QApplication::sendEvent(&field, &deactivate);
        QCOMPARE(field.value(), 65);
        QCOMPARE(finishes.count(), 2);
        QTest::mouseRelease(&field, Qt::LeftButton, Qt::NoModifier, {60, 15});
        QCOMPARE(finishes.count(), 2);
    }
    void exactValuesClampAndMinimumSizeFitsEditor()
    {
        ScrubValue field("Opacity", 0, 100, 50);
        showWidget(field);
        field.resize(field.minimumSizeHint());
        QTest::mouseClick(&field, Qt::LeftButton);
        auto *editor = field.findChild<QLineEdit *>("scrubEditor");
        QVERIFY(field.rect().contains(editor->geometry()));
        QTest::keyClicks(editor, "150");
        QCOMPARE(field.value(), 100);
        QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(field.value(), 100);
        QTest::mouseClick(&field, Qt::LeftButton);
        QTest::keyClicks(editor, "-20");
        QCOMPARE(field.value(), 0);
        QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(field.value(), 0);
    }
};
QTEST_MAIN(ScrubValueTest)
#include "test_scrub_value.moc"
