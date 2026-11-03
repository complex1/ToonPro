#pragma once
#include "document.h"
#include <QJsonObject>
#include <QPointer>

namespace toon {
struct AutoActionStep {
    QString operation, label;
    QJsonObject parameters;
    bool enabled = true;
};
struct AutoAction { QString id, name; QVector<AutoActionStep> steps; };
class AutoActions : public QObject {
    Q_OBJECT
public:
    explicit AutoActions(Document *document, QString path = {}, QObject *parent = nullptr);
    const QVector<AutoAction> &actions() const { return m_actions; }
    const AutoAction *action(const QString &id) const;
    QString create(const QString &name);
    bool rename(const QString &id, const QString &name);
    QString duplicate(const QString &id);
    bool remove(const QString &id);
    bool startRecording(const QString &id);
    void stopRecording();
    bool recording() const { return !m_recording.isEmpty(); }
    QString recordingId() const { return m_recording; }
    bool playing() const { return m_playing; }
    bool play(const QString &id);
    bool setStepEnabled(const QString &id, int index, bool enabled);
    bool removeStep(const QString &id, int index);
    bool moveStep(const QString &id, int from, int to);
    bool load(QString *error = nullptr);
    bool save(QString *error = nullptr);
    QString path() const { return m_path; }
    QString availabilityReason() const;
    static constexpr int maxActions = 128, maxSteps = 256;
signals:
    void changed();
    void stateChanged();
    void status(const QString &message);
private:
    bool fail(const QString &message, QString *error = nullptr);
    bool edited();
    AutoAction *mutableAction(const QString &id);
    void append(AutoActionStep step);
    void halt(const QString &reason);
    void committed(const Project &before, const Project &after, const QString &label,
                   const QString &beforeLayer, const QString &afterLayer, const QJsonObject &action);
    QPointer<Document> m_document;
    QString m_path, m_recording, m_recordLayer, m_recordShot, m_saveError;
    int m_recordFrame = 0;
    RasterSelection m_recordSelection;
    QVector<AutoAction> m_actions;
    bool m_playing = false, m_loadFailed = false;
};
}
