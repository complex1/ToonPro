#pragma once
#include "audio.h"
#include <QWidget>
#include <functional>
#include <optional>
namespace toon {
class AudioEnvelopeWidget final : public QWidget {
    Q_OBJECT
public:
    explicit AudioEnvelopeWidget(QWidget *parent=nullptr);
    void setClip(const AudioClip &clip,qint64 playheadSource,double fps);
    void clear();
    void setEditGuard(std::function<bool()> guard){m_guard=std::move(guard);}
    bool editing() const{return m_editing;}
    void finishEditing();
    void cancelEditing();
    std::optional<qint64> selectedKeySample() const;
    void selectKey(qint64 sourceSample);
signals:
    void clipEdited(const toon::AudioClip &clip);
    void editingFinished();
    void editingCancelled();
    void keySelectionChanged();
protected:
    void paintEvent(QPaintEvent *)override;
    void mousePressEvent(QMouseEvent *)override;
    void mouseMoveEvent(QMouseEvent *)override;
    void mouseReleaseEvent(QMouseEvent *)override;
    void mouseDoubleClickEvent(QMouseEvent *)override;
    void keyPressEvent(QKeyEvent *)override;
    void focusOutEvent(QFocusEvent *)override;
private:
    enum Handle{None=-3,FadeIn=-2,FadeOut=-1};
    QRectF plot() const;
    QPointF keyPoint(int index) const;
    QPointF fadePoint(bool in) const;
    qint64 sampleAt(qreal x) const;
    qreal xAt(qint64 sample) const;
    double gainAt(qreal y) const;
    bool beginEdit();
    void changeHandle(QPointF point);
    void publish();
    AudioClip m_clip,m_before;
    qint64 m_playhead=-1;
    double m_fps=24;
    int m_handle=None;
    bool m_valid=false,m_editing=false;
    std::function<bool()> m_guard;
};
}
