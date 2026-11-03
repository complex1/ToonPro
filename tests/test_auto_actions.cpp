#include "auto_actions.h"
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace toon;
namespace {
Project project(){auto p=Project::create();p.width=320;p.height=240;return p;}
const RasterLayer &layer(Document &d){return d.activeShot().layers[d.activeLayer()];}
QJsonObject read(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return QJsonDocument::fromJson(f.readAll()).object();}
bool write(const QString &path,const QJsonObject &o){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(QJsonDocument(o).toJson())>0;}
}
class AutoActionsTests:public QObject {
    Q_OBJECT
private slots:
    void recordReplayFreshDocumentAndSingleUndo(){
        QTemporaryDir dir;Document d;d.load(project());AutoActions a(&d,dir.filePath("actions.json"));QVERIFY(a.load());
        const auto id=a.create("Ink setup");QVERIFY(a.startRecording(id));d.addLayer();
        d.changeLayer("Ink",true,false,false,.55);d.setLayerBlendMode(LayerBlendMode::Multiply);
        LayerEffects e;e.blurEnabled=true;e.blurRadius=3;QVERIFY(d.setLayerEffects(layer(d).id,e));a.stopRecording();
        QCOMPARE(a.action(id)->steps.size(),4);QVERIFY(QFile::exists(a.path()));
        auto fresh=project();fresh.shots[0].layers[0].name="Different artwork";d.load(fresh);const auto before=d.project().toJson();
        QVERIFY(a.play(id));QCOMPARE(d.activeShot().layers.size(),2);QCOMPARE(layer(d).name,QString("Ink"));QCOMPARE(layer(d).opacity,.55);QCOMPARE(layer(d).blendMode,LayerBlendMode::Multiply);QCOMPARE(layer(d).effects,e);QCOMPARE(d.history()->count(),1);
        d.history()->undo();QCOMPARE(d.project().toJson(),before);d.history()->redo();QCOMPARE(layer(d).name,QString("Ink"));
        AutoActions reopened(&d,a.path());QVERIFY(reopened.load());QCOMPARE(reopened.action(id)->steps.size(),4);
    }
    void relativeSelectionsAndDisabledSteps(){
        QTemporaryDir dir;Document d;d.load(project());AutoActions a(&d,dir.filePath("a.json"));const auto id=a.create("Layers");
        QVERIFY(a.startRecording(id));d.addLayer();d.changeLayer("Top",true,false,false,.7);d.selectLayer(0);d.changeLayer("Bottom",true,false,false,1);a.stopRecording();
        QCOMPARE(a.action(id)->steps.size(),4);QCOMPARE(a.action(id)->steps[2].operation,QString("selectRelative"));
        QVERIFY(a.setStepEnabled(id,1,false));d.load(project());QVERIFY(a.play(id));QCOMPARE(d.activeLayer(),0);QCOMPARE(layer(d).name,QString("Bottom"));QCOMPARE(d.activeShot().layers[1].opacity,1.);
        QVERIFY(a.moveStep(id,3,2));QCOMPARE(a.action(id)->steps[2].operation,QString("properties"));QVERIFY(a.removeStep(id,3));
        const auto copy=a.duplicate(id);QVERIFY(!copy.isEmpty());QCOMPARE(a.action(copy)->steps.size(),3);QVERIFY(a.rename(copy,"Copied"));QVERIFY(a.remove(copy));
    }
    void preflightFailurePreservesArtworkAndHistory(){
        QTemporaryDir dir;Document d;d.load(project());AutoActions a(&d,dir.filePath("a.json"));const auto id=a.create("Create then select");
        d.addLayer();d.addLayer();d.selectLayer(0);QVERIFY(a.startRecording(id));d.addLayer();d.selectLayer(3);a.stopRecording();
        d.load(project());const auto before=d.project().toJson();QSignalSpy status(&a,&AutoActions::status);
        QVERIFY(!a.play(id));QCOMPARE(d.project().toJson(),before);QCOMPARE(d.history()->count(),0);QCOMPARE(d.activeLayer(),0);QVERIFY(status.last().first().toString().contains("Step 2"));
    }
    void unsupportedPaintingAndUndoStopBeforeDependentSteps(){
        QTemporaryDir dir;Document d;d.load(project());AutoActions a(&d,dir.filePath("a.json"));const auto id=a.create("Setup");
        QVERIFY(a.startRecording(id));d.addLayer();BrushSettings b;QVERIFY(d.beginStroke({20,20},1,b,false));d.continueStroke({30,30},1);d.endStroke();QVERIFY(!a.recording());QCOMPARE(a.action(id)->steps.size(),1);
        QVERIFY(a.startRecording(id));d.history()->undo();QVERIFY(!a.recording());QCOMPARE(a.action(id)->steps.size(),1);
        QVERIFY(a.startRecording(id));d.selectAllPixels();QVERIFY(!a.recording());QCOMPARE(a.action(id)->steps.size(),1);
        QVERIFY(a.startRecording(id));d.load(project());QVERIFY(!a.recording());
    }
    void pendingGestureGuardsAndDocumentDestruction(){
        QTemporaryDir dir;auto *d=new Document;d->load(project());AutoActions a(d,dir.filePath("a.json"));const auto id=a.create("Setup");
        QVERIFY(a.startRecording(id));d->addLayer();a.stopRecording();
        QVERIFY(d->beginLayerPropertyEdit());QVERIFY(!a.startRecording(id));QVERIFY(!a.play(id));d->cancelLayerPropertyEdit();
        BrushSettings b;QVERIFY(d->beginStroke({20,20},1,b,false));QVERIFY(!a.play(id));d->cancelStroke();
        delete d;QVERIFY(!a.play(id));QVERIFY(!a.availabilityReason().isEmpty());
    }
    void malformedLibraryIsAtomicAndNotOverwritten(){
        QTemporaryDir dir;Document d;d.load(project());AutoActions a(&d,dir.filePath("a.json"));const auto id=a.create("Valid");QVERIFY(a.startRecording(id));d.addLayer();a.stopRecording();
        auto data=read(a.path());auto actions=data["actions"].toArray();auto entry=actions[0].toObject();auto steps=entry["steps"].toArray();auto step=steps[0].toObject();step["operation"]="run shell";steps[0]=step;entry["steps"]=steps;actions[0]=entry;data["actions"]=actions;
        QVERIFY(write(a.path(),data));QString error;QVERIFY(!a.load(&error));QCOMPARE(a.actions().size(),1);QVERIFY(!a.save());QCOMPARE(read(a.path()),data);QVERIFY(a.create("Blocked").isEmpty());
        data["version"]=99;QVERIFY(write(a.path(),data));QVERIFY(!a.load());QCOMPARE(a.action(id)->steps.size(),1);
    }
    void nonActiveStructuralCommandsStopWithoutRecordingWrongTarget() {
        QTemporaryDir dir;Document d;d.load(project());d.addLayer();
        d.changeLayer("Drawing 1",true,false,false,1); // same name and pixels cannot identify the source
        const auto other=layer(d).id;d.selectLayer(0);AutoActions a(&d,dir.filePath("a.json"));const auto id=a.create("Active only");
        QVERIFY(a.startRecording(id));d.duplicateLayers({other});QVERIFY(!a.recording());QVERIFY(a.action(id)->steps.isEmpty());
        d.selectLayer(0);QVERIFY(a.startRecording(id));d.deleteLayers({other});QVERIFY(!a.recording());QVERIFY(a.action(id)->steps.isEmpty());
        d.addLayer();const auto unselected=layer(d).id;d.selectLayer(0);QVERIFY(a.startRecording(id));d.groupLayers({unselected});QVERIFY(!a.recording());QVERIFY(a.action(id)->steps.isEmpty());
    }
    void cancelledPropertyPreviewKeepsRecordingAndSelection() {
        QTemporaryDir dir;Document d;d.load(project());d.selectAllPixels();const auto selection=d.rasterSelection().path;
        AutoActions a(&d,dir.filePath("a.json"));const auto id=a.create("Cancel preview");QVERIFY(a.startRecording(id));
        QVERIFY(d.beginLayerPropertyEdit());d.previewLayerOpacity(.3);d.cancelLayerPropertyEdit();
        QVERIFY(a.recording());QVERIFY(a.action(id)->steps.isEmpty());QCOMPARE(d.rasterSelection().path,selection);
        d.addLayer();QCOMPARE(a.action(id)->steps.size(),1);QVERIFY(a.recording());a.stopRecording();
    }
    void frameSwitchDoesNotAppendRelativeSelection() {
        QTemporaryDir dir;Document d;d.load(project());AutoActions a(&d,dir.filePath("a.json"));const auto id=a.create("Frame guard");
        QVERIFY(a.startRecording(id));QVERIFY(d.setCurrentFrame(1));QVERIFY(!a.recording());QVERIFY(a.action(id)->steps.isEmpty());
    }
    void lockedTargetFailureIsAtomic() {
        QTemporaryDir dir;Document d;d.load(project());AutoActions a(&d,dir.filePath("a.json"));const auto id=a.create("Opacity");
        QVERIFY(a.startRecording(id));d.changeLayer(layer(d).name,true,false,false,.4);a.stopRecording();
        d.load(project());d.setLayersLocked({layer(d).id},true);const auto before=d.project().toJson();const int history=d.history()->count();
        QVERIFY(!a.play(id));QCOMPARE(d.project().toJson(),before);QCOMPARE(d.history()->count(),history);
    }
    void recordingStopsOnSaveFailureAndKeepsDraft() {
        QTemporaryDir dir;Document d;d.load(project());AutoActions a(&d,dir.filePath("a.json"));const auto id=a.create("Draft");QVERIFY(a.startRecording(id));
        QVERIFY(QFile::remove(a.path()));QVERIFY(QDir().mkdir(a.path()));QSignalSpy status(&a,&AutoActions::status);
        d.addLayer();QVERIFY(!a.recording());QCOMPARE(a.action(id)->steps.size(),1);QVERIFY(status.last().first().toString().contains("unsaved"));
        QVERIFY(!a.startRecording(id));QVERIFY(QDir(a.path()).removeRecursively());QVERIFY(a.save());QVERIFY(a.startRecording(id));a.stopRecording();
    }
    void stepLimitAndUnknownFieldValidation() {
        QTemporaryDir dir;Document d;d.load(project());AutoActions a(&d,dir.filePath("a.json"));const auto id=a.create("Validation");QVERIFY(a.startRecording(id));d.addLayer();a.stopRecording();
        auto data=read(a.path());auto entries=data["actions"].toArray();auto entry=entries[0].toObject();auto step=entry["steps"].toArray()[0].toObject();QJsonArray tooMany;
        for(int i=0;i<=AutoActions::maxSteps;++i)tooMany.append(step);entry["steps"]=tooMany;entries[0]=entry;data["actions"]=entries;QVERIFY(write(a.path(),data));QVERIFY(!a.load());
        auto params=step["parameters"].toObject();params["unknown"]=true;step["parameters"]=params;entry["steps"]=QJsonArray{step};entries[0]=entry;data["actions"]=entries;QVERIFY(write(a.path(),data));QVERIFY(!a.load());QCOMPARE(a.action(id)->steps.size(),1);
    }
    void fillThenTransformFailureKeepsPixelsSelectionAndHistory() {
        QTemporaryDir dir; Document d; d.load(project());
        QPainterPath selection; selection.addRect(QRectF(20,20,50,40)); d.setRasterSelection(selection);
        QVERIFY(d.fillRasterSelection(Qt::black));
        AutoActions a(&d,dir.filePath("a.json")); const auto id=a.create("Fill then move");
        QVERIFY(a.startRecording(id)); QVERIFY(d.fillRasterSelection(Qt::red));
        TransformParameters t; t.translation={30,10};
        QVERIFY(d.beginTransform()); QVERIFY(d.previewTransform(t)); QVERIFY(d.applyTransform());
        QVERIFY(a.recording()); a.stopRecording(); QCOMPARE(a.action(id)->steps.size(),2);
        d.load(project()); d.setRasterSelection(selection); QVERIFY(d.fillRasterSelection(Qt::blue));
        d.setLayerPositionLocked(true);
        const auto before=d.project(); const auto beforeSelection=d.rasterSelection();
        const int count=d.history()->count(), index=d.history()->index();
        QSignalSpy status(&a,&AutoActions::status);
        QVERIFY(!a.play(id));
        QCOMPARE(d.project().toJson(),before.toJson()); QCOMPARE(layer(d).tiles,before.shots[0].layers[0].tiles);
        QCOMPARE(d.rasterSelection().path,beforeSelection.path); QCOMPARE(d.rasterSelection().active,beforeSelection.active);
        QCOMPARE(d.rasterSelection().featherRadius,beforeSelection.featherRadius);
        QCOMPARE(d.history()->count(),count); QCOMPARE(d.history()->index(),index);
        QVERIFY(status.last().first().toString().contains("Step 2"));
    }
    void replayPreservesSourceEditingForAnimatedArtwork() {
        QTemporaryDir dir; Document d; d.load(project());
        QVERIFY(d.setKeyframeValue(layer(d).id,KeyframeProperty::PositionX,45));
        const auto before=d.project();
        d.setKeyframeSourceEditing(true);
        QPainterPath selection; selection.addRect(QRectF(20,20,40,30)); d.setRasterSelection(selection);
        AutoActions a(&d,dir.filePath("a.json")); const auto id=a.create("Source fill and move");
        QVERIFY(a.startRecording(id)); QVERIFY(d.fillRasterSelection(Qt::red));
        TransformParameters transform; transform.translation={15,5};
        QVERIFY(d.beginTransform()); QVERIFY(d.previewTransform(transform)); QVERIFY(d.applyTransform());
        QVERIFY(a.recording()); a.stopRecording(); QCOMPARE(a.action(id)->steps.size(),2);
        const auto expected=layer(d).tiles; const auto moved=d.rasterSelection().path;
        d.load(before); d.setRasterSelection(selection);
        QVERIFY(!d.keyframeSourceEditing()); QVERIFY(!a.play(id));
        QVERIFY(layer(d).tiles.isEmpty()); QCOMPARE(d.history()->count(),0);
        QCOMPARE(d.rasterSelection().path,selection);
        d.setKeyframeSourceEditing(true); d.setRasterSelection(selection);
        QVERIFY(a.play(id)); QCOMPARE(layer(d).tiles,expected); QCOMPARE(d.rasterSelection().path,moved);
        QVERIFY(d.keyframeSourceEditing()); QCOMPARE(d.history()->count(),1);
        QCOMPARE(d.activeShot().layerAnimations,before.shots[0].layerAnimations);
        d.history()->undo(); QVERIFY(layer(d).tiles.isEmpty()); QCOMPARE(d.rasterSelection().path,selection);
        QVERIFY(d.keyframeSourceEditing());
    }
    void extendedLibraryVersionAndDowngradeProtection() {
        QTemporaryDir dir; Document d; d.load(project()); AutoActions a(&d,dir.filePath("a.json"));
        const auto id=a.create("Fill and transform");
        QCOMPARE(read(a.path())["version"].toInt(),1);
        QPainterPath selection; selection.addRect(QRectF(10,10,60,40)); d.setRasterSelection(selection);
        QVERIFY(a.startRecording(id)); QVERIFY(d.fillRasterSelection(Qt::red));
        TransformParameters t; t.translation={10,0}; QVERIFY(d.beginTransform());
        QVERIFY(d.previewTransform(t)); QVERIFY(d.applyTransform()); a.stopRecording();
        const auto valid=read(a.path()); QCOMPARE(valid["version"].toInt(),2);
        AutoActions reopened(&d,a.path()); QVERIFY(reopened.load());
        QCOMPARE(reopened.action(id)->steps.size(),2);
        QCOMPARE(reopened.action(id)->steps[0].operation,QString("fill"));
        QCOMPARE(reopened.action(id)->steps[1].parameters["translationX"].toDouble(),10.);
        auto downgraded=valid; downgraded["version"]=1; QVERIFY(write(a.path(),downgraded));
        QVERIFY(!reopened.load()); QCOMPARE(reopened.action(id)->steps.size(),2);
        QVERIFY(!reopened.save()); QCOMPARE(read(a.path()),downgraded);
        QVERIFY(write(a.path(),valid)); QVERIFY(reopened.load());
        QVERIFY(reopened.removeStep(id,1)); QVERIFY(reopened.removeStep(id,0));
        QCOMPARE(read(a.path())["version"].toInt(),1); QVERIFY(reopened.load());
    }
    void extendedLibraryRejectsMalformedParametersAtomically() {
        QTemporaryDir dir; Document d; d.load(project()); AutoActions a(&d,dir.filePath("a.json"));
        const auto id=a.create("Validation"); const auto original=read(a.path());
        auto entries=original["actions"].toArray(); auto entry=entries[0].toObject();
        const QJsonObject fill{{"color","#ffff0000"}};
        const QJsonObject transform{{"translationX",10},{"translationY",0},{"scaleX",1},{"scaleY",1},{"rotation",0}};
        const QJsonObject bucket{{"x",10},{"y",20},{"color","#ff000000"},{"source",0},{"tolerance",24},{"gapClosing",1}};
        const auto loadStep=[&](const QString &operation,const QJsonObject &parameters) {
            auto data=original; data["version"]=2;
            entry["steps"]=QJsonArray{QJsonObject{{"operation",operation},{"label","Recorded step"},{"parameters",parameters},{"enabled",true}}};
            entries[0]=entry; data["actions"]=entries;
            return write(a.path(),data)&&a.load();
        };
        QVERIFY(loadStep("fill",fill));
        for(const QString &color:QStringList{"red","#ff0000","#ggff0000",""}) {
            auto p=fill; p["color"]=color; QVERIFY(!loadStep("fill",p));
            QCOMPARE(a.action(id)->steps[0].parameters,fill);
        }
        QVERIFY(loadStep("transform",transform));
        for(const auto &field:QStringList{"scaleX","scaleY"}) { auto p=transform; p[field]=0; QVERIFY(!loadStep("transform",p)); }
        auto p=transform; p["translationX"]="10"; QVERIFY(!loadStep("transform",p));
        p=transform; p["rotation"]=181; QVERIFY(!loadStep("transform",p));
        p=transform; p["sourcePixels"]="unsupported"; QVERIFY(!loadStep("transform",p));
        QCOMPARE(a.action(id)->steps[0].parameters,transform);
        QVERIFY(loadStep("bucket",bucket));
        p=bucket; p["source"]=2; QVERIFY(!loadStep("bucket",p));
        p["referenceOffset"]=0; QVERIFY(loadStep("bucket",p));
        for(int offset:{1-maxAnimationLayers,maxAnimationLayers-1}) {
            p["referenceOffset"]=offset; QVERIFY(loadStep("bucket",p));
        }
        for(int offset:{-maxAnimationLayers,maxAnimationLayers}) {
            p["referenceOffset"]=offset; QVERIFY(!loadStep("bucket",p));
        }
        p=bucket; p["referenceOffset"]=0; QVERIFY(!loadStep("bucket",p));
        for(const auto &bad:QVector<QPair<QString,double>>{{"source",3},{"tolerance",256},{"tolerance",.5},{"gapClosing",21},{"x",32769}}) {
            p=bucket; p[bad.first]=bad.second; QVERIFY(!loadStep("bucket",p));
        }
        QVERIFY(loadStep("bucket",bucket));
    }
    void groupDuplicateAndDeleteRemainRelative(){
        QTemporaryDir dir;Document d;d.load(project());AutoActions a(&d,dir.filePath("a.json"));const auto id=a.create("Group setup");
        QVERIFY(a.startRecording(id));d.addGroup();d.addLayer();d.duplicateLayers({layer(d).id});d.deleteLayer();a.stopRecording();QCOMPARE(a.action(id)->steps.size(),4);
        d.load(project());QVERIFY(a.play(id));QCOMPARE(d.activeShot().layers.size(),3);int groups=0;for(const auto &l:d.activeShot().layers)groups+=l.group;QCOMPARE(groups,1);QCOMPARE(d.history()->count(),1);
    }
};
QTEST_MAIN(AutoActionsTests)
#include "test_auto_actions.moc"
