#include "drawing_input.h"
#include <QCoreApplication>

namespace toon {
#ifdef Q_OS_MACOS
void configureMacDrawingInput();
#endif
void configureDrawingInput() {
    QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, false);
    QCoreApplication::setAttribute(Qt::AA_CompressTabletEvents, false);
#ifdef Q_OS_MACOS
    // Qt's compression attributes do not control AppKit's native event queue.
    configureMacDrawingInput();
#endif
}
}
