#include "symbol_editor_dialog.h"
#include "brush_library.h"
#include "canvas.h"
#include "drawing_tool_controller.h"
#include "object_tools.h"
#include "sheet.h"
#include "symbols.h"
#include "timeline_controller.h"
#include "tool_catalog.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QDoubleSpinBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>
using namespace toon;
namespace {
Project smallProject(){auto project=Project::create();project.width=128;project.height=128;project.shots[0].durationFrames=24;return project;}
void drawStroke(Canvas *canvas,QPointF from={25,25},QPointF to={90,75}){
    BrushSettings brush;brush.size=9;brush.color=QColor("#d24453");brush.pressureSize=false;canvas->applyBrushSettings(brush,0);canvas->setTool(Canvas::Tool::Brush);
    const auto drawing=sheetGeometry(canvas->project(),canvas->shot()).output.topLeft();
    const QPoint start=canvas->viewTransform().map(drawing+from).toPoint(),end=canvas->viewTransform().map(drawing+to).toPoint();
    QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,start);
    for(int i=1;i<=8;++i){const QPointF position=start+(end-start)*(i/8.);QMouseEvent event(QEvent::MouseMove,position,canvas->mapToGlobal(position),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(canvas,&event);}
    QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,end);
}
QByteArray contents(const QString &path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return file.readAll();}
}
class SymbolEditorTest final:public QObject{
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void canvasDrawingApplyUpdatesLinkedInstancesWithOneParentUndo(){
        Canvas source;source.setProject(smallProject());auto *document=source.document();QString error;
        const auto id=createEmptySymbol(document,"Face",&error);QVERIFY2(!id.isEmpty(),qPrintable(error));
        QVERIFY(!insertSymbolInstance(document,id,&error).isEmpty());QVERIFY(!insertSymbolInstance(document,id,&error).isEmpty());
        const auto beforeImage=renderMovieFrame(source.project());const int before=document->history()->count();
        SymbolEditorDialog editor(document,id,&source);editor.show();QTest::qWait(30);editor.canvas()->fitSheet();
        drawStroke(editor.canvas());QVERIFY(!editor.canvas()->shot().layers[0].tiles.isEmpty());
        QCOMPARE(renderMovieFrame(source.project()),beforeImage);QCOMPARE(document->history()->count(),before);
        QVERIFY2(editor.applyChanges(),qPrintable(editor.findChild<QLabel *>("symbolEditorError")->text()));
        QCOMPARE(document->history()->count(),before+1);QCOMPARE(symbolUseCount(source.project(),id),2);
        QVERIFY(!findSymbol(source.project(),id)->content.layers[0].tiles.isEmpty());QVERIFY(renderMovieFrame(source.project())!=beforeImage);
        const auto afterImage=renderMovieFrame(source.project());document->history()->undo();QCOMPARE(renderMovieFrame(source.project()),beforeImage);
        document->history()->redo();QCOMPARE(renderMovieFrame(source.project()),afterImage);
    }
    void cancellingAndLocalUndoLeaveSourceUnchanged(){
        Canvas source;source.setProject(smallProject());QString error;const auto id=createEmptySymbol(source.document(),"Cloud",&error);QVERIFY(!id.isEmpty());
        const int before=source.document()->history()->count();SymbolEditorDialog editor(source.document(),id,&source);editor.show();QTest::qWait(30);
        drawStroke(editor.canvas());QVERIFY(!editor.canvas()->shot().layers[0].tiles.isEmpty());
        editor.findChild<QToolButton *>("symbolEditorUndo")->click();QVERIFY(editor.canvas()->shot().layers[0].tiles.isEmpty());
        editor.findChild<QToolButton *>("symbolEditorRedo")->click();QVERIFY(!editor.canvas()->shot().layers[0].tiles.isEmpty());
        editor.reject();QCOMPARE(editor.result(),int(QDialog::Rejected));QCOMPARE(source.document()->history()->count(),before);
        QVERIFY(findSymbol(source.project(),id)->content.layers[0].tiles.isEmpty());
    }
    void vectorArtworkAndAnimatedDurationRemainEditable(){
        Canvas source;source.setProject(smallProject());QString error;const auto id=createEmptySymbol(source.document(),"Blink",&error);QVERIFY(!id.isEmpty());
        SymbolEditorDialog editor(source.document(),id,&source);editor.show();QTest::qWait(20);
        auto *duration=editor.findChild<ValueSlider *>("symbolDuration");duration->setValue(12);QMetaObject::invokeMethod(duration,"editingFinished");
        QCOMPARE(editor.canvas()->shot().durationFrames,12);
        QVERIFY(ObjectTools::addLayer(editor.canvas()->document(),LayerKind::Vector));
        const auto path=ObjectTools::addPath(editor.canvas()->document(),{BezierNode::corner({20,20}),BezierNode::corner({90,70})},false,Qt::black,4);QVERIFY(!path.isEmpty());
        QVERIFY(editor.timeline()->setFrame(5));QVERIFY(editor.applyChanges());
        const auto *definition=findSymbol(source.project(),id);QCOMPARE(definition->content.durationFrames,12);
        const auto &layer=definition->content.layers.last();QCOMPARE(layer.kind,LayerKind::Vector);QCOMPARE(layer.objects.first().id,path);
        SymbolEditorDialog reopened(source.document(),id,&source);QCOMPARE(reopened.canvas()->shot().layers.last().objects.first().id,path);
    }
    void staleDefinitionsCannotOverwriteNewerArtwork(){
        Canvas source;source.setProject(smallProject());QString error;const auto id=createEmptySymbol(source.document(),"Prop",&error);QVERIFY(!id.isEmpty());
        SymbolEditorDialog editor(source.document(),id,&source);editor.show();QTest::qWait(20);drawStroke(editor.canvas());
        const auto original=findSymbol(source.project(),id)->content;auto updated=original;updated.durationFrames=8;
        QVERIFY(updateSymbolContent(source.document(),id,updated,original,&error));const int before=source.document()->history()->count();
        QVERIFY(!editor.applyChanges());QVERIFY(editor.findChild<QLabel *>("symbolEditorError")->text().contains("changed"));
        QCOMPARE(source.document()->history()->count(),before);QCOMPARE(findSymbol(source.project(),id)->content.durationFrames,8);
        QVERIFY(findSymbol(source.project(),id)->content.layers[0].tiles.isEmpty());
    }
    void customBrushCatalogIsIsolatedAndKeyboardEntryDoesNotCloseEditor(){
        QTemporaryDir directory;BrushLibrary library(directory.filePath("brushes.json"));QVERIFY(library.load());
        ToolCatalog catalog(&library,directory.filePath("tools.json"));QVERIFY(catalog.load());ToolState state;state.brush.size=23;state.brush.color=Qt::magenta;
        const auto custom=catalog.createBrush("My custom ink",BrushType::Custom,state);QVERIFY(!custom.isEmpty());QVERIFY(catalog.save());const auto saved=contents(catalog.path());const auto sourceActive=catalog.activeToolId();
        Canvas source;source.setProject(smallProject());source.applyBrushSettings(state.brush,0);QString error;const auto id=createEmptySymbol(source.document(),"Custom",&error);QVERIFY(!id.isEmpty());
        SymbolEditorDialog editor(source.document(),id,&source,&catalog);editor.show();editor.activateWindow();QVERIFY(QTest::qWaitForWindowActive(&editor));
        auto *local=editor.findChild<ToolCatalog *>();QVERIFY(local);QVERIFY(local!=&catalog);QVERIFY(local->tool(custom));QVERIFY(local->path()!=catalog.path());
        const auto sourceTool=catalog.tools().first().id;QVERIFY(local->activateTool(sourceTool));QVERIFY(local->activateTool(custom));QCOMPARE(editor.canvas()->brush.size,23.);QCOMPARE(catalog.activeToolId(),sourceActive);
        auto *number=editor.findChild<ValueSlider *>("symbolDuration")->findChild<QDoubleSpinBox *>();number->setFocus();number->selectAll();QTest::keyClicks(number,"18");QTest::keyClick(number,Qt::Key_Return);
        QVERIFY(editor.isVisible());QCOMPARE(editor.canvas()->shot().durationFrames,18);QCOMPARE(contents(catalog.path()),saved);
        editor.reject();QCOMPARE(contents(catalog.path()),saved);
    }
};
QTEST_MAIN(SymbolEditorTest)
#include "test_symbol_editor.moc"
