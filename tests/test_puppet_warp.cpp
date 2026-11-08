#include "advanced_transform.h"
#include "puppet_warp_fixture.h"
#include <QtTest>
#include <limits>
#include <QFile>
#include <QJsonArray>

using namespace toon;
namespace {
qreal area(QPointF a,QPointF b,QPointF c){const auto u=b-a,v=c-a;return (u.x()*v.y()-u.y()*v.x())/2;}
QImage render(const Project &project,const RasterLayerPreview *preview=nullptr){
    QImage image(project.width,project.height,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter p(&image);paintLayers(p,project.shots[0].layers,true,preview);return image;
}
bool close(QPointF a,QPointF b){return QLineF(a,b).length()<1e-6;}
}
class PuppetWarpTest:public QObject{
    Q_OBJECT
private slots:
    void rigidDeformationInterpolatesPinsAndReproducesRotation(){
        const QPointF sample(43,64);QCOMPARE(deformPuppetPoint(sample,{}),sample);
        const QVector<PuppetPin> translated{{{10,10},{35,27}}};QCOMPARE(deformPuppetPoint(sample,translated),sample+QPointF(25,17));
        const QVector<QPointF> source{{0,0},{100,0},{80,90},{0,110}};QTransform rotation;rotation.translate(80,20);rotation.rotate(32);
        QVector<PuppetPin> pins;for(const auto &point:source)pins.append({point,rotation.map(point)});
        QVERIFY(close(deformPuppetPoint(sample,pins),rotation.map(sample)));
        for(const auto &pin:pins)QCOMPARE(deformPuppetPoint(pin.source,pins),pin.target);
    }
    void densityAndInsertedPinsPreserveCoverage(){
        const QRectF bounds(10,20,200,120);const QVector<PuppetPin> pins{{{110,80},{110,80}},{{37.25,69.5},{37.25,69.5}},{{10,20},{10,20}}};
        int previous=0;
        for(int density:{4,16,32}){
            PuppetMesh mesh;QString error;QVERIFY2(buildPuppetMesh(bounds,density,pins,&mesh,&error),qPrintable(error));QCOMPARE(mesh.source,mesh.target);QVERIFY(mesh.source.size()>previous);previous=mesh.source.size();
            double total=0;for(const auto &triangle:mesh.triangles){const auto value=area(mesh.source[triangle[0]],mesh.source[triangle[1]],mesh.source[triangle[2]]);QVERIFY(value>0);total+=value;}
            QVERIFY(std::abs(total-bounds.width()*bounds.height())<1e-5);
            for(int i=0;i<pins.size();++i){QCOMPARE(mesh.source[mesh.pinVertices[i]],pins[i].source);QPointF source;QVERIFY(inversePuppetPoint(mesh,pins[i].target,&source));QVERIFY(close(source,pins[i].source));}
        }
    }
    void identityPinsRetainEditableMeshWithoutResampling(){
        Document doc;doc.load(puppetWarpFixture());const auto before=doc.project().toJson();const auto original=render(doc.project());AdvancedTransform transform;
        QVERIFY(transform.begin(&doc));QVERIFY(transform.beginPuppet());QVERIFY(!transform.modified());QCOMPARE(render(doc.project(),transform.preview()),original);
        QVERIFY(transform.apply());QVERIFY(doc.activeShot().layers[0].puppetWarp);doc.history()->undo();QCOMPARE(doc.project().toJson(),before);QVERIFY(transform.begin(&doc));QVERIFY(transform.beginPuppet());
        for(const auto &point:{QPointF(103.2,93.7),QPointF(380.4,100.1),QPointF(240.2,260.3)})QVERIFY(transform.addPuppetPin(point)>=0);
        QVERIFY(transform.modified());QCOMPARE(render(doc.project(),transform.preview()),original);QVERIFY(transform.apply());QCOMPARE(doc.history()->count(),1);QVERIFY(doc.activeShot().layers[0].puppetWarp);QCOMPARE(render(doc.project()),original);doc.history()->undo();QCOMPARE(doc.project().toJson(),before);
    }
    void translationHasNoTransparentTriangleSeams(){
        Document doc;doc.load(puppetWarpFixture());const auto original=render(doc.project());AdvancedTransform transform;QVERIFY(transform.begin(&doc));QVERIFY(transform.beginPuppet(32));QCOMPARE(transform.addPuppetPin({240,180}),0);
        QVERIFY(transform.setPuppetTargets({{257,191}}));const auto preview=render(doc.project(),transform.preview());QVERIFY(transform.apply());const auto output=render(doc.project());
        for(int y=0;y<original.height()-11;++y)for(int x=0;x<original.width()-17;++x){QCOMPARE(output.pixel(x+17,y+11),original.pixel(x,y));QCOMPARE(preview.pixel(x+17,y+11),original.pixel(x,y));}
        QCOMPARE(doc.history()->count(),1);doc.history()->undo();QCOMPARE(render(doc.project()),original);doc.history()->redo();QCOMPARE(render(doc.project()),output);
    }
    void movingOnePinHoldsTheOthersAndSurvivesSave(){
        Document doc;doc.load(puppetWarpFixture());const auto original=doc.project().shots[0].layers[0].tiles;AdvancedTransform transform;QVERIFY(transform.begin(&doc));QVERIFY(transform.beginPuppet());
        const QVector<QPointF> points{{100,95},{380,95},{240,178}};for(const auto &point:points)QVERIFY(transform.addPuppetPin(point)>=0);
        auto targets=points;targets[2]+=QPointF(22,24);QVERIFY(transform.setPuppetTargets(targets));QVERIFY(transform.modified());
        for(int i=0;i<points.size();++i)QCOMPARE(transform.puppetMesh().target[transform.puppetMesh().pinVertices[i]],targets[i]);
        const auto pins=transform.pins();QVERIFY(transform.setPuppetDensity(28));QCOMPARE(transform.pins(),pins);
        QCOMPARE(doc.project().shots[0].layers[0].tiles,original);QCOMPARE(doc.history()->count(),0);QVERIFY(transform.apply());QCOMPARE(doc.history()->count(),1);
        const auto result=render(doc.project());QVERIFY(result.pixelColor(262,202).red()>150);
        QTemporaryDir directory;QString error;const auto file=directory.filePath("puppet.toonproj");QVERIFY2(saveProject(doc.project(),file,&error),qPrintable(error));
        const auto loaded=loadProject(file,&error);QVERIFY2(loaded.has_value(),qPrintable(error));QCOMPARE(render(*loaded),result);
        doc.history()->undo();QCOMPARE(doc.project().shots[0].layers[0].tiles,original);
    }
    void addingAfterDeformationAndDeletingPinsIsReversible(){
        Document doc;doc.load(puppetWarpFixture());const auto before=doc.project().toJson();AdvancedTransform transform;QVERIFY(transform.begin(&doc));QVERIFY(transform.beginPuppet());
        QCOMPARE(transform.addPuppetPin({130,110}),0);QVERIFY(transform.setPuppetTargets({{150,125}}));QCOMPARE(transform.addPuppetPin({330,235}),1);
        QVERIFY(close(transform.pins()[1].source,{310,220}));QCOMPARE(transform.pins()[1].target,QPointF(330,235));QVERIFY(transform.removePuppetPin(1));QCOMPARE(transform.pins().size(),1);
        QVERIFY(transform.resetPuppet());QVERIFY(puppetMeshIdentity(transform.pins()));transform.cancel();QCOMPARE(doc.project().toJson(),before);QCOMPARE(doc.history()->count(),0);
    }
    void invalidRequestsAreAtomicAndBounded(){
        Document doc;doc.load(puppetWarpFixture());AdvancedTransform transform;QVERIFY(transform.begin(&doc));QVERIFY(transform.beginPuppet());
        for(const auto &p:{QPointF(100,100),QPointF(380,100),QPointF(240,260)})QVERIFY(transform.addPuppetPin(p)>=0);
        const auto pins=transform.pins();const auto mesh=transform.puppetMesh().target;
        QVERIFY(!transform.setPuppetDensity(33));QVERIFY(!transform.setPuppetTargets({{1,1}}));
        QVERIFY(!transform.setPuppetTargets({{100,100},{100,100},{100,100}}));QVERIFY(!transform.setPuppetTargets({{100,100},{380,100},{std::numeric_limits<double>::quiet_NaN(),0}}));
        QCOMPARE(transform.addPuppetPin({1000,1000}),-1);QCOMPARE(transform.pins(),pins);QCOMPARE(transform.puppetMesh().target,mesh);QCOMPARE(doc.history()->count(),0);
    }
    void rasterSelectionMovesWithoutTouchingOtherPixels(){
        auto project=puppetWarpFixture();Document doc;doc.load(project);QPainterPath selection;selection.addRect(180,115,120,125);doc.setRasterSelection(selection);
        const auto original=render(doc.project());AdvancedTransform transform;QVERIFY(transform.begin(&doc));QVERIFY(transform.beginPuppet(8));QCOMPARE(transform.addPuppetPin({240,178}),0);QVERIFY(transform.setPuppetTargets({{260,193}}));QVERIFY(transform.apply());
        const auto output=render(doc.project());QCOMPARE(output.pixel(100,100),original.pixel(100,100));QCOMPARE(doc.rasterSelection().path.boundingRect(),QRectF(200,130,120,125));doc.history()->undo();QCOMPARE(render(doc.project()),original);QCOMPARE(doc.rasterSelection().path,selection);
    }
    void pinCountLocksAndUnsupportedSourcesAreGuarded(){
        auto project=puppetWarpFixture();Document doc;doc.load(project);AdvancedTransform transform;QVERIFY(transform.begin(&doc));QVERIFY(transform.beginPuppet(4));
        for(int y=0;y<8;++y)for(int x=0;x<8;++x)QVERIFY(transform.addPuppetPin({80.+x*44,70.+y*30})>=0);
        QCOMPARE(transform.pins().size(),64);QCOMPARE(transform.addPuppetPin({250,290}),-1);QCOMPARE(transform.pins().size(),64);QVERIFY(puppetMeshIdentity(transform.pins()));transform.cancel();
        project.shots[0].layers[0].locked=true;doc.load(project);QVERIFY(!transform.begin(&doc));QVERIFY(!doc.editingGestureActive());
        project.shots[0].layers[0].locked=false;project.shots[0].layers[0].kind=LayerKind::Vector;doc.load(project);QVERIFY(transform.begin(&doc));QVERIFY(!transform.beginPuppet());transform.cancel();
        project=puppetWarpFixture();doc.load(project);QPainterPath detailed;for(int x=0;x<120;++x)detailed.addRect(70+x*2,60,1,180);doc.setRasterSelection(detailed);QVERIFY(transform.begin(&doc));QVERIFY(!transform.beginPuppet());transform.cancel();QCOMPARE(doc.history()->count(),0);
    }
    void folderSharesMeshAndKeepsLayerProperties(){
        auto project=puppetWarpFixture();auto original=project.shots[0].layers[0];auto folder=RasterLayer::create("Fabric group");folder.group=true;original.parentId=folder.id;
        auto shade=original;shade.id=RasterLayer::create("id").id;shade.name="Shade";shade.opacity=.3;shade.blendMode=LayerBlendMode::Multiply;shade.clipped=true;
        project.shots[0].layers={folder,original,shade};Document doc;doc.load(project);AdvancedTransform transform;QVERIFY(transform.begin(&doc,{folder.id}));QVERIFY(transform.beginPuppet(8));QCOMPARE(transform.addPuppetPin({240,178}),0);QVERIFY(transform.setPuppetTargets({{250,188}}));QVERIFY(transform.apply());
        const auto &layers=doc.activeShot().layers;QCOMPARE(layers.size(),3);QCOMPARE(layers[1].parentId,folder.id);QCOMPARE(layers[2].parentId,folder.id);QCOMPARE(layers[2].opacity,.3);QVERIFY(layers[2].clipped);QCOMPARE(layers[2].blendMode,LayerBlendMode::Multiply);QCOMPARE(doc.history()->count(),1);doc.history()->undo();QCOMPARE(doc.activeShot().layers[1].tiles,original.tiles);QCOMPARE(doc.activeShot().layers[2].tiles,shade.tiles);
    }
    void retainedWarpReopensAfterSaveAndAlwaysUsesOriginalPixels(){
        const auto source=puppetWarpFixture();Document doc;doc.load(source);AdvancedTransform transform;QVERIFY(transform.begin(&doc));QVERIFY(transform.beginPuppet(20));
        const QVector<QPointF> anchors{{100,95},{380,95},{240,178}};for(const auto &point:anchors)QVERIFY(transform.addPuppetPin(point)>=0);auto targets=anchors;targets[2]+={20,15};QVERIFY(transform.setPuppetTargets(targets));QVERIFY(transform.apply());
        QTemporaryDir directory;const auto path=directory.filePath("retained.toonproj");QString error;QVERIFY2(saveProject(doc.project(),path,&error),qPrintable(error));QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.read(10),QByteArray("TOONPRO40\n"));file.close();
        const auto loaded=loadProject(path,&error);QVERIFY2(loaded.has_value(),qPrintable(error));QCOMPARE(loaded->shots[0].layers[0].puppetWarp->sourceTiles,source.shots[0].layers[0].tiles);
        doc.load(*loaded);const auto initial=render(doc.project());QPainterPath detailed;for(int i=0;i<110;++i)detailed.addRect(i*3,0,1,80);doc.setRasterSelection(detailed);QVERIFY(transform.begin(&doc));QVERIFY(transform.beginPuppet(4));QCOMPARE(transform.puppetDensity(),20);QCOMPARE(transform.pins().size(),3);QVERIFY(!transform.modified());QVERIFY(transform.apply());QCOMPARE(doc.history()->count(),0);QCOMPARE(render(doc.project()),initial);QCOMPARE(doc.rasterSelection().path,detailed);
        QVERIFY(transform.begin(&doc));QVERIFY(transform.beginPuppet());targets[2]+={8,10};QVERIFY(transform.setPuppetTargets(targets));transform.cancel();QCOMPARE(render(doc.project()),initial);QCOMPARE(doc.history()->count(),0);
        QVERIFY(transform.begin(&doc));QVERIFY(transform.beginPuppet());QVERIFY(transform.setPuppetTargets(targets));QVERIFY(transform.apply());const auto revised=render(doc.project());
        Document direct;direct.load(source);AdvancedTransform fresh;QVERIFY(fresh.begin(&direct));QVERIFY(fresh.beginPuppet(20));for(const auto &point:anchors)QVERIFY(fresh.addPuppetPin(point)>=0);QVERIFY(fresh.setPuppetTargets(targets));QVERIFY(fresh.apply());QCOMPARE(revised,render(direct.project()));
        doc.history()->undo();QCOMPARE(render(doc.project()),initial);doc.history()->redo();QCOMPARE(render(doc.project()),revised);
    }
    void retainedSelectionResetAndBakePreservePixelsAndUndo(){
        const auto source=puppetWarpFixture();Document doc;doc.load(source);QPainterPath path;path.addEllipse(QRectF(175,110,130,130));doc.setRasterSelection(path);AdvancedTransform transform;QVERIFY(transform.begin(&doc));QVERIFY(transform.beginPuppet(8));QVERIFY(transform.addPuppetPin({240,178})>=0);QVERIFY(transform.setPuppetTargets({{255,190}}));QVERIFY(transform.apply());
        const auto output=render(doc.project());QVERIFY(!layerCanPaint(doc.activeShot().layers,0));QVERIFY(!doc.beginStroke({100,100},1,BrushSettings{},false));
        QTemporaryDir directory;QString error;const auto file=directory.filePath("selection.toonproj");QVERIFY(saveProject(doc.project(),file,&error));const auto saved=loadProject(file,&error);QVERIFY2(saved.has_value(),qPrintable(error));QCOMPARE(saved->shots[0].layers[0].puppetWarp->selectionPath,path);doc.load(*saved);
        doc.clearRasterSelection();QVERIFY(transform.begin(&doc));QVERIFY(transform.beginPuppet());QVERIFY(transform.resetPuppet());QCOMPARE(render(doc.project(),transform.preview()),render(source));QVERIFY(transform.apply());QCOMPARE(render(doc.project()),render(source));QVERIFY(!doc.rasterSelection().active);
        QVERIFY(doc.bakePuppetWarp());QVERIFY(!doc.activeShot().layers[0].puppetWarp);QVERIFY(layerCanPaint(doc.activeShot().layers,0));doc.history()->undo();QVERIFY(doc.activeShot().layers[0].puppetWarp);doc.history()->undo();QCOMPARE(render(doc.project()),output);
    }
    void retainedStorageRejectsCorruptionAndUnsupportedMetadata(){
        auto film=puppetWarpFixture();film.kind=ProjectKind::Film;Document doc;doc.load(film);AdvancedTransform transform;QVERIFY(transform.begin(&doc));QVERIFY(transform.beginPuppet());QVERIFY(transform.addPuppetPin({240,178})>=0);QVERIFY(transform.setPuppetTargets({{250,188}}));QVERIFY(transform.apply());
        auto metadata=doc.project().toJson();QString error;auto pending=Project::fromJson(metadata,&error);QVERIFY(pending);QVERIFY(pending->validate().contains("source pixels"));
        auto old=metadata;old["version"]=39;QVERIFY(!Project::fromJson(old,&error));
        auto shots=metadata["shots"].toArray();auto shot=shots[0].toObject();auto layers=shot["layers"].toArray();auto layer=layers[0].toObject();auto state=layer["puppetWarp"].toObject();state["density"]=33;layer["puppetWarp"]=state;layers[0]=layer;shot["layers"]=layers;shots[0]=shot;metadata["shots"]=shots;QVERIFY(!Project::fromJson(metadata,&error));
        QTemporaryDir directory;const auto path=directory.filePath("bad.toonproj");QVERIFY(saveProject(doc.project(),path,&error));QFile file(path);QVERIFY(file.open(QIODevice::ReadWrite));QVERIFY(file.seek(file.size()-1));char byte;QCOMPARE(file.read(&byte,1),qint64(1));byte^=0x10;QVERIFY(file.seek(file.size()-1));QCOMPARE(file.write(&byte,1),qint64(1));file.close();QVERIFY(!loadProject(path,&error));QVERIFY(error.contains("source/checksum"));
    }
    void retainedSourcesShareTheProjectTileBudget(){
        Document doc;doc.load(puppetWarpFixture());AdvancedTransform transform;QVERIFY(transform.begin(&doc));QVERIFY(transform.beginPuppet());QVERIFY(transform.addPuppetPin({240,178})>=0);QVERIFY(transform.apply());
        auto project=doc.project();auto &state=*project.shots[0].layers[0].puppetWarp;const auto tile=state.sourceTiles.first();state.sourceTiles.clear();
        for(int i=0;i<1024;++i)state.sourceTiles.insert({i%32,i/32},tile);
        QVERIFY(project.validate().contains("1024-tile"));QString error;QVERIFY(!Project::fromJson(project.toJson(),&error));QVERIFY(error.contains("1024-tile"));
    }
};
QTEST_MAIN(PuppetWarpTest)
#include "test_puppet_warp.moc"
