#include "vector_edit_panel.h"
#include "drawing_panels.h"
#include "drawing_tool_controller.h"
#include "tool_session.h"
#include "value_slider.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include "property_row.h"
#include <QDoubleSpinBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollBar>
#include <QScrollArea>
#include <QStyle>
#include <QSlider>
#include <QStackedWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>
using namespace toon;
class VectorEditPanelTests:public QObject {
    Q_OBJECT
    static void prepare(Canvas &canvas) {
        auto project=Project::create(); project.width=640;project.height=480;
        canvas.resize(800,700); canvas.setProject(project);canvas.show();
        QVERIFY(ObjectTools::addLayer(canvas.document(),LayerKind::Vector));
        QVector<BezierNode> nodes;
        for(int i=0;i<12;++i) nodes.append(BezierNode::corner({100.+i*25,200.+(i%2)*.5}));
        QVERIFY(!ObjectTools::addPath(canvas.document(),nodes,false,QColor("#3f9bf5"),12).isEmpty());
        canvas.setTool(Canvas::Tool::VectorNode);
        const auto point=canvas.viewTransform().map(sheetGeometry(canvas.project(),canvas.shot()).output.topLeft()+QPointF(150,200));
        canvas.drawingTools()->press(point,Qt::NoModifier);canvas.drawingTools()->release(point,Qt::NoModifier);
        QVERIFY(canvas.drawingTools()->selectedObject());
    }
private slots:
    void initTestCase(){applyTheme();}
    void routesCompactPanelsAndExposesAccessibleIcons(){
        Canvas canvas;prepare(canvas);
        std::unique_ptr<QWidget> properties(drawingPanel(&canvas,"propertiesPanel"));properties->resize(240,600);properties->show();
        auto *panel=properties->findChild<VectorEditPanel*>();QVERIFY(panel);
        auto *scroll=panel->findChild<QScrollArea*>("vectorEditScroll");QVERIFY(scroll);
        for(const auto tool:{Canvas::Tool::VectorNode,Canvas::Tool::VectorSimplify,Canvas::Tool::VectorWidth}){
            canvas.setTool(tool);QTest::qWait(10);QVERIFY(panel->isVisible());QCOMPARE(scroll->horizontalScrollBar()->maximum(),0);
            for(auto *button:panel->findChildren<QToolButton*>())if(button->isVisible()){
                QVERIFY(!button->toolTip().isEmpty());QVERIFY(!button->accessibleName().isEmpty());QCOMPARE(button->iconSize(),QSize(16,16));
                QCOMPARE(button->size(),QSize(28,28));
            }
            for(auto *value:panel->findChildren<ValueSlider*>())if(value->isVisible()){
                QCOMPARE(value->findChild<QDoubleSpinBox*>()->width(),48);QVERIFY(value->findChild<QDoubleSpinBox*>()->suffix().isEmpty());
                QVERIFY(value->findChild<QSlider*>()->width()>=74);
            }
        }
        canvas.setTool(Canvas::Tool::VectorNode);
        auto *mode=panel->findChild<QTabBar*>("pathEditMode");mode->setCurrentIndex(2);
        QCOMPARE(canvas.drawingTools()->pathEditMode,DrawingToolController::PathEditMode::Width);
        canvas.document()->addLayer();
        QVERIFY(!mode->isEnabled());QCOMPARE(panel->findChild<QLabel*>("pathSelectionStatus")->text(),QString("Select a vector layer"));
    }
    void stylePreviewCommitsOnceAndCancelRestores(){
        Canvas canvas;prepare(canvas);VectorEditPanel panel(&canvas);panel.resize(240,600);panel.show();
        auto *width=panel.findChild<ValueSlider*>("pathStrokeWidth");auto *track=width->findChild<QSlider*>();
        const int history=canvas.document()->history()->count();
        track->setSliderDown(true);track->setValue(180);track->setValue(240);
        QCOMPARE(canvas.document()->history()->count(),history);QCOMPARE(canvas.drawingTools()->selectedObject()->strokeWidth,24.);
        track->setSliderDown(false);QCOMPARE(canvas.document()->history()->count(),history+1);
        canvas.document()->history()->undo();QCOMPARE(width->value(),12.);
        auto *opacity=panel.findChild<ValueSlider*>("pathStrokeOpacity");auto *alpha=opacity->findChild<QSlider*>();
        alpha->setSliderDown(true);alpha->setValue(40);QCOMPARE(canvas.drawingTools()->selectedObject()->opacity,.4);
        opacity->cancelEditing();QCOMPARE(canvas.drawingTools()->selectedObject()->opacity,1.);QCOMPARE(opacity->value(),100.);
        QCOMPARE(canvas.document()->history()->index(),history);
        canvas.setBrushColor(Qt::red);panel.findChild<QToolButton*>("pathUseForeground")->click();
        QCOMPARE(canvas.drawingTools()->selectedObject()->stroke,QColor(Qt::red));
        QCOMPARE(canvas.drawingTools()->selectedObject()->strokeWidth,12.);
    }
    void remembersEditToolSettingsIndependently(){
        QTemporaryDir dir;BrushLibrary library(dir.filePath("brushes.json"));QVERIFY(library.load());
        ToolCatalog catalog(&library,dir.filePath("tools.json"));QVERIFY(catalog.load());
        Canvas canvas;canvas.setProject(Project::create());ToolSession session(&canvas,&catalog);VectorEditPanel panel(&canvas);
        QVERIFY(catalog.activateTool("width-brush"));panel.findChild<ValueSlider*>("vectorEditBrushRadius")->setValue(64);
        panel.findChild<QTabBar*>("widthBrushDirection")->setCurrentIndex(1);
        QVERIFY(catalog.activateTool("simplify-brush"));panel.findChild<ValueSlider*>("vectorEditBrushRadius")->setValue(21);
        panel.findChild<ValueSlider*>("pathSimplifyTolerance")->setValue(5);
        QVERIFY(catalog.activateTool("width-brush"));QCOMPARE(canvas.drawingTools()->widthBrushRadius,64.);QVERIFY(!canvas.drawingTools()->widthThicken);
        QVERIFY(catalog.activateTool("path-edit"));panel.findChild<QTabBar*>("pathEditMode")->setCurrentIndex(1);
        QVERIFY(catalog.activateTool("simplify-brush"));QCOMPARE(canvas.drawingTools()->widthBrushRadius,21.);QCOMPARE(canvas.drawingTools()->simplifyTolerance,5.);
        QVERIFY(catalog.activateTool("path-edit"));QCOMPARE(canvas.drawingTools()->pathEditMode,DrawingToolController::PathEditMode::Nodes);
    }
    void responsiveRowsFitEveryEditModeAndKeepDrafts(){
        Canvas canvas;prepare(canvas);VectorEditPanel panel(&canvas);panel.resize(240,550);panel.show();
        auto *scroll=panel.findChild<QScrollArea*>("vectorEditScroll");QVERIFY(scroll);
        for(const auto tool:{Canvas::Tool::VectorNode,Canvas::Tool::VectorSimplify,Canvas::Tool::VectorWidth}){
            canvas.setTool(tool);
            if(tool==Canvas::Tool::VectorNode)canvas.drawingTools()->setPathEditMode(DrawingToolController::PathEditMode::Nodes);
            for(int width:{220,240,360}){
                panel.resize(width,550);QTest::qWait(10);
                QCOMPARE(panel.width(),width);QCOMPARE(scroll->horizontalScrollBar()->maximum(),0);
                QVERIFY(scroll->widget()->width()<=scroll->viewport()->width());
                for(auto *row:panel.findChildren<PropertyRow*>())if(row->isVisible()){
                    QVERIFY2(row->rect().contains(row->control()->geometry()),qPrintable(row->objectName()));
                    QVERIFY2(!row->labelWidget()->geometry().intersects(row->control()->geometry()),qPrintable(row->objectName()));
                    auto *slider=qobject_cast<ValueSlider*>(row->control());QVERIFY(slider);
                    QVERIFY(slider->findChild<QSlider*>()->width()>=74);
                    QCOMPARE(slider->findChild<QDoubleSpinBox*>()->width(),48);
                }
                auto *strengthRow=panel.findChild<PropertyRow*>("widthBrushStrengthRow");QVERIFY(strengthRow);
                QCOMPARE(strengthRow->isVisible(),tool==Canvas::Tool::VectorWidth);
            }
        }
        canvas.setTool(Canvas::Tool::VectorNode);panel.resize(240,550);panel.activateWindow();
        auto *width=panel.findChild<ValueSlider*>("pathStrokeWidth");
        auto *field=width->findChild<QDoubleSpinBox*>();auto *editor=field->findChild<QLineEdit*>();QVERIFY(editor);
        field->setFocus();QTRY_VERIFY(field->hasFocus());field->selectAll();QTest::keyClicks(field,"18.5");
        const int history=canvas.document()->history()->count();
        panel.resize(220,550);QTest::qWait(10);
        QVERIFY(field->hasFocus());QCOMPARE(editor->text(),QString("18.5"));
        QCOMPARE(canvas.document()->history()->count(),history);
        QTest::keyClick(field,Qt::Key_Escape);QCOMPARE(width->value(),12.);
        QCOMPARE(canvas.document()->history()->count(),history);
        panel.resize(220,140);QTest::qWait(10);QVERIFY(scroll->verticalScrollBar()->maximum()>0);
        auto *simplify=panel.findChild<QToolButton*>("pathSimplifyLayer");QTest::qWait(10);
        QVERIFY(!scroll->isAncestorOf(simplify));
        QVERIFY(panel.rect().contains(QRect(simplify->mapTo(&panel,QPoint()),simplify->size())));
    }
    void shortInspectorKeepsModesAndCommandsAvailable(){
        Canvas canvas;prepare(canvas);VectorEditPanel panel(&canvas);panel.resize(220,180);panel.show();
        auto *scroll=panel.findChild<QScrollArea*>("vectorEditScroll");QVERIFY(scroll);
        auto *mode=panel.findChild<QTabBar*>("pathEditMode");auto *status=panel.findChild<QLabel*>("pathSelectionStatus");
        auto *selected=panel.findChild<QToolButton*>("pathSimplifySelected");
        const auto id=canvas.drawingTools()->selectedObjectId();
        const auto nodeCount=canvas.drawingTools()->selectedObject()->nodes.size();
        const int history=canvas.document()->history()->count();
        mode->setCurrentIndex(1);QTest::qWait(20);QCOMPARE(panel.size(),QSize(220,180));
        QVERIFY(scroll->verticalScrollBar()->maximum()>0);
        const auto modeGeometry=mode->geometry(),statusGeometry=status->geometry(),commandGeometry=selected->geometry();
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());QTest::qWait(10);
        QCOMPARE(mode->geometry(),modeGeometry);QCOMPARE(status->geometry(),statusGeometry);QCOMPARE(selected->geometry(),commandGeometry);
        for(auto *pinned:{static_cast<QWidget*>(mode),static_cast<QWidget*>(status),static_cast<QWidget*>(selected)}){
            QVERIFY(!scroll->isAncestorOf(pinned));QVERIFY(panel.rect().contains(QRect(pinned->mapTo(&panel,QPoint()),pinned->size())));
        }
        QCOMPARE(canvas.drawingTools()->selectedObjectId(),id);QCOMPARE(canvas.document()->history()->count(),history);
        selected->click();QVERIFY(canvas.drawingTools()->selectedObject()->nodes.size()<nodeCount);
        QCOMPARE(canvas.document()->history()->count(),history+1);canvas.document()->history()->undo();
        QCOMPARE(canvas.drawingTools()->selectedObject()->nodes.size(),nodeCount);
        mode->setCurrentIndex(2);QTest::qWait(10);
        auto *reset=panel.findChild<QToolButton*>("pathResetWidths");QVERIFY(reset->isVisibleTo(&panel));QVERIFY(!selected->isVisibleTo(&panel));
        QVERIFY(panel.rect().contains(QRect(reset->mapTo(&panel,QPoint()),reset->size())));
        canvas.setTool(Canvas::Tool::VectorWidth);QTest::qWait(10);
        auto *direction=panel.findChild<QTabBar*>("widthBrushDirection");QVERIFY(direction->isVisibleTo(&panel));
        QVERIFY(!scroll->isAncestorOf(direction));QVERIFY(!mode->isVisibleTo(&panel));
        QVERIFY(!panel.findChild<QWidget*>("vectorEditCommands")->isVisibleTo(&panel));
        QCOMPARE(canvas.document()->history()->index(),history);
    }
    void largerTextKeepsContextAndPreciseDraft(){
        Canvas canvas;prepare(canvas);VectorEditPanel panel(&canvas);panel.resize(240,360);panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));panel.raise();panel.activateWindow();QVERIFY(QTest::qWaitForWindowActive(&panel));
        auto *scroll=panel.findChild<QScrollArea*>("vectorEditScroll");auto *mode=panel.findChild<QTabBar*>("pathEditMode");
        auto *status=panel.findChild<QLabel*>("pathSelectionStatus");auto *value=panel.findChild<ValueSlider*>("pathStrokeWidth");
        auto *field=value->findChild<QDoubleSpinBox*>();auto *editor=field->findChild<QLineEdit*>();
        field->setFocus();QTRY_VERIFY(field->hasFocus());field->selectAll();QTest::keyClicks(field,"18.5");
        const int history=canvas.document()->history()->count();auto font=panel.font();font.setPixelSize(24);panel.setFont(font);panel.resize(220,360);QTest::qWait(20);
        QCOMPARE(editor->text(),QString("18.5"));QVERIFY(field->hasFocus());QCOMPARE(canvas.document()->history()->count(),history);
        QVERIFY(status->height()>=status->fontMetrics().height());QCOMPARE(status->accessibleName(),status->text());
        QCOMPARE(scroll->horizontalScrollBar()->maximum(),0);
        for(int i=0;i<mode->count();++i){QVERIFY(mode->rect().contains(mode->tabRect(i)));QVERIFY(mode->tabRect(i).width()>=ui::metrics::actionTarget);QVERIFY(!mode->accessibleTabName(i).isEmpty());}
        QTest::keyClick(field,Qt::Key_Return);QCOMPARE(canvas.drawingTools()->selectedObject()->strokeWidth,18.5);QCOMPARE(canvas.document()->history()->count(),history+1);
        const auto statusGeometry=status->geometry(),modeGeometry=mode->geometry();
        canvas.document()->setLayersLocked({canvas.document()->activeShot().layers[canvas.document()->activeLayer()].id},true);QTest::qWait(10);
        QCOMPARE(status->geometry(),statusGeometry);QCOMPARE(mode->geometry(),modeGeometry);QVERIFY(status->text().contains("locked"));
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");if(!output.isEmpty()){
            QDir().mkpath(output);QVERIFY(panel.grab().save(output+"/vector-edit-larger-font.png"));
        }
    }
    void renderPreview(){
        if(qEnvironmentVariableIsEmpty("TOON_UI_PREVIEW"))QSKIP("Preview output is opt-in");
        QWidget gallery;auto *layout=new QHBoxLayout(&gallery);
        for(const auto tool:{Canvas::Tool::VectorNode,Canvas::Tool::VectorSimplify,Canvas::Tool::VectorWidth}){
            auto *canvas=new Canvas(&gallery);prepare(*canvas);canvas->hide();canvas->setTool(tool);
            if(tool==Canvas::Tool::VectorNode)canvas->drawingTools()->setPathEditMode(DrawingToolController::PathEditMode::Nodes);
            auto *panel=new VectorEditPanel(canvas);panel->setFixedSize(240,550);layout->addWidget(panel);
        }
        gallery.show();QTest::qWait(30);QDir().mkpath(qEnvironmentVariable("TOON_UI_PREVIEW"));
        QVERIFY(gallery.grab().save(qEnvironmentVariable("TOON_UI_PREVIEW")+"/vector-edit-properties.png"));
        auto *panel=gallery.findChild<VectorEditPanel*>();QVERIFY(panel);
        panel->setFixedSize(220,180);QTest::qWait(20);
        auto *scroll=panel->findChild<QScrollArea*>("vectorEditScroll");scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());QTest::qWait(10);
        QVERIFY(panel->grab().save(qEnvironmentVariable("TOON_UI_PREVIEW")+"/vector-edit-short.png"));
    }
};
QTEST_MAIN(VectorEditPanelTests)
#include "test_vector_edit_panel.moc"
