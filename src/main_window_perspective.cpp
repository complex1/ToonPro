#include "main_window.h"
#include "canvas.h"
#include "document.h"
#include "perspective_dialog.h"
#include "timeline_controller.h"
#include "tool_session.h"
#include <QApplication>

namespace toon {
void MainWindow::detectPerspective(){
    auto *document=m_canvas->document();
    if(!document->isOpen()||!resolveCelWorkflows()||!m_timeline->prepareTimelineEdit())return;
    bool added=false;
    {PerspectiveDialog dialog(document,this);connect(&dialog,&PerspectiveDialog::guidesAdded,this,[&added]{added=true;});dialog.exec();}
    if(added&&document->isOpen()&&!document->activeShot().guides.isEmpty()){
        m_toolSession->activateEngine(Canvas::Tool::Guides);
        m_canvas->selectGuide(document->activeShot().guides.last().id);
    }
    if(isVisible()&&!QApplication::activeModalWidget()&&!QApplication::activePopupWidget()
        &&QGuiApplication::applicationState()==Qt::ApplicationActive){
        auto *host=window();if(!QApplication::activeWindow())QApplication::setActiveWindow(host);
        if(host->isActiveWindow())m_canvas->setFocus(Qt::OtherFocusReason);
    }
}
}
