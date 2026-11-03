#include "history_panel.h"
#include "document.h"
#include "ui_theme.h"
#include <QDir>
#include <QListWidget>
#include <QSignalSpy>
#include <QStyle>
#include <QToolButton>
#include <QUndoCommand>
#include <QtTest>
using namespace toon;
namespace {
QListWidget *entries(HistoryPanel &panel) { return panel.findChild<QListWidget *>("historyEntries"); }
void add(Document &doc,const QString &label) { doc.history()->push(new QUndoCommand(label)); }
void clickRow(HistoryPanel &panel,int row) {
    auto *list=entries(panel);list->scrollToItem(list->item(row));
    QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,list->visualItemRect(list->item(row)).center());
}
Project project() { auto p=Project::create();p.width=320;p.height=240;return p; }
}
class HistoryPanelTests:public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void emptyAndLoadedState() {
        Document doc;HistoryPanel panel(&doc);auto *list=entries(panel);
        QVERIFY(list);QCOMPARE(list->count(),1);QCOMPARE(list->item(0)->text(),QString("No project"));QVERIFY(!list->isEnabled());
        doc.load(project());QVERIFY(list->isEnabled());QCOMPARE(list->count(),1);
        QVERIFY(list->item(0)->text().startsWith("Initial state"));QVERIFY(list->item(0)->text().contains("Saved"));
        QCOMPARE(panel.findChild<QToolButton *>("historyUndo")->iconSize(),QSize(18,18));
        QCOMPARE(panel.style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay),ui::tooltipDelayMs);
    }
    void clickRequestsAndTracksConfirmedState() {
        Document doc;doc.load(project());HistoryPanel panel(&doc);panel.resize(240,260);panel.show();
        add(doc,"New layer");add(doc,"Draw");add(doc,"Opacity");
        QSignalSpy requests(&panel,&HistoryPanel::requestHistoryIndex);auto *list=entries(panel);
        QCOMPARE(list->count(),4);QCOMPARE(list->currentRow(),3);
        // Without the MainWindow resolver, selection alone cannot mutate artwork.
        clickRow(panel,1);QCOMPARE(requests.count(),1);QCOMPARE(requests.last().first().toInt(),1);
        QCOMPARE(doc.history()->index(),3);QCOMPARE(list->currentRow(),3);
        connect(&panel,&HistoryPanel::requestHistoryIndex,doc.history(),&QUndoStack::setIndex);
        clickRow(panel,1);QCOMPARE(doc.history()->index(),1);QCOMPARE(list->currentRow(),1);
        QCOMPARE(list->item(2)->foreground().color(),QColor(ui::disabled));
        QVERIFY(!list->item(1)->icon().isNull());
        QTest::mouseClick(panel.findChild<QToolButton *>("historyRedo"),Qt::LeftButton);QCOMPARE(doc.history()->index(),2);
        QTest::mouseClick(panel.findChild<QToolButton *>("historyUndo"),Qt::LeftButton);QCOMPARE(doc.history()->index(),1);
        list->setFocus();QTest::keyClick(list,Qt::Key_End);QCOMPARE(doc.history()->index(),3);
        QTest::keyClick(list,Qt::Key_Home);QCOMPARE(doc.history()->index(),0);
    }
    void savedMarkerBranchAndReset() {
        Document doc;doc.load(project());HistoryPanel panel(&doc);auto *list=entries(panel);
        add(doc,"First");doc.history()->setClean();add(doc,"Second");
        QVERIFY(list->item(1)->text().endsWith("Saved"));QVERIFY(!list->item(2)->text().contains("Saved"));
        doc.history()->setIndex(0);add(doc,"Replacement");QCOMPARE(list->count(),2);
        QCOMPARE(list->item(1)->text(),QString("Replacement"));QCOMPARE(list->item(0)->text(),QString("Initial state"));
        doc.load(project());QCOMPARE(list->count(),1);QVERIFY(list->item(0)->text().startsWith("Initial state"));
        QVERIFY(list->item(0)->text().contains("Saved"));
    }
    void pruningLabelsReachableBaseline() {
        Document doc;doc.load(project());HistoryPanel panel(&doc);auto *list=entries(panel);
        for(int i=0;i<55;++i)add(doc,QString("Edit %1").arg(i));
        QCOMPARE(doc.history()->count(),50);QCOMPARE(list->count(),51);
        QCOMPARE(list->item(1)->text(),QString("Edit 5"));
        QCOMPARE(list->item(0)->text(),QString("Oldest retained state"));
        doc.history()->clear();QVERIFY(list->item(0)->text().startsWith("Oldest retained state"));
        doc.load(project());QVERIFY(list->item(0)->text().startsWith("Initial state"));
    }
    void busyAndLiveStrokeDoNotNavigateOrRebuild() {
        Document doc;doc.load(project());HistoryPanel panel(&doc);panel.resize(240,260);panel.show();
        add(doc,"First");add(doc,"Second");auto *list=entries(panel);auto *first=list->item(1);
        QSignalSpy requests(&panel,&HistoryPanel::requestHistoryIndex),blocked(&panel,&HistoryPanel::navigationBlocked);
        BrushSettings brush;brush.size=8;
        QVERIFY(doc.beginStroke({30,30},1,brush,false));
        for(int i=0;i<20;++i)doc.continueStroke({30.+i*2,35.},1);
        QCOMPARE(list->item(1),first);QCOMPARE(list->count(),3);
        clickRow(panel,0);QCOMPARE(requests.count(),0);QCOMPARE(blocked.count(),1);QVERIFY(doc.strokeActive());
        QCOMPARE(doc.history()->index(),2);doc.cancelStroke();
        panel.setInteractionEnabled(false);QVERIFY(!list->isEnabled());
        QTest::mouseClick(panel.findChild<QToolButton *>("historyUndo"),Qt::LeftButton);QCOMPARE(requests.count(),0);
        panel.setInteractionEnabled(true);clickRow(panel,0);QCOMPARE(requests.count(),1);
        QVERIFY(doc.beginLayerPropertyEdit());clickRow(panel,0);QCOMPARE(requests.count(),1);doc.cancelLayerPropertyEdit();
    }
    void compactPreview() {
        Document doc;doc.load(project());HistoryPanel panel(&doc);panel.resize(220,260);
        add(doc,"New raster layer");add(doc,"Brush stroke");doc.history()->setClean();
        add(doc,"Layer opacity");add(doc,"A long descriptive edit label which must remain reachable in its tooltip");doc.history()->setIndex(3);
        panel.show();QTest::qWait(30);
        const auto output=qEnvironmentVariable("TOON_HISTORY_PREVIEW");
        if(!output.isEmpty()){QVERIFY(QDir().mkpath(output));QVERIFY(panel.grab().save(output+"/history.png"));}
        QVERIFY(entries(panel)->viewport()->width()>160);
        QVERIFY(!entries(panel)->item(4)->toolTip().isEmpty());
    }
};
QTEST_MAIN(HistoryPanelTests)
#include "test_history_panel.moc"
