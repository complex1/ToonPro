#pragma once
#include "document.h"

namespace toon {
class ClipboardTools {
public:
    // Internal MIME metadata preserves placement for Toon Pro copies. The image
    // MIME payload also works with other desktop drawing applications.
    static bool copy(Document *document, bool cut = false, QString *error = nullptr);
    static bool paste(Document *document, bool newLayer = true,
                      std::optional<QPointF> topLeft = {}, QString *error = nullptr);
    static bool duplicate(Document *document, QString *error = nullptr);
    static bool importImage(Document *document, const QImage &image, const QString &name,
                            QPointF topLeft, QString *error = nullptr);
};
} // namespace toon
