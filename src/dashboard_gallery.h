#pragma once
#include <QListWidget>
namespace toon {
// Standard item-view selection/accessibility, with a shared card/row delegate.
class DashboardGallery final : public QListWidget {
    Q_OBJECT
  public:
    enum Role {
        SubtitleRole = Qt::UserRole + 1,
        TagsRole,
        KindRole,
        CoverRole,
        CoverKeyRole,
        TimestampRole,
        DurationRole,
        RateRole,
        FolderRole,
        PinnedRole
    };
    explicit DashboardGallery(QWidget *parent = nullptr);
    void setCards(bool cards);
    bool cards() const { return m_cards; }
    void setAnimationRows(bool enabled);
    void relayout();
  signals:
    void moreRequested(const QPoint &position);
    void visibleItemsChanged();

  protected:
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

  private:
    bool m_cards = true, m_animationRows = false;
};
} // namespace toon
