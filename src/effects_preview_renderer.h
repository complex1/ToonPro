#pragma once
#include "project.h"
#include "onion_settings.h"
#include "sheet.h"
#include <QObject>
#include <QHash>
#include <QImage>
#include <QRegion>
#include <QTransform>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
template<typename T> class QFutureWatcher;
class QTimer;

namespace toon {
enum class EffectsPreviewQuality { Draft, Exact };

// Immutable value snapshot. Prepare this on the owning GUI thread; workers
// never access Canvas, Document, widgets, the GPU or mutable application state.
struct EffectsPreviewSnapshot {
    quint64 context = 0, revision = 0;
    EffectsPreviewQuality quality = EffectsPreviewQuality::Exact;
    // Playback may display a completed older frame while the newest requested
    // frame waits. Edits/seeks use separate contexts and stay latest-only.
    bool playback = false;
    // Full, half, quarter or eighth source resolution during playback only.
    // Paused editing and exports never inherit this workspace preference.
    int playbackDivisor = 1;
    Project project;
    Shot shot;
    int frame = 0;
    QSize pixelSize;
    qreal devicePixelRatio = 1;
    int dotsPerMeterX = 3780, dotsPerMeterY = 3780;
    QTransform view;
    QRegion viewportClip; // Original logical widget clip; empty for Navigator.
    QColor surround = QColor("#1b1b1b");
    QColor symmetryColor = QColor("#bdbdbd");
    bool smoothDisplay = false, applyCamera = true, guides = true, drawPaperShadow = true;
    std::optional<RasterLayerPreview> transformPreview;
    BrushSymmetry symmetry;
    bool onionEnabled = false;
    OnionSkinSettings onionSettings;
    QString onionTrackId;
    QHash<QString,QImage> scenePreviews;
    QImage preparedBackground;
    std::optional<SheetGeometry> preparedGeometry;
};
struct EffectsPreviewResult {
    quint64 context = 0, revision = 0;
    EffectsPreviewQuality quality = EffectsPreviewQuality::Exact;
    bool playback = false;
    int frame = 0;
    QImage image;
    QString error;
    bool cancelled = false;
    qreal sourceScale = 1; // Actual source sampling, independent of scheduler quality.
    QString playbackFallbackReason;
};
using EffectsPreviewCancellation = std::shared_ptr<std::atomic_bool>;
using EffectsPreviewRenderFunction = std::function<EffectsPreviewResult(const EffectsPreviewSnapshot &, const EffectsPreviewCancellation &)>;
// Call on the GUI thread; cache/reuse these fields until view or sheet metadata changes.
bool prepareEffectsPreviewBackground(EffectsPreviewSnapshot &);
EffectsPreviewResult renderEffectsPreview(const EffectsPreviewSnapshot &, const EffectsPreviewCancellation &);

// One running job and one replaceable pending snapshot. Draft completions can
// advance the same context monotonically while newer values await rendering;
// Exact edit completions publish only for the latest requested revision.
// Playback keeps the running frame, replaces only the pending frame, and does
// not schedule idle refinements. This bounds work without starving display.
class EffectsPreviewRenderer final : public QObject {
    Q_OBJECT
public:
    explicit EffectsPreviewRenderer(QObject *parent = nullptr);
    EffectsPreviewRenderer(EffectsPreviewRenderFunction render, QObject *parent = nullptr);
    ~EffectsPreviewRenderer() override;
    void request(EffectsPreviewSnapshot snapshot);
    void invalidate();
    bool busy() const { return bool(m_running); }
    bool hasPending() const { return bool(m_pending) || bool(m_refinement); }
    quint64 latestRevision() const { return m_latestRevision; }
signals:
    void completed(const toon::EffectsPreviewResult &result);
private:
    struct Job;
    void start(EffectsPreviewSnapshot snapshot);
    void finished();
    EffectsPreviewRenderFunction m_render;
    QFutureWatcher<EffectsPreviewResult> *m_watcher;
    QTimer *m_refineTimer;
    std::shared_ptr<Job> m_running;
    std::optional<EffectsPreviewSnapshot> m_pending, m_refinement;
    quint64 m_generation = 0, m_context = 0, m_latestRevision = 0, m_publishedRevision = 0;
    EffectsPreviewQuality m_publishedQuality = EffectsPreviewQuality::Draft;
    bool m_hasContext = false, m_hasPublished = false, m_playback = false;
};
}
Q_DECLARE_METATYPE(toon::EffectsPreviewResult)
