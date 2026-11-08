#pragma once
#include <QHash>
#include <QImage>
#include <QPointer>
#include <QScrollArea>
#include <QSet>
#include <QStringList>
class QAction;
class QComboBox;
class QToolButton;
class QTreeWidgetItem;
class QTimer;
namespace toon {
class Document;
class ValueSlider;
class LayerTree;
class LayerPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit LayerPanel(Document *document, QWidget *parent = nullptr);
    ~LayerPanel() override;
    QStringList selectedLayerIds() const;
    void setImportImageAction(QAction *action);
    void refreshReferencePreviews() { refreshThumbnails(); }
signals:
    void reference3DLayerRequested(bool create);
    void convertToLineArtRequested();
    void editPuppetWarpRequested();
    void detectPerspectiveRequested();
    void selectedLayersChanged();
    void convertToSymbolRequested(const QStringList &layerIds);
    void symbolActionRequested(const QString &action);
private:
    void refresh();
    void refreshEnabled();
    void refreshThumbnails();
    void showContextMenu(const QPoint &position);
    QTreeWidgetItem *itemForId(const QString &id) const;
    QPointer<Document> m_document;
    QWidget *m_content;
    LayerTree *m_tree;
    QComboBox *m_blend;
    ValueSlider *m_opacity;
    QToolButton *m_locked, *m_positionLocked, *m_alphaLocked, *m_clipped, *m_annotation, *m_fillReference;
    QToolButton *m_add, *m_importImage, *m_notes, *m_group, *m_duplicate, *m_remove, *m_merge, *m_more;
    QTimer *m_thumbnailTimer;
    QHash<QString, bool> m_expanded;
    QHash<QString, quint64> m_thumbnailKeys;
    QHash<QString, QTreeWidgetItem *> m_items;
    QHash<QString, int> m_layerIndices, m_scopeNodes, m_scopeArtwork;
    QHash<QString, QString> m_parents, m_scopes;
    QHash<QString, QStringList> m_children;
    QSet<QString> m_lockedLayers, m_annotationLayers, m_animationLayers, m_containers, m_drawingContainers;
    QSet<QString> m_presentLayers;
    QVector<int> m_presentIndices;
    QStringList m_structure;
    QStringList m_displayDrawings;
    QString m_refreshedActive;
    bool m_dirty = true, m_updating = false, m_selecting = false;
};
} // namespace toon
