// On-demand UX inventory. Uses real application widgets with isolated synthetic data.
#include "main_window.h"
#include "canvas.h"
#include "auto_actions.h"
#include "auto_actions_panel.h"
#include "brush_settings_panel.h"
#include "brush_management_panel.h"
#include "reference_panel.h"
#include "character_model_panel.h"
#include "character_color_panel.h"
#include "character_recolor_dialog.h"
#include "cel_paint_check_panel.h"
#include "cel_shading_panel.h"
#include "light_table.h"
#include "effects_panel.h"
#include "guide_panel.h"
#include "selection_panel.h"
#include "vector_edit_panel.h"
#include "tool_navigation_panel.h"
#include "tool_catalog.h"
#include "tool_management_panel.h"
#include "drawing_tool_controller.h"
#include "scene3d_controller.h"
#include "scene3d_panel.h"
#include "model_library.h"
#include "timeline_controller.h"
#include "timeline_panel.h"
#include "project_dialog.h"
#include "video_export_dialog.h"
#include "audio_export_dialog.h"
#include "onion_settings_dialog.h"
#include "timeline_ripple_dialog.h"
#include "quick_access_toolbar.h"
#include "raster_tools.h"
#include "property_row.h"
#include "value_slider.h"
#include <QtWidgets>
#include <QtTest>
#include <QtEndian>
#include <cmath>
using namespace toon;

static QString slug(QString x) { x=x.toLower();x.replace(QRegularExpression("[^a-z0-9]+"),"-");return x.left(130).remove(QRegularExpression("^-|-$")); }
static void settle(){QTest::qWait(35);for(int i=0;i<4;++i)QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);QCoreApplication::processEvents();}
struct Capture {
 QString directory;QJsonArray screens;QStringList errors;int serial=0;
 QJsonArray controls(QWidget *root) {
  QJsonArray out;auto widgets=root->findChildren<QWidget*>();widgets.prepend(root);
  for(auto *w:widgets) {
   if(!w->isVisibleTo(root))continue;
   const auto type=QString(w->metaObject()->className());
   const bool interesting=qobject_cast<QAbstractButton*>(w)||qobject_cast<QComboBox*>(w)||qobject_cast<QLineEdit*>(w)||qobject_cast<QTabBar*>(w)||qobject_cast<QLabel*>(w)||qobject_cast<QAbstractSpinBox*>(w)||qobject_cast<QAbstractSlider*>(w)||qobject_cast<QAbstractItemView*>(w)||qobject_cast<QPlainTextEdit*>(w)||type.startsWith("toon::");
   if(!interesting)continue;
   const QRect r(w->mapTo(root,QPoint()),w->size());
   QJsonObject o{{"id",w->objectName()},{"type",type},{"name",w->accessibleName()},{"help",w->toolTip()},{"description",w->accessibleDescription()},{"enabled",w->isEnabled()},{"on_screen",root->rect().intersects(r)},{"bounds",QJsonArray{r.x(),r.y(),r.width(),r.height()}}};
   if(auto *b=qobject_cast<QAbstractButton*>(w)){o["text"]=b->text();if(b->isCheckable())o["checked"]=b->isChecked();}
   if(auto *l=qobject_cast<QLabel*>(w))o["text"]=l->text();
   if(auto *l=qobject_cast<QLineEdit*>(w)){o["text"]=l->text();o["placeholder"]=l->placeholderText();}
   if(auto *n=qobject_cast<QDoubleSpinBox*>(w)){o["value"]=n->value();o["minimum"]=n->minimum();o["maximum"]=n->maximum();o["decimals"]=n->decimals();}
   if(auto *n=qobject_cast<QSpinBox*>(w)){o["value"]=n->value();o["minimum"]=n->minimum();o["maximum"]=n->maximum();}
   if(auto *c=qobject_cast<QComboBox*>(w)){QJsonArray choices;for(int i=0;i<qMin(64,c->count());++i)choices.append(c->itemText(i));o["choices"]=choices;o["choice_count"]=c->count();o["value"]=c->currentText();}
   if(auto *t=qobject_cast<QTabBar*>(w)){QJsonArray tabs;for(int i=0;i<t->count();++i)tabs.append(QJsonObject{{"text",t->tabText(i)},{"help",t->tabToolTip(i)},{"enabled",t->isTabEnabled(i)},{"selected",i==t->currentIndex()}});o["tabs"]=tabs;}
   if(auto *l=qobject_cast<QListWidget*>(w)){QJsonArray rows;for(int i=0;i<qMin(30,l->count());++i)rows.append(l->item(i)->text());o["rows"]=rows;o["row_count"]=l->count();}
   if(auto *p=qobject_cast<QPlainTextEdit*>(w))o["text"]=p->toPlainText();
   if(auto *m=qobject_cast<QMenu*>(w)){QJsonArray actions;for(auto *a:m->actions())if(!a->isSeparator())actions.append(QJsonObject{{"text",a->text()},{"help",a->toolTip()},{"enabled",a->isEnabled()},{"shortcut",a->shortcut().toString()}});o["actions"]=actions;}
   out.append(o);
  }return out;
 }
 void shot(QWidget *w,QString family,QString state) {
  if(!w){errors<<family+": missing widget "+state;return;}settle();
  const QString file=QString("screenshots/%1-%2-%3.png").arg(++serial,3,10,QChar('0')).arg(slug(family),slug(state));
  auto pix=w->grab();if(pix.isNull()||!pix.save(directory+"/"+file)){errors<<"Cannot save "+file;return;}
  screens.append(QJsonObject{{"id",serial},{"family",family},{"state",state},{"file",file},{"class",w->metaObject()->className()},{"object_id",w->objectName()},{"logical_width",w->width()},{"logical_height",w->height()},{"pixel_width",pix.width()},{"pixel_height",pix.height()},{"scale",pix.devicePixelRatio()},{"controls",controls(w)}});
  qInfo().noquote()<<file;
 }
 void page(QWidget *root,QString family,QString state,bool scrolling=true) {
  shot(root,family,state);
  if(!scrolling)return;
  int n=0;for(auto *s:root->findChildren<QScrollArea*>()) {
   if(!s->isVisibleTo(root)||s->verticalScrollBar()->maximum()<10)continue;
   auto *bar=s->verticalScrollBar();const int old=bar->value(),max=bar->maximum();
   const int travel=qMax(100,bar->pageStep()-50);
   for(int y=travel;y<max;y+=travel){bar->setValue(y);shot(root,family,state+QString(" — scroll %1").arg(++n));}
   bar->setValue(max);shot(root,family,state+QString(" — bottom %1").arg(++n));bar->setValue(old);
  }
 }
 void tabs(QWidget *root,QString family,QString state,QSet<QTabBar*> visited={},int depth=0) {
  if(depth>3)return;
  for(auto *bar:root->findChildren<QTabBar*>()) {
   if(!bar->isVisibleTo(root)||visited.contains(bar)||bar->count()>24||bar->objectName().contains("dock"))continue;
   visited.insert(bar);const int old=bar->currentIndex();
   for(int i=0;i<bar->count();++i)if(bar->isTabEnabled(i)) {
    bar->setCurrentIndex(i);settle();QString name=bar->tabText(i);if(name.isEmpty())name=bar->tabToolTip(i);if(name.isEmpty())name=QString("tab %1").arg(i+1);
    const QString label=state+" / "+name;page(root,family,label);tabs(root,family,label,visited,depth+1);
   }bar->setCurrentIndex(old);settle();return;
  }
 }
 void dock(QDockWidget *d,QString state,bool variants=true) {
  const QString family=d->property("panelLabel").toString();d->setFloating(true);d->resize(d->objectName()=="animationPanel"?QSize(1080,320):QSize(320,600));d->show();d->raise();settle();
  page(d,family,state);tabs(d,family,state);
  if(variants){d->resize(240,d->objectName()=="animationPanel"?350:480);page(d,family,state+" — narrow");}
  d->hide();
 }
 void dialog(std::function<void()> open,QString family,QString state,bool browseTabs=true) {
  bool found=false;QTimer timer;timer.setSingleShot(true);
  QObject::connect(&timer,&QTimer::timeout,[&,this]{
   auto *w=QApplication::activeModalWidget();if(!w)for(auto *x:QApplication::topLevelWidgets())if(qobject_cast<QDialog*>(x)&&x->isVisible())w=x;
   if(!w){errors<<family+": dialog did not open";return;}
   found=true;page(w,family,state);if(browseTabs)tabs(w,family,state);
   if(auto *d=qobject_cast<QDialog*>(w))d->reject();else w->close();
  });timer.start(80);open();timer.stop();if(!found)errors<<family+": not captured";
 }
 void menu(QWidget *target,QString family,QString state) {
  bool found=false;QTimer timer;timer.setSingleShot(true);QObject::connect(&timer,&QTimer::timeout,[&,this]{
   auto *w=QApplication::activePopupWidget();if(w){found=true;shot(w,family,state);w->close();}
  });
  const QPoint p=qobject_cast<QAbstractScrollArea*>(target)?QPoint(12,12):target->rect().center();
  QContextMenuEvent e(QContextMenuEvent::Mouse,p,target->mapToGlobal(p));timer.start(60);auto *host=target->parentWidget();if(host&&host->contextMenuPolicy()==Qt::CustomContextMenu)QMetaObject::invokeMethod(host,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,p));else QApplication::sendEvent(target,&e);settle();timer.stop();
  if(!found)errors<<family+": context menu unavailable for "+state;
 }
 void finish(){QFile f(directory+"/capture-manifest.json");if(!f.open(QIODevice::WriteOnly))return;f.write(QJsonDocument(QJsonObject{{"application","Toon Pro"},{"version",QApplication::applicationVersion()},{"date",QDate::currentDate().toString(Qt::ISODate)},{"method","Real Qt application widgets, synthetic temporary documents; screenshots are not mockups."},{"screens",screens},{"capture_notes",QJsonArray::fromStringList(errors)}}).toJson());}
};
static Project fixture() {
 auto p=Project::create();p.movie="The Way Home — UX sample";p.width=960;p.height=540;p.shots[0].name="City overlook";p.shots[0].durationFrames=120;
 auto &layers=p.shots[0].layers;layers[0].name="Environment color";
 QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);
 {QPainter pt(&tile);pt.setRenderHint(QPainter::Antialiasing);QLinearGradient g(0,0,0,256);g.setColorAt(0,QColor("#9fcbd5"));g.setColorAt(1,QColor("#ebd4b5"));pt.fillRect(tile.rect(),g);pt.setPen(QPen(QColor("#405c70"),4));pt.drawLine(0,180,256,180);pt.fillRect(30,110,45,70,QColor("#6c8294"));pt.fillRect(125,65,50,115,QColor("#768b9d"));}
 for(int x=0;x<4;++x)for(int y=0;y<3;++y)layers[0].tiles[{x,y}]=tile;
 auto ink=RasterLayer::create("Character ink");QImage art(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);art.fill(Qt::transparent);{QPainter pt(&art);pt.setRenderHint(QPainter::Antialiasing);pt.setPen(QPen(QColor("#263443"),5,Qt::SolidLine,Qt::RoundCap));pt.setBrush(QColor("#f4dbc8"));pt.drawEllipse(QRectF(80,35,85,100));QPainterPath path;path.moveTo(70,220);path.cubicTo(75,100,170,100,185,220);pt.setBrush(QColor("#b46b59"));pt.drawPath(path);}ink.tiles[{1,0}]=art;layers.append(ink);
 auto shadow=RasterLayer::create("Shadow pass");shadow.opacity=.4;shadow.visible=false;layers.append(shadow);
 auto notes=RasterLayer::create("Layout notes");notes.annotation=true;layers.append(notes);
 ReferenceImage ref;ref.id=QUuid::createUuid().toString(QUuid::WithoutBraces);ref.name="Environment color study";ref.image=tile;p.shots[0].references.append(ref);
 QByteArray pcm(audioSampleRate*audioBytesPerSampleFrame*2,0);for(int i=0;i<audioSampleRate*2;++i){const qint16 v=qint16(std::sin(i*.06)*std::sin(i*.00015)*18000);qToLittleEndian(v,pcm.data()+i*4);qToLittleEndian(v,pcm.data()+i*4+2);}AudioClip clip;clip.id=QUuid::createUuid().toString(QUuid::WithoutBraces);clip.name="Dialogue — sample waveform";clip.asset=makeAudioAsset("Synthetic dialogue",pcm);clip.sampleCount=audioSampleRate*2;clip.startFrame=12;clip.fadeInSamples=4000;clip.fadeOutSamples=6000;p.shots[0].audioClips.append(clip);
 return p;
}

