#include "workspace_setup_widgets.h"
#include "icons.h"
#include "ui_theme.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
namespace toon {
QString managementStyle() {
    return QString("QWidget {color:%1;} QLabel[muted=\"true\"] {color:%2;} "
                   "QLineEdit {padding:9px 12px;min-height:20px;border:1px solid "
                   "%3;border-radius:4px;background:%4;} "
                   "QPushButton {min-height:22px;padding:8px 14px;border:1px "
                   "solid %3;border-radius:5px;background:transparent;} "
                   "QPushButton:hover {background:%5;} QPushButton:checked "
                   "{background:%6;border-color:%7;} "
                   "QPushButton[primary=\"true\"] "
                   "{background:%8;color:%4;border-color:%8;font-weight:600;} "
                   "QPushButton[primary=\"true\"]:hover {background:%7;} "
                   "QPushButton:disabled {color:%2;} "
                   "QToolButton {min-width:28px;min-height:28px;border:1px solid "
                   "transparent;border-radius:4px;} QToolButton:hover "
                   "{background:%5;} QToolButton:checked "
                   "{background:%6;border-color:%7;} "
                   "QComboBox {padding:8px "
                   "12px;min-height:20px;background:%4;border:1px solid "
                   "%3;border-radius:4px;} "
                   "QTabBar::tab {padding:11px "
                   "12px;background:transparent;border:0;border-bottom:2px solid "
                   "transparent;color:%2;} QTabBar::tab:selected "
                   "{color:%1;border-bottom-color:%7;} QTabBar::tab:hover "
                   "{background:%5;} "
                   "QCheckBox {spacing:8px;padding:5px 8px;} QCheckBox:checked "
                   "{background:%6;border-radius:4px;}")
        .arg(ui::text, ui::muted, ui::border, ui::base, ui::hover, ui::selected, ui::focus, ui::accent);
}
SetupDialog::SetupDialog(const QString &title, const QString &context, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint) {
    setWindowTitle(title);
    setAccessibleName(title);
    setWindowModality(Qt::WindowModal);
    setStyleSheet(managementStyle());
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(32, 24, 32, 22);
    outer->setSpacing(14);
    auto *header = new QHBoxLayout;
    auto *titles = new QVBoxLayout;
    auto *heading = new QLabel(title);
    auto font = heading->font();
    font.setPointSize(25);
    font.setWeight(QFont::DemiBold);
    heading->setFont(font);
    titles->addWidget(heading);
    if (!context.isEmpty()) {
        auto *sub = new QLabel(context);
        sub->setProperty("muted", true);
        sub->setWordWrap(true);
        titles->addWidget(sub);
    }
    header->addLayout(titles, 1);
    auto *close = new QToolButton;
    close->setIcon(appIcon("x"));
    close->setAccessibleName("Cancel " + title);
    close->setToolTip(close->accessibleName());
    connect(close, &QToolButton::clicked, this, &QDialog::reject);
    header->addWidget(close, 0, Qt::AlignTop);
    outer->addLayout(header);
    content = new QVBoxLayout;
    content->setSpacing(18);
    outer->addLayout(content, 1);
    error = new QLabel;
    error->setWordWrap(true);
    error->hide();
    outer->addWidget(error);
    footer = new QHBoxLayout;
    footer->setSpacing(12);
    footer->addStretch();
    outer->addLayout(footer);
    setMinimumWidth(680);
}
void SetupDialog::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor(ui::muted));
    p.setBrush(QColor(ui::panel));
    p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 7, 7);
}
int execSetupDialog(QDialog &dialog) {
    QWidget *window = dialog.parentWidget() ? dialog.parentWidget()->window() : nullptr;
    QWidget scrim(window);
    scrim.setObjectName("setupScrim");
    scrim.setStyleSheet("background:rgba(0,0,0,120)");
    if (window) {
        scrim.setGeometry(window->rect());
        scrim.show();
        scrim.raise();
    }
    if (auto *screen = dialog.screen()) {
        auto size = dialog.size().boundedTo(screen->availableGeometry().size() - QSize(48, 64));
        dialog.resize(size);
    }
    return dialog.exec();
}
ProjectTypeCards::ProjectTypeCards(QWidget *parent) : QTabBar(parent) {
    setExpanding(true);
    setDrawBase(false);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName("Project type");
    for (const auto &pair : QList<QPair<QString, QString>>{
             {"Drawing", "drawing"}, {"Animation", "animation"}, {"Movie", "movie"}}) {
        auto i = addTab(pair.first);
        setTabData(i, pair.second);
        setTabToolTip(i, pair.first + " project");
        setAccessibleTabName(i, pair.first + " project");
    }
}
QSize ProjectTypeCards::tabSizeHint(int) const { return {180, 224}; }
void ProjectTypeCards::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QStringList icons{"brush", "clapperboard", "folder-open"},
        descriptions{"A collection of drawings", "A collection of single-shot animations",
                     "Folders, drawings and animations"};
    for (int i = 0; i < count(); ++i) {
        auto r = tabRect(i).adjusted(2, 2, -12, -2);
        bool selected = i == currentIndex();
        p.setPen(QPen(QColor(selected ? ui::focus : ui::border), selected ? 2 : 1));
        p.setBrush(QColor(selected ? ui::selected : ui::panel));
        p.drawRoundedRect(r, 6, 6);
        appIcon(icons[i]).paint(&p, QRect(r.center().x() - 33, r.top() + 35, 66, 66));
        auto font = this->font();
        font.setPointSize(19);
        font.setWeight(QFont::DemiBold);
        p.setFont(font);
        p.setPen(QColor(ui::text));
        p.drawText(QRect(r.left() + 10, r.top() + 118, r.width() - 20, 30), Qt::AlignCenter, tabText(i));
        font.setPointSize(12);
        font.setWeight(QFont::Normal);
        p.setFont(font);
        p.setPen(QColor(ui::muted));
        p.drawText(QRect(r.left() + 20, r.top() + 155, r.width() - 40, 64),
                   Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, descriptions[i]);
        if (selected)
            appIcon("check").paint(&p, QRect(r.right() - 29, r.top() + 12, 18, 18));
        if (selected && hasFocus()) {
            p.setPen(QPen(QColor(ui::focus), 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(5, 5, -5, -5), 4, 4);
        }
    }
}
TagEditor::TagEditor(const QStringList &tags, QWidget *parent, bool presets) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    if (presets) {
        auto *grid = new QGridLayout;
        grid->setContentsMargins(0, 0, 0, 0);
        int n = 0;
        for (const auto &s : {"Storyboard", "Animatics", "Background", "Asset", "Character sheet"}) {
            auto *c = new QCheckBox(s);
            grid->addWidget(c, n / 3, n % 3);
            m_presets.append(c);
            ++n;
            connect(c, &QCheckBox::toggled, this, &TagEditor::tagsChanged);
        }
        layout->addLayout(grid);
    }
    auto *row = new QHBoxLayout;
    m_custom = new QLineEdit;
    m_custom->setObjectName("customTags");
    m_custom->setPlaceholderText("Add a custom tag");
    m_custom->setAccessibleName("Custom tag");
    m_custom->installEventFilter(this);
    row->addWidget(m_custom, 1);
    auto *add = new QPushButton(appIcon("plus"), "Add");
    add->setAutoDefault(false);
    connect(add, &QPushButton::clicked, this, &TagEditor::addDraft);
    row->addWidget(add);
    layout->addLayout(row);
    m_chips = new QWidget;
    layout->addWidget(m_chips);
    setTags(tags);
}
QStringList TagEditor::tags() const {
    QStringList result = m_tags;
    for (auto *c : m_presets)
        if (c->isChecked() && !result.contains(c->text(), Qt::CaseInsensitive))
            result.append(c->text());
    auto draft = m_custom->text().trimmed();
    if (!draft.isEmpty() && !result.contains(draft, Qt::CaseInsensitive))
        result.append(draft);
    return result;
}
void TagEditor::setTags(const QStringList &tags) {
    m_custom->clear();
    m_tags.clear();
    for (auto *c : m_presets) {
        const QSignalBlocker block(c);
        c->setChecked(tags.contains(c->text(), Qt::CaseInsensitive));
    }
    for (const auto &s : tags) {
        bool predefined = false;
        for (auto *c : m_presets)
            predefined |= c->text().compare(s, Qt::CaseInsensitive) == 0;
        if (!predefined && !m_tags.contains(s, Qt::CaseInsensitive))
            m_tags.append(s);
    }
    rebuildChips();
}
void TagEditor::addDraft() {
    auto text = m_custom->text().trimmed();
    if (text.isEmpty())
        return;
    for (auto *c : m_presets)
        if (c->text().compare(text, Qt::CaseInsensitive) == 0) {
            c->setChecked(true);
            m_custom->clear();
            return;
        }
    if (!m_tags.contains(text, Qt::CaseInsensitive))
        m_tags.append(text);
    m_custom->clear();
    rebuildChips();
    emit tagsChanged();
}
bool TagEditor::eventFilter(QObject *object, QEvent *event) {
    if (object == m_custom && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            addDraft();
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}
void TagEditor::rebuildChips() {
    delete m_chips->layout();
    qDeleteAll(m_chips->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly));
    auto *grid = new QGridLayout(m_chips);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setAlignment(Qt::AlignLeft);
    int n = 0;
    for (const auto &tag : m_tags) {
        auto *chip = new QPushButton(tag + "  ×");
        chip->setAutoDefault(false);
        chip->setAccessibleName("Remove tag " + tag);
        chip->setToolTip(chip->accessibleName());
        connect(chip, &QPushButton::clicked, this, [this, tag] {
            m_tags.removeAll(tag);
            rebuildChips();
            emit tagsChanged();
        });
        grid->addWidget(chip, n, 0);
        ++n;
    }
    m_chips->setVisible(!m_tags.isEmpty());
}
CanvasPreview::CanvasPreview(QWidget *parent) : QWidget(parent), m_color(Qt::white) {
    setMinimumSize(260, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName("Blank canvas preview");
}
void CanvasPreview::setCanvas(int width, int height, const QColor &color, bool transparent) {
    m_width = width;
    m_height = height;
    m_color = color;
    m_transparent = transparent;
    setAccessibleDescription(QString("%1 by %2 pixels, %3")
                                 .arg(width)
                                 .arg(height)
                                 .arg(transparent ? "transparent" : color.name()));
    update();
}
void CanvasPreview::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor(ui::border));
    p.setBrush(QColor(ui::raised));
    p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 5, 5);
    QRect well = rect().adjusted(16, 24, -16, -54);
    auto size = QSize(qMax(1, m_width), qMax(1, m_height)).scaled(well.size(), Qt::KeepAspectRatio);
    QRect canvas(well.center() - QPoint(size.width() / 2, size.height() / 2), size);
    if (m_transparent) {
        p.setClipRect(canvas);
        p.fillRect(canvas, QColor("#d4d4d4"));
        for (int y = canvas.top(); y < canvas.bottom(); y += 12)
            for (int x = canvas.left(); x < canvas.right(); x += 12)
                if (((x - canvas.left()) / 12 + (y - canvas.top()) / 12) % 2)
                    p.fillRect(x, y, 12, 12, QColor("#999999"));
        p.setClipping(false);
    } else
        p.fillRect(canvas, m_color);
    p.setPen(QColor(ui::muted));
    p.drawText(QRect(0, height() - 40, width(), 24), Qt::AlignCenter,
               QString("%1 × %2 px").arg(m_width).arg(m_height));
}
} // namespace toon
