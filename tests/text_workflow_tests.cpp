#include "brush_library.h"
#include "canvas.h"
#include "document.h"
#include "drawing_tool_controller.h"
#include "drawing_tool_panel.h"
#include "sheet.h"
#include "timeline_controller.h"
#include "tool_catalog.h"
#include "tool_session.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFontComboBox>
#include <QFontInfo>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QShortcut>
#include <QToolButton>
#include <QtTest>
using namespace toon;
namespace {
Project movie(){auto p=Project::create();p.width=640;p.height=360;return p;}
QPointF point(Canvas &canvas,QPointF artwork){return canvas.viewTransform().map(sheetGeometry(canvas.project(),canvas.shot()).output.topLeft()+artwork);}
void setup(Canvas &canvas){canvas.resize(900,650);canvas.setProject(movie());canvas.show();canvas.setTool(Canvas::Tool::Text);QCoreApplication::processEvents();}
void create(Canvas &canvas){QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,point(canvas,{40,50}).toPoint());QCoreApplication::processEvents();}
EditableObject object(Canvas &canvas){return canvas.shot().layers[canvas.document()->activeLayer()].objects.first();}
QPlainTextEdit *editor(Canvas &canvas){return canvas.findChild<QPlainTextEdit *>("artworkInlineText");}
void input(QPlainTextEdit *edit,const QString &text){edit->selectAll();QInputMethodEvent event;event.setCommitString(text);QApplication::sendEvent(edit,&event);}
QJsonObject read(const QString &path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return QJsonDocument::fromJson(file.readAll()).object();}
bool write(const QString &path,const QJsonObject &json){QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(QJsonDocument(json).toJson())>=0;}
}
class TextWorkflowTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void textToolDefaultMigrationAndRemovalAreStable(){
        QTemporaryDir directory;
        BrushLibrary library(directory.filePath("brushes.json"));
        QVERIFY(library.load());
        ToolCatalog current(&library,directory.filePath("current-tools.json"));
        QVERIFY(current.load()); QVERIFY(current.tool("text"));
        QCOMPARE(current.tool("text")->engine,Canvas::Tool::Text);
        QVERIFY(current.entry("entry/text")); QVERIFY(current.save());
        QCOMPARE(read(current.path())["version"].toInt(),12);