static void extraCaptures(Capture &cap,MainWindow &window) {
 auto *canvas=window.canvas();auto *doc=canvas->document();QTemporaryDir samples;const auto sample=samples.filePath("ux-sample.toon");QString error;saveProject(fixture(),sample,&error);window.openProject(sample);doc->selectLayer(1);canvas->setToolRouter({});canvas->fitSheet();
 cap.shot(&window,"Workspace","Drawing workspace — actual canvas");
 for(const auto id:{"animationPanel","framePropertiesPanel"})if(auto*d=window.findChild<QDockWidget*>(id)){d->show();d->raise();}
 cap.shot(&window,"Workspace","Drawing workspace with timeline");
 auto show=[&](const char *id){auto*d=window.findChild<QDockWidget*>(id);d->setFloating(true);d->resize(360,620);d->show();d->raise();settle();return d;};
 auto *bd=show("brushManagementPanel");auto *bm=window.findChild<BrushManagementPanel*>();
 QTimer timer;timer.setSingleShot(true);QObject::connect(&timer,&QTimer::timeout,[&]{
  auto *d=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!d)return;
  for(auto *b:d->findChildren<QToolButton*>())if(b->objectName().startsWith("brushType_")){b->click();cap.page(d,"New Brush",b->text());if(b->objectName().contains("DynamicStamp"))cap.tabs(d,"New Brush","Multi-stamp image color mode");}
  d->reject();
 });timer.start(80);bm->findChild<QToolButton*>("brushManagerAdd")->click();timer.stop();
 if(auto *list=bm->findChild<QListWidget*>("managedBrushes")){list->setSelectionMode(QAbstractItemView::ExtendedSelection);if(list->count()>3){list->item(0)->setSelected(true);list->item(2)->setSelected(true);cap.page(bd,"Brush Management","Multi-selection and batch metadata");}cap.menu(list->viewport(),"Brush Management","Brush context menu");}
 for(auto *edit:bm->findChildren<QLineEdit*>())if(edit->placeholderText().contains("Search",Qt::CaseInsensitive)){edit->setText("absent-sample-name");cap.page(bd,"Brush Management","No matching brushes");edit->clear();break;}bd->hide();
 auto *td=show("toolManagementPanel");auto *tm=window.findChild<ToolManagementPanel*>();tm->focusEntry("group/drawing");if(auto*b=tm->findChild<QToolButton*>("manageMembership"))cap.dialog([&]{b->click();},"Tool Management","Choose tools in group");td->hide();
 auto *ld=show("layersPanel");doc->selectLayer(1);doc->addGroup();doc->addLayer();doc->setLayerAlphaLocked(true);doc->addLayerMask();cap.page(ld,"Layers","Folder alpha lock and layer mask");for(auto *tree:ld->findChildren<QTreeWidget*>())if(tree->isVisible()){cap.menu(tree->viewport(),"Layers","Layer context menu");break;}ld->hide();
 auto *ad=show("autoActionsPanel");auto *actions=window.findChild<AutoActions*>();auto id=actions->create("Reusable layer setup");actions->startRecording(id);doc->addLayer();doc->changeLayer("Line art",true,false,false,.7);actions->stopRecording();
 if(auto *list=ad->findChild<QListWidget*>("autoActionSteps")){list->setCurrentRow(0);cap.menu(list->viewport(),"Auto Actions","Step reorder remove menu");}
 for(auto *b:ad->findChildren<QToolButton*>())if(b->objectName()=="autoActionMore"){
  QTimer t;t.setSingleShot(true);QObject::connect(&t,&QTimer::timeout,[&]{if(auto*w=QApplication::activePopupWidget()){cap.shot(w,"Auto Actions","Rename duplicate delete menu");w->close();}});t.start(60);b->click();t.stop();
 }ad->hide();
 auto *timeline=window.findChild<TimelineController*>();timeline->addTrack(true,DrawingTemplate::Anime,"Character");auto *tl=show("animationPanel");tl->resize(1100,320);
 for(const auto *button:{"timelineRangeSettings","timelineAddTrack"})if(auto*b=tl->findChild<QToolButton*>(button))cap.dialog([&]{b->click();},"Timeline dialogs",button);
 tl->hide();
 // Tool groups, tags, icon-only list, search and dropdown choices.
 auto *tools=show("toolsPanel");auto *catalog=window.toolCatalog();catalog->selectEntry("group/drawing");
 if(auto *nav=tools->findChild<ToolNavigationPanel*>()){nav->setIconsOnly(true);cap.page(tools,"Tools","Brush gallery icon grid");nav->setIconsOnly(false);cap.page(tools,"Tools","Brush gallery names and previews");}
 for(auto *combo:tools->findChildren<QComboBox*>())if(combo->isVisible()){combo->showPopup();settle();if(auto*w=QApplication::activePopupWidget())cap.shot(w,"Tools","Tag filter choices");combo->hidePopup();}
 tools->hide();
 auto *quick=window.findChild<QuickAccessToolbar*>();quick->addColor(QColor("#b46b59"));quick->addColor(QColor("#6c8294"));quick->addNavigation("hand");quick->addNavigation("fit-sheet");cap.shot(quick,"Quick Access","Pinned tools colors and navigation");
 if(auto*b=quick->findChild<QToolButton*>("quickAccessAddButton")){
  QTimer t;t.setSingleShot(true);QObject::connect(&t,&QTimer::timeout,[&]{if(auto*w=QApplication::activePopupWidget()){cap.shot(w,"Quick Access","Add tool color or navigation menu");w->close();}});t.start(60);b->click();t.stop();
 }
 // Error and pen-busy states remain visible in the designer's matrix.
 auto *pd=show("propertiesPanel");canvas->setTool(Canvas::Tool::Brush);doc->setLayersLocked({doc->activeShot().layers[doc->activeLayer()].id},true);cap.page(pd,"Tool Properties","Locked target — unavailable brush");cap.shot(&window,"Workspace","Unavailable action feedback context");doc->setLayersLocked({doc->activeShot().layers[doc->activeLayer()].id},false);pd->hide();
 for(auto*d:window.findChildren<QDockWidget*>())d->hide();
 for(const auto id:{"toolsPanel","brushPresetPanel","propertiesPanel","layersPanel"})if(auto*d=window.findChild<QDockWidget*>(id)){d->setFloating(false);d->show();d->raise();}
 window.resize(1500,950);canvas->fitSheet();cap.shot(&window,"Workspace","Compact drawing layout with artwork");window.setDrawingMode(true);cap.shot(&window,"Workspace","Tab drawing mode — artwork and quick access");window.setDrawingMode(false);
 cap.finish();doc->history()->setClean();window.hide();
}
// Focused captures for the shared drawing-density pass. No artist settings or
// artwork are touched; each dock retains its normal identity and controls.
static void drawingDensityCaptures(Capture &cap, MainWindow &window) {
 QTemporaryDir temporary;
 const auto path=temporary.filePath("density-sample.toon"); QString error;
 if(!saveProject(fixture(),path,&error)||!window.openProject(path)){cap.errors<<error;cap.finish();return;}
 window.canvas()->document()->selectLayer(1);window.canvas()->fitSheet();
 for(const auto id:{"animationPanel","framePropertiesPanel"})if(auto *dock=window.findChild<QDockWidget*>(id))dock->hide();
 cap.shot(&window,"Workspace","Drawing controls before optional dock inspection");
 if(auto *toolbox=window.findChild<ToolNavigationPanel*>("toolboxContent"))cap.shot(toolbox,"Toolbox","Fixed icon column");
 for(const auto id:{"toolsPanel","propertiesPanel","layersPanel","colorsPanel"}) {
  auto *dock=window.findChild<QDockWidget*>(id);dock->setFloating(true);dock->show();dock->raise();
  const QString family=dock->property("panelLabel").toString();
  dock->resize(264,480);cap.shot(dock,family,"Standard drawing dock");
  dock->resize(220,360);cap.shot(dock,family,"Narrow drawing dock");
  dock->hide();
 }
 auto *dock=window.findChild<QDockWidget*>("propertiesPanel");dock->show();dock->resize(220,360);
 window.canvas()->setToolRouter({});
 for(auto tool:{Canvas::Tool::Gradient,Canvas::Tool::Guides,Canvas::Tool::RectangleSelect}) {
  window.canvas()->setTool(tool);cap.shot(dock,"Tool Properties",QString("Compact drawing tabs tool %1").arg(int(tool)));
 }
 window.canvas()->setTool(Canvas::Tool::Brush);
 auto font=qApp->font();font.setPointSizeF(font.pointSizeF()+4);qApp->setFont(font);
 for(const auto id:{"toolsPanel","propertiesPanel","layersPanel","colorsPanel"}) {
  auto *panel=window.findChild<QDockWidget*>(id);panel->resize(240,420);panel->show();panel->raise();
  cap.shot(panel,panel->property("panelLabel").toString(),"Larger application font");panel->hide();
 }
 cap.finish();window.canvas()->document()->history()->setClean();window.hide();
}

