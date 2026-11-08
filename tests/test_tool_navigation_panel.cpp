#include "tool_navigation_panel.h"
#include "tool_catalog.h"
#include "feature_flags.h"
#include "icon_column.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include "icons.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

using namespace toon;
namespace {
QString firstTool(ToolCatalog &catalog, Canvas::Tool engine)
{
    for (const auto &tool : catalog.tools()) if (tool.engine == engine) return tool.id;
    return {};
}
QListWidgetItem *findItem(QListWidget *list, const QString &id)
{
    for (int row = 0; row < list->count(); ++row)
        if (list->item(row)->data(Qt::UserRole).toString() == id) return list->item(row);
    return nullptr;
}
int visibleCount(QListWidget *list)
{
    int count = 0;
    for (int row = 0; row < list->count(); ++row) if (!list->item(row)->isHidden()) ++count;
    return count;
}
void chooseContextAction(QListWidget *list, const QString &text)
{
    QTimer::singleShot(0, list, [text] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu) return;
        for (auto *action : menu->actions()) if (action->text() == text) { action->trigger(); break; }
        menu->close();
    });
    QMetaObject::invokeMethod(list, "customContextMenuRequested", Qt::DirectConnection,
                              Q_ARG(QPoint, list->visualItemRect(list->currentItem()).center()));
}
} // namespace

class ToolNavigationPanelTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }

    void semanticIconsAreDistinctAtWorkingSizesAndRetinaScale()
    {
        const QList<FeatureIcon> features{FeatureIcon::Arc, FeatureIcon::Bezier, FeatureIcon::Simplify,
            FeatureIcon::Pressure, FeatureIcon::Tilt, FeatureIcon::Texture, FeatureIcon::Tone,
            FeatureIcon::PerspectiveGrid, FeatureIcon::VanishingPoint, FeatureIcon::TwoPointPerspective,
            FeatureIcon::ParallelGuide, FeatureIcon::ThreePointPerspective, FeatureIcon::Fisheye,
            FeatureIcon::Warp, FeatureIcon::LinearGradient, FeatureIcon::RadialGradient,
            FeatureIcon::Blur, FeatureIcon::AlphaLock, FeatureIcon::Clipping};
        QSet<QString> names;
        for (const int size : {16, 18, 20}) for (const qreal ratio : {1., 2.}) {
            QSet<QByteArray> rendered;
            for (const auto feature : features) {
                const auto name = featureIconName(feature), label = featureIconLabel(feature);
                QVERIFY(!name.isEmpty()); QVERIFY(!label.isEmpty()); names.insert(name);
                const auto icon = featureIcon(feature); QVERIFY(!icon.isNull());
                const auto normal = icon.pixmap(QSize(size, size), ratio).toImage();
                const auto disabled = icon.pixmap(QSize(size, size), ratio, QIcon::Disabled).toImage();
                QCOMPARE(normal.size(), QSize(size * ratio, size * ratio));
                QVERIFY(!normal.isNull()); QVERIFY(normal != disabled);
                int alpha = 0;
                for (int y=0; y<normal.height(); ++y) for (int x=0; x<normal.width(); ++x)
                    alpha += normal.pixelColor(x,y).alpha();
                QVERIFY2(alpha > 0, qPrintable(name));
                const auto hash = QCryptographicHash::hash(QByteArrayView(
                    reinterpret_cast<const char *>(normal.constBits()), normal.sizeInBytes()), QCryptographicHash::Sha256);
                QVERIFY2(!rendered.contains(hash), qPrintable("Repeated visual identity: " + name));
                rendered.insert(hash);
            }
        }
        QCOMPARE(names.size(), features.size());
        const auto output = qEnvironmentVariable("TOON_UI_PREVIEW");
        if (!output.isEmpty()) {
            QVERIFY(QDir().mkpath(output));
            QPixmap sheet(QSize(570, 54 + features.size()*36)*2); sheet.setDevicePixelRatio(2);
            sheet.fill(QColor(ui::panel)); QPainter painter(&sheet);
            painter.setPen(QColor(ui::text)); painter.drawText(12, 19, "Drawing icons · actual logical sizes");
            painter.drawText(238, 19, "Normal"); painter.drawText(390, 19, "Disabled");
            for (int mode=0; mode<2; ++mode) for (int column=0; column<3; ++column)
                painter.drawText(238 + mode*152 + column*44, 42, QString::number(16+column*2));
            for (int row=0; row<features.size(); ++row) {
                const int y=54 + row*36;
                if (row%2) painter.fillRect(QRect(0,y,570,36),QColor(ui::base));
                painter.setPen(QColor(ui::text)); painter.drawText(QRect(12,y,215,36),Qt::AlignVCenter,featureIconLabel(features[row]));
                for (int mode=0; mode<2; ++mode) for (int column=0; column<3; ++column) {
                    const int size=16+column*2;
                    featureIcon(features[row]).paint(&painter,QRect(238+mode*152+column*44,y+(36-size)/2,size,size),
                        Qt::AlignCenter,mode?QIcon::Disabled:QIcon::Normal);
                }
            }
            painter.end(); QVERIFY(sheet.save(output + "/semantic-icons.png"));
        }
    }

    void compactRowsGrowForLargerFontsWithoutLosingSelection()
    {
        QTemporaryDir dir; ToolCatalog catalog(nullptr, dir.filePath("tools.json")); QVERIFY(catalog.load());
        QVERIFY(catalog.selectEntry("group/shapes"));
        ToolNavigationPanel panel(&catalog, false); panel.resize(240, 420); panel.show();
        auto *list = panel.findChild<QListWidget *>("toolEntries"); const auto active = catalog.activeToolId();
        QFont font = panel.font(); font.setPixelSize(28); panel.setFont(font); QCoreApplication::processEvents();
        QVERIFY(list->visualItemRect(list->item(0)).height() >= list->fontMetrics().height()+ui::metrics::sectionGap);
        QCOMPARE(catalog.activeToolId(), active); QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), active);
        QCOMPARE(list->horizontalScrollBar()->maximum(), 0);
    }

    void cancelDialogDoesNotChangeOrganization()
    {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        const int entries = catalog.entries().size(), tools = catalog.tools().size(), history = catalog.history()->count();
        const auto selected = catalog.selectedEntryId(), active = catalog.activeToolId();
        AddToolsDialog dialog(&catalog); dialog.show();
        dialog.findChild<QLineEdit *>("newGroupName")->setText("Uncommitted group");
        auto *choices = dialog.findChild<QListWidget *>("availableTools"); QVERIFY(choices->count() > 0);
        choices->item(0)->setCheckState(Qt::Checked);
        dialog.reject();
        QCOMPARE(catalog.entries().size(), entries); QCOMPARE(catalog.tools().size(), tools);
        QCOMPARE(catalog.history()->count(), history); QCOMPARE(catalog.selectedEntryId(), selected); QCOMPARE(catalog.activeToolId(), active);
    }

    void sharedDialogCreatesGroupAndAddsToItsDestination()
    {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        const auto rectangle = firstTool(catalog, Canvas::Tool::Rectangle), line = firstTool(catalog, Canvas::Tool::Line);
        QVERIFY(!rectangle.isEmpty()); QVERIFY(!line.isEmpty());
        AddToolsDialog create(&catalog); create.show();
        create.findChild<QLineEdit *>("newGroupName")->setText("Background drawing");
        auto *choices = create.findChild<QListWidget *>("availableTools");
        findItem(choices, rectangle)->setCheckState(Qt::Checked);
        create.findChild<QToolButton *>("groupIcon_image")->click();
        create.accept(); QCOMPARE(create.result(), int(QDialog::Accepted));
        const auto id = catalog.selectedEntryId(); const auto *entry = catalog.entry(id);
        QVERIFY(entry && entry->group); QCOMPARE(entry->name, QString("Background drawing"));
        QCOMPARE(entry->icon, QString("image")); QCOMPARE(entry->tools, QStringList{rectangle});
        const int history = catalog.history()->count();
        AddToolsDialog add(&catalog, AddToolsDialog::Context::GroupTools, id); add.show();
        QVERIFY(!add.findChild<QTabBar *>("toolCreationTabs")->isVisible());
        QVERIFY(!add.findChild<QLineEdit *>("newGroupName")->isVisible());
        choices = add.findChild<QListWidget *>("availableTools"); QVERIFY(!findItem(choices, rectangle));
        findItem(choices, line)->setCheckState(Qt::Checked); add.accept();
        QCOMPARE(catalog.entry(id)->tools, (QStringList{rectangle, line})); QCOMPARE(catalog.history()->count(), history + 1);
        catalog.history()->undo(); QCOMPARE(catalog.entry(id)->tools, QStringList{rectangle});
    }

    void standaloneVariantIsIndependentAndOnlyShowsOneTool()
    {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        const auto rectangle = firstTool(catalog, Canvas::Tool::Rectangle);
        const auto original = catalog.addStandalone(rectangle); QVERIFY(!original.isEmpty());
        const int before = catalog.history()->count(), entries = catalog.entries().size();
        AddToolsDialog dialog(&catalog, AddToolsDialog::Context::StandaloneTools, original); dialog.show();
        auto *choices = dialog.findChild<QListWidget *>("availableTools"); choices->setCurrentItem(findItem(choices, rectangle));
        dialog.findChild<QCheckBox *>("createToolVariant")->setChecked(true);
        dialog.findChild<QLineEdit *>("newToolName")->setText("Background outline"); dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted)); QCOMPARE(catalog.entries().size(), entries + 1);
        QCOMPARE(catalog.history()->count(), before + 1); QVERIFY(catalog.selectedEntryId() != original);
        QCOMPARE(catalog.visibleToolIds().size(), 1); const auto clone = catalog.activeToolId(); QVERIFY(clone != rectangle);
        QCOMPARE(catalog.tool(clone)->name, QString("Background outline"));
        QVERIFY(catalog.tool(clone)->state == catalog.tool(rectangle)->state);
        QVERIFY(!catalog.entry(original)->group); QCOMPARE(catalog.entry(original)->tools, QStringList{rectangle});
        catalog.history()->undo(); QCOMPARE(catalog.selectedEntryId(), original); QCOMPARE(catalog.visibleToolIds(), QStringList{rectangle});
    }

    void panelsSelectDuplicateRemoveAndUndoPlacements()
    {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        const auto rectangle = firstTool(catalog, Canvas::Tool::Rectangle);
        const auto standalone = catalog.addStandalone(rectangle); QVERIFY(!standalone.isEmpty());
        QWidget workspace; auto *layout = new QHBoxLayout(&workspace);
        auto *toolbox = new ToolNavigationPanel(&catalog, true); auto *tools = new ToolNavigationPanel(&catalog, false);
        layout->addWidget(toolbox); layout->addWidget(tools); workspace.resize(480, 500); workspace.show(); QTest::qWait(10);
        auto *groups = toolbox->findChild<QListWidget *>("toolboxEntries"); auto *list = tools->findChild<QListWidget *>("toolEntries");
        QCOMPARE(groups->currentItem()->data(Qt::UserRole).toString(), standalone);
        QCOMPARE(list->count(), 1); QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), rectangle);
        const int history = catalog.history()->count(); chooseContextAction(list, "Duplicate");
        QCOMPARE(catalog.history()->count(), history + 1); QCOMPARE(list->count(), 1);
        const auto copy = catalog.activeToolId(); QVERIFY(copy != rectangle); QVERIFY(catalog.tool(copy));
        QCOMPARE(groups->currentItem()->data(Qt::UserRole).toString(), catalog.selectedEntryId());
        tools->findChild<QToolButton *>("toolListRemove")->click(); QVERIFY(catalog.tool(copy));
        auto *undo = tools->findChild<QAction *>("organizationUndo"); QVERIFY(undo && undo->isEnabled());
        undo->trigger(); QCOMPARE(catalog.activeToolId(), copy); QCOMPARE(list->count(), 1);
        undo->trigger(); QCOMPARE(catalog.activeToolId(), rectangle); QCOMPARE(catalog.selectedEntryId(), standalone);
        const auto group = catalog.addGroup("Geometry", "grid-2x2", {rectangle});
        catalog.selectEntry(standalone); groups->setCurrentItem(findItem(groups, group));
        QCOMPARE(catalog.selectedEntryId(), group); QCOMPARE(list->count(), 1);
        QVERIFY(undo->shortcut().isEmpty()); // MainWindow owns focus-aware keyboard routing.
    }

    void mixedGroupReorderingPreservesQuarantinedToolsAndUndo()
    {
        if (features::legacy3DEditor) QSKIP("This regression exercises omitted 3D placements.");
        QTemporaryDir dir; ToolCatalog catalog(nullptr, dir.filePath("tools.json")); QVERIFY(catalog.load());
        const QStringList original{"line", "scene-object", "rectangle", "scene-camera", "ellipse"};
        const auto group = catalog.addGroup("Mixed tools", "folder-open", original); QVERIFY(!group.isEmpty());
        ToolNavigationPanel panel(&catalog, false);
        auto *list = panel.findChild<QListWidget *>("toolEntries"); QVERIFY(list); QCOMPARE(list->count(), 3);
        const int history = catalog.history()->count();
        QVERIFY(QMetaObject::invokeMethod(&panel, "movePlacement", Qt::DirectConnection,
                                          Q_ARG(QString, QString("line")), Q_ARG(int, 0)));
        QCOMPARE(catalog.entry(group)->tools, original); QCOMPARE(catalog.history()->count(), history);
        QVERIFY(QMetaObject::invokeMethod(&panel, "movePlacement", Qt::DirectConnection,
                                          Q_ARG(QString, QString("line")), Q_ARG(int, 2)));
        QCOMPARE(catalog.visibleToolIds(), (QStringList{"rectangle", "ellipse", "line"}));
        QCOMPARE(catalog.entry(group)->tools, (QStringList{"scene-object", "rectangle", "scene-camera", "ellipse", "line"}));
        QCOMPARE(catalog.history()->count(), history + 1);
        QCOMPARE(list->item(2)->data(Qt::UserRole).toString(), QString("line"));
        catalog.history()->undo(); QCOMPARE(catalog.entry(group)->tools, original);
        QCOMPARE(catalog.visibleToolIds(), (QStringList{"line", "rectangle", "ellipse"}));
        QVERIFY(QMetaObject::invokeMethod(&panel, "movePlacement", Qt::DirectConnection,
                                          Q_ARG(QString, QString("ellipse")), Q_ARG(int, 0)));
        QCOMPARE(catalog.visibleToolIds(), (QStringList{"ellipse", "line", "rectangle"}));
        QCOMPARE(catalog.entry(group)->tools, (QStringList{"ellipse", "line", "scene-object", "rectangle", "scene-camera"}));
        catalog.history()->undo(); QCOMPARE(catalog.entry(group)->tools, original);
    }

    void toolboxReorderingUsesVisibleRowsAndPreservesHiddenGroup()
    {
        if (features::legacy3DEditor) QSKIP("This regression exercises omitted 3D placements.");
        QTemporaryDir dir; ToolCatalog catalog(nullptr, dir.filePath("tools.json")); QVERIFY(catalog.load());
        QVERIFY(catalog.selectEntry("group/drawing"));
        QVERIFY(catalog.renameEntry("group/scene3d", "My reference room", "video"));
        QVERIFY(catalog.moveEntry("group/scene3d", 1));
        const auto hidden = *catalog.entry("group/scene3d");
        QStringList stored, visible;
        for (const auto &entry : catalog.entries()) stored.append(entry.id);
        for (const auto &entry : catalog.availableEntries()) visible.append(entry.id);
        QVERIFY(visible.size() >= 3); QCOMPARE(visible.first(), QString("group/drawing"));
        ToolNavigationPanel panel(&catalog, true);
        auto *list = panel.findChild<QListWidget *>("toolboxEntries"); QVERIFY(list);
        QVERIFY(!findItem(list, hidden.id));
        auto expectedStored = stored; expectedStored.move(0, stored.indexOf(visible[2]));
        auto expectedVisible = visible; expectedVisible.move(0, 2);
        const int history = catalog.history()->count();
        QVERIFY(QMetaObject::invokeMethod(&panel, "movePlacement", Qt::DirectConnection,
                                          Q_ARG(QString, visible.first()), Q_ARG(int, 2)));
        QStringList actualStored, actualVisible;
        for (const auto &entry : catalog.entries()) actualStored.append(entry.id);
        for (const auto &entry : catalog.availableEntries()) actualVisible.append(entry.id);
        QCOMPARE(actualStored, expectedStored); QCOMPARE(actualVisible, expectedVisible);
        QCOMPARE(catalog.history()->count(), history + 1);
        QCOMPARE(catalog.entry(hidden.id)->name, hidden.name); QCOMPARE(catalog.entry(hidden.id)->icon, hidden.icon);
        QCOMPARE(catalog.entry(hidden.id)->tools, hidden.tools); QCOMPARE(catalog.entry(hidden.id)->lastToolId, hidden.lastToolId);
        QCOMPARE(list->item(2)->data(Qt::UserRole).toString(), visible.first());
        catalog.history()->undo(); actualStored.clear();
        for (const auto &entry : catalog.entries()) actualStored.append(entry.id);
        QCOMPARE(actualStored, stored); QVERIFY(!findItem(list, hidden.id));
    }

    void emptyGroupAndSearchAreClearAndNarrowPanelsFit()
    {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        const auto previous = catalog.activeToolId(); catalog.addGroup("Empty", "folder-open");
        ToolNavigationPanel tools(&catalog, false); tools.resize(240, 420); tools.show(); QTest::qWait(10);
        QCOMPARE(catalog.activeToolId(), previous);
        QVERIFY(tools.findChild<QPushButton *>("toolListEmptyAdd")->isVisible());
        QVERIFY(!tools.findChild<QToolButton *>("toolListRemove")->isEnabled());
        auto *list = tools.findChild<QListWidget *>("toolEntries"); QCOMPARE(list->horizontalScrollBar()->maximum(), 0);
        for (auto *button : tools.findChildren<QToolButton *>()) {
            if (button->objectName().startsWith("qt_")) continue;
            QVERIFY(!button->toolTip().isEmpty()); QVERIFY(!button->accessibleName().isEmpty());
        }
        auto *search = tools.findChild<QLineEdit *>("toolPanelSearch"); search->setText("Ink");
        auto *clear = search->findChild<QToolButton *>("toolPanelSearchClear"); QVERIFY(clear);
        QCOMPARE(clear->toolTip(), QString("Clear tool search")); QCOMPARE(clear->accessibleName(), clear->toolTip());
        clear->click(); QVERIFY(search->text().isEmpty());
        AddToolsDialog add(&catalog, AddToolsDialog::Context::StandaloneTools); add.show();
        auto *choices = add.findChild<QListWidget *>("availableTools"); choices->setCurrentRow(0);
        auto *ok = add.findChild<QPushButton *>("confirmAddTools"); QVERIFY(ok->isEnabled());
        add.findChild<QLineEdit *>("toolSearch")->setText("No such tool xyz123"); QVERIFY(!ok->isEnabled());
        for (int i = 0; i < choices->count(); ++i) QVERIFY(choices->item(i)->isHidden());
        add.reject();
    }

    void toolboxColumnKeepsNamesAccessibleAndRowsIconOnly()
    {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        ToolNavigationPanel toolbox(&catalog, true); toolbox.resize(600, 700); toolbox.show(); QTest::qWait(10);
        QCOMPARE(toolbox.width(), IconColumn::ContentWidth);
        auto *list = toolbox.findChild<QListWidget *>("toolboxEntries"); QVERIFY(list);
        QCOMPARE(list->editTriggers(), QAbstractItemView::EditTriggers(QAbstractItemView::NoEditTriggers));
        QCOMPARE(list->horizontalScrollBar()->maximum(), 0);
        auto *drawing = findItem(list, "group/drawing"); QVERIFY(drawing);
        QCOMPARE(drawing->data(Qt::AccessibleTextRole).toString(), QString("Drawing"));
        QVERIFY(drawing->toolTip().startsWith("Drawing"));
        QCOMPARE(list->visualItemRect(drawing).height(), ui::metrics::railTarget);
        const auto before = list->viewport()->grab().toImage();
        QVERIFY(catalog.renameEntry("group/drawing", "A deliberately long group name that stays accessible"));
        QTest::qWait(10);
        QCOMPARE(list->currentItem()->text(), QString("A deliberately long group name that stays accessible"));
        QCOMPARE(list->currentItem()->data(Qt::AccessibleTextRole).toString(), list->currentItem()->text());
        QCOMPARE(list->viewport()->grab().toImage(), before); // Names are not painted or allowed to widen the column.
        for (auto *button : toolbox.findChildren<QToolButton *>()) {
            if (button->objectName().startsWith("qt_")) continue;
            QVERIFY(!button->toolTip().isEmpty()); QVERIFY(!button->accessibleName().isEmpty());
            QCOMPARE(button->size(), QSize(28, 28));
            QCOMPARE(button->iconSize(), QSize(ui::metrics::actionIcon, ui::metrics::actionIcon));
        }
        list->setFocus(); QTest::keyClick(list, Qt::Key_Down);
        QCOMPARE(catalog.selectedEntryId(), QString("group/shapes"));
        QTimer::singleShot(0, &toolbox, [] {
            auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog); QVERIFY(dialog->width() >= 250);
            dialog->setTextValue("Geometry"); dialog->accept();
        });
        QTest::keyClick(list, Qt::Key_F2);
        QCOMPARE(catalog.entry("group/shapes")->name, QString("Geometry"));
        QCOMPARE(list->currentItem()->text(), QString("Geometry"));
        QTimer::singleShot(0, &toolbox, [] {
            auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog); dialog->setTextValue("Cancelled name"); dialog->reject();
        });
        QTest::keyClick(list, Qt::Key_F2);
        QCOMPARE(catalog.entry("group/shapes")->name, QString("Geometry"));
        toolbox.resize(900, 240); QTest::qWait(10);
        QCOMPARE(toolbox.width(), IconColumn::ContentWidth);
        QVERIFY(list->verticalScrollBar()->maximum() > 0);
        QCOMPARE(list->horizontalScrollBar()->maximum(), 0);
        QVERIFY(list->viewport()->width() >= list->iconSize().width());
        list->scrollToBottom(); QTest::qWait(10);
        QVERIFY(list->viewport()->rect().intersects(list->visualItemRect(list->item(list->count() - 1))));
    }

    void brushRowShowsAStrokeAcrossThePreview()
    {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        ToolNavigationPanel panel(&catalog, false);
        auto *list = panel.findChild<QListWidget *>("toolEntries");
        auto *brush = findItem(list, firstTool(catalog, Canvas::Tool::Brush)); QVERIFY(brush);
        const auto placeholder = brush->icon().cacheKey();
        panel.resize(240, 420); panel.show();
        QTRY_VERIFY(brush->icon().cacheKey() != placeholder);
        const auto image = brush->icon().pixmap(QSize(46, 24)).toImage(); QVERIFY(!image.isNull());
        int left = image.width(), right = -1;
        const QColor base(ui::base);
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y).red() <= base.red() + 20) continue;
            left = qMin(left, x); right = qMax(right, x);
        }
        QVERIFY(right - left > image.width() / 2);
    }

    void previewsOnlyRenderVisibleRowsAndReuseCachedSamples()
    {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        QStringList ids;
        for (const auto &tool : catalog.tools()) if (tool.engine == Canvas::Tool::Brush) ids.append(tool.id);
        QVERIFY(ids.size() > 12);
        QVERIFY(!catalog.addGroup("All brush samples", "paintbrush", ids).isEmpty());
        QElapsedTimer elapsed; elapsed.start();
        ToolNavigationPanel panel(&catalog, false); panel.resize(240, 240);
        qInfo() << "Tool list creation for" << ids.size() << "brushes:" << elapsed.nsecsElapsed() / 1.e6 << "ms";
        auto *list = panel.findChild<QListWidget *>("toolEntries"); QVERIFY(list);
        auto *first = findItem(list, ids.first()), *last = findItem(list, ids.last()); QVERIFY(first && last);
        const auto firstPlaceholder = first->icon().cacheKey(), lastPlaceholder = last->icon().cacheKey();
        QTest::qWait(30); QCOMPARE(first->icon().cacheKey(), firstPlaceholder);
        panel.show(); QTRY_VERIFY(first->icon().cacheKey() != firstPlaceholder);
        QTest::qWait(60); QCOMPARE(last->icon().cacheKey(), lastPlaceholder);
        const auto firstRendered = first->icon().cacheKey();
        list->scrollToBottom(); QTRY_VERIFY(last->icon().cacheKey() != lastPlaceholder);
        const auto lastRendered = last->icon().cacheKey();
        QVERIFY(catalog.renameTool(ids.last(), "Renamed brush sample"));
        QTest::qWait(40); QCOMPARE(last->icon().cacheKey(), lastRendered);
        list->scrollToTop(); QTest::qWait(30); QCOMPARE(first->icon().cacheKey(), firstRendered);

        panel.hide(); auto changed = catalog.tool(ids.first())->state;
        changed.brush.hardness = .23; QVERIFY(catalog.updateToolState(ids.first(), changed));
        QTest::qWait(40); QCOMPARE(first->icon().cacheKey(), firstRendered);
        panel.show(); QTRY_VERIFY(first->icon().cacheKey() != firstRendered);
    }

    void addDialogFiltersBeforeRenderingAndRefreshesEditedSamples()
    {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        QStringList brushes;
        for (const auto &tool : catalog.tools()) if (tool.engine == Canvas::Tool::Brush) brushes.append(tool.id);
        QVERIFY(brushes.size() > 12);
        const auto target = brushes.last(); QVERIFY(catalog.renameTool(target, "Targeted sample lookup"));
        AddToolsDialog dialog(&catalog);
        auto *list = dialog.findChild<QListWidget *>("availableTools");
        auto *first = findItem(list, brushes.first()), *last = findItem(list, target); QVERIFY(first && last);
        const auto firstPlaceholder = first->icon().cacheKey(), lastPlaceholder = last->icon().cacheKey();
        dialog.findChild<QLineEdit *>("toolSearch")->setText("Targeted sample lookup");
        last->setCheckState(Qt::Checked);
        dialog.show(); QTRY_VERIFY(last->icon().cacheKey() != lastPlaceholder);
        QTest::qWait(40); QCOMPARE(first->icon().cacheKey(), firstPlaceholder);
        const auto rendered = last->icon().cacheKey();
        auto changed = catalog.tool(target)->state; changed.brush.size = 3;
        QVERIFY(catalog.updateToolState(target, changed));
        QTRY_VERIFY(last->icon().cacheKey() != rendered);
        QCOMPARE(last->checkState(), Qt::Checked);
        const auto updated = last->icon().cacheKey();
        QVERIFY(catalog.addToolTag({target}, "Updated metadata"));
        last = findItem(list, target); QVERIFY(last);
        QCOMPARE(last->icon().cacheKey(), updated); QCOMPARE(last->checkState(), Qt::Checked);
    }

    void previewsPauseForInkAndResumeWithoutRebuildingAfterEveryStroke()
    {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        Document document; document.load(Project::create());
        const auto id = firstTool(catalog, Canvas::Tool::Brush);
        ToolNavigationPanel panel(&catalog, false); panel.resize(240, 240); panel.setPreviewDocument(&document);
        auto *list = panel.findChild<QListWidget *>("toolEntries");
        auto *row = findItem(list, id); QVERIFY(row);
        const auto placeholder = row->icon().cacheKey();
        QVERIFY(document.beginStroke({40, 40}, 1., BrushSettings{}, false));
        panel.show(); QTest::qWait(50); QCOMPARE(row->icon().cacheKey(), placeholder);
        document.endStroke(); QTRY_VERIFY(row->icon().cacheKey() != placeholder);

        const auto rendered = row->icon().cacheKey();
        QVERIFY(document.beginStroke({40, 50}, 1., BrushSettings{}, false));
        auto changed = catalog.tool(id)->state; changed.brush.hardness = .32;
        QVERIFY(catalog.updateToolState(id, changed));
        QTest::qWait(50); QCOMPARE(row->icon().cacheKey(), rendered);
        document.endStroke(); QTRY_VERIFY(row->icon().cacheKey() != rendered);
        const auto updated = row->icon().cacheKey();
        QVERIFY(document.beginStroke({40, 60}, 1., BrushSettings{}, false)); document.endStroke();
        QTest::qWait(40); QCOMPARE(row->icon().cacheKey(), updated);
    }

    void compactNamedRowsAndFiltersFitWithoutInterruptingSearch()
    {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        ToolNavigationPanel panel(&catalog, false); panel.resize(240, 420); panel.show(); panel.activateWindow();
        auto *list = panel.findChild<QListWidget *>("toolEntries");
        auto *tags = panel.findChild<QComboBox *>("toolTagFilter");
        auto *search = panel.findChild<QLineEdit *>("toolPanelSearch");
        search->setFocus(); QTRY_VERIFY(search->hasFocus());
        QTest::keyClicks(search, "Round");
        const auto active = catalog.activeToolId();
        const int history = catalog.history()->count();
        for (int width : {180, 240, 320}) {
            panel.resize(width, 420); QCoreApplication::processEvents();
            QCOMPARE(panel.width(), width);
            QCOMPARE(tags->geometry().top(), search->geometry().top());
            QVERIFY(tags->geometry().right() < search->geometry().left());
            QVERIFY(panel.rect().contains(tags->geometry()));
            QVERIFY(panel.rect().contains(search->geometry()));
            QVERIFY(search->width() >= 76);
            QCOMPARE(list->horizontalScrollBar()->maximum(), 0);
            for (int row = 0; row < list->count(); ++row)
                if (!list->item(row)->isHidden()) QCOMPARE(list->visualItemRect(list->item(row)).height(), ui::metrics::toolRow);
            QCOMPARE(search->text(), QString("Round"));
            QVERIFY(search->hasFocus());
        }
        for (const auto *name : {"toolListAdd", "toolListRemove", "toolListMore"}) {
            const auto *button = panel.findChild<QToolButton *>(name); QVERIFY(button);
            QCOMPARE(button->size(), QSize(28, 28));
            QCOMPARE(button->iconSize(), QSize(ui::metrics::actionIcon, ui::metrics::actionIcon));
        }
        QCOMPARE(catalog.activeToolId(), active);
        QCOMPARE(catalog.history()->count(), history);
    }

    void filteringKeepsActiveToolAndDisablesHiddenSelectionActions()
    {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        const auto brush = firstTool(catalog, Canvas::Tool::Brush), rectangle = firstTool(catalog, Canvas::Tool::Rectangle);
        const auto group = catalog.addGroup("Working tools", "paintbrush", {brush, rectangle});
        QVERIFY(catalog.setToolTags({brush}, {"Ink"})); QVERIFY(catalog.setToolTags({rectangle}, {"Geometry"}));
        QVERIFY(catalog.activateTool(brush));
        ToolNavigationPanel panel(&catalog, false); panel.resize(240, 420); panel.show();
        auto *list = panel.findChild<QListWidget *>("toolEntries");
        auto *remove = panel.findChild<QToolButton *>("toolListRemove");
        QSignalSpy activated(&catalog, &ToolCatalog::activationRequested), options(&panel, &ToolNavigationPanel::viewOptionsChanged);
        const int history = catalog.history()->count();
        panel.setTagFilter("Geometry");
        QCOMPARE(visibleCount(list), 1); QCOMPARE(catalog.activeToolId(), brush); QCOMPARE(catalog.selectedEntryId(), group);
        QVERIFY(findItem(list, brush)->isHidden()); QVERIFY(!list->currentItem()); QVERIFY(!remove->isEnabled());
        QVERIFY(!list->dragEnabled()); QVERIFY(!list->acceptDrops());
        QCOMPARE(activated.count(), 0); QCOMPARE(catalog.history()->count(), history);
        panel.findChild<QLineEdit *>("toolPanelSearch")->setText("no match");
        QCOMPARE(visibleCount(list), 0); QVERIFY(panel.findChild<QLabel *>("toolFilterNoMatches")->isVisible());
        QVERIFY(!panel.findChild<QPushButton *>("toolListEmptyAdd")->isVisible());
        panel.findChild<QToolButton *>("toolClearFilters")->click();
        QCOMPARE(panel.tagFilter(), QString()); QCOMPARE(visibleCount(list), 2);
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), brush); QVERIFY(remove->isEnabled());
        QVERIFY(list->dragEnabled()); QVERIFY(list->acceptDrops()); QCOMPARE(activated.count(), 0);
        QCOMPARE(options.count(), 2);
    }

    void groupTagsInheritForFilteringWithoutChangingTools()
    {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        const auto brush = firstTool(catalog, Canvas::Tool::Brush), rectangle = firstTool(catalog, Canvas::Tool::Rectangle);
        const auto group = catalog.addGroup("Film", "paintbrush", {brush, rectangle});
        QVERIFY(catalog.setEntryTags(group, {"Production"}));
        ToolNavigationPanel panel(&catalog, false); panel.resize(240, 420); panel.show();
        auto *list = panel.findChild<QListWidget *>("toolEntries");
        panel.setTagFilter("production"); QCOMPARE(visibleCount(list), 2);
        QVERIFY(catalog.tool(brush)->tags.isEmpty()); QVERIFY(catalog.tool(rectangle)->tags.isEmpty());
        const auto other = catalog.addGroup("Unsorted", "folder-open", {rectangle});
        QCOMPARE(catalog.selectedEntryId(), other); QCOMPARE(panel.tagFilter(), QString("production"));
        QCOMPARE(visibleCount(list), 0); QVERIFY(!list->currentItem());
        panel.setTagFilter(ToolNavigationPanel::untaggedTagFilter()); QCOMPARE(visibleCount(list), 1);
        QVERIFY(catalog.selectEntry(group)); QCOMPARE(visibleCount(list), 0);
        panel.setTagFilter({}); QCOMPARE(visibleCount(list), 2);
        const auto standalone = catalog.addStandalone(brush); QVERIFY(!standalone.isEmpty());
        panel.setTagFilter(ToolNavigationPanel::untaggedTagFilter()); QCOMPARE(visibleCount(list), 1);
        QVERIFY(catalog.setToolTags({brush}, {"__untagged__"})); QCOMPARE(visibleCount(list), 0);
        panel.setTagFilter("__untagged__"); QCOMPARE(visibleCount(list), 1);
        auto *tags = panel.findChild<QComboBox *>("toolTagFilter");
        QVERIFY(tags->findData("__untagged__") >= 0); QVERIFY(tags->findData(ToolNavigationPanel::untaggedTagFilter()) >= 0);
    }

    void iconsOnlyKeepsNamesPreviewsAndManageActions()
    {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        ToolNavigationPanel panel(&catalog, false); panel.resize(240, 420); panel.show(); QTest::qWait(10);
        auto *list = panel.findChild<QListWidget *>("toolEntries"); const auto active = catalog.activeToolId();
        const auto preview = findItem(list, active)->icon().cacheKey();
        auto *action = panel.findChild<QAction *>("toolListIconsOnly"); QVERIFY(action); action->trigger();
        QVERIFY(panel.iconsOnly()); QCOMPARE(list->viewMode(), QListView::IconMode);
        QCOMPARE(list->iconSize(), QSize(46, 24)); QCOMPARE(list->gridSize(), QSize(52, ui::metrics::railTarget + 2));
        QCOMPARE(findItem(list, active)->icon().cacheKey(), preview); QCOMPARE(catalog.activeToolId(), active);
        QCOMPARE(list->horizontalScrollBar()->maximum(), 0); QVERIFY(!list->dragEnabled());
        QCOMPARE(findItem(list, active)->data(Qt::AccessibleTextRole).toString(), catalog.tool(active)->name);
        QVERIFY(!findItem(list, active)->toolTip().isEmpty());
        QTest::qWait(10); QVERIFY(list->visualItemRect(list->item(1)).left() > list->visualItemRect(list->item(0)).left());
        QSignalSpy manage(&panel, &ToolNavigationPanel::manageToolsRequested);
        panel.findChild<QAction *>("toolListManage")->trigger(); QCOMPARE(manage.count(), 1);
        QCOMPARE(manage.at(0).at(0).toString(), catalog.selectedEntryId()); QCOMPARE(manage.at(0).at(1).toString(), active);
        QTimer::singleShot(0, &panel, [] {
            auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
            dialog->setTextValue("Ink tile"); dialog->accept();
        });
        QTest::keyClick(list, Qt::Key_F2); QCOMPARE(catalog.tool(active)->name, QString("Ink tile"));
        panel.setIconsOnly(false); QCOMPARE(list->viewMode(), QListView::ListMode); QVERIFY(list->dragEnabled());
    }

    void metadataUpdatesKeepBrushPreviewAndShowColorAndCustomGroupImage()
    {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        ToolNavigationPanel tools(&catalog, false), toolbox(&catalog, true);
        tools.resize(240, 420); toolbox.resize(48, 700); tools.show(); toolbox.show(); QTest::qWait(10);
        auto *list = tools.findChild<QListWidget *>("toolEntries"), *groups = toolbox.findChild<QListWidget *>("toolboxEntries");
        const auto id = catalog.activeToolId(); const auto cached = findItem(list, id)->icon().cacheKey();
        QVERIFY(catalog.setToolTags({id}, {"Ink", "Favorites"})); QVERIFY(catalog.setToolColor({id}, QColor("#ffcc00")));
        QCOMPARE(findItem(list, id)->icon().cacheKey(), cached);
        QCOMPARE(findItem(list, id)->data(Qt::UserRole + 1).value<QColor>(), QColor("#ffcc00"));
        QVERIFY(findItem(list, id)->toolTip().contains("Favorites"));
        const auto group = catalog.selectedEntryId();
        QVERIFY(catalog.setEntryColor(group, QColor("#ff3366")));
        QImage custom(80, 20, QImage::Format_ARGB32_Premultiplied); custom.fill(QColor("#00ff00"));
        QVERIFY(catalog.setEntryIcon(group, "folder-open", custom));
        auto *entry = findItem(groups, group); QCOMPARE(entry->data(Qt::UserRole + 1).value<QColor>(), QColor("#ff3366"));
        const auto icon = entry->icon().pixmap(QSize(28, 28)).toImage();
        int left = icon.width(), right = -1, top = icon.height(), bottom = -1;
        for (int y = 0; y < icon.height(); ++y) for (int x = 0; x < icon.width(); ++x) {
            const auto pixel = icon.pixelColor(x, y); if (pixel.green() < 200 || pixel.red() > 30 || pixel.blue() > 30) continue;
            left = qMin(left, x); right = qMax(right, x); top = qMin(top, y); bottom = qMax(bottom, y);
        }
        QVERIFY(right > left); QVERIFY(bottom > top); QVERIFY(right - left >= 3 * (bottom - top));
        QCOMPARE(findItem(list, id)->icon().cacheKey(), cached);
        catalog.history()->undo(); QCOMPARE(catalog.entry(group)->customIcon.isNull(), true);
    }

    void filteredNavigationCancellationKeepsHiddenActiveToolAndFilter()
    {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        const auto brush = firstTool(catalog, Canvas::Tool::Brush), rectangle = firstTool(catalog, Canvas::Tool::Rectangle);
        catalog.addGroup("Working tools", "paintbrush", {brush, rectangle});
        QVERIFY(catalog.setToolTags({rectangle}, {"Geometry"})); QVERIFY(catalog.activateTool(brush));
        ToolNavigationPanel panel(&catalog, false); panel.resize(240, 420); panel.show(); QTest::qWait(10);
        panel.setTagFilter("Geometry");
        auto *list = panel.findChild<QListWidget *>("toolEntries");
        int requests = 0; catalog.setActivationGuard([&requests] { ++requests; return false; });
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(findItem(list, rectangle)).center());
        QCOMPARE(requests, 1); QCOMPARE(catalog.activeToolId(), brush); QCOMPARE(panel.tagFilter(), QString("Geometry"));
        QVERIFY(!list->currentItem()); QVERIFY(!panel.findChild<QToolButton *>("toolListRemove")->isEnabled());
        panel.setIconsOnly(true); QCOMPARE(requests, 1); QCOMPARE(catalog.activeToolId(), brush);
        panel.setTagFilter({}); QCOMPARE(requests, 1);
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), brush);
    }

    void contextTagsAssignAndRemoveWithoutTouchingBrushSettings()
    {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        const auto brush = firstTool(catalog, Canvas::Tool::Brush), rectangle = firstTool(catalog, Canvas::Tool::Rectangle);
        QVERIFY(catalog.setToolTags({rectangle}, {"Favorite"})); QVERIFY(catalog.activateTool(brush));
        ToolNavigationPanel panel(&catalog, false); panel.resize(240, 420); panel.show(); QTest::qWait(10);
        auto *list = panel.findChild<QListWidget *>("toolEntries");
        const auto state = catalog.tool(brush)->state;
        auto toggle = [&] {
            QTimer::singleShot(0, list, [] {
                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget()); QVERIFY(menu);
                auto *tags = menu->findChild<QMenu *>("toolTagsMenu"); QVERIFY(tags);
                for (auto *action : tags->actions()) if (action->text() == "Favorite") { action->trigger(); break; }
                menu->close();
            });
            QMetaObject::invokeMethod(list, "customContextMenuRequested", Qt::DirectConnection,
                Q_ARG(QPoint, list->visualItemRect(list->currentItem()).center()));
        };
        toggle(); QCOMPARE(catalog.tool(brush)->tags, QStringList{"Favorite"});
        toggle(); QVERIFY(catalog.tool(brush)->tags.isEmpty()); QVERIFY(catalog.tool(brush)->state == state);
        QTimer::singleShot(0, list, [list] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget()); QVERIFY(menu);
            auto *action = menu->findChild<QAction *>("newToolTag"); QVERIFY(action);
            QTimer::singleShot(0, list, [] {
                auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
                dialog->setTextValue("Backgrounds"); dialog->accept();
            });
            action->trigger(); menu->close();
        });
        QMetaObject::invokeMethod(list, "customContextMenuRequested", Qt::DirectConnection,
            Q_ARG(QPoint, list->visualItemRect(list->currentItem()).center()));
        QCOMPARE(catalog.tool(brush)->tags, QStringList{"Backgrounds"}); QVERIFY(catalog.tool(brush)->state == state);
        QCOMPARE(catalog.activeToolId(), brush);
    }

    void renderPreview()
    {
        if (qEnvironmentVariableIsEmpty("TOON_UI_PREVIEW")) QSKIP("Preview output is opt-in");
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        catalog.addGroup("Background drawing", "image", {firstTool(catalog, Canvas::Tool::Brush), firstTool(catalog, Canvas::Tool::Line), firstTool(catalog, Canvas::Tool::Rectangle)});
        QWidget gallery; auto *layout = new QHBoxLayout(&gallery);
        for (bool toolbox : {true, false}) {
            auto *panel = new ToolNavigationPanel(&catalog, toolbox);
            panel->setFixedHeight(650); if (!toolbox) panel->setFixedWidth(240);
            layout->addWidget(panel);
        }
        catalog.selectEntry("group/drawing");
        gallery.show(); QTest::qWait(20); const auto output = qEnvironmentVariable("TOON_UI_PREVIEW"); QDir().mkpath(output);
        QVERIFY(gallery.grab().save(output + "/tool-navigation.png"));
        catalog.selectEntry("group/guides"); QTest::qWait(20);
        QVERIFY(gallery.grab().save(output + "/tool-navigation-guides.png"));
        catalog.selectEntry("group/drawing");
        const auto brush = firstTool(catalog, Canvas::Tool::Brush);
        QVERIFY(catalog.setToolTags({brush}, {"Ink", "Favorites"}));
        QVERIFY(catalog.setToolColor({brush}, QColor("#f6c644")));
        QVERIFY(catalog.setEntryTags("group/drawing", {"Production"}));
        QVERIFY(catalog.setEntryColor("group/drawing", QColor("#47b5a0")));
        QImage custom(80, 40, QImage::Format_ARGB32_Premultiplied); custom.fill(QColor("#47b5a0"));
        QVERIFY(catalog.setEntryIcon("group/drawing", "paintbrush", custom));
        auto *tools = gallery.findChild<ToolNavigationPanel *>("toolListContent"); QVERIFY(tools);
        tools->setTagFilter("Production"); QTest::qWait(20);
        QVERIFY(gallery.grab().save(output + "/tool-navigation-tags.png"));
        tools->setIconsOnly(true); QTest::qWait(20);
        QVERIFY(gallery.grab().save(output + "/tool-navigation-icons.png"));
        AddToolsDialog dialog(&catalog); dialog.findChild<QLineEdit *>("newGroupName")->setText("Background drawing"); dialog.show(); QTest::qWait(20);
        QVERIFY(dialog.grab().save(output + "/add-tools.png"));
        dialog.hide(); tools->setTagFilter({}); tools->setIconsOnly(false);
        for (const auto &family : {QString("grass-and-ground-cover"), QString("soft-paint-and-spray"), QString("painted-color-stamps")}) {
            QVERIFY(catalog.selectEntry("group/catalog/" + family));
            QTest::qWait(180);
            QVERIFY(gallery.grab().save(output + "/catalog-" + family + ".png"));
        }
    }
};
QTEST_MAIN(ToolNavigationPanelTests)
#include "test_tool_navigation_panel.moc"
