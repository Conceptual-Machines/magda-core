#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <string>
#include <vector>

#include "DarkTheme.hpp"

// User-authored JSON themes (#88). A theme file is a role->colour map layered
// over a built-in base, so users only override the roles they care about:
//
//   {
//     "schemaVersion": 1,
//     "name": "My Theme",
//     "base": "dark",
//     "colours":       { "accentBlue": "#FF6B35", "background": "#101014" },
//     "syntaxColours": { "dslTokenKeyword": "#3AA0FF" }
//   }
//
// Roles absent from the file inherit from the base table. Unknown keys and
// invalid colour strings are non-fatal: they are skipped and reported in
// warnings, so a partly-broken file still renders instead of blanking the UI.

namespace magda {

// Highest schema version this build understands. Files may declare a newer
// version; they still load (forward compatible), with a warning.
inline constexpr int kThemeSchemaVersion = 1;

struct LoadedTheme {
    std::string id;    // stable identifier (source file stem)
    std::string name;  // display name from the file; falls back to id
    std::string base;  // resolved base id ("dark" / "light")
    DarkTheme::Palette palette{};
    DarkTheme::SyntaxPalette syntaxPalette{};
    std::vector<std::string> warnings;  // non-fatal issues, for logging/UI
};

struct ThemeFileEntry {
    std::string id;    // source file stem, used as the persisted theme id
    std::string name;  // display name (falls back to id)
    juce::File file;
};

// Parses a theme file. Returns nullopt only on a hard failure (unreadable, or
// not a JSON object) so the caller can fall back to a built-in. Malformed
// individual fields never fail the load — they inherit the base and warn.
std::optional<LoadedTheme> loadThemeFile(const juce::File& file);

// A theme compiled into the binary (assets/themes). Factory themes sit
// between the built-ins and the user's files in the picker; a user theme
// file with the same id overrides the embedded copy.
struct FactoryThemeEntry {
    std::string id;    // stable identifier (asset file stem, kebab-case)
    std::string name;  // display name from the embedded JSON
};

// The factory themes shipped in BinaryData, sorted by display name.
const std::vector<FactoryThemeEntry>& factoryThemes();

// Loads a factory theme by id. Returns nullopt for unknown ids.
std::optional<LoadedTheme> loadFactoryTheme(const std::string& id);

// Discovers *.json themes in paths::themesDir(), sorted by display name. Only
// lightweight metadata (id, name) is read here; the palette is loaded lazily
// via loadThemeFile when a theme is actually applied.
std::vector<ThemeFileEntry> scanUserThemes();

// Writes the given built-in table out as a complete, valid starter file so a
// user can edit from a full example. Returns false on write failure.
bool writeThemeTemplate(const juce::File& dest, const std::string& baseId,
                        const std::string& displayName);

struct ThemeApplyResult {
    bool ok = false;           // a palette was installed (built-in or user)
    bool isUserTheme = false;  // resolved to a user JSON file (vs a built-in)
    juce::File sourceFile;     // the loaded file, when isUserTheme (for hot-reload)
    std::vector<std::string> warnings;
};

// Installs the palette for `themeId` into DarkTheme's active tables. Built-in
// ids (dark/light/high-contrast) go through ThemeManager; any other id is
// treated as a user theme whose file is `themeId`.json in paths::themesDir().
// On an unknown id or an unreadable/invalid file the active palette resets to
// dark and ok stays false. Does NOT repaint - the caller owns the UI refresh.
// Shared by app startup and the runtime (Config-driven) theme switch.
ThemeApplyResult applyThemeById(const std::string& themeId);

// Reloads a user theme file and installs it without touching Config. Returns
// the load warnings on success, or nullopt if the file could not be parsed (in
// which case the current palette is left untouched). Used by hot-reload so an
// in-progress bad edit never blanks the running UI.
std::optional<std::vector<std::string>> reapplyUserThemeFile(const juce::File& file);

}  // namespace magda
