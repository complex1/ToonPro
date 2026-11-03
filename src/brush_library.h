#pragma once

#include "raster.h"
#include <QObject>
#include <QStringList>
#include <QVector>

namespace toon {

struct BrushPreset {
    QString id;
    QString name;
    BrushSettings brush;
    qreal smoothing = 0;
    QString source;
    QStringList compatibility;
    bool builtIn = false;
};

// Presets belong to the local user library; project files store painted pixels.
class BrushLibrary : public QObject {
    Q_OBJECT
public:
    static constexpr int maxCustomPresets = 128;
    static constexpr int maxTipSide = 2048;
    // Shared pixel budget for primary tips, textures, and dual tips.
    static constexpr qint64 maxTipPixels = 64 * 1024 * 1024;
    static constexpr qint64 maxFileBytes = 96 * 1024 * 1024;

    explicit BrushLibrary(QString path, QObject *parent = nullptr);
    const QVector<BrushPreset> &presets() const { return m_presets; }
    QString path() const { return m_path; }
    bool writable() const { return m_writable; }

    // Call once before editing. A failed load leaves the current list untouched
    // and disables writes until a later successful load, preserving the old file.
    bool load(QString *error = nullptr);
    bool addPresets(const QVector<BrushPreset> &presets, QString *error = nullptr);
    bool saveCurrent(const QString &name, const BrushSettings &brush, qreal smoothing,
                     QString *error = nullptr);
    bool renamePreset(const QString &id, const QString &name, QString *error = nullptr);
    bool removePreset(const QString &id, QString *error = nullptr);

signals:
    void changed();

private:
    bool commit(const QVector<BrushPreset> &presets, QString *error);
    bool checkWritable(QString *error) const;
    QString m_path;
    QVector<BrushPreset> m_presets;
    bool m_writable = false;
};

} // namespace toon
