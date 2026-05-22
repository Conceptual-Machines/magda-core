#pragma once

#include <juce_core/juce_core.h>

#include <unordered_map>

namespace magda {

// Per-plugin user preferences that travel with the user, not the project.
// Keyed by a plugin identifier — the internal pluginId string for built-ins
// (e.g. "drumgrid"), or juce::PluginDescription::createIdentifierString() for
// external VST/AU plugins. Persisted as JSON under dataDir().
class PluginPreferences {
  public:
    enum class PreferredClipEditor {
        Unset,      // no opinion — caller falls back to its own default
        PianoRoll,  // open MIDI clips on this track in the piano roll
        DrumGrid,   // open MIDI clips on this track in the drum grid
    };

    static PluginPreferences& getInstance();

    /** Look up the preferred clip editor for a plugin identifier.
     *  Returns Unset when no preference has been recorded. */
    PreferredClipEditor preferredClipEditorFor(const juce::String& pluginIdentifier) const;

    /** Record a preference. Pass Unset to clear. Writes immediately to disk. */
    void setPreferredClipEditor(const juce::String& pluginIdentifier, PreferredClipEditor editor);

  private:
    PluginPreferences();
    void load();
    void save() const;

    std::unordered_map<juce::String, PreferredClipEditor> preferredEditor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginPreferences)
};

}  // namespace magda
