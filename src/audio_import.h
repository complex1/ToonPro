#pragma once
#include "audio.h"
#include <QObject>
#include <QPointer>
#include <QTimer>

class QAudioDecoder;
namespace toon {
// Decoding is asynchronous. Final hashing/peak construction runs on a worker;
// a cancelled or superseded result never reaches the document.
class AudioImport final : public QObject {
    Q_OBJECT
public:
    explicit AudioImport(QObject *parent = nullptr);
    ~AudioImport() override;
    bool start(const QString &path);
    void cancel();
    bool busy() const { return m_busy; }
signals:
    void progress(int percent);
    void completed(AudioAssetPtr asset);
    void failed(const QString &reason);
    void busyChanged();
private:
    void fail(const QString &reason);
    void releaseDecoder();
    void finishDecode();
    QPointer<QAudioDecoder> m_decoder;
    QTimer m_timeout;
    QByteArray m_pcm;
    QString m_name;
    quint64 m_generation = 0;
    bool m_busy = false;
};
} // namespace toon
