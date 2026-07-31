#include "UserTheme.hpp"

#include <BinaryData.h>

#include <algorithm>

#include "ThemeSerialization.hpp"
#include "core/AppPaths.hpp"

namespace magda {

namespace {

std::string resolveBaseId(const juce::var& baseVar, std::vector<std::string>& warnings) {
    if (!baseVar.isString())
        return ThemeManager::kDarkThemeId;

    const auto raw = baseVar.toString().trim().toLowerCase();
    if (raw == "light")
        return ThemeManager::kLightThemeId;
    if (raw == "dark")
        return ThemeManager::kDarkThemeId;

    warnings.push_back("Unknown base \"" + baseVar.toString().toStdString() +
                       "\"; inheriting from dark");
    return ThemeManager::kDarkThemeId;
}

// Applies one { key: "#colour" } object over a palette, resolving keys through
// the shared name table and reporting anything it cannot use. Templated so the
// colour and syntax maps share exactly one code path.
template <typename PaletteT, typename FindFn>
void applyColourMap(const juce::DynamicObject& source, PaletteT& palette, FindFn&& find,
                    const char* roleKind, std::vector<std::string>& warnings) {
    for (const auto& entry : source.getProperties()) {
        const auto key = entry.name.toString();
        const auto role = find(key);
        if (!role) {
            warnings.push_back(std::string("Unknown ") + roleKind + " role \"" + key.toStdString() +
                               "\"");
            continue;
        }
        const auto colour = parseColourString(entry.value.toString());
        if (!colour) {
            warnings.push_back("Invalid colour for \"" + key.toStdString() +
                               "\": " + entry.value.toString().toStdString());
            continue;
        }
        palette[static_cast<std::size_t>(*role)] = colour->getARGB();
    }
}

// One parse path for file-based and embedded themes; only the source of the
// JSON text and the id differ.
std::optional<LoadedTheme> loadThemeFromText(const juce::String& text, std::string id) {
    juce::var parsed;
    const auto result = juce::JSON::parse(text, parsed);
    auto* obj = parsed.getDynamicObject();
    if (result.failed() || obj == nullptr)
        return std::nullopt;

    LoadedTheme theme;
    theme.id = std::move(id);

    if (obj->hasProperty("schemaVersion")) {
        const int version = static_cast<int>(obj->getProperty("schemaVersion"));
        if (version > kThemeSchemaVersion)
            theme.warnings.push_back(
                "Theme schemaVersion " + std::to_string(version) + " is newer than supported (" +
                std::to_string(kThemeSchemaVersion) + "); unrecognised keys are ignored");
    }

    theme.base = resolveBaseId(obj->getProperty("base"), theme.warnings);
    theme.palette = ThemeManager::builtInPalette(theme.base);
    theme.syntaxPalette = ThemeManager::builtInSyntaxPalette(theme.base);

    if (obj->hasProperty("name")) {
        const auto name = obj->getProperty("name").toString().trim();
        if (name.isNotEmpty())
            theme.name = name.toStdString();
    }
    if (theme.name.empty())
        theme.name = theme.id;

    if (auto* colours = obj->getProperty("colours").getDynamicObject())
        applyColourMap(*colours, theme.palette, &findColourRole, "colour", theme.warnings);

    if (auto* syntax = obj->getProperty("syntaxColours").getDynamicObject())
        applyColourMap(*syntax, theme.syntaxPalette, &findSyntaxColourRole, "syntax",
                       theme.warnings);

    return theme;
}

// Embedded factory themes. The id is the persisted identifier and follows the
// built-in kebab style; display names come from the JSON payloads.
struct FactoryThemeAsset {
    const char* id;
    const char* data;
    int size;
};

const FactoryThemeAsset kFactoryThemeAssets[] = {
    {"concrete-warehouse", BinaryData::concrete_warehouse_json,
     BinaryData::concrete_warehouse_jsonSize},
    {"neon-cyberpunk", BinaryData::neon_cyberpunk_json, BinaryData::neon_cyberpunk_jsonSize},
};

}  // namespace

std::optional<LoadedTheme> loadThemeFile(const juce::File& file) {
    if (!file.existsAsFile())
        return std::nullopt;

    return loadThemeFromText(file.loadFileAsString(),
                             file.getFileNameWithoutExtension().toStdString());
}

const std::vector<FactoryThemeEntry>& factoryThemes() {
    static const std::vector<FactoryThemeEntry> entries = [] {
        std::vector<FactoryThemeEntry> list;
        for (const auto& asset : kFactoryThemeAssets) {
            FactoryThemeEntry entry;
            entry.id = asset.id;
            entry.name = asset.id;
            if (auto loaded =
                    loadThemeFromText(juce::String::fromUTF8(asset.data, asset.size), asset.id))
                entry.name = loaded->name;
            list.push_back(std::move(entry));
        }
        std::sort(list.begin(), list.end(),
                  [](const FactoryThemeEntry& a, const FactoryThemeEntry& b) {
                      return juce::String(a.name).compareIgnoreCase(juce::String(b.name)) < 0;
                  });
        return list;
    }();
    return entries;
}

std::optional<LoadedTheme> loadFactoryTheme(const std::string& id) {
    for (const auto& asset : kFactoryThemeAssets)
        if (id == asset.id)
            return loadThemeFromText(juce::String::fromUTF8(asset.data, asset.size), asset.id);

    return std::nullopt;
}

std::vector<ThemeFileEntry> scanUserThemes() {
    std::vector<ThemeFileEntry> entries;

    const auto dir = paths::themesDir();
    if (!dir.isDirectory())
        return entries;

    for (const auto& file : dir.findChildFiles(juce::File::findFiles, false, "*.json")) {
        ThemeFileEntry entry;
        entry.id = file.getFileNameWithoutExtension().toStdString();
        entry.file = file;

        juce::var parsed;
        if (juce::JSON::parse(file.loadFileAsString(), parsed).wasOk())
            if (auto* obj = parsed.getDynamicObject(); obj != nullptr && obj->hasProperty("name")) {
                const auto name = obj->getProperty("name").toString().trim();
                if (name.isNotEmpty())
                    entry.name = name.toStdString();
            }
        if (entry.name.empty())
            entry.name = entry.id;

        entries.push_back(std::move(entry));
    }

    std::sort(entries.begin(), entries.end(), [](const ThemeFileEntry& a, const ThemeFileEntry& b) {
        return juce::String(a.name).compareIgnoreCase(juce::String(b.name)) < 0;
    });
    return entries;
}

bool writeThemeTemplate(const juce::File& dest, const std::string& baseId,
                        const std::string& displayName) {
    const std::string resolvedBase = (baseId == ThemeManager::kLightThemeId)
                                         ? ThemeManager::kLightThemeId
                                         : ThemeManager::kDarkThemeId;
    const auto& palette = ThemeManager::builtInPalette(resolvedBase);
    const auto& syntax = ThemeManager::builtInSyntaxPalette(resolvedBase);

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("schemaVersion", kThemeSchemaVersion);
    root->setProperty("name", juce::String(displayName));
    root->setProperty("base", juce::String(resolvedBase));

    juce::DynamicObject::Ptr colours = new juce::DynamicObject();
    for (std::size_t i = 0; i < palette.size(); ++i)
        colours->setProperty(juce::Identifier(colourRoleName(static_cast<ColourRole>(i))),
                             colourToHexString(juce::Colour(palette[i])));
    root->setProperty("colours", juce::var(colours.get()));

    juce::DynamicObject::Ptr syntaxObj = new juce::DynamicObject();
    for (std::size_t i = 0; i < syntax.size(); ++i)
        syntaxObj->setProperty(
            juce::Identifier(syntaxColourRoleName(static_cast<SyntaxColourRole>(i))),
            colourToHexString(juce::Colour(syntax[i])));
    root->setProperty("syntaxColours", juce::var(syntaxObj.get()));

    if (dest.getParentDirectory().createDirectory().failed())
        return false;

    return dest.replaceWithText(juce::JSON::toString(juce::var(root.get())));
}

DarkTheme::SyntaxPalette deriveSyntaxPalette(const DarkTheme::Palette& palette) {
    const auto role = [&palette](ColourRole r) {
        return juce::Colour(palette[static_cast<std::size_t>(r)]);
    };

    const auto editorBackground = role(ColourRole::E0);
    const auto textPrimary = role(ColourRole::TEXT_PRIMARY);
    const auto textSecondary = role(ColourRole::TEXT_SECONDARY);
    const auto textDim = role(ColourRole::TEXT_DIM);
    const auto accentPrimary = role(ColourRole::ACCENT_PRIMARY);
    const auto accentInfo = role(ColourRole::ACCENT_INFO);
    const auto statusBackground = accentInfo;

    DarkTheme::SyntaxPalette syntax{};
    const auto set = [&syntax](SyntaxColourRole r, juce::Colour colour) {
        syntax[static_cast<std::size_t>(r)] = colour.getARGB();
    };

    // Editor chrome tracks the elevation ramp and the text hierarchy.
    set(SyntaxColourRole::EDITOR_BACKGROUND, editorBackground);
    set(SyntaxColourRole::EDITOR_DEFAULT_TEXT, textPrimary);
    set(SyntaxColourRole::LINE_NUMBER_BACKGROUND, role(ColourRole::E1));
    set(SyntaxColourRole::LINE_NUMBER_TEXT, textDim);
    set(SyntaxColourRole::EDITOR_CARET, textPrimary);
    set(SyntaxColourRole::DSL_CARET, role(ColourRole::ACCENT_POSITIVE));

    // The editor selection is drawn over text, so it keeps an alpha; the DSL
    // console fills its selection opaquely and gets the accent mixed into the
    // surface behind it instead.
    set(SyntaxColourRole::EDITOR_SELECTION, accentPrimary.withAlpha(0.3f));
    set(SyntaxColourRole::DSL_SELECTION,
        role(ColourRole::E2).interpolatedWith(accentPrimary, 0.45f).withAlpha(1.0f));
    set(SyntaxColourRole::DSL_STATUS_BACKGROUND, statusBackground);
    set(SyntaxColourRole::DSL_STATUS_TEXT, statusBackground.contrasting(1.0f));

    set(SyntaxColourRole::DSL_OUTPUT_PROMPT, role(ColourRole::ACCENT_POSITIVE));
    set(SyntaxColourRole::DSL_OUTPUT_INFO, accentInfo);
    set(SyntaxColourRole::DSL_OUTPUT_TEXT, textSecondary);
    set(SyntaxColourRole::DSL_OUTPUT_ERROR, role(ColourRole::STATUS_DANGER));

    // Token hues spread across the six accent roles so no two token classes
    // collapse onto the same colour; structural tokens stay on the text ramp.
    set(SyntaxColourRole::DSL_TOKEN_ERROR, role(ColourRole::STATUS_ERROR));
    set(SyntaxColourRole::DSL_TOKEN_COMMENT, textDim);
    set(SyntaxColourRole::DSL_TOKEN_KEYWORD, accentInfo);
    set(SyntaxColourRole::DSL_TOKEN_METHOD, role(ColourRole::ACCENT_MODULATION));
    set(SyntaxColourRole::DSL_TOKEN_PARAM, role(ColourRole::ACCENT_PRIMARY_SOFT));
    set(SyntaxColourRole::DSL_TOKEN_OPERATOR, textSecondary);
    set(SyntaxColourRole::DSL_TOKEN_IDENTIFIER, textPrimary);
    set(SyntaxColourRole::DSL_TOKEN_NUMBER, role(ColourRole::ACCENT_POSITIVE));
    set(SyntaxColourRole::DSL_TOKEN_STRING, role(ColourRole::ACCENT_ATTENTION));
    set(SyntaxColourRole::DSL_TOKEN_BRACKET, textSecondary);
    set(SyntaxColourRole::DSL_TOKEN_PUNCTUATION, textSecondary);
    set(SyntaxColourRole::DSL_TOKEN_NOTE_NAME, accentPrimary);

    set(SyntaxColourRole::CHAT_TOKEN_TEXT, textPrimary);
    set(SyntaxColourRole::CHAT_TOKEN_PLUGIN_ALIAS, accentInfo);
    set(SyntaxColourRole::CHAT_TOKEN_PARAM_ALIAS, role(ColourRole::ACCENT_ATTENTION));
    set(SyntaxColourRole::CHAT_TOKEN_SLASH_COMMAND, role(ColourRole::ACCENT_POSITIVE));
    set(SyntaxColourRole::CHAT_TOKEN_PUNCTUATION, textSecondary);

    return syntax;
}

ThemeApplyResult applyThemeById(const std::string& themeId) {
    ThemeApplyResult result;

    if (ThemeManager::isBuiltInTheme(themeId)) {
        ThemeManager::setActiveBuiltInTheme(themeId);
        result.ok = true;
        return result;
    }

    const auto file = paths::themesDir().getChildFile(juce::String(themeId) + ".json");
    if (auto loaded = loadThemeFile(file)) {
        DarkTheme::setActivePalette(loaded->palette);
        DarkTheme::setActiveSyntaxPalette(loaded->syntaxPalette);
        result.ok = true;
        result.isUserTheme = true;
        result.sourceFile = file;
        result.warnings = std::move(loaded->warnings);
        return result;
    }

    // Factory themes resolve after the user file so a same-id file in the
    // Themes folder overrides the embedded copy (and stays hot-reloadable).
    if (auto factory = loadFactoryTheme(themeId)) {
        DarkTheme::setActivePalette(factory->palette);
        DarkTheme::setActiveSyntaxPalette(factory->syntaxPalette);
        result.ok = true;
        result.warnings = std::move(factory->warnings);
        return result;
    }

    // Unknown id, or the file is missing/unreadable/not an object. Report the
    // candidate user-file path anyway so the caller can keep watching it and
    // recover the moment a valid file appears (built-in ids returned above).
    result.sourceFile = file;
    DarkTheme::resetToDarkPalette();
    return result;
}

std::optional<std::vector<std::string>> reapplyUserThemeFile(const juce::File& file) {
    auto loaded = loadThemeFile(file);
    if (!loaded)
        return std::nullopt;

    DarkTheme::setActivePalette(loaded->palette);
    DarkTheme::setActiveSyntaxPalette(loaded->syntaxPalette);
    return std::move(loaded->warnings);
}

}  // namespace magda
