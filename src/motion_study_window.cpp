#include "motion_study_window.h"
#include "value_slider.h"
#include "icons.h"
#include "ui_metrics.h"
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QImageReader>
#include <algorithm>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <cmath>
namespace toon {
namespace {
QString framePath(QString directory,int frame){return QDir(directory).filePath(QString("%1.png").arg(frame+1,6,10,QChar('0')));}
QString program(const QString &name){auto p=QStandardPaths::findExecutable(name);if(!p.isEmpty())return p;for(const auto &dir:{"/opt/homebrew/bin","/usr/local/bin"}){p=QString(dir)+"/"+name;if(QFileInfo(p).isExecutable())return p;}return {};}
struct Imported {MotionStudy study;std::shared_ptr<QTemporaryDir> cache;QString error;};
bool run(const QString &program,const QStringList &args,const std::shared_ptr<std::atomic_bool> &cancel,QByteArray *output,QString *error){
    QProcess process;process.start(program,args);if(!process.waitForStarted(5000)){*error=process.errorString();return false;}
    QByteArray data,diagnostics;int elapsed=0;
    while(process.state()!=QProcess::NotRunning){process.waitForFinished(100);elapsed+=100;data+=process.readAllStandardOutput();diagnostics+=process.readAllStandardError();if(diagnostics.size()>8192)diagnostics=diagnostics.right(8192);
        if(cancel->load()||data.size()>8*1024*1024||elapsed>600000){process.kill();process.waitForFinished();*error=cancel->load()?"Cancelled.":"Video processing exceeded the study limit. Choose a shorter clip.";return false;}}
    data+=process.readAllStandardOutput();if(process.exitStatus()!=QProcess::NormalExit||process.exitCode()!=0){*error="Video decoder failed: "+QString::fromUtf8(diagnostics.right(1500));return false;}
    *output=data;return true;
}
Imported decode(const QString &path,const std::shared_ptr<std::atomic_bool> &cancel){
    Imported r;auto probe=program("ffprobe"),decoder=program("ffmpeg");
    if(probe.isEmpty()||decoder.isEmpty()){r.error="Video import requires FFmpeg and ffprobe on PATH. Saved studies can be opened without them.";return r;}
    QByteArray output;
    if(!run(probe,{"-v","error","-select_streams","v:0","-show_frames","-show_entries","frame=best_effort_timestamp_time,pkt_duration_time:stream=avg_frame_rate,duration","-of","json",path},cancel,&output,&r.error))return r;
    const auto json=QJsonDocument::fromJson(output).object();const auto streams=json["streams"].toArray(),frames=json["frames"].toArray();
    if(streams.isEmpty()||frames.isEmpty()||frames.size()>1800){r.error="Choose a video study with 1–1,800 frames. Trim longer footage before importing.";return r;}
    const auto stream=streams[0].toObject();const auto parts=stream["avg_frame_rate"].toString().split('/');
    if(parts.size()!=2||parts[1].toDouble()==0){r.error="Video frame rate could not be read.";return r;}
    r.study.fps=parts[0].toDouble()/parts[1].toDouble();r.study.name=QFileInfo(path).completeBaseName().left(200);
    const double origin=frames[0].toObject()["best_effort_timestamp_time"].toString().toDouble();
    for(auto value:frames){bool ok=false;const double time=value.toObject()["best_effort_timestamp_time"].toString().toDouble(&ok);if(!ok){r.error="Video is missing frame timestamps.";return r;}r.study.times.append(time-origin);}
    double tail=frames.last().toObject()["pkt_duration_time"].toString().toDouble();if(tail<=0)tail=1/r.study.fps;
    r.study.duration=r.study.times.last()+tail;r.study.sample(1);r.error=r.study.validate();if(!r.error.isEmpty())return r;
    r.cache=std::make_shared<QTemporaryDir>();if(!r.cache->isValid()){r.error="Could not create study cache.";return r;}
    if(!run(decoder,{"-v","error","-nostdin","-i",path,"-map","0:v:0","-an","-sn","-vf","scale=640:640:force_original_aspect_ratio=decrease","-vsync","0","-frames:v","1801",r.cache->filePath("%06d.png")},cancel,&output,&r.error))return r;
    if(QDir(r.cache->path()).entryList({"*.png"},QDir::Files).size()!=r.study.times.size())r.error="Decoded frame count does not match the source timing; the study was not imported.";
    return r;
}
QString savePackage(const MotionStudy &study,const QString &source,const QString &target,const std::shared_ptr<std::atomic_bool> &cancel){
    if(QFileInfo::exists(target))return "Choose a new study folder; existing files are preserved.";
    const QFileInfo destination(target);QTemporaryDir staging(destination.absolutePath()+"/.motion-study-XXXXXX");if(!staging.isValid())return "Could not prepare study folder.";
    for(int i=0;i<study.times.size();++i){if(cancel->load())return "Study save cancelled.";if(!QFile::copy(framePath(source,i),framePath(staging.path(),i)))return "Could not copy study frames.";}
    QSaveFile manifest(staging.filePath("study.json"));const auto bytes=QJsonDocument(study.toJson()).toJson();
    if(!manifest.open(QIODevice::WriteOnly)||manifest.write(bytes)!=bytes.size()||!manifest.commit())return "Could not save study metadata.";
    if(cancel->load())return "Study save cancelled.";
    if(!QDir().rename(staging.path(),target))return "Could not publish study folder.";staging.setAutoRemove(false);return {};
}
}
MotionStudyWindow::MotionStudyWindow(QWidget *parent):QWidget(parent,Qt::Window){
    setObjectName("motionStudyWindow");setAttribute(Qt::WA_DeleteOnClose);setWindowTitle("Motion Study");resize(960,720);
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(8,8,8,8);layout->setSpacing(6);
    m_controls=new QWidget;auto *tools=new QHBoxLayout(m_controls);tools->setContentsMargins(0,0,0,0);
    const auto button=[&](QString icon,QString help,auto action){auto *b=new QToolButton;b->setIcon(appIcon(icon));b->setIconSize({16,16});b->setFixedSize(28,28);b->setToolTip(help);b->setAccessibleName(help);tools->addWidget(b);connect(b,&QToolButton::clicked,this,action);return b;};
    button("file-plus","Import video",[this]{importVideo();});button("folder-open","Open saved study",[this]{openStudy();});button("save","Save study as…",[this]{saveStudy();});
    tools->addSpacing(10);
    for(int step=1;step<=3;++step){auto *b=new QToolButton;b->setText(step==1?"Ones":step==2?"Twos":"Threes");b->setToolTip("Reset frame choices to every "+QString::number(step)+" source frame(s)");tools->addWidget(b);connect(b,&QToolButton::clicked,this,[this,step]{if(m_study.times.isEmpty())return;m_timer->stop();m_study.sample(step);m_dirty=true;rebuild();});}
    tools->addStretch();button("layers","Add editable cels to current timeline",[this]{if(!m_study.times.isEmpty())emit insertRequested(m_study,m_frames,false);});
    button("image","Add locked reference track (excluded from movie export)",[this]{if(!m_study.times.isEmpty())emit insertRequested(m_study,m_frames,true);});layout->addWidget(m_controls);
    m_info=new QLabel("Import a short video to study its motion.");m_info->setWordWrap(true);layout->addWidget(m_info);
    m_mode=new QTabBar;m_mode->addTab("Study");m_mode->addTab("Source");m_mode->setExpanding(false);m_mode->setAccessibleName("Study or original video preview");layout->addWidget(m_mode);
    m_preview=new QLabel;m_preview->setAlignment(Qt::AlignCenter);m_preview->setMinimumSize(100,100);m_preview->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);layout->addWidget(m_preview,1);
    auto *transport=new QHBoxLayout;m_play=new QToolButton;m_play->setIcon(appIcon("play"));m_play->setToolTip("Play/pause study");m_play->setAccessibleName(m_play->toolTip());transport->addWidget(m_play);
    m_frame=new ValueSlider("Source frame",1,1,1);m_frame->setAccessibleName("Source frame");transport->addWidget(m_frame,1);layout->addLayout(transport);
    m_strip=new QListWidget;m_strip->setFlow(QListView::LeftToRight);m_strip->setWrapping(false);m_strip->setMaximumHeight(75);m_strip->setAccessibleName("Source frames; check to keep, uncheck to skip");layout->addWidget(m_strip);
    auto *bottom=new QHBoxLayout;m_status=new QLabel;m_status->setWordWrap(true);bottom->addWidget(m_status,1);auto *cancel=new QToolButton;cancel->setIcon(appIcon("x"));cancel->setToolTip("Cancel study processing");cancel->setAccessibleName(cancel->toolTip());bottom->addWidget(cancel);layout->addLayout(bottom);
    m_timer=new QTimer(this);m_timer->setSingleShot(true);
    connect(cancel,&QToolButton::clicked,this,[this]{if(m_cancel)m_cancel->store(true);});
    connect(m_play,&QToolButton::clicked,this,[this]{if(m_timer->isActive()){m_timer->stop();m_play->setIcon(appIcon("play"));}else if(!m_study.times.isEmpty()){m_play->setIcon(appIcon("pause"));m_playOffset=m_study.times[int(m_frame->value())-1];m_clock.restart();m_timer->start(1);}});
    connect(m_timer,&QTimer::timeout,this,[this]{double time=std::fmod(m_playOffset+m_clock.elapsed()/1000.,m_study.duration);int next=qMax(0,int(std::upper_bound(m_study.times.cbegin(),m_study.times.cend(),time)-m_study.times.cbegin())-1);if(int(m_frame->value())!=next+1){m_frame->setValueSilently(next+1);render();}m_timer->start(8);});
    connect(m_frame,&ValueSlider::valueChanged,this,[this]{m_timer->stop();m_play->setIcon(appIcon("play"));render();});connect(m_mode,&QTabBar::currentChanged,this,[this]{render();});
    connect(m_strip,&QListWidget::currentRowChanged,this,[this](int row){if(!m_refreshing&&row>=0)m_frame->setValue(row+1);});
    connect(m_strip,&QListWidget::itemChanged,this,[this](QListWidgetItem *item){if(m_refreshing)return;const int i=m_strip->row(item);if(i==0&&item->checkState()!=Qt::Checked){m_refreshing=true;item->setCheckState(Qt::Checked);m_refreshing=false;return;}m_study.kept[i]=item->checkState()==Qt::Checked;m_dirty=true;render();});
}
MotionStudyWindow::~MotionStudyWindow(){if(m_cancel)m_cancel->store(true);}
bool MotionStudyWindow::discard(){return !m_dirty||QMessageBox::question(this,"Unfinished study","Discard unsaved study changes?",QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Cancel)==QMessageBox::Discard;}
void MotionStudyWindow::closeEvent(QCloseEvent *event){if(m_busy){if(m_cancel)m_cancel->store(true);m_status->setText("Cancelling… close again after processing finishes.");event->ignore();return;}if(!discard()){event->ignore();return;}m_timer->stop();m_dirty=false;event->accept();}
void MotionStudyWindow::importVideo(){
    if(m_busy||!discard())return;const auto path=QFileDialog::getOpenFileName(this,"Import motion study video",{},"Videos (*.mp4 *.mov *.mkv *.webm *.avi);;All files (*)");if(path.isEmpty())return;
    importVideoFile(path);
}
void MotionStudyWindow::importVideoFile(const QString &path){
    if(m_busy)return;
    m_timer->stop();m_busy=true;m_controls->setEnabled(false);m_strip->setEnabled(false);m_cancel=std::make_shared<std::atomic_bool>(false);m_status->setText("Reading timestamps and preparing frames…");
    auto *watcher=new QFutureWatcher<Imported>(this);connect(watcher,&QFutureWatcher<Imported>::finished,this,[this,watcher]{auto result=watcher->result();watcher->deleteLater();m_busy=false;m_controls->setEnabled(true);m_strip->setEnabled(true);if(!result.error.isEmpty()){m_status->setText(result.error);emit studyLoaded(false);return;}m_study=result.study;m_cache=result.cache;m_frames=m_cache->path();m_saved.clear();m_dirty=true;rebuild();m_status->setText("640 px study previews · original video unchanged. Skipped frames remain available.");emit studyLoaded(true);});watcher->setFuture(QtConcurrent::run(decode,path,m_cancel));
}
void MotionStudyWindow::rebuild(){m_refreshing=true;m_strip->clear();for(int i=0;i<m_study.times.size();++i){auto *item=new QListWidgetItem(QString::number(i+1),m_strip);item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(m_study.kept[i]?Qt::Checked:Qt::Unchecked);item->setToolTip(QString("Source frame %1 · %2 s").arg(i+1).arg(m_study.times[i],0,'f',3));}m_frame->setRange(1,qMax(1,int(m_study.times.size())));m_frame->setValueSilently(1);m_refreshing=false;render();}
void MotionStudyWindow::render(){if(m_study.times.isEmpty())return;const int source=qBound(0,int(m_frame->value())-1,int(m_study.times.size())-1),shown=m_mode->currentIndex()==1?source:m_study.heldFrame(source);QImageReader reader(framePath(m_frames,shown));const auto size=reader.size();if(!size.isValid()||size.width()>1024||size.height()>1024){m_timer->stop();m_status->setText("Invalid study frame dimensions.");return;}QImage image=reader.read();m_preview->setPixmap(QPixmap::fromImage(image).scaled(m_preview->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));int kept=0;for(bool v:m_study.kept)kept+=v;m_info->setText(QString("%1 · %2 FPS source · %3 / %4 frames kept · %5 s · Showing %6 · %7 images/s average").arg(m_study.name).arg(m_study.fps,0,'f',3).arg(kept).arg(m_study.times.size()).arg(m_study.duration,0,'f',2).arg(shown+1).arg(kept/m_study.duration,0,'f',2));}
void MotionStudyWindow::saveStudy(){
    if(m_busy||m_study.times.isEmpty())return;auto path=QFileDialog::getSaveFileName(this,"Save motion study folder",{},"Motion study (*.toonstudy)");if(path.isEmpty())return;if(!path.endsWith(".toonstudy",Qt::CaseInsensitive))path+=".toonstudy";
    saveStudyAs(path);
}
void MotionStudyWindow::saveStudyAs(const QString &path){
    if(m_busy||m_study.times.isEmpty())return;
    m_timer->stop();m_busy=true;m_cancel=std::make_shared<std::atomic_bool>(false);m_controls->setEnabled(false);m_strip->setEnabled(false);m_status->setText("Saving all study frames…");
    auto *watcher=new QFutureWatcher<QString>(this);connect(watcher,&QFutureWatcher<QString>::finished,this,[this,watcher,path]{auto error=watcher->result();watcher->deleteLater();m_busy=false;m_controls->setEnabled(true);m_strip->setEnabled(true);if(error.isEmpty()){m_dirty=false;m_saved=path;m_status->setText("Study saved with all source frames and frame choices.");}else m_status->setText(error);emit studySaved(error.isEmpty());});watcher->setFuture(QtConcurrent::run(savePackage,m_study,m_frames,path,m_cancel));
}
void MotionStudyWindow::openStudy(){
    if(m_busy||!discard())return;const auto root=QFileDialog::getExistingDirectory(this,"Open motion study folder");if(root.isEmpty())return;
    openStudyFolder(root);
}
bool MotionStudyWindow::openStudyFolder(const QString &root){
    if(m_busy)return false;
    QFile file(QDir(root).filePath("study.json"));if(file.size()>1024*1024||!file.open(QIODevice::ReadOnly)){m_status->setText("Could not read study metadata.");return false;}
    QString error;const auto study=MotionStudy::fromJson(QJsonDocument::fromJson(file.readAll()).object(),&error);if(!study){m_status->setText(error);return false;}
    for(int i=0;i<study->times.size();++i){QFileInfo frame(framePath(root,i));if(!frame.isFile()||frame.isSymLink()||frame.size()>8*1024*1024){m_status->setText("Study has missing or invalid frames.");return false;}}
    m_timer->stop();m_study=*study;m_cache.reset();m_frames=root;m_saved=root;m_dirty=false;rebuild();m_status->setText("Study opened.");return true;
}
}
