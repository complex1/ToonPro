#include "timeline_cue_panel.h"
#include "document.h"
#include "icons.h"
#include "property_row.h"
#include "timeline_controller.h"
#include "ui_metrics.h"
#include "value_slider.h"
#include <QColorDialog>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace toon {
namespace {
const QStringList cueIcons{"pin", "circle", "split", "arrow-left-right", "type"};
QString kindName(int index) { return timelineCueKindName(TimelineCueKind(index)); }
QToolButton *button(QWidget *parent, const char *id, const char *icon, const QString &help) {
    auto *result = new QToolButton(parent); result->setObjectName(id);
    result->setIcon(appIcon(icon)); result->setIconSize({16,16}); result->setFixedSize(28,28);
    result->setAutoRaise(true); result->setToolTip(help); result->setAccessibleName(help); return result;
}
QIcon colorIcon(const QColor &color) {
    QPixmap pixmap(32,32); pixmap.fill(Qt::transparent); pixmap.setDevicePixelRatio(2);
    QPainter painter(&pixmap); painter.setPen(QColor("#999999")); painter.setBrush(color); painter.drawRect(1,1,13,13);
    return QIcon(pixmap);
}
QString frameLabel(const TimelineCue &cue) {
    return cue.durationFrames == 1 ? QString::number(cue.startFrame + 1)
        : QString("%1–%2").arg(cue.startFrame + 1).arg(cue.startFrame + cue.durationFrames);
}
QString singleLineInput(QString text) {
    for (auto &ch : text)
        if (ch.category() == QChar::Other_Control || ch == QChar::LineSeparator || ch == QChar::ParagraphSeparator) ch = QLatin1Char(' ');
    return text.left(100);
}
class CueTabs final : public QTabBar {
public:
    using QTabBar::QTabBar;
protected:
    // Icon tabs must not inherit the theme's text-tab padding or minimum text
    // width. Keep every category reachable in the narrow inspector, allowing
    // only the height to grow with the artist's chosen font.
    QSize tabSizeHint(int) const override { return {ui::metrics::actionTarget,std::max(ui::metrics::tabHeight,fontMetrics().height()+6)}; }
    QSize minimumTabSizeHint(int index) const override { return tabSizeHint(index); }
};
}
TimelineCuePanel::TimelineCuePanel(TimelineController *controller, QWidget *parent)
    : QWidget(parent), m_controller(controller) {
    setObjectName("timelineCueProperties"); setMinimumWidth(0);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(6,5,6,5); layout->setSpacing(4);
    auto *header = new QHBoxLayout; header->setSpacing(2);
    m_add = button(this,"cueAdd","plus","Add a timeline cue");
    m_delete = button(this,"cueDelete","trash","Delete the selected cue");
    m_previous = button(this,"cuePrevious","chevron-left","Go to the previous cue");
    m_next = button(this,"cueNext","chevron-right","Go to the next cue");
    auto *menu = new QMenu(m_add);
    for (int i = 0; i < cueIcons.size(); ++i) {
        auto *action = menu->addAction(appIcon(cueIcons[i]),kindName(i));
        action->setObjectName("cueAdd" + timelineCueKindKey(TimelineCueKind(i)));
        connect(action,&QAction::triggered,this,[this,i] { m_controller->addCue(TimelineCueKind(i)); });
    }
    m_add->setMenu(menu); m_add->setPopupMode(QToolButton::InstantPopup);
    header->addWidget(m_add); header->addWidget(m_delete); header->addStretch();
    header->addWidget(m_previous); header->addWidget(m_next); layout->addLayout(header);
    m_search = new QLineEdit; m_search->setObjectName("cueSearch"); m_search->setClearButtonEnabled(true);
    m_search->setPlaceholderText("Search cues"); m_search->setAccessibleName("Search timeline cues");
    m_search->setToolTip("Find cues by name, type, speaker or dialogue"); layout->addWidget(m_search);
    for (auto *clear : m_search->findChildren<QToolButton *>()) {
        clear->setToolTip("Clear cue search"); clear->setAccessibleName("Clear cue search");
    }
    m_list = new QListWidget; m_list->setObjectName("cueList"); m_list->setAccessibleName("Timeline cues in this shot");
    m_list->setMinimumWidth(0); m_list->setMinimumHeight(72); m_list->setMaximumHeight(168);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); m_list->setTextElideMode(Qt::ElideRight);
    m_list->setIconSize({16,16}); m_list->setUniformItemSizes(true); layout->addWidget(m_list);
    m_controls = new QWidget; auto *controls = new QVBoxLayout(m_controls);
    controls->setContentsMargins(0,0,0,0); controls->setSpacing(4);
    m_kind = new CueTabs; m_kind->setObjectName("cueKindTabs"); m_kind->setAccessibleName("Cue type");
    m_kind->setProperty("compactDrawingTabs",true);
    m_kind->setDrawBase(false); m_kind->setExpanding(true); m_kind->setIconSize({16,16}); m_kind->setUsesScrollButtons(false);
    for (int i = 0; i < cueIcons.size(); ++i) {
        m_kind->addTab(appIcon(cueIcons[i]),QString()); m_kind->setTabToolTip(i,kindName(i)); m_kind->setAccessibleTabName(i,kindName(i));
    }
    controls->addWidget(m_kind);
    m_name = new QLineEdit; m_name->setObjectName("cueName"); m_name->setAccessibleName("Cue name"); m_name->setMaxLength(100);
    m_name->setMinimumWidth(0); auto *nameRow = new PropertyRow("Name",m_name);
    m_color = button(this,"cueColor","palette","Change the cue color"); nameRow->setAccessory(m_color); controls->addWidget(nameRow);
    m_start = new ValueSlider("Cue start frame",1,1000000,1); m_start->setObjectName("cueStart");
    m_hold = new ValueSlider("Cue duration in frames",1,1000000,1); m_hold->setObjectName("cueHold");
    controls->addWidget(new PropertyRow("Start",m_start)); controls->addWidget(new PropertyRow("Hold",m_hold));
    m_speaker = new QLineEdit; m_speaker->setObjectName("cueSpeaker"); m_speaker->setAccessibleName("Dialogue speaker");
    m_speaker->setMaxLength(100); m_speaker->setMinimumWidth(0);
    m_speakerRow = new PropertyRow("Speaker",m_speaker); controls->addWidget(m_speakerRow);
    m_textLabel = new QLabel; m_textLabel->setTextFormat(Qt::PlainText); controls->addWidget(m_textLabel);
    m_text = new QPlainTextEdit; m_text->setObjectName("cueText"); m_text->setMinimumWidth(0);
    m_text->setMinimumHeight(60); m_text->setMaximumHeight(112); m_text->setTabChangesFocus(true);
    m_textLabel->setBuddy(m_text); controls->addWidget(m_text); layout->addWidget(m_controls);
    m_status = new QLabel; m_status->setObjectName("cueStatus"); m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true);
    layout->addWidget(m_status); layout->addStretch();
    m_name->installEventFilter(this); m_speaker->installEventFilter(this); m_text->installEventFilter(this);
    connect(m_name,&QLineEdit::textEdited,this,[this] {
        const auto clean = singleLineInput(m_name->text()); if (clean != m_name->text()) m_name->setText(clean); beginEdit(Name);
    });
    connect(m_speaker,&QLineEdit::textEdited,this,[this] {
        const auto clean = singleLineInput(m_speaker->text()); if (clean != m_speaker->text()) m_speaker->setText(clean); beginEdit(Speaker);
    });
    connect(m_name,&QLineEdit::editingFinished,this,&TimelineCuePanel::commitEditors);
    connect(m_speaker,&QLineEdit::editingFinished,this,&TimelineCuePanel::commitEditors);
    connect(m_text,&QPlainTextEdit::textChanged,this,[this] {
        if (m_refreshing || m_resolving) return;
        auto clean = m_text->toPlainText();
        clean.removeIf([](QChar ch) { return ch.category() == QChar::Other_Control && ch != QLatin1Char('\n') && ch != QLatin1Char('\t'); });
        clean = clean.left(4096);
        if (clean != m_text->toPlainText()) {
            const QSignalBlocker guard(m_text); auto cursor = m_text->textCursor(); const int position = cursor.position();
            m_text->setPlainText(clean); cursor = m_text->textCursor(); cursor.setPosition(std::min(int(clean.size()),position)); m_text->setTextCursor(cursor);
        }
        beginEdit(Text);
    });
    for (auto *field : {m_start,m_hold}) {
        const int kind = field == m_start ? Start : Hold;
        connect(field,&ValueSlider::editingStarted,this,[this,kind] { beginEdit(kind); });
        connect(field,&ValueSlider::editingFinished,this,[this,kind] { if (!m_refreshing && !m_resolving) { beginEdit(kind); commitEditors(); } });
        connect(field,&ValueSlider::editingCancelled,this,&TimelineCuePanel::cancelEditors);
    }
    connect(m_kind,&QTabBar::currentChanged,this,[this] { if (!m_refreshing && !m_resolving) { beginEdit(Kind); commitEditors(); } });
    connect(m_search,&QLineEdit::textChanged,this,&TimelineCuePanel::refresh);
    connect(m_list,&QListWidget::currentItemChanged,this,[this](QListWidgetItem *item) {
        if (!m_refreshing && item && !m_controller->selectCue(item->data(Qt::UserRole).toString())) refresh();
    });
    connect(m_delete,&QToolButton::clicked,this,[this] { m_controller->removeSelectedCue(); });
    connect(m_previous,&QToolButton::clicked,this,[this] { m_controller->stepCue(-1); });
    connect(m_next,&QToolButton::clicked,this,[this] { m_controller->stepCue(1); });
    connect(m_color,&QToolButton::clicked,this,&TimelineCuePanel::chooseColor);
    connect(controller,&TimelineController::changed,this,&TimelineCuePanel::refresh);
    connect(controller,&TimelineController::editResolutionRequested,this,&TimelineCuePanel::commitEditors);
    connect(controller,&TimelineController::editCancellationRequested,this,&TimelineCuePanel::cancelEditors);
    connect(controller->document(),&Document::loaded,this,&TimelineCuePanel::cancelEditors);
    connect(controller->document(),&Document::shotChanged,this,&TimelineCuePanel::cancelEditors);
    refresh();
}
const TimelineCue *TimelineCuePanel::selectedCue() const {
    return m_controller->document()->isOpen() ? timelineCue(m_controller->document()->activeShot(),m_controller->selectedCueId()) : nullptr;
}
void TimelineCuePanel::refreshList() {
    const bool open = m_controller->document()->isOpen();
    const auto shotId = open ? m_controller->document()->activeShot().id : QString();
    const auto cues = open ? m_controller->document()->activeShot().timelineCues : QVector<TimelineCue>();
    const auto search = m_search->text().trimmed();
    if (shotId != m_listShotId || cues != m_listCues || search != m_listSearch) {
        m_listShotId = shotId; m_listCues = cues; m_listSearch = search;
        auto sorted = cues; std::stable_sort(sorted.begin(),sorted.end(),[](const auto &a,const auto &b) { return a.startFrame < b.startFrame; });
        const QSignalBlocker blocker(m_list); m_list->clear();
        for (const auto &cue : sorted) {
            const auto kind = timelineCueKindName(cue.kind);
            const auto haystack = QStringList{cue.name,kind,cue.speaker,cue.text}.join(' ');
            if (!search.isEmpty() && !haystack.contains(search,Qt::CaseInsensitive)) continue;
            const auto label = QString("%1 · %2 · %3%4").arg(frameLabel(cue),kind,cue.name,
                cue.speaker.isEmpty() ? QString() : QString(" · %1").arg(cue.speaker));
            auto *item = new QListWidgetItem(appIcon(cueIcons.value(int(cue.kind))),label,m_list);
            item->setData(Qt::UserRole,cue.id); item->setData(Qt::AccessibleTextRole,label);
            item->setToolTip((label + (cue.text.isEmpty() ? QString() : '\n' + cue.text)).toHtmlEscaped().replace('\n',"<br>"));
            item->setSizeHint({0,std::max(30,fontMetrics().height()+8)});
        }
    }
    const QSignalBlocker blocker(m_list);
    const auto selectedId = m_controller->selectedCueId();
    QListWidgetItem *current = nullptr;
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(Qt::UserRole).toString() == selectedId) { current = m_list->item(i); break; }
    if (m_list->currentItem() != current) {
        m_list->setCurrentItem(current);
        if (current) m_list->scrollToItem(current); else m_list->clearSelection();
    }
}
void TimelineCuePanel::refresh() {
    if (m_refreshing || m_resolving) return;
    const auto id = m_controller->selectedCueId();
    const auto shotId = m_controller->document()->isOpen() ? m_controller->document()->activeShot().id : QString();
    const auto *cue = selectedCue();
    if (m_dirty && (id != m_id || shotId != m_shotId || !cue || !m_snapshot || *cue != *m_snapshot)) { cancelEditors(); return; }
    const QScopedValueRollback<bool> guard(m_refreshing,true);
    refreshList();
    const bool open = m_controller->document()->isOpen(), symbol = m_controller->property("symbolEditor").toBool();
    const bool editable = open && !symbol && !m_controller->playing()
        && !m_controller->document()->editingGestureActive() && !m_controller->document()->layerPropertyEditActive();
    m_add->setEnabled(editable && m_listCues.size() < maxShotTimelineCues);
    m_delete->setEnabled(editable && cue); m_previous->setEnabled(editable && !m_listCues.isEmpty()); m_next->setEnabled(m_previous->isEnabled());
    m_list->setEnabled(open && !symbol); m_search->setEnabled(open && !symbol);
    m_controls->setVisible(cue && !symbol); m_controls->setEnabled(editable);
    m_status->setText(symbol ? "Cues belong to shots." : !open ? "Open a project to add cues."
        : !cue ? (m_listCues.isEmpty() ? "Add a marker or dialogue cue." : "Select a cue.")
        : m_controller->playing() ? "Pause to edit cues." : QString());
    m_status->setVisible(!m_status->text().isEmpty());
    if (!cue) { m_id.clear(); m_shotId = shotId; return; }
    const bool dialogue = cue->kind == TimelineCueKind::Dialogue;
    m_speakerRow->setVisible(dialogue); m_textLabel->setText(dialogue ? "Dialogue" : "Notes");
    m_text->setAccessibleName(dialogue ? "Dialogue text" : "Cue notes");
    m_text->setToolTip(dialogue ? "Dialogue text · up to 4096 characters" : "Cue notes · up to 4096 characters");
    m_color->setIcon(colorIcon(cue->color)); m_color->setToolTip("Change cue color · " + cue->color.name());
    if (m_dirty) return;
    m_id = id; m_shotId = shotId; m_snapshot = *cue;
    if (m_name->text() != cue->name) m_name->setText(cue->name);
    if (m_speaker->text() != cue->speaker) m_speaker->setText(cue->speaker);
    if (m_text->toPlainText() != cue->text) m_text->setPlainText(cue->text);
    m_kind->setCurrentIndex(int(cue->kind));
    const int frames = m_controller->frameCount();
    m_start->setRange(1,frames - cue->durationFrames + 1); m_start->setSliderRange(1,std::min(240,frames - cue->durationFrames + 1));
    m_hold->setRange(1,frames - cue->startFrame); m_hold->setSliderRange(1,std::min(240,frames - cue->startFrame));
    m_start->setValueSilently(cue->startFrame + 1); m_hold->setValueSilently(cue->durationFrames);
}
void TimelineCuePanel::beginEdit(int field) {
    if (m_refreshing || m_resolving) return;
    if (!m_dirty) {
        const auto *cue = selectedCue(); if (!cue) return;
        m_snapshot = *cue; m_id = cue->id; m_shotId = m_controller->document()->activeShot().id;
    }
    m_dirty |= field; m_controller->setPendingEdits(this,true);
}
void TimelineCuePanel::commitEditors() {
    if (m_refreshing || m_resolving || !m_dirty || !m_snapshot) return;
    {
        const QScopedValueRollback<bool> guard(m_resolving,true);
        m_start->commitEditing(); m_hold->commitEditing();
        const auto *current = selectedCue();
        if (!current || current->id != m_id || m_controller->document()->activeShot().id != m_shotId || *current != *m_snapshot) {
            m_dirty = 0;
        } else {
            auto cue = *m_snapshot;
            if (m_dirty & Name) cue.name = m_name->text().trimmed();
            if (m_dirty & Start) cue.startFrame = qRound(m_start->value()) - 1;
            if (m_dirty & Hold) cue.durationFrames = qRound(m_hold->value());
            if (m_dirty & Kind) cue.kind = TimelineCueKind(m_kind->currentIndex());
            if (m_dirty & Speaker) cue.speaker = m_speaker->text().trimmed();
            if (m_dirty & Text) cue.text = m_text->toPlainText();
            if (cue.name.isEmpty()) {
                // Restore only the invalid name; keep other valid fields from
                // this draft when Save or a selection change resolves it.
                cue.name = m_snapshot->name; emit m_controller->document()->warning("A cue needs a name; its previous name was kept.");
            }
            if (cue == *current || m_controller->updateSelectedCue(cue)) m_dirty = 0;
        }
        m_controller->setPendingEdits(this,m_dirty != 0);
    }
    refresh();
}
void TimelineCuePanel::cancelEditors() {
    if (m_refreshing || m_resolving) return;
    {
        const QScopedValueRollback<bool> guard(m_resolving,true);
        m_start->cancelEditing(); m_hold->cancelEditing(); m_dirty = 0; m_snapshot.reset();
        m_controller->setPendingEdits(this,false);
    }
    refresh();
}
void TimelineCuePanel::chooseColor() {
    const auto id = m_controller->selectedCueId();
    if (id.isEmpty() || !m_controller->prepareTimelineEdit() || m_controller->selectedCueId() != id) return;
    const auto *cue = selectedCue(); if (!cue) return;
    const auto snapshot = *cue; const auto shotId = m_controller->document()->activeShot().id;
    bool stale = false; QColorDialog dialog(snapshot.color,this); dialog.setWindowTitle("Cue color");
    const auto loaded = connect(m_controller->document(),&Document::loaded,&dialog,[&] { stale = true; dialog.reject(); });
    const auto shotChanged = connect(m_controller->document(),&Document::shotChanged,&dialog,[&] { stale = true; dialog.reject(); });
    const bool accepted = dialog.exec() == QDialog::Accepted; disconnect(loaded); disconnect(shotChanged);
    cue = selectedCue();
    if (!accepted || stale || !cue || cue->id != id || *cue != snapshot || m_controller->document()->activeShot().id != shotId) return;
    auto updated = *cue; updated.color = dialog.selectedColor(); updated.color.setAlpha(255);
    m_controller->updateSelectedCue(updated);
}
bool TimelineCuePanel::eventFilter(QObject *watched, QEvent *event) {
    if ((watched == m_name || watched == m_speaker || watched == m_text)
        && (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape && m_dirty) {
            if (event->type() == QEvent::KeyPress) cancelEditors(); event->accept(); return true;
        }
    }
    if (watched == m_text && event->type() == QEvent::FocusOut) commitEditors();
    return QWidget::eventFilter(watched,event);
}
} // namespace toon
