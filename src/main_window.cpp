#include "effects_panel.h"
#include "cel_paint_check_panel.h"
#include "character_color_panel.h"
#include "character_recolor_dialog.h"
#include "cel_shading_panel.h"
#include "light_table.h"
#include "auto_actions.h"
#include "auto_actions_panel.h"
#include "history_panel.h"
#include "mask_tool.h"
#include "model_library.h"
#include "model_library_panel.h"
#include "scene3d_controller.h"
#include "main_window.h"
#include "symbol_library_panel.h"
#include "symbol_editor_dialog.h"
#include "symbols.h"
#include "project_save.h"
#include <QEventLoop>
#include "palette_library.h"
#include "color_panel.h"
#include "canvas.h"
#include "timeline_controller.h"
#include "movie_review_controller.h"
#include "timeline_panel.h"
#include "drawing_template_library.h"
#include "onion_settings.h"
#include <QJsonDocument>
#include "animation_render.h"
#include "video_export_dialog.h"
#include "audio_export_dialog.h"
#include "shots_panel.h"
#include "keyframe_render.h"
#include "keyframe_transform_tool.h"
#include "drawing_tool_controller.h"
#include "advanced_transform.h"
#include "clipboard_tools.h"
#include "layer_panel.h"
#include <QImageReader>
#include "drawing_panels.h"
#include "reference_panel.h"
#include "brush_panel.h"
#include "brush_preset_panel.h"
#include "brush_management_panel.h"
#include "brush_library.h"
#include "brush_settings_panel.h"
#include "tool_catalog.h"
#include "tool_session.h"
#include "tool_navigation_panel.h"
#include "tool_management_panel.h"
#include "icon_column.h"
#include "quick_access_toolbar.h"
#include "value_slider.h"
#include "property_row.h"
#include "icons.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include <QShortcut>
#include <QFile>
#include "project_dialog.h"
#include <QAction>
#include <QActionGroup>
#include <QAccessible>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QStyle>
#include <QTimer>
#include <QTextEdit>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QResizeEvent>
#include <QFrame>
#include <QScrollArea>
#include <algorithm>

