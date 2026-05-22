#pragma once

#include <juce_core/juce_core.h>

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "KitRow.hpp"

namespace magda {

// Per-plugin user preferences that travel with the user, not the project.
// Keyed by a plugin identifier — the internal pluginId string for built-ins
// (e.g. "drumgrid"), or juce::PluginDescription::createIdentifierString() for
// external VST/AU plugins. Persisted as JSON under dataDir().
class PluginPreferences {
  public:
    static PluginPreferences& getInstance();

    /** True iff MIDI clips on tracks whose primary instrument is this plugin
     *  should open in the Drum Grid editor by default rather than the Piano
     *  Roll. Returns true for built-in DrumGrid even when nothing is recorded,
     *  so the default UX works without on-disk config. */
    bool prefersDrumGrid(const juce::String& pluginIdentifier) const;

    /** Toggle the drum-grid preference. Writes immediately to disk. */
    void setPrefersDrumGrid(const juce::String& pluginIdentifier, bool prefer);

    /** The user-global default drum-kit rows for this plugin. Empty vector if
     *  no default has been recorded. Stamped onto new instances when they're
     *  added to a track. */
    std::vector<magda::KitRow> defaultKitRows(const juce::String& pluginIdentifier) const;

    /** Record the user-global default kit. Called automatically whenever the
     *  user edits the kit on any instance — the most recent edit wins. Pass
     *  an empty vector to clear. */
    void setDefaultKitRows(const juce::String& pluginIdentifier,
                           const std::vector<magda::KitRow>& rows);

  private:
    PluginPreferences();
    void load();
    void save() const;

    std::unordered_set<juce::String> drumGridPlugins_;
    std::unordered_map<juce::String, std::vector<magda::KitRow>> defaultKits_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginPreferences)
};

}  // namespace magda
