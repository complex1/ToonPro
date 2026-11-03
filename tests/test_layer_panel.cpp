#include "canvas.h"
#include "document.h"
#include "drawing_panels.h"
#include "layer_panel.h"
#include "icons.h"
#include "ui_metrics.h"
#include "main_window.h"
#include "timeline_controller.h"
#include "value_slider.h"
#include <QComboBox>
#include <QAction>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QSlider>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QScrollBar>
#include <QScopeGuard>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QtTest>
#include <cmath>
#include <functional>
using namespace toon;
namespace {
Project groupedProject() {
    auto p=Project::create();p.width=640;p.height=480;
    auto background=RasterLayer::create("Background");
    auto ink=RasterLayer::create("Main lines"), shadow=RasterLayer::create("Skin base"), notes=RasterLayer::create("Pose notes"), group=RasterLayer::create("Character");
    group.group=true;ink.parentId=shadow.parentId=group.id;notes.annotation=true;shadow.clipped=true;shadow.rowColor=QColor("#77bfaa");
    QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);
    {QPainter paint(&tile);paint.setPen(QPen(QColor("#484869"),5));paint.drawEllipse(QRect(60,35,110,140));paint.drawLine(115,175,110,240);}
    ink.tiles.insert({0,0},tile);tile.fill(Qt::transparent);{QPainter paint(&tile);paint.setPen(Qt::NoPen);paint.setBrush(QColor("#efbd9f"));paint.drawEllipse(QRect(60,35,110,140));}shadow.tiles.insert({0,0},tile);
    p.shots.first().layers={background,ink,shadow,group,notes};return p;
}
QTreeWidgetItem *item(QTreeWidget *tree,const QString &id){for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString()==id)return *it;return nullptr;}
const RasterLayer &active(const Document &d){return d.project().shots.first().layers[d.activeLayer()];}
QPoint rowPoint(QTreeWidget *tree,QTreeWidgetItem *i,int column=1){const auto rect=tree->visualItemRect(i);return {tree->columnViewportPosition(column)+(column==1?65:14),rect.center().y()};}
void moveHeld(QWidget *widget,QPoint point){QMouseEvent e(QEvent::MouseMove,QPointF(point),QPointF(widget->mapToGlobal(point)),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(widget,&e);}
QRgb imageLayerPixel(const RasterLayer &layer, int x, int y) {
    const TileKey key(int(std::floor(x / qreal(tileSize))), int(std::floor(y / qreal(tileSize))));
    const auto tile = layer.tiles.constFind(key);
    return tile == layer.tiles.cend() ? QRgb(0) : tile->pixel(x - key.first * tileSize, y - key.second * tileSize);
}
bool prepareImageImport(MainWindow &window, const QTemporaryDir &directory) {
    auto project = Project::create(); project.width = 640; project.height = 480;
    project.shots.first().layers = {RasterLayer::create("Drawing")};
    QString error;
    const auto path = directory.filePath("drawing.toonproj");
    if (!saveProject(project, path, &error) || !window.openProject(path)) return false;
    window.resize(1280, 860); window.show();
    QCoreApplication::processEvents();
    return true;
}
struct ImageChooserResult { bool opened = false; bool accepted = false; };
ImageChooserResult chooseArtwork(MainWindow &window, QToolButton *button, const QString &path = {},
                                const std::function<void()> &onOpen = {}) {
    const bool previous = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    const auto restore = qScopeGuard([previous] { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous); });
    ImageChooserResult result;
    const QFileInfo source(path);
    QTimer choose, timeout; choose.setInterval(25); timeout.setSingleShot(true);
    QObject::connect(&choose, &QTimer::timeout, &window, [&] {
        auto *dialog = window.findChild<QFileDialog *>();
        if (!dialog || !dialog->isVisible()) return;
        if (!result.opened) {
            result.opened = true;
            if (onOpen) onOpen();
            if (path.isEmpty()) { choose.stop(); dialog->reject(); return; }
            QObject::connect(dialog, &QFileDialog::accepted, &choose, [&] {
                result.accepted = true; choose.stop();
            });
            dialog->setDirectory(source.absolutePath());
            // QFileSystemModel populates asynchronously. Let directory changes
            // settle before selecting the file, especially in native Qt runs.
            return;
        }
        dialog->selectFile(source.absoluteFilePath());
        // A visible chooser may keep its focused filename editor instead of
        // applying selectFile(). Drive that ordinary editor as well, and retry
        // until the real dialog emits accepted rather than stopping early.
        if (auto *name = dialog->findChild<QLineEdit *>("fileNameEdit")) name->setText(source.fileName());
        const auto selected = dialog->selectedFiles();
        if (selected.size() == 1 && QFileInfo(selected.first()).absoluteFilePath() == source.absoluteFilePath())
            QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
    });
    QObject::connect(&timeout, &QTimer::timeout, &window, [&] {
        for (auto *dialog : window.findChildren<QFileDialog *>()) if (dialog->isVisible()) dialog->reject();
    });
    choose.start(); timeout.start(5000);
    button->click();
    choose.stop(); timeout.stop();
    return result;
}
}
class LayerPanelTests:public QObject{
 Q_OBJECT
private slots:
 void initTestCase(){applyTheme();}
 void hierarchyInlineRenameAndExpansion(){
    Document doc;LayerPanel panel(&doc);auto *tree=panel.findChild<QTreeWidget*>("layerTree");QVERIFY(!tree->isEnabled());auto p=groupedProject();doc.load(p);panel.resize(270,480);panel.show();QTest::qWait(20);
    QCOMPARE(tree->topLevelItemCount(),3);const auto groupId=p.shots.first().layers[3].id;auto *folder=item(tree,groupId);QCOMPARE(folder->childCount(),2);QVERIFY(folder->isExpanded());
    const auto inkId=p.shots.first().layers[1].id;auto *ink=item(tree,inkId);tree->setCurrentItem(ink,1);QCOMPARE(active(doc).id,inkId);
    tree->editItem(ink,1);auto *editor=tree->findChild<QLineEdit*>("layerNameEditor");QVERIFY(editor);editor->selectAll();QTest::keyClicks(editor,"Cel lines");QTest::keyClick(editor,Qt::Key_Return);
    QTRY_COMPARE(active(doc).name,QString("Cel lines"));folder->setExpanded(false);doc.history()->undo();QCOMPARE(item(tree,inkId)->text(1),QString("Main lines"));QVERIFY(!item(tree,groupId)->isExpanded());
    QVERIFY(!panel.findChild<QWidget*>("layerParent"));QVERIFY(!panel.findChild<QWidget*>("layerName"));
 }
 void eyeDragShowsTouchedRowsAsOneUndoWithoutChangingTarget(){
    Document doc;auto p=groupedProject();doc.load(p);LayerPanel panel(&doc);panel.resize(270,480);panel.show();QTest::qWait(20);auto *tree=panel.findChild<QTreeWidget*>("layerTree");
    const auto target=active(doc).id;auto *first=tree->topLevelItem(0);auto *last=tree->topLevelItem(2);
    QTest::mousePress(tree->viewport(),Qt::LeftButton,Qt::NoModifier,rowPoint(tree,first,0));moveHeld(tree->viewport(),rowPoint(tree,last,0));
    QCOMPARE(doc.history()->count(),0);for(const auto &l:doc.project().shots.first().layers)QVERIFY(!l.visible);
    QTest::mouseRelease(tree->viewport(),Qt::LeftButton,Qt::NoModifier,rowPoint(tree,last,0));QCOMPARE(doc.history()->count(),1);QCOMPARE(active(doc).id,target);
    doc.history()->undo();for(const auto &l:doc.project().shots.first().layers)QVERIFY(l.visible);
    QTest::mousePress(tree->viewport(),Qt::LeftButton,Qt::NoModifier,rowPoint(tree,first,0));QTest::keyClick(tree,Qt::Key_Escape);QVERIFY(doc.project().shots.first().layers.last().visible);QVERIFY(!doc.layerPropertyEditActive());
 }
 void compactHeaderFooterStayPinned_data(){QTest::addColumn<int>("width");QTest::addColumn<int>("height");QTest::newRow("compact")<<220<<220;QTest::newRow("narrow")<<240<<220;QTest::newRow("normal")<<270<<520;}
 void compactHeaderFooterStayPinned(){
    QFETCH(int,width);QFETCH(int,height);Document doc;doc.load(groupedProject());LayerPanel panel(&doc);panel.resize(width,height);panel.show();QTest::qWait(20);
    QVERIFY(panel.widget()->width()<=panel.viewport()->width());QCOMPARE(panel.horizontalScrollBar()->maximum(),0);QCOMPARE(panel.verticalScrollBar()->maximum(),0);
    auto *tree=panel.findChild<QTreeWidget*>("layerTree");auto *blend=panel.findChild<QComboBox*>("layerBlendMode");auto *trash=panel.findChild<QToolButton*>("removeLayer");
    const auto top=blend->mapTo(&panel,QPoint()),bottom=trash->mapTo(&panel,QPoint());tree->verticalScrollBar()->setValue(tree->verticalScrollBar()->maximum());
    QCOMPARE(blend->mapTo(&panel,QPoint()),top);QCOMPARE(trash->mapTo(&panel,QPoint()),bottom);QVERIFY(bottom.y()+trash->height()<=panel.height());QVERIFY(top.y()<tree->mapTo(&panel,QPoint()).y());
    QCOMPARE(tree->visualItemRect(tree->topLevelItem(0)).height(),qMax(ui::metrics::layerRow,tree->fontMetrics().height()+10));
    QVERIFY(tree->columnWidth(0)>=ui::metrics::actionTarget);QVERIFY(tree->columnWidth(2)>=ui::metrics::actionTarget);
    QCOMPARE(panel.findChild<QToolButton*>("layerAlphaLocked")->icon().name(),featureIconName(FeatureIcon::AlphaLock));
    QCOMPARE(panel.findChild<QToolButton*>("layerClipped")->icon().name(),featureIconName(FeatureIcon::Clipping));
    for(auto *button:panel.findChildren<QToolButton*>()){if(!button->isVisible())continue;QVERIFY(!button->toolTip().isEmpty());QVERIFY(!button->accessibleName().isEmpty());QVERIFY(!button->icon().isNull());
        QCOMPARE(button->size(),QSize(28,28));QCOMPARE(button->iconSize(),QSize(16,16));const auto point=button->mapTo(&panel,QPoint());QVERIFY(point.x()>=0);QVERIFY(point.x()+button->width()<=width);}
    QVERIFY(panel.findChild<QToolButton*>("addVectorLayer")->isEnabled());
    auto *opacity=panel.findChild<ValueSlider*>("layerOpacity");QVERIFY(opacity);
    QVERIFY(opacity->findChild<QSlider*>()->width()>=70);QVERIFY(opacity->findChild<QDoubleSpinBox*>()->width()<=80);
    QVERIFY(opacity->mapTo(&panel,QPoint()).y()>blend->mapTo(&panel,QPoint()).y());
 }
 void largerFontsKeepNamesAndRenameDraftReadable(){
    Document doc;doc.load(groupedProject());LayerPanel panel(&doc);panel.resize(320,600);panel.show();panel.activateWindow();
    auto *tree=panel.findChild<QTreeWidget*>("layerTree");QTest::qWait(20);
    const int before=tree->visualItemRect(tree->topLevelItem(0)).height();
    auto font=panel.font();font.setPixelSize(25);panel.setFont(font);QCoreApplication::processEvents();
    QCOMPARE(tree->font().pixelSize(),25);
    QTRY_VERIFY(tree->visualItemRect(tree->topLevelItem(0)).height()>before);
    const auto bounds=tree->visualItemRect(tree->topLevelItem(0));QVERIFY(bounds.height()>=tree->fontMetrics().height()+8);
    auto *target=tree->topLevelItem(0);const auto targetId=target->data(0,Qt::UserRole).toString();tree->setCurrentItem(target,1);QCOMPARE(active(doc).id,targetId);tree->editItem(target,1);
    auto *editor=tree->findChild<QLineEdit*>("layerNameEditor");QVERIFY(editor);editor->setFocus();editor->selectAll();QTest::keyClicks(editor,"Draft layer name");
    panel.resize(260,600);QCoreApplication::processEvents();
    QCOMPARE(editor->text(),QString("Draft layer name"));QVERIFY(editor->hasFocus());
    QVERIFY(editor->height()>=editor->fontMetrics().height()+4);
    const auto pos=editor->mapTo(tree->viewport(),QPoint());const auto row=tree->visualItemRect(target);
    QVERIFY(pos.y()>=row.top());QVERIFY(pos.y()+editor->height()<=row.bottom()+1);
    QTest::keyClick(editor,Qt::Key_Return);
    // Qt queues the delegate's commit/close after the editor Return event.
    QTRY_COMPARE(active(doc).name,QString("Draft layer name"));QCOMPARE(active(doc).id,targetId);
    QCOMPARE(item(tree,targetId)->text(1),QString("Draft layer name"));QCOMPARE(doc.history()->count(),1);
 }
 void multiSelectionAndActiveLayerStayConsistent(){
    Document doc;auto p=groupedProject();doc.load(p);LayerPanel panel(&doc);panel.resize(270,480);panel.show();QTest::qWait(20);auto *tree=panel.findChild<QTreeWidget*>("layerTree");
    auto *notes=tree->topLevelItem(0),*background=tree->topLevelItem(2);
    QTest::mouseClick(tree->viewport(),Qt::LeftButton,Qt::NoModifier,rowPoint(tree,notes));QTest::mouseClick(tree->viewport(),Qt::LeftButton,Qt::ShiftModifier,rowPoint(tree,background));
    QVERIFY(panel.selectedLayerIds().size()>=3);QVERIFY(!panel.findChild<QComboBox*>("layerBlendMode")->isEnabled());
    tree->clearSelection();notes->setSelected(true);background->setSelected(true);tree->setCurrentItem(background,1,QItemSelectionModel::NoUpdate);background->setSelected(false);
    QCOMPARE(panel.selectedLayerIds(),QStringList{p.shots.first().layers.last().id});QCOMPARE(active(doc).id,p.shots.first().layers.last().id);
    auto *blend=panel.findChild<QComboBox*>("layerBlendMode");blend->setCurrentIndex(blend->findData(int(LayerBlendMode::Multiply)));QCOMPARE(active(doc).blendMode,LayerBlendMode::Multiply);QCOMPARE(doc.project().shots.first().layers.first().blendMode,LayerBlendMode::Normal);
 }
 void liveOpacityAndFlags(){
    Document doc;doc.load(groupedProject());doc.selectLayer(1);LayerPanel panel(&doc);panel.resize(270,480);panel.show();QTest::qWait(20);auto *opacity=panel.findChild<ValueSlider*>("layerOpacity");
    auto *track=opacity->findChild<QSlider*>(); auto *number=opacity->findChild<QDoubleSpinBox*>(); QVERIFY(track); QVERIFY(number);
    track->setSliderDown(true);track->setValue(90);track->setValue(80);QCOMPARE(active(doc).opacity,.8);QCOMPARE(number->value(),80.);QCOMPARE(doc.history()->count(),0);
    track->setSliderDown(false);QCOMPARE(doc.history()->count(),1);doc.history()->undo();QCOMPARE(opacity->value(),100.);QCOMPARE(track->value(),100);
    track->setSliderDown(true);track->setValue(40);QCOMPARE(active(doc).opacity,.4);QTest::keyClick(track,Qt::Key_Escape);track->setSliderDown(false);
    QCOMPARE(active(doc).opacity,1.);QVERIFY(!doc.layerPropertyEditActive());QCOMPARE(doc.history()->index(),0);
    number->setFocus();number->selectAll();QTest::keyClicks(number,"65");QTest::keyClick(number,Qt::Key_Return);
    QCOMPARE(active(doc).opacity,.65);QCOMPARE(track->value(),65);QCOMPARE(doc.history()->count(),1);
    doc.history()->undo();QCOMPARE(number->value(),100.);
    panel.findChild<QToolButton*>("layerAlphaLocked")->click();QVERIFY(active(doc).alphaLocked);panel.findChild<QToolButton*>("layerPositionLocked")->click();QVERIFY(active(doc).positionLocked);
    panel.findChild<QToolButton*>("layerLocked")->click();QVERIFY(active(doc).locked);QVERIFY(!opacity->isEnabled());panel.findChild<QToolButton*>("layerLocked")->click();QVERIFY(!active(doc).locked);
 }
 void externalHistoryEndsPropertyGestureAndDocumentMayCloseFirst(){
    auto *doc=new Document;doc->load(Project::create());auto *panel=new LayerPanel(doc);panel->resize(270,480);panel->show();QTest::qWait(20);
    auto *opacity=panel->findChild<ValueSlider*>("layerOpacity");doc->addLayer();
    auto *number=opacity->findChild<QDoubleSpinBox*>();QVERIFY(number);number->setFocus();number->selectAll();QTest::keyClicks(number,"45");QVERIFY(doc->layerPropertyEditActive());
    doc->history()->undo();QVERIFY(!doc->layerPropertyEditActive());QCOMPARE(opacity->value(),100.);QCOMPARE(number->cleanText(),QString("100"));
    QTest::keyClick(number,Qt::Key_Return);QCOMPARE(active(*doc).opacity,1.);QCOMPARE(doc->history()->index(),0);
    delete doc;QVERIFY(!panel->findChild<QTreeWidget*>("layerTree")->isEnabled());delete panel;
 }
 void mainWindowUndoCancelsLiveOpacityBeforePreviousHistory(){
    MainWindow window;auto *canvas=window.findChild<Canvas*>();QVERIFY(canvas);canvas->setProject(Project::create());auto *doc=canvas->document();doc->addLayer();
    QVERIFY(doc->beginLayerPropertyEdit());doc->previewLayerOpacity(.3);auto *undo=window.findChild<QAction*>("undoAction");QVERIFY(undo->isEnabled());undo->trigger();
    QCOMPARE(doc->history()->index(),1);QVERIFY(!doc->layerPropertyEditActive());QCOMPARE(active(*doc).opacity,1.);
    undo->trigger();QCOMPARE(doc->project().shots.first().layers.size(),1);doc->history()->setClean();
 }
 void duplicateGroupDeleteAndContextColor(){
    Document doc;doc.load(Project::create());LayerPanel panel(&doc);panel.resize(270,480);panel.show();QTest::qWait(20);auto *tree=panel.findChild<QTreeWidget*>("layerTree");
    QVERIFY(!panel.findChild<QToolButton*>("removeLayer")->isEnabled());panel.findChild<QToolButton*>("duplicateLayer")->click();QCOMPARE(doc.project().shots.first().layers.size(),2);
    tree->selectAll();panel.findChild<QToolButton*>("addLayerGroup")->click();QVERIFY(active(doc).group);QCOMPARE(doc.project().shots.first().layers.size(),3);
    panel.findChild<QToolButton*>("addLayer")->click();QVERIFY(!active(doc).group);QVERIFY(!active(doc).parentId.isEmpty());
    QTimer::singleShot(0,[&]{auto *menu=panel.findChild<QMenu*>("layerContextMenu");QVERIFY(menu);bool merge=false,flat=false,raster=false,color=false;
        for(auto *a:menu->actions()){merge|=a->text()=="Merge Down";flat|=a->text()=="Flatten Image";if(a->text()=="Rasterize")raster=!a->isEnabled();if(a->text()=="Change Row Color"){color=true;for(auto *c:a->menu()->actions())if(c->text()=="Blue")c->trigger();}}
        QVERIFY(merge&&flat&&raster&&color);menu->close();});panel.findChild<QToolButton*>("layerMore")->click();QCOMPARE(active(doc).rowColor,QColor("#6995fe"));
    const auto id=active(doc).id;panel.findChild<QToolButton*>("removeLayer")->click();QVERIFY(!item(tree,id));doc.history()->undo();QVERIFY(item(tree,id));
 }
 void thumbnailsUpdateAfterPainting(){
    Canvas canvas;canvas.setProject(groupedProject());auto *doc=canvas.document();doc->selectLayer(1);LayerPanel panel(doc);panel.resize(270,480);panel.show();QTest::qWait(50);auto *tree=panel.findChild<QTreeWidget*>("layerTree");auto *ink=item(tree,active(*doc).id);
    // The role is deliberately discovered by its QImage payload, not the visual row's pixels.
    auto thumbnail=[&]{for(int role=Qt::UserRole;role<Qt::UserRole+20;++role){auto image=ink->data(0,role).value<QImage>();if(!image.isNull())return image;}return QImage();};
    const auto before=thumbnail();QVERIFY(!before.isNull());BrushSettings brush;brush.color=Qt::red;brush.size=60;QVERIFY(doc->beginStroke({150,120},1,brush,false));QVERIFY(tree->isEnabled());
    QTest::qWait(60);QCOMPARE(thumbnail(),before);doc->endStroke();QTRY_VERIFY(thumbnail()!=before);QVERIFY(tree->isEnabled());const auto output=qEnvironmentVariable("TOON_UI_PREVIEW","test-artifacts");QVERIFY(QDir().mkpath(output));QVERIFY(panel.grab().save(QDir(output).filePath("layers-panel.png")));
 }
 void importButtonUsesSharedActionAndGuardsPenContact(){
    QTemporaryDir directory;QVERIFY(directory.isValid());MainWindow window(directory.filePath("workspace.ini"));
    auto *button=window.findChild<QToolButton*>("importLayerImage");auto *action=window.findChild<QAction*>("importArtworkImageAction");
    QVERIFY(button);QVERIFY(action);QCOMPARE(button->defaultAction(),action);
    QCOMPARE(button->size(),QSize(28,28));QCOMPARE(button->iconSize(),QSize(16,16));
    QVERIFY(!button->toolTip().isEmpty());QVERIFY(!button->accessibleName().isEmpty());QVERIFY(!button->icon().isNull());
    QVERIFY(!button->isEnabled());QVERIFY(!chooseArtwork(window,button).opened);
    QVERIFY(prepareImageImport(window,directory));QVERIFY(button->isEnabled());
    auto *doc=window.canvas()->document();BrushSettings brush;brush.size=12;
    QVERIFY(doc->beginStroke({100,100},1,brush,false));
    // Drawing controls deliberately avoid flashing disabled while the pen is
    // down. The operation must also enforce its guard when activated directly.
    QVERIFY(!chooseArtwork(window,button).opened);QVERIFY(doc->strokeActive());
    QCOMPARE(doc->activeShot().layers.size(),1);doc->endStroke();doc->history()->setClean();
 }
 void importTransparentImageFromFooterCentersLayerAndPreparesTransform(){
    QTemporaryDir directory;QVERIFY(directory.isValid());MainWindow window(directory.filePath("workspace.ini"));
    QVERIFY(prepareImageImport(window,directory));auto *canvas=window.canvas();auto *doc=canvas->document();
    QImage image(20,10,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
    image.setPixel(2,3,qPremultiply(qRgba(180,80,220,128)));image.setPixel(18,8,qRgb(10,100,220));
    const auto path=directory.filePath("Garden detail.png");QVERIFY(image.save(path));
    QPainterPath selection;selection.addRect(QRectF(0,0,2,2));doc->setRasterSelection(selection);
    auto *button=window.findChild<QToolButton*>("importLayerImage");QVERIFY(button);
    const auto chosen=chooseArtwork(window,button,path);QVERIFY(chosen.opened);QVERIFY(chosen.accepted);
    QCOMPARE(doc->activeShot().layers.size(),2);QCOMPARE(doc->activeLayer(),1);QCOMPARE(active(*doc).name,QString("Garden detail"));
    QCOMPARE(active(*doc).kind,LayerKind::Raster);QCOMPARE(doc->history()->count(),1);QVERIFY(!doc->rasterSelection().active);
    QCOMPARE(imageLayerPixel(active(*doc),312,238),image.pixel(2,3));
    QCOMPARE(imageLayerPixel(active(*doc),328,243),image.pixel(18,8));QCOMPARE(imageLayerPixel(active(*doc),310,235),QRgb(0));
    QCOMPARE(canvas->tool(),Canvas::Tool::Transform);QVERIFY(doc->transformActive());QVERIFY(doc->transformMatrix().isIdentity());
    // An identity placement is safe to resolve before undoing the import.
    QVERIFY(canvas->resolvePendingTransform());doc->history()->undo();QCOMPARE(doc->activeShot().layers.size(),1);
    QCOMPARE(doc->history()->index(),0);QVERIFY(doc->activeShot().layers.first().tiles.isEmpty());
    doc->history()->redo();QCOMPARE(active(*doc).name,QString("Garden detail"));
    QCOMPARE(imageLayerPixel(active(*doc),312,238),image.pixel(2,3));
    const auto output=qEnvironmentVariable("TOON_IMAGE_IMPORT_PREVIEW");
    if(!output.isEmpty()){
        QDir().mkpath(output);canvas->fitSheet();QTest::qWait(40);
        QVERIFY(window.grab().save(QDir(output).filePath("image-layer-workspace.png")));
        auto *panel=window.findChild<LayerPanel*>();QVERIFY(panel);QVERIFY(panel->grab().save(QDir(output).filePath("image-layer-panel.png")));
    }
    doc->history()->setClean();
 }
 void importedOversizedImageFitsCanvasWithoutChangingAspectRatio(){
    QTemporaryDir directory;QVERIFY(directory.isValid());MainWindow window(directory.filePath("workspace.ini"));
    QVERIFY(prepareImageImport(window,directory));auto *doc=window.canvas()->document();
    QImage image(1280,320,QImage::Format_ARGB32_Premultiplied);image.fill(QColor(30,140,200,160));
    const auto path=directory.filePath("Wide background.png");QVERIFY(image.save(path));
    auto *button=window.findChild<QToolButton*>("importLayerImage");QVERIFY(button);const auto chosen=chooseArtwork(window,button,path);
    QVERIFY(chosen.opened);QVERIFY(chosen.accepted);QCOMPARE(doc->activeShot().layers.size(),2);
    QCOMPARE(doc->transformBounds(),QRectF(0,160,640,160));QCOMPARE(imageLayerPixel(active(*doc),0,160),image.pixel(0,0));
    QCOMPARE(imageLayerPixel(active(*doc),639,319),image.pixel(0,0));QCOMPARE(imageLayerPixel(active(*doc),0,159),QRgb(0));
    QCOMPARE(imageLayerPixel(active(*doc),640,200),QRgb(0));QCOMPARE(doc->history()->count(),1);
    doc->cancelTransform();doc->history()->setClean();
 }
 void cancelledAndInvalidImageImportsLeaveArtworkUntouched(){
    QTemporaryDir directory;QVERIFY(directory.isValid());MainWindow window(directory.filePath("workspace.ini"));
    QVERIFY(prepareImageImport(window,directory));auto *doc=window.canvas()->document();auto *button=window.findChild<QToolButton*>("importLayerImage");QVERIFY(button);
    const auto before=doc->project().toJson();const auto tool=window.canvas()->tool();
    const auto cancelled=chooseArtwork(window,button);QVERIFY(cancelled.opened);QVERIFY(!cancelled.accepted);
    QCOMPARE(doc->project().toJson(),before);QCOMPARE(doc->history()->count(),0);QCOMPARE(window.canvas()->tool(),tool);
    QFile file(directory.filePath("Broken.png"));QVERIFY(file.open(QIODevice::WriteOnly));file.write("This is not an image");file.close();
    const auto invalid=chooseArtwork(window,button,file.fileName());QVERIFY(invalid.opened);QVERIFY(invalid.accepted);
    QCOMPARE(doc->project().toJson(),before);QCOMPARE(doc->history()->count(),0);QCOMPARE(window.canvas()->tool(),tool);
    QVERIFY(!doc->transformActive());QVERIFY(!window.statusBar()->currentMessage().isEmpty());doc->history()->setClean();
 }
 void imageImportStopsPlaybackBeforeOpeningChooser(){
    QTemporaryDir directory;QVERIFY(directory.isValid());MainWindow window(directory.filePath("workspace.ini"));
    QVERIFY(prepareImageImport(window,directory));auto *timeline=window.findChild<TimelineController*>();QVERIFY(timeline);
    QVERIFY(timeline->setFrame(12));timeline->setPlaying(true);QVERIFY(timeline->playing());
    auto *button=window.findChild<QToolButton*>("importLayerImage");QVERIFY(button);bool stoppedInChooser=false;int frameInChooser=-1;
    const auto chosen=chooseArtwork(window,button,{},[&]{stoppedInChooser=!timeline->playing();frameInChooser=timeline->currentFrame();});
    QVERIFY(chosen.opened);QVERIFY(stoppedInChooser);QVERIFY(!timeline->playing());QCOMPARE(timeline->currentFrame(),frameInChooser);
    QCOMPARE(window.canvas()->document()->history()->count(),0);
 }
 void imageImportCommitsPendingLayerOpacityBeforeChooser(){
    QTemporaryDir directory;QVERIFY(directory.isValid());MainWindow window(directory.filePath("workspace.ini"));
    QVERIFY(prepareImageImport(window,directory));auto *doc=window.canvas()->document();QVERIFY(doc->beginLayerPropertyEdit());doc->previewLayerOpacity(.45);
    QImage image(8,8,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::red);const auto path=directory.filePath("Color patch.png");QVERIFY(image.save(path));
    auto *button=window.findChild<QToolButton*>("importLayerImage");QVERIFY(button);bool committedInChooser=false;
    const auto chosen=chooseArtwork(window,button,path,[&]{committedInChooser=!doc->layerPropertyEditActive()&&doc->history()->count()==1;});
    QVERIFY(chosen.opened);QVERIFY(chosen.accepted);QVERIFY(committedInChooser);QCOMPARE(doc->activeShot().layers.size(),2);
    QCOMPARE(doc->activeShot().layers.first().opacity,.45);QCOMPARE(doc->history()->count(),2);QVERIFY(doc->transformActive());
    doc->cancelTransform();doc->history()->undo();QCOMPARE(doc->activeShot().layers.size(),1);QCOMPARE(active(*doc).opacity,.45);
    doc->history()->undo();QCOMPARE(active(*doc).opacity,1.);doc->history()->setClean();
 }
};
QTEST_MAIN(LayerPanelTests)
#include "test_layer_panel.moc"
