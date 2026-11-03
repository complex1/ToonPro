#include "shots_panel.h"
#include "document.h"
#include "icons.h"
#include "ui_theme.h"
#include <QComboBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QHeaderView>
#include <QHash>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPainter>
#include <QPointer>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace toon {
namespace {
constexpr auto shotMime = "application/x-toon-pro-shot";
QString itemId(const QTreeWidgetItem *item) { return item ? item->data(0, Qt::UserRole).toString() : QString(); }
QToolButton *actionButton(const QString &id, const QString &icon, const QString &label) {
    auto *button = new QToolButton; button->setObjectName(id); button->setIcon(appIcon(icon));
    button->setIconSize({20, 20}); button->setFixedSize(32, 32);
    button->setToolTip(label); button->setAccessibleName(label); return button;
}
QString durationText(const Shot &shot, qreal fps) {
    return QString::number(shot.durationFrames / fps, 'f', 2) + " s";
}

// Keep editor widgets stable when an unrelated canvas change refreshes the list.
class ShotDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    mutable QPointer<QLineEdit> editor;
    mutable QString editingId;
    mutable int editingColumn = -1;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &index) const override {
        if (index.column() > 1) return nullptr;
        auto *edit = new QLineEdit(parent); edit->setObjectName("shotsInlineEditor");
        edit->setAccessibleName(index.column() == 0 ? "Shot number" : "Shot name"); edit->setMaxLength(index.column() == 0 ? 40 : 200);
        editor = edit; editingColumn = index.column(); editingId = index.siblingAtColumn(0).data(Qt::UserRole).toString();
        return edit;
    }
    bool editing(const QString &id, int column) const { return editor && editingId == id && editingColumn == column; }
};

class ShotTree : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;
    std::function<void(const QString &, const QString &, bool)> moveRequested;
    std::function<void()> deleteRequested;
protected:
    void startDrag(Qt::DropActions) override {
        const auto items = selectedItems();
        if (items.size() != 1 || items.first()->isHidden()) return;
        QDrag drag(this); auto *mime = new QMimeData;
        mime->setData(shotMime, itemId(items.first()).toUtf8()); drag.setMimeData(mime);
        drag.setPixmap(appIcon("clapperboard").pixmap(24, 24)); drag.exec(Qt::MoveAction);
        m_indicator = -1; viewport()->update();
    }
    void dragEnterEvent(QDragEnterEvent *event) override {
        if (event->source() == this && event->mimeData()->hasFormat(shotMime)) event->acceptProposedAction();
        else event->ignore();
    }
    void dragMoveEvent(QDragMoveEvent *event) override {
        if (event->source() != this || !event->mimeData()->hasFormat(shotMime)) { event->ignore(); return; }
        const auto target = dropTarget(event->position().toPoint());
        m_indicator = target.first ? (target.second ? visualItemRect(target.first).top() : visualItemRect(target.first).bottom()) : -1;
        const int edge = 20;
        if (event->position().y() < edge) verticalScrollBar()->setValue(verticalScrollBar()->value() - 1);
        else if (event->position().y() > viewport()->height() - edge) verticalScrollBar()->setValue(verticalScrollBar()->value() + 1);
        viewport()->update(); event->acceptProposedAction();
    }
    void dragLeaveEvent(QDragLeaveEvent *event) override { m_indicator = -1; viewport()->update(); event->accept(); }
    void dropEvent(QDropEvent *event) override {
        m_indicator = -1; viewport()->update();
        if (event->source() != this || !event->mimeData()->hasFormat(shotMime)) { event->ignore(); return; }
        const auto target = dropTarget(event->position().toPoint());
        if (target.first && moveRequested) moveRequested(QString::fromUtf8(event->mimeData()->data(shotMime)), itemId(target.first), target.second);
        event->setDropAction(Qt::MoveAction); event->accept();
    }
    void paintEvent(QPaintEvent *event) override {
        QTreeWidget::paintEvent(event);
        if (m_indicator >= 0) {
            QPainter painter(viewport()); painter.setPen(QPen(QColor(ui::accent), 3));
            painter.drawLine(2, m_indicator, viewport()->width() - 2, m_indicator);
        }
    }
    void keyPressEvent(QKeyEvent *event) override {
        if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && state() != EditingState) {
            if (deleteRequested) deleteRequested(); event->accept();
        } else QTreeWidget::keyPressEvent(event);
    }
private:
    std::pair<QTreeWidgetItem *, bool> dropTarget(const QPoint &point) const {
        if (auto *item = itemAt(point)) return {item, point.y() < visualItemRect(item).center().y()};
        for (int index = topLevelItemCount() - 1; index >= 0; --index)
            if (!topLevelItem(index)->isHidden()) return {topLevelItem(index), false};
        return {nullptr, false};
    }
    int m_indicator = -1;
};
} // namespace

