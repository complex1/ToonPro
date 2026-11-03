#pragma once
#include "brush_surface_settings.h"
#include <QWidget>
#include <QPolygonF>
class QCheckBox;
class QLabel;
class QTabBar;
class QToolButton;
namespace toon {
class SurfacePlaneEditor;
class ValueSlider;
class BrushSurfacePatternWidget : public QWidget {
    Q_OBJECT
public:
    explicit BrushSurfacePatternWidget(QWidget *parent = nullptr);
    ~BrushSurfacePatternWidget() override;
    void setPattern(const BrushSurfacePattern &pattern);
    // Tool changes and dialog closure discard an unfinished image-import draft.
    void resetPattern(const BrushSurfacePattern &pattern);
    void setArtworkFitAvailable(bool available);
    bool fitToArtwork(const QPolygonF &artworkFrame);
    bool importImage(const QString &path, QString *error = nullptr);
signals:
    void patternEdited(const toon::BrushSurfacePattern &pattern);
    void fitToArtworkRequested();
private:
    void refresh();
    void changed();
    BrushSurfacePattern m_pattern;
    BrushSurfacePattern m_syncedPattern;
    QTabBar *m_kind;
    SurfacePlaneEditor *m_preview;
    QCheckBox *m_perspective;
    QToolButton *m_import, *m_reset, *m_fit;
    QLabel *m_imageName;
    QWidget *m_actions;
    ValueSlider *m_frequency,*m_density,*m_angle,*m_tileWidth,*m_tileHeight;
    ValueSlider *m_phaseX,*m_phaseY,*m_planeWidth,*m_planeHeight,*m_cornerX,*m_cornerY;
    ValueSlider *m_densityEnd,*m_fadeLength,*m_seed;
    QWidget *m_frequencyRow,*m_densityRow,*m_tileWidthRow,*m_tileHeightRow;
    QWidget *m_planeWidthRow,*m_planeHeightRow,*m_cornerXRow,*m_cornerYRow;
    QWidget *m_densityEndRow,*m_fadeLengthRow,*m_seedRow;
    bool m_refreshing = false;
};
} // namespace toon
