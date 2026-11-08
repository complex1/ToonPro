#include "animation_test_fixture.h"
#include "canvas.h"
#include "document.h"
#include "easing_graph_widget.h"
#include "inbetween_panel.h"
#include "main_window.h"
#include "reference_panel.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

using namespace toon;
namespace {
QVector<TimelineCelRef> selected(const Project &project) {
    const auto &track=project.shots[0].animationTracks[0];
    return {{track.id,track.exposures[0].id},{track.id,track.exposures[1].id}};
}
struct Environment {
    QByteArray old=qgetenv("TOON_RIFE_ROOT");
    ~Environment() { if (old.isNull()) qunsetenv("TOON_RIFE_ROOT"); else qputenv("TOON_RIFE_ROOT",old); }
};
QString fakeRuntime(const QString &root,bool slow=false,bool broken=false) {
    QDir().mkpath(root+"/rife-v4.6");
    for (const auto &name : {"flownet.bin","flownet.param"}) { QFile model(root+"/rife-v4.6/"+name); model.open(QIODevice::WriteOnly); model.write("fixture"); }
    QFile script(root+"/rife-ncnn-vulkan"); script.open(QIODevice::WriteOnly);
    script.write("#!/bin/sh\n");
    script.write("printf '%s\\n' \"$@\" >> \"$(dirname \"$0\")/arguments.log\"\n");
    if (slow) script.write("sleep 5\n");
    script.write(broken?"exit 1\n":"cp \"$2\" \"$6\"\n"); script.close();
    script.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);
    return root;
}
void loadPair(Canvas &canvas,TimelineController &controller) {
    canvas.document()->load(animation_fixture::project()); controller.setSelectedCels(selected(canvas.document()->project()));
}
Project lineArtProject() {
    auto project=animation_fixture::project(); project.width=512; project.height=288;
    for (int i=0;i<2;++i) {
        QImage image(project.width,project.height,QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
        { QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(QPen(QColor(31,37,48),6)); painter.drawEllipse(QRectF(110+i*80,80,90,110)); }
        auto &layer=project.shots[0].layers[i?5:3]; layer.tiles.clear();
        for (int y=0;y<image.height();y+=tileSize) for (int x=0;x<image.width();x+=tileSize) {
            QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::transparent);
            { QPainter painter(&tile); painter.drawImage(QPoint(),image,QRect(x,y,tileSize,tileSize)); }
            layer.tiles.insert({x/tileSize,y/tileSize},tile);
        }
    }
    return project;
}
}
class InbetweenPanelTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void defaultsToLineArtAndRemembersOutputAcrossRecreation() {
        QTemporaryDir temporary; const auto path=temporary.filePath("settings.ini");
        Canvas canvas; TimelineController controller(&canvas); loadPair(canvas,controller);
        const auto before=canvas.document()->project().toJson();
        {
            QSettings settings(path,QSettings::IniFormat);
            InbetweenPanel panel(&controller,nullptr,&settings);
            auto *output=panel.findChild<QTabBar *>("inbetweenOutput");
            QCOMPARE(output->currentIndex(),1);
            output->setCurrentIndex(0);
            QCOMPARE(settings.value("inbetweens/outputMode").toString(),QString("white"));
        }
        {
            QSettings settings(path,QSettings::IniFormat);
            InbetweenPanel panel(&controller,nullptr,&settings);
            auto *output=panel.findChild<QTabBar *>("inbetweenOutput"); QCOMPARE(output->currentIndex(),0);
            output->setCurrentIndex(1);
        }
        {
            QSettings settings(path,QSettings::IniFormat);
            InbetweenPanel panel(&controller,nullptr,&settings);
            QCOMPARE(panel.findChild<QTabBar *>("inbetweenOutput")->currentIndex(),1);
            settings.setValue("inbetweens/outputMode","obsolete-mode"); settings.sync();
        }
        {
            QSettings settings(path,QSettings::IniFormat);
            InbetweenPanel panel(&controller,nullptr,&settings);
            QCOMPARE(panel.findChild<QTabBar *>("inbetweenOutput")->currentIndex(),1);
        }
        QCOMPARE(canvas.document()->history()->count(),0); QCOMPARE(canvas.document()->project().toJson(),before);
    }
    void selectsPosesGeneratesAndInsertsOneUndo() {
#ifdef Q_OS_WIN
        QSKIP("The fake helper uses a POSIX shell; real Windows runtime validation is separate.");
#endif
        Environment restore; QTemporaryDir temporary; QVERIFY(temporary.isValid());
        qputenv("TOON_RIFE_ROOT",fakeRuntime(temporary.path()+"/runtime").toUtf8());
        Canvas canvas; TimelineController controller(&canvas); loadPair(canvas,controller);
        InbetweenPanel panel(&controller); panel.resize(350,820); panel.show();
        auto *generate=panel.findChild<QPushButton *>("inbetweenGenerate"); auto *insert=panel.findChild<QPushButton *>("inbetweenInsert");
        QVERIFY(generate->isEnabled()); QVERIFY(!insert->isEnabled());
        const auto before=canvas.document()->project().toJson();
        panel.findChild<QTabBar *>("inbetweenEasingPresets")->setCurrentIndex(1);
        panel.generate(); QVERIFY(panel.busy()); QVERIFY(!insert->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),15000); QVERIFY2(panel.hasPreview(),qPrintable(panel.findChild<QLabel *>("inbetweenStatus")->text()));
        QCOMPARE(canvas.document()->history()->count(),0); QCOMPARE(canvas.document()->project().toJson(),before);
        QCOMPARE(panel.findChild<QListWidget *>("inbetweenFrameStrip")->count(),5);
        QFile arguments(temporary.path()+"/runtime/arguments.log"); QVERIFY(arguments.open(QIODevice::ReadOnly));
        const auto passed=arguments.readAll(); QVERIFY(passed.contains("0.0625")); QVERIFY(passed.contains("0.5625"));
        auto *view=panel.findChild<ReferenceView *>("inbetweenPreview"); view->setZoom(2);
        panel.findChild<QListWidget *>("inbetweenFrameStrip")->setCurrentRow(2); QCOMPARE(view->zoom(),2.0);
        QVERIFY(insert->isEnabled()); panel.insert(); QCOMPARE(canvas.document()->history()->count(),1);
        canvas.document()->history()->undo(); QCOMPARE(canvas.document()->project().toJson(),before);
        canvas.document()->history()->redo(); QCOMPARE(canvas.document()->activeShot().animationTracks[0].drawings.size(),5);
        QVERIFY(!panel.hasPreview());
    }
    void sourceEditAndCurveChangeInvalidatePreview() {
#ifdef Q_OS_WIN
        QSKIP("POSIX fake helper");
#endif
        Environment restore; QTemporaryDir temporary;
        qputenv("TOON_RIFE_ROOT",fakeRuntime(temporary.path()+"/runtime").toUtf8());
        Canvas canvas; TimelineController controller(&canvas); loadPair(canvas,controller); InbetweenPanel panel(&controller);
        panel.generate(); QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),15000); QVERIFY(panel.hasPreview());
        panel.findChild<QTabBar *>("inbetweenFrequency")->setCurrentIndex(1); QVERIFY(!panel.hasPreview());
        panel.generate(); QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),15000); QVERIFY(panel.hasPreview());
        QVERIFY(canvas.document()->selectAnimationTrack(canvas.document()->activeShot().animationTracks[0].id));
        QVERIFY(canvas.document()->renameAnimationDrawing("Changed pose")); QVERIFY(!panel.hasPreview());
        QVERIFY(!panel.findChild<QPushButton *>("inbetweenInsert")->isEnabled());
    }
    void cancelLeavesTimelineUntouched() {
#ifdef Q_OS_WIN
        QSKIP("POSIX fake helper");
#endif
        Environment restore; QTemporaryDir temporary;
        qputenv("TOON_RIFE_ROOT",fakeRuntime(temporary.path()+"/runtime",true).toUtf8());
        Canvas canvas; TimelineController controller(&canvas); loadPair(canvas,controller); InbetweenPanel panel(&controller);
        const auto before=canvas.document()->project().toJson(); panel.generate();
        QTest::qWait(150); panel.cancel(); QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),7000);
        QVERIFY(!panel.hasPreview()); QCOMPARE(canvas.document()->history()->count(),0); QCOMPARE(canvas.document()->project().toJson(),before);
    }
    void runtimeFailureAndTransparencyAreReadable() {
#ifdef Q_OS_WIN
        QSKIP("POSIX fake helper");
#endif
        Environment restore; QTemporaryDir temporary;
        qputenv("TOON_RIFE_ROOT",fakeRuntime(temporary.path()+"/runtime",false,true).toUtf8());
        Canvas canvas; TimelineController controller(&canvas); loadPair(canvas,controller); InbetweenPanel panel(&controller);
        panel.generate(); QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),15000);
        QVERIFY(!panel.hasPreview()); QVERIFY(panel.findChild<QLabel *>("inbetweenStatus")->text().contains("could not generate"));
        auto project=animation_fixture::project(); project.shots[0].layers[3].tiles.first().setPixelColor(1,1,Qt::transparent);
        qputenv("TOON_RIFE_ROOT",fakeRuntime(temporary.path()+"/runtime").toUtf8());
        canvas.document()->load(project); controller.setSelectedCels(selected(project));
        panel.generate(); QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),15000);
        QVERIFY(panel.hasPreview());
        panel.findChild<QTabBar *>("inbetweenOutput")->setCurrentIndex(0); QVERIFY(!panel.hasPreview());
        panel.generate(); QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),15000); QVERIFY(panel.hasPreview());
    }
    void workspaceHasDirectTimelineAction() {
        QTemporaryDir temporary; MainWindow window(temporary.filePath("settings.ini"));
        auto *dock=window.findChild<QDockWidget *>("inbetweenPanel"); QVERIFY(dock);
        auto *action=window.findChild<QToolButton *>("timelineInbetweens"); QVERIFY(action);
        QVERIFY(!action->accessibleName().isEmpty());
        const auto path=temporary.filePath("poses.toon"); QString error;
        const auto project=animation_fixture::project(); QVERIFY(saveProject(project,path,&error));
        QVERIFY(window.openProject(path)); window.show();
        window.findChild<TimelineController *>()->setSelectedCels(selected(project)); action->click();
        QVERIFY(!dock->isHidden());
    }
    void realRifeSmokeAndScreenshot() {
        if (qEnvironmentVariable("TOON_RIFE_REAL_TEST")!="1") QSKIP("Opt-in real GPU/model test");
        auto project=animation_fixture::project(); project.width=640; project.height=360;
        const auto directory=qEnvironmentVariable("TOON_RIFE_SAMPLE_DIR");
        for (int i=0;i<2;++i) {
            const QImage input(directory+QString("/%1.png").arg(i)); QVERIFY(!input.isNull());
            auto &layer=project.shots[0].layers[i?5:3]; layer.tiles.clear();
            for (int y=0;y<input.height();y+=tileSize) for (int x=0;x<input.width();x+=tileSize) {
                QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::transparent);
                { QPainter painter(&tile); painter.drawImage(QPoint(),input,QRect(x,y,tileSize,tileSize)); }
                layer.tiles.insert({x/tileSize,y/tileSize},tile);
            }
        }
        project.shots[0].animationTracks[0].exposures[0].durationFrames=4;
        Canvas canvas; TimelineController controller(&canvas); canvas.document()->load(project); controller.setSelectedCels(selected(project));
        InbetweenPanel panel(&controller); panel.resize(390,920); panel.show();
        panel.findChild<QTabBar *>("inbetweenOutput")->setCurrentIndex(0);
        panel.findChild<QTabBar *>("inbetweenEasingPresets")->setCurrentIndex(1);
        panel.generate(); QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),60000);
        QVERIFY2(panel.hasPreview(),qPrintable(panel.findChild<QLabel *>("inbetweenStatus")->text()));
        auto *strip=panel.findChild<QListWidget *>("inbetweenFrameStrip"); strip->setCurrentRow(2);
        const auto artifacts=qEnvironmentVariable("TOON_INBETWEEN_ARTIFACTS");
        if (!artifacts.isEmpty()) {
            QDir().mkpath(artifacts); QTest::qWait(100);
            QVERIFY(panel.grab().save(artifacts+"/inbetween-panel.png"));
        }
        const auto before=canvas.document()->project().toJson(); panel.insert();
        QCOMPARE(canvas.document()->history()->count(),1); QVERIFY(canvas.document()->project().validate().isEmpty());
        canvas.document()->history()->undo(); QCOMPARE(canvas.document()->project().toJson(),before);
        canvas.document()->history()->redo(); QVERIFY(canvas.document()->project().validate().isEmpty());
    }
    void realRifeTransparentLineArt() {
        if (qEnvironmentVariable("TOON_RIFE_REAL_TEST")!="1") QSKIP("Opt-in real GPU/model test");
        const auto project=lineArtProject(); const auto before=project.toJson();
        Canvas canvas; TimelineController controller(&canvas); canvas.document()->load(project); controller.setSelectedCels(selected(project));
        InbetweenPanel panel(&controller); panel.resize(390,980); panel.show();
        panel.findChild<QTabBar *>("inbetweenOutput")->setCurrentIndex(1);
        panel.generate(); QTRY_VERIFY_WITH_TIMEOUT(!panel.busy(),60000);
        QVERIFY2(panel.hasPreview(),qPrintable(panel.findChild<QLabel *>("inbetweenStatus")->text()));
        panel.findChild<QListWidget *>("inbetweenFrameStrip")->setCurrentRow(2);
        const auto artifacts=qEnvironmentVariable("TOON_INBETWEEN_ARTIFACTS");
        if (!artifacts.isEmpty()) { QDir().mkpath(artifacts); QTest::qWait(100); QVERIFY(panel.grab().save(artifacts+"/transparent-lineart-panel.png")); }
        panel.insert(); QCOMPARE(canvas.document()->history()->count(),1);
        QImage image(512,288,QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
        const auto &drawing=canvas.document()->activeShot().animationTracks[0].drawings[3];
        { QPainter painter(&image); for (const auto &layer:canvas.document()->activeShot().layers) if (layer.parentId==drawing.groupLayerId && !layer.group) paintRasterLayerPixels(painter,layer); }
        QCOMPARE(image.pixelColor(0,0).alpha(),0);
        bool soft=false,ink=false;
        for (int y=0;y<image.height();++y) for (int x=0;x<image.width();++x) {
            const int alpha=image.pixelColor(x,y).alpha(); soft |= alpha>0 && alpha<255; ink |= alpha>240;
        }
        QVERIFY(soft); QVERIFY(ink);
        if (!artifacts.isEmpty()) { QVERIFY(image.save(artifacts+"/transparent-midpoint.png")); QVERIFY(inbetweenWhiteMatte(image).save(artifacts+"/lineart-on-white.png")); }
        canvas.document()->history()->undo(); QCOMPARE(canvas.document()->project().toJson(),before);
        canvas.document()->history()->redo(); QVERIFY(canvas.document()->project().validate().isEmpty());
    }
};
QTEST_MAIN(InbetweenPanelTests)
#include "test_inbetween_panel.moc"
