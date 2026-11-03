#pragma once
#include "animation.h"
#include <QWidget>
class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class QToolButton;
namespace toon {
class DrawingTemplateLibrary;
// Embedded in Add Track. Draft changes stay local until the host creates a track;
// Save explicitly stores only the layer recipe in the global template library.
class DrawingTemplateWidget : public QWidget {
    Q_OBJECT
public:
    explicit DrawingTemplateWidget(DrawingTemplateLibrary *library = nullptr, QWidget *parent = nullptr);
    QVector<DrawingTemplateSlot> templateSlots() const;
    bool validTemplate() const;
    void setBuiltIn(DrawingTemplate type);
    void setTemplateSlots(const QVector<DrawingTemplateSlot> &layers);
signals:
    void changed();
private:
    void refreshPresets();
    void choosePreset();
    void rebuildLayers(const QVector<DrawingTemplateSlot> &layers, int selectedRow = 0);
    void updateActions();
    void changeDraft();
    void moveLayer(int direction);
    void showStatus(const QString &message);
    void finishLayerEdit();
    DrawingTemplateLibrary *m_library = nullptr;
    QComboBox *m_presets = nullptr;
    QTableWidget *m_layers = nullptr;
    QLineEdit *m_name = nullptr;
    QToolButton *m_add = nullptr, *m_remove = nullptr, *m_up = nullptr, *m_down = nullptr, *m_save = nullptr, *m_delete = nullptr;
    QLabel *m_status = nullptr;
    QString m_selectedPreset;
    bool m_refreshing = false;
};
} // namespace toon
