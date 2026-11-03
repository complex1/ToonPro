#include "mp4_timing.h"
#include <QtEndian>
#include <QVector>
#include <cmath>
#include <limits>
#include <numeric>

namespace toon {
namespace {
// Field layouts were checked against the Qt-bundled FFmpeg 7.1 MOV writer:
// https://github.com/FFmpeg/FFmpeg/blob/n7.1.3/libavformat/movenc.c
// This finalizer accepts only the small, non-fragmented files generated here.
constexpr qsizetype maximumFileSize = 80 * 1024 * 1024;
constexpr quint32 movieTimescale = 1000000;
constexpr quint32 tag(char a, char b, char c, char d) {
    return (quint32(a) << 24) | (quint32(b) << 16) | (quint32(c) << 8) | quint32(d);
}
struct Box {
    qsizetype begin = 0, payload = 0, end = 0;
    quint32 type = 0;
    qsizetype size() const { return end - payload; }
};
struct Field { qsizetype position = 0; int width = 0; };
struct Patch { Field field; quint64 value = 0; };
struct TimedHeader { Field timescale, duration; quint32 scale = 0; quint64 length = 0; };
struct Track {
    Box box;
    bool video = false;
    Field duration;
    quint64 length = 0;
};

class TimingCorrection {
public:
    explicit TimingCorrection(const QByteArray &bytes) : m_bytes(bytes) {}
    QString error;
    QVector<Patch> patches;

