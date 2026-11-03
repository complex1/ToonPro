#pragma once

#include "project.h"
#include <QHash>
#include <QStringList>
#include <QWidget>

class QComboBox;
class QLineEdit;
class QListWidget;
class QTabBar;
class QToolButton;

namespace toon {
class Document;
class ReferenceView;
class ValueSlider;

// A project-wide collection. Selecting an image never attaches it to a shot or
// changes artwork; all library edits use the document's ordinary undo history.
class CharacterModelPanel final : public QWidget {
    Q_OBJECT
public:
    explicit CharacterModelPanel(Document *document, QWidget *parent = nullptr);
    bool importImages(const QStringList &paths, QString *error = nullptr);
    void setSampling(bool enabled);
    QString selectedCharacterId() const { return m_characterId; }
    ModelSheetCategory selectedCategory() const;
    void selectCharacter(const QString &id);
    void selectCategory(ModelSheetCategory category);
    bool finishEditing();
signals:
    void colorSampled(QColor color);
    void characterSelected(const QString &id);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    const CharacterModel *character() const;
    const CharacterModelSheet *sheet() const;
    QString selectionKey() const;
    void refresh();
    void refreshSheets();
    void selectSheet();
    bool commitSheetName();
    void resetSheetName();
    void showSheetMenu(const QPoint &position);
    void showSheetActions(const QPoint &globalPosition);
    void reportError(const QString &error);
    Document *m_document;
    QComboBox *m_characters;
    QTabBar *m_categories;
    QListWidget *m_sheets;
    QLineEdit *m_name;
    ReferenceView *m_view;
    ValueSlider *m_zoom;
    QToolButton *m_addCharacter, *m_renameCharacter, *m_removeCharacter;
    QToolButton *m_import, *m_removeSheet, *m_fit;
    QString m_characterId, m_sheetId;
    QStringList m_characterState, m_sheetState;
    QHash<QString, QString> m_selection;
    bool m_zoomEditing = false, m_refreshing = false;
};
} // namespace toon
