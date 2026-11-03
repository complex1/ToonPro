#include "canvas.h"
#include "model_library.h"
#include "model_library_panel.h"
#include "scene3d_controller.h"
#include "scene_model_io.h"
#include "ui_theme.h"
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStyle>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>
#include <functional>

using namespace toon;
namespace {
Project smallProject() { auto project = Project::create(); project.width = 480; project.height = 320; return project; }
QToolButton *button(ModelLibraryPanel &panel, const char *name) { return panel.findChild<QToolButton *>(name); }
QListWidget *models(ModelLibraryPanel &panel) { return panel.findChild<QListWidget *>("modelLibraryModels"); }
QString selectedId(ModelLibraryPanel &panel) { return models(panel)->currentItem()->data(Qt::UserRole).toString(); }
QRect bounds(QWidget *widget, QWidget *panel) { return {widget->mapTo(panel, QPoint()), widget->size()}; }
QString addTriangle(ModelLibrary &library, const QString &path) {
    const float vertices[] = {-.5f, 0, 0, .5f, 0, 0, 0, 1, 0};
    QByteArray bytes(reinterpret_cast<const char *>(vertices), sizeof(vertices));
    const QJsonObject root{{"asset", QJsonObject{{"version", "2.0"}}},
        {"buffers", QJsonArray{QJsonObject{{"byteLength", bytes.size()}, {"uri", "data:application/octet-stream;base64," + QString::fromLatin1(bytes.toBase64())}}}},
        {"bufferViews", QJsonArray{QJsonObject{{"buffer", 0}, {"byteLength", bytes.size()}}}},
        {"accessors", QJsonArray{QJsonObject{{"bufferView", 0}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}}}},
        {"meshes", QJsonArray{QJsonObject{{"primitives", QJsonArray{QJsonObject{{"attributes", QJsonObject{{"POSITION", 0}}}}}}}}},
        {"nodes", QJsonArray{QJsonObject{{"mesh", 0}}}}, {"scenes", QJsonArray{QJsonObject{{"nodes", QJsonArray{0}}}}}, {"scene", 0}};
    QFile file(path); if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(QJsonDocument(root).toJson()); file.close();
    return library.importFile(path);
}
void chooseOption(ModelLibraryPanel &panel, const QString &text, const std::function<void(QInputDialog *)> &edit) {
    QTimer::singleShot(0, &panel, [&, text, edit] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget()); QVERIFY(menu);
        QAction *choice = nullptr;
        for (auto *action : menu->actions()) if (action->text() == text) choice = action;
        QVERIFY(choice); menu->setActiveAction(choice);
        QTimer::singleShot(0, &panel, [edit] {
            auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
            edit(dialog);
        });
        QTest::keyClick(menu, Qt::Key_Return);
    });
    button(panel, "modelLibraryOptions")->click();
}
}

class ModelLibraryPanelTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void compactControlsRemainReachableAtNarrowAndShortSizes() {
        QTemporaryDir directory; ModelLibrary library(directory.path()); QVERIFY(library.load());
        Canvas canvas; canvas.setProject(smallProject()); ModelLibraryPanel panel(&library, canvas.scene3D());
        panel.show();
        auto *search = panel.findChild<QLineEdit *>("modelLibrarySearch");
        auto *tags = panel.findChild<QComboBox *>("modelLibraryTagFilter");
        auto *status = panel.findChild<QLabel *>("modelLibraryStatus");
        QCOMPARE(panel.style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay, nullptr, &panel), ui::tooltipDelayMs);
        for (int width : {240, 300, 360}) for (int height : {240, 600}) {
            panel.resize(width, height); QCoreApplication::processEvents();
            QCOMPARE(panel.size(), QSize(width, height)); QVERIFY(panel.minimumSizeHint().width() <= 240);
            QCOMPARE(bounds(search, &panel).center().y(), bounds(tags, &panel).center().y());
            QVERIFY(search->width() >= 90); QVERIFY(tags->width() >= 65);
            QCOMPARE(models(panel)->horizontalScrollBar()->maximum(), 0);
            QVERIFY(panel.rect().contains(bounds(status, &panel)));
            QVERIFY(models(panel)->height() >= 80);
            for (const auto *name : {"import3DModel", "add3DModel", "favorite3DModel", "remove3DModel", "modelLibraryOptions", "modelLibraryFavoritesOnly"}) {
                auto *action = button(panel, name); QCOMPARE(action->size(), QSize(28, 28)); QCOMPARE(action->iconSize(), QSize(18, 18));
                QVERIFY(panel.rect().contains(bounds(action, &panel)));
                QVERIFY(!action->toolTip().isEmpty()); QVERIFY(!action->accessibleName().isEmpty());
            }
        }
        QCOMPARE(models(panel)->iconSize(), QSize(24, 24));
        for (int row = 0; row < models(panel)->count(); ++row) {
            auto *item = models(panel)->item(row); QCOMPARE(item->sizeHint().height(), qMax(32, models(panel)->fontMetrics().height() + 8));
            QVERIFY(!item->data(Qt::AccessibleDescriptionRole).toString().isEmpty());
            const auto icon = item->icon().pixmap(QSize(24, 24), 2); QCOMPARE(icon.size(), QSize(48, 48)); QCOMPARE(icon.devicePixelRatio(), 2.);
        }
    }
    void statusAndNoMatchesNeverResizeTheListOrActions() {
        QTemporaryDir directory; ModelLibrary library(directory.path()); QVERIFY(library.load());
        Canvas canvas; canvas.setProject(smallProject()); ModelLibraryPanel panel(&library, canvas.scene3D());
        panel.resize(240, 260); panel.show(); QCoreApplication::processEvents();
        auto *status = panel.findChild<QLabel *>("modelLibraryStatus");
        const auto listBounds = bounds(models(panel), &panel), actionBounds = bounds(button(panel, "add3DModel"), &panel);
        const auto message = QString("A very long model import warning that must remain readable without moving the list or the placement controls.");
        QVERIFY(QMetaObject::invokeMethod(canvas.scene3D(), "warning", Q_ARG(QString, message)));
        QCoreApplication::processEvents();
        QCOMPARE(status->text(), message); QCOMPARE(status->toolTip(), message); QCOMPARE(status->accessibleName(), message);
        QCOMPARE(bounds(models(panel), &panel), listBounds); QCOMPARE(bounds(button(panel, "add3DModel"), &panel), actionBounds);
        panel.findChild<QLineEdit *>("modelLibrarySearch")->setText("no model matches"); QCoreApplication::processEvents();
        QCOMPARE(models(panel)->count(), 0); QVERIFY(!button(panel, "add3DModel")->isEnabled());
        QVERIFY(!status->text().isEmpty()); QCOMPARE(bounds(models(panel), &panel), listBounds);
        panel.findChild<QLineEdit *>("modelLibrarySearch")->clear(); QCoreApplication::processEvents();
        QVERIFY(status->text().isEmpty()); QVERIFY(status->isVisible()); QCOMPARE(bounds(models(panel), &panel), listBounds);
        QCOMPARE(canvas.document()->history()->count(), 0); QCOMPARE(canvas.scene3D()->renderCount(), quint64(0));
    }
    void filtersSelectionAndSearchEntrySurviveUnrelatedRefresh() {
        QTemporaryDir directory; ModelLibrary library(directory.path()); QVERIFY(library.load());
        Canvas canvas; canvas.setProject(smallProject()); ModelLibraryPanel panel(&library, canvas.scene3D());
        panel.resize(360, 600); panel.show(); panel.activateWindow();
        auto *search = panel.findChild<QLineEdit *>("modelLibrarySearch");
        auto *tags = panel.findChild<QComboBox *>("modelLibraryTagFilter");
        const auto id = selectedId(panel); QVERIFY(library.setTags(id, {"Architecture and perspective reference"}));
        tags->setCurrentIndex(tags->findData("Architecture and perspective reference"));
        auto *row = models(panel)->currentItem(); QVERIFY(row); const auto iconKey = row->icon().cacheKey();
        search->setFocus(); search->setText(library.entry(id)->name.left(3)); search->setSelection(0, 2);
        const auto draft = search->text();
        button(panel, "favorite3DModel")->click();
        QCOMPARE(models(panel)->currentItem(), row); QCOMPARE(selectedId(panel), id); QCOMPARE(row->icon().cacheKey(), iconKey);
        QCOMPARE(search->text(), draft); QCOMPARE(search->selectedText(), draft.left(2));
        QCOMPARE(tags->currentData().toString(), QString("Architecture and perspective reference"));
        button(panel, "modelLibraryFavoritesOnly")->click(); QCOMPARE(models(panel)->count(), 1);
        QVERIFY(QMetaObject::invokeMethod(&library, "changed"));
        panel.resize(240, 260); QCoreApplication::processEvents();
        QCOMPARE(search->text(), draft); QCOMPARE(selectedId(panel), id); QVERIFY(button(panel, "favorite3DModel")->isChecked());
        QCOMPARE(canvas.document()->history()->count(), 0); QVERIFY(!canvas.scene3D()->scene());
        QCOMPARE(canvas.scene3D()->renderCount(), quint64(0));
    }
    void tagAndNameDialogsKeepDraftsDuringRefreshAndCancelSafely() {
        QTemporaryDir directory; ModelLibrary library(directory.filePath("library")); QVERIFY(library.load());
        const auto id = addTriangle(library, directory.filePath("triangle.gltf")); QVERIFY(!id.isEmpty());
        Canvas canvas; canvas.setProject(smallProject()); ModelLibraryPanel panel(&library, canvas.scene3D());
        panel.findChild<QTabBar *>("modelLibraryCollections")->setCurrentIndex(1); models(panel)->setCurrentRow(0);
        panel.resize(240, 260); panel.show(); panel.activateWindow();
        const auto originalName = library.entry(id)->name;
        chooseOption(panel, "Rename…", [&](QInputDialog *dialog) {
            dialog->setTextValue("City backdrop draft");
            QVERIFY(library.setFavorite(id, true)); panel.resize(300, 400);
            QCOMPARE(dialog->textValue(), QString("City backdrop draft")); QCOMPARE(library.entry(id)->name, originalName);
            dialog->reject();
        });
        QCOMPARE(library.entry(id)->name, originalName);
        chooseOption(panel, "Rename…", [&](QInputDialog *dialog) { dialog->setTextValue("City backdrop"); dialog->accept(); });
        QCOMPARE(library.entry(id)->name, QString("City backdrop")); QCOMPARE(selectedId(panel), id);
        chooseOption(panel, "Edit tags…", [&](QInputDialog *dialog) {
            dialog->setTextValue("Environment, Layout"); QVERIFY(library.setFavorite(id, false));
            QCOMPARE(dialog->textValue(), QString("Environment, Layout")); dialog->accept();
        });
        QCOMPARE(library.entry(id)->tags, (QStringList{"Environment", "Layout"})); QCOMPARE(selectedId(panel), id);
        chooseOption(panel, "Edit tags…", [&](QInputDialog *dialog) { dialog->setTextValue("Uncommitted"); dialog->reject(); });
        QCOMPARE(library.entry(id)->tags, (QStringList{"Environment", "Layout"}));
        QCOMPARE(canvas.document()->history()->count(), 0); QCOMPARE(canvas.scene3D()->renderCount(), quint64(0));
        QVERIFY(!canvas.scene3D()->scene());
    }
    void libraryPreviews() {
        if (qEnvironmentVariableIsEmpty("TOON_UI_PREVIEW")) QSKIP("Preview output is opt-in");
        QTemporaryDir directory; ModelLibrary library(directory.filePath("library")); QVERIFY(library.load());
        const auto id = addTriangle(library, directory.filePath("triangle.gltf")); QVERIFY(!id.isEmpty());
        QVERIFY(library.rename(id, "City backdrop — perspective study")); QVERIFY(library.setTags(id, {"Environment", "Perspective"}));
        Canvas canvas; canvas.setProject(smallProject()); ModelLibraryPanel panel(&library, canvas.scene3D());
        const auto output = qEnvironmentVariable("TOON_UI_PREVIEW"); QDir().mkpath(output);
        panel.resize(360, 600); panel.show(); QTest::qWait(30);
        QVERIFY(panel.grab().save(output + "/3d-library-starter.png"));
        panel.resize(240, 260); QTest::qWait(20);
        QVERIFY(panel.grab().save(output + "/3d-library-narrow-short.png"));
        panel.findChild<QTabBar *>("modelLibraryCollections")->setCurrentIndex(1); models(panel)->setCurrentRow(0);
        panel.resize(360, 600); QTest::qWait(20);
        QVERIFY(panel.grab().save(output + "/3d-library-assets.png"));
        panel.findChild<QLineEdit *>("modelLibrarySearch")->setText("No models match"); panel.resize(240, 260); QTest::qWait(20);
        QVERIFY(panel.grab().save(output + "/3d-library-empty.png"));
    }
};
QTEST_MAIN(ModelLibraryPanelTests)
#include "test_model_library_panel.moc"
