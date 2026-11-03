#include "model_library_panel.h"
#include "model_library.h"
#include "scene3d_controller.h"
#include "scene_model_io.h"
#include "icons.h"
#include "ui_theme.h"
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QProgressDialog>
#include <QSignalBlocker>
#include <QTabBar>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>

namespace toon {
namespace {
constexpr QSize thumbnailSize{24, 24};
constexpr int iconSourceRole = Qt::UserRole + 1;

class LibraryStatus final : public QLabel {
public:
    LibraryStatus() {
        setTextFormat(Qt::PlainText);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setFixedHeight(qMax(28, fontMetrics().height()));
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(contentsRect(), Qt::AlignLeading | Qt::AlignVCenter,
                         fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()));
    }
};

QToolButton *button(const char *id, const char *icon, const QString &help, bool checkable = false) {
    auto *result = new QToolButton; result->setObjectName(id); result->setIcon(appIcon(icon)); result->setIconSize({18, 18});
    result->setFixedSize(28, 28); result->setCheckable(checkable); result->setToolTip(help); result->setAccessibleName(help); return result;
}
QString modelSymbol(const ModelLibraryEntry &entry) {
    if (entry.builtIn) {
        if (entry.name.contains("sphere", Qt::CaseInsensitive)) return "circle";
        if (entry.name.contains("plane", Qt::CaseInsensitive)) return "rectangle-horizontal";
        if (entry.name.contains("mannequin", Qt::CaseInsensitive)) return "hand";
        if (entry.name.contains("building", Qt::CaseInsensitive)) return "panels-top-left";
    }
    return "box";
}
QIcon modelIcon(const ModelLibraryEntry &entry) {
    if (entry.thumbnail.isNull()) return appIcon(modelSymbol(entry));
    QIcon icon;
    const QSizeF size = QSizeF(entry.thumbnail.size()).scaled(QSizeF(thumbnailSize), Qt::KeepAspectRatio);
    const QRectF target(QPointF((thumbnailSize.width() - size.width()) / 2., (thumbnailSize.height() - size.height()) / 2.), size);
    for (const qreal ratio : {1., 2., 3.}) {
        QPixmap pixmap(thumbnailSize * ratio); pixmap.setDevicePixelRatio(ratio); pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap); painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(target, entry.thumbnail); painter.end(); icon.addPixmap(pixmap);
    }
    return icon;
}
struct ImportResult { Model3DPtr model; QString error; };
}
ModelLibraryPanel::ModelLibraryPanel(ModelLibrary *library, Scene3DController *controller, QWidget *parent)
    : QWidget(parent), m_library(library), m_controller(controller) {
    setObjectName("modelLibraryContent"); setMinimumSize(240, 220);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(4, 4, 4, 4); layout->setSpacing(4);
    m_tabs = new QTabBar; m_tabs->setObjectName("modelLibraryCollections"); m_tabs->setAccessibleName("3D model collections");
    m_tabs->setDocumentMode(true); m_tabs->setExpanding(true);
    m_tabs->addTab("Starter"); m_tabs->addTab("My Assets");
    m_tabs->setTabToolTip(0, "Built-in 3D reference models"); m_tabs->setTabToolTip(1, "Models in your global library");
    m_tabs->setAccessibleTabName(0, "Starter models"); m_tabs->setAccessibleTabName(1, "My assets"); layout->addWidget(m_tabs);
    auto *searchRow = new QHBoxLayout; searchRow->setSpacing(4);
    m_tags = new QComboBox; m_tags->setObjectName("modelLibraryTagFilter"); m_tags->setAccessibleName("Filter models by tag");
    m_tags->setToolTip("Filter models by tag"); m_tags->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_tags->setMinimumContentsLength(3); m_tags->setMinimumWidth(0); m_tags->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_search = new QLineEdit; m_search->setObjectName("modelLibrarySearch"); m_search->setPlaceholderText("Search");
    m_search->setAccessibleName("Search 3D models by name or tag"); m_search->setToolTip("Search model names and tags");
    m_search->setClearButtonEnabled(true); m_search->setMinimumWidth(0);
    for (auto *clear : m_search->findChildren<QToolButton *>()) {
        clear->setObjectName("modelLibrarySearchClear"); clear->setToolTip("Clear model search"); clear->setAccessibleName("Clear model search");
    }
    m_favoritesOnly = button("modelLibraryFavoritesOnly", "pin", "Show favorite models only", true);
    searchRow->addWidget(m_tags, 2); searchRow->addWidget(m_search, 3); searchRow->addWidget(m_favoritesOnly); layout->addLayout(searchRow);
    m_list = new QListWidget; m_list->setObjectName("modelLibraryModels"); m_list->setAccessibleName("3D reference models");
    m_list->setIconSize(thumbnailSize); m_list->setUniformItemSizes(true); m_list->setMinimumHeight(80);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); m_list->setTextElideMode(Qt::ElideRight);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu); layout->addWidget(m_list, 1);
    auto *actions = new QHBoxLayout; actions->setSpacing(4);
    m_import = button("import3DModel", "folder-open", "Import a GLB or glTF model into My Assets…");
    m_add = button("add3DModel", "plus", "Place the selected model in the active 3D scene");
    m_favorite = button("favorite3DModel", "pin", "Favorite the selected model", true);
    m_remove = button("remove3DModel", "trash", "Remove this model from the global library; placed models are kept");
    m_menu = button("modelLibraryOptions", "ellipsis", "Model options");
    actions->addWidget(m_import); actions->addWidget(m_add); actions->addWidget(m_favorite); actions->addStretch();
    actions->addWidget(m_remove); actions->addWidget(m_menu); layout->addLayout(actions);
    m_status = new LibraryStatus; m_status->setObjectName("modelLibraryStatus");
    m_status->setStyleSheet(QString("color: %1;").arg(ui::muted)); layout->addWidget(m_status);
    connect(library, &ModelLibrary::changed, this, &ModelLibraryPanel::refresh);
    connect(m_tabs, &QTabBar::currentChanged, this, &ModelLibraryPanel::refresh);
    connect(m_search, &QLineEdit::textChanged, this, &ModelLibraryPanel::refresh);
    connect(m_tags, &QComboBox::currentIndexChanged, this, [this] { if (!m_refreshing) refresh(); });
    connect(m_favoritesOnly, &QToolButton::toggled, this, &ModelLibraryPanel::refresh);
    connect(m_list, &QListWidget::currentItemChanged, this, &ModelLibraryPanel::refreshActions);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] { addSelected(); });
    connect(m_add, &QToolButton::clicked, this, &ModelLibraryPanel::addSelected);
    connect(m_remove, &QToolButton::clicked, this, &ModelLibraryPanel::removeSelected);
    connect(m_favorite, &QToolButton::clicked, this, [this] {
        const auto *entry = m_library->entry(selectedId()); if (!entry) return;
        QString error; if (!m_library->setFavorite(entry->id, !entry->favorite, &error)) setStatus(error);
        refreshActions();
    });
    connect(m_import, &QToolButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, "Import 3D model", {}, "glTF models (*.glb *.gltf)");
        if (!path.isEmpty()) importFileAsync(path);
    });
    auto showMenu = [this](QPoint global) {
        const auto *entry = m_library->entry(selectedId()); if (!entry) return;
        QMenu menu(this); auto *place = menu.addAction(appIcon("plus"), "Add to scene");
        auto *favorite = menu.addAction(appIcon("pin"), entry->favorite ? "Remove favorite" : "Add favorite");
        auto *tags = menu.addAction(appIcon("sliders-horizontal"), "Edit tags…");
        auto *rename = menu.addAction("Rename…"); rename->setEnabled(!entry->builtIn && m_library->writable());
        auto *remove = menu.addAction(appIcon("trash"), "Remove from library"); remove->setEnabled(!entry->builtIn && m_library->writable());
        tags->setEnabled(m_library->writable()); favorite->setEnabled(m_library->writable());
        const auto id = entry->id; const bool wasFavorite = entry->favorite;
        auto *chosen = menu.exec(global);
        if (chosen == place) addSelected(); else if (chosen == tags) editTags();
        else if (chosen == rename) renameSelected(); else if (chosen == remove) removeSelected();
        else if (chosen == favorite) { QString error; if (!m_library->setFavorite(id, !wasFavorite, &error)) setStatus(error); }
    };
    connect(m_menu, &QToolButton::clicked, this, [this, showMenu] { showMenu(m_menu->mapToGlobal(QPoint(0, m_menu->height()))); });
    connect(m_list, &QListWidget::customContextMenuRequested, this, [this, showMenu](QPoint position) {
        auto *item = m_list->itemAt(position); if (!item) return;
        m_list->setCurrentItem(item); showMenu(m_list->viewport()->mapToGlobal(position));
    });
    connect(controller, &Scene3DController::warning, this, &ModelLibraryPanel::setStatus);
    refresh(); if (m_list->count()) m_list->setCurrentRow(0);
}
ModelLibraryPanel::~ModelLibraryPanel() { if (m_cancelImport) m_cancelImport->store(true); }
QString ModelLibraryPanel::selectedId() const { return m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole).toString() : QString(); }
void ModelLibraryPanel::setStatus(const QString &message) {
    m_status->setText(message); m_status->setToolTip(message); m_status->setAccessibleName(message);
}
void ModelLibraryPanel::refreshActions() {
    const auto *entry = m_library->entry(selectedId()); const bool writable = m_library->writable();
    m_import->setEnabled(writable && !m_importing); m_add->setEnabled(entry != nullptr);
    m_favorite->setEnabled(writable && entry); m_favorite->setChecked(entry && entry->favorite);
    m_remove->setEnabled(writable && entry && !entry->builtIn); m_menu->setEnabled(entry != nullptr);
}
void ModelLibraryPanel::refresh() {
    if (m_refreshing) return;
    m_refreshing = true;
    const auto selected = selectedId(); const auto tag = m_tags->currentData().toString();
    QStringList tags;
    for (const auto &entry : m_library->entries()) for (const auto &value : entry.tags)
        if (!tags.contains(value, Qt::CaseInsensitive)) tags << value;
    tags.sort(Qt::CaseInsensitive); if (!tag.isEmpty() && !tags.contains(tag, Qt::CaseInsensitive)) tags << tag;
    { const QSignalBlocker block(m_tags); m_tags->clear(); m_tags->addItem("All tags", QString());
      for (const auto &value : tags) { m_tags->addItem(value, value); m_tags->setItemData(m_tags->count() - 1, value, Qt::ToolTipRole); }
      m_tags->setCurrentIndex(qMax(0, m_tags->findData(tag))); }
    m_tags->setToolTip(tag.isEmpty() ? "Filter models by tag" : "Filter models by tag: " + tag);
    const auto query = m_search->text().trimmed();
    QVector<const ModelLibraryEntry *> matches;
    for (const auto &entry : m_library->entries()) {
        if (entry.builtIn != (m_tabs->currentIndex() == 0)) continue;
        if (m_favoritesOnly->isChecked() && !entry.favorite) continue;
        if (!tag.isEmpty() && !entry.tags.contains(tag, Qt::CaseInsensitive)) continue;
        if (!query.isEmpty() && !entry.name.contains(query, Qt::CaseInsensitive)
            && !entry.tags.join(' ').contains(query, Qt::CaseInsensitive)) continue;
        matches << &entry;
    }
    bool sameRows = m_list->count() == matches.size();
    if (sameRows) for (int i = 0; i < matches.size(); ++i) if (m_list->item(i)->data(Qt::UserRole).toString() != matches[i]->id) { sameRows = false; break; }
    const QSignalBlocker listBlock(m_list);
    if (!sameRows) {
        m_list->clear(); for (const auto *entry : matches) {
            auto *item = new QListWidgetItem(m_list); item->setData(Qt::UserRole, entry->id);
            item->setSizeHint({0, qMax(32, m_list->fontMetrics().height() + 8)});
        }
    }
    for (int i = 0; i < matches.size(); ++i) {
        const auto &entry = *matches[i]; auto *item = m_list->item(i);
        item->setText(entry.name); item->setData(Qt::AccessibleTextRole, entry.name);
        const QString iconSource = entry.thumbnail.isNull() ? modelSymbol(entry) : QString::number(entry.thumbnail.cacheKey());
        if (item->data(iconSourceRole).toString() != iconSource) {
            item->setIcon(modelIcon(entry)); item->setData(iconSourceRole, iconSource);
        }
        const QString description = entry.name + (entry.favorite ? " · Favorite" : "")
            + (entry.tags.isEmpty() ? QString() : "\n" + entry.tags.join(", "))
            + (entry.builtIn ? "\nStarter model" : "\nMy Assets") + "\nDouble-click to add to the scene";
        item->setToolTip(description); item->setData(Qt::AccessibleDescriptionRole, description);
        if (entry.id == selected) m_list->setCurrentItem(item);
    }
    if (matches.isEmpty()) setStatus("No models match these filters.");
    else if (m_status->text() == "No models match these filters.") setStatus({});
    m_refreshing = false; refreshActions();
}
void ModelLibraryPanel::addSelected() {
    const auto *entry = m_library->entry(selectedId()); if (!entry) return;
    const auto name = entry->name;
    if (m_controller->addModel(entry->model, name)) setStatus("Added " + name + " to the scene.");
}
void ModelLibraryPanel::editTags() {
    const auto *entry = m_library->entry(selectedId()); if (!entry) return;
    const auto id = entry->id; bool accepted = false;
    const auto tags = QInputDialog::getText(this, "Model tags", "Tags, separated by commas", QLineEdit::Normal, entry->tags.join(", "), &accepted);
    if (!accepted) return;
    QString error; if (!m_library->setTags(id, tags.split(',', Qt::SkipEmptyParts), &error)) setStatus(error); else setStatus({});
}
void ModelLibraryPanel::renameSelected() {
    const auto *entry = m_library->entry(selectedId()); if (!entry || entry->builtIn) return;
    const auto id = entry->id; bool accepted = false;
    const auto name = QInputDialog::getText(this, "Rename model", "Name", QLineEdit::Normal, entry->name, &accepted);
    if (!accepted) return;
    QString error; if (!m_library->rename(id, name, &error)) setStatus(error); else setStatus({});
}
void ModelLibraryPanel::removeSelected() {
    const auto *entry = m_library->entry(selectedId()); if (!entry || entry->builtIn) return;
    const auto id = entry->id;
    QString error; if (!m_library->remove(id, &error)) setStatus(error);
    else setStatus("Removed from the library. Placed models are kept.");
}
void ModelLibraryPanel::importFileAsync(const QString &path) {
    if (m_importing || path.isEmpty()) return;
    if (!m_library->writable()) { setStatus("The 3D library is read-only."); emit importFinished(false, m_status->text()); return; }
    m_importing = true; m_cancelImport = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = m_cancelImport; auto result = std::make_shared<ImportResult>();
    auto *progress = new QProgressDialog("Importing 3D model…", "Cancel", 0, 0, this);
    progress->setObjectName("modelImportProgress"); progress->setWindowTitle("Import 3D model");
    progress->setWindowModality(Qt::NonModal); progress->setMinimumDuration(0);
    progress->setAutoClose(false); progress->setAutoReset(false);
    connect(progress, &QProgressDialog::canceled, this, [this, cancelled] {
        cancelled->store(true); setStatus("Import cancelled.");
    });
    progress->show(); refreshActions(); setStatus("Importing " + QFileInfo(path).fileName() + "…");
    auto *worker = QThread::create([path, result, cancelled] {
        if (!cancelled->load()) result->model = importModel3D(path, &result->error);
    });
    connect(worker, &QThread::finished, this, [this, result, cancelled, progress = QPointer<QProgressDialog>(progress), path] {
        if (progress) { progress->disconnect(this); progress->close(); progress->deleteLater(); }
        m_importing = false; m_cancelImport.reset();
        if (cancelled->load()) { setStatus("Import cancelled."); refreshActions(); emit importFinished(false, "Import cancelled."); return; }
        QString error = result->error;
        const auto id = result->model ? m_library->addModel(result->model, QFileInfo(path).completeBaseName(), &error) : QString();
        if (id.isEmpty()) {
            if (error.isEmpty()) error = "Could not import this model.";
            setStatus(error); refreshActions(); emit importFinished(false, error); return;
        }
        const auto *entry = m_library->entry(id);
        m_tabs->setCurrentIndex(entry && entry->builtIn ? 0 : 1);
        m_search->clear(); m_tags->setCurrentIndex(0); m_favoritesOnly->setChecked(false); refresh();
        for (int i = 0; i < m_list->count(); ++i) if (m_list->item(i)->data(Qt::UserRole).toString() == id) { m_list->setCurrentRow(i); break; }
        setStatus("Imported. Add the model to a scene when ready."); refreshActions(); emit importFinished(true, {});
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}
} // namespace toon
