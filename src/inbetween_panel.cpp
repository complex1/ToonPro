#include "inbetween_panel.h"
#include "document.h"
#include "easing_graph_widget.h"
#include "effect_render.h"
#include "icons.h"
#include "property_row.h"
#include "reference_panel.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <algorithm>
#include <array>

namespace toon {
namespace {
// Keep ReferenceView's navigation image fixed while scrubbing same-sized cels,
// so an artist inspecting an eye at high zoom does not lose their pan/zoom.
class InbetweenPreview final : public ReferenceView {
public:
    using ReferenceView::ReferenceView;
    void setFrame(const QImage &frame) {
        m_frame=frame;
        if (image().size()!=frame.size() || frame.isNull()) setImage(frame);
        update();
    }
protected:
    void paintEvent(QPaintEvent *event) override {
        if (m_frame.isNull()) { ReferenceView::paintEvent(event); return; }
        QPainter painter(this); painter.fillRect(rect(),QColor(ui::canvas));
        painter.setTransform(imageTransform()); painter.setRenderHint(QPainter::SmoothPixmapTransform);
        if (m_frame.hasAlphaChannel()) {
            QPixmap pattern(16,16); pattern.fill(QColor(ui::transparencyLight));
            { QPainter cells(&pattern); cells.fillRect(0,0,8,8,QColor(ui::transparencyDark)); cells.fillRect(8,8,8,8,QColor(ui::transparencyDark)); }
            painter.fillRect(m_frame.rect(),QBrush(pattern));
        }
        painter.drawImage(QPointF(),m_frame);
    }
private:
    QImage m_frame;
};
QToolButton *action(QWidget *parent, const char *name, const char *icon, const QString &help) {
    auto *button = new QToolButton(parent); button->setObjectName(name);
    button->setIcon(appIcon(icon)); button->setIconSize({16,16}); button->setFixedSize(28,28);
    button->setAutoRaise(true); button->setToolTip(help); button->setAccessibleName(help); return button;
}
void thumbnail(QLabel *label, const QImage &image) {
    label->setPixmap(QPixmap::fromImage(inbetweenWhiteMatte(image)).scaled(140, 76, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
QImage sourceThumbnail(const Project &project, const Shot &shot, const TimelineCelRef &ref) {
    const auto *track=celTrack(shot,ref.trackId); const auto *cel=celExposure(shot,ref);
    const auto *drawing=track && cel?celDrawing(*track,cel->drawingId):nullptr;
    if (!drawing) return {};
    QVector<RasterLayer> layers;
    for (int i=0;i<shot.layers.size();++i) if (shot.layers[i].id==drawing->groupLayerId
        || layerIsDescendant(shot.layers,i,drawing->groupLayerId)) {
        auto layer=shot.layers[i]; if (layer.id==drawing->groupLayerId) layer.parentId.clear(); layers.append(layer);
    }
    const auto size=QSize(project.width,project.height).scaled(140,76,Qt::KeepAspectRatio);
    QImage image(size,QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    clearEffectRenderError();
    { QPainter painter(&image); painter.scale(qreal(size.width())/project.width,qreal(size.height())/project.height); paintLayers(painter,layers,false); }
    return effectRenderError().isEmpty()?image:QImage{};
}
}

InbetweenPanel::InbetweenPanel(TimelineController *controller, QWidget *parent, QSettings *settings)
    : QWidget(parent), m_controller(controller), m_settings(settings) {
    setObjectName("inbetweenControls"); setMinimumWidth(240);
    auto *outer = new QVBoxLayout(this); outer->setContentsMargins(0,0,0,0);
    auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget; auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(6,5,6,5); layout->setSpacing(5); scroll->setWidget(content); outer->addWidget(scroll);
    auto *header = new QHBoxLayout;
    header->addWidget(new QLabel("Output", content)); header->addStretch();
    auto *selection = action(content, "inbetweenUseSelection", "mouse-pointer-2", "Use the two selected timeline cels");
    header->addWidget(selection); layout->addLayout(header);
    m_output = new QTabBar(content); m_output->setObjectName("inbetweenOutput");
    m_output->setAccessibleName("Generated drawing background"); m_output->setProperty("compactDrawingTabs",true);
    m_output->addTab("White background"); m_output->addTab("Transparent line art");
    // Stable names keep the preference independent of tab ordering. Unknown or
    // absent values use the artist's line-art default without editing history.
    const auto outputMode=settings?settings->value("inbetweens/outputMode").toString():QString();
    m_output->setCurrentIndex(outputMode=="white"?0:1);
    m_output->setTabToolTip(0,"Automatically composite source drawings on white for generation; output keeps the white background");
    m_output->setTabToolTip(1,"Single-color outlines: detect the darkest source ink and recover soft transparency. White fills become transparent; colored marks use the detected ink color.");
    layout->addWidget(m_output);
    auto *outputHelp=new QLabel("White is added automatically. Transparent line art uses one ink color; white fills become transparent.",content);
    outputHelp->setWordWrap(true); layout->addWidget(outputHelp);
    auto *poses = new QHBoxLayout;
    auto pose = [&](const QString &name, QComboBox **combo, QLabel **image) {
        auto *column = new QVBoxLayout;
        *image = new QLabel(content); (*image)->setObjectName("inbetween" + name + "Image");
        (*image)->setAlignment(Qt::AlignCenter); (*image)->setFixedHeight(76);
        (*image)->setMinimumWidth(0); (*image)->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
        (*image)->setText(name); (*image)->setAccessibleName(name + " pose thumbnail");
        *combo = new QComboBox(content); (*combo)->setObjectName("inbetween" + name + "Pose");
        (*combo)->setAccessibleName(name + " pose"); (*combo)->setMinimumWidth(0);
        (*combo)->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        (*combo)->setMinimumContentsLength(5); (*combo)->setToolTip("Choose the " + name.toLower() + " drawing exposure; its frame stays fixed");
        column->addWidget(*image); column->addWidget(*combo); poses->addLayout(column,1);
    };
    pose("Start", &m_first, &m_firstImage); pose("End", &m_last, &m_lastImage); layout->addLayout(poses);
    m_frequency = new QTabBar(content); m_frequency->setObjectName("inbetweenFrequency");
    m_frequency->setAccessibleName("Drawing frequency"); m_frequency->setProperty("compactDrawingTabs",true);
    m_frequency->addTab("Every frame"); m_frequency->addTab("On twos");
    m_frequency->setTabToolTip(0,"Generate a drawing at each frame between the poses");
    m_frequency->setTabToolTip(1,"Generate drawings two frames apart, holding each until the next drawing"); layout->addWidget(m_frequency);
    m_summary = new QLabel(content); m_summary->setWordWrap(true); m_summary->setTextFormat(Qt::PlainText); layout->addWidget(m_summary);
    m_presets = new QTabBar(content); m_presets->setObjectName("inbetweenEasingPresets");
    m_presets->setAccessibleName("In-between easing presets"); m_presets->setExpanding(false);
    m_presets->setProperty("compactDrawingTabs",true);
    for (const auto &name : {"Linear","Ease in","Ease out","Ease in-out"}) {
        int i = m_presets->addTab(name); m_presets->setTabToolTip(i, QString("%1 timing between the two poses").arg(name));
    }
    layout->addWidget(m_presets);
    m_graph = new EasingGraphWidget(content); m_graph->setObjectName("inbetweenEasingGraph");
    m_graph->setAccessibleName("In-between easing curve"); layout->addWidget(m_graph);
    auto *previewHeader = new QHBoxLayout;
    m_play = action(content,"inbetweenPlay","play","Play or pause the generated preview"); m_play->setCheckable(true);
    auto *fit = action(content,"inbetweenFit","maximize-2","Fit the in-between preview");
    previewHeader->addWidget(m_play); previewHeader->addWidget(new QLabel("Preview",content)); previewHeader->addStretch(); previewHeader->addWidget(fit);
    layout->addLayout(previewHeader);
    m_view = new InbetweenPreview(content); m_view->setObjectName("inbetweenPreview"); m_view->setMinimumHeight(140);
    m_view->setAccessibleName("Generated in-between preview"); m_view->setEmptyText("Select poses, then Generate");
    m_view->setToolTip("Scroll to zoom; drag to pan. Previewing does not change the timeline."); layout->addWidget(m_view,1);
    m_strip = new QListWidget(content); m_strip->setObjectName("inbetweenFrameStrip"); m_strip->setAccessibleName("Generated drawing strip");
    m_strip->setViewMode(QListView::IconMode); m_strip->setFlow(QListView::LeftToRight); m_strip->setWrapping(false);
    m_strip->setMovement(QListView::Static); m_strip->setIconSize({64,40}); m_strip->setFixedHeight(78);
    m_strip->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); layout->addWidget(m_strip);
    m_scrub = new ValueSlider("Preview drawing",1,2,1); m_scrub->setObjectName("inbetweenScrub");
    layout->addWidget(new PropertyRow("Drawing",m_scrub));
    m_status = new QLabel(content); m_status->setObjectName("inbetweenStatus"); m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true); m_status->setAccessibleName("In-between status"); layout->addWidget(m_status);
    m_progress = new QProgressBar(content); m_progress->setObjectName("inbetweenProgress"); m_progress->setAccessibleName("RIFE generation progress");
    m_progress->hide(); layout->addWidget(m_progress);
    auto *buttons = new QHBoxLayout; buttons->setSpacing(4);
    m_generate = new QPushButton(appIcon("sparkles"),"Generate",content); m_generate->setObjectName("inbetweenGenerate");
    m_generate->setToolTip("Generate full-resolution in-betweens locally with RIFE"); m_generate->setAccessibleName("Generate in-between preview");
    m_cancel = new QPushButton("Cancel",content); m_cancel->setObjectName("inbetweenCancel");
    m_cancel->setToolTip("Cancel generation without changing the drawings");
    buttons->addWidget(m_generate); buttons->addWidget(m_cancel); layout->addLayout(buttons);
    m_insert = new QPushButton(appIcon("check"),"Insert drawings",content); m_insert->setObjectName("inbetweenInsert");
    m_insert->setAccessibleName("Insert generated drawings into the timeline");
    m_insert->setToolTip("Insert this preview between the poses as one undoable edit"); layout->addWidget(m_insert);
    m_progressTimer = new QTimer(this); m_progressTimer->setInterval(100);
    m_playTimer = new QTimer(this);
    connect(selection,&QToolButton::clicked,this,&InbetweenPanel::useSelectedCels);
    connect(m_first,&QComboBox::currentIndexChanged,this,[this] { settingsChanged(); });
    connect(m_last,&QComboBox::currentIndexChanged,this,[this] { settingsChanged(); });
    connect(m_frequency,&QTabBar::currentChanged,this,[this] { settingsChanged(); });
    connect(m_output,&QTabBar::currentChanged,this,[this](int index) {
        if (m_settings && (index==0 || index==1)) {
            m_settings->setValue("inbetweens/outputMode",index==0?"white":"transparent-ink");
            m_settings->sync();
        }
        settingsChanged();
    });
    connect(m_presets,&QTabBar::currentChanged,this,[this](int index) {
        if (m_refreshing || index < 0) return;
        const KeyframeInterpolation presets[]{KeyframeInterpolation::Linear,KeyframeInterpolation::EaseIn,KeyframeInterpolation::EaseOut,KeyframeInterpolation::EaseInOut};
        m_curve = easingForInterpolation(presets[index]); m_graph->setCurve(m_curve.first,m_curve.second); settingsChanged();
    });
    connect(m_graph,&EasingGraphWidget::curveChanged,this,[this](QPointF first,QPointF second) {
        m_curve = {first,second}; { QSignalBlocker block(m_presets); m_presets->setCurrentIndex(-1); } settingsChanged();
    });
    connect(m_graph,&EasingGraphWidget::editingCancelled,this,[this] {
        m_curve = {m_graph->firstControl(),m_graph->secondControl()}; settingsChanged();
    });
    connect(m_generate,&QPushButton::clicked,this,&InbetweenPanel::generate);
    connect(m_cancel,&QPushButton::clicked,this,&InbetweenPanel::cancel);
    connect(m_insert,&QPushButton::clicked,this,&InbetweenPanel::insert);
    connect(fit,&QToolButton::clicked,m_view,&ReferenceView::fitImage);
    connect(m_play,&QToolButton::toggled,this,&InbetweenPanel::setPlaying);
    connect(m_strip,&QListWidget::currentRowChanged,this,&InbetweenPanel::showPreview);
    connect(m_scrub,&ValueSlider::valueChanged,this,[this](double value) { m_strip->setCurrentRow(qRound(value)-1); });
    connect(m_playTimer,&QTimer::timeout,this,[this] {
        if (!m_result) { setPlaying(false); return; }
        m_strip->setCurrentRow((m_strip->currentRow()+1) % m_strip->count());
    });
    connect(m_progressTimer,&QTimer::timeout,this,[this] {
        if (m_job) m_progress->setValue(m_job->completed.load(std::memory_order_relaxed));
    });
    connect(&m_watcher,&QFutureWatcher<InbetweenResult>::finished,this,&InbetweenPanel::finishGeneration);
    connect(controller,&TimelineController::changed,this,&InbetweenPanel::refresh);
    connect(controller->document(),&Document::changed,this,&InbetweenPanel::refresh);
    connect(controller->document(),&Document::loaded,this,&InbetweenPanel::refresh);
    connect(controller->document(),&Document::shotChanged,this,&InbetweenPanel::refresh);
    refresh();
}
InbetweenPanel::~InbetweenPanel() { if (m_job) m_job->cancelled = true; }
TimelineCelRef InbetweenPanel::reference(QComboBox *combo) const {
    return {combo->currentData(Qt::UserRole+1).toString(),combo->currentData().toString()};
}
void InbetweenPanel::selectReference(QComboBox *combo, const TimelineCelRef &ref) {
    for (int i = 0; i < combo->count(); ++i) if (combo->itemData(i).toString() == ref.exposureId
        && combo->itemData(i,Qt::UserRole+1).toString() == ref.trackId) { combo->setCurrentIndex(i); return; }
}
InbetweenPlan InbetweenPanel::currentPlan(QString *error) const {
    const auto *document = m_controller->document();
    if (!document->isOpen()) { if (error) *error = "Open an animation and select two pose cels."; return {}; }
    return planInbetweens(document->project(),document->activeShotId(),{reference(m_first),reference(m_last)},
        m_frequency->currentIndex()+1,m_curve,error,static_cast<InbetweenOutput>(m_output->currentIndex()));
}
void InbetweenPanel::refresh() {
    if (m_refreshing) return;
    auto *document = m_controller->document();
    const auto shot = document->isOpen() ? document->activeShotId() : QString();
    if (shot != m_shotId || document->contentRevision() != m_seenRevision || (m_first->count()==0 && document->isOpen())) {
        const auto first = reference(m_first), last = reference(m_last);
        invalidate("The document changed. Generate a new preview.");
        QScopedValueRollback<bool> refreshing(m_refreshing,true);
        m_shotId=shot; m_seenRevision=document->contentRevision(); m_first->clear(); m_last->clear();
        if (document->isOpen()) for (const auto &track : document->activeShot().animationTracks)
            for (const auto &cel : track.exposures) {
                const auto label = QString("%1 · F%2").arg(drawingDisplayId(track,cel.drawingId)).arg(cel.startFrame+1);
                for (auto *combo : {m_first,m_last}) {
                    combo->addItem(label,cel.id); combo->setItemData(combo->count()-1,track.id,Qt::UserRole+1);
                    combo->setItemData(combo->count()-1,label,Qt::ToolTipRole);
                }
            }
        selectReference(m_first,first); selectReference(m_last,last);
        if (m_last->count()>1 && reference(m_first)==reference(m_last)) m_last->setCurrentIndex(1);
    }
    const auto selected = m_controller->selectedCels();
    if (selected != m_lastSelection) {
        m_lastSelection=selected;
        if (selected.size()==2) useSelectedCels();
    }
    updateButtons();
}
void InbetweenPanel::useSelectedCels() {
    const auto selected=m_controller->selectedCels();
    if (selected.size()!=2) { m_status->setText("Select two drawing cels in the timeline, or choose the poses above."); return; }
    const auto previousFirst=reference(m_first), previousLast=reference(m_last);
    {
        QScopedValueRollback<bool> refreshing(m_refreshing,true);
        selectReference(m_first,selected[0]); selectReference(m_last,selected[1]);
    }
    // Order the visible selectors chronologically, matching the plan.
    QString error; const auto plan=currentPlan(&error);
    if (error.isEmpty()) {
        QScopedValueRollback<bool> refreshing(m_refreshing,true);
        selectReference(m_first,plan.first); selectReference(m_last,plan.last);
    }
    if (previousFirst!=reference(m_first) || previousLast!=reference(m_last)) settingsChanged();
    else updateButtons();
}
void InbetweenPanel::settingsChanged() {
    if (m_refreshing) return;
    QString error; const auto ordered=currentPlan(&error);
    if (error.isEmpty()) {
        QScopedValueRollback<bool> refreshing(m_refreshing,true);
        selectReference(m_first,ordered.first); selectReference(m_last,ordered.last);
    }
    invalidate("Settings changed. Generate a new preview."); updateButtons();
}
void InbetweenPanel::invalidate(const QString &reason) {
    if (m_job) m_job->cancelled=true;
    m_discardJob=m_busy; setPlaying(false); m_result.reset();
    m_strip->clear(); static_cast<InbetweenPreview *>(m_view)->setFrame({}); m_scrub->setValueSilently(1);
    m_status->setText(reason);
}
void InbetweenPanel::updateButtons() {
    QString error; const auto plan=currentPlan(&error);
    const bool ready=error.isEmpty() && !m_controller->playing();
    if (error.isEmpty()) {
        m_summary->setText(QString("F%1 → F%2 · %3 in-betweens").arg(plan.firstFrame+1).arg(plan.lastFrame+1).arg(plan.samples.size()));
        m_graph->setSegment(plan.firstFrame,plan.lastFrame,0,1,"progress");
        const auto key=plan.shotId+plan.first.exposureId+plan.last.exposureId+QString::number(m_seenRevision);
        if (key!=m_thumbnailKey && !m_busy) {
            m_thumbnailKey=key;
            const auto *document=m_controller->document();
            thumbnail(m_firstImage,sourceThumbnail(document->project(),document->activeShot(),plan.first));
            thumbnail(m_lastImage,sourceThumbnail(document->project(),document->activeShot(),plan.last));
        }
    } else {
        m_summary->setText(error); m_thumbnailKey.clear(); m_firstImage->setText("Start"); m_lastImage->setText("End");
    }
    const auto runtime=findRifeRuntime();
    m_generate->setEnabled(ready && !m_busy && runtime.error().isEmpty());
    if (ready && !m_busy && !m_result && !runtime.error().isEmpty()) m_status->setText(runtime.error());
    m_cancel->setEnabled(m_busy); m_insert->setEnabled(ready && !m_busy && m_result.has_value());
    m_play->setEnabled(m_result.has_value()); m_scrub->setEnabled(m_result.has_value());
    for (auto *widget : std::array<QWidget *,6>{m_first,m_last,m_frequency,m_presets,m_graph,m_output}) widget->setEnabled(!m_busy);
}
void InbetweenPanel::generate() {
    if (m_busy || !m_controller->prepareTimelineEdit()) return;
    m_graph->finishEditing();
    QString error; const auto plan=currentPlan(&error); const auto runtime=findRifeRuntime();
    if (!error.isEmpty() || !runtime.error().isEmpty()) { m_status->setText(error.isEmpty()?runtime.error():error); return; }
    invalidate({}); m_busy=true; m_discardJob=false;
    m_job=std::make_shared<RifeJobState>(); m_jobRevision=m_controller->document()->contentRevision();
    m_status->setText(QString("Generating %1 drawings…").arg(plan.samples.size()));
    m_progress->setRange(0,plan.samples.size()); m_progress->setValue(0); m_progress->show(); m_progressTimer->start(); updateButtons();
    const auto snapshot=m_controller->document()->project(); const auto state=m_job;
    m_watcher.setFuture(QtConcurrent::run([snapshot,plan,runtime,state] { return generateInbetweens(snapshot,plan,runtime,state); }));
}
void InbetweenPanel::cancel() {
    if (!m_busy) return;
    invalidate("Generation cancelled. The timeline is unchanged."); updateButtons();
}
void InbetweenPanel::finishGeneration() {
    auto result=m_watcher.result(); m_busy=false; m_progressTimer->stop(); m_progress->hide();
    QString error; const auto plan=currentPlan(&error);
    if (m_discardJob || result.cancelled || m_jobRevision!=m_controller->document()->contentRevision()
        || !error.isEmpty() || plan!=result.plan) { m_discardJob=false; m_job.reset(); updateButtons(); return; }
    m_job.reset();
    if (!result.error.isEmpty()) { m_status->setText(result.error); updateButtons(); return; }
    m_result=std::move(result); thumbnail(m_firstImage,m_result->first); thumbnail(m_lastImage,m_result->last);
    const QSignalBlocker blocker(m_strip);
    auto append=[&](const QImage &image,int frame,bool endpoint) {
        auto *item=new QListWidgetItem(QIcon(QPixmap::fromImage(inbetweenWhiteMatte(image).scaled(64,40,Qt::KeepAspectRatio,Qt::SmoothTransformation))),QString("F%1").arg(frame+1),m_strip);
        item->setToolTip(QString(endpoint?"Original pose · Frame %1":"Generated drawing · Frame %1").arg(frame+1));
    };
    append(m_result->first,plan.firstFrame,true);
    for (int i=0;i<plan.samples.size();++i) append(m_result->images[i],plan.samples[i].frame,false);
    append(m_result->last,plan.lastFrame,true);
    m_scrub->setRange(1,m_strip->count()); m_scrub->setValueSilently(1); m_strip->setCurrentRow(0); showPreview(0);
    m_status->setText(QString("%1 drawings ready to review. Insert adds one Undo step.").arg(plan.samples.size())); updateButtons();
}
void InbetweenPanel::showPreview(int index) {
    if (!m_result || index<0 || index>=m_result->images.size()+2) return;
    const QImage &image=index==0?m_result->first:index==m_result->images.size()+1?m_result->last:m_result->images[index-1];
    static_cast<InbetweenPreview *>(m_view)->setFrame(image); m_scrub->setValueSilently(index+1);
    if (m_playTimer->isActive()) {
        const int hold=index>0 && index<=m_result->plan.samples.size()?m_result->plan.samples[index-1].hold:m_result->plan.step;
        m_playTimer->start(std::max(16,qRound(1000.0*hold/m_controller->document()->project().fps)));
    }
}
void InbetweenPanel::setPlaying(bool playing) {
    if (!playing || !m_result) {
        m_playTimer->stop(); QSignalBlocker block(m_play); m_play->setChecked(false); m_play->setIcon(appIcon("play")); return;
    }
    m_play->setIcon(appIcon("pause")); m_playTimer->start(100); showPreview(m_strip->currentRow());
}
void InbetweenPanel::insert() {
    if (!m_result || m_busy || !m_controller->prepareTimelineEdit()) return;
    // prepareTimelineEdit can resolve a document gesture, invalidating this panel.
    if (!m_result) return;
    const auto result=*m_result; const auto revision=m_jobRevision;
    if (m_controller->document()->insertInbetweens(result.plan,result.images,revision)) {
        invalidate("Inserted in-between drawings. Undo restores the previous timeline."); updateButtons();
    }
}
} // namespace toon
