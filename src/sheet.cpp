#include "sheet.h"
#include "animation.h"
#include "camera_animation.h"
#include "keyframe_render.h"
#include <QColorSpace>
#include <QFontMetricsF>
#include <QSaveFile>
#include <cmath>

namespace toon {
namespace {
QFont titleFont() { QFont f("Arial"); f.setPixelSize(30); f.setBold(true); return f; }
QFont bodyFont() { QFont f("Arial"); f.setPixelSize(22); return f; }
QString title(const Project &p, const Shot &s) { return p.movie + "  /  " + s.number; }
}

SheetGeometry sheetGeometry(const Project &p, const Shot &shot) {
    const auto &s = shot.sheet;
    const qreal w = p.width + s.overscan * 2 + s.left + s.right;
    const qreal textWidth = qMax(1.0, w - 56.0);
    const auto titleHeight = QFontMetricsF(titleFont()).boundingRect(
        QRectF(0, 0, textWidth, 10000), Qt::TextWrapAnywhere, title(p, shot)).height();
    const auto nameHeight = QFontMetricsF(bodyFont()).boundingRect(
        QRectF(0, 0, textWidth, 10000), Qt::TextWrapAnywhere, shot.name).height();
    // Font metrics can be fractional. Keep the artwork origin on a pixel
    // boundary so a sheet crop and the movie frame use the same sampling phase.
    const qreal header = s.showHeader ? std::ceil(qMax(qreal(s.header), titleHeight + nameHeight + 88.0)) : 0;
    SheetGeometry g;
    g.paper = QRectF(0, 0, w, header + s.top + p.height + 2 * s.overscan + s.bottom);
    g.header = QRectF(0, 0, w, header);
    g.drawing = QRectF(s.left, header + s.top, p.width + 2 * s.overscan, p.height + 2 * s.overscan);
    g.output = QRectF(g.drawing.x() + s.overscan, g.drawing.y() + s.overscan, p.width, p.height);
    return g;
}

void paintSheetBackground(QPainter &painter, const Project &p, const Shot &shot, const SheetGeometry &g) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(g.paper, Qt::white);
    if (g.header.isEmpty()) { painter.restore(); return; }
    painter.save();
    painter.setClipRect(g.header, Qt::IntersectClip);
    painter.setPen(QColor("#222a35"));
    painter.setFont(titleFont());
    QRectF titleBounds;
    painter.drawText(g.header.adjusted(28, 20, -28, -20), Qt::TextWrapAnywhere, title(p, shot), &titleBounds);
    painter.setFont(bodyFont());
    painter.drawText(QRectF(28, titleBounds.bottom() + 10, g.header.width() - 56,
        g.header.height() - titleBounds.bottom() - 52), Qt::TextWrapAnywhere, shot.name);
    painter.setPen(QColor("#596575"));
    const auto footer = QString("%1 × %2 px     %3 FPS     %4 frames / %5 s     LAYOUT")
            .arg(p.width).arg(p.height).arg(p.fps, 0, 'g', 6)
            .arg(shot.durationFrames).arg(shot.durationFrames / p.fps, 0, 'f', 2);
    painter.drawText(QRectF(28, g.header.bottom() - 36, g.header.width() - 56, 28),
        Qt::AlignLeft | Qt::AlignVCenter,
        QFontMetricsF(bodyFont()).elidedText(footer, Qt::ElideRight, g.header.width() - 56));
    painter.restore();
    painter.setPen(QPen(QColor("#b4bdc9"), 1));
    painter.drawLine(g.header.bottomLeft(), g.header.bottomRight());
    painter.restore();
}

