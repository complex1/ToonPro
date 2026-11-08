#include "main_window.h"
#include "canvas.h"
#include "document.h"
#include "line_art_dialog.h"
#include "line_art_document.h"
#include "timeline_controller.h"
#include <QApplication>

namespace toon {
void MainWindow::convertToLineArt() {
    auto *document=m_canvas->document();
    if(!document->isOpen()||!resolveCelWorkflows()||!m_timeline->prepareTimelineEdit())return;
    const auto reason=lineArtSourceReason(document->project(),document->activeShotIndex(),document->activeShot().layers[document->activeLayer()].id,document->currentFrame());
    if(!reason.isEmpty()){showActionFeedback(reason,true);return;}
    {LineArtDialog dialog(document,this);dialog.exec();}
    if(isVisible()&&!QApplication::activeModalWidget()&&!QApplication::activePopupWidget()&&QGuiApplication::applicationState()==Qt::ApplicationActive){
        auto *host=window();if(!QApplication::activeWindow())QApplication::setActiveWindow(host);
        if(host->isActiveWindow())m_canvas->setFocus(Qt::OtherFocusReason);
    }
}
}
