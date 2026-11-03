#include "property_row.h"
#include "value_slider.h"
#include "main_window.h"
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QScrollArea>
#include <QSignalSpy>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTest>

using namespace toon;
namespace {
void layOut(PropertyRow &row, int width) {
    row.resize(width, row.heightForWidth(width));
    row.show();
    row.layout()->activate();
    QApplication::processEvents();
}
}

class PropertyRowTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void inlineControlsShareAnAlignedLabelBand() {
        auto *size = new ValueSlider("Size", 1, 4096, 12, 1, "px");
        PropertyRow sizeRow("Size", size);
        auto *pressure = new QToolButton;
        pressure->setFixedSize(28, 28);
        pressure->setAccessibleName("Pressure size");
        sizeRow.setAccessory(pressure);
        auto *flow = new ValueSlider("Flow", 0, 100, 100);
        PropertyRow flowRow("Flow", flow);
        layOut(sizeRow, 224);
        layOut(flowRow, 224);
        QCOMPARE(size->x(), flow->x());
        QCOMPARE(size->x(), 68);
        QVERIFY(sizeRow.labelWidget()->geometry().right() < pressure->geometry().left());
        QVERIFY(pressure->geometry().right() < size->geometry().left());
        QVERIFY(size->width() >= 128);
        QVERIFY(size->findChild<QSlider *>()->width() >= 74);
        QCOMPARE(size->findChild<QDoubleSpinBox *>()->width(), 48);
        QCOMPARE(sizeRow.heightForWidth(224), size->height());
        QVERIFY(sizeRow.height() <= 32);
        QCOMPARE(sizeRow.control(), size);
        QCOMPARE(sizeRow.accessibleName(), QString("Size"));
        QCOMPARE(sizeRow.labelWidget()->buddy(), size);
        QCOMPARE(size->findChild<QDoubleSpinBox *>()->accessibleName(), QString("Size"));
        QCOMPARE(size->findChild<QDoubleSpinBox *>()->accessibleDescription(), QString("px"));
    }
    void narrowAndLongLabelsWrapWithoutSqueezingTheEditor() {
        auto *control = new ValueSlider("Smoothing", 0, 100, 20);
        PropertyRow row("Smoothing", control);
        layOut(row, 150);
        QCOMPARE(control->x(), 0);
        QCOMPARE(control->width(), row.width());
        QVERIFY(control->y() > row.labelWidget()->geometry().bottom());
        QVERIFY(control->geometry().bottom() < row.height());
        QVERIFY(control->findChild<QSlider *>()->width() >= 74);
        QCOMPARE(row.minimumSizeHint().width(), 128);

        auto *longControl = new ValueSlider("Minimum brush pressure response", 0, 100, 20);
        PropertyRow longRow("Minimum brush pressure response", longControl);
        layOut(longRow, 224);
        QCOMPARE(longControl->x(), 0);
        QVERIFY(longRow.labelWidget()->height() >= longRow.labelWidget()->heightForWidth(224));
        QVERIFY(longControl->y() > longRow.labelWidget()->geometry().bottom());
        QVERIFY(longControl->geometry().bottom() < longRow.height());
        QVERIFY(longRow.minimumSizeHint().width() <= 128);
        layOut(longRow, 500);
        QVERIFY(longControl->x() > longRow.labelWidget()->geometry().right());
        QCOMPARE(longRow.heightForWidth(500), longControl->height());
    }
    void labelWidthAndFontChangesRemainResponsive() {
        auto *control = new ValueSlider("Size", 1, 4096, 12);
        PropertyRow row("Size", control);
        row.setLabelWidth(90);
        layOut(row, 240);
        QCOMPARE(control->x(), 94);
        QFont font = row.font(); font.setPointSize(font.pointSize() + 8);
        row.setFont(font);
        row.labelWidget()->setText("Long property label");
        layOut(row, 240);
        QCOMPARE(control->x(), 0);
        QVERIFY(control->y() > row.labelWidget()->geometry().bottom());
    }
    void resizeKeepsPreciseEntryAndGestureHistoryIntact() {
        auto *control = new ValueSlider("Size", 1, 4096, 12, 1, "px");
        PropertyRow row("Size", control);
        layOut(row, 224);
        row.activateWindow();
        auto *number = control->findChild<QDoubleSpinBox *>();
        auto *editor = number->findChild<QLineEdit *>();
        QSignalSpy starts(control, &ValueSlider::editingStarted);
        QSignalSpy finishes(control, &ValueSlider::editingFinished);
        QSignalSpy cancels(control, &ValueSlider::editingCancelled);
        QSignalSpy changes(control, &ValueSlider::valueChanged);
        number->setFocus(); number->selectAll();
        QTRY_VERIFY(number->hasFocus() || editor->hasFocus());
        QTest::keyClicks(number, "125.5");
        QCOMPARE(control->value(), 12.);
        QCOMPARE(editor->text(), QString("125.5"));
        layOut(row, 150);
        layOut(row, 300);
        QCOMPARE(editor->text(), QString("125.5"));
        QVERIFY(number->hasFocus() || editor->hasFocus());
        QCOMPARE(starts.count(), 1);
        QCOMPARE(finishes.count(), 0);
        QCOMPARE(changes.count(), 0);
        QTest::keyClick(number, Qt::Key_Return);
        QCOMPARE(control->value(), 125.5);
        QCOMPARE(finishes.count(), 1);
        QCOMPARE(changes.count(), 1);
        number->selectAll(); QTest::keyClicks(number, "200");
        layOut(row, 150);
        QTest::keyClick(number, Qt::Key_Escape);
        QCOMPARE(control->value(), 125.5);
        QCOMPARE(cancels.count(), 1);
        QCOMPARE(finishes.count(), 1);
    }
    void scrollLayoutUsesWrappedHeightWithoutOverlappingRows() {
        QScrollArea scroll;
        scroll.setWidgetResizable(true);
        auto *body = new QWidget;
        auto *layout = new QVBoxLayout(body);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        QList<PropertyRow *> rows;
        for (int i = 0; i < 5; ++i) {
            auto *control = new ValueSlider("Opacity", 0, 100, 100);
            auto *row = new PropertyRow(i == 0 ? "Minimum pressure" : "Opacity", control);
            layout->addWidget(row);
            rows.append(row);
        }
        layout->addStretch();
        scroll.setWidget(body);
        scroll.resize(180, 140);
        scroll.show();
        QApplication::processEvents();
        for (int i = 0; i < rows.size(); ++i) {
            auto *row = rows[i];
            QVERIFY(row->height() >= row->heightForWidth(row->width()));
            QVERIFY(row->control()->geometry().bottom() < row->height());
            if (i) QVERIFY(rows[i - 1]->geometry().bottom() < row->y());
        }
        QVERIFY(body->height() > scroll.viewport()->height());
    }
};
QTEST_MAIN(PropertyRowTest)
#include "test_property_row.moc"