ShotsPanel::ShotsPanel(Document *document, std::function<bool()> prepareTransition, QWidget *parent)
    : QWidget(parent), m_document(document), m_prepareTransition(std::move(prepareTransition)) {
    setObjectName("shotsManager"); setMinimumSize(260, 260); setFocusPolicy(Qt::StrongFocus);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(6, 6, 6, 6); layout->setSpacing(6);
    auto *actions = new QHBoxLayout; actions->setSpacing(3);
    m_add = actionButton("shotsAdd", "plus", "Create shot");
    m_duplicate = actionButton("shotsDuplicate", "copy", "Duplicate selected shot");
    m_delete = actionButton("shotsDelete", "trash", "Delete selected shots — Undo restores them");
    m_up = actionButton("shotsMoveUp", "arrow-up", "Move selected shot earlier");
    m_down = actionButton("shotsMoveDown", "arrow-down", "Move selected shot later");
    m_export = actionButton("shotsExport", "video", "Export selected shots in movie order");
    for (auto *button : {m_add, m_duplicate, m_delete, m_up, m_down}) actions->addWidget(button);
    actions->addStretch(); actions->addWidget(m_export); layout->addLayout(actions);

    m_sceneFilter = new QComboBox; m_sceneFilter->setObjectName("shotsSceneFilter");
    m_sceneFilter->setAccessibleName("Filter shots by scene"); m_sceneFilter->setToolTip("Filter by scene; movie order stays unchanged");
    m_sceneFilter->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); m_sceneFilter->setMinimumContentsLength(8);
    layout->addWidget(m_sceneFilter);
    auto *tree = new ShotTree; m_list = tree; tree->setObjectName("shotsList"); tree->setAccessibleName("Movie shots");
    tree->setHeaderLabels({"Number", "Shot", "Length"}); tree->setRootIsDecorated(false); tree->setIndentation(0);
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection); tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    tree->setUniformRowHeights(true); tree->setIconSize({16, 16}); tree->setTextElideMode(Qt::ElideRight);
    tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); tree->setDragEnabled(true); tree->setAcceptDrops(true);
    tree->setDropIndicatorShown(false); tree->setDragDropMode(QAbstractItemView::InternalMove);
    tree->setDefaultDropAction(Qt::MoveAction); tree->setSortingEnabled(false);
    tree->setToolTip("Select a shot to open it. Double-click its number or name to rename. Drag one shot to change movie order.");
    tree->header()->setSectionsMovable(false); tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::Fixed); tree->setColumnWidth(0, 78);
    tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(2, QHeaderView::Fixed); tree->setColumnWidth(2, 64);
    tree->setItemDelegate(new ShotDelegate(tree)); layout->addWidget(tree, 1);
    m_summary = new QLabel; m_summary->setObjectName("shotsSummary"); m_summary->setTextFormat(Qt::PlainText);
    m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); layout->addWidget(m_summary);
    auto *sceneRow = new QHBoxLayout; sceneRow->setSpacing(6); sceneRow->addWidget(new QLabel("Scene"));
    m_scene = new QComboBox; m_scene->setObjectName("shotsScene"); m_scene->setEditable(true); m_scene->setInsertPolicy(QComboBox::NoInsert);
    m_scene->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); m_scene->setMinimumContentsLength(6);
    m_scene->setAccessibleName("Scene for selected shots"); m_scene->setToolTip("Assign selected shots to a scene; type a new name or choose an existing scene");
    m_scene->lineEdit()->setMaxLength(100); sceneRow->addWidget(m_scene, 1); layout->addLayout(sceneRow);
    m_status = new QLabel; m_status->setObjectName("shotsStatus"); m_status->setTextFormat(Qt::PlainText);
    m_status->setFixedHeight(fontMetrics().height() + 4); m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    layout->addWidget(m_status);

    connect(tree, &QTreeWidget::itemSelectionChanged, this, &ShotsPanel::queueSelection);
    connect(tree, &QTreeWidget::currentItemChanged, this, &ShotsPanel::queueSelection);
    connect(tree, &QTreeWidget::itemChanged, this, &ShotsPanel::renameItem);
    tree->moveRequested = [this](const QString &source, const QString &target, bool before) { moveDropped(source, target, before); };
    tree->deleteRequested = [this] { deleteSelected(); };
    connect(m_sceneFilter, &QComboBox::currentIndexChanged, this, [this] { if (!m_refreshing) refresh(); });
    connect(m_scene->lineEdit(), &QLineEdit::editingFinished, this, &ShotsPanel::assignScene);
    connect(m_scene, &QComboBox::activated, this, &ShotsPanel::assignScene);
    connect(m_add, &QToolButton::clicked, this, [this] {
        perform([this] {
            const auto scene = m_sceneFilter->currentData().toString();
            const auto id = m_document->createShot({}, scene); if (id.isEmpty()) return false;
            m_followActive = true; return true;
        }, "Could not create a shot.");
    });
    connect(m_duplicate, &QToolButton::clicked, this, [this] {
        if (!settleSelection()) return; const auto ids = selectedShotIds(); if (ids.size() != 1) return;
        perform([this, ids] { const auto id = m_document->duplicateShot(ids.first()); m_followActive = !id.isEmpty(); return !id.isEmpty(); }, "Could not duplicate this shot.");
    });
    connect(m_delete, &QToolButton::clicked, this, &ShotsPanel::deleteSelected);
    connect(m_up, &QToolButton::clicked, this, [this] { moveSelected(-1); });
    connect(m_down, &QToolButton::clicked, this, [this] { moveSelected(1); });
    connect(m_export, &QToolButton::clicked, this, [this] {
        if (!settleSelection()) return; const auto ids = selectedShotIds(); if (ids.isEmpty()) return;
        if (perform([] { return true; }, {})) emit exportRequested(ids);
    });
    connect(document, &Document::loaded, this, [this] { m_selectionPending = false; m_followActive = true; requestRefresh(); });
    connect(document, &Document::changed, this, &ShotsPanel::requestRefresh);
    connect(document, &Document::shotChanged, this, [this] {
        // Undo and external navigation supersede an older queued list click.
        // Otherwise that stale click can postpone the active-row refresh and
        // later collapse a new multi-selection back to one active shot.
        if (!m_processing) { m_selectionPending = false; m_followActive = true; }
        requestRefresh();
    });
    connect(document, &QObject::destroyed, this, [this] { m_document = nullptr; refresh(); });
    refresh();
}

