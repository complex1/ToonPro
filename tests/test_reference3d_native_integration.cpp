#include "main_window.h"
#include "canvas.h"
#include "document.h"
#include "managed_project_store.h"
#include "reference3d_builtin.h"
#include "reference3d_catalog.h"
#include "reference3d_canvas.h"
#include "reference3d_layer_panel.h"
#include "reference3d_render.h"
#include "reference3d_review.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QSlider>
#include <algorithm>
#include <QAction>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTimer>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

using namespace toon;
namespace {
QPoint pointFor(Canvas *canvas,const ReferenceScene &scene,const QVector3D &world){
    const QSize frame(canvas->project().width,canvas->project().height);
    const auto clip=referenceViewProjection(scene.camera,frame)*QVector4D(world,1);const auto ndc=clip.toVector3DAffine();
    return canvas->artworkViewTransform().map(QPointF((ndc.x()+1)*.5*frame.width(),(1-ndc.y())*.5*frame.height())).toPoint();
}
ReferenceScene body(){ReferenceScene scene;scene.name="Pose study";ReferenceInstance model;model.id="body";model.name="Body";model.asset=createReferenceStarter(ReferenceStarterKind::Figure);scene.instances={model};scene.camera.yaw=0;scene.camera.pitch=0;scene.camera.orthographic=true;scene.camera.orthoScale=2.3f;scene.camera.target={0,.9f,0};return scene;}
}
class NativeReferenceIntegrationTests final:public QObject{
    Q_OBJECT
private slots:
    void initTestCase(){QStandardPaths::setTestModeEnabled(true);applyTheme();}
    void sustainedCanvasTransformsPublishDuringTheDragAndCommitOnce_data(){QTest::addColumn<bool>("inspector");QTest::addColumn<bool>("anime");QTest::newRow("canvas")<<false<<false;QTest::newRow("inspector")<<true<<false;QTest::newRow("anime-canvas")<<false<<true;QTest::newRow("anime-inspector")<<true<<true;}
    void sustainedCanvasTransformsPublishDuringTheDragAndCommitOnce(){
        QFETCH(bool,inspector);QFETCH(bool,anime);
        QTemporaryDir directory;MainWindow window(directory.filePath("workspace.ini"),nullptr,true);
        window.setManagedDocument(newManagedDocument("Movement study",false,640,480,Qt::white,false),[]{return true;},[]{});window.resize(1440,900);window.show();
        auto *canvas=window.canvas();canvas->fitSheet();auto *doc=canvas->document();auto model=body();if(anime){model.instances[0].asset=createReferenceCatalogAsset("anime/seed-san");QVERIFY(model.instances[0].asset);}QVERIFY(!doc->addReference3DLayer(model).isEmpty());window.findChild<QAction*>("reference3DReviewAction")->trigger();
        auto *controller=canvas->reference3D();controller->setMode(Reference3DCanvasMode::Translate);controller->setAxis(-1);controller->prepareLayerPreviews({640,480});QTRY_VERIFY_WITH_TIMEOUT(controller->previewReady(),15000);
        const auto source=*controller->scene();const auto at=pointFor(canvas,source,{0,1.05f,0});QVERIFY(canvas->rect().contains(at));const int history=doc->history()->index();
        QSignalSpy publications(controller,&Reference3DCanvasController::previewChanged);
        auto *field=window.findChild<ValueSlider*>("referencePositionX");auto *track=field?field->findChild<QSlider*>():nullptr;
        if(inspector){QVERIFY(track);track->setSliderDown(true);QVERIFY(doc->reference3DEditActive());}else{QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,at);QVERIFY(controller->interactionActive());}
        QVector<double> times;QEventLoop loop;QTimer driver,safety;driver.setInterval(12);driver.setTimerType(Qt::PreciseTimer);safety.setSingleShot(true);int moves=0;bool timedOut=false;
        connect(&driver,&QTimer::timeout,&loop,[&]{if(moves>=60)return;QElapsedTimer timer;timer.start();++moves;if(moves==60)driver.stop();if(inspector)field->setValue(.3*moves/60.);else QTest::mouseMove(canvas,at+QPoint(moves,0),0);times.append(timer.nsecsElapsed()/1e6);if(moves==60)loop.quit();});
        connect(&safety,&QTimer::timeout,&loop,[&]{timedOut=true;loop.quit();});driver.start();safety.start(15000);loop.exec();driver.stop();safety.stop();
        QVERIFY(!timedOut);QCOMPARE(moves,60);QVERIFY(publications.count()>0);QCOMPARE(doc->history()->index(),history);const int liveFrames=publications.count();
        QElapsedTimer release;release.start();if(inspector)track->setSliderDown(false);else QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,at+QPoint(60,0));QTRY_VERIFY_WITH_TIMEOUT(controller->previewReady(),15000);const auto settle=release.elapsed();
        QCOMPARE(doc->history()->index(),history+1);QVERIFY(controller->scene()->instances[0].translation.x()>.1f);QVERIFY(controller->previewError().isEmpty());
        std::sort(times.begin(),times.end());const double p95=times[qMin(int(times.size())-1,int(times.size()*.95))];
        const auto output=qEnvironmentVariable("TOON_REFERENCE3D_NATIVE_CAPTURE_DIR");if(!output.isEmpty()){QVERIFY(QDir().mkpath(output));const QString prefix=anime?"anime-":"";QFile file(QDir(output).filePath(prefix+(inspector?"inspector-transform.json":"canvas-transform.json")));QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(QJsonObject{{"requestedMoves",moves},{"timerCadenceMs",12},{"publishedDuringDrag",liveFrames},{"mouseDispatchP95Ms",p95},{"releaseToFinalMs",settle},{"undoCommandsAdded",1}}).toJson());}
        doc->history()->undo();QCOMPARE(serializeReferenceScene(*controller->scene()),serializeReferenceScene(source));window.markManagedSaved();
    }
    void directCanvasPoseAndInkAboveStayEditableAfterSaving(){
        QTemporaryDir directory;MainWindow window(directory.filePath("workspace.ini"),nullptr,true);
        window.setManagedDocument(newManagedDocument("Reference study",false,640,480,Qt::white,false),[]{return true;},[]{});
        window.resize(1440,900);window.show();auto *canvas=window.canvas();canvas->fitSheet();auto *doc=canvas->document();QString error;
        const auto layerId=doc->addReference3DLayer(body(),"Body reference",&error);QVERIFY2(!layerId.isEmpty(),qPrintable(error));
        window.findChild<QAction*>("reference3DReviewAction")->trigger();auto *panel=window.findChild<Reference3DLayerPanel*>();QVERIFY(panel);
        auto *controller=canvas->reference3D();controller->prepareLayerPreviews({640,480});QTRY_VERIFY_WITH_TIMEOUT(controller->previewReady(),15000);QVERIFY2(controller->previewError().isEmpty(),qPrintable(controller->previewError()));
        auto source=*doc->activeShot().layers[doc->activeLayer()].reference3d;const auto asset=source.instances[0].asset;const int elbow=asset->humanoidBones.value("leftLowerArm",-1);QVERIFY(elbow>=0);
        controller->setMode(Reference3DCanvasMode::Pose);controller->setAxis(2);controller->selectJoint(elbow);QCoreApplication::processEvents();
        const auto globals=evaluateReferenceNodeGlobals(source.instances[0]);QVERIFY(globals);const auto point=pointFor(canvas,source,(*globals)[elbow].map(QVector3D()));QVERIFY(canvas->rect().contains(point));
        const int history=doc->history()->index();QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,point);QTest::mouseMove(canvas,point+QPoint(35,-6));QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,point+QPoint(35,-6));
        QCOMPARE(doc->history()->index(),history+1);source=*doc->activeShot().layers[doc->activeLayer()].reference3d;QVERIFY(source.instances[0].poseRotations.contains(elbow));
        const auto nativeBytes=serializeReferenceScene(source);QVERIFY(!nativeBytes.isEmpty());
        panel->findChild<QToolButton*>("referenceDrawAbove")->click();QCOMPARE(doc->activeShot().layers[doc->activeLayer()].kind,LayerKind::Raster);
        canvas->brush.color=QColor(220,30,40);canvas->brush.size=14;canvas->setTool(Canvas::Tool::Brush);
        const auto center=canvas->artworkViewTransform().map(QPointF(320,240)).toPoint();QVERIFY(canvas->rect().contains(center));
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,center-QPoint(35,0));QTest::mouseMove(canvas,center+QPoint(35,0));QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,center+QPoint(35,0));
        QVERIFY(!doc->activeShot().layers[doc->activeLayer()].tiles.isEmpty());
        bool retained=false;for(const auto &layer:doc->activeShot().layers)if(layer.id==layerId){QCOMPARE(serializeReferenceScene(*layer.reference3d),nativeBytes);retained=true;}QVERIFY(retained);
        const auto file=directory.filePath("posed-drawing.toon");QVERIFY2(saveProject(doc->project(),file,&error),qPrintable(error));const auto loaded=loadProject(file,&error);QVERIFY2(loaded,qPrintable(error));
        retained=false;for(const auto &layer:loaded->shots[0].layers)if(layer.id==layerId){QVERIFY(layer.reference3d);QCOMPARE(serializeReferenceScene(*layer.reference3d),nativeBytes);retained=true;}QVERIFY(retained);
        const auto output=qEnvironmentVariable("TOON_REFERENCE3D_NATIVE_CAPTURE_DIR");if(!output.isEmpty()){QVERIFY(QDir().mkpath(output));QTRY_VERIFY_WITH_TIMEOUT(controller->previewReady(),15000);QVERIFY(window.grab().save(QDir(output).filePath("native-pose-and-ink.png")));QVERIFY(canvas->grab().save(QDir(output).filePath("native-canvas.png")));}
        window.markManagedSaved();
    }
};
QTEST_MAIN(NativeReferenceIntegrationTests)
#include "test_reference3d_native_integration.moc"
