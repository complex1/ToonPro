#pragma once
#include <QString>

namespace toon {
class Document;
struct Shot;
// Layer IDs refer to the shared Layers hierarchy, never exposure or track IDs.
// beforeId uses the saved bottom-to-top sibling order; empty appends at the top.
QString timelineOrganizationEditError(const Shot &, const QString &layerId,
                                      const QString &parentId, const QString &beforeId = {});
bool moveTimelineLayer(Document *, const QString &layerId, const QString &parentId,
                       const QString &beforeId = {}, QString *error = nullptr);
bool addTimelineFolder(Document *, const QString &parentId, const QString &name,
                       QString *createdId = nullptr, QString *error = nullptr);
}
