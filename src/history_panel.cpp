#include "history_panel.h"
#include "document.h"
#include "icons.h"
#include "ui_theme.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QListWidget>
#include <QSignalBlocker>
#include <QToolButton>
#include <QUndoCommand>
#include <QVBoxLayout>
#include <functional>

namespace toon {
namespace {
class HistoryList final : public QListWidget {
public:
    using QListWidget::QListWidget;
    std::function<void(int)> request;
protected:
    void keyPressEvent(QKeyEvent *event) override {
        if (event->modifiers() == Qt::NoModifier) {
            int destination = currentRow();
            switch (event->key()) {
            case Qt::Key_Up: --destination; break;
            case Qt::Key_Down: ++destination; break;
            case Qt::Key_PageUp: destination -= qMax(1,viewport()->height()/28); break;
            case Qt::Key_PageDown: destination += qMax(1,viewport()->height()/28); break;
            case Qt::Key_Home: destination = 0; break;
            case Qt::Key_End: destination = count() - 1; break;
            case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_Space: break;
            default: {
                const int previous=currentRow();QListWidget::keyPressEvent(event);
                if(currentRow()!=previous && request)request(currentRow());
                return;
            }
            }
            if (request) request(qBound(0, destination, count() - 1));
            event->accept(); return;
        }
        const int previous=currentRow();
        QListWidget::keyPressEvent(event);
        if(currentRow()!=previous && request)request(currentRow());
    }
};
}
HistoryPanel::HistoryPanel(Document *document, QWidget *parent)
    : QWidget(parent), m_document(document)
{
    setObjectName("historyPanelContent"); setAccessibleName("History");
    setMinimumSize(160, 110);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(4,4,4,4); layout->setSpacing(4);
    auto *header = new QHBoxLayout; header->setSpacing(4);
    const auto button = [this](const char *name, const char *icon, const QString &help) {
        auto *result = new QToolButton(this); result->setObjectName(name); result->setIcon(appIcon(icon));
        result->setIconSize({18,18}); result->setFixedSize(28,28);
        result->setToolTip(help); result->setAccessibleName(help);
        result->setStyleSheet("QToolButton { padding: 0; min-width: 26px; max-width: 26px; min-height: 26px; max-height: 26px; }");
        return result;
    };
    m_undo = button("historyUndo", "undo-2", "Undo artwork edit");
    m_redo = button("historyRedo", "redo-2", "Redo artwork edit");
    header->addWidget(m_undo); header->addWidget(m_redo); header->addStretch(); layout->addLayout(header);
    auto *list = new HistoryList(this); m_list = list;
    list->setObjectName("historyEntries"); list->setAccessibleName("Artwork history");
    list->setAccessibleDescription("Choose a state to undo or redo artwork edits. Later states are discarded when a new edit is made.");
    list->setFrameShape(QFrame::NoFrame); list->setIconSize({16,16}); list->setSpacing(0);
    list->setUniformItemSizes(true); list->setTextElideMode(Qt::ElideRight);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    layout->addWidget(list,1);
    list->request = [this](int index) { requestIndex(index); };
    connect(list,&QListWidget::itemClicked,this,[this](QListWidgetItem *item) { requestIndex(item->data(Qt::UserRole).toInt()); });
    connect(m_undo,&QToolButton::clicked,this,[this] { if(m_document)requestIndex(m_document->history()->index()-1); });
    connect(m_redo,&QToolButton::clicked,this,[this] { if(m_document)requestIndex(m_document->history()->index()+1); });
    if(document) {
        connect(document->history(),&QUndoStack::indexChanged,this,&HistoryPanel::refresh);
        connect(document->history(),&QUndoStack::cleanChanged,this,&HistoryPanel::refreshStates);
        connect(document,&Document::loaded,this,[this] { m_retainedBaseline=false; m_lastIndex=0; m_commands.clear();m_labels.clear();refresh(); });
        connect(document,&QObject::destroyed,this,[this] { m_document=nullptr;refresh(); });
        // A panel created after trimming cannot distinguish a saved project's
        // initial state from the rolling baseline. Use the honest generic label.
        m_retainedBaseline=document->history()->count()>0;
    }
    refresh();
}
bool HistoryPanel::mayRequest() const {
    return m_interactionEnabled && m_document && m_document->isOpen()
        && !m_document->strokeActive() && !m_document->committing()
        && !m_document->layerPropertyEditActive() && !m_document->keyframeEditActive()
        && !m_document->cameraEditActive() && !m_document->effectsEditActive()
        && !m_document->externalEditActive();
}
void HistoryPanel::setInteractionEnabled(bool enabled) {
    if(m_interactionEnabled==enabled)return;
    m_interactionEnabled=enabled;refreshStates();
}
void HistoryPanel::requestIndex(int index) {
    // A rejected click must not leave a misleading highlighted history state.
    refreshStates();
    if(!mayRequest()) {
        if(m_document&&m_document->isOpen())emit navigationBlocked("Finish the current edit before changing history.");
        return;
    }
    if(index<0 || index>m_document->history()->count() || index==m_document->history()->index())return;
    emit requestHistoryIndex(index);
    refreshStates();
}
void HistoryPanel::refresh() {
    QVector<const QUndoCommand *> commands; QVector<QString> labels;
    if(m_document)for(int i=0;i<m_document->history()->count();++i) {
        const auto *command=m_document->history()->command(i);
        commands.append(command);labels.append(command->text());
    }
    // Eviction and memory-budget clearing move the oldest reachable state.
    if(!m_commands.isEmpty() && (commands.isEmpty() || (commands.first()!=m_commands.first()
        && m_document && m_lastIndex>0 && m_document->history()->index()>0)))m_retainedBaseline=true;
    if(commands!=m_commands || labels!=m_labels || m_list->count()!=commands.size()+1) {
        m_commands=commands;m_labels=labels;QSignalBlocker block(m_list);m_list->clear();
        for(int row=0;row<=labels.size();++row) {
            auto *item=new QListWidgetItem(m_list);item->setData(Qt::UserRole,row);item->setSizeHint({0,28});
        }
    }
    m_lastIndex=m_document?m_document->history()->index():0;
    refreshStates();
}
void HistoryPanel::refreshStates() {
    const auto *stack=m_document?m_document->history():nullptr;
    const bool open=m_document && m_document->isOpen();
    const int index=stack?stack->index():0,clean=stack?stack->cleanIndex():-1;
    const bool available=m_interactionEnabled&&open;
    m_list->setEnabled(available);
    m_undo->setEnabled(available&&index>0);m_redo->setEnabled(available&&stack&&index<stack->count());
    QSignalBlocker block(m_list);
    for(int row=0;row<m_list->count();++row) {
        auto *item=m_list->item(row);
        QString label=row==0?(open?(m_retainedBaseline?"Oldest retained state":"Initial state"):"No project")
            :m_labels.value(row-1);
        if(label.isEmpty())label="Edit";
        const bool saved=open&&row==clean;
        if(saved)label+=" · Saved";
        item->setText(label);
        item->setToolTip(label+(row>index?"\nRedo to this state":row==index?"\nCurrent state":"\nUndo to this state"));
        item->setIcon(row==index&&open?appIcon("chevron-right"):QIcon());
        item->setForeground(QColor(row>index?ui::disabled:ui::text));
        auto font=item->font();font.setBold(row==index&&open);item->setFont(font);
        item->setData(Qt::AccessibleTextRole,label+(row==index?", current state":""));
    }
    m_list->setCurrentRow(index);
    if(auto *item=m_list->item(index))m_list->scrollToItem(item,QAbstractItemView::EnsureVisible);
}
} // namespace toon
