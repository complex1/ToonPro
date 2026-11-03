#pragma once

#include <QPointer>
#include <QVector>
#include <QWidget>

class QListWidget;
class QSettings;
class QToolButton;

namespace toon {
class Canvas;

// Saved sizes are workspace preferences. Only explicit activation edits the
// current brush; managing the library never changes artwork or tool settings.
class BrushPresetPanel final : public QWidget {
    Q_OBJECT
public:
    static constexpr int maxPresets = 64;
    BrushPresetPanel(Canvas *canvas, QSettings *settings, QWidget *parent = nullptr);
    const QVector<double> &sizes() const { return m_sizes; }
    bool addSize(double size);
    bool replaceSize(double oldSize, double newSize);
    bool removeSize(double size);
    void resetSizes();
    bool activateSize(double size);
    QSize sizeHint() const override { return {220, 156}; }

private:
    void restore();
    void save();
    void rebuild();
    void refreshActive();
    void refreshAvailability();
    bool compatibleTool() const;
    void showSizeDialog(bool editing = false, double size = 0);
    void showPresetMenu(const QPoint &position);
    void showOptions();
    QPointer<Canvas> m_canvas;
    QPointer<QSettings> m_settings;
    QVector<double> m_sizes;
    QListWidget *m_list = nullptr;
    QToolButton *m_add = nullptr;
    QToolButton *m_more = nullptr;
    bool m_playback = false;
};
} // namespace toon
