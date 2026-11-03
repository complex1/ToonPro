#include "auto_actions.h"
#include "raster_tools.h"
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace toon;

namespace {
Project blank() {
    auto project = Project::create();
    project.width = project.height = 96;
    project.transparent = true;
    project.shots[0].sheet.overscan = 0;
    return project;
}
QPainterPath rectangle(QRectF bounds) {
    QPainterPath path;
    path.addRect(bounds);
    return path;
}
void putImage(RasterLayer &layer, const QImage &image) {
    QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
    tile.fill(Qt::transparent);
    QPainter painter(&tile);
    painter.drawImage(0, 0, image);
    painter.end();
    layer.tiles[{0, 0}] = tile;
}
const RasterLayer &active(const Document &document) {
    return document.activeShot().layers[document.activeLayer()];
}
QColor pixel(const Document &document, int x, int y) {
    const auto tile = active(document).tiles.constFind({x / tileSize, y / tileSize});
    return tile == active(document).tiles.cend() ? QColor(Qt::transparent) : tile->pixelColor(x % tileSize, y % tileSize);
}
Project outlined(RasterSampleSource source) {
    auto project = blank();
    QImage image(96, 96, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setPen(QPen(Qt::black, 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(20, 20, 56, 56);
    painter.end();
    if (source == RasterSampleSource::ActiveLayer) {
        putImage(project.shots[0].layers[0], image);
    } else {
        auto ink = RasterLayer::create("Ink reference");
        // Reference mode must sample the chosen layer even when it is hidden.
        ink.visible = source != RasterSampleSource::ReferenceLayer;
        putImage(ink, image);
        project.shots[0].layers.append(ink);
    }
    return project;
}
RasterFillSettings settingsFor(const Document &document, RasterSampleSource source) {
    RasterFillSettings settings;
    settings.source = source;
    settings.tolerance = 12;
    settings.gapClosing = 2;
    if (source == RasterSampleSource::ReferenceLayer) settings.referenceLayerId = document.activeShot().layers[1].id;
    return settings;
}
}

class AutoActionFillTests : public QObject {
    Q_OBJECT
private slots:
    void fillRecordsColorAndUsesTargetSelectionAndFeather() {
        QTemporaryDir dir;
        Document document;
        document.load(blank());
        document.setRasterSelection(rectangle({10, 10, 25, 25}));
        QVERIFY(RasterTools::featherSelection(&document, 2));
        AutoActions actions(&document, dir.filePath("actions.json"));
        const auto id = actions.create("Selected color");
        QVERIFY(actions.startRecording(id));
        const QColor color(70, 130, 210, 160);
        QVERIFY(document.fillRasterSelection(color));
        QVERIFY(actions.recording());
        actions.stopRecording();
        QCOMPARE(actions.action(id)->steps.size(), 1);
        const auto &step = actions.action(id)->steps[0];
        QCOMPARE(step.operation, QString("fill"));
        QCOMPARE(step.parameters, QJsonObject({{"color", color.name(QColor::HexArgb)}}));

        document.load(blank());
        const auto targetSelection = rectangle({50, 40, 25, 25});
        document.setRasterSelection(targetSelection);
        QVERIFY(RasterTools::featherSelection(&document, 5));
        Document expected;
        expected.load(document.project());
        expected.setRasterSelection(targetSelection);
        QVERIFY(RasterTools::featherSelection(&expected, 5));
        QVERIFY(expected.fillRasterSelection(color));
        QVERIFY(actions.play(id));
        QCOMPARE(active(document).tiles, active(expected).tiles);
        QCOMPARE(pixel(document, 20, 20).alpha(), 0);
        QCOMPARE(document.rasterSelection().path, targetSelection);
        QCOMPARE(document.rasterSelection().featherRadius, 5.);
        QCOMPARE(document.history()->count(), 1);
        document.history()->undo();
        QVERIFY(active(document).tiles.isEmpty());
        QCOMPARE(document.rasterSelection().path, targetSelection);
        QCOMPARE(document.rasterSelection().featherRadius, 5.);
        document.history()->redo();
        QCOMPARE(active(document).tiles, active(expected).tiles);

        AutoActions reopened(&document, actions.path());
        QVERIFY(reopened.load());
        QCOMPARE(reopened.action(id)->steps[0].parameters, step.parameters);
    }

    void fillWithoutTargetSelectionFillsLayerAndRecolorsExistingTiles() {
        QTemporaryDir dir;
        Document document;
        document.load(blank());
        document.selectAllPixels();
        AutoActions actions(&document, dir.filePath("actions.json"));
        const auto id = actions.create("Flat fill");
        QVERIFY(actions.startRecording(id));
        QVERIFY(document.fillRasterSelection(Qt::red));
        actions.stopRecording();

        auto project = blank();
        QImage image(96, 96, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::blue);
        putImage(project.shots[0].layers[0], image);
        document.load(project);
        const auto before = active(document).tiles;
        const auto beforeJson = document.project().toJson();
        QVERIFY(!document.rasterSelection().active);
        QVERIFY(actions.play(id));
        // Pixel edits must count even though the project metadata and tile keys are unchanged.
        QCOMPARE(document.project().toJson(), beforeJson);
        QCOMPARE(pixel(document, 0, 0), QColor(Qt::red));
        QCOMPARE(pixel(document, 95, 95), QColor(Qt::red));
        QCOMPARE(pixel(document, 96, 96).alpha(), 0);
        QVERIFY(!document.rasterSelection().active);
        QCOMPARE(document.history()->count(), 1);
        document.history()->undo();
        QCOMPARE(active(document).tiles, before);
        QVERIFY(!document.rasterSelection().active);
        document.history()->redo();
        QCOMPARE(pixel(document, 40, 40), QColor(Qt::red));
    }

    void bucketRecordsAndReplaysSamplingSettings_data() {
        QTest::addColumn<int>("sourceValue");
        QTest::newRow("active-layer") << int(RasterSampleSource::ActiveLayer);
        QTest::newRow("all-layers") << int(RasterSampleSource::AllLayers);
        QTest::newRow("hidden-reference-layer") << int(RasterSampleSource::ReferenceLayer);
    }
    void bucketRecordsAndReplaysSamplingSettings() {
        QFETCH(int, sourceValue);
        const auto source = RasterSampleSource(sourceValue);
        QTemporaryDir dir;
        Document document;
        document.load(outlined(source));
        const auto settings = settingsFor(document, source);
        AutoActions actions(&document, dir.filePath("actions.json"));
        const auto id = actions.create("Bucket fill");
        QVERIFY(actions.startRecording(id));
        const QPointF point(40.5, 40.75);
        const QColor color(20, 140, 90, 220);
        QVERIFY(RasterTools::bucket(&document, point, color, settings));
        QVERIFY(actions.recording());
        actions.stopRecording();
        QCOMPARE(actions.action(id)->steps.size(), 1);
        const auto step = actions.action(id)->steps[0];
        QCOMPARE(step.operation, QString("bucket"));
        QCOMPARE(step.parameters.value("x").toDouble(), point.x());
        QCOMPARE(step.parameters.value("y").toDouble(), point.y());
        QCOMPARE(step.parameters.value("color").toString(), color.name(QColor::HexArgb));
        QCOMPARE(step.parameters.value("source").toInt(), sourceValue);
        QCOMPARE(step.parameters.value("tolerance").toInt(), 12);
        QCOMPARE(step.parameters.value("gapClosing").toInt(), 2);
        QCOMPARE(step.parameters.contains("referenceOffset"), source == RasterSampleSource::ReferenceLayer);
        if (source == RasterSampleSource::ReferenceLayer) {
            QCOMPARE(step.parameters.value("referenceOffset").toInt(), 1);
            QVERIFY(!QJsonDocument(step.parameters).toJson().contains(settings.referenceLayerId.toUtf8()));
        }

        // This fixture has fresh IDs; the recorded reference must resolve relatively.
        document.load(outlined(source));
        const auto selection = rectangle({0, 0, 96, 55});
        document.setRasterSelection(selection);
        QVERIFY(RasterTools::featherSelection(&document, 2));
        const auto before = active(document).tiles;
        Document expected;
        expected.load(document.project());
        expected.setRasterSelection(selection);
        QVERIFY(RasterTools::featherSelection(&expected, 2));
        QVERIFY(RasterTools::bucket(&expected, point, color, settingsFor(expected, source)));
        QVERIFY(actions.play(id));
        QCOMPARE(active(document).tiles, active(expected).tiles);
        QVERIFY(pixel(document, 40, 40).alpha() > 200);
        QCOMPARE(pixel(document, 40, 65).alpha(), 0);
        QCOMPARE(pixel(document, 5, 5).alpha(), 0);
        QCOMPARE(document.rasterSelection().path, selection);
        QCOMPARE(document.rasterSelection().featherRadius, 2.);
        QCOMPARE(document.history()->count(), 1);
        document.history()->undo();
        QCOMPARE(active(document).tiles, before);
        QCOMPARE(document.rasterSelection().path, selection);
        document.history()->redo();
        QCOMPARE(active(document).tiles, active(expected).tiles);
    }

    void bucketClampsRecordedSettingsToTheAppliedValues() {
        QTemporaryDir dir;
        Document document;
        document.load(blank());
        AutoActions actions(&document, dir.filePath("actions.json"));
        const auto id = actions.create("Clamped bucket");
        QVERIFY(actions.startRecording(id));
        RasterFillSettings settings;
        settings.tolerance = 999;
        settings.gapClosing = -10;
        QVERIFY(RasterTools::bucket(&document, {30, 30}, Qt::red, settings));
        actions.stopRecording();
        QCOMPARE(actions.action(id)->steps.size(), 1);
        QCOMPARE(actions.action(id)->steps[0].parameters.value("tolerance").toInt(), 255);
        QCOMPARE(actions.action(id)->steps[0].parameters.value("gapClosing").toInt(), 0);
    }

    void invalidAndUnchangedFillsDoNotRecord() {
        QTemporaryDir dir;
        Document document;
        document.load(blank());
        document.setRasterSelection(rectangle({20, 20, 20, 20}));
        AutoActions actions(&document, dir.filePath("actions.json"));
        const auto id = actions.create("No-op fills");
        QVERIFY(actions.startRecording(id));
        QVERIFY(document.fillRasterSelection(Qt::transparent));
        QVERIFY(!document.fillRasterSelection(QColor()));
        QVERIFY(RasterTools::bucket(&document, {5, 5}, Qt::red));
        RasterFillSettings settings;
        settings.source = RasterSampleSource(99);
        QVERIFY(!RasterTools::bucket(&document, {30, 30}, Qt::red, settings));
        settings.source = RasterSampleSource::ReferenceLayer;
        settings.referenceLayerId = "missing-reference";
        QVERIFY(!RasterTools::bucket(&document, {30, 30}, Qt::red, settings));
        QVERIFY(actions.recording());
        QVERIFY(actions.action(id)->steps.isEmpty());
        QVERIFY(active(document).tiles.isEmpty());
        QCOMPARE(document.history()->count(), 0);
        actions.stopRecording();
    }

    void lockedAndMissingReferenceTargetsFailAtomically() {
        QTemporaryDir dir;
        Document document;
        document.load(outlined(RasterSampleSource::ReferenceLayer));
        AutoActions actions(&document, dir.filePath("actions.json"));
        const auto id = actions.create("Reference fill");
        QVERIFY(actions.startRecording(id));
        QVERIFY(RasterTools::bucket(&document, {40, 40}, Qt::red, settingsFor(document, RasterSampleSource::ReferenceLayer)));
        actions.stopRecording();

        for (bool locked : {false, true}) {
            auto project = locked ? outlined(RasterSampleSource::ReferenceLayer) : blank();
            project.shots[0].layers[0].locked = locked;
            document.load(project);
            const auto selection = rectangle({10, 10, 70, 70});
            document.setRasterSelection(selection);
            const auto before = active(document).tiles;
            const auto beforeJson = document.project().toJson();
            QVERIFY(!actions.play(id));
            QCOMPARE(active(document).tiles, before);
            QCOMPARE(document.project().toJson(), beforeJson);
            QCOMPARE(document.rasterSelection().path, selection);
            QCOMPARE(document.history()->count(), 0);
        }
    }

    void fillReplayHonorsAlphaLock() {
        QTemporaryDir dir;
        Document document;
        document.load(blank());
        document.selectAllPixels();
        AutoActions actions(&document, dir.filePath("actions.json"));
        const auto id = actions.create("Recolor");
        QVERIFY(actions.startRecording(id));
        QVERIFY(document.fillRasterSelection(Qt::red));
        actions.stopRecording();

        auto project = blank();
        QImage image(96, 96, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        { QPainter painter(&image); painter.fillRect(20, 20, 30, 30, QColor(0, 0, 255, 128)); }
        putImage(project.shots[0].layers[0], image);
        project.shots[0].layers[0].alphaLocked = true;
        document.load(project);
        QVERIFY(actions.play(id));
        QCOMPARE(pixel(document, 30, 30), QColor(255, 0, 0, 128));
        QCOMPARE(pixel(document, 10, 10).alpha(), 0);
        QVERIFY(active(document).alphaLocked);
        QVERIFY(!document.rasterSelection().active);
        document.history()->undo();
        QCOMPARE(active(document).tiles, project.shots[0].layers[0].tiles);
    }
};

QTEST_MAIN(AutoActionFillTests)
#include "test_auto_action_fill.moc"