namespace toon {
namespace {
constexpr const char *drawingDockIds[] = {"toolsPanel", "brushPresetPanel", "propertiesPanel"};
class DrawingDrawerTabs final : public QTabBar {
public:
    using QTabBar::QTabBar;
protected:
    QSize minimumTabSizeHint(int index) const override {
        auto size = QTabBar::minimumTabSizeHint(index);
        // Native styles reserve several letters even with eliding enabled.
        // Allow labels to elide further before any of these three pages clips;
        // preserve the icon target, full accessible name and font-aware height.
        size.setWidth(ui::metrics::actionTarget);
        return size;
    }
};
class DrawerPages final : public QStackedWidget {
public:
    using QStackedWidget::QStackedWidget;
    QSize minimumSizeHint() const override { return {0, 0}; }
    QSize sizeHint() const override { return {280, 480}; }
};
bool pendingValueEntry(QWidget *window) {
    bool editing = false;
    for (auto *widget = QApplication::focusWidget(); widget; widget = widget->parentWidget()) {
        if (widget == window) return editing;
        if (auto *value = qobject_cast<ValueSlider *>(widget)) editing |= value->editingActive();
    }
    return false;
}
// Keep long failure messages available to accessibility and delayed tooltips,
// while allowing the status bar to shrink alongside the drawing workspace.
class StatusLabel final : public QLabel {
public:
    using QLabel::QLabel;
    QSize minimumSizeHint() const override { return {0, fontMetrics().height() + 6}; }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(contentsRect(), Qt::AlignVCenter | Qt::AlignLeft,
                         fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()));
    }
};
// A bounded current-tool summary. Elide only the name: the exact size remains
// visible even at larger application fonts or with long custom brush names.
class DrawingControlsButton final : public QToolButton {
public:
    using QToolButton::QToolButton;
    void setSummary(const QString &name, const QString &value) {
        m_name = name; m_value = value; refreshText();
    }
    QSize sizeHint() const override { return {196, qMax(ui::metrics::actionTarget, fontMetrics().height() + 8)}; }
    QSize minimumSizeHint() const override { return sizeHint(); }
protected:
    void resizeEvent(QResizeEvent *event) override { QToolButton::resizeEvent(event); refreshText(); }
    void changeEvent(QEvent *event) override {
        QToolButton::changeEvent(event);
        if (event->type() == QEvent::FontChange) { refreshText(); updateGeometry(); }
    }
private:
    void refreshText() {
        const QString suffix = m_value.isEmpty() ? QString() : " · " + m_value;
        const int available = qMax(12, width() - iconSize().width() - 20 - fontMetrics().horizontalAdvance(suffix));
        setText(fontMetrics().elidedText(m_name, Qt::ElideRight, available) + suffix);
    }
    QString m_name, m_value;
};
}
MainWindow::MainWindow(const QString &settingsPath, QWidget *parent) : QMainWindow(parent) {
    setObjectName("mainWindow");
    setWindowTitle("Toon Pro");
    resize(1320, 860);
    setDockOptions(AllowNestedDocks | AllowTabbedDocks | AnimatedDocks);
    const auto path = settingsPath.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/workspace.ini" : settingsPath;
    QDir().mkpath(QFileInfo(path).absolutePath());
    m_settings = std::make_unique<QSettings>(path, QSettings::IniFormat);
    m_workspaces = std::make_unique<WorkspaceLibrary>(m_settings.get());
    QString workspaceError;
    if (!m_workspaces->load(&workspaceError)) QTimer::singleShot(0,this,[this,workspaceError]{showActionFeedback(workspaceError,true);});
    m_recoverySave = new ProjectSave(this);
    m_recoverySave->setObjectName("recoveryWriter");
    connect(m_recoverySave, &ProjectSave::finished, this, [this](bool saved, bool cancelled, const QString &path, const QString &error) {
        if (saved) m_ownedRecoveryPath = path;
        else if (!cancelled) statusBar()->showMessage("Recovery snapshot failed: " + error, 10000);
    });
    m_palettes = new PaletteLibrary(QFileInfo(path).absolutePath() + "/palettes.json", this);
    QString paletteError; m_palettes->load(&paletteError);
    m_drawingTemplates = new DrawingTemplateLibrary(QFileInfo(path).absolutePath() + "/drawing-templates.json", this);
    QString templateError;
    if (!m_drawingTemplates->load(&templateError)) QTimer::singleShot(0,this,[this,templateError]{statusBar()->showMessage(templateError,12000);});
    m_brushLibrary = new BrushLibrary(QFileInfo(path).absolutePath() + "/brushes.json", this);
    QString brushLibraryError;
    if (!m_brushLibrary->load(&brushLibraryError)) QTimer::singleShot(0, this, [this, brushLibraryError] {
        QMessageBox::warning(this, "Brush library unavailable", brushLibraryError
            + "\nThe existing library was preserved. Built-in presets remain available.");
    });

    m_modelLibrary = new ModelLibrary(QFileInfo(path).absolutePath() + "/models3d", this);
    QString modelLibraryError;
    if(!m_modelLibrary->load(&modelLibraryError)) QTimer::singleShot(0,this,[this,modelLibraryError]{statusBar()->showMessage(modelLibraryError,12000);});
    auto *stack = new QStackedWidget;
    m_canvas = new Canvas;
    m_timeline = new TimelineController(m_canvas, this);
    m_canvas->setPlaybackPreviewDivisor(m_settings->value("view/playbackPreviewDivisor",1).toInt());
    connect(m_canvas,&Canvas::playbackPreviewChanged,this,[this]{
        m_settings->setValue("view/playbackPreviewDivisor",m_canvas->playbackPreviewDivisor());
    });
    m_autoActions = new AutoActions(m_canvas->document(), QFileInfo(path).absolutePath() + "/auto-actions.json", this);
    QString actionLibraryError;
    if (!m_autoActions->load(&actionLibraryError))
        QTimer::singleShot(0,this,[this,actionLibraryError]{showActionFeedback(actionLibraryError, true);});
    connect(m_autoActions, &AutoActions::status, this, [this](const QString &message){showActionFeedback(message, false);});
    m_timeline->setAudioScrubbing(m_settings->value("timeline/audioScrub", false).toBool());
    connect(m_timeline, &TimelineController::audioScrubbingChanged, this, [this](bool enabled) {
        m_settings->setValue("timeline/audioScrub", enabled); m_settings->sync();
    });
    m_timeline->setOnionSkinSettings(onionSkinSettingsFromJson(QJsonDocument::fromJson(m_settings->value("timeline/onionSkin").toByteArray()).object()));
    connect(m_timeline, &TimelineController::onionSkinSettingsCommitted, this, [this](const OnionSkinSettings &settings) {
        m_settings->setValue("timeline/onionSkin", QJsonDocument(onionSkinSettingsToJson(settings)).toJson(QJsonDocument::Compact));
        m_settings->sync();
    });
    m_toolCatalog = new ToolCatalog(m_brushLibrary, QFileInfo(path).absolutePath() + "/tools.json", this);
    QString toolCatalogError;
    if (!m_toolCatalog->load(&toolCatalogError)) QTimer::singleShot(0, this, [this, toolCatalogError] {
        statusBar()->showMessage(toolCatalogError + " The existing tool library was preserved.", 12000);
    });
    m_toolSession = new ToolSession(m_canvas, m_toolCatalog, this);
    connect(m_toolCatalog, &ToolCatalog::warning, this, [this](const QString &message) {
        statusBar()->showMessage(message, 10000);
    });
    m_welcome = new QWidget;
    auto *welcome = new QVBoxLayout(m_welcome);
    welcome->setContentsMargins(80, 70, 80, 70);
    welcome->addStretch();
    auto *eyebrow = new QLabel("TOON PRO  /  DESKTOP PREVIEW");
    eyebrow->setObjectName("sectionLabel");
    welcome->addWidget(eyebrow);
    auto *title = new QLabel("Every film begins\nwith a blank sheet.");
    title->setStyleSheet(QString("font-size: 42px; font-weight: bold; color: %1;").arg(ui::text));
    welcome->addWidget(title);
    auto *subtitle = new QLabel("A workspace for your hand-drawn anime.\nCreate a movie, set up its first shot, and arrange your panels.");
    subtitle->setObjectName("muted");
    subtitle->setStyleSheet("font-size: 16px; line-height: 1.5;");
    welcome->addWidget(subtitle);
    welcome->addSpacing(20);
    auto *row = new QHBoxLayout;
    auto *create = new QPushButton("New movie project");
    create->setObjectName("newProjectButton");
    create->setDefault(true);
    auto *open = new QPushButton("Open project…");
    create->setIcon(appIcon("file-plus")); open->setIcon(appIcon("folder-open"));
    create->setToolTip("New movie project (" + QKeySequence(QKeySequence::New).toString(QKeySequence::NativeText) + ")");
    open->setToolTip("Open project (" + QKeySequence(QKeySequence::Open).toString(QKeySequence::NativeText) + ")");
    row->addWidget(create); row->addWidget(open); row->addStretch();
    welcome->addLayout(row);
    welcome->addSpacing(35);
    auto *scope = new QLabel("Drawing and frame animation · brushes, editable paths, posed 3D references and recoverable saves.\nOpen Timeline and Frame Properties from the Panels menu to animate your drawings.");
    scope->setObjectName("muted");
    scope->setWordWrap(true);
    welcome->addWidget(scope);
    welcome->addStretch();
    stack->addWidget(m_welcome);
    stack->addWidget(m_canvas);
    setCentralWidget(stack);

    auto *file = menuBar()->addMenu("&File");
    auto *newAction = file->addAction("New movie project…", QKeySequence::New);
    newAction->setObjectName("newProjectAction");
    auto *openAction = file->addAction("Open project…", QKeySequence::Open);
    newAction->setIcon(appIcon("file-plus")); openAction->setIcon(appIcon("folder-open"));
    auto openDialog = [this] {
        const auto path = QFileDialog::getOpenFileName(this, "Open movie project", {}, "Toon Pro project (*.toonproj)");
        if (!path.isEmpty()) openProject(path);
    };
    connect(newAction, &QAction::triggered, this, &MainWindow::newProject);
    connect(create, &QPushButton::clicked, this, &MainWindow::newProject);
    connect(openAction, &QAction::triggered, this, openDialog);
    connect(open, &QPushButton::clicked, this, openDialog);
    file->addSeparator();
    auto *saveAction = file->addAction("Save project", QKeySequence::Save);
    saveAction->setObjectName("saveProjectAction");
    auto *saveAsAction = file->addAction("Save project as…", QKeySequence::SaveAs);
    saveAsAction->setObjectName("saveProjectAsAction");
    saveAction->setIcon(appIcon("save")); saveAsAction->setIcon(appIcon("save"));
    connect(saveAction, &QAction::triggered, this, [this] { save(); });
    connect(saveAsAction, &QAction::triggered, this, [this] { save(true); });
    auto *editAction = file->addAction("Project && shot settings…");
    editAction->setObjectName("projectSettingsAction");
    editAction->setIcon(appIcon("settings-2"));
    connect(editAction, &QAction::triggered, this, &MainWindow::editProject);
    file->addSeparator();
    auto *sheetExport = file->addAction("Export production sheet PNG…");
    auto *frameExport = file->addAction("Export movie frame PNG…");
    sheetExport->setObjectName("sheetExportAction"); frameExport->setObjectName("frameExportAction");
    connect(sheetExport, &QAction::triggered, this, [this] { exportImage(true); });
    connect(frameExport, &QAction::triggered, this, [this] { exportImage(false); });
    auto *sequenceExport = file->addAction("Export animation PNG sequence…");
    sequenceExport->setObjectName("sequenceExportAction");
    sequenceExport->setIcon(appIcon("clapperboard"));
    connect(sequenceExport, &QAction::triggered, this, &MainWindow::exportAnimation);
    auto *videoExport = file->addAction(appIcon("clapperboard"), "Export video MP4…");
    videoExport->setObjectName("videoExportAction");
    connect(videoExport, &QAction::triggered, this, &MainWindow::exportVideo);
    m_projectActions = {saveAction, saveAsAction, editAction, sheetExport, frameExport, sequenceExport, videoExport};
    auto *audioExport = file->addAction(appIcon("music"), "Export mixed audio WAV…");
    audioExport->setObjectName("audioExportAction");
    connect(audioExport, &QAction::triggered, this, &MainWindow::exportAudio);
    m_projectActions.append(audioExport);
    auto *importArtwork = file->addAction(appIcon("image"), "Import image as artwork layer…");
    importArtwork->setObjectName("importArtworkImageAction");
    importArtwork->setToolTip("Import image as artwork layer");
    m_projectActions.append(importArtwork);
    auto *importAudio = file->addAction(appIcon("music"), "Import audio…");
    importAudio->setObjectName("importAudioAction");
    importAudio->setToolTip("Import audio into the timeline at the current frame");
    m_projectActions.append(importAudio);
    connect(importAudio, &QAction::triggered, m_timeline, &TimelineController::importAudio);
    connect(importArtwork, &QAction::triggered, this, [this] {
        if (!m_canvas->hasProject() || m_canvas->document()->editingGestureActive()) return;
        m_timeline->setPlaying(false);
        m_canvas->document()->commitLayerPropertyEdit();
        const auto path = QFileDialog::getOpenFileName(this, "Import artwork image", {}, "Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)");
        if (path.isEmpty()) return;
        QImageReader reader(path); reader.setAutoTransform(true);
        const auto size = reader.size();
        if (!size.isValid() || qint64(size.width()) * size.height() > 64000000) {
            statusBar()->showMessage("Choose an image of up to 64 million pixels.", 6000); return;
        }
        auto image = reader.read();
        if (image.isNull()) { statusBar()->showMessage("Could not read image: " + reader.errorString(), 6000); return; }
        const auto &project = m_canvas->project();
        const bool fitted = image.width() > project.width || image.height() > project.height;
        if (fitted) image = image.scaled(project.width, project.height, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QPointF origin((project.width - image.width()) / 2.0, (project.height - image.height()) / 2.0);
        if (ClipboardTools::importImage(m_canvas->document(), image, QFileInfo(path).completeBaseName(), origin)) {
            m_canvas->document()->clearRasterSelection();
            m_canvas->setTool(Canvas::Tool::Transform);
            m_canvas->setFocus(Qt::OtherFocusReason);
            if (fitted) statusBar()->showMessage("Image fitted to the canvas. Drag the transform handles to position it.", 5000);
        }
    });
    for (auto *action : m_projectActions) action->setEnabled(false);
    file->addSeparator();
    auto *quit = file->addAction("Quit", QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);
    auto *edit = menuBar()->addMenu("&Edit");
    auto *undo = new QAction("Undo", this);
    auto *redo = new QAction("Redo", this);
    undo->setObjectName("undoAction"); redo->setObjectName("redoAction");
    undo->setIcon(appIcon("undo-2")); redo->setIcon(appIcon("redo-2"));
    undo->setEnabled(false); redo->setEnabled(false);
    auto *history = m_canvas->document()->history();
    auto organizationFocused = [this] {
        // Focus can change while a modal's children are being destroyed. Walk
        // the focused widget's parents instead of traversing that mutable tree.
        // QWidget::isAncestorOf stops at a floating dock's window boundary.
        // Follow ownership to this MainWindow before choosing its history.
        bool organization = false;
        for (auto *widget = QApplication::focusWidget(); widget; widget = widget->parentWidget()) {
            if (widget == this) return organization;
            organization |= qobject_cast<ToolNavigationPanel *>(widget) || qobject_cast<ToolManagementPanel *>(widget)
                || qobject_cast<BrushManagementPanel *>(widget);
        }
        return false;
    };
    auto updateHistoryActions = [this, history, undo, redo, organizationFocused] {
        if (organizationFocused()) {
            undo->setEnabled(m_toolCatalog->history()->canUndo());
            redo->setEnabled(m_toolCatalog->history()->canRedo());
            return;
        }
        const bool preview = m_canvas->scene3D()->editing() || m_canvas->document()->transformActive() || m_canvas->drawingTools()->active() || m_canvas->document()->layerPropertyEditActive();
        undo->setEnabled(preview || (m_canvas->document()->keyframeEditActive() || m_canvas->document()->cameraEditActive() || m_canvas->document()->effectsEditActive()) || m_timeline->hasPendingEdits() || history->canUndo());
        redo->setEnabled(!preview && !(m_canvas->document()->keyframeEditActive() || m_canvas->document()->cameraEditActive() || m_canvas->document()->effectsEditActive()) && !m_timeline->hasPendingEdits() && history->canRedo());
    };
    connect(qApp, &QApplication::focusChanged, this, updateHistoryActions);
    connect(m_toolCatalog->history(), &QUndoStack::canUndoChanged, this, updateHistoryActions);
    connect(m_toolCatalog->history(), &QUndoStack::canRedoChanged, this, updateHistoryActions);
    connect(history, &QUndoStack::canUndoChanged, this, updateHistoryActions);
    connect(history, &QUndoStack::canRedoChanged, this, updateHistoryActions);
    connect(m_canvas->document(), &Document::transformChanged, this, updateHistoryActions);
    connect(m_canvas->drawingTools(), &DrawingToolController::changed, this, updateHistoryActions);
    connect(m_canvas->scene3D(), &Scene3DController::changed, this, updateHistoryActions);
    connect(m_canvas->document(), &Document::changed, this, updateHistoryActions);
    connect(m_timeline, &TimelineController::changed, this, updateHistoryActions);
    connect(undo, &QAction::triggered, this, [this, history, organizationFocused] {
        if (organizationFocused()) { m_toolCatalog->history()->undo(); return; }
        m_timeline->setPlaying(false);
        if (m_timeline->hasPendingEdits()) { m_timeline->cancelEditing(); return; }
        if ((m_canvas->document()->keyframeEditActive() || m_canvas->document()->cameraEditActive() || m_canvas->document()->effectsEditActive())) { m_timeline->cancelEditing(); return; }
        if (m_canvas->scene3D()->editing()) { m_canvas->scene3D()->cancelEdit(); return; }
        if (m_canvas->drawingTools()->active()) { m_canvas->drawingTools()->cancel(); return; }
        if (m_canvas->document()->transformActive()) { m_canvas->document()->cancelTransform(); return; }
        if (m_canvas->document()->layerPropertyEditActive()) { m_canvas->document()->cancelLayerPropertyEdit(); return; }
        m_canvas->finishStroke(); history->undo();
    });
    connect(redo, &QAction::triggered, this, [this, history, organizationFocused] {
        if (organizationFocused()) { m_toolCatalog->history()->redo(); return; }
        m_timeline->setPlaying(false);
        if (m_timeline->hasPendingEdits()) return;
        if (m_canvas->scene3D()->editing() || m_canvas->document()->transformActive() || m_canvas->drawingTools()->active() || m_canvas->document()->layerPropertyEditActive()) return;
        m_canvas->finishStroke(); history->redo();
    });
    undo->setShortcut(QKeySequence::Undo); redo->setShortcut(QKeySequence::Redo);
    edit->addAction(undo); edit->addAction(redo);
    edit->addSeparator();
    const auto clipboardAction = [this, edit](const QString &name, const char *id, const char *icon, QKeySequence shortcut) {
        auto *action = new QAction(appIcon(icon), name, m_canvas); action->setObjectName(id);
        action->setShortcut(shortcut); action->setShortcutContext(Qt::WidgetShortcut);
        m_canvas->addAction(action); edit->addAction(action); return action;
    };
    auto *cut = clipboardAction("Cut pixels", "cutPixelsAction", "scissors", QKeySequence::Cut);
    auto *copy = clipboardAction("Copy pixels", "copyPixelsAction", "copy", QKeySequence::Copy);
    auto *paste = clipboardAction("Paste into new layer", "pastePixelsAction", "clipboard-paste", QKeySequence::Paste);
    auto *pasteActive = clipboardAction("Paste into active layer", "pasteActiveLayerAction", "clipboard-paste", QKeySequence("Ctrl+Shift+V"));
    auto *duplicatePixels = clipboardAction("Duplicate selected pixels", "duplicateSelectedPixelsAction", "copy", QKeySequence("Ctrl+J"));
    connect(cut, &QAction::triggered, this, [this] { ClipboardTools::copy(m_canvas->document(), true); });
    connect(copy, &QAction::triggered, this, [this] { ClipboardTools::copy(m_canvas->document()); });
    connect(paste, &QAction::triggered, this, [this] {
        if (ClipboardTools::paste(m_canvas->document())) {
            m_canvas->document()->clearRasterSelection(); m_canvas->setTool(Canvas::Tool::Transform);
        }
    });
    connect(pasteActive, &QAction::triggered, this, [this] { ClipboardTools::paste(m_canvas->document(), false); });
    connect(duplicatePixels, &QAction::triggered, this, [this] {
        if (ClipboardTools::duplicate(m_canvas->document())) {
            m_canvas->document()->clearRasterSelection(); m_canvas->setTool(Canvas::Tool::Transform);
        }
    });
    const auto updateClipboard = [this, cut, copy, paste, pasteActive, duplicatePixels] {
        const auto *document = m_canvas->document();
        const bool idle = document->isOpen() && !document->editingGestureActive() && !document->layerPropertyEditActive();
        bool drawable = false, readable = false;
        if (idle) {
            const auto &layers = document->activeShot().layers;
            const auto &layer = layers[document->activeLayer()];
            readable = !layer.group;
            drawable = layerCanPaint(layers, document->activeLayer()) && !layer.alphaLocked;
        }
        cut->setEnabled(drawable); copy->setEnabled(readable); duplicatePixels->setEnabled(readable);
        paste->setEnabled(idle); pasteActive->setEnabled(drawable);
    };
    connect(m_canvas->document(), &Document::changed, this, updateClipboard);
    connect(m_canvas->document(), &Document::selectionChanged, this, updateClipboard);
    connect(m_canvas->document(), &Document::transformChanged, this, updateClipboard);
    connect(m_canvas->drawingTools(), &DrawingToolController::changed, this, updateClipboard);
    updateClipboard();
    auto *selectMenu = menuBar()->addMenu("&Select");
    auto selectionAction = [this, selectMenu](const QString &label, const char *id, const QKeySequence &shortcut) {
        auto *action = new QAction(label, m_canvas); action->setObjectName(id);
        action->setShortcut(shortcut); action->setShortcutContext(Qt::WidgetShortcut);
        m_canvas->addAction(action); selectMenu->addAction(action); return action;
    };
    auto *selectAll = selectionAction("Select all pixels", "selectAllPixelsAction", QKeySequence::SelectAll);
    auto *invert = selectionAction("Invert selection", "invertSelectionAction", QKeySequence("Ctrl+Shift+I"));
    auto *deselect = selectionAction("Deselect", "deselectAction", QKeySequence("Ctrl+D"));
    auto *document = m_canvas->document();
    connect(selectAll, &QAction::triggered, document, &Document::selectAllPixels);
    connect(invert, &QAction::triggered, document, &Document::invertRasterSelection);
    connect(deselect, &QAction::triggered, document, &Document::clearRasterSelection);
    selectMenu->addSeparator();
    auto *transform = selectionAction("Transform pixels", "transformPixelsAction", QKeySequence("Ctrl+T"));
    auto *apply = selectionAction("Apply transform", "applyTransformAction", {});
    auto *cancel = selectionAction("Cancel transform", "cancelTransformAction", {});
    connect(transform, &QAction::triggered, this, [this] { m_canvas->setTool(Canvas::Tool::Transform); });
    connect(apply, &QAction::triggered, this, [this, document] {
        auto *advanced = m_canvas->drawingTools()->transform();
        if (advanced->active()) advanced->apply(); else document->applyTransform();
    });
    connect(cancel, &QAction::triggered, this, [this, document] {
        auto *advanced = m_canvas->drawingTools()->transform();
        if (advanced->active()) advanced->cancel(); else document->cancelTransform();
    });
    auto *advanced = selectionAction("Transform selected layers / perspective / warp", "advancedTransformAction", QKeySequence("Ctrl+Alt+T"));
    auto *boundary = selectionAction("Transform selection boundary", "transformBoundaryAction", {});
    connect(advanced, &QAction::triggered, this, [this] {
        const auto *layers = findChild<LayerPanel *>();
        m_canvas->drawingTools()->beginAdvanced(layers ? layers->selectedLayerIds() : QStringList());
    });
    connect(boundary, &QAction::triggered, this, [this] { m_canvas->drawingTools()->beginAdvanced({}, true); });
    selectMenu->addSeparator();
    auto *fill = selectionAction("Fill selection with foreground", "fillSelectionAction", {});
    auto *clearPixels = selectionAction("Clear selected pixels", "clearSelectedPixelsAction", QKeySequence(Qt::Key_Backspace));
    connect(fill, &QAction::triggered, this, [this, document] { document->fillRasterSelection(m_canvas->brush.color); });
    connect(clearPixels, &QAction::triggered, this, [this, document] {
        if (m_canvas->tool() != Canvas::Tool::Guides) document->clearSelectedPixels();
    });
    auto updateSelectionActions = [this, document, saveAction, saveAsAction, selectAll, invert, deselect, transform, advanced, boundary, apply, cancel, fill, clearPixels] {
        const bool idle = document->isOpen() && !document->editingGestureActive();
        selectAll->setEnabled(idle); invert->setEnabled(idle);
        deselect->setEnabled(idle && document->rasterSelection().active);
        transform->setEnabled(idle); advanced->setEnabled(idle);
        boundary->setEnabled(idle && document->rasterSelection().active && !document->rasterSelection().path.isEmpty());
        const bool transforming = document->transformActive() || m_canvas->drawingTools()->transform()->active();
        apply->setEnabled(transforming); cancel->setEnabled(transforming);
        const bool selected = idle && document->rasterSelection().active && !document->rasterSelection().path.isEmpty();
        bool canPaint = false, alphaLocked = false;
        if (selected) {
            const auto &layers = document->activeShot().layers;
            canPaint = layerCanPaint(layers, document->activeLayer());
            alphaLocked = layers[document->activeLayer()].alphaLocked;
        }
        fill->setEnabled(selected && canPaint);
        // Guide/path/object editing owns Backspace. Leave the key to Canvas
        // instead of clearing unrelated selected raster pixels underneath it.
        const auto tool = m_canvas->tool();
        const bool toolOwnsBackspace = m_canvas->drawingTools()->active()
            || tool == Canvas::Tool::Guides || tool == Canvas::Tool::Text || tool == Canvas::Tool::VectorNode
            || tool == Canvas::Tool::VectorPen || tool == Canvas::Tool::Polygon
            || tool == Canvas::Tool::PolygonSelect || tool == Canvas::Tool::Curve;
        clearPixels->setEnabled(selected && canPaint && !alphaLocked && !toolOwnsBackspace);
        for (auto *action : m_projectActions) action->setEnabled(document->isOpen() && !document->editingGestureActive());
        // Save resolves these previews through the shared editor controllers.
        // Keep it available before the gesture creates an Undo entry.
        if (document->isOpen() && (m_canvas->scene3D()->editing() || (document->keyframeEditActive() || document->cameraEditActive() || document->effectsEditActive()))) {
            saveAction->setEnabled(true); saveAsAction->setEnabled(true);
        }
    };
    connect(document, &Document::changed, this, updateSelectionActions);
    connect(document, &Document::rasterSelectionChanged, this, updateSelectionActions);
    connect(document, &Document::transformChanged, this, updateSelectionActions);
    connect(m_canvas->drawingTools(), &DrawingToolController::changed, this, updateSelectionActions);
    connect(m_canvas->scene3D(), &Scene3DController::changed, this, updateSelectionActions);
    connect(m_canvas->keyframeTransformTool(), &KeyframeTransformTool::changed, this, updateSelectionActions);
    connect(document, &Document::selectionChanged, this, updateSelectionActions);
    connect(m_canvas, &Canvas::toolChanged, this, updateSelectionActions);
    updateSelectionActions();
    auto *view = menuBar()->addMenu("&View");
    auto *fit = view->addAction("Fit sheet", QKeySequence("Ctrl+0"));
    fit->setIcon(appIcon("scan"));
    connect(fit, &QAction::triggered, m_canvas, &Canvas::fitSheet);
    auto *actualSize = view->addAction("Actual size", QKeySequence("Ctrl+1"));
    connect(actualSize, &QAction::triggered, this, [this] { m_canvas->setZoom(1); });
    connect(view->addAction("Reset view rotation"), &QAction::triggered, this, [this] { m_canvas->setRotation(0); m_canvas->fitSheet(); });
    m_canvas->setCanvasSmoothing(m_settings->value("view/canvasSmoothing", true).toBool());
    m_canvas->setHoldShapeEnabled(m_settings->value("drawing/holdShape", true).toBool());
    connect(m_canvas, &Canvas::holdShapeEnabledChanged, this, [this](bool enabled) {
        m_settings->setValue("drawing/holdShape", enabled);
    });
    auto *canvasSmoothing = view->addAction(appIcon("blend"), "Canvas smoothing (AA)");
    canvasSmoothing->setObjectName("canvasSmoothingAction");
    canvasSmoothing->setIconText("AA");
    canvasSmoothing->setToolTip("Canvas smoothing (AA) · display only\nSmooth zoomed or rotated artwork without changing pixels or exports.");
    canvasSmoothing->setCheckable(true);
    canvasSmoothing->setChecked(m_canvas->canvasSmoothing());
    connect(canvasSmoothing, &QAction::toggled, m_canvas, &Canvas::setCanvasSmoothing);
    connect(m_canvas, &Canvas::canvasSmoothingChanged, this, [this, canvasSmoothing](bool enabled) {
        canvasSmoothing->setChecked(enabled);
        m_settings->setValue("view/canvasSmoothing", enabled);
    });
    view->addSeparator();
    auto *selectionBar = view->addAction("Selection task bar");
    selectionBar->setObjectName("selectionTaskBarAction"); selectionBar->setCheckable(true);
    selectionBar->setChecked(m_canvas->selectionBarVisible());
    connect(selectionBar, &QAction::toggled, m_canvas, &Canvas::setSelectionBarVisible);
    connect(m_canvas, &Canvas::selectionBarVisibilityChanged, selectionBar, &QAction::setChecked);
    auto *focusSelection = view->addAction("Shade outside selection");
    focusSelection->setObjectName("selectionFocusOverlayAction"); focusSelection->setCheckable(true);
    focusSelection->setChecked(m_canvas->selectionFocusOverlay());
    connect(focusSelection, &QAction::toggled, m_canvas, &Canvas::setSelectionFocusOverlay);
    connect(m_canvas, &Canvas::selectionUiChanged, this, [this, focusSelection] {
        focusSelection->setChecked(m_canvas->selectionFocusOverlay());
    });
    auto *toolbar = addToolBar("Project controls");
    toolbar->setObjectName("projectToolbar");
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(20, 20));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->addAction(newAction);
    toolbar->addAction(openAction);
    toolbar->addAction(saveAction);
    toolbar->addSeparator();
    toolbar->addAction(undo);
    toolbar->addAction(redo);
    toolbar->addSeparator();
    toolbar->addAction(editAction);
    toolbar->addAction(fit);
    toolbar->addAction(canvasSmoothing);
    if (auto *button = qobject_cast<QToolButton *>(toolbar->widgetForAction(canvasSmoothing))) {
        // AA is a more recognizable label for this view option than a generic icon.
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setAccessibleName("Canvas smoothing");
    }
    m_quickAccess = new QuickAccessToolbar(m_canvas, m_toolCatalog, m_settings.get(), this);
    m_quickAccess->drawingModeAction()->setObjectName("drawingModeAction");
    addToolBar(Qt::TopToolBarArea, m_quickAccess);
    view->addSeparator();
    view->addAction(m_quickAccess->drawingModeAction());
    view->addAction(m_quickAccess->toggleViewAction());
    connect(m_quickAccess, &QuickAccessToolbar::drawingModeRequested, this, [this] {
        setDrawingMode(!m_drawingMode);
    });
    // Window-owned shortcut associations remain active when their menu/toolbars
    // are hidden for drawing mode. These are the same actions, not duplicates.
    addActions({newAction, openAction, saveAction, saveAsAction, undo, redo, fit, actualSize, quit});
    createPanels();
    const auto updateToolTitle = [this] {
        auto *properties = findChild<QDockWidget *>("propertiesPanel");
        const auto *tool = m_toolCatalog->tool(m_toolCatalog->activeToolId());
        const auto context = tool ? QString("Tool Properties · %1").arg(tool->name) : QString("Tool Properties");
        properties->setToolTip(context);
        properties->setAccessibleDescription(context);
        refreshDockTabs();
    };
    connect(m_toolCatalog, &ToolCatalog::changed, this, updateToolTitle);
    updateToolTitle();
    toolbar->addSeparator();
    createWorkspaceMenu(view, toolbar);
    createDrawingWorkspaceControls();
    struct ToolShortcut { const char *key; Canvas::Tool tool; };
    const ToolShortcut shortcuts[] = {
        {"B", Canvas::Tool::Brush}, {"E", Canvas::Tool::Eraser}, {"I", Canvas::Tool::Eyedropper},
        {"F", Canvas::Tool::Bucket}, {"D", Canvas::Tool::Gradient}, {"M", Canvas::Tool::RectangleSelect},
        {"L", Canvas::Tool::LassoSelect}, {"T", Canvas::Tool::Transform}, {"H", Canvas::Tool::Hand},
        {"G", Canvas::Tool::Guides}
    };
    for (const auto &entry : shortcuts) {
        auto *shortcut = new QShortcut(QKeySequence(entry.key), m_canvas);
        shortcut->setContext(Qt::WidgetShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, entry] { m_toolSession->activateEngine(entry.tool); });
    }
    connect(m_canvas, &Canvas::toolChanged, this, [this] {
        if (!m_drawingMode && !m_compactDrawingWorkspace &&
            m_canvas->hasProject() && m_canvas->toolAvailable()) {
            auto *properties = findChild<QDockWidget *>("propertiesPanel"); properties->show(); properties->raise();
        }
    });
    connect(m_canvas->document(), &Document::changed, this, [this] { ++m_documentRevision; updateTitle(); });
    connect(m_canvas->document(), &Document::drawingAidsChanged, this, [this] {
        ++m_documentRevision;
        m_dirty = !m_canvas->document()->history()->isClean() || m_canvas->document()->drawingAidsDirty();
        updateTitle();
    });
    connect(m_canvas->document(), &Document::drawingAidsCleanChanged, this, [this](bool) {
        m_dirty = !m_canvas->document()->history()->isClean() || m_canvas->document()->drawingAidsDirty();
        if (!m_dirty && !m_canvas->document()->committing()) {
            m_recoverySave->cancel();
            if (!m_ownedRecoveryPath.isEmpty()) { QFile::remove(m_ownedRecoveryPath); m_ownedRecoveryPath.clear(); }
        }
        updateTitle();
    });
    connect(m_canvas->document()->history(), &QUndoStack::indexChanged, this, [this] {
        // Undo/Redo may leave the document dirty while invalidating a recovery
        // snapshot. Normal commits may safely retain an older completed save.
        if (!m_canvas->document()->committing()) {
            m_recoverySave->cancel();
            if (!m_ownedRecoveryPath.isEmpty()) { QFile::remove(m_ownedRecoveryPath); m_ownedRecoveryPath.clear(); }
        }
    });
    connect(m_canvas->document()->history(), &QUndoStack::cleanChanged, this, [this](bool clean) {
        m_dirty = !clean || m_canvas->document()->drawingAidsDirty();
        if (!m_dirty && !m_canvas->document()->committing()) m_recoverySave->cancel();
        if (!m_dirty && !m_canvas->document()->committing() && !m_ownedRecoveryPath.isEmpty()) {
            QFile::remove(m_ownedRecoveryPath);
            m_ownedRecoveryPath.clear();
        }
        updateTitle();
    });
    auto *autosave = new QTimer(this);
    autosave->setObjectName("recoveryTimer");
    autosave->setInterval(30000);
    connect(autosave, &QTimer::timeout, this, &MainWindow::writeRecovery);
    autosave->start();
    m_statusFeedback = new QWidget;
    m_statusFeedback->setObjectName("canvasStatusFeedback");
    m_statusFeedback->setAccessibleName("Canvas status");
    m_statusFeedback->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *statusLayout = new QHBoxLayout(m_statusFeedback);
    statusLayout->setContentsMargins(6, 1, 6, 1); statusLayout->setSpacing(6);
    m_statusIcon = new QLabel;
    m_statusIcon->setObjectName("canvasStatusIcon");
    m_statusIcon->setFixedSize(20, 20);
    m_statusIcon->setPixmap(appIcon("triangle-alert").pixmap(QSize(18, 18), devicePixelRatioF()));
    m_statusIcon->setAlignment(Qt::AlignCenter);
    m_status = new StatusLabel;
    m_status->setObjectName("canvasStatusMessage");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusLayout->addWidget(m_statusIcon); statusLayout->addWidget(m_status, 1);
    statusBar()->addWidget(m_statusFeedback, 1);
    auto *version = new QLabel(QString("%1 · Drawing preview").arg(QCoreApplication::applicationVersion()));
    version->setObjectName("applicationVersionStatus");
    statusBar()->addPermanentWidget(version);
    m_statusMessageTimer = new QTimer(this);
    m_statusMessageTimer->setObjectName("canvasFeedbackMessageTimer");
    m_statusMessageTimer->setSingleShot(true);
    m_statusHighlightTimer = new QTimer(this);
    m_statusHighlightTimer->setObjectName("canvasFeedbackHighlightTimer");
    m_statusHighlightTimer->setSingleShot(true);
    m_statusHighlightTimer->setInterval(1600);
    connect(m_statusMessageTimer, &QTimer::timeout, this, [this] {
        m_actionFeedback.clear();
        refreshStatusFeedback();
    });
    connect(m_statusHighlightTimer, &QTimer::timeout, this, &MainWindow::refreshStatusFeedback);
    connect(m_canvas, &Canvas::interactionHintChanged, this, [this](const QString &message) {
        m_interactionHint = message;
        // Leaving the canvas or resolving its restriction clears an attempted
        // action hint immediately. Unrelated runtime errors retain their timer.
        if (m_feedbackIsBlockedAttempt && message != m_actionFeedback) {
            m_actionFeedback.clear();
            m_statusMessageTimer->stop();
            m_statusHighlightTimer->stop();
        }
        refreshStatusFeedback();
    });
    connect(m_canvas, &Canvas::actionBlocked, this, [this](const QString &message) {
        showActionFeedback(message, true);
    });
    connect(m_canvas->document(), &Document::warning, this, [this](const QString &message) {
        showActionFeedback(message, false);
    });
    connect(m_canvas->document(), &Document::frameChanged, this, &MainWindow::refreshStatusFeedback);
    refreshStatusFeedback();
    connect(m_canvas, &Canvas::viewChanged, this, [this] {
        if (!m_canvas->hasProject()) return;
        m_viewStatus = QString("%1  /  %2     %3 × %4     %5 FPS     Zoom %6%     Rotation %7°")
            .arg(m_canvas->project().movie, m_canvas->shot().number)
            .arg(m_canvas->project().width).arg(m_canvas->project().height).arg(m_canvas->project().fps)
            .arg(qRound(m_canvas->zoom() * 100)).arg(m_canvas->rotation(), 0, 'f', 0);
        refreshStatusFeedback();
    });
    // Provisional dock placement leaves a stale tall minimum until the layout
    // activates. Resolve the final hidden/tabbed layout before sizing, otherwise
    // resize() is clamped to that obsolete minimum even on the welcome screen.
    layout()->invalidate();
    layout()->activate();
    resize(1320, 860);
    // Finish panel migration while the window is still in its normal state.
    // Qt retains a deferred layout snapshot for hidden maximized/fullscreen
    // windows; removing the legacy Toolbox would leave stale items in it.
    restoreWorkspace();
    if (m_settings->contains("geometry")) restoreGeometry(m_settings->value("geometry").toByteArray());
    if (auto *tabs = findChild<QTabBar *>("brushSettingsCategory")) {
        const int saved = m_settings->value("brush/category", 0).toInt();
        tabs->setCurrentIndex(saved >= 0 && saved < tabs->count() ? saved : 0);
    }
    // QAction's default tooltip omits shortcuts. The same meaningful label is
    // used by toolbar icons and menu actions, while menus retain visible text.
    for (auto *action : findChildren<QAction *>()) {
        if (action->isSeparator()) continue;
        auto help = action->toolTip();
        if (help.isEmpty()) help = action->text();
        if (!action->shortcut().isEmpty())
            help += QStringLiteral(" (%1)").arg(action->shortcut().toString(QKeySequence::NativeText));
        action->setToolTip(help);
    }
    QTimer::singleShot(0, this, &MainWindow::recoverFloatingPanels);
    connect(qApp, &QGuiApplication::screenRemoved, this, [this] { recoverFloatingPanels(); });
    // Tab toggles drawing mode from the canvas. In panels it remains ordinary
    // keyboard focus navigation, including sliders, tabs and icon actions.
    qApp->installEventFilter(this);
}
MainWindow::~MainWindow() {
    qApp->removeEventFilter(this);
    m_recoverySave->cancel();
    disconnect(m_recoverySave, nullptr, this, nullptr);
    m_timeline->setPlaying(false);
    m_toolSession->flush();
    disconnect(m_timeline, nullptr, this, nullptr);
    disconnect(m_canvas->scene3D(), nullptr, this, nullptr);
    disconnect(m_autoActions, nullptr, this, nullptr);
    // Stop recording while the document is still alive; panel state and user
    // library updates must not run from QUndoStack teardown notifications.
    if(m_autoActions->recording())m_autoActions->stopRecording();
    delete m_autoActions; m_autoActions=nullptr;
    // QUndoStack emits while its Document is being destroyed, after menus may
    // already have gone. Stop callbacks before QWidget starts deleting children.
    disconnect(qApp, nullptr, this, nullptr);
    disconnect(m_toolCatalog->history(), nullptr, this, nullptr);
    disconnect(m_canvas, nullptr, this, nullptr);
    disconnect(m_canvas->document(), nullptr, this, nullptr);
    disconnect(m_canvas->document()->history(), nullptr, this, nullptr);
    disconnect(m_canvas->drawingTools(), nullptr, this, nullptr);
}

