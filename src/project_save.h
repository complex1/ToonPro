#pragma once
#include "project.h"
#include <QObject>
#include <memory>
template<typename T> class QFutureWatcher;
namespace toon {
// Compresses an immutable snapshot off the GUI thread. Only the GUI-side
// completion may publish it, so cancellation cannot resurrect old recovery.
class ProjectSave final : public QObject {
    Q_OBJECT
public:
    explicit ProjectSave(QObject *parent=nullptr);
    ~ProjectSave() override;
    bool start(Project snapshot,const QString &destination);
    void cancel();
    bool busy() const { return m_busy; }
signals:
    void finished(bool saved,bool cancelled,const QString &path,const QString &error);
private:
    struct Job;
    std::shared_ptr<Job> m_job;
    QFutureWatcher<void> *m_watcher;
    bool m_busy=false;
};
}
