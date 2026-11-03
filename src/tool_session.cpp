#include "tool_session.h"
#include "drawing_tool_controller.h"
#include "brush_settings_panel.h"
#include "drawing_tool_panel.h"
#include "value_slider.h"
#include <QApplication>
#include <QJsonArray>
#include <QScopedValueRollback>
#include <QScopeGuard>
#include <cmath>
#include <algorithm>

namespace toon {
namespace {
qreal number(const QJsonObject &o, const char *key, qreal fallback, qreal low, qreal high) {
    const qreal value = o.value(QLatin1String(key)).toDouble(fallback);
    return std::isfinite(value) ? qBound(low, value, high) : fallback;
}
QColor color(const QJsonObject &o, const char *key, QColor fallback) {
    const QColor value(o.value(QLatin1String(key)).toString());
    return value.isValid() ? value : fallback;
}
bool sceneTool(Canvas::Tool tool) {
    return tool == Canvas::Tool::SceneObject || tool == Canvas::Tool::SceneCamera || tool == Canvas::Tool::SceneRig;
}
bool defaultStampPlacement(const ToolDefinition &tool) {
    return tool.engine == Canvas::Tool::Brush &&
        (tool.brushType == BrushType::Stamp || tool.brushType == BrushType::ColoredStamp);
}
}
ToolSession::ToolSession(Canvas *canvas, ToolCatalog *catalog, QObject *parent)
    : QObject(parent), m_canvas(canvas), m_catalog(catalog) {
    canvas->setProperty("toolCatalogManaged", true);
    // Group selections survive restarts. Reuse those remembered tools for
    // shortcuts until this session has a more recent choice for that engine.
    for (const auto &entry : catalog->entries())
        if (const auto *tool = catalog->tool(entry.lastToolId))
            if (!m_lastEngineTool.contains(int(tool->engine)) || entry.id == catalog->selectedEntryId())
                m_lastEngineTool[int(tool->engine)] = tool->id;
    const QPointer<ToolSession> self(this);
    canvas->setToolRouter([self](Canvas::Tool engine) {
        if (!self || self->m_applying) return false;
        self->activateEngine(engine);
        return true;
    });
    catalog->setActivationGuard([self] {
        return !self || !self->m_canvas || self->m_applying || self->m_canvas->resolvePendingTransform();
    });
    catalog->setAutoSaveGuard([self] {
        return !self || !self->m_canvas || !self->m_canvas->document() ||
               !self->m_canvas->document()->editingGestureActive();
    });
    connect(catalog, &ToolCatalog::aboutToActivate, this, [this] {
        if (m_applying || !m_canvas) return;
        // This signal follows the activation guard, but precedes source-state
        // capture and the target's model echo. Resolve a tool-property draft
        // here so a successful switch cannot lose it or apply it to the next
        // tool. Rejected switches retain their draft and focus untouched.
        ValueSlider *value = nullptr;
        bool toolProperties = false;
        for (auto *widget = QApplication::focusWidget(); widget; widget = widget->parentWidget()) {
            if (widget == m_canvas->window()) {
                if (toolProperties && value && value->editingActive()) value->commitEditing();
                break;
            }
            if (!value) value = qobject_cast<ValueSlider *>(widget);
            toolProperties |= qobject_cast<BrushSettingsPanel *>(widget) || qobject_cast<DrawingToolPanel *>(widget);
        }
        captureCurrent();
    });
    connect(catalog, &ToolCatalog::activationRequested, this, &ToolSession::apply);
    connect(catalog, &ToolCatalog::changed, this, &ToolSession::synchronizeStampPlacement);
    connect(canvas, &Canvas::brushChanged, this, &ToolSession::captureCurrent);
    connect(canvas, &Canvas::selectionCombineChanged, this, &ToolSession::captureCurrent);
    connect(canvas, &Canvas::toolSettingsEdited, this, &ToolSession::captureCurrent);
    connect(canvas, &Canvas::stampPlacementEnabledChanged, this, [this] {
        if (m_applying || !m_catalog || !m_canvas || m_current.isEmpty()) return;
        const auto *tool = m_catalog->tool(m_current);
        if (!tool || tool->engine != Canvas::Tool::Brush) return;
        m_stampPlacementExplicit = true;
        captureCurrent();
    });
    apply(catalog->activeToolId());
}
ToolSession::~ToolSession() {
    flush();
    if (m_catalog) {
        m_catalog->setActivationGuard({});
        m_catalog->setAutoSaveGuard({});
    }
    if (m_canvas) {
        m_canvas->setToolRouter({});
        m_canvas->setProperty("toolCatalogManaged", false);
    }
}
ToolState ToolSession::capture() const {
    ToolState result;
    if (!m_canvas) return result;
    result.brush = m_canvas->brush;
    // Foreground/background are shared color choices, never tool identity.
    result.brush.color = BrushSettings{}.color;
    result.brush.backgroundColor = BrushSettings{}.backgroundColor;
    result.smoothing = m_canvas->stabilization();
    const auto *c = m_canvas->drawingTools();
    QJsonArray stops;
    for (const auto &stop : c->gradientStops)
        stops.append(QJsonArray{stop.first, stop.second.name(QColor::HexArgb)});
    const auto &text = c->textStyle;
    result.options = {
        {"sampleSource", int(c->fillSettings.source)}, {"referenceLayer", c->fillSettings.referenceLayerId},
        {"tolerance", c->fillSettings.tolerance}, {"gapClosing", c->fillSettings.gapClosing},
        {"gradientStops", stops}, {"shapeStroke", c->shapeStroke}, {"shapeFill", c->shapeFill},
        {"closedPath", c->closedPath}, {"fillColor", c->fillColor.name(QColor::HexArgb)},
        {"strokeWidth", c->strokeWidth}, {"retouchSize", c->retouchSettings.size},
        {"retouchStrength", c->retouchSettings.strength}, {"selectionCombine", int(m_canvas->selectionCombine())},
        {"selectionBoundary", c->selectionBoundary}, {"warpMode", c->warpMode},
        {"pathEditMode", int(c->pathEditMode)}, {"simplifyTolerance", c->simplifyTolerance},
        {"widthBrushRadius", c->widthBrushRadius}, {"widthBrushStrength", c->widthBrushStrength},
        {"widthThicken", c->widthThicken}, {"eraseSegments", c->eraseSegments},
        {"text", text.text}, {"fontFamily", text.fontFamily}, {"fontSize", text.fontSize},
        {"bold", text.bold}, {"italic", text.italic}, {"alignment", int(text.alignment)},
        {"lineSpacing", text.lineSpacing}, {"textWidth", text.textWidth}
    };
    // Ordinary property edits keep metadata defaults dynamic. Only a deliberate
    // placement-mode choice becomes a per-tool override.
    if (m_stampPlacementExplicit && m_catalog)
        if (const auto *tool = m_catalog->tool(m_current); tool && tool->engine == Canvas::Tool::Brush)
            result.options.insert("stampPlacement", m_canvas->stampPlacementEnabled());
    return result;
}
void ToolSession::synchronizeStampPlacement() {
    if (m_applying || !m_canvas || !m_catalog || m_catalog->activeToolId() != m_current) return;
    const auto *tool = m_catalog->tool(m_current);
    if (!tool) return;
    const auto option = tool->state.options.value("stampPlacement");
    const bool enabled = tool->engine == Canvas::Tool::Brush && option.toBool(defaultStampPlacement(*tool));
    m_stampPlacementExplicit = option.isBool();
    const QScopedValueRollback<bool> guard(m_applying, true);
    m_canvas->setStampPlacementEnabled(enabled);
}
void ToolSession::captureCurrent() {
    if (m_applying || !m_canvas || !m_catalog || m_current.isEmpty()) return;
    // Scene properties belong to the document, not a raster brush preset.
    if (const auto *tool = m_catalog->tool(m_current); tool && sceneTool(tool->engine)) return;
    m_catalog->updateToolState(m_current, capture());
}
void ToolSession::flush() {
    captureCurrent();
    if (m_catalog && m_catalog->writable()) {
        QString error;
        if (!m_catalog->save(&error)) emit m_catalog->warning(error);
    }
}
bool ToolSession::activateEngine(Canvas::Tool engine) {
    if (!m_canvas || !m_catalog) return false;
    if (const auto *current = m_catalog->tool(m_current); current && current->engine == engine) {
        // Re-entering Transform or Guides may intentionally start a new gesture;
        // preserve settings and still run Canvas's ordinary gesture lifecycle.
        if (!m_catalog->activateTool(m_current)) return false;
        QScopedValueRollback<bool> guard(m_applying, true);
        m_canvas->setTool(engine);
        return true;
    }
    auto preferred = m_lastEngineTool.value(int(engine));
    if (const auto *tool = m_catalog->tool(preferred); tool && tool->engine == engine)
        return m_catalog->activateTool(preferred);
    for (const auto &tool : m_catalog->tools())
        if (tool.engine == engine) return m_catalog->activateTool(tool.id);
    return false;
}
void ToolSession::apply(const QString &id) {
    if (m_applying || !m_canvas || !m_catalog) return;
    QScopedValueRollback<bool> guard(m_applying, true);
    m_canvas->setProperty("restoringToolState", true);
    const auto clearRestoring = qScopeGuard([canvas=m_canvas] { if (canvas) canvas->setProperty("restoringToolState", false); });
    const auto *definition = m_catalog->tool(id);
    if (!definition) {
        m_current.clear();
        m_stampPlacementExplicit = false;
        m_canvas->setStampPlacementEnabled(false);
        m_canvas->setToolAvailable(false);
        m_canvas->notifyToolStateApplied();
        return;
    }
    // Copy before signals: setting controls may notify the catalog and detach its
    // storage. Imported image resources remain value-owned through QImage COW.
    const ToolDefinition tool = *definition;
    m_canvas->finishStroke();
    m_canvas->drawingTools()->cancel();
    m_current = id; m_lastEngineTool[int(tool.engine)] = id;
    m_stampPlacementExplicit = tool.state.options.value("stampPlacement").isBool();
    m_canvas->setStampPlacementEnabled(tool.engine == Canvas::Tool::Brush &&
        tool.state.options.value("stampPlacement").toBool(defaultStampPlacement(tool)));
    if (sceneTool(tool.engine)) {
        m_canvas->setToolAvailable(true);
        m_canvas->setTool(tool.engine);
        m_canvas->notifyToolStateApplied();
        return;
    }
    auto brush = tool.state.brush;
    brush.color = m_canvas->brush.color;
    brush.backgroundColor = m_canvas->brush.backgroundColor;
    // Exact state restoration must not inherit another tool's Protect Texture.
    m_canvas->setBrushSettings(brush);
    m_canvas->setStabilization(tool.state.smoothing);
    const auto &o = tool.state.options;
    auto *c = m_canvas->drawingTools();
    c->fillSettings.source = static_cast<RasterSampleSource>(int(number(o, "sampleSource", 1, 0, 2)));
    c->fillSettings.referenceLayerId = o.value("referenceLayer").toString();
    c->fillSettings.tolerance = int(number(o, "tolerance", 24, 0, 255));
    c->fillSettings.gapClosing = int(number(o, "gapClosing", 0, 0, 20));
    c->gradientStops.clear();
    for (const auto &value : o.value("gradientStops").toArray()) {
        const auto stop = value.toArray();
        if (stop.size() != 2 || !stop[0].isDouble()) continue;
        const QColor stopColor(stop[1].toString());
        if (stopColor.isValid()) c->gradientStops.append({qBound(0., stop[0].toDouble(), 1.), stopColor});
        if (c->gradientStops.size() == 32) break;
    }
    if (c->gradientStops.size() < 2) c->gradientStops = {{0, Qt::black}, {1, Qt::white}};
    std::sort(c->gradientStops.begin(), c->gradientStops.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    c->radialGradient = tool.engine == Canvas::Tool::Gradient && tool.variant == 1;
    c->shapeStroke = o.value("shapeStroke").toBool(true);
    c->shapeFill = o.value("shapeFill").toBool(false);
    c->closedPath = o.value("closedPath").toBool(false);
    c->fillColor = color(o, "fillColor", Qt::white);
    c->strokeWidth = number(o, "strokeWidth", 3, .1, 1024);
    c->retouchSettings.size = number(o, "retouchSize", 48, 1, 512);
    c->retouchSettings.strength = number(o, "retouchStrength", .5, .01, 1);
    c->retouchSettings.mode = static_cast<RetouchMode>(qBound(0, tool.variant, 5));
    c->pathEditMode = static_cast<DrawingToolController::PathEditMode>(int(number(o, "pathEditMode", 0, 0, 2)));
    c->simplifyTolerance = number(o, "simplifyTolerance", 2, .1, 30);
    c->widthBrushRadius = number(o, "widthBrushRadius", 30, 1, 256);
    c->widthBrushStrength = number(o, "widthBrushStrength", .2, .01, 1);
    c->widthThicken = o.value("widthThicken").toBool(true);
    c->eraseSegments = o.value("eraseSegments").toBool(false);
    c->selectionBoundary = o.value("selectionBoundary").toBool(false);
    c->warpMode = tool.engine == Canvas::Tool::AdvancedTransform && tool.variant == 1;
    c->textStyle = EditableObject::create(ObjectKind::Text);
    c->textStyle.text = o.value("text").toString("Text");
    c->textStyle.fontFamily = o.value("fontFamily").toString("Sans Serif");
    c->textStyle.fontSize = number(o, "fontSize", 32, 1, 1024);
    c->textStyle.bold = o.value("bold").toBool();
    c->textStyle.italic = o.value("italic").toBool();
    const int alignment = o.value("alignment").toInt(Qt::AlignLeft);
    c->textStyle.alignment = alignment == Qt::AlignRight ? Qt::AlignRight : alignment == Qt::AlignHCenter ? Qt::AlignHCenter : Qt::AlignLeft;
    c->textStyle.lineSpacing = number(o, "lineSpacing", 1.2, .5, 4);
    c->textStyle.textWidth = number(o, "textWidth", 0, 0, 10000);
    c->textStyle.fill = m_canvas->brush.color;
    c->textStyle.stroke = QColor();
    m_canvas->setSelectionCombine(static_cast<SelectionCombine>(int(number(o, "selectionCombine", 0, 0, 3))));
    m_canvas->setToolAvailable(true);
    m_canvas->setTool(tool.engine);
    if (tool.engine == Canvas::Tool::Guides)
        m_canvas->setGuideCreationType(static_cast<GuideType>(qBound(0, tool.variant, 5)));
    m_canvas->notifyToolStateApplied();
    c->notifySettings();
}
} // namespace toon