static void drawingCompositionCaptures(Capture &cap, MainWindow &window) {
 QTemporaryDir temporary;QString error;const auto path=temporary.filePath("composition.toon");
 if(!saveProject(fixture(),path,&error)||!window.openProject(path)){cap.errors<<error;cap.finish();return;}
 window.canvas()->document()->selectLayer(1);window.applyDrawingWorkspace(false);window.canvas()->fitSheet();
 auto *quick=window.findChild<QuickAccessToolbar*>();
 quick->addColor(QColor("#b46b59"));quick->addColor(QColor("#6c8294"));quick->addNavigation("hand");quick->addNavigation("fit-sheet");
 cap.shot(&window,"Workspace","Drawing preset — daily controls adjacent");
 for(const auto id:{"toolsPanel","brushPresetPanel","propertiesPanel"})if(auto *dock=window.findChild<QDockWidget*>(id))
  cap.shot(dock,dock->property("panelLabel").toString(),"Drawing stack allocation");
 cap.shot(quick,"Quick Access","Grouped pins — original order preserved");
 window.applyDrawingWorkspace(true);window.resize(900,720);settle();window.canvas()->fitSheet();
 cap.shot(&window,"Workspace","Compact drawing — controls closed");
 cap.shot(quick,"Quick Access","Current tool size and color");
 window.toggleDrawingControls();cap.shot(&window,"Workspace","Compact drawing — controls open");
 window.toggleDrawingControls();window.setDrawingMode(true);cap.shot(&window,"Workspace","Drawing mode — Quick access only");window.setDrawingMode(false);
 const auto originalFont=qApp->font();auto font=originalFont;font.setPointSizeF(font.pointSizeF()+4);qApp->setFont(font);window.resize(1100,800);
 cap.shot(&window,"Workspace","Compact drawing — larger font");
 window.toggleDrawingControls();cap.shot(&window,"Workspace","Open drawing controls — larger font");
 qApp->setFont(originalFont);window.applyDrawingWorkspace(false);
 for(const QSize size:{QSize(1440,900),QSize(1024,768)}){
  window.resize(size);settle();window.canvas()->fitSheet();
  cap.shot(&window,"Workspace",QString("Drawing acceptance %1 x %2").arg(size.width()).arg(size.height()));
 }
 window.applyDrawingWorkspace(true);window.resize(900,768);settle();window.canvas()->fitSheet();
 cap.shot(&window,"Workspace","Compact acceptance 900 x 768");
 if(auto *button=quick->findChild<QToolButton*>();button){button->setFocus(Qt::TabFocusReason);cap.shot(quick,"Quick Access","Keyboard focus");}
 const auto closedCanvas=window.canvas()->geometry();const auto closedZoom=window.canvas()->zoom();
 window.toggleDrawingControls();settle();
 auto *drawer=window.findChild<QWidget *>("drawingControlsDrawer");
 auto *drawerTabs=window.findChild<QTabBar *>("drawingDrawerTabs");
 if(!drawer||!drawerTabs||!drawer->isVisible())cap.errors<<"Compact drawing drawer did not open";
 else {
  if(window.canvas()->geometry()!=closedCanvas||window.canvas()->zoom()!=closedZoom)
   cap.errors<<"Opening the drawer changed the canvas geometry or zoom";
  for(int i=0;i<drawerTabs->count();++i){drawerTabs->setCurrentIndex(i);cap.shot(&window,"Drawer",QString("900 x 768 — %1").arg(drawerTabs->tabText(i)));}
  window.resize(900,600);settle();
  for(int i=0;i<drawerTabs->count();++i){drawerTabs->setCurrentIndex(i);cap.shot(&window,"Drawer",QString("900 x 600 — %1").arg(drawerTabs->tabText(i)));}
  qApp->setFont(font);window.resize(900,768);settle();
  for(int i=0;i<drawerTabs->count();++i){drawerTabs->setCurrentIndex(i);cap.shot(&window,"Drawer",QString("Larger text — %1").arg(drawerTabs->tabText(i)));}
  qApp->setFont(originalFont);settle();window.toggleDrawingControls();cap.shot(&window,"Drawer","Closed — canvas retained");
 }
 cap.finish();window.canvas()->document()->history()->setClean();window.hide();
}

static void advancedManagementCaptures(Capture &cap, MainWindow &window) {
 QTemporaryDir temporary;QString error;const auto path=temporary.filePath("advanced.toon");
 if(!saveProject(fixture(),path,&error)||!window.openProject(path)){cap.errors<<error;cap.finish();return;}
 auto *canvas=window.canvas();canvas->setTool(Canvas::Tool::Brush);
 auto *settings=window.findChild<BrushSettingsPanel*>();settings->openAdvancedSettings();
 auto *dialog=settings->findChild<QDialog*>("advancedBrushDialog");
 auto *categories=settings->findChild<QTabBar*>("brushSettingsCategory");
 dialog->resize(480,600);
 for(int i=0;i<categories->count();++i){
  categories->setCurrentIndex(i);cap.shot(dialog,"Advanced Brush",categories->tabToolTip(i));
  for(auto *tabs:dialog->findChildren<QTabBar*>())if(tabs!=categories&&tabs->isVisibleTo(dialog)){
   const int old=tabs->currentIndex();
   for(int j=0;j<tabs->count();++j){tabs->setCurrentIndex(j);cap.shot(dialog,"Advanced Brush",categories->tabToolTip(i)+" / "+tabs->tabText(j));}
   tabs->setCurrentIndex(old);
  }
 }
 for(const auto &option : {qMakePair(3,QString("clusterSprayEnabled")),qMakePair(4,QString("brushTextureEnabled")),qMakePair(5,QString("brushDualEnabled")),qMakePair(7,QString("speedDynamicsEnabled"))}){
  categories->setCurrentIndex(option.first);
  if(option.first==7)dialog->findChild<QTabBar*>("brushTransferTabs")->setCurrentIndex(2);
  auto *toggle=dialog->findChild<QCheckBox*>(option.second);
  if(!toggle){cap.errors<<"Missing optional brush control: "+option.second;continue;}
  const bool old=toggle->isChecked();toggle->setChecked(true);
  cap.page(dialog,"Advanced Brush",categories->tabToolTip(option.first)+" enabled");
  toggle->setChecked(old);
 }
 dialog->resize(360,400);
 for(int i:{2,3,7,11}){categories->setCurrentIndex(i);cap.shot(dialog,"Advanced Brush",categories->tabToolTip(i)+" narrow");}
 dialog->close();
 for(const auto id:{"brushManagementPanel","toolManagementPanel"}){
  auto *dock=window.findChild<QDockWidget*>(id);dock->setFloating(true);dock->show();dock->raise();
  if(auto *manager=dock->findChild<BrushManagementPanel*>())manager->focusBrush(window.toolCatalog()->activeToolId());
  if(auto *manager=dock->findChild<ToolManagementPanel*>())manager->focusEntry("group/drawing");
  const auto title=dock->property("panelLabel").toString();
  for(const auto size:{QSize(360,600),QSize(680,520),QSize(240,360)}){
   dock->resize(size);cap.shot(dock,title,QString("%1 x %2").arg(size.width()).arg(size.height()));
  }
  dock->resize(360,600);cap.tabs(dock,title,"Categories");dock->hide();
 }
 auto font=qApp->font();font.setPointSizeF(font.pointSizeF()+4);qApp->setFont(font);
 settings->openAdvancedSettings();dialog->resize(480,600);categories->setCurrentIndex(2);cap.shot(dialog,"Advanced Brush","Larger font — dynamics");dialog->close();
 for(const auto id:{"brushManagementPanel","toolManagementPanel"}){auto *dock=window.findChild<QDockWidget*>(id);dock->show();dock->resize(360,600);cap.shot(dock,dock->property("panelLabel").toString(),"Larger font");dock->hide();}
 cap.finish();canvas->document()->history()->setClean();window.hide();
}

