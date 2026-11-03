#pragma once
#include "palette_library.h"
#include <QColor>
#include <QPointer>
#include <QWidget>

class QComboBox;
class QAction;
class QGridLayout;
class QLabel;
class QLineEdit;
class QScrollArea;
class QToolButton;
namespace toon {
class Canvas;

// Direct saturation/value editing with an adjacent hue strip. Arrow keys edit
// saturation/value; Shift+Left/Right edit hue without needing another control.
class ColorPicker : public QWidget {
    Q_OBJECT
public:
    explicit ColorPicker(QWidget *parent = nullptr);
    void setColor(const QColor &color);
    QColor color() const;
    QSize sizeHint() const override { return {220, 180}; }
signals:
    void colorChanged(const QColor &color);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
private:
    QRectF colorRect() const;
    QRectF hueRect() const;
    void chooseAt(QPointF point);
    qreal m_hue = 0, m_saturation = 0, m_value = 0, m_alpha = 1;
    enum class Drag { None, Color, Hue } m_drag = Drag::None;
};

class ColorPanel : public QWidget {
    Q_OBJECT
public:
    explicit ColorPanel(Canvas *canvas, QWidget *parent = nullptr);
    void setPaletteLibrary(PaletteLibrary *library);
    QString currentPaletteId() const { return m_paletteId; }
    void setCurrentPaletteId(const QString &id);
signals:
    void paletteSelected(const QString &id);
private:
    void refresh();
    void chooseColor(const QColor &color);
    ColorPalette displayedPalette() const;
    void refreshPalettes();
    void refreshSwatches();
    void refreshPaletteActions();
    void createPalette();
    void renamePalette();
    void deletePalette();
    void addSwatch();
    void updateSwatch();
    void renameSwatch();
    void deleteSwatch();
    void paletteMenu();
    void swatchMenu(const QString &id, const QPoint &globalPosition);
    void reportPaletteError(const QString &error);
    Canvas *m_canvas;
    ColorPicker *m_picker;
    QToolButton *m_foreground, *m_background;
    QLineEdit *m_hex;
    QPointer<PaletteLibrary> m_library;
    QComboBox *m_paletteChoice;
    QToolButton *m_paletteMore, *m_swatchAdd;
    QAction *m_paletteAdd, *m_swatchUpdate, *m_swatchDelete;
    QWidget *m_swatchContent;
    QScrollArea *m_swatchScroll;
    QGridLayout *m_swatchGrid;
    QLabel *m_paletteStatus;
    QString m_paletteId, m_swatchId, m_paletteError;
    bool m_editBackground = false, m_refreshingPalettes = false;
};
} // namespace toon
