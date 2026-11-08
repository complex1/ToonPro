#include "canvas.h"
#include "document.h"
#include "drawing_tool_controller.h"
#include "drawing_tool_panel.h"
#include "layer_panel.h"
#include "raster_tools.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QtTest>

using namespace toon;
namespace {
Project fixture() {
    auto project=Project::create(ProjectKind::DrawingOnly);project.width=256;project.height=256;project.transparent=true;
    auto colors=RasterLayer::create("Colors");auto ink=RasterLayer::create("Ink");ink.locked=true;
    QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);
    {QPainter p(&tile);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(Qt::black,3));p.drawRoundedRect(QRectF(40,40,160,160),24,24);}
    ink.tiles.insert({0,0},tile);project.shots[0].layers={colors,ink};return project;
}
QTreeWidgetItem *row(QTreeWidget *tree,const QString &id){for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString()==id)return *it;return nullptr;}
const RasterLayer &layer(const Document &doc,const QString &id){for(const auto &l:doc.activeShot().layers)if(l.id==id)return l;Q_UNREACHABLE();}
}
class FillReferenceUiTests:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void directMarkHandlesLockedInkBatchSelectionAndUndo(){
        Canvas canvas;const auto project=fixture();canvas.setProject(project);auto *doc=canvas.document();LayerPanel panel(doc);panel.resize(220,360);panel.show();
        auto *tree=panel.findChild<QTreeWidget*>("layerTree");auto *mark=panel.findChild<QToolButton*>("layerFillReference");QVERIFY(tree);QVERIFY(mark);QCOMPARE(mark->size(),QSize(ui::metrics::actionTarget,ui::metrics::actionTarget));
        const auto ink=project.shots[0].layers[1].id,colors=project.shots[0].layers[0].id;
        tree->setCurrentItem(row(tree,ink),1);QVERIFY(mark->isEnabled());mark->click();QVERIFY(layer(*doc,ink).fillReference);QCOMPARE(doc->history()->count(),1);
        QTRY_VERIFY(row(tree,ink)->toolTip(1).contains("Fill reference"));QVERIFY(row(tree,ink)->data(1,Qt::AccessibleTextRole).toString().contains("Fill reference"));
        tree->clearSelection();tree->setCurrentItem(row(tree,colors),1,QItemSelectionModel::NoUpdate);row(tree,colors)->setSelected(true);row(tree,ink)->setSelected(true);
        QVERIFY(!mark->isChecked());mark->click();QVERIFY(layer(*doc,colors).fillReference);QVERIFY(layer(*doc,ink).fillReference);QCOMPARE(doc->history()->count(),2);
        doc->history()->undo();QVERIFY(!layer(*doc,colors).fillReference);QVERIFY(layer(*doc,ink).fillReference);doc->history()->redo();QVERIFY(layer(*doc,colors).fillReference);
        QCoreApplication::processEvents();QCOMPARE(panel.horizontalScrollBar()->maximum(),0);QVERIFY(panel.widget()->width()<=panel.viewport()->width());
    }
    void boundaryModesUseMarkedReferencesAndPreserveExplicitChoice(){
        Canvas canvas;auto project=fixture();project.shots[0].layers[1].fillReference=true;project.shots[0].layers[1].visible=false;canvas.setProject(project);canvas.setTool(Canvas::Tool::Bucket);
        DrawingToolPanel panel(&canvas);panel.resize(220,360);panel.show();auto *tools=canvas.drawingTools();auto *tabs=panel.findChild<QTabBar*>("sampleSource");auto *picker=panel.findChild<QComboBox*>("fillReferenceLayer");auto *summary=panel.findChild<QLabel*>("fillReferenceSummary");auto *marked=panel.findChild<QToolButton*>("fillUseMarkedReferences");auto *caption=panel.findChild<QLabel*>("fillBoundarySourceLabel");QVERIFY(tabs);QVERIFY(picker);QVERIFY(summary);QVERIFY(marked);QVERIFY(caption);
        tabs->setCurrentIndex(1);QCOMPARE(tools->fillSettings.source,RasterSampleSource::ActiveLayer);QVERIFY(caption->text().contains("Current layer"));QVERIFY(!picker->isVisible());
        tabs->setCurrentIndex(0);QCOMPARE(tools->fillSettings.source,RasterSampleSource::AllLayers);QVERIFY(caption->text().contains("Whole drawing"));
        tabs->setCurrentIndex(2);QCOMPARE(tools->fillSettings.source,RasterSampleSource::MarkedReferences);QVERIFY(picker->isVisible());QVERIFY(summary->text().contains("1 reference layer"));QVERIFY(summary->text().contains("1 hidden"));QVERIFY(canvas.toolBlockReason().isEmpty());
        const auto inkId=project.shots[0].layers[1].id;picker->setCurrentIndex(picker->findData(inkId));QCOMPARE(tools->fillSettings.source,RasterSampleSource::ReferenceLayer);QCOMPARE(tools->fillSettings.referenceLayerId,inkId);QCOMPARE(summary->text(),QString("Reference: Ink"));
        marked->click();QCOMPARE(tools->fillSettings.source,RasterSampleSource::MarkedReferences);QCOMPARE(tabs->currentIndex(),2);QVERIFY(marked->isChecked());
        QVERIFY(canvas.document()->setFillReferences({inkId},false));QTRY_VERIFY(!canvas.toolBlockReason().isEmpty());QVERIFY(summary->text().contains("reference",Qt::CaseInsensitive));QCOMPARE(canvas.document()->history()->count(),1);
    }
    void missingExplicitReferenceDoesNotChooseAnotherLayer(){
        Canvas canvas;canvas.setProject(fixture());canvas.setTool(Canvas::Tool::Bucket);auto *tools=canvas.drawingTools();tools->fillSettings.source=RasterSampleSource::ReferenceLayer;tools->fillSettings.referenceLayerId="missing-source";
        DrawingToolPanel panel(&canvas);panel.show();auto *picker=panel.findChild<QComboBox*>("fillReferenceLayer");QVERIFY(picker);QCOMPARE(picker->currentIndex(),-1);QVERIFY(tools->fillSettings.referenceLayerId.isEmpty());QVERIFY(!canvas.toolBlockReason().isEmpty());QCOMPARE(tools->fillSettings.source,RasterSampleSource::ReferenceLayer);
    }
    void contextMenuMarksAndExposesPerspectiveWithoutMutatingArtwork(){
        Canvas canvas;const auto project=fixture();canvas.setProject(project);auto *doc=canvas.document();LayerPanel panel(doc);panel.resize(230,360);panel.show();const auto colors=project.shots[0].layers[0].id;const auto original=layer(*doc,colors).tiles;
        bool found=false;QTimer::singleShot(20,&panel,[&]{auto *menu=panel.findChild<QMenu*>("layerContextMenu");if(!menu)return;for(auto *action:menu->actions())if(action->objectName()=="markFillReferenceAction"){found=true;action->trigger();break;}menu->close();});panel.findChild<QToolButton*>("layerMore")->click();QVERIFY(found);QVERIFY(layer(*doc,colors).fillReference);QCOMPARE(layer(*doc,colors).tiles,original);
        QSignalSpy detect(&panel,&LayerPanel::detectPerspectiveRequested);bool perspective=false;QTimer::singleShot(20,&panel,[&]{auto *menu=panel.findChild<QMenu*>("layerContextMenu");if(!menu)return;for(auto *action:menu->actions())if(action->text()=="Detect perspective…"){perspective=true;action->trigger();break;}menu->close();});panel.findChild<QToolButton*>("layerMore")->click();QVERIFY(perspective);QCOMPARE(detect.count(),1);QCOMPARE(doc->history()->count(),1);
    }
    void captureReviewedControls(){
        const auto directory=qEnvironmentVariable("TOON_FILL_REFERENCE_CAPTURE_DIR");if(directory.isEmpty())return;QDir().mkpath(directory);
        Canvas canvas;auto p=fixture();p.shots[0].layers[1].fillReference=true;canvas.setProject(p);canvas.setTool(Canvas::Tool::Bucket);canvas.drawingTools()->fillSettings.source=RasterSampleSource::MarkedReferences;
        LayerPanel layers(canvas.document());layers.resize(220,360);layers.show();DrawingToolPanel properties(&canvas);properties.resize(220,300);properties.show();QTest::qWait(40);
        QVERIFY(layers.grab().save(directory+"/layers-fill-reference.png"));QVERIFY(properties.grab().save(directory+"/fill-reference-properties.png"));
    }
};
QTEST_MAIN(FillReferenceUiTests)
#include "test_fill_reference_ui.moc"