static void referenceEffectsCaptures(Capture &cap, MainWindow &window) {
 QTemporaryDir temporary;QString error;const auto path=temporary.filePath("reference-effects.toon");
 if(!saveProject(fixture(),path,&error)||!window.openProject(path)){cap.errors<<error;cap.finish();return;}
 auto *canvas=window.canvas();auto *doc=canvas->document();doc->selectLayer(1);
 auto *refs=window.findChild<QDockWidget*>("referencesPanel");
 refs->setFloating(true);refs->show();refs->raise();
 for(const auto size:{QSize(320,480),QSize(220,280)}){
  refs->resize(size);cap.shot(refs,"References",QString("Single image %1 x %2").arg(size.width()).arg(size.height()));
 }
 const auto study=doc->activeShot().references.first().image;
 for(int i=0;i<7;++i)doc->addReference(QString("Environment lighting and color study %1").arg(i+2),study);
 refs->resize(320,480);cap.shot(refs,"References","Multiple images");
 auto *list=refs->findChild<QListWidget*>("referenceList");list->setCurrentRow(list->count()-1);list->scrollToItem(list->currentItem());
 refs->resize(220,280);cap.shot(refs,"References","Short with thumbnail overflow");
 const auto font=qApp->font();auto large=font;large.setPointSizeF(font.pointSizeF()+4);qApp->setFont(large);
 refs->resize(260,400);cap.shot(refs,"References","Larger text");qApp->setFont(font);
 const auto references=doc->activeShot().references;for(const auto &reference:references)doc->removeReference(reference.id);
 refs->resize(220,280);cap.shot(refs,"References","Empty");refs->hide();
 auto *fx=window.findChild<QDockWidget*>("effectsPanel");fx->setFloating(true);fx->resize(300,480);fx->show();fx->raise();
 auto *panel=fx->findChild<EffectsPanel*>();auto *tabs=panel->findChild<QTabBar*>("effectsTabs");
 tabs->setCurrentIndex(0);cap.shot(fx,"Effects","Mask absent");panel->findChild<QToolButton*>("maskAddWhite")->click();cap.page(fx,"Effects","Mask present");
 const char *toggleIds[]={"effectBlurEnabled","effectShadowEnabled","effectGlowEnabled","effectColorEnabled"};
 for(int i=1;i<5;++i){tabs->setCurrentIndex(i);cap.shot(fx,"Effects",tabs->tabToolTip(i)+" disabled");panel->findChild<QToolButton*>(toggleIds[i-1])->setChecked(true);cap.page(fx,"Effects",tabs->tabToolTip(i)+" enabled");}
 fx->resize(180,280);tabs->setCurrentIndex(2);cap.page(fx,"Effects","Shadow narrow and short");
 tabs->setCurrentIndex(1);cap.page(fx,"Effects","Blur narrow and short");
 fx->resize(240,400);qApp->setFont(large);tabs->setCurrentIndex(0);cap.page(fx,"Effects","Mask larger text");tabs->setCurrentIndex(4);cap.page(fx,"Effects","Color larger text");qApp->setFont(font);
 const auto id=doc->activeShot().layers[doc->activeLayer()].id;doc->setLayersLocked({id},true);cap.page(fx,"Effects","Locked target");doc->setLayersLocked({id},false);
 cap.finish();doc->history()->setClean();window.hide();
}

static void drawingInspectorsCaptures(Capture &cap, MainWindow &window) {
 QTemporaryDir temporary;QString error;const auto path=temporary.filePath("inspectors.toon");
 if(!saveProject(fixture(),path,&error)||!window.openProject(path)){cap.errors<<error;cap.finish();return;}
 auto *canvas=window.canvas();auto *doc=canvas->document();canvas->setToolRouter({});doc->selectLayer(1);
 auto *dock=window.findChild<QDockWidget*>("propertiesPanel");dock->setFloating(true);dock->show();dock->raise();dock->resize(260,480);
 canvas->setTool(Canvas::Tool::Guides);
 for(int i=0;i<6;++i)canvas->addDefaultGuide(GuideType(i));
 cap.page(dock,"Guides","Multiple assistants");cap.tabs(dock,"Guides","Properties");
 dock->resize(220,280);cap.page(dock,"Guides","Narrow and short");
 const auto font=qApp->font();auto large=font;large.setPointSizeF(font.pointSizeF()+4);qApp->setFont(large);dock->resize(260,420);cap.page(dock,"Guides","Larger text");qApp->setFont(font);
 for(auto guide:canvas->drawingGuides()){
  canvas->selectGuide(guide.id);guide.customColor=true;guide.limited=true;canvas->updateGuide(guide);
  dock->resize(220,280);cap.page(dock,"Guides",guideTypeName(guide.type)+" custom settings short");
 }
 canvas->setTool(Canvas::Tool::RectangleSelect);QPainterPath selected;selected.addRect(320,25,160,210);doc->setRasterSelection(selected);
 dock->resize(260,400);cap.page(dock,"Selection and Transform","Selection");cap.tabs(dock,"Selection and Transform","Selection settings");
 canvas->setTool(Canvas::Tool::Transform);if(!doc->transformActive())doc->beginTransform();
 TransformParameters parameters;parameters.translation={15,10};parameters.rotation=8;doc->previewTransform(parameters);
 dock->resize(220,220);cap.page(dock,"Selection and Transform","Pending raster transform short");
 auto *selectionTabs=dock->findChild<SelectionPanel*>()->findChild<QTabBar*>("selectionDetailTabs");
 selectionTabs->setCurrentIndex(1);cap.page(dock,"Selection and Transform","Transform display settings short");selectionTabs->setCurrentIndex(0);
 qApp->setFont(large);dock->resize(260,360);cap.page(dock,"Selection and Transform","Transform larger text");qApp->setFont(font);doc->cancelTransform();doc->clearRasterSelection();
 canvas->setTool(Canvas::Tool::AdvancedTransform);canvas->drawingTools()->beginAdvanced();dock->resize(220,220);cap.page(dock,"Selection and Transform","Perspective transform short");
 canvas->drawingTools()->warpMode=true;canvas->drawingTools()->notifySettings();cap.page(dock,"Selection and Transform","Warp transform short");
 qApp->setFont(large);dock->resize(260,320);cap.page(dock,"Selection and Transform","Perspective transform larger text");qApp->setFont(font);canvas->drawingTools()->cancel();
 auto vectorProject=fixture();auto vector=RasterLayer::create("Editable vector ink");vector.kind=LayerKind::Vector;vectorProject.shots[0].layers.append(vector);canvas->setProject(vectorProject);doc->selectLayer(vectorProject.shots[0].layers.size()-1);
 canvas->setTool(Canvas::Tool::Brush);BrushSettings ink;ink.size=8;doc->beginStroke({200,160},1,ink,false);for(int i=1;i<25;++i)doc->continueStroke({200+i*10.,160+35*std::sin(i*.2)},.8);doc->endStroke();canvas->fitSheet();
 canvas->setTool(Canvas::Tool::VectorNode);dock->resize(260,480);cap.page(dock,"Vector Properties","No selection");
 const auto object=doc->activeShot().layers[doc->activeLayer()].objects.value(0);
 if(!object.id.isEmpty())canvas->drawingTools()->selectAnimatedPath(object.id);
 if(!canvas->drawingTools()->selectedObject())cap.errors<<"Vector capture has no selected path";
 for(auto tool:{Canvas::Tool::VectorNode,Canvas::Tool::VectorSimplify,Canvas::Tool::VectorWidth}){
  canvas->setTool(tool);dock->resize(260,480);cap.page(dock,"Vector Properties",QString("Tool %1").arg(int(tool)));cap.tabs(dock,"Vector Properties",QString("Modes %1").arg(int(tool)));
  dock->resize(220,240);cap.page(dock,"Vector Properties",QString("Short %1").arg(int(tool)));
 }
 qApp->setFont(large);canvas->setTool(Canvas::Tool::VectorNode);dock->resize(260,360);cap.page(dock,"Vector Properties","Larger text");
 cap.finish();doc->history()->setClean();window.hide();
}

