#include "reference3d_review.h"
#include "reference3d.h"
#include "reference3d_builtin.h"
#include "reference3d_catalog.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSlider>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <QUndoStack>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

using namespace toon;
namespace {
bool settle(Reference3DReviewWindow &window, int timeoutMs = 30000) {
    if (window.viewportReady()) return true;
    QEventLoop loop; QTimer poll, timeout; bool ready = false;
    poll.setInterval(2); poll.setTimerType(Qt::PreciseTimer); timeout.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (window.viewportReady()) { ready = true; loop.quit(); } });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(); timeout.start(timeoutMs); loop.exec(); return ready;
}
double percentile(QVector<double> values, double fraction) {
    if (values.isEmpty()) return 0;
    std::sort(values.begin(), values.end()); return values[qBound(0, int(std::ceil(fraction * values.size())) - 1, int(values.size()) - 1)];
}
QJsonObject measure(const ReferenceAssetPtr &asset, const QString &scenario, const QString &control, int intervalMs, bool closeup, const ReferenceScene *initialScene=nullptr) {
    QTemporaryDir directory; Reference3DReviewWindow window(nullptr, directory.path()); window.resize(1180, 760); window.show();
    QString error; QJsonObject row{{"scene", scenario}, {"control", control}, {"cadenceMs", intervalMs}, {"updatesRequested", 60}};
    // Source generation, canonical validation, window setup and the first frame
    // all complete before the measured sustained transform interaction begins.
    if (initialScene ? !window.setScene(*initialScene,&error) : (!asset || !window.addAsset(*asset, &error))) { row["error"] = error; return row; }
    if (closeup) { auto scene = window.scene(); scene.camera.distance *= .72f; if (!window.setScene(scene, &error)) { row["error"] = error; return row; } }
    if (!settle(window) || window.viewportImage().isNull()) { row["error"] = "Initial frame failed: " + window.viewportError(); return row; }
    auto *field = window.findChild<ValueSlider *>(control); auto *track = field ? field->findChild<QSlider *>() : nullptr;
    if (!field || !track) { row["error"] = "Transform control is unavailable"; return row; }
    const double initial = field->value();
    const double span = control.contains("Position") ? (closeup ? .06 : .45) : control.contains("Rotation") ? 90. : .3;
    const auto before = window.viewportImage(); qint64 previousKey = before.cacheKey();
    QVector<double> dispatchMs; QVector<qint64> publicationTimes; QVector<qint64> updateTimes;
    int updates = 0, readyDuringDrag = 0; bool timedOut = false; QElapsedTimer elapsed; elapsed.start();
    QEventLoop loop; QTimer driver, observe, safety; driver.setTimerType(Qt::PreciseTimer); driver.setInterval(intervalMs);
    observe.setTimerType(Qt::PreciseTimer); observe.setInterval(2); safety.setSingleShot(true);
    const int undoBefore = window.undoStack()->index(); track->setSliderDown(true);
    QObject::connect(&observe, &QTimer::timeout, &loop, [&] {
        const auto image = window.viewportImage(); const auto key = image.cacheKey();
        if (!image.isNull() && key != previousKey) { previousKey = key; publicationTimes.append(elapsed.elapsed()); }
        if (window.viewportReady()) ++readyDuringDrag;
    });
    QObject::connect(&driver, &QTimer::timeout, &loop, [&] {
        updateTimes.append(elapsed.elapsed()); QElapsedTimer dispatch; dispatch.start();
        field->setValue(initial + span * double(updates + 1) / 60.); dispatchMs.append(dispatch.nsecsElapsed() / 1e6);
        if (++updates == 60) loop.quit();
    });
    QObject::connect(&safety, &QTimer::timeout, &loop, [&] { timedOut = true; loop.quit(); });
    driver.start(); observe.start(); safety.start(30000); loop.exec(); driver.stop(); observe.stop(); safety.stop();
    const auto dragMs = elapsed.elapsed(); QElapsedTimer release; release.start(); track->setSliderDown(false);
    const double releaseMs = release.nsecsElapsed() / 1e6; QElapsedTimer settleTimer; settleTimer.start(); const bool ready = settle(window);
    const qint64 settleMs = settleTimer.elapsed();
    const auto final = window.viewportImage();
    row["updatesCompleted"] = updates; row["timedOut"] = timedOut; row["dragElapsedMs"] = double(dragMs);
    row["dispatchMeanMs"] = dispatchMs.isEmpty() ? 0 : std::accumulate(dispatchMs.cbegin(), dispatchMs.cend(), 0.) / dispatchMs.size();
    row["dispatchP95Ms"] = percentile(dispatchMs, .95); row["dispatchMaxMs"] = percentile(dispatchMs, 1);
    row["publicationsDuringDrag"] = publicationTimes.size(); row["readySamplesDuringDrag"] = readyDuringDrag;
    row["firstPublicationDuringDragMs"] = publicationTimes.isEmpty() ? -1 : double(publicationTimes.first());
    row["lastPublicationDuringDragMs"] = publicationTimes.isEmpty() ? -1 : double(publicationTimes.last());
    row["releaseDispatchMs"] = releaseMs; row["finalSettleMs"] = double(settleMs); row["finalReady"] = ready;
    row["finalImageChanged"] = !final.isNull() && final != before; row["finalControlValue"] = field->value();
    row["undoCommandsAdded"] = window.undoStack()->index() - undoBefore;
    row["viewportWidth"] = window.viewportWidget()->width(); row["viewportHeight"] = window.viewportWidget()->height();
    row["imageWidth"] = final.width(); row["imageHeight"] = final.height();
    row["renderError"] = window.viewportError();
    QVector<double> gaps; for (int i = 1; i < updateTimes.size(); ++i) gaps.append(double(updateTimes[i] - updateTimes[i - 1]));
    row["actualUpdateGapP95Ms"] = percentile(gaps, .95); row["actualUpdateGapMaxMs"] = percentile(gaps, 1);
    QElapsedTimer resize; resize.start(); window.resize(1040, 680); QCoreApplication::processEvents(QEventLoop::AllEvents); const bool resized = settle(window);
    row["resizeSettleMs"] = double(resize.elapsed()); row["resizeReady"] = resized;
    row["resizedImageWidth"] = window.viewportImage().width(); row["resizedImageHeight"] = window.viewportImage().height();
    return row;
}
}