void MainWindow::refreshStatusFeedback() {
    const auto message = !m_actionFeedback.isEmpty() ? m_actionFeedback
        : !m_interactionHint.isEmpty() ? m_interactionHint
        : m_canvas->document()->keyframeSourceEditing() ? "Source artwork · " + m_viewStatus : m_viewStatus;
    const bool feedback = !m_actionFeedback.isEmpty() || !m_interactionHint.isEmpty();
    m_status->setText(message);
    m_status->setToolTip(message);
    m_status->setAccessibleName(message);
    m_status->setAccessibleDescription(feedback ? "Canvas action feedback" : "Project and canvas view");
    const bool toolGuidance = (m_canvas->tool() == Canvas::Tool::Arc || m_canvas->document()->stampPlacementActive())
        && m_canvas->toolBlockReason().isEmpty();
    m_statusIcon->setVisible(feedback && (!toolGuidance || !m_actionFeedback.isEmpty()));
    const auto iconHelp = !m_actionFeedback.isEmpty() && !m_feedbackIsBlockedAttempt
        ? QStringLiteral("Warning") : QStringLiteral("Action unavailable");
    m_statusIcon->setAccessibleName(iconHelp);
    m_statusIcon->setToolTip(iconHelp);
    const bool highlighted = feedback && m_statusHighlightTimer->isActive();
    m_statusFeedback->setProperty("highlighted", highlighted);
    m_statusFeedback->setStyleSheet(QString(
        "QWidget#canvasStatusFeedback { background: %1; border-left: 2px solid %2; border-radius: 3px; }")
        .arg(highlighted ? ui::selected : ui::panel, highlighted ? ui::accent : ui::panel));
}

