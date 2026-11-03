#include "canvas.h"
#include "document.h"
#include "main_window.h"
#include "symbol_library_panel.h"
#include "symbol_editor_dialog.h"
#include "symbols.h"
#include "keyframe_transform_tool.h"
#include "light_table.h"
#include "timeline_controller.h"
#include "sheet.h"
#include <QAction>
#include <QDockWidget>
#include <QDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <QUuid>
#include <QtTest>
#include <functional>
using namespace toon;
namespace {
Project fixture() {
    auto project=Project::create();project.width=320;project.height=180;
    auto content=Project::create().shots[0];content.durationFrames=12;
    content.layers[0].name="Lantern lines";
    QImage pixels(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);pixels.fill(Qt::transparent);
    QPainter p(&pixels);p.fillRect(QRect(45,35,40,60),QColor("#e6ad65"));p.end();
    content.layers[0].tiles[{0,0}]=pixels;
    SymbolDefinition definition{QUuid::createUuid().toString(QUuid::WithoutBraces),"Festival lantern",content};
    project.symbols.append(definition);
    auto a=RasterLayer::create("Lantern A");a.kind=LayerKind::Symbol;a.symbol=SymbolInstance{definition.id};
    auto b=a;b.id=QUuid::createUuid().toString(QUuid::WithoutBraces);b.name="Lantern B";
    project.shots[0].layers={a,b};
    project.shots[0].layerAnimations.append({b.id,{},{{KeyframeProperty::PositionX,{{0,130}}}}, {}});
    return project;
}
QString launch(QToolButton *button,const std::function<QString(SymbolEditorDialog&)> &inspect) {
    QString error;bool opened=false;QTimer timer,timeout;timer.setInterval(5);timeout.setSingleShot(true);
    QObject::connect(&timer,&QTimer::timeout,[&]{
        if(auto *editor=qobject_cast<SymbolEditorDialog*>(QApplication::activeModalWidget())){
            // A registered modal may not yet be an exposed/active Cocoa window.
            // Real keyboard/button input cannot arrive until that hand-off has
            // completed; do not reject it halfway through native presentation.
            if(!editor->windowHandle()||!editor->windowHandle()->isExposed()||!editor->isActiveWindow())return;
            timer.stop();opened=true;error=inspect(*editor);if(editor->isVisible())editor->reject();
        }else if(auto *message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){
            error=message->text();timer.stop();message->reject();
        }
    });
    QObject::connect(&timeout,&QTimer::timeout,[&]{
        auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        error=QString("Symbol editor did not finish: visited=%1 app=%2 visible=%3 active=%4 exposed=%5")
            .arg(opened).arg(int(QGuiApplication::applicationState())).arg(dialog&&dialog->isVisible())
            .arg(dialog&&dialog->isActiveWindow()).arg(dialog&&dialog->windowHandle()&&dialog->windowHandle()->isExposed());
        if(dialog)dialog->reject();
    });
    timer.start();timeout.start(20000);button->click();timer.stop();timeout.stop();
    if(!opened&&error.isEmpty())error="Symbol editor did not open";return error;
}
SymbolLibraryPanel *showSymbols(MainWindow &window){
    auto *dock=window.findChild<QDockWidget*>("symbolsPanel");if(!dock)return nullptr;
    dock->show();dock->raise();return dock->findChild<SymbolLibraryPanel*>();
}
QString focusState(const MainWindow &window){
    const auto describe=[](QWidget *widget){return widget?QString("%1(%2)").arg(widget->metaObject()->className(),widget->objectName()):QString("none");};
    return QString("app=%1 parentActive=%2 nativeActive=%3 active=%4 focus=%5 modal=%6")
        .arg(int(QGuiApplication::applicationState())).arg(window.isActiveWindow())
        .arg(window.windowHandle()&&window.windowHandle()->isActive())
        .arg(describe(QApplication::activeWindow()),describe(QApplication::focusWidget()),describe(QApplication::activeModalWidget()));
}
bool showAndActivate(MainWindow &window){
    window.show();
    if(!QTest::qWaitForWindowExposed(&window))return false;
    window.raise();window.activateWindow();
    return QTest::qWaitForWindowActive(&window);
}
}
class SymbolsWorkspaceTests:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void editorCancelKeepsParentAndRestoresCanvasFocus(){
        QTemporaryDir directory;QString setupError;const auto path=directory.filePath("cancel.toonproj");
        QVERIFY2(saveProject(fixture(),path,&setupError),qPrintable(setupError));
        MainWindow window(directory.filePath("workspace.ini"));QVERIFY(window.openProject(path));
        QVERIFY2(showAndActivate(window),qPrintable(focusState(window)));
        auto *panel=showSymbols(window);QVERIFY(panel);auto *edit=panel->findChild<QToolButton*>("symbolEdit");QVERIFY(edit&&edit->isEnabled());
        auto *document=window.canvas()->document();const auto before=document->project().toJson();
        const auto error=launch(edit,[&](SymbolEditorDialog &editor)->QString{
            auto *local=editor.canvas()->document();local->addLayer();
            if(document->project().toJson()!=before)return "Draft edited parent before Save";
            const auto capture=qEnvironmentVariable("TOON_SYMBOL_CAPTURE");
            if(!capture.isEmpty())editor.grab().save(capture+"-editor.png");
            QTest::keyClick(&editor,Qt::Key_Escape);return {};
        });
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(document->project().toJson(),before);QCOMPARE(document->history()->count(),0);
        QTRY_VERIFY2(window.canvas()->hasFocus(),qPrintable(focusState(window)));
        const auto capture=qEnvironmentVariable("TOON_SYMBOL_CAPTURE");if(!capture.isEmpty())window.grab().save(capture+"-workspace.png");
        QVERIFY(window.close());
    }
    void editorSaveUpdatesBothInstancesAndPersistsWithOneUndo(){
        QTemporaryDir directory;QString error;auto project=fixture();const auto path=directory.filePath("lantern.toonproj");QVERIFY(saveProject(project,path,&error));
        MainWindow window(directory.filePath("workspace.ini"));QVERIFY(window.openProject(path));
        QVERIFY2(showAndActivate(window),qPrintable(focusState(window)));
        auto *panel=showSymbols(window);QVERIFY(panel);auto *edit=panel->findChild<QToolButton*>("symbolEdit");QVERIFY(edit);
        auto *document=window.canvas()->document();
        error=launch(edit,[&](SymbolEditorDialog &editor)->QString{
            auto changed=editor.canvas()->project();auto &pixel=changed.shots[0].layers[0].tiles[{0,0}];
            QPainter painter(&pixel);painter.fillRect(QRect(45,35,40,60),QColor("#73bea5"));painter.end();
            editor.canvas()->document()->replaceProject(changed,"Change lantern color");
            auto *save=editor.findChild<QPushButton*>("symbolEditorSave");
            if(!save)return "Save changes button is missing";
            save->click();
            if(editor.result()!=QDialog::Accepted)return "Save changes did not close the editor";
            return {};
        });
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(document->history()->count(),1);
        QTRY_VERIFY2(window.canvas()->hasFocus(),qPrintable(focusState(window)));
        auto frame=renderMovieFrame(document->project(),0,document->activeShotIndex());
        QCOMPARE(frame.pixelColor(60,50),QColor("#73bea5"));QCOMPARE(frame.pixelColor(190,50),QColor("#73bea5"));
        document->history()->undo();frame=renderMovieFrame(document->project(),0,document->activeShotIndex());QCOMPARE(frame.pixelColor(60,50),QColor("#e6ad65"));
        document->history()->redo();auto *save=window.findChild<QAction*>("saveProjectAction");QVERIFY(save);save->trigger();
        QVERIFY(document->history()->isClean());auto reopened=loadProject(path,&error);QVERIFY2(reopened,qPrintable(error));
        frame=renderMovieFrame(*reopened,0,0);QCOMPARE(frame.pixelColor(190,50),QColor("#73bea5"));QVERIFY(window.close());
    }
    void closingEditorDoesNotTakeFocusFromAnotherWindow(){
        QTemporaryDir directory;QString error;const auto path=directory.filePath("other-window.toonproj");
        QVERIFY2(saveProject(fixture(),path,&error),qPrintable(error));
        QWidget other;other.setObjectName("otherWorkspace");auto *layout=new QVBoxLayout(&other);
        auto *text=new QLineEdit;text->setObjectName("otherEditor");layout->addWidget(text);
        MainWindow window(directory.filePath("workspace.ini"));QVERIFY(window.openProject(path));
        QVERIFY2(showAndActivate(window),qPrintable(focusState(window)));
        auto *panel=showSymbols(window);QVERIFY(panel);auto *edit=panel->findChild<QToolButton*>("symbolEdit");QVERIFY(edit);
        const auto before=window.canvas()->project().toJson();
        error=launch(edit,[&](SymbolEditorDialog &editor)->QString{
            // Present another destination only after the modal closes. Opening
            // two native windows before the test begins queues competing Cocoa
            // activation events and does not model this closing-time hand-off.
            QObject::connect(&editor,&QDialog::finished,&other,[&]{
                other.show();QApplication::setActiveWindow(&other);text->setFocus();
            });
            editor.reject();
            return {};
        });
        QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(QApplication::activeWindow(),&other);QVERIFY(text->hasFocus());QVERIFY(!window.canvas()->hasFocus());
        QCOMPARE(window.canvas()->project().toJson(),before);QCOMPARE(window.canvas()->document()->history()->count(),0);
        QVERIFY(window.close());
    }
    void effectsWorkerRetainsSharedSymbolContentsAndPublishesEdits(){
        auto project=fixture();auto &fx=project.symbols[0].content.layers[0].effects;fx.colorEnabled=true;fx.saturation=-100;
        Canvas canvas;canvas.resize(800,600);QSignalSpy pending(&canvas,&Canvas::effectsPreviewPendingChanged);
        canvas.setProject(project);canvas.show();canvas.fitSheet();
        QEvent leave(QEvent::Leave);QApplication::sendEvent(&canvas,&leave);canvas.repaint();
        QTRY_VERIFY_WITH_TIMEOUT(!pending.isEmpty()&&!canvas.effectsPreviewPending(),15000);
        const auto sample=[&]{const auto image=canvas.grab().toImage();const auto p=canvas.viewTransform().map(sheetGeometry(canvas.project(),canvas.shot()).output.topLeft()+QPointF(60,50))*canvas.devicePixelRatioF();return image.pixelColor(qRound(p.x()),qRound(p.y()));};
        QCOMPARE(sample(),renderMovieFrame(canvas.project()).pixelColor(60,50));
        const auto before=canvas.project().symbols[0].content;auto content=before;
        QPainter painter(&content.layers[0].tiles[{0,0}]);painter.fillRect(QRect(45,35,40,60),QColor("#73bea5"));painter.end();
        QString error;QVERIFY2(updateSymbolContent(canvas.document(),project.symbols[0].id,content,before,&error),qPrintable(error));
        canvas.repaint();QTRY_VERIFY_WITH_TIMEOUT(!canvas.effectsPreviewPending(),15000);
        QCOMPARE(sample(),renderMovieFrame(canvas.project()).pixelColor(60,50));
    }
    void lightTableSnapshotsResolvePosedSymbolsWithoutChangingTheirSource(){
        auto project=fixture();Canvas canvas;canvas.setProject(project);LightTablePanel panel(&canvas);QString error;
        QVERIFY2(panel.pinLayer(project.shots[0].id,project.shots[0].layers[1].id,&error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),10000);QCOMPARE(panel.pins().size(),1);
        const auto &pin=panel.pins()[0];QVERIFY(qAbs(pin.bounds.left()-175)<2);QVERIFY(qAbs(pin.bounds.width()-40)<2);
        QCOMPARE(pin.image.pixelColor(pin.image.width()/2,pin.image.height()/2),QColor("#e6ad65"));
        QCOMPARE(canvas.project().symbols[0].content.layers[0].tiles,project.symbols[0].content.layers[0].tiles);
    }
    void transformUsesLinkedContentBoundsAndPixelToolsGiveClearReason(){
        Canvas canvas;TimelineController timeline(&canvas);canvas.resize(800,600);canvas.setProject(fixture());canvas.show();canvas.fitSheet();
        auto *document=canvas.document();document->selectLayer(0);canvas.setTool(Canvas::Tool::Brush);
        QVERIFY(canvas.toolBlockReason().contains("symbol",Qt::CaseInsensitive));
        canvas.setTool(Canvas::Tool::Transform);QVERIFY(canvas.keyframeTransformTool()->enabled());QVERIFY(canvas.toolBlockReason().isEmpty());
        const auto bounds=canvas.keyframeTransformTool()->box().boundingRect();QVERIFY2(bounds.width()<100,qPrintable(QString::number(bounds.width())));
        auto *tool=canvas.keyframeTransformTool();const auto start=tool->box().boundingRect().center().toPoint();const auto end=start+QPoint(35,20);
        const auto master=document->project().symbols[0].content.layers[0].tiles;
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,start);
        QMouseEvent move(QEvent::MouseMove,end,canvas.mapToGlobal(end),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&canvas,&move);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,end);
        QCOMPARE(document->history()->count(),1);QVERIFY(document->activeShot().layers[0].symbol.has_value());
        QCOMPARE(document->project().symbols[0].content.layers[0].tiles,master);document->history()->undo();
        QVERIFY(document->setKeyframeValue(document->activeShot().layers[0].id,KeyframeProperty::PositionX,20));
        auto frame=renderMovieFrame(document->project(),0,document->activeShotIndex());QCOMPARE(frame.pixelColor(90,50),QColor("#e6ad65"));
        canvas.setTool(Canvas::Tool::Brush);QVERIFY(!tool->enabled());QVERIFY(!canvas.toolBlockReason().isEmpty());
        QVERIFY(!document->flattenImage());QVERIFY(document->activeShot().layers[0].symbol.has_value());
    }
};
QTEST_MAIN(SymbolsWorkspaceTests)
#include "test_symbols_workspace.moc"