    bool prepare(double fps) {
        if (m_bytes.size() < 32 || m_bytes.size() > maximumFileSize)
            return fail("The single-frame MP4 has an invalid or unsupported size.");
        QVector<Box> top;
        if (!children(0, m_bytes.size(), top)) return false;
        Box ftyp, moov;
        if (!one(top, tag('f','t','y','p'), ftyp) || !one(top, tag('m','o','o','v'), moov)) return false;
        if (moov.end - moov.begin > 1024 * 1024)
            return fail("The single-frame MP4 metadata is too large.");
        if (ftyp.size() < 8 || (ftyp.size() - 8) % 4)
            return fail("The MP4 file-type box is malformed.");
        for (const auto &box : top) {
            if (box.type == tag('m','d','a','t')) m_media.append(box);
            if (box.type == tag('m','o','o','f') || box.type == tag('s','i','d','x')
                || box.type == tag('m','f','r','a'))
                return fail("Fragmented MP4 timing is not supported by this finalizer.");
        }
        if (m_media.isEmpty()) return fail("The MP4 contains no media data.");
        QVector<Box> movie;
        if (!children(moov.payload, moov.end, movie)) return false;
        for (const auto &box : movie)
            if (box.type == tag('m','v','e','x')) return fail("Fragmented MP4 timing is not supported by this finalizer.");
        Box mvhd;
        TimedHeader movieHeader;
        if (!one(movie, tag('m','v','h','d'), mvhd) || !timedHeader(mvhd, true, movieHeader)) return false;
        m_oldMovieScale = movieHeader.scale;
        const auto roundedRate = quint64(std::llround(fps * movieTimescale));
        const auto divisor = std::gcd(roundedRate, quint64(movieTimescale));
        m_videoScale = quint32(roundedRate / divisor);
        m_videoDuration = quint32(movieTimescale / divisor);
        m_movieVideoDuration = (quint64(m_videoDuration) * movieTimescale + m_videoScale / 2) / m_videoScale;

        QVector<Track> tracks;
        int videoCount = 0;
        for (const auto &box : movie) {
            if (box.type != tag('t','r','a','k')) continue;
            Track track;
            if (!readTrack(box, track)) return false;
            videoCount += track.video ? 1 : 0;
            tracks.append(track);
        }
        if (videoCount != 1 || tracks.size() > 2)
            return fail("The MP4 must contain exactly one video track and at most one audio track.");
        quint64 movieDuration = 0;
        for (const auto &track : tracks) {
            quint64 length = m_movieVideoDuration;
            if (!track.video && !rescale(track.length, m_oldMovieScale, length)) return false;
            if (!patch(track.duration, length) || !correctTrack(track)) return false;
            movieDuration = qMax(movieDuration, length);
        }
        return patch(movieHeader.timescale, movieTimescale) && patch(movieHeader.duration, movieDuration);
    }

private:
    bool fail(const QString &message) { if (error.isEmpty()) error = message; return false; }
    quint32 u32(qsizetype position) const { return qFromBigEndian<quint32>(m_bytes.constData() + position); }
    quint64 u64(qsizetype position) const { return qFromBigEndian<quint64>(m_bytes.constData() + position); }
    quint64 value(Field field) const { return field.width == 4 ? u32(field.position) : u64(field.position); }
    bool children(qsizetype begin, qsizetype end, QVector<Box> &result) {
        while (begin < end) {
            if (++m_boxes > 4096 || end - begin < 8) return fail("The MP4 box structure is malformed or too complex.");
            quint64 length = u32(begin);
            const quint32 type = u32(begin + 4);
            qsizetype header = 8;
            if (length == 1) {
                if (end - begin < 16) return fail("The MP4 extended-size box is truncated.");
                length = u64(begin + 8); header = 16;
            }
            if (length < quint64(header) || length > quint64(end - begin))
                return fail("The MP4 box has an invalid size.");
            const auto next = begin + qsizetype(length);
            result.append({begin, begin + header, next, type});
            begin = next;
        }
        return begin == end;
    }
    bool one(const QVector<Box> &boxes, quint32 type, Box &result, bool required = true) {
        int count = 0;
        for (const auto &box : boxes) if (box.type == type) { result = box; ++count; }
        if (count > 1 || (required && count != 1)) return fail("The MP4 contains missing or duplicate timing boxes.");
        return true;
    }
    bool version(const Box &box, int &result, bool allowOne = true) {
        if (box.size() < 4) return fail("The MP4 timing box is truncated.");
        result = quint8(m_bytes[box.payload]);
        if (result > (allowOne ? 1 : 0)) return fail("The MP4 timing box version is unsupported.");
        return true;
    }
    bool flagsZero(const Box &box) {
        if (box.size() < 4 || (u32(box.payload) & 0x00ffffff))
            return fail("The MP4 timing box contains unsupported flags.");
        return true;
    }
    bool timedHeader(const Box &box, bool movie, TimedHeader &header) {
        int v;
        if (!version(box, v) || !flagsZero(box)) return false;
        const qsizetype expected = movie ? (v ? 112 : 100) : (v ? 36 : 24);
        if (box.size() != expected) return fail("The MP4 duration header has an unexpected size.");
        header.timescale = {box.payload + (v ? 20 : 12), 4};
        header.duration = {box.payload + (v ? 24 : 16), v ? 8 : 4};
        header.scale = quint32(value(header.timescale)); header.length = value(header.duration);
        if (!header.scale) return fail("The MP4 time scale is zero.");
        return true;
    }
    bool readTrack(const Box &box, Track &track) {
        track.box = box;
        QVector<Box> childrenOfTrack, media;
        Box tkhd, mdia, hdlr;
        if (!children(box.payload, box.end, childrenOfTrack) || !one(childrenOfTrack, tag('t','k','h','d'), tkhd)
            || !one(childrenOfTrack, tag('m','d','i','a'), mdia) || !children(mdia.payload, mdia.end, media)
            || !one(media, tag('h','d','l','r'), hdlr)) return false;
        int v;
        if (!version(tkhd, v)) return false;
        if (tkhd.size() != (v ? 96 : 84)) return fail("The MP4 track header has an unexpected size.");
        track.duration = {tkhd.payload + (v ? 28 : 20), v ? 8 : 4}; track.length = value(track.duration);
        if (!version(hdlr, v, false) || !flagsZero(hdlr)) return false;
        if (hdlr.size() < 24) return fail("The MP4 handler box is truncated.");
        const auto handler = u32(hdlr.payload + 8);
        track.video = handler == tag('v','i','d','e');
        if (!track.video && handler != tag('s','o','u','n'))
            return fail("The MP4 contains an unsupported track type.");
        return true;
    }
    bool correctTrack(const Track &track) {
        QVector<Box> trackBoxes, mediaBoxes;
        Box mdia, mdhd, edts;
        if (!children(track.box.payload, track.box.end, trackBoxes)
            || !one(trackBoxes, tag('m','d','i','a'), mdia)
            || !one(trackBoxes, tag('e','d','t','s'), edts, false)
            || !children(mdia.payload, mdia.end, mediaBoxes)
            || !one(mediaBoxes, tag('m','d','h','d'), mdhd)) return false;
        TimedHeader mediaHeader;
        if (!timedHeader(mdhd, false, mediaHeader)) return false;
        if (edts.type && !correctEdits(edts, track.video)) return false;
        if (!track.video) return true;
        Box minf, stbl, stts, stsz, stsc, stco, co64, ctts;
        QVector<Box> mediaInfo, sampleTable;
        if (!one(mediaBoxes, tag('m','i','n','f'), minf) || !children(minf.payload, minf.end, mediaInfo)
            || !one(mediaInfo, tag('s','t','b','l'), stbl) || !children(stbl.payload, stbl.end, sampleTable)
            || !one(sampleTable, tag('s','t','t','s'), stts)
            || !one(sampleTable, tag('s','t','s','z'), stsz)
            || !one(sampleTable, tag('s','t','s','c'), stsc)
            || !one(sampleTable, tag('s','t','c','o'), stco, false)
            || !one(sampleTable, tag('c','o','6','4'), co64, false)
            || !one(sampleTable, tag('c','t','t','s'), ctts, false)) return false;
        for (const auto &box : sampleTable)
            if (box.type == tag('c','s','l','g') || box.type == tag('s','t','z','2'))
                return fail("The MP4 contains unsupported video timing or sample tables.");
        int v;
        if (!version(stts, v, false) || !flagsZero(stts)) return false;
        if (stts.size() != 16 || u32(stts.payload + 4) != 1 || u32(stts.payload + 8) != 1
            || !u32(stts.payload + 12)) return fail("The MP4 video must contain exactly one timed sample.");
        if (ctts.type) {
            if (!version(ctts, v) || !flagsZero(ctts)) return false;
            if (ctts.size() != 16 || u32(ctts.payload + 4) != 1 || u32(ctts.payload + 8) != 1
                || u32(ctts.payload + 12) != 0)
                return fail("A video composition offset cannot be corrected safely.");
        }
        if (!version(stsz, v, false) || !flagsZero(stsz)) return false;
        if (stsz.size() < 12 || u32(stsz.payload + 8) != 1)
            return fail("The MP4 video must contain exactly one media sample.");
        const quint32 fixedSize = u32(stsz.payload + 4);
        if (stsz.size() != (fixedSize ? 12 : 16)) return fail("The MP4 sample-size table is malformed.");
        const quint32 sampleSize = fixedSize ? fixedSize : u32(stsz.payload + 12);
        if (!sampleSize) return fail("The MP4 video sample is empty.");
        if (!version(stsc, v, false) || !flagsZero(stsc)) return false;
        if (stsc.size() != 20 || u32(stsc.payload + 4) != 1 || u32(stsc.payload + 8) != 1
            || u32(stsc.payload + 12) != 1 || u32(stsc.payload + 16) != 1)
            return fail("The MP4 video chunk layout is unsupported.");
        if (bool(stco.type) == bool(co64.type)) return fail("The MP4 video chunk-offset table is missing or ambiguous.");
        const auto &offsets = stco.type ? stco : co64;
        if (!version(offsets, v, false) || !flagsZero(offsets)) return false;
        if (offsets.size() != (stco.type ? 12 : 16) || u32(offsets.payload + 4) != 1)
            return fail("The MP4 video must contain exactly one media chunk.");
        const quint64 offset = stco.type ? u32(offsets.payload + 8) : u64(offsets.payload + 8);
        bool contained = false;
        for (const auto &data : m_media)
            contained |= offset >= quint64(data.payload) && offset <= quint64(data.end)
                && sampleSize <= quint64(data.end) - offset;
        if (!contained) return fail("The MP4 video sample is outside its media data.");
        return patch(mediaHeader.timescale, m_videoScale) && patch(mediaHeader.duration, m_videoDuration)
            && patch({stts.payload + 12, 4}, m_videoDuration);
    }
    bool correctEdits(const Box &edts, bool video) {
        QVector<Box> edits;
        Box elst;
        if (!children(edts.payload, edts.end, edits) || !one(edits, tag('e','l','s','t'), elst)) return false;
        int v;
        if (!version(elst, v) || !flagsZero(elst)) return false;
        if (elst.size() < 8) return fail("The MP4 edit list is truncated.");
        const quint32 count = u32(elst.payload + 4);
        const qsizetype entrySize = v ? 20 : 12;
        if (!count || count > 1024 || quint64(elst.size() - 8) != quint64(count) * entrySize || (video && count != 1))
            return fail("The MP4 edit list is unsupported.");
        for (quint32 i = 0; i < count; ++i) {
            const qsizetype entry = elst.payload + 8 + qsizetype(i) * entrySize;
            const Field duration{entry, v ? 8 : 4};
            const Field mediaTime{entry + (v ? 8 : 4), v ? 8 : 4};
            const quint64 media = value(mediaTime);
            const auto signedLimit = v ? quint64(std::numeric_limits<qint64>::max()) : quint64(std::numeric_limits<qint32>::max());
            const auto empty = v ? std::numeric_limits<quint64>::max() : quint64(std::numeric_limits<quint32>::max());
            if ((video && media != 0) || (!video && media > signedLimit && media != empty)
                || u32(entry + (v ? 16 : 8)) != 0x00010000)
                return fail("The MP4 contains unsupported edit offsets or playback rates.");
            quint64 length = m_movieVideoDuration;
            if (!video && !rescale(value(duration), m_oldMovieScale, length)) return false;
            if (!patch(duration, length)) return false;
        }
        return true;
    }
    bool rescale(quint64 input, quint32 sourceScale, quint64 &output) {
        const quint64 seconds = input / sourceScale;
        const quint64 fractional = ((input % sourceScale) * movieTimescale + sourceScale / 2) / sourceScale;
        if (seconds > (std::numeric_limits<quint64>::max() - fractional) / movieTimescale)
            return fail("The MP4 duration is too large to rescale safely.");
        output = seconds * movieTimescale + fractional;
        return true;
    }
    bool patch(Field field, quint64 replacement) {
        if ((field.width != 4 && field.width != 8) || field.position < 0
            || field.position > m_bytes.size() - field.width
            || (field.width == 4 && replacement > std::numeric_limits<quint32>::max()))
            return fail("The corrected MP4 duration does not fit its header.");
        patches.append({field, replacement});
        return true;
    }
    const QByteArray &m_bytes;
    QVector<Box> m_media;
    int m_boxes = 0;
    quint32 m_oldMovieScale = 0, m_videoScale = 0, m_videoDuration = 0;
    quint64 m_movieVideoDuration = 0;
};
}

bool correctSingleFrameMp4Timing(QByteArray *bytes, double fps, QString *error) {
    if (error) error->clear();
    if (!bytes || !std::isfinite(fps) || fps < 1 || fps > 120) {
        if (error) *error = "Single-frame video timing needs a valid buffer and a frame rate from 1 to 120 FPS.";
        return false;
    }
    TimingCorrection correction(*bytes);
    if (!correction.prepare(fps)) {
        if (error) *error = correction.error;
        return false;
    }
    // Validation and overflow checks are complete. Changing only fixed-width
    // fields keeps every atom size, media byte and chunk offset unchanged.
    char *data = bytes->data();
    for (const auto &patch : correction.patches) {
        if (patch.field.width == 4) qToBigEndian(quint32(patch.value), data + patch.field.position);
        else qToBigEndian(patch.value, data + patch.field.position);
    }
    return true;
}
} // namespace toon
