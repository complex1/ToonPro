#pragma once
#include "movie_playback_sequence.h"
#include "project.h"
#include <QObject>
#include <QPointer>
#include <QTimer>

class QWidget;
namespace toon {
class TimelineController;
class MovieReviewRenderer;
class AudioPlayback;
enum class ReviewMode { Shot, Scene, Movie };

// Review has its own playhead and immutable project snapshot. Crossing a cut
// must never change the editing document, its selection, or its undo history.
class MovieReviewController final : public QObject {
    Q_OBJECT
public:
    explicit MovieReviewController(TimelineController *timeline);
    ~MovieReviewController() override;
    TimelineController *timeline() const { return m_timeline; }
    ReviewMode mode() const { return m_mode; }
    bool setMode(ReviewMode mode);
    const Project &project() const { return m_project; }
    const QVector<MoviePlaybackSegment> &segments() const { return m_segments; }
    int frameCount() const { return m_frameCount; }
    int currentFrame() const { return m_frame; }
    MoviePlaybackLocation location() const { return moviePlaybackLocation(m_segments,m_frame); }
    bool playing() const { return m_playing; }
    bool looping() const { return m_looping; }
    bool setFrame(int frame);
    void setPlaying(bool playing);
    void setLooping(bool looping);
    bool editCurrentShot();
    int previewDivisor() const;
    void setPreviewDivisor(int divisor);
    void requestThumbnail(int shotIndex);
signals:
    void changed();
    void sequenceChanged();
    void previewChanged(const QImage &image);
    void thumbnailReady(int shotIndex,const QImage &image);
protected:
    bool eventFilter(QObject *,QEvent *) override;
private:
    void tick();
    void requestPreview();
    void showFrame(int frame);
    void invalidateReview();
    TimelineController *m_timeline;
    MovieReviewRenderer *m_renderer;
    AudioPlayback *m_audio;
    QPointer<QWidget> m_viewport;
    Project m_project;
    QVector<MoviePlaybackSegment> m_segments;
    QTimer m_timer;
    ReviewMode m_mode = ReviewMode::Shot;
    int m_frame = 0, m_start = 0, m_frameCount = 0, m_prefetchShot = -1;
    bool m_playing = false, m_looping = true, m_waitingForFrame = false;
};
} // namespace toon
