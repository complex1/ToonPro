#pragma once
#include <QImage>
#include <QStringList>
#include <QTransform>
#include <QWidget>

class QLabel;
class QListWidget;
class QToolButton;
namespace toon {
class Document;
class ValueSlider;
class CharacterModelPanel;
class CharacterColorPanel;
class Canvas;
class LightTablePanel;

// Shared bounded image import for shot references and character model sheets.
bool readReferenceImage(const QString &path, QImage *image, QString *name, QString *error = nullptr);

class ReferenceView : public QWidget {
    Q_OBJECT
public:
    explicit ReferenceView(QWidget *parent = nullptr);
    void setImage(const QImage &image);
    void setEmptyText(const QString &text);
    const QImage &image() const { return m_image; }
    qreal zoom() const { return m_zoom; }
    QTransform imageTransform() const;
    void setSampling(bool enabled);
    bool sampling() const { return m_sampling; }
public slots:
    void fitImage();
    void setZoom(qreal zoom);
signals:
    void viewChanged();
    void colorSampled(QColor color);
protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
private:
    void zoomAt(qreal zoom, QPointF position);
    void sampleAt(QPointF position);
    QImage m_image;
    QString m_emptyText = "Add a reference image";
    qreal m_zoom = 1;
    QPointF m_center, m_last;
    bool m_fit = true, m_dragging = false;
    bool m_sampling = false, m_samplingGesture = false;
};

class ReferencePanel : public QWidget {
    Q_OBJECT
public:
    explicit ReferencePanel(Document *document, QWidget *parent = nullptr);
    bool importImage(const QString &path, QString *error = nullptr);
    void setSampling(bool enabled);
    void setCurrentColor(const QColor &color);
    void attachCanvas(Canvas *canvas);
    LightTablePanel *lightTable() const { return m_lightTable; }
    bool finishEditing();
signals:
    void colorSampled(QColor color);
private:
    void refresh();
    void selectReference();
    Document *m_document;
    QListWidget *m_list;
    ReferenceView *m_view;
    ValueSlider *m_zoom;
    QLabel *m_name;
    QToolButton *m_add, *m_remove, *m_fit;
    CharacterModelPanel *m_characters;
    CharacterColorPanel *m_characterColors;
    LightTablePanel *m_lightTable = nullptr;
    QString m_selectedId;
    QStringList m_referenceState;
    bool m_refreshed = false, m_zoomEditing = false;
};
} // namespace toon
