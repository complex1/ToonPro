#pragma once
#include "line_art.h"
#include "project.h"
namespace toon {
QString lineArtSourceReason(const Project &,int shot,const QString &layer,int frame);
LineArtInput lineArtSource(const Project &,int shot,const QString &layer,int frame,QString *error=nullptr);
}
