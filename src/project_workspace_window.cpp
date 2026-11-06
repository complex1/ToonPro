#include "project_workspace_window.h"
#include "canvas.h"
#include "dashboard_gallery.h"
#include "document.h"
#include "drawing_tool_controller.h"
#include "icons.h"
#include "main_window.h"
#include "sheet.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include "value_slider.h"
#include "workspace_setup_widgets.h"
#include <QApplication>
#include <QAudioOutput>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMediaPlayer>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVideoWidget>
#include <QtConcurrentRun>
#include <atomic>
namespace toon {
namespace {
QPushButton *button(const QString &label, const char *id, const char *icon, QBoxLayout *row) {
    auto *b = new QPushButton(appIcon(icon), label);
    b->setObjectName(id);
    b->setAccessibleName(label);
    b->setToolTip(label);
    b->setIconSize({16, 16});
    b->setMinimumHeight(28);
    row->addWidget(b);
    return b;
}
QString kindName(const QString &kind) {
    if (kind == "movie")
        return "Movie";
    if (kind == "drawing")
        return "Drawing";
    if (kind == "animation")
        return "Animation";
    if (kind == "image")
        return "Image";
    if (kind == "video")
        return "Video";
    if (kind == "audio")
        return "Audio";
    return kind;
}
// Heavy storage operations finish on a worker; the modal only pumps UI and
// cancellation. ProjectStore creates SQL connections on that worker thread.
bool job(QWidget *parent, const QString &title,
         const std::function<bool(QString *, const std::function<bool()> &)> &work, QString *error) {
    QPointer<QWidget> previousFocus = QApplication::focusWidget();
    struct Result {
        bool ok = false;
        QString error;
    } result;
    {
        auto cancel = std::make_shared<std::atomic_bool>(false);
        QProgressDialog progress(title, "Cancel", 0, 0, parent);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.setAutoClose(false);
        progress.setAutoReset(false);
        QFutureWatcher<Result> watcher;
        QEventLoop wait;
        QObject::connect(&progress, &QProgressDialog::canceled, parent, [cancel] { cancel->store(true); });
        QObject::connect(&watcher, &QFutureWatcher<Result>::finished, &wait, &QEventLoop::quit);
        watcher.setFuture(QtConcurrent::run([work, cancel] {
            Result r;
            r.ok = work(&r.error, [cancel] { return cancel->load(); });
            return r;
        }));
        progress.show();
        if (!watcher.isFinished())
            wait.exec();
        progress.close();
        result = watcher.result();
    }
    // Restore only the editor this dialog owned, after native destruction.
    if (previousFocus && !QApplication::activeModalWidget() && parent->window()->isActiveWindow())
        previousFocus->setFocus(Qt::OtherFocusReason);
    if (error)
        *error = result.error;
    return result.ok;
}
} // namespace
ProjectWorkspaceWindow::ProjectWorkspaceWindow(const QString &settingsPath, QWidget *parent)
    : QMainWindow(parent) {
    setObjectName("projectWorkspaceWindow");
    setWindowTitle("Toon Pro");
    resize(1440, 960);
    setMinimumSize(960, 680);
    const auto path =
        settingsPath.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/management.ini"
            : settingsPath;
    QDir().mkpath(QFileInfo(path).absolutePath());
    m_settings = std::make_unique<QSettings>(path, QSettings::IniFormat);
    m_navigation = new QToolBar("Editor navigation", this);
    m_navigation->setObjectName("managementNavigation");
    m_navigation->setMovable(false);
    m_navigation->setFloatable(false);
    m_navigation->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_navigation->setIconSize({16, 16});
    auto *home = m_navigation->addAction(appIcon("grid-2x2"), "Projects");
    connect(home, &QAction::triggered, this, &ProjectWorkspaceWindow::showHome);
    auto *back = m_navigation->addAction(appIcon("folder-open"), "Project dashboard");
    connect(back, &QAction::triggered, this, [this] {
        if (prepareNavigation()) {
            m_tabs->setCurrentIndex(0);
            m_home->hide();
            m_projectPage->show();
            refreshDashboard();
            updateNavigation();
        }
    });
    auto *create = new QAction(appIcon("file-plus"), "New project", this);
    create->setObjectName("managedNewProject");
    create->setShortcut(QKeySequence::New);
    addAction(create);
    connect(create, &QAction::triggered, this, &ProjectWorkspaceWindow::newProjectDialog);
    auto *open = new QAction(appIcon("folder-open"), "Open project", this);
    open->setShortcut(QKeySequence::Open);
    addAction(open);
    connect(open, &QAction::triggered, this, [this] {
        auto p = QFileDialog::getExistingDirectory(this, "Open Toon Pro project folder");
        if (!p.isEmpty()) {
            QString e;
            if (!openManagedProject(p, &e))
                feedback(e, true);
        }
    });
    m_backupAction = new QAction("Back up saved project…", this);
    connect(m_backupAction, &QAction::triggered, this, [this] {
        if (!m_store || m_busy)
            return;
        const auto p = QFileDialog::getSaveFileName(this, "New backup folder", m_store->root() + "-backup",
                                                    "Project folder (*.toonstudio)");
        if (p.isEmpty())
            return;
        m_busy = true;
        QString e;
        const auto store = m_store;
        const bool ok = job(
            this, "Backing up saved project…",
            [store, p](QString *err, const auto &cancel) { return store->backup(p, err, cancel); }, &e);
        m_busy = false;
        feedback(ok ? "Saved project backed up; unsaved tab edits remain in their tabs." : e, !ok);
    });
    m_locationAction = new QAction("Default project location…", this);
    connect(m_locationAction, &QAction::triggered, this, [this] {
        const auto p = QFileDialog::getExistingDirectory(this, "Default folder for new projects",
                                                         m_settings->value("workspace").toString());
        if (!p.isEmpty())
            m_settings->setValue("workspace", p);
    });
    m_tabs = new QTabWidget;
    m_tabs->setObjectName("documentTabs");
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(false);
    m_tabs->setTabBarAutoHide(true);
    m_tabs->setDocumentMode(true);
    // Keep management navigation beside document tabs, saving a whole toolbar row.
    m_tabs->setCornerWidget(m_navigation, Qt::TopLeftCorner);
    m_tabs->tabBar()->setExpanding(false);
    m_tabs->tabBar()->setUsesScrollButtons(true);
    m_tabs->tabBar()->setElideMode(Qt::ElideRight);
    m_tabs->setIconSize({16, 16});
    m_tabs->installEventFilter(this);
    m_tabs->tabBar()->installEventFilter(this);
    setCentralWidget(m_tabs);
    m_dashboard = new QWidget;
    auto *dashboardLayout = new QVBoxLayout(m_dashboard);
    dashboardLayout->setContentsMargins(0, 0, 0, 0);
    m_tabs->addTab(m_dashboard, appIcon("grid-2x2"), "Projects");
    m_tabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
    auto style = managementStyle();
    m_home = new QWidget;
    m_home->setObjectName("projectsDashboard");
    m_home->setStyleSheet(style);
    auto *homeLayout = new QVBoxLayout(m_home);
    homeLayout->setContentsMargins(48, 34, 40, 24);
    homeLayout->setSpacing(24);
    auto *homeHeader = new QHBoxLayout;
    auto *homeTitles = new QVBoxLayout;
    auto *heading = new QLabel("Projects");
    auto font = heading->font();
    font.setPointSize(32);
    font.setWeight(QFont::DemiBold);
    heading->setFont(font);
    homeTitles->addWidget(heading);
    auto *sub = new QLabel("Pick up where you left off.");
    sub->setProperty("muted", true);
    homeTitles->addWidget(sub);
    homeHeader->addLayout(homeTitles, 1);
    auto *homeOpen = button("Open project", "homeOpenProject", "folder-open", homeHeader);
    connect(homeOpen, &QPushButton::clicked, open, &QAction::trigger);
    auto *homeCreate = button("New project", "homeNewProject", "plus", homeHeader);
    homeCreate->setProperty("primary", true);
    connect(homeCreate, &QPushButton::clicked, create, &QAction::trigger);
    homeLayout->addLayout(homeHeader);
    auto *homeFilters = new QHBoxLayout;
    m_homeCollections = new QTabBar;
    m_homeCollections->setObjectName("homeCollections");
    m_homeCollections->setDrawBase(false);
    for (const auto &s : {"All projects", "Pinned", "Archived"})
        m_homeCollections->addTab(s);
    homeFilters->addWidget(m_homeCollections);
    homeFilters->addStretch();
    m_homeSearch = new QLineEdit;
    m_homeSearch->setObjectName("homeSearch");
    m_homeSearch->setAccessibleName("Search projects");
    m_homeSearch->setPlaceholderText("Search projects");
    m_homeSearch->setClearButtonEnabled(true);
    m_homeSearch->setMinimumWidth(160);
    m_homeSearch->setMaximumWidth(280);
    homeFilters->addWidget(m_homeSearch, 1);
    m_homeSort = new QComboBox;
    m_homeSort->addItems({"Last opened", "Name A–Z"});
    m_homeSort->setAccessibleName("Sort projects");
    homeFilters->addWidget(m_homeSort);
    const auto iconButton = [](QHBoxLayout *row, const char *icon, const QString &name) {
        auto *b = new QToolButton;
        b->setIcon(appIcon(icon));
        b->setIconSize({18, 18});
        b->setFixedSize(36, 36);
        b->setCheckable(true);
        b->setToolTip(name);
        b->setAccessibleName(name);
        row->addWidget(b);
        return b;
    };
    auto *homeGrid = iconButton(homeFilters, "grid-2x2", "Project grid view"),
         *homeList = iconButton(homeFilters, "list", "Project list view");
    auto *homeViews = new QButtonGroup(this);
    homeViews->addButton(homeGrid);
    homeViews->addButton(homeList);
    homeGrid->setChecked(true);
    homeLayout->addLayout(homeFilters);
    m_projects = new DashboardGallery;
    m_projects->setObjectName("managedProjects");
    m_projects->setAccessibleName("Projects");
    m_projects->setSelectionMode(QAbstractItemView::SingleSelection);
    homeLayout->addWidget(m_projects, 1);
    auto *homeFooter = new QHBoxLayout;
    m_homeStatus = new QLabel;
    m_homeStatus->setProperty("muted", true);
    homeFooter->addWidget(m_homeStatus, 1);
    auto *settings = iconButton(homeFooter, "settings-2", "Project settings");
    settings->setCheckable(false);
    connect(settings, &QToolButton::clicked, m_locationAction, &QAction::trigger);
    homeLayout->addLayout(homeFooter);
    dashboardLayout->addWidget(m_home);
    connect(homeGrid, &QToolButton::clicked, this, [this] { m_projects->setCards(true); });
    connect(homeList, &QToolButton::clicked, this, [this] { m_projects->setCards(false); });
    connect(m_homeCollections, &QTabBar::currentChanged, this, [this] { populateHome(); });
    connect(m_homeSearch, &QLineEdit::textChanged, this, [this] { populateHome(); });
    connect(m_homeSort, &QComboBox::currentIndexChanged, this, [this] { populateHome(); });
    connect(m_projects, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        QString e;
        if (!openManagedProject(item->data(Qt::UserRole).toString(), &e))
            feedback(e, true);
    });
    connect(m_projects, &DashboardGallery::moreRequested, this, &ProjectWorkspaceWindow::homeMenu);
    connect(m_projects, &QWidget::customContextMenuRequested, this, &ProjectWorkspaceWindow::homeMenu);
    m_projectPage = new QWidget;
    m_projectPage->setObjectName("typedProjectDashboard");
    m_projectPage->setStyleSheet(style);
    auto *layout = new QVBoxLayout(m_projectPage);
    layout->setContentsMargins(40, 22, 32, 20);
    layout->setSpacing(20);
    m_breadcrumb = new QLabel;
    m_breadcrumb->setProperty("muted", true);
    m_breadcrumb->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    layout->addWidget(m_breadcrumb);
    connect(m_breadcrumb, &QLabel::linkActivated, this, [this](const QString &id) {
        if (id == "projects")
            showHome();
        else
            for (auto *item : m_folders->findItems("*", Qt::MatchWildcard | Qt::MatchRecursive))
                if (item->data(0, Qt::UserRole).toString() == id) {
                    m_folders->setCurrentItem(item);
                    break;
                }
    });
    auto *header = new QHBoxLayout;
    auto *titles = new QVBoxLayout;
    m_title = new QLabel;
    font = m_title->font();
    font.setPointSize(28);
    font.setWeight(QFont::DemiBold);
    m_title->setFont(font);
    titles->addWidget(m_title);
    m_subtitle = new QLabel;
    m_subtitle->setProperty("muted", true);
    titles->addWidget(m_subtitle);
    header->addLayout(titles, 1);
    m_addFolder = button("New folder", "managedNewFolder", "folder-plus", header);
    m_newDrawing = button("New drawing", "managedNewDrawing", "plus", header);
    m_newAnimation = button("New animation", "managedNewAnimation", "plus", header);
    m_import = button("Import assets", "managedImportAssets", "plus", header);
    m_import->setProperty("primary", true);
    auto *more = iconButton(header, "ellipsis", "Project actions");
    more->setCheckable(false);
    connect(more, &QToolButton::clicked, this, [this, more] {
        QMenu menu(this);
        menu.addAction(m_backupAction);
        menu.addAction(m_locationAction);
        auto *undo = menu.addAction("Undo organization");
        connect(undo, &QAction::triggered, this, [this] {
            if (m_store && !m_busy) {
                QString e;
                if (!m_store->undoOrganization(&e))
                    feedback(e, true);
                refreshDashboard();
            }
        });
        menu.exec(more->mapToGlobal(QPoint(0, more->height())));
    });
    layout->addLayout(header);
    connect(m_newDrawing, &QPushButton::clicked, this, [this] { newDocumentDialog(false); });
    connect(m_newAnimation, &QPushButton::clicked, this, [this] { newDocumentDialog(true); });
    connect(m_addFolder, &QPushButton::clicked, this, &ProjectWorkspaceWindow::newFolder);
    connect(m_import, &QPushButton::clicked, this, &ProjectWorkspaceWindow::importAssets);
    auto *filters = new QHBoxLayout;
    m_collections = new QTabWidget;
    m_collections->setObjectName("projectCollections");
    for (const auto &s : {"Content", "Assets"})
        m_collections->addTab(new QWidget, s);
    m_collections->setMaximumHeight(52);
    m_collections->setDocumentMode(true);
    m_collections->tabBar()->setDrawBase(false);
    m_collections->setMaximumWidth(310);
    filters->addWidget(m_collections);
    filters->addStretch();
    m_search = new QLineEdit;
    m_search->setObjectName("managedSearch");
    m_search->setPlaceholderText("Search drawings");
    m_search->setAccessibleName("Search content");
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(160);
    m_search->setMaximumWidth(280);
    filters->addWidget(m_search, 1);
    m_sort = new QComboBox;
    m_sort->addItems({"Last edited", "Name A–Z"});
    m_sort->setAccessibleName("Sort content");
    filters->addWidget(m_sort);
    m_grid = iconButton(filters, "grid-2x2", "Content grid view");
    m_grid->setObjectName("contentGridView");
    m_list = iconButton(filters, "list", "Content list view");
    m_list->setObjectName("contentListView");
    auto *views = new QButtonGroup(this);
    views->addButton(m_grid);
    views->addButton(m_list);
    m_grid->setChecked(true);
    layout->addLayout(filters);
    auto *split = new QSplitter;
    split->setObjectName("projectContentSplit");
    split->setChildrenCollapsible(false);
    m_sidebar = new QWidget;
    auto *side = new QVBoxLayout(m_sidebar);
    side->setContentsMargins(0, 0, 14, 0);
    auto *folderHeader = new QLabel("Folders");
    auto bold = folderHeader->font();
    bold.setWeight(QFont::DemiBold);
    folderHeader->setFont(bold);
    side->addWidget(folderHeader);
    m_folders = new QTreeWidget;
    m_folders->setObjectName("managedFolders");
    m_folders->setHeaderHidden(true);
    m_folders->setFrameShape(QFrame::NoFrame);
    m_folders->setIconSize({20, 20});
    m_folders->setStyleSheet("QTreeView::item{min-height:34px;}");
    side->addWidget(m_folders);
    m_sidebar->setMinimumWidth(180);
    m_sidebar->setMaximumWidth(290);
    split->addWidget(m_sidebar);
    auto *library = new QWidget;
    auto *libraryLayout = new QVBoxLayout(library);
    libraryLayout->setContentsMargins(0, 0, 0, 0);
    libraryLayout->setSpacing(16);
    m_typeTabs = new QTabBar;
    m_typeTabs->setObjectName("assetTypeFilters");
    m_typeTabs->setDrawBase(false);
    for (const auto &s : {"All", "Images", "Video", "Audio", "Drawings", "Animations"})
        m_typeTabs->addTab(s);
    libraryLayout->addWidget(m_typeTabs);
    m_type = new QComboBox(m_projectPage);
    m_type->hide();
    m_type->addItem("All types", "");
    for (const auto &k : {"image", "video", "audio", "drawing", "animation"})
        m_type->addItem(kindName(k), k);
    m_tagRow = new QWidget;
    auto *tagLayout = new QHBoxLayout(m_tagRow);
    tagLayout->setContentsMargins(0, 0, 0, 0);
    auto *tagFilter = new QPushButton(appIcon("sliders-horizontal"), "Filter tags");
    tagFilter->setObjectName("filterTags");
    tagLayout->addWidget(tagFilter);
    tagLayout->addStretch();
    libraryLayout->addWidget(m_tagRow);
    connect(tagFilter, &QPushButton::clicked, this, [this, tagFilter] {
        if (!m_store)
            return;
        QMenu menu(this);
        QStringList tags{"Storyboard", "Animatics", "Background", "Asset", "Character sheet"};
        for (const auto &item : m_store->items())
            for (const auto &tag : item.tags)
                if (!tags.contains(tag, Qt::CaseInsensitive))
                    tags.append(tag);
        auto *all = menu.addAction("All tags");
        all->setCheckable(true);
        all->setChecked(m_filterTag.isEmpty());
        connect(all, &QAction::triggered, this, [this, tagFilter] {
            m_filterTag.clear();
            tagFilter->setText("Filter tags");
            refreshDashboard();
        });
        menu.addSeparator();
        for (const auto &tag : tags) {
            auto *action = menu.addAction(tag);
            action->setCheckable(true);
            action->setChecked(tag == m_filterTag);
            connect(action, &QAction::triggered, this, [this, tag, tagFilter] {
                m_filterTag = tag;
                tagFilter->setText(tag);
                refreshDashboard();
            });
        }
        menu.exec(tagFilter->mapToGlobal(QPoint(0, tagFilter->height())));
    });
    m_items = new DashboardGallery;
    m_items->setObjectName("managedItems");
    m_items->setAccessibleName("Project content");
    libraryLayout->addWidget(m_items, 1);
    m_empty = new QLabel;
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setProperty("muted", true);
    m_empty->setMinimumHeight(140);
    libraryLayout->addWidget(m_empty);
    split->addWidget(library);
    m_inspector = new QWidget;
    m_inspector->setObjectName("assetInspector");
    m_inspector->setMinimumWidth(250);
    m_inspector->setMaximumWidth(340);
    auto *inspectorLayout = new QVBoxLayout(m_inspector);
    inspectorLayout->setContentsMargins(0, 0, 0, 0);
    auto *inspectorScroll = new QScrollArea;
    inspectorScroll->setWidgetResizable(true);
    inspectorScroll->setFrameShape(QFrame::NoFrame);
    inspectorLayout->addWidget(inspectorScroll);
    auto *inspectorBody = new QWidget;
    inspectorScroll->setWidget(inspectorBody);
    auto *info = new QVBoxLayout(inspectorBody);
    info->setContentsMargins(18, 0, 0, 0);
    m_inspectorName = new QLabel("Asset details");
    m_inspectorName->setWordWrap(true);
    bold = m_inspectorName->font();
    bold.setPointSize(16);
    bold.setWeight(QFont::DemiBold);
    m_inspectorName->setFont(bold);
    info->addWidget(m_inspectorName);
    m_preview = new QLabel("Select an asset");
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumSize(200, 160);
    m_preview->setMaximumHeight(240);
    info->addWidget(m_preview);
    m_video = new QVideoWidget;
    m_video->setMinimumSize(200, 160);
    m_video->hide();
    info->addWidget(m_video);
    m_mediaControls = new QWidget;
    auto *media = new QVBoxLayout(m_mediaControls);
    media->setContentsMargins(0, 0, 0, 0);
    auto *mediaRow = new QHBoxLayout;
    auto *play = button("Play / Pause", "assetPlay", "play", mediaRow),
         *stop = button("Stop", "assetStop", "square", mediaRow);
    media->addLayout(mediaRow);
    m_mediaSeek = new QSlider(Qt::Horizontal);
    m_mediaSeek->setRange(0, 1000);
    m_mediaSeek->setAccessibleName("Media preview position");
    media->addWidget(m_mediaSeek);
    m_mediaTime = new QLabel("00:00 / 00:00");
    m_mediaTime->setProperty("muted", true);
    media->addWidget(m_mediaTime);
    info->addWidget(m_mediaControls);
    m_mediaControls->hide();
    m_details = new QLabel;
    m_details->setWordWrap(true);
    m_details->setProperty("muted", true);
    info->addWidget(m_details);
    info->addWidget(new QLabel("Tags"));
    m_assetTags = new TagEditor({}, nullptr, false);
    info->addWidget(m_assetTags);
    auto *moveFolder = new QPushButton(appIcon("folder"), "Move to folder…");
    info->addWidget(moveFolder);
    connect(moveFolder, &QPushButton::clicked, this, [this] { organizeSelected(false); });
    info->addStretch();
    split->addWidget(m_inspector);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 0);
    split->setSizes({220, 900, 280});
    layout->addWidget(split, 1);
    auto *footer = new QHBoxLayout;
    m_status = new QLabel;
    m_status->setProperty("muted", true);
    m_status->setWordWrap(true);
    footer->addWidget(m_status, 1);
    m_open = button("Open selected", "managedOpenSelected", "folder-open", footer);
    m_open->hide();
    connect(m_open, &QPushButton::clicked, this, [this] { openItems(selectedItems()); });
    layout->addLayout(footer);
    dashboardLayout->addWidget(m_projectPage);
    m_projectPage->hide();
    m_player = new QMediaPlayer(this);
    m_audio = new QAudioOutput(this);
    m_player->setAudioOutput(m_audio);
    m_player->setVideoOutput(m_video);
    connect(play, &QPushButton::clicked, this, [this] {
        if (m_player->source().isEmpty())
            return;
        if (m_player->playbackState() == QMediaPlayer::PlayingState)
            m_player->pause();
        else
            m_player->play();
    });
    connect(stop, &QPushButton::clicked, m_player, &QMediaPlayer::stop);
    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &s) { feedback("Preview unavailable: " + s, true); });
    connect(m_mediaSeek, &QSlider::sliderMoved, this,
            [this](int position) { m_player->setPosition(m_player->duration() * position / 1000); });
    const auto mediaReadout = [this] {
        auto time = [](qint64 ms) {
            return QString("%1:%2")
                .arg(ms / 60000, 2, 10, QChar('0'))
                .arg((ms / 1000) % 60, 2, 10, QChar('0'));
        };
        m_mediaTime->setText(time(m_player->position()) + " / " + time(m_player->duration()));
        if (!m_mediaSeek->isSliderDown()) {
            const QSignalBlocker block(m_mediaSeek);
            m_mediaSeek->setValue(m_player->duration() ? m_player->position() * 1000 / m_player->duration()
                                                       : 0);
        }
    };
    connect(m_player, &QMediaPlayer::positionChanged, this, [mediaReadout] { mediaReadout(); });
    connect(m_player, &QMediaPlayer::durationChanged, this, [mediaReadout] { mediaReadout(); });
    connect(m_assetTags, &TagEditor::tagsChanged, this, [this] {
        if (m_refreshing || m_busy || !m_store)
            return;
        auto ids = selectedItems();
        if (ids.size() != 1)
            return;
        for (const auto &item : m_store->items())
            if (item.id == ids.first()) {
                QString e;
                if (!m_store->organize(ids, item.collection, item.folder, m_assetTags->tags(), &e))
                    feedback(e, true);
                else
                    refreshDashboard();
                break;
            }
    });
    connect(m_items, &QListWidget::itemSelectionChanged, this, [this] {
        if (m_refreshing)
            return;
        m_open->setVisible(selectedItems().size() > 1);
        previewSelected();
    });
    connect(m_items, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        auto id = item->data(Qt::UserRole).toString();
        if (id.startsWith("folder:")) {
            for (auto *node : m_folders->findItems("*", Qt::MatchWildcard | Qt::MatchRecursive))
                if (node->data(0, Qt::UserRole).toString() == id.mid(7)) {
                    m_folders->setCurrentItem(node);
                    break;
                }
        } else
            openItems(selectedItems());
    });
    connect(m_items, &DashboardGallery::moreRequested, this, &ProjectWorkspaceWindow::itemMenu);
    connect(m_items, &QWidget::customContextMenuRequested, this, &ProjectWorkspaceWindow::itemMenu);
    connect(m_items, &DashboardGallery::visibleItemsChanged, this,
            &ProjectWorkspaceWindow::requestThumbnails);
    connect(m_grid, &QToolButton::clicked, this, [this] {
        m_items->setCards(true);
        m_settings->setValue("gallery/" + m_galleryContext, true);
    });
    connect(m_list, &QToolButton::clicked, this, [this] {
        m_items->setCards(false);
        m_settings->setValue("gallery/" + m_galleryContext, false);
    });
    connect(m_folders, &QTreeWidget::currentItemChanged, this, [this] {
        if (m_refreshing)
            return;
        m_showRemoved = m_folders->currentItem() &&
                        m_folders->currentItem()->data(0, Qt::UserRole).toString() == "@removed";
        refreshDashboard();
    });
    connect(m_search, &QLineEdit::textChanged, this, [this] { refreshDashboard(); });
    connect(m_sort, &QComboBox::currentIndexChanged, this, [this] { refreshDashboard(); });
    connect(m_typeTabs, &QTabBar::currentChanged, this, [this](int index) {
        m_type->setCurrentIndex(index);
        refreshDashboard();
    });
    connect(m_collections, &QTabWidget::currentChanged, this, [this] {
        if (m_refreshing)
            return;
        m_showRemoved = false;
        m_filterTag.clear();
        if (m_tagRow)
            m_tagRow->findChild<QPushButton *>("filterTags")->setText("Filter tags");
        const QSignalBlocker block(m_folders);
        m_folders->setCurrentItem(nullptr);
        refreshDashboard();
    });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (m_refreshing)
            return;
        auto *previous = qobject_cast<MainWindow *>(m_tabs->widget(m_currentTab));
        if (previous && !previous->prepareManagedLeave()) {
            const QSignalBlocker block(m_tabs);
            m_tabs->setCurrentIndex(m_currentTab);
            feedback("Finish the active edit before changing tabs.", true);
            return;
        }
        m_currentTab = index;
        stopPreview();
        updateNavigation();
        if (index == 0 && m_store && m_projectPage->isVisible())
            refreshDashboard();
        if (auto *editor = qobject_cast<MainWindow *>(m_tabs->widget(index)))
            editor->canvas()->setFocus(Qt::OtherFocusReason);
    });
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (index > 0)
            closeSession(m_tabs->widget(index));
    });
    auto *timer = new QTimer(this);
    timer->setInterval(30000);
    connect(timer, &QTimer::timeout, this, [this] {
        if (m_busy || !m_recoveryQueue.isEmpty())
            return;
        m_recoveryQueue = m_sessions.keys();
        continueRecovery();
    });
    timer->start();
    statusBar()->hide();
    showHome();
}

