#include "workspace_dialog.h"
#include "workspace_library.h"
#include "icons.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QListWidget>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QToolButton>
#include <QVBoxLayout>

namespace toon {
namespace {
class WorkspaceList final : public QListWidget {
public:
    using QListWidget::QListWidget;
protected:
    void keyPressEvent(QKeyEvent *event) override {
        // Cocoa treats Return as an edit request in item views. Here it
        // uses the selected workspace, while an inline rename owns its key.
        if(state()!=EditingState && (event->modifiers()&~Qt::KeypadModifier)==Qt::NoModifier &&
           (event->key()==Qt::Key_Return || event->key()==Qt::Key_Enter) && currentItem()) {
            emit itemActivated(currentItem());event->accept();return;
        }
        QListWidget::keyPressEvent(event);
    }
};
QToolButton *action(QWidget *parent,const QString &id,const QString &icon,const QString &help) {
    auto *button=new QToolButton(parent);button->setObjectName(id);button->setIcon(appIcon(icon));
    button->setIconSize({ui::metrics::actionIcon,ui::metrics::actionIcon});
    button->setFixedSize(ui::metrics::actionTarget,ui::metrics::actionTarget);
    button->setProperty("compactDrawingAction",true);button->setFocusPolicy(Qt::StrongFocus);
    button->setToolTip(help);button->setAccessibleName(help);return button;
}
}
WorkspaceDialog::WorkspaceDialog(WorkspaceLibrary *library,QWidget *parent):QDialog(parent),m_library(library) {
    setObjectName("workspaceManagerDialog");setWindowTitle("Saved workspaces");setWindowModality(Qt::WindowModal);
    setMinimumSize(260,260);resize(360,360);
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(10,10,10,10);layout->setSpacing(6);
    auto *toolbar=new QHBoxLayout;toolbar->setSpacing(ui::metrics::controlGap);
    m_rename=action(this,"workspaceRename","pencil","Rename selected workspace");
    m_remove=action(this,"workspaceDelete","trash","Delete selected saved workspace");
    toolbar->addWidget(m_rename);toolbar->addWidget(m_remove);toolbar->addStretch();layout->addLayout(toolbar);
    m_list=new WorkspaceList;m_list->setObjectName("workspaceList");m_list->setAccessibleName("Saved custom workspaces");
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setWordWrap(false);m_list->setTextElideMode(Qt::ElideRight);layout->addWidget(m_list,1);
    m_empty=new QLabel("Save the current layout from Workspace → Save as.");m_empty->setObjectName("workspaceEmpty");m_empty->setWordWrap(true);layout->addWidget(m_empty);
    m_error=new QLabel;m_error->setObjectName("workspaceError");m_error->setWordWrap(true);m_error->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(m_error);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close);m_use=buttons->addButton("Use workspace",QDialogButtonBox::AcceptRole);
    m_use->setObjectName("workspaceUse");m_use->setDefault(true);layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);connect(m_use,&QPushButton::clicked,this,&WorkspaceDialog::acceptSelected);
    connect(m_rename,&QToolButton::clicked,this,&WorkspaceDialog::renameSelected);connect(m_remove,&QToolButton::clicked,this,&WorkspaceDialog::removeSelected);
    connect(m_list,&QListWidget::currentItemChanged,this,[this]{refreshActions();});
    connect(m_list,&QListWidget::itemActivated,this,[this]{acceptSelected();});
    connect(m_list,&QListWidget::itemChanged,this,[this](QListWidgetItem *item){
        if(m_refreshing || !m_library || !item)return;
        const auto id=item->data(Qt::UserRole).toString(),name=item->text();QString error;
        const bool renamed=m_library->rename(id,name,&error);
        // Do not remove the item while its delegate is committing the editor.
        // The queued library refresh runs after that edit has closed.
        if(const auto *saved=m_library->find(id)) {
            const QScopedValueRollback<bool> guard(m_refreshing,true);
            item->setText(saved->name);item->setData(Qt::AccessibleTextRole,saved->name);item->setToolTip(saved->name);
        }
        displayError(renamed?QString():error);
    });
    if(library){connect(library,&WorkspaceLibrary::changed,this,[this]{refresh();},Qt::QueuedConnection);connect(library,&QObject::destroyed,this,[this]{m_library=nullptr;refresh();});}
    refresh();
}
QString WorkspaceDialog::selectedWorkspaceId() const {return m_list->currentItem()?m_list->currentItem()->data(Qt::UserRole).toString():QString();}
void WorkspaceDialog::done(int result){
    m_closedWithFocus=QApplication::activeWindow()==this;
    QDialog::done(result);
}
void WorkspaceDialog::displayError(const QString &error){m_error->setText(error);m_error->setToolTip(error);m_error->setAccessibleName(error);m_error->setVisible(!error.isEmpty());}
void WorkspaceDialog::refresh(const QString &preferredId) {
    if(m_refreshing)return;const QScopedValueRollback<bool> guard(m_refreshing,true);
    const auto id=preferredId.isEmpty()?selectedWorkspaceId():preferredId;const int previous=m_list->currentRow();
    // A rename's queued notification can arrive after another inline edit has
    // opened. Preserve existing items so that notification cannot destroy the
    // new editor or lose its text merely to repaint the same list.
    for(int row=m_list->count()-1;row>=0;--row)
        if(!m_library||!m_library->find(m_list->item(row)->data(Qt::UserRole).toString()))delete m_list->takeItem(row);
    if(m_library)for(int row=0;row<m_library->entries().size();++row) {
        const auto &entry=m_library->entries()[row];QListWidgetItem *item=nullptr;
        for(int i=0;i<m_list->count();++i)if(m_list->item(i)->data(Qt::UserRole).toString()==entry.id){item=m_list->item(i);break;}
        if(!item){item=new QListWidgetItem(appIcon("panels-top-left"),entry.name);item->setData(Qt::UserRole,entry.id);m_list->insertItem(row,item);}
        else if(m_list->row(item)!=row)m_list->insertItem(row,m_list->takeItem(m_list->row(item)));
        if(item->text()!=entry.name)item->setText(entry.name);
        if(item->data(Qt::AccessibleTextRole).toString()!=entry.name)item->setData(Qt::AccessibleTextRole,entry.name);
        if(item->toolTip()!=entry.name)item->setToolTip(entry.name);
        if(!(item->flags()&Qt::ItemIsEditable))item->setFlags(item->flags()|Qt::ItemIsEditable);
        const QSize size(0,qMax(ui::metrics::actionTarget,QFontMetrics(m_list->font()).height()+10));if(item->sizeHint()!=size)item->setSizeHint(size);
        if(entry.id==id&&m_list->currentItem()!=item)m_list->setCurrentItem(item);
    }
    if(!m_list->currentItem() && m_list->count())m_list->setCurrentRow(qBound(0,previous,m_list->count()-1));
    m_empty->setVisible(m_list->count()==0);
    if(!m_library)displayError("Workspace settings are no longer available.");
    else if(!m_library->writable())displayError(m_library->loadError());
    refreshActions();
}
void WorkspaceDialog::refreshActions() {
    const bool selected=m_library && m_library->find(selectedWorkspaceId());
    m_use->setEnabled(selected);m_rename->setEnabled(selected&&m_library->writable());m_remove->setEnabled(selected&&m_library->writable());
}
void WorkspaceDialog::renameSelected() {
    if(m_library && m_library->writable() && m_list->currentItem()){displayError({});m_list->setFocus(Qt::OtherFocusReason);m_list->editItem(m_list->currentItem());}
}
void WorkspaceDialog::removeSelected() {
    if(!m_library || selectedWorkspaceId().isEmpty())return;
    QString error;if(!m_library->remove(selectedWorkspaceId(),&error))displayError(error);else displayError({});
}
void WorkspaceDialog::acceptSelected() {
    if(!m_library || !m_library->find(selectedWorkspaceId()))return;
    // Restore after exec() returns so the owner's layout gesture guard does
    // not see this dialog as an active modal window.
    accept();
}
} // namespace toon
