#pragma once
#include <QWidget>
namespace toon {
class Canvas;
class VectorEditPanel final : public QWidget {
    Q_OBJECT
public:
    explicit VectorEditPanel(Canvas *canvas, QWidget *parent = nullptr);
};
}
