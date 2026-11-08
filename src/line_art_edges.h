#pragma once
#include "line_art.h"

namespace toon {
// Worker-only luminance abstraction, coherent ridge extraction and raster ink.
QImage extractLineArtInk(const QImage &, const LineArtSettings &, double scale,
                         const LineArtCancellation &);
}
