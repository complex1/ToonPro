#include "audio_scrub.h"
#include "audio_mix.h"
#include "project.h"
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QBuffer>
#include <QElapsedTimer>
#include <QMediaDevices>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace toon {
struct AudioScrub::Private {
    Private(AudioScrub *owner, OutputMode output) : q(owner), mode(output) {
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, q, [this] { render(); });
    }
    AudioScrub *q;
    OutputMode mode;
    QVector<AudioClip> clips;
    double fps = 24;
    int frameCount = 0, pending = -1, rendered = -1;
    bool active = false, outputAttempted = false;
    QTimer timer;
    QElapsedTimer elapsed;
    std::unique_ptr<QAudioSink> sink;
    QBuffer buffer;
    void openOutput() {
        if (outputAttempted || mode == OutputMode::Silent) return;
        outputAttempted = true;
        const auto device = QMediaDevices::defaultAudioOutput();
        QAudioFormat format;
        format.setSampleRate(audioSampleRate); format.setChannelCount(audioChannels);
        format.setSampleFormat(QAudioFormat::Int16);
        if (device.isNull() || !device.isFormatSupported(format)) {
            emit q->warning("Audio scrubbing is unavailable on this output device."); return;
        }
        sink = std::make_unique<QAudioSink>(device, format);
        sink->setBufferFrameCount(1024);
    }
    void render() {
        if (!active || pending < 0) return;
        const int frame = pending; pending = -1;
        if (frame == rendered) return;
        rendered = frame; elapsed.restart();
        if (sink) sink->reset();
        buffer.close(); buffer.setData({});
        AudioMixStream stream(clips, fps, frame, 0, frameCount - 1, false);
        if (!stream.valid() || !stream.hasAudio()) return;
        // 80 ms is long enough to identify dialogue without letting old seeks
        // accumulate. A 2 ms edge ramp avoids clicks at grain boundaries.
        constexpr int frames = audioSampleRate * 80 / 1000, ramp = audioSampleRate * 2 / 1000;
        QByteArray pcm = stream.read(frames * audioBytesPerSampleFrame);
        const int count = pcm.size() / audioBytesPerSampleFrame;
        for (int i = 0; i < count; ++i) {
            const double envelope = std::min({1.0, double(i) / ramp, double(count - 1 - i) / ramp});
            for (int c = 0; c < audioChannels; ++c) {
                auto *address = pcm.data() + i * audioBytesPerSampleFrame + c * sizeof(qint16);
                qint16 sample; std::memcpy(&sample, address, sizeof(sample));
                sample = qint16(std::lround(sample * envelope));
                std::memcpy(address, &sample, sizeof(sample));
            }
        }
        openOutput();
        if (sink) {
            sink->reset(); buffer.close(); buffer.setData(pcm); buffer.open(QIODevice::ReadOnly);
            sink->start(&buffer);
            if (sink->error() != QtAudio::NoError || sink->isNull()) {
                sink->reset(); sink.reset(); buffer.close();
                emit q->warning("Audio scrubbing output stopped.");
            }
        }
        emit q->grainReady(frame, pcm);
    }
};
AudioScrub::AudioScrub(QObject *parent, OutputMode mode)
    : QObject(parent), d(std::make_unique<Private>(this, mode)) {}
AudioScrub::~AudioScrub() { end(); }
bool AudioScrub::active() const { return d->active; }
void AudioScrub::begin(const Shot &shot, double fps) {
    end();
    if (shot.durationFrames <= 0 || !std::isfinite(fps) || fps < 1 || fps > 120) return;
    d->clips = shot.audioClips; d->fps = fps; d->frameCount = shot.durationFrames; d->active = true;
}
void AudioScrub::seek(int frame) {
    if (!d->active || frame < 0 || frame >= d->frameCount) return;
    d->pending = frame;
    if (!d->elapsed.isValid() || d->elapsed.elapsed() >= 30) { d->timer.stop(); d->render(); }
    else if (!d->timer.isActive()) d->timer.start(int(30 - d->elapsed.elapsed()));
}
void AudioScrub::end() {
    d->active = false; d->timer.stop(); d->pending = d->rendered = -1; d->elapsed.invalidate();
    if (d->sink) { d->sink->reset(); d->sink.reset(); }
    d->buffer.close(); d->buffer.setData({}); d->clips.clear(); d->outputAttempted = false;
}
}
