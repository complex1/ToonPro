#include "drawing_tool_panel.h"
#include "drawing_tool_controller.h"
#include "advanced_transform.h"
#include "drawing_panels.h"
#include "icons.h"
#include "ui_theme.h"
#include "value_slider.h"
#include "property_row.h"
#include <QTabBar>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QToolButton>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QStackedWidget>
#include <QDir>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QLineEdit>
#include <QLabel>
#include <QtTest>
using namespace toon;
class DrawingToolPanelTests:public QObject{
 Q_OBJECT
private slots:
 void initTestCase(){applyTheme();}
 void liveBrushSamplesDoNotBroadcastUnchangedToolProperties(){
    Canvas canvas;canvas.setProject(Project::create());
    std::unique_ptr<QWidget> properties(drawingPanel(&canvas,"propertiesPanel"));
    auto *stack=qobject_cast<QStackedWidget*>(properties.get());QVERIFY(stack);
    auto *doc=canvas.document();auto *controller=canvas.drawingTools();
    QSignalSpy settings(controller,&DrawingToolController::changed);
    QSignalSpy artwork(&canvas,&Canvas::documentChanged);
    QSignalSpy live(doc,&Document::strokeUpdated);
    auto brush=canvas.brush;brush.size=6;brush.opacity=.26;
    QVERIFY(doc->beginStroke({100,100},1.,brush,false));
    for(int i=1;i<=32;++i)doc->continueStroke({100.+i*3,100.+i},1.);
    QCOMPARE(settings.count(),0);QCOMPARE(stack->currentIndex(),0);
    QCOMPARE(live.count(),33);QCOMPARE(artwork.count(),0);QVERIFY(doc->strokeActive());
    doc->endStroke();QVERIFY(artwork.count()>0);QVERIFY(settings.count()>0);QCOMPARE(doc->history()->count(),1);
    settings.clear();
    QVERIFY(doc->beginStroke({250,100},1.,brush,false));
    doc->continueStroke({275,110},1.);QCOMPARE(settings.count(),0);
    doc->cancelStroke();QVERIFY(settings.count()>0);QCOMPARE(doc->history()->count(),1);
    doc->history()->undo();QVERIFY(doc->project().shots.first().layers.first().tiles.isEmpty());
 }
 void gradientStopsAddMoveRecolorAndRemove(){
    GradientStopsWidget stops;stops.resize(260,76);stops.show();QTest::qWait(10);QSignalSpy change(&stops,&GradientStopsWidget::stopsChanged);
    QTest::mouseClick(&stops,Qt::LeftButton,Qt::NoModifier,{130,50});QCOMPARE(stops.stops().size(),3);QCOMPARE(stops.selectedIndex(),1);stops.setSelectedColor(Qt::red);QCOMPARE(stops.stops()[1].second,QColor(Qt::red));
    QTest::keyClick(&stops,Qt::Key_Right);QVERIFY(stops.stops()[1].first>.5);QTest::keyClick(&stops,Qt::Key_Delete);QCOMPARE(stops.stops().size(),2);stops.removeSelected();QCOMPARE(stops.stops().size(),2);QVERIFY(change.count()>=4);
 }
 void compactPagesAndToolRouting(){
    Canvas canvas;canvas.setProject(Project::create());std::unique_ptr<QWidget> properties(drawingPanel(&canvas,"propertiesPanel"));properties->resize(260,600);properties->show();
    auto *panel=properties->findChild<DrawingToolPanel*>();QVERIFY(panel);
    const QList<Canvas::Tool> tools={Canvas::Tool::Eyedropper,Canvas::Tool::Bucket,Canvas::Tool::Gradient,Canvas::Tool::Rectangle,Canvas::Tool::Retouch,Canvas::Tool::Text,Canvas::Tool::EllipseSelect,Canvas::Tool::AdvancedTransform};
    for(auto tool:tools){canvas.setTool(tool);QTest::qWait(10);QVERIFY(panel->isVisible());QCOMPARE(panel->horizontalScrollBar()->maximum(),0);QVERIFY2(panel->widget()->width()<=panel->viewport()->width(),qPrintable(QString::number(int(tool))));for(auto *b:panel->findChildren<QToolButton*>()){if(!b->isVisible()||b->objectName().startsWith("qt_"))continue;QVERIFY2(!b->toolTip().isEmpty(),qPrintable(b->objectName()));QVERIFY2(!b->accessibleName().isEmpty(),qPrintable(b->objectName()));}}
    canvas.drawingTools()->cancel();
 }
 void pairedValuesUpdateToolSettingsAndFitNarrowPanels(){
    Canvas canvas;canvas.setProject(Project::create());DrawingToolPanel panel(&canvas);panel.resize(240,600);panel.show();
    const QList<QPair<Canvas::Tool,QString>> fields={{Canvas::Tool::Bucket,"fillTolerance"},{Canvas::Tool::Rectangle,"shapeStrokeWidth"},{Canvas::Tool::Retouch,"retouchSize"},{Canvas::Tool::Text,"artworkFontSize"},{Canvas::Tool::AdvancedTransform,"artworkSkewX"}};
    for(const auto &field:fields){canvas.setTool(field.first);QTest::qWait(10);auto *control=panel.findChild<ValueSlider*>(field.second);QVERIFY(control);QVERIFY(control->isVisible());
        auto *track=control->findChild<QSlider*>();auto *number=control->findChild<QDoubleSpinBox*>();QVERIFY(track);QVERIFY(number);QVERIFY(track->width()>=70);QVERIFY(number->width()<=80);QCOMPARE(panel.horizontalScrollBar()->maximum(),0);}
    canvas.drawingTools()->cancel();canvas.setTool(Canvas::Tool::Bucket);
    auto *tolerance=panel.findChild<ValueSlider*>("fillTolerance");tolerance->findChild<QSlider*>()->setValue(64);QCOMPARE(canvas.drawingTools()->fillSettings.tolerance,64);
    auto *number=tolerance->findChild<QDoubleSpinBox*>();number->setFocus();number->selectAll();QTest::keyClicks(number,"23");QTest::keyClick(number,Qt::Key_Return);QCOMPARE(canvas.drawingTools()->fillSettings.tolerance,23);QCOMPARE(tolerance->findChild<QSlider*>()->value(),23);
    canvas.setTool(Canvas::Tool::Eyedropper);QVERIFY(!tolerance->isVisible());QVERIFY(!qobject_cast<QWidget*>(tolerance->property("fieldLabel").value<QObject*>())->isVisible());
    canvas.setTool(Canvas::Tool::VectorEraser);QVERIFY(!panel.findChild<ValueSlider*>("retouchStrength")->isVisible());
 }
 void responsiveRowsKeepEditorsUsableAndHiddenPagesDoNotForceScrolling(){
    Canvas canvas;canvas.setProject(Project::create());DrawingToolPanel panel(&canvas);panel.resize(220,150);panel.show();canvas.setTool(Canvas::Tool::Bucket);
    QTRY_COMPARE(panel.verticalScrollBar()->maximum(),0);
    auto *tolerance=panel.findChild<ValueSlider*>("fillTolerance");
    auto *row=qobject_cast<PropertyRow*>(tolerance->parentWidget());QVERIFY(row);
    QVERIFY(tolerance->x()>row->labelWidget()->geometry().right());
    QVERIFY(row->height()<=32);
    canvas.setTool(Canvas::Tool::Text);QTRY_VERIFY(panel.verticalScrollBar()->maximum()>0);
    canvas.setTool(Canvas::Tool::Bucket);QTRY_COMPARE(panel.verticalScrollBar()->maximum(),0);
    canvas.setTool(Canvas::Tool::Eyedropper);QTRY_VERIFY(!row->isVisible());
    const QList<Canvas::Tool> tools={Canvas::Tool::Bucket,Canvas::Tool::Gradient,Canvas::Tool::Rectangle,Canvas::Tool::Retouch,Canvas::Tool::Text,Canvas::Tool::AdvancedTransform};
    for(int width:{220,240,320})for(auto tool:tools){
        panel.resize(width,420);canvas.setTool(tool);QTest::qWait(10);
        QCOMPARE(panel.horizontalScrollBar()->maximum(),0);
        for(auto *property:panel.findChildren<PropertyRow*>()){
            if(!property->isVisible())continue;
            auto *control=property->control();
            QVERIFY2(!property->labelWidget()->geometry().intersects(control->geometry()),qPrintable(property->accessibleName()));
            QVERIFY2(control->geometry().bottom()<property->height(),qPrintable(property->accessibleName()));
            QVERIFY(control->width()>=128);
            if(auto *value=qobject_cast<ValueSlider*>(control)){
                QVERIFY(value->findChild<QSlider*>()->width()>=74);
                QCOMPARE(value->findChild<QDoubleSpinBox*>()->width(),48);
            }
        }
        for(const auto &id:{"gradientForeground","shapeStroke","shapeFillColor","artworkBold","applyArtworkText","flipArtworkHorizontal"}){
            auto *action=panel.findChild<QToolButton*>(id);QVERIFY(action);
            if(action->isVisible())QVERIFY2(action->size()==QSize(28,28),qPrintable(QString("%1 at panel width %2: %3 × %4").arg(id).arg(width).arg(action->width()).arg(action->height())));
        }
    }
    canvas.drawingTools()->cancel();
 }
 void advancedTransformCompletionStaysVisibleAndPreservesEditing(){
    auto project=Project::create();project.width=320;project.height=240;
    QImage image(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);image.fill(QColor("#d07d53"));
    project.shots[0].layers[0].tiles.insert({0,0},image);
    Canvas canvas;canvas.setProject(project);DrawingToolPanel panel(&canvas);panel.resize(220,190);panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));panel.activateWindow();QVERIFY(QTest::qWaitForWindowActive(&panel));
    canvas.setTool(Canvas::Tool::AdvancedTransform);canvas.drawingTools()->beginAdvanced({},false);
    auto *transform=canvas.drawingTools()->transform();QVERIFY(transform->active());
    auto *footer=panel.findChild<QWidget*>("advancedTransformFooter");QVERIFY(footer);
    auto *apply=panel.findChild<QToolButton*>("applyAdvancedTransform");auto *cancel=panel.findChild<QToolButton*>("cancelAdvancedTransform");
    auto *skew=panel.findChild<ValueSlider*>("artworkSkewX")->findChild<QDoubleSpinBox*>();
    panel.ensureWidgetVisible(skew);skew->setFocus();skew->selectAll();QTest::keyClicks(skew,"12.5");
    auto font=panel.font();font.setPointSizeF(font.pointSizeF()+4);panel.setFont(font);panel.resize(240,240);QTest::qWait(20);
    QCOMPARE(skew->findChild<QLineEdit*>()->text(),QString("12.5"));QVERIFY(skew->hasFocus());
    const auto position=footer->geometry();
    for(int scroll:{0,panel.verticalScrollBar()->maximum()}){
        panel.verticalScrollBar()->setValue(scroll);QCoreApplication::processEvents();QCOMPARE(footer->geometry(),position);
        for(auto *action:{apply,cancel}){
            QVERIFY(panel.rect().contains(QRect(action->mapTo(&panel,QPoint()),action->size())));
            QVERIFY(!panel.widget()->isAncestorOf(action));QVERIFY(action->height()>=28);QVERIFY(!action->text().isEmpty());
        }
        QVERIFY(panel.viewport()->geometry().bottom()<footer->geometry().top());
    }
    QTest::keyClick(skew,Qt::Key_Return);
    QVERIFY(transform->setMatrix(QTransform::fromTranslate(12,8)));apply->click();QVERIFY(!transform->active());
    QCOMPARE(canvas.document()->history()->count(),1);canvas.document()->history()->undo();
    QCOMPARE(canvas.document()->project().shots[0].layers[0].tiles,project.shots[0].layers[0].tiles);
    canvas.drawingTools()->beginAdvanced({},false);QVERIFY(transform->setMatrix(QTransform::fromTranslate(5,4)));
    cancel->click();QVERIFY(!transform->active());QCOMPARE(canvas.document()->history()->index(),0);
    canvas.setTool(Canvas::Tool::Bucket);QTRY_VERIFY(!footer->isVisible());
    QTRY_COMPARE(panel.verticalScrollBar()->maximum(),0);
 }
 void textExactEntrySurvivesReflowAndCommitsOneUndo(){
    Canvas canvas;canvas.resize(800,600);canvas.setProject(Project::create());DrawingToolPanel panel(&canvas);panel.resize(320,550);panel.show();panel.activateWindow();canvas.setTool(Canvas::Tool::Text);
    canvas.drawingTools()->press({400,300},Qt::NoModifier);canvas.drawingTools()->release({400,300},Qt::NoModifier);
    const auto selected=canvas.drawingTools()->selectedObject();QVERIFY(selected);const qreal previousWidth=selected->textWidth;
    const int history=canvas.document()->history()->count();
    auto *value=panel.findChild<ValueSlider*>("artworkTextWidth");auto *number=value->findChild<QDoubleSpinBox*>();auto *editor=number->findChild<QLineEdit*>();
    number->setFocus();number->selectAll();QTRY_VERIFY(number->hasFocus()||editor->hasFocus());QTest::keyClicks(number,"1234");
    for(int width:{220,320}){panel.resize(width,550);QTest::qWait(10);canvas.drawingTools()->notifySettings();QCOMPARE(editor->text(),QString("1234"));QVERIFY(number->hasFocus()||editor->hasFocus());QCOMPARE(canvas.document()->history()->count(),history);}
    QTest::keyClick(number,Qt::Key_Return);QCOMPARE(canvas.drawingTools()->selectedObject()->textWidth,1234.);QCOMPARE(canvas.document()->history()->count(),history+1);
    canvas.document()->history()->undo();QCOMPARE(canvas.drawingTools()->selectedObject()->textWidth,previousWidth);QCOMPARE(value->value(),previousWidth);
 }
 void vectorWidthSliderCommitsOnceAndUndoSynchronizesSilently(){
    Canvas canvas;canvas.resize(800,600);canvas.setProject(Project::create());DrawingToolPanel panel(&canvas);panel.resize(240,600);panel.show();canvas.setTool(Canvas::Tool::VectorPen);
    canvas.drawingTools()->press({300,250},Qt::NoModifier);canvas.drawingTools()->release({300,250},Qt::NoModifier);
    canvas.drawingTools()->press({500,250},Qt::NoModifier);canvas.drawingTools()->release({500,250},Qt::NoModifier);canvas.drawingTools()->finishPath();canvas.setTool(Canvas::Tool::VectorNode);
    const auto before=canvas.drawingTools()->selectedObject();QVERIFY(before);const auto originalWidth=before->strokeWidth;const int history=canvas.document()->history()->count();
    auto *width=panel.findChild<ValueSlider*>("shapeStrokeWidth");auto *track=width->findChild<QSlider*>();track->setSliderDown(true);track->setValue(150);track->setValue(280);
    QCOMPARE(canvas.document()->history()->count(),history);track->setSliderDown(false);QCOMPARE(canvas.document()->history()->count(),history+1);QCOMPARE(canvas.drawingTools()->selectedObject()->strokeWidth,28.);
    canvas.document()->history()->undo();QCOMPARE(canvas.drawingTools()->selectedObject()->strokeWidth,originalWidth);QCOMPARE(width->value(),originalWidth);QCOMPARE(canvas.document()->history()->index(),history);
 }
 void textDraftUsedForNewPlacement(){
    Canvas canvas;canvas.resize(800,600);canvas.setProject(Project::create());DrawingToolPanel panel(&canvas);panel.show();canvas.setTool(Canvas::Tool::Text);
    auto *editor=panel.findChild<QPlainTextEdit*>("artworkText");QVERIFY(editor);editor->setPlainText("First line\nSecond line");QCOMPARE(canvas.drawingTools()->textStyle.text,editor->toPlainText());
    canvas.drawingTools()->press({400,300},Qt::NoModifier);QVERIFY(canvas.drawingTools()->selectedObject());QCOMPARE(canvas.drawingTools()->selectedObject()->text,editor->toPlainText());
 }
 void textDraftAndColorStaySynchronizedAcrossUndoAndVectorSelection(){
    Canvas canvas;canvas.resize(800,600);canvas.setProject(Project::create());DrawingToolPanel panel(&canvas);panel.show();
    canvas.setBrushColor(Qt::red);canvas.setTool(Canvas::Tool::Text);
    auto *editor=panel.findChild<QPlainTextEdit*>("artworkText");editor->setPlainText("Original");
    canvas.drawingTools()->press({400,300},Qt::NoModifier);canvas.drawingTools()->release({400,300},Qt::NoModifier);
    const auto id=canvas.drawingTools()->selectedObject()->id;QCOMPARE(canvas.drawingTools()->selectedObject()->fill,QColor(Qt::red));
    editor->setPlainText("Edited");panel.findChild<QToolButton*>("applyArtworkText")->click();QCOMPARE(canvas.drawingTools()->selectedObject()->text,QString("Edited"));
    canvas.document()->history()->undo();QCOMPARE(editor->toPlainText(),QString("Original"));QCOMPARE(canvas.drawingTools()->textStyle.text,QString("Original"));
    editor->setPlainText("Unapplied draft");
    const auto outline=editableObjectPath(*canvas.drawingTools()->selectedObject());const auto glyph=outline.pointAtPercent(.01);
    const auto view=canvas.viewTransform().map(sheetGeometry(canvas.project(),canvas.shot()).output.topLeft()+glyph);
    canvas.drawingTools()->press(view,Qt::NoModifier);canvas.drawingTools()->release(view,Qt::NoModifier);
    QCOMPARE(editor->toPlainText(),QString("Unapplied draft"));QCOMPARE(canvas.drawingTools()->textStyle.text,QString("Unapplied draft"));
    editor->setPlainText("Original");
    ObjectTools::addLayer(canvas.document(),LayerKind::Vector);canvas.setTool(Canvas::Tool::VectorPen);
    canvas.drawingTools()->press({300,250},Qt::NoModifier);canvas.drawingTools()->release({300,250},Qt::NoModifier);
    canvas.drawingTools()->press({500,250},Qt::NoModifier);canvas.drawingTools()->release({500,250},Qt::NoModifier);canvas.drawingTools()->finishPath();
    canvas.setTool(Canvas::Tool::VectorNode);canvas.drawingTools()->notifySettings();QCOMPARE(editor->toPlainText(),QString("Original"));
    canvas.setTool(Canvas::Tool::Text);const int count=canvas.project().shots.first().layers.size();
    canvas.drawingTools()->press({400,250},Qt::NoModifier);canvas.drawingTools()->release({400,250},Qt::NoModifier);
    QCOMPARE(canvas.project().shots.first().layers.size(),count+1);QCOMPARE(canvas.drawingTools()->selectedObject()->kind,ObjectKind::Text);
    QCOMPARE(canvas.drawingTools()->selectedObject()->text,QString("Original"));QVERIFY(canvas.drawingTools()->selectedObject()->id!=id);
 }
 void toolsHaveRealIconsAndAccessibleTabs(){
    Canvas canvas;canvas.setProject(Project::create());std::unique_ptr<QWidget> tools(drawingPanel(&canvas,"toolsPanel"));tools->resize(240,300);tools->show();auto*tabs=tools->findChild<QTabWidget*>();QVERIFY(tabs);QCOMPARE(tabs->count(),3);
    QVERIFY(tools->findChild<QToolButton*>("toolArc")->icon().name() != tools->findChild<QToolButton*>("toolCurve")->icon().name());
    for(int i=0;i<3;++i){tabs->setCurrentIndex(i);QTest::qWait(10);for(auto*b:tools->findChildren<QToolButton*>()){if(!b->objectName().startsWith("tool"))continue;QVERIFY2(!b->icon().isNull(),qPrintable(b->objectName()));QVERIFY(!b->accessibleName().isEmpty());}}
 }
 void renderPreview(){
    if(qEnvironmentVariableIsEmpty("TOON_UI_PREVIEW"))QSKIP("Preview output is opt-in");
    Canvas canvas;canvas.setProject(Project::create());QWidget gallery;auto*layout=new QHBoxLayout(&gallery);const QList<Canvas::Tool> tools={Canvas::Tool::Gradient,Canvas::Tool::VectorPen,Canvas::Tool::Text,Canvas::Tool::Retouch,Canvas::Tool::AdvancedTransform};
    QVector<Canvas*> canvases;for(auto t:tools){auto*c=new Canvas(&gallery);c->setProject(Project::create());c->hide();canvases<<c;auto*p=new DrawingToolPanel(c);p->setFixedSize(250,570);layout->addWidget(p);c->setTool(t);}gallery.show();QTest::qWait(30);QDir().mkpath(qEnvironmentVariable("TOON_UI_PREVIEW"));QVERIFY(gallery.grab().save(qEnvironmentVariable("TOON_UI_PREVIEW")+"/drawing-properties.png"));
 }
};
QTEST_MAIN(DrawingToolPanelTests)
#include "test_drawing_tool_panel.moc"
