#pragma once

#include <QByteArray>
#include <QColor>
#include <QObject>
#include <QVector>

namespace toon {
inline constexpr int maxColorPalettes = 64;
inline constexpr int maxPaletteSwatches = 256;
struct ColorSwatch {
    QString id, name;
    QColor color;
    bool operator==(const ColorSwatch &) const = default;
};
struct ColorPalette {
    QString id, name;
    QVector<ColorSwatch> swatches;
    bool operator==(const ColorPalette &) const = default;
};
ColorPalette standardColorPalette();
// Global artist palettes, separate from project artwork. Invalid/newer and
// externally changed files are preserved instead of silently replaced.
class PaletteLibrary final : public QObject {
    Q_OBJECT
public:
    explicit PaletteLibrary(QString filePath, QObject *parent = nullptr);
    bool load(QString *error = nullptr);
    const QVector<ColorPalette> &entries() const { return m_entries; }
    const ColorPalette *palette(const QString &id) const;
    bool writable() const { return m_writable; }
    QString loadError() const { return m_error; }
    QString filePath() const { return m_filePath; }
    QString createPalette(const QString &name, QString *error = nullptr);
    bool renamePalette(const QString &id, const QString &name, QString *error = nullptr);
    bool removePalette(const QString &id, QString *error = nullptr);
    QString addSwatch(const QString &paletteId, const QString &name, const QColor &color, QString *error = nullptr);
    bool updateSwatch(const QString &paletteId, const QString &swatchId, const QColor &color, QString *error = nullptr);
    bool renameSwatch(const QString &paletteId, const QString &swatchId, const QString &name, QString *error = nullptr);
    bool removeSwatch(const QString &paletteId, const QString &swatchId, QString *error = nullptr);
signals:
    void changed();
private:
    bool commit(const QVector<ColorPalette> &entries, QString *error);
    bool fail(const QString &message, QString *error);
    QString m_filePath, m_error;
    QVector<ColorPalette> m_entries;
    QByteArray m_loadedBytes;
    bool m_existed = false, m_writable = false;
};
} // namespace toon
