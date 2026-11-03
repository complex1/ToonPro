#include "auto_actions_panel.h"
#include "auto_actions.h"
#include "document.h"
#include "raster_tools.h"
#include "ui_theme.h"
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QSignalSpy>
#include <QStyle>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>
using namespace toon;
namespace {
QToolButton *control(AutoActionsPanel &panel,const char *id) {return panel.findChild<QToolButton*>(id);}
Project fixture(){auto p=Project::create();p.width=320;p.height=240;p.transparent=true;return p;}
}
class AutoActionsPanelTests:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void recordAndReplayWithWorkspaceRouting() {
        QTemporaryDir dir;Document doc;doc.load(fixture());AutoActions actions(&doc,dir.filePath("actions.json"));QVERIFY(actions.load());
        const auto id=actions.create("Ink setup");QVERIFY(!id.isEmpty());AutoActionsPanel panel(&actions);panel.show();panel.selectAction(id);
        QTest::mouseClick(control(panel,"autoActionRecord"),Qt::LeftButton);QVERIFY(actions.recording());
        QVERIFY(!panel.findChild<QComboBox*>("autoActionSelector")->isEnabled());QVERIFY(!control(panel,"autoActionPlay")->isEnabled());
        doc.addLayer();doc.changeLayer("Ink",true,false,false,.6);
        QTest::mouseClick(control(panel,"autoActionStop"),Qt::LeftButton);QVERIFY(!actions.recording());
        auto *steps=panel.findChild<QListWidget*>("autoActionSteps");QCOMPARE(steps->count(),actions.action(id)->steps.size());QVERIFY(steps->count()>=2);
        QVERIFY(steps->item(steps->count()-1)->toolTip().contains("Ink"));QVERIFY(steps->item(steps->count()-1)->toolTip().contains("60%"));
        const int count=doc.history()->count();QSignalSpy requested(&panel,&AutoActionsPanel::playRequested);
        QTest::mouseClick(control(panel,"autoActionPlay"),Qt::LeftButton);QCOMPARE(requested.count(),1);QCOMPARE(requested.first().first().toString(),id);
        QCOMPARE(doc.history()->count(),count); // panel must not bypass workspace guards
        connect(&panel,&AutoActionsPanel::playRequested,&actions,[&](const QString &action){QVERIFY(actions.play(action));});
        QTest::mouseClick(control(panel,"autoActionPlay"),Qt::LeftButton);QCOMPARE(doc.history()->count(),count+1);
        QCOMPARE(doc.activeShot().layers[doc.activeLayer()].name,QString("Ink"));
        QVERIFY(std::abs(doc.activeShot().layers[doc.activeLayer()].opacity-.6)<.001);
    }
    void stepChecksAndContextReordering() {
        QTemporaryDir dir;Document doc;doc.load(fixture());AutoActions actions(&doc,dir.filePath("actions.json"));
        const auto id=actions.create("Setup");QVERIFY(actions.startRecording(id));doc.addLayer();doc.changeLayer("Ink",true,false,false,1);actions.stopRecording();
        AutoActionsPanel panel(&actions);panel.resize(240,280);panel.show();panel.selectAction(id);
        auto *steps=panel.findChild<QListWidget*>("autoActionSteps");QVERIFY(steps->count()>=2);
        steps->item(0)->setCheckState(Qt::Unchecked);QVERIFY(!actions.action(id)->steps[0].enabled);
        steps->item(0)->setCheckState(Qt::Checked);QVERIFY(actions.action(id)->steps[0].enabled);
        const auto firstOperation=actions.action(id)->steps[0].operation;bool clicked=false;
        QTimer::singleShot(0,[&]{for(auto *widget:QApplication::topLevelWidgets())if(auto *menu=qobject_cast<QMenu*>(widget);menu&&menu->isVisible())
            for(auto *action:menu->actions())if(action->text()=="Move down"){clicked=true;QTest::mouseClick(menu,Qt::LeftButton,Qt::NoModifier,menu->actionGeometry(action).center());return;}});
        QTimer::singleShot(2000,&panel,[]{if(auto *popup=QApplication::activePopupWidget())popup->close();});
        emit steps->customContextMenuRequested(steps->visualItemRect(steps->item(0)).center());
        QVERIFY(clicked);QCOMPARE(actions.action(id)->steps[1].operation,firstOperation);
        QVERIFY(actions.save());AutoActions loaded(&doc,actions.path());QVERIFY(loaded.load());QCOMPARE(loaded.action(id)->steps[1].operation,firstOperation);
    }
    void availabilityAndEmptyRecording() {
        QTemporaryDir dir;Document doc;AutoActions actions(&doc,dir.filePath("actions.json"));const auto id=actions.create("Empty");
        AutoActionsPanel panel(&actions);panel.show();QVERIFY(!control(panel,"autoActionRecord")->isEnabled());QVERIFY(!control(panel,"autoActionPlay")->isEnabled());
        doc.load(fixture());QTRY_VERIFY(control(panel,"autoActionRecord")->isEnabled());
        doc.addLayer();QTRY_VERIFY(control(panel,"autoActionRecord")->isEnabled());
        panel.setInteractionEnabled(false);QVERIFY(!control(panel,"autoActionRecord")->isEnabled());QVERIFY(!control(panel,"autoActionAdd")->isEnabled());
        panel.setInteractionEnabled(true);QTest::mouseClick(control(panel,"autoActionRecord"),Qt::LeftButton);QVERIFY(actions.recording());
        panel.setInteractionEnabled(false);QVERIFY(control(panel,"autoActionStop")->isEnabled());
        QTest::mouseClick(control(panel,"autoActionStop"),Qt::LeftButton);QVERIFY(!actions.recording());QVERIFY(actions.action(id)->steps.isEmpty());
        panel.setInteractionEnabled(true);QVERIFY(!control(panel,"autoActionPlay")->isEnabled());
    }
    void fillTransformDetailsAndRecordingState() {
        QTemporaryDir dir;Document doc;doc.load(fixture());AutoActions actions(&doc,dir.filePath("actions.json"));
        QPainterPath region;region.addRect(40,40,40,30);doc.setRasterSelection(region);
        const auto id=actions.create("Color and position");AutoActionsPanel panel(&actions);panel.selectAction(id);
        QVERIFY(actions.startRecording(id));QVERIFY(doc.fillRasterSelection(QColor("#cc3366")));
        QVERIFY(doc.beginTransform());TransformParameters t;t.translation={25,15};t.scaleX=1.25;t.rotation=10;
        QVERIFY(doc.previewTransform(t));QVERIFY(actions.recording());QCOMPARE(actions.action(id)->steps.size(),1);
        QVERIFY(doc.applyTransform());QVERIFY(actions.recording());actions.stopRecording();
        auto *steps=panel.findChild<QListWidget*>("autoActionSteps");QCOMPARE(steps->count(),2);
        QVERIFY(steps->item(0)->toolTip().contains("#cc3366"));QVERIFY(steps->item(1)->toolTip().contains("125%"));
        QVERIFY(steps->item(1)->toolTip().contains("25, 15 px"));QVERIFY(control(panel,"autoActionPlay")->isEnabled());
        const auto output=qEnvironmentVariable("TOON_AUTO_ACTIONS_PREVIEW");
        if(!output.isEmpty()){QVERIFY(QDir().mkpath(output));panel.resize(240,260);panel.show();QVERIFY(QTest::qWaitForWindowExposed(&panel));QVERIFY(panel.grab().save(output+"/fill-transform.png"));}
    }
    void libraryChangesAndCompactLayout() {
        QTemporaryDir dir;Document doc;doc.load(fixture());AutoActions actions(&doc,dir.filePath("actions.json"));AutoActionsPanel panel(&actions);
        const auto id=actions.create("Character layer setup");panel.selectAction(id);QCOMPARE(panel.selectedActionId(),id);
        QVERIFY(actions.rename(id,"Ink and color"));QCOMPARE(panel.findChild<QComboBox*>("autoActionSelector")->currentText(),QString("Ink and color"));
        const auto copy=actions.duplicate(id);QVERIFY(!copy.isEmpty());panel.selectAction(copy);QCOMPARE(panel.selectedActionId(),copy);
        QVERIFY(actions.remove(copy));QCOMPARE(panel.selectedActionId(),id);
        QVERIFY(actions.startRecording(id));doc.addLayer();doc.changeLayer("Line art",true,false,false,.75);actions.stopRecording();
        panel.resize(220,260);panel.show();QVERIFY(QTest::qWaitForWindowExposed(&panel));
        for(const auto *name:{"autoActionAdd","autoActionMore","autoActionRecord","autoActionStop","autoActionPlay"}) {
            auto *button=control(panel,name);QVERIFY(button);QVERIFY(!button->icon().isNull());QCOMPARE(button->iconSize(),QSize(18,18));
            QVERIFY(!button->toolTip().isEmpty());QVERIFY(!button->accessibleName().isEmpty());
            QCOMPARE(button->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,button),ui::tooltipDelayMs);
            const auto topLeft=button->mapTo(&panel,QPoint());QVERIFY(topLeft.x()>=0);QVERIFY(topLeft.x()+button->width()<=panel.width());
        }
        const auto output=qEnvironmentVariable("TOON_AUTO_ACTIONS_PREVIEW");
        if(!output.isEmpty()){QVERIFY(QDir().mkpath(output));QVERIFY(panel.grab().save(output+"/actions.png"));panel.resize(190,180);QApplication::processEvents();QVERIFY(panel.grab().save(output+"/actions-narrow.png"));}
    }
};
QTEST_MAIN(AutoActionsPanelTests)
#include "test_auto_actions_panel.moc"