static QImage characterCaptureImage(int view, const QColor &costume) {
 QImage image(360,480,QImage::Format_ARGB32_Premultiplied);image.fill(QColor("#f5f2ea"));
 QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);
 p.setPen(QPen(QColor("#d5d0c5"),1));
 for(int y=70;y<460;y+=70)p.drawLine(25,y,335,y);
 p.setPen(QPen(QColor("#343840"),3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
 p.setBrush(QColor("#eac6ad"));
 const int dx=view==1?15:0;
 p.drawEllipse(QRectF(145+dx,48,70,88));
 p.setBrush(QColor("#414751"));QPainterPath hair;hair.moveTo(143+dx,92);hair.cubicTo(130+dx,25,232+dx,28,217+dx,97);hair.lineTo(195+dx,69);hair.lineTo(175+dx,83);hair.closeSubpath();p.drawPath(hair);
 p.setBrush(costume);QPainterPath coat;coat.moveTo(148,138);coat.lineTo(212,138);coat.lineTo(240,268);coat.lineTo(120,268);coat.closeSubpath();p.drawPath(coat);
 p.drawLine(150,269,147,398);p.drawLine(178,270,172,398);p.drawLine(188,270,194,398);p.drawLine(214,269,217,398);
 p.drawLine(147,398,129,412);p.drawLine(129,412,172,412);p.drawLine(194,398,194,412);p.drawLine(194,412,235,412);p.drawLine(235,412,217,398);
 p.drawLine(147,155,113,235);p.drawLine(213,155,247,235);
 if(view!=2){p.drawLine(160+dx,96,170+dx,96);p.drawLine(191+dx,96,201+dx,96);p.drawArc(QRectF(171+dx,108,20,10),180*16,180*16);}
 return image;
}
static void characterModelCaptures(Capture &cap, MainWindow &window) {
 QTemporaryDir temporary;QString error;const auto path=temporary.filePath("character-models.toonproj");
 if(!saveProject(fixture(),path,&error)||!window.openProject(path)){cap.errors<<error;cap.finish();return;}
 auto *doc=window.canvas()->document();auto *dock=window.findChild<QDockWidget*>("referencesPanel");
 auto *panel=dock->findChild<CharacterModelPanel*>();auto *tabs=dock->findChild<QTabBar*>("referenceTabs");
 if(!panel||!tabs){cap.errors<<"Missing Character Model Sheets panel";cap.finish();return;}
 dock->setFloating(true);dock->move(80,80);dock->show();dock->raise();tabs->setCurrentIndex(1);
 dock->resize(260,420);cap.shot(dock,"Model Sheets","No characters");
 const auto hana=doc->addCharacter("Hana — lead character",&error);
 if(hana.isEmpty()){cap.errors<<error;cap.finish();return;}
 panel->selectCharacter(hana);cap.shot(dock,"Model Sheets","Character without sheets");
 QVector<CharacterModelSheet> sheets;
 for(int i=0;i<3;++i)sheets.append({{},QStringList{"Front","Profile","Back"}[i],ModelSheetCategory::Turnaround,characterCaptureImage(i,QColor("#8fa5b5"))});
 sheets.append({{},"Soft smile",ModelSheetCategory::Expression,characterCaptureImage(0,QColor("#8fa5b5"))});
 sheets.append({{},"Winter coat — long costume reference name",ModelSheetCategory::Costume,characterCaptureImage(0,QColor("#a47f78"))});
 if(!doc->addCharacterSheets(hana,sheets,&error))cap.errors<<error;
 doc->addCharacter("Ren — supporting character",&error);panel->selectCharacter(hana);
 for(const auto size:{QSize(320,520),QSize(220,340)}){
  dock->resize(size);panel->selectCategory(ModelSheetCategory::Turnaround);cap.shot(dock,"Model Sheets",QString("Turnarounds %1 x %2").arg(size.width()).arg(size.height()));
 }
 panel->selectCategory(ModelSheetCategory::Expression);cap.shot(dock,"Model Sheets","Expressions");
 panel->selectCategory(ModelSheetCategory::Costume);cap.shot(dock,"Model Sheets","Costumes");
 panel->findChild<ValueSlider*>("characterModelZoom")->setValue(150);
 const auto before=panel->findChild<ReferenceView*>("characterModelView")->imageTransform();
 if(doc->createShot("Close-up").isEmpty())cap.errors<<"Could not create second shot";
 if(panel->findChild<ReferenceView*>("characterModelView")->imageTransform()!=before)cap.errors<<"Shot change reset model sheet view";
 cap.shot(dock,"Model Sheets","Same costume in second shot");
 panel->findChild<QToolButton*>("fitCharacterSheet")->click();
 const auto font=qApp->font();auto large=font;large.setPointSizeF(font.pointSizeF()+4);qApp->setFont(large);
 dock->resize(260,420);cap.shot(dock,"Model Sheets","Larger text");qApp->setFont(font);
 tabs->setCurrentIndex(0);dock->resize(220,280);cap.shot(dock,"References","Images tab remains compact");
 cap.finish();doc->history()->setClean();window.hide();
}

static void characterColorCaptures(Capture &cap, MainWindow &window) {
 QTemporaryDir temporary;QString error;const auto path=temporary.filePath("character-colors.toonproj");
 if(!saveProject(fixture(),path,&error)||!window.openProject(path)){cap.errors<<error;cap.finish();return;}
 auto *canvas=window.canvas();auto *doc=canvas->document();auto *dock=window.findChild<QDockWidget*>("referencesPanel");
 auto *panel=dock->findChild<CharacterColorPanel*>();auto *tabs=dock->findChild<QTabBar*>("referenceTabs");
 if(!panel||!tabs){cap.errors<<"Missing Character Color Models panel";cap.finish();return;}
 dock->setFloating(true);dock->move(80,80);dock->show();dock->raise();tabs->setCurrentIndex(2);
 dock->resize(260,420);cap.shot(dock,"Color Models","No characters");
 const auto hana=doc->addCharacter("Hana — lead character",&error);
 panel->selectCharacter(hana);cap.shot(dock,"Color Models","Character without colors");
 using C=CharacterColorCategory;
 struct NamedColor{QString name;C category;QColor color;};
 const QVector<NamedColor> palette{
  {"Base",C::Skin,QColor("#ecc6ad")},{"Highlight",C::Skin,QColor("#fae7d4")},
  {"Cheek blush",C::Skin,QColor("#d89087")},{"Warm edge",C::Skin,QColor("#ca9886")},
  {"Hair base",C::Hair,QColor("#3c4354")},{"Hair shine",C::Hair,QColor("#777c97")},
  {"Winter coat",C::Costume,QColor("#a47f78")},{"School uniform",C::Costume,QColor("#8fa5b5")},
  {"Face shadow",C::Shadow,QColor("#ad8c9a")},{"Soft shadow overlay",C::Shadow,QColor(64,49,82,128)}};
 for(const auto &color:palette)if(doc->addCharacterColor(hana,color.name,color.category,color.color,&error).isEmpty())cap.errors<<error;
 doc->addCharacterSheets(hana,{{{},"Front",ModelSheetCategory::Turnaround,characterCaptureImage(0,QColor("#8fa5b5"))}},&error);
 const auto ren=doc->addCharacter("Ren — supporting character",&error);
 doc->addCharacterColor(ren,"Base",C::Skin,QColor("#c38c6b"),&error);panel->selectCharacter(hana);
 for(const auto size:{QSize(440,480),QSize(220,320)}){
  dock->resize(size);panel->selectCategory(C::Skin);cap.shot(dock,"Color Models",QString("Skin %1 x %2").arg(size.width()).arg(size.height()));
 }
 for(const auto category:{C::Hair,C::Costume,C::Shadow}){
  panel->selectCategory(category);cap.shot(dock,"Color Models",QString("Category %1").arg(int(category)));
 }
 if(doc->createShot("Close-up").isEmpty())cap.errors<<"Could not create second shot";
 if(panel->selectedCharacterId()!=hana||panel->selectedCategory()!=C::Shadow)cap.errors<<"Shot change reset character colors";
 cap.shot(dock,"Color Models","Same shadow palette in second shot");
 const auto font=qApp->font();auto large=font;large.setPointSizeF(font.pointSizeF()+4);qApp->setFont(large);
 dock->resize(260,420);cap.shot(dock,"Color Models","Larger text");qApp->setFont(font);
 cap.dialog([&]{panel->findChild<QToolButton*>("editCharacterColor")->click();},"Color Models","Edit named color",false);
 panel->selectCharacter(ren);tabs->setCurrentIndex(1);cap.shot(dock,"Model Sheets","Same character selection from colors");
 if(dock->findChild<CharacterModelPanel*>()->selectedCharacterId()!=ren)cap.errors<<"Character tab selection was not shared";
 tabs->setCurrentIndex(2);panel->selectCharacter(hana);panel->selectCategory(C::Skin);
 cap.finish();doc->history()->setClean();window.hide();
}

static void characterVariantCaptures(Capture &cap, MainWindow &window) {
 QTemporaryDir temporary;QString error;const auto path=temporary.filePath("character-variants.toonproj");
 if(!saveProject(fixture(),path,&error)||!window.openProject(path)){cap.errors<<error;cap.finish();return;}
 auto *doc=window.canvas()->document();auto *dock=window.findChild<QDockWidget*>("referencesPanel");
 auto *panel=dock->findChild<CharacterColorPanel*>();dock->findChild<QTabBar*>("referenceTabs")->setCurrentIndex(2);
 dock->setFloating(true);dock->move(80,80);dock->resize(340,500);dock->show();dock->raise();
 cap.shot(dock,"Color Variants","No character");
 const auto hana=doc->addCharacter("Hana");panel->selectCharacter(hana);
 using C=CharacterColorCategory;
 const auto base=doc->addCharacterColor(hana,"Skin base",C::Skin,QColor("#edb995"));
 const auto blush=doc->addCharacterColor(hana,"Cheek blush",C::Skin,QColor("#d68b83"));
 doc->addCharacterColor(hana,"Hair base",C::Hair,QColor("#303b52"));
 doc->addCharacterColor(hana,"School uniform",C::Costume,QColor("#8fa5b5"));
 doc->addCharacterColor(hana,"Face shadow",C::Shadow,QColor("#967887"));
 QString night,interior;
 const auto variants=doc->project().characters.first().colorVariants;
 for(const auto &variant:variants){
  QColor skin,cheek;
  if(variant.name=="Night"){night=variant.id;skin=QColor("#7687ac");cheek=QColor("#8d7f9c");}
  else if(variant.name=="Sunset"){skin=QColor("#efaa79");cheek=QColor("#dc8178");}
  else{interior=variant.id;skin=QColor("#d2b097");cheek=QColor("#bb8c82");}
  if(!doc->updateCharacterColor(hana,base,"Skin base",C::Skin,skin,&error,variant.id)||!doc->updateCharacterColor(hana,blush,"Cheek blush",C::Skin,cheek,&error,variant.id))cap.errors<<error;
 }
 panel->selectVariant({});cap.shot(dock,"Color Variants","Day palette");
 for(const auto &variant:variants){panel->selectVariant(variant.id);cap.shot(dock,"Color Variants",variant.name+" palette");}
 const auto custom=doc->addCharacterColorVariant(hana,"Blue moonlight",night,&error);
 if(custom.isEmpty())cap.errors<<error;
 panel->selectVariant(custom);dock->resize(220,340);cap.shot(dock,"Color Variants","Custom palette narrow");
 const auto font=qApp->font();auto larger=font;larger.setPointSizeF(font.pointSizeF()+3);qApp->setFont(larger);
 dock->resize(260,420);panel->selectVariant(interior);cap.shot(dock,"Color Variants","Larger text");qApp->setFont(font);
 doc->createShot("Interior reverse angle");panel->selectVariant(night);cap.shot(dock,"Color Variants","Night palette in another shot");
 auto *checkDock=window.findChild<QDockWidget*>("celPaintCheckPanel");checkDock->setFloating(true);checkDock->move(80,80);checkDock->resize(280,480);
 dock->hide();checkDock->show();checkDock->raise();
 auto *check=checkDock->findChild<CelPaintCheckPanel*>();if(check->selectedVariantId()!=night)cap.errors<<"Paint Check did not follow the selected palette";
 cap.shot(checkDock,"Color Variants","Paint Check approved lighting palette");checkDock->hide();
 if(auto *save=window.findChild<QAction*>("saveProjectAction"))save->trigger();
 if(window.isWindowModified()||!window.openProject(path))cap.errors<<"Could not save and reopen lighting palettes";
 dock->resize(340,500);dock->show();dock->raise();panel->selectCharacter(hana);panel->selectVariant(night);
 cap.shot(dock,"Color Variants","Saved Night colors reopened");
 cap.finish();window.hide();
}

static void characterRecolorCaptures(Capture &cap, MainWindow &window) {
 QTemporaryDir temporary; QString error; auto project=Project::create();project.width=384;project.height=288;
 project.shots[0].layers[0].name="Hana · color A1";
 QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);
 {QPainter painter(&tile);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(Qt::NoPen);
  painter.setBrush(QColor("#34445c"));painter.drawEllipse(QRectF(68,18,120,140));
  painter.setBrush(QColor("#edb995"));painter.drawEllipse(QRectF(82,48,92,108));
  painter.setBrush(QColor("#7f9dad"));painter.drawRoundedRect(QRectF(62,158,132,92),30,30);
  painter.setPen(QPen(QColor("#34445c"),3));painter.drawLine(101,94,112,94);painter.drawLine(145,94,156,94);
  painter.setPen(QPen(QColor("#aa736f"),3));painter.drawArc(QRectF(117,110,25,18),180*16,180*16);}
 project.shots[0].layers[0].tiles[{0,0}]=tile;
 auto second=Project::create().shots.first();second.number="C002";second.name="Reverse angle";second.layers[0].name="Hana · color B1";
 second.layers[0].tiles[{0,0}]=tile;project.shots.append(second);
 const auto path=temporary.filePath("character-recolor.toonproj");
 if(!saveProject(project,path,&error)||!window.openProject(path)){cap.errors<<error;cap.finish();return;}
 auto *doc=window.canvas()->document();const auto hana=doc->addCharacter("Hana");
 using C=CharacterColorCategory;
 const auto skin=doc->addCharacterColor(hana,"Skin base",C::Skin,QColor("#edb995"));
 const auto hair=doc->addCharacterColor(hana,"Hair base",C::Hair,QColor("#34445c"));
 const auto coat=doc->addCharacterColor(hana,"Coat",C::Costume,QColor("#7f9dad"));
 QString night;for(const auto &v:doc->project().characters.first().colorVariants)if(v.name=="Night")night=v.id;
 doc->updateCharacterColor(hana,skin,"Skin base",C::Skin,QColor("#8b91bb"),nullptr,night);
 doc->updateCharacterColor(hana,hair,"Hair base",C::Hair,QColor("#22243e"),nullptr,night);
 doc->updateCharacterColor(hana,coat,"Coat",C::Costume,QColor("#4b6386"),nullptr,night);
 auto *dock=window.findChild<QDockWidget*>("referencesPanel");dock->setFloating(true);dock->resize(260,430);dock->show();dock->raise();
 dock->findChild<QTabBar*>("referenceTabs")->setCurrentIndex(2);
 auto *colors=dock->findChild<CharacterColorPanel*>();colors->selectCharacter(hana);colors->selectVariant(night);
 cap.shot(dock,"Recolor","Color Models entry point");dock->hide();
 CharacterRecolorDialog dialog(doc,hana,night,&window);dialog.show();dialog.raise();settle();
 cap.shot(&dialog,"Recolor","Choose drawings");
 dialog.findChild<QComboBox*>("characterRecolorScope")->setCurrentIndex(1);
 dialog.findChild<QToolButton*>("characterRecolorSelectAll")->click();dialog.preview();
 for(int i=0;dialog.busy()&&i<1000;++i)QTest::qWait(10);
 if(dialog.busy()||!dialog.previewResult().error.isEmpty())cap.errors<<"Recolor preview failed: "+dialog.previewResult().error;
 cap.shot(&dialog,"Recolor","Night preview");
 dialog.findChild<QListWidget*>("characterRecolorDrawings")->setCurrentRow(1);
 cap.shot(&dialog,"Recolor","Second shot selected drawing");
 for(auto *tab:dialog.findChildren<QTabBar*>())if(tab->count()==2){tab->setCurrentIndex(0);cap.shot(&dialog,"Recolor","Original drawing");tab->setCurrentIndex(1);}
 dialog.resize(520,680);cap.shot(&dialog,"Recolor","Narrow preview");
 const auto font=qApp->font();auto larger=font;larger.setPointSizeF(font.pointSizeF()+3);qApp->setFont(larger);
 dialog.resize(680,680);cap.shot(&dialog,"Recolor","Larger text");qApp->setFont(font);
 if(!dialog.applyPreview())cap.errors<<"Could not apply recolor preview";
 cap.shot(window.canvas(),"Recolor","Applied artwork");doc->history()->undo();
 cap.shot(window.canvas(),"Recolor","Undo restores original");
 CharacterRecolorDialog unchanged(doc,hana,{},&window);unchanged.show();
 unchanged.findChild<QComboBox*>("characterRecolorTargetPalette")->setCurrentIndex(0);unchanged.preview();
 for(int i=0;unchanged.busy()&&i<1000;++i)QTest::qWait(10);
 cap.shot(&unchanged,"Recolor","No changes to apply");unchanged.reject();
 cap.finish();doc->history()->setClean();window.hide();
}

