#include "canvas.h"
#include "document.h"
#include "movie_review_controller.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include <QSignalSpy>
#include <QtTest>
using namespace toon;
namespace {
Project fixture(){
    auto p=Project::create();p.width=128;p.height=128;p.fps=120;p.shots.clear();
    for(int i=0;i<3;++i){auto s=Project::create().shots[0];s.number=QString("C00%1").arg(i+1);s.scene=i==1?"B":"A";s.durationFrames=12;p.shots.append(s);}
    return p;
}
}
class MovieReviewControllerTests final:public QObject{
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void reviewDoesNotNavigateOrMutateEditingDocument(){
        Canvas canvas;canvas.resize(420,320);canvas.setProject(fixture(),2);TimelineController timeline(&canvas);
        QVERIFY(timeline.setFrame(5));const auto json=canvas.project().toJson();const auto shot=canvas.document()->activeShotId();
        const auto layer=canvas.document()->activeLayer();const auto history=canvas.document()->history()->count();
        auto *review=timeline.movieReview();QVERIFY(review->setMode(ReviewMode::Movie));QCOMPARE(review->frameCount(),36);QCOMPARE(review->currentFrame(),29);
        QVERIFY(review->setFrame(13));QCOMPARE(review->location(),(MoviePlaybackLocation{1,1}));
        QCOMPARE(canvas.document()->activeShotId(),shot);QCOMPARE(canvas.document()->currentFrame(),5);QCOMPARE(canvas.document()->activeLayer(),layer);
        QCOMPARE(canvas.project().toJson(),json);QCOMPARE(canvas.document()->history()->count(),history);
        QVERIFY(review->setMode(ReviewMode::Shot));QCOMPARE(canvas.document()->activeShotId(),shot);QCOMPARE(canvas.document()->currentFrame(),5);
        QVERIFY(!canvas.findChild<QWidget *>("movieReviewViewport")->isVisible());
    }
    void sceneUsesActiveSceneInMovieOrder(){
        Canvas canvas;canvas.setProject(fixture(),2);TimelineController timeline(&canvas);auto *review=timeline.movieReview();
        QVERIFY(review->setMode(ReviewMode::Scene));QCOMPARE(review->segments().size(),2);QCOMPARE(review->frameCount(),24);
        QCOMPARE(review->segments()[0].shotIndex,0);QCOMPARE(review->segments()[1].shotIndex,2);QCOMPARE(review->currentFrame(),12);
        QVERIFY(review->setFrame(2));QVERIFY(review->setMode(ReviewMode::Movie));QCOMPARE(review->frameCount(),36);QCOMPARE(review->currentFrame(),24);
    }
    void explicitEditOpensReviewedShotAndFrame(){
        Canvas canvas;canvas.setProject(fixture());TimelineController timeline(&canvas);auto *review=timeline.movieReview();
        QVERIFY(review->setMode(ReviewMode::Movie));QVERIFY(review->setFrame(17));QVERIFY(review->editCurrentShot());
        QCOMPARE(review->mode(),ReviewMode::Shot);QCOMPARE(canvas.document()->activeShotIndex(),1);QCOMPARE(timeline.currentFrame(),5);QCOMPARE(canvas.document()->history()->count(),0);
    }
    void pendingGuardCanRefuseWithoutLosingContext(){
        Canvas canvas;canvas.setProject(fixture());TimelineController timeline(&canvas);timeline.setTransitionGuard([]{return false;});
        QVERIFY(!timeline.movieReview()->setMode(ReviewMode::Movie));QCOMPARE(timeline.movieReview()->mode(),ReviewMode::Shot);
        QCOMPARE(timeline.currentFrame(),0);QCOMPARE(canvas.document()->activeShotIndex(),0);
    }
    void playbackCrossesCutsStopsAtEndAndPreservesDocument(){
        Canvas canvas;canvas.resize(420,320);canvas.setProject(fixture());TimelineController timeline(&canvas);auto *review=timeline.movieReview();
        const auto snapshot=canvas.project().toJson();QSignalSpy changes(review,&MovieReviewController::changed);
        QVERIFY(review->setMode(ReviewMode::Movie));review->setLooping(false);review->setPlaying(true);QVERIFY(review->playing());
        QTRY_VERIFY_WITH_TIMEOUT(review->currentFrame()>=12,2000);QTRY_VERIFY_WITH_TIMEOUT(!review->playing(),2000);
        QCOMPARE(review->currentFrame(),35);QCOMPARE(review->location(),(MoviePlaybackLocation{2,11}));
        QCOMPARE(timeline.currentFrame(),0);QCOMPARE(canvas.document()->activeShotIndex(),0);QCOMPARE(canvas.project().toJson(),snapshot);
        QVERIFY(changes.size()>3);review->setPlaying(true);QVERIFY(review->currentFrame()<35);review->setPlaying(false);
    }
    void seekStopsClockAndShotPlaybackCannotOverlap(){
        Canvas canvas;canvas.setProject(fixture());TimelineController timeline(&canvas);auto *review=timeline.movieReview();
        QVERIFY(review->setMode(ReviewMode::Movie));review->setPlaying(true);QVERIFY(review->setFrame(20));QVERIFY(!review->playing());
        QTest::qWait(40);QCOMPARE(review->currentFrame(),20);review->setPlaying(true);timeline.setPlaying(false);QVERIFY(!review->playing());
        review->setPlaying(true);QVERIFY(timeline.prepareTimelineEdit());QCOMPARE(review->mode(),ReviewMode::Shot);QVERIFY(!review->playing());
    }
    void modificationsAndToolChangesInvalidateReview(){
        Canvas canvas;canvas.setProject(fixture());TimelineController timeline(&canvas);auto *review=timeline.movieReview();
        QVERIFY(review->setMode(ReviewMode::Movie));canvas.setTool(Canvas::Tool::Hand);QCOMPARE(review->mode(),ReviewMode::Shot);
        QVERIFY(review->setMode(ReviewMode::Movie));QVERIFY(canvas.document()->beginLayerPropertyEdit());canvas.document()->previewLayerOpacity(.5);canvas.document()->commitLayerPropertyEdit();QCOMPARE(review->mode(),ReviewMode::Shot);
        QVERIFY(review->setMode(ReviewMode::Movie));canvas.setProject(fixture());QCOMPARE(review->mode(),ReviewMode::Shot);
    }
    void nestedSymbolsAndClosedDocumentsDoNotOfferMovieReview(){
        Canvas canvas;TimelineController timeline(&canvas);QVERIFY(!timeline.movieReview()->setMode(ReviewMode::Movie));
        canvas.setProject(fixture());timeline.setProperty("symbolEditor",true);QVERIFY(!timeline.movieReview()->setMode(ReviewMode::Scene));
        QVERIFY(!timeline.movieReview()->setMode(ReviewMode::Movie));QVERIFY(!timeline.movieReview()->setFrame(0));
    }
    void viewportOwnsPenAndKeyboardUntilReturningToDrawing(){
        Canvas canvas;canvas.resize(480,340);canvas.setProject(fixture());TimelineController timeline(&canvas);canvas.show();
        auto *review=timeline.movieReview();QVERIFY(review->setMode(ReviewMode::Movie));auto *view=canvas.findChild<QWidget *>("movieReviewViewport");QVERIFY(view);
        QTRY_VERIFY(view->isVisible());QCOMPARE(view->geometry(),canvas.rect());
        const auto snapshot=canvas.project().toJson();QTest::mousePress(view,Qt::LeftButton,Qt::NoModifier,{40,40});QTest::mouseMove(view,{100,100});QTest::mouseRelease(view,Qt::LeftButton,Qt::NoModifier,{100,100});
        QVERIFY(!canvas.document()->strokeActive());QCOMPARE(canvas.project().toJson(),snapshot);
        QTest::keyClick(view,Qt::Key_Right);QCOMPARE(review->currentFrame(),1);QTest::keyClick(view,Qt::Key_Escape);QCOMPARE(review->mode(),ReviewMode::Shot);
    }
};
QTEST_MAIN(MovieReviewControllerTests)
#include "movie_review_controller_tests.moc"
