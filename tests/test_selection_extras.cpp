#include "selection_extras.h"
#include "canvas.h"
#include "reference_panel.h"
#include "value_slider.h"
#include "ui_theme.h"
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QLineEdit>
#include <QSignalSpy>
#include <QToolButton>
#include <QTabBar>
#include <QtTest>
using namespace toon;
class SelectionExtrasTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void savedSelectionAndDisplayControlsRemainCompact() {
        Canvas canvas; canvas.setProject(Project::create()); auto &document = *canvas.document();
        SelectionExtras extras(&canvas); extras.resize(240, 160); extras.show();
        auto *tabs = extras.findChild<QTabBar *>("selectionDetailTabs"); QVERIFY(tabs);
        QPainterPath selection; selection.addRect({100, 100, 200, 120}); document.setRasterSelection(selection);
        auto *feather = extras.findChild<ValueSlider *>("selectionFeather"); QVERIFY(feather); QVERIFY(feather->isEnabled());
        auto *featherTrack=feather->findChild<QSlider*>(); auto *featherNumber=feather->findChild<QDoubleSpinBox*>();
        featherTrack->setSliderDown(true);featherTrack->setValue(5);featherTrack->setValue(8);featherTrack->setSliderDown(false);
        QCOMPARE(document.rasterSelection().featherRadius, 8.0);QCOMPARE(featherNumber->value(),8.0);
        auto *shade=extras.findChild<ValueSlider*>("selectionShadeOpacity");QVERIFY(shade);
        tabs->setCurrentIndex(1); QCoreApplication::processEvents();
        shade->findChild<QSlider*>()->setValue(55);QCOMPARE(canvas.selectionShadeOpacity(),.55);
        auto *shadeNumber=shade->findChild<QDoubleSpinBox*>();shadeNumber->setFocus();shadeNumber->selectAll();QTest::keyClicks(shadeNumber,"70");QTest::keyClick(shadeNumber,Qt::Key_Return);
        QCOMPARE(canvas.selectionShadeOpacity(),.7);QCOMPARE(shade->findChild<QSlider*>()->value(),70);QCOMPARE(document.history()->count(),0);
        tabs->setCurrentIndex(0); QCoreApplication::processEvents();
        auto *names = extras.findChild<QComboBox *>("savedSelectionList"); names->setEditText("Face");
        QTest::mouseClick(extras.findChild<QToolButton *>("saveNamedSelection"), Qt::LeftButton);
        QCOMPARE(document.project().shots[0].savedSelections.size(), 1); QCOMPARE(names->currentText(), QString("Face"));
        document.clearRasterSelection(); QTest::mouseClick(extras.findChild<QToolButton *>("loadNamedSelection"), Qt::LeftButton);
        QCOMPARE(document.rasterSelection().path, selection); QCOMPARE(document.rasterSelection().featherRadius, 8.0);
        tabs->setCurrentIndex(1); QCoreApplication::processEvents();
        QTest::mouseClick(extras.findChild<QToolButton *>("selectionAnimation"), Qt::LeftButton); QVERIFY(!canvas.selectionAnimated());
        QTest::mouseClick(extras.findChild<QToolButton *>("selectionHatching"), Qt::LeftButton); QVERIFY(!canvas.selectionFocusOverlay());
        for (auto *button : extras.findChildren<QToolButton *>()) { QVERIFY(!button->toolTip().isEmpty()); QVERIFY(!button->accessibleName().isEmpty()); QVERIFY(button->width() >= 28); QVERIFY(button->height() >= 28); }
        QVERIFY(extras.width() <= 240);
        for(auto *control:extras.findChildren<ValueSlider*>()){QVERIFY(control->findChild<QSlider*>()->width()>=70);QVERIFY(control->findChild<QDoubleSpinBox*>()->width()<=80);}
        QDir().mkpath("test-artifacts"); QVERIFY(extras.grab().save("test-artifacts/selection-extras.png"));
        tabs->setCurrentIndex(0); QCoreApplication::processEvents();
        QTest::mouseClick(extras.findChild<QToolButton *>("deleteNamedSelection"), Qt::LeftButton); QVERIFY(document.project().shots[0].savedSelections.isEmpty());
    }
    void referenceSamplerUsesImageCoordinatesWithoutPanOrHistory() {
        Document document; document.load(Project::create());
        QImage image(64, 64, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::red); image.setPixelColor(20, 30, QColor(20, 80, 140, 128));
        QVERIFY(document.addReference("Palette", image)); const int count = document.history()->count();
        ReferencePanel panel(&document); panel.resize(280, 420); panel.show(); QTest::qWait(10);
        auto *view = panel.findChild<ReferenceView *>("referenceView"); QVERIFY(view); QSignalSpy colors(&panel, &ReferencePanel::colorSampled);
        const auto transform = view->imageTransform(); const auto location = transform.map(QPointF(20.5, 30.5)).toPoint();
        QTest::mouseClick(view, Qt::LeftButton, Qt::AltModifier, location);
        QCOMPARE(colors.size(), 1); QCOMPARE(qvariant_cast<QColor>(colors.takeFirst().at(0)), image.pixelColor(20, 30)); QCOMPARE(view->imageTransform(), transform);
        panel.setSampling(true); QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, location); QCOMPARE(colors.size(), 1);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, QPoint(1, 1)); QCOMPARE(colors.size(), 1);
        QCOMPARE(document.history()->count(), count);
    }

    void narrowExtrasKeepDraftsAndTargets() {
        Canvas canvas; canvas.setProject(Project::create()); auto *doc = canvas.document();
        QPainterPath selection; selection.addRect({20, 30, 100, 80}); doc->setRasterSelection(selection);
        SelectionExtras extras(&canvas); extras.resize(200, 180); extras.show();
        QCoreApplication::processEvents();
        QCOMPARE(extras.width(), 200);
        for (auto *button : extras.findChildren<QToolButton *>()) {
            if (!button->isVisibleTo(&extras)) continue;
            QCOMPARE(button->size(), QSize(28, 28));
            const QRect bounds(button->mapTo(&extras, QPoint()), button->size());
            QVERIFY(extras.rect().contains(bounds));
        }
        for (auto *control : extras.findChildren<ValueSlider *>()) {
            if (!control->isVisibleTo(&extras)) continue;
            QVERIFY(control->findChild<QSlider *>()->width() >= 74);
            QCOMPARE(control->findChild<QDoubleSpinBox *>()->width(), 48);
        }
        auto *names = extras.findChild<QComboBox *>("savedSelectionList");
        names->setEditText("Face close-up");
        auto *field = extras.findChild<ValueSlider *>("selectionFeather")->findChild<QDoubleSpinBox *>();
        field->setFocus(); QTRY_VERIFY(field->hasFocus());
        field->selectAll(); QTest::keyClicks(field, "12");
        extras.resize(280, 180); doc->rasterSelectionChanged(); QCoreApplication::processEvents();
        QVERIFY(field->hasFocus()); QCOMPARE(field->findChild<QLineEdit *>()->text(), QString("12"));
        QCOMPARE(names->currentText(), QString("Face close-up")); QCOMPARE(doc->rasterSelection().featherRadius, 0.0);
        QTest::keyClick(field, Qt::Key_Escape);
        QCOMPARE(field->value(), 0.0); QCOMPARE(doc->rasterSelection().featherRadius, 0.0);
        QCOMPARE(doc->history()->count(), 0);
    }

    void tabsMeasureActiveContentAndKeepSavedSelectionDrafts() {
        Canvas canvas; canvas.setProject(Project::create()); auto *doc = canvas.document();
        QPainterPath selection; selection.addRect({20, 30, 100, 80}); doc->setRasterSelection(selection);
        SelectionExtras extras(&canvas); extras.resize(240, 200); extras.show();
        auto *tabs = extras.findChild<QTabBar *>("selectionDetailTabs"); QVERIFY(tabs);
        auto *pages = extras.findChild<QWidget *>("selectionDetailPages"); QVERIFY(pages);
        auto *names = extras.findChild<QComboBox *>("savedSelectionList"); names->setEditText("Face close-up draft");
        QTest::qWait(20); const auto selectionHeight = pages->sizeHint().height();
        tabs->setCurrentIndex(1); QTest::qWait(20);
        QVERIFY(!names->isVisibleTo(&extras)); QVERIFY(extras.findChild<ValueSlider *>("selectionShadeOpacity")->isVisibleTo(&extras));
        QVERIFY(pages->sizeHint().height() < selectionHeight);
        auto font = extras.font(); font.setPixelSize(24); extras.setFont(font); extras.resize(240, 200); QTest::qWait(20);
        for (int i = 0; i < tabs->count(); ++i) {
            QVERIFY(tabs->rect().contains(tabs->tabRect(i)));
            QVERIFY(!tabs->tabToolTip(i).isEmpty()); QVERIFY(!tabs->accessibleTabName(i).isEmpty());
        }
        tabs->setCurrentIndex(0); doc->rasterSelectionChanged(); QTest::qWait(20);
        QCOMPARE(names->currentText(), QString("Face close-up draft")); QVERIFY(names->isVisibleTo(&extras));
        QCOMPARE(doc->history()->count(), 0); QCOMPARE(doc->rasterSelection().path, selection);
        extras.findChild<QToolButton *>("saveNamedSelection")->click();
        QCOMPARE(doc->activeShot().savedSelections.size(), 1);
        QCOMPARE(doc->activeShot().savedSelections.first().name, QString("Face close-up draft"));
    }
};
QTEST_MAIN(SelectionExtrasTest)
#include "test_selection_extras.moc"
