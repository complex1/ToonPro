#include "dashboard_gallery.h"
#include "icons.h"
#include "ui_theme.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTimer>
namespace toon {
namespace {
class CardDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        auto *gallery = qobject_cast<const DashboardGallery *>(parent());
        if (!gallery)
            return;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setRenderHint(QPainter::SmoothPixmapTransform);
        const bool cards = gallery->cards(), selected = option.state & QStyle::State_Selected;
        QRect r = option.rect.adjusted(4, 4, -12, cards ? -16 : -4);
        p->setBrush(QColor(selected ? ui::selected : ui::raised));
        p->setPen(QPen(QColor(selected ? ui::focus : ui::border), selected ? 1.5 : 1));
        p->drawRoundedRect(r, 6, 6);
        const bool folder = index.data(DashboardGallery::FolderRole).toBool();
        const int thumbnailHeight = cards ? qRound(r.width() * 9. / 16.) : r.height() - 16;
        QRect thumb = cards ? QRect(r.x() + 1, r.y() + 1, r.width() - 2, thumbnailHeight)
                            : QRect(r.x() + 10, r.y() + 8, 150, thumbnailHeight);
        if (!cards && r.width() < 700)
            thumb.setWidth(110);
        auto cover = qvariant_cast<QPixmap>(index.data(DashboardGallery::CoverRole));
        if (!cover.isNull()) {
            QPainterPath clip;
            clip.addRoundedRect(thumb, 5, 5);
            p->save();
            p->setClipPath(clip);
            const auto scaled = cover.size().scaled(thumb.size(), Qt::KeepAspectRatioByExpanding);
            QRect target(
                QPoint(thumb.center().x() - scaled.width() / 2, thumb.center().y() - scaled.height() / 2),
                scaled);
            p->drawPixmap(target, cover);
            p->restore();
        } else {
            p->fillRect(thumb, QColor(ui::panel));
            const auto kind = index.data(DashboardGallery::KindRole).toString();
            const auto icon = folder                                   ? "folder"
                              : kind == "audio"                        ? "music"
                              : kind == "video" || kind == "animation" ? "clapperboard"
                                                                       : "image";
            const int size = cards ? 56 : 36;
            appIcon(icon).paint(p, QRect(thumb.center() - QPoint(size / 2, size / 2), QSize(size, size)));
        }
        const int left = cards ? r.left() + 16 : thumb.right() + 20;
        const int y = cards ? thumb.bottom() + 12 : r.top() + 22;
        const bool pinned = index.data(DashboardGallery::PinnedRole).toBool();
        const int textWidth =
            cards ? r.width() - (folder ? 32 : 60 + (pinned ? 29 : 0)) : qMax(60, r.right() - left - 240);
        QFont title = option.font;
        title.setPointSizeF(qMax(12., option.font.pointSizeF() + 1));
        title.setWeight(QFont::DemiBold);
        p->setFont(title);
        p->setPen(QColor(ui::text));
        p->drawText(QRect(left, y, textWidth, 26), Qt::AlignVCenter,
                    p->fontMetrics().elidedText(index.data().toString(), Qt::ElideRight, textWidth));
        p->setFont(option.font);
        p->setPen(QColor(ui::muted));
        p->drawText(QRect(left, y + 29, cards ? r.width() - 32 : textWidth, 22), Qt::AlignVCenter,
                    p->fontMetrics().elidedText(index.data(DashboardGallery::SubtitleRole).toString(),
                                                Qt::ElideRight, cards ? r.width() - 32 : textWidth));
        if (!cards) {
            p->setPen(QColor(ui::text));
            p->drawText(QRect(r.right() - 225, r.top(), 90, r.height()), Qt::AlignCenter,
                        index.data(DashboardGallery::DurationRole).toString());
            p->drawText(QRect(r.right() - 130, r.top(), 82, r.height()), Qt::AlignCenter,
                        index.data(DashboardGallery::RateRole).toString());
        }
        auto tags = index.data(DashboardGallery::TagsRole).toStringList();
        int x = left;
        for (const auto &tag : tags) {
            int width = p->fontMetrics().horizontalAdvance(tag) + 16;
            if (x + width > r.right() - 12)
                break;
            QRect chip(x, y + 56, width, 23);
            p->setPen(QColor(ui::border));
            p->setBrush(QColor(ui::selected));
            p->drawRoundedRect(chip, 3, 3);
            p->setPen(QColor(ui::text));
            p->drawText(chip, Qt::AlignCenter, tag);
            x += width + 6;
        }
        const QRect more(r.right() - 34, cards ? y - 1 : r.center().y() - 14, 28, 28);
        if (!folder) {
            p->setBrush(QColor(ui::text));
            p->setPen(Qt::NoPen);
            for (int n = 0; n < 3; ++n)
                p->drawEllipse(QPointF(more.center().x() - 6 + n * 6, more.center().y()), 1.5, 1.5);
        }
        if (pinned)
            appIcon("pin").paint(p, QRect(more.left() - 29, more.top() + 5, 18, 18));
        if (option.state & QStyle::State_HasFocus) {
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(QColor(ui::focus), 1, Qt::DashLine));
            p->drawRoundedRect(r.adjusted(3, 3, -3, -3), 4, 4);
        }
        p->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override {
        auto *gallery = qobject_cast<const DashboardGallery *>(parent());
        if (!gallery || !gallery->cards())
            return {option.rect.width(), 112};
        return gallery->gridSize();
    }
};
} // namespace
DashboardGallery::DashboardGallery(QWidget *parent) : QListWidget(parent) {
    setItemDelegate(new CardDelegate(this));
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setFrameShape(QFrame::NoFrame);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setUniformItemSizes(true);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setSpacing(0);
    setAccessibleDescription("Double-click to open. Ctrl or Command-click and Shift-click select "
                             "multiple items. Shift+F10 opens actions for the selected item.");
    setStyleSheet(QString("QListWidget {background:%1;border:0;outline:0;} "
                          "QListWidget::item {background:transparent;border:0;}")
                      .arg(ui::base));
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { emit visibleItemsChanged(); });
    setCards(true);
}
void DashboardGallery::setCards(bool cards) {
    m_cards = cards;
    setViewMode(cards ? QListView::IconMode : QListView::ListMode);
    setFlow(cards ? QListView::LeftToRight : QListView::TopToBottom);
    setWrapping(cards);
    setWordWrap(false);
    relayout();
}
void DashboardGallery::setAnimationRows(bool enabled) {
    m_animationRows = enabled;
    setCards(!enabled);
}
void DashboardGallery::relayout() {
    if (m_cards) {
        // Keep cards compact, including sparse galleries. Wider windows show
        // more columns instead of stretching a few cards across the page.
        const int width = qMin(220, qMax(1, viewport()->width() - 24));
        setGridSize({width, qRound((width - 16) * 9. / 16.) + 118});
    } else
        setGridSize({});
    doItemsLayout();
    viewport()->update();
    QTimer::singleShot(0, this, [this] { emit visibleItemsChanged(); });
}
void DashboardGallery::resizeEvent(QResizeEvent *e) {
    QListWidget::resizeEvent(e);
    relayout();
}
void DashboardGallery::mousePressEvent(QMouseEvent *e) {
    auto *item = itemAt(e->position().toPoint());
    if (item && !item->data(FolderRole).toBool() && e->button() == Qt::LeftButton) {
        auto r = visualItemRect(item).adjusted(4, 4, -12, cards() ? -16 : -4);
        const int y = cards() ? r.y() + qRound(r.width() * 9. / 16.) + 11 : r.center().y() - 14;
        QRect more(r.right() - 34, y, 28, 28);
        if (more.contains(e->position().toPoint())) {
            if (!item->isSelected()) {
                clearSelection();
                item->setSelected(true);
            }
            setCurrentItem(item, QItemSelectionModel::NoUpdate);
            emit moreRequested(e->position().toPoint());
            return;
        }
    }
    QListWidget::mousePressEvent(e);
}
void DashboardGallery::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Menu || (e->key() == Qt::Key_F10 && e->modifiers() == Qt::ShiftModifier)) {
        if (currentItem())
            emit moreRequested(visualItemRect(currentItem()).center());
        e->accept();
        return;
    }
    QListWidget::keyPressEvent(e);
}
} // namespace toon
