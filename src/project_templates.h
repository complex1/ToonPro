#pragma once
#include "project.h"

namespace toon {
struct ProjectStarterTemplate {
    QString id, name, summary, icon, workspaceId;
};
const QVector<ProjectStarterTemplate> &projectStarterTemplates();
// A fresh project owns fresh drawing/track IDs. Selecting a template in the
// dialog constructs it once; ordinary field refreshes preserve those IDs.
std::optional<Project> projectFromStarterTemplate(const QString &id, ProjectKind kind = ProjectKind::Film);
QString starterDrawingLayerId(const Project &project, const QString &templateId);
} // namespace toon
