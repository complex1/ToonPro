#include "canvas.h"
#include "document.h"
#include "movie_review_controller.h"
#include "movie_review_panel.h"
#include "timeline_controller.h"
#include "timeline_panel.h"
#include "timeline_test_geometry.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QAbstractScrollArea>
#include <QAction>
#include <QDir>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPointingDevice>
#include <QStackedWidget>
#include <QSignalSpy>
#include <QStyle>
#include <QTabBar>
#include <QTabletEvent>
#include <QToolButton>
#include <QtTest>

using namespace toon;
namespace {
Project movieProject() {
    auto project = Project::create(); project.width=160; project.height=120;
    project.shots[0].durationFrames=6; project.shots[0].scene="Market"; project.shots[0].number="C001"; project.shots[0].name="Arrival";
    auto second=Project::create().shots.first(); second.durationFrames=8; second.scene="Home"; second.number="C002"; second.name="Kitchen"; project.shots.append(second);
    auto third=Project::create().shots.first(); third.durationFrames=10; third.scene="Market"; third.number="C003"; third.name="A very long shot title for the thumbnail strip"; project.shots.append(third);
    const QColor sky[]{QColor("#90b9c8"),QColor("#d5ad7c"),QColor("#ba8ba4")};
    const QColor buildings[]{QColor("#435c77"),QColor("#6a5247"),QColor("#554e72")};
    for(int i=0;i<project.shots.size();++i){
        QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);
        QPainter painter(&tile);painter.fillRect(QRect(0,0,160,120),sky[i]);painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#f5e8b9"));painter.drawEllipse(QRect(18+i*30,12,24,24));
        painter.fillRect(QRect(0,90,160,30),buildings[i]);
        for(int column=0;column<4;++column){const int x=8+column*42,y=45+(column+i)%3*9;painter.fillRect(QRect(x,y,30,90-y),buildings[i]);painter.fillRect(QRect(x+6,y+9,8,10),QColor("#eedba8"));}
        painter.end();project.shots[i].layers.first().tiles.insert({0,0},tile);
    }
    return project;
}
void settle() { QCoreApplication::processEvents(); for(int i=0;i<4;++i)QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest); }
QToolButton *button(QWidget &panel,const char *name) { return panel.findChild<QToolButton *>(name); }
}
class MovieReviewPanelTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void controllerCanBeDestroyedBeforeItsDockPanel() {
        Canvas canvas;canvas.setProject(movieProject());
        auto *controller=new TimelineController(&canvas);
        auto *panel=new TimelinePanel(controller);
        panel->resize(700,300);panel->show();settle();
        delete controller;
        delete panel;
        QVERIFY(!canvas.document()->strokeActive());
    }
    void scopesAndGlobalPlayheadKeepTheDrawingUntouched() {
        Canvas canvas; canvas.setProject(movieProject()); TimelineController controller(&canvas); TimelinePanel panel(&controller);
        panel.resize(700,300); panel.show(); settle();
        auto *modes=panel.findChild<QTabBar *>("timelineReviewModes"); QVERIFY(modes); QCOMPARE(modes->count(),3); QCOMPARE(modes->currentIndex(),0);
        QVERIFY(button(panel,"timelinePlay")->isVisibleTo(&panel));
        const auto source=canvas.project().toJson(); const auto shotId=canvas.document()->activeShot().id; const int history=canvas.document()->history()->count();
        modes->setCurrentIndex(2); settle(); auto *review=controller.movieReview();
        QCOMPARE(review->mode(),ReviewMode::Movie); QCOMPARE(review->frameCount(),24);
        QVERIFY(!button(panel,"timelinePlay")->isVisibleTo(&panel)); QVERIFY(button(panel,"moviePlay")->isVisibleTo(&panel));
        button(panel,"movieNextShot")->click(); QCOMPARE(review->currentFrame(),6); QCOMPARE(review->location().shotIndex,1); QCOMPARE(review->location().frame,0);
        auto *frame=panel.findChild<ValueSlider *>("movieCurrentFrame"); QVERIFY(frame); frame->findChild<QDoubleSpinBox *>()->setValue(12);
        QCOMPARE(review->currentFrame(),11); QCOMPARE(review->location().frame,5);
        QCOMPARE(canvas.document()->activeShot().id,shotId); QCOMPARE(canvas.document()->currentFrame(),0);
        QCOMPARE(canvas.project().toJson(),source); QCOMPARE(canvas.document()->history()->count(),history);
        modes->setCurrentIndex(1); QCOMPARE(review->mode(),ReviewMode::Scene); QCOMPARE(review->frameCount(),16);
        button(panel,"movieNextShot")->click(); QCOMPARE(review->location().shotIndex,2);
        modes->setCurrentIndex(0); QCOMPARE(review->mode(),ReviewMode::Shot); QVERIFY(button(panel,"timelinePlay")->isVisibleTo(&panel));
        QCOMPARE(canvas.document()->activeShot().id,shotId); QCOMPARE(canvas.document()->currentFrame(),0);
    }
    void editReturnsToTheReviewedShotAndLocalFrame() {
        Canvas canvas; canvas.setProject(movieProject()); TimelineController controller(&canvas); TimelinePanel panel(&controller); panel.resize(700,300); panel.show(); settle();
        auto *review=controller.movieReview(); QVERIFY(review->setMode(ReviewMode::Movie)); QVERIFY(review->setFrame(9));
        const auto source=canvas.project().toJson(); const int history=canvas.document()->history()->count();
        button(panel,"movieEditShot")->click(); QCOMPARE(review->mode(),ReviewMode::Shot); QCOMPARE(canvas.document()->activeShotIndex(),1); QCOMPARE(canvas.document()->currentFrame(),3);
        QCOMPARE(panel.findChild<QTabBar *>("timelineReviewModes")->currentIndex(),0);
        QCOMPARE(canvas.project().toJson(),source); QCOMPARE(canvas.document()->history()->count(),history);
    }
    void thumbnailsSeekAndKeyboardStepsTheGlobalPlayhead() {
        Canvas canvas; canvas.setProject(movieProject()); TimelineController controller(&canvas); TimelinePanel panel(&controller); panel.resize(700,300); panel.show(); settle();
        auto *review=controller.movieReview(); QVERIFY(review->setMode(ReviewMode::Movie)); settle();
        auto *strip=panel.findChild<QAbstractScrollArea *>("movieShotStrip"); QVERIFY(strip);
        QTest::mouseClick(strip->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(154,24)); QCOMPARE(review->currentFrame(),6);
        QTest::keyClick(strip,Qt::Key_Right); QCOMPARE(review->currentFrame(),7);
        QTest::keyClick(strip,Qt::Key_End); QCOMPARE(review->currentFrame(),23); QCOMPARE(review->location().shotIndex,2);
        QTest::keyClick(strip,Qt::Key_Home); QCOMPARE(review->currentFrame(),0);
        QTest::mouseClick(strip->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(154,24));
        QTest::keyClick(strip,Qt::Key_Return); QCOMPARE(review->mode(),ReviewMode::Shot); QCOMPARE(canvas.document()->activeShotIndex(),1);
    }
    void compactControlsRemainReachableAndHaveDelayedHelp() {
        Canvas canvas; canvas.setProject(movieProject()); TimelineController controller(&canvas); TimelinePanel panel(&controller); panel.resize(700,360); panel.show(); settle();
        QSignalSpy thumbnails(controller.movieReview(),&MovieReviewController::thumbnailReady);
        QVERIFY(controller.movieReview()->setMode(ReviewMode::Movie)); settle();
        QTRY_VERIFY_WITH_TIMEOUT(([&]{QSet<int> ready;for(const auto &args:thumbnails)ready.insert(args[0].toInt());return ready.size()==3;})(),5000);settle();
        auto *strip=panel.findChild<QAbstractScrollArea *>("movieShotStrip");const auto cards=strip->viewport()->grab().toImage();
        // The first card's playhead triangle must not leave a filled brush
        // behind and paint every following card with the light focus color.
        QCOMPARE(cards.pixelColor(qRound(149*cards.devicePixelRatio()),qRound(5*cards.devicePixelRatio())),QColor(ui::panel));
        const QString capture=qEnvironmentVariable("TOON_UI_PREVIEW"); if(!capture.isEmpty())QVERIFY(QDir().mkpath(capture));
        for(int width:{700,360,240}) {
            panel.resize(width,360); settle(); QCOMPARE(panel.width(),width);
            for(auto *control:panel.findChildren<QToolButton *>())if(control->isVisibleTo(&panel)) {
                const QRect bounds(control->mapTo(&panel,QPoint()),control->size()); QVERIFY2(bounds.left()>=0&&bounds.right()<panel.width(),qPrintable(control->objectName()));
                QVERIFY(!control->toolTip().isEmpty()); QVERIFY(!control->accessibleName().isEmpty()); QCOMPARE(control->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,control),600);
            }
            auto *frame=panel.findChild<ValueSlider *>("movieCurrentFrame"); QCOMPARE(frame->findChild<QDoubleSpinBox *>()->width(),48); QVERIFY(frame->findChild<QDoubleSpinBox *>()->suffix().isEmpty());
            if(!capture.isEmpty())QVERIFY(panel.grab().save(capture+QString("/movie-review-%1.png").arg(width)));
        }
        auto *quality=button(panel,"moviePreviewQuality"); QVERIFY(quality->menu()); quality->menu()->findChild<QAction *>("moviePreviewQuarter")->trigger();
        QCOMPARE(controller.movieReview()->previewDivisor(),4); QVERIFY(quality->menu()->findChild<QAction *>("moviePreviewQuarter")->isChecked());
        const bool loop=controller.movieReview()->looping(); button(panel,"movieLoop")->click(); QCOMPARE(controller.movieReview()->looping(),!loop);
    }
    void penSelectsShotWithoutDrawing() {
        Canvas canvas; canvas.setProject(movieProject()); TimelineController controller(&canvas); TimelinePanel panel(&controller); panel.resize(700,300); panel.show(); settle();
        auto *review=controller.movieReview(); QVERIFY(review->setMode(ReviewMode::Movie)); settle();panel.raise();panel.activateWindow();QVERIFY(QTest::qWaitForWindowActive(&panel));
        auto *strip=panel.findChild<QAbstractScrollArea *>("movieShotStrip"); QVERIFY(strip); const auto source=canvas.project().toJson();
        QPointingDevice pen("Review pen",751,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,3);
        const QPointF local(154,24);
        for(auto type:{QEvent::TabletPress,QEvent::TabletRelease}) {
            QTabletEvent event(type,&pen,local,strip->viewport()->mapToGlobal(local),type==QEvent::TabletPress?.5:0,0,0,0,0,0,Qt::NoModifier,Qt::LeftButton,type==QEvent::TabletPress?Qt::LeftButton:Qt::NoButton);
            QApplication::sendEvent(strip->viewport(),&event); QVERIFY(event.isAccepted());
        }
        QCOMPARE(review->currentFrame(),6); QTRY_VERIFY(strip->hasFocus()); QCOMPARE(canvas.project().toJson(),source); QCOMPARE(canvas.document()->currentFrame(),0);
        QTest::keyClick(strip,Qt::Key_Escape); QCOMPARE(review->mode(),ReviewMode::Shot);
    }
    void resizeAndQualityKeepUnfinishedFrameEntry() {
        Canvas canvas; canvas.setProject(movieProject()); TimelineController controller(&canvas); TimelinePanel panel(&controller); panel.resize(700,360); panel.show(); settle();
        QVERIFY(controller.movieReview()->setMode(ReviewMode::Movie));settle();panel.raise();panel.activateWindow();QVERIFY(QTest::qWaitForWindowActive(&panel));
        auto *frame=panel.findChild<ValueSlider *>("movieCurrentFrame");auto *number=frame->findChild<QDoubleSpinBox *>();auto *editor=number->findChild<QLineEdit *>();
        number->setFocus();QTRY_VERIFY(number->hasFocus()||editor->hasFocus());number->selectAll();QTest::keyClicks(number,"13");
        QCOMPARE(editor->text(),QString("13"));QVERIFY(frame->editingActive());QCOMPARE(controller.movieReview()->currentFrame(),0);
        const auto source=canvas.project().toJson();
        for(int width:{240,700,360}){panel.resize(width,360);settle();QCOMPARE(editor->text(),QString("13"));QVERIFY(number->hasFocus()||editor->hasFocus());}
        button(panel,"moviePreviewQuality")->menu()->findChild<QAction *>("moviePreviewHalf")->trigger();QCOMPARE(editor->text(),QString("13"));
        QTest::keyClick(number,Qt::Key_Return);QCOMPARE(controller.movieReview()->currentFrame(),12);QCOMPARE(canvas.project().toJson(),source);
    }
    void repeatedSpaceAndModifiedNavigationAreIgnored() {
        Canvas canvas;canvas.setProject(movieProject());TimelineController controller(&canvas);TimelinePanel panel(&controller);panel.resize(700,300);panel.show();settle();
        auto *review=controller.movieReview();QVERIFY(review->setMode(ReviewMode::Movie));settle();auto *strip=panel.findChild<QAbstractScrollArea *>("movieShotStrip");
        QKeyEvent repeated(QEvent::KeyPress,Qt::Key_Space,Qt::NoModifier,QString(),true);QApplication::sendEvent(strip,&repeated);QVERIFY(!review->playing());
        QTest::keyClick(strip,Qt::Key_Right,Qt::ControlModifier);QCOMPARE(review->currentFrame(),0);
        QVERIFY(review->setFrame(7));QTest::keyClick(strip,Qt::Key_Enter,Qt::KeypadModifier);QCOMPARE(review->mode(),ReviewMode::Shot);QCOMPARE(canvas.document()->activeShotIndex(),1);QCOMPARE(canvas.document()->currentFrame(),1);
    }
    void guardedModeChangeAndSymbolEditorStayInShotMode() {
        Canvas canvas; canvas.setProject(movieProject()); TimelineController controller(&canvas); TimelinePanel panel(&controller);
        controller.setTransitionGuard([]{return false;}); panel.findChild<QTabBar *>("timelineReviewModes")->setCurrentIndex(2);
        QCOMPARE(controller.movieReview()->mode(),ReviewMode::Shot); QCOMPARE(panel.findChild<QTabBar *>("timelineReviewModes")->currentIndex(),0);
        TimelineController symbols(&canvas); symbols.setProperty("symbolEditor",true); TimelinePanel symbolPanel(&symbols); symbolPanel.show(); settle();
        QVERIFY(!symbolPanel.findChild<QTabBar *>("timelineReviewModes")->isVisibleTo(&symbolPanel)); QVERIFY(!symbols.movieReview()->setMode(ReviewMode::Movie));
    }
    void closingPanelCancelsAnActiveRangeGestureSafely() {
        Canvas canvas;canvas.setProject(movieProject());TimelineController controller(&canvas);
        auto *panel=new TimelinePanel(&controller);panel->resize(700,360);panel->show();settle();
        auto *grid=panel->findChild<QAbstractScrollArea *>("timelineGrid");QVERIFY(grid);
        const auto source=canvas.project().toJson();const int history=canvas.document()->history()->count();
        const QPointF start(timeline_test::labelWidth(grid)+25,12),end=start+QPointF(40,0);
        QMouseEvent press(QEvent::MouseButtonPress,start,grid->viewport()->mapToGlobal(start),Qt::LeftButton,Qt::LeftButton,Qt::ShiftModifier);
        QApplication::sendEvent(grid->viewport(),&press);QVERIFY(controller.hasPendingEdits());
        QMouseEvent move(QEvent::MouseMove,end,grid->viewport()->mapToGlobal(end),Qt::NoButton,Qt::LeftButton,Qt::ShiftModifier);
        QApplication::sendEvent(grid->viewport(),&move);QVERIFY(controller.hasPendingEdits());
        QSignalSpy changes(&controller,&TimelineController::changed);delete panel;
        QVERIFY(!controller.hasPendingEdits());QVERIFY(!changes.isEmpty());QVERIFY(!controller.playbackRangeEnabled());
        QCOMPARE(canvas.project().toJson(),source);QCOMPARE(canvas.document()->history()->count(),history);QVERIFY(controller.setFrame(1));
    }
};
QTEST_MAIN(MovieReviewPanelTests)
#include "movie_review_panel_tests.moc"
