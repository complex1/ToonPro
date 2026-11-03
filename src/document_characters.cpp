#include "document.h"
#include <QUuid>
#include <new>

namespace toon {
namespace {
bool fail(QString *error, const QString &message) { if (error) *error = message; return false; }
int characterIndex(const Project &project, const QString &id) {
    for (int i = 0; i < project.characters.size(); ++i) if (project.characters[i].id == id) return i;
    return -1;
}
int sheetIndex(const CharacterModel &character, const QString &id) {
    for (int i = 0; i < character.sheets.size(); ++i) if (character.sheets[i].id == id) return i;
    return -1;
}
QString freshId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
}

bool Document::characterOperationReady(QString *error) const {
    if (error) error->clear();
    if (!m_open) return fail(error, "Open a project before editing the character library.");
    if (editingGestureActive() || layerPropertyEditActive())
        return fail(error, "Finish the current edit before changing the character library.");
    return true;
}
bool Document::commitCharacterChange(const Project &project, const QString &label, QString *error) {
    const auto validation = project.validate();
    if (!validation.isEmpty()) return fail(error, validation);
    if (!commit(m_project, project, label)) return fail(error, "Could not update the character library.");
    return true;
}
QString Document::addCharacter(const QString &name, QString *error) {
    if (!characterOperationReady(error)) return {};
    auto project = m_project;
    const auto id = freshId();
    project.characters.append({id, name.trimmed(), {}, {}, standardCharacterColorVariants({})});
    return commitCharacterChange(project, "Add character", error) ? id : QString{};
}
bool Document::renameCharacter(const QString &id, const QString &name, QString *error) {
    if (!characterOperationReady(error)) return false;
    const int index = characterIndex(m_project, id);
    if (index < 0) return fail(error, "Select a character to rename.");
    const auto trimmed = name.trimmed();
    if (m_project.characters[index].name == trimmed) return true;
    auto project = m_project;
    project.characters[index].name = trimmed;
    return commitCharacterChange(project, "Rename character", error);
}
bool Document::removeCharacter(const QString &id, QString *error) {
    if (!characterOperationReady(error)) return false;
    const int index = characterIndex(m_project, id);
    if (index < 0) return fail(error, "Select a character to remove.");
    auto project = m_project;
    project.characters.removeAt(index);
    return commitCharacterChange(project, "Remove character", error);
}
bool Document::addCharacterSheets(const QString &characterId, const QVector<CharacterModelSheet> &sheets, QString *error) {
    if (!characterOperationReady(error)) return false;
    const int index = characterIndex(m_project, characterId);
    if (index < 0) return fail(error, "Select a character before importing model sheets.");
    if (sheets.isEmpty()) return fail(error, "Choose at least one model sheet image.");
    qsizetype count = sheets.size();
    qint64 pixels = 0;
    for (const auto &character : m_project.characters) {
        count += character.sheets.size();
        for (const auto &sheet : character.sheets) pixels += qint64(sheet.image.width()) * sheet.image.height();
    }
    if (count > maxProjectCharacterSheets) return fail(error, "A project supports up to 256 character model sheets.");
    for (const auto &sheet : sheets) {
        if (sheet.image.isNull() || sheet.image.width() > maxCharacterSheetDimension || sheet.image.height() > maxCharacterSheetDimension)
            return fail(error, "Character model sheets must contain valid pixels and measure at most 4096 pixels per side.");
        pixels += qint64(sheet.image.width()) * sheet.image.height();
        if (pixels > maxProjectCharacterSheetPixels) return fail(error, "Character model sheets exceed the 64 megapixel project limit.");
    }
    try {
        auto project = m_project;
        auto &destination = project.characters[index].sheets;
        for (const auto &sheet : sheets) {
            const auto image = sheet.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            if (image.isNull()) return fail(error, "There is not enough memory to import the model sheets.");
            destination.append({freshId(), sheet.name.trimmed(), sheet.category, image});
        }
        return commitCharacterChange(project, sheets.size() == 1 ? "Add character model sheet" : "Add character model sheets", error);
    } catch (const std::bad_alloc &) {
        return fail(error, "There is not enough memory to import the model sheets.");
    }
}
bool Document::renameCharacterSheet(const QString &characterId, const QString &sheetId, const QString &name, QString *error) {
    if (!characterOperationReady(error)) return false;
    const int character = characterIndex(m_project, characterId);
    if (character < 0) return fail(error, "Select a character model sheet to rename.");
    const int sheet = sheetIndex(m_project.characters[character], sheetId);
    if (sheet < 0) return fail(error, "Select a character model sheet to rename.");
    const auto trimmed = name.trimmed();
    if (m_project.characters[character].sheets[sheet].name == trimmed) return true;
    auto project = m_project;
    project.characters[character].sheets[sheet].name = trimmed;
    return commitCharacterChange(project, "Rename character model sheet", error);
}
bool Document::setCharacterSheetCategory(const QString &characterId, const QString &sheetId, ModelSheetCategory category, QString *error) {
    if (!characterOperationReady(error)) return false;
    const int character = characterIndex(m_project, characterId);
    if (character < 0) return fail(error, "Select a character model sheet to move.");
    const int sheet = sheetIndex(m_project.characters[character], sheetId);
    if (sheet < 0) return fail(error, "Select a character model sheet to move.");
    if (m_project.characters[character].sheets[sheet].category == category) return true;
    auto project = m_project;
    project.characters[character].sheets[sheet].category = category;
    return commitCharacterChange(project, "Move character model sheet", error);
}
bool Document::removeCharacterSheet(const QString &characterId, const QString &sheetId, QString *error) {
    if (!characterOperationReady(error)) return false;
    const int character = characterIndex(m_project, characterId);
    if (character < 0) return fail(error, "Select a character model sheet to remove.");
    const int sheet = sheetIndex(m_project.characters[character], sheetId);
    if (sheet < 0) return fail(error, "Select a character model sheet to remove.");
    auto project = m_project;
    project.characters[character].sheets.removeAt(sheet);
    return commitCharacterChange(project, "Remove character model sheet", error);
}
} // namespace toon
