#include "animation.h"
#include "advanced_transform.h"
#include "canvas.h"
#include "clipboard_tools.h"
#include "drawing_tool_controller.h"
#include "drawing_tool_panel.h"
#include "layer_panel.h"
#include "raster_tools.h"
#include "sheet.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QPainter>
#include <QSignalSpy>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QToolButton>
#include <QUuid>
#include <QtTest>
#include <cmath>

using namespace toon;
namespace {
QString id(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
QImage tile(QColor color) {QImage image(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);image.fill(color);return image;}
Project animatedProject(bool boundaries=false) {
    auto project=Project::create();project.width=project.height=64;project.transparent=true;
    auto &shot=project.shots[0];shot.durationFrames=8;shot.sheet.overscan=0;
    auto background=RasterLayer::create("Background");background.tiles[{}]=tile(boundaries?Qt::white:Qt::green);
    auto a=RasterLayer::create("A ink"),b=RasterLayer::create("B ink");
    auto groupA=RasterLayer::create("A"),groupB=RasterLayer::create("B"),trackRoot=RasterLayer::create("Character");
    groupA.group=groupB.group=trackRoot.group=true;
    a.parentId=groupA.id;b.parentId=groupB.id;groupA.parentId=groupB.parentId=trackRoot.id;
    a.tiles[{}]=tile(Qt::transparent);b.tiles[{}]=tile(Qt::transparent);
    {QPainter pa(&a.tiles[{}]),pb(&b.tiles[{}]);
        if(boundaries){pa.fillRect(32,0,1,64,Qt::black);pb.fillRect(16,0,1,64,Qt::black);}
        else{pa.fillRect(0,0,32,64,Qt::red);pb.fillRect(0,0,64,64,Qt::blue);}}
    AnimationDrawing drawingA{id(),groupA.id,{{"ink",a.id}}},drawingB{id(),groupB.id,{{"ink",b.id}}};
    AnimationTrack track;track.id=id();track.rootGroupLayerId=trackRoot.id;track.drawingTemplate={{"ink","Ink"}};
    track.drawings={drawingA,drawingB};track.exposures={{id(),drawingA.id,0,2},{id(),drawingB.id,3,1},{id(),drawingA.id,5,1}};
    shot.layers={background,a,groupA,b,groupB,trackRoot};shot.animationTracks={track};return project;
}
QTreeWidgetItem *row(QTreeWidget *tree,const QString &id) {
    for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString()==id)return *it;return nullptr;
}
QColor pixel(const RasterLayer &layer,QPoint point) {
    const TileKey key(int(std::floor(point.x()/qreal(tileSize))),int(std::floor(point.y()/qreal(tileSize))));
    const auto image=layer.tiles.value(key);return image.isNull()?QColor(Qt::transparent):image.pixelColor(point.x()-key.first*tileSize,point.y()-key.second*tileSize);
}
QColor storedPixel(const Document &doc,int index,QPoint point) {
    return pixel(doc.project().shots[0].layers[index],point);
}
void compareArtwork(const Project &actual,const Project &expected) {
    QCOMPARE(actual.toJson(),expected.toJson());
    QCOMPARE(actual.shots.size(),expected.shots.size());
    for(int s=0;s<actual.shots.size();++s){QCOMPARE(actual.shots[s].layers.size(),expected.shots[s].layers.size());
        for(int i=0;i<actual.shots[s].layers.size();++i)QCOMPARE(actual.shots[s].layers[i].tiles,expected.shots[s].layers[i].tiles);}
}
void addStoredDrawings(Project &project,int count) {
    auto &shot=project.shots[0];auto &track=shot.animationTracks[0];
    for(int i=0;i<count;++i){auto group=RasterLayer::create(QString("Stored %1").arg(i));group.group=true;group.parentId=track.rootGroupLayerId;
        auto ink=RasterLayer::create("Ink");ink.parentId=group.id;
        track.drawings.append({id(),group.id,{{"ink",ink.id}}});shot.layers.append(group);shot.layers.append(ink);}
}
}
class AnimationToolsTests final:public QObject {
    Q_OBJECT
private slots:
    void mergedSamplingAndExplicitReferencesUseOnlyCurrentExposure() {
        const auto project=animatedProject();QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        Document doc;doc.load(project);const auto &layers=project.shots[0].layers;
        QCOMPARE(RasterTools::sample(&doc,{10,10}),QColor(Qt::red));
        QCOMPARE(RasterTools::sample(&doc,{50,10}),QColor(Qt::green));
        QCOMPARE(RasterTools::sample(&doc,{10,10},RasterSampleSource::ReferenceLayer,layers[5].id),QColor(Qt::red));
        QVERIFY(!RasterTools::sample(&doc,{10,10},RasterSampleSource::ReferenceLayer,layers[3].id).isValid());
        QVERIFY(doc.setCurrentFrame(3));doc.selectLayer(3);
        QCOMPARE(doc.activeLayer(),3); // The evaluated layer list has different indices.
        QCOMPARE(RasterTools::sample(&doc,{10,10}),QColor(Qt::blue));
        QCOMPARE(RasterTools::sample(&doc,{10,10},RasterSampleSource::ActiveLayer),QColor(Qt::blue));
        QCOMPARE(RasterTools::sample(&doc,{10,10},RasterSampleSource::ReferenceLayer,layers[5].id),QColor(Qt::blue));
        QVERIFY(!RasterTools::sample(&doc,{10,10},RasterSampleSource::ReferenceLayer,layers[1].id).isValid());
        QVERIFY(doc.setCurrentFrame(2));QCOMPARE(RasterTools::sample(&doc,{10,10}),QColor(Qt::green));
        QCOMPARE(RasterTools::sample(&doc,{10,10},RasterSampleSource::ReferenceLayer,layers[5].id).alpha(),0);
        QVERIFY(doc.setCurrentFrame(5));QCOMPARE(RasterTools::sample(&doc,{10,10}),QColor(Qt::red));
        compareArtwork(doc.project(),project);QCOMPARE(doc.history()->count(),0);
    }
    void activeReferenceCanIgnoreEyeVisibilityWithoutReadingAnotherCel() {
        auto project=animatedProject();project.shots[0].layers[1].visible=false;Document doc;doc.load(project);
        QCOMPARE(RasterTools::sample(&doc,{10,10}),QColor(Qt::green));
        QCOMPARE(RasterTools::sample(&doc,{10,10},RasterSampleSource::ReferenceLayer,project.shots[0].layers[1].id),QColor(Qt::red));
        QVERIFY(doc.setCurrentFrame(3));
        QVERIFY(!RasterTools::sample(&doc,{10,10},RasterSampleSource::ReferenceLayer,project.shots[0].layers[1].id).isValid());
    }
    void bucketAndColorSelectionsDoNotIncludeInactiveBoundariesOrColors() {
        Document doc;auto project=animatedProject(true);doc.load(project);doc.selectLayer(0);
        RasterFillSettings settings;settings.tolerance=0;settings.source=RasterSampleSource::AllLayers;
        QVERIFY(RasterTools::bucket(&doc,{8,10},Qt::red,settings));
        QCOMPARE(storedPixel(doc,0,{24,10}),QColor(Qt::red));QCOMPARE(storedPixel(doc,0,{48,10}),QColor(Qt::white));
        QCOMPARE(doc.project().shots[0].layers[3].tiles,project.shots[0].layers[3].tiles);
        doc.history()->undo();QVERIFY(doc.setCurrentFrame(3));doc.selectLayer(0);
        QVERIFY(RasterTools::bucket(&doc,{8,10},Qt::blue,settings));
        QCOMPARE(storedPixel(doc,0,{8,10}),QColor(Qt::blue));QCOMPARE(storedPixel(doc,0,{24,10}),QColor(Qt::white));
        doc.load(animatedProject());
        QVERIFY(RasterTools::selectColor(&doc,{10,10},settings,false));
        QVERIFY(doc.rasterSelection().path.contains({10,10}));QVERIFY(!doc.rasterSelection().path.contains({50,10}));
        QVERIFY(doc.setCurrentFrame(3));QVERIFY(RasterTools::selectColor(&doc,{10,10},settings,true));
        QVERIFY(doc.rasterSelection().path.contains({10,10}));QVERIFY(doc.rasterSelection().path.contains({50,10}));
    }
    void layerRowsPreserveCanonicalIndicesAndOnlyThumbnailExposedDrawings() {
        Document doc;const auto project=animatedProject();doc.load(project);doc.selectLayer(1);
        LayerPanel panel(&doc);panel.resize(270,560);panel.show();
        auto *tree=panel.findChild<QTreeWidget*>("layerTree");QVERIFY(tree);
        const auto &layers=project.shots[0].layers;auto *a=row(tree,layers[1].id),*b=row(tree,layers[3].id),*track=row(tree,layers[5].id);
        QVERIFY(a&&b&&track);QVERIFY(!a->isHidden());QVERIFY(b->isHidden());
        constexpr int thumbnailRole=Qt::UserRole+9;
        QTRY_VERIFY(!a->data(0,thumbnailRole).value<QImage>().isNull());
        QVERIFY(b->data(0,thumbnailRole).value<QImage>().isNull());
        const auto thumbnail=a->data(0,thumbnailRole).value<QImage>().cacheKey();
        QSignalSpy rowChanges(tree->model(),&QAbstractItemModel::dataChanged);
        QVERIFY(doc.setCurrentFrame(1));QTest::qWait(45);QCOMPARE(a->data(0,thumbnailRole).value<QImage>().cacheKey(),thumbnail);QCOMPARE(rowChanges.count(),0);
        QVERIFY(doc.setCurrentFrame(3));QCOMPARE(doc.activeLayer(),3);
        QCOMPARE(row(tree,layers[1].id),a);QCOMPARE(row(tree,layers[3].id),b);
        QVERIFY(a->isHidden());QVERIFY(!b->isHidden());QCOMPARE(tree->currentItem(),b);
        QTRY_VERIFY(!b->data(0,thumbnailRole).value<QImage>().isNull());
        QVERIFY(!panel.selectedLayerIds().contains(layers[1].id));
        tree->setCurrentItem(a,1);QCOMPARE(doc.activeLayer(),3);QCOMPARE(tree->currentItem(),b);
        QVERIFY(doc.setCurrentFrame(2));QVERIFY(a->isHidden());QVERIFY(b->isHidden());QVERIFY(!track->isHidden());
        QCOMPARE(doc.project().shots[0].layers[doc.activeLayer()].id,layers[5].id);
        QVERIFY(doc.setCurrentFrame(5));QCOMPARE(doc.activeLayer(),1);QVERIFY(!a->isHidden());
        QCOMPARE(doc.history()->count(),0);
    }
    void emptyTrackKeepsItsVisibleContainerAndNoDrawingSelection() {
        auto project=animatedProject();auto &shot=project.shots[0];
        const auto background=shot.layers[0],track=shot.layers[5];shot.layers={background,track};
        shot.animationTracks[0].drawings.clear();shot.animationTracks[0].exposures.clear();
        QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        Document doc;doc.load(project);QVERIFY(doc.selectAnimationTrack(shot.animationTracks[0].id));LayerPanel panel(&doc);
        auto *tree=panel.findChild<QTreeWidget*>("layerTree");QVERIFY(tree);
        auto *root=row(tree,track.id);QVERIFY(root);QVERIFY(!root->isHidden());QCOMPARE(root->childCount(),0);
        QCOMPARE(doc.activeLayer(),1);QCOMPARE(panel.selectedLayerIds(),QStringList{track.id});
        QVERIFY(doc.setCurrentFrame(6));QCOMPARE(doc.activeLayer(),1);QVERIFY(!root->isHidden());
    }
    void referencePickerAndEyedropperFollowFrameWithoutStaleIds() {
        const auto project=animatedProject();Canvas canvas;canvas.resize(800,600);canvas.setProject(project);
        DrawingToolPanel panel(&canvas);auto *reference=panel.findChild<QComboBox*>("fillReferenceLayer");QVERIFY(reference);
        const auto &layers=project.shots[0].layers;QVERIFY(reference->findData(layers[1].id)>=0);QCOMPARE(reference->findData(layers[3].id),-1);
        reference->setCurrentIndex(reference->findData(layers[1].id));
        QVERIFY(canvas.document()->setCurrentFrame(3));QCOMPARE(reference->findData(layers[1].id),-1);QVERIFY(reference->findData(layers[3].id)>=0);
        QVERIFY(canvas.drawingTools()->fillSettings.referenceLayerId!=layers[1].id);
        canvas.setTool(Canvas::Tool::Eyedropper);canvas.drawingTools()->fillSettings.source=RasterSampleSource::AllLayers;
        const auto point=canvas.viewTransform().map(sheetGeometry(project,project.shots[0]).output.topLeft()+QPointF(10,10));
        QVERIFY(canvas.drawingTools()->press(point,Qt::NoModifier,1));QCOMPARE(canvas.brush.color,QColor(Qt::blue));
        QVERIFY(canvas.document()->setCurrentFrame(2));QVERIFY(reference->findData(layers[0].id)>0);
        QCOMPARE(reference->itemText(0),QString("Marked reference layers"));
        QCOMPARE(reference->currentIndex(),-1);QVERIFY(canvas.drawingTools()->fillSettings.referenceLayerId.isEmpty());
    }
    void clipboardCopiesTheRemappedCurrentDrawingOnly() {
        Document doc;const auto project=animatedProject();doc.load(project);doc.selectLayer(1);QString error;
        QVERIFY2(ClipboardTools::copy(&doc,false,&error),qPrintable(error));
        QCOMPARE(QApplication::clipboard()->image().pixelColor(0,0),QColor(Qt::red));
        QVERIFY(doc.setCurrentFrame(3));QCOMPARE(doc.activeLayer(),3);
        QVERIFY2(ClipboardTools::copy(&doc,false,&error),qPrintable(error));
        QCOMPARE(QApplication::clipboard()->image().pixelColor(0,0),QColor(Qt::blue));
        QVERIFY(doc.setCurrentFrame(2));QVERIFY(!ClipboardTools::copy(&doc,false,&error));
        compareArtwork(doc.project(),project);QCOMPARE(doc.history()->count(),0);
    }
    void importUsesDrawingCapacityAndEmptyTrackIsProtected() {
        auto project=animatedProject();addStoredDrawings(project,35);QVERIFY(project.shots[0].layers.size()>maxDrawingLayers);
        QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));Document doc;doc.load(project);doc.selectLayer(1);
        LayerPanel panel(&doc);auto *add=panel.findChild<QToolButton*>("addLayer");auto *vector=panel.findChild<QToolButton*>("addVectorLayer");
        QVERIFY(add&&add->isEnabled());QVERIFY(vector&&vector->isEnabled());
        QImage image(4,4,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::yellow);QString error;
        QVERIFY2(ClipboardTools::importImage(&doc,image,"Imported",{40,4},&error),qPrintable(error));
        QCOMPARE(doc.history()->count(),1);const auto &shot=doc.project().shots[0];
        const auto &created=shot.layers[doc.activeLayer()];QCOMPARE(created.parentId,project.shots[0].layers[2].id);
        QCOMPARE(pixel(created,{41,5}),QColor(Qt::yellow));
        for(const auto &layer:shot.layers)if(layer.id==project.shots[0].layers[3].id)QCOMPARE(layer.tiles,project.shots[0].layers[3].tiles);
        doc.history()->undo();compareArtwork(doc.project(),project);
        QVERIFY(doc.setCurrentFrame(2));QVERIFY(!add->isEnabled());
        QVERIFY(!ClipboardTools::importImage(&doc,image,"Rejected",{0,0},&error));QVERIFY(error.contains("Create a drawing"));
        compareArtwork(doc.project(),project);
    }
    void fullDrawingRejectsImportWithoutChangingClipboardOrPixels() {
        auto project=animatedProject();auto &shot=project.shots[0];
        for(int i=0;i<maxDrawingLayers-2;++i){auto layer=RasterLayer::create(QString("Extra %1").arg(i));layer.parentId=shot.layers[2].id;shot.layers.append(layer);}
        QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));Document doc;doc.load(project);doc.selectLayer(1);
        LayerPanel panel(&doc);QVERIFY(!panel.findChild<QToolButton*>("addLayer")->isEnabled());
        QImage image(4,4,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::yellow);QApplication::clipboard()->setImage(image);QString error;
        QVERIFY(!ClipboardTools::importImage(&doc,image,"Overflow",{0,0},&error));QVERIFY(error.contains("64"));
        compareArtwork(doc.project(),project);QCOMPARE(QApplication::clipboard()->image(),image);QCOMPARE(doc.history()->count(),0);
    }
    void animationGroupTransformChangesOnlyExposedDrawingAndCommitsOnce() {
        Document doc;const auto project=animatedProject();doc.load(project);doc.selectLayer(1);
        AdvancedTransform transform;QVERIFY(transform.begin(&doc,{project.shots[0].layers[5].id}));
        QVERIFY(transform.setMatrix(QTransform::fromTranslate(8,0)));QVERIFY(!doc.setCurrentFrame(3));QCOMPARE(doc.currentFrame(),0);
        QVERIFY(transform.apply());QVERIFY(!transform.active());QCOMPARE(doc.history()->count(),1);
        QCOMPARE(storedPixel(doc,1,{2,10}).alpha(),0);QCOMPARE(storedPixel(doc,1,{36,10}),QColor(Qt::red));
        QCOMPARE(doc.project().shots[0].layers[0].tiles,project.shots[0].layers[0].tiles);
        QCOMPARE(doc.project().shots[0].layers[3].tiles,project.shots[0].layers[3].tiles);
        doc.history()->undo();compareArtwork(doc.project(),project);
        QVERIFY(doc.setCurrentFrame(3));QVERIFY(transform.begin(&doc,{project.shots[0].layers[5].id}));
        QVERIFY(transform.setMatrix(QTransform::fromTranslate(0,8)));transform.cancel();compareArtwork(doc.project(),project);
        QVERIFY(doc.setCurrentFrame(2));QVERIFY(!transform.begin(&doc,{project.shots[0].layers[5].id}));
    }
};
QTEST_MAIN(AnimationToolsTests)
#include "test_animation_tools.moc"