void paintSheetArtwork(QPainter &painter, const Project &p, const Shot &shot, const SheetGeometry &g,
                       bool guides, const RasterLayerPreview *preview, bool smoothDisplay,
                       bool sceneReferences, int frame, bool applyCamera) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.save();
    // A pending transform can travel beyond the sheet and return intact. Keep
    // its preview visible in the workspace; committed artwork/export retains
    // the normal paper clip. Apply crops pixels to each layer's drawing bounds.
    if (!preview) painter.setClipRect(g.paper.adjusted(0, g.header.height(), 0, 0), Qt::IntersectClip);
    painter.translate(g.output.topLeft());
    const auto camera = applyCamera ? shotCameraTransform(shot, frame, QSizeF(p.width, p.height)) : QTransform();
    painter.setWorldTransform(camera, true);
    // Vector poses must be rasterized where the camera looks, including the
    // surrounding sheet. Cropping in output coordinates loses off-frame poses
    // that a camera pan brings into view.
    const auto visible = g.paper.adjusted(0, g.header.height(), 0, 0).translated(-g.output.topLeft());
    const auto sourceBounds = camera.inverted().mapRect(visible).adjusted(-2, -2, 2, 2);
    paintLayers(painter, evaluateLayers(p, shot, frame, false, sourceBounds), true, preview,
        smoothDisplay, sceneReferences ? QSize(p.width,p.height) : QSize());
    painter.restore();
    if (guides && shot.sheet.showGuides) {
        painter.setPen(QPen(QColor("#bdc8d6"), 1, Qt::DashLine));
        painter.drawRect(g.drawing);
        painter.setPen(QPen(QColor("#526d91"), 2));
        painter.drawRect(g.output);
        const auto center = g.output.center();
        for (auto pt : {QPointF(center.x(), g.output.top()), QPointF(center.x(), g.output.bottom())})
            painter.drawLine(pt - QPointF(0, 10), pt + QPointF(0, 10));
        for (auto pt : {QPointF(g.output.left(), center.y()), QPointF(g.output.right(), center.y())})
            painter.drawLine(pt - QPointF(10, 0), pt + QPointF(10, 0));
        if (shot.sheet.safeArea) {
            painter.setPen(QPen(QColor("#99a9bc"), 1, Qt::DashLine));
            painter.drawRect(g.output.adjusted(p.width * .05, p.height * .05, -p.width * .05, -p.height * .05));
        }
    }
    painter.restore();
}

void paintSheet(QPainter &painter, const Project &p, const Shot &shot, bool guides,
                const RasterLayerPreview *preview, bool smoothDisplay, bool sceneReferences, int frame, bool applyCamera) {
    const auto geometry = sheetGeometry(p, shot);
    paintSheetBackground(painter, p, shot, geometry);
    paintSheetArtwork(painter, p, shot, geometry, guides, preview, smoothDisplay, sceneReferences, frame, applyCamera);
}

QImage renderSheet(const Project &p, const Shot &shot, bool guides, int frame) {
    const auto size = sheetGeometry(p, shot).paper.size().toSize();
    if (qint64(size.width()) * size.height() > 64000000) return {};
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    image.setColorSpace(QColorSpace::SRgb);
    image.fill(Qt::white);
    QPainter painter(&image);
    const bool filtered = !shot.layerAnimations.isEmpty()
        || !shotCameraTransform(shot, frame, QSizeF(p.width, p.height)).isIdentity();
    paintSheet(painter, p, shot, guides, nullptr, filtered, false, frame);
    if (!keyframeRenderError().isEmpty()) return {};
    return image;
}

QImage renderMovieFrame(const Project &p, int frame, int shotIndex) {
    if (shotIndex < 0 || shotIndex >= p.shots.size() || frame < 0 || frame >= p.shots[shotIndex].durationFrames) return {};
    QImage image(p.width, p.height, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    image.setColorSpace(QColorSpace::SRgb);
    image.fill(p.transparent ? Qt::transparent : Qt::white);
    QPainter painter(&image);
    const auto &shot=p.shots[shotIndex]; QString error;
    const auto camera = shotCameraTransform(shot, frame, QSizeF(p.width, p.height));
    const auto sourceBounds = camera.inverted().mapRect(QRectF(0, 0, p.width, p.height)).adjusted(-2, -2, 2, 2);
    const auto layers=evaluateLayers(p,shot,frame,false,sourceBounds,&error);
    if(!error.isEmpty())return {};
    // Animated fractional transforms need filtered sampling in final artwork,
    // independently of the editor's canvas display preference.
    painter.setWorldTransform(camera, true);
    paintLayers(painter, layers, false, nullptr, !shot.layerAnimations.isEmpty() || !camera.isIdentity());
    if (!keyframeRenderError().isEmpty()) return {};
    return image;
}

bool savePng(const QImage &image, const QString &path, QString *error) {
    if (image.isNull()) { if (error) *error = "Could not allocate the export image."; return false; }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}
} // namespace toon