static void celWorkflowCaptures(Capture &cap, MainWindow &window) {
 QTemporaryDir temporary; QString error; auto project=Project::create();
 project.width=320;project.height=240;
 auto &layers=project.shots[0].layers;layers[0].name="Inks";
 QImage ink(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);ink.fill(Qt::transparent);
 {QPainter p(&ink);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(QColor("#30323b"),3));p.drawEllipse(QRectF(45,25,170,170));}
 layers[0].tiles[{0,0}]=ink;
 auto paint=RasterLayer::create("Skin paint");QImage color(ink.size(),ink.format());color.fill(Qt::transparent);
 {QPainter p(&color);p.setPen(Qt::NoPen);p.setBrush(QColor("#eac3a7"));p.drawEllipse(QRectF(46,26,168,168));p.setCompositionMode(QPainter::CompositionMode_Clear);p.drawRect(QRect(95,100,3,3));p.setCompositionMode(QPainter::CompositionMode_SourceOver);p.fillRect(QRect(220,210,1,1),Qt::black);}
 paint.tiles[{0,0}]=color;layers.prepend(paint);
 const auto path=temporary.filePath("cel-workflows.toonproj");
 if(!saveProject(project,path,&error)||!window.openProject(path)){cap.errors<<error;cap.finish();return;}
 auto *canvas=window.canvas();auto *doc=canvas->document();
 auto *check=window.findChild<CelPaintCheckPanel*>();auto *shading=window.findChild<CelShadingPanel*>();
 auto *light=window.findChild<LightTablePanel*>();
 auto *refs=window.findChild<QDockWidget*>("referencesPanel");
 auto *checkDock=window.findChild<QDockWidget*>("celPaintCheckPanel");auto *shadeDock=window.findChild<QDockWidget*>("celShadingPanel");
 doc->selectLayer(0);check->startCheck();
 for(int i=0;check->busy()&&i<400;++i)QTest::qWait(10);
 if(check->busy())cap.errors<<"Paint check timed out";
 for(auto *dock:{checkDock,shadeDock,refs}){dock->setFloating(true);dock->move(50,60);dock->resize(280,560);}
 checkDock->show();checkDock->raise();cap.shot(checkDock,"Cel Paint Check","Review holes and stray pixels");checkDock->hide();
 shadeDock->show();shadeDock->raise();shading->setOutlineLayerId(layers.last().id);
 QPainterPath boundary(QPointF(130,25));boundary.cubicTo(170,80,80,140,130,195);
 shading->addBoundary(boundary,CelShadeKind::Shadow);shading->addFill({75,100},CelShadeKind::Shadow,QColor("#9b7893"));
 for(int i=0;shading->busy()&&i<400;++i)QTest::qWait(10);
 cap.shot(shadeDock,"Cel Shading","Boundary and separate shade preview");
 shading->findChild<QTabBar*>("celShadingMode")->setCurrentIndex(1);cap.shot(canvas,"Cel Shading","Finished boundary preview");shadeDock->hide();
 refs->findChild<QTabBar*>("referenceTabs")->setCurrentIndex(3);refs->show();refs->raise();
 light->pinLayer(doc->activeShotId(),layers.last().id,&error);
 for(int i=0;light->busy()&&i<400;++i)QTest::qWait(10);
 if(light->pins().isEmpty())cap.errors<<"Light table snapshot missing: "+error;
 light->setPinTransform(0,{18,8},1.1,12);light->setEditing(true);
 cap.shot(refs,"Light Table","Pinned drawing and trace controls");cap.shot(canvas,"Light Table","Shift rotate and scale handles");light->setEditing(false);
 if(auto *save=window.findChild<QAction*>("saveProjectAction"))save->trigger();
 if(window.isWindowModified()||!window.openProject(path))cap.errors<<"Could not save and reopen drawing aids";
 if(light->pins().size()!=1||light->pins()[0].offset!=QPointF(18,8)||!shading->pending())cap.errors<<"Drawing aids did not restore";
 cap.shot(refs,"Light Table","Restored embedded snapshot");
 refs->hide();shadeDock->show();shadeDock->raise();
 for(int i=0;shading->busy()&&i<400;++i)QTest::qWait(10);
 cap.shot(shadeDock,"Cel Shading","Restored unfinished draft");shadeDock->hide();refs->show();refs->raise();
 const auto font=qApp->font();auto larger=font;larger.setPointSizeF(font.pointSizeF()+3);qApp->setFont(larger);
 refs->resize(240,460);cap.shot(refs,"Light Table","Narrow with larger text");qApp->setFont(font);
 refs->hide();shadeDock->resize(220,420);shadeDock->show();cap.shot(shadeDock,"Cel Shading","Narrow panel");shadeDock->hide();
 checkDock->resize(220,420);checkDock->show();check->findChild<QTabWidget*>("celPaintCheckTabs")->setCurrentIndex(0);cap.shot(checkDock,"Cel Paint Check","Narrow settings");
 cap.finish();doc->history()->setClean();window.hide();
}

