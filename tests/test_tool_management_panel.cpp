#include "tool_management_panel.h"
#include "tool_catalog.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSplitter>
#include <QStyle>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

using namespace toon;
namespace {
QListWidgetItem *findItem(QListWidget *list, const QString &id) {
    for (int i = 0; i < list->count(); ++i) if (list->item(i)->data(Qt::UserRole).toString() == id) return list->item(i);
    return nullptr;
}
QToolButton *button(QWidget &panel, const char *id) { return panel.findChild<QToolButton *>(id); }
void commit(QObject *field) { QVERIFY(QMetaObject::invokeMethod(field, "editingFinished")); }
void selectTools(ToolManagementPanel &panel, const QStringList &ids) {
    panel.findChild<QTabWidget *>("managementTabs")->setCurrentIndex(1);
    auto *list = panel.findChild<QListWidget *>("managedTools"); list->clearSelection();
    for (const auto &id : ids) { auto *item = findItem(list, id); QVERIFY(item); item->setSelected(true); }
}
QString makeIcon(const QString &path, QSize size = {640, 320}) {
    QImage image(size, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    QPainter painter(&image); painter.fillRect(image.rect().adjusted(size.width()/8, size.height()/8, -size.width()/8, -size.height()/8), QColor("#68B899"));
    painter.end(); return image.save(path) ? path : QString();
}
}
class ToolManagementPanelTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void browsingAndMetadataNeverActivatePaintingTools() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        ToolManagementPanel panel(&catalog); panel.resize(480, 680); panel.show();
        const auto active = catalog.activeToolId(), selected = catalog.selectedEntryId();
        QSignalSpy activation(&catalog, &ToolCatalog::activationRequested);
        panel.focusEntry("group/shapes");
        auto *groups = panel.findChild<QListWidget *>("managedGroups"); groups->setCurrentItem(findItem(groups, "group/color"));
        QCOMPARE(catalog.activeToolId(), active); QCOMPARE(catalog.selectedEntryId(), selected);
        auto *name = panel.findChild<QLineEdit *>("manageGroupName"); name->setText("Color studies"); commit(name);
        QCOMPARE(catalog.entry("group/color")->name, QString("Color studies"));
        auto *tags = panel.findChild<QLineEdit *>("manageGroupTags"); tags->setText("Paint,  Favorites, paint"); commit(tags);
        QCOMPARE(catalog.entry("group/color")->tags, (QStringList{"Paint", "Favorites"}));
        button(panel, "manageGroupColorPreset1")->click(); QCOMPARE(catalog.entry("group/color")->color, QColor("#58BB94"));
        button(panel, "manageGroupIcon_image")->click(); QCOMPARE(catalog.entry("group/color")->icon, QString("image"));
        panel.focusEntry("entry/hand"); QCOMPARE(panel.findChild<QTabWidget *>("managementTabs")->currentIndex(), 1);
        QVERIFY(findItem(panel.findChild<QListWidget *>("managedTools"), "hand")->isSelected());
        panel.focusEntry("group/shapes", "rectangle");
        QVERIFY(findItem(panel.findChild<QListWidget *>("managedTools"), "rectangle")->isSelected());
        QCOMPARE(activation.count(), 0); QCOMPARE(catalog.activeToolId(), active); QCOMPARE(catalog.selectedEntryId(), selected);
    }
    void batchTagsPreserveOtherTagsAndColorIsOneUndo() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        QVERIFY(catalog.setToolTags({"line"}, {"Background"})); QVERIFY(catalog.setToolTags({"rectangle"}, {"Layout"}));
        ToolManagementPanel panel(&catalog); selectTools(panel, {"line", "rectangle"});
        const auto active = catalog.activeToolId(); QSignalSpy activation(&catalog, &ToolCatalog::activationRequested);
        auto *tag = panel.findChild<QLineEdit *>("manageToolTag"); const auto before = catalog.history()->count();
        tag->setText("Favorite"); button(panel, "manageAddTag")->click();
        QCOMPARE(catalog.history()->count(), before + 1);
        QCOMPARE(catalog.tool("line")->tags, (QStringList{"Background", "Favorite"}));
        QCOMPARE(catalog.tool("rectangle")->tags, (QStringList{"Layout", "Favorite"}));
        tag->setText("FAVORITE"); button(panel, "manageRemoveTag")->click();
        QCOMPARE(catalog.tool("line")->tags, QStringList{"Background"}); QCOMPARE(catalog.tool("rectangle")->tags, QStringList{"Layout"});
        const auto colorHistory = catalog.history()->count(); button(panel, "manageToolColorPreset3")->click();
        QCOMPARE(catalog.history()->count(), colorHistory + 1);
        QCOMPARE(catalog.tool("line")->color, QColor("#A78BF4")); QCOMPARE(catalog.tool("rectangle")->color, QColor("#A78BF4"));
        catalog.history()->undo(); QVERIFY(!catalog.tool("line")->color.isValid()); QVERIFY(!catalog.tool("rectangle")->color.isValid());
        catalog.history()->redo(); button(panel, "manageToolColorClear")->click(); QVERIFY(!catalog.tool("line")->color.isValid());
        const auto validHistory = catalog.history()->count(); tag->clear(); button(panel, "manageAddTag")->click();
        QCOMPARE(catalog.history()->count(), validHistory); QVERIFY(!panel.findChild<QLabel *>("toolManagementError")->text().isEmpty());
        QCOMPARE(activation.count(), 0); QCOMPARE(catalog.activeToolId(), active);
    }
    void filteredOutSelectionsAreExcludedFromBatchEdits() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        ToolManagementPanel panel(&catalog); selectTools(panel, {"line", "rectangle"});
        panel.findChild<QLineEdit *>("manageToolSearch")->setText("Rectangle");
        panel.findChild<QLineEdit *>("manageToolTag")->setText("Geometry"); button(panel, "manageAddTag")->click();
        QVERIFY(catalog.tool("line")->tags.isEmpty()); QCOMPARE(catalog.tool("rectangle")->tags, QStringList{"Geometry"});
        panel.focusEntry("group/shapes");
        panel.findChild<QLineEdit *>("manageGroupSearch")->setText("no group matches this");
        QVERIFY(!button(panel, "manageRemoveGroup")->isEnabled());
        QVERIFY(!panel.findChild<QLineEdit *>("manageGroupName")->isEnabled());
        QVERIFY(panel.findChild<QLineEdit *>("manageGroupName")->text().isEmpty());
    }
    void focusOutCommitsKeepRowsStableAndUnrelatedRefreshKeepsDraftText() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        ToolManagementPanel panel(&catalog); panel.resize(480, 680); panel.show(); panel.activateWindow(); panel.focusEntry("group/shapes");
        auto *groups = panel.findChild<QListWidget *>("managedGroups");
        auto *originalRow = findItem(groups, "group/shapes"); auto *colorRow = findItem(groups, "group/color");
        auto *name = panel.findChild<QLineEdit *>("manageGroupName"); name->setFocus(); name->setText("Geometry draft");
        QVERIFY(catalog.setToolColor({"line"}, QColor("#58BB94")));
        QCOMPARE(name->text(), QString("Geometry draft")); QCOMPARE(findItem(groups, "group/shapes"), originalRow);
        groups->scrollToItem(colorRow); QTest::qWait(10);
        QTest::mouseClick(groups->viewport(), Qt::LeftButton, Qt::NoModifier, groups->visualItemRect(colorRow).center());
        QCOMPARE(catalog.entry("group/shapes")->name, QString("Geometry draft"));
        QCOMPARE(groups->currentItem(), colorRow); QCOMPARE(findItem(groups, "group/shapes"), originalRow);
        QCOMPARE(name->text(), catalog.entry("group/color")->name);
    }
    void groupsCreateAndMembershipCommitsAtomically() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        ToolManagementPanel panel(&catalog); panel.show(); button(panel, "manageAddGroup")->click();
        const auto id = catalog.selectedEntryId(); QVERIFY(catalog.entry(id)); QVERIFY(catalog.entry(id)->tools.isEmpty());
        auto *name = panel.findChild<QLineEdit *>("manageGroupName"); name->setText("Backgrounds"); commit(name);
        QCOMPARE(catalog.entry(id)->name, QString("Backgrounds")); const auto before = catalog.history()->count();
        QTimer::singleShot(0, &panel, [&] {
            auto *dialog = panel.findChild<QDialog *>("groupMembershipDialog"); QVERIFY(dialog);
            auto *choices = dialog->findChild<QListWidget *>("membershipTools");
            findItem(choices, "line")->setCheckState(Qt::Checked); findItem(choices, "rectangle")->setCheckState(Qt::Checked);
            dialog->findChild<QPushButton *>("applyGroupMembership")->click();
        });
        button(panel, "manageMembership")->click(); QCOMPARE(catalog.entry(id)->tools, (QStringList{"line", "rectangle"}));
        QCOMPARE(catalog.history()->count(), before + 1); catalog.history()->undo(); QVERIFY(catalog.entry(id)->tools.isEmpty()); catalog.history()->redo();
        QTimer::singleShot(0, &panel, [&] {
            auto *dialog = panel.findChild<QDialog *>("groupMembershipDialog"); QVERIFY(dialog);
            findItem(dialog->findChild<QListWidget *>("membershipTools"), "line")->setCheckState(Qt::Unchecked); dialog->reject();
        });
        button(panel, "manageMembership")->click(); QCOMPARE(catalog.entry(id)->tools, (QStringList{"line", "rectangle"}));
        button(panel, "manageRemoveGroup")->click(); QVERIFY(!catalog.entry(id)); catalog.history()->undo(); QVERIFY(catalog.entry(id));
    }
    void iconsAreBoundedEmbeddedResettableAndInvalidImportsDoNotMutate() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        ToolManagementPanel panel(&catalog); panel.focusEntry("group/shapes");
        const auto path = makeIcon(dir.filePath("icon.png")); QVERIFY(!path.isEmpty());
        const auto active = catalog.activeToolId(); QSignalSpy activation(&catalog, &ToolCatalog::activationRequested);
        QVERIFY(panel.importGroupIcon(path)); QCOMPARE(catalog.entry("group/shapes")->customIcon.size(), QSize(256, 128));
        QVERIFY(catalog.entry("group/shapes")->customIcon.hasAlphaChannel()); QVERIFY(catalog.save());
        ToolCatalog restored(&library, catalog.path()); QVERIFY(restored.load());
        QCOMPARE(restored.entry("group/shapes")->customIcon, catalog.entry("group/shapes")->customIcon);
        const auto history = catalog.history()->count();
        QVERIFY(!panel.importGroupIcon(dir.filePath("missing.png")));
        QFile invalid(dir.filePath("invalid.png")); QVERIFY(invalid.open(QIODevice::WriteOnly)); invalid.write("not an image"); invalid.close();
        QVERIFY(!panel.importGroupIcon(invalid.fileName()));
        const auto oversized = makeIcon(dir.filePath("wide.png"), {4097, 1}); QVERIFY(!oversized.isEmpty()); QVERIFY(!panel.importGroupIcon(oversized));
        QFile large(dir.filePath("large.png")); QVERIFY(large.open(QIODevice::WriteOnly)); QVERIFY(large.resize(8 * 1024 * 1024 + 1)); large.close();
        QVERIFY(!panel.importGroupIcon(large.fileName())); QCOMPARE(catalog.history()->count(), history);
        QVERIFY(!panel.findChild<QLabel *>("toolManagementError")->text().isEmpty());
        button(panel, "manageClearIcon")->click(); QVERIFY(catalog.entry("group/shapes")->customIcon.isNull());
        catalog.history()->undo(); QVERIFY(!catalog.entry("group/shapes")->customIcon.isNull());
        button(panel, "manageGroupIcon_palette")->click(); QCOMPARE(catalog.entry("group/shapes")->icon, QString("palette"));
        QVERIFY(catalog.entry("group/shapes")->customIcon.isNull());
        const auto small = makeIcon(dir.filePath("small.png"), {24, 12}); QVERIFY(panel.importGroupIcon(small));
        QCOMPARE(catalog.entry("group/shapes")->customIcon.size(), QSize(24, 12));
        QCOMPARE(activation.count(), 0); QCOMPARE(catalog.activeToolId(), active);
    }
    void metadataAndGroupControlsFitNarrowPanels() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        ToolManagementPanel panel(&catalog); panel.resize(360, 600); panel.show(); QTest::qWait(20);
        QCOMPARE(panel.style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay, nullptr, &panel), ui::tooltipDelayMs);
        auto *tabs = panel.findChild<QTabWidget *>("managementTabs");
        for (int width : {240, 300, 360}) for (int height : {260, 600}) {
            panel.resize(width, height); QCoreApplication::processEvents();
            QCOMPARE(panel.size(), QSize(width, height));
            for (int tab = 0; tab < tabs->count(); ++tab) {
                tabs->setCurrentIndex(tab); QTest::qWait(10);
                auto *scroll = tabs->widget(tab)->findChild<QScrollArea *>(); QVERIFY(scroll);
                QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
                QVERIFY(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
                auto *search = panel.findChild<QLineEdit *>(tab == 0 ? "manageGroupSearch" : "manageToolSearch");
                const auto searchPosition = search->mapTo(&panel, QPoint());
                const auto addPosition = button(panel, "manageAddGroup")->mapTo(&panel, QPoint());
                const auto removePosition = button(panel, "manageRemoveGroup")->mapTo(&panel, QPoint());
                QVERIFY(panel.rect().contains(QRect(searchPosition, search->size())));
                QVERIFY(searchPosition.y() < scroll->mapTo(&panel, QPoint()).y());
                if (height == 260) QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
                scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
                QCoreApplication::processEvents();
                QCOMPARE(search->mapTo(&panel, QPoint()), searchPosition);
                if (tab == 0) {
                    QCOMPARE(button(panel, "manageAddGroup")->mapTo(&panel, QPoint()), addPosition);
                    QCOMPARE(button(panel, "manageRemoveGroup")->mapTo(&panel, QPoint()), removePosition);
                    QVERIFY(panel.rect().contains(QRect(addPosition, button(panel, "manageAddGroup")->size())));
                    QVERIFY(panel.rect().contains(QRect(removePosition, button(panel, "manageRemoveGroup")->size())));
                }
                scroll->verticalScrollBar()->setValue(0);
            }
        }
        for (auto *control : panel.findChildren<QToolButton *>()) {
            QVERIFY2(!control->toolTip().isEmpty(), qPrintable(control->objectName()));
            QVERIFY2(!control->accessibleName().isEmpty(), qPrintable(control->objectName()));
            if (control->objectName().startsWith("manage") && !qobject_cast<QLineEdit *>(control->parentWidget())) {
                QCOMPARE(control->size(), QSize(ui::metrics::actionTarget, ui::metrics::actionTarget));
                const auto expected = control->objectName().startsWith("manageGroupIcon_")
                    ? QSize(ui::metrics::primaryIcon, ui::metrics::primaryIcon) : QSize(ui::metrics::actionIcon, ui::metrics::actionIcon);
                QCOMPARE(control->iconSize(), expected);
            }
        }
        panel.resize(300, 600); QTest::qWait(10);
        auto *tools = panel.findChild<QListWidget *>("managedTools"); const auto previousHeight = tools->height();
        panel.resize(300, 800); QTest::qWait(10);
        QVERIFY(tools->height() >= previousHeight + 60);
        tabs->setCurrentIndex(0); panel.resize(300, 600); QTest::qWait(10);
        auto *groups = panel.findChild<QListWidget *>("managedGroups"); const auto previousGroupsHeight = groups->height();
        panel.resize(300, 800); QTest::qWait(10);
        QVERIFY(groups->height() >= previousGroupsHeight + 60);
    }
    void browsingAndMembershipStayReachableWhileMetadataScrolls() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        ToolManagementPanel panel(&catalog); panel.resize(240, 300); panel.show(); panel.focusEntry("group/shapes");
        QCoreApplication::processEvents();
        auto *groups = panel.findChild<QListWidget *>("managedGroups");
        auto *scroll = panel.findChild<QScrollArea *>("manageGroupsScroll");
        auto *membership = button(panel, "manageMembership");
        QVERIFY(groups && scroll && membership); QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
        const auto listPosition = groups->mapTo(&panel, QPoint());
        const auto memberPosition = membership->mapTo(&panel, QPoint());
        const auto active = catalog.activeToolId(), entry = catalog.selectedEntryId();
        const auto history = catalog.history()->count(); auto *selectedRow = groups->currentItem();
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        QCoreApplication::processEvents();
        QCOMPARE(groups->mapTo(&panel, QPoint()), listPosition);
        QCOMPARE(membership->mapTo(&panel, QPoint()), memberPosition);
        QVERIFY(panel.rect().contains(QRect(listPosition, groups->size())));
        QVERIFY(panel.rect().contains(QRect(memberPosition, membership->size())));
        QVERIFY(groups->height() >= 60); QVERIFY(membership->isEnabled());
        QCOMPARE(groups->currentItem(), selectedRow);
        QCOMPARE(catalog.activeToolId(), active); QCOMPARE(catalog.selectedEntryId(), entry);
        QCOMPARE(catalog.history()->count(), history);
        panel.findChild<QLineEdit *>("manageGroupSearch")->setText("no group matches this");
        QVERIFY(!membership->isEnabled());
        QCOMPARE(catalog.history()->count(), history);
    }
    void sideBySideLayoutAndLargerFontsPreserveDraftsAndSelection() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        ToolManagementPanel panel(&catalog); panel.resize(360, 600); panel.show(); panel.activateWindow(); panel.focusEntry("group/shapes");
        auto *groups = panel.findChild<QListWidget *>("managedGroups");
        auto *name = panel.findChild<QLineEdit *>("manageGroupName");
        auto *split = panel.findChild<QSplitter *>("manageGroupsSplit");
        auto *scroll = panel.findChild<QScrollArea *>("manageGroupsScroll");
        QVERIFY(groups && name && split && scroll);
        auto *selected = groups->currentItem(); const auto original = catalog.entry("group/shapes")->name;
        const auto active = catalog.activeToolId(); const auto history = catalog.history()->count();
        name->setFocus(); QTRY_VERIFY(name->hasFocus()); name->selectAll(); QTest::keyClicks(name, "Environment draft");
        panel.resize(660, 480); QCoreApplication::processEvents();
        QCOMPARE(split->orientation(), Qt::Horizontal);
        QVERIFY(groups->mapTo(&panel, QPoint()).x() + groups->width() <= scroll->mapTo(&panel, QPoint()).x());
        QCOMPARE(groups->currentItem(), selected); QCOMPARE(name->text(), QString("Environment draft")); QVERIFY(name->hasFocus());
        QFont font = panel.font(); font.setPixelSize(24); panel.setFont(font); QCoreApplication::processEvents();
        QVERIFY(groups->visualItemRect(selected).height() >= groups->fontMetrics().height() + ui::metrics::sectionGap);
        QCOMPARE(name->text(), QString("Environment draft")); QVERIFY(name->hasFocus());
        panel.resize(300, 600); QCoreApplication::processEvents();
        QCOMPARE(split->orientation(), Qt::Vertical);
        QCOMPARE(groups->currentItem(), selected); QCOMPARE(name->text(), QString("Environment draft")); QVERIFY(name->hasFocus());
        QCOMPARE(catalog.entry("group/shapes")->name, original); QCOMPARE(catalog.activeToolId(), active);
        QCOMPARE(catalog.history()->count(), history);
        commit(name); QCOMPARE(catalog.entry("group/shapes")->name, QString("Environment draft"));
        QCOMPARE(catalog.history()->count(), history + 1);
    }
    void resizingKeepsDraftsAndErrorsKeepHeaderGeometryStable() {
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        ToolManagementPanel panel(&catalog); panel.resize(360, 600); panel.show(); panel.activateWindow(); panel.focusEntry("group/shapes");
        auto *name = panel.findChild<QLineEdit *>("manageGroupName");
        auto *row = findItem(panel.findChild<QListWidget *>("managedGroups"), "group/shapes");
        name->setFocus(); QTRY_VERIFY(name->hasFocus()); name->selectAll(); QTest::keyClicks(name, "Layout draft");
        const auto originalName = catalog.entry("group/shapes")->name;
        for (int width : {240, 300, 360}) {
            panel.resize(width, 600); QCoreApplication::processEvents();
            QCOMPARE(name->text(), QString("Layout draft")); QVERIFY(name->hasFocus());
            QCOMPARE(catalog.entry("group/shapes")->name, originalName);
            QCOMPARE(findItem(panel.findChild<QListWidget *>("managedGroups"), "group/shapes"), row);
        }
        selectTools(panel, {"line", "rectangle"});
        auto *search = panel.findChild<QLineEdit *>("manageToolSearch");
        auto *scroll = panel.findChild<QScrollArea *>("manageToolsScroll");
        QCoreApplication::processEvents(); const auto searchGeometry = search->geometry(), scrollGeometry = scroll->geometry();
        auto *tag = panel.findChild<QLineEdit *>("manageToolTag"); tag->clear(); button(panel, "manageAddTag")->click();
        auto *error = panel.findChild<QLabel *>("toolManagementError"); QVERIFY(!error->text().isEmpty());
        QVERIFY(error->toolTip().contains("without commas")); QCOMPARE(error->accessibleName(), error->toolTip());
        QCoreApplication::processEvents(); QCOMPARE(search->geometry(), searchGeometry); QCOMPARE(scroll->geometry(), scrollGeometry);
        tag->setText("Favorite"); button(panel, "manageAddTag")->click();
        QCoreApplication::processEvents(); QVERIFY(error->text().isEmpty());
        QCOMPARE(search->geometry(), searchGeometry); QCOMPARE(scroll->geometry(), scrollGeometry);
    }
    void managerPreviews() {
        if (qEnvironmentVariableIsEmpty("TOON_UI_PREVIEW")) QSKIP("Preview output is opt-in");
        QTemporaryDir dir; BrushLibrary library(dir.filePath("brushes.json")); QVERIFY(library.load());
        ToolCatalog catalog(&library, dir.filePath("tools.json")); QVERIFY(catalog.load());
        QVERIFY(catalog.renameEntry("group/shapes", "Background construction"));
        QVERIFY(catalog.setEntryTags("group/shapes", {"Backgrounds", "Layout"})); QVERIFY(catalog.setEntryColor("group/shapes", QColor("#58BB94")));
        QVERIFY(catalog.setToolTags({"line", "rectangle"}, {"Backgrounds", "Favorite"}));
        QVERIFY(catalog.setToolColor({"line", "rectangle"}, QColor("#A78BF4")));
        ToolManagementPanel panel(&catalog); panel.resize(360, 600); panel.show(); panel.focusEntry("group/shapes");
        QVERIFY(panel.importGroupIcon(makeIcon(dir.filePath("icon.png")))); QTest::qWait(20);
        const auto output = qEnvironmentVariable("TOON_UI_PREVIEW"); QDir().mkpath(output);
        QVERIFY(panel.grab().save(output + "/tool-management-groups.png"));
        panel.resize(240, 600); QTest::qWait(20); QVERIFY(panel.grab().save(output + "/tool-management-narrow.png"));
        panel.resize(360, 600); selectTools(panel, {"line", "rectangle"}); QTest::qWait(20);
        QVERIFY(panel.grab().save(output + "/tool-management-tools.png"));
        panel.resize(240, 260); panel.focusEntry("group/shapes"); QTest::qWait(20);
        auto *scroll = panel.findChild<QScrollArea *>("manageGroupsScroll");
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum()); QTest::qWait(20);
        QVERIFY(panel.grab().save(output + "/tool-management-short.png"));
        panel.resize(660, 480); QTest::qWait(20); QVERIFY(panel.grab().save(output + "/tool-management-wide.png"));
        QFont font = panel.font(); font.setPixelSize(22); panel.setFont(font);
        panel.resize(360, 600); QTest::qWait(20); QVERIFY(panel.grab().save(output + "/tool-management-larger-font.png"));
    }
};
QTEST_MAIN(ToolManagementPanelTests)
#include "test_tool_management_panel.moc"
