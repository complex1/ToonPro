#pragma once
#include "project.h"
namespace toon {
struct MotionStudy {
    QString name;
    double fps=24, duration=0;
    QVector<double> times;
    QVector<bool> kept;
    QString validate() const;
    void sample(int step);
    int heldFrame(int sourceFrame) const;
    QJsonObject toJson() const;
    static std::optional<MotionStudy> fromJson(const QJsonObject &,QString *error);
};
// Creates independent cels, preserving elapsed time at the destination FPS.
// The caller publishes the validated project as one Undo transaction.
bool appendMotionStudy(Project *project,int shotIndex,int start,const MotionStudy &study,
                       const QString &framesDirectory,bool reference,QString *error);
}