void MainWindow::showActionFeedback(const QString &message, bool blockedAttempt) {
    if (message.isEmpty()) return;
    m_actionFeedback = message;
    m_feedbackIsBlockedAttempt = blockedAttempt;
    m_statusMessageTimer->start(blockedAttempt ? 5000 : 8000);
    m_statusHighlightTimer->start();
    statusBar()->clearMessage();
    refreshStatusFeedback();
    QAccessibleEvent event(m_status, QAccessible::Alert);
    QAccessible::updateAccessibility(&event);
}

bool MainWindow::event(QEvent *event) {
    const auto type = event->type();
    const bool handled = QMainWindow::event(event);
    if (type == QEvent::WindowStateChange && m_drawingMode && !m_changingDrawingMode && !isFullScreen()) {
        // The native window menu can also leave fullscreen. Restore the same
        // workspace rather than leaving the artist with hidden docks.
        QTimer::singleShot(0, this, [this] {
            if (m_drawingMode && !m_changingDrawingMode && !isFullScreen()) setDrawingMode(false);
        });
    }
    // Qt creates/reuses dock tab bars as docks are moved and restored. Only
    // layout changes need this small deferred pass; painting never schedules it.
    if ((type == QEvent::ChildPolished || type == QEvent::LayoutRequest)
        && !m_dockTabsRefreshPending) {
        m_dockTabsRefreshPending = true;
        QTimer::singleShot(0, this, [this] {
            m_dockTabsRefreshPending = false;
            refreshDockTabs();
        });
    }
    return handled;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_canvas && (event->type() == QEvent::Resize || event->type() == QEvent::Move || event->type() == QEvent::Show)) {
        refreshDrawingDrawerGeometry();
    }
    // Pen samples, paints and all other hot-path events need no workspace work.
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::Show)
        return QMainWindow::eventFilter(watched, event);
    auto *widget = qobject_cast<QWidget *>(watched);
    // isAncestorOf stops at top-level windows, including floating docks.
    // Ownership still leads to this workspace for their controls.
    bool belongs = false;
    for (auto *owner = widget; owner; owner = owner->parentWidget())
        if (owner == this) { belongs = true; break; }
    if (!belongs)
        return QMainWindow::eventFilter(watched, event);
    if (event->type() == QEvent::Show && m_compactDrawingWorkspace && !m_arrangingWorkspace && !m_changingDrawingMode) {
        for (int index = 0; index < 3; ++index) {
            if (widget != findChild<QDockWidget *>(drawingDockIds[index])) continue;
            // Route the existing Panels menu to the same overlay page; the
            // original empty dock must never occupy canvas space in this mode.
            widget->hide();
            QTimer::singleShot(0, this, [this, index] {
                if (!m_compactDrawingWorkspace || m_drawingMode || !canChangeDrawingWorkspace()) return;
                m_drawingDrawerTabs->setCurrentIndex(index); m_drawingDrawerOpen = true;
                refreshDrawingDrawerVisibility(); refreshDrawingWorkspaceControls();
            });
            return true;
        }
    }
    if (m_drawingMode && !m_changingDrawingMode && event->type() == QEvent::Show) {
        // Tool/property callbacks and restored floating docks must not bring
        // inspectors back over a focused drawing session.
        if ((qobject_cast<QDockWidget *>(widget) && m_panels.contains(qobject_cast<QDockWidget *>(widget))) ||
            (qobject_cast<QToolBar *>(widget) && widget != m_quickAccess) ||
            widget == menuBar() || widget == statusBar()) {
            widget->hide();
        }
    }
    if (event->type() != QEvent::KeyPress || !m_canvas->hasProject())
        return QMainWindow::eventFilter(watched, event);
    const auto *key = static_cast<QKeyEvent *>(event);
    // Inline rename delegates and text editors own Escape (cancel/revert).
    // Hiding their parent first would focus them out and commit the draft.
    const bool textEditor = qobject_cast<QLineEdit *>(widget) || qobject_cast<QTextEdit *>(widget) ||
        qobject_cast<QPlainTextEdit *>(widget) || qobject_cast<QComboBox *>(widget);
    if (!textEditor && key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier && m_drawingDrawer &&
        m_drawingDrawer->isVisible() && (widget == m_drawingDrawer || m_drawingDrawer->isAncestorOf(widget)) &&
        !QApplication::activeModalWidget() && !QApplication::activePopupWidget() && !pendingValueEntry(this)) {
        if (!key->isAutoRepeat()) toggleDrawingControls();
        event->accept(); return true;
    }
    if (key->key() != Qt::Key_Tab || key->modifiers() != Qt::NoModifier ||
        QApplication::activeModalWidget() || QApplication::activePopupWidget())
        return QMainWindow::eventFilter(watched, event);
    if (widget != m_canvas) return QMainWindow::eventFilter(watched, event);
    if (!key->isAutoRepeat()) setDrawingMode(!m_drawingMode);
    event->accept();
    return true;
}

void MainWindow::setDrawingMode(bool enabled) {
    if (!m_quickAccess || m_changingDrawingMode) return;
    if (enabled == m_drawingMode || !m_canvas->hasProject() ||
        m_canvas->document()->editingGestureActive() || m_canvas->document()->layerPropertyEditActive() ||
        m_canvas->selectionGestureActive() || m_canvas->holdShapePending() ||
        m_canvas->drawingTools()->active() || m_canvas->scene3D()->editing() || pendingValueEntry(this) ||
        QApplication::mouseButtons() != Qt::NoButton || QApplication::activeModalWidget() || QApplication::activePopupWidget()) {
        m_quickAccess->setDrawingMode(m_drawingMode);
        return;
    }
    m_changingDrawingMode = true;
    setUpdatesEnabled(false);
    if (enabled) {
        m_normalLayout = saveState(3);
        m_normalGeometry = saveGeometry();
        m_normalWindowState = windowState() & ~Qt::WindowMinimized;
        m_normalVisibility.clear();
        const auto remember = [this](QWidget *widget) {
            m_normalVisibility.append({widget, !widget->isHidden()});
        };
        for (auto *panel : m_panels) remember(panel);
        if (m_drawingDrawer) remember(m_drawingDrawer);
        for (auto *bar : findChildren<QToolBar *>(QString(), Qt::FindDirectChildrenOnly)) remember(bar);
        remember(menuBar()); remember(statusBar());
        m_drawingMode = true;
        m_timeline->setPlaying(false);
        m_timeline->movieReview()->setMode(ReviewMode::Shot);
        for (const auto &entry : m_normalVisibility) if (entry.first) entry.first->hide();
        m_quickAccess->show();
        showFullScreen();
    } else {
        m_drawingMode = false;
        if (!(m_normalWindowState & Qt::WindowFullScreen)) {
            setWindowState(m_normalWindowState);
            restoreGeometry(m_normalGeometry);
        }
        restoreState(m_normalLayout, 3);
        for (const auto &entry : m_normalVisibility) if (entry.first) entry.first->setVisible(entry.second);
        setWindowState(m_normalWindowState);
        m_normalVisibility.clear();
    }
    m_quickAccess->setDrawingMode(m_drawingMode);
    refreshDrawingWorkspaceControls();
    refreshDrawingDrawerVisibility();
    setUpdatesEnabled(true);
    m_changingDrawingMode = false;
    activateWindow();
    m_canvas->setFocus(Qt::ShortcutFocusReason);
}

void MainWindow::refreshDockTabs() {
    // Direct children are QMainWindow's dock tabs. Inspector category tabs are
    // nested inside docks and retain their own navigation and sizing.
    for (auto *tabs : findChildren<QTabBar *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (!tabs->property("compactDockTabs").toBool()) {
            tabs->setProperty("compactDockTabs", true);
            tabs->setAccessibleName("Workspace panels");
            tabs->setExpanding(false);
            tabs->setUsesScrollButtons(true);
            tabs->setElideMode(Qt::ElideNone);
            // Refresh once when the explicit compact role is assigned. Local
            // hard-coded tab CSS used to override shared density rules.
            tabs->style()->unpolish(tabs);
            tabs->style()->polish(tabs);
            QEvent styleChanged(QEvent::StyleChange);
            QApplication::sendEvent(tabs, &styleChanged);
            tabs->updateGeometry();
        }
        const QSize iconSize(ui::metrics::tabIcon, ui::metrics::tabIcon);
        if (tabs->iconSize() != iconSize) tabs->setIconSize(iconSize);
        QList<QDockWidget *> peers;
        for (int i = 0; i < tabs->count(); ++i) {
            QDockWidget *match = nullptr;
            for (auto *panel : m_panels) {
                if (tabs->tabText(i) != panel->windowTitle() &&
                    !(tabs->tabText(i).isEmpty() && tabs->accessibleTabName(i) == panel->property("panelLabel").toString())) continue;
                match = panel;
                const auto help = panel->toolTip();
                const auto name = panel->property("panelLabel").toString();
                if (tabs->tabToolTip(i) != help) tabs->setTabToolTip(i, help);
                if (tabs->accessibleTabName(i) != name) tabs->setAccessibleTabName(i, name);
                break;
            }
            peers.append(match);
        }
        // Keep all three daily panels reachable. Qt owns the actual tab titles;
        // native eliding preserves those names and their accessibility while
        // sharing the available width instead of adding scroll-arrow clicks.
        const QStringList dailyPanels{"colorsPanel", "referencesPanel", "controlPanel"};
        const bool dailyGroup = peers.size() >= 2 && std::all_of(peers.cbegin(), peers.cend(), [&](auto *panel) {
            return panel && dailyPanels.contains(panel->objectName());
        });
        if (tabs->expanding() != dailyGroup) tabs->setExpanding(dailyGroup);
        if (tabs->usesScrollButtons() == dailyGroup) tabs->setUsesScrollButtons(!dailyGroup);
        const auto elide = dailyGroup ? Qt::ElideRight : Qt::ElideNone;
        if (tabs->elideMode() != elide) tabs->setElideMode(elide);
    }
}

void MainWindow::navigateHistory(int index) {
    auto *doc=m_canvas->document();auto *stack=doc->history();
    if(!doc->isOpen() || index<0 || index>stack->count() || index==stack->index())return;
    if(doc->strokeActive() || m_autoActions->playing()) {
        showActionFeedback("Finish the current stroke before moving through history.",true);return;
    }
    // A transform decision can commit and prune retained history. Keep the
    // requested command identity instead of accidentally selecting a new index.
    const auto *target=index?stack->command(index-1):nullptr;
    const auto *oldest=stack->count()?stack->command(0):nullptr;
    bool resetDuringDecision=false;
    const auto resetWatch=connect(stack,&QUndoStack::indexChanged,this,[&]{
        if(stack->count()==0)resetDuringDecision=true;
    });
    const bool prepared=m_timeline->prepareTimelineEdit();
    disconnect(resetWatch);
    if(!prepared)return;
    if(resetDuringDecision){showActionFeedback("History was reset by this edit. Choose a retained state.",true);return;}
    int destination=-1;
    if(!target){if(!oldest || (stack->count() && stack->command(0)==oldest))destination=0;}
    else for(int i=0;i<stack->count();++i)if(stack->command(i)==target){destination=i+1;break;}
    if(destination<0){showActionFeedback("That history state is no longer retained. Choose another state.",true);return;}
    if(m_autoActions->recording())m_autoActions->stopRecording();
    stack->setIndex(destination);
}

