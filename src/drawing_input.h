#pragma once

namespace toon {
// Call on the GUI thread after QApplication construction, before opening the
// workspace. Preserve input samples; QWidget may still combine paint requests.
void configureDrawingInput();
}
