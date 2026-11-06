#include "project_assets_panel.h"
#include "document.h"
#include "icons.h"
#include "property_row.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include <QAudioOutput>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHideEvent>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QTabBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoSink>
#include <QtConcurrentRun>
#include <algorithm>

namespace toon {
class AssetPreview final : public QWidget {
public:
    AssetPreview() {
        setObjectName("projectAssetPreview"); setAccessibleName("Selected asset preview");
        setMinimumSize(80, 80); setMaximumHeight(190);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    void setImage(QImage image) { m_image = std::move(image); update(); }
    bool hasImage() const { return !m_image.isNull(); }
    QSize sizeHint() const override { return {240, 140}; }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.fillRect(rect(), QColor(ui::base));
        if (m_image.isNull()) {
            p.setPen(QColor(ui::muted)); p.drawText(rect(), Qt::AlignCenter, "Preview"); return;
        }
        const auto size = QSizeF(m_image.size()).scaled(QSizeF(this->size()) - QSizeF(8, 8), Qt::KeepAspectRatio);
        const QRectF target((width() - size.width()) / 2, (height() - size.height()) / 2, size.width(), size.height());
        p.setRenderHint(QPainter::SmoothPixmapTransform); p.drawImage(target, m_image);
    }
private:
    QImage m_image;
};
namespace {
class AssetStatus final : public QLabel {
public:
    AssetStatus() {
        setTextFormat(Qt::PlainText); setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setFixedHeight(qMax(ui::metrics::propertyHeight, fontMetrics().height() + 4));
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setPen(palette().color(QPalette::WindowText));
        p.drawText(contentsRect(), Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()));
    }
};
QToolButton *assetButton(const char *id, const char *icon, const QString &help) {
    auto *b = new QToolButton; b->setObjectName(id); b->setIcon(appIcon(icon));
    b->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon});
    b->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
    b->setAccessibleName(help); b->setToolTip(help); return b;
}
QString typeIcon(const QString &type) {
    if (type == "image") return "image";
    if (type == "audio") return "music";
    if (type == "video") return "clapperboard";
    if (type == "effect") return "sparkles";
    if (type == "model") return "box";
    return "folder-open";
}
QString bytesLabel(qint64 bytes) {
    if (bytes >= 1024LL * 1024 * 1024) return QString::number(bytes / (1024. * 1024 * 1024), 'f', 1) + " GB";
    if (bytes >= 1024 * 1024) return QString::number(bytes / (1024. * 1024), 'f', 1) + " MB";
    return QString::number(qMax<qint64>(1, bytes / 1024)) + " KB";
}
QStringList parseTags(const QString &text) {
    QStringList tags;
    for (const auto &part : text.split(',', Qt::SkipEmptyParts)) {
        const auto tag = part.trimmed();
        if (!tag.isEmpty() && !tags.contains(tag, Qt::CaseInsensitive)) tags.append(tag);
    }
    return tags;
}
struct AssetImportResult { StudioProject store; QString error; bool success = false, cancelled = false; };
struct AssetImageResult { QImage image; QString error; };
}
ProjectAssetsPanel::ProjectAssetsPanel(Document *document, std::function<bool()> prepareTransition, QWidget *parent)
    : QWidget(parent), m_document(document), m_prepareTransition(std::move(prepareTransition)) {
    setObjectName("projectAssetsPanel"); setMinimumWidth(230);
    auto *outer = new QVBoxLayout(this); outer->setContentsMargins(4, 4, 4, 4); outer->setSpacing(4);
    m_createPackage = new QPushButton(appIcon("folder-plus"), "Create project folder…");
    m_createPackage->setObjectName("assetsCreatePackage");
    m_createPackage->setToolTip("Save this artwork in a portable project folder with an asset library");
    outer->addWidget(m_createPackage);
    connect(m_createPackage, &QPushButton::clicked, this, &ProjectAssetsPanel::createPackageRequested);
    auto *bar = new QHBoxLayout; bar->setSpacing(4);
    m_import = assetButton("assetsImport", "plus", "Import images, video, audio and other assets…");
    m_cancel = assetButton("assetsCancelImport", "x", "Cancel asset import");
    m_newFolder = assetButton("assetsNewFolder", "folder-plus", "Create an asset folder…");
    m_reload = assetButton("assetsReload", "rotate-ccw", "Reload the asset catalog from disk");
    m_remove = assetButton("assetsRemove", "trash", "Move asset to Removed; its source and artwork copies are kept");
    m_restore = assetButton("assetsRestore", "undo-2", "Restore selected asset");
    bar->addWidget(m_import); bar->addWidget(m_cancel); bar->addWidget(m_newFolder); bar->addWidget(m_reload);
    bar->addStretch(); bar->addWidget(m_restore); bar->addWidget(m_remove); outer->addLayout(bar);
    m_tabs = new QTabBar; m_tabs->setObjectName("assetsCollectionTabs");
    m_tabs->setProperty("compactDrawingTabs", true); m_tabs->setDrawBase(false);
    m_tabs->setExpanding(false); m_tabs->setAccessibleName("Asset collections");
    m_tabs->addTab("Assets"); m_tabs->addTab("Removed");
    m_tabs->setTabToolTip(0, "Project asset library"); m_tabs->setAccessibleTabName(0, "Project assets");
    m_tabs->setTabToolTip(1, "Removed assets can be restored"); m_tabs->setAccessibleTabName(1, "Removed assets");
    outer->addWidget(m_tabs);
    auto *filters = new QHBoxLayout; filters->setSpacing(4);
    m_type = new QComboBox; m_type->setObjectName("assetsTypeFilter"); m_type->setAccessibleName("Asset type filter");
    m_type->addItem("All types", "");
    for (const auto &type : QStringList{"image", "video", "audio", "effect", "model", "file"})
        m_type->addItem(type.left(1).toUpper() + type.mid(1), type);
    m_type->setMinimumWidth(0); m_type->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_type->setMinimumContentsLength(4);
    m_search = new QLineEdit; m_search->setObjectName("assetsSearch"); m_search->setPlaceholderText("Search");
    m_search->setAccessibleName("Search asset names, tags and folders"); m_search->setClearButtonEnabled(true);
    filters->addWidget(m_type); filters->addWidget(m_search, 1); outer->addLayout(filters);
    m_folderFilter = new QComboBox; m_folderFilter->setObjectName("assetsFolderFilter");
    m_folderFilter->setAccessibleName("Asset folder filter"); outer->addWidget(m_folderFilter);
    m_list = new QTreeWidget; m_list->setObjectName("projectAssetsList"); m_list->setAccessibleName("Project assets");
    m_list->setColumnCount(2); m_list->setHeaderLabels({"Asset", "Type"}); m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true); m_list->setMinimumHeight(90);
    m_list->setIconSize({ui::metrics::layerThumbnail, ui::metrics::layerThumbnail});
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    outer->addWidget(m_list, 2);
    auto *scroll = new QScrollArea; scroll->setObjectName("assetsDetailsScroll");
    scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setMinimumHeight(80);
    m_details = new QWidget; auto *details = new QVBoxLayout(m_details);
    details->setContentsMargins(0, 0, 0, 0); details->setSpacing(4);
    m_preview = new AssetPreview; details->addWidget(m_preview);
    auto *media = new QHBoxLayout; media->setSpacing(4);
    m_play = assetButton("assetsPlay", "play", "Play or pause selected audio/video");
    m_stop = assetButton("assetsStop", "x", "Stop asset preview");
    m_use = assetButton("assetsUse", "plus", "Place a copy in the current drawing or audio timeline");
    m_attach = assetButton("assetsAttach", "link-2", "Attach this asset to the current shot's production references");
    media->addWidget(m_play); media->addWidget(m_stop); media->addStretch();
    media->addWidget(m_attach); media->addWidget(m_use); details->addLayout(media);
    m_info = new QLabel; m_info->setObjectName("assetsInfo"); m_info->setWordWrap(true); details->addWidget(m_info);
    auto field = [details](const QString &label, const char *id, int maximum) {
        auto *edit = new QLineEdit; edit->setObjectName(id); edit->setAccessibleName(label); edit->setMaxLength(maximum);
        details->addWidget(new PropertyRow(label, edit)); return edit;
    };
    m_name = field("Name", "assetName", 200); m_tags = field("Tags", "assetTags", 1000);
    m_tags->setPlaceholderText("Comma-separated tags");
    m_folder = new QComboBox; m_folder->setObjectName("assetFolder"); m_folder->setAccessibleName("Asset folder");
    m_folder->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); m_folder->setMinimumContentsLength(4);
    details->addWidget(new PropertyRow("Folder", m_folder));
    m_usageLabel = new QLabel("Attached to"); details->addWidget(m_usageLabel);
    m_usedIn = new QListWidget; m_usedIn->setObjectName("assetUsedIn");
    m_usedIn->setAccessibleName("Shots that reference this asset"); m_usedIn->setMaximumHeight(100);
    details->addWidget(m_usedIn); details->addStretch();
    scroll->setWidget(m_details); outer->addWidget(scroll, 3);
    m_status = new AssetStatus; m_status->setObjectName("assetsStatus"); outer->addWidget(m_status);
    connect(m_tabs, &QTabBar::currentChanged, this, &ProjectAssetsPanel::refreshList);
    connect(m_type, &QComboBox::currentIndexChanged, this, [this] { if (!m_refreshing) refreshList(); });
    connect(m_folderFilter, &QComboBox::currentIndexChanged, this, [this] { if (!m_refreshing) refreshList(); });
    connect(m_search, &QLineEdit::textChanged, this, &ProjectAssetsPanel::refreshList);
    connect(m_list, &QTreeWidget::currentItemChanged, this, &ProjectAssetsPanel::selectedChanged);
    connect(m_name, &QLineEdit::textEdited, this, [this] { if (!m_refreshing) m_dirty = true; });
    connect(m_tags, &QLineEdit::textEdited, this, [this] { if (!m_refreshing) m_dirty = true; });
    connect(m_name, &QLineEdit::editingFinished, this, [this] { commitDetails(); });
    connect(m_tags, &QLineEdit::editingFinished, this, [this] { commitDetails(); });
    connect(m_folder, &QComboBox::activated, this, [this] { if (!m_refreshing) { m_dirty = true; commitDetails(); } });
    connect(m_import, &QToolButton::clicked, this, [this] {
        if (!finishEditing()) return;
        const auto paths = QFileDialog::getOpenFileNames(this, "Import project assets", {}, "All files (*)");
        if (!paths.isEmpty()) importFiles(paths);
    });
    connect(m_cancel, &QToolButton::clicked, this, [this] {
        if (m_cancelImport) m_cancelImport->store(true); setStatus("Cancelling import…");
    });
    connect(m_newFolder, &QToolButton::clicked, this, &ProjectAssetsPanel::addFolder);
    connect(m_reload, &QToolButton::clicked, this, [this] {
        if (!m_store || m_importing) return;
        if (m_dirty && QMessageBox::question(this, "Reload assets?", "Discard the unfinished asset details and reload the catalog?",
            QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Discard) return;
        const auto root = m_store->rootPath(); setProjectRoot(root);
    });
    connect(m_remove, &QToolButton::clicked, this, &ProjectAssetsPanel::removeSelected);
    connect(m_restore, &QToolButton::clicked, this, &ProjectAssetsPanel::restoreSelected);
    connect(m_play, &QToolButton::clicked, this, &ProjectAssetsPanel::togglePlayback);
    connect(m_stop, &QToolButton::clicked, this, &ProjectAssetsPanel::stopPreview);
    connect(m_use, &QToolButton::clicked, this, &ProjectAssetsPanel::useSelected);
    connect(m_attach, &QToolButton::clicked, this, [this] { recordCurrentShotUse(); });
    connect(m_usedIn, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (item && prepareAction()) emit openShotRequested(item->data(Qt::UserRole).toString());
    });
    if (document) {
        connect(document, &Document::shotChanged, this, &ProjectAssetsPanel::refreshUsedIn);
        connect(document, &Document::loaded, this, &ProjectAssetsPanel::refreshUsedIn);
        connect(document, &Document::changed, this, [this] {
            if (m_document && m_document->strokeActive()) {
                if (m_player && m_player->playbackState() == QMediaPlayer::PlayingState) m_player->pause();
                return;
            }
            if (m_deferredPreview && isVisible()) { m_deferredPreview = false; showPreview(); }
        });
    }
    setProjectRoot({});
}
ProjectAssetsPanel::~ProjectAssetsPanel() { if (m_cancelImport) m_cancelImport->store(true); stopPreview(); }
QString ProjectAssetsPanel::projectRoot() const { return m_store ? m_store->rootPath() : QString(); }
void ProjectAssetsPanel::setProjectRoot(const QString &path) {
    if (m_cancelImport) m_cancelImport->store(true);
    ++m_generation; stopPreview(); m_selected.clear(); m_loadedAsset.reset(); m_dirty = false;
    QString error; m_store = path.isEmpty() ? std::nullopt : StudioProject::open(path, &error);
    m_createPackage->setVisible(!m_store);
    setStatus(error.isEmpty() && !m_store ? "A project folder keeps imported media together with your artwork." : error);
    refreshList();
}
void ProjectAssetsPanel::setStatus(const QString &message) {
    m_status->setText(message); m_status->setToolTip(message); m_status->setAccessibleName(message);
}
bool ProjectAssetsPanel::finishEditing() {
    if (m_importing) { setStatus("Finish or cancel the asset import before changing projects."); return false; }
    if (!commitDetails()) return false;
    stopPreview(); return true;
}
bool ProjectAssetsPanel::prepareAction() {
    if (!finishEditing()) return false;
    if (m_document && m_document->editingGestureActive()) { setStatus("Finish the canvas gesture first."); return false; }
    return !m_prepareTransition || m_prepareTransition();
}
bool ProjectAssetsPanel::commitDetails() {
    if (m_refreshing || !m_dirty) return true;
    if (!m_store || !m_loadedAsset || m_importing) return false;
    auto changed = *m_loadedAsset; changed.name = m_name->text().trimmed();
    changed.tags = parseTags(m_tags->text()); changed.folder = m_folder->currentText();
    if (changed == *m_loadedAsset) { m_dirty = false; return true; }
    QString error;
    if (!m_store->updateAsset(changed, &error)) { setStatus(error); return false; }
    m_loadedAsset = changed; m_dirty = false; setStatus("Asset details saved.");
    // Update the existing row without replacing the focused text editor.
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        auto *item = m_list->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == changed.id) {
            item->setText(0, changed.name); item->setToolTip(0, changed.name + "\n" + changed.folder + "\n" + changed.tags.join(", "));
        }
    }
    return true;
}
void ProjectAssetsPanel::refreshList() {
    if (m_refreshing || !commitDetails()) return;
    const QScopedValueRollback<bool> guard(m_refreshing, true);
    const QString filterFolder = m_folderFilter->currentData().toString();
    m_folderFilter->clear(); m_folderFilter->addItem("All folders", "");
    if (m_store) for (const auto &folder : m_store->folders()) m_folderFilter->addItem(folder, folder);
    m_folderFilter->setCurrentIndex(qMax(0, m_folderFilter->findData(filterFolder)));
    const QSignalBlocker block(m_list); m_list->clear();
    const auto query = m_search->text().trimmed(); const auto type = m_type->currentData().toString();
    if (m_store) for (const auto &asset : m_store->assets()) {
        if (asset.removed != (m_tabs->currentIndex() == 1)) continue;
        if (!type.isEmpty() && asset.type != type) continue;
        if (!filterFolder.isEmpty() && asset.folder != filterFolder && !asset.folder.startsWith(filterFolder + "/")) continue;
        if (!query.isEmpty() && !(asset.name + " " + asset.folder + " " + asset.tags.join(" ")).contains(query, Qt::CaseInsensitive)) continue;
        auto *item = new QTreeWidgetItem(m_list, {asset.name, asset.type});
        item->setData(0, Qt::UserRole, asset.id); item->setIcon(0, appIcon(typeIcon(asset.type)));
        item->setSizeHint(0, {0, qMax(32, fontMetrics().height() + 12)});
        item->setToolTip(0, asset.name + "\n" + asset.folder + "\n" + asset.tags.join(", "));
        if (asset.id == m_selected) m_list->setCurrentItem(item);
    }
    if (!m_list->currentItem() && m_list->topLevelItemCount()) m_list->setCurrentItem(m_list->topLevelItem(0));
    const auto id = m_list->currentItem() ? m_list->currentItem()->data(0, Qt::UserRole).toString() : QString();
    const bool changed = m_selected != id; m_selected = id;
    refreshDetails(changed || !m_loadedAsset); refreshActions();
}
void ProjectAssetsPanel::selectedChanged() {
    if (m_refreshing) return;
    const auto id = m_list->currentItem() ? m_list->currentItem()->data(0, Qt::UserRole).toString() : QString();
    if (!commitDetails()) {
        const QSignalBlocker block(m_list);
        for (int i = 0; i < m_list->topLevelItemCount(); ++i)
            if (m_list->topLevelItem(i)->data(0, Qt::UserRole).toString() == m_selected) m_list->setCurrentItem(m_list->topLevelItem(i));
        return;
    }
    m_selected = id; refreshDetails(); refreshActions();
}
void ProjectAssetsPanel::refreshDetails(bool reloadEditors) {
    const auto *asset = m_store ? m_store->asset(m_selected) : nullptr;
    m_details->setEnabled(asset && !m_importing);
    if (reloadEditors) {
        const QScopedValueRollback<bool> guard(m_refreshing, true);
        m_loadedAsset = asset ? std::optional<StudioAsset>(*asset) : std::nullopt; m_dirty = false;
        m_name->setText(asset ? asset->name : QString()); m_tags->setText(asset ? asset->tags.join(", ") : QString());
        m_folder->clear(); if (m_store) m_folder->addItems(m_store->folders());
        m_folder->setCurrentIndex(asset ? m_folder->findText(asset->folder) : -1);
        m_info->setText(asset ? asset->type + " · " + bytesLabel(asset->bytes) : QString());
        showPreview();
    }
    refreshUsedIn();
}
void ProjectAssetsPanel::refreshActions() {
    const auto *asset = m_store ? m_store->asset(m_selected) : nullptr;
    const bool enabled = m_store.has_value() && !m_importing;
    m_import->setEnabled(enabled); m_newFolder->setEnabled(enabled); m_reload->setEnabled(enabled);
    m_cancel->setVisible(m_importing); m_cancel->setEnabled(m_importing);
    m_tabs->setEnabled(enabled); m_type->setEnabled(enabled); m_search->setEnabled(enabled); m_folderFilter->setEnabled(enabled);
    m_list->setEnabled(enabled); m_details->setEnabled(enabled && asset);
    m_remove->setVisible(m_tabs->currentIndex() == 0); m_remove->setEnabled(enabled && asset && !asset->removed);
    m_restore->setVisible(m_tabs->currentIndex() == 1); m_restore->setEnabled(enabled && asset && asset->removed);
    const bool media = asset && (asset->type == "video" || asset->type == "audio");
    m_play->setVisible(media); m_stop->setVisible(media);
    m_use->setVisible(asset && (asset->type == "image" || asset->type == "audio"));
    m_use->setEnabled(enabled && asset && !asset->removed && m_document && m_document->isOpen());
    m_attach->setEnabled(enabled && asset && !asset->removed && m_document && m_document->isOpen());
    m_name->setReadOnly(asset && asset->removed); m_tags->setReadOnly(asset && asset->removed);
    m_folder->setEnabled(enabled && asset && !asset->removed);
}
void ProjectAssetsPanel::refreshUsedIn() {
    m_usedIn->clear();
    const auto *asset = m_store ? m_store->asset(m_selected) : nullptr;
    if (asset && m_document && m_document->isOpen()) for (const auto &id : asset->shotIds) {
        auto shot = std::find_if(m_document->project().shots.cbegin(), m_document->project().shots.cend(), [&](const Shot &s) { return s.id == id; });
        auto *item = new QListWidgetItem(shot == m_document->project().shots.cend() ? "Unavailable shot" : shot->number + " · " + shot->name, m_usedIn);
        item->setData(Qt::UserRole, id); item->setToolTip("Open the attached shot");
        if (shot == m_document->project().shots.cend()) item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    }
    m_usageLabel->setText("Attached to · " + QString::number(m_usedIn->count()));
    refreshActions();
}
void ProjectAssetsPanel::importFiles(const QStringList &paths) {
    if (paths.isEmpty() || !m_store || !prepareAction()) return;
    m_importing = true; m_cancelImport = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_cancelImport; const auto generation = m_generation;
    const auto folder = m_folderFilter->currentData().toString();
    auto store = *m_store; auto *watcher = new QFutureWatcher<AssetImportResult>(this);
    connect(watcher, &QFutureWatcher<AssetImportResult>::finished, this, [this, watcher, generation] {
        auto result = watcher->result(); watcher->deleteLater();
        m_importing = false; m_cancelImport.reset();
        if (generation != m_generation) { refreshActions(); emit importFinished(false, "Previous project import finished."); return; }
        if (result.success) {
            m_store = std::move(result.store); m_tabs->setCurrentIndex(0);
            if (!m_store->assets().isEmpty()) m_selected = m_store->assets().last().id;
            m_loadedAsset.reset(); refreshList();
        }
        const QString message = result.success ? "Assets imported into the project folder." : result.cancelled ? "Asset import cancelled." : result.error;
        setStatus(message); refreshActions(); emit importFinished(result.success, message);
    });
    watcher->setFuture(QtConcurrent::run([store = std::move(store), paths, folder, cancel]() mutable {
        AssetImportResult result; result.store = std::move(store);
        result.success = result.store.importFiles(paths, folder, {}, &result.error, [cancel] { return cancel->load(); });
        result.cancelled = cancel->load() && !result.success; return result;
    }));
    setStatus("Importing assets…"); refreshActions();
}
void ProjectAssetsPanel::addFolder() {
    if (!m_store || !prepareAction()) return;
    bool accepted = false;
    const auto name = QInputDialog::getText(this, "New asset folder", "Folder name", QLineEdit::Normal, {}, &accepted);
    if (!accepted) return;
    QString error;
    if (!m_store->createFolder(name.trimmed(), &error)) { setStatus(error); return; }
    refreshList(); m_folderFilter->setCurrentIndex(m_folderFilter->findData(name.trimmed()));
    setStatus("Asset folder created.");
}
void ProjectAssetsPanel::removeSelected() {
    if (!m_store || m_selected.isEmpty() || !prepareAction()) return;
    QString error;
    if (!m_store->removeAsset(m_selected, &error)) { setStatus(error); return; }
    m_loadedAsset.reset(); refreshList(); setStatus("Moved to Removed. The original file and placed artwork are kept.");
}
void ProjectAssetsPanel::restoreSelected() {
    if (!m_store || m_selected.isEmpty() || !prepareAction()) return;
    QString error;
    if (!m_store->restoreAsset(m_selected, &error)) { setStatus(error); return; }
    m_loadedAsset.reset(); refreshList(); setStatus("Asset restored.");
}
bool ProjectAssetsPanel::recordCurrentShotUse() {
    if (!m_store || !m_document || !m_document->isOpen() || !prepareAction()) return false;
    const auto *asset = m_store->asset(m_selected); if (!asset || asset->removed) return false;
    auto changed = *asset; if (changed.shotIds.contains(m_document->activeShotId())) return true;
    changed.shotIds.append(m_document->activeShotId()); QString error;
    if (!m_store->updateAsset(changed, &error)) { setStatus(error); return false; }
    m_loadedAsset = changed; refreshUsedIn(); setStatus("Asset attached to " + m_document->activeShot().number + "."); return true;
}
void ProjectAssetsPanel::useSelected() {
    if (!m_store || !prepareAction()) return;
    const auto *asset = m_store->asset(m_selected); if (!asset || asset->removed) return;
    QString error; const auto path = m_store->assetPath(asset->id, &error);
    if (path.isEmpty()) { setStatus(error); return; }
    if (asset->type == "image") emit placeImageRequested(path);
    else if (asset->type == "audio") emit placeAudioRequested(path);
}
void ProjectAssetsPanel::stopPreview() {
    ++m_previewGeneration; m_deferredPreview = false;
    if (m_player) { m_player->stop(); m_player->setSource({}); }
    m_play->setIcon(appIcon("play"));
}
void ProjectAssetsPanel::showPreview() {
    stopPreview(); m_preview->setImage({});
    if (!m_store) return;
    const auto *asset = m_store->asset(m_selected); if (!asset) return;
    if (m_document && m_document->strokeActive()) { m_deferredPreview = true; return; }
    QString error; const auto path = m_store->assetPath(asset->id, &error);
    if (path.isEmpty()) { setStatus(error); return; }
    if (asset->type != "image") return;
    const auto generation = m_previewGeneration;
    auto *watcher = new QFutureWatcher<AssetImageResult>(this);
    connect(watcher, &QFutureWatcher<AssetImageResult>::finished, this, [this, watcher, generation] {
        const auto result = watcher->result(); watcher->deleteLater();
        if (generation != m_previewGeneration) return;
        if (m_document && m_document->strokeActive()) { m_deferredPreview = true; return; }
        m_preview->setImage(result.image); if (!result.error.isEmpty()) setStatus(result.error);
    });
    watcher->setFuture(QtConcurrent::run([path] {
        AssetImageResult result; QImageReader reader(path); reader.setAutoTransform(true);
        const QSize dimensions = reader.size();
        if (!dimensions.isValid() || dimensions.width() > 65536 || dimensions.height() > 65536) {
            result.error = "An image preview is unavailable for this file."; return result;
        }
        reader.setScaledSize(dimensions.scaled({960, 640}, Qt::KeepAspectRatio)); result.image = reader.read();
        if (result.image.isNull()) result.error = "Could not preview image: " + reader.errorString(); return result;
    }));
}
void ProjectAssetsPanel::togglePlayback() {
    if (!m_store || m_importing || (m_document && m_document->strokeActive())) return;
    const auto *asset = m_store->asset(m_selected);
    if (!asset || (asset->type != "audio" && asset->type != "video")) return;
    QString error; const auto path = m_store->assetPath(asset->id, &error);
    if (path.isEmpty()) { setStatus(error); return; }
    if (!m_player) {
        m_player = new QMediaPlayer(this); m_player->setObjectName("projectAssetMediaPlayer");
        m_audio = new QAudioOutput(this); m_audio->setVolume(.65);
        m_video = new QVideoSink(this); m_player->setAudioOutput(m_audio); m_player->setVideoSink(m_video);
        connect(m_video, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
            if (isVisible() && (!m_document || !m_document->strokeActive())) m_preview->setImage(frame.toImage());
        });
        connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &message) { setStatus("Preview unavailable: " + message); });
        connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
            m_play->setIcon(appIcon(state == QMediaPlayer::PlayingState ? "pause" : "play"));
        });
    }
    const QUrl source = QUrl::fromLocalFile(path);
    if (m_player->source() != source) m_player->setSource(source);
    if (m_player->playbackState() == QMediaPlayer::PlayingState) m_player->pause(); else m_player->play();
}
void ProjectAssetsPanel::hideEvent(QHideEvent *event) { stopPreview(); QWidget::hideEvent(event); }
void ProjectAssetsPanel::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    if (m_deferredPreview || !m_preview->hasImage()) showPreview();
}
} // namespace toon
