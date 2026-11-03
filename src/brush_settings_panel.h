#pragma once
#include "brush_settings.h"
#include <QPointer>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <vector>

class QComboBox;
class QDialog;
class QListWidget;
class QTabBar;
class QFormLayout;
class QStackedWidget;
class QVBoxLayout;

namespace toon {
class BrushLibrary;
class Canvas;
class ValueSlider;

class BrushSettingsPanel : public QWidget {
    Q_OBJECT
public:
    explicit BrushSettingsPanel(Canvas *canvas, BrushLibrary *library = nullptr, QWidget *parent = nullptr);
    void setLibrary(BrushLibrary *library);
    bool importTexture(const QString &path, QString *error = nullptr);
public slots:
    void openAdvancedSettings();
private:
    using Reader = std::function<qreal(const BrushSettings &)>;
    using Writer = std::function<void(BrushSettings &, qreal)>;
    using Edit = std::function<void(BrushSettings &)>;
    QFormLayout *category(const QString &name);
    QWidget *number(QFormLayout *form, const QString &label, const char *id,
                        qreal minimum, qreal maximum, Reader read, Writer write,
                        qreal scale = 1, int decimals = 0, const QString &suffix = {});
    void check(QFormLayout *form, const QString &label, const char *id, Reader read, Writer write);
    QComboBox *choice(QFormLayout *form, const QString &label, const char *id,
                      const QStringList &labels, Reader read, Writer write);
    QComboBox *inputControl(QFormLayout *form, const QString &label, const char *id,
                           Reader read, Writer write, bool direction = false);
    void dynamic(QFormLayout *form, const QString &label, const char *id,
                 std::function<BrushDynamic &(BrushSettings &)> field,
                 bool minimum = true, bool direction = false, bool framed = true);
    void mutate(const Edit &edit);
    void refresh();
    void refreshTips();
    void chooseColor(bool background);
    Canvas *m_canvas;
    QPointer<BrushLibrary> m_library;
    QDialog *m_advancedDialog = nullptr;
    QTabBar *m_category;
    QStackedWidget *m_pages;
    QComboBox *m_tipPicker = nullptr;
    QComboBox *m_dualPicker = nullptr;
    QListWidget *m_patternPicker = nullptr;
    std::vector<std::function<void()>> m_refreshers;
    std::vector<std::function<void()>> m_quickRefreshers;
    bool m_advancedInitialized = false;
    bool m_refreshing = false;
};
} // namespace toon
