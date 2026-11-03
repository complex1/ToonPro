#include "brush_management_panel.h"
#include "brush_engine.h"
#include "brush_types.h"
#include "main_window.h"
#include "property_row.h"
#include "tool_catalog.h"
#include "tool_session.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStyle>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

using namespace toon;
namespace {
QListWidgetItem *itemFor(QListWidget *list, const QString &id) {
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == id) return list->item(i);
    return nullptr;
}
QToolButton *button(QWidget &panel, const char *name) { return panel.findChild<QToolButton *>(name); }
void selectBrushes(BrushManagementPanel &panel, const QStringList &ids) {
    auto *list = panel.findChild<QListWidget *>("managedBrushes"); QVERIFY(list);
    list->clearSelection();
    for (const auto &id : ids) { auto *item = itemFor(list, id); QVERIFY(item); item->setSelected(true); }
}
void commit(QObject *field) { QVERIFY(QMetaObject::invokeMethod(field, "editingFinished")); }
QString brushNamed(const ToolCatalog &catalog, const QString &name) {
    for (const auto &tool : catalog.tools()) if (tool.name == name) return tool.id;
    return {};
}
QImage coloredStamp() {
    QImage image(40, 24, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.fillRect(QRect(4, 4, 12, 16), QColor("#e05843"));
    painter.fillRect(QRect(20, 4, 16, 16), QColor("#45ba88"));
    return image;
}
}

class BrushManagementPanelTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }

    void createDifferentTypesAndImageTips() {
        QTemporaryDir dir; QString error;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load(&error));
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load(&error));
        Canvas canvas; canvas.setProject(Project::create()); ToolSession session(&canvas, &catalog);
        BrushManagementPanel panel(&canvas, &library, &catalog);
        const auto initialActive = catalog.activeToolId();
        const auto artworkHistory = canvas.document()->history()->count();
        for (const auto type : {BrushType::Pen, BrushType::Ink, BrushType::Soft, BrushType::Spray}) {
            const auto id = panel.addBrush("My " + brushTypeName(type), type, "Characters", {"Favorite"}, {}, {}, false, &error);
            QVERIFY2(!id.isEmpty(), qPrintable(error));
            const auto *tool = catalog.tool(id); QVERIFY(tool);
            QCOMPARE(tool->engine, Canvas::Tool::Brush); QCOMPARE(tool->brushType, type);
            QCOMPARE(tool->category, QString("Characters")); QCOMPARE(tool->tags, QStringList{"Favorite"});
            QVERIFY(validateBrushSettings(tool->state.brush, &error));
            if (type == BrushType::Soft) QVERIFY(tool->state.brush.hardness < .5);
            if (type == BrushType::Spray) QVERIFY(tool->state.brush.scatter > 0);
        }
        const auto path = dir.filePath("two-colors.png"); QVERIFY(coloredStamp().save(path));
        QVERIFY2(panel.importImageBrush(path, "Monochrome tip", BrushType::Stamp, "Effects", {"Particles"}, &error), qPrintable(error));
        const auto monoId = brushNamed(catalog, "Monochrome tip"); QVERIFY(!monoId.isEmpty());
        const auto mono = *catalog.tool(monoId);
        QCOMPARE(mono.brushType, BrushType::Stamp); QVERIFY(!mono.state.brush.tipMask.isNull());
        QVERIFY(mono.state.brush.colorTip.isNull());
        QVERIFY2(panel.importImageBrush(path, "Colored tip", BrushType::ColoredStamp, "Effects", {"Particles"}, &error), qPrintable(error));
        const auto colorId = brushNamed(catalog, "Colored tip"); QVERIFY(!colorId.isEmpty());
        const auto color = *catalog.tool(colorId);
        QCOMPARE(color.brushType, BrushType::ColoredStamp); QVERIFY(!color.state.brush.colorTip.isNull());
        QCOMPARE(color.state.brush.colorTip.pixelColor(8, 8), QColor("#e05843"));
        QCOMPARE(color.state.brush.colorTip.pixelColor(24, 8), QColor("#45ba88"));
        QCOMPARE(color.state.brush.colorTip.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(catalog.activeToolId(), initialActive);
        QCOMPARE(canvas.document()->history()->count(), artworkHistory);
        QVERIFY(catalog.save(&error));
        ToolCatalog restored(&library, catalog.path()); QVERIFY(restored.load(&error));
        QCOMPARE(restored.tool(colorId)->state.brush.colorTip, color.state.brush.colorTip);
        QCOMPARE(restored.tool(monoId)->state.brush.tipMask, mono.state.brush.tipMask);
    }

    void dynamicStampCreatesOneContinuousBrushWithAllTips() {
        QTemporaryDir dir; QString error;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        Canvas canvas; canvas.setProject(Project::create()); ToolSession session(&canvas, &catalog);
        BrushManagementPanel panel(&canvas, &library, &catalog);
        const auto active = catalog.activeToolId(); const auto history = catalog.history()->index();
        QImage scan(20, 12, QImage::Format_RGB32); scan.fill(Qt::white); scan.setPixelColor(7, 4, Qt::black);
        const QVector<QImage> sources{coloredStamp(), scan};
        const auto mono = panel.addDynamicStampBrush("Leaf scatter", sources, false, "Nature", {"Leaves"}, "group/drawing", &error);
        QVERIFY2(!mono.isEmpty(), qPrintable(error)); QCOMPARE(catalog.history()->index(), history + 1);
        QCOMPARE(catalog.activeToolId(), active); QVERIFY(catalog.entry("group/drawing")->tools.contains(mono));
        const auto state = catalog.tool(mono)->state;
        QCOMPARE(catalog.tool(mono)->brushType, BrushType::DynamicStamp);
        QCOMPARE(catalog.tool(mono)->category, QString("Nature")); QCOMPARE(catalog.tool(mono)->tags, QStringList{"Leaves"});
        QCOMPARE(state.brush.tipFrames.size(), 2); QCOMPARE(state.brush.tipOrder, BrushTipOrder::Shuffle);
        QVERIFY(state.brush.colorTip.isNull()); QVERIFY(!state.brush.tipMask.isNull());
        QCOMPARE(brushTipFrameImage(state.brush, 0).constScanLine(8)[8], uchar(255));
        const auto scanTip = brushTipFrameImage(state.brush, 1);
        QCOMPARE(scanTip.constScanLine(0)[0], uchar(0)); QCOMPARE(scanTip.constScanLine(4)[7], uchar(255));
        QVERIFY(state.brush.sizeDynamics.jitter > 0); QVERIFY(!state.brush.sizeDynamics.jitterPerStroke);
        QVERIFY(state.brush.angleDynamics.jitter > 0); QVERIFY(!state.brush.angleDynamics.jitterPerStroke);
        QVERIFY(state.brush.colorDynamics.enabled); QVERIFY(state.brush.colorDynamics.perTip); QVERIFY(state.brush.colorDynamics.saturation > 0);
        QVERIFY(!state.brush.colorDynamics.applyToColorTip);
        QCOMPARE(state.options.value("stampPlacement").toBool(true), false);
        QVERIFY(catalog.activateTool(mono)); QVERIFY(!canvas.stampPlacementEnabled());
        catalog.history()->undo(); QVERIFY(!catalog.tool(mono)); catalog.history()->redo(); QVERIFY(catalog.tool(mono));

        const auto color = panel.addDynamicStampBrush("Color leaves", sources, true, {}, {}, {}, &error);
        QVERIFY2(!color.isEmpty(), qPrintable(error));
        const auto colorState = catalog.tool(color)->state;
        QVERIFY(colorState.brush.colorDynamics.applyToColorTip);
        QVERIFY(colorState.brush.tipMask.isNull()); QCOMPARE(colorState.brush.tipFrames.size(), 2);
        QCOMPARE(brushTipFrameImage(colorState.brush, 0), sources[0]);
        QCOMPARE(brushTipFrameImage(colorState.brush, 0).pixelColor(0, 0).alpha(), 0);
        // Activation/Undo captures the ordinary canvas tool options. Verify the
        // authored brush is unchanged, then compare the complete saved snapshot.
        const auto savedMonoState = catalog.tool(mono)->state;
        QCOMPARE(savedMonoState.brush, state.brush); QCOMPARE(savedMonoState.smoothing, state.smoothing);
        QCOMPARE(savedMonoState.options.value("stampPlacement"), state.options.value("stampPlacement"));
        QVERIFY2(catalog.save(&error), qPrintable(error));
        ToolCatalog restored(&library, catalog.path()); QVERIFY2(restored.load(&error), qPrintable(error));
        QCOMPARE(restored.tool(mono)->state, savedMonoState); QCOMPARE(restored.tool(color)->state, colorState);
        QCOMPARE(restored.tool(color)->brushType, BrushType::DynamicStamp);
        QCOMPARE(canvas.document()->history()->count(), 0);
    }

    void dynamicStampBatchImportsFitTipsAndRejectIncompleteBatches() {
        QTemporaryDir dir; QString error;
        BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load()); Canvas canvas;
        BrushManagementPanel panel(&canvas, &library, &catalog);
        QImage large(900, 600, QImage::Format_ARGB32_Premultiplied); large.fill(QColor(30, 90, 160, 200));
        const auto first = dir.filePath("large.png"), second = dir.filePath("leaf.png");
        QVERIFY(large.save(first)); QVERIFY(coloredStamp().save(second));
        const auto initial = catalog.tools().size();
        QVERIFY2(panel.importDynamicStampBrush({first, second}, "Imported leaves", true, "Plants", {"Dynamic"}, {}, &error), qPrintable(error));
        QCOMPARE(catalog.tools().size(), initial + 1);
        const auto id = brushNamed(catalog, "Imported leaves"); QVERIFY(!id.isEmpty());
        const auto &brush = catalog.tool(id)->state.brush;
        QCOMPARE(brush.tipFrames.size(), 2); QCOMPARE(brushTipFrameImage(brush, 0).size(), QSize(512, 341));
        const auto count = catalog.tools().size(); const auto history = catalog.history()->index(); const auto presets = library.presets().size();
        QVERIFY(!panel.importDynamicStampBrush({first, dir.filePath("missing.png")}, "Bad batch", true, {}, {}, {}, &error)); QVERIFY(!error.isEmpty());
        QVERIFY(panel.addDynamicStampBrush("Empty", {}, false, {}, {}, {}, &error).isEmpty());
        QVERIFY(panel.addDynamicStampBrush("Only one", {coloredStamp()}, false, {}, {}, {}, &error).isEmpty());
        QVERIFY(panel.addDynamicStampBrush("Too many", QVector<QImage>(17, coloredStamp()), true, {}, {}, {}, &error).isEmpty());
        QImage empty(16, 16, QImage::Format_ARGB32_Premultiplied); empty.fill(Qt::transparent);
        QVERIFY(panel.addDynamicStampBrush("Invisible", {coloredStamp(), empty}, true, {}, {}, {}, &error).isEmpty());
        QImage blank(16, 16, QImage::Format_RGB32); blank.fill(Qt::white);
        QVERIFY(panel.addDynamicStampBrush("Blank scan", {coloredStamp(), blank}, false, {}, {}, {}, &error).isEmpty());
        QCOMPARE(catalog.tools().size(), count); QCOMPARE(catalog.history()->index(), history); QCOMPARE(library.presets().size(), presets);
    }

    void currentDynamicStampCopyPreservesEditedTipOrderAndDynamics() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        Canvas canvas; canvas.setProject(Project::create()); ToolSession session(&canvas, &catalog);
        BrushManagementPanel panel(&canvas, &library, &catalog);
        const auto source = panel.addDynamicStampBrush("Dynamic source", {coloredStamp(), coloredStamp()}, true); QVERIFY(!source.isEmpty());
        QVERIFY(catalog.activateTool(source)); auto settings = canvas.brush;
        settings.tipOrder = BrushTipOrder::Ordered; settings.sizeDynamics.jitter = .62; settings.angleDynamics.jitter = .34;
        settings.colorDynamics.saturation = .45; settings.colorDynamics.perTip = false;
        canvas.setBrushSettings(settings);
        QString error; const auto copy = panel.addBrush("Dynamic copy", BrushType::DynamicStamp, {}, {}, {}, {}, true, &error);
        QVERIFY2(!copy.isEmpty(), qPrintable(error)); QCOMPARE(catalog.tool(copy)->state.brush, settings);
        QCOMPARE(catalog.tool(copy)->state.options.value("stampPlacement").toBool(true), false);
    }

    void browsingRenameAndBatchMetadataKeepPaintingTool() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        Canvas canvas; canvas.setProject(Project::create()); ToolSession session(&canvas, &catalog);
        BrushManagementPanel panel(&canvas, &library, &catalog);
        const auto first = panel.addBrush("Clean line", BrushType::Pen, "Characters", {"Inking"});
        const auto second = panel.addBrush("Soft light", BrushType::Soft, "Effects", {"Lighting"});
        QVERIFY(!first.isEmpty()); QVERIFY(!second.isEmpty());
        const auto active = catalog.activeToolId(), entry = catalog.selectedEntryId();
        QSignalSpy activation(&catalog, &ToolCatalog::activationRequested);
        panel.focusBrush(first);
        auto *name = panel.findChild<QLineEdit *>("brushManagerName"); QVERIFY(name);
        name->setText("Final line"); commit(name); QCOMPARE(catalog.tool(first)->name, QString("Final line"));
        selectBrushes(panel, {first, second});
        auto *tag = panel.findChild<QLineEdit *>("brushManagerTag"); QVERIFY(tag);
        tag->setText("Favorite"); const auto beforeTag = catalog.history()->index();
        button(panel, "brushManagerAddTag")->click(); QCOMPARE(catalog.history()->index(), beforeTag + 1);
        QCOMPARE(catalog.tool(first)->tags, (QStringList{"Inking", "Favorite"}));
        QCOMPARE(catalog.tool(second)->tags, (QStringList{"Lighting", "Favorite"}));
        tag->setText("FAVORITE"); button(panel, "brushManagerRemoveTag")->click();
        QCOMPARE(catalog.tool(first)->tags, QStringList{"Inking"}); QCOMPARE(catalog.tool(second)->tags, QStringList{"Lighting"});
        auto *category = panel.findChild<QComboBox *>("brushManagerCategory"); QVERIFY(category && category->isEditable());
        category->setEditText("Production"); const auto beforeCategory = catalog.history()->index(); commit(category->lineEdit());
        QCOMPARE(catalog.history()->index(), beforeCategory + 1);
        QCOMPARE(catalog.tool(first)->category, QString("Production")); QCOMPARE(catalog.tool(second)->category, QString("Production"));
        button(panel, "brushManagerUndo")->click();
        QCOMPARE(catalog.tool(first)->category, QString("Characters")); QCOMPARE(catalog.tool(second)->category, QString("Effects"));
        button(panel, "brushManagerRedo")->click();
        QCOMPARE(catalog.tool(first)->category, QString("Production")); QCOMPARE(catalog.tool(second)->category, QString("Production"));
        QCOMPARE(activation.count(), 0); QCOMPARE(catalog.activeToolId(), active); QCOMPARE(catalog.selectedEntryId(), entry);
        QCOMPARE(canvas.document()->history()->count(), 0);
    }

    void filtersExcludeHiddenBrushesFromBatchEditsAndDeletion() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load()); Canvas canvas;
        BrushManagementPanel panel(&canvas, &library, &catalog);
        const auto first = panel.addBrush("Cel pen", BrushType::Pen, "Characters", {"Lines"});
        const auto second = panel.addBrush("Fog spray", BrushType::Spray, "Effects", {"Atmosphere"});
        QVERIFY(!first.isEmpty()); QVERIFY(!second.isEmpty()); selectBrushes(panel, {first, second});
        auto *search = panel.findChild<QLineEdit *>("brushSearch"); QVERIFY(search); search->setText("Fog spray");
        auto *tag = panel.findChild<QLineEdit *>("brushManagerTag"); tag->setText("Favorite");
        button(panel, "brushManagerAddTag")->click();
        QCOMPARE(catalog.tool(first)->tags, QStringList{"Lines"});
        QCOMPARE(catalog.tool(second)->tags, (QStringList{"Atmosphere", "Favorite"}));
        const auto beforeDelete = catalog.history()->index(); button(panel, "brushManagerDelete")->click();
        QCOMPARE(catalog.history()->index(), beforeDelete + 1); QVERIFY(catalog.tool(first)); QVERIFY(!catalog.tool(second));
        button(panel, "brushManagerUndo")->click(); QVERIFY(catalog.tool(first)); QVERIFY(catalog.tool(second));
        QCOMPARE(catalog.tool(second)->tags, (QStringList{"Atmosphere", "Favorite"}));
        search->setText("No brush has this name");
        QVERIFY(!button(panel, "brushManagerDelete")->isEnabled());
        QVERIFY(!button(panel, "brushManagerUse")->isEnabled());
        QVERIFY(!panel.findChild<QLineEdit *>("brushManagerName")->isEnabled());
        QCOMPARE(panel.findChild<QLabel *>("brushManagerSummary")->accessibleName(), QString("Select a brush"));
        QCOMPARE(panel.findChild<QLabel *>("brushManagerTags")->accessibleName(), QString("Brush tags"));
        search->clear();
        auto *type = panel.findChild<QComboBox *>("brushTypeFilter"); QVERIFY(type);
        const auto sprayIndex = type->findData(int(BrushType::Spray)); QVERIFY(sprayIndex >= 0); type->setCurrentIndex(sprayIndex);
        auto *list = panel.findChild<QListWidget *>("managedBrushes");
        QVERIFY(itemFor(list, first)->isHidden()); QVERIFY(!itemFor(list, second)->isHidden());
        type->setCurrentIndex(0);
        auto *categories = panel.findChild<QComboBox *>("brushCategoryFilter"); QVERIFY(categories);
        const auto effectsIndex = categories->findText("Effects"); QVERIFY(effectsIndex >= 0); categories->setCurrentIndex(effectsIndex);
        QVERIFY(itemFor(list, first)->isHidden()); QVERIFY(!itemFor(list, second)->isHidden());
        categories->setCurrentIndex(0);
        auto *tags = panel.findChild<QComboBox *>("brushTagFilter"); QVERIFY(tags);
        const auto linesIndex = tags->findText("Lines"); QVERIFY(linesIndex >= 0); tags->setCurrentIndex(linesIndex);
        QVERIFY(!itemFor(list, first)->isHidden()); QVERIFY(itemFor(list, second)->isHidden());
    }

    void explicitUseAdvancedAndGroupAssignment() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        Canvas canvas; canvas.setProject(Project::create()); ToolSession session(&canvas, &catalog);
        BrushManagementPanel panel(&canvas, &library, &catalog);
        canvas.setBrushSize(67); canvas.setStabilization(21); canvas.setBrushColor(QColor("#dd3355"));
        const auto id = panel.addBrush("Current brush copy", BrushType::Custom, "Personal", {"Favorite"}, {}, {}, true);
        QVERIFY(!id.isEmpty()); QCOMPARE(catalog.tool(id)->state.brush.size, 67.); QCOMPARE(catalog.tool(id)->state.smoothing, 21.);
        canvas.setBrushSize(18); panel.focusBrush(id); button(panel, "brushManagerUse")->click();
        QCOMPARE(catalog.activeToolId(), id); QCOMPARE(canvas.brush.size, 67.); QCOMPARE(canvas.stabilization(), 21.);
        QCOMPARE(canvas.brush.color, QColor("#dd3355"));
        QSignalSpy advanced(&panel, &BrushManagementPanel::advancedSettingsRequested);
        button(panel, "brushManagerAdvanced")->click(); QCOMPARE(advanced.count(), 1); QCOMPARE(advanced.first().first().toString(), id);
        const auto group = catalog.addGroup("Production brushes", "brush"); QVERIFY(!group.isEmpty()); panel.focusBrush(id);
        auto *groups = panel.findChild<QComboBox *>("brushManagerGroup"); QVERIFY(groups);
        const auto groupIndex = groups->findData(group); QVERIFY(groupIndex >= 0); groups->setCurrentIndex(groupIndex);
        const auto before = catalog.history()->index(); button(panel, "brushManagerAssignGroup")->click();
        QCOMPARE(catalog.history()->index(), before + 1); QVERIFY(catalog.entry(group)->tools.contains(id));
        button(panel, "brushManagerUndo")->click(); QVERIFY(!catalog.entry(group)->tools.contains(id));
        QCOMPARE(canvas.document()->history()->count(), 0);
    }

    void duplicateKeepsIndependentSettingsAndDraftsSurviveUnrelatedRefresh() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load()); Canvas canvas;
        BrushManagementPanel panel(&canvas, &library, &catalog); panel.resize(400, 760); panel.show(); panel.activateWindow();
        const auto source = panel.addBrush("Soft rim", BrushType::Soft, "Light", {"Painting"}); QVERIFY(!source.isEmpty());
        panel.focusBrush(source); const auto active = catalog.activeToolId();
        button(panel, "brushManagerDuplicate")->click();
        const auto copy = brushNamed(catalog, "Soft rim copy"); QVERIFY(!copy.isEmpty()); QVERIFY(copy != source);
        QCOMPARE(catalog.tool(copy)->state, catalog.tool(source)->state);
        QCOMPARE(catalog.tool(copy)->category, QString("Light")); QCOMPARE(catalog.tool(copy)->tags, QStringList{"Painting"});
        QCOMPARE(catalog.activeToolId(), active);
        auto changed = catalog.tool(copy)->state; changed.brush.size = 43; QVERIFY(catalog.updateToolState(copy, changed));
        QCOMPARE(catalog.tool(copy)->state.brush.size, 43.); QVERIFY(catalog.tool(source)->state.brush.size != 43.);
        auto *list = panel.findChild<QListWidget *>("managedBrushes"); auto *row = itemFor(list, copy); QVERIFY(row);
        auto *name = panel.findChild<QLineEdit *>("brushManagerName"); name->setFocus(); QTRY_VERIFY(name->hasFocus()); name->setText("Rim light draft");
        QVERIFY(catalog.addToolTag({source}, "Other tag"));
        QCOMPARE(name->text(), QString("Rim light draft")); QCOMPARE(itemFor(list, copy), row);
        commit(name); QCOMPARE(catalog.tool(copy)->name, QString("Rim light draft"));
        QCOMPARE(catalog.tool(source)->name, QString("Soft rim")); QCOMPARE(catalog.activeToolId(), active);
    }

    void brushPreviewsWaitUntilTheStrokeIsFinished() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        Canvas canvas; canvas.setProject(Project::create());
        BrushManagementPanel panel(&canvas, &library, &catalog); panel.resize(400, 760);
        const auto id = panel.addBrush("Preview brush", BrushType::Pen); QVERIFY(!id.isEmpty());
        auto *list = panel.findChild<QListWidget *>("managedBrushes"); auto *row = itemFor(list, id); QVERIFY(row);
        const auto placeholder = row->icon().cacheKey(); panel.show();
        QTRY_VERIFY(row->icon().cacheKey() != placeholder);
        const auto originalPreview = row->icon().cacheKey();
        auto changed = catalog.tool(id)->state; changed.brush.hardness = .05; changed.brush.size = 30;
        QVERIFY(catalog.updateToolState(id, changed));
        auto *doc = canvas.document(); QVERIFY(doc->beginStroke({500, 300}, 1, canvas.brush, false));
        QTest::qWait(25); QVERIFY(doc->strokeActive()); QCOMPARE(row->icon().cacheKey(), originalPreview);
        doc->continueStroke({540, 330}, .8); QTest::qWait(25);
        QCOMPARE(row->icon().cacheKey(), originalPreview);
        doc->endStroke(); QVERIFY(!doc->strokeActive());
        QTRY_VERIFY(row->icon().cacheKey() != originalPreview);
    }

    void invalidImportsLeaveLibraryAndCatalogUntouched() {
        QTemporaryDir dir; QString error; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load(&error));
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load(&error)); Canvas canvas;
        BrushManagementPanel panel(&canvas, &library, &catalog);
        const auto tools = catalog.tools().size(), presets = library.presets().size(); const auto history = catalog.history()->index();
        QVERIFY(!panel.importImageBrush(dir.filePath("missing.png"), "Missing", BrushType::ColoredStamp, {}, {}, &error));
        QVERIFY(!error.isEmpty());
        QFile broken(dir.filePath("broken.png")); QVERIFY(broken.open(QIODevice::WriteOnly)); broken.write("not an image"); broken.close();
        QVERIFY(!panel.importImageBrush(broken.fileName(), "Broken", BrushType::Stamp, {}, {}, &error));
        QVERIFY(!panel.importAbrFile(broken.fileName(), &error));
        QCOMPARE(catalog.tools().size(), tools); QCOMPARE(library.presets().size(), presets); QCOMPARE(catalog.history()->index(), history);
        QVERIFY(!error.isEmpty());
    }

    void creationDialogExposesBrushTypesAndCancelLeavesLibraryUntouched() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load()); Canvas canvas;
        BrushManagementPanel panel(&canvas, &library, &catalog); panel.show();
        const auto before = catalog.tools().size(); const auto history = catalog.history()->index();
        bool visited = false;
        QTimer::singleShot(0, &panel, [&] {
            auto *dialog = panel.findChild<QDialog *>("newBrushDialog");
            if (!dialog) { QFAIL("Add brush did not open its configuration dialog"); return; }
            const auto closeDialog = qScopeGuard([dialog] { if (dialog->isVisible()) dialog->reject(); });
            visited = true;
            auto *sources = dialog->findChild<QTabBar *>("newBrushSource");
            const bool bothSources = sources && sources->count() == 2;
            dialog->resize(340, 420); QTest::qWait(20); QCOMPARE(dialog->width(), 340);
            auto *form = dialog->findChild<QScrollArea *>("newBrushFormScroll"); QVERIFY(form);
            for (const char *type : {"Pen", "Ink", "Soft", "Spray", "Stamp", "ColoredStamp", "DynamicStamp"}) {
                auto *choice = dialog->findChild<QToolButton *>(QString("brushType_") + type);
                if (!choice) { dialog->reject(); QFAIL(qPrintable(QString("Missing brush type ") + type)); return; }
                QVERIFY(!choice->accessibleName().isEmpty());
                QCOMPARE(choice->iconSize(), QSize(ui::metrics::primaryIcon, ui::metrics::primaryIcon)); QVERIFY(choice->height() >= ui::metrics::actionTarget);
                form->ensureWidgetVisible(choice); QCoreApplication::processEvents();
                QVERIFY(dialog->rect().contains(QRect(choice->mapTo(dialog, QPoint()), choice->size())));
            }
            auto *name = dialog->findChild<QLineEdit *>("newBrushName"); QVERIFY(name);
            QVERIFY(qobject_cast<PropertyRow *>(name->parentWidget()));
            name->setFocus(); name->setText("New brush draft");
            name->setModified(true);
            sources->setCurrentIndex(1); sources->setCurrentIndex(0);
            dialog->resize(380, 420); QTest::qWait(20);
            QCOMPARE(name->text(), QString("New brush draft"));
            dialog->reject();
            QVERIFY(bothSources);
        });
        button(panel, "brushManagerAdd")->click(); QTRY_VERIFY(visited);
        QCOMPARE(catalog.tools().size(), before); QCOMPARE(catalog.history()->index(), history);
    }

    void dynamicStampDialogKeepsCompactBatchControlsAndDraft() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load()); Canvas canvas;
        BrushManagementPanel panel(&canvas, &library, &catalog); panel.show();
        const auto count = catalog.tools().size(); const auto history = catalog.history()->index(); bool visited = false;
        QTimer::singleShot(0, &panel, [&] {
            auto *dialog = panel.findChild<QDialog *>("newBrushDialog"); QVERIFY(dialog);
            const auto closeDialog = qScopeGuard([dialog] { if (dialog->isVisible()) dialog->reject(); });
            auto *type = button(*dialog, "brushType_DynamicStamp"); QVERIFY(type); type->click();
            auto *set = dialog->findChild<QWidget *>("newBrushStampSet"); QVERIFY(set); QVERIFY(set->isVisible());
            auto *mode = dialog->findChild<QTabBar *>("newBrushStampColorMode"); QVERIFY(mode); QCOMPARE(mode->count(), 2);
            QVERIFY(!mode->tabToolTip(0).isEmpty()); QVERIFY(!mode->tabToolTip(1).isEmpty());
            auto *images = dialog->findChild<QListWidget *>("newBrushStampImages"); QVERIFY(images); QCOMPARE(images->count(), 0);
            auto *add = button(*dialog, "newBrushStampAdd"); auto *remove = button(*dialog, "newBrushStampRemove"); QVERIFY(add); QVERIFY(remove);
            QCOMPARE(add->size(), QSize(28, 28)); QCOMPARE(add->iconSize(), QSize(ui::metrics::actionIcon, ui::metrics::actionIcon)); QVERIFY(!add->accessibleName().isEmpty());
            QVERIFY(add->isEnabled()); QVERIFY(!remove->isEnabled()); QVERIFY(!button(*dialog, "newBrushImage")->isVisible());
            dialog->resize(340, 480); QTest::qWait(10); QCOMPARE(dialog->width(), 340);
            auto *form = dialog->findChild<QScrollArea *>("newBrushFormScroll"); QVERIFY(form);
            form->ensureWidgetVisible(set); QCoreApplication::processEvents();
            QVERIFY(dialog->rect().contains(QRect(mode->mapTo(dialog, QPoint()), mode->size())));
            QVERIFY(dialog->rect().contains(QRect(images->mapTo(dialog, QPoint()), images->size())));
            auto *name = dialog->findChild<QLineEdit *>("newBrushName"); QVERIFY(name); name->setText("My leaves"); name->setModified(true);
            mode->setCurrentIndex(1); dialog->findChild<QPushButton *>("createBrush")->click();
            QVERIFY(dialog->isVisible()); QVERIFY(!dialog->findChild<QLabel *>("newBrushError")->text().isEmpty());
            auto *sources = dialog->findChild<QTabBar *>("newBrushSource"); sources->setCurrentIndex(1); QVERIFY(!set->isVisible());
            sources->setCurrentIndex(0); type->click(); QVERIFY(set->isVisible()); QCOMPARE(mode->currentIndex(), 1); QCOMPARE(name->text(), QString("My leaves"));
            if(const auto screenshot=qEnvironmentVariable("TOON_DYNAMIC_STAMP_DIALOG_SCREENSHOT");!screenshot.isEmpty())QVERIFY(dialog->grab().save(screenshot));
            visited = true; dialog->reject();
        });
        button(panel, "brushManagerAdd")->click(); QTRY_VERIFY(visited);
        QCOMPARE(catalog.tools().size(), count); QCOMPARE(catalog.history()->index(), history);
    }

    void currentBrushDialogKeepsCopyNameAndOriginalSettings() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        Canvas canvas; canvas.setProject(Project::create()); ToolSession session(&canvas, &catalog);
        BrushManagementPanel panel(&canvas, &library, &catalog); panel.show();
        const auto active = catalog.activeToolId();
        QVERIFY(catalog.renameTool(active, "Character ink")); canvas.setBrushSize(57); canvas.setStabilization(34);
        bool visited = false;
        QTimer::singleShot(0, &panel, [&] {
            auto *dialog = panel.findChild<QDialog *>("newBrushDialog"); QVERIFY(dialog);
            const auto closeDialog = qScopeGuard([dialog] { if (dialog->isVisible()) dialog->reject(); });
            auto *sources = dialog->findChild<QTabBar *>("newBrushSource"); QVERIFY(sources); sources->setCurrentIndex(1);
            auto *name = dialog->findChild<QLineEdit *>("newBrushName"); QVERIFY(name);
            QCOMPARE(name->text(), QString("Character ink copy"));
            for (auto *choice : dialog->findChildren<QToolButton *>())
                if (choice->objectName().startsWith("brushType_")) QVERIFY(!choice->isEnabled());
            sources->setCurrentIndex(0); sources->setCurrentIndex(1);
            QCOMPARE(name->text(), QString("Character ink copy"));
            visited = true;
            auto *create = dialog->findChild<QPushButton *>("createBrush"); QVERIFY(create); create->click();
        });
        button(panel, "brushManagerAdd")->click(); QTRY_VERIFY(visited);
        const auto copy = brushNamed(catalog, "Character ink copy"); QVERIFY(!copy.isEmpty()); QVERIFY(copy != active);
        QCOMPARE(catalog.tool(copy)->state.brush.size, 57.); QCOMPARE(catalog.tool(copy)->state.smoothing, 34.);
        QCOMPARE(catalog.activeToolId(), active); QCOMPARE(canvas.document()->history()->count(), 0);
    }

    void controlsFitNarrowPanelAndUseSharedHelp() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load()); Canvas canvas;
        BrushManagementPanel panel(&canvas, &library, &catalog); panel.resize(360, 600); panel.show(); QTest::qWait(20);
        QCOMPARE(panel.width(), 360);
        QCOMPARE(panel.style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay, nullptr, &panel), ui::tooltipDelayMs);
        for (auto *scroll : panel.findChildren<QScrollArea *>()) {
            QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
            QVERIFY(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
        }
        for (auto *control : panel.findChildren<QToolButton *>()) {
            if (!control->objectName().startsWith("brushManager")) continue;
            QVERIFY2(!control->toolTip().isEmpty(), qPrintable(control->objectName()));
            QVERIFY2(!control->accessibleName().isEmpty(), qPrintable(control->objectName()));
            QCOMPARE(control->iconSize(), QSize(ui::metrics::actionIcon, ui::metrics::actionIcon)); QCOMPARE(control->size(), QSize(28, 28));
        }
        auto *list = panel.findChild<QListWidget *>("managedBrushes"); QVERIFY(list);
        QCOMPARE(list->selectionMode(), QAbstractItemView::ExtendedSelection);
        QCOMPARE(list->iconSize(), QSize(46, 24));
        for (int row = 0; row < list->count(); ++row) {
            QCOMPARE(list->item(row)->sizeHint().height(), qMax(ui::metrics::toolRow,list->fontMetrics().height()+ui::metrics::sectionGap));
            QVERIFY(!list->item(row)->text().contains('\n'));
            QVERIFY(list->item(row)->toolTip().contains('\n'));
        }
        auto *details = panel.findChild<QScrollArea *>("brushManagerDetailsScroll"); QVERIFY(details);
        auto *metadata = panel.findChild<QTabBar *>("brushManagerMetadataTabs"); QVERIFY(metadata);
        metadata->setCurrentIndex(1);
        auto *tags = panel.findChild<QComboBox *>("brushTagFilter");
        auto *search = panel.findChild<QLineEdit *>("brushSearch"); QVERIFY(tags && search);
        for (int width : {240, 300, 360}) {
            panel.resize(width, 360); QTest::qWait(20); QCOMPARE(panel.width(), width); QCOMPARE(panel.height(), 360);
            QCOMPARE(tags->geometry().top(), search->geometry().top());
            QCOMPARE(details->horizontalScrollBar()->maximum(), 0);
            QVERIFY(list->height() >= 96);
            for (const char *id : {"brushManagerAdd", "brushManagerImport", "brushManagerDelete", "brushManagerUse", "brushManagerAdvanced"}) {
                auto *action = button(panel, id); QVERIFY(action);
                QVERIFY(panel.rect().contains(QRect(action->mapTo(&panel, QPoint()), action->size())));
                QVERIFY(!details->isAncestorOf(action));
            }
            details->verticalScrollBar()->setValue(details->verticalScrollBar()->maximum());
            QVERIFY(button(panel, "brushManagerAssignGroup")->isVisible());
        }
        panel.resize(360, 600); QTest::qWait(20);
        QCOMPARE(details->verticalScrollBar()->maximum(), 0);
        const auto listGeometry = list->geometry(), detailsGeometry = details->geometry();
        button(panel, "brushManagerAddTag")->click(); QTest::qWait(20); // Empty tag reports an error.
        auto *status = panel.findChild<QLabel *>("brushManagerError"); QVERIFY(status);
        QVERIFY(!status->text().isEmpty()); QCOMPARE(status->toolTip(), status->text());
        QCOMPARE(status->accessibleDescription(), status->text());
        QCOMPARE(list->geometry(), listGeometry); QCOMPARE(details->geometry(), detailsGeometry);
        const auto previousHeight = list->height(); panel.resize(360, 800); QTest::qWait(20);
        QVERIFY(list->height() >= previousHeight + 100);
    }

    void compactMetadataKeepsDraftsAndBatchEditHistory() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        Canvas canvas; canvas.setProject(Project::create()); ToolSession session(&canvas, &catalog);
        BrushManagementPanel panel(&canvas, &library, &catalog);
        const auto first = panel.addBrush("Cel line", BrushType::Ink, "Characters", {"Line art"});
        const auto second = panel.addBrush("Highlight", BrushType::Pen, "Effects", {"Lighting"});
        const auto active = catalog.activeToolId(); selectBrushes(panel, {first, second});
        panel.resize(360, 600); panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        panel.raise(); panel.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&panel));
        auto *category = panel.findChild<QComboBox *>("brushManagerCategory"); QVERIFY(category);
        QVERIFY(qobject_cast<PropertyRow *>(category->parentWidget()));
        auto *editor = category->lineEdit(); editor->setFocus(); QTRY_VERIFY(editor->hasFocus());
        editor->selectAll(); QTest::keyClicks(editor, "Production draft");
        auto *tag = panel.findChild<QLineEdit *>("brushManagerTag"); QVERIFY(tag); tag->setText("Work in progress");
        const auto before = catalog.history()->index();
        panel.resize(240, 360); QTest::qWait(20);
        QVERIFY(catalog.addGroup("Organization refresh", "folder-open").size() > 0);
        QCOMPARE(editor->text(), QString("Production draft")); QVERIFY(editor->hasFocus());
        QCOMPARE(tag->text(), QString("Work in progress"));
        QCOMPARE(catalog.tool(first)->category, QString("Characters"));
        QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(catalog.tool(first)->category, QString("Production draft"));
        QCOMPARE(catalog.tool(second)->category, QString("Production draft"));
        QCOMPARE(catalog.history()->index(), before + 2); // One group edit and one batch category edit.
        catalog.history()->undo();
        QCOMPARE(catalog.tool(first)->category, QString("Characters"));
        QCOMPARE(catalog.tool(second)->category, QString("Effects"));
        QCOMPARE(catalog.activeToolId(), active); QCOMPARE(canvas.document()->history()->count(), 0);
    }

    void metadataTabsKeepBrowsingSeparateAndGrowWithFonts() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        Canvas canvas; canvas.setProject(Project::create()); ToolSession session(&canvas,&catalog);
        BrushManagementPanel panel(&canvas,&library,&catalog);
        const auto selected=panel.addBrush("Production leaf brush with a long name",BrushType::Pen,"Backgrounds",{"Leaves"});
        const auto active=catalog.activeToolId(); const auto original=catalog.tool(selected)->state; panel.focusBrush(selected);
        panel.resize(360,600); panel.show(); panel.activateWindow();
        auto *list=panel.findChild<QListWidget *>("managedBrushes");
        auto *tabs=panel.findChild<QTabBar *>("brushManagerMetadataTabs");
        auto *details=panel.findChild<QScrollArea *>("brushManagerDetailsScroll");
        auto *name=panel.findChild<QLineEdit *>("brushManagerName");
        auto *tag=panel.findChild<QLineEdit *>("brushManagerTag");
        QVERIFY(list&&tabs&&details&&name&&tag); QCOMPARE(tabs->count(),2);
        QVERIFY(name->isVisible()); QVERIFY(!tag->isVisible());
        tabs->setCurrentIndex(1); QCoreApplication::processEvents();
        QVERIFY(tag->isVisible()); QVERIFY(!name->isVisible());
        tag->setText("Unfinished tag");
        tabs->setCurrentIndex(0); tabs->setCurrentIndex(1);
        QCOMPARE(tag->text(),QString("Unfinished tag"));
        QCOMPARE(catalog.activeToolId(),active); QCOMPARE(canvas.document()->history()->count(),0);
        QFont font=panel.font();font.setPixelSize(24);panel.setFont(font);panel.resize(280,500);
        QCoreApplication::processEvents();
        QVERIFY(list->fontMetrics().height()>24);
        QVERIFY(list->visualItemRect(itemFor(list,selected)).height()>=list->fontMetrics().height()+8);
        auto *status=panel.findChild<QLabel *>("brushManagerError"); QVERIFY(status);
        QVERIFY(status->height()>=status->fontMetrics().height());
        QCOMPARE(tag->text(),QString("Unfinished tag"));
        QCOMPARE(itemFor(list,selected)->data(Qt::UserRole).toString(),selected);
        QCOMPARE(details->horizontalScrollBar()->maximum(),0);
        for(int i=0;i<tabs->count();++i){QVERIFY(!tabs->accessibleTabName(i).isEmpty());QVERIFY(!tabs->tabToolTip(i).isEmpty());}
        QCOMPARE(catalog.tool(selected)->state,original);
    }

    void creationKeepsSourceAndCompletionPinnedAtLargeFonts() {
        QTemporaryDir dir;BrushLibrary library(dir.filePath("brushes.json"));QVERIFY(library.load());
        ToolCatalog catalog(&library,dir.filePath("tools.json"));QVERIFY(catalog.load());Canvas canvas;
        BrushManagementPanel panel(&canvas,&library,&catalog);panel.show();
        const auto count=catalog.tools().size();bool visited=false;
        QTimer::singleShot(0,&panel,[&]{
            auto *dialog=panel.findChild<QDialog *>("newBrushDialog");QVERIFY(dialog);
            const auto cleanup=qScopeGuard([dialog]{dialog->reject();});
            QFont font=dialog->font();font.setPixelSize(22);dialog->setFont(font);dialog->resize(340,400);
            auto *type=button(*dialog,"brushType_DynamicStamp");QVERIFY(type);type->click();
            QTest::qWait(30);
            auto *scroll=dialog->findChild<QScrollArea *>("newBrushFormScroll");
            auto *sources=dialog->findChild<QTabBar *>("newBrushSource");
            auto *create=dialog->findChild<QPushButton *>("createBrush");
            QVERIFY(scroll&&sources&&create);QCOMPARE(dialog->size(),QSize(340,400));
            QVERIFY(scroll->verticalScrollBar()->maximum()>0);QCOMPARE(scroll->horizontalScrollBar()->maximum(),0);
            QVERIFY(!scroll->isAncestorOf(sources));QVERIFY(!scroll->isAncestorOf(create));
            const auto sourceRect=QRect(sources->mapTo(dialog,QPoint()),sources->size());
            const auto createRect=QRect(create->mapTo(dialog,QPoint()),create->size());
            QVERIFY(dialog->rect().contains(sourceRect));QVERIFY(dialog->rect().contains(createRect));
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());QCoreApplication::processEvents();
            QCOMPARE(QRect(sources->mapTo(dialog,QPoint()),sources->size()),sourceRect);
            QCOMPARE(QRect(create->mapTo(dialog,QPoint()),create->size()),createRect);
            create->click();QCoreApplication::processEvents();
            auto *error=dialog->findChild<QLabel *>("newBrushError");QVERIFY(error);QVERIFY(!error->text().isEmpty());
            QVERIFY(!scroll->isAncestorOf(error));QVERIFY(error->isVisible());QVERIFY(dialog->isVisible());
            visited=true;
        });
        button(panel,"brushManagerAdd")->click();QVERIFY(visited);QCOMPARE(catalog.tools().size(),count);
    }

    void workspaceOpensBrushManager() {
        QTemporaryDir dir; MainWindow window(dir.filePath("workspace.ini")); window.resize(1380, 900); window.show();
        auto *dock = window.findChild<QDockWidget *>("brushManagementPanel"); QVERIFY(dock);
        QVERIFY(dock->findChild<BrushManagementPanel *>());
        auto *open = window.findChild<QAction *>("toolListManageBrushes"); QVERIFY(open); open->trigger();
        QTRY_VERIFY(dock->isVisible());
        window.close();
    }

    void managerPreviews() {
        if (qEnvironmentVariableIsEmpty("TOON_UI_PREVIEW")) QSKIP("Preview output is opt-in");
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load()); Canvas canvas;
        canvas.setProject(Project::create());
        BrushManagementPanel panel(&canvas, &library, &catalog);
        for (const auto type : {BrushType::Pen, BrushType::Ink, BrushType::Soft, BrushType::Spray})
            QVERIFY(!panel.addBrush("Production " + brushTypeName(type), type, "Characters", {"Favorite", "Production"}).isEmpty());
        const auto stamp = panel.addBrush("Autumn leaves", BrushType::ColoredStamp, "Backgrounds", {"Nature", "Favorite"}, {}, coloredStamp());
        QVERIFY(!stamp.isEmpty()); panel.focusBrush(stamp);
        auto *previewList = panel.findChild<QListWidget *>("managedBrushes"); QVERIFY(previewList);
        const auto placeholder = itemFor(previewList, stamp)->icon().cacheKey();
        panel.resize(360, 600); panel.show(); QTest::qWait(30);
        QTRY_VERIFY(itemFor(previewList, stamp)->icon().cacheKey() != placeholder);
        const auto output = qEnvironmentVariable("TOON_UI_PREVIEW"); QDir().mkpath(output);
        QVERIFY(panel.grab().save(output + "/brush-management.png"));
        panel.findChild<QTabBar *>("brushManagerMetadataTabs")->setCurrentIndex(1);QTest::qWait(20);
        QVERIFY(panel.grab().save(output + "/brush-management-organize.png"));
        panel.findChild<QTabBar *>("brushManagerMetadataTabs")->setCurrentIndex(0);
        panel.resize(240, 600); QTest::qWait(30); QVERIFY(panel.grab().save(output + "/brush-management-narrow.png"));
        panel.resize(240, 360); QTest::qWait(30); QVERIFY(panel.grab().save(output + "/brush-management-short.png"));
        QTimer::singleShot(0, &panel, [&] {
            auto *dialog = panel.findChild<QDialog *>("newBrushDialog"); QVERIFY(dialog);
            QTest::qWait(20); QVERIFY(dialog->grab().save(output + "/brush-management-new.png"));
            button(*dialog,"brushType_ColoredStamp")->click(); QTest::qWait(20);
            QVERIFY(dialog->grab().save(output + "/brush-management-new-stamp.png")); dialog->reject();
        });
        button(panel, "brushManagerAdd")->click();
    }
};
QTEST_MAIN(BrushManagementPanelTests)
#include "test_brush_management_panel.moc"
