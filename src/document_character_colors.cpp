#include "document.h"
#include <QUuid>
#include <QSet>
#include <cmath>

namespace toon {
namespace {
bool fail(QString *error, const QString &message) { if (error) *error = message; return false; }
int characterIndex(const Project &project, const QString &id) {
    for (int index = 0; index < project.characters.size(); ++index)
        if (project.characters[index].id == id) return index;
    return -1;
}
int colorIndex(const CharacterModel &character, const QString &id) {
    for (int index = 0; index < character.colors.size(); ++index)
        if (character.colors[index].id == id) return index;
    return -1;
}
int variantIndex(const CharacterModel &character, const QString &id) {
    for (int index = 0; index < character.colorVariants.size(); ++index)
        if (character.colorVariants[index].id == id) return index;
    return -1;
}
std::optional<QColor> normalizedColor(const QColor &color) {
    if (!color.isValid()) return {};
    for (const qreal channel : {color.redF(), color.greenF(), color.blueF(), color.alphaF()})
        if (!std::isfinite(channel) || channel < 0 || channel > 1) return {};
    return QColor::fromRgba(color.rgba());
}
}

QString Document::addCharacterColor(const QString &characterId, const QString &name,
                                   CharacterColorCategory category, const QColor &color, QString *error) {
    if (!characterOperationReady(error)) return {};
    const int character = characterIndex(m_project, characterId);
    if (character < 0) { fail(error, "Select a character before adding a color."); return {}; }
    const auto rgba = normalizedColor(color);
    if (!rgba) { fail(error, "Choose a valid RGBA color."); return {}; }
    if (m_project.characters[character].colors.size() >= maxCharacterColors) {
        fail(error, "A character supports up to 256 color swatches."); return {};
    }
    qsizetype count = 0;
    for (const auto &entry : m_project.characters) count += entry.colors.size();
    if (count >= maxProjectCharacterColors) { fail(error, "A project supports up to 8192 character color swatches."); return {}; }
    auto project = m_project;
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    project.characters[character].colors.append({id, name.trimmed(), category, *rgba});
    for (auto &variant : project.characters[character].colorVariants) variant.colors.insert(id, *rgba);
    return commitCharacterChange(project, "Add character color", error) ? id : QString{};
}

bool Document::updateCharacterColor(const QString &characterId, const QString &colorId, const QString &name,
                                    CharacterColorCategory category, const QColor &color, QString *error, const QString &variantId) {
    if (!characterOperationReady(error)) return false;
    const int character = characterIndex(m_project, characterId);
    if (character < 0) return fail(error, "Select a character color to edit.");
    const int swatch = colorIndex(m_project.characters[character], colorId);
    if (swatch < 0) return fail(error, "Select a character color to edit.");
    const int variant = variantIndex(m_project.characters[character], variantId);
    if (!variantId.isEmpty() && variant < 0) return fail(error, "The selected color variant is no longer available.");
    const auto rgba = normalizedColor(color);
    if (!rgba) return fail(error, "Choose a valid RGBA color.");
    const auto &current = m_project.characters[character].colors[swatch];
    const CharacterColorSwatch updated{colorId, name.trimmed(), category, variantId.isEmpty() ? *rgba : current.color};
    if (current == updated && (variantId.isEmpty() || m_project.characters[character].colorVariants[variant].colors.value(colorId) == *rgba)) return true;
    auto project = m_project;
    project.characters[character].colors[swatch] = updated;
    if (variant >= 0) project.characters[character].colorVariants[variant].colors[colorId] = *rgba;
    return commitCharacterChange(project, "Edit character color", error);
}

bool Document::removeCharacterColor(const QString &characterId, const QString &colorId, QString *error) {
    if (!characterOperationReady(error)) return false;
    const int character = characterIndex(m_project, characterId);
    if (character < 0) return fail(error, "Select a character color to remove.");
    const int swatch = colorIndex(m_project.characters[character], colorId);
    if (swatch < 0) return fail(error, "Select a character color to remove.");
    auto project = m_project;
    project.characters[character].colors.removeAt(swatch);
    for (auto &variant : project.characters[character].colorVariants) variant.colors.remove(colorId);
    return commitCharacterChange(project, "Remove character color", error);
}

QString Document::addCharacterColorVariant(const QString &characterId, const QString &name,
                                           const QString &sourceVariantId, QString *error) {
    if (!characterOperationReady(error)) return {};
    const int character = characterIndex(m_project, characterId);
    if (character < 0) { fail(error, "Select a character before adding a color variant."); return {}; }
    const auto &source = m_project.characters[character];
    if (!sourceVariantId.isEmpty() && variantIndex(source, sourceVariantId) < 0) {
        fail(error, "The source color variant is no longer available."); return {};
    }
    if (source.colorVariants.size() >= maxCharacterColorVariants) {
        fail(error, "A character supports up to 16 lighting variants in addition to Day."); return {};
    }
    CharacterColorVariant variant{QUuid::createUuid().toString(QUuid::WithoutBraces), name.trimmed(), {}};
    for (const auto &swatch : characterVariantColors(source, sourceVariantId)) variant.colors.insert(swatch.id, swatch.color);
    auto project = m_project; project.characters[character].colorVariants.append(variant);
    return commitCharacterChange(project, "Add character color variant", error) ? variant.id : QString{};
}

bool Document::renameCharacterColorVariant(const QString &characterId, const QString &variantId,
                                           const QString &name, QString *error) {
    if (!characterOperationReady(error)) return false;
    const int character = characterIndex(m_project, characterId);
    if (character < 0) return fail(error, "Select a character color variant to rename.");
    const int variant = variantIndex(m_project.characters[character], variantId);
    if (variant < 0) return fail(error, "Select a lighting variant to rename; Day is the base palette.");
    if (m_project.characters[character].colorVariants[variant].name == name.trimmed()) return true;
    auto project = m_project; project.characters[character].colorVariants[variant].name = name.trimmed();
    return commitCharacterChange(project, "Rename character color variant", error);
}

bool Document::removeCharacterColorVariant(const QString &characterId, const QString &variantId, QString *error) {
    if (!characterOperationReady(error)) return false;
    const int character = characterIndex(m_project, characterId);
    if (character < 0) return fail(error, "Select a character color variant to remove.");
    const int variant = variantIndex(m_project.characters[character], variantId);
    if (variant < 0) return fail(error, "Select a lighting variant to remove; Day is the base palette.");
    auto project = m_project; project.characters[character].colorVariants.removeAt(variant);
    return commitCharacterChange(project, "Remove character color variant", error);
}

bool Document::addStandardCharacterColorVariants(const QString &characterId, QString *error) {
    if (!characterOperationReady(error)) return false;
    const int character = characterIndex(m_project, characterId);
    if (character < 0) return fail(error, "Select a character before adding lighting variants.");
    auto project = m_project; auto &entry = project.characters[character];
    QSet<QString> names;
    for (const auto &variant : entry.colorVariants) names.insert(variant.name.toCaseFolded());
    const auto previousCount = entry.colorVariants.size();
    for (const auto &variant : standardCharacterColorVariants(entry.colors))
        if (!names.contains(variant.name.toCaseFolded())) entry.colorVariants.append(variant);
    if (entry.colorVariants.size() == previousCount) return true;
    return commitCharacterChange(project, "Add standard lighting variants", error);
}
} // namespace toon