int ShotsPanel::shotIndex(const QString &id) const {
    if (!m_document || !m_document->isOpen()) return -1;
    const auto &shots = m_document->project().shots;
    for (int index = 0; index < shots.size(); ++index) if (shots[index].id == id) return index;
    return -1;
}

QStringList ShotsPanel::selectedShotIds() const {
    QStringList result;
    for (int row = 0; row < m_list->topLevelItemCount(); ++row) {
        const auto *item = m_list->topLevelItem(row);
        if (!item->isHidden() && item->isSelected()) result.append(itemId(item));
    }
    return result;
}

void ShotsPanel::setStatus(const QString &text) {
    m_status->setToolTip(text); m_status->setAccessibleName(text);
    m_status->setText(m_status->fontMetrics().elidedText(text, Qt::ElideRight, qMax(1, m_status->width())));
}

void ShotsPanel::requestRefresh() {
    m_pendingRefresh = true;
    if (!m_processing && !m_refreshing && !m_selectionPending && isVisible()) refresh();
}

void ShotsPanel::showEvent(QShowEvent *event) { QWidget::showEvent(event); refresh(); }

void ShotsPanel::restoreSelection(const QStringList &ids, const QString &current) {
    const QSignalBlocker block(m_list); m_list->clearSelection(); QTreeWidgetItem *target = nullptr;
    for (int row = 0; row < m_list->topLevelItemCount(); ++row) {
        auto *item = m_list->topLevelItem(row); const auto id = itemId(item);
        item->setSelected(!item->isHidden() && ids.contains(id));
        if (!item->isHidden() && id == current) target = item;
    }
    m_list->setCurrentItem(target, 1, QItemSelectionModel::NoUpdate);
}

