// Opt-in, isolated offscreen benchmark; no application settings or project files.
#include "canvas.h"
#include "raster_tools.h"
#include <QApplication>
#include <QElapsedTimer>
#include <iostream>
using namespace toon;
template<class Work> void measure(const char *label, Work work) {
    std::cout << label << "..." << std::flush;
    QElapsedTimer timer; timer.start(); work();
    std::cout << " " << timer.nsecsElapsed() / 1000000.0 << " ms" << std::endl;
}
int main(int argc, char **argv) {
    QApplication app(argc, argv); auto project = Project::create();
    project.width = project.height = 512; project.shots[0].sheet.overscan = 0;
    quint32 seed = 0x12345678;
    for (int ty = 0; ty < 2; ++ty) for (int tx = 0; tx < 2; ++tx) {
        QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < tileSize; ++y) {
            auto row = reinterpret_cast<QRgb *>(tile.scanLine(y));
            for (int x = 0; x < tileSize; ++x) {
                seed = seed * 1664525 + 1013904223;
                row[x] = seed >> 30 ? qRgb(255, 0, 0) : qRgb(255, 255, 255);
            }
        }
        tile.setPixel(0, 0, qRgb(255, 0, 0));
        project.shots[0].layers[0].tiles.insert({tx, ty}, tile);
    }
    Canvas canvas; canvas.resize(1000, 800); canvas.document()->load(project);
    canvas.show(); app.processEvents(); app.processEvents();
    auto *document = canvas.document(); RasterFillSettings settings;
    settings.source = RasterSampleSource::ActiveLayer; settings.tolerance = 0;
    measure("Color Range + overlay", [&] { if (!RasterTools::selectColor(document, {.5, .5}, settings, false)) std::exit(2); app.processEvents(); });
    std::cout << "elements " << document->rasterSelection().path.elementCount() << std::endl;
    measure("Selection tile masks", [&] { RasterSelectionCache cache(&document->rasterSelection().path); for (int y=0;y<2;++y) for(int x=0;x<2;++x) (void)cache.tile({x,y}); });
    measure("Invert + overlay", [&] { document->invertRasterSelection(); app.processEvents(); });
    measure("Deselect + draw", [&] { document->clearRasterSelection(); BrushSettings brush; brush.size=8; brush.pressureSize=false; document->beginStroke({20,20},1,brush,false); document->endStroke(); app.processEvents(); });
}
