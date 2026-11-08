#include "document.h"
#include <QUuid>

namespace toon {
bool Document::addPerspectiveGuides(const QVector<DrawingGuide> &guides,const QString &shotId,
                                    quint64 expectedRevision,int frame,const QString &sourceId) {
    const auto fail=[this](const QString &reason){emit warning(reason);return false;};
    if(!isOpen()||editingGestureActive()||layerPropertyEditActive())return fail("Finish the current edit before adding guides.");
    if(activeShotId()!=shotId||contentRevision()!=expectedRevision||currentFrame()!=frame
        ||(!sourceId.isEmpty()&&activeShot().layers[activeLayer()].id!=sourceId))
        return fail("The drawing or source changed. Reopen perspective detection to use the current image.");
    if(guides.isEmpty())return fail("Choose a supported perspective candidate or draw two guide lines first.");
    if(guides.size()+activeShot().guides.size()>maxDrawingGuides)return fail("A drawing supports up to 64 guides. Remove a guide before adding more.");
    auto after=project();auto &shot=after.shots[activeShotIndex()];
    for(auto guide:guides){
        guide.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        if(guide.name.isEmpty())guide.name="Detected "+guideTypeName(guide.type);
        shot.guides.append(guide);
    }
    // Guide visibility is deliberate when adding a new proposal. Snapping
    // remains the artist's existing independent preference.
    shot.guideSettings.visible=true;
    const auto error=after.validate();if(!error.isEmpty())return fail(error);
    return commit(project(),after,"Add detected perspective guides");
}
}
