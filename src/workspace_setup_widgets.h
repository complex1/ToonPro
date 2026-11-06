#pragma once
#include <QColor>
#include <QDialog>
#include <QTabBar>
#include <QWidget>
class QLabel;
class QCheckBox;
class QLineEdit;
class QVBoxLayout;
class QHBoxLayout;
class QDialogButtonBox;
namespace toon {
class SetupDialog : public QDialog {
  public:
    SetupDialog(const QString &title, const QString &context, QWidget *parent);
    QVBoxLayout *content;
    QHBoxLayout *footer;
    QLabel *error;

  protected:
    void paintEvent(QPaintEvent *) override;
};
int execSetupDialog(QDialog &dialog);
class ProjectTypeCards final : public QTabBar {
  public:
    explicit ProjectTypeCards(QWidget *parent = nullptr);

  protected:
    QSize tabSizeHint(int index) const override;
    void paintEvent(QPaintEvent *) override;
};
class TagEditor final : public QWidget {
    Q_OBJECT
  public:
    explicit TagEditor(const QStringList &tags = {}, QWidget *parent = nullptr, bool presets = true);
    QStringList tags() const;
    void setTags(const QStringList &tags);
  signals:
    void tagsChanged();

  protected:
    bool eventFilter(QObject *, QEvent *) override;

  private:
    void addDraft();
    void rebuildChips();
    QList<QCheckBox *> m_presets;
    QLineEdit *m_custom;
    QWidget *m_chips;
    QStringList m_tags;
};
class CanvasPreview final : public QWidget {
  public:
    explicit CanvasPreview(QWidget *parent = nullptr);
    void setCanvas(int width, int height, const QColor &color, bool transparent);

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    int m_width = 1920, m_height = 1080;
    QColor m_color;
    bool m_transparent = false;
};
QString managementStyle();
} // namespace toon
