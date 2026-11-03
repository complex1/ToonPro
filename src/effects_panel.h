#pragma once
#include "layer_effects.h"
#include "property_animation.h"
#include <QHash>
#include <QPointer>
#include <QWidget>
#include <array>
#include <functional>

class QLabel;
class QTabBar;
class QScrollArea;
class QToolButton;
class QVBoxLayout;
namespace toon {
class Canvas;
class Document;
class ValueSlider;
class ShadowOffsetWidget;
class EffectPropertyPages;
class TimelineController;
class EffectsPanel final : public QWidget {
    Q_OBJECT
public:
    explicit EffectsPanel(Canvas *canvas, QWidget *parent = nullptr);
    ~EffectsPanel() override;
    bool editing() const { return m_editing; }
    void setTimelineController(TimelineController *controller);
public slots:
    void finishEditing();
    void cancelEditing();
signals:
    void pendingEditsChanged(bool pending);
private:
    void refresh();
    void refreshPreviewStatus();
    void beginValue();
    void finishValue();
    void previewValue(int field, double value);
    void changeEffect(const std::function<void(LayerEffects &)> &change);
    void pickColor(bool shadow);
    bool prepareAction();
    void maskAction(const std::function<void()> &action);
    void refreshThumbnail();
    QToolButton *keyButton(QWidget *parent, KeyframeProperty property);
    void toggleChannel(KeyframeProperty property, bool enabled);
    void selectChannel(KeyframeProperty property);
    void refreshKeyButtons();
    void beginMaskValue();
    void previewMaskValue(int field, double value);
    LayerEffects displayedEffects() const;
    LayerMask displayedMask() const;
    ValueSlider *addValue(QVBoxLayout *layout, const char *id, const QString &label,
                          double low, double high, double initial, int decimals,
                          const QString &unit, int field);
    QPointer<Canvas> m_canvas;
    QPointer<Document> m_document;
    QPointer<TimelineController> m_timeline;
    QLabel *m_target, *m_status, *m_thumbnail;
    QTabBar *m_tabs, *m_maskMode, *m_maskPropertyTabs;
    EffectPropertyPages *m_pages;
    QScrollArea *m_scroll;
    std::array<int,5> m_scrollPositions{};
    int m_currentPage = 0;
    QToolButton *m_master, *m_addAdjustment, *m_maskAdd, *m_maskSelection,
        *m_maskEnabled, *m_maskRemove, *m_maskPaint, *m_maskInvert,
        *m_maskRevealAll, *m_maskHideAll, *m_shadowColor, *m_glowColor;
    std::array<QToolButton *,4> m_enabled{};
    std::array<QWidget *,4> m_effectBodies{}, m_effectHeadings{};
    std::array<ValueSlider *,11> m_values{};
    std::array<ValueSlider *,3> m_maskValues{};
    std::array<ValueSlider *,7> m_maskPlacementValues{};
    QHash<int,QToolButton *> m_keyButtons;
    QToolButton *m_maskPresentationInvert;
    ShadowOffsetWidget *m_offset;
    QWidget *m_maskBrush, *m_maskControls, *m_maskPlacement;
    LayerEffects m_draft;
    LayerMask m_maskDraft;
    QString m_editLayerId;
    int m_editFrame = -1;
    quint64 m_thumbnailKey = 0;
    bool m_refreshing = false, m_resolving = false, m_editing = false;
};
} // namespace toon
