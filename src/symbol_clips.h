#pragma once
#include "symbol_types.h"
#include <QPair>
#include <QVector>

namespace toon {
class Document;
struct Shot;

enum class SymbolClipOperation { Move, TrimStart, TrimEnd, Slip };

// Move and Slip take deltas. TrimStart/TrimEnd take absolute, zero-based
// positions; the end is exclusive. This never modifies the input on failure.
bool previewSymbolClipEdit(const SymbolInstance &instance, int parentDuration,
                           SymbolClipOperation operation, int value,
                           SymbolInstance *out, QString *error = nullptr);
int symbolClipEnd(const SymbolInstance &instance, int parentDuration);
QVector<QPair<int, int>> symbolClipIntervals(const Shot &shot, const QString &layerId);
QString symbolClipEditError(const Shot &shot, const QString &layerId);
bool editSymbolClip(Document *document, const QString &layerId,
                    SymbolClipOperation operation, int value, QString *error = nullptr);
bool setSymbolClipTiming(Document *document, const QString &layerId,
                         int start, int duration, int offset, QString *error = nullptr);
} // namespace toon
