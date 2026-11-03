#pragma once
#include <QObject>
#include <memory>
namespace toon {
struct Shot;
// Short, bounded previews while dragging the playhead. New seeks replace older
// requests; scrubbing never drives the document clock or modifies artwork.
class AudioScrub final : public QObject {
    Q_OBJECT
public:
    enum class OutputMode { System, Silent };
    explicit AudioScrub(QObject *parent = nullptr, OutputMode mode = OutputMode::System);
    ~AudioScrub() override;
    void begin(const Shot &shot, double fps);
    void seek(int frame);
    void end();
    bool active() const;
signals:
    void warning(const QString &message);
    void grainReady(int frame, const QByteArray &nativeStereoPcm);
private:
    struct Private;
    std::unique_ptr<Private> d;
};
}