void ShotsPanel::refresh() {
    if (m_processing || m_refreshing) { m_pendingRefresh = true; return; }
    m_refreshing = true; m_pendingRefresh = false;
    const QSignalBlocker listBlock(m_list), filterBlock(m_sceneFilter), sceneBlock(m_scene);
    QStringList selected = selectedShotIds(); QString current = itemId(m_list->currentItem());
    const bool open = m_document && m_document->isOpen();
    QString filter = m_sceneFilter->currentData().toString(); QStringList scenes;
    if (open) for (const auto &shot : m_document->project().shots) if (!scenes.contains(shot.scene)) scenes.append(shot.scene);
    if (m_followActive && open) {
        selected = {m_document->activeShotId()}; current = selected.first();
        const int index = shotIndex(current);
        if (index >= 0 && !filter.isEmpty() && m_document->project().shots[index].scene != filter) filter.clear();
    }
    scenes.sort(Qt::CaseInsensitive); m_sceneFilter->clear(); m_sceneFilter->addItem("All scenes", QString());
    for (const auto &scene : scenes) m_sceneFilter->addItem(scene, scene);
    m_sceneFilter->setCurrentIndex(qMax(0, m_sceneFilter->findData(filter))); filter = m_sceneFilter->currentData().toString();
    QHash<QString, QTreeWidgetItem *> existing;
    for (int row = 0; row < m_list->topLevelItemCount(); ++row) existing.insert(itemId(m_list->topLevelItem(row)), m_list->topLevelItem(row));
    auto *delegate = static_cast<ShotDelegate *>(m_list->itemDelegate());
    qint64 frames = 0; int visible = 0;
    if (open) {
        const auto &project = m_document->project();
        for (int index = 0; index < project.shots.size(); ++index) {
            const auto &shot = project.shots[index]; auto *item = existing.take(shot.id);
            if (!item) { item = new QTreeWidgetItem; item->setData(0, Qt::UserRole, shot.id); item->setFlags(item->flags() | Qt::ItemIsEditable); }
            if (m_list->indexOfTopLevelItem(item) != index) {
                const int old = m_list->indexOfTopLevelItem(item); if (old >= 0) m_list->takeTopLevelItem(old);
                m_list->insertTopLevelItem(index, item);
            }
            const QStringList texts{shot.number, shot.name, durationText(shot, project.fps)};
            for (int column = 0; column < texts.size(); ++column) {
                if (!delegate->editing(shot.id, column) && item->text(column) != texts[column]) item->setText(column, texts[column]);
                item->setToolTip(column, QString("%1 · %2\n%3 · %4 frames · %5").arg(shot.number, shot.name, shot.scene).arg(shot.durationFrames).arg(texts[2]));
            }
            const bool active = shot.id == m_document->activeShotId();
            item->setIcon(0, active ? appIcon("clapperboard") : QIcon());
            auto font = item->font(1); font.setBold(active); item->setFont(1, font);
            item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter); item->setSizeHint(0, QSize(0, 32));
            item->setHidden(!filter.isEmpty() && shot.scene != filter); frames += shot.durationFrames;
            if (!item->isHidden()) ++visible;
        }
    }
    for (auto *item : existing) delete item;
    restoreSelection(selected, current); m_settledSelection = selectedShotIds(); m_settledCurrent = itemId(m_list->currentItem());
    const auto ids = m_settledSelection; QString sceneValue; bool mixed = false;
    for (const auto &id : ids) {
        const int index = shotIndex(id); if (index < 0) continue; const auto scene = m_document->project().shots[index].scene;
        if (sceneValue.isEmpty()) sceneValue = scene; else if (sceneValue != scene) mixed = true;
    }
    if (mixed) sceneValue.clear();
    const bool newTargets = ids != m_sceneTargets;
    const bool draft = m_scene->lineEdit()->isModified() && !newTargets && sceneValue == m_sceneValue;
    if (!draft) {
        m_scene->clear(); m_scene->addItems(scenes); m_scene->setCurrentIndex(-1); m_scene->setEditText(sceneValue);
        m_scene->lineEdit()->setModified(false);
    }
    m_scene->lineEdit()->setPlaceholderText(mixed ? "Multiple scenes" : "Scene name");
    m_sceneTargets = ids; m_sceneValue = sceneValue;
    m_summary->setText(!open ? "No movie open" : QString("%1 shots · %2 s%3").arg(m_document->project().shots.size())
        .arg(frames / m_document->project().fps, 0, 'f', 2)
        .arg(filter.isEmpty() ? QString() : QString(" · %1 shown").arg(visible)));
    m_summary->setToolTip(m_summary->text());
    m_followActive = false; m_refreshing = false; updateActions();
}

void ShotsPanel::updateActions() {
    const bool open = m_document && m_document->isOpen(); const auto ids = selectedShotIds(); const bool single = ids.size() == 1;
    const int index = single ? shotIndex(ids.first()) : -1; const auto count = open ? m_document->project().shots.size() : 0;
    m_add->setEnabled(open && count < maxProjectShots); m_duplicate->setEnabled(open && single && count < maxProjectShots);
    m_delete->setEnabled(open && !ids.isEmpty() && ids.size() < count);
    m_up->setEnabled(index > 0); m_down->setEnabled(index >= 0 && index + 1 < count);
    m_export->setEnabled(open && !ids.isEmpty()); m_scene->setEnabled(open && !ids.isEmpty());
    m_sceneFilter->setEnabled(open); m_list->setEnabled(open);
}

