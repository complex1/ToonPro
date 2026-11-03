#include "audio_import.h"
#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QUrl>
#include <QtConcurrent>
#include <QtEndian>

namespace toon {
AudioImport::AudioImport(QObject *parent) : QObject(parent) {
    m_timeout.setSingleShot(true); m_timeout.setInterval(30000);
    connect(&m_timeout, &QTimer::timeout, this, [this] { fail("Audio decoding stopped responding. Try another audio file."); });
}
AudioImport::~AudioImport() { cancel(); }
void AudioImport::releaseDecoder() {
    m_timeout.stop();
    if (m_decoder) {
        disconnect(m_decoder, nullptr, this, nullptr);
        m_decoder->stop(); m_decoder->deleteLater(); m_decoder = nullptr;
    }
}
void AudioImport::cancel() {
    ++m_generation; releaseDecoder(); m_pcm.clear();
    if (m_busy) { m_busy = false; emit busyChanged(); }
}
void AudioImport::fail(const QString &reason) {
    if (!m_busy) return;
    cancel(); emit failed(reason);
}
bool AudioImport::start(const QString &path) {
    if (m_busy) return false;
    const QFileInfo file(path);
    if (!file.isFile() || !file.isReadable() || file.size() <= 0 || file.size() > maxAudioAssetBytes) {
        emit failed("Choose a readable audio file of up to 128 MiB."); return false;
    }
    const auto generation = ++m_generation; m_pcm.clear(); m_name = file.completeBaseName().left(120);
    if (m_name.trimmed().isEmpty()) m_name = "Audio";
    m_busy = true; emit busyChanged(); emit progress(0);
    if (!m_busy || m_generation != generation) return false;
    auto *decoder = new QAudioDecoder(this); m_decoder = decoder;
    if (!decoder->isSupported()) { fail("The audio decoder is unavailable on this system."); return false; }
    QAudioFormat format; format.setSampleRate(audioSampleRate); format.setChannelCount(audioChannels); format.setSampleFormat(QAudioFormat::Int16);
    decoder->setAudioFormat(format);
    connect(decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this, [this, decoder](QAudioDecoder::Error error) {
        if (m_decoder != decoder || !m_busy) return;
        if (error != QAudioDecoder::NoError) fail("Could not import audio: " + decoder->errorString());
    });
    connect(decoder, &QAudioDecoder::durationChanged, this, [this](qint64 durationMs) {
        if (durationMs > maxAudioAssetBytes * 1000 / (audioSampleRate * audioBytesPerSampleFrame))
            fail("This audio exceeds the 128 MiB decoded limit. Trim it before importing.");
    });
    connect(decoder, &QAudioDecoder::bufferReady, this, [this, decoder] {
        if (m_decoder != decoder || !m_busy) return;
        const auto buffer = decoder->read();
        if (!buffer.isValid()) return;
        const auto format = buffer.format();
        if (format.sampleRate() != audioSampleRate || format.channelCount() != audioChannels || format.sampleFormat() != QAudioFormat::Int16) {
            fail("The decoder could not convert this file to 48 kHz stereo audio."); return;
        }
        if (buffer.byteCount() > maxAudioAssetBytes - m_pcm.size()) {
            fail("This audio exceeds the 128 MiB decoded limit. Trim it before importing."); return;
        }
        m_pcm.append(buffer.constData<char>(), buffer.byteCount()); m_timeout.start();
        const qint64 duration = decoder->duration();
        emit progress(duration > 0 ? int(qBound(qint64(0), decoder->position() * 95 / duration, qint64(95))) : -1);
    });
    connect(decoder, &QAudioDecoder::finished, this, &AudioImport::finishDecode);
    decoder->setSource(QUrl::fromLocalFile(file.absoluteFilePath()));
    if (!m_busy || m_decoder != decoder) return false;
    m_timeout.start(); decoder->start(); return true;
}
void AudioImport::finishDecode() {
    if (!m_busy || !m_decoder) return;
    const auto generation = m_generation;
    releaseDecoder();
    if (m_pcm.isEmpty()) { fail("This file contains no decodable audio."); return; }
    emit progress(96);
    if (!m_busy || generation != m_generation) return;
    auto *watcher = new QFutureWatcher<AudioAssetPtr>(this);
    connect(watcher, &QFutureWatcher<AudioAssetPtr>::finished, this, [this, watcher, generation] {
        const auto asset = watcher->result(); watcher->deleteLater();
        if (generation != m_generation || !m_busy) return;
        if (!asset) { fail("Could not prepare the imported audio."); return; }
        m_busy = false; emit progress(100); emit busyChanged();
        if (generation == m_generation) emit completed(asset);
    });
    auto bytes = std::move(m_pcm); const auto name = m_name;
    watcher->setFuture(QtConcurrent::run([name, bytes = std::move(bytes)]() mutable -> AudioAssetPtr {
        if constexpr (Q_BYTE_ORDER == Q_BIG_ENDIAN) {
            for (qsizetype i = 0; i + 1 < bytes.size(); i += 2) std::swap(bytes[i], bytes[i + 1]);
        }
        try { return makeAudioAsset(name, std::move(bytes)); } catch (const std::bad_alloc &) { return {}; }
    }));
}
} // namespace toon