void MainWindow::createPanels() {
    auto *menu = menuBar()->addMenu("&Panels");
    auto dock = [this, menu](const QString &title, const char *id, QWidget *content, Qt::DockWidgetArea area) {
        const QMap<QString, QString> shortTitles = {{"propertiesPanel", "Properties"}, {"brushPresetPanel", "Sizes"},
            {"framePropertiesPanel", "Frame"}, {"controlPanel", "Navigator"}, {"referencesPanel", "Refs"}};
        auto *panel = new QDockWidget(shortTitles.value(id, title), this);
        panel->setObjectName(id);
        panel->setProperty("panelType", "standard");
        if (QStringList{"toolsPanel", "brushPresetPanel", "propertiesPanel", "layersPanel", "colorsPanel",
                        "brushManagementPanel", "toolManagementPanel", "referencesPanel", "effectsPanel",
                        "celPaintCheckPanel", "celShadingPanel", "symbolsPanel"}.contains(id))
            panel->setProperty("compactDrawingPanel", true);
        panel->setProperty("panelLabel", title);
        panel->setAccessibleName(title);
        panel->setToolTip(title);
        const QMap<QString, QString> icons = {{"toolsPanel", "brush"}, {"propertiesPanel", "settings-2"},
            {"layersPanel", "layers"}, {"colorsPanel", "palette"}, {"controlPanel", "scan"},
            {"referencesPanel", "image"}, {"symbolsPanel", "toon-symbol"}, {"effectsPanel", "sparkles"}, {"animationPanel", "clapperboard"},
            {"celPaintCheckPanel", "toon-paint-check"}, {"celShadingPanel", "toon-cel-shading"},
            {"framePropertiesPanel", "sliders-horizontal"}, {"shotsPanel", "clapperboard"},
            {"historyPanel", "undo-2"}, {"autoActionsPanel", "play"}, {"toolManagementPanel", "settings-2"}, {"brushManagementPanel", "brush"}, {"brushPresetPanel", "circle"}, {"modelLibraryPanel", "box"}};
        panel->setWindowIcon(appIcon(icons.value(id)));
        panel->toggleViewAction()->setIcon(panel->windowIcon());
        panel->toggleViewAction()->setText(title);
        panel->toggleViewAction()->setToolTip(QString("Show or hide %1").arg(title));
        panel->setWidget(content);
        panel->setAllowedAreas(Qt::AllDockWidgetAreas);
        addDockWidget(area, panel);
        menu->addAction(panel->toggleViewAction());
        m_panels.append(panel);
        for (auto *button : panel->findChildren<QAbstractButton *>()) {
            if (button->objectName() == "qt_dockwidget_closebutton") {
                button->setToolTip(QString("Hide %1 panel").arg(title));
                button->setAccessibleName(button->toolTip());
            } else if (button->objectName() == "qt_dockwidget_floatbutton") {
                button->setToolTip(QString("Float or dock %1 panel").arg(title));
                button->setAccessibleName(button->toolTip());
            }
        }
        return panel;
    };
    auto *toolbox = new ToolNavigationPanel(m_toolCatalog, true);
    toolbox->setPreviewDocument(m_canvas->document());
    connect(toolbox, &ToolNavigationPanel::importBrushesRequested, this, &MainWindow::importBrushes);
    connect(toolbox, &ToolNavigationPanel::manageBrushesRequested, this, &MainWindow::openBrushManagement);
    connect(toolbox, &ToolNavigationPanel::manageToolsRequested, this, &MainWindow::openToolManagement);
    m_toolboxColumn = new IconColumn("Toolbox", toolbox, this);
    m_toolboxColumn->setObjectName("toolboxPanel");
    m_toolboxColumn->setWindowIcon(appIcon("folder-open"));
    m_toolboxColumn->toggleViewAction()->setIcon(m_toolboxColumn->windowIcon());
    addToolBar(Qt::LeftToolBarArea, m_toolboxColumn);
    menu->addAction(m_toolboxColumn->toggleViewAction());
    auto *control = new QWidget;
    auto *layout = new QVBoxLayout(control);
    layout->setContentsMargins(6, 6, 6, 6); layout->setSpacing(4);
    layout->addWidget(new Navigator(m_canvas), 1);
    auto *zoom = new ValueSlider("Canvas zoom", 1, 1600, m_canvas->zoom() * 100, 1, " %");
    zoom->setObjectName("navigatorZoom");
    zoom->setSingleStep(10);
    auto *rotation = new ValueSlider("Canvas rotation", -180, 180, m_canvas->rotation(), 1, " °");
    rotation->setObjectName("navigatorRotation");
    rotation->setSingleStep(15);
    layout->addWidget(new PropertyRow("Zoom", zoom));
    layout->addWidget(new PropertyRow("Rotation", rotation));
    auto *actions = new QHBoxLayout;
    actions->setSpacing(4);
    auto iconButton = [](const char *id, const char *icon, const QString &label) {
        auto *button = new QToolButton;
        button->setObjectName(id); button->setIcon(appIcon(icon));
        button->setIconSize(QSize(18, 18)); button->setToolTip(label); button->setAccessibleName(label);
        button->setFixedSize(28, 28); return button;
    };
    auto *fit = iconButton("navigatorFit", "scan", "Fit sheet (" + QKeySequence("Ctrl+0").toString(QKeySequence::NativeText) + ")");
    auto *reset = iconButton("navigatorResetRotation", "rotate-ccw", "Reset view rotation");
    actions->addWidget(fit); actions->addWidget(reset); actions->addStretch();
    layout->addLayout(actions);
    dock("Navigator", "controlPanel", control, Qt::RightDockWidgetArea);
    connect(zoom, &ValueSlider::valueChanged, this, [this](double value) { m_canvas->setZoom(value / 100); });
    connect(rotation, &ValueSlider::valueChanged, m_canvas, &Canvas::setRotation);
    connect(fit, &QToolButton::clicked, m_canvas, &Canvas::fitSheet);
    connect(reset, &QToolButton::clicked, this, [this] { m_canvas->setRotation(0); });
    connect(m_canvas, &Canvas::viewChanged, this, [this, zoom, rotation] {
        const QSignalBlocker a(zoom), b(rotation);
        zoom->setValue(m_canvas->zoom() * 100); rotation->setValue(m_canvas->rotation());
    });
    struct Panel { const char *title; const char *id; const char *message; Qt::DockWidgetArea area; };
    const Panel panels[] = {
        {"Tools", "toolsPanel", "", Qt::LeftDockWidgetArea},
        {"Brush Preset", "brushPresetPanel", "", Qt::LeftDockWidgetArea},
        {"Tool Properties", "propertiesPanel", "", Qt::RightDockWidgetArea},
        {"Layers", "layersPanel", "", Qt::RightDockWidgetArea},
        {"Colors", "colorsPanel", "", Qt::RightDockWidgetArea},
        {"References", "referencesPanel", "", Qt::RightDockWidgetArea},
        {"Effects", "effectsPanel", "", Qt::RightDockWidgetArea},
        {"Cel Paint Check", "celPaintCheckPanel", "", Qt::RightDockWidgetArea},
        {"Cel Shading", "celShadingPanel", "", Qt::RightDockWidgetArea},
        {"Timeline", "animationPanel", "", Qt::BottomDockWidgetArea},
        {"Frame Properties", "framePropertiesPanel", "", Qt::RightDockWidgetArea}
    };
    for (const auto &spec : panels) {
        auto *content = new QWidget;
        auto *box = new QVBoxLayout(content);
        box->setContentsMargins(16, 16, 16, 16);
        auto *label = new QLabel(spec.message);
        label->setWordWrap(true); label->setObjectName("muted");
        content->setMinimumWidth(200);
        box->addWidget(label); box->addStretch();
        if (QString(spec.id) == "toolsPanel") {
            delete content;
            auto *navigation = new ToolNavigationPanel(m_toolCatalog, false);
            navigation->setPreviewDocument(m_canvas->document());
            connect(navigation, &ToolNavigationPanel::importBrushesRequested, this, &MainWindow::importBrushes);
            connect(navigation, &ToolNavigationPanel::manageBrushesRequested, this, &MainWindow::openBrushManagement);
            connect(navigation, &ToolNavigationPanel::manageToolsRequested, this, &MainWindow::openToolManagement);
            navigation->setIconsOnly(m_settings->value("tools/iconsOnly", false).toBool());
            navigation->setTagFilter(m_settings->value("tools/tagFilter").toString());
            connect(navigation, &ToolNavigationPanel::viewOptionsChanged, this, [this, navigation] {
                m_settings->setValue("tools/iconsOnly", navigation->iconsOnly());
                m_settings->setValue("tools/tagFilter", navigation->tagFilter());
            });
            content = navigation;
        } else if (QString(spec.id) == "brushPresetPanel") {
            delete content;
            content = new BrushPresetPanel(m_canvas, m_settings.get());
        } else if (QStringList{"propertiesPanel", "layersPanel", "colorsPanel"}.contains(spec.id)) {
            delete content;
            content = drawingPanel(m_canvas, spec.id);
            if (auto *layers = qobject_cast<LayerPanel *>(content))
                layers->setImportImageAction(findChild<QAction *>("importArtworkImageAction"));
            if (auto *colors = qobject_cast<ColorPanel *>(content)) {
                colors->setPaletteLibrary(m_palettes);
                colors->setCurrentPaletteId(m_settings->value("colors/palette").toString());
                connect(colors, &ColorPanel::paletteSelected, this, [this](const QString &id) {
                    m_settings->setValue("colors/palette", id); m_settings->sync();
                });
            }
            auto *settings = qobject_cast<BrushSettingsPanel *>(content);
            if (!settings) settings = content->findChild<BrushSettingsPanel *>();
            if (settings) settings->setLibrary(m_brushLibrary);
        } else if (QString(spec.id) == "referencesPanel") {
            delete content;
            auto *references = new ReferencePanel(m_canvas->document());
            references->attachCanvas(m_canvas);
            references->setCurrentColor(m_canvas->brush.color);
            connect(m_canvas, &Canvas::brushChanged, references, [this, references] {
                references->setCurrentColor(m_canvas->brush.color);
            });
            references->setSampling(m_canvas->tool() == Canvas::Tool::Eyedropper);
            connect(references, &ReferencePanel::colorSampled, m_canvas, &Canvas::setBrushColor);
            connect(m_canvas, &Canvas::toolChanged, references, [this, references] {
                references->setSampling(m_canvas->tool() == Canvas::Tool::Eyedropper);
            });
            content = references;
        } else if (QString(spec.id) == "celPaintCheckPanel") {
            delete content; content = new CelPaintCheckPanel(m_canvas);
        } else if (QString(spec.id) == "celShadingPanel") {
            delete content; content = new CelShadingPanel(m_canvas);
        } else if (QString(spec.id) == "effectsPanel") {
            delete content;
            auto *effects=new EffectsPanel(m_canvas);effects->setTimelineController(m_timeline);content=effects;
            connect(effects,&EffectsPanel::pendingEditsChanged,m_timeline,[this,effects](bool pending){m_timeline->setPendingEdits(effects,pending);});
            connect(m_timeline,&TimelineController::editResolutionRequested,effects,&EffectsPanel::finishEditing);
            connect(m_timeline,&TimelineController::editCancellationRequested,effects,&EffectsPanel::cancelEditing);
        } else if (QString(spec.id) == "animationPanel") {
            delete content; content = new TimelinePanel(m_timeline, nullptr, m_drawingTemplates);
        } else if (QString(spec.id) == "framePropertiesPanel") {
            delete content; content = new FramePropertiesPanel(m_timeline);
        }
        dock(spec.title, spec.id, content, spec.area);
    }
    auto *symbols = new SymbolLibraryPanel(m_canvas, m_timeline, m_toolCatalog);
    symbols->setTransitionGuard([this]{return resolveCelWorkflows();});
    auto *symbolDock = dock("Symbols", "symbolsPanel", symbols, Qt::RightDockWidgetArea);
    connect(symbols, &SymbolLibraryPanel::editRequested, this, [this](const QString &id) {
        if (!resolveCelWorkflows() || !m_timeline->prepareTimelineEdit()) return;
        auto *document=m_canvas->document();
        if (!document->isOpen() || document->editingGestureActive() || document->layerPropertyEditActive()) return;
        m_toolSession->flush(); QString error;
        if (!m_toolCatalog->save(&error)) { showActionFeedback(error,true); return; }
        bool returnFocus=false;
        {
            SymbolEditorDialog editor(document,id,m_canvas,m_toolCatalog,this);
            editor.exec();
            returnFocus=editor.closedWithFocus();
        }
        // Destroy the native modal before restoring Qt's focus bookkeeping.
        // activateWindow() alone does not repair a missing active QWidget, and
        // must not pull the artist away from another window or application.
        if(returnFocus&&isVisible()&&!QApplication::activeModalWidget()&&!QApplication::activePopupWidget()){
            if(!QApplication::activeWindow()&&QGuiApplication::applicationState()==Qt::ApplicationActive)
                QApplication::setActiveWindow(this);
            if(isActiveWindow())m_canvas->setFocus(Qt::OtherFocusReason);
        }
    });
    connect(symbols, &SymbolLibraryPanel::transformRequested, this, [this] {
        m_timeline->editAnimatedTransform();m_canvas->setFocus(Qt::OtherFocusReason);
    });
    for (auto *layers : findChildren<LayerPanel *>()) {
        layers->setProperty("symbolsEnabled",true);
        connect(layers,&LayerPanel::convertToSymbolRequested,this,[this,symbols,symbolDock](const QStringList &ids){
            if (!resolveCelWorkflows() || !m_timeline->prepareTimelineEdit()) return;
            symbolDock->show();symbolDock->raise();symbols->convertLayers(ids);
        });
        connect(layers,&LayerPanel::symbolActionRequested,this,[this,symbols,symbolDock](const QString &action){
            if (!resolveCelWorkflows() || !m_timeline->prepareTimelineEdit()) return;
            if(action=="edit"){symbolDock->show();symbolDock->raise();symbols->editCurrentInstance();return;}
            auto *document=m_canvas->document();if(!document->isOpen())return;
            const auto id=document->activeShot().layers[document->activeLayer()].id;QString error;
            if(action=="unique")makeSymbolInstanceUnique(document,id,&error);
            else if(action=="break-apart")breakApartSymbolInstance(document,id,&error);
            if(!error.isEmpty())showActionFeedback(error,true);
        });
    }
    auto *paintCheck = findChild<CelPaintCheckPanel *>();
    if (auto *colors = findChild<CharacterColorPanel *>()) {
        connect(colors, &CharacterColorPanel::paletteSelected, paintCheck,
            [paintCheck](const QString &characterId, const QString &variantId) {
                paintCheck->selectPalette(characterId, variantId);
            });
        connect(colors, &CharacterColorPanel::recolorRequested, this,
            [this](const QString &characterId, const QString &variantId) {
                if (!resolveCelWorkflows()) return;
                m_timeline->setPlaying(false);
                auto *document = m_canvas->document();
                if (!document->isOpen() || document->editingGestureActive() || document->layerPropertyEditActive()) {
                    showActionFeedback("Finish the current edit before recoloring drawings.", true); return;
                }
                {
                    CharacterRecolorDialog dialog(document, characterId, variantId, this);
                    dialog.exec();
                }
                activateWindow();
                m_canvas->setFocus(Qt::OtherFocusReason);
            });
    }
    auto *shading = findChild<CelShadingPanel *>();
    auto *lightTable = findChild<LightTablePanel *>();
    connect(paintCheck, &CelPaintCheckPanel::pendingChanged, this, [this, paintCheck](bool pending) {
        m_timeline->setPendingEdits(paintCheck, pending);
    });
    connect(m_timeline, &TimelineController::editCancellationRequested, paintCheck, &CelPaintCheckPanel::cancelEditing);
    m_timeline->setTransitionGuard([this] { return resolveCelWorkflows(); });
    connect(shading, &CelShadingPanel::activeChanged, lightTable, [this, lightTable](bool active) {
        if (active) { lightTable->setEditing(false); m_canvas->setCursor(Qt::CrossCursor); }
    });
    connect(lightTable, &LightTablePanel::editingChanged, shading, [this, shading](bool editing) {
        if (editing) { shading->setActive(false); m_canvas->setCursor(Qt::OpenHandCursor); }
    });
    auto *historyPanel = new HistoryPanel(m_canvas->document());
    dock("History", "historyPanel", historyPanel, Qt::RightDockWidgetArea);
    connect(historyPanel,&HistoryPanel::requestHistoryIndex,this,&MainWindow::navigateHistory);
    connect(historyPanel,&HistoryPanel::navigationBlocked,this,[this](const QString &message){showActionFeedback(message,true);});
    auto *actionsPanel = new AutoActionsPanel(m_autoActions);
    dock("Auto Actions", "autoActionsPanel", actionsPanel, Qt::RightDockWidgetArea);
    connect(actionsPanel,&AutoActionsPanel::playRequested,this,[this](const QString &id){
        if (m_canvas->document()->strokeActive() || m_autoActions->recording() || m_autoActions->playing()) {
            showActionFeedback("Finish the current stroke or recording before playing an action.",true);return;
        }
        if (m_timeline->prepareTimelineEdit()) m_autoActions->play(id);
    });
    const auto updateWorkflowAvailability=[this,historyPanel,actionsPanel]{
        const bool ready=!m_timeline->playing() && !m_canvas->scene3D()->editing()
            && !m_canvas->drawingTools()->active() && !m_timeline->hasPendingEdits();
        historyPanel->setInteractionEnabled(ready && !m_autoActions->playing());
        actionsPanel->setInteractionEnabled(ready);
    };
    connect(m_timeline,&TimelineController::changed,this,updateWorkflowAvailability);
    connect(m_canvas->drawingTools(),&DrawingToolController::changed,this,updateWorkflowAvailability);
    connect(m_canvas->scene3D(),&Scene3DController::changed,this,updateWorkflowAvailability);
    connect(m_autoActions,&AutoActions::stateChanged,this,updateWorkflowAvailability);
    updateWorkflowAvailability();
    auto *modelLibrary = dock("3D Library", "modelLibraryPanel", new ModelLibraryPanel(m_modelLibrary,m_canvas->scene3D()), Qt::LeftDockWidgetArea);
    modelLibrary->setFloating(true);modelLibrary->resize(320,480);
    auto *shots = new ShotsPanel(m_canvas->document(), [this] { return m_timeline->prepareTimelineEdit(); });
    auto *shotsDock = dock("Shots", "shotsPanel", shots, Qt::LeftDockWidgetArea);
    shotsDock->setFloating(true); shotsDock->resize(420, 540);
    connect(shots, &ShotsPanel::exportRequested, this, &MainWindow::exportShots);
    connect(m_canvas->scene3D(),&Scene3DController::sceneAdded,this,[this]{
        if (!m_compactDrawingWorkspace) {
            auto *properties=findChild<QDockWidget *>("propertiesPanel");properties->show();properties->raise();
        }
    });
    auto *manager = dock("Tool Management", "toolManagementPanel",
                         new ToolManagementPanel(m_toolCatalog), Qt::LeftDockWidgetArea);
    // Organization is occasional work. Start as a hidden floating panel so it
    // does not widen the artist's existing Tools column; saved layouts can dock it.
    manager->setFloating(true);
    manager->resize(360, 600);
    auto *brushManager = new BrushManagementPanel(m_canvas, m_brushLibrary, m_toolCatalog);
    auto *brushDock = dock("Brush Management", "brushManagementPanel", brushManager, Qt::LeftDockWidgetArea);
    brushDock->setFloating(true); brushDock->resize(360, 600);
    connect(brushManager, &BrushManagementPanel::advancedSettingsRequested, this, [this](const QString &id) {
        if (!m_canvas->hasProject() || !m_toolCatalog->activateTool(id)) return;
        if (auto *settings = findChild<BrushSettingsPanel *>())
            settings->openAdvancedSettings();
    });
    auto *tools = findChild<QDockWidget *>("toolsPanel");
    auto *brushPresets = findChild<QDockWidget *>("brushPresetPanel");
    splitDockWidget(tools, brushPresets, Qt::Vertical);
    auto *properties = findChild<QDockWidget *>("propertiesPanel");
    auto *layers = findChild<QDockWidget *>("layersPanel");
    addDockWidget(Qt::LeftDockWidgetArea, properties);
    splitDockWidget(brushPresets, properties, Qt::Vertical);
    auto *colors = findChild<QDockWidget *>("colorsPanel");
    splitDockWidget(colors, layers, Qt::Vertical);
    for (const auto *id : {"referencesPanel", "controlPanel", "symbolsPanel"})
        tabifyDockWidget(colors, findChild<QDockWidget *>(id));
    connect(m_timeline, &TimelineController::audioPropertiesRequested, this, [this] {
        auto *panel = findChild<QDockWidget *>("framePropertiesPanel"); panel->show(); panel->raise();
    });
    connect(findChild<QDockWidget *>("animationPanel"), &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (!visible) m_timeline->setPlaying(false);
        else if (m_canvas->hasProject() && !m_arrangingWorkspace) findChild<QDockWidget *>("framePropertiesPanel")->show();
    });
    for (const auto *id : {"effectsPanel", "celPaintCheckPanel", "celShadingPanel", "historyPanel", "autoActionsPanel", "framePropertiesPanel"})
        tabifyDockWidget(layers, findChild<QDockWidget *>(id));
    resizeDocks({tools, colors}, {220, 264}, Qt::Horizontal);
    resizeDocks({tools, brushPresets, properties}, {318, 228, 250}, Qt::Vertical);
    resizeDocks({colors, layers}, {370, 430}, Qt::Vertical);
    for (auto *panel : m_panels) panel->hide();
    m_toolboxColumn->hide();
    m_defaultLayout = saveState(3);
    menu->addSeparator();
    auto *workspaceReset = menu->addAction("Reset workspace");
    workspaceReset->setObjectName("resetWorkspaceAction");
    connect(workspaceReset, &QAction::triggered, this, &MainWindow::resetWorkspace);
}

