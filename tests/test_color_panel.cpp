#include "canvas.h"
#include "color_panel.h"
#include "ui_metrics.h"
#include "drawing_panels.h"
#include "main_window.h"
#include <QDir>
#include <QLineEdit>
#include <QLabel>
#include <QMouseEvent>
#include <QToolButton>
#include <QtTest>
#include <memory>

using namespace toon;
class ColorPanelTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void directPickerKeepsForegroundBackgroundAndHistorySeparate() {
        Canvas canvas; canvas.setProject(Project::create()); ColorPanel panel(&canvas);
        panel.resize(260, 370); panel.show(); QTest::qWait(10);
        auto *picker = panel.findChild<ColorPicker *>(); QVERIFY(picker);
        const auto background = canvas.brush.backgroundColor;
        canvas.setBrushColor(Qt::red);
        QTest::mouseClick(picker, Qt::LeftButton, Qt::NoModifier, {picker->width() - 14, picker->height() / 3});
        QTest::mouseClick(picker, Qt::LeftButton, Qt::NoModifier, {picker->width() - 38, 4});
        const auto foreground = canvas.brush.color;
        QVERIFY(foreground.green() > 230); QVERIFY(foreground.red() < 30);
        QCOMPARE(canvas.brush.backgroundColor, background);
        panel.findChild<QToolButton *>("backgroundColor")->click();
        panel.findChild<QToolButton *>("colorSwatch3")->click();
        QCOMPARE(canvas.brush.backgroundColor, QColor("#b54e55"));
        QCOMPARE(canvas.brush.color, foreground);
        panel.findChild<QToolButton *>("swapColors")->click();
        QCOMPARE(canvas.brush.color, QColor("#b54e55"));
        QCOMPARE(canvas.brush.backgroundColor, foreground);
        QCOMPARE(canvas.document()->history()->count(), 0);
        QVERIFY(canvas.document()->history()->isClean());
    }
    void exactInputExternalUpdatesAndKeyboard() {
        Canvas canvas; ColorPanel panel(&canvas); QVERIFY(!panel.isEnabled());
        canvas.setProject(Project::create()); QVERIFY(panel.isEnabled());
        panel.resize(260, 370); panel.show();
        auto *hex = panel.findChild<QLineEdit *>("colorHex");
        hex->setText("#334455"); QVERIFY(QMetaObject::invokeMethod(hex, "editingFinished", Qt::DirectConnection));
        QCOMPARE(canvas.brush.color, QColor("#334455"));
        hex->setText("#12"); QVERIFY(QMetaObject::invokeMethod(hex, "editingFinished", Qt::DirectConnection));
        QCOMPARE(canvas.brush.color, QColor("#334455")); QCOMPARE(hex->text(), QString("#334455"));
        hex->setText("#80334455"); QVERIFY(QMetaObject::invokeMethod(hex, "editingFinished", Qt::DirectConnection));
        QCOMPARE(canvas.brush.color.alpha(), 128);
        auto *picker = panel.findChild<ColorPicker *>(); const auto before = picker->color();
        picker->setFocus(); QTest::keyClick(picker, Qt::Key_Up);
        QVERIFY(canvas.brush.color.valueF() > before.valueF()); QCOMPARE(canvas.brush.color.alpha(), 128);
        auto brush = canvas.brush; brush.color = QColor("#D06030"); brush.backgroundColor = QColor("#AABBCC"); canvas.setBrushSettings(brush);
        QCOMPARE(hex->text(), QString("#D06030"));
        panel.findChild<QToolButton *>("backgroundColor")->click(); QCOMPARE(hex->text(), QString("#AABBCC"));
        const auto hueBefore = picker->color().hsvHueF(); QTest::keyClick(picker, Qt::Key_Right, Qt::ShiftModifier);
        QVERIFY(canvas.brush.backgroundColor.hsvHueF() > hueBefore);
        QCOMPARE(canvas.brush.color, QColor("#D06030"));
    }
    void toolsAreDistinctAccessibleDirectAndCompact() {
        Canvas canvas; canvas.setProject(Project::create());
        std::unique_ptr<QWidget> panel(drawingPanel(&canvas, "toolsPanel")); panel->resize(220, 140); panel->show();
        auto *brush = panel->findChild<QToolButton *>("toolBrush"); auto *lasso = panel->findChild<QToolButton *>("toolLasso");
        for (const auto *id : {"toolBrush", "toolEraser", "toolRectangle", "toolLasso", "toolTransform", "toolHand", "toolGuides"}) {
            auto *button = panel->findChild<QToolButton *>(id); QVERIFY(button);
            QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonIconOnly);
            QVERIFY(!button->toolTip().isEmpty()); QVERIFY(!button->accessibleName().isEmpty());
            QVERIFY(!button->icon().isNull());
            const auto detail = QString("%1 is %2 × %3").arg(id).arg(button->width()).arg(button->height());
            QVERIFY2(button->width() >= 28 && button->height() >= 28, qPrintable(detail));
        }
        QVERIFY(brush->isChecked()); brush->click(); QVERIFY(brush->isChecked()); lasso->click(); QCOMPARE(canvas.tool(), Canvas::Tool::LassoSelect);
        QVERIFY(!brush->isChecked()); QVERIFY(lasso->isChecked());
        auto *rectangle = panel->findChild<QToolButton *>("toolRectangle");
        QVERIFY(lasso->icon().cacheKey() != rectangle->icon().cacheKey());
        QVERIFY(!lasso->geometry().intersects(rectangle->geometry()));
        QVERIFY(lasso->isVisible()); QVERIFY(rectangle->isVisible());
    }
    void lostReleaseDoesNotLetHoverChangeColor() {
        ColorPicker picker; picker.resize(220, 180); picker.setColor(Qt::red); picker.show();
        QTest::mousePress(&picker, Qt::LeftButton, Qt::NoModifier, {80, 60});
        const auto before = picker.color();
        QMouseEvent hover(QEvent::MouseMove, QPointF(140, 120), QPointF(picker.mapToGlobal(QPoint(140, 120))),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&picker, &hover);
        QCOMPARE(picker.color(), before);
        QTest::mouseRelease(&picker, Qt::LeftButton, Qt::NoModifier, {140, 120});
        QCOMPARE(picker.color(), before);
    }
    void largerFontsAndResizePreserveHexDraftAndStatus() {
        Canvas canvas; canvas.setProject(Project::create()); ColorPanel panel(&canvas);
        panel.resize(240,450); panel.show(); panel.activateWindow();
        auto *hex=panel.findChild<QLineEdit*>("colorHex"); auto *status=panel.findChild<QLabel*>("colorPaletteStatus");
        const auto color=canvas.brush.color; const auto originalHeight=status->height();
        hex->setFocus(); QTRY_VERIFY(hex->hasFocus()); hex->selectAll(); QTest::keyClicks(hex,"#aabb");
        auto font=panel.font();font.setPixelSize(22);panel.setFont(font);panel.resize(220,480);QCoreApplication::processEvents();
        QCOMPARE(hex->text(),QString("#aabb"));QVERIFY(hex->hasFocus());QCOMPARE(canvas.brush.color,color);
        QCOMPARE(status->font().pixelSize(),22);QCOMPARE(hex->font().pixelSize(),22);
        QTRY_VERIFY(status->height()>originalHeight);QVERIFY(status->height()>=status->fontMetrics().height());
        QVERIFY(hex->height()>=hex->fontMetrics().height()+4);
        QVERIFY(panel.findChild<ColorPicker*>()->height()>=140);
        hex->setText("#aabbcc");QTest::keyClick(hex,Qt::Key_Return);QCOMPARE(canvas.brush.color,QColor("#aabbcc"));
        QCOMPARE(canvas.document()->history()->count(),0);
    }
    void compactColorPanelAndPreview() {
        Canvas canvas; canvas.setProject(Project::create()); ColorPanel panel(&canvas);
        panel.resize(220, 350); panel.show(); QTest::qWait(10);
        QVERIFY(panel.minimumSizeHint().width() <= 220);
        QCOMPARE(panel.height(),350);
        QVERIFY(panel.findChild<ColorPicker *>()->height()>=140);
        QCOMPARE(panel.findChild<QToolButton *>("brushColor")->size(),QSize(ui::metrics::actionTarget,ui::metrics::actionTarget));
        QCOMPARE(panel.findChild<QToolButton *>("swapColors")->iconSize(),QSize(ui::metrics::actionIcon,ui::metrics::actionIcon));
        QCOMPARE(panel.findChild<QToolButton *>("colorPaletteMore")->size(),QSize(28,28));
        QVERIFY(!panel.findChild<QToolButton *>("colorPaletteAdd"));
        auto *first=panel.findChild<QToolButton *>("colorSwatch0");
        auto *sixth=panel.findChild<QToolButton *>("colorSwatch5");
        auto *seventh=panel.findChild<QToolButton *>("colorSwatch6");
        QCOMPARE(first->height(),28);QCOMPARE(first->y(),sixth->y());QVERIFY(seventh->y()>first->y());
        for (auto *button : panel.findChildren<QToolButton *>()) {
            QVERIFY(!button->toolTip().isEmpty()); QVERIFY(!button->accessibleName().isEmpty()); QVERIFY(!button->icon().isNull());
            const auto pos = button->mapTo(&panel, QPoint());
            QVERIFY(pos.x() >= 0 && pos.x() + button->width() <= panel.width());
        }
        canvas.setBrushColor(QColor("#CE906C"));
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW","test-artifacts");QVERIFY(QDir().mkpath(output));QVERIFY(panel.grab().save(QDir(output).filePath("colors-panel.png")));
    }
};
QTEST_MAIN(ColorPanelTests)
#include "test_color_panel.moc"
