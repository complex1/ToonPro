#pragma once

#include "project.h"
#include <QHash>
#include <QStringList>
#include <QWidget>

class QComboBox;
class QAction;
class QLineEdit;
class QListWidget;
class QTabBar;
class QToolButton;

namespace toon {
class Document;

class CharacterColorPanel final : public QWidget {
    Q_OBJECT
public:
    explicit CharacterColorPanel(Document *document, QWidget *parent = nullptr);
    void setCurrentColor(const QColor &color);
    QColor currentColor() const { return m_currentColor; }
    QString selectedCharacterId() const { return m_characterId; }
    void selectCharacter(const QString &id);
    QString selectedVariantId() const { return m_variantId; }
    void selectVariant(const QString &id);
    CharacterColorCategory selectedCategory() const;
    void selectCategory(CharacterColorCategory category);
    // Resolve a pending name before Save or leaving the document. A rejected
    // draft stays available for correction and prevents the caller proceeding.
    bool finishEditing();
signals:
    void colorSelected(QColor color);
    void characterSelected(QString id);
    void paletteSelected(QString characterId, QString variantId);
    void recolorRequested(QString characterId, QString variantId);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    const CharacterModel *character() const;
    const CharacterColorSwatch *swatch() const;
    QString selectionKey() const;
    void refresh();
    void refreshVariants();
    void refreshColors();
    void refreshActions();
    void selectColor();
    void useSelectedColor();
    void resetName();
    bool commitName();
    void addColor();
    void replaceColor();
    void editColor();
    void removeColor();
    void showColorMenu(const QPoint &point);
    void duplicateVariant();
    void renameVariant();
    void removeVariant();
    void addStandardVariants();
    bool ready();
    void reportError(const QString &error);
    Document *m_document;
    QComboBox *m_characters;
    QTabBar *m_categories;
    QTabBar *m_variants;
    QListWidget *m_colors;
    QLineEdit *m_name;
    QToolButton *m_addCharacter, *m_renameCharacter, *m_removeCharacter;
    QToolButton *m_add, *m_update, *m_edit, *m_remove, *m_recolor;
    QToolButton *m_addVariant, *m_variantMore;
    QAction *m_duplicateVariant, *m_renameVariant, *m_removeVariant, *m_standardVariants;
    QColor m_currentColor = QColor("#202530");
    QString m_characterId, m_colorId, m_variantId;
    QStringList m_characterState, m_colorState, m_variantState;
    QVector<CharacterColorSwatch> m_paletteColors;
    QHash<QString, QString> m_selection, m_variantSelections;
    CharacterColorCategory m_category = CharacterColorCategory::Skin;
    bool m_refreshing = false;
    bool m_committingName = false;
};
} // namespace toon