void MainWindow::restoreWorkspace() {
    if (m_settings->contains("layoutV3")) {
        m_restoredLayout = restoreState(m_settings->value("layoutV3").toByteArray(), 3);
    } else if (m_settings->contains("layoutV2")) {
        // Restore the old Toolbox as a temporary dock so its split is removed
        // cleanly while keeping the artist's other panel placements intact.
        m_toolboxColumn->setObjectName("toolboxColumnMigration");
        auto *legacyToolbox = new QDockWidget("Toolbox", this);
        legacyToolbox->setObjectName("toolboxPanel");
        addDockWidget(Qt::LeftDockWidgetArea, legacyToolbox);
        legacyToolbox->hide();
        m_restoredLayout = restoreState(m_settings->value("layoutV2").toByteArray(), 2);
        const auto side = dockWidgetArea(legacyToolbox) == Qt::RightDockWidgetArea
            ? Qt::RightToolBarArea : Qt::LeftToolBarArea;
        const bool visible = !legacyToolbox->isHidden();
        removeDockWidget(legacyToolbox);
        delete legacyToolbox;
        m_toolboxColumn->setObjectName("toolboxPanel");
        if (m_restoredLayout) {
            addToolBar(side, m_toolboxColumn);
            m_toolboxColumn->setVisible(visible);
        }
    }
    if (!m_restoredLayout) restoreState(m_defaultLayout, 3);
    const bool legacyControlsVisible = drawingControlsVisible();
    m_compactDrawingWorkspace = m_restoredLayout && m_settings->value("workspace/compactDrawing", false).toBool();
    m_drawingDrawerOpen = m_settings->value("workspace/drawingDrawerOpen", legacyControlsVisible).toBool();
    m_drawingDrawerTabs->setCurrentIndex(std::clamp(m_settings->value("workspace/drawingDrawerPage", 0).toInt(), 0, 2));
    if (m_compactDrawingWorkspace) attachDrawingDrawer(true);
    const QString savedId=m_settings->value("workspace/activeId",m_compactDrawingWorkspace?"compact":"illustration").toString();
    m_activeWorkspaceId=m_restoredLayout?savedId:QString("illustration");
    if(!QStringList{"illustration","compact","anime","storyboard","animatic"}.contains(m_activeWorkspaceId)&&!m_workspaces->find(m_activeWorkspaceId))m_activeWorkspaceId.clear();
    if(m_restoredLayout)restoreInspectorPage(m_settings->value("workspace/inspectorPage").toString());
    refreshWorkspaceMenu();
    refreshDrawingWorkspaceControls();
    const auto side = toolBarArea(m_toolboxColumn);
    if (side != Qt::LeftToolBarArea && side != Qt::RightToolBarArea)
        addToolBar(Qt::LeftToolBarArea, m_toolboxColumn);
}

void MainWindow::resetWorkspace() {
    if (!canChangeDrawingWorkspace()) return;
    applyDrawingWorkspace(false);
    m_canvas->setRotation(0);
    m_canvas->fitSheet();
}
void MainWindow::showDrawingWorkspace() {
    if (m_drawingMode) { setDrawingMode(false); if (m_drawingMode) return; }
    m_toolboxColumn->show();
    for (const auto *id : {"toolsPanel", "brushPresetPanel", "propertiesPanel", "colorsPanel", "referencesPanel", "controlPanel", "layersPanel"})
        findChild<QDockWidget *>(id)->show();
    findChild<QDockWidget *>("toolsPanel")->raise();
    findChild<QDockWidget *>("propertiesPanel")->raise();
    findChild<QDockWidget *>("colorsPanel")->raise();
    findChild<QDockWidget *>("layersPanel")->raise();
    m_canvas->fitSheet();
}

bool MainWindow::drawingControlsVisible() const {
    if (m_compactDrawingWorkspace) return m_drawingDrawerOpen;
    for (const auto *id : {"toolsPanel", "brushPresetPanel", "propertiesPanel"})
        if (const auto *dock = findChild<QDockWidget *>(id); dock && !dock->isHidden()) return true;
    return false;
}

bool MainWindow::canChangeDrawingWorkspace() {
    if (m_changingDrawingMode || m_arrangingWorkspace) return false;
    // Even a status-style refresh can synchronously resize a fitted canvas.
    // Keep pen contact entirely quiet, like Tab drawing-mode's existing guard.
    if (m_canvas->document()->strokeActive()) return false;
    if (m_canvas->document()->editingGestureActive() || m_canvas->document()->layerPropertyEditActive() || m_timeline->hasPendingEdits() || pendingValueEntry(this) ||
        m_canvas->selectionGestureActive() || m_canvas->holdShapePending() ||
        m_canvas->document()->stampPlacementActive() || m_canvas->drawingTools()->active() ||
        m_canvas->scene3D()->editing() || QApplication::mouseButtons() != Qt::NoButton ||
        QApplication::activeModalWidget() || QApplication::activePopupWidget()) {
        showActionFeedback("Finish or cancel the current edit before changing the drawing layout.", true);
        refreshDrawingWorkspaceControls();
        return false;
    }
    return true;
}

void MainWindow::applyDrawingWorkspace(bool compact) {
    applyWorkspace(compact ? "compact" : "illustration");
}

void MainWindow::toggleDrawingControls() {
    if (!m_compactDrawingWorkspace || m_drawingMode || !canChangeDrawingWorkspace()) {
        if (m_compactDrawingControls) {
            const QSignalBlocker blocked(m_compactDrawingControls);
            m_compactDrawingControls->setChecked(drawingControlsVisible());
        }
        return;
    }
    const bool keyboardOpening = !m_drawingDrawerOpen && m_compactDrawingControls->hasFocus();
    m_drawingDrawerOpen = !m_drawingDrawerOpen;
    refreshDrawingDrawerVisibility();
    refreshDrawingWorkspaceControls();
    if (keyboardOpening) m_drawingDrawerTabs->setFocus(Qt::ShortcutFocusReason);
    else m_canvas->setFocus(Qt::ShortcutFocusReason);
}

QMenu *MainWindow::createPopupMenu() {
    auto *menu = QMainWindow::createPopupMenu();
    if (!menu) return nullptr;
    for (int index = 0; index < m_drawingDrawerPanelActions.size(); ++index) {
        auto *original = findChild<QDockWidget *>(drawingDockIds[index])->toggleViewAction();
        if (!menu->actions().contains(original)) continue;
        menu->insertAction(original, m_drawingDrawerPanelActions[index]); menu->removeAction(original);
    }
    return menu;
}