int main(int argc, char **argv) {
    QApplication application(argc, argv); QStandardPaths::setTestModeEnabled(true); applyTheme();
    QString error; const auto figure = createReferenceStarter(ReferenceStarterKind::Figure, &error);
    const auto head = createReferenceStarter(ReferenceStarterKind::Head, &error);
    if (!figure || !head) { std::fprintf(stderr, "Could not create benchmark source: %s\n", qPrintable(error)); return 2; }
    QJsonArray rows;
    const auto catalogIds = qEnvironmentVariable("TOON_REFERENCE3D_BENCHMARK_MODELS").split(',', Qt::SkipEmptyParts);
    if (!catalogIds.isEmpty()) for (const auto &id : catalogIds) {
        const auto asset = createReferenceCatalogAsset(id, &error);
        if (!asset) { std::fprintf(stderr, "Catalog benchmark %s: %s\n", qPrintable(id), qPrintable(error)); return 2; }
        for (const auto &control : {QString("referencePositionX"), QString("referenceRotationZ"), QString("referenceScaleX")}) {
            const auto row = measure(asset, id, control, 12, false); rows.append(row);
            std::fprintf(stdout, "%s\n", QJsonDocument(row).toJson(QJsonDocument::Compact).constData()); std::fflush(stdout);
        }
    }
    else for (const bool closeup : {false, true}) for (const int interval : {12, 20})
        for (const auto &control : {QString("referencePositionX"), QString("referenceRotationZ"), QString("referenceScaleX")}) {
            const auto row = measure(closeup ? head : figure, closeup ? "head-closeup" : "single-figure", control, interval, closeup);
            rows.append(row); const auto line = QJsonDocument(row).toJson(QJsonDocument::Compact);
            std::fprintf(stdout, "%s\n", line.constData()); std::fflush(stdout);
        }
    const auto scenePath=qEnvironmentVariable("TOON_REFERENCE3D_BENCHMARK_SCENE");
    if(!scenePath.isEmpty()){
        const auto scene=loadReferenceScene(scenePath,&error);if(!scene){std::fprintf(stderr,"Scene benchmark: %s\n",qPrintable(error));return 2;}
        for(const auto &control:{QString("referencePositionX"),QString("referenceRotationZ"),QString("referenceScaleX")}){
            const auto row=measure({},"anime-pets-together",control,12,false,&*scene);rows.append(row);
            std::fprintf(stdout,"%s\n",QJsonDocument(row).toJson(QJsonDocument::Compact).constData());std::fflush(stdout);
        }
    }
    const QJsonObject report{{"schemaVersion", 1}, {"label", qEnvironmentVariable("TOON_REFERENCE3D_BENCHMARK_LABEL")},
        {"architecture", QSysInfo::currentCpuArchitecture()}, {"qtVersion", qVersion()}, {"platform", QApplication::platformName()},
        {"method", "60 actual ValueSlider changes per gesture, Qt precise timer at 12/20 ms; setup/first-frame excluded; publications observed by QImage cacheKey; no new diagnostics API"}, {"measurements", rows}};
    const auto output = qEnvironmentVariable("TOON_REFERENCE3D_BENCHMARK_OUTPUT");
    if (!output.isEmpty()) { QFile file(output); if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(report).toJson()) < 0) return 3; }
    return 0;
}
