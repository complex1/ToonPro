#pragma once
#include <QWidget>
namespace toon {
class Canvas;
QWidget *drawingPanel(Canvas *canvas, const QString &kind);
}
