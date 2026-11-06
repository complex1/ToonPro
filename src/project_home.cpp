#include "project_home.h"
#include "icons.h"
#include "ui_metrics.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTabBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace toon {
namespace {
constexpr auto recentKey = "studio/recentProjectsV1";
QString identity(const QString &path) {
    const QFileInfo info(path);
    return info.exists() ? info.canonicalFilePath() : QDir::cleanPath(info.absoluteFilePath());
}
QString kindLabel(const QString &key) {
    if (key == "drawing-only") return "Drawing only";
    if (key == "series") return "Series";
    return "Film";
}
QToolButton *tool(const char *id, const char *icon, const QString &help) {
    auto *b = new QToolButton;
    b->setObjectName(id); b->setIcon(appIcon(icon));
    b->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon});
    b->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
    b->setAccessibleName(help); b->setToolTip(help); return b;
}
}
ProjectHome::ProjectHome(QSettings *settings, QWidget *parent) : QWidget(parent), m_settings(settings) {
    setObjectName("projectHome");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20); layout->setSpacing(12);
    auto *heading = new QLabel("Your projects"); heading->setObjectName("sectionLabel"); layout->addWidget(heading);
    auto *actions = new QHBoxLayout; actions->setSpacing(6);
    auto *create = new QPushButton(appIcon("file-plus"), "New project");
    create->setObjectName("newProjectButton"); create->setDefault(true);
    create->setAccessibleName("New project"); create->setToolTip("Create a Film, Series or Drawing-only project");
    auto *open = new QPushButton(appIcon("folder-open"), "Open project…");
    open->setObjectName("homeOpenFile"); open->setToolTip("Open a saved .toonproj or project marker");
    auto *folder = new QPushButton(appIcon("folder-open"), "Open folder…");
    folder->setObjectName("homeOpenFolder"); folder->setToolTip("Open a portable Toon Pro project folder");
    m_resume = new QPushButton("Continue working");
    m_resume->setObjectName("homeResume"); m_resume->hide();
    actions->addWidget(create); actions->addWidget(open); actions->addWidget(folder); actions->addStretch();
    layout->addLayout(actions); layout->addWidget(m_resume, 0, Qt::AlignLeft);
    m_tabs = new QTabBar; m_tabs->setObjectName("homeProjectTabs");
    m_tabs->setProperty("compactDrawingTabs", true); m_tabs->setDrawBase(false);
    m_tabs->setExpanding(false); m_tabs->setAccessibleName("Project collections");
    m_tabs->addTab("Recent"); m_tabs->addTab("Pinned"); m_tabs->addTab("Archived");
    for (int i = 0; i < m_tabs->count(); ++i) {
        m_tabs->setTabToolTip(i, m_tabs->tabText(i) + " projects");
        m_tabs->setAccessibleTabName(i, m_tabs->tabText(i) + " projects");
    }
    layout->addWidget(m_tabs);
    m_list = new QTreeWidget; m_list->setObjectName("homeProjects");
    m_list->setAccessibleName("Saved projects"); m_list->setColumnCount(3);
    m_list->setHeaderLabels({"Project", "Type", "Location"});
    m_list->setRootIsDecorated(false); m_list->setUniformRowHeights(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon});
    m_list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_list->setTextElideMode(Qt::ElideMiddle); layout->addWidget(m_list, 1);
    auto *footer = new QHBoxLayout; footer->setSpacing(4);
    m_open = new QPushButton(appIcon("folder-open"), "Open"); m_open->setObjectName("homeOpenSelected");
    m_pin = tool("homePin", "pin", "Pin or unpin the selected project"); m_pin->setCheckable(true);
    m_archive = new QPushButton("Archive"); m_archive->setObjectName("homeArchive");
    m_archive->setToolTip("Move this entry between Recent and Archived; project files stay unchanged");
    m_remove = tool("homeRemove", "trash", "Remove from this list; project files are kept");
    footer->addWidget(m_open); footer->addWidget(m_pin); footer->addWidget(m_archive);
    footer->addStretch(); footer->addWidget(m_remove); layout->addLayout(footer);
    m_status = new QLabel; m_status->setObjectName("homeStatus");
    m_status->setWordWrap(true); m_status->setMinimumHeight(fontMetrics().height() + 4); layout->addWidget(m_status);
    connect(create, &QPushButton::clicked, this, &ProjectHome::newProjectRequested);
    connect(open, &QPushButton::clicked, this, &ProjectHome::openFileRequested);
    connect(folder, &QPushButton::clicked, this, &ProjectHome::openFolderRequested);
    connect(m_resume, &QPushButton::clicked, this, &ProjectHome::resumeRequested);
    connect(m_tabs, &QTabBar::currentChanged, this, &ProjectHome::refresh);
    connect(m_list, &QTreeWidget::currentItemChanged, this, &ProjectHome::updateActions);
    connect(m_open, &QPushButton::clicked, this, [this] {
        const auto path = selectedPath();
        if (!path.isEmpty() && QFileInfo::exists(path)) emit projectRequested(path);
        else updateActions();
    });
    connect(m_list, &QTreeWidget::itemActivated, this, [this] { m_open->click(); });
    connect(m_pin, &QToolButton::clicked, this, [this] { changeEntry(0); });
    connect(m_archive, &QPushButton::clicked, this, [this] { changeEntry(1); });
    connect(m_remove, &QToolButton::clicked, this, [this] { changeEntry(2); });
    load(); refresh();
}
void ProjectHome::load() {
    if (!m_settings) return;
    const auto bytes = m_settings->value(recentKey).toByteArray();
    if (bytes.isEmpty()) return;
    if (bytes.size() > 1024 * 1024) { m_readOnly = true; return; }
    QJsonParseError error; const auto json = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !json.isArray() || json.array().size() > 256) {
        m_readOnly = true; return;
    }
    for (const auto &value : json.array()) {
        const auto o = value.toObject(); const auto path = o["path"].toString();
        if (path.isEmpty() || !QDir::isAbsolutePath(path)) { m_readOnly = true; m_entries.clear(); return; }
        m_entries.append({path, o["title"].toString(), o["kind"].toString(),
                          o["opened"].toString().toLongLong(), o["pinned"].toBool(), o["archived"].toBool()});
    }
}
void ProjectHome::store() {
    if (!m_settings || m_readOnly) return;
    QJsonArray items;
    for (const auto &entry : m_entries)
        items.append(QJsonObject{{"path", entry.path}, {"title", entry.title}, {"kind", entry.kind},
                                 {"opened", QString::number(entry.opened)}, {"pinned", entry.pinned}, {"archived", entry.archived}});
    m_settings->setValue(recentKey, QJsonDocument(items).toJson(QJsonDocument::Compact)); m_settings->sync();
}
QString ProjectHome::selectedPath() const {
    return m_list->currentItem() ? m_list->currentItem()->data(0, Qt::UserRole).toString() : QString();
}
void ProjectHome::rememberProject(const QString &path, const QString &title, const QString &kindKey) {
    if (path.trimmed().isEmpty() || m_readOnly) return;
    const auto key = identity(path);
    auto entry = std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry &e) { return identity(e.path) == key; });
    if (entry == m_entries.end()) {
        if (m_entries.size() >= 256) {
            auto oldest = std::min_element(m_entries.begin(), m_entries.end(), [](const Entry &a, const Entry &b) {
                if (a.pinned != b.pinned) return !a.pinned;
                return a.opened < b.opened;
            });
            if (oldest->pinned) { m_status->setText("The project list is full. Remove an entry to remember another project."); return; }
            m_entries.erase(oldest);
        }
        m_entries.append({key, title, kindKey, QDateTime::currentMSecsSinceEpoch(), false, false});
    } else {
        entry->path = key; entry->title = title; entry->kind = kindKey; entry->opened = QDateTime::currentMSecsSinceEpoch();
        entry->archived = false;
    }
    store(); refresh();
}
void ProjectHome::setCurrentProject(const QString &title) {
    m_resume->setVisible(!title.isEmpty());
    m_resume->setText(title.isEmpty() ? "Continue working" : "Continue · " + title);
}
void ProjectHome::refresh() {
    const auto current = selectedPath(); const QSignalBlocker block(m_list); m_list->clear();
    auto entries = m_entries;
    std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        if (a.pinned != b.pinned) return a.pinned; return a.opened > b.opened;
    });
    for (const auto &entry : entries) {
        const int tab = m_tabs->currentIndex();
        if ((tab == 2) != entry.archived || (tab == 1 && !entry.pinned)) continue;
        const bool missing = !QFileInfo::exists(entry.path);
        auto *item = new QTreeWidgetItem(m_list, {entry.title.isEmpty() ? QFileInfo(entry.path).fileName() : entry.title,
                                                kindLabel(entry.kind), (missing ? "Missing · " : "") + QDir::toNativeSeparators(entry.path)});
        item->setData(0, Qt::UserRole, entry.path);
        item->setIcon(0, appIcon(entry.pinned ? "pin" : entry.kind == "drawing-only" ? "image" : "clapperboard"));
        item->setSizeHint(0, QSize(0, qMax(32, fontMetrics().height() + 12)));
        for (int i = 0; i < 3; ++i) item->setToolTip(i, item->text(0) + "\n" + (missing ? "Location unavailable\n" : "") + entry.path);
        if (entry.path == current) m_list->setCurrentItem(item);
    }
    if (!m_list->currentItem() && m_list->topLevelItemCount()) m_list->setCurrentItem(m_list->topLevelItem(0));
    updateActions();
}
void ProjectHome::updateActions() {
    const auto path = selectedPath();
    const auto entry = std::find_if(m_entries.cbegin(), m_entries.cend(), [&](const Entry &e) { return e.path == path; });
    const bool selected = entry != m_entries.cend();
    m_open->setEnabled(selected && QFileInfo::exists(path));
    m_pin->setEnabled(selected && !m_readOnly); m_remove->setEnabled(selected && !m_readOnly);
    m_archive->setEnabled(selected && !m_readOnly);
    const QSignalBlocker block(m_pin); m_pin->setChecked(selected && entry->pinned);
    m_archive->setText(selected && entry->archived ? "Restore" : "Archive");
    m_status->setText(m_readOnly ? "The saved project list could not be read. It was preserved; Open project remains available."
                     : selected && !QFileInfo::exists(path) ? "Project location is unavailable. Open it from its new location to add it again."
                     : m_list->topLevelItemCount() == 0 ? "No projects in this collection." : QString());
}
void ProjectHome::changeEntry(int operation) {
    if (m_readOnly) return;
    const auto path = selectedPath();
    auto entry = std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry &e) { return e.path == path; });
    if (entry == m_entries.end()) return;
    if (operation == 0) entry->pinned = !entry->pinned;
    else if (operation == 1) entry->archived = !entry->archived;
    else m_entries.erase(entry);
    store(); refresh();
}
void ProjectHome::showEvent(QShowEvent *event) { QWidget::showEvent(event); refresh(); }
} // namespace toon
