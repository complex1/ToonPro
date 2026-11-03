#pragma once
#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QVariant>

// Test gestures target the real, responsive row geometry instead of assuming
// fixed label widths when the Timeline grows organization controls.
namespace timeline_test {
inline int metric(QAbstractScrollArea *view, const char *name) {
    const int result=view->property(name).toInt();
    if(result<=0) qFatal("Timeline geometry property missing: %s",name);
    return result;
}
inline int labelWidth(QAbstractScrollArea *view) { return metric(view,"timelineLabelWidth"); }
inline int rowCenter(QAbstractScrollArea *view,int row) {
    const int height=metric(view,"timelineRowHeight");
    return metric(view,"timelineRulerHeight")+row*height+height/2-view->verticalScrollBar()->value();
}
}
