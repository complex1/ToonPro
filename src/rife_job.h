#pragma once
#include "inbetween.h"
#include <atomic>
#include <memory>

namespace toon {
struct RifeRuntime {
    QString executable, model;
    QString error() const;
};
RifeRuntime findRifeRuntime();
struct RifeJobState {
    std::atomic_bool cancelled{false};
    std::atomic_int completed{0};
};
struct InbetweenResult {
    InbetweenPlan plan;
    QImage first, last;
    QVector<QImage> images;
    QString error;
    bool cancelled = false;
};
// Owned project snapshot and job state may outlive the panel. No UI or Document
// is accessed here. The helper uses typed arguments, with bounded cancel waits.
InbetweenResult generateInbetweens(const Project &, const InbetweenPlan &,
    const RifeRuntime &, const std::shared_ptr<RifeJobState> &);
} // namespace toon
