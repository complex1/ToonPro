#include "symbol_library_panel.h"
#include "icons.h"
#include "canvas.h"
#include "symbols.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QAction>
#include <QDoubleSpinBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSignalSpy>
#include <QSlider>
#include <QStyle>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QtTest>
using namespace toon;
namespace {
Project smallProject(){auto project=Project::create();project.width=128;project.height=128;project.shots[0].durationFrames=24;return project;}
QToolButton *button(QWidget &panel,const char *id){return panel.findChild<QToolButton *>(id);}
void answerName(QWidget &panel,const QString &name){
    auto *timer=new QTimer(&panel);timer->setInterval(5);
    QObject::connect(timer,&QTimer::timeout,&panel,[&panel,timer,name]{auto *dialog=panel.findChild<QInputDialog *>("symbolNameDialog");if(!dialog||!dialog->isVisible())return;timer->stop();dialog->setTextValue(name);dialog->accept();timer->deleteLater();});
    QTimer::singleShot(3000,timer,[timer]{timer->stop();timer->deleteLater();});timer->start();
}
}
class SymbolsPanelTest final:public QObject{
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void insertFilterSelectionAndSharedEditing(){
        Canvas canvas;canvas.setProject(smallProject());auto *document=canvas.document();
        QString error;const auto eye=createEmptySymbol(document,"Eye blink",&error);QVERIFY2(!eye.isEmpty(),qPrintable(error));
        const auto mouth=createEmptySymbol(document,"Mouth",&error);QVERIFY(!mouth.isEmpty());
        TimelineController timeline(&canvas);SymbolLibraryPanel panel(&canvas,&timeline);panel.resize(240,520);panel.show();
        panel.setCurrentSymbol(eye);QSignalSpy edit(&panel,&SymbolLibraryPanel::editRequested);button(panel,"symbolEdit")->click();
        QCOMPARE(edit.size(),1);QCOMPARE(edit.first().first().toString(),eye);
        const int before=document->history()->count();button(panel,"symbolInsert")->click();QCOMPARE(document->history()->count(),before+1);
        QVERIFY(canvas.shot().layers[document->activeLayer()].symbol);QCOMPARE(canvas.shot().layers[document->activeLayer()].symbol->symbolId,eye);
        button(panel,"symbolInsert")->click();QCOMPARE(symbolUseCount(canvas.project(),eye),2);
        QVERIFY(!panel.findChild<QAction *>("symbolDelete")->isEnabled());
        const int selected=document->activeLayer();document->selectLayer(0);QVERIFY(panel.findChild<QWidget *>("symbolInstanceProperties")->isHidden());
        document->selectLayer(selected);QVERIFY(!panel.findChild<QWidget *>("symbolInstanceProperties")->isHidden());
        panel.editCurrentInstance();QCOMPARE(edit.size(),2);QCOMPARE(edit.last().first().toString(),eye);
        auto *search=panel.findChild<QLineEdit *>("symbolSearch");search->setText("mouth");auto *list=panel.findChild<QListWidget *>("symbolList");
        int shown=0;for(int i=0;i<list->count();++i)if(!list->item(i)->isHidden()){++shown;QCOMPARE(list->item(i)->text(),QString("Mouth"));}QCOMPARE(shown,1);
        search->clear();panel.setCurrentSymbol(mouth);QVERIFY(panel.findChild<QAction *>("symbolDelete")->isEnabled());
        panel.findChild<QAction *>("symbolDelete")->trigger();QVERIFY(!findSymbol(canvas.project(),mouth));document->history()->undo();QVERIFY(findSymbol(canvas.project(),mouth));
    }
    void playbackGesturesCommitOnceAndCancelPreservesTiming(){
        Canvas canvas;canvas.setProject(smallProject());auto *document=canvas.document();QString error;
        const auto id=createEmptySymbol(document,"Walk",&error);QVERIFY(!id.isEmpty());
        const auto original=findSymbol(canvas.project(),id)->content;auto content=original;content.durationFrames=12;
        QVERIFY(updateSymbolContent(document,id,content,original,&error));QVERIFY(!insertSymbolInstance(document,id,&error).isEmpty());
        TimelineController timeline(&canvas);SymbolLibraryPanel panel(&canvas,&timeline);panel.resize(240,520);panel.show();
        auto *mode=panel.findChild<QTabBar *>("symbolPlayback");mode->setCurrentIndex(int(SymbolPlayback::PingPong));
        QCOMPARE(canvas.shot().layers[document->activeLayer()].symbol->playback,SymbolPlayback::PingPong);
        auto *field=panel.findChild<ValueSlider *>("symbolSpeed");auto *slider=field->findChild<QSlider *>();const int before=document->history()->count();
        const int start=slider->value();slider->setSliderDown(true);slider->setValue(start+20);slider->setValue(start+40);QCOMPARE(document->history()->count(),before);
        slider->setSliderDown(false);QCOMPARE(document->history()->count(),before+1);
        QCOMPARE(canvas.shot().layers[document->activeLayer()].symbol->speed,field->value()/100.);
        const auto speed=canvas.shot().layers[document->activeLayer()].symbol->speed;
        slider->setSliderDown(true);slider->setValue(start);timeline.cancelEditing();QCOMPARE(canvas.shot().layers[document->activeLayer()].symbol->speed,speed);QCOMPARE(document->history()->count(),before+1);
        mode->setCurrentIndex(int(SymbolPlayback::SingleFrame));QVERIFY(!field->isEnabled());
        auto *first=panel.findChild<ValueSlider *>("symbolFirstFrame");first->setValue(7);QMetaObject::invokeMethod(first,"editingFinished");
        QCOMPARE(canvas.shot().layers[document->activeLayer()].symbol->firstFrame,6);
        document->history()->undo();QCOMPARE(canvas.shot().layers[document->activeLayer()].symbol->firstFrame,0);
    }
    void conversionAndUniqueActionsAreUndoable(){
        Canvas canvas;canvas.setProject(smallProject());auto *document=canvas.document();TimelineController timeline(&canvas);
        SymbolLibraryPanel panel(&canvas,&timeline);panel.resize(240,520);panel.show();
        const auto layerId=canvas.shot().layers[0].id;answerName(panel,"Reusable prop");panel.convertLayers({layerId});
        QCOMPARE(canvas.project().symbols.size(),1);QCOMPARE(canvas.shot().layers[0].kind,LayerKind::Symbol);
        const auto linked=canvas.shot().layers[0].symbol->symbolId;button(panel,"symbolInsert")->click();QCOMPARE(symbolUseCount(canvas.project(),linked),2);
        panel.findChild<QAction *>("symbolMakeUnique")->trigger();QCOMPARE(canvas.project().symbols.size(),2);
        QVERIFY(canvas.shot().layers[document->activeLayer()].symbol->symbolId!=linked);
        document->history()->undo();QCOMPARE(canvas.project().symbols.size(),1);
        panel.findChild<QAction *>("symbolBreakApart")->trigger();QVERIFY(!canvas.shot().layers[document->activeLayer()].symbol);
        document->history()->undo();QVERIFY(canvas.shot().layers[document->activeLayer()].symbol);
    }
    void compactControlsAndBlockedNavigation(){
        Canvas canvas;canvas.setProject(smallProject());TimelineController timeline(&canvas);SymbolLibraryPanel panel(&canvas,&timeline);panel.resize(240,500);panel.show();
        QString error;const auto id=createEmptySymbol(canvas.document(),"Prop",&error);insertSymbolInstance(canvas.document(),id,&error);QTest::qWait(10);
        QVERIFY(!appIcon("toon-symbol").isNull());
        auto *library=panel.findChild<QListWidget *>("symbolList");QVERIFY(library&&library->count());QVERIFY(!library->item(0)->icon().isNull());
        auto *tabs=panel.findChild<QTabBar *>("symbolPlayback");QVERIFY(tabs);
        for(int i=0;i<tabs->count();++i){QVERIFY(tabs->rect().contains(tabs->tabRect(i)));QVERIFY(!tabs->tabToolTip(i).isEmpty());}
        for(auto *control:panel.findChildren<QToolButton *>())if(control->isVisibleTo(&panel)){
            QVERIFY(!control->accessibleName().isEmpty());QVERIFY(!control->toolTip().isEmpty());
            QCOMPARE(control->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,control),600);
            QVERIFY(control->iconSize().width()<=16);
            const QRect bounds(control->mapTo(&panel,QPoint()),control->size());QVERIFY(bounds.left()>=0&&bounds.right()<panel.width());
        }
        for(auto *number:panel.findChildren<QDoubleSpinBox *>()){QCOMPARE(number->width(),48);QVERIFY(number->suffix().isEmpty());}
        panel.setTransitionGuard([]{return false;});const int before=canvas.document()->history()->count();button(panel,"symbolInsert")->click();QCOMPARE(canvas.document()->history()->count(),before);
        QSignalSpy edit(&panel,&SymbolLibraryPanel::editRequested);panel.editCurrentInstance();QVERIFY(edit.isEmpty());
    }
};
QTEST_MAIN(SymbolsPanelTest)
#include "test_symbols_panel.moc"