void ShotsPanel::queueSelection() {
    if (m_refreshing || m_processing) return;
    m_followActive = false;
    if (m_selectionPending) return;
    m_selectionPending = true; QTimer::singleShot(0, this, [this] { if (m_selectionPending) settleSelection(); });
}

bool ShotsPanel::settleSelection() {
    if (m_processing || m_refreshing) return false;
    m_selectionPending = false;
    if (!m_document || !m_document->isOpen()) return false;
    auto *item = m_list->currentItem(); const auto id = itemId(item);
    if (item && item->isSelected() && !item->isHidden() && id != m_document->activeShotId()) {
        m_processing = true;
        const bool prepared = !m_prepareTransition || m_prepareTransition();
        const bool success = prepared && m_document->selectShot(id);
        m_processing = false;
        if (!success) {
            restoreSelection(m_settledSelection, m_settledCurrent); m_followActive = false;
            setStatus(prepared ? "Could not open this shot." : "Shot switch canceled."); refresh(); return false;
        }
    }
    m_settledSelection = selectedShotIds(); m_settledCurrent = itemId(m_list->currentItem());
    setStatus({}); refresh(); return true;
}

bool ShotsPanel::perform(const std::function<bool()> &action, const QString &failure) {
    if (m_processing || !m_document || !m_document->isOpen()) return false;
    if (m_selectionPending && !settleSelection()) return false;
    m_processing = true; const bool prepared = !m_prepareTransition || m_prepareTransition();
    const bool result = prepared && action(); m_processing = false;
    setStatus(result ? QString() : prepared ? failure : QString("Change canceled.")); refresh(); return result;
}

void ShotsPanel::renameItem(QTreeWidgetItem *item, int column) {
    if (m_refreshing || m_processing || column > 1 || !item) return;
    const auto id = itemId(item); const auto number = item->text(0).trimmed(), name = item->text(1).trimmed();
    const int index = shotIndex(id); if (index < 0) return;
    const auto &shot = m_document->project().shots[index]; if (shot.number == number && shot.name == name) return;
    if (!perform([this, id, number, name] { return m_document->renameShot(id, number, name); }, "Shot number and name are required.")) {
        const int at = shotIndex(id); if (at < 0) return;
        const QSignalBlocker block(m_list); const auto &saved = m_document->project().shots[at];
        item->setText(0, saved.number); item->setText(1, saved.name);
    }
}

void ShotsPanel::assignScene() {
    if (m_refreshing || m_processing || !m_document || !m_document->isOpen()) return;
    const auto ids = selectedShotIds(); const auto scene = m_scene->currentText().trimmed();
    if (ids.isEmpty() || scene == m_sceneValue) return;
    if (scene.isEmpty()) { setStatus("Enter a scene name."); return; }
    perform([this, ids, scene] {
        const auto active = m_document->activeShotId();
        bool success = true; if (ids.size() > 1) m_document->history()->beginMacro("Assign shots to scene");
        for (const auto &id : ids) success = m_document->setShotScene(id, scene) && success;
        if (ids.size() > 1) m_document->history()->endMacro();
        if (m_document->activeShotId() != active) success = m_document->selectShot(active) && success;
        return success;
    }, "Could not assign this scene.");
}

void ShotsPanel::moveSelected(int offset) {
    if (!settleSelection()) return; const auto ids = selectedShotIds(); if (ids.size() != 1) return;
    const int destination = shotIndex(ids.first()) + offset;
    perform([this, ids, destination] { return m_document->moveShot(ids.first(), destination); }, "Could not move this shot.");
}

void ShotsPanel::moveDropped(const QString &source, const QString &target, bool before) {
    if (!settleSelection()) return;
    const int from = shotIndex(source), at = shotIndex(target); if (from < 0 || at < 0) return;
    int destination = at + (before ? 0 : 1); if (from < destination) --destination;
    if (destination == from) return;
    perform([this, source, destination] { return m_document->moveShot(source, destination); }, "Could not move this shot.");
}

void ShotsPanel::deleteSelected() {
    if (!settleSelection()) return; const auto ids = selectedShotIds();
    if (ids.isEmpty() || ids.size() >= m_document->project().shots.size()) { setStatus("Keep at least one shot in the movie."); return; }
    perform([this, ids] {
        bool success = true; if (ids.size() > 1) m_document->history()->beginMacro("Delete shots");
        for (const auto &id : ids) success = m_document->deleteShot(id) && success;
        if (ids.size() > 1) m_document->history()->endMacro();
        m_followActive = true; return success;
    }, "Could not delete these shots.");
}
} // namespace toon
