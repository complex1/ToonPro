#include "brush_settings_panel.h"
#include "brush_surface_pattern_widget.h"
#include "canvas.h"
#include "document.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QDialog>
#include <QCheckBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QImage>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>
using namespace toon;
namespace {
void enter(ValueSlider *slider,const QString &text){auto *field=slider->findChild<QDoubleSpinBox*>();QVERIFY(field);field->setFocus();field->selectAll();QTest::keyClicks(field,text);QTest::keyClick(field,Qt::Key_Return);}
void openSurface(BrushSettingsPanel &panel){panel.show();panel.openAdvancedSettings();auto *category=panel.findChild<QTabBar*>("brushSettingsCategory");QVERIFY(category);category->setCurrentIndex(11);QTest::qWait(20);}
}
class SurfaceIntegrationTests:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void pendingImageConfigurationSurvivesOtherBrushEditsAndImportsAtomically(){
        Canvas canvas;canvas.setProject(Project::create());BrushSettingsPanel panel(&canvas);openSurface(panel);
        auto *surface=panel.findChild<BrushSurfacePatternWidget*>();QVERIFY(surface);
        auto *kind=surface->findChild<QTabBar*>("surfaceKind");auto *width=surface->findChild<ValueSlider*>("surfaceTileWidth");QVERIFY(kind&&width);
        QSignalSpy edits(surface,&BrushSurfacePatternWidget::patternEdited);
        kind->setCurrentIndex(int(BrushSurfaceKind::Image));QCOMPARE(canvas.brush.surface.kind,BrushSurfaceKind::Off);QCOMPARE(edits.size(),0);
        width->setValue(384);QCOMPARE(canvas.brush.surface.tileSize.width(),128.);QCOMPARE(edits.size(),0);
        auto *perspective=surface->findChild<QCheckBox*>("surfacePerspective");auto *preview=surface->findChild<QWidget*>("surfacePlanePreview");QVERIFY(perspective&&preview);perspective->setChecked(true);QTest::keyClick(preview,Qt::Key_Right);QCOMPARE(edits.size(),0);QCOMPARE(canvas.brush.surface.kind,BrushSurfaceKind::Off);
        auto *size=panel.findChild<ValueSlider*>("quickBrushSize");QVERIFY(size);size->setValue(55);canvas.setBrushColor(QColor("#b24725"));
        QCOMPARE(canvas.brush.size,55.);QCOMPARE(kind->currentIndex(),int(BrushSurfaceKind::Image));QCOMPARE(width->value(),384.);
        QTemporaryDir directory;QString error;QVERIFY(!surface->importImage(directory.filePath("missing.png"),&error));QCOMPARE(kind->currentIndex(),int(BrushSurfaceKind::Image));QCOMPARE(width->value(),384.);QCOMPARE(canvas.brush.surface.kind,BrushSurfaceKind::Off);
        QImage source(20,20,QImage::Format_ARGB32_Premultiplied);source.fill(QColor(40,160,210,128));const auto path=directory.filePath("tile.png");QVERIFY(source.save(path));
        QVERIFY2(surface->importImage(path,&error),qPrintable(error));QCOMPARE(canvas.brush.surface.kind,BrushSurfaceKind::Image);QCOMPARE(canvas.brush.surface.image,source);QCOMPARE(canvas.brush.surface.tileSize.width(),384.);QVERIFY(canvas.brush.surface.perspective);QCOMPARE(canvas.brush.surface.quad[0].x(),1.);QCOMPARE(canvas.brush.size,55.);QCOMPARE(canvas.brush.color,QColor("#b24725"));QCOMPARE(edits.size(),1);
        QCOMPARE(canvas.document()->history()->count(),0);
        QCoreApplication::processEvents();auto *actions=surface->findChild<QWidget*>("surfaceActions");auto *import=surface->findChild<QToolButton*>("surfaceImport");QVERIFY(actions&&import);
        QVERIFY(actions->geometry().top()>preview->geometry().bottom());QVERIFY(import->mapTo(surface,QPoint()).y()>preview->geometry().bottom());QVERIFY(import->geometry().bottom()<actions->height());
        const auto previewDirectory=qEnvironmentVariable("TOON_SURFACE_PREVIEW");if(!previewDirectory.isEmpty()){QDir().mkpath(previewDirectory);QVERIFY(panel.findChild<QDialog*>("advancedBrushDialog")->grab().save(previewDirectory+"/surface-integrated.png"));}
    }
    void invalidQuadEntryRestoresNumbersAndCanvas(){
        Canvas canvas;canvas.setProject(Project::create());auto brush=canvas.brush;brush.surface.kind=BrushSurfaceKind::Dots;brush.surface.perspective=true;canvas.setBrushSettings(brush);
        BrushSettingsPanel panel(&canvas);openSurface(panel);auto *x=panel.findChild<ValueSlider*>("surfaceCornerX");QVERIFY(x);const auto original=canvas.brush.surface;
        x->setValue(600);QCOMPARE(x->value(),0.);QCOMPARE(canvas.brush.surface,original);
        enter(x,"600");QCOMPARE(x->value(),0.);QCOMPARE(x->findChild<QDoubleSpinBox*>()->value(),0.);QCOMPARE(canvas.brush.surface,original);
        enter(x,"32");QCOMPARE(canvas.brush.surface.quad[0].x(),32.);QCOMPARE(x->value(),32.);
        const auto valid=canvas.brush.surface;enter(x,"512");QCOMPARE(canvas.brush.surface,valid);QCOMPARE(x->value(),32.);
        QCOMPARE(canvas.document()->history()->count(),0);
    }
    void fitUsesTheArtworkFrameAndPreservesPatternScaleWithoutHistory(){
        auto project=Project::create();project.width=1536;project.height=864;
        auto &sheet=project.shots[0].sheet;sheet.left=81;sheet.right=49;sheet.top=75;sheet.bottom=36;sheet.header=180;sheet.overscan=64;
        project.shots[0].camera2D.position={120,80};project.shots[0].camera2D.zoom=1.2;project.shots[0].camera2D.rotation=12;
        Canvas canvas;canvas.resize(900,700);canvas.setProject(project);canvas.setCameraView(false);auto brush=canvas.brush;
        brush.surface.kind=BrushSurfaceKind::Dots;brush.surface.perspective=true;brush.surface.planeSize={720,360};brush.surface.tileSize={120,90};brush.surface.phase={17,-23};brush.surface.angle=37;canvas.setBrushSettings(brush);
        BrushSettingsPanel panel(&canvas);openSurface(panel);auto *fit=panel.findChild<QToolButton*>("surfaceFitArtwork");QVERIFY(fit);QVERIFY(fit->isVisible());QVERIFY(fit->isEnabled());
        const auto geometry=sheetGeometry(canvas.project(),canvas.shot());QVERIFY(geometry.output.topLeft()!=QPointF());
        QPolygonF expected;for(const auto &point:QPolygonF(geometry.output))expected.append(canvas.artworkPoint(canvas.viewTransform().map(point)));
        const auto before=canvas.brush.surface;fit->click();auto changed=canvas.brush.surface;
        for(int i=0;i<4;++i)QVERIFY(QLineF(changed.quad[i],expected[i]).length()<1e-6);
        QVERIFY(QLineF(changed.quad[0],QPointF(0,0)).length()<1e-6);QVERIFY(QLineF(changed.quad[2],QPointF(project.width,project.height)).length()<1e-6);
        changed.quad=before.quad;QCOMPARE(changed,before);QCOMPARE(canvas.document()->history()->count(),0);
        canvas.setCameraView(true);canvas.setRotation(23);canvas.setZoom(.7);
        QPolygonF cameraExpected;for(const auto &point:QPolygonF(geometry.output))cameraExpected.append(canvas.artworkPoint(canvas.viewTransform().map(point)));
        QVERIFY(QLineF(cameraExpected[0],expected[0]).length()>1);const auto beforeCamera=canvas.brush.surface;fit->click();changed=canvas.brush.surface;
        for(int i=0;i<4;++i)QVERIFY(QLineF(changed.quad[i],cameraExpected[i]).length()<1e-6);
        changed.quad=beforeCamera.quad;QCOMPARE(changed,beforeCamera);QCOMPARE(canvas.document()->history()->count(),0);
        auto *perspective=panel.findChild<QCheckBox*>("surfacePerspective");QVERIFY(perspective);perspective->setChecked(false);QVERIFY(!fit->isVisible());
    }
    void closeAndToolStateChangesDiscardOnlyTheUnfinishedImageDraft(){
        Canvas canvas;canvas.setProject(Project::create());BrushSettingsPanel panel(&canvas);openSurface(panel);
        auto *surface=panel.findChild<BrushSurfacePatternWidget*>();auto *kind=surface->findChild<QTabBar*>("surfaceKind");auto *width=surface->findChild<ValueSlider*>("surfaceTileWidth");auto *dialog=panel.findChild<QDialog*>("advancedBrushDialog");QVERIFY(kind&&width&&dialog);
        kind->setCurrentIndex(int(BrushSurfaceKind::Image));width->setValue(333);dialog->reject();panel.openAdvancedSettings();QCOMPARE(kind->currentIndex(),int(BrushSurfaceKind::Off));QCOMPARE(width->value(),128.);
        kind->setCurrentIndex(int(BrushSurfaceKind::Image));width->setValue(444);canvas.notifyToolStateApplied();QCOMPARE(kind->currentIndex(),int(BrushSurfaceKind::Off));QCOMPARE(width->value(),128.);QCOMPARE(canvas.brush.surface.kind,BrushSurfaceKind::Off);
        kind->setCurrentIndex(int(BrushSurfaceKind::Image));kind->setCurrentIndex(int(BrushSurfaceKind::Dots));QCOMPARE(canvas.brush.surface.kind,BrushSurfaceKind::Dots);dialog->reject();panel.openAdvancedSettings();QCOMPARE(kind->currentIndex(),int(BrushSurfaceKind::Dots));
        QCOMPARE(canvas.document()->history()->count(),0);
    }
};
QTEST_MAIN(SurfaceIntegrationTests)
#include "test_surface_integration.moc"
