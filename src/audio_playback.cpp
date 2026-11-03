#include "audio_playback.h"
#include "audio_mix.h"
#include "project.h"
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QElapsedTimer>
#include <QMediaDevices>
#include <QTimer>
#include <algorithm>
#include <functional>

namespace toon {
struct AudioPlayback::Private {
    explicit Private(AudioPlayback *owner, OutputMode outputMode) : q(owner), mode(outputMode) {
        watchdog.setInterval(100);
        QObject::connect(&watchdog, &QTimer::timeout, q, [this] { checkOutput(); });
    }
    AudioPlayback *q;
    OutputMode mode;
    std::unique_ptr<AudioMixStream> stream;
    std::unique_ptr<QAudioSink> sink;
    std::unique_ptr<QMediaDevices> devices;
    QAudioDevice device;
    QAudioFormat format;
    QTimer watchdog;
    QElapsedTimer wall;
    bool active = false, audioClock = false, warned = false;
    quint64 generation = 0;
    mutable qint64 lastElapsed = 0;
    qint64 lastProcessed = 0, lastProgressWall = 0;

    qint64 elapsed() const {
        if (!active) return 0;
        const qint64 value = audioClock && sink ? sink->processedUSecs() : wall.nsecsElapsed() / 1000;
        lastElapsed = std::max(lastElapsed, value);
        return lastElapsed;
    }
    void startOutput(const std::function<std::unique_ptr<AudioMixStream>(AudioMixFormat)> &makeStream);
    void releaseOutput() {
        watchdog.stop();
        if (sink) {
            QObject::disconnect(sink.get(), nullptr, q, nullptr);
            // stop() drains synchronously on Darwin/Linux. Discard the queued
            // samples so pause, seek and error recovery remain immediate.
            sink->reset();
            sink.reset();
        }
        stream.reset();
        devices.reset();
        device = {};
    }
    void fallback(const QString &reason) {
        if (!active) return;
        elapsed();
        audioClock = false;
        releaseOutput();
        if (!warned) {
            warned = true;
            emit q->warning(reason + QStringLiteral(" Playback continues without audio."));
        }
    }
    void checkOutput() {
        if (!active || !audioClock || !sink) return;
        if (sink->error() != QtAudio::NoError || sink->state() == QtAudio::StoppedState
            || sink->state() == QtAudio::SuspendedState) {
            fallback(QStringLiteral("Audio output stopped."));
            return;
        }
        const auto processed = sink->processedUSecs(), now = wall.nsecsElapsed() / 1000;
        if (processed > lastProcessed) {
            lastProcessed = processed;
            lastProgressWall = now;
        } else if (now - lastProgressWall >= 750000) {
            fallback(QStringLiteral("Audio output is not responding."));
        }
    }
};

AudioPlayback::AudioPlayback(QObject *parent, OutputMode mode)
    : QObject(parent), d(std::make_unique<Private>(this, mode)) {}
AudioPlayback::~AudioPlayback() { stop(); }
bool AudioPlayback::active() const { return d->active; }
bool AudioPlayback::usingAudioClock() const { return d->active && d->audioClock; }
qint64 AudioPlayback::elapsedUSecs() const { return d->elapsed(); }

void AudioPlayback::stop() {
    ++d->generation;
    d->active = false;
    d->audioClock = false;
    d->releaseOutput();
    d->lastElapsed = 0;
    d->wall.invalidate();
}

void AudioPlayback::start(const Shot &shot, double fps, int startFrame, int rangeFirst, int rangeLast, bool loop) {
    stop();
    d->warned = false;
    if (shot.durationFrames <= 0) return;
    rangeFirst = std::clamp(rangeFirst, 0, shot.durationFrames - 1);
    rangeLast = std::clamp(rangeLast, rangeFirst, shot.durationFrames - 1);
    d->startOutput([&](AudioMixFormat format) {
        return std::make_unique<AudioMixStream>(shot.audioClips, fps, startFrame, rangeFirst, rangeLast, loop, format);
    });
}

void AudioPlayback::start(const Project &project, const QVector<MoviePlaybackSegment> &segments,
                          int startFrame, int rangeFirst, int rangeLast, bool loop) {
    stop();
    d->warned = false;
    const int duration = moviePlaybackDuration(segments);
    if (!duration) return;
    rangeFirst = std::clamp(rangeFirst, 0, duration - 1);
    rangeLast = std::clamp(rangeLast, rangeFirst, duration - 1);
    d->startOutput([&](AudioMixFormat format) {
        return std::make_unique<AudioMixStream>(project, segments, startFrame, rangeFirst, rangeLast, loop, format);
    });
}

void AudioPlayback::Private::startOutput(const std::function<std::unique_ptr<AudioMixStream>(AudioMixFormat)> &makeStream) {
    stream = makeStream(AudioMixFormat::Signed16);
    if (!stream->valid()) { stream.reset(); return; }
    wall.start();
    active = true;
    if (!stream->hasAudio() || mode == AudioPlayback::OutputMode::Silent) {
        stream.reset();
        return;
    }

    devices = std::make_unique<QMediaDevices>();
    device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) { fallback(QStringLiteral("No audio output device is available.")); return; }
    format.setSampleRate(audioSampleRate);
    format.setChannelCount(audioChannels);
    format.setSampleFormat(QAudioFormat::Int16);
    if (!device.isFormatSupported(format)) {
        format.setSampleFormat(QAudioFormat::Float);
        if (!device.isFormatSupported(format)) {
            fallback(QStringLiteral("The audio output does not support 48 kHz stereo playback."));
            return;
        }
        stream = makeStream(AudioMixFormat::Float32);
    }
    const auto startedGeneration = generation;
    QObject::connect(devices.get(), &QMediaDevices::audioOutputsChanged, q, [this, startedGeneration] {
        if (!active || !audioClock || generation != startedGeneration) return;
        const auto current = QMediaDevices::defaultAudioOutput();
        if (current.isNull() || current.id() != device.id() || !current.isFormatSupported(format))
            fallback(QStringLiteral("The audio output device changed."));
    }, Qt::QueuedConnection);
    sink = std::make_unique<QAudioSink>(device, format);
    const auto *startedSink = sink.get();
    QObject::connect(sink.get(), &QAudioSink::stateChanged, q, [this, startedSink, startedGeneration](QtAudio::State state) {
        if (!active || !audioClock || sink.get() != startedSink || generation != startedGeneration) return;
        if (sink->error() != QtAudio::NoError || state == QtAudio::StoppedState
            || state == QtAudio::SuspendedState)
            fallback(QStringLiteral("Audio output stopped."));
        // Startup idle is allowed briefly; the watchdog catches an idle stream
        // or an apparently active device whose processed clock no longer moves.
    }, Qt::QueuedConnection);
    sink->setBufferFrameCount(2048);
    lastProcessed = 0;
    lastProgressWall = wall.nsecsElapsed() / 1000;
    audioClock = true;
    sink->start(stream.get());
    if (sink->isNull() || sink->error() != QtAudio::NoError
        || sink->state() == QtAudio::StoppedState) {
        fallback(QStringLiteral("Audio output could not be started."));
        return;
    }
    watchdog.start();
}
} // namespace toon
