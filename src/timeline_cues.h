#pragma once
#include <QColor>
#include <QJsonArray>
#include <QString>
#include <QVector>
#include <optional>

namespace toon {
class Document;
struct Shot;

inline constexpr int maxShotTimelineCues = 1024;
inline constexpr int maxProjectTimelineCues = 16384;
enum class TimelineCueKind { Marker, KeyPose, Breakdown, Inbetween, Dialogue };

// Editorial timing only: never part of the rendered artwork.
struct TimelineCue {
    QString id;
    TimelineCueKind kind = TimelineCueKind::Marker;
    int startFrame = 0;
    int durationFrames = 1;
    QString name, speaker, text;
    QColor color = QColor("#c7a56b");
    bool operator==(const TimelineCue &) const = default;
};

QString timelineCueKindKey(TimelineCueKind kind);
QString timelineCueKindName(TimelineCueKind kind);
std::optional<TimelineCueKind> timelineCueKindFromKey(const QString &key);
const TimelineCue *timelineCue(const Shot &shot, const QString &id);
QString validateTimelineCues(const Shot &shot);
qint64 timelineCueHistoryCost(const Shot &before, const Shot &after);
QJsonArray timelineCuesToJson(const QVector<TimelineCue> &cues);
bool timelineCuesFromJson(const QJsonArray &array, QVector<TimelineCue> *cues, QString *error);
bool previewTimelineCueTiming(const TimelineCue &cue, int parentDuration, int start, int duration,
                             TimelineCue *out, QString *error = nullptr);
QString addTimelineCue(Document *document, TimelineCue cue, QString *error = nullptr);
bool updateTimelineCue(Document *document, const TimelineCue &cue, QString *error = nullptr);
bool removeTimelineCue(Document *document, const QString &id, QString *error = nullptr);
} // namespace toon
