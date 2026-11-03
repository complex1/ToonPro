#pragma once

#include "canvas.h"
#include "tool_catalog.h"
#include <QHash>
#include <QPointer>

namespace toon {
// One workspace bridge owns the currently loaded settings; document pixels and
// local tool organization continue to use separate undo histories.
class ToolSession final : public QObject {
    Q_OBJECT
public:
    ToolSession(Canvas *canvas, ToolCatalog *catalog, QObject *parent = nullptr);
    ~ToolSession() override;
    bool activateEngine(Canvas::Tool engine);
    void flush();
private:
    ToolState capture() const;
    void captureCurrent();
    void synchronizeStampPlacement();
    void apply(const QString &id);
    QPointer<Canvas> m_canvas;
    QPointer<ToolCatalog> m_catalog;
    QString m_current;
    QHash<int, QString> m_lastEngineTool;
    bool m_applying = false;
    bool m_stampPlacementExplicit = false;
};
} // namespace toon
