#include "character_recolor.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>
#include <QUuid>
#include <QtConcurrentRun>

using namespace toon;
namespace {
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QSize canvas(3840, 2160);
    CharacterModel character{id(), "Benchmark character", {}, {
        {id(), "Skin", CharacterColorCategory::Skin, QColor("#e7ad8b")},
        {id(), "Hair", CharacterColorCategory::Hair, QColor("#352031")}}};
    character.colorVariants = standardCharacterColorVariants(character.colors);
    character.colorVariants[0].colors[character.colors[0].id] = QColor("#6875a8");
    character.colorVariants[0].colors[character.colors[1].id] = QColor("#121e3b");
    const RecolorSettings settings{character, {}, character.colorVariants[0].id, canvas};
    QVector<RecolorTarget> sources;
    const QString shotId = id(); qint64 expectedChanges = 0;
    for (int drawing = 0; drawing < 3; ++drawing) {
        auto layer = RasterLayer::create(QString("4K colors %1").arg(drawing + 1));
        for (int ty = 0; ty < 9; ++ty) for (int tx = 0; tx < 15; ++tx) {
            QImage image(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
            for (int y = 0; y < tileSize && ty * tileSize + y < canvas.height(); ++y) {
                auto *pixels = reinterpret_cast<QRgb *>(image.scanLine(y));
                for (int x = 0; x < tileSize; ++x) {
                    if ((x + y + drawing) % 4 == 0) { pixels[x] = qRgb(0, 0, 0); continue; }
                    const auto color = character.colors[((x / 32) + (y / 32)) % 2].color;
                    const int alpha = y % 16 == 0 ? 128 : 255;
                    pixels[x] = qPremultiply(qRgba(color.red(), color.green(), color.blue(), alpha)); ++expectedChanges;
                }
            }
            layer.tiles[{tx, ty}] = image;
        }
        sources.append({shotId, layer.id, layer.name, layer});
    }
    QFutureWatcher<RecolorResult> preview, cancelledPreview;
    QTimer heartbeat; heartbeat.setInterval(10);
    QElapsedTimer clock, cancelClock;
    qint64 lastHeartbeat = 0, maximumGap = 0, ticks = 0;
    bool success = true;
    QJsonObject report{{"canvas", "3840 x 2160"}, {"drawings", 3}, {"tilePixels", double(405LL * tileSize * tileSize)},
                       {"expectedChangedPixels", double(expectedChanges)}};
    QObject::connect(&heartbeat, &QTimer::timeout, [&] {
        const auto now = clock.elapsed(); maximumGap = qMax(maximumGap, now - lastHeartbeat); lastHeartbeat = now; ++ticks;
    });
    QObject::connect(&cancelledPreview, &QFutureWatcher<RecolorResult>::finished, [&] {
        const auto result = cancelledPreview.result();
        report["cancelPhaseMs"] = double(cancelClock.elapsed());
        report["cancelled"] = result.cancelled;
        report["cancelledDrawingsDiscarded"] = result.drawings.isEmpty();
        success = success && result.cancelled && result.drawings.isEmpty(); report["passed"] = success;
        QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Indented);
        app.exit(success ? 0 : 1);
    });
    QObject::connect(&preview, &QFutureWatcher<RecolorResult>::finished, [&] {
        heartbeat.stop(); const auto elapsed = clock.elapsed(); maximumGap = qMax(maximumGap, elapsed - lastHeartbeat);
        const auto result = preview.result();
        report["previewMs"] = double(elapsed); report["heartbeatTicks"] = double(ticks);
        report["maxHeartbeatGapMs"] = double(maximumGap); report["changedPixels"] = double(result.changedPixels);
        report["error"] = result.error;
        success = result.error.isEmpty() && !result.cancelled && result.drawings.size() == 3 && result.changedPixels == expectedChanges;
        qint64 retainedPreviewPixels = 0;
        for (const auto &drawing : result.drawings) {
            retainedPreviewPixels += qint64(drawing.before.width()) * drawing.before.height() + qint64(drawing.after.width()) * drawing.after.height();
            success = success && drawing.before.width() <= 2048 && drawing.before.height() <= 2048
                && drawing.after.width() <= 2048 && drawing.after.height() <= 2048;
        }
        report["retainedPreviewPixels"] = double(retainedPreviewPixels);
        auto cancellation = std::make_shared<std::atomic_bool>(false);
        cancelClock.start();
        cancelledPreview.setFuture(QtConcurrent::run([sources, settings, cancellation] { return prepareCharacterRecolor(sources, settings, cancellation); }));
        QTimer::singleShot(10, &app, [cancellation] { cancellation->store(true); });
    });
    clock.start(); heartbeat.start();
    preview.setFuture(QtConcurrent::run([sources, settings] { return prepareCharacterRecolor(sources, settings); }));
    return app.exec();
}
