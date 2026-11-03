#pragma once
#include "onion_settings.h"
#include <QDialog>

class QToolButton;
namespace toon {
class ValueSlider;
// Live workspace preview. Reject restores the supplied settings; accept retains
// the preview. The caller persists only after Accepted, without artwork Undo.
class OnionSettingsDialog final : public QDialog {
    Q_OBJECT
public:
    explicit OnionSettingsDialog(const OnionSkinSettings &settings, QWidget *parent = nullptr);
    const OnionSkinSettings &settings() const { return m_settings; }
public slots:
    void accept() override;
    void reject() override;
    void resetDefaults();
signals:
    void settingsChanged(const OnionSkinSettings &settings);
private:
    void updateControls();
    void chooseColor(bool previous);
    void commitEditors();
    OnionSkinSettings m_original, m_settings;
    ValueSlider *m_previous = nullptr, *m_next = nullptr, *m_opacity = nullptr;
    QToolButton *m_previousColor = nullptr, *m_nextColor = nullptr;
};
} // namespace toon
