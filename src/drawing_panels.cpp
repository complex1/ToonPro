#include "scene3d_panel.h"
#include "feature_flags.h"
#include "scene3d_controller.h"
#include "drawing_panels.h"
#include "brush_settings_panel.h"
#include "layer_panel.h"
#include "color_panel.h"
#include "selection_panel.h"
#include "guide_panel.h"
#include "drawing_tool_panel.h"
#include "vector_edit_panel.h"
#include <QTabWidget>
#include <QTabBar>
#include "canvas.h"
#include "icons.h"
#include "ui_metrics.h"
#include <QButtonGroup>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace toon {
namespace {
// The hidden brush page must not dictate a narrow selection page's dock width.
class ToolPropertiesStack final : public QStackedWidget {
public:
    ToolPropertiesStack() { layout()->setSizeConstraint(QLayout::SetNoConstraint); }
    QSize minimumSizeHint() const override {
        return currentWidget() ? currentWidget()->minimumSizeHint().expandedTo(currentWidget()->minimumSize()) : QSize();
    }
    QSize sizeHint() const override { return currentWidget() ? currentWidget()->sizeHint() : QSize(); }
};
}
QWidget *drawingPanel(Canvas *canvas, const QString &kind) {
    if (kind == "propertiesPanel") {
        auto *stack = new ToolPropertiesStack; stack->setObjectName("toolPropertiesStack");
        stack->setProperty("compactDrawingPanel", true);
        stack->addWidget(new BrushSettingsPanel(canvas));
        stack->addWidget(new SelectionPanel(canvas));
        stack->addWidget(new GuidePanel(canvas));
        stack->addWidget(new DrawingToolPanel(canvas));
        stack->addWidget(new VectorEditPanel(canvas));
        // Apply the shared drawing tab role once. Separate advanced dialogs and
        // 3D inspectors retain their own density; no updates run during drawing.
        for (int page = 0; page < stack->count(); ++page)
            for (auto *tabs : stack->widget(page)->findChildren<QTabBar *>()) {
                if (tabs->window() != stack) continue;
                tabs->setProperty("compactDrawingTabs", true);
                tabs->setIconSize({ui::metrics::tabIcon, ui::metrics::tabIcon});
            }
        auto *scenePanel = features::legacy3DEditor ? new Scene3DPanel(canvas->scene3D()) : nullptr;
        if (scenePanel) stack->addWidget(scenePanel);
        const auto updatePage = [canvas, stack, scenePanel] {
            stack->setEnabled(canvas->hasProject() && canvas->toolAvailable());
            const auto tool = canvas->tool();
            const bool selection = tool == Canvas::Tool::RectangleSelect || tool == Canvas::Tool::LassoSelect
                || tool == Canvas::Tool::Transform || canvas->document()->transformActive();
            const bool vectorEdit = tool == Canvas::Tool::VectorNode || tool == Canvas::Tool::VectorSimplify || tool == Canvas::Tool::VectorWidth || tool == Canvas::Tool::VectorNudge;
            const bool scene = scenePanel && (tool == Canvas::Tool::SceneObject || tool == Canvas::Tool::SceneCamera || tool == Canvas::Tool::SceneRig);
            if(scene && tool == Canvas::Tool::SceneRig) scenePanel->setRigMode();
            else if(scene) scenePanel->setCameraMode(tool == Canvas::Tool::SceneCamera);
            const int page = scene ? 5 : vectorEdit ? 4 : selection ? 1 : tool == Canvas::Tool::Guides ? 2 : tool>=Canvas::Tool::Eyedropper ? 3 : 0;
            if (stack->currentIndex() != page) {
                stack->setCurrentIndex(page);
                stack->updateGeometry();
            }
        };
        QObject::connect(canvas, &Canvas::toolChanged, stack, updatePage);
        QObject::connect(canvas, &Canvas::toolAvailabilityChanged, stack, updatePage);
        QObject::connect(canvas, &Canvas::documentChanged, stack, updatePage);
        QObject::connect(canvas->document(), &Document::transformChanged, stack, updatePage);
        updatePage(); return stack;
    }
    if (kind == "layersPanel") {
        auto *panel = new LayerPanel(canvas->document());
        QObject::connect(canvas->scene3D(), &Scene3DController::previewChanged, panel, &LayerPanel::refreshReferencePreviews);
        QObject::connect(panel, &LayerPanel::selectedLayersChanged, canvas, &Canvas::refreshLayerSelectionContext);
        return panel;
    }
    if (kind == "colorsPanel") return new ColorPanel(canvas);
    auto *widget = new QWidget;
    widget->setProperty("compactDrawingPanel", true);
    widget->setMinimumWidth(220);
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(ui::metrics::panelMargin, ui::metrics::panelMargin, ui::metrics::panelMargin, ui::metrics::panelMargin);
    auto *doc = canvas->document();
    if (kind == "toolsPanel") {
        struct ToolButton { QString label; Canvas::Tool tool; QString icon; const char *id; int category; };
        using T=Canvas::Tool;
        const QList<ToolButton> tools = {
            {"Brush (B)",T::Brush,"brush","toolBrush",0}, {"Eraser (E)",T::Eraser,"eraser","toolEraser",0},
            {"Eyedropper (I)",T::Eyedropper,"pipette","toolEyedropper",0}, {"Paint bucket (F)",T::Bucket,"paint-bucket","toolBucket",0},
            {"Gradient (D)",T::Gradient,featureIconName(FeatureIcon::LinearGradient),"toolGradient",0}, {"Straight line",T::Line,"minus","toolLine",0},
            {"Rectangle",T::Rectangle,"rectangle-horizontal","toolShapeRectangle",0}, {"Ellipse",T::Ellipse,"circle","toolEllipse",0},
            {"Polygon — click points, Enter to finish",T::Polygon,"pentagon","toolPolygon",0},
            {"Arc — drag endpoints, lift, then place the bend",T::Arc,featureIconName(FeatureIcon::Arc),"toolArc",0},
            {"Cubic curve — start, two controls, end",T::Curve,featureIconName(FeatureIcon::Bezier),"toolCurve",0},
            {"Vector pen — click/drag nodes, Enter to finish",T::VectorPen,"pen-tool","toolVectorPen",0}, {"Artwork text",T::Text,"type","toolText",0},
            {"Rectangle selection (M)",T::RectangleSelect,"square-dashed","toolRectangle",1}, {"Lasso selection (L)",T::LassoSelect,"lasso","toolLasso",1},
            {"Ellipse selection",T::EllipseSelect,"circle-dashed","toolEllipseSelect",1},
            {"Polygon selection — click points, Enter to finish",T::PolygonSelect,"pentagon","toolPolygonSelect",1},
            {"Magic wand — contiguous color",T::MagicWand,"wand-sparkles","toolMagicWand",1}, {"Color range — matching colors",T::ColorRange,"palette","toolColorRange",1},
            {"Transform (T)",T::Transform,"move","toolTransform",2}, {"Distort, perspective and warp",T::AdvancedTransform,"square-dashed-mouse-pointer","toolAdvancedTransform",2},
            {"Edit vector nodes",T::VectorNode,"mouse-pointer-2","toolVectorNode",2}, {"Vector eraser",T::VectorEraser,"scissors","toolVectorEraser",2},
            {"Retouch: smudge, mix, blur, sharpen, dodge and burn",T::Retouch,"droplet","toolRetouch",2},
            {"Hand (H)",T::Hand,"hand","toolHand",2}, {"Perspective guides (G)",T::Guides,featureIconName(FeatureIcon::PerspectiveGrid),"toolGuides",2}};
        auto *tabs=new QTabWidget;tabs->setObjectName("drawingToolCategories");
        tabs->tabBar()->setProperty("compactDrawingTabs", true); tabs->setIconSize({ui::metrics::tabIcon, ui::metrics::tabIcon});
        const char *tabIcons[]={"brush","square-dashed","settings-2"};const char *tabNames[]={"Draw","Select","Edit"};
        QVector<QGridLayout*> grids;int counts[3]={};
        for(int i=0;i<3;++i){auto*page=new QWidget;auto*grid=new QGridLayout(page);grid->setContentsMargins(ui::metrics::controlGap,ui::metrics::controlGap,ui::metrics::controlGap,ui::metrics::controlGap);grid->setSpacing(ui::metrics::controlGap);grids.append(grid);tabs->addTab(page,appIcon(tabIcons[i]),"");tabs->setTabToolTip(i,tabNames[i]);tabs->tabBar()->setAccessibleTabName(i,tabNames[i]);}
        auto *group=new QButtonGroup(widget);group->setExclusive(true);
        for(const auto &entry:tools){
            auto *button=new QToolButton;button->setCheckable(true);group->addButton(button);button->setObjectName(entry.id);
            button->setIcon(appIcon(entry.icon));button->setIconSize({ui::metrics::primaryIcon,ui::metrics::primaryIcon});
            button->setProperty("compactDrawingAction", true); button->setMinimumSize(ui::metrics::actionTarget,ui::metrics::actionTarget);
            button->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
            button->setToolTip(entry.label);button->setAccessibleName(entry.label);button->setChecked(canvas->tool()==entry.tool);
            int n=counts[entry.category]++;grids[entry.category]->addWidget(button,n/4,n%4);
            QObject::connect(button,&QToolButton::clicked,widget,[canvas,entry]{canvas->setTool(entry.tool);canvas->setFocus();});
            QObject::connect(canvas,&Canvas::toolChanged,button,[canvas,button,entry,tabs]{button->setChecked(canvas->tool()==entry.tool);if(canvas->tool()==entry.tool)tabs->setCurrentIndex(entry.category);});
        }
        layout->addWidget(tabs);
    }
    layout->addStretch();
    widget->setEnabled(doc->isOpen());
    QObject::connect(canvas, &Canvas::documentChanged, widget, [doc, widget] { widget->setEnabled(doc->isOpen()); });
    return widget;
}
} // namespace toon
