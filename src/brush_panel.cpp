#include "brush_panel.h"
#include "abr_import.h"
#include "brush_library.h"
#include "brush_engine.h"
#include "canvas.h"
#include "icons.h"
#include "ui_theme.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace toon {
namespace {
QPixmap tipPreview(BrushSettings brush, QSize size, bool stroke) {
    if (stroke) {
        // Show the tool silhouette clearly against editor chrome, independently of the current ink color.
        brush.color = QColor(brush.colorDynamics.enabled ? ui::accent : ui::text);
        return QPixmap::fromImage(brushStrokePreview(brush, size, QColor(ui::base)));
    }
    QImage image(size, QImage::Format_ARGB32_Premultiplied); image.fill(QColor(ui::base));
    brush.size = 24;
    const auto mask = brushTipMask(brush, {});
    QImage tip(mask.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < mask.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(tip.scanLine(y));
        for (int x = 0; x < mask.width(); ++x) row[x] = qPremultiply(qRgba(242, 242, 242, mask.constScanLine(y)[x]));
    }
    QPainter p(&image); p.drawImage(QPointF((size.width() - tip.width()) / 2.0, (size.height() - tip.height()) / 2.0), tip); p.end();
    return QPixmap::fromImage(image);
}
bool matches(const BrushPreset &preset, const Canvas *canvas) {
    auto a = preset.brush, b = canvas->brush;
    a.color = b.color; a.backgroundColor = b.backgroundColor;
    return a == b && preset.smoothing == canvas->stabilization();
}
}
BrushPanel::BrushPanel(Canvas *canvas, BrushLibrary *library, QWidget *parent)
    : QWidget(parent), m_canvas(canvas), m_library(library) {
    setObjectName("brushContent"); setMinimumWidth(245);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(8, 8, 8, 8);
    auto *actions = new QHBoxLayout;
    auto button = [this, actions](const QString &text, const QString &icon, const char *id) {
        auto *b = new QToolButton(this); b->setObjectName(id); b->setIcon(appIcon(icon));
        b->setToolTip(text); b->setAccessibleName(text); b->setMinimumSize(30, 30); actions->addWidget(b); return b;
    };
    m_import = button("Import Photoshop brushes (.abr)", "folder-open", "importBrushes");
    m_import->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_save = button("Save current brush as a preset", "plus", "saveBrushPreset");
    actions->addStretch();
    m_rename = button("Rename preset", "settings-2", "renameBrushPreset");
    m_remove = button("Delete preset", "trash", "deleteBrushPreset");
    layout->addLayout(actions);
    m_list = new QListWidget(this); m_list->setObjectName("brushPresetList");
    m_list->setAccessibleName("Brush presets"); m_list->setIconSize(QSize(44, 36)); m_list->setSpacing(3);
    layout->addWidget(m_list, 1);
    m_preview = new QLabel(this); m_preview->setObjectName("brushPreview");
    m_preview->setMinimumSize(220, 64);
    m_preview->setAlignment(Qt::AlignCenter); m_preview->setToolTip("Tip and spacing preview"); layout->addWidget(m_preview);
    auto *caption = new QHBoxLayout;
    m_name = new QLabel(this); m_name->setObjectName("currentBrushPreset"); m_name->setWordWrap(true);
    m_name->setTextFormat(Qt::PlainText); caption->addWidget(m_name, 1);
    m_info = new QToolButton(this); m_info->setObjectName("brushCompatibility");
    m_info->setIcon(appIcon("sticky-note")); m_info->setAccessibleName("Brush source and compatibility");
    m_info->setToolTip("Brush source and compatibility"); caption->addWidget(m_info); layout->addLayout(caption);
    m_previewTimer.setSingleShot(true); m_previewTimer.setInterval(35);
    connect(&m_previewTimer, &QTimer::timeout, this, [this] {
        m_preview->setPixmap(tipPreview(m_canvas->brush, {220, 64}, true));
    });
    connect(m_list, &QListWidget::currentRowChanged, this, &BrushPanel::selectPreset);
    const auto reapplyPreset = [this] {
        const auto *preset = selectedPreset();
        if (preset && (!matches(*preset, m_canvas) || m_canvas->tool() != Canvas::Tool::Brush)) selectPreset();
    };
    connect(m_list, &QListWidget::itemClicked, this, reapplyPreset);
    connect(m_list, &QListWidget::itemActivated, this, reapplyPreset);
    connect(m_library, &BrushLibrary::changed, this, &BrushPanel::refresh);
    connect(m_canvas, &Canvas::brushChanged, this, &BrushPanel::updatePreview);
    connect(m_import, &QToolButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, "Import Photoshop brush tips", {}, "Photoshop brushes (*.abr)");
        if (path.isEmpty()) return;
        QString error;
        if (!importAbrFile(path, &error)) { showError(error); return; }
        QMessageBox report(this); report.setWindowTitle("Brush import"); report.setIcon(QMessageBox::Information);
        report.setTextFormat(Qt::PlainText); report.setText("Brushes imported. Supported settings were mapped; compatibility details list remaining differences.");
        report.setDetailedText(m_importReport); report.exec();
    });
    connect(m_save, &QToolButton::clicked, this, [this] {
        bool ok = false; const auto *selected = selectedPreset();
        const auto name = QInputDialog::getText(this, "Save brush preset", "Name", QLineEdit::Normal,
            selected ? selected->name + " copy" : "My brush", &ok);
        if (!ok) return;
        QString error; if (!saveCurrent(name, &error)) showError(error);
    });
    connect(m_rename, &QToolButton::clicked, this, [this] {
        const auto *selected = selectedPreset(); if (!selected || selected->builtIn) return;
        const auto id = selected->id; bool ok = false;
        const auto name = QInputDialog::getText(this, "Rename brush preset", "Name", QLineEdit::Normal, selected->name, &ok);
        if (!ok) return;
        QString error; if (!m_library->renamePreset(id, name, &error)) showError(error);
    });
    connect(m_remove, &QToolButton::clicked, this, [this] {
        const auto *selected = selectedPreset(); if (!selected || selected->builtIn) return;
        const auto id = selected->id;
        if (QMessageBox::question(this, "Delete brush preset?", "Delete this preset from your local brush library?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        QString error; if (!m_library->removePreset(id, &error)) showError(error);
    });
    connect(m_info, &QToolButton::clicked, this, [this] {
        const auto *selected = selectedPreset(); if (!selected) return;
        QMessageBox info(this); info.setWindowTitle(selected->name); info.setTextFormat(Qt::PlainText);
        info.setText(selected->source.isEmpty() ? "Native Toon Pro preset" : selected->source);
        if (!selected->compatibility.isEmpty()) info.setDetailedText(selected->compatibility.join('\n'));
        info.exec();
    });
    refresh();
}
const BrushPreset *BrushPanel::selectedPreset() const {
    for (const auto &preset : m_library->presets()) if (preset.id == m_activeId) return &preset;
    return nullptr;
}
void BrushPanel::showError(const QString &message) {
    QMessageBox box(this); box.setWindowTitle("Brush library"); box.setIcon(QMessageBox::Warning);
    box.setTextFormat(Qt::PlainText); box.setText(message); box.exec();
}
void BrushPanel::refresh() {
    const QSignalBlocker block(m_list); m_list->clear();
    int selected = -1;
    for (const auto &preset : m_library->presets()) {
        auto *item = new QListWidgetItem(QIcon(tipPreview(preset.brush, {44, 36}, false)), preset.name, m_list);
        item->setData(Qt::UserRole, preset.id);
        item->setToolTip(preset.name + (preset.builtIn ? " · Built in" : "")
            + (preset.brush.tipMask.isNull() ? " · Round tip" : " · Sampled tip"));
        if (preset.id == m_activeId) selected = m_list->count() - 1;
    }
    m_list->setCurrentRow(selected);
    if (selected < 0) m_activeId.clear();
    m_import->setEnabled(m_library->writable()); m_save->setEnabled(m_library->writable());
    updatePreview();
}
void BrushPanel::selectPreset() {
    const auto *item = m_list->currentItem();
    m_activeId = item ? item->data(Qt::UserRole).toString() : QString();
    if (const auto *preset = selectedPreset()) {
        m_canvas->applyBrushSettings(preset->brush, preset->smoothing);
        m_canvas->setTool(Canvas::Tool::Brush);
    }
    updatePreview();
}
void BrushPanel::updatePreview() {
    const auto *selected = selectedPreset();
    m_name->setText(selected ? selected->name + (matches(*selected, m_canvas) ? "" : " · Modified") : "Current brush");
    m_previewTimer.start();
    m_rename->setEnabled(selected && !selected->builtIn && m_library->writable());
    m_remove->setEnabled(selected && !selected->builtIn && m_library->writable());
    m_info->setEnabled(selected);
}
bool BrushPanel::saveCurrent(const QString &name, QString *error) {
    BrushPreset preset; preset.name = name.trimmed(); preset.brush = m_canvas->brush; preset.smoothing = m_canvas->stabilization();
    if (const auto *selected = selectedPreset()) { preset.source = selected->source; preset.compatibility = selected->compatibility; }
    if (!m_library->addPresets({preset}, error)) return false;
    m_list->setCurrentRow(m_list->count() - 1); return true;
}
bool BrushPanel::importAbrFile(const QString &path, QString *error) {
    QStringList ids;
    if (!importAbrIntoLibrary(m_library, path, &m_importReport, error, &ids)) return false;
    m_list->setCurrentRow(m_list->count() - ids.size());
    return true;
}
bool importAbrIntoLibrary(BrushLibrary *library, const QString &path, QString *importReport,
    QString *error, QStringList *importedIds) {
    if (importReport) importReport->clear();
    if (importedIds) importedIds->clear();
    const auto imported = importAbr(path);
    if (!imported.error.isEmpty()) {
        if (error) *error = imported.error + (imported.warnings.isEmpty() ? QString() : "\n" + imported.warnings.join('\n'));
        return false;
    }
    if (imported.brushes.isEmpty()) { if (error) *error = "No compatible brush tips or presets were found."; return false; }
    QVector<BrushPreset> presets;
    const auto fileName = QFileInfo(path).fileName();
    QStringList report{QString("%1 — ABR %2.%3").arg(fileName).arg(imported.version).arg(imported.subversion)};
    for (const auto &sample : imported.brushes) {
        BrushPreset preset; preset.name = sample.name.left(120);
        if (sample.settings) preset.brush = *sample.settings;
        else {
            preset.brush.tipMask = sample.mask;
            preset.brush.size = qBound(1, qMax(sample.mask.width(), sample.mask.height()), 256);
            preset.brush.spacing = .1; preset.brush.opacity = 1; preset.brush.pressureSize = true;
        }
        preset.source = QString("%1 · ABR %2.%3 · %4").arg(fileName).arg(imported.version).arg(imported.subversion).arg(sample.sourceId);
        preset.compatibility = sample.compatibility;
        if (!sample.settings) preset.compatibility += QStringList{
            "Sampled tip pixels imported.", "Preset settings are not mapped; using native defaults for size, flow, spacing, pressure and smoothing.",
            "Photoshop dynamics, scattering, textures, dual brushes, blending and tool presets are not transferred."};
        if (!sample.computed && sample.sourceSize != sample.mask.size())
            preset.compatibility.append(QString("Tip resized from %1 × %2 to %3 × %4 pixels; the original ABR is unchanged.")
                .arg(sample.sourceSize.width()).arg(sample.sourceSize.height())
                .arg(sample.mask.width()).arg(sample.mask.height()));
        presets.append(preset);
        report.append(sample.computed ? QString("%1: native round tip").arg(preset.name)
            : QString("%1: %2 × %3 sampled tip").arg(preset.name).arg(sample.mask.width()).arg(sample.mask.height()));
        for (const auto &detail : preset.compatibility) report.append("  " + detail);
    }
    report += imported.warnings;
    const auto first = library->presets().size();
    if (!library->addPresets(presets, error)) return false;
    if (importReport) *importReport = report.join('\n');
    if (importedIds) for (auto i = first; i < library->presets().size(); ++i)
        importedIds->append(library->presets()[i].id);
    return true;
}
} // namespace toon