int main(int argc,char **argv) {
 QApplication app(argc,argv);QApplication::setApplicationName("Toon Pro UX Capture");QApplication::setApplicationVersion(qEnvironmentVariable("TOON_CAPTURE_APP_VERSION", "development"));QStandardPaths::setTestModeEnabled(true);applyTheme();
 const auto args=app.arguments();if(args.size()<2)return 2;Capture cap;cap.directory=QDir(args[1]).absolutePath();QDir().mkpath(cap.directory+"/screenshots");QTemporaryDir tmp;
 MainWindow window(tmp.filePath("workspace.ini"));window.resize(1500,950);window.show();settle();cap.shot(&window,"Workspace","Welcome — no project");
 if(args.contains("--cel-workflows")){celWorkflowCaptures(cap,window);return cap.errors.isEmpty()?0:1;}
 if(args.contains("--character-variants")){characterVariantCaptures(cap,window);return cap.errors.isEmpty()?0:1;}
 if(args.contains("--character-recolor")){characterRecolorCaptures(cap,window);return cap.errors.isEmpty()?0:1;}
 if(args.contains("--character-colors")){characterColorCaptures(cap,window);return cap.errors.isEmpty()?0:1;}
 if(args.contains("--character-models")){characterModelCaptures(cap,window);return cap.errors.isEmpty()?0:1;}
 if(args.contains("--drawing-inspectors")){drawingInspectorsCaptures(cap,window);return cap.errors.isEmpty()?0:1;}
 if(args.contains("--reference-effects")){referenceEffectsCaptures(cap,window);return cap.errors.isEmpty()?0:1;}
 if(args.contains("--advanced-management")){advancedManagementCaptures(cap,window);return cap.errors.isEmpty()?0:1;}
 if(args.contains("--drawing-composition")){drawingCompositionCaptures(cap,window);return cap.errors.isEmpty()?0:1;}
 if(args.contains("--drawing-density")){drawingDensityCaptures(cap,window);return cap.errors.isEmpty()?0:1;}
 if(args.contains("--extras")){extraCaptures(cap,window);return 0;}
 const auto docks=window.findChildren<QDockWidget*>();
 for(auto *d:docks)cap.dock(d,"No project",false);
 auto p=fixture();auto *canvas=window.canvas();auto *doc=canvas->document();auto *catalog=window.toolCatalog();
 canvas->setProject(p);doc->selectLayer(1);canvas->fitSheet();
 for(const auto id:{"toolsPanel","brushPresetPanel","propertiesPanel","layersPanel"})if(auto *d=window.findChild<QDockWidget*>(id)){d->setFloating(false);d->show();d->raise();}
 settle();cap.shot(&window,"Workspace","Drawing workspace — populated project");
 for(auto *t:window.findChildren<QToolBar*>())if(!t->objectName().isEmpty()){t->show();cap.shot(t,"Toolbars",t->objectName());}
 for(auto *d:docks)cap.dock(d,"Project open");
 auto *prop=window.findChild<QDockWidget*>("propertiesPanel");prop->setFloating(true);prop->resize(320,650);prop->show();
 using T=Canvas::Tool;const QList<QPair<T,QString>> tools={{T::Brush,"Brush"},{T::Eraser,"Eraser"},{T::Hand,"Hand"},{T::RectangleSelect,"Rectangle selection"},{T::LassoSelect,"Lasso selection"},{T::EllipseSelect,"Ellipse selection"},{T::PolygonSelect,"Polygon selection"},{T::MagicWand,"Magic wand"},{T::ColorRange,"Color range"},{T::Eyedropper,"Eyedropper"},{T::Bucket,"Paint bucket"},{T::Gradient,"Gradient"},{T::Line,"Line"},{T::Rectangle,"Rectangle"},{T::Ellipse,"Ellipse"},{T::Polygon,"Polygon"},{T::Arc,"Arc"},{T::Curve,"Bezier curve"},{T::Text,"Artwork text"},{T::Retouch,"Retouch"},{T::VectorPen,"Vector pen"},{T::VectorEraser,"Vector eraser"},{T::VectorNode,"Path Edit"},{T::VectorSimplify,"Simplify Brush"},{T::VectorWidth,"Width Brush"},{T::AdvancedTransform,"Advanced transform"}};
 canvas->setToolRouter({});
 for(const auto &entry:tools){doc->cancelTransform();canvas->drawingTools()->cancel();canvas->setTool(entry.first);prop->show();prop->resize(320,620);cap.page(prop,"Tool Properties",entry.second+" — raster target");cap.tabs(prop,"Tool Properties",entry.second);}
 canvas->setTool(T::RectangleSelect);QPainterPath select;select.addRect(320,25,160,210);doc->setRasterSelection(select);cap.page(prop,"Selection and Transform","Selection active");cap.tabs(prop,"Selection and Transform","Selection active");cap.shot(canvas,"Canvas overlays","Selection boundary, outside shade and contextual toolbar");
 canvas->setTool(T::Transform);if(!doc->transformActive())doc->beginTransform();TransformParameters transform;transform.translation={45,30};transform.rotation=12;doc->previewTransform(transform);cap.page(prop,"Selection and Transform","Pending transform — Apply and Cancel");cap.shot(canvas,"Canvas overlays","Pending raster transform handles");
 cap.dialog([&]{canvas->setTool(T::Brush);},"Decisions","Pending transform — Apply Discard Cancel",false);doc->cancelTransform();doc->clearRasterSelection();
 canvas->setTool(T::Guides);for(int i=0;i<6;++i)canvas->addDefaultGuide(GuideType(i));cap.page(prop,"Guides","Multiple perspective assistants");cap.tabs(prop,"Guides","Multiple assistants");cap.shot(canvas,"Canvas overlays","Perspective assistants and handles");
 for(int i=0;i<6;++i){const auto gs=canvas->drawingGuides();if(i<gs.size())canvas->selectGuide(gs[i].id);cap.page(prop,"Guides",guideTypeName(GuideType(i))+" selected");}
 canvas->setTool(T::Brush);if(auto *brush=window.findChild<BrushSettingsPanel*>()){
  brush->openAdvancedSettings();auto *dialog=brush->findChild<QDialog*>("advancedBrushDialog");dialog->resize(560,720);cap.page(dialog,"Advanced Brush","General");cap.tabs(dialog,"Advanced Brush","Category");dialog->resize(360,480);cap.page(dialog,"Advanced Brush","Narrow layout");dialog->close();
  auto b=canvas->brush;b.texture.enabled=true;b.texture.pattern=p.shots[0].references[0].image;b.texture.name="Sample grain";b.dual.enabled=true;b.dual.tipMask=b.texture.pattern;b.colorDynamics.enabled=true;b.colorDynamics.hue=.15;b.scatter=.5;b.count=3;b.sizeDynamics.control=BrushControl::Pressure;b.sizeDynamics.jitter=.2;b.symmetry.mode=SymmetryMode::Radial;canvas->setBrushSettings(b);
  brush->openAdvancedSettings();dialog->resize(560,720);cap.tabs(dialog,"Advanced Brush","Enabled texture dual color scatter symmetry dynamics");dialog->close();
 }
 // Global organization and modal creation flows.
 for(auto *nav:window.findChildren<ToolNavigationPanel*>())if(nav->objectName()=="toolboxContent"||nav->objectName()=="toolListContent"){
  cap.dialog([&]{nav->openAddDialog();},"Add Tools",nav->objectName());
  if(nav->objectName()=="toolListContent"){
   auto *d=window.findChild<QDockWidget*>("toolsPanel");d->show();nav->setIconsOnly(true);cap.page(d,"Tools","Icon grid");nav->setIconsOnly(false);
   if(auto *search=nav->findChild<QLineEdit*>("toolPanelSearch")){search->setText("no-such-sample-tool");cap.page(d,"Tools","No search matches");search->clear();}
   d->hide();
  }
 }
 auto *bm=window.findChild<BrushManagementPanel*>();if(bm){auto *d=window.findChild<QDockWidget*>("brushManagementPanel");d->show();if(auto *list=bm->findChild<QListWidget*>("managedBrushes"))if(list->count())list->setCurrentRow(0);cap.page(d,"Brush Management","Brush selected");
  if(auto *add=bm->findChild<QToolButton*>("brushManagerAdd"))cap.dialog([&]{add->click();},"New Brush","Type and metadata");d->hide();}
 auto *tm=window.findChild<ToolManagementPanel*>();if(tm){auto *d=window.findChild<QDockWidget*>("toolManagementPanel");d->show();tm->focusEntry("group/drawing");cap.page(d,"Tool Management","Drawing group selected");cap.tabs(d,"Tool Management","Selected group");
  for(auto *b:tm->findChildren<QToolButton*>())if(b->toolTip().contains("membership",Qt::CaseInsensitive))cap.dialog([&]{b->click();},"Tool Management","Group membership");d->hide();}
 // Vector tool contexts with an actual editable brush path.
 doc->cancelTransform();canvas->drawingTools()->cancel();auto vectorProject=p;auto vector=RasterLayer::create("Editable vector ink");vector.kind=LayerKind::Vector;vectorProject.shots[0].layers.append(vector);canvas->setProject(vectorProject);doc->selectLayer(vectorProject.shots[0].layers.size()-1);canvas->setTool(T::Brush);BrushSettings ink;ink.size=8;doc->beginStroke({200,160},1,ink,false);for(int i=1;i<25;++i)doc->continueStroke({200+i*10.,160+35*std::sin(i*.2)},.8);doc->endStroke();
 canvas->setTool(T::VectorNode);canvas->fitSheet();prop->show();cap.page(prop,"Vector Editing","No path selected");
 const auto obj=doc->activeShot().layers[doc->activeLayer()].objects.value(0);if(!obj.id.isEmpty())canvas->drawingTools()->selectAnimatedPath(obj.id);
 for(auto tool:{T::VectorNode,T::VectorSimplify,T::VectorWidth,T::VectorEraser}){canvas->setTool(tool);cap.page(prop,"Vector Editing",QString("Tool %1 — path selected").arg(int(tool)));cap.tabs(prop,"Vector Editing",QString("Tool %1").arg(int(tool)));}cap.shot(canvas,"Canvas overlays","Editable vector brush path");
 // 3D modes use the built-in mannequin; isolated from the artist's library.
 canvas->drawingTools()->cancel();canvas->setProject(p);auto *scene=canvas->scene3D();scene->addModel(builtInModels3D().value(3));scene->selectTool(false);prop->show();cap.page(prop,"3D Properties","Model selected");cap.tabs(prop,"3D Properties","Object");scene->selectTool(true);cap.page(prop,"3D Properties","Camera");cap.tabs(prop,"3D Properties","Camera");
 scene->selectRigTool();scene->createRig(true);cap.page(prop,"3D Rig","Humanoid rest skeleton");cap.tabs(prop,"3D Rig","Rest skeleton");
 scene->setRigEditMode(Scene3DController::RigEditMode::Weights);cap.page(prop,"3D Weights","Unbound model");
 scene->setRigEditMode(Scene3DController::RigEditMode::Pose);cap.page(prop,"3D Pose","Pose mode without binding");cap.tabs(prop,"3D Pose","Pose mode");scene->endEdit();
 // Timeline, keyframes, audio, camera and export dialogs.
 canvas->setProject(p);auto *timeline=window.findChild<TimelineController*>();timeline->addTrack(true,DrawingTemplate::Anime,"Character animation");timeline->newDrawing();timeline->setFrame(12);timeline->newDrawing();timeline->setPlaybackRange(0,71);timeline->setPlaybackRangeEnabled(true);timeline->setOnionSkin(true);
 auto *timelineDock=window.findChild<QDockWidget*>("animationPanel");cap.dock(timelineDock,"Frame animation with layered template and exposures");
 auto *frameDock=window.findChild<QDockWidget*>("framePropertiesPanel");cap.dock(frameDock,"Frame animation drawing selected");
 doc->selectLayer(0);timeline->selectPropertyLayer(doc->activeShot().layers[0].id);timeline->setPropertyEnabled(KeyframeProperty::PositionX,true);timeline->setPropertyKey(KeyframeProperty::PositionX);timeline->setFrame(30);timeline->beginPropertyEdit(KeyframeProperty::PositionX);timeline->previewPropertyValue(KeyframeProperty::PositionX,160);timeline->finishPropertyEdit();cap.dock(frameDock,"Keyframe animation selected");
 timeline->selectAudioClip(doc->activeShot().audioClips[0].id);cap.dock(frameDock,"Audio clip selected");timeline->selectCamera();cap.dock(frameDock,"2D camera selected");
 cap.dialog([&]{QDialog *d=new ProjectDialog(p,true,&window);d->exec();delete d;},"Project Setup","New movie project");
 cap.dialog([&]{ProjectDialog d(p,false,&window);d.exec();},"Project Setup","Edit shot and production sheet");
 cap.dialog([&]{OnionSettingsDialog d(canvas->onionSkinSettings(),&window);d.exec();},"Onion Skin","Settings");
 cap.dialog([&]{TimelineRippleDialog d(timeline,&window);d.exec();},"Timeline Editing","Insert delete and move time");
 cap.dialog([&]{VideoExportDialog d(doc->project(),0,0,71,true,&window);d.exec();},"Video Export","Shot movie and queue");
 cap.dialog([&]{AudioExportDialog d(doc->project(),0,0,71,true,&window);d.exec();},"Audio Export","Shot and movie mix");
 timelineDock->show();for(auto *b:timelineDock->findChildren<QToolButton*>())if(b->objectName()=="timelineAddTrack")cap.dialog([&]{b->click();},"Add Track","Static and frame animation templates");
 // History and recorded action variants.
 canvas->setProject(p);doc->selectLayer(1);auto *actions=window.findChild<AutoActions*>();const auto id=actions->create("Ink setup and place");QPainterPath region;region.addRect(300,20,200,220);doc->setRasterSelection(region);actions->startRecording(id);doc->fillRasterSelection(QColor("#b66e56"));doc->beginTransform();transform={};transform.translation={20,10};doc->previewTransform(transform);doc->applyTransform();auto *actionDock=window.findChild<QDockWidget*>("autoActionsPanel");cap.dock(actionDock,"Recording fill and transform");actions->stopRecording();cap.dock(actionDock,"Recorded workflow ready to play");actions->setStepEnabled(id,0,false);cap.dock(actionDock,"Optional step unchecked");auto *historyDock=window.findChild<QDockWidget*>("historyPanel");cap.dock(historyDock,"Populated history");doc->history()->undo();cap.dock(historyDock,"Undo with available redo state");
 // Menus expose occasional commands without running them.
 for(auto *d:docks){d->show();for(auto *list:d->findChildren<QListWidget*>())if(list->isVisibleTo(d)&&list->count()){list->setCurrentRow(0);cap.menu(list->viewport(),d->property("panelLabel").toString(),"Context menu");break;}d->hide();}
 for(auto *a:window.menuBar()->actions())if(a->menu()){auto *m=a->menu();m->popup({50,50});settle();cap.shot(m,"Application menus",a->text().remove('&'));m->close();}
 // Last overall view and focused drawing mode.
 doc->cancelTransform();doc->clearRasterSelection();canvas->setTool(T::Brush);for(const auto id:{"toolsPanel","brushPresetPanel","propertiesPanel","layersPanel"})if(auto *d=window.findChild<QDockWidget*>(id)){d->setFloating(false);d->show();d->raise();}window.resize(1500,950);canvas->fitSheet();settle();cap.shot(&window,"Workspace","Final drawing layout");window.setDrawingMode(true);settle();cap.shot(&window,"Workspace","Tab drawing mode — quick access only");window.setDrawingMode(false);
 cap.finish();doc->history()->setClean();window.hide();return cap.screens.isEmpty()?1:0;
}
