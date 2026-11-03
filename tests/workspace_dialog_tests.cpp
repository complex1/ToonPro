#include "workspace_dialog.h"
#include "workspace_library.h"
#include "ui_theme.h"
#include <QDir>
#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QPointer>
#include <QSettings>
#include <QStyle>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

using namespace toon;
namespace {
WorkspaceSnapshot snapshot(){QMainWindow window;window.resize(800,600);auto *dock=new QDockWidget("Layers",&window);dock->setObjectName("layersPanel");dock->setWidget(new QLabel("Layers"));window.addDockWidget(Qt::RightDockWidgetArea,dock);return {window.saveState(3)};}
QToolButton *button(QWidget &widget,const char *name){return widget.findChild<QToolButton *>(name);}
void settle(){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);}
QLineEdit *visibleEditor(QListWidget *list){for(auto *editor:list->findChildren<QLineEdit *>())if(editor->isVisible())return editor;return nullptr;}
}
class WorkspaceDialogTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){applyTheme();}
    void emptyAndCorruptLibrariesHaveClearNonDestructiveStates(){
        QTemporaryDir directory;QSettings settings(directory.filePath("workspace.ini"),QSettings::IniFormat);WorkspaceLibrary library(&settings);QVERIFY(library.load());
        WorkspaceDialog empty(&library);empty.show();settle();QVERIFY(empty.findChild<QLabel *>("workspaceEmpty")->isVisible());QVERIFY(!button(empty,"workspaceRename")->isEnabled());QVERIFY(!button(empty,"workspaceDelete")->isEnabled());QVERIFY(!empty.findChild<QPushButton *>("workspaceUse")->isEnabled());
        empty.reject();settings.setValue(WorkspaceLibrary::settingsKey(),QByteArray("{corrupt"));settings.sync();QVERIFY(!library.load());
        WorkspaceDialog broken(&library);broken.show();settle();QVERIFY(broken.findChild<QLabel *>("workspaceError")->isVisible());QVERIFY(!broken.findChild<QLabel *>("workspaceError")->text().isEmpty());QVERIFY(!broken.findChild<QPushButton *>("workspaceUse")->isEnabled());QCOMPARE(settings.value(WorkspaceLibrary::settingsKey()).toByteArray(),QByteArray("{corrupt"));
    }
    void renameValidatesInlineAndDeleteKeepsOtherLayouts(){
        QTemporaryDir directory;QSettings settings(directory.filePath("workspace.ini"),QSettings::IniFormat);WorkspaceLibrary library(&settings);QVERIFY(library.load());QString first,second;const auto state=snapshot();
        QVERIFY(library.add("Painting",state,&first));QVERIFY(library.add("Inking",state,&second));
        WorkspaceDialog dialog(&library);dialog.show();QVERIFY(QTest::qWaitForWindowExposed(&dialog));dialog.raise();dialog.activateWindow();QVERIFY(QTest::qWaitForWindowActive(&dialog));
        auto *list=dialog.findChild<QListWidget *>("workspaceList");QCOMPARE(list->count(),2);list->setCurrentRow(0);button(dialog,"workspaceRename")->click();settle();
        auto *editor=visibleEditor(list);QVERIFY(editor);editor->selectAll();QTest::keyClicks(editor,"My painting");QTest::keyClick(editor,Qt::Key_Return);settle();
        QCOMPARE(library.find(first)->name,QString("My painting"));QCOMPARE(dialog.result(),int(QDialog::Rejected));QVERIFY(dialog.isVisible());QCOMPARE(dialog.selectedWorkspaceId(),first);
        button(dialog,"workspaceRename")->click();settle();editor=visibleEditor(list);QVERIFY(editor);editor->selectAll();QTest::keyClicks(editor,"INKING");QTest::keyClick(editor,Qt::Key_Return);settle();
        QCOMPARE(library.find(first)->name,QString("My painting"));QCOMPARE(list->currentItem()->text(),QString("My painting"));QVERIFY(!dialog.findChild<QLabel *>("workspaceError")->text().isEmpty());QVERIFY(dialog.isVisible());
        button(dialog,"workspaceDelete")->click();QTRY_COMPARE(list->count(),1);QVERIFY(!library.find(first));QVERIFY(library.find(second));QCOMPARE(dialog.selectedWorkspaceId(),second);QCOMPARE(library.find(second)->snapshot,state);
        dialog.findChild<QPushButton *>("workspaceUse")->click();QCOMPARE(dialog.result(),int(QDialog::Accepted));QCOMPARE(dialog.selectedWorkspaceId(),second);
        QSettings reopenedSettings(settings.fileName(),QSettings::IniFormat);WorkspaceLibrary reopened(&reopenedSettings);QVERIFY(reopened.load());QCOMPARE(reopened.entries(),library.entries());
    }
    void useReturnsIdentityWithoutChangingWorkspaceOrLibrary(){
        QTemporaryDir directory;QSettings settings(directory.filePath("workspace.ini"),QSettings::IniFormat);settings.setValue("layoutV3",QByteArray("current layout"));WorkspaceLibrary library(&settings);QVERIFY(library.load());QString id;QVERIFY(library.add("My animatic layout",snapshot(),&id));const auto before=settings.value(WorkspaceLibrary::settingsKey()).toByteArray();
        WorkspaceDialog dialog(&library);dialog.show();QVERIFY(QTest::qWaitForWindowExposed(&dialog));dialog.raise();dialog.activateWindow();QVERIFY(QTest::qWaitForWindowActive(&dialog));settle();auto *list=dialog.findChild<QListWidget *>("workspaceList");const auto rect=list->visualItemRect(list->item(0));
        QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,rect.center());QTest::mouseDClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,rect.center());QTRY_COMPARE(dialog.result(),int(QDialog::Accepted));QCOMPARE(dialog.selectedWorkspaceId(),id);QCOMPARE(settings.value(WorkspaceLibrary::settingsKey()).toByteArray(),before);QCOMPARE(settings.value("layoutV3").toByteArray(),QByteArray("current layout"));
        for(int variant=0;variant<3;++variant) {
            const auto key=variant?Qt::Key_Enter:Qt::Key_Return;
            const auto modifiers=variant==2?Qt::KeypadModifier:Qt::NoModifier;
            WorkspaceDialog keyboard(&library);keyboard.show();QVERIFY(QTest::qWaitForWindowExposed(&keyboard));keyboard.raise();keyboard.activateWindow();QVERIFY(QTest::qWaitForWindowActive(&keyboard));auto *keyboardList=keyboard.findChild<QListWidget *>("workspaceList");keyboardList->setFocus();QTRY_VERIFY(keyboardList->hasFocus());QTest::keyClick(keyboardList,key,modifiers);QCOMPARE(keyboard.result(),int(QDialog::Accepted));QCOMPARE(keyboard.selectedWorkspaceId(),id);
        }
    }
    void queuedLibraryRefreshPreservesAnOpenRenameEditor(){
        QTemporaryDir directory;QSettings settings(directory.filePath("workspace.ini"),QSettings::IniFormat);WorkspaceLibrary library(&settings);QVERIFY(library.load());QString first;QVERIFY(library.add("Painting",snapshot(),&first));
        WorkspaceDialog dialog(&library);dialog.show();QVERIFY(QTest::qWaitForWindowExposed(&dialog));dialog.raise();dialog.activateWindow();QVERIFY(QTest::qWaitForWindowActive(&dialog));
        auto *list=dialog.findChild<QListWidget *>("workspaceList");list->setCurrentRow(0);button(dialog,"workspaceRename")->click();QPointer<QLineEdit> editor=visibleEditor(list);QVERIFY(editor);editor->selectAll();QTest::keyClicks(editor,"My unfinished name");
        QVERIFY(library.add("Animation",snapshot()));settle();QVERIFY(editor&&editor->isVisible());QCOMPARE(visibleEditor(list),editor.data());QCOMPARE(editor->text(),QString("My unfinished name"));QCOMPARE(dialog.selectedWorkspaceId(),first);QCOMPARE(list->count(),2);
        QTest::keyClick(editor,Qt::Key_Escape);settle();QCOMPARE(library.find(first)->name,QString("Painting"));QVERIFY(dialog.isVisible());
    }
    void compactAndLargerTextKeepActionsAccessible(){
        QTemporaryDir directory;QSettings settings(directory.filePath("workspace.ini"),QSettings::IniFormat);WorkspaceLibrary library(&settings);QVERIFY(library.load());QVERIFY(library.add("A long custom workspace name for painting on the tablet",snapshot()));
        WorkspaceDialog dialog(&library);dialog.resize(280,320);dialog.show();settle();const auto original=dialog.font();const QString output=qEnvironmentVariable("TOON_UI_PREVIEW");if(!output.isEmpty())QVERIFY(QDir().mkpath(output));
        for(bool large:{false,true}){auto font=original;if(large)font.setPixelSize(QFontMetrics(original).height()+4);dialog.setFont(font);dialog.resize(280,320);settle();
            for(const auto *name:{"workspaceRename","workspaceDelete"}){auto *control=button(dialog,name);QVERIFY(control);QVERIFY(!control->accessibleName().isEmpty());QVERIFY(!control->toolTip().isEmpty());QCOMPARE(control->size(),QSize(28,28));QCOMPARE(control->iconSize(),QSize(16,16));QCOMPARE(control->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay,nullptr,control),600);QVERIFY(dialog.rect().contains(QRect(control->mapTo(&dialog,QPoint()),control->size())));}
            auto *use=dialog.findChild<QPushButton *>("workspaceUse");QVERIFY(dialog.rect().contains(QRect(use->mapTo(&dialog,QPoint()),use->size())));QCOMPARE(dialog.selectedWorkspaceId(),library.entries()[0].id);
            if(!output.isEmpty())QVERIFY(dialog.grab().save(output+QString("/workspaces-%1.png").arg(large?"large-text":"compact")));
        }
    }
};
QTEST_MAIN(WorkspaceDialogTests)
#include "workspace_dialog_tests.moc"