ProjectWorkspaceWindow::~ProjectWorkspaceWindow() {
    m_thumbnailQueue.clear();
    if (m_waitForThumbnail)
        m_waitForThumbnail();
    m_waitForThumbnail = {};
    stopPreview();
    m_sessions.clear();
    delete m_tabs;
    m_tabs = nullptr;
}
MainWindow *ProjectWorkspaceWindow::editorForItem(const QString &id) const {
    return m_sessions.value(id).editor;
}
QString ProjectWorkspaceWindow::collection() const {
    return m_collections->currentIndex() == 0 ? "content" : "assets";
}
QString ProjectWorkspaceWindow::folder() const {
    auto *i = m_folders->currentItem();
    return i ? i->data(0, Qt::UserRole).toString() : QString();
}
QStringList ProjectWorkspaceWindow::selectedItems() const {
    QStringList ids;
    for (auto *i : m_items->selectedItems())
        if (!i->data(DashboardGallery::FolderRole).toBool())
            ids.append(i->data(Qt::UserRole).toString());
    return ids;
}
void ProjectWorkspaceWindow::feedback(const QString &s, bool error) {
    m_status->setText(s);
    statusBar()->setVisible(error);
    statusBar()->showMessage(s, error ? 12000 : 5000);
}
bool ProjectWorkspaceWindow::prepareNavigation() {
    if (m_busy)
        return false;
    if (auto *e = qobject_cast<MainWindow *>(m_tabs->currentWidget()))
        return e->prepareManagedLeave();
    return true;
}
void ProjectWorkspaceWindow::updateNavigation() {
    m_navigation->setVisible(m_tabs->currentIndex() > 0);
    m_tabs->setTabText(0, m_store && !m_home->isVisible() ? m_store->title() : "Projects");
}
void ProjectWorkspaceWindow::showHome() {
    if (!prepareNavigation())
        return;
    m_tabs->setCurrentIndex(0);
    m_projectPage->hide();
    m_home->show();
    stopPreview();
    populateHome();
    updateNavigation();
}
namespace {
QString pathKey(const QString &path) {
    return QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex());
}
QString projectSetting(const QString &path) { return "projectDetails/" + pathKey(path) + "/"; }
QString previewDirectory() {
    auto dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/dashboard-previews";
    QDir().mkpath(dir);
    return dir;
}
QString editedLabel(const QDateTime &time) {
    if (!time.isValid())
        return {};
    auto secs = time.secsTo(QDateTime::currentDateTime());
    if (secs < 60)
        return "Just now";
    if (secs < 3600)
        return QString("%1 min ago").arg(secs / 60);
    if (secs < 86400)
        return QString("%1 hr ago").arg(secs / 3600);
    return time.toString("d MMM yyyy");
}
QString thumbnailKey(const QString &root, const ManagedItem &item) {
    return pathKey(
        root + "/" + item.id + "/" + item.revision + "/" +
        QString::number(QFileInfo(QDir(root).filePath(item.path)).lastModified().toMSecsSinceEpoch()));
}
} // namespace
void ProjectWorkspaceWindow::populateHome() {
    auto paths = m_settings->value("projects").toStringList();
    auto pinned = m_settings->value("pinned").toStringList(),
         archived = m_settings->value("archived").toStringList();
    if (m_homeSort->currentIndex() == 1)
        std::sort(paths.begin(), paths.end(), [this](const auto &a, const auto &b) {
            return m_settings->value(projectSetting(a) + "title", QFileInfo(a).completeBaseName())
                       .toString()
                       .localeAwareCompare(
                           m_settings->value(projectSetting(b) + "title", QFileInfo(b).completeBaseName())
                               .toString()) < 0;
        });
    m_projects->clear();
    for (const auto &path : paths) {
        const auto prefix = projectSetting(path),
                   name = m_settings->value(prefix + "title", QFileInfo(path).completeBaseName()).toString();
        if ((m_homeCollections->currentIndex() == 2) != archived.contains(path))
            continue;
        if (m_homeCollections->currentIndex() == 1 && !pinned.contains(path))
            continue;
        if (!name.contains(m_homeSearch->text().trimmed(), Qt::CaseInsensitive))
            continue;
        auto *item = new QListWidgetItem(name, m_projects);
        item->setData(Qt::UserRole, path);
        item->setData(DashboardGallery::KindRole, m_settings->value(prefix + "type", "drawing"));
        auto opened = m_settings->value(prefix + "opened").toDateTime();
        item->setData(DashboardGallery::SubtitleRole,
                      kindName(item->data(DashboardGallery::KindRole).toString()) + " · " +
                          (opened.isValid() ? "Opened " + editedLabel(opened) : "Not opened here"));
        item->setData(DashboardGallery::PinnedRole, pinned.contains(path));
        item->setData(DashboardGallery::CoverRole, QPixmap(m_settings->value(prefix + "cover").toString()));
        item->setToolTip(name + "\n" + path);
    }
    m_homeStatus->setText(m_projects->count() ? QString("%1 projects").arg(m_projects->count())
                                              : "No projects here. Create a project to get started.");
    m_projects->relayout();
}
void ProjectWorkspaceWindow::homeMenu(const QPoint &point) {
    auto *item = m_projects->itemAt(point);
    if (!item)
        item = m_projects->currentItem();
    if (!item)
        return;
    const auto path = item->data(Qt::UserRole).toString();
    QMenu menu(this);
    auto *open = menu.addAction("Open project");
    auto pinned = m_settings->value("pinned").toStringList(),
         archived = m_settings->value("archived").toStringList();
    auto *pin = menu.addAction(pinned.contains(path) ? "Unpin project" : "Pin project");
    auto *archive = menu.addAction(archived.contains(path) ? "Restore project" : "Archive project");
    menu.addSeparator();
    auto *forget = menu.addAction("Remove from recent projects");
    auto *action = menu.exec(m_projects->viewport()->mapToGlobal(point));
    if (action == open) {
        QString error;
        if (!openManagedProject(path, &error))
            feedback(error, true);
        return;
    }
    if (action == pin) {
        if (pinned.contains(path))
            pinned.removeAll(path);
        else
            pinned.append(path);
        m_settings->setValue("pinned", pinned);
    } else if (action == archive) {
        if (archived.contains(path))
            archived.removeAll(path);
        else
            archived.append(path);
        m_settings->setValue("archived", archived);
    } else if (action == forget) {
        auto paths = m_settings->value("projects").toStringList();
        paths.removeAll(path);
        m_settings->setValue("projects", paths);
    }
    populateHome();
}
void ProjectWorkspaceWindow::rememberProject() {
    if (!m_store)
        return;
    auto paths = m_settings->value("projects").toStringList();
    paths.removeAll(m_store->root());
    paths.prepend(m_store->root());
    m_settings->setValue("projects", paths);
    const auto prefix = projectSetting(m_store->root());
    m_settings->setValue(prefix + "title", m_store->title());
    m_settings->setValue(prefix + "type", m_store->type());
    m_settings->setValue(prefix + "opened", QDateTime::currentDateTime());
    m_settings->sync();
}
bool ProjectWorkspaceWindow::createManagedProject(const QString &p, const QString &n, const QString &t,
                                                  QString *e) {
    if (!prepareNavigation() || !closeAllSessions())
        return false;
    auto store = ManagedProjectStore::create(p, n, t, e);
    if (!store)
        return false;
    m_store = store;
    m_showRemoved = false;
    m_filterTag.clear();
    if (m_tagRow)
        m_tagRow->findChild<QPushButton *>("filterTags")->setText("Filter tags");
    m_galleryContext.clear();
    {
        const QSignalBlocker block(m_collections);
        m_collections->setCurrentIndex(0);
    }
    {
        const QSignalBlocker block(m_folders);
        m_folders->setCurrentItem(nullptr);
    }
    rememberProject();
    m_tabs->setCurrentIndex(0);
    m_home->hide();
    m_projectPage->show();
    refreshDashboard();
    return true;
}
bool ProjectWorkspaceWindow::openManagedProject(const QString &path, QString *e) {
    if (!prepareNavigation())
        return false;
    const QString root =
        QFileInfo(path).isDir() ? QFileInfo(path).canonicalFilePath() : QFileInfo(path).absolutePath();
    if (m_store && m_store->root() == root) {
        m_tabs->setCurrentIndex(0);
        m_home->hide();
        m_projectPage->show();
        refreshDashboard();
        return true;
    }
    if (!closeAllSessions())
        return false;
    auto store = ManagedProjectStore::open(path, e);
    if (!store)
        return false;
    m_store = store;
    m_showRemoved = false;
    m_filterTag.clear();
    if (m_tagRow)
        m_tagRow->findChild<QPushButton *>("filterTags")->setText("Filter tags");
    m_galleryContext.clear();
    {
        const QSignalBlocker block(m_collections);
        m_collections->setCurrentIndex(0);
    }
    {
        const QSignalBlocker block(m_folders);
        m_folders->setCurrentItem(nullptr);
    }
    rememberProject();
    m_tabs->setCurrentIndex(0);
    m_home->hide();
    m_projectPage->show();
    m_folders->setCurrentItem(nullptr);
    refreshDashboard();
    return true;
}
void ProjectWorkspaceWindow::newProjectDialog() {
    if (!prepareNavigation())
        return;
    SetupDialog d("New project", "Choose how you’d like to create.", this);
    d.setObjectName("managedProjectDialog");
    auto *type = new ProjectTypeCards;
    type->setObjectName("managedProjectType");
    d.content->addWidget(type);
    auto *name = new QLineEdit;
    name->setPlaceholderText("Enter project name");
    name->setObjectName("managedProjectName");
    name->setAccessibleName("Project name");
    d.content->addWidget(new QLabel("Project name"));
    d.content->addWidget(name);
    QString destination =
        m_settings
            ->value("workspace",
                    QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Toon Pro")
            .toString();
    auto *location = new QPushButton(appIcon("folder-open"), "Project location…");
    location->setObjectName("managedChooseWorkspace");
    location->setAutoDefault(false);
    location->setToolTip(destination);
    d.footer->insertWidget(0, location);
    connect(location, &QPushButton::clicked, &d, [&] {
        auto path = QFileDialog::getExistingDirectory(&d, "New-project location", destination);
        if (!path.isEmpty()) {
            destination = path;
            location->setToolTip(path);
        }
    });
    auto *cancel = button("Cancel", "cancelProject", "x", d.footer),
         *create = button("Create project", "managedCreateProject", "plus", d.footer);
    create->setProperty("primary", true);
    create->setDefault(true);
    connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
    connect(create, &QPushButton::clicked, &d, [&] {
        auto n = name->text().trimmed();
        QString error;
        if (n.isEmpty() || n.contains('/') || n.contains('\\'))
            error = "Enter a project name without path separators.";
        else if (QFileInfo::exists(QDir(destination).filePath(n + ".toonstudio")))
            error = "A project with this name already exists here.";
        if (!error.isEmpty()) {
            d.error->setText(error);
            d.error->show();
            return;
        }
        d.accept();
    });
    d.resize(960, 535);
    name->setFocus();
    if (execSetupDialog(d) != QDialog::Accepted)
        return;
    QString error;
    QDir().mkpath(destination);
    if (createManagedProject(QDir(destination).filePath(name->text().trimmed() + ".toonstudio"),
                             name->text().trimmed(), type->tabData(type->currentIndex()).toString(), &error))
        m_settings->setValue("workspace", destination);
    else
        feedback(error, true);
}
QString ProjectWorkspaceWindow::folderPath(const QString &id) const {
    if (!m_store || id.isEmpty() || id == "@removed")
        return {};
    auto folders = m_store->folders();
    QStringList parts;
    QString current = id;
    QSet<QString> seen;
    while (!current.isEmpty() && !seen.contains(current)) {
        seen.insert(current);
        auto it =
            std::find_if(folders.begin(), folders.end(), [&](const auto &f) { return f.id == current; });
        if (it == folders.end())
            break;
        parts.prepend(it->name);
        current = it->parent;
    }
    return parts.join(" / ");
}
void ProjectWorkspaceWindow::refreshDashboard() {
    if (!m_store || m_busy || m_refreshing)
        return;
    m_refreshing = true;
    const auto selected = selectedItems();
    const auto currentId =
        m_items->currentItem() ? m_items->currentItem()->data(Qt::UserRole).toString() : QString();
    const auto selectedFolder = folder();
    const auto scroll = m_items->verticalScrollBar()->value();
    QString error;
    auto items = m_store->items(&error);
    const auto folders = m_store->folders(&error);
    const bool assets = collection() == "assets", movie = m_store->type() == "movie", removed = m_showRemoved;
    const auto currentFolder = selectedFolder == "@removed" ? QString() : selectedFolder;
    for (const auto &i : items)
        if (m_sessions.contains(i.id))
            m_tabs->setTabText(m_tabs->indexOf(m_sessions[i.id].editor),
                               i.name + (m_sessions[i.id].editor->managedDirty() ? " *" : ""));
    m_title->setText(m_store->title());
    m_subtitle->setText(removed                        ? "Removed items"
                        : assets                       ? "Your project’s reusable assets."
                        : movie                        ? "Everything for your movie, in one place."
                        : m_store->type() == "drawing" ? "Your drawings, all in one place."
                                                       : "One shot at a time.");
    m_newDrawing->setVisible(!assets && !removed && (m_store->type() == "drawing" || movie));
    m_newAnimation->setVisible(!assets && !removed && (m_store->type() == "animation" || movie));
    m_import->setVisible(assets && !removed);
    m_addFolder->setVisible(!removed && (assets || movie));
    m_newDrawing->setProperty("primary", true);
    m_newAnimation->setProperty("primary", true);
    for (auto *action : {m_newDrawing, m_newAnimation}) {
        action->style()->unpolish(action);
        action->style()->polish(action);
    }
    m_collections->setTabText(0, m_store->type() == "drawing"     ? "Drawings"
                                 : m_store->type() == "animation" ? "Shots"
                                                                  : "Content");
    m_sidebar->setVisible(assets || movie);
    m_inspector->setVisible(assets && !removed);
    m_typeTabs->setVisible(assets && !removed);
    m_tagRow->setVisible((assets || movie) && !removed);
    m_search->setPlaceholderText(assets                         ? "Search assets"
                                 : movie                        ? "Search content"
                                 : m_store->type() == "drawing" ? "Search drawings"
                                                                : "Search shots");
    QString breadcrumb = "<a style='color:" + QString(ui::muted) +
                         ";text-decoration:none' href='projects'>Projects</a>  / "
                         " <a style='color:" +
                         QString(ui::muted) + ";text-decoration:none' href=''>" +
                         m_store->title().toHtmlEscaped() + "</a>";
    if (assets)
        breadcrumb += "  /  Assets";
    if (removed)
        breadcrumb += "  /  Removed";
    else if (!currentFolder.isEmpty())
        breadcrumb += "  /  " + folderPath(currentFolder).toHtmlEscaped();
    m_breadcrumb->setText(breadcrumb);
    m_folders->clear();
    auto *root = new QTreeWidgetItem(m_folders, {assets ? "All assets" : "All content"});
    root->setData(0, Qt::UserRole, QString());
    root->setIcon(0, appIcon("folder-open"));
    QHash<QString, QTreeWidgetItem *> nodes;
    nodes.insert({}, root);
    for (const auto &f : folders)
        if (f.collection == collection()) {
            auto *node = new QTreeWidgetItem({f.name});
            node->setData(0, Qt::UserRole, f.id);
            node->setIcon(0, appIcon("folder"));
            nodes.insert(f.id, node);
        }
    for (const auto &f : folders)
        if (f.collection == collection())
            nodes.value(f.parent, root)->addChild(nodes.value(f.id));
    auto *trash = new QTreeWidgetItem(m_folders, {"Removed"});
    trash->setData(0, Qt::UserRole, "@removed");
    trash->setIcon(0, appIcon("trash"));
    nodes.insert("@removed", trash);
    m_folders->expandAll();
    m_folders->setCurrentItem(nodes.value(removed ? "@removed" : currentFolder, root));
    m_items->clear();
    const auto query = m_search->text().trimmed(),
               kind = assets && !removed ? m_type->currentData().toString() : QString();
    if (m_sort->currentIndex() == 1)
        std::sort(items.begin(), items.end(),
                  [](const auto &a, const auto &b) { return a.name.localeAwareCompare(b.name) < 0; });
    else
        std::stable_sort(items.begin(), items.end(), [this](const auto &a, const auto &b) {
            return QFileInfo(QDir(m_store->root()).filePath(a.path)).lastModified() >
                   QFileInfo(QDir(m_store->root()).filePath(b.path)).lastModified();
        });
    if (movie && !assets && !removed && query.isEmpty() && m_filterTag.isEmpty())
        for (const auto &f : folders)
            if (f.collection == collection() && f.parent == currentFolder) {
                auto *row = new QListWidgetItem(f.name, m_items);
                row->setData(Qt::UserRole, "folder:" + f.id);
                row->setData(DashboardGallery::FolderRole, true);
                int count = 0;
                for (const auto &i : items)
                    count += !i.removed && i.folder == f.id;
                row->setData(DashboardGallery::SubtitleRole, QString("%1 items").arg(count));
            }
    int count = 0;
    for (const auto &i : items) {
        if (i.removed != removed || (!removed && i.collection != collection()) ||
            (!removed && !currentFolder.isEmpty() && i.folder != currentFolder) ||
            (!removed && movie && !assets && currentFolder.isEmpty() && query.isEmpty() &&
             m_filterTag.isEmpty() && !i.folder.isEmpty()) ||
            (!kind.isEmpty() && kind != i.kind) ||
            (!removed && !m_filterTag.isEmpty() && !i.tags.contains(m_filterTag, Qt::CaseInsensitive)) ||
            (!query.isEmpty() && !(i.name + " " + i.tags.join(" ")).contains(query, Qt::CaseInsensitive)))
            continue;
        auto *row = new QListWidgetItem(i.name, m_items);
        row->setData(Qt::UserRole, i.id);
        row->setData(DashboardGallery::KindRole, i.kind);
        row->setData(DashboardGallery::TagsRole, (assets || movie) ? i.tags : QStringList{});
        row->setData(DashboardGallery::SubtitleRole,
                     kindName(i.kind) + " · " +
                         editedLabel(QFileInfo(QDir(m_store->root()).filePath(i.path)).lastModified()));
        const auto key = thumbnailKey(m_store->root(), i);
        row->setData(DashboardGallery::CoverKeyRole, key);
        if (auto *pix = m_thumbnails.object(key))
            row->setData(DashboardGallery::CoverRole, *pix);
        auto meta = m_thumbnailMetadata.value(key);
        row->setData(DashboardGallery::DurationRole, meta.value("duration"));
        row->setData(DashboardGallery::RateRole, meta.value("rate"));
        row->setToolTip(i.name + "\n" + kindName(i.kind) +
                        (i.tags.isEmpty() ? QString() : "\n" + i.tags.join(", ")));
        row->setSelected(selected.contains(i.id));
        if (i.id == currentId)
            m_items->setCurrentItem(row, QItemSelectionModel::NoUpdate);
        ++count;
    }
    auto context = m_store->type() + "/" + collection();
    if (context != m_galleryContext) {
        m_galleryContext = context;
        bool cards =
            m_settings->value("gallery/" + context, assets || m_store->type() != "animation").toBool();
        m_items->setCards(cards);
        m_grid->setChecked(cards);
        m_list->setChecked(!cards);
    }
    m_items->relayout();
    m_items->verticalScrollBar()->setValue(scroll);
    m_status->setText(error.isEmpty()
                          ? QString("%1 %2").arg(count).arg(assets                           ? "assets"
                                                            : m_store->type() == "drawing"   ? "drawings"
                                                            : m_store->type() == "animation" ? "shots"
                                                                                             : "items")
                          : error);
    m_empty->setText(removed  ? "No removed items."
                     : assets ? "Import images, video or audio to start your asset library."
                     : m_store->type() == "drawing" ? "Create your first drawing."
                     : m_store->type() == "animation"
                         ? "Create your first single-shot animation."
                         : "Create a folder, drawing or animation to get started.");
    m_empty->setVisible(m_items->count() == 0);
    m_items->setVisible(m_items->count() > 0);
    m_open->setVisible(selectedItems().size() > 1);
    m_refreshing = false;
    updateNavigation();
    previewSelected();
    requestThumbnails();
}
void ProjectWorkspaceWindow::requestThumbnails() {
    if (m_busy || m_refreshing || !m_store || !m_projectPage->isVisible() || m_tabs->currentIndex() != 0)
        return;
    auto items = m_store->items();
    for (int n = 0; n < m_items->count() && m_thumbnailQueue.size() < 12; ++n) {
        auto *row = m_items->item(n);
        const auto key = row->data(DashboardGallery::CoverKeyRole).toString();
        if (key.isEmpty() || m_pendingThumbnails.contains(key) || m_thumbnails.contains(key) ||
            !m_items->viewport()->rect().intersects(m_items->visualItemRect(row)))
            continue;
        auto it = std::find_if(items.begin(), items.end(),
                               [&](const auto &i) { return i.id == row->data(Qt::UserRole).toString(); });
        if (it == items.end() || (it->kind != "drawing" && it->kind != "animation" && it->kind != "image"))
            continue;
        m_pendingThumbnails.insert(key);
        m_thumbnailQueue.enqueue({key, it->id, QDir(m_store->root()).filePath(it->path), it->kind, m_store});
    }
    startNextThumbnail();
}
void ProjectWorkspaceWindow::startNextThumbnail() {
    if (m_thumbnailRunning || m_thumbnailQueue.isEmpty())
        return;
    m_thumbnailRunning = true;
    const auto task = m_thumbnailQueue.dequeue();
    struct Preview {
        QImage image;
        QVariantMap metadata;
    };
    auto *watcher = new QFutureWatcher<Preview>(this);
    connect(watcher, &QFutureWatcher<Preview>::finished, this, [this, watcher, task] {
        auto result = watcher->result();
        watcher->deleteLater();
        m_thumbnailRunning = false;
        applyThumbnail(task.key, result.image, result.metadata);
        if (!result.image.isNull()) {
            m_settings->setValue(projectSetting(task.store->root()) + "cover",
                                 previewDirectory() + "/" + task.key + ".png");
        }
        // Compact grids can expose more cards than one bounded queue holds.
        requestThumbnails();
        startNextThumbnail();
    });
    watcher->setFuture(QtConcurrent::run([task] {
        Preview result;
        auto path = previewDirectory() + "/" + task.key;
        result.image.load(path + ".png");
        QFile json(path + ".json");
        if (json.open(QIODevice::ReadOnly))
            result.metadata = QJsonDocument::fromJson(json.readAll()).object().toVariantMap();
        if (!result.image.isNull())
            return result;
        if (task.kind == "image") {
            QImageReader reader(task.path);
            reader.setAutoTransform(true);
            auto size = reader.size();
            if (size.isValid() && qint64(size.width()) * size.height() <= 64000000) {
                result.metadata["dimensions"] = QString("%1 × %2 px").arg(size.width()).arg(size.height());
                reader.setScaledSize(size.scaled({640, 400}, Qt::KeepAspectRatio));
                result.image = reader.read();
            }
        } else {
            QString error;
            auto p = task.store->loadDocument(task.id, false, &error);
            if (p) {
                result.metadata["dimensions"] = QString("%1 × %2 px").arg(p->width).arg(p->height);
                if (task.kind == "animation" && !p->shots.isEmpty()) {
                    result.metadata["duration"] =
                        QString::number(p->shots[0].durationFrames / p->fps, 'f', 1) + " s";
                    result.metadata["rate"] = QString::number(p->fps, 'g', 4) + " fps";
                }
                result.image =
                    renderMovieFrame(*p).scaled({640, 400}, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
        }
        if (!result.image.isNull()) {
            QSaveFile file(path + ".png");
            if (file.open(QIODevice::WriteOnly) && result.image.save(&file, "PNG"))
                file.commit();
            QSaveFile meta(path + ".json");
            if (meta.open(QIODevice::WriteOnly)) {
                meta.write(QJsonDocument(QJsonObject::fromVariantMap(result.metadata)).toJson());
                meta.commit();
            }
        }
        return result;
    }));
    m_waitForThumbnail = [future = watcher->future()]() mutable { future.waitForFinished(); };
}
void ProjectWorkspaceWindow::applyThumbnail(const QString &key, const QImage &image,
                                            const QVariantMap &metadata) {
    if (image.isNull())
        return;
    auto pix = QPixmap::fromImage(image);
    m_thumbnails.insert(key, new QPixmap(pix));
    m_thumbnailMetadata[key] = metadata;
    bool selectedPreview = false;
    for (int n = 0; n < m_items->count(); ++n) {
        auto *row = m_items->item(n);
        if (row->data(DashboardGallery::CoverKeyRole).toString() != key)
            continue;
        row->setData(DashboardGallery::CoverRole, pix);
        row->setData(DashboardGallery::DurationRole, metadata.value("duration"));
        row->setData(DashboardGallery::RateRole, metadata.value("rate"));
        if (row->isSelected() && m_inspector->isVisible()) {
            selectedPreview = true;
            m_preview->setPixmap(pix.scaled(300, 220, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }
    if (selectedPreview)
        previewSelected();
}
bool ProjectWorkspaceWindow::openItems(const QStringList &ids) {
    if (!m_store || !prepareNavigation())
        return false;
    QString error;
    const auto items = m_store->items(&error);
    bool result = true;
    for (const auto &id : ids) {
        if (m_sessions.contains(id)) {
            m_tabs->setCurrentWidget(m_sessions[id].editor);
            continue;
        }
        auto it = std::find_if(items.begin(), items.end(), [&](const auto &i) { return i.id == id; });
        if (it == items.end() || it->removed) {
            result = false;
            continue;
        }
        if (it->kind != "drawing" && it->kind != "animation") {
            previewSelected();
            continue;
        }
        bool recover = false;
        if (!it->recovery.isEmpty()) {
            const auto answer = QMessageBox::question(
                this, "Recover drawing?", "Recover newer unsaved edits to " + it->name + "?",
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
            if (answer == QMessageBox::Cancel) {
                result = false;
                continue;
            }
            recover = answer == QMessageBox::Yes;
            if (!recover)
                m_store->discardRecovery(id, &error);
        }
        std::optional<Project> p;
        m_busy = true;
        const auto store = m_store;
        const bool loaded = job(
            this, "Opening " + it->name + "…",
            [&p, store, id, recover](QString *e, const auto &cancel) {
                if (cancel())
                    return false;
                p = store->loadDocument(id, recover, e);
                return p.has_value() && !cancel();
            },
            &error);
        m_busy = false;
        if (!loaded) {
            feedback(error, true);
            result = false;
            continue;
        }
        const auto dir = QFileInfo(m_settings->fileName()).absolutePath() + "/editor";
        QDir().mkpath(dir);
        auto *editor = new MainWindow(dir + "/workspace.ini", m_tabs, true);
        editor->menuBar()->setNativeMenuBar(false);
        Session session{id, it->revision, editor, 0};
        m_sessions.insert(id, session);
        editor->setManagedDocument(*p, [this, id] { return saveSession(id); }, [] {});
        editor->setObjectName("managedEditor_" + id);
        connect(editor->canvas()->document(), &Document::changed, this, [this, id] {
            if (!m_sessions.contains(id))
                return;
            auto &s = m_sessions[id];
            ++s.generation;
            const int tab = m_tabs->indexOf(s.editor);
            auto label = m_tabs->tabText(tab);
            if (label.endsWith(" *"))
                label.chop(2);
            m_tabs->setTabText(tab, label + (s.editor->managedDirty() ? " *" : ""));
        });
        m_tabs->addTab(editor, appIcon(it->kind == "drawing" ? "brush" : "clapperboard"), it->name);
        if (recover)
            editor->canvas()->document()->history()->resetClean();
        m_tabs->setCurrentWidget(editor);
    }
    return result;
}
void ProjectWorkspaceWindow::continueRecovery() {
    if (m_busy)
        return;
    while (!m_recoveryQueue.isEmpty()) {
        auto id = m_recoveryQueue.takeFirst();
        if (m_sessions.contains(id) && m_sessions[id].editor->managedDirty() && saveSession(id, true))
            return;
    }
}
bool ProjectWorkspaceWindow::saveSession(const QString &id, bool recovery) {
    if (m_busy || !m_sessions.contains(id))
        return false;
    auto &s = m_sessions[id];
    if (recovery && (!s.editor->managedDirty() || s.editor->canvas()->document()->editingGestureActive() ||
                     s.editor->canvas()->document()->layerPropertyEditActive()))
        return false;
    const auto snapshot = s.editor->canvas()->project();
    const auto store = m_store;
    const auto expected = s.revision;
    const auto generation = s.generation;
    QString revision, error;
    m_busy = true;
    if (recovery) {
        auto *watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
            const auto error = watcher->result();
            watcher->deleteLater();
            m_busy = false;
            if (!error.isEmpty())
                feedback("Recovery failed: " + error, true);
            QTimer::singleShot(0, this, &ProjectWorkspaceWindow::continueRecovery);
        });
        watcher->setFuture(QtConcurrent::run([store, id, expected, snapshot] {
            QString error;
            store->saveDocument(id, expected, snapshot, true, &error);
            return error;
        }));
        return true;
    }
    const bool ok = job(
        this, "Saving document…",
        [store, id, expected, snapshot, recovery, &revision](QString *e, const auto &cancel) {
            revision = store->saveDocument(id, expected, snapshot, recovery, e, cancel);
            return !revision.isEmpty();
        },
        &error);
    m_busy = false;
    if (ok && !recovery) {
        s.revision = revision;
        if (s.generation == generation)
            s.editor->markManagedSaved();
        auto items = m_store->items();
        for (const auto &i : items)
            if (i.id == id)
                m_tabs->setTabText(m_tabs->indexOf(s.editor),
                                   i.name + (s.editor->managedDirty() ? " *" : ""));
    }
    if (!ok && !error.isEmpty())
        feedback(error, true);
    return ok;
}
bool ProjectWorkspaceWindow::closeSession(QWidget *widget) {
    if (m_busy)
        return false;
    auto *editor = qobject_cast<MainWindow *>(widget);
    if (!editor)
        return true;
    if (!editor->prepareManagedLeave())
        return false;
    QString id;
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it)
        if (it->editor == editor)
            id = it.key();
    if (editor->managedDirty()) {
        const auto answer = QMessageBox::warning(
            this, "Unsaved drawing", "Save changes before closing this tab?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Cancel)
            return false;
        if (answer == QMessageBox::Save && !editor->saveManaged())
            return false;
        if (answer == QMessageBox::Discard) {
            QString e;
            m_store->discardRecovery(id, &e);
        }
    }
    const int index = m_tabs->indexOf(editor);
    m_sessions.remove(id);
    m_refreshing = true;
    m_tabs->removeTab(index);
    m_refreshing = false;
    m_currentTab = m_tabs->currentIndex();
    delete editor;
    return true;
}
bool ProjectWorkspaceWindow::closeAllSessions() {
    if (m_busy)
        return false;
    while (m_tabs->count() > 1)
        if (!closeSession(m_tabs->widget(m_tabs->count() - 1)))
            return false;
    return true;
}
bool ProjectWorkspaceWindow::eventFilter(QObject *watched, QEvent *event) {
    // Check before QTabWidget hides the old page: hiding a canvas can finish a
    // pending input gesture, so currentChanged alone is too late for pen safety.
    if (m_tabs && (watched == m_tabs || watched == m_tabs->tabBar()) &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::KeyPress)) {
        if (m_busy || (qobject_cast<MainWindow *>(m_tabs->currentWidget()) && !prepareNavigation())) {
            feedback("Finish the active edit before changing tabs.", true);
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}
void ProjectWorkspaceWindow::closeEvent(QCloseEvent *e) {
    if (!closeAllSessions()) {
        e->ignore();
        return;
    }
    m_settings->sync();
    e->accept();
}
void ProjectWorkspaceWindow::newDocumentDialog(bool animation) {
    if (!m_store || m_busy)
        return;
    SetupDialog d(animation ? "New animation" : "New drawing",
                  m_store->title() +
                      (folderPath(folder()).isEmpty() ? QString() : "  /  " + folderPath(folder())),
                  this);
    d.setObjectName("managedDocumentDialog");
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *body = new QWidget;
    auto *columns = new QHBoxLayout(body);
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(30);
    auto *form = new QVBoxLayout;
    form->setSpacing(10);
    columns->addLayout(form, 1);
    auto *name = new QLineEdit;
    name->setText(animation ? "Untitled animation" : "Untitled drawing");
    name->setObjectName("newDocumentName");
    name->setAccessibleName(animation ? "Animation name" : "Drawing name");
    form->addWidget(new QLabel("Name"));
    form->addWidget(name);
    auto *canvasHeader = new QHBoxLayout;
    canvasHeader->addWidget(new QLabel("Canvas size"), 1);
    auto *lock = new QToolButton;
    lock->setObjectName("canvasAspectLock");
    lock->setIcon(appIcon("lock-keyhole-open"));
    lock->setCheckable(true);
    lock->setAccessibleName("Lock canvas aspect ratio");
    lock->setToolTip(lock->accessibleName());
    canvasHeader->addWidget(lock);
    auto *swap = new QToolButton;
    swap->setObjectName("canvasSwap");
    swap->setIcon(appIcon("arrow-left-right"));
    swap->setAccessibleName("Swap width and height");
    swap->setToolTip(swap->accessibleName());
    canvasHeader->addWidget(swap);
    form->addLayout(canvasHeader);
    auto *preset = new QTabBar;
    preset->setObjectName("canvasPresets");
    preset->setDrawBase(false);
    preset->setAccessibleName("Canvas preset");
    for (const auto &s : {"Landscape", "Square", "Portrait", "Custom"})
        preset->addTab(s);
    form->addWidget(preset);
    auto *width = new ValueSlider("Width · px", 64, 8192, 1920),
         *height = new ValueSlider("Height · px", 64, 8192, 1080);
    width->setObjectName("newDocumentWidth");
    height->setObjectName("newDocumentHeight");
    auto *sizes = new QFormLayout;
    sizes->addRow("Width · px", width);
    sizes->addRow("Height · px", height);
    form->addLayout(sizes);
    auto *background = new QTabBar;
    background->setObjectName("canvasBackground");
    background->setAccessibleName("Canvas background");
    background->setDrawBase(false);
    background->addTab(appIcon("square"), "White");
    background->addTab(appIcon("grid-2x2"), "Transparent");
    background->addTab(appIcon("palette"), "Color");
    form->addWidget(new QLabel("Background"));
    form->addWidget(background);
    QColor color = QColor("#c9b8a4");
    auto *swatch = new QPushButton;
    swatch->setObjectName("canvasColor");
    swatch->setAccessibleName("Choose canvas background color");
    swatch->setToolTip(swatch->accessibleName());
    swatch->setAutoDefault(false);
    swatch->setVisible(false);
    form->addWidget(swatch);
    auto *preview = new CanvasPreview;
    preview->setObjectName("newDrawingPreview");
    columns->addWidget(preview, 1, Qt::AlignTop);
    preview->setMinimumHeight(300);
    bool syncing = false;
    double ratio = 1920. / 1080.;
    const auto update = [&] {
        preview->setCanvas(width->value(), height->value(),
                           background->currentIndex() == 2 ? color : QColor(Qt::white),
                           background->currentIndex() == 1);
        swatch->setText(color.name().toUpper());
        swatch->setIcon(QIcon());
        swatch->setStyleSheet(QString("QPushButton{border-left:24px solid %1;}").arg(color.name()));
    };
    const auto dimensionsChanged = [&](bool wide) {
        if (syncing)
            return;
        syncing = true;
        if (lock->isChecked()) {
            if (wide)
                height->setValue(qBound(64., qRound(width->value() / ratio) * 1., 8192.));
            else
                width->setValue(qBound(64., qRound(height->value() * ratio) * 1., 8192.));
        }
        const QSignalBlocker block(preset);
        int index = 3;
        if (width->value() == 1920 && height->value() == 1080)
            index = 0;
        else if (width->value() == 2048 && height->value() == 2048)
            index = 1;
        else if (width->value() == 1080 && height->value() == 1920)
            index = 2;
        preset->setCurrentIndex(index);
        syncing = false;
        update();
    };
    connect(width, &ValueSlider::valueChanged, &d, [&] { dimensionsChanged(true); });
    connect(height, &ValueSlider::valueChanged, &d, [&] { dimensionsChanged(false); });
    connect(preset, &QTabBar::currentChanged, &d, [&](int index) {
        if (syncing || index == 3)
            return;
        syncing = true;
        const QSize sizes[] = {{1920, 1080}, {2048, 2048}, {1080, 1920}};
        width->setValue(sizes[index].width());
        height->setValue(sizes[index].height());
        ratio = width->value() / height->value();
        syncing = false;
        update();
    });
    connect(lock, &QToolButton::toggled, &d, [&](bool on) {
        ratio = width->value() / height->value();
        lock->setIcon(appIcon(on ? "lock-keyhole" : "lock-keyhole-open"));
    });
    connect(swap, &QToolButton::clicked, &d, [&] {
        syncing = true;
        auto w = width->value();
        width->setValue(height->value());
        height->setValue(w);
        ratio = width->value() / height->value();
        syncing = false;
        dimensionsChanged(true);
    });
    connect(background, &QTabBar::currentChanged, &d, [&](int index) {
        swatch->setVisible(index == 2);
        update();
    });
    connect(swatch, &QPushButton::clicked, &d, [&] {
        auto chosen = QColorDialog::getColor(color, &d, "Canvas background");
        if (chosen.isValid()) {
            color = chosen;
            update();
        }
    });
    update();
    ValueSlider *fps = nullptr, *frames = nullptr;
    if (animation) {
        fps = new ValueSlider("Frame rate", 1, 120, 24, 3);
        fps->setObjectName("newDocumentFps");
        frames = new ValueSlider("Frames", 1, 100000, 120);
        frames->setObjectName("newDocumentFrames");
        frames->setSliderRange(1, 3600);
        auto *timing = new QFormLayout;
        timing->addRow("Frame rate", fps);
        timing->addRow("Frames", frames);
        form->addLayout(timing);
    }
    TagEditor *tags = nullptr;
    if (m_store->type() == "movie") {
        form->addWidget(new QLabel("Tags"));
        tags = new TagEditor;
        form->addWidget(tags);
    }
    form->addStretch();
    scroll->setWidget(body);
    d.content->addWidget(scroll);
    auto *cancel = button("Cancel", "cancelDocument", "x", d.footer),
         *create = button(animation ? "Create animation" : "Create drawing", "createManagedDocument", "plus",
                          d.footer);
    create->setProperty("primary", true);
    create->setDefault(true);
    connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
    connect(create, &QPushButton::clicked, &d, [&] {
        width->commitEditing();
        height->commitEditing();
        if (fps)
            fps->commitEditing();
        if (frames)
            frames->commitEditing();
        const auto n = name->text().trimmed();
        if (n.isEmpty()) {
            d.error->setText("Enter a name.");
            d.error->show();
            return;
        }
        const int pixelsWide = width->value(), pixelsHigh = height->value(),
                  frameCount = frames ? frames->value() : 1;
        const double rate = fps ? fps->value() : 24;
        const bool transparent = background->currentIndex() == 1;
        const QColor chosenColor = background->currentIndex() == 2 ? color : QColor(Qt::white);
        QString id, error;
        const auto store = m_store;
        const auto destination = folder();
        const auto labels = tags ? tags->tags() : QStringList{};
        m_busy = true;
        const bool ok = job(
            &d, "Creating document…",
            [&id, store, n, animation, pixelsWide, pixelsHigh, chosenColor, transparent, rate, frameCount,
             destination, labels](QString *e, const auto &cancel) {
                if (cancel())
                    return false;
                auto p = newManagedDocument(n, animation, pixelsWide, pixelsHigh, chosenColor, transparent,
                                            rate, frameCount);
                id = store->createDocument(n, animation ? "animation" : "drawing", p, destination, labels, e,
                                           cancel);
                return !id.isEmpty();
            },
            &error);
        m_busy = false;
        if (!ok) {
            d.error->setText(error);
            d.error->show();
            return;
        }
        d.accept();
        QTimer::singleShot(0, this, [this, id] {
            refreshDashboard();
            openItems({id});
        });
    });
    d.resize(1040, m_store->type() == "movie" ? 760 : 600);
    name->selectAll();
    name->setFocus();
    execSetupDialog(d);
}
void ProjectWorkspaceWindow::newFolder() {
    if (!m_store || m_busy)
        return;
    SetupDialog d(
        "New folder",
        m_store->title() + (folderPath(folder()).isEmpty() ? QString() : " / " + folderPath(folder())), this);
    auto *name = new QLineEdit;
    name->setAccessibleName("Folder name");
    name->setPlaceholderText("Folder name");
    d.content->addWidget(name);
    auto *cancel = button("Cancel", "cancelFolder", "x", d.footer),
         *create = button("Create folder", "createFolder", "folder-plus", d.footer);
    create->setProperty("primary", true);
    create->setDefault(true);
    connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
    connect(create, &QPushButton::clicked, &d, [&] {
        QString error;
        if (m_store->createFolder(name->text(), folder(), collection(), &error).isEmpty()) {
            d.error->setText(error);
            d.error->show();
            return;
        }
        d.accept();
        refreshDashboard();
    });
    d.resize(680, 240);
    name->setFocus();
    execSetupDialog(d);
}
void ProjectWorkspaceWindow::importAssets() {
    if (!m_store || m_busy)
        return;
    SetupDialog d("Import assets", "Add images, video and audio to your project.", this);
    d.setObjectName("managedImportDialog");
    auto *columns = new QHBoxLayout;
    columns->setSpacing(28);
    auto *left = new QVBoxLayout;
    auto *right = new QVBoxLayout;
    columns->addLayout(left, 3);
    columns->addLayout(right, 2);
    d.content->addLayout(columns, 1);
    auto *files = new QTreeWidget;
    files->setObjectName("importFiles");
    files->setHeaderLabels({"File", "Type", "Size"});
    files->setRootIsDecorated(false);
    files->setSelectionMode(QAbstractItemView::ExtendedSelection);
    files->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    files->setColumnWidth(1, 65);
    files->setColumnWidth(2, 65);
    files->setMinimumHeight(260);
    left->addWidget(files, 1);
    auto *fileActions = new QHBoxLayout;
    auto *add = button("Add files…", "addImportFiles", "plus", fileActions),
         *remove = button("Remove", "removeImportFiles", "x", fileActions);
    fileActions->addStretch();
    left->addLayout(fileActions);
    auto *copyInfo = new QLabel("Files are copied into this project. Your originals stay in place.");
    copyInfo->setProperty("muted", true);
    copyInfo->setWordWrap(true);
    left->addWidget(copyInfo);
    right->addWidget(new QLabel("Import to folder"));
    auto *folders = new QComboBox;
    folders->setAccessibleName("Import destination folder");
    folders->addItem("All assets", QString());
    for (const auto &f : m_store->folders())
        if (f.collection == "assets")
            folders->addItem(folderPath(f.id), f.id);
    folders->setCurrentIndex(qMax(0, folders->findData(folder())));
    right->addWidget(folders);
    right->addWidget(new QLabel("Tags"));
    auto *tags = new TagEditor;
    right->addWidget(tags);
    right->addStretch();
    auto *cancel = button("Cancel", "cancelImport", "x", d.footer),
         *import = button("Import assets", "confirmImportAssets", "download", d.footer);
    import->setProperty("primary", true);
    import->setEnabled(false);
    connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
    const auto update = [&] {
        import->setEnabled(files->topLevelItemCount() > 0);
        import->setText(QString("Import %1 assets").arg(files->topLevelItemCount()));
    };
    connect(add, &QPushButton::clicked, &d, [&] {
        const auto paths = QFileDialog::getOpenFileNames(
            &d, "Import images, video and audio", {},
            "Media (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff *.gif *.mp4 *.mov "
            "*.mkv *.webm *.avi *.wav *.mp3 *.m4a *.ogg *.flac *.aac)");
        for (const auto &path : paths) {
            bool exists = false;
            for (int n = 0; n < files->topLevelItemCount(); ++n)
                exists |= files->topLevelItem(n)->data(0, Qt::UserRole).toString() == path;
            if (exists)
                continue;
            QFileInfo info(path);
            auto *row = new QTreeWidgetItem(
                files, {info.fileName(), info.suffix().toUpper(), QLocale().formattedDataSize(info.size())});
            row->setData(0, Qt::UserRole, path);
            row->setToolTip(0, path);
            row->setIcon(0, appIcon("file"));
        }
        update();
    });
    connect(remove, &QPushButton::clicked, &d, [&] {
        qDeleteAll(files->selectedItems());
        update();
    });
    connect(import, &QPushButton::clicked, &d, [&] {
        QStringList paths;
        for (int n = 0; n < files->topLevelItemCount(); ++n)
            paths.append(files->topLevelItem(n)->data(0, Qt::UserRole).toString());
        if (paths.isEmpty())
            return;
        const auto labels = tags->tags();
        const auto destination = folders->currentData().toString();
        const auto store = m_store;
        QString error;
        m_busy = true;
        const bool ok = job(
            &d, "Importing assets…",
            [store, paths, destination, labels](QString *e, const auto &cancel) {
                return store->importFiles(paths, destination, labels, e, cancel);
            },
            &error);
        m_busy = false;
        if (!ok) {
            d.error->setText(error);
            d.error->show();
            return;
        }
        d.accept();
        refreshDashboard();
        feedback("Assets imported");
    });
    d.resize(1060, 650);
    execSetupDialog(d);
}
void ProjectWorkspaceWindow::organizeSelected(bool move) {
    if (!m_store || m_busy)
        return;
    const auto ids = selectedItems();
    if (ids.isEmpty())
        return;
    auto items = m_store->items();
    auto it = std::find_if(items.begin(), items.end(), [&](const auto &i) { return i.id == ids.first(); });
    if (it == items.end())
        return;
    const auto target =
        move ? (collection() == "content" ? QString("assets") : QString("content")) : collection();
    SetupDialog d(
        move ? (target == "assets" ? "Move to Assets" : "Move to Content") : "Tags and folder",
        move ? "Keep your original editable artwork and animation." : "Organize the selected items.", this);
    d.setObjectName("managedOrganizeDialog");
    auto *summary = new QHBoxLayout;
    auto *cover = new QLabel;
    cover->setFixedSize(172, 100);
    const auto key = thumbnailKey(m_store->root(), *it);
    if (auto *pix = m_thumbnails.object(key))
        cover->setPixmap(pix->scaled(172, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    else
        cover->setPixmap(appIcon(it->kind == "drawing" ? "image" : "clapperboard").pixmap(56));
    summary->addWidget(cover);
    auto *summaryText = new QVBoxLayout;
    summaryText->addWidget(
        new QLabel(ids.size() == 1 ? it->name : QString("%1 selected items").arg(ids.size())));
    auto *details =
        new QLabel(kindName(it->kind) +
                   (ids.size() == 1 ? " · " + m_thumbnailMetadata.value(key).value("dimensions").toString()
                                    : QString()));
    details->setProperty("muted", true);
    summaryText->addWidget(details);
    summary->addLayout(summaryText, 1);
    d.content->addLayout(summary);
    d.content->addWidget(new QLabel(target == "assets" ? "Asset folder" : "Content folder"));
    auto *folders = new QComboBox;
    folders->setAccessibleName("Destination folder");
    folders->addItem(target == "assets" ? "All assets" : "All content", QString());
    for (const auto &f : m_store->folders())
        if (f.collection == target)
            folders->addItem(folderPath(f.id), f.id);
    if (!move)
        folders->setCurrentIndex(qMax(0, folders->findData(it->folder)));
    d.content->addWidget(folders);
    d.content->addWidget(new QLabel("Tags"));
    auto *tags = new TagEditor(move && ids.size() > 1 ? QStringList{} : it->tags);
    d.content->addWidget(tags);
    if (ids.size() > 1) {
        auto *note = new QLabel(move ? "New tags are added to each item’s existing tags."
                                     : "These tags replace the tags on all selected items.");
        note->setProperty("muted", true);
        note->setWordWrap(true);
        d.content->addWidget(note);
    }
    auto *cancel = button("Cancel", "cancelOrganize", "x", d.footer),
         *apply = button(move ? (target == "assets" ? "Move to Assets" : "Move to Content") : "Save changes",
                         "confirmOrganize", "check", d.footer);
    apply->setProperty("primary", true);
    connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
    connect(apply, &QPushButton::clicked, &d, [&] {
        QString error;
        if (!m_store->organize(ids, target, folders->currentData().toString(), tags->tags(), &error,
                               move && ids.size() > 1)) {
            d.error->setText(error);
            d.error->show();
            return;
        }
        d.accept();
        refreshDashboard();
    });
    d.resize(760, 650);
    execSetupDialog(d);
}
void ProjectWorkspaceWindow::itemMenu(const QPoint &point) {
    if (!m_store || m_busy)
        return;
    const auto ids = selectedItems();
    if (ids.isEmpty())
        return;
    QMenu menu(this);
    auto *open =
        menu.addAction(collection() == "assets" ? "Open / preview selected" : "Open selected in tabs");
    auto *tags = menu.addAction("Edit tags / folder…");
    auto *move = menu.addAction(collection() == "content" ? "Move to Assets…" : "Move to Content…");
    move->setEnabled(std::all_of(ids.begin(), ids.end(), [&](const auto &id) {
        for (const auto &item : m_store->items())
            if (item.id == id)
                return item.kind == "drawing" || item.kind == "animation";
        return false;
    }));
    auto *rename = menu.addAction("Rename…");
    auto *remove = menu.addAction(m_showRemoved ? "Restore" : "Remove");
    const auto *chosen = menu.exec(m_items->viewport()->mapToGlobal(point));
    if (chosen == open)
        openItems(ids);
    else if (chosen == tags)
        organizeSelected(false);
    else if (chosen == move)
        organizeSelected(true);
    else if (chosen == rename) {
        bool ok = false;
        auto n = QInputDialog::getText(this, "Rename item", "Name", QLineEdit::Normal, {}, &ok);
        if (ok) {
            QString e;
            if (!m_store->rename(ids.first(), n, &e))
                feedback(e, true);
        }
    } else if (chosen == remove) {
        QString e;
        if (!m_store->setRemoved(ids, !m_showRemoved, &e))
            feedback(e, true);
    }
    refreshDashboard();
}
void ProjectWorkspaceWindow::stopPreview() {
    ++m_previewGeneration;
    if (m_player) {
        m_player->stop();
        m_player->setSource({});
    }
    if (m_video)
        m_video->hide();
    if (m_mediaControls)
        m_mediaControls->hide();
    if (m_preview) {
        m_preview->show();
        m_preview->clear();
        m_preview->setText("Select an asset");
    }
}
void ProjectWorkspaceWindow::previewSelected() {
    if (m_refreshing || m_busy || !m_store || !m_inspector->isVisible())
        return;
    stopPreview();
    const auto ids = selectedItems();
    m_assetTags->setEnabled(ids.size() == 1);
    if (ids.size() != 1) {
        m_inspectorName->setText(ids.isEmpty() ? "Asset details"
                                               : QString("%1 selected assets").arg(ids.size()));
        m_details->clear();
        m_inspectedId.clear();
        m_inspectedTags.clear();
        m_assetTags->setTags({});
        return;
    }
    const auto items = m_store->items();
    auto it = std::find_if(items.begin(), items.end(), [&](const auto &i) { return i.id == ids.first(); });
    if (it == items.end())
        return;
    const auto key = thumbnailKey(m_store->root(), *it);
    m_inspectorName->setText(it->name);
    if (m_inspectedId != it->id || m_inspectedTags != it->tags) {
        m_inspectedId = it->id;
        m_inspectedTags = it->tags;
        m_assetTags->setTags(it->tags);
    }
    auto meta = m_thumbnailMetadata.value(key);
    m_details->setText(kindName(it->kind) +
                       (it->kind == "drawing" || it->kind == "animation" ? " · Editable source" : QString()) +
                       "\n" + meta.value("dimensions").toString() +
                       (it->bytes ? "\n" + QLocale().formattedDataSize(it->bytes) : QString()) +
                       (meta.value("duration").isValid()
                            ? "\n" + meta.value("duration").toString() + " · " + meta.value("rate").toString()
                            : QString()));
    if (auto *pix = m_thumbnails.object(key))
        m_preview->setPixmap(pix->scaled(300, 220, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    else
        m_preview->setPixmap(appIcon(it->kind == "audio"                              ? "music"
                                     : it->kind == "video" || it->kind == "animation" ? "clapperboard"
                                                                                      : "image")
                                 .pixmap(64));
    if (it->kind == "drawing" || it->kind == "animation" || it->kind == "image")
        return;
    QString error;
    const auto path = m_store->assetPath(it->id, &error);
    if (path.isEmpty()) {
        m_preview->setText(error);
        return;
    }
    m_player->setSource(QUrl::fromLocalFile(path));
    m_mediaControls->show();
    m_video->setVisible(it->kind == "video");
    m_preview->setVisible(it->kind != "video");
}
} // namespace toon
