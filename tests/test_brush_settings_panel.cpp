#include "brush_settings_panel.h"
#include "brush_widgets.h"
#include "brush_library.h"
#include "standard_brushes.h"
#include "catalog_brushes.h"
#include "canvas.h"
#include "main_window.h"
#include "value_slider.h"
#include "property_row.h"
#include "tool_session.h"
#include "icons.h"
#include "ui_metrics.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStyle>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTabBar>
#include <QToolButton>
#include <QListWidget>
#include <QLineEdit>
#include <QLabel>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QtTest>
#include <functional>

using namespace toon;
static int builtinTipCount(bool includeColored = true) {
    int count = 0;
    for (const auto &preset : standardBrushPresets())
        if (!preset.brush.tipMask.isNull() || (includeColored && !preset.brush.colorTip.isNull())) ++count;
    for (const auto &preset : catalogBrushPresets())
        if (!preset.brush.tipMask.isNull() || (includeColored && !preset.brush.colorTip.isNull())) ++count;
    return count;
}
static QDoubleSpinBox *number(QWidget &panel, const char *name) {
    auto *control = panel.findChild<ValueSlider *>(name);
    return control ? control->findChild<QDoubleSpinBox *>() : nullptr;
}
class BrushSettingsPanelTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void settingsEditActualBrushAndKeepOtherValues() {
        Canvas canvas; canvas.setProject(Project::create()); BrushSettingsPanel panel(&canvas);
        panel.show(); panel.openAdvancedSettings();
        struct NumberCase { const char *id; qreal value; qreal scale; std::function<qreal(const BrushSettings &)> read; };
#define NUMBER(id, value, scale, field) {id, value, scale, [](const BrushSettings &b) { return qreal(b.field); }}
        const NumberCase cases[] = {
            NUMBER("brushSize", 73.5, 1, size), NUMBER("brushHardness", 42, 100, hardness),
            NUMBER("brushStrokeOpacity", 63, 100, strokeOpacity), NUMBER("brushFlow", 24, 100, opacity),
            NUMBER("brushSpacing", 138, 100, spacing), NUMBER("brushAirbrushRate", 33, 1, airbrushRate),
            NUMBER("brushAngle", -41, 1, angle), NUMBER("brushRoundness", 53, 100, roundness),
            NUMBER("sizeJitter", 61, 100, sizeDynamics.jitter), NUMBER("sizeMinimum", 31, 100, sizeDynamics.minimum),
            NUMBER("sizeFade", 87, 1, sizeDynamics.fade), NUMBER("brushTiltScale", 217, 100, tiltScale),
            NUMBER("angleJitter", 35, 100, angleDynamics.jitter), NUMBER("angleFade", 68, 1, angleDynamics.fade),
            NUMBER("roundnessJitter", 21, 100, roundnessDynamics.jitter), NUMBER("roundnessMinimum", 28, 100, roundnessDynamics.minimum),
            NUMBER("roundnessFade", 98, 1, roundnessDynamics.fade), NUMBER("brushScatter", 218, 100, scatter),
            NUMBER("brushScatterFade", 94, 1, scatterFade), NUMBER("brushCount", 4, 1, count),
            NUMBER("brushCountJitter", 56, 100, countJitter), NUMBER("brushCountFade", 32, 1, countFade),
            NUMBER("brushTextureScale", 125, 100, texture.scale), NUMBER("brushTextureBrightness", 25, 100, texture.brightness),
            NUMBER("brushTextureContrast", -32, 100, texture.contrast), NUMBER("brushTextureDepth", 65, 100, texture.depth),
            NUMBER("textureDepthJitter", 12, 100, texture.dynamics.jitter), NUMBER("textureDepthMinimum", 17, 100, texture.dynamics.minimum),
            NUMBER("textureDepthFade", 38, 1, texture.dynamics.fade), NUMBER("brushDualSize", 44.5, 1, dual.size),
            NUMBER("brushDualSpacing", 74, 100, dual.spacing), NUMBER("brushDualScatter", 130, 100, dual.scatter),
            NUMBER("brushDualCount", 3, 1, dual.count), NUMBER("brushForegroundBackground", 53, 100, colorDynamics.foregroundBackground),
            NUMBER("brushColorFade", 40, 1, colorDynamics.fade), NUMBER("brushHueJitter", 27, 100, colorDynamics.hue),
            NUMBER("brushSaturationJitter", 18, 100, colorDynamics.saturation), NUMBER("brushBrightnessJitter", 36, 100, colorDynamics.brightness),
            NUMBER("brushColorPurity", -23, 100, colorDynamics.purity), NUMBER("opacityJitter", 21, 100, opacityDynamics.jitter),
            NUMBER("opacityMinimum", 33, 100, opacityDynamics.minimum), NUMBER("opacityFade", 97, 1, opacityDynamics.fade),
            NUMBER("flowJitter", 43, 100, flowDynamics.jitter), NUMBER("flowMinimum", 19, 100, flowDynamics.minimum),
            NUMBER("flowFade", 67, 1, flowDynamics.fade), NUMBER("brushPoseTiltX", 37, 1, pose.tiltX),
            NUMBER("brushPoseTiltY", -17, 1, pose.tiltY), NUMBER("brushPoseRotation", 113, 1, pose.rotation),
            NUMBER("brushPosePressure", 41, 100, pose.pressure), NUMBER("brushSymmetryAngle", 38, 1, symmetry.angle),
            NUMBER("brushSymmetryCount", 9, 1, symmetry.count), NUMBER("brushSymmetryRadius", 381, 1, symmetry.radius),
            NUMBER("brushSymmetryDistance", 672, 1, symmetry.distance), NUMBER("brushSymmetryAmplitude", 41, 1, symmetry.amplitude),
            NUMBER("brushSymmetryWavelength", 235, 1, symmetry.wavelength)
        };
#undef NUMBER
        for (const auto &entry : cases) {
            auto *control = number(panel, entry.id); QVERIFY2(control, entry.id);
            control->setValue(entry.value);
            QVERIFY2(qAbs(entry.read(canvas.brush) - entry.value / entry.scale) < 1e-9, entry.id);
        }
        // Edits in other categories retain the already-entered general values.
        QCOMPARE(canvas.brush.size, 73.5); QCOMPARE(canvas.brush.opacity, .24); QCOMPARE(canvas.brush.strokeOpacity, .63);
        auto *spacing = number(panel, "brushTipSpacing"); QCOMPARE(spacing->value(), 138);
        spacing->setValue(234); QCOMPARE(canvas.brush.spacing, 2.34);
        QCOMPARE(number(panel, "brushSpacing")->value(), 234);
        number(panel, "brushSymmetryX")->setValue(32.5);
        number(panel, "brushSymmetryY")->setValue(74.5);
        QCOMPARE(canvas.brush.symmetry.center, QPointF(.325, .745));
        panel.findChild<ValueSlider *>("brushStabilization")->setValue(68); QCOMPARE(canvas.stabilization(), 68);
        QVERIFY(canvas.document()->history()->isClean());
    }
    void everyNumericSettingCanSlideAndRemainsLinkedToItsVisualWidget() {
        Canvas canvas; canvas.setProject(Project::create()); BrushSettingsPanel panel(&canvas);
        panel.show(); panel.openAdvancedSettings();
        panel.resize(240, 620); panel.show();
        auto *tabs = panel.findChild<QTabBar *>("brushSettingsCategory"); tabs->setCurrentIndex(1);
        auto *angle = panel.findChild<ValueSlider *>("brushAngle"); QVERIFY(angle);
        auto *track = angle->findChild<QSlider *>(); QVERIFY(track);
        auto *tip = panel.findChild<BrushVisualWidget *>("brushTipVisual"); QVERIFY(tip);
        track->setSliderDown(true); track->setValue(40); track->setValue(75);
        QCOMPARE(canvas.brush.angle, 75.); QCOMPARE(tip->brush().angle, 75.);
        track->setSliderDown(false);
        QTest::keyClick(tip, Qt::Key_Right);
        QCOMPARE(angle->value(), 76.); QCOMPARE(track->value(), 76);
        auto *field = angle->findChild<QDoubleSpinBox *>(); QVERIFY(field);
        field->setValue(-31); QCOMPARE(canvas.brush.angle, -31.); QCOMPARE(tip->brush().angle, -31.);
        for (auto *number : panel.findChildren<QDoubleSpinBox *>()) {
            auto *control = qobject_cast<ValueSlider *>(number->parentWidget()); QVERIFY(control);
            QVERIFY(control->findChild<QSlider *>());
        }
        QVERIFY(panel.findChildren<ValueSlider *>().size() > 50);
        QCOMPARE(canvas.document()->history()->count(), 0);
    }

    void dynamicsSelectorsTogglesAndPresetRefresh() {
        Canvas canvas; canvas.setProject(Project::create()); BrushSettingsPanel panel(&canvas);
        panel.show(); panel.openAdvancedSettings();
        struct ChoiceCase { const char *id; int index; std::function<int(const BrushSettings &)> read; };
#define CHOICE(id, value, field) {id, value, [](const BrushSettings &b) { return int(b.field); }}
        const ChoiceCase choices[] = {
            CHOICE("brushBlendMode", 18, blendMode), CHOICE("sizeControl", 2, sizeDynamics.control),
            CHOICE("angleControl", 6, angleDynamics.control), CHOICE("roundnessControl", 4, roundnessDynamics.control),
            CHOICE("brushScatterControl", 3, scatterControl), CHOICE("brushCountControl", 4, countControl),
            CHOICE("brushTextureMode", 5, texture.mode), CHOICE("textureDepthControl", 2, texture.dynamics.control),
            CHOICE("brushDualMode", 4, dual.mode), CHOICE("brushColorControl", 1, colorDynamics.control),
            CHOICE("opacityControl", 3, opacityDynamics.control), CHOICE("flowControl", 2, flowDynamics.control)
        };
#undef CHOICE
        for (const auto &entry : choices) {
            auto *control = panel.findChild<QComboBox *>(entry.id); QVERIFY2(control, entry.id);
            control->setCurrentIndex(entry.index); QCOMPARE(entry.read(canvas.brush), entry.index);
        }
        panel.findChild<QToolButton *>("brushSymmetryChoice9")->click();
        QCOMPARE(canvas.brush.symmetry.mode, SymmetryMode::Radial);
        QVERIFY(!canvas.brush.pressureSize);
        auto *pressure = panel.findChild<QCheckBox *>("brushPressureSize"); QVERIFY(!pressure->isChecked());
        pressure->setChecked(true); QCOMPARE(canvas.brush.sizeDynamics.control, BrushControl::Pressure);
        QCOMPARE(panel.findChild<QComboBox *>("sizeControl")->currentIndex(), 1);
        pressure->setChecked(false); QCOMPARE(canvas.brush.sizeDynamics.control, BrushControl::Off);
        struct CheckCase { const char *id; bool value; std::function<bool(const BrushSettings &)> read; };
#define CHECK(id, value, field) {id, value, [](const BrushSettings &b) { return b.field; }}
        const CheckCase checks[] = {
            CHECK("brushAirbrush", true, airbrush), CHECK("brushFlipX", true, flipX), CHECK("brushFlipY", true, flipY),
            CHECK("brushProjection", true, projection), CHECK("brushScatterBothAxes", true, scatterBothAxes),
            CHECK("brushTextureEnabled", true, texture.enabled), CHECK("brushTextureInvert", true, texture.invert),
            CHECK("brushTextureEachTip", true, texture.eachTip), CHECK("brushDualEnabled", true, dual.enabled),
            CHECK("brushColorDynamicsEnabled", true, colorDynamics.enabled), CHECK("brushColorPerTip", false, colorDynamics.perTip),
            CHECK("brushOverrideTiltX", true, pose.overrideTiltX), CHECK("brushOverrideTiltY", true, pose.overrideTiltY),
            CHECK("brushOverrideRotation", true, pose.overrideRotation), CHECK("brushOverridePressure", true, pose.overridePressure),
            CHECK("brushNoise", true, noise), CHECK("brushWetEdges", true, wetEdges), CHECK("brushSmoothingEnabled", false, smoothingEnabled),
            CHECK("brushProtectTexture", true, protectTexture)
        };
#undef CHECK
        for (const auto &entry : checks) {
            auto *control = panel.findChild<QCheckBox *>(entry.id); QVERIFY2(control, entry.id);
            control->setChecked(entry.value); QCOMPARE(entry.read(canvas.brush), entry.value);
        }
        QVERIFY(!canvas.brush.texture.pattern.isNull());
        QVERIFY(panel.findChild<QCheckBox *>("brushBuildUp")->isChecked());
        panel.findChild<QCheckBox *>("brushBuildUp")->setChecked(false);
        QVERIFY(!panel.findChild<QCheckBox *>("brushAirbrush")->isChecked());
        QVERIFY(!panel.findChild<ValueSlider *>("brushStabilization")->isEnabled());
        BrushSettings reset; reset.color = canvas.brush.color; reset.backgroundColor = QColor("#b54e55");
        canvas.setBrushSettings(reset);
        QCOMPARE(panel.findChild<ValueSlider *>("brushHardness")->value(), 100);
        QVERIFY(panel.findChild<QToolButton *>("brushSymmetryChoice0")->isChecked());
        QCOMPARE(panel.findChild<QPushButton *>("brushBackgroundColor")->toolTip(), QString("Background #B54E55"));
        QVERIFY(panel.findChild<ValueSlider *>("brushStabilization")->isEnabled());
        QVERIFY(!panel.findChild<QCheckBox *>("brushNoise")->isChecked());
        QCOMPARE(canvas.document()->history()->count(), 0);
    }
    void sampledPickersUseLibraryAndKeepPrimarySettings() {
        QTemporaryDir temp; BrushLibrary library(temp.filePath("brushes.json")); QString error; QVERIFY(library.load(&error));
        BrushPreset preset; preset.name = "Leaf"; preset.brush.tipMask = QImage(5, 3, QImage::Format_Grayscale8); preset.brush.tipMask.fill(160);
        QVERIFY(library.addPresets({preset}, &error));
        Canvas canvas; canvas.setProject(Project::create()); canvas.setBrushSize(82);
        BrushSettingsPanel panel(&canvas); panel.setLibrary(&library);
        panel.show(); panel.openAdvancedSettings();
        auto *primary = panel.findChild<QComboBox *>("brushTipPicker"); auto *dual = panel.findChild<QComboBox *>("brushDualTip");
        QCOMPARE(primary->count(), 2 + builtinTipCount()); QCOMPARE(dual->count(), 2 + builtinTipCount(false));
        QVERIFY(primary->findText("Leaf") >= 0); QVERIFY(dual->findText("Leaf") >= 0);
        primary->setCurrentIndex(primary->findText("Leaf")); QCOMPARE(canvas.brush.tipMask, preset.brush.tipMask); QCOMPARE(canvas.brush.size, 82);
        dual->setCurrentIndex(dual->findText("Leaf")); QCOMPARE(canvas.brush.dual.tipMask, preset.brush.tipMask); QCOMPARE(canvas.brush.dual.name, QString("Leaf"));
        primary->setCurrentIndex(0); QVERIFY(canvas.brush.tipMask.isNull()); QVERIFY(!canvas.brush.dual.tipMask.isNull());
        const auto id = library.presets().last().id; QVERIFY(library.removePreset(id, &error));
        QCOMPARE(dual->currentText(), QString("Leaf")); // Already chosen pixels survive removal from the picker library.
        QCOMPARE(canvas.brush.dual.tipMask, preset.brush.tipMask);
        dual->setCurrentIndex(0); QVERIFY(canvas.brush.dual.tipMask.isNull());
    }
    void patternsImportWithAlphaAndRejectInvalidInput() {
        QTemporaryDir temp; Canvas canvas; canvas.setProject(Project::create()); BrushSettingsPanel panel(&canvas);
        panel.show(); panel.openAdvancedSettings();
        auto *picker = panel.findChild<QListWidget *>("brushTexturePattern"); QCOMPARE(picker->count(), 4);
        picker->setCurrentRow(2); QCOMPARE(canvas.brush.texture.name, QString("Checker")); QVERIFY(canvas.brush.texture.enabled);
        QVERIFY(canvas.brush.texture.pattern.pixelColor(0, 0) != canvas.brush.texture.pattern.pixelColor(8, 0));
        QImage image(3, 1, QImage::Format_ARGB32); image.setPixelColor(0, 0, Qt::white);
        image.setPixelColor(1, 0, QColor(255, 255, 255, 128)); image.setPixelColor(2, 0, QColor(255, 255, 255, 0));
        const auto path = temp.filePath("grain.png"); QVERIFY(image.save(path)); QString error;
        QVERIFY2(panel.importTexture(path, &error), qPrintable(error));
        QCOMPARE(canvas.brush.texture.name, QString("grain")); QCOMPARE(canvas.brush.texture.pattern.format(), QImage::Format_Grayscale8);
        QCOMPARE(canvas.brush.texture.pattern.constScanLine(0)[0], uchar(255));
        QCOMPARE(canvas.brush.texture.pattern.constScanLine(0)[1], uchar(128));
        QCOMPARE(canvas.brush.texture.pattern.constScanLine(0)[2], uchar(0));
        const auto before = canvas.brush;
        QImage oversized(2049, 1, QImage::Format_Grayscale8); oversized.fill(255); QVERIFY(oversized.save(temp.filePath("large.png")));
        QVERIFY(!panel.importTexture(temp.filePath("large.png"), &error)); QVERIFY(error.contains("2048")); QVERIFY(canvas.brush == before);
        QFile invalid(temp.filePath("invalid.png")); QVERIFY(invalid.open(QIODevice::WriteOnly)); invalid.write("not an image"); invalid.close();
        QVERIFY(!panel.importTexture(invalid.fileName(), &error)); QVERIFY(canvas.brush == before);
        QVERIFY(!panel.importTexture(temp.filePath("missing.png"), &error)); QVERIFY(canvas.brush == before);
    }
    void compactDockContainsOnlyDrawingControlsAndWrench() {
        Canvas canvas; canvas.setProject(Project::create()); BrushSettingsPanel panel(&canvas);
        panel.resize(240, 540); panel.show(); QCoreApplication::processEvents();
        auto *dialog = panel.findChild<QDialog *>("advancedBrushDialog"); QVERIFY(dialog); QVERIFY(!dialog->isVisible());
        const QStringList quickIds{"quickBrushSize", "quickBrushStrokeOpacity", "quickBrushFlow", "quickBrushHardness", "quickBrushStabilization"};
        int visibleNumbers = 0;
        for (auto *control : panel.findChildren<ValueSlider *>()) {
            if (!control->isVisible()) continue;
            QVERIFY2(quickIds.contains(control->objectName()), qPrintable(control->objectName()));
            ++visibleNumbers;
        }
        QCOMPARE(visibleNumbers, quickIds.size());
        auto *pressure = panel.findChild<QToolButton *>("quickBrushPressureSize"); QVERIFY(pressure);
        QVERIFY(pressure->isVisible()); QVERIFY(pressure->isCheckable());
        auto *wrench = panel.findChild<QToolButton *>("advancedBrushSettings"); QVERIFY(wrench);
        QVERIFY(wrench->isVisible()); QVERIFY(!wrench->icon().isNull()); QVERIFY(wrench->text().isEmpty());
        QVERIFY(!wrench->toolTip().isEmpty()); QVERIFY(!wrench->accessibleName().isEmpty());
        QCOMPARE(wrench->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay, nullptr, wrench), 600);
        QVERIFY(!pressure->toolTip().isEmpty()); QVERIFY(!pressure->accessibleName().isEmpty());
        QCOMPARE(pressure->icon().name(), featureIconName(FeatureIcon::Pressure));
        for (auto *action : {pressure, wrench, panel.findChild<QToolButton *>("holdShapeToggle")}) {
            QVERIFY(action); QCOMPARE(action->iconSize(), QSize(16, 16));
            QCOMPARE(action->size(), QSize(28, 28));
        }
        for (const auto &id : quickIds) {
            auto *control = panel.findChild<ValueSlider *>(id); QVERIFY(control);
            auto *field = control->findChild<QDoubleSpinBox *>(); QVERIFY(field);
            QCOMPARE(field->width(), 48); QVERIFY(field->suffix().isEmpty());
            const QRect rect(control->mapTo(&panel, QPoint()), control->size());
            QVERIFY2(rect.left() >= 0 && rect.right() < panel.width(), qPrintable(id));
        }
        QTest::mouseClick(wrench, Qt::LeftButton);
        QTRY_VERIFY(dialog->isVisible()); QCOMPARE(dialog->windowModality(), Qt::WindowModal);
        QVERIFY(panel.findChild<QTabBar *>("brushSettingsCategory")->isVisible());
        QCOMPARE(panel.findChildren<QDialog *>("advancedBrushDialog").size(), 1);
        dialog->close();
    }

    void stabilizationModesAreDirectAndAdvancedTuningStaysSynchronized() {
        Canvas canvas; canvas.setProject(Project::create()); BrushSettingsPanel panel(&canvas);
        panel.resize(220, 220); panel.show(); QCoreApplication::processEvents();
        auto *quick = panel.findChild<QTabBar *>("quickBrushStabilizationMode");
        auto *advanced = panel.findChild<QTabBar *>("brushStabilizationMode");
        auto *deadZone = panel.findChild<ValueSlider *>("brushStabilizationDeadZone");
        auto *finish = panel.findChild<QCheckBox *>("brushStabilizationFinish");
        QVERIFY(quick && advanced && deadZone && finish); QCOMPARE(quick->count(), 2);
        QVERIFY(quick->isVisible());
        for (int i = 0; i < quick->count(); ++i) {
            QVERIFY(quick->rect().contains(quick->tabRect(i)));
            QVERIFY(!quick->tabToolTip(i).isEmpty()); QVERIFY(!quick->accessibleTabName(i).isEmpty());
        }
        QCOMPARE(quick->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay, nullptr, quick), 600);
        QTest::mouseClick(quick, Qt::LeftButton, Qt::NoModifier, quick->tabRect(1).center());
        QCOMPARE(canvas.stabilizationMode(), StrokeFilterMode::Stabilizer);
        QCOMPARE(advanced->currentIndex(), 1);
        panel.openAdvancedSettings(); QCoreApplication::processEvents();
        QVERIFY(deadZone->isVisible());
        deadZone->setValue(3.5); finish->setChecked(false);
        QCOMPARE(canvas.stabilizationDeadZone(), 3.5); QVERIFY(!canvas.stabilizationFinishAtEndpoint());
        QVERIFY(deadZone->findChild<QSlider *>()); QCOMPARE(deadZone->findChild<QDoubleSpinBox *>()->width(), 48);
        canvas.setStabilizationMode(StrokeFilterMode::Smooth);
        QCOMPARE(quick->currentIndex(), 0); QCOMPARE(advanced->currentIndex(), 0); QVERIFY(!deadZone->isVisible());
        auto settings = canvas.brush; settings.smoothingEnabled = false; canvas.setBrushSettings(settings);
        QVERIFY(!quick->isEnabled()); QVERIFY(!advanced->isEnabled()); QVERIFY(!finish->isEnabled());
        QCOMPARE(canvas.document()->history()->count(), 0);
        panel.findChild<QDialog *>("advancedBrushDialog")->close();
    }

    void stabilizationControlPreviews() {
        const auto output = qEnvironmentVariable("TOON_UI_PREVIEW"); if (output.isEmpty()) return;
        QVERIFY(QDir().mkpath(output));
        Canvas canvas; canvas.setProject(Project::create()); canvas.setStabilization(80);
        canvas.setStabilizationMode(StrokeFilterMode::Stabilizer); canvas.setStabilizationDeadZone(2);
        BrushSettingsPanel panel(&canvas); panel.resize(220, 220); panel.show(); QCoreApplication::processEvents();
        QVERIFY(panel.grab().save(QDir(output).filePath("stabilization-quick.png")));
        panel.openAdvancedSettings(); auto *dialog = panel.findChild<QDialog *>("advancedBrushDialog");
        dialog->resize(400, 520); QCoreApplication::processEvents();
        QVERIFY(dialog->grab().save(QDir(output).filePath("stabilization-options.png"))); dialog->close();
    }

    void quickRowsStayInlineAndFitAShortDrawingDock() {
        Canvas canvas; canvas.setProject(Project::create()); BrushSettingsPanel panel(&canvas);
        panel.show();
        const QStringList ids{"quickBrushSize", "quickBrushStrokeOpacity", "quickBrushFlow", "quickBrushHardness", "quickBrushStabilization"};
        for (int width : {220, 240, 320}) {
            panel.resize(width, 200); QTest::qWait(5);
            QCOMPARE(panel.width(), width);
            int previousBottom = -1, controlX = -1;
            for (const auto &id : ids) {
                auto *row = panel.findChild<PropertyRow *>(id + "Row"); QVERIFY(row);
                auto *control = panel.findChild<ValueSlider *>(id); QVERIFY(control);
                auto *track = control->findChild<QSlider *>(); QVERIFY(track);
                auto *field = control->findChild<QDoubleSpinBox *>(); QVERIFY(field);
                QVERIFY2(row->height() <= 32, qPrintable(QString("%1 row %2x%3 field %4x%5 min %6 hint %7 control %8x%9")
                    .arg(id).arg(row->width()).arg(row->height()).arg(field->width()).arg(field->height())
                    .arg(field->minimumHeight()).arg(field->sizeHint().height()).arg(control->width()).arg(control->height())));
                QVERIFY(row->labelWidget()->geometry().intersects(QRect(0, control->y(), row->width(), control->height())));
                QVERIFY(track->width() >= 74); QCOMPARE(field->width(), 48);
                const auto top = control->mapTo(&panel, QPoint());
                QVERIFY(top.y() > previousBottom); previousBottom = top.y() + control->height() - 1;
                if (controlX < 0) controlX = top.x(); else QCOMPARE(top.x(), controlX);
            }
            QVERIFY(previousBottom < panel.height());
            auto *scroll = panel.findChild<QScrollArea *>("quickBrushScroll"); QVERIFY(scroll);
            QCOMPARE(scroll->verticalScrollBar()->maximum(), 0);
        }
        auto *pressure = panel.findChild<QToolButton *>("quickBrushPressureSize");
        auto *size = panel.findChild<ValueSlider *>("quickBrushSize");
        QCOMPARE(pressure->mapTo(&panel, QPoint()).y(), size->mapTo(&panel, QPoint()).y());
        panel.resize(220, 120); QTest::qWait(5);
        QVERIFY(panel.findChild<QToolButton *>("advancedBrushSettings")->isVisible());
        QVERIFY(panel.findChild<QScrollArea *>("quickBrushScroll")->verticalScrollBar()->maximum() > 0);
    }

    void quickControlsAndAdvancedDialogShareLiveSettings() {
        Canvas canvas; canvas.setProject(Project::create()); BrushSettingsPanel panel(&canvas);
        panel.resize(270, 540); panel.show();
        number(panel, "quickBrushSize")->setValue(71.5);
        number(panel, "quickBrushStrokeOpacity")->setValue(64);
        number(panel, "quickBrushFlow")->setValue(39);
        number(panel, "quickBrushHardness")->setValue(83);
        panel.findChild<ValueSlider *>("quickBrushStabilization")->setValue(42);
        auto *pressure = panel.findChild<QToolButton *>("quickBrushPressureSize"); QVERIFY(pressure);
        pressure->setChecked(false); pressure->setChecked(true);
        QCOMPARE(canvas.brush.size, 71.5); QCOMPARE(canvas.brush.strokeOpacity, .64);
        QCOMPARE(canvas.brush.opacity, .39); QCOMPARE(canvas.brush.hardness, .83);
        QCOMPARE(canvas.stabilization(), 42); QCOMPARE(canvas.brush.sizeDynamics.control, BrushControl::Pressure);
        panel.openAdvancedSettings();
        auto *dialog = panel.findChild<QDialog *>("advancedBrushDialog"); QVERIFY(dialog); QTRY_VERIFY(dialog->isVisible());
        QCOMPARE(number(panel, "brushSize")->value(), 71.5);
        QCOMPARE(number(panel, "brushStrokeOpacity")->value(), 64.);
        QCOMPARE(number(panel, "brushFlow")->value(), 39.);
        QCOMPARE(number(panel, "brushHardness")->value(), 83.);
        QCOMPARE(panel.findChild<ValueSlider *>("brushStabilization")->value(), 42.);
        number(panel, "brushSize")->setValue(58);
        number(panel, "brushFlow")->setValue(21);
        number(panel, "brushAngle")->setValue(36);
        panel.findChild<QCheckBox *>("brushPressureSize")->setChecked(false);
        QCOMPARE(number(panel, "quickBrushSize")->value(), 58.);
        QCOMPARE(number(panel, "quickBrushFlow")->value(), 21.); QVERIFY(!pressure->isChecked());
        auto *buttons = dialog->findChild<QDialogButtonBox *>(); QVERIFY(buttons);
        QVERIFY(buttons->button(QDialogButtonBox::Close)); QVERIFY(!buttons->button(QDialogButtonBox::Apply));
        QVERIFY(!buttons->button(QDialogButtonBox::Cancel));
        buttons->button(QDialogButtonBox::Close)->click(); QTRY_VERIFY(!dialog->isVisible());
        QCOMPARE(canvas.brush.angle, 36.); QCOMPARE(canvas.brush.size, 58.);
        auto changed = canvas.brush; changed.size = 96; changed.angle = -42; changed.opacity = .73;
        canvas.setBrushSettings(changed);
        QCOMPARE(number(panel, "quickBrushSize")->value(), 96.);
        QCOMPARE(number(panel, "quickBrushFlow")->value(), 73.);
        panel.openAdvancedSettings(); QTRY_VERIFY(dialog->isVisible());
        QCOMPARE(number(panel, "brushSize")->value(), 96.); QCOMPARE(number(panel, "brushAngle")->value(), -42.);
        QCOMPARE(panel.findChildren<QDialog *>("advancedBrushDialog").size(), 1);
        dialog->close(); QCOMPARE(canvas.brush.angle, -42.);
        QCOMPARE(canvas.document()->history()->count(), 0); QVERIFY(canvas.document()->history()->isClean());
    }

    void preciseEntryAndEscapeKeepCommittedSettings() {
        Canvas canvas; canvas.setProject(Project::create()); BrushSettingsPanel panel(&canvas);
        panel.show(); panel.openAdvancedSettings();
        auto *dialog = panel.findChild<QDialog *>("advancedBrushDialog"); QVERIFY(dialog); QTRY_VERIFY(dialog->isVisible());
        auto *tabs = panel.findChild<QTabBar *>("brushSettingsCategory"); tabs->setCurrentIndex(1);
        auto *angle = number(panel, "brushAngle"); QVERIFY(angle);
        angle->setFocus(); angle->selectAll(); QTest::keyClicks(angle, "47");
        QTest::keyClick(angle, Qt::Key_Return); QCOMPARE(canvas.brush.angle, 47.); QVERIFY(dialog->isVisible());
        angle->selectAll(); QTest::keyClicks(angle, "83"); QTest::keyClick(angle, Qt::Key_Escape);
        QCOMPARE(canvas.brush.angle, 47.); QVERIFY(dialog->isVisible());
        tabs->setFocus(); QTest::keyClick(tabs, Qt::Key_Escape); QTRY_VERIFY(!dialog->isVisible());
        QCOMPARE(canvas.brush.angle, 47.);
        panel.openAdvancedSettings(); QTRY_VERIFY(dialog->isVisible());
        QCOMPARE(tabs->currentIndex(), 1); QCOMPARE(angle->value(), 47.);
        QCOMPARE(canvas.document()->history()->count(), 0); dialog->close();
    }

    void quickAndAdvancedEditsStayWithTheirToolAfterReopen() {
        QTemporaryDir temp; BrushLibrary library(temp.filePath("brushes.json")); QVERIFY(library.load());
        const auto path = temp.filePath("tools.json"); QString brushId, eraserId;
        {
            ToolCatalog catalog(&library, path); QVERIFY(catalog.load());
            for (const auto &tool : catalog.tools()) {
                if (brushId.isEmpty() && tool.engine == Canvas::Tool::Brush) brushId = tool.id;
                if (eraserId.isEmpty() && tool.engine == Canvas::Tool::Eraser) eraserId = tool.id;
            }
            QVERIFY(!brushId.isEmpty()); QVERIFY(!eraserId.isEmpty());
            Canvas canvas; canvas.setProject(Project::create()); ToolSession session(&canvas, &catalog);
            BrushSettingsPanel panel(&canvas, &library); panel.show(); QVERIFY(catalog.activateTool(brushId));
            number(panel, "quickBrushSize")->setValue(79);
            panel.findChild<ValueSlider *>("quickBrushStabilization")->setValue(63);
            panel.openAdvancedSettings(); number(panel, "brushScatter")->setValue(142);
            auto *dialog = panel.findChild<QDialog *>("advancedBrushDialog"); dialog->close();
            QVERIFY(catalog.activateTool(eraserId)); number(panel, "quickBrushSize")->setValue(27);
            QVERIFY(catalog.activateTool(brushId)); QCOMPARE(number(panel, "quickBrushSize")->value(), 79.);
            panel.openAdvancedSettings(); QCOMPARE(number(panel, "brushScatter")->value(), 142.);
            QCOMPARE(panel.findChild<ValueSlider *>("quickBrushStabilization")->value(), 63.);
            dialog->close(); session.flush(); QCOMPARE(canvas.document()->history()->count(), 0);
        }
        ToolCatalog catalog(&library, path); QVERIFY(catalog.load());
        Canvas canvas; canvas.setProject(Project::create()); ToolSession session(&canvas, &catalog);
        BrushSettingsPanel panel(&canvas, &library); panel.show(); QVERIFY(catalog.activateTool(brushId));
        QCOMPARE(number(panel, "quickBrushSize")->value(), 79.); QCOMPARE(canvas.brush.scatter, 1.42);
        panel.openAdvancedSettings(); QCOMPARE(number(panel, "brushScatter")->value(), 142.);
        panel.findChild<QDialog *>("advancedBrushDialog")->close(); QVERIFY(catalog.activateTool(eraserId));
        QCOMPARE(number(panel, "quickBrushSize")->value(), 27.); QCOMPARE(canvas.document()->history()->count(), 0);
    }

    void unavailableContextClosesModalAndHiddenDialogDoesNotStealFocus() {
        QWidget host; auto *layout = new QVBoxLayout(&host);
        auto *canvas = new Canvas(&host); layout->addWidget(canvas);
        auto *panel = new BrushSettingsPanel(canvas, nullptr, &host); layout->addWidget(panel);
        auto *editor = new QLineEdit(&host); layout->addWidget(editor);
        host.resize(480, 740); host.show(); host.activateWindow(); QCoreApplication::processEvents();
        auto *dialog = panel->findChild<QDialog *>("advancedBrushDialog"); QVERIFY(dialog);
        panel->openAdvancedSettings(); QVERIFY(!dialog->isVisible());
        canvas->setProject(Project::create()); panel->openAdvancedSettings(); QTRY_VERIFY(dialog->isVisible());
        QSignalSpy finished(dialog, &QDialog::finished);
        canvas->setTool(Canvas::Tool::RectangleSelect); QTRY_VERIFY(!dialog->isVisible()); QCOMPARE(finished.count(), 1);
        // Cocoa restores the parent window asynchronously after a modal closes.
        // Establish that state before testing that later hidden-dialog refreshes
        // leave an artist's new field focus alone.
        host.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&host));
        editor->setFocus(); QTRY_VERIFY(editor->hasFocus());
        canvas->setTool(Canvas::Tool::Brush); canvas->setTool(Canvas::Tool::Hand);
        canvas->setToolAvailable(false); canvas->setToolAvailable(true);
        QCOMPARE(QApplication::focusWidget(), editor); QCOMPARE(finished.count(), 1);
        canvas->setTool(Canvas::Tool::Brush); panel->openAdvancedSettings(); QTRY_VERIFY(dialog->isVisible());
        canvas->setToolAvailable(false); QTRY_VERIFY(!dialog->isVisible()); QCOMPARE(finished.count(), 2);
        panel->openAdvancedSettings(); QVERIFY(!dialog->isVisible());
        canvas->setToolAvailable(true); canvas->setTool(Canvas::Tool::Eraser);
        panel->openAdvancedSettings(); QTRY_VERIFY(dialog->isVisible()); dialog->close();
        QCOMPARE(canvas->document()->history()->count(), 0);
    }

    void closedDialogRefreshesChangedLibraryTipsAndImportedTextureOnReopen() {
        QTemporaryDir temp; BrushLibrary library(temp.filePath("brushes.json")); QVERIFY(library.load());
        BrushPreset oldPreset; oldPreset.name = "Old tip";
        oldPreset.brush.tipMask = QImage(5, 3, QImage::Format_Grayscale8); oldPreset.brush.tipMask.fill(160);
        QVERIFY(library.addPresets({oldPreset})); const QString oldId = library.presets().last().id;
        Canvas canvas; canvas.setProject(Project::create()); BrushSettingsPanel panel(&canvas, &library);
        panel.show(); panel.openAdvancedSettings();
        auto *dialog = panel.findChild<QDialog *>("advancedBrushDialog"); QVERIFY(dialog);
        auto *tip = panel.findChild<QComboBox *>("brushTipPicker"); QVERIFY(tip); QCOMPARE(tip->count(), 2 + builtinTipCount());
        QVERIFY(tip->findText("Old tip") >= 0);
        tip->setCurrentIndex(tip->findText("Old tip")); QCOMPARE(canvas.brush.tipMask, oldPreset.brush.tipMask); dialog->close();
        QVERIFY(library.removePreset(oldId));
        BrushPreset newPreset; newPreset.name = "New tip";
        newPreset.brush.tipMask = QImage(7, 4, QImage::Format_Grayscale8); newPreset.brush.tipMask.fill(81);
        QVERIFY(library.addPresets({newPreset}));
        auto brush = canvas.brush; brush.tipMask = newPreset.brush.tipMask; canvas.setBrushSettings(brush);
        QImage texture(3, 2, QImage::Format_Grayscale8); texture.fill(117);
        const auto path = temp.filePath("Hidden texture.png"); QVERIFY(texture.save(path));
        QString error; QVERIFY2(panel.importTexture(path, &error), qPrintable(error));
        QVERIFY(!dialog->isVisible()); panel.openAdvancedSettings(); QTRY_VERIFY(dialog->isVisible());
        QCOMPARE(tip->count(), 2 + builtinTipCount()); QCOMPARE(tip->currentText(), QString("New tip")); QCOMPARE(tip->findText("Old tip"), -1);
        auto *patterns = panel.findChild<QListWidget *>("brushTexturePattern"); QVERIFY(patterns); QVERIFY(patterns->currentItem());
        QCOMPARE(patterns->currentItem()->data(Qt::UserRole).toString(), QString("custom"));
        QCOMPARE(patterns->currentItem()->toolTip(), QString("Hidden texture"));
        QCOMPARE(canvas.brush.texture.pattern.size(), texture.size());
        QCOMPARE(canvas.brush.texture.pattern.pixelColor(1, 1), texture.pixelColor(1, 1));
        QCOMPARE(canvas.document()->history()->count(), 0); dialog->close();
    }

    void advancedCategoriesRenderWithoutHorizontalOverflow() {
        Canvas canvas; canvas.setProject(Project::create()); BrushSettingsPanel panel(&canvas);
        panel.resize(240, 540); panel.show(); panel.openAdvancedSettings();
        auto *dialog = panel.findChild<QDialog *>("advancedBrushDialog"); QVERIFY(dialog);
        auto *category = panel.findChild<QTabBar *>("brushSettingsCategory"); QCOMPARE(category->count(), 12);
        auto *pages = panel.findChild<QStackedWidget *>("brushSettingsPages");
        for (const int width : {360, 480, 600}) {
          dialog->resize(width, 620);
          for (int i = 0; i < category->count(); ++i) {
            category->setCurrentIndex(i); QTest::qWait(1);
            QCOMPARE(dialog->width(), width); QCOMPARE(panel.width(), 240);
            QCOMPARE(pages->currentIndex(), i);
            auto *scroll = qobject_cast<QScrollArea *>(pages->currentWidget()); QVERIFY(scroll);
            QVERIFY2(scroll->widget()->width() <= scroll->viewport()->width(), qPrintable(category->tabToolTip(i)));
            for (auto *control : scroll->findChildren<ValueSlider *>()) {
                if (!control->isVisibleTo(scroll)) continue;
                auto *field = control->findChild<QDoubleSpinBox *>(); QVERIFY(field);
                auto *track = control->findChild<QSlider *>(); QVERIFY(track);
                QCOMPARE(field->width(), 48); QVERIFY(field->suffix().isEmpty());
                QVERIFY2(track->width() >= 74, qPrintable(control->objectName()));
                const auto rect = QRect(control->mapTo(scroll->widget(), QPoint()), control->size());
                QVERIFY2(rect.left() >= 0 && rect.right() < scroll->widget()->width(), qPrintable(control->objectName()));
            }
          }
        }
        dialog->close();
    }

    void compactAdvancedRowsKeepNavigationAndPreciseDrafts() {
        Canvas canvas; canvas.setProject(Project::create()); BrushSettingsPanel panel(&canvas);
        panel.resize(240, 420); panel.show(); panel.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&panel));
        panel.openAdvancedSettings();
        auto *dialog = panel.findChild<QDialog *>("advancedBrushDialog"); QVERIFY(dialog);
        dialog->resize(480, 460); dialog->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(dialog));
        auto *categories = panel.findChild<QTabBar *>("brushSettingsCategory"); QVERIFY(categories);
        auto *pages = panel.findChild<QStackedWidget *>("brushSettingsPages"); QVERIFY(pages);
        QCOMPARE(categories->count(), 12);
        for (int i = 0; i < categories->count(); ++i) {
            QCOMPARE(categories->tabRect(i).size(), QSize(28, 28));
            QVERIFY(categories->rect().contains(categories->tabRect(i)));
            QVERIFY(!categories->tabToolTip(i).isEmpty());
            QVERIFY(!categories->accessibleTabName(i).isEmpty());
        }
        for (const auto &name : {"brushSize", "brushHardness", "brushStrokeOpacity", "brushFlow", "brushSpacing", "brushStabilization"}) {
            auto *control = panel.findChild<ValueSlider *>(name); QVERIFY(control);
            auto *row = qobject_cast<PropertyRow *>(control->parentWidget()); QVERIFY(row);
            QVERIFY2(row->height() <= 32, name);
            QVERIFY(row->labelWidget()->geometry().intersects(QRect(0, control->y(), row->width(), control->height())));
        }
        auto *rate = panel.findChild<ValueSlider *>("brushAirbrushRate"); QVERIFY(rate);
        auto *rateRow = qobject_cast<PropertyRow *>(rate->parentWidget()); QVERIFY(rateRow);
        QVERIFY(rateRow->isHidden());
        panel.findChild<QCheckBox *>("brushAirbrush")->setChecked(true);
        QVERIFY(!rateRow->isHidden());
        panel.findChild<QCheckBox *>("brushAirbrush")->setChecked(false);
        QVERIFY(rateRow->isHidden());
        categories->setFocus(); QTest::keyClick(categories, Qt::Key_Down);
        QCOMPARE(categories->currentIndex(), 1); QCOMPARE(pages->currentIndex(), 1);
        QTest::keyClick(categories, Qt::Key_Up); QCOMPARE(categories->currentIndex(), 0);
        categories->setCurrentIndex(3);
        auto *scatter = panel.findChild<ValueSlider *>("brushScatter"); QVERIFY(scatter);
        auto *entry = scatter->findChild<QDoubleSpinBox *>()->findChild<QLineEdit *>(); QVERIFY(entry);
        entry->setFocus(); QTRY_VERIFY(entry->hasFocus()); entry->selectAll(); QTest::keyClicks(entry, "731");
        const auto before = canvas.brush.scatter;
        dialog->resize(360, 400); QTest::qWait(5);
        auto updated = canvas.brush; updated.hardness = .43; canvas.setBrushSettings(updated);
        QCOMPARE(dialog->width(), 360); QCOMPARE(panel.width(), 240);
        QCOMPARE(entry->text(), QString("731")); QVERIFY(entry->hasFocus()); QCOMPARE(canvas.brush.scatter, before);
        auto *scroll = qobject_cast<QScrollArea *>(pages->currentWidget()); QVERIFY(scroll);
        QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
        auto *close = dialog->findChild<QPushButton *>("closeAdvancedBrushSettings"); QVERIFY(close);
        QVERIFY(close->isVisible());
        QVERIFY(dialog->rect().contains(QRect(close->mapTo(dialog, QPoint()), close->size())));
        QTest::keyClick(entry, Qt::Key_Return); QCOMPARE(canvas.brush.scatter, 7.31); QVERIFY(dialog->isVisible());
        entry->selectAll(); QTest::keyClicks(entry, "259"); QTest::keyClick(entry, Qt::Key_Escape);
        QCOMPARE(canvas.brush.scatter, 7.31); QVERIFY(dialog->isVisible());
        QCOMPARE(canvas.document()->history()->count(), 0); dialog->close();
    }

    void dynamicsTabsKeepValuesAndMeasureOnlyTheChosenResponse() {
        Canvas canvas; canvas.setProject(Project::create()); BrushSettingsPanel panel(&canvas);
        panel.show(); panel.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&panel));
        panel.openAdvancedSettings();
        auto *dialog = panel.findChild<QDialog *>("advancedBrushDialog"); QVERIFY(dialog);
        dialog->resize(360, 400); dialog->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(dialog));
        auto *category = panel.findChild<QTabBar *>("brushSettingsCategory");
        auto *pages = panel.findChild<QStackedWidget *>("brushSettingsPages");
        auto *shape = panel.findChild<QTabBar *>("brushShapeDynamicsTabs");
        auto *transfer = panel.findChild<QTabBar *>("brushTransferTabs"); QVERIFY(shape && transfer);
        QCOMPARE(shape->count(), 3); QCOMPARE(transfer->count(), 3);
        for (auto *tabs : {shape, transfer}) for (int i = 0; i < tabs->count(); ++i) {
            QVERIFY(!tabs->tabText(i).isEmpty()); QVERIFY(!tabs->tabToolTip(i).isEmpty());
            QVERIFY(!tabs->accessibleTabName(i).isEmpty());
        }
        category->setCurrentIndex(2); shape->setCurrentIndex(0);
        auto *scroll = qobject_cast<QScrollArea *>(pages->currentWidget()); QVERIFY(scroll);
        panel.findChild<QComboBox *>("sizeControl")->setCurrentIndex(int(BrushControl::Fade));
        panel.findChild<ValueSlider *>("sizeJitter")->setValue(23);
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        const int sizeScroll = scroll->verticalScrollBar()->value();
        const auto before = canvas.brush;
        shape->setCurrentIndex(1);
        QTRY_VERIFY(panel.findChild<ValueSlider *>("angleJitter")->isVisibleTo(dialog));
        QVERIFY(!panel.findChild<ValueSlider *>("sizeJitter")->isVisibleTo(dialog));
        QTRY_COMPARE(scroll->verticalScrollBar()->maximum(), 0);
        shape->setCurrentIndex(0);
        QTRY_COMPARE(scroll->verticalScrollBar()->value(), sizeScroll);
        QCOMPARE(canvas.brush, before);
        category->setCurrentIndex(7); transfer->setCurrentIndex(1);
        QVERIFY(panel.findChild<ValueSlider *>("flowJitter")->isVisibleTo(dialog));
        QVERIFY(!panel.findChild<ValueSlider *>("opacityJitter")->isVisibleTo(dialog));
        transfer->setCurrentIndex(2);
        QVERIFY(panel.findChild<QCheckBox *>("speedDynamicsEnabled")->isVisibleTo(dialog));
        QVERIFY(!panel.findChild<ValueSlider *>("flowJitter")->isVisibleTo(dialog));
        category->setCurrentIndex(0); category->setCurrentIndex(7); QCOMPARE(transfer->currentIndex(), 2);
        QCOMPARE(canvas.brush, before); QCOMPARE(canvas.document()->history()->count(), 0);
        dialog->close(); panel.openAdvancedSettings(); QCOMPARE(transfer->currentIndex(), 2); dialog->close();
    }

    void optionalTextureAndDualDetailsKeepTheirValuesWhenCollapsed() {
        Canvas canvas; canvas.setProject(Project::create()); BrushSettingsPanel panel(&canvas);
        panel.show(); panel.openAdvancedSettings();
        auto *dialog = panel.findChild<QDialog *>("advancedBrushDialog"); QVERIFY(dialog);
        dialog->resize(360, 400);
        auto *category = panel.findChild<QTabBar *>("brushSettingsCategory");
        auto *pages = panel.findChild<QStackedWidget *>("brushSettingsPages");
        for (const auto &entry : {std::pair{4, QString("Texture")}, std::pair{5, QString("Dual")}}) {
            category->setCurrentIndex(entry.first);
            const auto prefix = "brush" + entry.second;
            auto *enabled = panel.findChild<QCheckBox *>(prefix + "Enabled"); QVERIFY(enabled);
            auto *details = panel.findChild<QWidget *>(prefix + "Details"); QVERIFY(details);
            auto *scroll = qobject_cast<QScrollArea *>(pages->currentWidget()); QVERIFY(scroll);
            QVERIFY(enabled->isVisibleTo(dialog)); QVERIFY(details->isHidden());
            QTRY_COMPARE(scroll->verticalScrollBar()->maximum(), 0);
            enabled->setChecked(true); QTRY_VERIFY(details->isVisibleTo(dialog));
            auto *value = panel.findChild<ValueSlider *>(entry.first == 4 ? "brushTextureScale" : "brushDualSize"); QVERIFY(value);
            value->setValue(137);
            enabled->setChecked(false); QTRY_VERIFY(details->isHidden());
            QCOMPARE(value->value(), 137.);
            enabled->setChecked(true); QCOMPARE(value->value(), 137.);
            QVERIFY(value->isVisibleTo(dialog));
        }
        QCOMPARE(canvas.document()->history()->count(), 0); dialog->close();
    }

    void largerAdvancedTextKeepsDirectTabsAndCloseReachable() {
        Canvas canvas; canvas.setProject(Project::create()); BrushSettingsPanel panel(&canvas);
        panel.show(); panel.openAdvancedSettings();
        auto *dialog = panel.findChild<QDialog *>("advancedBrushDialog"); QVERIFY(dialog);
        QFont font = dialog->font(); font.setPixelSize(20); dialog->setFont(font); dialog->resize(480, 520);
        auto *categories = panel.findChild<QTabBar *>("brushSettingsCategory"); categories->setCurrentIndex(2);
        auto *tabs = panel.findChild<QTabBar *>("brushShapeDynamicsTabs");
        auto *close = panel.findChild<QPushButton *>("closeAdvancedBrushSettings");
        QTRY_VERIFY(close->height() >= close->fontMetrics().height() + 4);
        QVERIFY(dialog->rect().contains(QRect(close->mapTo(dialog, QPoint()), close->size())));
        for (int i = 0; i < tabs->count(); ++i) {
            tabs->setCurrentIndex(i); QCoreApplication::processEvents();
            QVERIFY(tabs->rect().contains(tabs->tabRect(i)));
            QVERIFY(tabs->tabRect(i).height() >= tabs->fontMetrics().height() + 4);
        }
        QCOMPARE(categories->iconSize(), QSize(ui::metrics::tabIcon, ui::metrics::tabIcon));
        for (auto *button : dialog->findChildren<QToolButton *>()) {
            if (!button->property("compactDrawingAction").toBool()) continue;
            QVERIFY(button->width() >= ui::metrics::actionTarget);
            QVERIFY(button->height() >= ui::metrics::actionTarget);
            QCOMPARE(button->iconSize(), QSize(ui::metrics::actionIcon, ui::metrics::actionIcon));
        }
        dialog->close();
    }

    void propertyPreviews() {
        const auto output = qEnvironmentVariable("TOON_UI_PREVIEW");
        if (output.isEmpty()) QSKIP("Set TOON_UI_PREVIEW to save brush-property previews.");
        QVERIFY(QDir().mkpath(output));
        Canvas canvas; canvas.setProject(Project::create()); BrushSettingsPanel panel(&canvas);
        panel.resize(240, 540); panel.show(); QTest::qWait(1);
        QVERIFY(panel.grab().save(QDir(output).filePath("brush-properties-compact.png")));
        panel.resize(220, 200); QTest::qWait(1);
        QVERIFY(panel.grab().save(QDir(output).filePath("brush-properties-short.png")));
        panel.resize(240, 540);
        panel.openAdvancedSettings(); auto *dialog = panel.findChild<QDialog *>("advancedBrushDialog"); QVERIFY(dialog);
        dialog->resize(480, 600); QTest::qWait(1);
        auto *category = panel.findChild<QTabBar *>("brushSettingsCategory");
        for (int index : {0, 1, 3, 4, 8, 9}) {
            category->setCurrentIndex(index); QTest::qWait(1);
            QVERIFY(dialog->grab().save(QDir(output).filePath(QString("brush-properties-advanced-%1.png").arg(index))));
        }
        dialog->resize(360, 400); category->setCurrentIndex(3); QTest::qWait(1);
        QVERIFY(dialog->grab().save(QDir(output).filePath("brush-properties-advanced-narrow.png")));
        dialog->close();
    }
};
QTEST_MAIN(BrushSettingsPanelTest)
#include "test_brush_settings_panel.moc"
