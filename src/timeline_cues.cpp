#include "timeline_cues.h"
#include "document.h"
#include "project.h"
#include <QJsonObject>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <new>

namespace toon {
namespace {
bool fail(QString *error, const QString &message) {
    if (error) *error = message;
    return false;
}
bool report(Document *document, QString *error, const QString &message) {
    if (error) *error = message;
    else if (document) emit document->warning(message);
    return false;
}
bool validSingleLine(const QString &text, bool required) {
    return (!required || !text.isEmpty()) && text.size() <= 100 && text == text.trimmed()
        && std::none_of(text.cbegin(), text.cend(), [](QChar ch) {
            return ch.category() == QChar::Other_Control || ch == QChar::LineSeparator || ch == QChar::ParagraphSeparator;
        });
}
QString cueError(const TimelineCue &cue, int duration) {
    if (QUuid(cue.id).isNull()) return "A timeline cue needs a valid identifier.";
    if (timelineCueKindKey(cue.kind).isEmpty()) return "Choose a supported timeline cue kind.";
    if (!validSingleLine(cue.name, true)) return "Cue names must contain 1 to 100 characters on one line.";
    if (!validSingleLine(cue.speaker, false)) return "Speaker names allow up to 100 characters on one line.";
    if (cue.text.size() > 4096 || std::any_of(cue.text.cbegin(), cue.text.cend(), [](QChar ch) {
            return ch.category() == QChar::Other_Control && ch != QLatin1Char('\n') && ch != QLatin1Char('\t');
        })) return "Cue notes allow up to 4096 characters without control characters other than newlines and tabs.";
    if (!cue.color.isValid() || cue.color.alpha() != 255) return "Choose an opaque cue color.";
    if (duration < 1 || duration > 1000000 || cue.startFrame < 0 || cue.durationFrames < 1
        || qint64(cue.startFrame) + cue.durationFrames > duration)
        return "Keep the cue inside the shot with a duration of at least one frame.";
    return {};
}
bool ready(Document *document, QString *error) {
    if (error) error->clear();
    if (!document || !document->isOpen()) return report(document, error, "Open a project first.");
    if (document->editingGestureActive() || document->layerPropertyEditActive())
        return report(document, error, "Finish the current edit before changing timeline cues.");
    return true;
}
bool commit(Document *document, const Project &after, const QString &label, QString *error) {
    const auto invalid = after.validate();
    if (!invalid.isEmpty()) return report(document, error, invalid);
    QString detail;
    const auto connection = QObject::connect(document, &Document::warning, document,
        [&](const QString &message) { detail = message; });
    const bool okay = document->replaceSymbolProject(after, label);
    QObject::disconnect(connection);
    return okay || report(document, error, detail.isEmpty() ? QString("Could not change the timeline cue.") : detail);
}
int cueIndex(const Shot &shot, const QString &id) {
    for (int i = 0; i < shot.timelineCues.size(); ++i) if (shot.timelineCues[i].id == id) return i;
    return -1;
}
} // namespace

QString timelineCueKindKey(TimelineCueKind kind) {
    switch (kind) {
    case TimelineCueKind::Marker: return "marker";
    case TimelineCueKind::KeyPose: return "key-pose";
    case TimelineCueKind::Breakdown: return "breakdown";
    case TimelineCueKind::Inbetween: return "inbetween";
    case TimelineCueKind::Dialogue: return "dialogue";
    }
    return {};
}
QString timelineCueKindName(TimelineCueKind kind) {
    switch (kind) {
    case TimelineCueKind::Marker: return "Marker";
    case TimelineCueKind::KeyPose: return "Key pose";
    case TimelineCueKind::Breakdown: return "Breakdown";
    case TimelineCueKind::Inbetween: return "In-between";
    case TimelineCueKind::Dialogue: return "Dialogue";
    }
    return {};
}
std::optional<TimelineCueKind> timelineCueKindFromKey(const QString &key) {
    for (auto kind : {TimelineCueKind::Marker, TimelineCueKind::KeyPose, TimelineCueKind::Breakdown,
                      TimelineCueKind::Inbetween, TimelineCueKind::Dialogue})
        if (timelineCueKindKey(kind) == key) return kind;
    return {};
}
const TimelineCue *timelineCue(const Shot &shot, const QString &id) {
    const int index = cueIndex(shot, id);
    return index < 0 ? nullptr : &shot.timelineCues[index];
}
QString validateTimelineCues(const Shot &shot) {
    if (shot.timelineCues.size() > maxShotTimelineCues) return "A shot supports up to 1024 timeline cues.";
    QSet<QUuid> ids;
    for (const auto &cue : shot.timelineCues) {
        const auto error = cueError(cue, shot.durationFrames);
        if (!error.isEmpty()) return error;
        if (ids.contains(QUuid(cue.id))) return "Timeline cue identifiers must be unique.";
        ids.insert(QUuid(cue.id));
    }
    return {};
}
qint64 timelineCueHistoryCost(const Shot &before, const Shot &after) {
    if (before.timelineCues == after.timelineCues) return 0;
    const auto size = [](const Shot &shot) {
        qint64 bytes = shot.timelineCues.size() * qint64(sizeof(TimelineCue));
        for (const auto &cue : shot.timelineCues)
            bytes += 2LL * (cue.id.size() + cue.name.size() + cue.speaker.size() + cue.text.size());
        return bytes;
    };
    return size(before) + size(after);
}
QJsonArray timelineCuesToJson(const QVector<TimelineCue> &cues) {
    QJsonArray array;
    for (const auto &cue : cues) array.append(QJsonObject{
        {"id", cue.id}, {"kind", timelineCueKindKey(cue.kind)}, {"startFrame", cue.startFrame},
        {"durationFrames", cue.durationFrames}, {"name", cue.name}, {"speaker", cue.speaker},
        {"text", cue.text}, {"color", cue.color.name(QColor::HexRgb)}});
    return array;
}
bool timelineCuesFromJson(const QJsonArray &array, QVector<TimelineCue> *cues, QString *error) {
    if (error) error->clear();
    if (!cues) return fail(error, "Missing timeline cue destination.");
    if (array.size() > maxShotTimelineCues) return fail(error, "A shot supports up to 1024 timeline cues.");
    QVector<TimelineCue> parsed;
    QSet<QUuid> ids;
    for (const auto &value : array) {
        const auto object = value.toObject();
        if (!value.isObject() || object.size() != 8 || !object.value("id").isString()
            || !object.value("kind").isString() || !object.value("name").isString()
            || !object.value("speaker").isString() || !object.value("text").isString()
            || !object.value("color").isString()) return fail(error, "Invalid timeline cue metadata.");
        const auto kind = timelineCueKindFromKey(object.value("kind").toString());
        if (!kind) return fail(error, "Unsupported timeline cue kind.");
        const auto integer = [&](const char *key, int minimum, int *out) {
            const auto value = object.value(QLatin1String(key));
            const double number = value.toDouble(-1);
            if (!value.isDouble() || !std::isfinite(number) || number != std::floor(number)
                || number < minimum || number > 1000000) return false;
            *out = int(number); return true;
        };
        TimelineCue cue;
        cue.id = object.value("id").toString(); cue.kind = *kind;
        cue.name = object.value("name").toString(); cue.speaker = object.value("speaker").toString();
        cue.text = object.value("text").toString();
        const auto color = object.value("color").toString();
        if (color.size() != 7 || !color.startsWith(QLatin1Char('#'))
            || std::any_of(color.cbegin() + 1, color.cend(), [](QChar ch) {
                return !((ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))
                    || (ch.toLower() >= QLatin1Char('a') && ch.toLower() <= QLatin1Char('f')));
            })) return fail(error, "Cue colors must be #RRGGBB hexadecimal values.");
        cue.color = QColor(color);
        if (!integer("startFrame", 0, &cue.startFrame) || !integer("durationFrames", 1, &cue.durationFrames))
            return fail(error, "Invalid timeline cue timing.");
        const auto invalid = cueError(cue, 1000000);
        if (!invalid.isEmpty()) return fail(error, invalid);
        if (ids.contains(QUuid(cue.id))) return fail(error, "Timeline cue identifiers must be unique.");
        ids.insert(QUuid(cue.id)); parsed.append(std::move(cue));
    }
    *cues = std::move(parsed); return true;
}
bool previewTimelineCueTiming(const TimelineCue &cue, int parentDuration, int start, int duration,
                             TimelineCue *out, QString *error) {
    if (error) error->clear();
    if (!out) return fail(error, "Missing timeline cue preview destination.");
    auto result = cue; result.startFrame = start; result.durationFrames = duration;
    const auto invalid = cueError(result, parentDuration);
    if (!invalid.isEmpty()) return fail(error, invalid);
    *out = std::move(result); return true;
}
QString addTimelineCue(Document *document, TimelineCue cue, QString *error) {
    if (!ready(document, error)) return {};
    try {
        if (cue.id.isEmpty()) cue.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        cue.name = cue.name.trimmed(); cue.speaker = cue.speaker.trimmed();
        auto after = document->project();
        after.shots[document->activeShotIndex()].timelineCues.append(cue);
        return commit(document, after, "Add timeline cue", error) ? cue.id : QString{};
    } catch (const std::bad_alloc &) { report(document, error, "There is not enough memory to add a timeline cue."); return {}; }
}
bool updateTimelineCue(Document *document, const TimelineCue &cue, QString *error) {
    if (!ready(document, error)) return false;
    const int index = cueIndex(document->activeShot(), cue.id);
    if (index < 0) return report(document, error, "This timeline cue no longer exists.");
    try {
        auto clean = cue; clean.name = clean.name.trimmed(); clean.speaker = clean.speaker.trimmed();
        if (document->activeShot().timelineCues[index] == clean) return true;
        auto after = document->project(); after.shots[document->activeShotIndex()].timelineCues[index] = std::move(clean);
        return commit(document, after, "Edit timeline cue", error);
    } catch (const std::bad_alloc &) { return report(document, error, "There is not enough memory to edit this timeline cue."); }
}
bool removeTimelineCue(Document *document, const QString &id, QString *error) {
    if (!ready(document, error)) return false;
    const int index = cueIndex(document->activeShot(), id);
    if (index < 0) return report(document, error, "This timeline cue no longer exists.");
    try {
        auto after = document->project(); after.shots[document->activeShotIndex()].timelineCues.removeAt(index);
        return commit(document, after, "Remove timeline cue", error);
    } catch (const std::bad_alloc &) { return report(document, error, "There is not enough memory to remove this timeline cue."); }
}
} // namespace toon
