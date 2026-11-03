#include "mp4_timing.h"
#include <QMap>
#include <QtEndian>
#include <QtTest>
#include <functional>
#include <limits>

using namespace toon;
namespace {
// Independent ISO-BMFF fixtures. Offsets are recorded while writing each field;
// assertions do not reuse the finalizer's atom traversal or duration calculation.
struct Bytes {
    QByteArray data;
    QMap<QString, int> fields;
    void raw(QByteArray value) { data += value; }
    void zero(int count) { data += QByteArray(count, '\0'); }
    void u16(quint16 value) { char bytes[2]; qToBigEndian(value, bytes); data.append(bytes, 2); }
    void u32(const QString &name, quint32 value) {
        if (!name.isEmpty()) fields.insert(name, data.size());
        char bytes[4]; qToBigEndian(value, bytes); data.append(bytes, 4);
    }
    void u64(const QString &name, quint64 value) {
        if (!name.isEmpty()) fields.insert(name, data.size());
        char bytes[8]; qToBigEndian(value, bytes); data.append(bytes, 8);
    }
    void append(const Bytes &value) {
        const int offset = data.size();
        for (auto i = value.fields.cbegin(); i != value.fields.cend(); ++i) fields.insert(i.key(), offset + i.value());
        data += value.data;
    }
};
Bytes atom(const char *type, const Bytes &payload, const QString &name, bool extended = false) {
    Bytes result; result.fields.insert(name + ".box", 0);
    result.u32({}, extended ? 1 : quint32(payload.data.size() + 8)); result.raw(QByteArray(type, 4));
    if (extended) result.u64({}, quint64(payload.data.size() + 16));
    result.append(payload); return result;
}
Bytes atom(const char *type, const QByteArray &payload, const QString &name) {
    Bytes data; data.raw(payload); return atom(type, data, name);
}
void fullHeader(Bytes &data, const QString &name, bool version1, quint32 flags = 0) {
    data.u32(name + ".flags", (version1 ? 0x01000000u : 0) | flags);
}
void clockFields(Bytes &data, const QString &name, bool version1, quint32 timescale, quint64 duration) {
    fullHeader(data, name, version1);
    if (version1) { data.u64({}, 100); data.u64({}, 101); }
    else { data.u32({}, 100); data.u32({}, 101); }
    data.u32(name + ".timescale", timescale);
    if (version1) data.u64(name + ".duration", duration); else data.u32(name + ".duration", quint32(duration));
}
Bytes movieHeader(bool version1, quint64 duration) {
    Bytes b; clockFields(b, "movie", version1, 1000, duration);
    b.u32({}, 0x00010000); b.u16(0x0100); b.zero(10); b.zero(36); b.zero(24); b.u32({}, 3);
    return atom("mvhd", b, "movie");
}
Bytes trackHeader(const QString &prefix, bool version1, quint64 duration, int id) {
    Bytes b; const auto key = prefix + ".track"; fullHeader(b, key, version1, 3);
    if (version1) { b.u64({}, 100); b.u64({}, 101); } else { b.u32({}, 100); b.u32({}, 101); }
    b.u32({}, id); b.u32({}, 0);
    if (version1) b.u64(key + ".duration", duration); else b.u32(key + ".duration", quint32(duration));
    b.zero(8); b.u16(0); b.u16(0); b.u16(prefix == "audio" ? 0x0100 : 0); b.u16(0); b.zero(36);
    b.u32({}, 160 << 16); b.u32({}, 90 << 16); return atom("tkhd", b, key);
}
Bytes mediaHeader(const QString &prefix, bool version1, quint32 timescale, quint64 duration) {
    Bytes b; const auto key = prefix + ".media"; clockFields(b, key, version1, timescale, duration);
    b.u16(0x55c4); b.u16(0); return atom("mdhd", b, key);
}
Bytes editList(const QString &prefix, bool version1, bool audio) {
    const auto key = prefix + ".edit"; Bytes b; fullHeader(b, key, version1); b.u32(key + ".count", audio ? 2 : 1);
    for (int i = 0; i < (audio ? 2 : 1); ++i) {
        const auto entry = key + QString::number(i);
        const quint64 duration = audio ? (i == 0 ? 500 : 1500) : 1000;
        const qint64 mediaTime = audio ? (i == 0 ? -1 : 1024) : 0;
        if (version1) { b.u64(entry + ".duration", duration); b.u64(entry + ".mediaTime", quint64(mediaTime)); }
        else { b.u32(entry + ".duration", quint32(duration)); b.u32(entry + ".mediaTime", quint32(mediaTime)); }
        b.u32(entry + ".rate", 0x00010000);
    }
    Bytes container; container.append(atom("elst", b, key)); return atom("edts", container, prefix + ".edits");
}
struct Options {
    bool version1 = false, withAudio = false, extended = false, co64 = false;
    bool withEdits = true, ctts = false, cttsVersion1 = false, variableSizes = false;
    bool duplicateStts = false, secondVideo = false;
};
Bytes track(const QString &prefix, const Options &options, bool audio = false) {
    const auto p = prefix + '.';
    Bytes body; body.append(trackHeader(prefix, options.version1, audio ? 2000 : 1000, audio ? 2 : 1));
    if (options.withEdits) body.append(editList(prefix, options.version1, audio));
    Bytes media; media.append(mediaHeader(prefix, options.version1, audio ? 48000 : 15360, audio ? 96000 : 15360));
    Bytes handler; fullHeader(handler, p + "handler", false); handler.u32({}, 0);
    handler.fields.insert(p + "handler.type", handler.data.size()); handler.raw(audio ? "soun" : "vide"); handler.zero(12); handler.raw("Fixture\0");
    media.append(atom("hdlr", handler, p + "handler"));
    Bytes table;
    Bytes stts; fullHeader(stts, p + "time", false); stts.u32(p + "time.entries", 1);
    stts.u32(p + "time.samples", audio ? 2 : 1); stts.u32(p + "time.delta", audio ? 48000 : 15360);
    table.append(atom("stts", stts, p + "time"));
    if (!audio && options.duplicateStts) table.append(atom("stts", stts, p + "duplicateTime"));
    if (!audio && options.ctts) {
        Bytes ctts; fullHeader(ctts, p + "composition", options.cttsVersion1); ctts.u32(p + "composition.entries", 1);
        ctts.u32(p + "composition.samples", 1); ctts.u32(p + "composition.offset", 0);
        table.append(atom("ctts", ctts, p + "composition"));
    }
    Bytes sizes; fullHeader(sizes, p + "sizes", false);
    sizes.u32(p + "sizes.constant", options.variableSizes ? 0 : 16); sizes.u32(p + "sizes.count", audio ? 2 : 1);
    if (options.variableSizes) { sizes.u32(p + "sizes.sample", 16); if (audio) sizes.u32(p + "sizes.sample2", 16); }
    table.append(atom("stsz", sizes, p + "sizes"));
    Bytes chunks; fullHeader(chunks, p + "mapping", false); chunks.u32(p + "mapping.entries", 1);
    chunks.u32(p + "mapping.first", 1); chunks.u32(p + "mapping.samples", audio ? 2 : 1); chunks.u32(p + "mapping.description", 1);
    table.append(atom("stsc", chunks, p + "mapping"));
    Bytes offsets; fullHeader(offsets, p + "chunks", false); offsets.u32(p + "chunks.entries", 1);
    if (options.co64) offsets.u64(p + "chunks.offset", 0); else offsets.u32(p + "chunks.offset", 0);
    table.append(atom(options.co64 ? "co64" : "stco", offsets, p + "chunks"));
    Bytes info; info.append(atom("stbl", table, p + "table")); media.append(atom("minf", info, p + "info"));
    body.append(atom("mdia", media, p + "mdia")); return atom("trak", body, p + "trak");
}
void put32(QByteArray &data, int offset, quint32 value) { qToBigEndian(value, data.data() + offset); }
void put64(QByteArray &data, int offset, quint64 value) { qToBigEndian(value, data.data() + offset); }
quint32 get32(const QByteArray &data, int offset) { return qFromBigEndian<quint32>(data.constData() + offset); }
quint64 get64(const QByteArray &data, int offset) { return qFromBigEndian<quint64>(data.constData() + offset); }
Bytes fixture(Options options = {}) {
    Bytes result; Bytes type; type.raw("isom"); type.u32({}, 512); type.raw("isomiso2avc1mp41");
    result.append(atom("ftyp", type, "type")); result.append(atom("free", QByteArray("preserved-free"), "padding"));
    QByteArray payload(96, '\0');
    for (int i = 0; i < payload.size(); ++i) payload[i] = char((i * 73 + 19) & 255);
    payload.replace(24, 4, "moov"); // Box-looking bytes inside media must not be parsed.
    result.append(atom("mdat", payload, "mediaData"));
    Bytes movie; movie.append(movieHeader(options.version1, options.withAudio ? 2000 : 1000));
    movie.append(track("video", options));
    if (options.withAudio) movie.append(track("audio", options, true));
    if (options.secondVideo) movie.append(track("otherVideo", options));
    movie.append(atom("udta", QByteArray("unrelated metadata"), "userData"));
    result.append(atom("moov", movie, "movieContainer", options.extended));
    const quint64 start = result.fields["mediaData.box"] + 8;
    for (const auto &prefix : {QString("video"), QString("audio"), QString("otherVideo")}) {
        if (!result.fields.contains(prefix + ".chunks.offset")) continue;
        const quint64 offset = start + (prefix == "audio" ? 16 : 0);
        if (options.co64) put64(result.data, result.fields[prefix + ".chunks.offset"], offset);
        else put32(result.data, result.fields[prefix + ".chunks.offset"], quint32(offset));
    }
    return result;
}
void putDuration(QByteArray &bytes, const Bytes &source, const QString &key, quint64 value, bool version1) {
    if (version1) put64(bytes, source.fields[key], value); else put32(bytes, source.fields[key], quint32(value));
}
QByteArray expected(const Bytes &source, const Options &options, quint32 videoScale, quint32 videoTicks, quint64 movieTicks) {
    auto bytes = source.data;
    put32(bytes, source.fields["movie.timescale"], 1000000);
    putDuration(bytes, source, "movie.duration", options.withAudio ? qMax<quint64>(movieTicks, 2000000) : movieTicks, options.version1);
    put32(bytes, source.fields["video.media.timescale"], videoScale);
    putDuration(bytes, source, "video.media.duration", videoTicks, options.version1);
    put32(bytes, source.fields["video.time.delta"], videoTicks);
    putDuration(bytes, source, "video.track.duration", movieTicks, options.version1);
    if (options.withEdits) putDuration(bytes, source, "video.edit0.duration", movieTicks, options.version1);
    if (options.withAudio) {
        putDuration(bytes, source, "audio.track.duration", 2000000, options.version1);
        if (options.withEdits) {
            putDuration(bytes, source, "audio.edit0.duration", 500000, options.version1);
            putDuration(bytes, source, "audio.edit1.duration", 1500000, options.version1);
        }
    }
    return bytes;
}
}

