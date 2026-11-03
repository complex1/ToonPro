#pragma once
#include "audio.h"
#include <QPointer>
#include <QWidget>
#include <optional>
class QLabel;
class QLineEdit;
class QToolButton;
class QMenu;
class QTabWidget;
namespace toon {
class TimelineController;
class ValueSlider;
class AudioEnvelopeWidget;
class AudioPanel final : public QWidget {
    Q_OBJECT
public:
    explicit AudioPanel(TimelineController *controller,QWidget *parent=nullptr);
    ~AudioPanel() override;
protected:
    bool eventFilter(QObject *watched,QEvent *event) override;
private:
    void refresh();
    bool beginEdit(int field);
    void commitEditors();
    void cancelEditors();
    AudioClip draftClip() const;
    qint64 playheadSample(const AudioClip &clip) const;
    void updatePreview();
    void setAutomation(bool enabled);
    void addGainKey();
    void deleteGainKey();
    void rebuildCrossfadeMenu();
    QPointer<TimelineController> m_controller;
    QLineEdit *m_name;
    QToolButton *m_mute,*m_automate,*m_addKey,*m_deleteKey,*m_crossfade,*m_linear,*m_equalPower;
    QMenu *m_crossfadeMenu;
    QTabWidget *m_tabs;
    AudioEnvelopeWidget *m_envelope;
    ValueSlider *m_start,*m_sourceIn,*m_duration,*m_volume,*m_fadeIn,*m_fadeOut;
    QLabel *m_status;
    std::optional<AudioClip> m_snapshot,m_envelopeDraft;
    QString m_id;
    int m_dirty=0;
    qint64 m_volumeSample=0;
    bool m_refreshing=false,m_resolving=false;
};
}
