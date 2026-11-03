#include "drawing_template_dialog.h"
#include "drawing_template_library.h"
#include "ui_theme.h"
#include <QComboBox>
#include <QFile>
#include <QLineEdit>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>
using namespace toon;
class DrawingTemplatePanelTests : public QObject {
    Q_OBJECT
private slots:
    void compactDraftTypesNamesOrderAndExplicitSave() {
        applyTheme(); QTemporaryDir dir; DrawingTemplateLibrary library(dir.filePath("templates.json")); QVERIFY(library.load());
        DrawingTemplateWidget widget(&library); widget.resize(280,440); widget.show(); widget.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&widget)); QVERIFY(QTest::qWaitForWindowActive(&widget));
        auto *presets=widget.findChild<QComboBox*>("animationDrawingTemplate"); auto *table=widget.findChild<QTableWidget*>("drawingTemplateLayers");
        QVERIFY(presets&&table); QCOMPARE(presets->currentIndex(),2); QCOMPARE(table->rowCount(),6); QCOMPARE(table->item(0,0)->text(),QString("Line Art"));
        auto *type=qobject_cast<QComboBox*>(table->cellWidget(0,1)); QVERIFY(type); type->setCurrentIndex(type->findData(int(LayerKind::Vector)));
        table->item(0,0)->setText("Ink"); QCOMPARE(widget.templateSlots().last().kind,LayerKind::Vector); QCOMPARE(widget.templateSlots().last().name,QString("Ink"));
        QVERIFY(library.entries().isEmpty()); QVERIFY(!QFile::exists(library.filePath()));
        table->setCurrentCell(0,0); widget.findChild<QToolButton*>("drawingTemplateLayerDown")->click();
        QCOMPARE(widget.templateSlots()[4].name,QString("Ink"));
        widget.findChild<QToolButton*>("drawingTemplateAddLayer")->click(); QCOMPARE(table->rowCount(),7);
        widget.findChild<QToolButton*>("drawingTemplateRemoveLayer")->click(); QCOMPARE(table->rowCount(),6);
        table->item(0,0)->setText(""); QVERIFY(!widget.validTemplate()); table->item(0,0)->setText("Highlights"); QVERIFY(widget.validTemplate());
        widget.findChild<QLineEdit*>("drawingTemplateName")->setText("My character"); auto *save=widget.findChild<QToolButton*>("drawingTemplateSave"); QVERIFY(save->isEnabled()); save->click();
        QCOMPARE(library.entries().size(),1); QCOMPARE(library.entries()[0].layers,widget.templateSlots());
        widget.setBuiltIn(DrawingTemplate::Simple); QCOMPARE(table->rowCount(),1); QVERIFY(!widget.findChild<QToolButton*>("drawingTemplateRemoveLayer")->isEnabled());
        presets->setCurrentIndex(presets->findData(library.entries()[0].id)); QCOMPARE(table->rowCount(),6);
        table->setCurrentCell(0,0); table->editItem(table->item(0,0));
        auto *editor=table->findChild<QLineEdit*>(); QVERIFY(editor); editor->setFocus(); QTRY_VERIFY(editor->hasFocus());
        editor->selectAll(); QTest::keyClicks(editor,"Unfinished rename"); QCOMPARE(editor->text(),QString("Unfinished rename"));
        QSignalSpy saveClicks(save,&QToolButton::clicked);
        QTest::mouseClick(save,Qt::LeftButton); QCOMPARE(saveClicks.size(),1);
        QCOMPARE(library.entries()[0].layers.last().name,QString("Unfinished rename"));
        const auto recipe=widget.templateSlots(); widget.findChild<QToolButton*>("drawingTemplateDelete")->click(); QVERIFY(library.entries().isEmpty()); QCOMPARE(widget.templateSlots(),recipe);
        for(auto *button:widget.findChildren<QToolButton*>())if(button->objectName().startsWith("drawingTemplate")){
            QVERIFY(!button->toolTip().isEmpty()); QVERIFY(!button->accessibleName().isEmpty()); QVERIFY(!button->icon().isNull()); QCOMPARE(button->iconSize(),QSize(20,20));
        }
        if(qEnvironmentVariableIsSet("TOON_TEMPLATE_SCREENSHOT"))QVERIFY(widget.grab().save(qEnvironmentVariable("TOON_TEMPLATE_SCREENSHOT")));
    }
    void readOnlyLibraryStillAllowsLocalRecipes() {
        QTemporaryDir dir; const auto path=dir.filePath("templates.json"); QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("corrupted"); file.close();
        DrawingTemplateLibrary library(path); QVERIFY(!library.load()); DrawingTemplateWidget widget(&library);
        QVERIFY(widget.validTemplate()); QVERIFY(!widget.findChild<QToolButton*>("drawingTemplateSave")->isEnabled());
        widget.setTemplateSlots({{"ink","Ink",LayerKind::Vector}}); QVERIFY(widget.validTemplate()); QCOMPARE(widget.templateSlots().first().kind,LayerKind::Vector);
        QFile unchanged(path); QVERIFY(unchanged.open(QIODevice::ReadOnly)); QCOMPARE(unchanged.readAll(),QByteArray("corrupted"));
    }
};
QTEST_MAIN(DrawingTemplatePanelTests)
#include "drawing_template_panel_tests.moc"
