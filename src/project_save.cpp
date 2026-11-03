#include "project_save.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QTemporaryFile>
#include <QtConcurrentRun>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif
namespace toon {
struct ProjectSave::Job {
    std::atomic_bool cancelled{false};
    QString path,temporary,error;
    bool prepared=false;
    ~Job(){if(!temporary.isEmpty())QFile::remove(temporary);}
};
namespace {
bool publish(const QString &source,const QString &destination,QString *error){
#ifdef Q_OS_WIN
    if(MoveFileExW(reinterpret_cast<LPCWSTR>(source.utf16()),reinterpret_cast<LPCWSTR>(destination.utf16()),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return true;
    *error=QString("Could not publish the project (Windows error %1).").arg(GetLastError());
#else
    if(std::rename(QFile::encodeName(source).constData(),QFile::encodeName(destination).constData())==0)return true;
    *error="Could not publish the project: "+QString::fromLocal8Bit(std::strerror(errno));
#endif
    return false;
}
}
ProjectSave::ProjectSave(QObject *parent):QObject(parent),m_watcher(new QFutureWatcher<void>(this)){
    connect(m_watcher,&QFutureWatcher<void>::finished,this,[this]{
        auto job=std::move(m_job);if(!job)return;
        const bool cancelled=job->cancelled.load(std::memory_order_relaxed);
        bool saved=false;
        if(!cancelled&&job->prepared){
            const QFileInfo target(job->path);
            if(target.exists()&&!target.isWritable())job->error="The project destination is read-only.";
            else if(target.exists()&&target.isFile()&&!QFile::setPermissions(job->temporary,target.permissions()))job->error="Could not preserve the project file permissions.";
            else saved=publish(job->temporary,job->path,&job->error);
        }
        m_busy=false;emit finished(saved,cancelled,job->path,cancelled?QString():job->error);
    });
}
ProjectSave::~ProjectSave(){cancel();disconnect(m_watcher,nullptr,this,nullptr);}
void ProjectSave::cancel(){if(m_job)m_job->cancelled.store(true,std::memory_order_relaxed);}
bool ProjectSave::start(Project snapshot,const QString &destination){
    if(m_busy||destination.trimmed().isEmpty())return false;
    auto job=std::make_shared<Job>();job->path=QFileInfo(destination).absoluteFilePath();m_job=job;m_busy=true;
    m_watcher->setFuture(QtConcurrent::run([job,snapshot=std::move(snapshot)]{
        if(job->cancelled.load(std::memory_order_relaxed))return;
        const QFileInfo target(job->path);
        QTemporaryFile temporary(target.absoluteDir().filePath(".toon-save-XXXXXX"));
        if(!temporary.open()){job->error="Could not prepare project save: "+temporary.errorString();return;}
        job->temporary=temporary.fileName();temporary.setAutoRemove(false);temporary.close();
        // saveProject uses QSaveFile internally; the destination is an owned
        // sibling temporary file until the main thread approves publication.
        job->prepared=saveProject(snapshot,job->temporary,&job->error,[job]{return job->cancelled.load(std::memory_order_relaxed);});
    }));
    return true;
}
}
