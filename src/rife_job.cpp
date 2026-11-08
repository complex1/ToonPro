#include "rife_job.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QProcess>
#include <QTemporaryDir>
#include <new>

namespace toon {
QString RifeRuntime::error() const {
    if (!QFileInfo(executable).isExecutable() || !QFileInfo(model + "/flownet.bin").isFile()
        || !QFileInfo(model + "/flownet.param").isFile()) return "RIFE is unavailable in this build. See the In-betweens setup guide.";
    return {};
}
RifeRuntime findRifeRuntime() {
    QStringList roots;
    const auto configured = qEnvironmentVariable("TOON_RIFE_ROOT");
    if (!configured.isEmpty()) roots.append(configured);
    const auto app = QCoreApplication::applicationDirPath();
    roots << app + "/../Resources/rife" << app + "/rife";
    for (const auto &root : roots) {
#ifdef Q_OS_WIN
        const auto executable = root + "/rife-ncnn-vulkan.exe";
#else
        const auto executable = root + "/rife-ncnn-vulkan";
#endif
        RifeRuntime runtime{QDir::cleanPath(executable), QDir::cleanPath(root + "/rife-v4.6")};
        if (runtime.error().isEmpty()) return runtime;
    }
    return {};
}

InbetweenResult generateInbetweens(const Project &project, const InbetweenPlan &plan,
        const RifeRuntime &runtime, const std::shared_ptr<RifeJobState> &state) {
    InbetweenResult result; result.plan = plan;
    const auto cancelled = [&] { return state && state->cancelled.load(std::memory_order_relaxed); };
    const auto fail = [&](const QString &error) { result.error = error; result.images.clear(); return result; };
    try {
        if (cancelled()) { result.cancelled = true; return result; }
        const auto runtimeError = runtime.error();
        if (!runtimeError.isEmpty()) return fail(runtimeError);
        result.first = renderInbetweenSource(project, plan, false, &result.error);
        if (!result.error.isEmpty()) return fail(result.error);
        if (cancelled()) { result.cancelled = true; return result; }
        result.last = renderInbetweenSource(project, plan, true, &result.error);
        if (!result.error.isEmpty()) return fail(result.error);
        const auto ink=plan.output==InbetweenOutput::TransparentInk?inbetweenInkColor(result.first,result.last):QColor(Qt::black);
        const auto firstMatte=inbetweenWhiteMatte(result.first), lastMatte=inbetweenWhiteMatte(result.last);
        if (firstMatte.isNull() || lastMatte.isNull()) return fail("Not enough memory to prepare the source drawings.");
        if (plan.output==InbetweenOutput::WhiteBackground) { result.first=firstMatte; result.last=lastMatte; }
        QTemporaryDir temporary;
        if (!temporary.isValid()) return fail("Could not create temporary files for RIFE.");
        const auto first = temporary.filePath("first.png"), last = temporary.filePath("last.png");
        if (!firstMatte.save(first) || !lastMatte.save(last)) return fail("Could not write the source poses for RIFE.");
        for (const auto &sample : plan.samples) {
            if (cancelled()) { result.cancelled = true; result.images.clear(); return result; }
            // Curve endpoints can produce an exact hold. The helper itself
            // accepts only strict interior times, so preserve source pixels.
            QImage image;
            if (sample.time <= 0) image = firstMatte;
            else if (sample.time >= 1) image = lastMatte;
            else {
                const auto output = temporary.filePath("generated.png");
                QFile::remove(output);
                QProcess process;
                process.setProcessChannelMode(QProcess::MergedChannels);
                process.start(runtime.executable, {"-0", first, "-1", last, "-o", output,
                    "-m", runtime.model, "-s", QString::number(sample.time, 'g', 10), "-j", "1:1:1"});
                if (!process.waitForStarted(5000)) return fail("RIFE could not start: " + process.errorString());
                QElapsedTimer timeout; timeout.start(); QByteArray log;
                while (process.state() != QProcess::NotRunning) {
                    process.waitForFinished(50);
                    log.append(process.readAll());
                    if (log.size() > 8192) log = log.right(8192);
                    if (cancelled() || timeout.elapsed() > 120000) {
                        process.kill(); process.waitForFinished(3000);
                        if (cancelled()) { result.cancelled = true; result.images.clear(); return result; }
                        return fail("RIFE took too long to generate a drawing. Try a smaller canvas.");
                    }
                }
                log.append(process.readAll());
                if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
                    return fail("RIFE could not generate this preview. " + QString::fromUtf8(log.right(1200)).trimmed());
                QImageReader reader(output);
                if (reader.size() != plan.size) return fail("RIFE returned a drawing with unexpected dimensions.");
                image = reader.read();
                if (!inbetweenImageOpaque(image)) return fail("RIFE returned an invalid or transparent drawing.");
                image = image.convertToFormat(QImage::Format_RGB888);
            }
            if (plan.output==InbetweenOutput::TransparentInk) image=inbetweenTransparentInk(image,ink);
            if (image.isNull()) return fail("Not enough memory to prepare the generated drawings.");
            result.images.append(image);
            if (state) state->completed.fetch_add(1, std::memory_order_relaxed);
        }
        if (cancelled()) { result.cancelled = true; result.images.clear(); }
        return result;
    } catch (const std::bad_alloc &) {
        return fail("Not enough memory for this preview. Use fewer drawings or a smaller canvas.");
    }
}
} // namespace toon
