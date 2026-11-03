#pragma once
#include <QWidget>
#include <QTimer>
#include <QStringList>

class QLabel;
class QListWidget;
class QToolButton;
namespace toon {
class Canvas;
class BrushLibrary;
struct BrushPreset;
bool importAbrIntoLibrary(BrushLibrary *library, const QString &path, QString *report = nullptr,
    QString *error = nullptr, QStringList *importedIds = nullptr);
class BrushPanel : public QWidget {
    Q_OBJECT
public:
    BrushPanel(Canvas *canvas, BrushLibrary *library, QWidget *parent = nullptr);
    bool importAbrFile(const QString &path, QString *error = nullptr);
    bool saveCurrent(const QString &name, QString *error = nullptr);
    QString lastImportReport() const { return m_importReport; }
private:
    const BrushPreset *selectedPreset() const;
    void refresh();
    void selectPreset();
    void updatePreview();
    void showError(const QString &message);
    Canvas *m_canvas;
    BrushLibrary *m_library;
    QListWidget *m_list;
    QLabel *m_preview, *m_name;
    QToolButton *m_import, *m_save, *m_rename, *m_remove, *m_info;
    QString m_activeId, m_importReport;
    QTimer m_previewTimer;
};
} // namespace toon
