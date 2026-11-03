#include "movie_review_renderer.h"
#include <QColorSpace>
#include <QHash>
#include <QThread>
#include <QTimer>
#include <algorithm>

namespace toon {
namespace {
QSize fittedSize(const Project &project, QSize available) {
    if (available.isEmpty() || project.width <= 0 || project.height <= 0) return {};
    available = available.boundedTo({4096, 4096});
    return QSize(project.width, project.height).scaled(available, Qt::KeepAspectRatio).expandedTo({1, 1});
}
// The shared preview painter includes editor Notes. Movie review follows the
// export compositor instead: annotation branches and 3D references stay out.
// Keep stable implicitly-shared array identities so reduced-source caching
// remains useful on every frame of a shot.
QVector<RasterLayer> movieLayers(const QVector<RasterLayer> &layers) {
    struct Cache { QVector<RasterLayer> source, output; };
    static thread_local Cache cache;
    if (cache.source.constData() == layers.constData() && cache.source.size() == layers.size()) return cache.output;
    QSet<QString> excluded;
    qint64 bytes = 0;
    for (const auto &layer : layers) {
        if (layer.annotation || layer.kind == LayerKind::Scene3D || layer.scene3d) excluded.insert(layer.id);
        for (const auto &tile : layer.tiles) bytes += tile.sizeInBytes();
        if (layer.mask) for (const auto &tile : layer.mask->tiles) bytes += tile.sizeInBytes();
    }
    if (excluded.isEmpty()) return layers;
    // Project validation limits folder nesting to eight levels.
    for (int depth = 0; depth <= 8; ++depth) {
        const auto count = excluded.size();
        for (const auto &layer : layers) if (excluded.contains(layer.parentId)) excluded.insert(layer.id);
        if (count == excluded.size()) break;
    }
    QVector<RasterLayer> result;
    result.reserve(layers.size());
    for (const auto &layer : layers) if (!excluded.contains(layer.id)) result.append(layer);
    cache = {};
    if (bytes <= 64 * 1024 * 1024) cache = {layers, result};
    return result;
}
EffectsPreviewResult renderReview(const EffectsPreviewSnapshot &source, const EffectsPreviewCancellation &cancel) {
    auto snapshot = source;
    snapshot.shot.layers = movieLayers(snapshot.shot.layers);
    return renderEffectsPreview(snapshot, cancel);
}
}

MovieReviewRenderer::MovieReviewRenderer(QObject *parent) : MovieReviewRenderer(renderReview, parent) {}
MovieReviewRenderer::MovieReviewRenderer(EffectsPreviewRenderFunction render, QObject *parent)
    : QObject(parent), m_frames(new EffectsPreviewRenderer(render, this)),
      m_thumbnails(new EffectsPreviewRenderer(std::move(render), this)) {
    connect(m_frames, &EffectsPreviewRenderer::completed, this, [this](const EffectsPreviewResult &result) {
        if (result.context != m_context || m_frameShot < 0) return;
        if (!result.error.isEmpty()) { emit failed(result.error); return; }
        if (!result.image.isNull()) emit frameReady(m_frameShot, result.frame, result.image);
    });
    connect(m_thumbnails, &EffectsPreviewRenderer::completed, this, [this](const EffectsPreviewResult &result) {
        if (result.context != m_thumbnailContext || m_thumbnailShot < 0) return;
        const int shot = m_thumbnailShot;
        m_thumbnailShot = -1;
        m_thumbnailRequested.remove(shot);
        if (!result.image.isNull()) m_thumbnailCache.insert(shot, new QImage(result.image));
        nextThumbnail();
        // A missing thumbnail does not interrupt audio or prevent reviewing
        // another shot; the full frame's error is surfaced by the viewport.
        if (!result.image.isNull()) emit thumbnailReady(shot, result.image);
    });
}
MovieReviewRenderer::~MovieReviewRenderer() { clear(); }
void MovieReviewRenderer::clear() {
    Q_ASSERT(thread() == QThread::currentThread());
    ++m_context; ++m_thumbnailContext;
    m_frames->invalidate(); m_thumbnails->invalidate();
    m_project = {};
    m_thumbnailCache.clear(); m_thumbnailQueue.clear(); m_thumbnailRequested.clear(); m_priorityThumbnails.clear();
    m_frameShot = m_frameNumber = m_thumbnailShot = -1;
    m_background = {}; m_thumbnailBackground = {}; m_frameSize = {};
}
void MovieReviewRenderer::setProject(const Project &project) {
    clear();
    m_project = project;
}
EffectsPreviewSnapshot MovieReviewRenderer::snapshot(int shotIndex, int localFrame, int divisor, bool playing, QSize pixels) {
    EffectsPreviewSnapshot result;
    // Rendering only needs the current shot plus the shared Symbol library;
    // an in-flight frame must not keep every shot's audio/images alive.
    result.project.width = m_project.width;
    result.project.height = m_project.height;
    result.project.fps = m_project.fps;
    result.project.transparent = m_project.transparent;
    result.project.symbols = m_project.symbols;
    result.shot = m_project.shots[shotIndex];
    result.frame = localFrame;
    result.pixelSize = pixels;
    result.playback = playing;
    result.playbackDivisor = divisor;
    result.smoothDisplay = true;
    result.applyCamera = true;
    result.guides = false;
    result.drawPaperShadow = false;
    const QRectF artwork(0, 0, m_project.width, m_project.height);
    result.preparedGeometry = SheetGeometry{artwork, {}, artwork, artwork};
    result.view.scale(pixels.width() / qreal(m_project.width), pixels.height() / qreal(m_project.height));
    auto &background = pixels.width() <= 192 && pixels.height() <= 108 ? m_thumbnailBackground : m_background;
    if (background.size() != pixels) {
        background = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
        background.setColorSpace(QColorSpace::SRgb);
        background.setDotsPerMeterX(result.dotsPerMeterX);
        background.setDotsPerMeterY(result.dotsPerMeterY);
        background.fill(m_project.transparent ? Qt::transparent : Qt::white);
    }
    result.preparedBackground = background;
    return result;
}
void MovieReviewRenderer::requestFrame(int shotIndex, int localFrame, int divisor, bool playing, QSize viewportPixels) {
    Q_ASSERT(thread() == QThread::currentThread());
    if (shotIndex < 0 || shotIndex >= m_project.shots.size() || localFrame < 0
        || localFrame >= m_project.shots[shotIndex].durationFrames) return;
    const auto pixels = fittedSize(m_project, viewportPixels);
    if (pixels.isEmpty()) return;
    if (divisor != 2 && divisor != 4 && divisor != 8) divisor = 1;
    if (!playing) divisor = 1;
    const bool sameView = shotIndex == m_frameShot && pixels == m_frameSize && divisor == m_divisor && playing == m_playing;
    if (sameView && localFrame == m_frameNumber) return;
    // A cut, loop wrap, seek or resized viewport must never publish an older
    // context. Forward playback within a shot may finish its current frame.
    if (!sameView || !playing || localFrame < m_frameNumber) ++m_context;
    m_frameShot = shotIndex; m_frameNumber = localFrame; m_frameSize = pixels;
    m_divisor = divisor; m_playing = playing;
    auto request = snapshot(shotIndex, localFrame, divisor, playing, pixels);
    request.context = m_context; request.revision = ++m_revision;
    m_frames->request(std::move(request));
}
QImage MovieReviewRenderer::cachedThumbnail(int shotIndex) const {
    const auto *image = m_thumbnailCache.object(shotIndex);
    return image ? *image : QImage{};
}
void MovieReviewRenderer::requestThumbnail(int shotIndex, bool prioritize) {
    Q_ASSERT(thread() == QThread::currentThread());
    if (shotIndex < 0 || shotIndex >= m_project.shots.size()) return;
    if (const auto *cached = m_thumbnailCache.object(shotIndex)) {
        const auto image = *cached;
        const auto context = m_thumbnailContext;
        QTimer::singleShot(0, this, [this, shotIndex, image, context] {
            if (context == m_thumbnailContext) emit thumbnailReady(shotIndex, image);
        });
        return;
    }
    if (m_thumbnailRequested.contains(shotIndex)) {
        if (prioritize && shotIndex != m_thumbnailShot) {
            m_thumbnailQueue.removeAll(shotIndex); m_thumbnailQueue.prepend(shotIndex);
            m_priorityThumbnails.insert(shotIndex);
        }
        return;
    }
    // Rapid scrolling never schedules the whole movie behind the playhead.
    if (m_thumbnailQueue.size() >= 32) {
        // Visible-strip requests must not evict the current/next cut's preview.
        auto remove = std::find_if(m_thumbnailQueue.begin(), m_thumbnailQueue.end(),
                                  [this](int shot) { return !m_priorityThumbnails.contains(shot); });
        if (remove == m_thumbnailQueue.end()) remove = m_thumbnailQueue.begin();
        const int shot = *remove;
        m_thumbnailQueue.erase(remove); m_thumbnailRequested.remove(shot); m_priorityThumbnails.remove(shot);
    }
    m_thumbnailRequested.insert(shotIndex);
    if (prioritize) { m_thumbnailQueue.prepend(shotIndex); m_priorityThumbnails.insert(shotIndex); }
    else m_thumbnailQueue.enqueue(shotIndex);
    nextThumbnail();
}
void MovieReviewRenderer::nextThumbnail() {
    if (m_thumbnailShot >= 0 || m_thumbnailQueue.isEmpty()) return;
    m_thumbnailShot = m_thumbnailQueue.dequeue();
    m_priorityThumbnails.remove(m_thumbnailShot);
    const auto pixels = fittedSize(m_project, {192, 108});
    const auto extent = std::max(m_project.width, m_project.height);
    const int divisor = extent > 1536 ? 8 : extent > 768 ? 4 : extent > 384 ? 2 : 1;
    auto request = snapshot(m_thumbnailShot, 0, divisor, true, pixels);
    request.context = m_thumbnailContext; request.revision = ++m_revision;
    m_thumbnails->request(std::move(request));
}
} // namespace toon
