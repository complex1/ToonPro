#include "animation_test_fixture.h"
#include "canvas.h"
#include "document.h"
#include "timeline_controller.h"
#include "timeline_panel.h"
#include "ui_theme.h"
#include <QAbstractScrollArea>
#include <QApplication>
#include <QAction>
#include <QContextMenuEvent>
#include <QMenu>
#include <QWindow>
#include <QDir>
#include <QInputDialog>
#include <QMouseEvent>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

using namespace toon;
namespace {
Project fixture(){
    auto p=Project::create();p.width=192;p.height=128;p.shots[0].durationFrames=48;
    auto background=RasterLayer::create("Background"),folder=RasterLayer::create("Character"),ink=RasterLayer::create("Line art"),paint=RasterLayer::create("Colors"),foreground=RasterLayer::create("Effects");
    folder.group=true;ink.parentId=paint.parentId=folder.id;p.shots[0].layers={background,folder,paint,ink,foreground};return p;
}
QString named(const Canvas &canvas,const QString &name){for(const auto &layer:canvas.shot().layers)if(layer.name==name)return layer.id;return {};}
const RasterLayer &layer(const Canvas &canvas,const QString &id){for(const auto &layer:canvas.shot().layers)if(layer.id==id)return layer;qFatal("Missing fixture layer");}
QAbstractScrollArea *grid(QWidget &panel){return panel.findChild<QAbstractScrollArea *>("timelineGrid");}
void settle(){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);}
void show(TimelinePanel &panel,QSize size={1050,520}){panel.resize(size);panel.show();QVERIFY(QTest::qWaitForWindowExposed(&panel));panel.raise();panel.activateWindow();QVERIFY(QTest::qWaitForWindowActive(&panel));settle();}
QStringList rows(QAbstractScrollArea *view){return view->property("timelineRowIds").toStringList();}
int rowTop(QAbstractScrollArea *view,const QString &id){return view->property("timelineRulerHeight").toInt()+rows(view).indexOf(id)*view->property("timelineRowHeight").toInt()-view->verticalScrollBar()->value();}
QPoint header(QAbstractScrollArea *view,const QString &id,int offset=-1){return {100,rowTop(view,id)+(offset<0?view->property("timelineRowHeight").toInt()/2:offset)};}
QToolButton *button(QWidget &panel,const QString &id,const QString &action){for(auto *b:panel.findChildren<QToolButton *>())if(b->property("timelineLayerId")==id&&b->property("timelineRowAction")==action&&b->isVisibleTo(&panel))return b;return nullptr;}
void move(QAbstractScrollArea *view,QPoint p){QMouseEvent event(QEvent::MouseMove,p,view->viewport()->mapToGlobal(p),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view->viewport(),&event);}
void drag(QAbstractScrollArea *view,QPoint from,QPoint to){QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,from);move(view,to);QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,to);}
}
class TimelineOrganizationPanelTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();QVERIFY(fixture().validate().isEmpty());}
    void visibilityAndLockAreDirectAndFollowDocumentUndo(){
        Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);TimelinePanel panel(&c);show(panel);
        const auto ink=named(canvas,"Line art");const int active=canvas.document()->activeLayer(),frame=c.currentFrame();
        auto *eye=button(panel,ink,"visible");QVERIFY(eye);eye->click();QVERIFY(!layer(canvas,ink).visible);QCOMPARE(canvas.document()->history()->count(),1);QCOMPARE(canvas.document()->activeLayer(),active);QCOMPARE(c.currentFrame(),frame);
        canvas.document()->history()->undo();QVERIFY(layer(canvas,ink).visible);QVERIFY(button(panel,ink,"visible")->toolTip().startsWith("Hide"));
        button(panel,ink,"lock")->click();QVERIFY(layer(canvas,ink).locked);QVERIFY(button(panel,ink,"lock")->isChecked());
        canvas.document()->setLayersLocked({ink},false);QVERIFY(!button(panel,ink,"lock")->isChecked());
        canvas.document()->setLayersVisible({ink},false);QVERIFY(button(panel,ink,"visible")->toolTip().startsWith("Show"));
    }
    void soloIsPreviewOnlyAndClearActionRestoresAll(){
        Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);TimelinePanel panel(&c);show(panel);const auto ink=named(canvas,"Line art"),paint=named(canvas,"Colors");const auto before=canvas.project().toJson();
        button(panel,ink,"solo")->click();QCOMPARE(c.timelineSoloLayers(),QSet<QString>{ink});QVERIFY(canvas.timelineSoloIncludesLayer(ink));QVERIFY(!canvas.timelineSoloIncludesLayer(paint));
        auto *clear=panel.findChild<QToolButton *>("timelineClearSolo");QVERIFY(clear->isVisible());QVERIFY(button(panel,ink,"solo")->isChecked());
        clear->click();QVERIFY(c.timelineSoloLayers().isEmpty());QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),0);QVERIFY(!clear->isVisible());
    }
    void collapseHidesChildrenWithoutChangingSelectionOrTiming(){
        auto project=animation_fixture::project();auto &shot=project.shots[0];auto folder=RasterLayer::create("Cast");folder.group=true;const auto trackId=shot.animationTracks[0].id,rootId=shot.animationTracks[0].rootGroupLayerId;
        shot.layers[animation_fixture::layerIndex(shot,rootId)].parentId=folder.id;shot.layers.append(folder);QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        Canvas canvas;canvas.setProject(project);TimelineController c(&canvas);TimelinePanel panel(&c);show(panel);auto *view=grid(panel);QVERIFY(rows(view).contains(trackId));
        QVERIFY(c.selectTrack(trackId));const auto exposure=canvas.shot().animationTracks[0].exposures.first();c.setSelectedCels({{trackId,exposure.id}});const auto before=canvas.project().toJson();
        button(panel,folder.id,"collapse")->click();QVERIFY(!rows(view).contains(trackId));QCOMPARE(c.selectedCels().size(),1);QCOMPARE(c.selectedCels()[0].exposureId,exposure.id);QCOMPARE(c.selectedTrackId(),trackId);
        button(panel,folder.id,"collapse")->click();QVERIFY(rows(view).contains(trackId));QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),0);
        for(const auto &drawing:canvas.shot().animationTracks[0].drawings)QVERIFY(!rows(view).contains(drawing.groupLayerId));
    }
    void headerDragReordersOnReleaseAndUndoRestores(){
        Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);TimelinePanel panel(&c);show(panel);auto *view=grid(panel);const auto top=named(canvas,"Effects"),bottom=named(canvas,"Background");const auto before=canvas.project().toJson();
        const auto from=header(view,top),to=header(view,bottom,view->property("timelineRowHeight").toInt()-3);
        QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,from);move(view,to);QVERIFY(view->property("timelineDropValid").toBool());QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),0);
        QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,to);QCOMPARE(canvas.shot().layers.first().id,top);QCOMPARE(canvas.document()->history()->count(),1);QCOMPARE(c.currentFrame(),0);
        canvas.document()->history()->undo();QCOMPARE(canvas.project().toJson(),before);
    }
    void dropInsideFolderAndKeyboardReorderingPreserveTree(){
        Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);TimelinePanel panel(&c);show(panel);auto *view=grid(panel);const auto top=named(canvas,"Effects"),folder=named(canvas,"Character"),ink=named(canvas,"Line art");
        drag(view,header(view,top),header(view,folder));QCOMPARE(layer(canvas,top).parentId,folder);QCOMPARE(canvas.document()->history()->count(),1);QVERIFY(rows(view).indexOf(top)>rows(view).indexOf(folder));
        QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,header(view,top));QTest::keyClick(view,Qt::Key_Down,Qt::ControlModifier|Qt::ShiftModifier);
        QVERIFY(rows(view).indexOf(top)>rows(view).indexOf(ink));QCOMPARE(layer(canvas,top).parentId,folder);QCOMPARE(canvas.document()->history()->count(),2);
    }
    void invalidLockedDropAndCancelledDragNeverCommit(){
        Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);TimelinePanel panel(&c);show(panel);auto *view=grid(panel);const auto top=named(canvas,"Effects"),folder=named(canvas,"Character");
        canvas.document()->setLayersLocked({folder},true);const auto before=canvas.project().toJson();const int history=canvas.document()->history()->count();
        QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,header(view,top));move(view,header(view,folder));QVERIFY(!view->property("timelineDropValid").toBool());QVERIFY(!view->property("timelineDropError").toString().isEmpty());QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,header(view,folder));QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),history);
        canvas.document()->setLayersLocked({folder},false);const auto unlocked=canvas.project().toJson();
        for(bool deactivate:{false,true}){QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,header(view,top));move(view,header(view,folder));
            if(deactivate){QEvent event(QEvent::WindowDeactivate);QApplication::sendEvent(view,&event);}else QTest::keyClick(view,Qt::Key_Escape);
            QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,header(view,folder));QCOMPARE(canvas.project().toJson(),unlocked);}
    }
    void folderCannotCollapseDuringHeaderDragOrAcceptRejectedTransition(){
        Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);TimelinePanel panel(&c);show(panel);auto *view=grid(panel);const auto top=named(canvas,"Effects"),folder=named(canvas,"Character");
        QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,header(view,top));QVERIFY(!button(panel,folder,"collapse")->isEnabled());QTest::keyClick(view,Qt::Key_Escape);QVERIFY(button(panel,folder,"collapse")->isEnabled());
        const auto before=canvas.project().toJson();c.setTransitionGuard([]{return false;});button(panel,top,"visible")->click();QCOMPARE(canvas.project().toJson(),before);
        drag(view,header(view,top),header(view,folder));QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),0);
    }
    void heldHeaderDragCannotDeleteNudgeOrImplicitlyCommitTiming(){
        auto project=animation_fixture::project();auto &shot=project.shots[0];auto folder=RasterLayer::create("Cast");folder.group=true;shot.layers.append(folder);
        SymbolDefinition definition{animation_fixture::id(),"Mouth",Project::create().shots[0]};definition.content.durationFrames=4;project.symbols.append(definition);
        auto symbol=RasterLayer::create("Mouth poses");symbol.kind=LayerKind::Symbol;symbol.symbol=SymbolInstance{definition.id,SymbolPlayback::Loop,0,1,1,12,0};shot.layers.append(symbol);
        QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        Canvas canvas;canvas.setProject(project);TimelineController c(&canvas);TimelinePanel panel(&c);show(panel);auto *view=grid(panel);
        const auto before=canvas.project().toJson();
        for(const auto &rowId:QStringList{shot.animationTracks[0].id,"symbol:"+symbol.id}){
            const auto from=header(view,rowId);QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,from);const auto to=header(view,folder.id);
            move(view,to);QVERIFY(view->property("timelineDropValid").toBool());
            QTest::keyClick(view,Qt::Key_Delete);QTest::keyClick(view,Qt::Key_Backspace);QTest::keyClick(view,Qt::Key_Right,Qt::AltModifier);QTest::keyClick(view,Qt::Key_Left,Qt::AltModifier|Qt::ShiftModifier);
            QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),0);QVERIFY(view->property("timelineDropValid").toBool());
            QTest::keyClick(view,Qt::Key_Escape);QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,to);QCOMPARE(canvas.project().toJson(),before);
            QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,header(view,rowId));move(view,header(view,folder.id));QVERIFY(view->property("timelineDropValid").toBool());
            QVERIFY(c.setFrame(c.currentFrame()+1));QVERIFY(!view->property("timelineDropValid").toBool());
            QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,header(view,folder.id));QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),0);
        }
    }
    void createNamedFolderThroughDirectAction(){
        Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);TimelinePanel panel(&c);show(panel);bool accepted=false;
        QTimer poll;poll.setInterval(5);QTimer timeout;timeout.setSingleShot(true);
        connect(&poll,&QTimer::timeout,&panel,[&]{if(auto *dialog=qobject_cast<QInputDialog *>(QApplication::activeModalWidget())){if(!dialog->isActiveWindow())return;poll.stop();dialog->setTextValue("Backgrounds");accepted=true;dialog->accept();}});
        connect(&timeout,&QTimer::timeout,&panel,[]{if(auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()))dialog->reject();});
        poll.start();timeout.start(15000);panel.findChild<QToolButton *>("timelineAddFolder")->click();poll.stop();timeout.stop();QVERIFY(accepted);
        const auto id=named(canvas,"Backgrounds");QVERIFY(!id.isEmpty());QVERIFY(layer(canvas,id).group);QCOMPARE(canvas.document()->history()->count(),1);QVERIFY(rows(grid(panel)).contains(id));
    }
    void headerMenusRetainTrackActionsAndProtectAnimationFolders(){
        Canvas canvas;canvas.setProject(fixture());TimelineController c(&canvas);TimelinePanel panel(&c);show(panel);auto *view=grid(panel);
        const auto states=[&](const QString &rowId){
            QHash<QString,bool> result;QTimer poll;poll.setInterval(5);QTimer deadline;deadline.setSingleShot(true);
            connect(&poll,&QTimer::timeout,&panel,[&]{auto *menu=view->findChild<QMenu *>("timelineOrganizationMenu");
                if(!menu||!menu->isVisible()||!menu->windowHandle()||!menu->windowHandle()->isExposed())return;
                poll.stop();for(auto *action:menu->actions())if(!action->objectName().isEmpty())result.insert(action->objectName(),action->isEnabled());menu->close();});
            connect(&deadline,&QTimer::timeout,&panel,[]{if(auto *popup=QApplication::activePopupWidget())popup->close();});
            const auto point=header(view,rowId);QContextMenuEvent event(QContextMenuEvent::Mouse,point,view->viewport()->mapToGlobal(point));
            poll.start();deadline.start(10000);QApplication::sendEvent(view->viewport(),&event);poll.stop();deadline.stop();return result;
        };
        auto actions=states(named(canvas,"Effects"));QVERIFY(actions.value("timelineConvertTrack"));QVERIFY(actions.value("timelineDeleteTrack"));
        actions=states(named(canvas,"Line art"));QVERIFY(actions.contains("timelineConvertTrack"));QVERIFY(!actions.value("timelineConvertTrack"));QVERIFY(actions.value("timelineDeleteTrack"));
        auto project=animation_fixture::project();auto &shot=project.shots[0];auto folder=RasterLayer::create("Cast");folder.group=true;const auto track=shot.animationTracks[0];
        shot.layers[animation_fixture::layerIndex(shot,track.rootGroupLayerId)].parentId=folder.id;shot.layers.append(folder);canvas.setProject(project);settle();
        const auto before=canvas.project().toJson();actions=states(folder.id);QVERIFY(actions.contains("timelineDeleteTrack"));QVERIFY(!actions.value("timelineDeleteTrack"));QVERIFY(!actions.value("timelineConvertTrack"));
        actions=states(track.id);QVERIFY(actions.value("timelineDeleteTrack"));QVERIFY(!actions.contains("timelineConvertTrack"));QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),0);
    }
    void visibleRowButtonsStayCompactAccessibleAndBounded(){
        auto project=fixture();for(int i=0;i<30;++i)project.shots[0].layers.append(RasterLayer::create(QString("Extra %1").arg(i)));
        Canvas canvas;canvas.setProject(project);TimelineController c(&canvas);TimelinePanel panel(&c);show(panel,{820,420});auto *view=grid(panel);
        for(int pass=0;pass<2;++pass){if(pass){auto f=view->font();f.setPointSize(18);view->setFont(f);settle();}view->verticalScrollBar()->setValue(pass?view->verticalScrollBar()->maximum():0);settle();int count=0;
            for(auto *b:view->findChildren<QToolButton *>())if(!b->property("timelineRowAction").toString().isEmpty()&&b->isVisibleTo(view)){
                ++count;QCOMPARE(b->size(),QSize(28,28));QCOMPARE(b->iconSize(),QSize(16,16));QVERIFY(!b->icon().isNull());QVERIFY(!b->toolTip().isEmpty());QVERIFY(!b->accessibleName().isEmpty());QCOMPARE(b->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,b),600);
                QVERIFY(b->geometry().top()>=view->property("timelineRulerHeight").toInt());QVERIFY(view->viewport()->rect().contains(b->geometry()));}
            QVERIFY(count>0);QVERIFY(count<60);QVERIFY(view->property("timelineRowHeight").toInt()>=32);
        }
    }
    void deepFolderHeadersKeepNamesAndControlsSeparateAtCompactWidths(){
        auto project=fixture();auto &shot=project.shots[0];QString parent;for(const auto &entry:shot.layers)if(entry.name=="Character")parent=entry.id;
        QStringList folders;for(int i=1;i<=5;++i){auto folder=RasterLayer::create(QString("Nested folder %1").arg(i));folder.group=true;folder.parentId=parent;parent=folder.id;folders.append(folder.id);shot.layers.append(folder);}
        QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));Canvas canvas;canvas.setProject(project);TimelineController c(&canvas);TimelinePanel panel(&c);show(panel,{1050,720});auto *view=grid(panel);
        for(int width:{240,900}){view->setFixedWidth(width);settle();
            for(const auto &id:folders){auto *caret=button(panel,id,"collapse"),*eye=button(panel,id,"visible"),*lock=button(panel,id,"lock"),*solo=button(panel,id,"solo");QVERIFY(caret&&eye&&lock&&solo);
                // The identity icon follows the caret; its text starts 50 px later.
                QVERIFY(eye->x()-(caret->x()+50)>=44);QVERIFY(!caret->geometry().intersects(eye->geometry()));QVERIFY(!eye->geometry().intersects(lock->geometry()));QVERIFY(!lock->geometry().intersects(solo->geometry()));
                QCOMPARE(caret->size(),QSize(28,28));QVERIFY(caret->toolTip().contains("Character / Nested folder 1"));QVERIFY(caret->accessibleName().contains(layer(canvas,id).name));
            }
        }
    }
    void renderPreview(){
        const auto output=qEnvironmentVariable("TOON_UI_PREVIEW");if(output.isEmpty())QSKIP("Opt-in track organization captures.");
        auto project=fixture();auto &shot=project.shots[0];QString character;for(const auto &entry:shot.layers)if(entry.name=="Character")character=entry.id;
        auto folderLayer=RasterLayer::create("Animation drawings");folderLayer.group=true;folderLayer.parentId=character;shot.layers.append(folderLayer);
        auto animated=animation_fixture::project();const auto track=animated.shots[0].animationTracks[0];
        for(auto entry:animated.shots[0].layers)if(entry.id==track.rootGroupLayerId||!entry.parentId.isEmpty()){
            if(entry.id==track.rootGroupLayerId){entry.parentId=folderLayer.id;entry.name="Character animation";}shot.layers.append(entry);}
        shot.animationTracks.append(track);QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        Canvas canvas;canvas.setProject(project);TimelineController c(&canvas);TimelinePanel panel(&c);show(panel,{1100,520});const auto folder=named(canvas,"Character");auto *view=grid(panel);QVERIFY(rows(view).contains(track.id));
        button(panel,folder,"solo")->click();QDir().mkpath(output);QVERIFY(panel.grab().save(output+"/timeline-organization.png"));
        const auto before=canvas.project().toJson();const auto from=header(view,named(canvas,"Effects"));
        QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,from);const auto to=header(view,folder);move(view,to);QVERIFY(view->property("timelineDropValid").toBool());QVERIFY(panel.grab().save(output+"/timeline-organization-drop.png"));
        QTest::keyClick(view,Qt::Key_Escape);QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,to);QCOMPARE(canvas.project().toJson(),before);
        panel.resize(360,640);settle();QVERIFY(panel.grab().save(output+"/timeline-organization-compact.png"));
        view->setFixedWidth(240);settle();QVERIFY(view->grab().save(output+"/timeline-organization-grid-240.png"));view->setMinimumWidth(0);view->setMaximumWidth(QWIDGETSIZE_MAX);
        auto font=panel.font();font.setPointSize(16);panel.setFont(font);panel.resize(720,520);settle();QVERIFY(panel.grab().save(output+"/timeline-organization-larger-text.png"));
    }
};
QTEST_MAIN(TimelineOrganizationPanelTests)
#include "test_timeline_organization_panel.moc"