void MainWindow::createDrawingDrawer() {
    // The drawer is a sibling of the canvas, outside its layout. Besides keeping
    // the viewport stable, ignored pen events cannot bubble into the artwork.
    auto *drawer = new QFrame(this);
    m_drawingDrawer = drawer;
    drawer->setObjectName("drawingControlsDrawer");
    drawer->setAccessibleName("Drawing controls");
    drawer->setFrameShape(QFrame::StyledPanel);
    drawer->setStyleSheet(QString("QFrame#drawingControlsDrawer { background: %1; border: 1px solid %2; }")
        .arg(ui::panel, ui::border));
    drawer->setAutoFillBackground(true);
    auto *layout = new QVBoxLayout(drawer);
    layout->setContentsMargins(4, 4, 4, 4); layout->setSpacing(4);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    auto *header = new QHBoxLayout; header->setSpacing(4);
    auto *tabs = new DrawingDrawerTabs(drawer); m_drawingDrawerTabs = tabs;
    tabs->setObjectName("drawingDrawerTabs"); tabs->setAccessibleName("Drawing control pages");
    tabs->setProperty("compactDrawingTabs", true);
    tabs->setIconSize({ui::metrics::tabIcon, ui::metrics::tabIcon});
    tabs->setExpanding(true); tabs->setUsesScrollButtons(false); tabs->setElideMode(Qt::ElideRight);
    tabs->setMinimumWidth(0); tabs->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    const QStringList labels{"Tools", "Sizes", "Properties"};
    const QStringList icons{"brush", "circle", "sliders-horizontal"};
    for (int index = 0; index < labels.size(); ++index) {
        tabs->addTab(appIcon(icons[index]), labels[index]);
        tabs->setTabToolTip(index, labels[index]); tabs->setAccessibleTabName(index, labels[index]);
    }
    header->addWidget(tabs, 1);
    auto *close = new QToolButton(drawer);
    close->setObjectName("closeDrawingDrawer"); close->setIcon(appIcon("x"));
    close->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon});
    close->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
    close->setToolTip("Close drawing controls (Esc)"); close->setAccessibleName("Close drawing controls");
    close->setFocusPolicy(Qt::TabFocus); header->addWidget(close, 0, Qt::AlignTop);
    layout->addLayout(header);
    auto *pages = new DrawerPages(drawer); m_drawingDrawerStack = pages;
    pages->setObjectName("drawingDrawerPages"); pages->setMinimumSize(0, 0);
    layout->addWidget(pages, 1);
    for (const auto *id : drawingDockIds) {
        m_drawingDrawerContents.append(findChild<QDockWidget *>(id)->widget());
        auto *scroll = new QScrollArea(pages);
        scroll->setFrameShape(QFrame::NoFrame); scroll->setWidgetResizable(true);
        scroll->setMinimumSize(0, 0); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        pages->addWidget(scroll); m_drawingDrawerPages.append(scroll);
    }
    // Route the persistent Panels menu before QDockWidget can change layout.
    // Showing and immediately hiding an empty dock can still resize the canvas.
    for (int index = 0; index < 3; ++index) {
        auto *dock = findChild<QDockWidget *>(drawingDockIds[index]);
        auto *original = dock->toggleViewAction();
        auto *action = new QAction(original->icon(), original->text(), this);
        action->setObjectName(QString("drawingPanelAction_%1").arg(drawingDockIds[index]));
        action->setCheckable(true); action->setChecked(original->isChecked());
        action->setEnabled(original->isEnabled());
        m_drawingDrawerPanelActions.append(action);
        for (auto *menu : findChildren<QMenu *>()) {
            if (!menu->actions().contains(original)) continue;
            menu->insertAction(original, action); menu->removeAction(original);
        }
        connect(original, &QAction::changed, this, [this, original, action] {
            action->setEnabled(original->isEnabled());
            if (!m_compactDrawingWorkspace) action->setChecked(original->isChecked());
        });
        connect(action, &QAction::triggered, this, [this, original, index] {
            if (!m_compactDrawingWorkspace) { original->trigger(); return; }
            if (!m_drawingMode && canChangeDrawingWorkspace()) {
                m_drawingDrawerTabs->setCurrentIndex(index); m_drawingDrawerOpen = true;
            }
            refreshDrawingDrawerVisibility(); refreshDrawingWorkspaceControls();
        });
    }
    connect(tabs, &QTabBar::currentChanged, pages, &QStackedWidget::setCurrentIndex);
    connect(tabs, &QTabBar::currentChanged, this, [this] { refreshDrawingDrawerVisibility(); });
    connect(close, &QToolButton::clicked, this, &MainWindow::toggleDrawingControls);
    drawer->hide();
}

void MainWindow::attachDrawingDrawer(bool attached) {
    if (m_drawingDrawerAttached == attached) return;
    m_drawingDrawer->hide();
    for (int index = 0; index < 3; ++index) {
        auto *dock = findChild<QDockWidget *>(drawingDockIds[index]);
        auto *content = m_drawingDrawerContents[index].data();
        auto *page = m_drawingDrawerPages[index].data();
        if (attached) {
            dock->hide(); dock->setWidget(nullptr); page->setWidget(content);
        } else {
            page->takeWidget(); dock->setWidget(content);
        }
        content->show();
    }
    m_drawingDrawerAttached = attached;
}

void MainWindow::refreshDrawingDrawerGeometry() {
    if (!m_drawingDrawer || !m_drawingDrawerAttached) return;
    const QRect available(m_canvas->mapTo(this, QPoint()), m_canvas->size());
    const auto inner = available.adjusted(8, 8, -8, -8);
    m_drawingDrawer->setGeometry(QRect(inner.topLeft(), QSize(std::min(288, inner.width()), std::min(560, inner.height()))));
    if (m_drawingDrawer->isVisible()) m_drawingDrawer->raise();
}

void MainWindow::refreshDrawingDrawerVisibility() {
    if (!m_drawingDrawer) return;
    const bool visible = m_compactDrawingWorkspace && m_drawingDrawerAttached && m_drawingDrawerOpen &&
        !m_drawingMode && m_canvas->hasProject();
    if (visible) refreshDrawingDrawerGeometry();
    m_drawingDrawer->setVisible(visible);
    if (visible) m_drawingDrawer->raise();
    for (int index = 0; index < m_drawingDrawerPanelActions.size(); ++index) {
        auto *action = m_drawingDrawerPanelActions[index].data();
        const bool checked = m_compactDrawingWorkspace
            ? m_drawingDrawerOpen && m_drawingDrawerTabs->currentIndex() == index
            : findChild<QDockWidget *>(drawingDockIds[index])->toggleViewAction()->isChecked();
        action->setChecked(checked);
    }
}

void MainWindow::createDrawingWorkspaceControls() {
    createDrawingDrawer();
    auto *context = new QWidget;
    auto *row = new QHBoxLayout(context);
    row->setContentsMargins(0, 0, 0, 0); row->setSpacing(ui::metrics::controlGap);
    auto *summary = new DrawingControlsButton(context);
    m_compactDrawingControls = summary;
    summary->setObjectName("compactDrawingControls");
    summary->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    summary->setIconSize({ui::metrics::primaryIcon, ui::metrics::primaryIcon});
    summary->setProperty("compactDrawingAction", true);
    summary->setCheckable(true); summary->setFocusPolicy(Qt::TabFocus);
    summary->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    summary->setMinimumHeight(ui::metrics::actionTarget);
    row->addWidget(summary);
    auto *color = new QToolButton(context);
    m_compactDrawingColor = color;
    color->setObjectName("compactDrawingColor");
    color->setProperty("compactDrawingAction", true);
    color->setIconSize({ui::metrics::primaryIcon, ui::metrics::primaryIcon});
    color->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
    color->setFocusPolicy(Qt::TabFocus); row->addWidget(color);
    auto *action = new QWidgetAction(m_quickAccess);
    action->setObjectName("compactDrawingContext"); action->setDefaultWidget(context);
    m_compactDrawingContext = action;
    m_quickAccess->insertAction(m_quickAccess->findChild<QAction *>("quickAccessAdd"), action);
    connect(summary, &QToolButton::clicked, this, &MainWindow::toggleDrawingControls);
    connect(color, &QToolButton::clicked, this, [this] {
        if (!canChangeDrawingWorkspace()) return;
        auto *colors = findChild<QDockWidget *>("colorsPanel");
        colors->show(); colors->raise();
        m_canvas->setFocus(Qt::ShortcutFocusReason);
    });
    for (const auto *id : {"toolsPanel", "brushPresetPanel", "propertiesPanel"})
        connect(findChild<QDockWidget *>(id), &QDockWidget::visibilityChanged, this, [this] {
            if (!m_changingDrawingMode && !m_arrangingWorkspace) refreshDrawingWorkspaceControls();
        });
    connect(m_canvas, &Canvas::brushChanged, this, &MainWindow::refreshDrawingWorkspaceControls);
    connect(m_canvas, &Canvas::toolChanged, this, &MainWindow::refreshDrawingWorkspaceControls);
    connect(m_toolCatalog, &ToolCatalog::changed, this, &MainWindow::refreshDrawingWorkspaceControls);
    refreshDrawingWorkspaceControls();
}

void MainWindow::refreshDrawingWorkspaceControls() {
    if (!m_compactDrawingContext) return;
    m_compactDrawingContext->setVisible(m_compactDrawingWorkspace && !m_drawingMode);
    if (!m_compactDrawingWorkspace) return;
    const auto *tool = m_toolCatalog->tool(m_toolCatalog->activeToolId());
    const QString name = tool ? tool->name : "Drawing controls";
    const bool brush = m_canvas->tool() == Canvas::Tool::Brush || m_canvas->tool() == Canvas::Tool::Eraser;
    const QString size = brush ? QString::number(m_canvas->brush.size, 'f', 1).remove(QRegularExpression("\\.0$")) : QString();
    auto *summary = static_cast<DrawingControlsButton *>(m_compactDrawingControls);
    summary->setSummary(name, size);
    summary->setIcon(appIcon(tool ? tool->icon : QString("brush")));
    const QString help = QString("%1 drawing controls — %2%3")
        .arg(drawingControlsVisible() ? "Hide" : "Show", name, brush ? QString(" · %1 px").arg(size) : QString());
    summary->setToolTip(help); summary->setAccessibleName(help);
    { const QSignalBlocker blocked(summary); summary->setChecked(drawingControlsVisible()); }
    const auto color = m_canvas->brush.color;
    const QString colorName = color.name(QColor::HexArgb);
    if (m_compactDrawingColor->property("swatchColor").toString() != colorName) {
        QIcon icon;
        for (const qreal scale : {1., 2., 3.}) {
            QPixmap swatch(QSize(ui::metrics::primaryIcon, ui::metrics::primaryIcon) * scale);
            swatch.setDevicePixelRatio(scale); swatch.fill(Qt::transparent);
            QPainter painter(&swatch); painter.setPen(QColor(ui::muted)); painter.setBrush(color);
            painter.drawRect(QRect(1, 1, ui::metrics::primaryIcon - 3, ui::metrics::primaryIcon - 3));
            painter.end(); icon.addPixmap(swatch);
        }
        m_compactDrawingColor->setIcon(icon);
        m_compactDrawingColor->setProperty("swatchColor", colorName);
        const QString help = QString("Show Colors — foreground %1").arg(color.name());
        m_compactDrawingColor->setToolTip(help); m_compactDrawingColor->setAccessibleName(help);
    }
}
void MainWindow::openToolManagement(const QString &entryId, const QString &toolId) {
    auto *panel = findChild<QDockWidget *>("toolManagementPanel");
    if (!panel) return;
    if (auto *manager = qobject_cast<ToolManagementPanel *>(panel->widget()))
        manager->focusEntry(entryId, toolId);
    panel->show();
    panel->raise();
    panel->activateWindow();
}
void MainWindow::openBrushManagement() {
    auto *panel = findChild<QDockWidget *>("brushManagementPanel");
    if (!panel) return;
    panel->show(); panel->raise(); panel->activateWindow();
    if (auto *manager = qobject_cast<BrushManagementPanel *>(panel->widget())) {
        manager->focusBrush(m_toolCatalog->activeToolId()); manager->setFocus(Qt::OtherFocusReason);
    }
}
void MainWindow::importBrushes() {
    const auto path = QFileDialog::getOpenFileName(this, "Import Photoshop brushes", {}, "Photoshop brushes (*.abr)");
    if (path.isEmpty()) return;
    QString report, error;
    QStringList ids;
    if (!importAbrIntoLibrary(m_brushLibrary, path, &report, &error, &ids)) {
        QMessageBox::warning(this, "Could not import brushes", error); return;
    }
    QMessageBox message(QMessageBox::Information, "Brushes imported",
        QString("%1 brushes are available in Add Tools. Supported settings were mapped; see compatibility details for differences.").arg(ids.size()),
        QMessageBox::Ok, this);
    message.setDetailedText(report);
    message.exec();
}
void MainWindow::recoverFloatingPanels() {
    for (auto *panel : m_panels) {
        if (!panel->isFloating()) continue;
        bool reachable = false;
        const QRect titleArea(panel->frameGeometry().topLeft(), QSize(panel->width(), 32));
        for (auto *screen : QGuiApplication::screens())
            if (screen->availableGeometry().intersected(titleArea).width() > 80) reachable = true;
        if (!reachable) panel->move(frameGeometry().topLeft() + QPoint(60, 60));
    }
}