class Mp4TimingTests final : public QObject {
    Q_OBJECT
private slots:
    void rationalDurationsAndSupportedVersions_data() {
        QTest::addColumn<double>("fps"); QTest::addColumn<quint32>("scale"); QTest::addColumn<quint32>("ticks");
        QTest::addColumn<quint64>("movieTicks"); QTest::addColumn<bool>("version1");
        struct Case { const char *name; double fps; quint32 scale, ticks; quint64 movie; };
        const Case cases[] = {{"1.5", 1.5, 3, 2, 666667}, {"29.97", 29.97, 2997, 100, 33367},
            {"23.976", 23.976, 2997, 125, 41708}, {"59.94", 59.94, 2997, 50, 16683},
            {"1", 1, 1, 1, 1000000}, {"120", 120, 120, 1, 8333}};
        for (bool v1 : {false, true}) for (const auto &entry : cases)
            QTest::newRow(qPrintable(QString("%1-v%2").arg(entry.name).arg(v1 ? 1 : 0)))
                << entry.fps << entry.scale << entry.ticks << entry.movie << v1;
    }
    void rationalDurationsAndSupportedVersions() {
        QFETCH(double, fps); QFETCH(quint32, scale); QFETCH(quint32, ticks); QFETCH(quint64, movieTicks); QFETCH(bool, version1);
        Options options; options.version1 = version1; options.variableSizes = version1;
        const auto source = fixture(options); auto bytes = source.data; QString error = "stale error";
        QVERIFY2(correctSingleFrameMp4Timing(&bytes, fps, &error), qPrintable(error)); QVERIFY(error.isEmpty());
        QCOMPARE(bytes, expected(source, options, scale, ticks, movieTicks));
        QCOMPARE(get32(bytes, source.fields["video.media.timescale"]), scale);
        QCOMPARE(get32(bytes, source.fields["video.time.delta"]), ticks);
        const auto duration = version1 ? get64(bytes, source.fields["video.track.duration"]) : get32(bytes, source.fields["video.track.duration"]);
        QCOMPARE(duration, movieTicks); QCOMPARE(bytes.size(), source.data.size());
    }
    void audioSamplesOffsetsAndUnrelatedBoxesStayUnchanged_data() {
        QTest::addColumn<bool>("version1"); QTest::newRow("v0") << false; QTest::newRow("v1") << true;
    }
    void audioSamplesOffsetsAndUnrelatedBoxesStayUnchanged() {
        QFETCH(bool, version1); Options options; options.withAudio = true; options.version1 = version1; options.co64 = true;
        options.extended = true; options.variableSizes = true; options.ctts = true; options.cttsVersion1 = version1;
        const auto source = fixture(options); auto bytes = source.data; QString error;
        QVERIFY2(correctSingleFrameMp4Timing(&bytes, 1.5, &error), qPrintable(error));
        QCOMPARE(bytes, expected(source, options, 3, 2, 666667));
        const int mdat = source.fields["mediaData.box"];
        QCOMPARE(bytes.mid(mdat, get32(source.data, mdat)), source.data.mid(mdat, get32(source.data, mdat)));
        QCOMPARE(get64(bytes, source.fields["video.chunks.offset"]), get64(source.data, source.fields["video.chunks.offset"]));
        QCOMPARE(get64(bytes, source.fields["audio.chunks.offset"]), get64(source.data, source.fields["audio.chunks.offset"]));
        const int audioHeader = source.fields["audio.media.box"];
        QCOMPARE(bytes.mid(audioHeader, get32(source.data, audioHeader)), source.data.mid(audioHeader, get32(source.data, audioHeader)));
        QCOMPARE(get32(bytes, source.fields["audio.time.delta"]), quint32(48000));
        if (version1) {
            QCOMPARE(get64(bytes, source.fields["audio.edit0.mediaTime"]), std::numeric_limits<quint64>::max());
            QCOMPARE(get64(bytes, source.fields["audio.edit1.mediaTime"]), quint64(1024));
        } else {
            QCOMPARE(get32(bytes, source.fields["audio.edit0.mediaTime"]), std::numeric_limits<quint32>::max());
            QCOMPARE(get32(bytes, source.fields["audio.edit1.mediaTime"]), quint32(1024));
        }
    }
    void optionalEditsAndZeroCompositionOffsets() {
        for (bool version1 : {false, true}) {
            Options options; options.withEdits = false; options.ctts = true; options.cttsVersion1 = version1;
            const auto source = fixture(options); auto bytes = source.data; QString error;
            QVERIFY2(correctSingleFrameMp4Timing(&bytes, 29.97, &error), qPrintable(error));
            QCOMPARE(bytes, expected(source, options, 2997, 100, 33367));
            const auto once = bytes;
            QVERIFY2(correctSingleFrameMp4Timing(&bytes, 29.97, &error), qPrintable(error)); QCOMPARE(bytes, once);
        }
    }
    void invalidFpsIsRejectedWithoutMutation_data() {
        QTest::addColumn<double>("fps");
        QTest::newRow("zero") << 0.0; QTest::newRow("negative") << -1.5;
        QTest::newRow("below-supported") << .5; QTest::newRow("above-supported") << 120.001;
        QTest::newRow("nan") << std::numeric_limits<double>::quiet_NaN();
        QTest::newRow("infinity") << std::numeric_limits<double>::infinity();
        QTest::newRow("negative-infinity") << -std::numeric_limits<double>::infinity();
    }
    void invalidFpsIsRejectedWithoutMutation() {
        QFETCH(double, fps); const auto source = fixture(); auto bytes = source.data; QString error;
        QVERIFY(!correctSingleFrameMp4Timing(&bytes, fps, &error)); QVERIFY(!error.isEmpty()); QCOMPARE(bytes, source.data);
    }
    void malformedAndUnsupportedInputsAreTransactional_data() {
        QTest::addColumn<QByteArray>("bytes");
        const auto source = fixture();
        auto mutation = [&](const char *name, const std::function<void(QByteArray &)> &edit) { auto bytes = source.data; edit(bytes); QTest::newRow(name) << bytes; };
        auto field = [&](const char *name, const QString &key, quint32 value) { mutation(name, [&](QByteArray &b) { put32(b, source.fields[key], value); }); };
        QTest::newRow("empty") << QByteArray(); QTest::newRow("short-header") << QByteArray("\0\0\0", 3);
        mutation("truncated-container", [](QByteArray &b) { b.chop(1); });
        mutation("partial-trailing-box", [](QByteArray &b) { b += QByteArray("trailer", 7); });
        mutation("box-underflow", [](QByteArray &b) { put32(b, 0, 7); });
        mutation("box-overflow", [](QByteArray &b) { put32(b, 0, std::numeric_limits<quint32>::max()); });
        field("nested-overrun", "video.mdia.box", quint32(source.data.size()));
        field("zero-size-box", "video.time.box", 0);
        field("movie-zero-timescale", "movie.timescale", 0);
        field("media-zero-timescale", "video.media.timescale", 0);
        field("unsupported-mvhd-version", "movie.flags", 0x02000000);
        field("unsupported-mdhd-version", "video.media.flags", 0x02000000);
        field("unsupported-tkhd-version", "video.track.flags", 0x02000003);
        field("unsupported-elst-version", "video.edit.flags", 0x02000000);
        field("stts-version-one", "video.time.flags", 0x01000000);
        field("multiple-timing-entries", "video.time.entries", 2);
        field("multiple-video-samples", "video.time.samples", 2);
        field("stsz-multiple-samples", "video.sizes.count", 2);
        field("zero-video-sample-size", "video.sizes.constant", 0);
        field("sample-crosses-mdat", "video.sizes.constant", 200);
        field("multiple-chunk-entries", "video.chunks.entries", 2);
        field("chunk-points-outside-media", "video.chunks.offset", 8);
        field("chunk-points-past-eof", "video.chunks.offset", quint32(source.data.size() + 1));
        field("multiple-stsc-entries", "video.mapping.entries", 2);
        field("wrong-first-chunk", "video.mapping.first", 2);
        field("multiple-samples-per-chunk", "video.mapping.samples", 2);
        field("unsupported-description", "video.mapping.description", 2);
        field("multiple-video-edits", "video.edit.count", 2);
        field("video-edit-media-offset", "video.edit0.mediaTime", 1);
        field("video-edit-rate", "video.edit0.rate", 0x00008000);
        mutation("missing-video-stts", [&](QByteArray &b) { b.replace(source.fields["video.time.box"] + 4, 4, "free"); });
        mutation("missing-video-stsz", [&](QByteArray &b) { b.replace(source.fields["video.sizes.box"] + 4, 4, "free"); });
        mutation("no-video-handler", [&](QByteArray &b) { b.replace(source.fields["video.handler.type"], 4, "soun"); });
        mutation("fragmented-movie", [](QByteArray &b) { b += atom("moof", QByteArray(), "fragment").data; });
        mutation("segment-index", [](QByteArray &b) { b += atom("sidx", QByteArray(24, '\0'), "index").data; });
        Options duplicate; duplicate.duplicateStts = true; QTest::newRow("duplicate-stts") << fixture(duplicate).data;
        Options multiple; multiple.secondVideo = true; QTest::newRow("multiple-video-tracks") << fixture(multiple).data;
        Options composition; composition.ctts = true;
        for (bool signedVersion : {false, true}) {
            composition.cttsVersion1 = signedVersion; auto bad = fixture(composition);
            put32(bad.data, bad.fields["video.composition.offset"], signedVersion ? 0xffffffff : 1);
            QTest::newRow(signedVersion ? "negative-composition-offset" : "positive-composition-offset") << bad.data;
        }
        Options wide; wide.co64 = true; wide.extended = true; auto extended = fixture(wide);
        put64(extended.data, extended.fields["movieContainer.box"] + 8, std::numeric_limits<quint64>::max());
        QTest::newRow("extended-box-overflow") << extended.data;
        auto wideOffset = fixture(wide); put64(wideOffset.data, wideOffset.fields["video.chunks.offset"], std::numeric_limits<quint64>::max());
        QTest::newRow("co64-offset-overflow") << wideOffset.data;
        for (bool v1 : {false, true}) {
            Options audio; audio.withAudio = true; audio.version1 = v1; auto overflow = fixture(audio);
            if (v1) put64(overflow.data, overflow.fields["audio.track.duration"], std::numeric_limits<quint64>::max());
            else put32(overflow.data, overflow.fields["audio.track.duration"], 5000000);
            QTest::newRow(v1 ? "audio-v1-rescale-overflow" : "audio-v0-rescale-overflow") << overflow.data;
        }
    }
    void malformedAndUnsupportedInputsAreTransactional() {
        QFETCH(QByteArray, bytes); const auto original = bytes; QString error;
        QVERIFY(!correctSingleFrameMp4Timing(&bytes, 1.5, &error)); QVERIFY(!error.isEmpty()); QCOMPARE(bytes, original);
    }
    void metadataSizeLimit() {
        constexpr int maximumMetadata = 1024 * 1024;
        for (int excess : {0, 1}) {
            auto source = fixture();
            const int box = source.fields["movieContainer.box"];
            const int extra = maximumMetadata + excess - int(get32(source.data, box));
            // The moov is last, so adding an explicitly sized free child gives a
            // valid container exactly at, or one byte beyond, the supported cap.
            source.data += atom("free", QByteArray(extra - 8, '\0'), "metadataPadding").data;
            put32(source.data, box, maximumMetadata + excess);
            auto bytes = source.data; QString error;
            if (excess == 0) {
                QVERIFY2(correctSingleFrameMp4Timing(&bytes, 1.5, &error), qPrintable(error));
                QCOMPARE(bytes, expected(source, {}, 3, 2, 666667));
            } else {
                QVERIFY(!correctSingleFrameMp4Timing(&bytes, 1.5, &error));
                QVERIFY(!error.isEmpty()); QCOMPARE(bytes, source.data);
            }
        }
    }
    void fileSizeLimit() {
        constexpr qsizetype maximumFile = 80 * 1024 * 1024;
        auto bytes = fixture().data;
        const auto paddingLength = maximumFile + 1 - bytes.size();
        const auto paddingStart = bytes.size();
        bytes.resize(maximumFile + 1, '\0');
        // A valid, opaque top-level padding box makes the file oversized without
        // relying on malformed bytes to trigger the rejection.
        put32(bytes, paddingStart, quint32(paddingLength));
        bytes.replace(paddingStart + 4, 4, "free");
        const auto original = bytes; QString error;
        QVERIFY(!correctSingleFrameMp4Timing(&bytes, 1.5, &error));
        QVERIFY(!error.isEmpty()); QCOMPARE(bytes, original);
    }
    void excessiveBoxCountIsRejected() {
        auto bytes = fixture().data;
        const auto emptyBox = atom("free", QByteArray(), "padding").data;
        for (int i = 0; i < 4097; ++i) bytes += emptyBox;
        const auto original = bytes; QString error;
        QVERIFY(!correctSingleFrameMp4Timing(&bytes, 1.5, &error));
        QVERIFY(!error.isEmpty()); QCOMPARE(bytes, original);
    }
    void everyTruncatedPrefixIsRejectedWithoutMutation() {
        Options options; options.version1 = true; options.withAudio = true;
        options.extended = true; options.co64 = true; options.ctts = true;
        options.cttsVersion1 = true; options.variableSizes = true;
        const auto source = fixture(options);
        for (int size = 0; size < source.data.size(); ++size) {
            auto bytes = source.data.left(size); const auto original = bytes; QString error;
            const auto context = QString("Accepted a truncated prefix of %1 bytes").arg(size);
            QVERIFY2(!correctSingleFrameMp4Timing(&bytes, 1.5, &error), qPrintable(context));
            QVERIFY(!error.isEmpty()); QCOMPARE(bytes, original);
        }
    }
    void nullOutputIsRejected() {
        QString error; QVERIFY(!correctSingleFrameMp4Timing(nullptr, 1.5, &error)); QVERIFY(!error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(Mp4TimingTests)
#include "mp4_timing_tests.moc"