        // A pre-text catalog must contain legacy-compatible brushes. Relabeling
        // today's connected/surface presets as version 7 is intentionally invalid.
        ToolCatalog catalog(nullptr,directory.filePath("legacy-tools.json"));
        QVERIFY(catalog.load()); QVERIFY(catalog.save());
        auto legacy=read(catalog.path()); legacy["version"]=7;
        QJsonArray tools,entries;
        for(const auto &value:legacy["tools"].toArray())
            if(value.toObject()["id"]!="text") tools.append(value);
        for(const auto &value:legacy["entries"].toArray())
            if(value.toObject()["id"]!="entry/text") entries.append(value);
        legacy["tools"]=tools; legacy["entries"]=entries;
        const auto active=legacy["activeToolId"].toString();
        QVERIFY(write(catalog.path(),legacy));
        QString error;
        QVERIFY2(catalog.load(&error),qPrintable(error));
        QVERIFY(catalog.tool("text")); QVERIFY(catalog.entry("entry/text"));
        QCOMPARE(catalog.activeToolId(),active);
        QCOMPARE(catalog.entries().size(),entries.size()+1);
        QVERIFY(catalog.save()); QVERIFY(catalog.removeEntry("entry/text"));
        QVERIFY(catalog.save()); QVERIFY(catalog.load()); QVERIFY(!catalog.entry("entry/text"));
        Canvas canvas;setup(canvas);ToolSession session(&canvas,&catalog);QVERIFY(!catalog.addStandalone("text").isEmpty());QVERIFY(catalog.activateTool("text"));QCOMPARE(canvas.tool(),Canvas::Tool::Text);
    }
    void clickPlacementOpensInlineEditorAndSupportsUnicodeMultiline(){
        Canvas canvas;setup(canvas);create(canvas);QVERIFY(editor(canvas));QVERIFY(editor(canvas)->isVisible());QVERIFY(canvas.drawingTools()->textEditing());QCOMPARE(canvas.shot().layers[canvas.document()->activeLayer()].kind,LayerKind::Text);const int history=canvas.document()->history()->index();const auto original=object(canvas);
        input(editor(canvas),QString::fromUtf8("こんにちは\nScene 1 — 帰り道"));QCOMPARE(object(canvas).text,QString::fromUtf8("こんにちは\nScene 1 — 帰り道"));QCOMPARE(canvas.document()->history()->index(),history);QVERIFY(canvas.document()->externalEditActive());
        QTest::keyClick(editor(canvas),Qt::Key_Return,Qt::ControlModifier);QVERIFY(!canvas.drawingTools()->textEditing());QVERIFY(!editor(canvas)->isVisible());QCOMPARE(canvas.document()->history()->index(),history+1);QCOMPARE(canvas.tool(),Canvas::Tool::Text);
        canvas.document()->history()->undo();QCOMPARE(object(canvas),original);canvas.document()->history()->redo();QVERIFY(object(canvas).text.contains(QString::fromUtf8("こんにちは")));
    }
    void escapeRestoresArtworkAndTextUndoStaysInsideEditor(){
        Canvas canvas;setup(canvas);create(canvas);canvas.drawingTools()->finishTextEdit();const auto original=object(canvas);const int history=canvas.document()->history()->index();QVERIFY(canvas.drawingTools()->beginTextEdit());input(editor(canvas),"Changed text");QVERIFY(object(canvas).text!=original.text);
        editor(canvas)->undo();QCOMPARE(object(canvas).text,original.text);input(editor(canvas),"Canceled draft");QTest::keyClick(editor(canvas),Qt::Key_Escape);QCOMPARE(object(canvas),original);QCOMPARE(canvas.document()->history()->index(),history);QVERIFY(!canvas.document()->externalEditActive());
    }
    void doubleClickReopensExistingTextAndToolChangeCommits(){
        Canvas canvas;setup(canvas);create(canvas);input(editor(canvas),"Hello");canvas.drawingTools()->finishTextEdit();const auto id=object(canvas).id;const auto count=canvas.shot().layers.size();
        const auto bounds=editableObjectBounds(object(canvas));const auto at=point(canvas,bounds.center()).toPoint();QTest::mouseDClick(&canvas,Qt::LeftButton,Qt::NoModifier,at);QVERIFY(canvas.drawingTools()->textEditing());QCOMPARE(canvas.drawingTools()->selectedObjectId(),id);QCOMPARE(canvas.shot().layers.size(),count);
        input(editor(canvas),"Goodbye");canvas.setTool(Canvas::Tool::Brush);QVERIFY(!canvas.drawingTools()->textEditing());QCOMPARE(object(canvas).text,QString("Goodbye"));
    }
    void saveAndFrameTransitionFlushTextWithoutLosingStyles(){
        Canvas canvas;setup(canvas);TimelineController controller(&canvas);create(canvas);input(editor(canvas),"Saved dialogue");controller.finishEditing();QVERIFY(!canvas.drawingTools()->textEditing());QTemporaryDir directory;QString error;QVERIFY2(saveProject(canvas.project(),directory.filePath("text.toonproj"),&error),qPrintable(error));auto restored=loadProject(directory.filePath("text.toonproj"),&error);QVERIFY2(restored.has_value(),qPrintable(error));QCOMPARE(restored->shots[0].layers.last().objects.first().text,QString("Saved dialogue"));
        QVERIFY(canvas.drawingTools()->beginTextEdit());input(editor(canvas),"Next frame");QVERIFY(controller.setFrame(12));QVERIFY(!canvas.drawingTools()->textEditing());QCOMPARE(object(canvas).text,QString("Next frame"));QCOMPARE(canvas.document()->currentFrame(),12);
        QVERIFY(canvas.drawingTools()->beginTextEdit());input(editor(canvas),"Discarded");controller.cancelEditing();QCOMPARE(object(canvas).text,QString("Next frame"));QVERIFY(!canvas.document()->externalEditActive());
    }
    void propertyTextAndFontEditLiveThenCommitOnce(){
        Canvas canvas;setup(canvas);DrawingToolPanel panel(&canvas);panel.resize(260,650);panel.show();create(canvas);canvas.drawingTools()->finishTextEdit();auto *text=panel.findChild<QPlainTextEdit *>("artworkText");QVERIFY(text);const int history=canvas.document()->history()->index();text->setFocus();input(text,"Property draft");QCOMPARE(object(canvas).text,QString("Property draft"));QCOMPARE(canvas.document()->history()->index(),history);QVERIFY(canvas.drawingTools()->textEditing());
        QTest::keyClick(text,Qt::Key_Return,Qt::ControlModifier);QCOMPARE(canvas.document()->history()->index(),history+1);QVERIFY(!canvas.drawingTools()->textEditing());
        auto *size=panel.findChild<ValueSlider *>("artworkFontSize")->findChild<QDoubleSpinBox *>();size->setValue(48);QCOMPARE(object(canvas).fontSize,48.0);QVERIFY(!canvas.drawingTools()->textEditing());panel.findChild<QToolButton *>("artworkBold")->click();QVERIFY(object(canvas).bold);
        text->setFocus();input(text,"Not saved");QTest::keyClick(text,Qt::Key_Escape);QCOMPARE(object(canvas).text,QString("Property draft"));QCOMPARE(text->toPlainText(),QString("Property draft"));
    }
    void lockedTextAndExternalUndoCannotStartOrResurrectDrafts(){
        Canvas canvas;setup(canvas);create(canvas);canvas.drawingTools()->finishTextEdit();const auto layer=canvas.shot().layers[canvas.document()->activeLayer()].id;canvas.document()->setLayersLocked({layer},true);QVERIFY(!canvas.drawingTools()->beginTextEdit());canvas.document()->history()->undo();QVERIFY(canvas.drawingTools()->beginTextEdit());input(editor(canvas),"Transient");canvas.document()->history()->undo();QVERIFY(!canvas.drawingTools()->textEditing());QVERIFY(!editor(canvas)->isVisible());QVERIFY(!canvas.document()->externalEditActive());
    }
    void unchangedPropertyDraftEndsAndFontEditsPreserveColor(){
        Canvas canvas;setup(canvas);DrawingToolPanel panel(&canvas);panel.resize(260,650);panel.show();create(canvas);canvas.drawingTools()->finishTextEdit();auto *text=panel.findChild<QPlainTextEdit *>("artworkText");const auto original=object(canvas);const int history=canvas.document()->history()->index();
        text->setPlainText(original.text);canvas.drawingTools()->finishTextEdit();QVERIFY(!canvas.drawingTools()->textEditing());QVERIFY(!canvas.document()->externalEditActive());QCOMPARE(canvas.document()->history()->index(),history);
        canvas.setBrushColor(Qt::red);panel.findChild<ValueSlider *>("artworkFontSize")->setValue(40);QCOMPARE(object(canvas).fontSize,40.0);QCOMPARE(object(canvas).fill,original.fill);
        QVERIFY(canvas.drawingTools()->beginTextEdit());QVERIFY(canvas.drawingTools()->textEditing());QVERIFY(editor(canvas)->isVisible());QCOMPARE(canvas.document()->history()->index(),history+1);input(editor(canvas),"Inline after properties");QCOMPARE(object(canvas).text,QString("Inline after properties"));canvas.drawingTools()->finishTextEdit();QVERIFY(!canvas.document()->externalEditActive());QCOMPARE(canvas.document()->history()->index(),history+2);canvas.document()->history()->undo();QCOMPARE(object(canvas).text,original.text);QCOMPARE(object(canvas).fontSize,40.0);canvas.document()->history()->undo();QCOMPARE(object(canvas),original);
    }
    void inlinePenInputDoesNotReachCanvas(){
        Canvas canvas;setup(canvas);create(canvas);auto *edit=editor(canvas);QVERIFY(edit->isVisible());QVERIFY(edit->testAttribute(Qt::WA_NoMousePropagation));const auto original=object(canvas);const auto count=canvas.shot().layers[canvas.document()->activeLayer()].objects.size();const auto history=canvas.document()->history()->index();QPointingDevice pen("Text pen",105,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
        for(QWidget *target:{static_cast<QWidget *>(edit),edit->viewport()})for(auto type:{QEvent::TabletPress,QEvent::TabletMove,QEvent::TabletRelease}){const auto position=target->rect().center();const bool release=type==QEvent::TabletRelease;QTabletEvent event(type,&pen,position,target->mapToGlobal(position),release?0:.5,0,0,0,0,0,Qt::NoModifier,type==QEvent::TabletMove?Qt::NoButton:Qt::LeftButton,release?Qt::NoButton:Qt::LeftButton);event.setAccepted(false);QApplication::sendEvent(target,&event);QVERIFY(canvas.drawingTools()->textEditing());QVERIFY(!canvas.document()->strokeActive());}
        QCOMPARE(object(canvas),original);QCOMPARE(canvas.shot().layers[canvas.document()->activeLayer()].objects.size(),count);QCOMPARE(canvas.document()->history()->index(),history);
    }
    void saveShortcutResolvesInlineTextWhileUndoStaysLocal(){
        Canvas canvas;setup(canvas);TimelineController controller(&canvas);QShortcut save(QKeySequence::Save,&canvas);bool saved=false;connect(&save,&QShortcut::activated,this,[&]{controller.finishEditing();saved=true;});create(canvas);const auto original=object(canvas);const int history=canvas.document()->history()->index();input(editor(canvas),"Shortcut draft");QTest::keySequence(editor(canvas),QKeySequence::Undo);QCOMPARE(object(canvas),original);QCOMPARE(canvas.document()->history()->index(),history);input(editor(canvas),"Shortcut save");QTest::keySequence(editor(canvas),QKeySequence::Save);QVERIFY(saved);QVERIFY(!canvas.drawingTools()->textEditing());QCOMPARE(object(canvas).text,QString("Shortcut save"));QCOMPARE(canvas.document()->history()->index(),history+1);
    }
    void genericSavedFontResolvesToItsActualFamilyInProperties(){
        Canvas canvas;setup(canvas);create(canvas);canvas.drawingTools()->finishTextEdit();const auto original=object(canvas);DrawingToolPanel panel(&canvas);panel.resize(240,640);panel.show();canvas.drawingTools()->notifySettings();auto *font=panel.findChild<QFontComboBox *>("artworkFont");const auto resolved=QFontInfo(QFont(original.fontFamily)).family();QCOMPARE(font->currentText(),font->findText(resolved)>=0?resolved:original.fontFamily);QCOMPARE(QFontInfo(QFont(font->currentText())).family(),resolved);QCOMPARE(object(canvas),original);
    }
    void preview(){
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");if(output.isEmpty())QSKIP("Opt-in Text workflow preview.");Canvas canvas;setup(canvas);create(canvas);QVERIFY(editor(canvas)->isVisible());input(editor(canvas),QString::fromUtf8("帰り道\nThe way home"));QVERIFY(QDir().mkpath(output));QVERIFY(canvas.grab().save(output+"/text-inline.png"));canvas.drawingTools()->finishTextEdit();QVERIFY(canvas.grab().save(output+"/text-artwork.png"));DrawingToolPanel panel(&canvas);panel.resize(240,640);panel.show();canvas.drawingTools()->notifySettings();QCoreApplication::processEvents();QVERIFY(panel.grab().save(output+"/text-properties.png"));
    }
};
QTEST_MAIN(TextWorkflowTests)
#include "text_workflow_tests.moc"
