#include "brush_engine.h"
#include "brush_settings_panel.h"
#include "camera_tool.h"
#include "keyframe_transform_tool.h"
#include "mask_tool.h"
#include "tool_catalog.h"
#include "tool_session.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>
using namespace toon;
namespace {
ToolState sampledState(bool colored=false,bool multiple=false) {
    ToolState state;state.brush=brushSettingsForType(colored?BrushType::ColoredStamp:BrushType::Stamp);
    if(colored){state.brush.colorTip=QImage(16,20,QImage::Format_ARGB32_Premultiplied);state.brush.colorTip.fill(QColor(140,50,220,190));}
    else{state.brush.tipMask=QImage(16,20,QImage::Format_Grayscale8);state.brush.tipMask.fill(210);}
    if(multiple){QImage first(16,20,QImage::Format_Grayscale8),second(16,20,QImage::Format_Grayscale8);first.fill(110);second.fill(230);setBrushTipImages(state.brush,{first,second});}
    return state;
}
QString sampled(ToolCatalog &catalog,const QString &name,BrushType type,bool multiple=false){
    return catalog.createBrush(name,type,sampledState(type==BrushType::ColoredStamp,multiple),"Test brushes",{},"group/drawing");
}
void setup(Canvas &canvas){auto project=Project::create();project.width=320;project.height=240;canvas.setProject(project);}
QJsonValue option(const ToolCatalog &catalog,const QString &id){const auto *tool=catalog.tool(id);return tool?tool->state.options.value("stampPlacement"):QJsonValue();}
void pointer(Canvas &canvas,QEvent::Type type,QPointF point){
    const auto local=canvas.viewTransform().map(sheetGeometry(canvas.project(),canvas.shot()).output.topLeft()+point);
    const bool release=type==QEvent::MouseButtonRelease;
    QMouseEvent event(type,local,canvas.mapToGlobal(local),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
        release?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&canvas,&event);
}
}
class StampPlacementSettingsTests:public QObject{
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void metadataDefaultsKeepSampledInkAndCustomStrokeBehavior(){
        QTemporaryDir dir;ToolCatalog catalog(nullptr,dir.filePath("tools.json"));QVERIFY(catalog.load());
        const auto ink=sampled(catalog,"Textured ink",BrushType::Ink);
        const auto stamp=sampled(catalog,"Stamp",BrushType::Stamp);
        const auto colored=sampled(catalog,"Colored stamp",BrushType::ColoredStamp);
        const auto multiple=sampled(catalog,"Multiple tips",BrushType::Custom,true);
        for(const auto &id:{ink,stamp,colored,multiple})QVERIFY(!id.isEmpty());
        Canvas canvas;setup(canvas);ToolSession session(&canvas,&catalog);
        QVERIFY(catalog.activateTool("brush/round"));QVERIFY(!canvas.stampPlacementEnabled());QVERIFY(!canvas.stampPlacementAvailable());
        QVERIFY(catalog.activateTool(ink));QVERIFY(!canvas.stampPlacementEnabled());QVERIFY(canvas.stampPlacementAvailable());
        QVERIFY(catalog.activateTool(stamp));QVERIFY(canvas.stampPlacementEnabled());QVERIFY(canvas.stampPlacementAvailable());
        QVERIFY(catalog.activateTool(colored));QVERIFY(canvas.stampPlacementEnabled());QVERIFY(canvas.stampPlacementAvailable());
        QVERIFY(catalog.activateTool(multiple));QVERIFY(!canvas.stampPlacementEnabled());QVERIFY(canvas.stampPlacementAvailable());
        canvas.setStampPlacementEnabled(true);QCOMPARE(option(catalog,multiple),QJsonValue(true));
        QVERIFY(catalog.activateTool("eraser"));QVERIFY(!canvas.stampPlacementEnabled());QVERIFY(!canvas.stampPlacementAvailable());
        QVERIFY(catalog.activateTool(multiple));QVERIFY(canvas.stampPlacementEnabled());
        QCOMPARE(canvas.document()->history()->count(),0);
    }
    void defaultRemainsImplicitUntilUserOverridesIt(){
        QTemporaryDir dir;ToolCatalog catalog(nullptr,dir.filePath("tools.json"));QVERIFY(catalog.load());
        const auto id=sampled(catalog,"Convertible texture",BrushType::Ink);QVERIFY(!id.isEmpty());
        Canvas canvas;setup(canvas);ToolSession session(&canvas,&catalog);QVERIFY(catalog.activateTool(id));
        canvas.setBrushSize(42);session.flush();QVERIFY(option(catalog,id).isUndefined());
        const auto brush=canvas.brush;
        QVERIFY(catalog.setBrushMetadata({id},BrushType::Stamp,"Nature"));QVERIFY(canvas.stampPlacementEnabled());
        QCOMPARE(canvas.brush,brush);QVERIFY(option(catalog,id).isUndefined());
        catalog.history()->undo();QVERIFY(!canvas.stampPlacementEnabled());QVERIFY(option(catalog,id).isUndefined());
        catalog.history()->redo();QVERIFY(canvas.stampPlacementEnabled());
        canvas.setStampPlacementEnabled(false);QCOMPARE(option(catalog,id),QJsonValue(false));
        QVERIFY(catalog.setBrushMetadata({id},BrushType::Ink,"Ink"));QVERIFY(!canvas.stampPlacementEnabled());
        QVERIFY(catalog.setBrushMetadata({id},BrushType::Stamp,"Nature"));QVERIFY(!canvas.stampPlacementEnabled());
        canvas.setBrushFlow(.4);session.flush();QCOMPARE(option(catalog,id),QJsonValue(false));
    }
    void overridesAreIndependentAndSurviveDuplicateAndReload(){
        QTemporaryDir dir;const auto path=dir.filePath("tools.json");ToolCatalog catalog(nullptr,path);QVERIFY(catalog.load());
        const auto stamp=sampled(catalog,"Single image",BrushType::Stamp);
        const auto ink=sampled(catalog,"Texture stroke",BrushType::Ink);QVERIFY(!stamp.isEmpty());QVERIFY(!ink.isEmpty());
        QString copy;
        {
            Canvas canvas;setup(canvas);ToolSession session(&canvas,&catalog);
            QVERIFY(catalog.activateTool(stamp));canvas.setStampPlacementEnabled(false);
            copy=catalog.duplicateTool(stamp,"group/drawing","Single image copy");QVERIFY(!copy.isEmpty());
            QCOMPARE(canvas.tool(),Canvas::Tool::Brush);QVERIFY(!canvas.stampPlacementEnabled());QCOMPARE(option(catalog,copy),QJsonValue(false));
            canvas.setStampPlacementEnabled(true);QCOMPARE(option(catalog,copy),QJsonValue(true));
            QVERIFY(catalog.activateTool(stamp));QVERIFY(!canvas.stampPlacementEnabled());
            QVERIFY(catalog.activateTool(ink));QVERIFY(!canvas.stampPlacementEnabled());canvas.setStampPlacementEnabled(true);
            QVERIFY(catalog.activateTool(copy));QVERIFY(canvas.stampPlacementEnabled());session.flush();
        }
        ToolCatalog restored(nullptr,path);QString error;QVERIFY2(restored.load(&error),qPrintable(error));
        Canvas canvas;setup(canvas);ToolSession session(&canvas,&restored);
        QVERIFY(restored.activateTool(stamp));QVERIFY(!canvas.stampPlacementEnabled());
        QVERIFY(restored.activateTool(copy));QVERIFY(canvas.stampPlacementEnabled());
        QVERIFY(restored.activateTool(ink));QVERIFY(canvas.stampPlacementEnabled());
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(QJsonDocument::fromJson(file.readAll()).object()["version"].toInt(),10);
    }
    void brushTipChangesPreserveConfiguredMode(){
        QTemporaryDir dir;ToolCatalog catalog(nullptr,dir.filePath("tools.json"));QVERIFY(catalog.load());
        const auto id=sampled(catalog,"Editable texture",BrushType::Custom);QVERIFY(!id.isEmpty());
        Canvas canvas;setup(canvas);ToolSession session(&canvas,&catalog);QVERIFY(catalog.activateTool(id));
        canvas.setStampPlacementEnabled(true);const auto original=canvas.brush;
        auto round=original;round.tipMask={};canvas.setBrushSettings(round);
        QVERIFY(canvas.stampPlacementEnabled());QVERIFY(!canvas.stampPlacementAvailable());QCOMPARE(option(catalog,id),QJsonValue(true));
        canvas.setBrushSettings(original);QVERIFY(canvas.stampPlacementAvailable());QVERIFY(canvas.stampPlacementEnabled());
        QVERIFY(catalog.activateTool("brush/round"));QVERIFY(!canvas.stampPlacementEnabled());
        QVERIFY(catalog.activateTool(id));QVERIFY(canvas.stampPlacementEnabled());
    }
    void compactPropertyToggleReflectsToolAndDoesNotEditArtwork(){
        QTemporaryDir dir;ToolCatalog catalog(nullptr,dir.filePath("tools.json"));QVERIFY(catalog.load());
        const auto stamp=sampled(catalog,"Stamp",BrushType::Stamp);const auto ink=sampled(catalog,"Ink",BrushType::Ink);
        Canvas canvas;setup(canvas);ToolSession session(&canvas,&catalog);BrushSettingsPanel panel(&canvas);
        panel.resize(240,500);panel.show();QApplication::processEvents();
        auto *toggle=panel.findChild<QToolButton*>("stampPlacementToggle");QVERIFY(toggle);
        QVERIFY(toggle->isCheckable());QVERIFY(!toggle->icon().isNull());
        QCOMPARE(toggle->accessibleName(),QString("Adjust stamp"));QVERIFY(toggle->toolTip().contains("resize"));
        QVERIFY(catalog.activateTool("brush/round"));QVERIFY(toggle->isHidden());
        QVERIFY(catalog.activateTool(stamp));QVERIFY(!toggle->isHidden());QVERIFY(toggle->isChecked());QCOMPARE(toggle->text(),QString("On"));
        QApplication::processEvents();
        // A text-bearing On/Off action can grow with the font. Inspect its
        // laid-out visible state, not the hidden widget's initial size hint.
        QVERIFY(toggle->height()>=ui::metrics::actionTarget);
        QVERIFY(toggle->height()>=toggle->fontMetrics().height()+4);
        const auto preview=qEnvironmentVariable("TOON_STAMP_TOGGLE_PREVIEW");
        if(!preview.isEmpty()){
            QVERIFY(QDir().mkpath(preview));panel.resize(220,420);QApplication::processEvents();
            QVERIFY(panel.grab().save(QDir(preview).filePath("adjust-stamp-on.png")));
        }
        const auto before=canvas.project().toJson();const auto brush=canvas.brush;
        QTest::mouseClick(toggle,Qt::LeftButton);QVERIFY(!canvas.stampPlacementEnabled());QCOMPARE(option(catalog,stamp),QJsonValue(false));QCOMPARE(toggle->text(),QString("Off"));
        if(!preview.isEmpty()){
            QApplication::processEvents();QVERIFY(panel.grab().save(QDir(preview).filePath("adjust-stamp-off.png")));
        }
        QCOMPARE(canvas.brush,brush);QCOMPARE(canvas.project().toJson(),before);QCOMPARE(canvas.document()->history()->count(),0);
        QVERIFY(catalog.activateTool(ink));QVERIFY(!toggle->isHidden());QVERIFY(!toggle->isChecked());
        QTest::mouseClick(toggle,Qt::LeftButton);QVERIFY(canvas.stampPlacementEnabled());QCOMPARE(option(catalog,ink),QJsonValue(true));
        QVERIFY(catalog.activateTool("eraser"));QVERIFY(toggle->isHidden());
        QVERIFY(catalog.activateTool(stamp));QVERIFY(!toggle->isChecked());
    }
    void disablingThroughPropertiesPlacesStampThenAllowsContinuousDrawing(){
        QTemporaryDir dir;ToolCatalog catalog(nullptr,dir.filePath("tools.json"));QVERIFY(catalog.load());
        const auto id=sampled(catalog,"Adjustable stamp",BrushType::Stamp);QVERIFY(!id.isEmpty());
        Canvas canvas;setup(canvas);canvas.resize(900,760);canvas.setZoom(1);
        ToolSession session(&canvas,&catalog);QVERIFY(catalog.activateTool(id));
        auto brush=canvas.brush;brush.size=24;brush.spacing=.1;brush.lockRandomSeed=true;brush.randomSeed=411;
        brush.smoothingEnabled=false;canvas.setBrushSettings(brush);canvas.setStabilization(0);canvas.setHoldShapeEnabled(false);
        BrushSettingsPanel panel(&canvas);panel.resize(240,500);canvas.show();panel.show();QApplication::processEvents();
        auto *toggle=panel.findChild<QToolButton*>("stampPlacementToggle");QVERIFY(toggle);QVERIFY(toggle->isChecked());
        const auto blank=renderMovieFrame(canvas.project());
        pointer(canvas,QEvent::MouseButtonPress,{80,60});pointer(canvas,QEvent::MouseButtonRelease,{80,60});
        QVERIFY(canvas.document()->stampPlacementActive());QCOMPARE(canvas.document()->history()->count(),0);
        TransformParameters adjusted;adjusted.translation={30,15};adjusted.scaleX=adjusted.scaleY=1.5;adjusted.rotation=25;
        QVERIFY(canvas.document()->previewTransform(adjusted));
        const auto placed=renderMovieFrame(canvas.document()->displayProject());QVERIFY(placed!=blank);

        QTest::mouseClick(toggle,Qt::LeftButton);
        QVERIFY(!toggle->isChecked());QCOMPARE(toggle->text(),QString("Off"));QVERIFY(!canvas.stampPlacementEnabled());
        QVERIFY(!canvas.document()->stampPlacementActive());QVERIFY(!canvas.document()->transformActive());
        QCOMPARE(renderMovieFrame(canvas.project()),placed);QCOMPARE(canvas.document()->history()->count(),1);
        QCOMPARE(option(catalog,id),QJsonValue(false));
        canvas.document()->history()->undo();QCOMPARE(renderMovieFrame(canvas.project()),blank);
        canvas.document()->history()->redo();QCOMPARE(renderMovieFrame(canvas.project()),placed);
        QVERIFY(!toggle->isChecked());QVERIFY(!canvas.stampPlacementEnabled());

        Document expected;expected.load(canvas.project());
        QVERIFY(expected.beginStroke({60,180},1,canvas.brush,false));expected.continueStroke({200,180},1);expected.endStroke();
        pointer(canvas,QEvent::MouseButtonPress,{60,180});pointer(canvas,QEvent::MouseMove,{200,180});
        pointer(canvas,QEvent::MouseButtonRelease,{200,180});
        QVERIFY(!canvas.document()->stampPlacementActive());QVERIFY(!canvas.document()->transformActive());
        QCOMPARE(renderMovieFrame(canvas.project()),renderMovieFrame(expected.project()));
        QCOMPARE(canvas.document()->history()->count(),2);

        QTest::mouseClick(toggle,Qt::LeftButton);
        QVERIFY(toggle->isChecked());QCOMPARE(toggle->text(),QString("On"));QCOMPARE(option(catalog,id),QJsonValue(true));
        QCOMPARE(canvas.document()->history()->count(),2);
        const auto continuous=renderMovieFrame(canvas.project());
        pointer(canvas,QEvent::MouseButtonPress,{260,80});pointer(canvas,QEvent::MouseMove,{280,120});
        pointer(canvas,QEvent::MouseButtonRelease,{280,120});
        QVERIFY(canvas.document()->stampPlacementActive());QVERIFY(canvas.document()->transformActive());
        QCOMPARE(renderMovieFrame(canvas.project()),continuous);QCOMPARE(canvas.document()->history()->count(),2);
        canvas.document()->cancelTransform();
    }
    void temporaryCanvasModesRefreshVisibilityWithoutChangingChoice(){
        QTemporaryDir dir;ToolCatalog catalog(nullptr,dir.filePath("tools.json"));QVERIFY(catalog.load());
        const auto stamp=sampled(catalog,"Stamp",BrushType::Stamp);
        Canvas canvas;setup(canvas);ToolSession session(&canvas,&catalog);QVERIFY(catalog.activateTool(stamp));
        BrushSettingsPanel panel(&canvas);panel.show();
        auto *toggle=panel.findChild<QToolButton*>("stampPlacementToggle");QVERIFY(toggle);QVERIFY(!toggle->isHidden());
        QVERIFY(canvas.document()->addLayerMask());
        canvas.maskTool()->setEnabled(true);QVERIFY(canvas.maskTool()->enabled());QVERIFY(toggle->isHidden());
        canvas.maskTool()->setEnabled(false);QVERIFY(!toggle->isHidden());
        canvas.cameraTool()->setEnabled(true);QVERIFY(canvas.cameraTool()->enabled());QVERIFY(toggle->isHidden());
        canvas.cameraTool()->setEnabled(false);QVERIFY(!toggle->isHidden());
        canvas.keyframeTransformTool()->setEnabled(true);QVERIFY(canvas.keyframeTransformTool()->enabled());QVERIFY(toggle->isHidden());
        canvas.keyframeTransformTool()->setEnabled(false);QVERIFY(!toggle->isHidden());
        QVERIFY(toggle->isChecked());QVERIFY(canvas.stampPlacementEnabled());QVERIFY(option(catalog,stamp).isUndefined());
    }
};
QTEST_MAIN(StampPlacementSettingsTests)
#include "test_stamp_placement_settings.moc"
