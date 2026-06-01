#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "DeviceInfo.hpp"
#include "KitRow.hpp"

namespace magda {

// Per-plugin user preferences that travel with the user, not the project.
// Persisted keys must name the real MAGDA device/plugin, not runtime wrapper
// plugins from Tracktion. Use identifierForDevice() to derive those keys from
// DeviceInfo instead of reading pluginId/uniqueId directly at call sites.
class PluginPreferences {
  public:
    class Listener {
      public:
        virtual ~Listener() = default;
        virtual void drumGridPreferenceChanged(const juce::String& pluginIdentifier) {
            juce::ignoreUnused(pluginIdentifier);
        }
    };

    static PluginPreferences& getInstance();

    /** True iff MIDI clips on tracks whose primary instrument has this
     *  preference key should open in the Drum Grid editor by default rather
     *  than the Piano Roll. Returns true for built-in DrumGrid even when
     *  nothing is recorded, so the default UX works without on-disk config. */
    bool prefersDrumGrid(const juce::String& pluginIdentifier) const;

    /** Toggle the drum-grid preference. Writes immediately to disk. */
    void setPrefersDrumGrid(const juce::String& pluginIdentifier, bool prefer);

    /** Canonical key for user-global plugin preferences from the MAGDA model.
     *  External plugins prefer DeviceInfo::uniqueId (JUCE scan identity).
     *  Internal MAGDA devices fall back to DeviceInfo::pluginId ("4osc",
     *  "drumgrid", ...). This keeps call sites out of the legacy naming split. */
    static juce::String identifierForDevice(const DeviceInfo& device);

    void addListener(Listener* listener);
    void removeListener(Listener* listener);

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
    void loadUnlocked();
    void saveUnlocked() const;
    void notifyDrumGridPreferenceChanged(const juce::String& pluginIdentifier);

    mutable std::mutex mutex_;
    std::unordered_set<juce::String> drumGridPlugins_;
    std::unordered_map<juce::String, std::vector<magda::KitRow>> defaultKits_;
    juce::ListenerList<Listener> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginPreferences)
};

}  // namespace magda
