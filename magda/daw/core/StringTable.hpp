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

    /** Get the number of loaded strings. */
    int size() const {
        return static_cast<int>(strings_.size());
    }

  private:
    StringTable();

    static void parseObject(const juce::var& obj, const juce::String& prefix,
                            std::unordered_map<juce::String, juce::String>& out);

    std::unordered_map<juce::String, juce::String> strings_;
    juce::String language_ = "en";
};

/** Shorthand: look up a localized string by key. */
inline juce::String tr(const juce::String& key) {
    return StringTable::getInstance().get(key);
}

}  // namespace magda
