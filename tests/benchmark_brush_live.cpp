// Opt-in live drawing benchmark. All settings/project writes use a temporary
// directory. Optional first argument: directory containing brushes.json/tools.json
// to COPY for a read-only reproduction of a locally configured brush.
// --catalog and --large use isolated built-in/synthetic brushes instead. --events N
// controls samples along the same path (default 240); --large is opt-in, never CI.
#include "main_window.h"
#include "canvas.h"
#include "tool_catalog.h"
#include "catalog_brushes.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace toon;
namespace {
QPointF point(qreal index) {
    return {150 + index * 6., 520 + std::sin(index * .05) * 220};
}
void send(Canvas &canvas, QEvent::Type type, QPointF view, bool contact = true) {
    QMouseEvent event(type, view, canvas.mapToGlobal(view),
        type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
        type == QEvent::MouseButtonRelease || !contact ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &event);
}
QString hash(const Document &document) {
    const auto image = renderMovieFrame(document.project());
    return QString::fromLatin1(QCryptographicHash::hash(QByteArrayView(
        reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes()), QCryptographicHash::Sha256).toHex());
}
void timing(QJsonObject &result, const QString &prefix, QVector<double> values) {
    if (values.isEmpty()) return;
    std::sort(values.begin(), values.end());
    result.insert(prefix + "_median_ms", values[values.size()/2]);
    result.insert(prefix + "_p95_ms", values[values.size()*95/100]);
    result.insert(prefix + "_max_ms", values.back());
}
void report(const QString &profile, const QString &mode, const QVector<double> &events,
            const QVector<double> &paints, double release, const Document &document, QJsonObject result) {
    const auto artifacts=qEnvironmentVariable("TOON_BRUSH_BENCHMARK_IMAGES");
    if(!artifacts.isEmpty()) { QDir().mkpath(artifacts);renderMovieFrame(document.project()).save(artifacts+"/"+profile+"-"+mode+".png"); }
    result.insert("profile", profile); result.insert("mode", mode); result.insert("events", events.size());
    timing(result, "input", events); timing(result, "paint", paints);
    result.insert("release_ms", release); result.insert("artwork_sha256", hash(document));
    std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << std::endl;
}
QString run(MainWindow &window, const Project &project, const QString &name, const BrushSettings &brush, bool workspace, qreal smoothing = 0, int eventCount = 240, bool measureHover = false) {
    Canvas *canvas = window.canvas();
    Document local; local.load(project);
    canvas->setProject(project); canvas->setTool(Canvas::Tool::Brush); canvas->applyBrushSettings(brush,smoothing); canvas->fitSheet();
    auto *document = workspace ? canvas->document() : &local;
    QApplication::processEvents();
    // Keep each frame synchronous so queued input cannot hide painting latency.
    QImage image(window.size()*window.devicePixelRatioF(),QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(window.devicePixelRatioF());
    window.render(&image);
    // Model and workspace must consume the same floating-point positions.
    // Even tiny view-transform rounding can land a sampled edge
    // on opposite sides of a pixel boundary; do not round production input.
    QVector<QPointF> views, artwork;
    for (int i = 0; i <= eventCount; ++i) {
        const auto view = canvas->viewTransform().map(point(i * (240. / eventCount)) + sheetGeometry(canvas->project(), canvas->shot()).output.topLeft());
        views.append(view); artwork.append(canvas->artworkPoint(view));
    }
    QJsonObject metadata{{"platform", QApplication::platformName()}, {"qt_version", qVersion()},
        {"device_pixel_ratio", window.devicePixelRatioF()}, {"canvas_device_pixel_ratio", canvas->devicePixelRatioF()},
        {"window_logical_size", QJsonArray{window.width(), window.height()}},
        {"canvas_logical_size", QJsonArray{canvas->width(), canvas->height()}},
        {"render_pixel_size", QJsonArray{image.width(), image.height()}},
        {"artwork_size", QJsonArray{project.width, project.height}}, {"brush_size", brush.size},
        {"brush_spacing", brush.spacing}, {"brush_hardness", brush.hardness},
        {"size_jitter", brush.sizeDynamics.jitter}, {"angle_jitter", brush.angleDynamics.jitter},
        {"tip_count", brush.tipFrames.size()}, {"tip_order", int(brush.tipOrder)}, {"workspace_initialization_ms", window.property("benchmark_startup_ms").toDouble()},
        {"constant_pressure", 1}, {"random_seed", 12345}, {"stabilization", smoothing}};
    if (workspace && measureHover) {
        QVector<double> sizeChanges, sizePaints, hoverInputs, hoverPaints;
        // Exercise the same size-change signal path as Brush Preset. A separate
        // hover pass distinguishes inspector/outline cost from raster stamping.
        send(*canvas, QEvent::MouseMove, views[0], false);
        QApplication::processEvents(); window.render(&image);
        const auto initialBuilds = canvas->brushOutlineBuildCount();
        for (int i = 0; i < 12; ++i) {
            QElapsedTimer timer; timer.start();
            canvas->setBrushSize(i % 2 == 0 ? brush.size / 2 : brush.size);
            sizeChanges.append(timer.nsecsElapsed()/1000000.);
            timer.restart(); QApplication::processEvents(); window.render(&image);
            sizePaints.append(timer.nsecsElapsed()/1000000.);
        }
        const auto hoverBuilds = canvas->brushOutlineBuildCount();
        metadata.insert("size_change_outline_builds", qint64(hoverBuilds - initialBuilds));
        for (int i = 0; i <= 24; ++i) {
            const auto view = views[i * eventCount / 24];
            QElapsedTimer timer; timer.start();
            send(*canvas, QEvent::MouseMove, view, false);
            hoverInputs.append(timer.nsecsElapsed()/1000000.);
            timer.restart(); QApplication::processEvents(); window.render(&image);
            hoverPaints.append(timer.nsecsElapsed()/1000000.);
        }
        timing(metadata, "size_change", sizeChanges); timing(metadata, "size_change_paint", sizePaints);
        timing(metadata, "hover_input", hoverInputs); timing(metadata, "hover_paint", hoverPaints);
        metadata.insert("hover_outline_builds", qint64(canvas->brushOutlineBuildCount() - hoverBuilds));
    }
    const auto strokeOutlineBuilds = canvas->brushOutlineBuildCount();
    document->setBrushRandomSeed(12345);
    if (workspace) send(*canvas,QEvent::MouseButtonPress,views[0]);
    else if (!document->beginStroke(artwork[0],1,brush,false)) throw std::runtime_error("Cannot begin stroke");
    QVector<double> events, paints, cacheTimes, cursorTimes, canvasPaintTimes, pumpTimes, captureTimes, livePaintTimes;
    const auto rasterBuilds=canvas->brushCursorRasterBuildCount();
    for (int i=1;i<=eventCount;++i) {
        QElapsedTimer timer; timer.start();
        if(workspace) send(*canvas,QEvent::MouseMove,views[i]);
        else document->continueStroke(artwork[i],1);
        events.append(timer.nsecsElapsed()/1000000.);
        timer.restart();
        if(workspace) {
            for(const char *name:{"profilePaintNs","profileCacheNs","profileCursorNs"})canvas->setProperty(name,qulonglong(0));
            QElapsedTimer stage;stage.start();QApplication::processEvents();
            pumpTimes.append(stage.nsecsElapsed()/1000000.);
            livePaintTimes.append(canvas->property("profilePaintNs").toULongLong()/1000000.);
            stage.restart();window.render(&image);captureTimes.append(stage.nsecsElapsed()/1000000.);
        }
        paints.append(timer.nsecsElapsed()/1000000.);
        if(workspace&&qEnvironmentVariableIsSet("TOON_PROFILE_CANVAS")) {
            cacheTimes.append(canvas->property("profileCacheNs").toULongLong()/1000000.);
            cursorTimes.append(canvas->property("profileCursorNs").toULongLong()/1000000.);
            canvasPaintTimes.append(canvas->property("profilePaintNs").toULongLong()/1000000.);
        }
    }
    if (workspace) {
        timing(metadata,"event_pump",pumpTimes);timing(metadata,"forced_capture",captureTimes);timing(metadata,"live_canvas",livePaintTimes);
        timing(metadata,"cache",cacheTimes);timing(metadata,"cursor",cursorTimes);timing(metadata,"canvas_paint",canvasPaintTimes);
        metadata.insert("cursor_raster_builds",qint64(canvas->brushCursorRasterBuildCount()-rasterBuilds));
        // Introspection stays outside input and paint timings; capture the final
        // live dab, before releasing restores the idle cursor shape.
        metadata.insert("stroke_outline_builds", qint64(canvas->brushOutlineBuildCount() - strokeOutlineBuilds));
        metadata.insert("cursor_path_elements", canvas->brushCursorPath().elementCount());
    }
    QElapsedTimer timer; timer.start();
    if(workspace) send(*canvas,QEvent::MouseButtonRelease,views[eventCount]);
    else document->endStroke();
    const double release=timer.nsecsElapsed()/1000000.;
    if(document->strokeActive() || document->project().shots[0].layers[0].tiles.isEmpty())
        throw std::runtime_error("Benchmark stroke did not finish or paint");
    report(name,workspace?"workspace":"model",events,paints,release,*document,metadata);
    document->history()->setClean();
    return hash(*document);
}
}
int main(int argc,char **argv) {
    QApplication app(argc,argv); QTemporaryDir temporary;
    QApplication::setOrganizationName("ToonProBenchmark"); QApplication::setApplicationName("LiveBrush");
    try {
        if (!temporary.isValid()) throw std::runtime_error("Cannot create isolated settings");
        const auto args=app.arguments();
        bool catalog = false, large = false, advanced = false, connected = false;
        int eventCount = 240;
        QString brushDirectory;
        for (int i = 1; i < args.size(); ++i) {
            const auto &arg = args[i];
            if (arg == "--catalog") catalog = true;
            else if (arg == "--large") large = true;
            else if (arg == "--connected") connected = true;
            else if (arg == "--advanced") advanced = true;
            else if (arg == "--events" || arg.startsWith("--events=")) {
                const auto value = arg == "--events" ? (++i < args.size() ? args[i] : QString()) : arg.mid(9);
                bool ok = false; eventCount = value.toInt(&ok);
                if (!ok || eventCount < 8 || eventCount > 2400)
                    throw std::runtime_error("--events requires an integer between 8 and 2400");
            } else if (arg.startsWith("--") || !brushDirectory.isEmpty()) {
                throw std::runtime_error("Usage: benchmark [brush-directory] [--catalog | --large | --advanced | --connected] [--events N]");
            } else brushDirectory = arg;
        }
        if (int(catalog) + int(large) + int(advanced) + int(connected) > 1) throw std::runtime_error("Choose one preset workload");
        if(!brushDirectory.isEmpty() && !catalog && !large && !advanced && !connected) for(const auto &name:{"brushes.json","tools.json"})
            if(!QFile::copy(QDir(brushDirectory).filePath(name),temporary.filePath(name))) throw std::runtime_error("Cannot copy benchmark brush configuration");
        applyTheme(); QElapsedTimer startup; startup.start(); MainWindow window(temporary.filePath("workspace.ini"));
        const double startupMs = startup.nsecsElapsed()/1000000.;
        window.setProperty("benchmark_startup_ms",startupMs);
        window.resize(1600,1000); window.show();
        Project project=Project::create();project.width=1920;project.height=1080;
        QString error; const auto path=temporary.filePath("benchmark.toonproj");
        if(!saveProject(project,path,&error)||!window.openProject(path)) throw std::runtime_error(error.toStdString());
        window.showNormal();window.resize(1600,1000);QApplication::processEvents();
        if (connected) {
            const auto presets=catalogBrushPresets();
            for(const auto &spec:QVector<QPair<QString,int>>{{"chain-border",128},{"chain-border",512},{"chain-border",1024},{"fence-posts",512},{"smoke-trail",512}}) {
                const auto found=std::find_if(presets.cbegin(),presets.cend(),[&](const BrushPreset &p){return p.id=="builtin/catalog/"+spec.first;});
                if(found==presets.cend()) throw std::runtime_error("Missing connected brush");
                auto brush=found->brush;brush.size=spec.second;
                const auto name=spec.first+"_"+QString::number(spec.second);
                const auto model=run(window,project,name,brush,false,0,eventCount);
                const auto workspace=run(window,project,name,brush,true,0,eventCount);
                if(model!=workspace)throw std::runtime_error("Connected model/workspace artwork differs");
            }
            return 0;
        }
        if (advanced) {
            QVector<QPair<QString,BrushSettings>> profiles;
            const auto presets=catalogBrushPresets();
            for(const QString &id:QStringList{"meadow-tuft-set","broadleaf-crown-set","cumulus-mass-set","distant-bird-flock","painted-tree","painted-cloud"}){
                const auto found=std::find_if(presets.cbegin(),presets.cend(),[&](const BrushPreset &preset){return preset.id=="builtin/catalog/"+id;});
                if(found==presets.cend())throw std::runtime_error("Missing advanced benchmark brush");
                auto brush=found->brush;if(id=="painted-cloud"){brush.size=1024;brush.spacing=1.05;}
                profiles.append({id,brush});
            }
            for(const auto &[name,brush]:profiles){const auto model=run(window,project,name,brush,false,0,eventCount);const auto workspace=run(window,project,name,brush,true,0,eventCount,true);if(model!=workspace)throw std::runtime_error("Advanced model/workspace artwork differs: "+name.toStdString());}
            return 0;
        }
        if (large) {
            QVector<QPair<QString, BrushSettings>> profiles;
            BrushSettings hard; hard.size = 128; hard.pressureSize = false;
            profiles.append({"large_round_hard_128px", hard});
            hard.size = 256; profiles.append({"large_round_hard_256px", hard});
            auto soft = hard; soft.hardness = .15; soft.opacity = .18;
            profiles.append({"large_round_soft_256px", soft});
            auto sampled = hard; sampled.spacing = .06; sampled.opacity = .26;
            sampled.tipMask = QImage(64, 64, QImage::Format_Grayscale8);
            for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x)
                sampled.tipMask.scanLine(y)[x] = uchar((x*31 + y*17 + x*y*3) % 256);
            sampled.texture.enabled = true; sampled.texture.pattern = sampled.tipMask;
            sampled.texture.scale = 2; sampled.texture.eachTip = false;
            profiles.append({"large_sampled_texture_static_256px", sampled});
            sampled.sizeDynamics.jitter = .5; sampled.sizeDynamics.minimum = .35;
            sampled.angleDynamics.jitter = .45;
            profiles.append({"large_sampled_texture_dynamic_256px", sampled});
            const auto presets = catalogBrushPresets();
            for (const auto &name : {"Brick Surface", "Cloud Edge Soft", "Granular Spray"}) {
                const auto found = std::find_if(presets.cbegin(), presets.cend(),
                    [name](const BrushPreset &preset) { return preset.name == name; });
                if (found == presets.cend()) throw std::runtime_error("Missing large benchmark catalog brush");
                auto brush = found->brush; brush.size = 256;
                profiles.append({QString("large_%1_256px").arg(found->name), brush});
            }
            for (const auto &[name, brush] : profiles) {
                const auto model = run(window, project, name, brush, false, 0, eventCount);
                const auto workspace = run(window, project, name, brush, true, 0, eventCount, true);
                if (model != workspace) throw std::runtime_error("Large model/workspace artwork differs: " + name.toStdString());
            }
            return 0;
        }
        if (catalog) {
            const QStringList names{"Clean Contour", "Granular Spray", "Short Grass Tuft",
                "Cloud Edge Soft", "Brick Surface", "Calm Ripples", "Painted Leaf Cluster"};
            for (const auto &name : names) {
                for (const auto &preset : catalogBrushPresets()) {
                    if (preset.name != name) continue;
                    // No smoothing here: model/workspace pixels must agree for
                    // identical samples. The normal preset retains its filter.
                    const auto model = run(window, project, preset.name, preset.brush, false, 0, eventCount);
                    const auto workspace = run(window, project, preset.name, preset.brush, true, 0, eventCount);
                    if (model != workspace) throw std::runtime_error("Catalog model/workspace artwork differs: " + preset.name.toStdString());
                }
            }
            return 0;
        }
        BrushSettings particle;
        particle.size=6;particle.spacing=.13901060675182483;particle.opacity=.26;
        particle.scatter=7.495069781553399;particle.scatterBothAxes=true;
        particle.texture.enabled=true;particle.texture.scale=10;particle.texture.invert=true;particle.texture.contrast=-.18;
        particle.texture.pattern=QImage(64,64,QImage::Format_Grayscale8);
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)particle.texture.pattern.scanLine(y)[x]=uchar((x*31+y*17+x*y*3)%256);
        if(!brushDirectory.isEmpty()) if(const auto *tool=window.toolCatalog()->tool("brush/builtin/sketch")) particle=tool->state.brush;
        BrushSettings sampled=particle;sampled.size=128;sampled.scatter=.4;sampled.spacing=.10;
        sampled.tipMask=particle.texture.pattern;sampled.angleDynamics.jitter=.4;
        if(!brushDirectory.isEmpty()) for(const auto &tool:window.toolCatalog()->tools()) if(!tool.state.brush.tipMask.isNull()) {sampled.tipMask=tool.state.brush.tipMask;break;}
        for(bool workspace:{false,true}) {
            run(window,project,"particle_6px",particle,workspace,0,eventCount);
            run(window,project,"sampled_texture_128px",sampled,workspace,0,eventCount);
        }
        run(window,project,"particle_6px_stabilized_10",particle,true,10,eventCount);
    } catch(const std::exception &error) { std::cerr<<error.what()<<std::endl;return 1; }
}
