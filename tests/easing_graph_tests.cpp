#include "easing_graph_widget.h"
#include "ui_theme.h"
#include <QFocusEvent>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QStyle>
#include <QtTest>
using namespace toon;
namespace {
void pointer(EasingGraphWidget &graph, QEvent::Type type, QPointF point, Qt::MouseButtons buttons = Qt::LeftButton) {
    QMouseEvent event(type, point, graph.mapToGlobal(point), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : buttons, Qt::NoModifier);
    QApplication::sendEvent(&graph, &event);
}
}
class EasingGraphTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void dragPreviewsAndFinishesOnce() {
        EasingGraphWidget graph; graph.resize(230,174); graph.show(); QCoreApplication::processEvents();
        QSignalSpy start(&graph,&EasingGraphWidget::editingStarted), change(&graph,&EasingGraphWidget::curveChanged), finish(&graph,&EasingGraphWidget::editingFinished);
        const auto from=graph.handlePosition(0),to=from+QPointF(22,-18);
        pointer(graph,QEvent::MouseButtonPress,from); pointer(graph,QEvent::MouseMove,to);
        QCOMPARE(start.count(),1); QCOMPARE(change.count(),1); QCOMPARE(finish.count(),0); QVERIFY(graph.editing());
        pointer(graph,QEvent::MouseButtonRelease,to); QCOMPARE(finish.count(),1); QVERIFY(!graph.editing());
        QVERIFY(graph.firstControl().x()>1./3); QVERIFY(graph.firstControl().y()>1./3);
    }
    void escapeRestoresControlsWithoutFinishing() {
        EasingGraphWidget graph; graph.resize(230,174); graph.show(); QCoreApplication::processEvents();
        const auto first=graph.firstControl(),second=graph.secondControl(); QSignalSpy finish(&graph,&EasingGraphWidget::editingFinished), cancel(&graph,&EasingGraphWidget::editingCancelled);
        const auto from=graph.handlePosition(1); pointer(graph,QEvent::MouseButtonPress,from); pointer(graph,QEvent::MouseMove,from+QPointF(-30,40));
        QTest::keyClick(&graph,Qt::Key_Escape); QCOMPARE(cancel.count(),1); QCOMPARE(finish.count(),0);
        QCOMPARE(graph.firstControl(),first); QCOMPARE(graph.secondControl(),second); QVERIFY(!graph.editing());
        pointer(graph,QEvent::MouseButtonRelease,from); QCOMPARE(finish.count(),0);
    }
    void refreshDoesNotReplaceActiveDrag() {
        EasingGraphWidget graph; graph.resize(230,174); graph.show(); QCoreApplication::processEvents();
        const auto from=graph.handlePosition(0); pointer(graph,QEvent::MouseButtonPress,from); pointer(graph,QEvent::MouseMove,from+QPointF(25,-30));
        const auto active=graph.firstControl(); graph.setCurve({0,0},{1,1}); QCOMPARE(graph.firstControl(),active);
        graph.finishEditing(); graph.setCurve({0,0},{1,1}); QCOMPARE(graph.firstControl(),QPointF(0,0)); QCOMPARE(graph.secondControl(),QPointF(1,1));
    }
    void handlesClampAndMissingReleaseFinishes() {
        EasingGraphWidget graph; graph.resize(230,174); graph.show(); QCoreApplication::processEvents();
        QSignalSpy finish(&graph,&EasingGraphWidget::editingFinished);
        pointer(graph,QEvent::MouseButtonPress,graph.handlePosition(0)); pointer(graph,QEvent::MouseMove,{-1000,-1000});
        QCOMPARE(graph.firstControl(),QPointF(0,1)); pointer(graph,QEvent::MouseMove,{-1000,-1000},Qt::NoButton);
        QCOMPARE(finish.count(),1); QVERIFY(!graph.editing());
        pointer(graph,QEvent::MouseButtonRelease,{-1000,-1000}); QCOMPARE(finish.count(),1);
    }
    void keyboardAndAccessibility() {
        EasingGraphWidget graph; graph.resize(230,174); graph.show(); graph.setFocus(); QCoreApplication::processEvents();
        QVERIFY(!graph.accessibleName().isEmpty()); QVERIFY(!graph.toolTip().isEmpty()); QCOMPARE(graph.style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,&graph),600);
        graph.setSegment(0,12,20,100,"px"); QVERIFY(graph.accessibleDescription().contains("Frames 1 to 13"));
        const auto first=graph.firstControl(),second=graph.secondControl(); QSignalSpy finish(&graph,&EasingGraphWidget::editingFinished);
        QTest::keyClick(&graph,Qt::Key_Up); QVERIFY(qAbs(graph.firstControl().y()-first.y()-.01)<1e-9); QCOMPARE(finish.count(),1);
        QTest::keyClick(&graph,Qt::Key_Tab); QCOMPARE(graph.selectedHandle(),1); QTest::keyClick(&graph,Qt::Key_Left,Qt::ShiftModifier);
        QVERIFY(qAbs(graph.secondControl().x()-second.x()+.1)<1e-9); QCOMPARE(finish.count(),2);
    }
    void unrelatedClicksDoNotStartEdits() {
        EasingGraphWidget graph; graph.resize(230,174); graph.show(); QCoreApplication::processEvents(); QSignalSpy start(&graph,&EasingGraphWidget::editingStarted);
        pointer(graph,QEvent::MouseButtonPress,{3,3}); pointer(graph,QEvent::MouseButtonRelease,{3,3}); QCOMPARE(start.count(),0);
        graph.setEnabled(false); pointer(graph,QEvent::MouseButtonPress,graph.handlePosition(0)); QCOMPARE(start.count(),0);
    }
};
QTEST_MAIN(EasingGraphTests)
#include "easing_graph_tests.moc"
