#include "canvas.h"
#include "document.h"
#include "main_window.h"
#include "property_row.h"
#include "reference_panel.h"
#include "ui_metrics.h"
#include "value_slider.h"
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QScrollBar>
#include <QSlider>
#include <QStyle>
#include <QTemporaryDir>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

using namespace toon;
static QImage referenceFixture(QSize size = {480, 360}) {
    QImage image(size, QImage::Format_ARGB32_Premultiplied); image.fill(QColor("#ebe8dd"));
    QPainter p(&image);
    p.fillRect(QRect(0, 0, size.width(), size.height() / 2), QColor("#8eaeba"));
    p.fillRect(QRect(size.width() / 8, size.height() / 3, size.width() / 3, size.height() / 2), QColor("#526d76"));
    p.fillRect(QRect(size.width() / 2, size.height() / 4, size.width() / 3, size.height() / 2), QColor("#ce906c"));
    return image;
}
class ReferencesTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void importUndoRemoveAndPortableSave() {
        QTemporaryDir temp; const auto source = temp.filePath("Color study.png");
        const auto fixture = referenceFixture(); QVERIFY(fixture.save(source));
        Document document; document.load(Project::create()); ReferencePanel panel(&document);
        QString error; QVERIFY2(panel.importImage(source, &error), qPrintable(error));
        QCOMPARE(document.project().shots.first().references.size(), 1);
        QCOMPARE(document.history()->count(), 1);
        const auto reference = document.project().shots.first().references.first();
        QCOMPARE(reference.name, QString("Color study")); QCOMPARE(reference.image, fixture);
        QCOMPARE(panel.findChild<QListWidget *>("referenceList")->count(), 1);
        document.history()->undo(); QVERIFY(document.project().shots.first().references.isEmpty());
        QVERIFY(panel.findChild<ReferenceView *>("referenceView")->image().isNull());
        document.history()->redo(); QCOMPARE(panel.findChild<ReferenceView *>("referenceView")->image(), fixture);
        const auto path = temp.filePath("portable.toonproj");
        QVERIFY(saveProject(document.project(), path, &error)); QVERIFY(QFile::remove(source));
        auto loaded = loadProject(path, &error); QVERIFY2(loaded.has_value(), qPrintable(error));
        QCOMPARE(loaded->shots.first().references.first().image, fixture);
        panel.findChild<QToolButton *>("removeReference")->click();
        QVERIFY(document.project().shots.first().references.isEmpty());
        document.history()->undo(); QCOMPARE(document.project().shots.first().references.first().id, reference.id);
    }
    void viewerNavigationIsIndependentAndSurvivesArtworkEdits() {
        Document document; document.load(Project::create()); QString error;
        QVERIFY(document.addReference("Study", referenceFixture(), &error));
        ReferencePanel panel(&document); panel.resize(360, 600); panel.show(); QTest::qWait(20);
        auto *view = panel.findChild<ReferenceView *>("referenceView"); auto *zoom = panel.findChild<ValueSlider *>("referenceZoom");
        const auto projectBefore = document.project().toJson();
        const auto sheetBefore = renderSheet(document.project(), document.project().shots.first());
        const auto frameBefore = renderMovieFrame(document.project());
        zoom->findChild<QDoubleSpinBox *>()->setValue(175);
        QCOMPARE(view->zoom(), 1.75);
        const auto start = view->rect().center(); const QPoint delta(20, 15);
        const auto before = view->imageTransform().map(QPointF(0, 0));
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(view, start + delta);
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, start + delta);
        QVERIFY(QLineF(view->imageTransform().map(QPointF()), before + delta).length() < .01);
        const auto navigated = view->imageTransform();
        QCOMPARE(document.project().toJson(), projectBefore);
        QCOMPARE(renderSheet(document.project(), document.project().shots.first()), sheetBefore);
        QCOMPARE(renderMovieFrame(document.project()), frameBefore);
        QCOMPARE(document.history()->count(), 1);
        BrushSettings brush; QVERIFY(document.beginStroke({100, 100}, 1, brush, false));
        document.continueStroke({200, 100}, 1); document.endStroke();
        QCOMPARE(view->imageTransform(), navigated); // Painting does not reset the reference view.
        panel.findChild<QToolButton *>("fitReference")->click();
        QVERIFY(QRectF(view->rect()).contains(view->imageTransform().mapRect(QRectF(view->image().rect()))));
        QCOMPARE(zoom->value(), qRound(view->zoom() * 1000) / 10.0);
    }
    void importBoundsAndFailuresLeaveDocumentUntouched() {
        QTemporaryDir temp; Document document; ReferencePanel panel(&document); QString error;
        QVERIFY(!panel.importImage(temp.filePath("missing.png"), &error));
        document.load(Project::create()); const auto initial = document.project().toJson();
        const auto badPath = temp.filePath("broken.png"); QFile bad(badPath);
        QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("not an image"); bad.close();
        QVERIFY(!panel.importImage(badPath, &error));
        QCOMPARE(document.project().toJson(), initial); QCOMPARE(document.history()->count(), 0);
        const auto wide = temp.filePath("wide.png"); QVERIFY(referenceFixture({3000, 1000}).save(wide));
        QVERIFY2(panel.importImage(wide, &error), qPrintable(error));
        const auto size = document.project().shots.first().references.first().image.size();
        QCOMPARE(size.width(), 2048); QVERIFY(qAbs(double(size.width()) / size.height() - 3) < .01);
        const auto originalFile = QImage(wide); QCOMPARE(originalFile.size(), QSize(3000, 1000));
        for (int i = 1; i < maxProjectReferences; ++i) QVERIFY(document.addReference(QString::number(i), referenceFixture({4, 4}), &error));
        const auto count = document.history()->count();
        QVERIFY(!document.addReference("Over limit", referenceFixture({4, 4}), &error));
        QCOMPARE(document.history()->count(), count);
        QCOMPARE(document.project().shots.first().references.size(), maxProjectReferences);
    }
    void compactLayoutKeepsPreviewNavigationAndNamesAvailable() {
        Document document; document.load(Project::create());
        const auto longName = QString("Character costume and lighting reference with a very long production name");
        QVERIFY(document.addReference(longName, referenceFixture()));
        for (int index = 1; index < 8; ++index)
            QVERIFY(document.addReference(QString("Study %1").arg(index), referenceFixture({240, 320})));
        ReferencePanel panel(&document); panel.resize(240, 280); panel.show(); QTest::qWait(20);
        QCOMPARE(panel.size(), QSize(240, 280));
        auto *view = panel.findChild<ReferenceView *>("referenceView");
        auto *list = panel.findChild<QListWidget *>("referenceList");
        auto *caption = panel.findChild<QLabel *>("referenceName");
        auto *zoom = panel.findChild<ValueSlider *>("referenceZoom");
        QVERIFY(view); QVERIFY(list); QVERIFY(caption); QVERIFY(zoom);
        QVERIFY(qobject_cast<PropertyRow *>(zoom->parentWidget()));
        QVERIFY(view->height() >= 96);
        QVERIFY(zoom->findChild<QSlider *>()->width() >= 74);
        for (auto *widget : QList<QWidget *>{view, list, caption, zoom,
                 panel.findChild<QToolButton *>("importReference"),
                 panel.findChild<QToolButton *>("removeReference"), panel.findChild<QToolButton *>("fitReference")}) {
            QVERIFY(widget);
            QVERIFY(panel.rect().contains(QRect(widget->mapTo(&panel, QPoint()), widget->size())));
        }
        QVERIFY(caption->text().contains(longName));
        QCOMPARE(caption->toolTip(), caption->text());
        QCOMPARE(caption->accessibleName(), caption->text());
        const auto captionHeight = caption->height();
        const auto previewHeight = view->height();
        // Names remain available for keyboard type-ahead despite icon-only tiles.
        QCOMPARE(list->item(0)->text(), longName);
        QCOMPARE(list->item(0)->data(Qt::AccessibleTextRole).toString(), longName);
        const auto retina = list->item(0)->icon().pixmap(QSize(40, 40), 2.0);
        QCOMPARE(retina.devicePixelRatio(), 2.0); QCOMPARE(retina.width(), 80);
        list->setFocus(); QTest::keyClick(list, Qt::Key_End);
        QCOMPARE(list->currentRow(), 7);
        QTRY_VERIFY(list->viewport()->rect().contains(list->visualItemRect(list->currentItem()).center()));
        QCOMPARE(caption->height(), captionHeight); QCOMPARE(view->height(), previewHeight);
        QVERIFY(list->horizontalScrollBar()->maximum() > 0);
        panel.resize(220, 280); QTest::qWait(20);
        QCOMPARE(panel.width(), 220); QVERIFY(zoom->findChild<QSlider *>()->width() >= 74);
        const auto history = document.history()->count();
        QCOMPARE(history, 8); // Selection, resizing and scrolling are view-only.
    }
    void zoomDraftSurvivesFitResizeAndArtworkRefresh() {
        Document document; document.load(Project::create());
        QVERIFY(document.addReference("Study", referenceFixture()));
        ReferencePanel panel(&document); panel.resize(320, 480); panel.show(); panel.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&panel));
        auto *zoom = panel.findChild<ValueSlider *>("referenceZoom");
        auto *number = zoom->findChild<QDoubleSpinBox *>();
        auto *edit = number->findChild<QLineEdit *>();
        auto *view = panel.findChild<ReferenceView *>("referenceView");
        edit->setFocus(); QTRY_VERIFY(edit->hasFocus()); edit->selectAll(); QTest::keyClicks(edit, "175");
        const auto beforeResize = view->zoom();
        panel.resize(220, 280); QTest::qWait(20);
        QVERIFY(view->zoom() != beforeResize); // Fit still responds while the exact-entry draft is untouched.
        document.addLayer();
        QCOMPARE(edit->text(), QString("175")); QVERIFY(edit->hasFocus());
        const auto history = document.history()->count();
        QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(view->zoom(), 1.75); QCOMPARE(zoom->value(), 175.0);
        QCOMPARE(document.history()->count(), history);
        panel.resize(320, 480); QTest::qWait(20);
        QCOMPARE(view->zoom(), 1.75); // Explicit zoom has left Fit mode.
        edit->selectAll(); QTest::keyClicks(edit, "250"); QTest::keyClick(edit, Qt::Key_Escape);
        QCOMPARE(zoom->value(), 175.0); QCOMPARE(view->zoom(), 1.75);
    }
    void singleReferenceReclaimsStripWithoutResettingManualView() {
        Document document; document.load(Project::create());
        ReferencePanel panel(&document); panel.resize(240, 280); panel.show(); QTest::qWait(20);
        auto *list = panel.findChild<QListWidget *>("referenceList");
        auto *view = panel.findChild<ReferenceView *>("referenceView");
        auto *caption = panel.findChild<QLabel *>("referenceName");
        auto *zoom = panel.findChild<ValueSlider *>("referenceZoom");
        QVERIFY(!list->isVisible()); QVERIFY(view->image().isNull());
        QVERIFY(document.addReference("Costume", referenceFixture())); QTest::qWait(20);
        QVERIFY(!list->isVisible()); QCOMPARE(list->count(), 1);
        QVERIFY(caption->isVisible()); QVERIFY(caption->text().contains("Costume"));
        const auto singleHeight = view->height();
        // The new collection tabs use one row; a lone image still reclaims its thumbnail strip.
        QVERIFY(singleHeight + panel.findChild<QTabBar *>("referenceTabs")->height() >= 200);
        zoom->setValue(175); QCOMPARE(view->zoom(), 1.75);
        const auto start = view->rect().center(); const auto end = start + QPoint(16, 11);
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(view, end); QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, end);
        const auto center = view->imageTransform().inverted().map(QRectF(view->rect()).center());
        const auto selected = list->currentItem()->data(Qt::UserRole).toString();
        QVERIFY(document.addReference("Lighting", referenceFixture({180, 320})));
        const auto second = document.activeShot().references.last().id;
        QTRY_VERIFY(list->isVisible()); QTest::qWait(20);
        QCOMPARE(list->count(), 2); QVERIFY(view->height() <= singleHeight - list->height());
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), selected);
        QCOMPARE(view->zoom(), 1.75);
        QVERIFY(QLineF(view->imageTransform().inverted().map(QRectF(view->rect()).center()), center).length() < .01);
        document.removeReference(second); QTRY_VERIFY(!list->isVisible()); QTest::qWait(20);
        QCOMPARE(view->height(), singleHeight); QCOMPARE(view->zoom(), 1.75);
        QVERIFY(QLineF(view->imageTransform().inverted().map(QRectF(view->rect()).center()), center).length() < .01);
        document.history()->undo(); QTRY_VERIFY(list->isVisible()); QTest::qWait(20);
        QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), selected);
        QCOMPARE(view->zoom(), 1.75);
        const auto history = document.history()->count();
        list->setCurrentRow(1); QCOMPARE(view->image().size(), QSize(180, 320));
        QCOMPARE(document.history()->count(), history);
    }
    void largerFontKeepsCaptionAndFitWithZoomDraft() {
        Document document; document.load(Project::create());
        const QString name = "Character lighting reference with a long production name";
        QVERIFY(document.addReference(name, referenceFixture()));
        QVERIFY(document.addReference("Costume", referenceFixture()));
        ReferencePanel panel(&document); panel.resize(320, 480); panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel)); panel.raise(); panel.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&panel));
        auto *caption = panel.findChild<QLabel *>("referenceName");
        auto *zoom = panel.findChild<ValueSlider *>("referenceZoom");
        auto *row = qobject_cast<PropertyRow *>(zoom->parentWidget()); QVERIFY(row);
        auto *fit = panel.findChild<QToolButton *>("fitReference");
        QCOMPARE(fit->parentWidget(), row);
        auto *field = zoom->findChild<QDoubleSpinBox *>();
        auto *edit = field->findChild<QLineEdit *>();
        edit->setFocus(); QTRY_VERIFY(edit->hasFocus()); edit->selectAll(); QTest::keyClicks(edit, "175");
        const auto history = document.history()->count();
        QFont font = panel.font(); font.setPixelSize(24); panel.setFont(font); panel.resize(220, 360);
        QTest::qWait(20);
        QCOMPARE(panel.size(), QSize(220, 360));
        QVERIFY(caption->height() >= caption->fontMetrics().height() + 2);
        QVERIFY(caption->text().contains(name)); QCOMPARE(caption->accessibleName(), caption->text());
        QCOMPARE(caption->toolTip(), caption->text()); QVERIFY(caption->width() > 0);
        QCOMPARE(edit->text(), QString("175")); QVERIFY(edit->hasFocus());
        QVERIFY(row->control()->y() > row->labelWidget()->y()); // Large label wraps above a useful track.
        QVERIFY(zoom->findChild<QSlider *>()->width() >= 74); QCOMPARE(field->width(), 48);
        for (const char *id : {"importReference", "removeReference", "fitReference"}) {
            auto *action = panel.findChild<QToolButton *>(id); QVERIFY(action);
            QCOMPARE(action->iconSize(), QSize(ui::metrics::actionIcon, ui::metrics::actionIcon));
            QCOMPARE(action->size(), QSize(ui::metrics::actionTarget, ui::metrics::actionTarget));
            QVERIFY(!action->toolTip().isEmpty()); QVERIFY(!action->accessibleName().isEmpty());
            QCOMPARE(action->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay, nullptr, action), 600);
            QVERIFY(panel.rect().contains(QRect(action->mapTo(&panel, QPoint()), action->size())));
        }
        QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(panel.findChild<ReferenceView *>("referenceView")->zoom(), 1.75);
        QCOMPARE(document.history()->count(), history);
    }
    void compactViewerSamplesAndLastRemovalClearsState() {
        Document document; document.load(Project::create()); const auto image = referenceFixture({64, 64});
        QVERIFY(document.addReference("Color study", image));
        ReferencePanel panel(&document); panel.resize(240, 280); panel.show(); QTest::qWait(20);
        auto *view = panel.findChild<ReferenceView *>("referenceView"); auto *list = panel.findChild<QListWidget *>("referenceList");
        QSignalSpy samples(&panel, &ReferencePanel::colorSampled);
        const auto transform = view->imageTransform();
        const auto location = transform.map(QPointF(20.5, 30.5)).toPoint();
        QTest::mouseClick(view, Qt::LeftButton, Qt::AltModifier, location);
        QCOMPARE(samples.size(), 1); QCOMPARE(qvariant_cast<QColor>(samples.takeFirst().first()), image.pixelColor(20, 30));
        panel.setSampling(true); QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, location);
        QCOMPARE(samples.size(), 1); QCOMPARE(view->imageTransform(), transform);
        QTest::mousePress(view, Qt::MiddleButton, Qt::NoModifier, location);
        QTest::mouseMove(view, location + QPoint(12, 8));
        QTest::mouseRelease(view, Qt::MiddleButton, Qt::NoModifier, location + QPoint(12, 8));
        QVERIFY(view->imageTransform() != transform); QCOMPARE(samples.size(), 1);
        QCOMPARE(document.history()->count(), 1);
        panel.findChild<QToolButton *>("removeReference")->click();
        QCOMPARE(list->count(), 0); QVERIFY(view->image().isNull());
        auto *caption = panel.findChild<QLabel *>("referenceName");
        QVERIFY(caption->text().isEmpty()); QVERIFY(caption->toolTip().isEmpty()); QVERIFY(caption->accessibleName().isEmpty());
        QVERIFY(!panel.findChild<QToolButton *>("fitReference")->isEnabled());
        QVERIFY(!panel.findChild<ValueSlider *>("referenceZoom")->isEnabled());
        QVERIFY(!panel.findChild<QToolButton *>("removeReference")->isEnabled());
        QVERIFY(panel.findChild<QToolButton *>("importReference")->isEnabled());
        document.history()->undo(); QCOMPARE(view->image(), image); QCOMPARE(list->count(), 1);
        QVERIFY(panel.findChild<ValueSlider *>("referenceZoom")->isEnabled());
    }
    void compactReferencePreviews() {
        if (qEnvironmentVariableIsEmpty("TOON_UI_PREVIEW")) QSKIP("Preview output is opt-in");
        Document document; document.load(Project::create());
        QVERIFY(document.addReference("Character costume and lighting reference with a long production name", referenceFixture()));
        for (int index = 1; index < 8; ++index)
            QVERIFY(document.addReference(QString("Study %1").arg(index), referenceFixture(index % 2 ? QSize(240, 320) : QSize(600, 240))));
        ReferencePanel panel(&document); panel.resize(320, 480); panel.show(); QTest::qWait(30);
        const auto output = qEnvironmentVariable("TOON_UI_PREVIEW"); QDir().mkpath(output);
        QVERIFY(panel.grab().save(output + "/references.png"));
        panel.resize(220, 480); QTest::qWait(30); QVERIFY(panel.grab().save(output + "/references-narrow.png"));
        panel.resize(240, 280); QTest::qWait(30); QVERIFY(panel.grab().save(output + "/references-short.png"));
        auto *list = panel.findChild<QListWidget *>("referenceList"); list->setCurrentRow(7); list->scrollToItem(list->currentItem());
        QTest::qWait(30); QVERIFY(panel.grab().save(output + "/references-overflow.png"));
        QFont font = panel.font(); const QFont normalFont = font; font.setPixelSize(24); panel.setFont(font); panel.resize(220, 360);
        QTest::qWait(30); QVERIFY(panel.grab().save(output + "/references-larger-font.png"));
        panel.setFont(normalFont); panel.resize(240, 280);
        while (list->count() > 1) panel.findChild<QToolButton *>("removeReference")->click();
        QTest::qWait(30); QVERIFY(panel.grab().save(output + "/references-single.png"));
        while (list->count()) panel.findChild<QToolButton *>("removeReference")->click();
        QTest::qWait(30); QVERIFY(panel.grab().save(output + "/references-empty.png"));
        document.history()->setClean();
    }
    void workspaceRecoveryAndPreview() {
        QTemporaryDir temp; const auto path = temp.filePath("movie.toonproj"); QString error;
        auto project = Project::create(); project.movie = "Character design";
        QVERIFY(saveProject(project, path, &error));
        MainWindow window(temp.filePath("workspace.ini")); window.resize(1380, 960); window.show();
        QVERIFY(window.openProject(path));
        auto *panel = window.findChild<ReferencePanel *>(); QVERIFY(panel);
        const auto source = temp.filePath("Color study.png"); QVERIFY(referenceFixture().save(source));
        QVERIFY(panel->importImage(source, &error)); QVERIFY(window.isWindowModified());
        QVERIFY(QMetaObject::invokeMethod(window.findChild<QTimer *>("recoveryTimer"), "timeout", Qt::DirectConnection));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path + ".recovery"), 10000);
        const auto recovered = loadProject(path + ".recovery", &error); QVERIFY(recovered);
        QCOMPARE(recovered->shots.first().references.first().image, referenceFixture());
        for (const auto *id : {"toolsPanel", "propertiesPanel", "layersPanel", "colorsPanel", "referencesPanel", "controlPanel"})
            window.findChild<QDockWidget *>(id)->show();
        window.findChild<QDockWidget *>("propertiesPanel")->raise();
        window.findChild<QDockWidget *>("referencesPanel")->raise();
        auto *smoothing = window.findChild<ValueSlider *>("quickBrushStabilization"); QVERIFY(smoothing);
        smoothing->findChild<QDoubleSpinBox *>()->setValue(45); QCOMPARE(window.canvas()->stabilization(), 45.0);
        QTest::qWait(40); window.canvas()->fitSheet();
        QDir().mkpath("test-artifacts"); QVERIFY(window.grab().save("test-artifacts/references.png"));
        window.canvas()->document()->history()->setClean(); window.close();
    }
};
QTEST_MAIN(ReferencesTest)
#include "test_references.moc"