void MainWindow::newProject() {
    std::optional<Project> project;QString path,workspaceId,layerId;bool returnFocus=false;
    {
        ProjectDialog dialog(Project::create(),true,this);
        if(dialog.exec()==QDialog::Accepted){
            project=dialog.resultProject();path=dialog.filePath();
            workspaceId=dialog.requestedWorkspaceId();layerId=dialog.requestedLayerId();
        }
        returnFocus=dialog.closedWithFocus();
    }
    if(project&&confirmDiscard()){
        const bool replace=!QFileInfo::exists(path)||QMessageBox::question(this,"Replace project?",
            "This file already exists. Replace it with the new project?",QMessageBox::Yes|QMessageBox::No,
            QMessageBox::No)==QMessageBox::Yes;
        if(replace){
            QString error;
            if(saveProject(*project,path,&error)){
                displayProject(*project,path,false);
                if(!workspaceId.isEmpty())applyWorkspace(workspaceId);
                for(int i=0;i<m_canvas->shot().layers.size();++i)if(m_canvas->shot().layers[i].id==layerId){m_canvas->document()->selectLayer(i);break;}
            }else QMessageBox::warning(this,"Could not create project",error);
        }
    }
    // As with the Symbol editor, destruction of a native modal can clear Qt's
    // active-widget bookkeeping. Repair only focus this dialog owned, and do
    // not take focus back from another window/application chosen on close.
    if(returnFocus&&isVisible()&&!QApplication::activeModalWidget()&&!QApplication::activePopupWidget()){
        if(!QApplication::activeWindow()&&QGuiApplication::applicationState()==Qt::ApplicationActive)
            QApplication::setActiveWindow(this);
        if(isActiveWindow()){
            if(m_canvas->hasProject())m_canvas->setFocus(Qt::OtherFocusReason);
            else if(auto *create=m_welcome->findChild<QPushButton *>("newProjectButton"))create->setFocus(Qt::OtherFocusReason);
        }
    }
}
bool MainWindow::openProject(const QString &path) {
    if (!confirmDiscard()) return false;
    QString error;
    auto project = loadProject(path, &error);
    if (!project) { QMessageBox::warning(this, "Could not open project", error); return false; }
    bool recovered = false;
    const auto recoveryPath = path + ".recovery";
    if (QFileInfo::exists(recoveryPath) && QFileInfo(recoveryPath).lastModified() >= QFileInfo(path).lastModified()) {
        const auto recovery = loadProject(recoveryPath, &error);
        if (recovery) {
            const auto answer = QMessageBox::question(this, "Recover unsaved artwork?",
                "A newer recovery snapshot exists for this project. Restore it? Choose No to open the saved project.",
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
            if (answer == QMessageBox::Cancel) return false;
            if (answer == QMessageBox::Yes) { project = recovery; recovered = true; }
        } else QMessageBox::warning(this, "Recovery unavailable", "The recovery snapshot could not be read. The saved project will open.\n" + error);
    }
    displayProject(*project, path, recovered);
    return true;
}
void MainWindow::displayProject(const Project &project, const QString &path, bool dirty) {
    m_recoverySave->cancel();
    ++m_projectGeneration; ++m_documentRevision;
    m_timeline->setPlaying(false);
    // A new document must not inherit a transient failure from the previous
    // one. Ordinary tool and pointer changes still retain runtime warnings.
    m_actionFeedback.clear();
    m_feedbackIsBlockedAttempt = false;
    m_statusMessageTimer->stop();
    m_statusHighlightTimer->stop();
    m_path = path; m_dirty = dirty;
    qobject_cast<QStackedWidget *>(centralWidget())->setCurrentWidget(m_canvas);
    for (auto *action : m_projectActions) action->setEnabled(true);
    m_canvas->setProject(project);
    if (!m_restoredLayout) { showDrawingWorkspace(); m_restoredLayout = true; }
    if (dirty) m_canvas->document()->history()->resetClean();
    refreshDrawingDrawerVisibility();
    refreshStatusFeedback();
    updateTitle();
}
void MainWindow::editProject() {
    if (!resolveCelWorkflows()) return;
    m_timeline->setPlaying(false); m_timeline->finishEditing();
    if (!m_canvas->hasProject() || m_canvas->document()->editingGestureActive()) return;
    m_canvas->document()->commitLayerPropertyEdit();
    std::optional<Project> changed;bool returnFocus=false;
    {
        ProjectDialog dialog(m_canvas->project(),false,this,m_canvas->document()->activeShotIndex());
        if(dialog.exec()==QDialog::Accepted)changed=dialog.resultProject();
        returnFocus=dialog.closedWithFocus();
    }
    if(changed&&changed->toJson()!=m_canvas->project().toJson())m_canvas->document()->replaceProject(*changed,"Project settings");
    if(returnFocus&&isVisible()&&!QApplication::activeModalWidget()&&!QApplication::activePopupWidget()){
        if(!QApplication::activeWindow()&&QGuiApplication::applicationState()==Qt::ApplicationActive)
            QApplication::setActiveWindow(this);
        if(isActiveWindow())m_canvas->setFocus(Qt::OtherFocusReason);
    }
}
bool MainWindow::resolveCelWorkflows() {
    const auto *shading = findChild<CelShadingPanel *>();
    const auto *check = findChild<CelPaintCheckPanel *>();
    if (((shading && shading->pending()) || (check && check->pending())) && m_canvas->document()->strokeActive()) {
        showActionFeedback("Finish the stroke before resolving the coloring preview.", true);
        return false;
    }
    for (auto *panel : findChildren<CelShadingPanel *>())
        if (!panel->finishPersistence()) {
            showActionFeedback("Finish the current boundary edit before continuing; the shading draft is retained.", true); return false;
        }
    for (auto *panel : findChildren<LightTablePanel *>())
        if (!panel->finishPersistence()) {
            showActionFeedback(panel->busy() ? "The light-table snapshot is still being prepared. Try again when it is ready."
                : "Finish the current light-table edit before continuing.", true); return false;
        }
    for (auto *panel : findChildren<CelPaintCheckPanel *>())
        if (!panel->finishEditing()) return false;
    auto *document = m_canvas->document();
    if (document->isOpen() && hasDrawingAids(document->project().drawingAids)) {
        auto aids = document->project().drawingAids;
        aids.activeShotId = document->activeShotId(); aids.activeFrame = document->currentFrame();
        QString error;
        if (!document->setDrawingAids(aids, &error)) { showActionFeedback(error, true); return false; }
    }
    return true;
}
bool MainWindow::save(bool choosePath) {
    if (!resolveCelWorkflows()) return false;
    if (!m_canvas->hasProject()) return false;
    m_timeline->setPlaying(false); m_timeline->finishEditing();
    m_canvas->scene3D()->endEdit();
    m_canvas->document()->commitLayerPropertyEdit();
    if (m_canvas->document()->transformActive() || m_canvas->drawingTools()->transform()->active()) {
        statusBar()->showMessage("Apply or cancel the transform before saving.", 5000); return false;
    }
    m_canvas->drawingTools()->finishGesture();
    m_canvas->finishStroke();
    for (auto *references : findChildren<ReferencePanel *>()) if (!references->finishEditing()) return false;
    QString path = m_path;
    if (choosePath || path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, "Save movie project", m_path, "Toon Pro project (*.toonproj)");
        if (path.isEmpty()) return false;
        if (!path.endsWith(".toonproj", Qt::CaseInsensitive)) path += ".toonproj";
    }
    QString error;
    m_recoverySave->cancel();
    const auto revision = m_documentRevision, generation = m_projectGeneration;
    const auto originalPath = m_path;
    ProjectSave writer;
    QProgressDialog progress("Saving project…", "Cancel", 0, 0, this);
    progress.setObjectName("projectSaveProgress");progress.setWindowTitle("Save project");
    progress.setWindowModality(Qt::WindowModal);progress.setMinimumDuration(0);
    bool saved = false, cancelled = false;
    QEventLoop wait;
    connect(&progress, &QProgressDialog::canceled, &writer, &ProjectSave::cancel);
    connect(&writer, &ProjectSave::finished, &wait, [&](bool ok, bool wasCancelled, const QString &, const QString &reason) {
        saved = ok; cancelled = wasCancelled; error = reason; wait.quit();
    });
    writer.start(m_canvas->project(), path);progress.show();wait.exec();progress.close();
    if (!saved) { if (!cancelled) QMessageBox::warning(this, "Could not save project", error); return false; }
    if (!originalPath.isEmpty()) QFile::remove(originalPath + ".recovery");
    if (path != originalPath) QFile::remove(path + ".recovery");
    if (m_ownedRecoveryPath == originalPath + ".recovery") m_ownedRecoveryPath.clear();
    const bool exact = generation == m_projectGeneration && revision == m_documentRevision;
    if (generation == m_projectGeneration) {
        m_path = path;
        if (exact) {
            m_canvas->document()->history()->setClean();
            m_canvas->document()->markDrawingAidsSaved();
        }
        else m_canvas->document()->history()->resetClean();
        m_dirty = !exact; updateTitle();
    }
    statusBar()->showMessage(exact ? "Project saved" : "Snapshot saved; newer edits remain unsaved", 5000);
    return exact;
}
bool MainWindow::confirmDiscard() {
    if (!resolveCelWorkflows()) return false;
    m_timeline->setPlaying(false); m_timeline->finishEditing();
    m_canvas->scene3D()->endEdit();
    m_canvas->finishStroke();
    m_canvas->document()->commitLayerPropertyEdit();
    m_canvas->drawingTools()->finishGesture();
    if (!m_canvas->resolvePendingTransform()) return false;
    for (auto *references : findChildren<ReferencePanel *>()) if (!references->finishEditing()) return false;
    if (!m_dirty) return true;
    const auto answer = QMessageBox::warning(this, "Unsaved project changes", "Save changes before continuing?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Discard) { m_recoverySave->cancel(); QFile::remove(m_path + ".recovery"); return true; }
    return save();
}
void MainWindow::updateTitle() {
    setWindowModified(m_dirty);
    setWindowTitle(m_canvas->hasProject() ? QString("%1 — %2[*] — Toon Pro").arg(m_canvas->project().movie, m_canvas->shot().number) : "Toon Pro");
}
void MainWindow::exportImage(bool sheet) {
    if (!resolveCelWorkflows()) return;
    m_timeline->setPlaying(false); m_timeline->finishEditing();
    if (!m_canvas->hasProject() || m_canvas->document()->editingGestureActive()) return;
    m_canvas->document()->commitLayerPropertyEdit();
    const auto shot = m_canvas->shot();
    auto path = QFileDialog::getSaveFileName(this, sheet ? "Export production sheet" : "Export movie frame",
        sheet ? "production-sheet.png" : "movie-frame.png", "PNG image (*.png)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".png", Qt::CaseInsensitive)) path += ".png";
    QString error;
    const int frame = m_canvas->document()->currentFrame();
    const auto image = sheet ? renderSheet(m_canvas->project(), shot, true, frame)
        : renderMovieFrame(m_canvas->project(), frame, m_canvas->document()->activeShotIndex());
    if (image.isNull() && !keyframeRenderError().isEmpty()) {
        QMessageBox::warning(this, "Export failed", keyframeRenderError());
        return;
    }
    if (!savePng(image, path, &error)) QMessageBox::warning(this, "Export failed", error);
    else statusBar()->showMessage(sheet ? "Production sheet exported" : "Movie frame exported", 3500);
}
void MainWindow::exportAnimation() {
    if (!resolveCelWorkflows()) return;
    if (!m_canvas->hasProject()) return;
    m_timeline->setPlaying(false); m_timeline->finishEditing();
    m_canvas->finishStroke(); m_canvas->drawingTools()->finishGesture();
    m_canvas->scene3D()->endEdit(); m_canvas->document()->commitLayerPropertyEdit();
    if (!m_canvas->resolvePendingTransform()) return;
    const auto parent = QFileDialog::getExistingDirectory(this, "Choose parent folder for the PNG sequence");
    if (parent.isEmpty()) return;
    const auto snapshot = m_canvas->project();
    const int shotIndex = m_canvas->document()->activeShotIndex();
    QDir folder(parent);
    QString name = "Toon-Pro-frames";
    for (int suffix = 2; folder.exists(name); ++suffix) name = QString("Toon-Pro-frames-%1").arg(suffix);
    const auto destination = folder.filePath(name);
    QProgressDialog progress("Exporting animation frames…", "Cancel", 0, snapshot.shots[shotIndex].durationFrames, this);
    progress.setWindowModality(Qt::WindowModal); progress.setMinimumDuration(0);
    progress.setAutoClose(false); progress.setAutoReset(false);
    QString error;
    const bool exported = exportPngSequence(snapshot, shotIndex, 0, snapshot.shots[shotIndex].durationFrames - 1, destination,
        [&progress](int completed, int total) {
            progress.setMaximum(total); progress.setValue(completed);
            QApplication::processEvents();
            return !progress.wasCanceled();
        }, &error);
    progress.close();
    if (exported) statusBar()->showMessage("PNG sequence exported to " + destination, 10000);
    else if (progress.wasCanceled()) statusBar()->showMessage(error + " " + destination, 10000);
    else QMessageBox::warning(this, "Could not export animation", error);
}
void MainWindow::exportAudio() {
    if (!m_canvas->hasProject() || !m_timeline->prepareTimelineEdit()) return;
    AudioExportDialog dialog(m_canvas->project(), m_canvas->document()->activeShotIndex(),
        m_timeline->playbackFirstFrame(), m_timeline->playbackLastFrame(), m_timeline->playbackRangeEnabled(), this);
    connect(&dialog, &AudioExportDialog::exported, this, [this](const QString &path) {
        statusBar()->showMessage("Audio exported to " + path, 10000);
    });
    dialog.exec();
}
void MainWindow::exportVideo() {
    exportShots({});
}
void MainWindow::exportShots(const QStringList &shotIds) {
    if (!resolveCelWorkflows()) return;
    if (!m_canvas->hasProject()) return;
    m_timeline->setPlaying(false); m_timeline->finishEditing();
    m_canvas->finishStroke(); m_canvas->drawingTools()->finishGesture();
    m_canvas->scene3D()->endEdit(); m_canvas->document()->commitLayerPropertyEdit();
    if (!m_canvas->resolvePendingTransform()) return;
    VideoExportDialog dialog(m_canvas->project(), m_canvas->document()->activeShotIndex(), m_timeline->playbackFirstFrame(),
        m_timeline->playbackLastFrame(), m_timeline->playbackRangeEnabled(), this);
    if (!shotIds.isEmpty()) dialog.setQueuedShotIds(shotIds);
    connect(&dialog, &VideoExportDialog::exported, this, [this](const QString &path) {
        statusBar()->showMessage("Video exported to " + path, 10000);
    });
    dialog.exec();
}
void MainWindow::writeRecovery() {
    const auto *document = m_canvas->document();
    // Pixel and advanced transforms keep previews outside the committed project.
    // Brush/retouch strokes and editable-object drags mutate the live project
    // while a gesture is pending, so wait until those edits finish.
    const bool liveObjectEdit = document->externalEditActive()
        && !m_canvas->drawingTools()->transform()->active();
    if (m_recoverySave->busy() || !m_dirty || m_path.isEmpty() || !m_canvas->hasProject() || document->strokeActive() || (document->keyframeEditActive() || document->cameraEditActive() || document->effectsEditActive())
        || liveObjectEdit || document->layerPropertyEditActive()) return;
    for (auto *panel : findChildren<LightTablePanel *>()) if (panel->busy()) return;
    auto snapshot = m_canvas->project();
    if (hasDrawingAids(snapshot.drawingAids)) {
        snapshot.drawingAids.activeShotId = document->activeShotId();
        snapshot.drawingAids.activeFrame = document->currentFrame();
    }
    m_recoverySave->start(snapshot, m_path + ".recovery");
}
void MainWindow::closeEvent(QCloseEvent *event) {
    if (!confirmDiscard()) { event->ignore(); return; }
    m_settings->setValue("geometry", m_drawingMode ? m_normalGeometry : saveGeometry());
    m_toolSession->flush();
    m_settings->setValue("layoutV3", m_drawingMode ? m_normalLayout : saveState(3));
    m_settings->setValue("workspace/compactDrawing", m_compactDrawingWorkspace);
    m_settings->setValue("workspace/drawingDrawerOpen", m_drawingDrawerOpen);
    m_settings->setValue("workspace/drawingDrawerPage", m_drawingDrawerTabs->currentIndex());
    m_settings->setValue("workspace/activeId",m_activeWorkspaceId);
    if(auto *tabs=findChild<QTabBar *>("framePropertiesTabs"))m_settings->setValue("workspace/inspectorPage",tabs->tabData(tabs->currentIndex()).toString());
    if (auto *tabs = findChild<QTabBar *>("brushSettingsCategory"))
        m_settings->setValue("brush/category", tabs->currentIndex());
    m_settings->sync();
    event->accept();
}
} // namespace toon
