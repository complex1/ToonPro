#include "cel_paint_check.h"
#include "cel_paint_check_panel.h"
#include "canvas.h"
#include "document.h"
#include "reference_panel.h"
#include <QComboBox>
#include <QElapsedTimer>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSignalSpy>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

using namespace toon;
namespace {
QImage artwork() {
    QImage image(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
    QPainter painter(&image);painter.fillRect(QRect(5,5,30,30),Qt::red);painter.fillRect(QRect(0,40,8,24),Qt::blue);
    painter.fillRect(QRect(50,20,2,1),Qt::red);painter.fillRect(QRect(20,20,3,3),Qt::green);
    painter.setCompositionMode(QPainter::CompositionMode_Clear);painter.fillRect(QRect(10,10,2,2),Qt::transparent);return image;
}
Project projectWithPainting(bool second=false) {
    auto project=Project::create();project.width=64;project.height=64;
    project.shots[0].layers[0].tiles[{0,0}]=artwork();
    if(second){auto layer=RasterLayer::create("Colors 2");layer.tiles[{0,0}]=artwork();project.shots[0].layers.append(layer);}
    return project;
}
QVector<CelPaintCheckTarget> targets(const Project &project) {
    QVector<CelPaintCheckTarget>result;
    for(const auto &layer:project.shots[0].layers)result.append({project.shots[0].id,layer.id,layer.name,QRect(0,0,project.width,project.height),layer,{}});
    return result;
}
QVector<int> indices(const CelPaintCheckResult &result,CelPaintIssueKind kind) {
    QVector<int>resultIndices;for(int i=0;i<result.issues.size();++i)if(result.issues[i].kind==kind)resultIndices.append(i);return resultIndices;
}
CelPaintCheckSettings settings() {CelPaintCheckSettings result;result.approvedColors={Qt::red,Qt::blue,Qt::black};return result;}
}
class CelPaintCheckTests:public QObject {
    Q_OBJECT
private slots:
    void findsSmallHolesStraysEdgeLeaksAndUnapprovedColors() {
        const auto result=inspectCelPaint(targets(projectWithPainting()),settings());
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QVERIFY(!result.cancelled);
        QCOMPARE(indices(result,CelPaintIssueKind::Hole).size(),1);
        QCOMPARE(indices(result,CelPaintIssueKind::Stray).size(),1);
        QCOMPARE(indices(result,CelPaintIssueKind::Leak).size(),1);
        QCOMPARE(indices(result,CelPaintIssueKind::OutsidePalette).size(),1);
        const auto &hole=result.issues[indices(result,CelPaintIssueKind::Hole).first()];QCOMPARE(hole.pixelCount,4);QCOMPARE(hole.bounds,QRect(10,10,2,2));
        QCOMPARE(result.issues[indices(result,CelPaintIssueKind::Stray).first()].pixelCount,2);
        QCOMPARE(result.issues[indices(result,CelPaintIssueKind::OutsidePalette).first()].pixelCount,9);
    }
    void antialiasMixturesAndSemiTransparentEdgesAreNotFlatPaletteErrors() {
        auto project=projectWithPainting();auto &image=project.shots[0].layers[0].tiles[{0,0}];image.fill(Qt::transparent);
        {QPainter painter(&image);painter.fillRect(QRect(5,5,54,54),Qt::white);painter.fillRect(QRect(5,5,21,54),Qt::black);painter.fillRect(QRect(26,5,1,54),QColor(127,127,127));painter.fillRect(QRect(40,20,3,3),Qt::red);}
        image.setPixel(55,55,qPremultiply(qRgba(127,127,127,100)));
        auto options=settings();options.approvedColors={Qt::white,Qt::black};options.tolerance=4;
        const auto result=inspectCelPaint(targets(project),options);QVERIFY(result.error.isEmpty());
        const auto off=indices(result,CelPaintIssueKind::OutsidePalette);QCOMPARE(off.size(),1);QCOMPARE(result.issues[off.first()].pixelCount,9);
    }
    void reportsLargeEnclosedRegionsWithoutPreparingAnUnsafeFill() {
        auto project=projectWithPainting();auto &image=project.shots[0].layers[0].tiles[{0,0}];image.fill(Qt::transparent);
        {QPainter painter(&image);painter.setPen(QPen(Qt::black,2));painter.drawRect(QRect(5,5,50,50));}
        const auto result=inspectCelPaint(targets(project),settings());const auto holes=indices(result,CelPaintIssueKind::Hole);QCOMPARE(holes.size(),1);
        QVERIFY(result.issues[holes.first()].pixelCount>256);QVERIFY(!result.issues[holes.first()].repairable);
        const auto preview=prepareCelPaintRepairs(result,holes,Qt::red);QVERIFY(!preview.error.isEmpty());
    }
    void repairPreviewLeavesOriginalsUntouchedAndApplyIsOneUndoForAllDrawings() {
        Document document;document.load(projectWithPainting(true));const auto original=document.project();
        const auto check=inspectCelPaint(targets(original),settings());QVector<int> chosen;
        for(int i=0;i<check.issues.size();++i)chosen.append(i);
        const auto repairs=prepareCelPaintRepairs(check,chosen,Qt::yellow);QVERIFY2(repairs.error.isEmpty(),qPrintable(repairs.error));QCOMPARE(repairs.drawings.size(),2);
        QCOMPARE(document.history()->count(),0);QCOMPARE(document.project().shots[0].layers[0].tiles,original.shots[0].layers[0].tiles);
        const auto &preview=repairs.drawings[0].preview;QCOMPARE(preview.pixelColor(10,10),QColor(Qt::yellow));QCOMPARE(preview.pixelColor(50,20).alpha(),0);QCOMPARE(preview.pixelColor(0,50).alpha(),0);QVERIFY(preview.pixelColor(20,20)!=QColor(Qt::green));
        QString error;QVERIFY2(applyCelPaintRepairs(&document,repairs,&error),qPrintable(error));QCOMPARE(document.history()->count(),1);
        QCOMPARE(document.project().shots[0].layers[0].tiles.value(TileKey(0,0)).pixelColor(10,10),QColor(Qt::yellow));
        QCOMPARE(document.project().shots[0].layers[1].tiles.value(TileKey(0,0)).pixelColor(10,10),QColor(Qt::yellow));
        document.history()->undo();for(int i=0;i<2;++i)QCOMPARE(document.project().shots[0].layers[i].tiles,original.shots[0].layers[i].tiles);
        document.history()->redo();QCOMPARE(document.project().shots[0].layers[1].tiles.value(TileKey(0,0)).pixelColor(10,10),QColor(Qt::yellow));
    }
    void staleOrLockedTargetRejectsWholeBatch() {
        Document document;document.load(projectWithPainting(true));const auto check=inspectCelPaint(targets(document.project()),settings());
        const auto repairs=prepareCelPaintRepairs(check,indices(check,CelPaintIssueKind::Hole),Qt::yellow);
        auto changed=document.project();changed.shots[0].layers[1].tiles[{0,0}].setPixelColor(10,10,Qt::cyan);document.replaceProject(changed,"Other edit");
        QString error;const int history=document.history()->count();QVERIFY(!applyCelPaintRepairs(&document,repairs,&error));QVERIFY(error.contains("changed"));QCOMPARE(document.history()->count(),history);
        QCOMPARE(document.project().shots[0].layers[0].tiles.value(TileKey(0,0)).pixelColor(10,10).alpha(),0);
        document.load(projectWithPainting(true));const auto lockedCheck=inspectCelPaint(targets(document.project()),settings());const auto lockedRepairs=prepareCelPaintRepairs(lockedCheck,indices(lockedCheck,CelPaintIssueKind::Hole),Qt::yellow);
        document.setLayersLocked({document.project().shots[0].layers[1].id},true);const int lockedHistory=document.history()->count();QVERIFY(!applyCelPaintRepairs(&document,lockedRepairs,&error));QVERIFY(error.contains("Unlock"));QCOMPARE(document.history()->count(),lockedHistory);
    }
    void activeGestureAndAlphaLockCannotLoseArtwork() {
        Document document;document.load(projectWithPainting());const auto check=inspectCelPaint(targets(document.project()),settings());const auto repairs=prepareCelPaintRepairs(check,indices(check,CelPaintIssueKind::Hole),Qt::yellow);
        BrushSettings brush;QVERIFY(document.beginStroke({40,40},1,brush,false));QString error;QVERIFY(!applyCelPaintRepairs(&document,repairs,&error));QVERIFY(document.strokeActive());document.cancelStroke();
        document.setLayerAlphaLocked(true);QVERIFY(!applyCelPaintRepairs(&document,repairs,&error));QVERIFY(error.contains("Unlock"));
    }
    void cancellationAndMemoryBudgetsAreBounded() {
        auto cancellation=std::make_shared<std::atomic_bool>(true);const auto check=inspectCelPaint(targets(projectWithPainting()),settings(),cancellation);QVERIFY(check.cancelled);
        auto source=targets(projectWithPainting());source[0].bounds=QRect(0,0,8192,8192);QVERIFY(!inspectCelPaint(source,settings()).error.isEmpty());
        source=targets(projectWithPainting());source.append(source.first());QVERIFY(!inspectCelPaint(source,settings()).error.isEmpty());
        const auto valid=inspectCelPaint(targets(projectWithPainting()),settings());const auto cancelledPreview=prepareCelPaintRepairs(valid,indices(valid,CelPaintIssueKind::Hole),Qt::yellow,cancellation);QVERIFY(cancelledPreview.cancelled);
    }
    void panelRunsInBackgroundAndCancelDiscardsOnlyThePreview() {
        Canvas canvas;canvas.resize(600,500);canvas.setProject(projectWithPainting());canvas.setBrushColor(Qt::yellow);
        CelPaintCheckPanel panel(&canvas);panel.resize(240,600);panel.show();canvas.show();
        QSignalSpy scanned(&panel,&CelPaintCheckPanel::inspectionFinished);panel.startCheck();QTRY_COMPARE_WITH_TIMEOUT(scanned.count(),1,10000);
        auto *list=panel.findChild<QListWidget*>("celPaintCheckProblems");QVERIFY(list);QVERIFY(list->count()>0);
        for(int i=0;i<list->count();++i)QCOMPARE(list->item(i)->checkState(),Qt::Unchecked);
        const auto holes=indices(panel.inspection(),CelPaintIssueKind::Hole);QVERIFY(!holes.isEmpty());list->item(holes.first())->setCheckState(Qt::Checked);list->setCurrentRow(holes.first());
        const auto before=canvas.document()->project().shots[0].layers[0].tiles;
        QSignalSpy prepared(&panel,&CelPaintCheckPanel::previewFinished);panel.previewRepairs();QVERIFY(panel.pending());QTRY_COMPARE_WITH_TIMEOUT(prepared.count(),1,10000);
        auto *preview=panel.findChild<ReferenceView*>("celPaintCheckPreview");QVERIFY(preview);QCOMPARE(preview->image().pixelColor(10,10),QColor(Qt::yellow));
        QCOMPARE(canvas.document()->history()->count(),0);QCOMPARE(canvas.document()->project().shots[0].layers[0].tiles,before);
        panel.cancelEditing();QVERIFY(!panel.pending());QCOMPARE(preview->image().pixelColor(10,10).alpha(),0);QCOMPARE(canvas.document()->history()->count(),0);
    }
    void documentChangesInvalidatePendingResultsAndRepairs() {
        Canvas canvas;canvas.setProject(projectWithPainting());CelPaintCheckPanel panel(&canvas);
        QSignalSpy scanned(&panel,&CelPaintCheckPanel::inspectionFinished);panel.startCheck();QTRY_COMPARE_WITH_TIMEOUT(scanned.count(),1,10000);
        auto *list=panel.findChild<QListWidget*>("celPaintCheckProblems");const int hole=indices(panel.inspection(),CelPaintIssueKind::Hole).first();list->item(hole)->setCheckState(Qt::Checked);
        QSignalSpy prepared(&panel,&CelPaintCheckPanel::previewFinished);panel.previewRepairs();QTRY_COMPARE_WITH_TIMEOUT(prepared.count(),1,10000);QVERIFY(panel.pending());
        canvas.document()->addLayer();QVERIFY(!panel.pending());QVERIFY(panel.inspection().issues.isEmpty());QVERIFY(!panel.applyRepairs());
    }
    void animatedSourceInspectionHighlightsTheDisplayedPositionAndSourceMode() {
        for(bool sourceMode:{false,true}) {
            Canvas canvas;canvas.resize(600,500);canvas.setProject(projectWithPainting());
            auto *document=canvas.document();const auto id=document->activeShot().layers[0].id;
            QVERIFY(document->setKeyframeValue(id,KeyframeProperty::PositionX,30));
            QVERIFY(document->setKeyframeValue(id,KeyframeProperty::PositionY,20));
            document->setKeyframeSourceEditing(sourceMode);canvas.setZoom(4);canvas.show();
            CelPaintCheckPanel panel(&canvas);panel.show();QSignalSpy scanned(&panel,&CelPaintCheckPanel::inspectionFinished);panel.startCheck();QTRY_COMPARE_WITH_TIMEOUT(scanned.count(),1,10000);
            const auto hole=indices(panel.inspection(),CelPaintIssueKind::Hole).first();auto *list=panel.findChild<QListWidget*>("celPaintCheckProblems");list->setCurrentRow(hole);
            const auto origin=sheetGeometry(canvas.project(),canvas.shot()).output.topLeft();
            QCOMPARE(canvas.center(),origin+QPointF(sourceMode?10:40,sourceMode?10:30));
            auto *overlay=canvas.findChild<QWidget*>("celPaintCheckOverlay");QVERIFY(overlay);QVERIFY(overlay->isVisible());
            QImage image(canvas.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
            {QPainter painter(&image);overlay->render(&painter,QPoint(),QRegion(),QWidget::DrawChildren);}
            // The filled suspect pixels remain under the center after the
            // source layer's displayed transform or source-artwork switch.
            QVERIFY(image.pixelColor(canvas.width()/2+2,canvas.height()/2+2).alpha()>0);
            QVERIFY(image.pixelColor(canvas.width()/2-100,canvas.height()/2-80).alpha()==0);
        }
    }
    void changingTheArtworkFrameRejectsAPreparedRepair() {
        Document document;document.load(projectWithPainting());const auto check=inspectCelPaint(targets(document.project()),settings());
        const auto repairs=prepareCelPaintRepairs(check,indices(check,CelPaintIssueKind::Hole),Qt::yellow);
        auto project=document.project();project.width=128;document.replaceProject(project,"Resize artwork frame");
        const auto history=document.history()->count();QString error;QVERIFY(!applyCelPaintRepairs(&document,repairs,&error));
        QVERIFY(error.contains("frame changed"));QCOMPARE(document.history()->count(),history);
    }
    void canvasCanBeDestroyedBeforeItsPanelAndHiddenPanelsLeaveNoHighlight() {
        auto *canvas=new Canvas;canvas->resize(600,500);canvas->setProject(projectWithPainting());canvas->show();
        auto *panel=new CelPaintCheckPanel(canvas);panel->show();
        QSignalSpy scanned(panel,&CelPaintCheckPanel::inspectionFinished);panel->startCheck();QTRY_COMPARE_WITH_TIMEOUT(scanned.count(),1,10000);
        auto *overlay=canvas->findChild<QWidget*>("celPaintCheckOverlay");QVERIFY(overlay);QVERIFY(overlay->isVisible());panel->hide();QVERIFY(!overlay->isVisible());
        delete canvas;delete panel;
    }
    void workerCompletionWaitsForPenUpAndRejectsChangedStroke() {
        Canvas canvas;auto project=projectWithPainting();project.width=1024;project.height=1024;canvas.setProject(project);CelPaintCheckPanel panel(&canvas);
        QSignalSpy scanned(&panel,&CelPaintCheckPanel::inspectionFinished);panel.startCheck();
        BrushSettings brush;QVERIFY(canvas.document()->beginStroke({400,400},1,brush,false));QTest::qWait(150);
        QCOMPARE(scanned.count(),0);QVERIFY(panel.busy());canvas.document()->continueStroke({420,420},1);canvas.document()->endStroke();
        QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),10000);QVERIFY(panel.inspection().issues.isEmpty());QCOMPARE(scanned.count(),0);
    }
    void penContactOnlyHidesHighlightAndPenUpInvalidatesRepairPreview() {
        Canvas canvas;canvas.resize(600,500);canvas.setProject(projectWithPainting());canvas.setBrushColor(Qt::yellow);canvas.show();
        CelPaintCheckPanel panel(&canvas);panel.show();QSignalSpy scanned(&panel,&CelPaintCheckPanel::inspectionFinished);panel.startCheck();QTRY_COMPARE_WITH_TIMEOUT(scanned.count(),1,10000);
        const auto hole=indices(panel.inspection(),CelPaintIssueKind::Hole).first();auto *list=panel.findChild<QListWidget*>("celPaintCheckProblems");list->item(hole)->setCheckState(Qt::Checked);list->setCurrentRow(hole);
        QSignalSpy prepared(&panel,&CelPaintCheckPanel::previewFinished);panel.previewRepairs();QTRY_COMPARE_WITH_TIMEOUT(prepared.count(),1,10000);QVERIFY(panel.pending());
        auto *preview=panel.findChild<ReferenceView*>("celPaintCheckPreview");auto *overlay=canvas.findChild<QWidget*>("celPaintCheckOverlay");QVERIFY(preview);QVERIFY(overlay);QVERIFY(overlay->isVisible());
        const auto imageKey=preview->image().cacheKey();const auto issueCount=panel.inspection().issues.size();QSignalSpy pending(&panel,&CelPaintCheckPanel::pendingChanged);
        const auto origin=sheetGeometry(canvas.project(),canvas.shot()).output.topLeft();const auto contact=canvas.viewTransform().map(origin+QPointF(30,30)).toPoint();
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,contact);QVERIFY(canvas.document()->strokeActive());
        QVERIFY(!overlay->isVisible());QVERIFY(panel.pending());QCOMPARE(pending.count(),0);QCOMPARE(preview->image().cacheKey(),imageKey);QCOMPARE(panel.inspection().issues.size(),issueCount);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,contact);QVERIFY(!canvas.document()->strokeActive());
        QVERIFY(!panel.pending());QVERIFY(panel.inspection().issues.isEmpty());QVERIFY(preview->image().isNull());QCOMPARE(pending.count(),1);
    }
    void fourKInspectionKeepsUiEventsMoving() {
        auto project=projectWithPainting();project.width=3840;project.height=2160;Canvas canvas;canvas.setProject(project);CelPaintCheckPanel panel(&canvas);
        QTimer heartbeat;heartbeat.setInterval(1);int ticks=0;connect(&heartbeat,&QTimer::timeout,this,[&]{++ticks;});heartbeat.start();
        QSignalSpy scanned(&panel,&CelPaintCheckPanel::inspectionFinished);QElapsedTimer elapsed;elapsed.start();panel.startCheck();QVERIFY(panel.busy());
        QTRY_COMPARE_WITH_TIMEOUT(scanned.count(),1,30000);QVERIFY(ticks>2);QVERIFY(panel.inspection().error.isEmpty());
        qInfo()<<"4K paint check milliseconds:"<<elapsed.elapsed()<<"UI heartbeats:"<<ticks;
    }
};
QTEST_MAIN(CelPaintCheckTests)
#include "test_cel_paint_check.moc"
