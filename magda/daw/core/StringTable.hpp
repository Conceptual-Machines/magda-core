#pragma once

#include <juce_core/juce_core.h>

#include <unordered_map>

namespace magda {

/**
 * @brief Centralized string table for UI localization.
 *
 * Loads strings from a flat-namespace JSON file (lang/en.json etc.).
 * Keys use dot notation: "menu.file.new_project", "tracks.mute".
 *
 * Usage:
 *   auto& st = StringTable::getInstance();
 *   button.setText(st.get("tracks.mute"));  // "M"
 *
 * Or via the free function:
 *   button.setText(tr("tracks.mute"));
 */
class StringTable {
  public:
    static StringTable& getInstance();

    StringTable(const StringTable&) = delete;
    StringTable& operator=(const StringTable&) = delete;

    /** Load a JSON language file. Replaces all current strings. */
    bool load(const juce::File& jsonFile);

    /** Load from a JSON string (e.g. embedded BinaryData). */
    bool loadFromString(const juce::String& json);

    /** Look up a string by dotted key. Returns the key itself if not found. */
    juce::String get(const juce::String& key) const;

    /** Get the currently loaded language code (e.g. "en"). */
    juce::String getLanguage() const {
        return language_;
    }

    /**
     * Load a language by code (e.g. "en", "fr").
     * Searches the same candidate directories as the constructor.
     * Returns true if a matching lang/<code>.json was found and loaded.
     */
    bool loadLanguage(const juce::String& languageCode);

    /** Return the first existing lang/ directory searched by the loader. */
    static juce::File findLangDirectory();

    /** Get the number of loaded strings in the active locale map. */
    int size() const {
        return static_cast<int>(localized_.size());
    }

  private:
    StringTable();

    static void parseObject(const juce::var& obj, const juce::String& prefix,
                            std::unordered_map<juce::String, juce::String>& out);

    // `english_` is the master table, loaded from lang/en.json at construction
    // and never modified afterwards — it's the fallback for any key missing in
    // the current locale. `localized_` holds the active non-English locale (or
    // aliases `english_` when the active language is English).
    std::unordered_map<juce::String, juce::String> english_;
    std::unordered_map<juce::String, juce::String> localized_;
    juce::String language_ = "en";
};

/** Shorthand: look up a localized string by key. */
inline juce::String tr(const juce::String& key) {
    return StringTable::getInstance().get(key);
}

/** The ellipsis affordance glyph (U+2026 …). It is presentation, not
 *  translatable data, so it is appended in code rather than stored in the
 *  locale files — this keeps it consistent and at the correct font scale in
 *  every locale (CJK in particular). */
inline juce::String ellipsisGlyph() {
    return juce::String::fromUTF8("\xe2\x80\xa6");
}

/**
 * Look up a localized string and guarantee exactly one trailing ellipsis.
 *
 * Any ellipsis already present in the source or a translation (ASCII "..." or
 * one or more "…" glyphs) is stripped first, so this is idempotent and the
 * locale files can never double up — a translation that still carries "..."
 * gets normalized to a single "…". Use for menu items, dialog-opening buttons,
 * and progress messages.
 */
inline juce::String trEllipsis(const juce::String& key) {
    auto s = tr(key).trimEnd();
    for (;;) {
        if (s.endsWith(ellipsisGlyph()))
            s = s.dropLastCharacters(1).trimEnd();
        else if (s.endsWith("..."))
            s = s.dropLastCharacters(3).trimEnd();
        else
            break;
    }
    return s + ellipsisGlyph();
}

}  // namespace magda
