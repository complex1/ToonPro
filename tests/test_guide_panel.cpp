#include "canvas.h"
#include "drawing_panels.h"
#include "guide_panel.h"
#include "main_window.h"
#include "tool_catalog.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include "value_slider.h"
#include "property_row.h"
#include <QAction>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollBar>
#include <QScrollArea>
#include <QSlider>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

using namespace toon;
namespace {
QToolButton *button(QWidget &panel, const char *id) { return panel.findChild<QToolButton *>(id); }
DrawingGuide selected(Canvas &canvas) {
    for (const auto &guide : canvas.drawingGuides()) if (guide.id == canvas.selectedGuideId()) return guide;
    return {};
}
void commit(QObject *field) { QVERIFY(QMetaObject::invokeMethod(field, "editingFinished")); }
void clickColumn(QTreeWidget *tree, int row, int column) {
    auto *item = tree->topLevelItem(row); QVERIFY(item);
    const auto rect = tree->visualItemRect(item);
    const auto x = tree->header()->sectionViewportPosition(column) + tree->columnWidth(column) / 2;
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, {x, rect.center().y()});
}
}
class GuidePanelTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }

    void typeTabsAndPlacementKeepCreationTransient() {
        Canvas canvas; GuidePanel panel(&canvas);
        QVERIFY(!button(panel, "addGuide")->isEnabled());
        canvas.setProject(Project::create()); canvas.setTool(Canvas::Tool::Guides);
        panel.resize(270, 650); panel.show(); QTest::qWait(10);
        auto *tabs = panel.findChild<QTabBar *>("guideTypes"); QVERIFY(tabs); QCOMPARE(tabs->count(), 7);
        QCOMPARE(tabs->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay, nullptr, tabs), ui::tooltipDelayMs);
        for (int i = 0; i < tabs->count(); ++i) {
            QVERIFY(!tabs->tabIcon(i).isNull()); QVERIFY(!tabs->tabToolTip(i).isEmpty());
            QVERIFY(!tabs->accessibleTabName(i).isEmpty()); QVERIFY(tabs->tabText(i).isEmpty());
        }
        tabs->setCurrentIndex(2); QCOMPARE(canvas.guideCreationType(), GuideType::TwoPoint);
        button(panel, "addGuide")->click(); QVERIFY(canvas.guideCreating());
        QVERIFY(!tabs->isEnabled()); QVERIFY(!button(panel, "addGuide")->isEnabled());
        QVERIFY(button(panel, "cancelGuideCreation")->isVisible());
        QVERIFY(panel.findChild<QLabel *>("guidePlacementStatus")->text().contains("3"));
        QCOMPARE(canvas.document()->history()->count(), 0);
        button(panel, "cancelGuideCreation")->click(); QVERIFY(!canvas.guideCreating());
        QCOMPARE(canvas.drawingGuides().size(), 0); QVERIFY(tabs->isEnabled());
    }

    void extendedTypePlacementAndProperties_data() {
        QTest::addColumn<int>("index"); QTest::addColumn<int>("typeValue");
        QTest::addColumn<QString>("placement");
        QTest::newRow("three-point") << 4 << int(GuideType::ThreePoint) << "three vanishing points";
        QTest::newRow("fisheye") << 5 << int(GuideType::Fisheye) << "curve height";
    }
    void ellipseHasDirectAccessibleTypeAndOnlyRelevantProperties() {
        Canvas canvas; canvas.setProject(Project::create()); canvas.setTool(Canvas::Tool::Guides);
        GuidePanel panel(&canvas); panel.resize(220, 540); panel.show(); QTest::qWait(10);
        auto *types = panel.findChild<QTabBar *>("guideTypes"); QCOMPARE(types->count(), 7);
        QVERIFY(types->tabRect(6).right() < types->width());
        QTest::mouseClick(types, Qt::LeftButton, Qt::NoModifier, types->tabRect(6).center());
        QCOMPARE(canvas.guideCreationType(), GuideType::Ellipse);
        QCOMPARE(types->accessibleTabName(6), QString("Ellipse ruler"));
        QVERIFY(types->tabToolTip(6).contains("12 screen pixels")); QVERIFY(!types->tabIcon(6).isNull());
        button(panel, "addGuide")->click(); QVERIFY(canvas.guideCreating());
        QVERIFY(panel.findChild<QLabel *>("guidePlacementStatus")->text().contains("ellipse height"));
        button(panel, "cancelGuideCreation")->click(); canvas.addDefaultGuide(GuideType::Ellipse);
        auto *density = panel.findChild<ValueSlider *>("guideDensity"); QVERIFY(!density->isVisibleTo(&panel));
        QVERIFY(button(panel, "guideVertical")->isHidden());
        auto *name = panel.findChild<QLineEdit *>("guideName"); name->setText("Eye construction"); commit(name);
        QCOMPARE(selected(canvas).name, QString("Eye construction"));
        auto *list = panel.findChild<QTreeWidget *>("guideList");
        clickColumn(list, 0, 2); QVERIFY(selected(canvas).locked);
        clickColumn(list, 0, 3); QVERIFY(!selected(canvas).snapping);
        clickColumn(list, 0, 1); QVERIFY(!selected(canvas).visible);
        canvas.document()->history()->undo(); QVERIFY(selected(canvas).visible);
        canvas.addDefaultGuide(GuideType::ParallelRuler); QVERIFY(density->isVisibleTo(&panel));
        const auto output = qEnvironmentVariable("TOON_UI_PREVIEW");
        if (!output.isEmpty()) {
            canvas.selectGuide(canvas.drawingGuides().first().id); QDir().mkpath(output);
            QVERIFY(panel.grab().save(output + "/ellipse-guide-properties.png"));
        }
    }
    void extendedTypePlacementAndProperties() {
        QFETCH(int, index); QFETCH(int, typeValue); QFETCH(QString, placement);
        const auto type = static_cast<GuideType>(typeValue);
        Canvas canvas; canvas.setProject(Project::create()); canvas.setTool(Canvas::Tool::Guides);
        GuidePanel panel(&canvas); panel.resize(270, 650); panel.show(); QTest::qWait(10);
        auto *tabs = panel.findChild<QTabBar *>("guideTypes");
        QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(index).center());
        QCOMPARE(canvas.guideCreationType(), type);
        QVERIFY(button(panel, "addGuide")->toolTip().contains(placement));
        if (type == GuideType::Fisheye) QVERIFY(button(panel, "addGuide")->toolTip().contains("Shift"));
        button(panel, "addGuide")->click(); QVERIFY(canvas.guideCreating());
        QVERIFY(panel.findChild<QLabel *>("guidePlacementStatus")->text().contains(placement));
        QCOMPARE(canvas.document()->history()->count(), 0);
        button(panel, "cancelGuideCreation")->click(); QVERIFY(!canvas.guideCreating());
        canvas.addDefaultGuide(type); QCOMPARE(canvas.drawingGuides().size(), 1);
        QCOMPARE(selected(canvas).type, type); QVERIFY(button(panel, "guideVertical")->isHidden());
        auto *density = panel.findChild<ValueSlider *>("guideDensity"); density->setValue(12); commit(density);
        QCOMPARE(selected(canvas).subdivisions, 12);
        auto *name = panel.findChild<QLineEdit *>("guideName"); name->setText("Camera study"); commit(name);
        QCOMPARE(selected(canvas).name, QString("Camera study"));
        button(panel, "guideCustomColor")->click();
        auto *hex = panel.findChild<QLineEdit *>("guideColorHex"); hex->setText("#77BFAA"); commit(hex);
        QCOMPARE(selected(canvas).color, QColor("#77BFAA"));
        button(panel, "guideLimited")->click(); QVERIFY(selected(canvas).limited);
        auto *width = panel.findChild<ValueSlider *>("guideAreaWidth"); width->setValue(1200); commit(width);
        QCOMPARE(selected(canvas).area.width(), 1200.);
        canvas.document()->history()->undo(); QCOMPARE(selected(canvas).area.width(), 1920.);
        QTest::qWait(10);
        QDir().mkpath("test-artifacts");
        const auto suffix = type == GuideType::ThreePoint ? "three-point" : "fisheye";
        QVERIFY(panel.grab().save(QString("test-artifacts/guide-properties-%1.png").arg(suffix)));
    }

    void multipleGuidesSelectionFlagsAndHistory() {
        Canvas canvas; canvas.setProject(Project::create()); canvas.setTool(Canvas::Tool::Guides);
        GuidePanel panel(&canvas); panel.resize(270, 650); panel.show();
        canvas.addDefaultGuide(GuideType::Perspective); const auto firstId = canvas.selectedGuideId();
        canvas.addDefaultGuide(GuideType::TwoPoint); const auto secondId = canvas.selectedGuideId();
        auto *tree = panel.findChild<QTreeWidget *>("guideList"); QVERIFY(tree); QCOMPARE(tree->topLevelItemCount(), 2);
        QVERIFY(firstId != secondId); QCOMPARE(tree->currentItem()->data(0, Qt::UserRole).toString(), secondId);
        tree->setCurrentItem(tree->topLevelItem(0)); QCOMPARE(canvas.selectedGuideId(), firstId);
        QCOMPARE(panel.findChild<QLineEdit *>("guideName")->text(), selected(canvas).name);
        QVERIFY(button(panel, "guideVertical")->isHidden());
        QTest::qWait(10); clickColumn(tree, 0, 1); QVERIFY(!selected(canvas).visible);
        clickColumn(tree, 0, 2); QVERIFY(selected(canvas).locked);
        clickColumn(tree, 0, 3); QVERIFY(!selected(canvas).snapping);
        canvas.document()->history()->undo(); QVERIFY(selected(canvas).snapping);
        button(panel, "duplicateGuide")->click(); QCOMPARE(canvas.drawingGuides().size(), 3);
        QVERIFY(canvas.selectedGuideId() != firstId); QVERIFY(canvas.selectedGuideId() != secondId);
        button(panel, "removeGuide")->click(); QCOMPARE(canvas.drawingGuides().size(), 2);
        canvas.document()->history()->undo(); QCOMPARE(canvas.drawingGuides().size(), 3);
        for (int row = 0; row < tree->topLevelItemCount(); ++row)
            for (int column = 1; column < 4; ++column) {
                QVERIFY(!tree->topLevelItem(row)->icon(column).isNull());
                QVERIFY(!tree->topLevelItem(row)->toolTip(column).isEmpty());
                QVERIFY(!tree->topLevelItem(row)->data(column, Qt::AccessibleTextRole).toString().isEmpty());
            }
    }

    void selectedPropertiesCommitOnceAndGlobalSettingsStayIndependent() {
        Canvas canvas; canvas.setProject(Project::create()); canvas.setTool(Canvas::Tool::Guides);
        canvas.addDefaultGuide(GuideType::TwoPoint);
        GuidePanel panel(&canvas); panel.resize(270, 680); panel.show();
        auto *name = panel.findChild<QLineEdit *>("guideName");
        const auto before = canvas.document()->history()->count();
        name->setText("Street corner"); QCOMPARE(canvas.document()->history()->count(), before);
        commit(name); QCOMPARE(selected(canvas).name, QString("Street corner"));
        QCOMPARE(canvas.document()->history()->count(), before + 1);
        auto *density = panel.findChild<ValueSlider *>("guideDensity"); density->setValue(14); commit(density);
        QCOMPARE(selected(canvas).subdivisions, 14);
        density->setValue(1); commit(density); QCOMPARE(selected(canvas).subdivisions, 1);
        QVERIFY(button(panel, "guideVertical")->isVisible()); button(panel, "guideVertical")->click();
        QVERIFY(!selected(canvas).vertical);
        button(panel, "guideLimited")->click(); QVERIFY(selected(canvas).limited);
        auto *width = panel.findChild<ValueSlider *>("guideAreaWidth"); width->setValue(650); commit(width);
        QCOMPARE(selected(canvas).area.width(), 650.);
        button(panel, "guideCustomColor")->click(); QVERIFY(selected(canvas).customColor);
        auto *hex = panel.findChild<QLineEdit *>("guideColorHex"); hex->setText("#B070ED"); commit(hex);
        QCOMPARE(selected(canvas).color, QColor("#B070ED"));
        auto *opacity = panel.findChild<ValueSlider *>("guideOpacity"); opacity->setValue(35); commit(opacity);
        QCOMPARE(selected(canvas).opacity, .35);
        auto *properties = panel.findChild<QTabWidget *>("guidePropertyTabs"); properties->setCurrentIndex(1);
        button(panel, "guidesSnapping")->click(); QVERIFY(canvas.guideSettings().snapping);
        button(panel, "guidesVisible")->click(); QVERIFY(!canvas.guideSettings().visible);
        button(panel, "guidesHandles")->click(); QVERIFY(!canvas.guideSettings().handles);
        auto *globalHex = panel.findChild<QLineEdit *>("guidesColorHex"); globalHex->setText("#55CC99"); commit(globalHex);
        QCOMPARE(canvas.guideSettings().color, QColor("#55CC99")); QCOMPARE(selected(canvas).color, QColor("#B070ED"));
        globalHex->setText("nope"); commit(globalHex); QCOMPARE(globalHex->text(), QString("#55CC99"));
        canvas.document()->history()->undo(); QCOMPARE(canvas.guideSettings().color, QColor("#5681ff"));
    }

    void numericSlidersCommitOneGuideEditAndKeepPreciseEntry() {
        Canvas canvas; canvas.setProject(Project::create()); canvas.setTool(Canvas::Tool::Guides);
        canvas.addDefaultGuide(GuideType::Perspective);
        GuidePanel panel(&canvas); panel.resize(240, 650); panel.show();
        auto *density = panel.findChild<ValueSlider *>("guideDensity"); QVERIFY(density);
        auto *track = density->findChild<QSlider *>(); QVERIFY(track);
        const int initial = selected(canvas).subdivisions;
        const int before = canvas.document()->history()->count();
        track->setSliderDown(true); track->setValue(10); track->setValue(18); track->setValue(24);
        QCOMPARE(selected(canvas).subdivisions, initial);
        QCOMPARE(canvas.document()->history()->count(), before);
        track->setSliderDown(false);
        QCOMPARE(selected(canvas).subdivisions, 24);
        QCOMPARE(canvas.document()->history()->count(), before + 1);
        canvas.document()->history()->undo(); QCOMPARE(selected(canvas).subdivisions, initial);
        QCOMPARE(density->value(), double(initial));
        auto *number = density->findChild<QDoubleSpinBox *>(); QVERIFY(number);
        number->setValue(13); QCOMPARE(selected(canvas).subdivisions, 13);
        button(panel, "guideLimited")->click();
        auto *width = panel.findChild<ValueSlider *>("guideAreaWidth"); QVERIFY(width);
        auto *widthNumber = width->findChild<QDoubleSpinBox *>(); QVERIFY(widthNumber);
        widthNumber->setValue(876.5); QCOMPARE(selected(canvas).area.width(), 876.5);
        for (auto *field : panel.findChildren<QDoubleSpinBox *>()) {
            auto *control = qobject_cast<ValueSlider *>(field->parentWidget()); QVERIFY(control);
            QVERIFY(control->findChild<QSlider *>());
        }
    }

    void guideSetsAppendWithoutReplacingExistingGuides() {
        QTemporaryDir temp; Canvas canvas; canvas.setProject(Project::create());
        canvas.addDefaultGuide(GuideType::Perspective); const auto original = canvas.drawingGuides().first();
        QString error; const auto path = temp.filePath("street.toonguides");
        QVERIFY2(saveGuideSet(canvas.drawingGuides(), canvas.guideSettings(), path, &error), qPrintable(error));
        QVector<DrawingGuide> imported; GuideSettings settings;
        QVERIFY2(loadGuideSet(path, &imported, &settings, &error), qPrintable(error));
        canvas.replaceGuideSet(imported, settings, true); QCOMPARE(canvas.drawingGuides().size(), 2);
        QCOMPARE(canvas.drawingGuides().first(), original);
        QVERIFY(canvas.drawingGuides().last().id != original.id);
        QCOMPARE(canvas.drawingGuides().last().points, original.points);
        canvas.document()->history()->undo(); QCOMPARE(canvas.drawingGuides().size(), 1);
    }

    void ordinaryBrushDabsDoNotRebuildGuideProperties() {
        Canvas canvas; canvas.setProject(Project::create()); canvas.addDefaultGuide(GuideType::Perspective);
        GuidePanel panel(&canvas); auto *name = panel.findChild<QLineEdit *>("guideName");
        name->setText("Uncommitted field");
        QVERIFY(canvas.document()->beginStroke({100, 100}, 1, canvas.brush, false));
        QVERIFY(panel.findChild<QWidget *>("guidePanelBody")->isEnabled());
        canvas.document()->continueStroke({120, 120}, 1);
        QCOMPARE(name->text(), QString("Uncommitted field"));
        canvas.document()->endStroke();
        QVERIFY(panel.findChild<QWidget *>("guidePanelBody")->isEnabled()); QCOMPARE(name->text(), QString("Uncommitted field"));
        QVERIFY(selected(canvas).name != name->text());
    }

    void backspaceDeletesGuidesWithoutClearingSelectedPixels() {
        QTemporaryDir temp; MainWindow window(temp.filePath("workspace.ini"));
        auto project = Project::create(); QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
        tile.fill(QColor("#6699CC")); project.shots.first().layers.first().tiles[{0, 0}] = tile;
        QString error; const auto path = temp.filePath("drawing.toonproj");
        QVERIFY2(saveProject(project, path, &error), qPrintable(error)); QVERIFY(window.openProject(path));
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.raise(); window.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *canvas = window.canvas();
        canvas->addDefaultGuide(GuideType::Perspective); canvas->document()->selectAllPixels();
        const auto original = canvas->project().shots.first().layers.first().tiles;
        auto *clear = window.findChild<QAction *>("clearSelectedPixelsAction"); QVERIFY(clear); QVERIFY(clear->isEnabled());
        canvas->setTool(Canvas::Tool::Guides); QVERIFY(!clear->isEnabled());
        canvas->setFocus(); QTRY_VERIFY(canvas->hasFocus());
        QTest::keyClick(canvas, Qt::Key_Backspace); QCOMPARE(canvas->drawingGuides().size(), 0);
        QCOMPARE(canvas->project().shots.first().layers.first().tiles, original);
        canvas->setTool(Canvas::Tool::Brush); QVERIFY(clear->isEnabled());
        QTest::keyClick(canvas, Qt::Key_Backspace);
        QVERIFY(canvas->project().shots.first().layers.first().tiles != original);
        canvas->document()->history()->setClean();
    }

    void toolPanelsPropertiesAndSnappingStaySynchronized() {
        QTemporaryDir temp; MainWindow window(temp.filePath("workspace.ini"));
        window.canvas()->setProject(Project::create());
        QVERIFY(!window.findChild<QAction *>("toolbarGuidesAction"));
        QString guideId;
        for (const auto &tool : window.toolCatalog()->tools()) if (tool.engine == Canvas::Tool::Guides) { guideId = tool.id; break; }
        QVERIFY(!guideId.isEmpty()); QVERIFY(window.toolCatalog()->activateTool(guideId));
        QCOMPARE(window.canvas()->tool(), Canvas::Tool::Guides);
        auto *stack = window.findChild<QStackedWidget *>("toolPropertiesStack"); QVERIFY(stack);
        QVERIFY(qobject_cast<GuidePanel *>(stack->currentWidget()));
        auto *list = window.findChild<QListWidget *>("toolEntries"); QVERIFY(list && list->currentItem());
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), guideId);
        QVERIFY(!window.findChild<QAction *>("snapToGuidesAction"));
        auto *snap = window.findChild<QToolButton *>("guidesSnapping"); QVERIFY(snap); QVERIFY(snap->isEnabled());
        snap->click(); QVERIFY(window.canvas()->guideSettings().snapping);
        window.canvas()->setTool(Canvas::Tool::Brush); QVERIFY(snap->isChecked());
        auto settings = window.canvas()->guideSettings(); settings.snapping = false; window.canvas()->setGuideSettings(settings);
        QVERIFY(!snap->isChecked());
        window.canvas()->document()->history()->setClean();
    }

    void narrowPanelHasNoHorizontalOverflow() {
        Canvas canvas; canvas.setProject(Project::create()); canvas.setTool(Canvas::Tool::Guides);
        canvas.addDefaultGuide(GuideType::TwoPoint);
        auto guide = selected(canvas); guide.limited = true; guide.customColor = true; canvas.updateGuide(guide);
        GuidePanel panel(&canvas); panel.resize(270, 650); panel.show(); QTest::qWait(20);
        auto *scroll=panel.findChild<QScrollArea *>("guideSelectedScroll");QVERIFY(scroll);
        QCOMPARE(panel.width(), 270); QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        QVERIFY(scroll->widget()->width() <= scroll->viewport()->width());
        QVERIFY(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
        for (auto *control : panel.findChildren<QToolButton *>()) {
            QVERIFY2(!control->toolTip().isEmpty(), qPrintable(control->objectName()));
            QVERIFY2(!control->accessibleName().isEmpty(), qPrintable(control->objectName()));
        }
        QDir().mkpath("test-artifacts"); QVERIFY(panel.grab().save("test-artifacts/guide-properties.png"));
        for (int width : {270, 240, 220}) {
            panel.resize(width, 650); QTest::qWait(10);
            QCOMPARE(panel.width(), width); QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
            QVERIFY(scroll->widget()->minimumSizeHint().width() <= scroll->viewport()->width());
            auto *tabs = panel.findChild<QTabBar *>("guideTypes");
            for (int index = 0; index < tabs->count(); ++index) {
                QVERIFY(tabs->rect().contains(tabs->tabRect(index)));
                QVERIFY(tabs->tabRect(index).width() >= 28);
                QCOMPARE(tabs->tabRect(index).height(), 28);
                QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(index).center());
                QCOMPARE(tabs->currentIndex(), index);
            }
            tabs->setCurrentIndex(0); tabs->setFocus();
            QTest::keyClick(tabs, Qt::Key_Right); QCOMPARE(tabs->currentIndex(), 1);
            tabs->setCurrentIndex(4); QTest::keyClick(tabs, Qt::Key_Right); QCOMPARE(tabs->currentIndex(), 5);
            for (auto *row : panel.findChildren<PropertyRow *>()) if (row->isVisible()) {
                QVERIFY2(row->rect().contains(row->control()->geometry()), qPrintable(row->objectName()));
                QVERIFY2(!row->labelWidget()->geometry().intersects(row->control()->geometry()), qPrintable(row->objectName()));
                auto *slider = qobject_cast<ValueSlider *>(row->control()); QVERIFY(slider);
                QVERIFY(slider->findChild<QSlider *>()->width() >= 74);
                QCOMPARE(slider->findChild<QDoubleSpinBox *>()->width(), 48);
            }
            for (auto *control : panel.findChildren<QToolButton *>()) if (control->isVisible()) {
                QCOMPARE(control->size(), QSize(28, 28));
                QCOMPARE(control->iconSize(), QSize(16, 16));
            }
        }
        panel.resize(270, 220); QTest::qWait(10); QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
        auto *opacity = panel.findChild<ValueSlider *>("guideOpacity"); scroll->ensureWidgetVisible(opacity); QTest::qWait(10);
        QVERIFY(scroll->viewport()->rect().contains(opacity->mapTo(scroll->viewport(), opacity->rect().center())));
    }

    void resizingPreservesNumericDraftAndSingleCommit() {
        Canvas canvas; canvas.setProject(Project::create()); canvas.setTool(Canvas::Tool::Guides);
        canvas.addDefaultGuide(GuideType::Perspective);
        auto guide = selected(canvas); guide.limited = true; canvas.updateGuide(guide);
        GuidePanel panel(&canvas); panel.resize(270, 700); panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel)); panel.raise(); panel.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&panel));
        auto *width = panel.findChild<ValueSlider *>("guideAreaWidth");
        auto *field = width->findChild<QDoubleSpinBox *>(); auto *editor = field->findChild<QLineEdit *>(); QVERIFY(editor);
        field->setFocus(); QTRY_VERIFY(field->hasFocus()); field->selectAll(); QTest::keyClicks(field, "876.5");
        const int history = canvas.document()->history()->count();
        panel.resize(220, 700); QTest::qWait(10);
        QVERIFY(field->hasFocus()); QCOMPARE(editor->text(), QString("876.5"));
        QCOMPARE(canvas.document()->history()->count(), history); QCOMPARE(selected(canvas).area.width(), 1920.);
        QTest::keyClick(field, Qt::Key_Return);
        QCOMPARE(selected(canvas).area.width(), 876.5); QCOMPARE(canvas.document()->history()->count(), history + 1);
        canvas.document()->history()->undo(); QCOMPARE(selected(canvas).area.width(), 1920.);
    }

    void propertyScrollingKeepsGuideActionsAndCategoriesReachable() {
        Canvas canvas;canvas.setProject(Project::create());canvas.setTool(Canvas::Tool::Guides);
        for(int i=0;i<6;++i)canvas.addDefaultGuide(GuideType::Perspective);
        auto guide=selected(canvas);guide.limited=true;guide.customColor=true;guide.opacity=.37;canvas.updateGuide(guide);
        GuidePanel panel(&canvas);panel.resize(220,360);panel.show();QTest::qWait(20);
        auto *tabs=panel.findChild<QTabWidget *>("guidePropertyTabs");
        auto *selectedScroll=panel.findChild<QScrollArea *>("guideSelectedScroll");
        auto *globalScroll=panel.findChild<QScrollArea *>("guideGlobalScroll");
        auto *tree=panel.findChild<QTreeWidget *>("guideList");QVERIFY(tabs&&selectedScroll&&globalScroll&&tree);
        QTRY_VERIFY(selectedScroll->verticalScrollBar()->maximum()>0);
        const auto listGeometry=tree->geometry(),tabsGeometry=tabs->tabBar()->geometry();
        selectedScroll->verticalScrollBar()->setValue(selectedScroll->verticalScrollBar()->maximum());
        const int position=selectedScroll->verticalScrollBar()->value();
        for(const auto *id:{"addGuide","duplicateGuide","removeGuide","loadGuideSet","saveGuideSet"}){
            auto *action=button(panel,id);QVERIFY(action);QVERIFY(action->isVisibleTo(&panel));
            QVERIFY(panel.rect().contains(QRect(action->mapTo(&panel,QPoint()),action->size())));
            QVERIFY(!selectedScroll->isAncestorOf(action));
        }
        const auto original=selected(canvas);const auto history=canvas.document()->history()->count();
        tabs->setCurrentIndex(1);QTest::qWait(10);QCOMPARE(globalScroll->verticalScrollBar()->maximum(),0);
        QCOMPARE(tree->geometry(),listGeometry);QCOMPARE(tabs->tabBar()->geometry(),tabsGeometry);
        tabs->setCurrentIndex(0);QCOMPARE(selectedScroll->verticalScrollBar()->value(),position);
        QCOMPARE(selected(canvas),original);QCOMPARE(canvas.document()->history()->count(),history);
        button(panel,"guideCustomColor")->click();
        QVERIFY(!panel.findChild<ValueSlider *>("guideOpacity")->isVisibleTo(&panel));
        QCOMPARE(selected(canvas).opacity,.37);canvas.document()->history()->undo();
        QVERIFY(panel.findChild<ValueSlider *>("guideOpacity")->isVisibleTo(&panel));QCOMPARE(selected(canvas).opacity,.37);
        const QString output=qEnvironmentVariable("TOON_UI_PREVIEW");if(!output.isEmpty()){
            QDir().mkpath(output);QVERIFY(panel.grab().save(output+"/guides-short-selected.png"));
            tabs->setCurrentIndex(1);QVERIFY(panel.grab().save(output+"/guides-short-global.png"));
        }
    }

    void shrinkingGuideListKeepsCurrentGuideAndFitsStatusColumns_data() {
        QTest::addColumn<int>("fontPixels");
        QTest::newRow("standard-font") << 0;
        QTest::newRow("larger-font") << 24;
    }

    void shrinkingGuideListKeepsCurrentGuideAndFitsStatusColumns() {
        QFETCH(int, fontPixels);
        Canvas canvas; canvas.setProject(Project::create()); canvas.setTool(Canvas::Tool::Guides);
        for (auto type : {GuideType::Perspective, GuideType::VanishingPoint, GuideType::TwoPoint,
                          GuideType::ParallelRuler, GuideType::ThreePoint, GuideType::Fisheye})
            canvas.addDefaultGuide(type);
        GuidePanel panel(&canvas);
        if (fontPixels) { auto font = panel.font(); font.setPixelSize(fontPixels); panel.setFont(font); }
        panel.resize(300, 700); panel.show(); QTest::qWait(20);
        auto *tree = panel.findChild<QTreeWidget *>("guideList"); QVERIFY(tree);
        QCOMPARE(tree->currentItem(), tree->topLevelItem(5));
        tree->scrollToItem(tree->currentItem(), QAbstractItemView::EnsureVisible);
        const auto currentId = canvas.selectedGuideId();
        const int history = canvas.document()->history()->count();
        const int previousHeight = tree->viewport()->height();
        panel.resize(220, 360); QTest::qWait(20);
        QVERIFY(tree->viewport()->height() < previousHeight);
        const auto selectedIsVisible = [tree] {
            const auto rect = tree->visualItemRect(tree->currentItem());
            return rect.top() >= 0 && rect.bottom() < tree->viewport()->height();
        };
        QTRY_VERIFY(selectedIsVisible());
        QCOMPARE(canvas.selectedGuideId(), currentId);
        QCOMPARE(canvas.document()->history()->count(), history);
        QCOMPARE(tree->horizontalScrollBar()->maximum(), 0);
        for (int column = 1; column < 4; ++column) {
            QCOMPARE(tree->columnWidth(column), ui::metrics::actionTarget);
            QVERIFY(tree->header()->sectionViewportPosition(column) + tree->columnWidth(column)
                    <= tree->viewport()->width());
        }
        // Refreshing this guide must not pull the list back down after the
        // artist deliberately scrolls to inspect the other guides.
        tree->verticalScrollBar()->setValue(0);
        QVERIFY(!selectedIsVisible());
        auto guide = selected(canvas); ++guide.subdivisions; canvas.updateGuide(guide); QTest::qWait(10);
        QCOMPARE(tree->verticalScrollBar()->value(), 0);
        QCOMPARE(canvas.selectedGuideId(), currentId);
        QCOMPARE(canvas.document()->history()->count(), history + 1);
    }

    void largerFontKeepsRowsCreationAndDraftsAvailable() {
        Canvas canvas;canvas.setProject(Project::create());canvas.setTool(Canvas::Tool::Guides);canvas.addDefaultGuide(GuideType::TwoPoint);
        GuidePanel panel(&canvas);panel.resize(260,480);panel.show();QVERIFY(QTest::qWaitForWindowExposed(&panel));
        panel.raise();panel.activateWindow();QVERIFY(QTest::qWaitForWindowActive(&panel));
        auto *name=panel.findChild<QLineEdit *>("guideName");name->setFocus();QTRY_VERIFY(name->hasFocus());
        name->selectAll();QTest::keyClicks(name,"Street draft");const auto history=canvas.document()->history()->count();
        auto font=panel.font();font.setPixelSize(24);panel.setFont(font);panel.resize(220,360);QTest::qWait(20);
        QCOMPARE(name->text(),QString("Street draft"));QVERIFY(name->hasFocus());QCOMPARE(canvas.document()->history()->count(),history);
        auto *tree=panel.findChild<QTreeWidget *>("guideList");QVERIFY(tree->visualItemRect(tree->currentItem()).height()>=tree->fontMetrics().height()+ui::metrics::sectionGap);
        auto *type=panel.findChild<QLabel *>("guideTypeName");QVERIFY(type->height()>=type->fontMetrics().height()+2);
        auto *tabs=panel.findChild<QTabBar *>("guideTypes");
        for(int i=0;i<tabs->count();++i)QVERIFY(tabs->rect().contains(tabs->tabRect(i)));
        for(auto *scroll:panel.findChildren<QScrollArea *>())QCOMPARE(scroll->horizontalScrollBar()->maximum(),0);
        QTest::keyClick(name,Qt::Key_Return);QCOMPARE(selected(canvas).name,QString("Street draft"));
        QCOMPARE(canvas.document()->history()->count(),history+1);
        canvas.beginGuideCreation(GuideType::Fisheye);QTest::qWait(10);
        auto *placement=panel.findChild<QLabel *>("guidePlacementStatus");QVERIFY(placement->isVisibleTo(&panel));
        QVERIFY(placement->accessibleName().contains("curve height"));QVERIFY(placement->height()>=placement->fontMetrics().height()+2);
        auto *cancel=button(panel,"cancelGuideCreation");QVERIFY(panel.rect().contains(QRect(cancel->mapTo(&panel,QPoint()),cancel->size())));
        const QString output=qEnvironmentVariable("TOON_UI_PREVIEW");if(!output.isEmpty()){
            QDir().mkpath(output);QVERIFY(panel.grab().save(output+"/guides-placement-larger-font.png"));
            cancel->click();QVERIFY(panel.grab().save(output+"/guides-larger-font.png"));
        }
    }

    void workspacePreview_data() {
        QTest::addColumn<int>("typeValue"); QTest::addColumn<QString>("suffix");
        QTest::newRow("three-point") << int(GuideType::ThreePoint) << "three-point";
        QTest::newRow("fisheye") << int(GuideType::Fisheye) << "fisheye";
    }
    void workspacePreview() {
        QFETCH(int, typeValue); QFETCH(QString, suffix);
        const auto type = static_cast<GuideType>(typeValue);
        QTemporaryDir temp; MainWindow window(temp.filePath("workspace.ini"));
        auto project = Project::create(); project.movie = "City sequence"; project.shots.first().name = guideTypeName(type);
        QString error; const auto path = temp.filePath("city.toonproj");
        QVERIFY2(saveProject(project, path, &error), qPrintable(error)); QVERIFY(window.openProject(path));
        auto *canvas = window.canvas(); canvas->addDefaultGuide(type);
        auto street = selected(*canvas); street.name = type == GuideType::Fisheye ? "Wide lens" : "City height";
        street.subdivisions = 6; canvas->updateGuide(street); canvas->setGuideCreationType(type);
        canvas->setTool(Canvas::Tool::Guides);
        window.resize(1440, 940); window.show();
        window.findChild<QDockWidget *>("propertiesPanel")->raise(); QTest::qWait(20); canvas->fitSheet(); QTest::qWait(20);
        QDir().mkpath("test-artifacts");
        QVERIFY(window.grab().save(QString("test-artifacts/guide-workspace-%1.png").arg(suffix)));
        canvas->document()->history()->setClean();
    }
};
QTEST_MAIN(GuidePanelTest)
#include "test_guide_panel.moc"
