#pragma once
#include "brush_types.h"
#include <QHash>
#include <QIcon>
#include <QTimer>
#include <QWidget>
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QToolButton;
namespace toon {
class Canvas;
class BrushLibrary;
class ToolCatalog;
struct ToolState;
class BrushManagementPanel final : public QWidget {
    Q_OBJECT
public:
    BrushManagementPanel(Canvas *canvas, BrushLibrary *library, ToolCatalog *catalog, QWidget *parent = nullptr);
    ~BrushManagementPanel() override;
    QString addBrush(const QString &name, BrushType type, const QString &category = {}, const QStringList &tags = {},
                     const QString &destinationEntryId = {}, const QImage &image = {}, bool fromCurrent = false, QString *error = nullptr);
    bool importImageBrush(const QString &path, const QString &name, BrushType type, const QString &category = {},
                          const QStringList &tags = {}, QString *error = nullptr);
    QString addDynamicStampBrush(const QString &name, const QVector<QImage> &images, bool preserveColors = false,
                                 const QString &category = {}, const QStringList &tags = {},
                                 const QString &destinationEntryId = {}, QString *error = nullptr);
    bool importDynamicStampBrush(const QStringList &paths, const QString &name, bool preserveColors = false,
                                 const QString &category = {}, const QStringList &tags = {},
                                 const QString &destinationEntryId = {}, QString *error = nullptr);
    bool importAbrFile(const QString &path, QString *error = nullptr);
public slots:
    void focusBrush(const QString &id);
signals:
    void advancedSettingsRequested(const QString &id);
private:
    void showEvent(QShowEvent *event) override;
    QStringList selectedIds() const;
    void refresh();
    void filter();
    void refreshDetails();
    void refreshPreviews();
    void createDialog(const QString &imagePath = {});
    void importDialog();
    void duplicateSelected();
    void deleteSelected();
    void editTag(bool add);
    void setError(const QString &message);
    QString createBrush(const QString &name, BrushType type, const ToolState &state, const QString &category,
                        const QStringList &tags, const QString &destination, QString *error);
    Canvas *m_canvas;
    BrushLibrary *m_library;
    ToolCatalog *m_catalog;
    QListWidget *m_list;
    QLineEdit *m_search, *m_name, *m_tag;
    QComboBox *m_tags, *m_categories, *m_types, *m_category, *m_group;
    QWidget *m_details;
    QLabel *m_summary, *m_tagSummary, *m_error, *m_empty;
    QToolButton *m_add, *m_import, *m_remove, *m_duplicate, *m_use, *m_advanced, *m_assign, *m_importDetails;
    QString m_importReport;
    QStringList m_inspected;
    bool m_refreshing = false;
    QHash<QString, QIcon> m_previews;
    QTimer m_previewTimer;
};
} // namespace toon
