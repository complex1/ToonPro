#include "abr_import.h"
#include "brush_library.h"
#include "standard_brushes.h"
#include "catalog_brushes.h"
#include "brush_panel.h"
#include "brush_settings_panel.h"
#include "brush_engine.h"
#include "canvas.h"
#include "drawing_panels.h"
#include "main_window.h"
#include "tool_catalog.h"
#include "value_slider.h"
#include <QDir>
#include <QDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QtTest>
#include <cmath>
#include <memory>

using namespace toon;
static QString fixturePath() { return QString(TOON_TEST_SOURCE_DIR) + "/fixtures/abr/sampled-v2.abr"; }
static QByteArray bytes(const QString &path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
class BrushPanelTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void selectEditSaveAndReload() {
        QTemporaryDir temp; QString error;
        BrushLibrary library(temp.filePath("brushes.json")); QVERIFY(library.load(&error));
        Canvas canvas; canvas.setProject(Project::create()); canvas.setBrushColor(QColor("#b54e55"));
        BrushPanel panel(&canvas, &library);
        std::unique_ptr<QWidget> properties(drawingPanel(&canvas, "propertiesPanel"));
        auto *list = panel.findChild<QListWidget *>("brushPresetList"); QCOMPARE(list->count(), 3 + standardBrushPresets().size() + catalogBrushPresets().size());
        list->setCurrentRow(1);
        const auto preset = library.presets()[1];
        QCOMPARE(canvas.brush.size, preset.brush.size); QCOMPARE(canvas.brush.opacity, preset.brush.opacity);
        QCOMPARE(canvas.stabilization(), preset.smoothing); QCOMPARE(canvas.brush.color, QColor("#b54e55"));
        QCOMPARE(properties->findChild<ValueSlider *>("quickBrushFlow")->value(), preset.brush.opacity * 100);
        QVERIFY(!panel.findChild<QToolButton *>("deleteBrushPreset")->isEnabled());
        properties->show();
        auto *settings = properties->findChild<BrushSettingsPanel *>(); QVERIFY(settings);
        settings->openAdvancedSettings();
        auto *advanced = settings->findChild<QDialog *>("advancedBrushDialog"); QVERIFY(advanced);
        auto *spacing = properties->findChild<ValueSlider *>("brushSpacing");
        spacing->setValue(35); QCOMPARE(canvas.brush.spacing, .35);
        QVERIFY(panel.findChild<QLabel *>("currentBrushPreset")->text().contains("Modified"));
        advanced->close();
        panel.resize(280, 480); panel.show(); QTest::qWait(10);
        canvas.setTool(Canvas::Tool::Eraser);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->currentItem()).center());
        QCOMPARE(canvas.brush.spacing, preset.brush.spacing); QCOMPARE(canvas.tool(), Canvas::Tool::Brush);
        QVERIFY(!panel.findChild<QLabel *>("currentBrushPreset")->text().contains("Modified"));
        settings->openAdvancedSettings();
        spacing->setValue(35);
        advanced->close();
        QVERIFY(panel.saveCurrent("My sketch", &error));
        QCOMPARE(list->count(), 4 + standardBrushPresets().size() + catalogBrushPresets().size()); QVERIFY(canvas.document()->history()->isClean());
        QVERIFY(panel.findChild<QToolButton *>("deleteBrushPreset")->isEnabled());
        BrushLibrary reopened(library.path()); QVERIFY(reopened.load(&error));
        QCOMPARE(reopened.presets().last().name, QString("My sketch"));
        QCOMPARE(reopened.presets().last().brush.spacing, .35);
        QCOMPARE(reopened.presets().last().brush.color, BrushSettings{}.color);
    }
    void importPersistsPixelsAndReportAndRejectsDamageAtomically() {
        QTemporaryDir temp; QString error;
        BrushLibrary library(temp.filePath("brushes.json")); QVERIFY(library.load(&error));
        Canvas canvas; canvas.setProject(Project::create()); BrushPanel panel(&canvas, &library);
        const auto source = temp.filePath("sampled.abr"); QVERIFY(QFile::copy(fixturePath(), source));
        QVERIFY2(panel.importAbrFile(source, &error), qPrintable(error));
        QCOMPARE(library.presets().size(), 4 + standardBrushPresets().size() + catalogBrushPresets().size()); QCOMPARE(canvas.brush.tipMask.size(), QSize(5, 3));
        QVERIFY(panel.lastImportReport().contains("Test wedge"));
        QVERIFY(panel.lastImportReport().contains("not mapped"));
        QVERIFY(QFile::remove(source));
        canvas.setBrushSize(50); canvas.setBrushSpacing(.8); canvas.setPressureSize(false);
        QVERIFY(panel.saveCurrent("Wedge stamp", &error));
        const auto custom = library.presets().last();
        QVERIFY(custom.source.contains("sampled.abr")); QVERIFY(!custom.compatibility.isEmpty());
        BrushLibrary reopened(library.path()); QVERIFY(reopened.load(&error));
        QCOMPARE(reopened.presets().last().brush.tipMask, canvas.brush.tipMask);
        QCOMPARE(reopened.presets().last().brush.spacing, .8);
        const auto fileBefore = bytes(library.path()); const auto countBefore = library.presets().size();
        QFile broken(source); QVERIFY(broken.open(QIODevice::WriteOnly)); broken.write("bad"); broken.close();
        QVERIFY(!panel.importAbrFile(source, &error));
        QCOMPARE(bytes(library.path()), fileBefore); QCOMPARE(library.presets().size(), countBefore);
        auto *doc = canvas.document(); QVERIFY(doc->beginStroke({500, 300}, 1, canvas.brush, false)); doc->endStroke();
        QVERIFY(!doc->project().shots.first().layers.first().tiles.isEmpty());
        const auto picture = renderMovieFrame(doc->project());
        QVERIFY(picture.pixelColor(500, 300) != QColor(Qt::white));
        const auto projectPath = temp.filePath("artwork.toonproj"); QVERIFY(saveProject(doc->project(), projectPath, &error));
        QVERIFY(QFile::remove(library.path())); const auto loaded = loadProject(projectPath, &error); QVERIFY(loaded);
        QCOMPARE(renderMovieFrame(*loaded), picture);
    }
    void cursorFollowsSampledTipAndBrushSwitchFinishesStroke() {
        Canvas canvas; canvas.resize(1000, 800); canvas.show(); canvas.setProject(Project::create());
        canvas.setZoom(1); auto imported = importAbr(fixturePath()); QVERIFY(imported.error.isEmpty());
        BrushSettings brush; brush.size = 50; brush.pressureSize = false; brush.tipMask = imported.brushes.first().mask;
        canvas.applyBrushSettings(brush, 20);
        const QPointF pointer(500, 400);
        QMouseEvent hover(QEvent::MouseMove, pointer, canvas.mapToGlobal(pointer), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &hover);
        const auto footprint = canvas.brushCursorRect();
        QVERIFY(footprint.width() <= 52 && footprint.width() >= 30);
        QVERIFY(footprint.height() <= 32 && footprint.height() >= 20);
        QVERIFY(footprint.width() > footprint.height());
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, pointer.toPoint());
        QTest::mouseMove(&canvas, pointer.toPoint() + QPoint(50, 0));
        canvas.applyBrushSettings(BrushSettings{}, 0);
        QVERIFY(!canvas.document()->strokeActive()); QCOMPARE(canvas.document()->history()->count(), 1);
        QVERIFY(canvas.brush.tipMask.isNull());
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, pointer.toPoint() + QPoint(50, 0));
        QCOMPARE(canvas.document()->history()->count(), 1);
    }
    void workspacePreview() {
        QTemporaryDir temp; QString error; const auto path = temp.filePath("brush-study.toonproj");
        auto project = Project::create(); project.movie = "Brush study"; QVERIFY(saveProject(project, path, &error));
        MainWindow window(temp.filePath("workspace.ini")); window.resize(1380, 960); window.show(); QVERIFY(window.openProject(path));
        QVERIFY(!window.findChild<BrushPanel *>());
        auto *library = window.findChild<BrushLibrary *>(); QVERIFY(library);
        QString report; QStringList importedIds;
        QVERIFY2(importAbrIntoLibrary(library, fixturePath(), &report, &error, &importedIds), qPrintable(error));
        QVERIFY(report.contains("Test wedge")); QVERIFY(!importedIds.isEmpty());
        auto *catalog = window.toolCatalog(); QString importedTool;
        for (const auto &tool : catalog->tools()) if (tool.brushPresetId == importedIds.first()) { importedTool = tool.id; break; }
        QVERIFY(!importedTool.isEmpty()); QVERIFY(catalog->activateTool(importedTool));
        auto *canvas = window.canvas(); canvas->setBrushSize(56); canvas->setBrushSpacing(.8); canvas->setPressureSize(false);
        canvas->setBrushColor(QColor("#497fc0"));
        auto *doc = canvas->document();
        QVERIFY(doc->beginStroke({400, 500}, 1, canvas->brush, false)); doc->continueStroke({1400, 500}, 1); doc->endStroke();
        QVERIFY(!catalog->duplicateTool(importedTool, catalog->selectedEntryId(), "Wedge pattern").isEmpty());
        auto *column = window.findChild<QToolBar *>("toolboxPanel"); QVERIFY(column); column->show();
        for (const auto *id : {"toolsPanel", "propertiesPanel", "layersPanel", "controlPanel"})
            window.findChild<QDockWidget *>(id)->show();
        window.findChild<QDockWidget *>("propertiesPanel")->raise(); window.findChild<QDockWidget *>("layersPanel")->raise();
        auto *list = window.findChild<QListWidget *>("toolEntries"); QVERIFY(list && list->currentItem());
        QVERIFY(!list->currentItem()->icon().isNull()); QCOMPARE(list->currentItem()->text(), QString("Wedge pattern"));
        QTest::qWait(40); canvas->fitSheet();
        QDir().mkpath("test-artifacts"); QVERIFY(window.grab().save("test-artifacts/brush-library.png"));
        doc->history()->setClean(); window.close();
    }
    void advancedBrushStudy() {
        BrushSettings base; base.color = QColor("#29548c"); base.size = 28; base.spacing = .14;
        QVector<QPair<QString, BrushSettings>> examples;
        examples.append({"Round ink · pressure size", base});
        auto b = base; b.strokeOpacity = .3; b.opacity = .2; examples.append({"Opacity 30% · flow 20%", b});
        b = base; b.hardness = .05; examples.append({"Soft round · hardness 5%", b});
        b = base; b.roundness = .2; b.angle = -35; examples.append({"Flat tip · angle −35°", b});
        b = base; b.pressureSize = false; b.sizeDynamics.control = BrushControl::Fade; b.sizeDynamics.fade = 35;
        examples.append({"Size fade · 35 stamps", b});
        b = base; b.size = 12; b.scatter = .7; b.scatterBothAxes = true; b.count = 3; b.spacing = .5;
        examples.append({"Scatter · three stamps per step", b});
        b = base; b.texture.enabled = true; b.texture.pattern = QImage(16, 16, QImage::Format_Grayscale8);
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) b.texture.pattern.scanLine(y)[x] = (x % 4 && y % 4) ? 255 : 0;
        examples.append({"Canvas texture · artwork anchored", b});
        b = base; b.dual.enabled = true; b.dual.size = 10; b.dual.count = 3; b.dual.scatter = .7; b.dual.spacing = .4;
        examples.append({"Dual brush · secondary round mask", b});
        b = base; b.colorDynamics.enabled = true; b.colorDynamics.hue = 1; b.colorDynamics.perTip = true; b.spacing = .5;
        examples.append({"Color dynamics · hue per tip", b});
        b = base; b.wetEdges = true; b.opacity = .3; b.spacing = .4; examples.append({"Wet edges · native rim effect", b});
        b = base; b.noise = true; b.hardness = .5; b.opacity = .3; b.spacing = .4; examples.append({"Noise · soft coverage grain", b});
        b = base; b.color = QColor("#bc7049"); b.blendMode = BrushBlend::Multiply; examples.append({"Multiply · stroke blend", b});
        QImage study(1100, 738, QImage::Format_ARGB32_Premultiplied); study.fill(QColor("#222630"));
        QPainter painter(&study); painter.setPen(QColor("#e6eaf0")); painter.setFont(QFont("Arial", 16, QFont::Bold));
        painter.drawText(20, 32, "Toon Pro 0.5 · Native brush study"); painter.setFont(QFont("Arial", 11));
        for (int i = 0; i < examples.size(); ++i) {
            const QPoint origin(20 + (i % 2) * 540, 65 + (i / 2) * 110);
            painter.drawText(origin, examples[i].first);
            const auto preview = brushStrokePreview(examples[i].second, {520, 80}, QColor("#c9d2da"));
            QVERIFY(!preview.isNull()); painter.drawImage(origin + QPoint(0, 9), preview);
        }
        painter.end(); QDir().mkpath("test-artifacts"); QVERIFY(study.save("test-artifacts/brush-engine-study.png"));
    }
};
QTEST_MAIN(BrushPanelTest)
#include "test_brush_panel.moc"
