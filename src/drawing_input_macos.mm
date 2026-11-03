#include "drawing_input.h"
#import <AppKit/NSEvent.h>

namespace toon {
void configureMacDrawingInput() {
    // AppKit coalesces mouse-dragged AND tablet events by default. Once merged,
    // their intermediate positions cannot be recovered by the stroke engine.
    // This is process-local and also preserves mouse-emulating tablet input.
    [NSEvent setMouseCoalescingEnabled:NO];
}
}
