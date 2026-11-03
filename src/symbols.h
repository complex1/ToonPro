#pragma once
#include "project.h"
#include <QStringList>

namespace toon {
class Document;
const SymbolDefinition *findSymbol(const Project &project, const QString &id);
int symbolUseCount(const Project &project, const QString &id);
// Returns -1 outside the instance's clip window, or for invalid timing.
int symbolLocalFrame(const SymbolInstance &instance, int parentFrame, int durationFrames);
// Bounds in the definition's coordinates, including nested instances.
QRectF symbolArtworkBounds(const Project &project, const QString &id, int frame = 0);
// Materializes only the render copies; canonical instance layers keep no tiles.
bool resolveSymbolLayers(const Project &project, const Shot &shot, int frame,
                         QVector<RasterLayer> &layers, QRectF bounds = {}, QString *error = nullptr);
void clearSymbolRenderCache();
QString symbolRenderError();

QString createEmptySymbol(Document *document, const QString &name, QString *error = nullptr);
// Returns the new definition ID and selects its replacement instance.
QString convertLayersToSymbol(Document *document, const QStringList &layerIds,
                              const QString &name, QString *error = nullptr);
// Returns the new instance layer ID.
QString insertSymbolInstance(Document *document, const QString &symbolId, QString *error = nullptr);
bool renameSymbol(Document *document, const QString &id, const QString &name, QString *error = nullptr);
QString duplicateSymbol(Document *document, const QString &id, QString *error = nullptr);
bool removeSymbol(Document *document, const QString &id, QString *error = nullptr);
QString makeSymbolInstanceUnique(Document *document, const QString &layerId, QString *error = nullptr);
bool setSymbolInstance(Document *document, const QString &layerId, const SymbolInstance &instance,
                       QString *error = nullptr);
// Detaches at the current frame, retaining editable layers, groups and poses.
bool breakApartSymbolInstance(Document *document, const QString &layerId, QString *error = nullptr);
// An isolated regular drawing/timeline workspace. Dependency definitions are retained.
Project editProjectForSymbol(const Project &project, const QString &id, QString *error = nullptr);
// expectedOriginal guards against replacing a definition changed since opening.
bool updateSymbolContent(Document *document, const QString &id, const Shot &content,
                         const Shot &expectedOriginal, QString *error = nullptr);
} // namespace toon
