// Opt-in benchmark: never opens a native window. Run with QT_QPA_PLATFORM=offscreen.
// Usage: benchmark_selection [width height] [model|canvas] [no-focus|no-bar|no-overlays].
#include "canvas.h"
#include "document.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace toon;
namespace {
QString resolution;
QString profile;
void measure(const char *operation, int iterations, const std::function<void(int)> &work) {
    QVector<double> samples;
    for (int i = 0; i < iterations; ++i) {
        QElapsedTimer timer; timer.start(); work(i);
        samples.append(timer.nsecsElapsed() / 1000000.0);
    }
    std::sort(samples.begin(), samples.end());
    QJsonObject result{{"resolution", resolution}, {"operation", operation}, {"iterations", iterations},
        {"min_ms", samples.first()}, {"median_ms", samples[samples.size() / 2]}, {"max_ms", samples.last()}};
    if (!profile.isEmpty()) result.insert("profile", profile);
    std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << std::endl;
}
Project denseProject(int width, int height) {
    auto project = Project::create(); project.width = width; project.height = height;
    auto &layer = project.shots.first().layers.first();
    for (int y = 0; y < height; y += tileSize) for (int x = 0; x < width; x += tileSize) {
        QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::transparent);
        QPainter painter(&tile);
        const QRect valid(0, 0, qMin(tileSize, width - x), qMin(tileSize, height - y));
        painter.fillRect(valid, QColor::fromRgb(45 + (x / tileSize * 17) % 120,
                                               65 + (y / tileSize * 19) % 120, 150));
        painter.setClipRect(valid); painter.setPen(QPen(QColor("#c6d8d5"), 3));
        painter.drawLine(0, 0, tileSize, tileSize); painter.end();
        layer.tiles[{x / tileSize, y / tileSize}] = tile;
    }
    const auto error = project.validate();
    if (!error.isEmpty()) throw std::runtime_error(error.toStdString());
    return project;
}
QPainterPath fullSelection(int width, int height) {
    QPainterPath selection; selection.addRect(QRectF(0, 0, width, height)); return selection;
}
void stroke(Document &document) {
    BrushSettings brush; brush.size = 128; brush.spacing = .02; brush.pressureSize = false;
    brush.color = QColor("#d99469");
    if (!document.beginStroke(QPointF(100, 200), 1.0, brush, false))
        throw std::runtime_error("Could not start benchmark brush stroke");
    for (int i = 1; i <= 16; ++i) document.continueStroke(QPointF(100 + i * 32, 200), 1.0);
    document.endStroke();
}
}
int main(int argc, char **argv) {
    QApplication application(argc, argv);
    const auto arguments = application.arguments();
    const int width = arguments.size() > 1 ? arguments[1].toInt() : 1920;
    const int height = arguments.size() > 2 ? arguments[2].toInt() : 1080;
    const QString mode = arguments.size() > 3 ? arguments[3] : QStringLiteral("model");
    profile = arguments.size() > 4 ? arguments[4] : QString();
    if ((width != 1920 || height != 1080) && (width != 3840 || height != 2160)) return 2;
    if (mode != "model" && mode != "canvas") return 2;
    resolution = QString::number(width) + "x" + QString::number(height);
    try {
        const auto project = denseProject(width, height);
        const auto selection = fullSelection(width, height);
        if (mode == "model") {
            Document document; document.load(project); document.setRasterSelection(selection);
            measure("begin_transform", 1, [&](int) {
                if (!document.beginTransform()) throw std::runtime_error("Could not begin transform");
            });
            measure("preview_move", 3, [&](int index) {
                TransformParameters parameters; parameters.translation = QPointF(20 + index * 10, 12 + index * 5);
                if (!document.previewTransform(parameters)) throw std::runtime_error("Move preview failed");
            });
            measure("preview_rotate", 3, [&](int index) {
                TransformParameters parameters; parameters.rotation = 1 + index;
                if (!document.previewTransform(parameters)) throw std::runtime_error("Rotate preview failed");
            });
            measure("apply_transform", 1, [&](int) {
                document.applyTransform();
                if (document.transformActive()) throw std::runtime_error("Could not apply transform");
            });
        }
        if (mode == "model") {
            Document document; document.load(project); document.setRasterSelection(selection);
            measure("brush_large_selection_512px", 1, [&](int) { stroke(document); });
        }
        if (mode == "model") {
            Document document; document.load(project);
            measure("brush_no_selection_512px", 1, [&](int) { stroke(document); });
        }
        if (mode == "canvas") {
            Canvas canvas; canvas.resize(1200, 760); canvas.setProject(project);
            if (profile == "no-focus" || profile == "no-overlays") canvas.setSelectionFocusOverlay(false);
            if (profile == "no-bar" || profile == "no-overlays") canvas.setSelectionBarVisible(false);
            if (profile == "no-overlays") canvas.findChild<QWidget *>("selectionOverlay")->hide();
            canvas.document()->setRasterSelection(selection); canvas.ensurePolished(); canvas.fitSheet();
            QImage viewport(canvas.size(), QImage::Format_ARGB32_Premultiplied);
            canvas.render(&viewport); // Exclude first font/resource initialization.
            measure("canvas_render_1200x760", 3, [&](int) { canvas.render(&viewport); });
            if (!canvas.document()->beginTransform()) throw std::runtime_error("Could not begin canvas transform");
            measure("preview_move_update", 3, [&](int index) {
                TransformParameters parameters; parameters.translation = QPointF(20 + index * 10, 12 + index * 5);
                if (!canvas.document()->previewTransform(parameters)) throw std::runtime_error("Canvas move preview failed");
            });
            measure("preview_move_and_render", 3, [&](int index) {
                TransformParameters parameters; parameters.translation = QPointF(20 + index * 10, 12 + index * 5);
                if (!canvas.document()->previewTransform(parameters)) throw std::runtime_error("Canvas move preview failed");
                canvas.render(&viewport);
            });
            measure("preview_rotate_and_render", 3, [&](int index) {
                TransformParameters parameters; parameters.rotation = 1 + index;
                if (!canvas.document()->previewTransform(parameters)) throw std::runtime_error("Canvas rotate preview failed");
                canvas.render(&viewport);
            });
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << std::endl; return 1;
    }
    return 0;
}
