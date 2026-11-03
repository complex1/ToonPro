#include "clipboard_tools.h"
#include "raster_tools.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QtTest>
#include <cmath>

using namespace toon;
namespace {
int coordinate(int n){return int(std::floor(n/qreal(tileSize)));}
void pixel(RasterLayer &layer,int x,int y,QRgb color){const TileKey key(coordinate(x),coordinate(y));if(!layer.tiles.contains(key)){QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);layer.tiles.insert(key,tile);}layer.tiles[key].setPixel(x-key.first*tileSize,y-key.second*tileSize,color);}
QRgb pixel(const RasterLayer &layer,int x,int y){const TileKey key(coordinate(x),coordinate(y));const auto tile=layer.tiles.constFind(key);return tile==layer.tiles.cend()?0:tile->pixel(x-key.first*tileSize,y-key.second*tileSize);}
Project project(){auto p=Project::create();p.width=320;p.height=240;p.shots.first().layers={RasterLayer::create("Ink")};return p;}
QPainterPath rectangle(QRectF r){QPainterPath path;path.addRect(r);return path;}
}
class ClipboardToolsTest:public QObject {
    Q_OBJECT
private slots:
    void copyPastePreservesNegativeOriginTransparencyAndUndo() {
        auto p=project();auto &layer=p.shots.first().layers[0];pixel(layer,-8,4,qRgb(230,10,40));pixel(layer,-4,7,qPremultiply(qRgba(30,120,230,100)));
        Document doc;doc.load(p);QString error;QVERIFY2(ClipboardTools::copy(&doc,false,&error),qPrintable(error));QCOMPARE(doc.history()->count(),0);
        QVERIFY(ClipboardTools::paste(&doc,true,{},&error));QCOMPARE(doc.project().shots.first().layers.size(),2);QCOMPARE(doc.history()->count(),1);
        const auto &copy=doc.project().shots.first().layers[doc.activeLayer()];QCOMPARE(pixel(copy,-8,4),qRgb(230,10,40));QCOMPARE(pixel(copy,-4,7),qPremultiply(qRgba(30,120,230,100)));QCOMPARE(pixel(copy,-6,6),QRgb(0));
        doc.history()->undo();QCOMPARE(doc.project().shots.first().layers.size(),1);
    }
    void cutOnlySelectionAndOneUndo() {
        auto p=project();auto &layer=p.shots.first().layers[0];pixel(layer,20,30,qRgb(200,20,10));pixel(layer,70,30,qRgb(10,20,200));Document doc;doc.load(p);
        doc.setRasterSelection(rectangle({10,20,30,30}));QVERIFY(ClipboardTools::copy(&doc,true));QCOMPARE(doc.history()->count(),1);
        QCOMPARE(pixel(doc.project().shots.first().layers[0],20,30),QRgb(0));QCOMPARE(pixel(doc.project().shots.first().layers[0],70,30),qRgb(10,20,200));
        doc.history()->undo();QCOMPARE(doc.project().shots.first().layers[0].tiles,p.shots.first().layers[0].tiles);
    }
    void duplicateDoesNotReplaceSystemClipboard() {
        QImage original(2,2,QImage::Format_ARGB32_Premultiplied);original.fill(Qt::green);QGuiApplication::clipboard()->setImage(original);
        auto p=project();pixel(p.shots.first().layers[0],40,40,qRgb(20,50,200));Document doc;doc.load(p);QVERIFY(ClipboardTools::duplicate(&doc));
        QCOMPARE(QGuiApplication::clipboard()->image(),original);QCOMPARE(pixel(doc.project().shots.first().layers[1],40,40),qRgb(20,50,200));
    }
    void externalImageCenteredOrPlacedAndActivePasteRespectsSelection() {
        Document doc;doc.load(project());QImage image(20,10,QImage::Format_ARGB32_Premultiplied);image.fill(QColor(200,50,80));QGuiApplication::clipboard()->setImage(image);
        QVERIFY(ClipboardTools::paste(&doc));QCOMPARE(pixel(doc.project().shots.first().layers[1],150,115),qRgb(200,50,80));
        doc.selectLayer(0);doc.setRasterSelection(rectangle({25,30,5,10}));QVERIFY(ClipboardTools::paste(&doc,false,QPointF(20,30)));
        const auto &layer=doc.project().shots.first().layers[0];QCOMPARE(pixel(layer,26,32),qRgb(200,50,80));QCOMPARE(pixel(layer,22,32),QRgb(0));
        doc.clearRasterSelection();QVERIFY(ClipboardTools::importImage(&doc,image,"Background",{30,50}));QCOMPARE(doc.project().shots.first().layers[doc.activeLayer()].name,QString("Background"));
    }
    void lockedCutAndOutOfBoundsImportAreAtomic() {
        auto p=project();pixel(p.shots.first().layers[0],20,20,qRgb(80,30,10));p.shots.first().layers[0].locked=true;Document doc;doc.load(p);QString error;
        QVERIFY(!ClipboardTools::copy(&doc,true,&error));QVERIFY(!error.isEmpty());QVERIFY(ClipboardTools::copy(&doc,false));
        QImage image(20,20,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::red);
        QVERIFY(!ClipboardTools::importImage(&doc,image,"Too far",{5000,0},&error));QCOMPARE(doc.history()->count(),0);QCOMPARE(doc.project().shots.first().layers[0].tiles,p.shots.first().layers[0].tiles);
    }
    void featheredCopyAndCutUseCoverage() {
        auto p=project();auto &layer=p.shots.first().layers[0];for(int y=20;y<80;++y)for(int x=20;x<80;++x)pixel(layer,x,y,qRgb(220,40,30));
        Document doc;doc.load(p);doc.setRasterSelection(rectangle({35,35,30,30}));
        QVERIFY(RasterTools::featherSelection(&doc,5));
        QVERIFY(ClipboardTools::copy(&doc,true));const auto image=QGuiApplication::clipboard()->image();
        QVERIFY(image.width()>30);QVERIFY(image.height()>30);
        const auto alpha=qAlpha(pixel(doc.project().shots.first().layers[0],35,45));
        QVERIFY(alpha>0 && alpha<255);QCOMPARE(qAlpha(pixel(doc.project().shots.first().layers[0],50,50)),0);
        doc.history()->undo();QCOMPARE(doc.project().shots.first().layers[0].tiles,p.shots.first().layers[0].tiles);
    }
};
QTEST_MAIN(ClipboardToolsTest)
#include "test_clipboard_tools.moc"
