#pragma once
#include "effects_preview_renderer.h"
#include <QCache>
#include <QQueue>
#include <QSet>

namespace toon {
// Review owns an immutable project snapshot. Rendering never changes the
// active document, its shot selection, drawing history or editor overlays.
class MovieReviewRenderer final : public QObject {
    Q_OBJECT
public:
    explicit MovieReviewRenderer(QObject *parent = nullptr);
    MovieReviewRenderer(EffectsPreviewRenderFunction render, QObject *parent = nullptr);
    ~MovieReviewRenderer() override;
    void setProject(const Project &project);
    void clear();
    void requestFrame(int shotIndex, int localFrame, int divisor, bool playing, QSize viewportPixels);
    QImage cachedThumbnail(int shotIndex) const;
    void requestThumbnail(int shotIndex, bool prioritize = false);
signals:
    void frameReady(int shotIndex, int localFrame, const QImage &image);
    void thumbnailReady(int shotIndex, const QImage &image);
    void failed(const QString &reason);
private:
    EffectsPreviewSnapshot snapshot(int shotIndex, int localFrame, int divisor, bool playing, QSize pixels);
    void nextThumbnail();
    Project m_project;
    EffectsPreviewRenderer *m_frames, *m_thumbnails;
    QCache<int, QImage> m_thumbnailCache{128};
    QQueue<int> m_thumbnailQueue;
    QSet<int> m_thumbnailRequested, m_priorityThumbnails;
    quint64 m_context = 1, m_revision = 0, m_thumbnailContext = 1;
    int m_frameShot = -1, m_frameNumber = -1, m_divisor = 1, m_thumbnailShot = -1;
    bool m_playing = false;
    QSize m_frameSize;
    QImage m_background, m_thumbnailBackground;
};
} // namespace toon
