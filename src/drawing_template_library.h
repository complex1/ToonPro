#pragma once
#include "animation.h"
#include <QObject>

namespace toon {
inline constexpr int maxDrawingTemplates = 128;
struct DrawingTemplateEntry {
    QString id, name;
    QVector<DrawingTemplateSlot> layers;
    bool operator==(const DrawingTemplateEntry &) const = default;
};

// A local preset library. Only names and layer types are stored; artwork remains
// in projects. A damaged/newer or externally changed file is never overwritten.
class DrawingTemplateLibrary : public QObject {
    Q_OBJECT
public:
    explicit DrawingTemplateLibrary(QString filePath, QObject *parent = nullptr);
    bool load(QString *error = nullptr);
    const QVector<DrawingTemplateEntry> &entries() const { return m_entries; }
    bool writable() const { return m_writable; }
    QString loadError() const { return m_error; }
    QString filePath() const { return m_filePath; }
    bool saveTemplate(const QString &name, const QVector<DrawingTemplateSlot> &layers, QString *error = nullptr);
    bool removeTemplate(const QString &id, QString *error = nullptr);
signals:
    void changed();
private:
    bool commit(const QVector<DrawingTemplateEntry> &entries, QString *error);
    bool fail(const QString &message, QString *error);
    QString m_filePath, m_error;
    QVector<DrawingTemplateEntry> m_entries;
    QByteArray m_loadedBytes;
    bool m_existed = false, m_writable = false;
};
} // namespace toon
