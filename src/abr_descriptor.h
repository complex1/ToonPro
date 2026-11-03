#pragma once
#include "brush_settings.h"
#include <QByteArrayView>
#include <QStringList>
#include <QVector>

namespace toon {
struct AbrDescriptorPreset {
    QString name, sampleId;
    BrushSettings settings;
    QStringList compatibility;
    bool computed = false;
};
struct AbrDescriptorResult {
    QVector<AbrDescriptorPreset> presets;
    QStringList warnings;
    QString error;
    bool unsupported = false;
};
// Adobe action descriptors (version 16), bounded independently from sample data.
AbrDescriptorResult readAbrDescriptors(QByteArrayView bytes);
}
